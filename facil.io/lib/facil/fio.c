/* *****************************************************************************
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
***************************************************************************** */

#include <fio.h>

#define FIO_INCLUDE_STR
#include <fio.h>

#define FIO_FORCE_MALLOC_TMP 1
#define FIO_INCLUDE_LINKED_LIST
#include <fio.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <poll.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <arpa/inet.h>

/* force poll for testing? */
#ifndef FIO_ENGINE_POLL
#define FIO_ENGINE_POLL 0
#endif

#if !FIO_ENGINE_POLL && !FIO_ENGINE_EPOLL && !FIO_ENGINE_KQUEUE
#if defined(__linux__)
#define FIO_ENGINE_EPOLL 1
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||     \
    defined(__OpenBSD__) || defined(__bsdi__) || defined(__DragonFly__)
#define FIO_ENGINE_KQUEUE 1
#else
#define FIO_ENGINE_POLL 1
#endif
#endif

/* for kqueue and epoll only */
#ifndef FIO_POLL_MAX_EVENTS
#define FIO_POLL_MAX_EVENTS 64
#endif

#ifndef FIO_POLL_TICK
#define FIO_POLL_TICK 1000
#endif

#ifndef FIO_USE_URGENT_QUEUE
#define FIO_USE_URGENT_QUEUE 1
#endif

#ifndef DEBUG_SPINLOCK
#define DEBUG_SPINLOCK 0
#endif

/* Slowloris mitigation  (must be less than 1<<16) */
#ifndef FIO_SLOWLORIS_LIMIT
#define FIO_SLOWLORIS_LIMIT (1 << 10)
#endif

#if !defined(__clang__) && !defined(__GNUC__)
#define __thread _Thread_value
#endif

#ifndef FIO_TLS_WEAK
#define FIO_TLS_WEAK __attribute__((weak))
#endif

/* Mitigates MAP_ANONYMOUS not being defined on older versions of MacOS */
#if !defined(MAP_ANONYMOUS)
#if defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_ANONYMOUS 0
#endif
#endif

/* *****************************************************************************
Event deferring (declarations)
***************************************************************************** */

static void deferred_on_close(void *uuid_, void *pr_);
static void deferred_on_shutdown(void *arg, void *arg2);
static void deferred_on_ready(void *arg, void *arg2);
static void deferred_on_data(void *uuid, void *arg2);
static void deferred_ping(void *arg, void *arg2);

/* *****************************************************************************
Section Start Marker











                       Main State Machine Data Structures












***************************************************************************** */

typedef void (*fio_uuid_link_fn)(void *);
#define FIO_SET_NAME fio_uuid_links
#define FIO_SET_OBJ_TYPE fio_uuid_link_fn
#define FIO_SET_OBJ_COMPARE(o1, o2) 1
#include <fio.h>

/** User-space socket buffer data */
typedef struct fio_packet_s fio_packet_s;
struct fio_packet_s {
  fio_packet_s *next;
  int (*write_func)(int fd, struct fio_packet_s *packet);
  void (*dealloc)(void *buffer);
  union {
    void *buffer;
    intptr_t fd;
  } data;
  uintptr_t offset;
  uintptr_t length;
};

/** Connection data (fd_data) */
typedef struct {
  /* current data to be send */
  fio_packet_s *packet;
  /** the last packet in the queue. */
  fio_packet_s **packet_last;
  /* Data sent so far */
  size_t sent;
  /* fd protocol */
  fio_protocol_s *protocol;
  /* timer handler */
  time_t active;
  /** The number of pending packets that are in the queue. */
  uint16_t packet_count;
  /* timeout settings */
  uint8_t timeout;
  /* indicates that the fd should be considered scheduled (added to poll) */
  fio_lock_i scheduled;
  /* protocol lock */
  fio_lock_i protocol_lock;
  /* used to convert `fd` to `uuid` and validate connections */
  uint8_t counter;
  /* socket lock */
  fio_lock_i sock_lock;
  /** Connection is open */
  uint8_t open;
  /** indicated that the connection should be closed. */
  uint8_t close;
  /** peer address length */
  uint8_t addr_len;
  /** peer address length */
  uint8_t addr[48];
  /** RW hooks. */
  fio_rw_hook_s *rw_hooks;
  /** RW udata. */
  void *rw_udata;
  /* Objects linked to the UUID */
  fio_uuid_links_s links;
} fio_fd_data_s;

typedef struct {
  struct timespec last_cycle;
  /* connection capacity */
  uint32_t capa;
  /* connections counted towards shutdown (NOT while running) */
  uint32_t connection_count;
  /* thread list */
  fio_ls_s thread_ids;
  /* active workers */
  uint16_t workers;
  /* timer handler */
  uint16_t threads;
  /* timeout review loop flag */
  uint8_t need_review;
  /* spinning down process */
  uint8_t volatile active;
  /* worker process flag - true also for single process */
  uint8_t is_worker;
  /* polling and global lock */
  fio_lock_i lock;
  /* The highest active fd with a protocol object */
  uint32_t max_protocol_fd;
  /* timer handler */
  pid_t parent;
#if FIO_ENGINE_POLL
  struct pollfd *poll;
#endif
  fio_fd_data_s info[];
} fio_data_s;

/** The logging level */
#if DEBUG
int FIO_LOG_LEVEL = FIO_LOG_LEVEL_DEBUG;
#else
int FIO_LOG_LEVEL = FIO_LOG_LEVEL_INFO;
#endif
static fio_data_s *fio_data = NULL;

/* used for protocol locking by task type. */
typedef struct {
  fio_lock_i locks[3];
  unsigned rsv : 8;
} protocol_metadata_s;

/* used for accessing the protocol locking in a safe byte aligned way. */
union protocol_metadata_union_u {
  size_t opaque;
  protocol_metadata_s meta;
};

#define fd_data(fd) (fio_data->info[(uintptr_t)(fd)])
#define uuid_data(uuid) fd_data(fio_uuid2fd((uuid)))
#define fd2uuid(fd)                                                            \
  ((intptr_t)((((uintptr_t)(fd)) << 8) | fd_data((fd)).counter))

/**
 * Returns the maximum number of open files facil.io can handle per worker
 * process.
 *
 * Total OS limits might apply as well but aren't shown.
 *
 * The value of 0 indicates either that the facil.io library wasn't initialized
 * yet or that it's resources were released.
 */
size_t fio_capa(void) {
  if (fio_data)
    return fio_data->capa;
  return 0;
}

/* *****************************************************************************
Packet allocation (for socket's user-buffer)
***************************************************************************** */

static inline void fio_packet_free(fio_packet_s *packet) {
  packet->dealloc(packet->data.buffer);
  fio_free(packet);
}
static inline fio_packet_s *fio_packet_alloc(void) {
  fio_packet_s *packet = fio_malloc(sizeof(*packet));
  FIO_ASSERT_ALLOC(packet);
  return packet;
}

/* *****************************************************************************
Core Connection Data Clearing
***************************************************************************** */

/* set the minimal max_protocol_fd */
static void fio_max_fd_min(uint32_t fd) {
  if (fio_data->max_protocol_fd > fd)
    return;
  fio_lock(&fio_data->lock);
  if (fio_data->max_protocol_fd < fd)
    fio_data->max_protocol_fd = fd;
  fio_unlock(&fio_data->lock);
}

/* set the minimal max_protocol_fd */
static void fio_max_fd_shrink(void) {
  fio_lock(&fio_data->lock);
  uint32_t fd = fio_data->max_protocol_fd;
  while (fd && fd_data(fd).protocol == NULL)
    --fd;
  fio_data->max_protocol_fd = fd;
  fio_unlock(&fio_data->lock);
}

/* resets connection data, marking it as either open or closed. */
static inline int fio_clear_fd(intptr_t fd, uint8_t is_open) {
  fio_packet_s *packet;
  fio_protocol_s *protocol;
  fio_rw_hook_s *rw_hooks;
  void *rw_udata;
  fio_uuid_links_s links;
  fio_lock(&(fd_data(fd).sock_lock));
  links = fd_data(fd).links;
  packet = fd_data(fd).packet;
  protocol = fd_data(fd).protocol;
  rw_hooks = fd_data(fd).rw_hooks;
  rw_udata = fd_data(fd).rw_udata;
  fd_data(fd) = (fio_fd_data_s){
      .open = is_open,
      .sock_lock = fd_data(fd).sock_lock,
      .protocol_lock = fd_data(fd).protocol_lock,
      .rw_hooks = (fio_rw_hook_s *)&FIO_DEFAULT_RW_HOOKS,
      .counter = fd_data(fd).counter + 1,
      .packet_last = &fd_data(fd).packet,
  };
  fio_unlock(&(fd_data(fd).sock_lock));
  if (rw_hooks && rw_hooks->cleanup)
    rw_hooks->cleanup(rw_udata);
  while (packet) {
    fio_packet_s *tmp = packet;
    packet = packet->next;
    fio_packet_free(tmp);
  }
  if (fio_uuid_links_count(&links)) {
    FIO_SET_FOR_LOOP(&links, pos) {
      if (pos->hash)
        pos->obj((void *)pos->hash);
    }
  }
  fio_uuid_links_free(&links);
  if (protocol && protocol->on_close) {
    fio_defer(deferred_on_close, (void *)fd2uuid(fd), protocol);
  }
  if (is_open)
    fio_max_fd_min(fd);
  return 0;
}

static inline void fio_force_close_in_poll(intptr_t uuid) {
  uuid_data(uuid).close = 2;
  fio_force_close(uuid);
}

/* *****************************************************************************
Protocol Locking and UUID validation
***************************************************************************** */

/* Macro for accessing the protocol locking / metadata. */
#define prt_meta(prt) (((union protocol_metadata_union_u *)(&(prt)->rsv))->meta)

/** locks a connection's protocol returns a pointer that need to be unlocked. */
inline static fio_protocol_s *protocol_try_lock(intptr_t fd,
                                                enum fio_protocol_lock_e type) {
  errno = 0;
  if (fio_trylock(&fd_data(fd).protocol_lock))
    goto would_block;
  fio_protocol_s *pr = fd_data(fd).protocol;
  if (!pr) {
    fio_unlock(&fd_data(fd).protocol_lock);
    goto invalid;
  }
  if (fio_trylock(&prt_meta(pr).locks[type])) {
    fio_unlock(&fd_data(fd).protocol_lock);
    goto would_block;
  }
  fio_unlock(&fd_data(fd).protocol_lock);
  return pr;
would_block:
  errno = EWOULDBLOCK;
  return NULL;
invalid:
  errno = EBADF;
  return NULL;
}
/** See `fio_protocol_try_lock` for details. */
inline static void protocol_unlock(fio_protocol_s *pr,
                                   enum fio_protocol_lock_e type) {
  fio_unlock(&prt_meta(pr).locks[type]);
}

/** returns 1 if the UUID is valid and 0 if it isn't. */
#define uuid_is_valid(uuid)                                                    \
  ((intptr_t)(uuid) >= 0 &&                                                    \
   ((uint32_t)fio_uuid2fd((uuid))) < fio_data->capa &&                         \
   ((uintptr_t)(uuid)&0xFF) == uuid_data((uuid)).counter)

/* public API. */
fio_protocol_s *fio_protocol_try_lock(intptr_t uuid,
                                      enum fio_protocol_lock_e type) {
  if (!uuid_is_valid(uuid)) {
    errno = EBADF;
    return NULL;
  }
  return protocol_try_lock(fio_uuid2fd(uuid), type);
}

/* public API. */
void fio_protocol_unlock(fio_protocol_s *pr, enum fio_protocol_lock_e type) {
  protocol_unlock(pr, type);
}

/* *****************************************************************************
UUID validation and state
***************************************************************************** */

/* public API. */
intptr_t fio_fd2uuid(int fd) {
  if (fd < 0 || (size_t)fd >= fio_data->capa)
    return -1;
  if (!fd_data(fd).open) {
    fio_lock(&fd_data(fd).protocol_lock);
    fio_clear_fd(fd, 1);
    fio_unlock(&fd_data(fd).protocol_lock);
  }
  return fd2uuid(fd);
}

/* public API. */
int fio_is_valid(intptr_t uuid) { return uuid_is_valid(uuid); }

/* public API. */
int fio_is_closed(intptr_t uuid) {
  return !uuid_is_valid(uuid) || !uuid_data(uuid).open || uuid_data(uuid).close;
}

void fio_stop(void) {
  if (fio_data)
    fio_data->active = 0;
}

/* public API. */
int16_t fio_is_running(void) { return fio_data && fio_data->active; }

/* public API. */
struct timespec fio_last_tick(void) {
  return fio_data->last_cycle;
}

#define touchfd(fd) fd_data((fd)).active = fio_data->last_cycle.tv_sec

/* public API. */
void fio_touch(intptr_t uuid) {
  if (uuid_is_valid(uuid))
    touchfd(fio_uuid2fd(uuid));
}

/* public API. */
fio_str_info_s fio_peer_addr(intptr_t uuid) {
  if (fio_is_closed(uuid) || !uuid_data(uuid).addr_len)
    return (fio_str_info_s){.data = NULL, .len = 0, .capa = 0};
  return (fio_str_info_s){.data = (char *)uuid_data(uuid).addr,
                          .len = uuid_data(uuid).addr_len,
                          .capa = 0};
}

/**
 * Writes the local machine address (qualified host name) to the buffer.
 *
 * Returns the amount of data written (excluding the NUL byte).
 *
 * `limit` is the maximum number of bytes in the buffer, including the NUL byte.
 *
 * If the returned value == limit - 1, the result might have been truncated.
 *
 * If 0 is returned, an erro might have occured (see `errno`) and the contents
 * of `dest` is undefined.
 */
size_t fio_local_addr(char *dest, size_t limit) {
  if (gethostname(dest, limit))
    return 0;

  struct addrinfo hints, *info;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_CANONNAME;   // get cannonical name

  if (getaddrinfo(dest, "http", &hints, &info) != 0)
    return 0;

  for (struct addrinfo *pos = info; pos; pos = pos->ai_next) {
    if (pos->ai_canonname) {
      size_t len = strlen(pos->ai_canonname);
      if (len >= limit)
        len = limit - 1;
      memcpy(dest, pos->ai_canonname, len);
      dest[len] = 0;
      freeaddrinfo(info);
      return len;
    }
  }

  freeaddrinfo(info);
  return 0;
}

/* *****************************************************************************
UUID attachments (linking objects to the UUID's lifetime)
***************************************************************************** */

/* public API. */
void fio_uuid_link(intptr_t uuid, void *obj, void (*on_close)(void *obj)) {
  if (!uuid_is_valid(uuid))
    goto invalid;
  fio_lock(&uuid_data(uuid).sock_lock);
  if (!uuid_is_valid(uuid))
    goto locked_invalid;
  fio_uuid_links_overwrite(&uuid_data(uuid).links, (uintptr_t)obj, on_close,
                           NULL);
  fio_unlock(&uuid_data(uuid).sock_lock);
  return;
locked_invalid:
  fio_unlock(&uuid_data(uuid).sock_lock);
invalid:
  errno = EBADF;
  on_close(obj);
}

/* public API. */
int fio_uuid_unlink(intptr_t uuid, void *obj) {
  if (!uuid_is_valid(uuid))
    goto invalid;
  fio_lock(&uuid_data(uuid).sock_lock);
  if (!uuid_is_valid(uuid))
    goto locked_invalid;
  /* default object comparison is always true */
  int ret =
      fio_uuid_links_remove(&uuid_data(uuid).links, (uintptr_t)obj, NULL, NULL);
  if (ret)
    errno = ENOTCONN;
  fio_unlock(&uuid_data(uuid).sock_lock);
  return ret;
locked_invalid:
  fio_unlock(&uuid_data(uuid).sock_lock);
invalid:
  errno = EBADF;
  return -1;
}

/* *****************************************************************************
Section Start Marker











                         Default Thread / Fork handler

                           And Concurrency Helpers












***************************************************************************** */

/**
OVERRIDE THIS to replace the default `fork` implementation.

Behaves like the system's `fork`.
*/
#pragma weak fio_fork
int __attribute__((weak)) fio_fork(void) { return fork(); }

/**
 * OVERRIDE THIS to replace the default pthread implementation.
 *
 * Accepts a pointer to a function and a single argument that should be executed
 * within a new thread.
 *
 * The function should allocate memory for the thread object and return a
 * pointer to the allocated memory that identifies the thread.
 *
 * On error NULL should be returned.
 */
#pragma weak fio_thread_new
void *__attribute__((weak))
fio_thread_new(void *(*thread_func)(void *), void *arg) {
  pthread_t *thread = malloc(sizeof(*thread));
  FIO_ASSERT_ALLOC(thread);
  if (pthread_create(thread, NULL, thread_func, arg))
    goto error;
  return thread;
error:
  free(thread);
  return NULL;
}

/**
 * OVERRIDE THIS to replace the default pthread implementation.
 *
 * Frees the memory associated with a thread identifier (allows the thread to
 * run it's course, just the identifier is freed).
 */
#pragma weak fio_thread_free
void __attribute__((weak)) fio_thread_free(void *p_thr) {
  if (*((pthread_t *)p_thr)) {
    pthread_detach(*((pthread_t *)p_thr));
  }
  free(p_thr);
}

/**
 * OVERRIDE THIS to replace the default pthread implementation.
 *
 * Accepts a pointer returned from `fio_thread_new` (should also free any
 * allocated memory) and joins the associated thread.
 *
 * Return value is ignored.
 */
#pragma weak fio_thread_join
int __attribute__((weak)) fio_thread_join(void *p_thr) {
  if (!p_thr || !(*((pthread_t *)p_thr)))
    return -1;
  pthread_join(*((pthread_t *)p_thr), NULL);
  *((pthread_t *)p_thr) = (pthread_t)NULL;
  free(p_thr);
  return 0;
}

/* *****************************************************************************
Suspending and renewing thread execution (signaling events)
***************************************************************************** */

#ifndef DEFER_THROTTLE
#define DEFER_THROTTLE 2097148UL
#endif
#ifndef FIO_DEFER_THROTTLE_LIMIT
#define FIO_DEFER_THROTTLE_LIMIT 134217472UL
#endif

/**
 * The polling throttling model will use pipes to suspend and resume threads...
 *
 * However, it seems the approach is currently broken, at least on macOS.
 * I don't know why.
 *
 * If polling is disabled, the progressive throttling model will be used.
 *
 * The progressive throttling makes concurrency and parallelism likely, but uses
 * progressive nano-sleep throttling system that is less exact.
 */
#ifndef FIO_DEFER_THROTTLE_POLL
#define FIO_DEFER_THROTTLE_POLL 0
#endif

typedef struct fio_thread_queue_s {
  fio_ls_embd_s node;
  int fd_wait;   /* used for weaiting (read signal) */
  int fd_signal; /* used for signalling (write) */
} fio_thread_queue_s;

fio_ls_embd_s fio_thread_queue = FIO_LS_INIT(fio_thread_queue);
fio_lock_i fio_thread_lock = FIO_LOCK_INIT;
static __thread fio_thread_queue_s fio_thread_data = {.fd_wait = -1,
                                                      .fd_signal = -1};

FIO_FUNC inline void fio_thread_make_suspendable(void) {
  if (fio_thread_data.fd_signal >= 0)
    return;
  int fd[2] = {0, 0};
  int ret = pipe(fd);
  FIO_ASSERT(ret == 0, "`pipe` failed.");
  FIO_ASSERT(fio_set_non_block(fd[0]) == 0,
             "(fio) couldn't set internal pipe to non-blocking mode.");
  FIO_ASSERT(fio_set_non_block(fd[1]) == 0,
             "(fio) couldn't set internal pipe to non-blocking mode.");
  fio_thread_data.fd_wait = fd[0];
  fio_thread_data.fd_signal = fd[1];
}

FIO_FUNC inline void fio_thread_cleanup(void) {
  if (fio_thread_data.fd_signal < 0)
    return;
  close(fio_thread_data.fd_wait);
  close(fio_thread_data.fd_signal);
  fio_thread_data.fd_wait = -1;
  fio_thread_data.fd_signal = -1;
}

/* suspend thread execution (might be resumed unexpectedly) */
FIO_FUNC void fio_thread_suspend(void) {
  fio_lock(&fio_thread_lock);
  fio_ls_embd_push(&fio_thread_queue, &fio_thread_data.node);
  fio_unlock(&fio_thread_lock);
  struct pollfd list = {
      .events = (POLLPRI | POLLIN),
      .fd = fio_thread_data.fd_wait,
  };
  if (poll(&list, 1, 5000) > 0) {
    /* thread was removed from the list through signal */
    uint64_t data;
    int r = read(fio_thread_data.fd_wait, &data, sizeof(data));
    (void)r;
  } else {
    /* remove self from list */
    fio_lock(&fio_thread_lock);
    fio_ls_embd_remove(&fio_thread_data.node);
    fio_unlock(&fio_thread_lock);
  }
}

/* wake up a single thread */
FIO_FUNC void fio_thread_signal(void) {
  fio_thread_queue_s *t;
  int fd = -2;
  fio_lock(&fio_thread_lock);
  t = (fio_thread_queue_s *)fio_ls_embd_shift(&fio_thread_queue);
  if (t)
    fd = t->fd_signal;
  fio_unlock(&fio_thread_lock);
  if (fd >= 0) {
    uint64_t data = 1;
    int r = write(fd, (void *)&data, sizeof(data));
    (void)r;
  } else if (fd == -1) {
    /* hardly the best way, but there's a thread sleeping on air */
    kill(getpid(), SIGCONT);
  }
}

/* wake up all threads */
FIO_FUNC void fio_thread_broadcast(void) {
  while (fio_ls_embd_any(&fio_thread_queue)) {
    fio_thread_signal();
  }
}

static size_t fio_poll(void);
/**
 * A thread entering this function should wait for new evennts.
 */
static void fio_defer_thread_wait(void) {
#if FIO_ENGINE_POLL
  fio_poll();
  return;
#endif
  if (FIO_DEFER_THROTTLE_POLL) {
    fio_thread_suspend();
  } else {
    /* keeps threads active (concurrent), but reduces performance */
    static __thread size_t static_throttle = 262143UL;
    fio_throttle_thread(static_throttle);
    if (fio_defer_has_queue())
      static_throttle = 1;
    else if (static_throttle < FIO_DEFER_THROTTLE_LIMIT)
      static_throttle = (static_throttle << 1);
  }
}

static inline void fio_defer_on_thread_start(void) {
  if (FIO_DEFER_THROTTLE_POLL)
    fio_thread_make_suspendable();
}
static inline void fio_defer_thread_signal(void) {
  if (FIO_DEFER_THROTTLE_POLL)
    fio_thread_signal();
}
static inline void fio_defer_on_thread_end(void) {
  if (FIO_DEFER_THROTTLE_POLL) {
    fio_thread_broadcast();
    fio_thread_cleanup();
  }
}

/* *****************************************************************************
Section Start Marker














                             Task Management

                  Task / Event schduling and execution















***************************************************************************** */

#ifndef DEFER_QUEUE_BLOCK_COUNT
#if UINTPTR_MAX <= 0xFFFFFFFF
/* Almost a page of memory on most 32 bit machines: ((4096/4)-8)/3 */
#define DEFER_QUEUE_BLOCK_COUNT 338
#else
/* Almost a page of memory on most 64 bit machines: ((4096/8)-8)/3 */
#define DEFER_QUEUE_BLOCK_COUNT 168
#endif
#endif

/* task node data */
typedef struct {
  void (*func)(void *, void *);
  void *arg1;
  void *arg2;
} fio_defer_task_s;

/* task queue block */
typedef struct fio_defer_queue_block_s fio_defer_queue_block_s;
struct fio_defer_queue_block_s {
  fio_defer_task_s tasks[DEFER_QUEUE_BLOCK_COUNT];
  fio_defer_queue_block_s *next;
  size_t write;
  size_t read;
  unsigned char state;
};

/* task queue object */
typedef struct { /* a lock for the state machine, used for multi-threading
                    support */
  fio_lock_i lock;
  /* current active block to pop tasks */
  fio_defer_queue_block_s *reader;
  /* current active block to push tasks */
  fio_defer_queue_block_s *writer;
  /* static, built-in, queue */
  fio_defer_queue_block_s static_queue;
} fio_task_queue_s;

/* the state machine - this holds all the data about the task queue and pool */
static fio_task_queue_s task_queue_normal = {
    .reader = &task_queue_normal.static_queue,
    .writer = &task_queue_normal.static_queue};

static fio_task_queue_s task_queue_urgent = {
    .reader = &task_queue_urgent.static_queue,
    .writer = &task_queue_urgent.static_queue};

/* *****************************************************************************
Internal Task API
***************************************************************************** */

#if DEBUG
static size_t fio_defer_count_alloc, fio_defer_count_dealloc;
#define COUNT_ALLOC fio_atomic_add(&fio_defer_count_alloc, 1)
#define COUNT_DEALLOC fio_atomic_add(&fio_defer_count_dealloc, 1)
#define COUNT_RESET                                                            \
  do {                                                                         \
    fio_defer_count_alloc = fio_defer_count_dealloc = 0;                       \
  } while (0)
#else
#define COUNT_ALLOC
#define COUNT_DEALLOC
#define COUNT_RESET
#endif

static inline void fio_defer_push_task_fn(fio_defer_task_s task,
                                          fio_task_queue_s *queue) {
  fio_lock(&queue->lock);

  /* test if full */
  if (queue->writer->state && queue->writer->write == queue->writer->read) {
    /* return to static buffer or allocate new buffer */
    if (queue->static_queue.state == 2) {
      queue->writer->next = &queue->static_queue;
    } else {
      queue->writer->next = fio_malloc(sizeof(*queue->writer->next));
      COUNT_ALLOC;
      if (!queue->writer->next)
        goto critical_error;
    }
    queue->writer = queue->writer->next;
    queue->writer->write = 0;
    queue->writer->read = 0;
    queue->writer->state = 0;
    queue->writer->next = NULL;
  }

  /* place task and finish */
  queue->writer->tasks[queue->writer->write++] = task;
  /* cycle buffer */
  if (queue->writer->write == DEFER_QUEUE_BLOCK_COUNT) {
    queue->writer->write = 0;
    queue->writer->state = 1;
  }
  fio_unlock(&queue->lock);
  return;

critical_error:
  fio_unlock(&queue->lock);
  FIO_ASSERT_ALLOC(NULL)
}

#define fio_defer_push_task(func_, arg1_, arg2_)                               \
  do {                                                                         \
    fio_defer_push_task_fn(                                                    \
        (fio_defer_task_s){.func = func_, .arg1 = arg1_, .arg2 = arg2_},       \
        &task_queue_normal);                                                   \
    fio_defer_thread_signal();                                                 \
  } while (0)

#if FIO_USE_URGENT_QUEUE
#define fio_defer_push_urgent(func_, arg1_, arg2_)                             \
  fio_defer_push_task_fn(                                                      \
      (fio_defer_task_s){.func = func_, .arg1 = arg1_, .arg2 = arg2_},         \
      &task_queue_urgent)
#else
#define fio_defer_push_urgent(func_, arg1_, arg2_)                             \
  fio_defer_push_task(func_, arg1_, arg2_)
#endif

static inline fio_defer_task_s fio_defer_pop_task(fio_task_queue_s *queue) {
  fio_defer_task_s ret = (fio_defer_task_s){.func = NULL};
  fio_defer_queue_block_s *to_free = NULL;
  /* lock the state machine, grab/create a task and place it at the tail */
  fio_lock(&queue->lock);

  /* empty? */
  if (queue->reader->write == queue->reader->read && !queue->reader->state)
    goto finish;
  /* collect task */
  ret = queue->reader->tasks[queue->reader->read++];
  /* cycle */
  if (queue->reader->read == DEFER_QUEUE_BLOCK_COUNT) {
    queue->reader->read = 0;
    queue->reader->state = 0;
  }
  /* did we finish the queue in the buffer? */
  if (queue->reader->write == queue->reader->read) {
    if (queue->reader->next) {
      to_free = queue->reader;
      queue->reader = queue->reader->next;
    } else {
      if (queue->reader != &queue->static_queue &&
          queue->static_queue.state == 2) {
        to_free = queue->reader;
        queue->writer = &queue->static_queue;
        queue->reader = &queue->static_queue;
      }
      queue->reader->write = queue->reader->read = queue->reader->state = 0;
    }
  }

finish:
  if (to_free == &queue->static_queue) {
    queue->static_queue.state = 2;
    queue->static_queue.next = NULL;
  }
  fio_unlock(&queue->lock);

  if (to_free && to_free != &queue->static_queue) {
    fio_free(to_free);
    COUNT_DEALLOC;
  }
  return ret;
}

/* same as fio_defer_clear_queue , just inlined */
static inline void fio_defer_clear_tasks_for_queue(fio_task_queue_s *queue) {
  fio_lock(&queue->lock);
  while (queue->reader) {
    fio_defer_queue_block_s *tmp = queue->reader;
    queue->reader = queue->reader->next;
    if (tmp != &queue->static_queue) {
      COUNT_DEALLOC;
      free(tmp);
    }
  }
  queue->static_queue = (fio_defer_queue_block_s){.next = NULL};
  queue->reader = queue->writer = &queue->static_queue;
  fio_unlock(&queue->lock);
}

/**
 * Performs a single task from the queue, returning -1 if the queue was empty.
 */
static inline int
fio_defer_perform_single_task_for_queue(fio_task_queue_s *queue) {
  fio_defer_task_s task = fio_defer_pop_task(queue);
  if (!task.func)
    return -1;
  task.func(task.arg1, task.arg2);
  return 0;
}

static inline void fio_defer_clear_tasks(void) {
  fio_defer_clear_tasks_for_queue(&task_queue_normal);
#if FIO_USE_URGENT_QUEUE
  fio_defer_clear_tasks_for_queue(&task_queue_urgent);
#endif
}

static void fio_defer_on_fork(void) {
  task_queue_normal.lock = FIO_LOCK_INIT;
#if FIO_USE_URGENT_QUEUE
  task_queue_urgent.lock = FIO_LOCK_INIT;
#endif
}

/* *****************************************************************************
External Task API
***************************************************************************** */

/** Defer an execution of a function for later. */
int fio_defer(void (*func)(void *, void *), void *arg1, void *arg2) {
  /* must have a task to defer */
  if (!func)
    goto call_error;
  fio_defer_push_task(func, arg1, arg2);
  return 0;

call_error:
  return -1;
}

/** Performs all deferred functions until the queue had been depleted. */
void fio_defer_perform(void) {
#if FIO_USE_URGENT_QUEUE
  while (fio_defer_perform_single_task_for_queue(&task_queue_urgent) == 0 ||
         fio_defer_perform_single_task_for_queue(&task_queue_normal) == 0)
    ;
#else
  while (fio_defer_perform_single_task_for_queue(&task_queue_normal) == 0)
    ;
#endif
  //   for (;;) {
  // #if FIO_USE_URGENT_QUEUE
  //     fio_defer_task_s task = fio_defer_pop_task(&task_queue_urgent);
  //     if (!task.func)
  //       task = fio_defer_pop_task(&task_queue_normal);
  // #else
  //     fio_defer_task_s task = fio_defer_pop_task(&task_queue_normal);
  // #endif
  //     if (!task.func)
  //       return;
  //     task.func(task.arg1, task.arg2);
  //   }
}

/** Returns true if there are deferred functions waiting for execution. */
int fio_defer_has_queue(void) {
#if FIO_USE_URGENT_QUEUE
  return task_queue_urgent.reader != task_queue_urgent.writer ||
         task_queue_urgent.reader->write != task_queue_urgent.reader->read ||
         task_queue_normal.reader != task_queue_normal.writer ||
         task_queue_normal.reader->write != task_queue_normal.reader->read;
#else
  return task_queue_normal.reader != task_queue_normal.writer ||
         task_queue_normal.reader->write != task_queue_normal.reader->read;
#endif
}

/** Clears the queue. */
void fio_defer_clear_queue(void) { fio_defer_clear_tasks(); }

/* Thread pool task */
static void *fio_defer_cycle(void *ignr) {
  fio_defer_on_thread_start();
  for (;;) {
    fio_defer_perform();
    if (!fio_is_running())
      break;
    fio_defer_thread_wait();
  }
  fio_defer_on_thread_end();
  return ignr;
}

/* thread pool type */
typedef struct {
  size_t thread_count;
  void *threads[];
} fio_defer_thread_pool_s;

/* joins a thread pool */
static void fio_defer_thread_pool_join(fio_defer_thread_pool_s *pool) {
  for (size_t i = 0; i < pool->thread_count; ++i) {
    fio_thread_join(pool->threads[i]);
  }
  free(pool);
}

/* creates a thread pool */
static fio_defer_thread_pool_s *fio_defer_thread_pool_new(size_t count) {
  if (!count)
    count = 1;
  fio_defer_thread_pool_s *pool =
      malloc(sizeof(*pool) + (count * sizeof(void *)));
  FIO_ASSERT_ALLOC(pool);
  pool->thread_count = count;
  for (size_t i = 0; i < count; ++i) {
    pool->threads[i] = fio_thread_new(fio_defer_cycle, NULL);
    if (!pool->threads[i]) {
      pool->thread_count = i;
      goto error;
    }
  }
  return pool;
error:
  FIO_LOG_FATAL("couldn't spawn threads for thread pool, attempting shutdown.");
  fio_stop();
  fio_defer_thread_pool_join(pool);
  return NULL;
}

/* *****************************************************************************
Section Start Marker









                                     Timers










***************************************************************************** */

typedef struct {
  fio_ls_embd_s node;
  struct timespec due;
  size_t interval; /*in ms */
  size_t repetitions;
  void (*task)(void *);
  void *arg;
  void (*on_finish)(void *);
} fio_timer_s;

static fio_ls_embd_s fio_timers = FIO_LS_INIT(fio_timers);

static fio_lock_i fio_timer_lock = FIO_LOCK_INIT;

/** Marks the current time as facil.io's cycle time */
static inline void fio_mark_time(void) {
  clock_gettime(CLOCK_REALTIME, &fio_data->last_cycle);
}

/** Calculates the due time for a task, given it's interval */
static struct timespec fio_timer_calc_due(size_t interval) {
  struct timespec now = fio_last_tick();
  if (interval >= 1000) {
    unsigned long long secs = interval / 1000;
    now.tv_sec += secs;
    interval -= secs * 1000;
  }
  now.tv_nsec += (interval * 1000000UL);
  if (now.tv_nsec >= 1000000000L) {
    now.tv_nsec -= 1000000000L;
    now.tv_sec += 1;
  }
  return now;
}

/** Returns the number of miliseconds until the next event, up to FIO_POLL_TICK
 */
static size_t fio_timer_calc_first_interval(void) {
  if (fio_defer_has_queue())
    return 0;
  if (fio_ls_embd_is_empty(&fio_timers)) {
    return FIO_POLL_TICK;
  }
  struct timespec now = fio_last_tick();
  struct timespec due =
      FIO_LS_EMBD_OBJ(fio_timer_s, node, fio_timers.next)->due;
  if (due.tv_sec < now.tv_sec ||
      (due.tv_sec == now.tv_sec && due.tv_nsec <= now.tv_nsec))
    return 0;
  size_t interval = 1000L * (due.tv_sec - now.tv_sec);
  if (due.tv_nsec >= now.tv_nsec) {
    interval += (due.tv_nsec - now.tv_nsec) / 1000000L;
  } else {
    interval -= (now.tv_nsec - due.tv_nsec) / 1000000L;
  }
  if (interval > FIO_POLL_TICK)
    interval = FIO_POLL_TICK;
  return interval;
}

/* simple a<=>b if "a" is bigger a negative result is returned, eq == 0. */
static int fio_timer_compare(struct timespec a, struct timespec b) {
  if (a.tv_sec == b.tv_sec) {
    if (a.tv_nsec < b.tv_nsec)
      return 1;
    if (a.tv_nsec > b.tv_nsec)
      return -1;
    return 0;
  }
  if (a.tv_sec < b.tv_sec)
    return 1;
  return -1;
}

/** Places a timer in an ordered linked list. */
static void fio_timer_add_order(fio_timer_s *timer) {
  timer->due = fio_timer_calc_due(timer->interval);
  // fio_ls_embd_s *pos = &fio_timers;
  fio_lock(&fio_timer_lock);
  FIO_LS_EMBD_FOR(&fio_timers, node) {
    fio_timer_s *t2 = FIO_LS_EMBD_OBJ(fio_timer_s, node, node);
    if (fio_timer_compare(timer->due, t2->due) >= 0) {
      fio_ls_embd_push(node, &timer->node);
      goto finish;
    }
  }
  fio_ls_embd_push(&fio_timers, &timer->node);
finish:
  fio_unlock(&fio_timer_lock);
}

/** Performs a timer task and re-adds it to the queue (or cleans it up) */
static void fio_timer_perform_single(void *timer_, void *ignr) {
  fio_timer_s *timer = timer_;
  timer->task(timer->arg);
  if (!timer->repetitions || fio_atomic_sub(&timer->repetitions, 1))
    goto reschedule;
  if (timer->on_finish)
    timer->on_finish(timer->arg);
  free(timer);
  return;
  (void)ignr;
reschedule:
  fio_timer_add_order(timer);
}

/** schedules all timers that are due to be performed. */
static void fio_timer_schedule(void) {
  struct timespec now = fio_last_tick();
  fio_lock(&fio_timer_lock);
  while (fio_ls_embd_any(&fio_timers) &&
         fio_timer_compare(
             FIO_LS_EMBD_OBJ(fio_timer_s, node, fio_timers.next)->due, now) >=
             0) {
    fio_ls_embd_s *tmp = fio_ls_embd_remove(fio_timers.next);
    fio_defer(fio_timer_perform_single, FIO_LS_EMBD_OBJ(fio_timer_s, node, tmp),
              NULL);
  }
  fio_unlock(&fio_timer_lock);
}

static void fio_timer_clear_all(void) {
  fio_lock(&fio_timer_lock);
  while (fio_ls_embd_any(&fio_timers)) {
    fio_timer_s *timer =
        FIO_LS_EMBD_OBJ(fio_timer_s, node, fio_ls_embd_pop(&fio_timers));
    if (timer->on_finish)
      timer->on_finish(timer->arg);
    free(timer);
  }
  fio_unlock(&fio_timer_lock);
}

/**
 * Creates a timer to run a task at the specified interval.
 *
 * The task will repeat `repetitions` times. If `repetitions` is set to 0, task
 * will repeat forever.
 *
 * Returns -1 on error.
 *
 * The `on_finish` handler is always called (even on error).
 */
int fio_run_every(size_t milliseconds, size_t repetitions, void (*task)(void *),
                  void *arg, void (*on_finish)(void *)) {
  if (!task || (milliseconds == 0 && !repetitions))
    return -1;
  fio_timer_s *timer = malloc(sizeof(*timer));
  FIO_ASSERT_ALLOC(timer);
  fio_mark_time();
  *timer = (fio_timer_s){
      .due = fio_timer_calc_due(milliseconds),
      .interval = milliseconds,
      .repetitions = repetitions,
      .task = task,
      .arg = arg,
      .on_finish = on_finish,
  };
  fio_timer_add_order(timer);
  return 0;
}

/* *****************************************************************************
Section Start Marker











                               Concurrency Helpers












***************************************************************************** */

volatile uint8_t fio_signal_children_flag = 0;
volatile fio_lock_i fio_signal_set_flag = 0;
/* store old signal handlers to propegate signal handling */
static struct sigaction fio_old_sig_chld;
static struct sigaction fio_old_sig_pipe;
static struct sigaction fio_old_sig_term;
static struct sigaction fio_old_sig_int;
#if !FIO_DISABLE_HOT_RESTART
static struct sigaction fio_old_sig_usr1;
#endif

/*
 * Zombie Reaping
 * With thanks to Dr Graham D Shaw.
 * http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
 */
static void reap_child_handler(int sig) {
  (void)(sig);
  int old_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
  errno = old_errno;
  if (fio_old_sig_chld.sa_handler != SIG_IGN &&
      fio_old_sig_chld.sa_handler != SIG_DFL)
    fio_old_sig_chld.sa_handler(sig);
}

/* initializes zombie reaping for the process */
void fio_reap_children(void) {
  struct sigaction sa;
  if (fio_old_sig_chld.sa_handler)
    return;
  sa.sa_handler = reap_child_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, &fio_old_sig_chld) == -1) {
    perror("Child reaping initialization failed");
    kill(0, SIGINT);
    exit(errno);
  }
}

/* handles the SIGUSR1, SIGINT and SIGTERM signals. */
static void sig_int_handler(int sig) {
  struct sigaction *old = NULL;
  switch (sig) {
#if !FIO_DISABLE_HOT_RESTART
  case SIGUSR1:
    fio_signal_children_flag = 1;
    old = &fio_old_sig_usr1;
    break;
#endif
    /* fallthrough */
  case SIGINT:
    if (!old)
      old = &fio_old_sig_int;
    /* fallthrough */
  case SIGTERM:
    if (!old)
      old = &fio_old_sig_term;
    fio_stop();
    break;
  case SIGPIPE:
    if (!old)
      old = &fio_old_sig_pipe;
  /* fallthrough */
  default:
    break;
  }
  /* propagate signale handling to previous existing handler (if any) */
  if (old && old->sa_handler != SIG_IGN && old->sa_handler != SIG_DFL)
    old->sa_handler(sig);
}

/* setup handling for the SIGUSR1, SIGPIPE, SIGINT and SIGTERM signals. */
static void fio_signal_handler_setup(void) {
  /* setup signal handling */
  struct sigaction act;
  if (fio_trylock(&fio_signal_set_flag))
    return;

  memset(&act, 0, sizeof(act));

  act.sa_handler = sig_int_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_RESTART | SA_NOCLDSTOP;

  if (sigaction(SIGINT, &act, &fio_old_sig_int)) {
    perror("couldn't set signal handler");
    return;
  };

  if (sigaction(SIGTERM, &act, &fio_old_sig_term)) {
    perror("couldn't set signal handler");
    return;
  };
#if !FIO_DISABLE_HOT_RESTART
  if (sigaction(SIGUSR1, &act, &fio_old_sig_usr1)) {
    perror("couldn't set signal handler");
    return;
  };
#endif

  act.sa_handler = SIG_IGN;
  if (sigaction(SIGPIPE, &act, &fio_old_sig_pipe)) {
    perror("couldn't set signal handler");
    return;
  };
}

void fio_signal_handler_reset(void) {
  struct sigaction old;
  if (fio_signal_set_flag)
    return;
  fio_unlock(&fio_signal_set_flag);
  memset(&old, 0, sizeof(old));
  sigaction(SIGINT, &fio_old_sig_int, &old);
  sigaction(SIGTERM, &fio_old_sig_term, &old);
  sigaction(SIGPIPE, &fio_old_sig_pipe, &old);
  if (fio_old_sig_chld.sa_handler)
    sigaction(SIGCHLD, &fio_old_sig_chld, &old);
#if !FIO_DISABLE_HOT_RESTART
  sigaction(SIGUSR1, &fio_old_sig_usr1, &old);
  memset(&fio_old_sig_usr1, 0, sizeof(fio_old_sig_usr1));
#endif
  memset(&fio_old_sig_int, 0, sizeof(fio_old_sig_int));
  memset(&fio_old_sig_term, 0, sizeof(fio_old_sig_term));
  memset(&fio_old_sig_pipe, 0, sizeof(fio_old_sig_pipe));
  memset(&fio_old_sig_chld, 0, sizeof(fio_old_sig_chld));
}

/**
 * Returns 1 if the current process is a worker process or a single process.
 *
 * Otherwise returns 0.
 *
 * NOTE: When cluster mode is off, the root process is also the worker process.
 *       This means that single process instances don't automatically respawn
 *       after critical errors.
 */
int fio_is_worker(void) { return fio_data->is_worker; }

/**
 * Returns 1 if the current process is the master (root) process.
 *
 * Otherwise returns 0.
 */
int fio_is_master(void) {
  return fio_data->is_worker == 0 || fio_data->workers == 1;
}

/** returns facil.io's parent (root) process pid. */
pid_t fio_parent_pid(void) { return fio_data->parent; }

static inline size_t fio_detect_cpu_cores(void) {
  ssize_t cpu_count = 0;
#ifdef _SC_NPROCESSORS_ONLN
  cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  if (cpu_count < 0) {
    FIO_LOG_WARNING("CPU core count auto-detection failed.");
    return 0;
  }
#else
  FIO_LOG_WARNING("CPU core count auto-detection failed.");
#endif
  return cpu_count;
}

/**
 * Returns the number of expected threads / processes to be used by facil.io.
 *
 * The pointers should start with valid values that match the expected threads /
 * processes values passed to `fio_run`.
 *
 * The data in the pointers will be overwritten with the result.
 */
void fio_expected_concurrency(int16_t *threads, int16_t *processes) {
  if (!threads || !processes)
    return;
  if (!*threads && !*processes) {
    /* both options set to 0 - default to cores*cores matrix */
    ssize_t cpu_count = fio_detect_cpu_cores();
#if FIO_CPU_CORES_LIMIT
    if (cpu_count > FIO_CPU_CORES_LIMIT) {
      static int print_cores_warning = 1;
      if (print_cores_warning) {
        FIO_LOG_WARNING(
            "Detected %zu cores. Capping auto-detection of cores to %zu.\n"
            "      Avoid this message by setting threads / workers manually.\n"
            "      To increase auto-detection limit, recompile with:\n"
            "             -DFIO_CPU_CORES_LIMIT=%zu",
            (size_t)cpu_count, (size_t)FIO_CPU_CORES_LIMIT, (size_t)cpu_count);
        print_cores_warning = 0;
      }
      cpu_count = FIO_CPU_CORES_LIMIT;
    }
#endif
    *threads = *processes = (int16_t)cpu_count;
    if (cpu_count > 3) {
      /* leave a core available for the kernel */
      --(*processes);
    }
  } else if (*threads < 0 || *processes < 0) {
    /* Set any option that is less than 0 be equal to cores/value */
    /* Set any option equal to 0 be equal to the other option in value */
    ssize_t cpu_count = fio_detect_cpu_cores();
    size_t thread_cpu_adjust = (*threads <= 0 ? 1 : 0);
    size_t worker_cpu_adjust = (*processes <= 0 ? 1 : 0);

    if (cpu_count > 0) {
      int16_t tmp = 0;
      if (*threads < 0)
        tmp = (int16_t)(cpu_count / (*threads * -1));
      else if (*threads == 0) {
        tmp = -1 * *processes;
        thread_cpu_adjust = 0;
      } else
        tmp = *threads;
      if (*processes < 0)
        *processes = (int16_t)(cpu_count / (*processes * -1));
      else if (*processes == 0) {
        *processes = -1 * *threads;
        worker_cpu_adjust = 0;
      }
      *threads = tmp;
      tmp = *processes;
      if (worker_cpu_adjust && (*processes * *threads) >= cpu_count &&
          cpu_count > 3) {
        /* leave a resources available for the kernel */
        --*processes;
      }
      if (thread_cpu_adjust && (*threads * tmp) >= cpu_count && cpu_count > 3) {
        /* leave a resources available for the kernel */
        --*threads;
      }
    }
  }

  /* make sure we have at least one process and at least one thread */
  if (*processes <= 0)
    *processes = 1;
  if (*threads <= 0)
    *threads = 1;
}

static fio_lock_i fio_fork_lock = FIO_LOCK_INIT;

/* *****************************************************************************
Section Start Marker













                       Polling State Machine - epoll














***************************************************************************** */
#if FIO_ENGINE_EPOLL
#include <sys/epoll.h>

/**
 * Returns a C string detailing the IO engine selected during compilation.
 *
 * Valid values are "kqueue", "epoll" and "poll".
 */
char const *fio_engine(void) { return "epoll"; }

/* epoll tester, in and out */
static int evio_fd[3] = {-1, -1, -1};

static void fio_poll_close(void) {
  for (int i = 0; i < 3; ++i) {
    if (evio_fd[i] != -1) {
      close(evio_fd[i]);
      evio_fd[i] = -1;
    }
  }
}

static void fio_poll_init(void) {
  fio_poll_close();
  for (int i = 0; i < 3; ++i) {
    evio_fd[i] = epoll_create1(EPOLL_CLOEXEC);
    if (evio_fd[i] == -1)
      goto error;
  }
  for (int i = 1; i < 3; ++i) {
    struct epoll_event chevent = {
        .events = (EPOLLOUT | EPOLLIN),
        .data.fd = evio_fd[i],
    };
    if (epoll_ctl(evio_fd[0], EPOLL_CTL_ADD, evio_fd[i], &chevent) == -1)
      goto error;
  }
  return;
error:
  FIO_LOG_FATAL("couldn't initialize epoll.");
  fio_poll_close();
  exit(errno);
  return;
}

static inline int fio_poll_add2(int fd, uint32_t events, int ep_fd) {
  struct epoll_event chevent;
  int ret;
  do {
    errno = 0;
    chevent = (struct epoll_event){
        .events = events,
        .data.fd = fd,
    };
    ret = epoll_ctl(ep_fd, EPOLL_CTL_MOD, fd, &chevent);
    if (ret == -1 && errno == ENOENT) {
      errno = 0;
      chevent = (struct epoll_event){
          .events = events,
          .data.fd = fd,
      };
      ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd, &chevent);
    }
  } while (errno == EINTR);

  return ret;
}

static inline void fio_poll_add_read(intptr_t fd) {
  fio_poll_add2(fd, (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT),
                evio_fd[1]);
  return;
}

static inline void fio_poll_add_write(intptr_t fd) {
  fio_poll_add2(fd, (EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT),
                evio_fd[2]);
  return;
}

static inline void fio_poll_add(intptr_t fd) {
  if (fio_poll_add2(fd, (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT),
                    evio_fd[1]) == -1)
    return;
  fio_poll_add2(fd, (EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT),
                evio_fd[2]);
  return;
}

FIO_FUNC inline void fio_poll_remove_fd(intptr_t fd) {
  struct epoll_event chevent = {.events = (EPOLLOUT | EPOLLIN), .data.fd = fd};
  epoll_ctl(evio_fd[1], EPOLL_CTL_DEL, fd, &chevent);
  epoll_ctl(evio_fd[2], EPOLL_CTL_DEL, fd, &chevent);
}

static size_t fio_poll(void) {
  int timeout_millisec = fio_timer_calc_first_interval();
  struct epoll_event internal[2];
  struct epoll_event events[FIO_POLL_MAX_EVENTS];
  int total = 0;
  /* wait for events and handle them */
  int internal_count = epoll_wait(evio_fd[0], internal, 2, timeout_millisec);
  if (internal_count == 0)
    return internal_count;
  for (int j = 0; j < internal_count; ++j) {
    int active_count =
        epoll_wait(internal[j].data.fd, events, FIO_POLL_MAX_EVENTS, 0);
    if (active_count > 0) {
      for (int i = 0; i < active_count; i++) {
        if (events[i].events & (~(EPOLLIN | EPOLLOUT))) {
          // errors are hendled as disconnections (on_close)
          fio_force_close_in_poll(fd2uuid(events[i].data.fd));
        } else {
          // no error, then it's an active event(s)
          if (events[i].events & EPOLLOUT) {
            fio_defer_push_urgent(deferred_on_ready,
                                  (void *)fd2uuid(events[i].data.fd), NULL);
          }
          if (events[i].events & EPOLLIN)
            fio_defer_push_task(deferred_on_data,
                                (void *)fd2uuid(events[i].data.fd), NULL);
        }
      } // end for loop
      total += active_count;
    }
  }
  return total;
}

#endif
/* *****************************************************************************
Section Start Marker













                       Polling State Machine - kqueue














***************************************************************************** */
#if FIO_ENGINE_KQUEUE
#include <sys/event.h>

/**
 * Returns a C string detailing the IO engine selected during compilation.
 *
 * Valid values are "kqueue", "epoll" and "poll".
 */
char const *fio_engine(void) { return "kqueue"; }

static int evio_fd = -1;

static void fio_poll_close(void) { close(evio_fd); }

static void fio_poll_init(void) {
  fio_poll_close();
  evio_fd = kqueue();
  if (evio_fd == -1) {
    FIO_LOG_FATAL("couldn't open kqueue.\n");
    exit(errno);
  }
}

static inline void fio_poll_add_read(intptr_t fd) {
  struct kevent chevent[1];
  EV_SET(chevent, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT,
         0, 0, ((void *)fd));
  do {
    errno = 0;
    kevent(evio_fd, chevent, 1, NULL, 0, NULL);
  } while (errno == EINTR);
  return;
}

static inline void fio_poll_add_write(intptr_t fd) {
  struct kevent chevent[1];
  EV_SET(chevent, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT,
         0, 0, ((void *)fd));
  do {
    errno = 0;
    kevent(evio_fd, chevent, 1, NULL, 0, NULL);
  } while (errno == EINTR);
  return;
}

static inline void fio_poll_add(intptr_t fd) {
  struct kevent chevent[2];
  EV_SET(chevent, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT,
         0, 0, ((void *)fd));
  EV_SET(chevent + 1, fd, EVFILT_WRITE,
         EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT, 0, 0, ((void *)fd));
  do {
    errno = 0;
    kevent(evio_fd, chevent, 2, NULL, 0, NULL);
  } while (errno == EINTR);
  return;
}

FIO_FUNC inline void fio_poll_remove_fd(intptr_t fd) {
  if (evio_fd < 0)
    return;
  struct kevent chevent[2];
  EV_SET(chevent, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  EV_SET(chevent + 1, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
  do {
    errno = 0;
    kevent(evio_fd, chevent, 2, NULL, 0, NULL);
  } while (errno == EINTR);
}

static size_t fio_poll(void) {
  if (evio_fd < 0)
    return -1;
  int timeout_millisec = fio_timer_calc_first_interval();
  struct kevent events[FIO_POLL_MAX_EVENTS] = {{0}};

  const struct timespec timeout = {
      .tv_sec = (timeout_millisec / 1000),
      .tv_nsec = ((timeout_millisec & (~1023UL)) * 1000000)};
  /* wait for events and handle them */
  int active_count =
      kevent(evio_fd, NULL, 0, events, FIO_POLL_MAX_EVENTS, &timeout);

  if (active_count > 0) {
    for (int i = 0; i < active_count; i++) {
      // test for event(s) type
      if (events[i].filter == EVFILT_WRITE) {
        fio_defer_push_urgent(deferred_on_ready,
                              ((void *)fd2uuid(events[i].udata)), NULL);
      } else if (events[i].filter == EVFILT_READ) {
        fio_defer_push_task(deferred_on_data, (void *)fd2uuid(events[i].udata),
                            NULL);
      }
      if (events[i].flags & (EV_EOF | EV_ERROR)) {
        fio_force_close_in_poll(fd2uuid(events[i].udata));
      }
    }
  } else if (active_count < 0) {
    if (errno == EINTR)
      return 0;
    return -1;
  }
  return active_count;
}

#endif
/* *****************************************************************************
Section Start Marker













                       Polling State Machine - poll














***************************************************************************** */

#if FIO_ENGINE_POLL

/**
 * Returns a C string detailing the IO engine selected during compilation.
 *
 * Valid values are "kqueue", "epoll" and "poll".
 */
char const *fio_engine(void) { return "poll"; }

#define FIO_POLL_READ_EVENTS (POLLPRI | POLLIN)
#define FIO_POLL_WRITE_EVENTS (POLLOUT)

static void fio_poll_close(void) {}

static void fio_poll_init(void) {}

static inline void fio_poll_remove_fd(int fd) {
  fio_data->poll[fd].fd = -1;
  fio_data->poll[fd].events = 0;
}

static inline void fio_poll_add_read(int fd) {
  fio_data->poll[fd].fd = fd;
  fio_data->poll[fd].events |= FIO_POLL_READ_EVENTS;
}

static inline void fio_poll_add_write(int fd) {
  fio_data->poll[fd].fd = fd;
  fio_data->poll[fd].events |= FIO_POLL_WRITE_EVENTS;
}

static inline void fio_poll_add(int fd) {
  fio_data->poll[fd].fd = fd;
  fio_data->poll[fd].events = FIO_POLL_READ_EVENTS | FIO_POLL_WRITE_EVENTS;
}

static inline void fio_poll_remove_read(int fd) {
  fio_lock(&fio_data->lock);
  if (fio_data->poll[fd].events & FIO_POLL_WRITE_EVENTS)
    fio_data->poll[fd].events = FIO_POLL_WRITE_EVENTS;
  else {
    fio_poll_remove_fd(fd);
  }
  fio_unlock(&fio_data->lock);
}

static inline void fio_poll_remove_write(int fd) {
  fio_lock(&fio_data->lock);
  if (fio_data->poll[fd].events & FIO_POLL_READ_EVENTS)
    fio_data->poll[fd].events = FIO_POLL_READ_EVENTS;
  else {
    fio_poll_remove_fd(fd);
  }
  fio_unlock(&fio_data->lock);
}

/** returns non-zero if events were scheduled, 0 if idle */
static size_t fio_poll(void) {
  /* shrink fd poll range */
  size_t end = fio_data->capa; // max_protocol_fd might break TLS
  size_t start = 0;
  struct pollfd *list = NULL;
  fio_lock(&fio_data->lock);
  while (start < end && fio_data->poll[start].fd == -1)
    ++start;
  while (start < end && fio_data->poll[end - 1].fd == -1)
    --end;
  if (start != end) {
    /* copy poll list for multi-threaded poll */
    list = fio_malloc(sizeof(struct pollfd) * end);
    memcpy(list + start, fio_data->poll + start,
           (sizeof(struct pollfd)) * (end - start));
  }
  fio_unlock(&fio_data->lock);

  int timeout = fio_timer_calc_first_interval();
  size_t count = 0;

  if (start == end) {
    fio_throttle_thread((timeout * 1000000UL));
  } else if (poll(list + start, end - start, timeout) == -1) {
    goto finish;
  }
  for (size_t i = start; i < end; ++i) {
    if (list[i].revents) {
      touchfd(i);
      ++count;
      if (list[i].revents & FIO_POLL_WRITE_EVENTS) {
        // FIO_LOG_DEBUG("Poll Write %zu => %p", i, (void *)fd2uuid(i));
        fio_poll_remove_write(i);
        fio_defer_push_urgent(deferred_on_ready, (void *)fd2uuid(i), NULL);
      }
      if (list[i].revents & FIO_POLL_READ_EVENTS) {
        // FIO_LOG_DEBUG("Poll Read %zu => %p", i, (void *)fd2uuid(i));
        fio_poll_remove_read(i);
        fio_defer_push_task(deferred_on_data, (void *)fd2uuid(i), NULL);
      }
      if (list[i].revents & (POLLHUP | POLLERR)) {
        // FIO_LOG_DEBUG("Poll Hangup %zu => %p", i, (void *)fd2uuid(i));
        fio_poll_remove_fd(i);
        fio_force_close_in_poll(fd2uuid(i));
      }
      if (list[i].revents & POLLNVAL) {
        // FIO_LOG_DEBUG("Poll Invalid %zu => %p", i, (void *)fd2uuid(i));
        fio_poll_remove_fd(i);
        fio_lock(&fd_data(i).protocol_lock);
        fio_clear_fd(i, 0);
        fio_unlock(&fd_data(i).protocol_lock);
      }
    }
  }
finish:
  fio_free(list);
  return count;
}

#endif /* FIO_ENGINE_POLL */

/* *****************************************************************************
Section Start Marker












                         IO Callbacks / Event Handling













***************************************************************************** */

/* *****************************************************************************
Mock Protocol Callbacks and Service Funcions
***************************************************************************** */
static void mock_on_ev(intptr_t uuid, fio_protocol_s *protocol) {
  (void)uuid;
  (void)protocol;
}

static void mock_on_data(intptr_t uuid, fio_protocol_s *protocol) {
  fio_suspend(uuid);
  (void)protocol;
}

static uint8_t mock_on_shutdown(intptr_t uuid, fio_protocol_s *protocol) {
  return 0;
  (void)protocol;
  (void)uuid;
}

static uint8_t mock_on_shutdown_eternal(intptr_t uuid,
                                        fio_protocol_s *protocol) {
  return 255;
  (void)protocol;
  (void)uuid;
}

static void mock_ping(intptr_t uuid, fio_protocol_s *protocol) {
  (void)protocol;
  fio_force_close(uuid);
}
static void mock_ping2(intptr_t uuid, fio_protocol_s *protocol) {
  (void)protocol;
  touchfd(fio_uuid2fd(uuid));
  if (uuid_data(uuid).timeout == 255)
    return;
  protocol->ping = mock_ping;
  uuid_data(uuid).timeout = 8;
  fio_close(uuid);
}

FIO_FUNC void mock_ping_eternal(intptr_t uuid, fio_protocol_s *protocol) {
  (void)protocol;
  fio_touch(uuid);
}

/* *****************************************************************************
Deferred event handlers - these tasks safely forward the events to the Protocol
***************************************************************************** */

static void deferred_on_close(void *uuid_, void *pr_) {
  fio_protocol_s *pr = pr_;
  if (pr->rsv)
    goto postpone;
  pr->on_close((intptr_t)uuid_, pr);
  return;
postpone:
  fio_defer_push_task(deferred_on_close, uuid_, pr_);
}

static void deferred_on_shutdown(void *arg, void *arg2) {
  if (!uuid_data(arg).protocol) {
    return;
  }
  fio_protocol_s *pr = protocol_try_lock(fio_uuid2fd(arg), FIO_PR_LOCK_TASK);
  if (!pr) {
    if (errno == EBADF)
      return;
    goto postpone;
  }
  touchfd(fio_uuid2fd(arg));
  uint8_t r = pr->on_shutdown ? pr->on_shutdown((intptr_t)arg, pr) : 0;
  if (r) {
    if (r == 255) {
      uuid_data(arg).timeout = 0;
    } else {
      fio_atomic_add(&fio_data->connection_count, 1);
      uuid_data(arg).timeout = r;
    }
    pr->ping = mock_ping2;
    protocol_unlock(pr, FIO_PR_LOCK_TASK);
  } else {
    fio_atomic_add(&fio_data->connection_count, 1);
    uuid_data(arg).timeout = 8;
    pr->ping = mock_ping;
    protocol_unlock(pr, FIO_PR_LOCK_TASK);
    fio_close((intptr_t)arg);
  }
  return;
postpone:
  fio_defer_push_task(deferred_on_shutdown, arg, NULL);
  (void)arg2;
}

static void deferred_on_ready_usr(void *arg, void *arg2) {
  errno = 0;
  fio_protocol_s *pr = protocol_try_lock(fio_uuid2fd(arg), FIO_PR_LOCK_WRITE);
  if (!pr) {
    if (errno == EBADF)
      return;
    goto postpone;
  }
  pr->on_ready((intptr_t)arg, pr);
  protocol_unlock(pr, FIO_PR_LOCK_WRITE);
  return;
postpone:
  fio_defer_push_task(deferred_on_ready, arg, NULL);
  (void)arg2;
}

static void deferred_on_ready(void *arg, void *arg2) {
  errno = 0;
  if (fio_flush((intptr_t)arg) > 0 || errno == EWOULDBLOCK || errno == EAGAIN) {
    if (arg2)
      fio_defer_push_urgent(deferred_on_ready, arg, NULL);
    else
      fio_poll_add_write(fio_uuid2fd(arg));
    return;
  }
  if (!uuid_data(arg).protocol) {
    return;
  }

  fio_defer_push_task(deferred_on_ready_usr, arg, NULL);
}

static void deferred_on_data(void *uuid, void *arg2) {
  if (fio_is_closed((intptr_t)uuid)) {
    return;
  }
  if (!uuid_data(uuid).protocol)
    goto no_protocol;
  fio_protocol_s *pr = protocol_try_lock(fio_uuid2fd(uuid), FIO_PR_LOCK_TASK);
  if (!pr) {
    if (errno == EBADF) {
      return;
    }
    goto postpone;
  }
  fio_unlock(&uuid_data(uuid).scheduled);
  pr->on_data((intptr_t)uuid, pr);
  protocol_unlock(pr, FIO_PR_LOCK_TASK);
  if (!fio_trylock(&uuid_data(uuid).scheduled)) {
    fio_poll_add_read(fio_uuid2fd((intptr_t)uuid));
  }
  return;

postpone:
  if (arg2) {
    /* the event is being forced, so force rescheduling */
    fio_defer_push_task(deferred_on_data, (void *)uuid, (void *)1);
  } else {
    /* the protocol was locked, so there might not be any need for the event */
    fio_poll_add_read(fio_uuid2fd((intptr_t)uuid));
  }
  return;

no_protocol:
  /* a missing protocol might still want to invoke the RW hook flush */
  deferred_on_ready(uuid, arg2);
  return;
}

static void deferred_ping(void *arg, void *arg2) {
  if (!uuid_data(arg).protocol ||
      (uuid_data(arg).timeout &&
       (uuid_data(arg).timeout + uuid_data(arg).active >
        (fio_data->last_cycle.tv_sec)))) {
    return;
  }
  fio_protocol_s *pr = protocol_try_lock(fio_uuid2fd(arg), FIO_PR_LOCK_WRITE);
  if (!pr)
    goto postpone;
  pr->ping((intptr_t)arg, pr);
  protocol_unlock(pr, FIO_PR_LOCK_WRITE);
  return;
postpone:
  fio_defer_push_task(deferred_ping, arg, NULL);
  (void)arg2;
}

/* *****************************************************************************
Forcing / Suspending IO events
***************************************************************************** */

void fio_force_event(intptr_t uuid, enum fio_io_event ev) {
  if (!uuid_is_valid(uuid))
    return;
  switch (ev) {
  case FIO_EVENT_ON_DATA:
    fio_trylock(&uuid_data(uuid).scheduled);
    fio_defer_push_task(deferred_on_data, (void *)uuid, (void *)1);
    break;
  case FIO_EVENT_ON_TIMEOUT:
    fio_defer_push_task(deferred_ping, (void *)uuid, NULL);
    break;
  case FIO_EVENT_ON_READY:
    fio_defer_push_urgent(deferred_on_ready, (void *)uuid, NULL);
    break;
  }
}

void fio_suspend(intptr_t uuid) {
  if (uuid_is_valid(uuid))
    fio_trylock(&uuid_data(uuid).scheduled);
}

/* *****************************************************************************
Section Start Marker












                               IO Socket Layer

                     Read / Write / Accept / Connect / etc'













***************************************************************************** */

/* *****************************************************************************
Internal socket initialization functions
***************************************************************************** */

/**
Sets a socket to non blocking state.

This function is called automatically for the new socket, when using
`fio_accept` or `fio_connect`.
*/
int fio_set_non_block(int fd) {
/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
  /* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
  int flags;
  if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    flags = 0;
#ifdef O_CLOEXEC
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC);
#else
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
#elif defined(FIONBIO)
  /* Otherwise, use the old way of doing it */
  static int flags = 1;
  return ioctl(fd, FIONBIO, &flags);
#else
#error No functions / argumnet macros for non-blocking sockets.
#endif
}

static void fio_tcp_addr_cpy(int fd, int family, struct sockaddr *addrinfo) {
  const char *result =
      inet_ntop(family,
                family == AF_INET
                    ? (void *)&(((struct sockaddr_in *)addrinfo)->sin_addr)
                    : (void *)&(((struct sockaddr_in6 *)addrinfo)->sin6_addr),
                (char *)fd_data(fd).addr, sizeof(fd_data(fd).addr));
  if (result) {
    fd_data(fd).addr_len = strlen((char *)fd_data(fd).addr);
  } else {
    fd_data(fd).addr_len = 0;
    fd_data(fd).addr[0] = 0;
  }
}

/**
 * `fio_accept` accepts a new socket connection from a server socket - see the
 * server flag on `fio_socket`.
 *
 * NOTE: this function does NOT attach the socket to the IO reactor -see
 * `fio_attach`.
 */
intptr_t fio_accept(intptr_t srv_uuid) {
  struct sockaddr_in6 addrinfo[2]; /* grab a slice of stack (aligned) */
  socklen_t addrlen = sizeof(addrinfo);
  int client;
#ifdef SOCK_NONBLOCK
  client = accept4(fio_uuid2fd(srv_uuid), (struct sockaddr *)addrinfo, &addrlen,
                   SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (client <= 0)
    return -1;
#else
  client = accept(fio_uuid2fd(srv_uuid), (struct sockaddr *)addrinfo, &addrlen);
  if (client <= 0)
    return -1;
  if (fio_set_non_block(client) == -1) {
    close(client);
    return -1;
  }
#endif
  // avoid the TCP delay algorithm.
  {
    int optval = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
  }
  // handle socket buffers.
  {
    int optval = 0;
    socklen_t size = (socklen_t)sizeof(optval);
    if (!getsockopt(client, SOL_SOCKET, SO_SNDBUF, &optval, &size) &&
        optval <= 131072) {
      optval = 131072;
      setsockopt(client, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
      optval = 131072;
      setsockopt(client, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
    }
  }

  fio_lock(&fd_data(client).protocol_lock);
  fio_clear_fd(client, 1);
  fio_unlock(&fd_data(client).protocol_lock);
  /* copy peer address */
  if (((struct sockaddr *)addrinfo)->sa_family == AF_UNIX) {
    fd_data(client).addr_len = uuid_data(srv_uuid).addr_len;
    if (uuid_data(srv_uuid).addr_len) {
      memcpy(fd_data(client).addr, uuid_data(srv_uuid).addr,
             uuid_data(srv_uuid).addr_len + 1);
    }
  } else {
    fio_tcp_addr_cpy(client, ((struct sockaddr *)addrinfo)->sa_family,
                     (struct sockaddr *)addrinfo);
  }

  return fd2uuid(client);
}

/* Creates a Unix socket - returning it's uuid (or -1) */
static intptr_t fio_unix_socket(const char *address, uint8_t server) {
  /* Unix socket */
  struct sockaddr_un addr = {0};
  size_t addr_len = strlen(address);
  if (addr_len >= sizeof(addr.sun_path)) {
    FIO_LOG_ERROR("(fio_unix_socket) address too long (%zu bytes > %zu bytes).",
                  addr_len, sizeof(addr.sun_path) - 1);
    errno = ENAMETOOLONG;
    return -1;
  }
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, address, addr_len + 1); /* copy the NUL byte. */
#if defined(__APPLE__)
  addr.sun_len = addr_len;
#endif
  // get the file descriptor
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    return -1;
  }
  if (fio_set_non_block(fd) == -1) {
    close(fd);
    return -1;
  }
  if (server) {
    unlink(addr.sun_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      // perror("couldn't bind unix socket");
      close(fd);
      return -1;
    }
    if (listen(fd, SOMAXCONN) < 0) {
      // perror("couldn't start listening to unix socket");
      close(fd);
      return -1;
    }
    /* chmod for foriegn connections */
    fchmod(fd, 0777);
  } else {
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1 &&
        errno != EINPROGRESS) {
      close(fd);
      return -1;
    }
  }
  fio_lock(&fd_data(fd).protocol_lock);
  fio_clear_fd(fd, 1);
  fio_unlock(&fd_data(fd).protocol_lock);
  if (addr_len < sizeof(fd_data(fd).addr)) {
    memcpy(fd_data(fd).addr, address, addr_len + 1); /* copy the NUL byte. */
    fd_data(fd).addr_len = addr_len;
  }
  return fd2uuid(fd);
}

/* Creates a TCP/IP socket - returning it's uuid (or -1) */
static intptr_t fio_tcp_socket(const char *address, const char *port,
                               uint8_t server) {
  /* TCP/IP socket */
  // setup the address
  struct addrinfo hints = {0};
  struct addrinfo *addrinfo;       // will point to the results
  memset(&hints, 0, sizeof hints); // make sure the struct is empty
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  if (getaddrinfo(address, port, &hints, &addrinfo)) {
    // perror("addr err");
    return -1;
  }
  // get the file descriptor
  int fd =
      socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
  if (fd <= 0) {
    freeaddrinfo(addrinfo);
    return -1;
  }
  // make sure the socket is non-blocking
  if (fio_set_non_block(fd) < 0) {
    freeaddrinfo(addrinfo);
    close(fd);
    return -1;
  }
  if (server) {
    {
      // avoid the "address taken"
      int optval = 1;
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    }
    // bind the address to the socket
    int bound = 0;
    for (struct addrinfo *i = addrinfo; i != NULL; i = i->ai_next) {
      if (!bind(fd, i->ai_addr, i->ai_addrlen))
        bound = 1;
    }
    if (!bound) {
      // perror("bind err");
      freeaddrinfo(addrinfo);
      close(fd);
      return -1;
    }
#ifdef TCP_FASTOPEN
    {
      // support TCP Fast Open when available
      int optval = 128;
      setsockopt(fd, addrinfo->ai_protocol, TCP_FASTOPEN, &optval,
                 sizeof(optval));
    }
#endif
    if (listen(fd, SOMAXCONN) < 0) {
      freeaddrinfo(addrinfo);
      close(fd);
      return -1;
    }
  } else {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    errno = 0;
    for (struct addrinfo *i = addrinfo; i; i = i->ai_next) {
      if (connect(fd, i->ai_addr, i->ai_addrlen) == 0 || errno == EINPROGRESS)
        goto socket_okay;
    }
    freeaddrinfo(addrinfo);
    close(fd);
    return -1;
  }
socket_okay:
  fio_lock(&fd_data(fd).protocol_lock);
  fio_clear_fd(fd, 1);
  fio_unlock(&fd_data(fd).protocol_lock);
  fio_tcp_addr_cpy(fd, addrinfo->ai_family, (void *)addrinfo);
  freeaddrinfo(addrinfo);
  return fd2uuid(fd);
}

/* PUBLIC API: opens a server or client socket */
intptr_t fio_socket(const char *address, const char *port, uint8_t server) {
  intptr_t uuid;
  if (port) {
    char *pos = (char *)port;
    int64_t n = fio_atol(&pos);
    /* make sure port is only numerical */
    if (*pos) {
      FIO_LOG_ERROR("(fio_socket) port %s is not a number.", port);
      errno = EINVAL;
      return -1;
    }
    /* a negative port number will revert to a Unix socket. */
    if (n <= 0) {
      if (n < -1)
        FIO_LOG_WARNING("(fio_socket) negative port number %s is ignored.",
                        port);
      port = NULL;
    }
  }
  if (!address && !port) {
    FIO_LOG_ERROR("(fio_socket) both address and port are missing or invalid.");
    errno = EINVAL;
    return -1;
  }
  if (!port) {
    do {
      errno = 0;
      uuid = fio_unix_socket(address, server);
    } while (errno == EINTR);
  } else {
    do {
      errno = 0;
      uuid = fio_tcp_socket(address, port, server);
    } while (errno == EINTR);
  }
  return uuid;
}

/* *****************************************************************************
Internal socket flushing related functions
***************************************************************************** */

#ifndef BUFFER_FILE_READ_SIZE
#define BUFFER_FILE_READ_SIZE 49152
#endif

#if !defined(USE_SENDFILE) && !defined(USE_SENDFILE_LINUX) &&                  \
    !defined(USE_SENDFILE_BSD) && !defined(USE_SENDFILE_APPLE)
#if defined(__linux__) /* linux sendfile works  */
#define USE_SENDFILE_LINUX 1
#elif defined(__FreeBSD__) /* FreeBSD sendfile should work - not tested */
#define USE_SENDFILE_BSD 1
#elif defined(__APPLE__) /* Is the apple sendfile still broken? */
#define USE_SENDFILE_APPLE 2
#else /* sendfile might not be available - always set to 0 */
#define USE_SENDFILE 0
#endif

#endif

static void fio_sock_perform_close_fd(intptr_t fd) { close(fd); }

static inline void fio_sock_packet_rotate_unsafe(uintptr_t fd) {
  fio_packet_s *packet = fd_data(fd).packet;
  fd_data(fd).packet = packet->next;
  fio_atomic_sub(&fd_data(fd).packet_count, 1);
  if (!packet->next) {
    fd_data(fd).packet_last = &fd_data(fd).packet;
    fd_data(fd).packet_count = 0;
  } else if (&packet->next == fd_data(fd).packet_last) {
    fd_data(fd).packet_last = &fd_data(fd).packet;
  }
  fio_packet_free(packet);
}

static int fio_sock_write_buffer(int fd, fio_packet_s *packet) {
  int written = fd_data(fd).rw_hooks->write(
      fd2uuid(fd), fd_data(fd).rw_udata,
      ((uint8_t *)packet->data.buffer + packet->offset), packet->length);
  if (written > 0) {
    packet->length -= written;
    packet->offset += written;
    if (!packet->length) {
      fio_sock_packet_rotate_unsafe(fd);
    }
  }
  return written;
}

static int fio_sock_write_from_fd(int fd, fio_packet_s *packet) {
  ssize_t asked = 0;
  ssize_t sent = 0;
  ssize_t total = 0;
  char buff[BUFFER_FILE_READ_SIZE];
  do {
    packet->offset += sent;
    packet->length -= sent;
  retry:
    asked = pread(packet->data.fd, buff,
                  ((packet->length < BUFFER_FILE_READ_SIZE)
                       ? packet->length
                       : BUFFER_FILE_READ_SIZE),
                  packet->offset);
    if (asked <= 0)
      goto read_error;
    sent = fd_data(fd).rw_hooks->write(fd2uuid(fd), fd_data(fd).rw_udata, buff,
                                       asked);
  } while (sent == asked && packet->length);
  if (sent >= 0) {
    packet->offset += sent;
    packet->length -= sent;
    total += sent;
    if (!packet->length) {
      fio_sock_packet_rotate_unsafe(fd);
      return 1;
    }
  }
  return total;

read_error:
  if (sent == 0) {
    fio_sock_packet_rotate_unsafe(fd);
    return 1;
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    goto retry;
  return -1;
}

#if USE_SENDFILE_LINUX /* linux sendfile API */
#include <sys/sendfile.h>

static int fio_sock_sendfile_from_fd(int fd, fio_packet_s *packet) {
  ssize_t sent;
  sent =
      sendfile64(fd, packet->data.fd, (off_t *)&packet->offset, packet->length);
  if (sent < 0)
    return -1;
  packet->length -= sent;
  if (!packet->length)
    fio_sock_packet_rotate_unsafe(fd);
  return sent;
}

#elif USE_SENDFILE_BSD || USE_SENDFILE_APPLE /* FreeBSD / Apple API */
#include <sys/uio.h>

static int fio_sock_sendfile_from_fd(int fd, fio_packet_s *packet) {
  off_t act_sent = 0;
  ssize_t ret = 0;
  while (packet->length) {
    act_sent = packet->length;
#if USE_SENDFILE_APPLE
    ret = sendfile(packet->data.fd, fd, packet->offset, &act_sent, NULL, 0);
#else
    ret = sendfile(packet->data.fd, fd, packet->offset, (size_t)act_sent, NULL,
                   &act_sent, 0);
#endif
    if (ret < 0)
      goto error;
    packet->length -= act_sent;
    packet->offset += act_sent;
  }
  fio_sock_packet_rotate_unsafe(fd);
  return act_sent;
error:
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
    packet->length -= act_sent;
    packet->offset += act_sent;
  }
  return -1;
}

#else
static int (*fio_sock_sendfile_from_fd)(int fd, fio_packet_s *packet) =
    fio_sock_write_from_fd;

#endif

/* *****************************************************************************
Socket / Connection Functions
***************************************************************************** */

/**
 * Returns the information available about the socket's peer address.
 *
 * If no information is available, the struct will be initialized with zero
 * (`addr == NULL`).
 * The information is only available when the socket was accepted using
 * `fio_accept` or opened using `fio_connect`.
 */

/**
 * `fio_read` attempts to read up to count bytes from the socket into the
 * buffer starting at `buffer`.
 *
 * `fio_read`'s return values are wildly different then the native return
 * values and they aim at making far simpler sense.
 *
 * `fio_read` returns the number of bytes read (0 is a valid return value which
 * simply means that no bytes were read from the buffer).
 *
 * On a fatal connection error that leads to the connection being closed (or if
 * the connection is already closed), `fio_read` returns -1.
 *
 * The value 0 is the valid value indicating no data was read.
 *
 * Data might be available in the kernel's buffer while it is not available to
 * be read using `fio_read` (i.e., when using a transport layer, such as TLS).
 */
ssize_t fio_read(intptr_t uuid, void *buffer, size_t count) {
  if (!uuid_is_valid(uuid) || !uuid_data(uuid).open) {
    errno = EBADF;
    return -1;
  }
  if (count == 0)
    return 0;
  fio_lock(&uuid_data(uuid).sock_lock);
  ssize_t (*rw_read)(intptr_t, void *, void *, size_t) =
      uuid_data(uuid).rw_hooks->read;
  void *udata = uuid_data(uuid).rw_udata;
  fio_unlock(&uuid_data(uuid).sock_lock);
  int old_errno = errno;
  ssize_t ret;
retry_int:
  ret = rw_read(uuid, udata, buffer, count);
  if (ret > 0) {
    fio_touch(uuid);
    return ret;
  }
  if (ret < 0 && errno == EINTR)
    goto retry_int;
  if (ret < 0 &&
      (errno == EWOULDBLOCK || errno == EAGAIN || errno == ENOTCONN)) {
    errno = old_errno;
    return 0;
  }
  fio_force_close(uuid);
  return -1;
}

/**
 * `fio_write2_fn` is the actual function behind the macro `fio_write2`.
 */
ssize_t fio_write2_fn(intptr_t uuid, fio_write_args_s options) {
  if (!uuid_is_valid(uuid))
    goto error;

  /* create packet */
  fio_packet_s *packet = fio_packet_alloc();
  *packet = (fio_packet_s){
      .length = options.length,
      .offset = options.offset,
      .data.buffer = (void *)options.data.buffer,
  };
  if (options.is_fd) {
    packet->write_func = (uuid_data(uuid).rw_hooks == &FIO_DEFAULT_RW_HOOKS)
                             ? fio_sock_sendfile_from_fd
                             : fio_sock_write_from_fd;
    packet->dealloc =
        (options.after.dealloc ? options.after.dealloc
                               : (void (*)(void *))fio_sock_perform_close_fd);
  } else {
    packet->write_func = fio_sock_write_buffer;
    packet->dealloc = (options.after.dealloc ? options.after.dealloc : free);
  }
  /* add packet to outgoing list */
  uint8_t was_empty = 1;
  fio_lock(&uuid_data(uuid).sock_lock);
  if (!uuid_is_valid(uuid)) {
    goto locked_error;
  }
  if (uuid_data(uuid).packet)
    was_empty = 0;
  if (options.urgent == 0) {
    *uuid_data(uuid).packet_last = packet;
    uuid_data(uuid).packet_last = &packet->next;
  } else {
    fio_packet_s **pos = &uuid_data(uuid).packet;
    if (*pos)
      pos = &(*pos)->next;
    packet->next = *pos;
    *pos = packet;
    if (!packet->next) {
      uuid_data(uuid).packet_last = &packet->next;
    }
  }
  fio_atomic_add(&uuid_data(uuid).packet_count, 1);
  fio_unlock(&uuid_data(uuid).sock_lock);

  if (was_empty) {
    touchfd(fio_uuid2fd(uuid));
    deferred_on_ready((void *)uuid, (void *)1);
  }
  return 0;
locked_error:
  fio_unlock(&uuid_data(uuid).sock_lock);
  fio_packet_free(packet);
  errno = EBADF;
  return -1;
error:
  if (options.after.dealloc) {
    options.after.dealloc((void *)options.data.buffer);
  }
  errno = EBADF;
  return -1;
}

/** A noop function for fio_write2 in cases not deallocation is required. */
void FIO_DEALLOC_NOOP(void *arg) { (void)arg; }

/**
 * Returns the number of `fio_write` calls that are waiting in the socket's
 * queue and haven't been processed.
 */
size_t fio_pending(intptr_t uuid) {
  if (!uuid_is_valid(uuid))
    return 0;
  return uuid_data(uuid).packet_count;
}

/**
 * `fio_close` marks the connection for disconnection once all the data was
 * sent. The actual disconnection will be managed by the `fio_flush` function.
 *
 * `fio_flash` will be automatically scheduled.
 */
void fio_close(intptr_t uuid) {
  if (!uuid_is_valid(uuid)) {
    errno = EBADF;
    return;
  }
  if (uuid_data(uuid).packet || uuid_data(uuid).sock_lock) {
    uuid_data(uuid).close = 1;
    fio_poll_add_write(fio_uuid2fd(uuid));
    return;
  }
  fio_force_close(uuid);
}

/**
 * `fio_force_close` closes the connection immediately, without adhering to any
 * protocol restrictions and without sending any remaining data in the
 * connection buffer.
 */
void fio_force_close(intptr_t uuid) {
  if (!uuid_is_valid(uuid)) {
    errno = EBADF;
    return;
  }
  // FIO_LOG_DEBUG("fio_force_close called for uuid %p", (void *)uuid);
  /* make sure the close marker is set */
  if (!uuid_data(uuid).close)
    uuid_data(uuid).close = 1;
  /* clear away any packets in case we want to cut the connection short. */
  fio_packet_s *packet = NULL;
  fio_lock(&uuid_data(uuid).sock_lock);
  packet = uuid_data(uuid).packet;
  uuid_data(uuid).packet = NULL;
  uuid_data(uuid).packet_last = &uuid_data(uuid).packet;
  uuid_data(uuid).sent = 0;
  fio_unlock(&uuid_data(uuid).sock_lock);
  while (packet) {
    fio_packet_s *tmp = packet;
    packet = packet->next;
    fio_packet_free(tmp);
  }
  /* check for rw-hooks termination packet */
  if (uuid_data(uuid).open && (uuid_data(uuid).close & 1) &&
      uuid_data(uuid).rw_hooks->before_close(uuid, uuid_data(uuid).rw_udata)) {
    uuid_data(uuid).close = 2; /* don't repeat the before_close callback */
    fio_touch(uuid);
    fio_poll_add_write(fio_uuid2fd(uuid));
    return;
  }
  fio_lock(&uuid_data(uuid).protocol_lock);
  fio_clear_fd(fio_uuid2fd(uuid), 0);
  fio_unlock(&uuid_data(uuid).protocol_lock);
  close(fio_uuid2fd(uuid));
#if FIO_ENGINE_POLL
  fio_poll_remove_fd(fio_uuid2fd(uuid));
#endif
  if (fio_data->connection_count)
    fio_atomic_sub(&fio_data->connection_count, 1);
}

/**
 * `fio_flush` attempts to write any remaining data in the internal buffer to
 * the underlying file descriptor and closes the underlying file descriptor once
 * if it's marked for closure (and all the data was sent).
 *
 * Return values: 1 will be returned if data remains in the buffer. 0
 * will be returned if the buffer was fully drained. -1 will be returned on an
 * error or when the connection is closed.
 */
ssize_t fio_flush(intptr_t uuid) {
  if (!uuid_is_valid(uuid))
    goto invalid;
  errno = 0;
  ssize_t flushed = 0;
  int tmp;
  /* start critical section */
  if (fio_trylock(&uuid_data(uuid).sock_lock))
    goto would_block;

  if (!uuid_data(uuid).packet)
    goto flush_rw_hook;

  const fio_packet_s *old_packet = uuid_data(uuid).packet;
  const size_t old_sent = uuid_data(uuid).sent;

  tmp = uuid_data(uuid).packet->write_func(fio_uuid2fd(uuid),
                                           uuid_data(uuid).packet);
  if (tmp <= 0) {
    goto test_errno;
  }

  if (uuid_data(uuid).packet_count >= FIO_SLOWLORIS_LIMIT &&
      uuid_data(uuid).packet == old_packet &&
      uuid_data(uuid).sent >= old_sent &&
      (uuid_data(uuid).sent - old_sent) < 32768) {
    /* Slowloris attack assumed */
    goto attacked;
  }

  /* end critical section */
  fio_unlock(&uuid_data(uuid).sock_lock);

  /* test for fio_close marker */
  if (!uuid_data(uuid).packet && uuid_data(uuid).close)
    goto closed;

  /* return state */
  return uuid_data(uuid).open && uuid_data(uuid).packet != NULL;

would_block:
  errno = EWOULDBLOCK;
  return -1;

closed:
  fio_force_close(uuid);
  return -1;

flush_rw_hook:
  flushed = uuid_data(uuid).rw_hooks->flush(uuid, uuid_data(uuid).rw_udata);
  fio_unlock(&uuid_data(uuid).sock_lock);
  if (!flushed)
    return 0;
  if (flushed < 0) {
    goto test_errno;
  }
  touchfd(fio_uuid2fd(uuid));
  return 1;

test_errno:
  fio_unlock(&uuid_data(uuid).sock_lock);
  switch (errno) {
  case EWOULDBLOCK: /* fallthrough */
#if EWOULDBLOCK != EAGAIN
  case EAGAIN: /* fallthrough */
#endif
  case ENOTCONN:      /* fallthrough */
  case EINPROGRESS:   /* fallthrough */
  case ENOSPC:        /* fallthrough */
  case EADDRNOTAVAIL: /* fallthrough */
  case EINTR:
    return 1;
  case EFAULT:
    FIO_LOG_ERROR("fio_flush EFAULT - possible memory address error sent to "
                  "Unix socket.");
    /* fallthrough */
  case EPIPE:  /* fallthrough */
  case EIO:    /* fallthrough */
  case EINVAL: /* fallthrough */
  case EBADF:
    uuid_data(uuid).close = 1;
    fio_force_close(uuid);
    return -1;
  }
  fprintf(stderr, "UUID error: %p (%d)\n", (void *)uuid, errno);
  perror("No errno handler");
  return 0;

invalid:
  /* bad UUID */
  errno = EBADF;
  return -1;

attacked:
  /* don't close, just detach from facil.io and mark uuid as invalid */
  FIO_LOG_WARNING("(facil.io) possible Slowloris attack from %.*s",
                  (int)fio_peer_addr(uuid).len, fio_peer_addr(uuid).data);
  fio_unlock(&uuid_data(uuid).sock_lock);
  fio_clear_fd(fio_uuid2fd(uuid), 0);
  return -1;
}

/** `fio_flush_all` attempts flush all the open connections. */
size_t fio_flush_all(void) {
  if (!fio_data)
    return 0;
  size_t count = 0;
  for (uintptr_t i = 0; i <= fio_data->max_protocol_fd; ++i) {
    if ((fd_data(i).open || fd_data(i).packet) && fio_flush(fd2uuid(i)) > 0)
      ++count;
  }
  return count;
}

/* *****************************************************************************
Connection Read / Write Hooks, for overriding the system calls
***************************************************************************** */

static ssize_t fio_hooks_default_read(intptr_t uuid, void *udata, void *buf,
                                      size_t count) {
  return read(fio_uuid2fd(uuid), buf, count);
  (void)(udata);
}
static ssize_t fio_hooks_default_write(intptr_t uuid, void *udata,
                                       const void *buf, size_t count) {
  return write(fio_uuid2fd(uuid), buf, count);
  (void)(udata);
}

static ssize_t fio_hooks_default_before_close(intptr_t uuid, void *udata) {
  return 0;
  (void)udata;
  (void)uuid;
}

static ssize_t fio_hooks_default_flush(intptr_t uuid, void *udata) {
  return 0;
  (void)(uuid);
  (void)(udata);
}

static void fio_hooks_default_cleanup(void *udata) { (void)(udata); }

const fio_rw_hook_s FIO_DEFAULT_RW_HOOKS = {
    .read = fio_hooks_default_read,
    .write = fio_hooks_default_write,
    .flush = fio_hooks_default_flush,
    .before_close = fio_hooks_default_before_close,
    .cleanup = fio_hooks_default_cleanup,
};

/**
 * Replaces an existing read/write hook with another from within a read/write
 * hook callback.
 *
 * Does NOT call any cleanup callbacks.
 *
 * Returns -1 on error, 0 on success.
 */
int fio_rw_hook_replace_unsafe(intptr_t uuid, fio_rw_hook_s *rw_hooks,
                               void *udata) {
  int replaced = -1;
  uint8_t was_locked;
  intptr_t fd = fio_uuid2fd(uuid);
  if (!rw_hooks->read)
    rw_hooks->read = fio_hooks_default_read;
  if (!rw_hooks->write)
    rw_hooks->write = fio_hooks_default_write;
  if (!rw_hooks->flush)
    rw_hooks->flush = fio_hooks_default_flush;
  if (!rw_hooks->before_close)
    rw_hooks->before_close = fio_hooks_default_before_close;
  if (!rw_hooks->cleanup)
    rw_hooks->cleanup = fio_hooks_default_cleanup;
  /* protect against some fulishness... but not all of it. */
  was_locked = fio_trylock(&fd_data(fd).sock_lock);
  if (fd2uuid(fd) == uuid) {
    fd_data(fd).rw_hooks = rw_hooks;
    fd_data(fd).rw_udata = udata;
    replaced = 0;
  }
  if (!was_locked)
    fio_unlock(&fd_data(fd).sock_lock);
  return replaced;
}

/** Sets a socket hook state (a pointer to the struct). */
int fio_rw_hook_set(intptr_t uuid, fio_rw_hook_s *rw_hooks, void *udata) {
  if (fio_is_closed(uuid))
    goto invalid_uuid;
  if (!rw_hooks->read)
    rw_hooks->read = fio_hooks_default_read;
  if (!rw_hooks->write)
    rw_hooks->write = fio_hooks_default_write;
  if (!rw_hooks->flush)
    rw_hooks->flush = fio_hooks_default_flush;
  if (!rw_hooks->before_close)
    rw_hooks->before_close = fio_hooks_default_before_close;
  if (!rw_hooks->cleanup)
    rw_hooks->cleanup = fio_hooks_default_cleanup;
  intptr_t fd = fio_uuid2fd(uuid);
  fio_rw_hook_s *old_rw_hooks;
  void *old_udata;
  fio_lock(&fd_data(fd).sock_lock);
  if (fd2uuid(fd) != uuid) {
    fio_unlock(&fd_data(fd).sock_lock);
    goto invalid_uuid;
  }
  old_rw_hooks = fd_data(fd).rw_hooks;
  old_udata = fd_data(fd).rw_udata;
  fd_data(fd).rw_hooks = rw_hooks;
  fd_data(fd).rw_udata = udata;
  fio_unlock(&fd_data(fd).sock_lock);
  if (old_rw_hooks && old_rw_hooks->cleanup)
    old_rw_hooks->cleanup(old_udata);
  return 0;
invalid_uuid:
  if (!rw_hooks->cleanup)
    rw_hooks->cleanup(udata);
  return -1;
}

/* *****************************************************************************
Section Start Marker












                           IO Protocols and Attachment













***************************************************************************** */

/* *****************************************************************************
Setting the protocol
***************************************************************************** */

/* managing the protocol pointer array and the `on_close` callback */
static int fio_attach__internal(void *uuid_, void *protocol_) {
  intptr_t uuid = (intptr_t)uuid_;
  fio_protocol_s *protocol = (fio_protocol_s *)protocol_;
  if (protocol) {
    if (!protocol->on_close) {
      protocol->on_close = mock_on_ev;
    }
    if (!protocol->on_data) {
      protocol->on_data = mock_on_data;
    }
    if (!protocol->on_ready) {
      protocol->on_ready = mock_on_ev;
    }
    if (!protocol->ping) {
      protocol->ping = mock_ping;
    }
    if (!protocol->on_shutdown) {
      protocol->on_shutdown = mock_on_shutdown;
    }
    prt_meta(protocol) = (protocol_metadata_s){.rsv = 0};
  }
  if (!uuid_is_valid(uuid))
    goto invalid_uuid_unlocked;
  fio_lock(&uuid_data(uuid).protocol_lock);
  if (!uuid_is_valid(uuid)) {
    goto invalid_uuid;
  }
  fio_protocol_s *old_pr = uuid_data(uuid).protocol;
  uuid_data(uuid).open = 1;
  uuid_data(uuid).protocol = protocol;
  touchfd(fio_uuid2fd(uuid));
  fio_unlock(&uuid_data(uuid).protocol_lock);
  if (old_pr) {
    /* protocol replacement */
    fio_defer_push_task(deferred_on_close, (void *)uuid, old_pr);
    if (!protocol) {
      /* hijacking */
      fio_poll_remove_fd(fio_uuid2fd(uuid));
      fio_poll_add_write(fio_uuid2fd(uuid));
    }
  } else if (protocol) {
    /* adding a new uuid to the reactor */
    fio_poll_add(fio_uuid2fd(uuid));
  }
  fio_max_fd_min(fio_uuid2fd(uuid));
  return 0;

invalid_uuid:
  fio_unlock(&uuid_data(uuid).protocol_lock);
invalid_uuid_unlocked:
  // FIO_LOG_DEBUG("fio_attach failed for invalid uuid %p", (void *)uuid);
  if (protocol)
    fio_defer_push_task(deferred_on_close, (void *)uuid, protocol);
  if (uuid == -1)
    errno = EBADF;
  else
    errno = ENOTCONN;
  return -1;
}

/**
 * Attaches (or updates) a protocol object to a socket UUID.
 * Returns -1 on error and 0 on success.
 */
void fio_attach(intptr_t uuid, fio_protocol_s *protocol) {
  fio_attach__internal((void *)uuid, protocol);
}
/** Attaches (or updates) a protocol object to a socket UUID.
 * Returns -1 on error and 0 on success.
 */
void fio_attach_fd(int fd, fio_protocol_s *protocol) {
  fio_attach__internal((void *)fio_fd2uuid(fd), protocol);
}

/** Sets a timeout for a specific connection (only when running and valid). */
void fio_timeout_set(intptr_t uuid, uint8_t timeout) {
  if (uuid_is_valid(uuid)) {
    touchfd(fio_uuid2fd(uuid));
    uuid_data(uuid).timeout = timeout;
  } else {
    FIO_LOG_DEBUG("Called fio_timeout_set for invalid uuid %p", (void *)uuid);
  }
}
/** Gets a timeout for a specific connection. Returns 0 if there's no set
 * timeout or the connection is inactive. */
uint8_t fio_timeout_get(intptr_t uuid) { return uuid_data(uuid).timeout; }

/* *****************************************************************************
Core Callbacks for forking / starting up / cleaning up
***************************************************************************** */

typedef struct {
  fio_ls_embd_s node;
  void (*func)(void *);
  void *arg;
} callback_data_s;

typedef struct {
  fio_lock_i lock;
  fio_ls_embd_s callbacks;
} callback_collection_s;

static callback_collection_s callback_collection[FIO_CALL_NEVER + 1];

static void fio_state_on_idle_perform(void *task, void *arg) {
  ((void (*)(void *))(uintptr_t)task)(arg);
}

static inline void fio_state_callback_ensure(callback_collection_s *c) {
  if (c->callbacks.next)
    return;
  c->callbacks = (fio_ls_embd_s)FIO_LS_INIT(c->callbacks);
}

/** Adds a callback to the list of callbacks to be called for the event. */
void fio_state_callback_add(callback_type_e c_type, void (*func)(void *),
                            void *arg) {
  if (c_type == FIO_CALL_ON_INITIALIZE && fio_data) {
    func(arg);
    return;
  }
  if (!func || (int)c_type < 0 || c_type > FIO_CALL_NEVER)
    return;
  fio_lock(&callback_collection[c_type].lock);
  fio_state_callback_ensure(&callback_collection[c_type]);
  callback_data_s *tmp = malloc(sizeof(*tmp));
  FIO_ASSERT_ALLOC(tmp);
  *tmp = (callback_data_s){.func = func, .arg = arg};
  fio_ls_embd_push(&callback_collection[c_type].callbacks, &tmp->node);
  fio_unlock(&callback_collection[c_type].lock);
}

/** Removes a callback from the list of callbacks to be called for the event. */
int fio_state_callback_remove(callback_type_e c_type, void (*func)(void *),
                              void *arg) {
  if ((int)c_type < 0 || c_type > FIO_CALL_NEVER)
    return -1;
  fio_lock(&callback_collection[c_type].lock);
  FIO_LS_EMBD_FOR(&callback_collection[c_type].callbacks, pos) {
    callback_data_s *tmp = (FIO_LS_EMBD_OBJ(callback_data_s, node, pos));
    if (tmp->func == func && tmp->arg == arg) {
      fio_ls_embd_remove(&tmp->node);
      free(tmp);
      goto success;
    }
  }
  fio_unlock(&callback_collection[c_type].lock);
  return -1;
success:
  fio_unlock(&callback_collection[c_type].lock);
  return -0;
}

/** Forces all the existing callbacks to run, as if the event occurred. */
void fio_state_callback_force(callback_type_e c_type) {
  if ((int)c_type < 0 || c_type > FIO_CALL_NEVER)
    return;
  /* copy collection */
  fio_ls_embd_s copy = FIO_LS_INIT(copy);
  fio_lock(&callback_collection[c_type].lock);
  fio_state_callback_ensure(&callback_collection[c_type]);
  switch (c_type) {            /* the difference between `unshift` and `push` */
  case FIO_CALL_ON_INITIALIZE: /* fallthrough */
  case FIO_CALL_PRE_START:     /* fallthrough */
  case FIO_CALL_BEFORE_FORK:   /* fallthrough */
  case FIO_CALL_AFTER_FORK:    /* fallthrough */
  case FIO_CALL_IN_CHILD:      /* fallthrough */
  case FIO_CALL_IN_MASTER:     /* fallthrough */
  case FIO_CALL_ON_START:      /* fallthrough */
    FIO_LS_EMBD_FOR(&callback_collection[c_type].callbacks, pos) {
      callback_data_s *tmp = fio_malloc(sizeof(*tmp));
      FIO_ASSERT_ALLOC(tmp);
      *tmp = *(FIO_LS_EMBD_OBJ(callback_data_s, node, pos));
      fio_ls_embd_unshift(&copy, &tmp->node);
    }
    break;

  case FIO_CALL_ON_IDLE: /* idle callbacks are orderless and evented */
    FIO_LS_EMBD_FOR(&callback_collection[c_type].callbacks, pos) {
      callback_data_s *tmp = FIO_LS_EMBD_OBJ(callback_data_s, node, pos);
      fio_defer_push_task(fio_state_on_idle_perform,
                          (void *)(uintptr_t)tmp->func, tmp->arg);
    }
    break;

  case FIO_CALL_ON_SHUTDOWN:     /* fallthrough */
  case FIO_CALL_ON_FINISH:       /* fallthrough */
  case FIO_CALL_ON_PARENT_CRUSH: /* fallthrough */
  case FIO_CALL_ON_CHILD_CRUSH:  /* fallthrough */
  case FIO_CALL_AT_EXIT:         /* fallthrough */
  case FIO_CALL_NEVER:           /* fallthrough */
  default:
    FIO_LS_EMBD_FOR(&callback_collection[c_type].callbacks, pos) {
      callback_data_s *tmp = fio_malloc(sizeof(*tmp));
      FIO_ASSERT_ALLOC(tmp);
      *tmp = *(FIO_LS_EMBD_OBJ(callback_data_s, node, pos));
      fio_ls_embd_push(&copy, &tmp->node);
    }
    break;
  }

  fio_unlock(&callback_collection[c_type].lock);
  /* run callbacks + free data */
  while (fio_ls_embd_any(&copy)) {
    callback_data_s *tmp =
        FIO_LS_EMBD_OBJ(callback_data_s, node, fio_ls_embd_pop(&copy));
    if (tmp->func) {
      tmp->func(tmp->arg);
    }
    fio_free(tmp);
  }
}

/** Clears all the existing callbacks for the event. */
void fio_state_callback_clear(callback_type_e c_type) {
  if ((int)c_type < 0 || c_type > FIO_CALL_NEVER)
    return;
  fio_lock(&callback_collection[c_type].lock);
  fio_state_callback_ensure(&callback_collection[c_type]);
  while (fio_ls_embd_any(&callback_collection[c_type].callbacks)) {
    callback_data_s *tmp = FIO_LS_EMBD_OBJ(
        callback_data_s, node,
        fio_ls_embd_shift(&callback_collection[c_type].callbacks));
    free(tmp);
  }
  fio_unlock(&callback_collection[c_type].lock);
}

void fio_state_callback_on_fork(void) {
  for (size_t i = 0; i < (FIO_CALL_NEVER + 1); ++i) {
    callback_collection[i].lock = FIO_LOCK_INIT;
  }
}
void fio_state_callback_clear_all(void) {
  for (size_t i = 0; i < (FIO_CALL_NEVER + 1); ++i) {
    fio_state_callback_clear((callback_type_e)i);
  }
}

/* *****************************************************************************
IO bound tasks
***************************************************************************** */

// typedef struct {
//   enum fio_protocol_lock_e type;
//   void (*task)(intptr_t uuid, fio_protocol_s *, void *udata);
//   void *udata;
//   void (*fallback)(intptr_t uuid, void *udata);
// } fio_defer_iotask_args_s;

static void fio_io_task_perform(void *uuid_, void *args_) {
  fio_defer_iotask_args_s *args = args_;
  intptr_t uuid = (intptr_t)uuid_;
  fio_protocol_s *pr = fio_protocol_try_lock(uuid, args->type);
  if (!pr)
    goto postpone;
  args->task(uuid, pr, args->udata);
  fio_protocol_unlock(pr, args->type);
  fio_free(args);
  return;
postpone:
  if (errno == EBADF) {
    if (args->fallback)
      args->fallback(uuid, args->udata);
    fio_free(args);
    return;
  }
  fio_defer_push_task(fio_io_task_perform, uuid_, args_);
}
/**
 * Schedules a protected connection task. The task will run within the
 * connection's lock.
 *
 * If an error ocuurs or the connection is closed before the task can run, the
 * `fallback` task wil be called instead, allowing for resource cleanup.
 */
void fio_defer_io_task FIO_IGNORE_MACRO(intptr_t uuid,
                                        fio_defer_iotask_args_s args) {
  if (!args.task) {
    if (args.fallback)
      fio_defer_push_task((void (*)(void *, void *))args.fallback, (void *)uuid,
                          args.udata);
    return;
  }
  fio_defer_iotask_args_s *cpy = fio_malloc(sizeof(*cpy));
  FIO_ASSERT_ALLOC(cpy);
  *cpy = args;
  fio_defer_push_task(fio_io_task_perform, (void *)uuid, cpy);
}

/* *****************************************************************************
Initialize the library
***************************************************************************** */

static void fio_pubsub_on_fork(void);

/* Called within a child process after it starts. */
static void fio_on_fork(void) {
  fio_timer_lock = FIO_LOCK_INIT;
  fio_data->lock = FIO_LOCK_INIT;
  fio_defer_on_fork();
  fio_malloc_after_fork();
  fio_poll_init();
  fio_state_callback_on_fork();

  const size_t limit = fio_data->capa;
  for (size_t i = 0; i < limit; ++i) {
    fd_data(i).sock_lock = FIO_LOCK_INIT;
    fd_data(i).protocol_lock = FIO_LOCK_INIT;
    if (fd_data(i).protocol) {
      fd_data(i).protocol->rsv = 0;
      fio_force_close(fd2uuid(i));
    }
  }

  fio_pubsub_on_fork();
  fio_max_fd_shrink();
  uint16_t old_active = fio_data->active;
  fio_data->active = 0;
  fio_defer_perform();
  fio_data->active = old_active;
  fio_data->is_worker = 1;
}

static void fio_mem_destroy(void);
static void __attribute__((destructor)) fio_lib_destroy(void) {
  uint8_t add_eol = fio_is_master();
  fio_data->active = 0;
  fio_on_fork();
  fio_defer_perform();
  fio_timer_clear_all();
  fio_defer_perform();
  fio_state_callback_force(FIO_CALL_AT_EXIT);
  fio_state_callback_clear_all();
  fio_defer_perform();
  fio_poll_close();
  fio_free(fio_data);
  /* memory library destruction must be last */
  fio_mem_destroy();
  FIO_LOG_DEBUG("(%d) facil.io resources released, exit complete.",
                (int)getpid());
  if (add_eol)
    fprintf(stderr, "\n"); /* add EOL to logs (logging adds EOL before text */
}

static void fio_mem_init(void);
static void fio_cluster_init(void);
static void fio_pubsub_initialize(void);
static void __attribute__((constructor)) fio_lib_init(void) {
  /* detect socket capacity - MUST be first...*/
  ssize_t capa = 0;
  {
#ifdef _SC_OPEN_MAX
    capa = sysconf(_SC_OPEN_MAX);
#elif defined(FOPEN_MAX)
    capa = FOPEN_MAX;
#endif
    // try to maximize limits - collect max and set to max
    struct rlimit rlim = {.rlim_max = 0};
    if (getrlimit(RLIMIT_NOFILE, &rlim) == -1) {
      FIO_LOG_WARNING("`getrlimit` failed in `fio_lib_init`.");
      perror("\terrno:");
    } else {
      rlim_t original = rlim.rlim_cur;
      rlim.rlim_cur = rlim.rlim_max;
      if (rlim.rlim_cur > FIO_MAX_SOCK_CAPACITY) {
        rlim.rlim_cur = rlim.rlim_max = FIO_MAX_SOCK_CAPACITY;
      }
      while (setrlimit(RLIMIT_NOFILE, &rlim) == -1 && rlim.rlim_cur > original)
        --rlim.rlim_cur;
      getrlimit(RLIMIT_NOFILE, &rlim);
      capa = rlim.rlim_cur;
      if (capa > 1024) /* leave a slice of room */
        capa -= 16;
    }
    /* initialize memory allocator */
    fio_mem_init();
    /* initialize polling engine */
    fio_poll_init();
    /* initialize the cluster engine */
    fio_pubsub_initialize();
#if DEBUG
#if FIO_ENGINE_POLL
    FIO_LOG_INFO("facil.io " FIO_VERSION_STRING " capacity initialization:\n"
                 "*    Meximum open files %zu out of %zu\n"
                 "*    Allocating %zu bytes for state handling.\n"
                 "*    %zu bytes per connection + %zu for state handling.",
                 capa, (size_t)rlim.rlim_max,
                 (sizeof(*fio_data) + (capa * (sizeof(*fio_data->poll))) +
                  (capa * (sizeof(*fio_data->info)))),
                 (sizeof(*fio_data->poll) + sizeof(*fio_data->info)),
                 sizeof(*fio_data));
#else
    FIO_LOG_INFO("facil.io " FIO_VERSION_STRING " capacity initialization:\n"
                 "*    Meximum open files %zu out of %zu\n"
                 "*    Allocating %zu bytes for state handling.\n"
                 "*    %zu bytes per connection + %zu for state handling.",
                 capa, (size_t)rlim.rlim_max,
                 (sizeof(*fio_data) + (capa * (sizeof(*fio_data->info)))),
                 (sizeof(*fio_data->info)), sizeof(*fio_data));
#endif
#endif
  }

#if FIO_ENGINE_POLL
  /* allocate and initialize main data structures by detected capacity */
  fio_data = fio_mmap(sizeof(*fio_data) + (capa * (sizeof(*fio_data->poll))) +
                      (capa * (sizeof(*fio_data->info))));
  FIO_ASSERT_ALLOC(fio_data);
  fio_data->capa = capa;
  fio_data->poll =
      (void *)((uintptr_t)(fio_data + 1) + (sizeof(fio_data->info[0]) * capa));
#else
  /* allocate and initialize main data structures by detected capacity */
  fio_data = fio_mmap(sizeof(*fio_data) + (capa * (sizeof(*fio_data->info))));
  FIO_ASSERT_ALLOC(fio_data);
  fio_data->capa = capa;
#endif
  fio_data->parent = getpid();
  fio_data->connection_count = 0;
  fio_mark_time();

  for (ssize_t i = 0; i < capa; ++i) {
    fio_clear_fd(i, 0);
#if FIO_ENGINE_POLL
    fio_data->poll[i].fd = -1;
#endif
  }

  /* call initialization callbacks */
  fio_state_callback_force(FIO_CALL_ON_INITIALIZE);
  fio_state_callback_clear(FIO_CALL_ON_INITIALIZE);
}

/* *****************************************************************************
Section Start Marker












                             Running the IO Reactor













***************************************************************************** */

static void fio_cluster_signal_children(void);

static void fio_review_timeout(void *arg, void *ignr) {
  // TODO: Fix review for connections with no protocol?
  (void)ignr;
  fio_protocol_s *tmp;
  time_t review = fio_data->last_cycle.tv_sec;
  intptr_t fd = (intptr_t)arg;

  uint16_t timeout = fd_data(fd).timeout;
  if (!timeout)
    timeout = 300; /* enforced timout settings */
  if (!fd_data(fd).protocol || (fd_data(fd).active + timeout >= review))
    goto finish;
  tmp = protocol_try_lock(fd, FIO_PR_LOCK_STATE);
  if (!tmp) {
    if (errno == EBADF)
      goto finish;
    goto reschedule;
  }
  if (prt_meta(tmp).locks[FIO_PR_LOCK_TASK] ||
      prt_meta(tmp).locks[FIO_PR_LOCK_WRITE])
    goto unlock;
  fio_defer_push_task(deferred_ping, (void *)fio_fd2uuid((int)fd), NULL);
unlock:
  protocol_unlock(tmp, FIO_PR_LOCK_STATE);
finish:
  do {
    fd++;
  } while (!fd_data(fd).protocol && (fd <= fio_data->max_protocol_fd));

  if (fio_data->max_protocol_fd < fd) {
    fio_data->need_review = 1;
    return;
  }
reschedule:
  fio_defer_push_task(fio_review_timeout, (void *)fd, NULL);
}

/* reactor pattern cycling - common actions */
static void fio_cycle_schedule_events(void) {
  static int idle = 0;
  static time_t last_to_review = 0;
  fio_mark_time();
  fio_timer_schedule();
  fio_max_fd_shrink();
  if (fio_signal_children_flag) {
    /* hot restart support */
    fio_signal_children_flag = 0;
    fio_cluster_signal_children();
  }
  int events = fio_poll();
  if (events < 0) {
    return;
  }
  if (events > 0) {
    idle = 1;
  } else {
    /* events == 0 */
    if (idle) {
      fio_state_callback_force(FIO_CALL_ON_IDLE);
      idle = 0;
    }
  }
  if (fio_data->need_review && fio_data->last_cycle.tv_sec != last_to_review) {
    last_to_review = fio_data->last_cycle.tv_sec;
    fio_data->need_review = 0;
    fio_defer_push_task(fio_review_timeout, (void *)0, NULL);
  }
}

/* reactor pattern cycling during cleanup */
static void fio_cycle_unwind(void *ignr, void *ignr2) {
  if (fio_data->connection_count) {
    fio_cycle_schedule_events();
    fio_defer_push_task(fio_cycle_unwind, ignr, ignr2);
    return;
  }
  fio_stop();
  return;
}

/* reactor pattern cycling */
static void fio_cycle(void *ignr, void *ignr2) {
  fio_cycle_schedule_events();
  if (fio_data->active) {
    fio_defer_push_task(fio_cycle, ignr, ignr2);
    return;
  }
  return;
}

/* TODO: fixme */
static void fio_worker_startup(void) {
  /* Call the on_start callbacks for worker processes. */
  if (fio_data->workers == 1 || fio_data->is_worker) {
    fio_state_callback_force(FIO_CALL_ON_START);
    fio_state_callback_clear(FIO_CALL_ON_START);
  }

  if (fio_data->workers == 1) {
    /* Single Process - the root is also a worker */
    fio_data->is_worker = 1;
  } else if (fio_data->is_worker) {
    /* Worker Process */
    FIO_LOG_INFO("%d is running.", (int)getpid());
  } else {
    /* Root Process should run in single thread mode */
    fio_data->threads = 1;
  }

  /* require timeout review */
  fio_data->need_review = 1;

  /* the cycle task will loop by re-scheduling until it's time to finish */
  fio_defer_push_task(fio_cycle, NULL, NULL);

  /* A single thread doesn't need a pool. */
  if (fio_data->threads > 1) {
    fio_defer_thread_pool_join(fio_defer_thread_pool_new(fio_data->threads));
  } else {
    fio_defer_perform();
  }
}

/* performs all clean-up / shutdown requirements except for the exit sequence */
static void fio_worker_cleanup(void) {
  /* switch to winding down */
  if (fio_data->is_worker)
    FIO_LOG_INFO("(%d) detected exit signal.", (int)getpid());
  else
    FIO_LOG_INFO("Server Detected exit signal.");
  fio_state_callback_force(FIO_CALL_ON_SHUTDOWN);
  for (size_t i = 0; i <= fio_data->max_protocol_fd; ++i) {
    if (fd_data(i).protocol) {
      fio_defer_push_task(deferred_on_shutdown, (void *)fd2uuid(i), NULL);
    }
  }
  fio_defer_push_task(fio_cycle_unwind, NULL, NULL);
  fio_defer_perform();
  for (size_t i = 0; i <= fio_data->max_protocol_fd; ++i) {
    if (fd_data(i).protocol || fd_data(i).open) {
      fio_force_close(fd2uuid(i));
    }
  }
  fio_timer_clear_all();
  fio_defer_perform();
  if (!fio_data->is_worker) {
    fio_cluster_signal_children();
    fio_defer_perform();
    while (wait(NULL) != -1)
      ;
  }
  fio_defer_perform();
  fio_state_callback_force(FIO_CALL_ON_FINISH);
  fio_defer_perform();
  fio_signal_handler_reset();
  if (fio_data->parent == getpid()) {
    FIO_LOG_INFO("   ---  Shutdown Complete  ---\n");
  } else {
    FIO_LOG_INFO("(%d) cleanup complete.", (int)getpid());
  }
}

static void fio_sentinel_task(void *arg1, void *arg2);
static void *fio_sentinel_worker_thread(void *arg) {
  errno = 0;
  pid_t child = fio_fork();
  /* release fork lock. */
  fio_unlock(&fio_fork_lock);
  if (child == -1) {
    FIO_LOG_FATAL("couldn't spawn worker.");
    perror("\n           errno");
    kill(fio_parent_pid(), SIGINT);
    fio_stop();
    return NULL;
  } else if (child) {
    int status;
    waitpid(child, &status, 0);
#if DEBUG
    if (fio_data->active) { /* !WIFEXITED(status) || WEXITSTATUS(status) */
      if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        FIO_LOG_FATAL("Child worker (%d) crashed. Stopping services.", child);
        fio_state_callback_force(FIO_CALL_ON_CHILD_CRUSH);
      } else {
        FIO_LOG_FATAL("Child worker (%d) shutdown. Stopping services.", child);
      }
      kill(0, SIGINT);
    }
#else
    if (fio_data->active) {
      /* don't call any functions while forking. */
      fio_lock(&fio_fork_lock);
      if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        FIO_LOG_ERROR("Child worker (%d) crashed. Respawning worker.",
                      (int)child);
        fio_state_callback_force(FIO_CALL_ON_CHILD_CRUSH);
      } else {
        FIO_LOG_WARNING("Child worker (%d) shutdown. Respawning worker.",
                        (int)child);
      }
      fio_defer_push_task(fio_sentinel_task, NULL, NULL);
      fio_unlock(&fio_fork_lock);
    }
#endif
  } else {
    fio_on_fork();
    fio_state_callback_force(FIO_CALL_AFTER_FORK);
    fio_state_callback_force(FIO_CALL_IN_CHILD);
    fio_worker_startup();
    fio_worker_cleanup();
    exit(0);
  }
  return NULL;
  (void)arg;
}

static void fio_sentinel_task(void *arg1, void *arg2) {
  if (!fio_data->active)
    return;
  fio_state_callback_force(FIO_CALL_BEFORE_FORK);
  fio_lock(&fio_fork_lock); /* will wait for worker thread to release lock. */
  void *thrd =
      fio_thread_new(fio_sentinel_worker_thread, (void *)&fio_fork_lock);
  fio_thread_free(thrd);
  fio_lock(&fio_fork_lock);   /* will wait for worker thread to release lock. */
  fio_unlock(&fio_fork_lock); /* release lock for next fork. */
  fio_state_callback_force(FIO_CALL_AFTER_FORK);
  fio_state_callback_force(FIO_CALL_IN_MASTER);
  (void)arg1;
  (void)arg2;
}

FIO_FUNC void fio_start_(void) {} /* marker for SublimeText3 jump feature */

/**
 * Starts the facil.io event loop. This function will return after facil.io is
 * done (after shutdown).
 *
 * See the `struct fio_start_args` details for any possible named arguments.
 *
 * This method blocks the current thread until the server is stopped (when a
 * SIGINT/SIGTERM is received).
 */
void fio_start FIO_IGNORE_MACRO(struct fio_start_args args) {
  fio_expected_concurrency(&args.threads, &args.workers);
  fio_signal_handler_setup();

  fio_data->workers = (uint16_t)args.workers;
  fio_data->threads = (uint16_t)args.threads;
  fio_data->active = 1;
  fio_data->is_worker = 0;

  fio_state_callback_force(FIO_CALL_PRE_START);

  FIO_LOG_INFO(
      "Server is running %u %s X %u %s with facil.io " FIO_VERSION_STRING
      " (%s)\n"
      "* Detected capacity: %d open file limit\n"
      "* Root pid: %d\n"
      "* Press ^C to stop\n",
      fio_data->workers, fio_data->workers > 1 ? "workers" : "worker",
      fio_data->threads, fio_data->threads > 1 ? "threads" : "thread",
      fio_engine(), fio_data->capa, (int)fio_data->parent);

  if (args.workers > 1) {
    for (int i = 0; i < args.workers && fio_data->active; ++i) {
      fio_sentinel_task(NULL, NULL);
    }
  }
  fio_worker_startup();
  fio_worker_cleanup();
}

/* *****************************************************************************
Section Start Marker















                       Converting Numbers to Strings (and back)
















***************************************************************************** */

/* *****************************************************************************
Strings to Numbers
***************************************************************************** */

FIO_FUNC inline size_t fio_atol_skip_zero(char **pstr) {
  char *const start = *pstr;
  while (**pstr == '0') {
    ++(*pstr);
  }
  return (size_t)(*pstr - *start);
}

/* consumes any digits in the string (base 2-10), returning their value */
FIO_FUNC inline uint64_t fio_atol_consume(char **pstr, uint8_t base) {
  uint64_t result = 0;
  const uint64_t limit = UINT64_MAX - (base * base);
  while (**pstr >= '0' && **pstr < ('0' + base) && result <= (limit)) {
    result = (result * base) + (**pstr - '0');
    ++(*pstr);
  }
  return result;
}

/* returns true if there's data to be skipped */
FIO_FUNC inline uint8_t fio_atol_skip_test(char **pstr, uint8_t base) {
  return (**pstr >= '0' && **pstr < ('0' + base));
}

/* consumes any digits in the string (base 2-10), returning the count skipped */
FIO_FUNC inline uint64_t fio_atol_skip(char **pstr, uint8_t base) {
  uint64_t result = 0;
  while (fio_atol_skip_test(pstr, base)) {
    ++result;
    ++(*pstr);
  }
  return result;
}

/* consumes any hex data in the string, returning their value */
FIO_FUNC inline uint64_t fio_atol_consume_hex(char **pstr) {
  uint64_t result = 0;
  const uint64_t limit = UINT64_MAX - (16 * 16);
  for (; result <= limit;) {
    uint8_t tmp;
    if (**pstr >= '0' && **pstr <= '9')
      tmp = **pstr - '0';
    else if (**pstr >= 'A' && **pstr <= 'F')
      tmp = **pstr - ('A' - 10);
    else if (**pstr >= 'a' && **pstr <= 'f')
      tmp = **pstr - ('a' - 10);
    else
      return result;
    result = (result << 4) | tmp;
    ++(*pstr);
  }
  return result;
}

/* returns true if there's data to be skipped */
FIO_FUNC inline uint8_t fio_atol_skip_hex_test(char **pstr) {
  return (**pstr >= '0' && **pstr <= '9') || (**pstr >= 'A' && **pstr <= 'F') ||
         (**pstr >= 'a' && **pstr <= 'f');
}

/* consumes any digits in the string (base 2-10), returning the count skipped */
FIO_FUNC inline uint64_t fio_atol_skip_hex(char **pstr) {
  uint64_t result = 0;
  while (fio_atol_skip_hex_test(pstr)) {
    ++result;
    ++(*pstr);
  }
  return result;
}

/* caches a up to 8*8 */
// static inline fio_atol_pow_10_cache(size_t ex) {}

/**
 * A helper function that converts between String data to a signed int64_t.
 *
 * Numbers are assumed to be in base 10. Octal (`0###`), Hex (`0x##`/`x##`) and
 * binary (`0b##`/ `b##`) are recognized as well. For binary Most Significant
 * Bit must come first.
 *
 * The most significant difference between this function and `strtol` (aside of
 * API design), is the added support for binary representations.
 */
int64_t fio_atol(char **pstr) {
  /* No binary representation in strtol */
  char *str = *pstr;
  uint64_t result = 0;
  uint8_t invert = 0;
  while (isspace(*str))
    ++(str);
  if (str[0] == '-') {
    invert ^= 1;
    ++str;
  } else if (*str == '+') {
    ++(str);
  }

  if (str[0] == 'B' || str[0] == 'b' ||
      (str[0] == '0' && (str[1] == 'b' || str[1] == 'B'))) {
    /* base 2 */
    if (str[0] == '0')
      str++;
    str++;
    fio_atol_skip_zero(&str);
    while (str[0] == '0' || str[0] == '1') {
      result = (result << 1) | (str[0] - '0');
      str++;
    }
    goto sign; /* no overlow protection, since sign might be embedded */

  } else if (str[0] == 'x' || str[0] == 'X' ||
             (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))) {
    /* base 16 */
    if (str[0] == '0')
      str++;
    str++;
    fio_atol_skip_zero(&str);
    result = fio_atol_consume_hex(&str);
    if (fio_atol_skip_hex_test(&str)) /* too large for a number */
      return 0;
    goto sign; /* no overlow protection, since sign might be embedded */
  } else if (str[0] == '0') {
    fio_atol_skip_zero(&str);
    /* base 8 */
    result = fio_atol_consume(&str, 8);
    if (fio_atol_skip_test(&str, 8)) /* too large for a number */
      return 0;
  } else {
    /* base 10 */
    result = fio_atol_consume(&str, 10);
    if (fio_atol_skip_test(&str, 10)) /* too large for a number */
      return 0;
  }
  if (result & ((uint64_t)1 << 63))
    result = INT64_MAX; /* signed overflow protection */
sign:
  if (invert)
    result = 0 - result;
  *pstr = str;
  return (int64_t)result;
}

/** A helper function that converts between String data to a signed double. */
double fio_atof(char **pstr) { return strtold(*pstr, pstr); }

/* *****************************************************************************
Numbers to Strings
***************************************************************************** */

/**
 * A helper function that writes a signed int64_t to a string.
 *
 * No overflow guard is provided, make sure there's at least 68 bytes
 * available (for base 2).
 *
 * Offers special support for base 2 (binary), base 8 (octal), base 10 and base
 * 16 (hex). An unsupported base will silently default to base 10. Prefixes
 * are automatically added (i.e., "0x" for hex and "0b" for base 2).
 *
 * Returns the number of bytes actually written (excluding the NUL
 * terminator).
 */
size_t fio_ltoa(char *dest, int64_t num, uint8_t base) {
  const char notation[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  size_t len = 0;
  char buf[48]; /* we only need up to 20 for base 10, but base 3 needs 41... */

  if (!num)
    goto zero;

  switch (base) {
  case 1: /* fallthrough */
  case 2:
    /* Base 2 */
    {
      uint64_t n = num; /* avoid bit shifting inconsistencies with signed bit */
      uint8_t i = 0;    /* counting bits */
      dest[len++] = '0';
      dest[len++] = 'b';

      while ((i < 64) && (n & 0x8000000000000000) == 0) {
        n = n << 1;
        i++;
      }
      /* make sure the Binary representation doesn't appear signed. */
      if (i) {
        dest[len++] = '0';
      }
      /* write to dest. */
      while (i < 64) {
        dest[len++] = ((n & 0x8000000000000000) ? '1' : '0');
        n = n << 1;
        i++;
      }
      dest[len] = 0;
      return len;
    }
  case 8:
    /* Base 8 */
    {
      uint64_t l = 0;
      if (num < 0) {
        dest[len++] = '-';
        num = 0 - num;
      }
      dest[len++] = '0';

      while (num) {
        buf[l++] = '0' + (num & 7);
        num = num >> 3;
      }
      while (l) {
        --l;
        dest[len++] = buf[l];
      }
      dest[len] = 0;
      return len;
    }

  case 16:
    /* Base 16 */
    {
      uint64_t n = num; /* avoid bit shifting inconsistencies with signed bit */
      uint8_t i = 0;    /* counting bits */
      dest[len++] = '0';
      dest[len++] = 'x';
      while (i < 8 && (n & 0xFF00000000000000) == 0) {
        n = n << 8;
        i++;
      }
      /* make sure the Hex representation doesn't appear misleadingly signed. */
      if (i && (n & 0x8000000000000000)) {
        dest[len++] = '0';
        dest[len++] = '0';
      }
      /* write the damn thing, high to low */
      while (i < 8) {
        uint8_t tmp = (n & 0xF000000000000000) >> 60;
        dest[len++] = notation[tmp];
        tmp = (n & 0x0F00000000000000) >> 56;
        dest[len++] = notation[tmp];
        i++;
        n = n << 8;
      }
      dest[len] = 0;
      return len;
    }
  case 3: /* fallthrough */
  case 4: /* fallthrough */
  case 5: /* fallthrough */
  case 6: /* fallthrough */
  case 7: /* fallthrough */
  case 9: /* fallthrough */
    /* rare bases */
    if (num < 0) {
      dest[len++] = '-';
      num = 0 - num;
    }
    uint64_t l = 0;
    while (num) {
      uint64_t t = num / base;
      buf[l++] = '0' + (num - (t * base));
      num = t;
    }
    while (l) {
      --l;
      dest[len++] = buf[l];
    }
    dest[len] = 0;
    return len;

  default:
    break;
  }
  /* Base 10, the default base */

  if (num < 0) {
    dest[len++] = '-';
    num = 0 - num;
  }
  uint64_t l = 0;
  while (num) {
    uint64_t t = num / 10;
    buf[l++] = '0' + (num - (t * 10));
    num = t;
  }
  while (l) {
    --l;
    dest[len++] = buf[l];
  }
  dest[len] = 0;
  return len;

zero:
  switch (base) {
  case 1:
  case 2:
    dest[len++] = '0';
    dest[len++] = 'b';
    break;
  case 8:
    dest[len++] = '0';
    break;
  case 16:
    dest[len++] = '0';
    dest[len++] = 'x';
    dest[len++] = '0';
    break;
  }
  dest[len++] = '0';
  dest[len] = 0;
  return len;
}

/**
 * A helper function that converts between a double to a string.
 *
 * No overflow guard is provided, make sure there's at least 130 bytes
 * available (for base 2).
 *
 * Supports base 2, base 10 and base 16. An unsupported base will silently
 * default to base 10. Prefixes aren't added (i.e., no "0x" or "0b" at the
 * beginning of the string).
 *
 * Returns the number of bytes actually written (excluding the NUL
 * terminator).
 */
size_t fio_ftoa(char *dest, double num, uint8_t base) {
  if (base == 2 || base == 16) {
    /* handle the binary / Hex representation the same as if it were an
     * int64_t
     */
    int64_t *i = (void *)&num;
    return fio_ltoa(dest, *i, base);
  }

  size_t written = sprintf(dest, "%g", num);
  uint8_t need_zero = 1;
  char *start = dest;
  while (*start) {
    if (*start == ',') // locale issues?
      *start = '.';
    if (*start == '.' || *start == 'e') {
      need_zero = 0;
      break;
    }
    start++;
  }
  if (need_zero) {
    dest[written++] = '.';
    dest[written++] = '0';
  }
  return written;
}

/* *****************************************************************************
Section Start Marker







                       SSL/TLS Weak Symbols for TLS Support








***************************************************************************** */

/**
 * Returns the number of registered ALPN protocol names.
 *
 * This could be used when deciding if protocol selection should be delegated to
 * the ALPN mechanism, or whether a protocol should be immediately assigned.
 *
 * If no ALPN protocols are registered, zero (0) is returned.
 */
uintptr_t FIO_TLS_WEAK fio_tls_alpn_count(void *tls) {
  return 0;
  (void)tls;
}

/**
 * Establishes an SSL/TLS connection as an SSL/TLS Server, using the specified
 * context / settings object.
 *
 * The `uuid` should be a socket UUID that is already connected to a peer (i.e.,
 * the result of `fio_accept`).
 *
 * The `udata` is an opaque user data pointer that is passed along to the
 * protocol selected (if any protocols were added using `fio_tls_alpn_add`).
 */
void FIO_TLS_WEAK fio_tls_accept(intptr_t uuid, void *tls, void *udata) {
  FIO_LOG_FATAL("No supported SSL/TLS library available.");
  exit(-1);
  return;
  (void)uuid;
  (void)tls;
  (void)udata;
}

/**
 * Establishes an SSL/TLS connection as an SSL/TLS Client, using the specified
 * context / settings object.
 *
 * The `uuid` should be a socket UUID that is already connected to a peer (i.e.,
 * one received by a `fio_connect` specified callback `on_connect`).
 *
 * The `udata` is an opaque user data pointer that is passed along to the
 * protocol selected (if any protocols were added using `fio_tls_alpn_add`).
 */
void FIO_TLS_WEAK fio_tls_connect(intptr_t uuid, void *tls, void *udata) {
  FIO_LOG_FATAL("No supported SSL/TLS library available.");
  exit(-1);
  return;
  (void)uuid;
  (void)tls;
  (void)udata;
}

/**
 * Increase the reference count for the TLS object.
 *
 * Decrease with `fio_tls_destroy`.
 */
void FIO_TLS_WEAK fio_tls_dup(void *tls) {
  FIO_LOG_FATAL("No supported SSL/TLS library available.");
  exit(-1);
  return;
  (void)tls;
}

/**
 * Destroys the SSL/TLS context / settings object and frees any related
 * resources / memory.
 */
void FIO_TLS_WEAK fio_tls_destroy(void *tls) {
  FIO_LOG_FATAL("No supported SSL/TLS library available.");
  exit(-1);
  return;
  (void)tls;
}

/* *****************************************************************************
Section Start Marker















                       Listening to Incoming Connections
















***************************************************************************** */

/* *****************************************************************************
The listening protocol (use the facil.io API to make a socket and attach it)
***************************************************************************** */

typedef struct {
  fio_protocol_s pr;
  intptr_t uuid;
  void *udata;
  void (*on_open)(intptr_t uuid, void *udata);
  void (*on_start)(intptr_t uuid, void *udata);
  void (*on_finish)(intptr_t uuid, void *udata);
  char *port;
  char *addr;
  size_t port_len;
  size_t addr_len;
  void *tls;
} fio_listen_protocol_s;

static void fio_listen_cleanup_task(void *pr_) {
  fio_listen_protocol_s *pr = pr_;
  if (pr->tls)
    fio_tls_destroy(pr->tls);
  if (pr->on_finish) {
    pr->on_finish(pr->uuid, pr->udata);
  }
  fio_force_close(pr->uuid);
  if (pr->addr &&
      (!pr->port || *pr->port == 0 ||
       (pr->port[0] == '0' && pr->port[1] == 0)) &&
      fio_is_master()) {
    /* delete Unix sockets */
    unlink(pr->addr);
  }
  free(pr_);
}

static void fio_listen_on_startup(void *pr_) {
  fio_state_callback_remove(FIO_CALL_ON_SHUTDOWN, fio_listen_cleanup_task, pr_);
  fio_listen_protocol_s *pr = pr_;
  fio_attach(pr->uuid, &pr->pr);
  if (pr->port_len)
    FIO_LOG_DEBUG("(%d) started listening on port %s", (int)getpid(), pr->port);
  else
    FIO_LOG_DEBUG("(%d) started listening on Unix Socket at %s", (int)getpid(),
                  pr->addr);
}

static void fio_listen_on_close(intptr_t uuid, fio_protocol_s *pr_) {
  fio_listen_cleanup_task(pr_);
  (void)uuid;
}

static void fio_listen_on_data(intptr_t uuid, fio_protocol_s *pr_) {
  fio_listen_protocol_s *pr = (fio_listen_protocol_s *)pr_;
  for (int i = 0; i < 4; ++i) {
    intptr_t client = fio_accept(uuid);
    if (client == -1)
      return;
    pr->on_open(client, pr->udata);
  }
}

static void fio_listen_on_data_tls(intptr_t uuid, fio_protocol_s *pr_) {
  fio_listen_protocol_s *pr = (fio_listen_protocol_s *)pr_;
  for (int i = 0; i < 4; ++i) {
    intptr_t client = fio_accept(uuid);
    if (client == -1)
      return;
    fio_tls_accept(client, pr->tls, pr->udata);
    pr->on_open(client, pr->udata);
  }
}

static void fio_listen_on_data_tls_alpn(intptr_t uuid, fio_protocol_s *pr_) {
  fio_listen_protocol_s *pr = (fio_listen_protocol_s *)pr_;
  for (int i = 0; i < 4; ++i) {
    intptr_t client = fio_accept(uuid);
    if (client == -1)
      return;
    fio_tls_accept(client, pr->tls, pr->udata);
  }
}

/* stub for editor - unused */
void fio_listen____(void);
/**
 * Schedule a network service on a listening socket.
 *
 * Returns the listening socket or -1 (on error).
 */
intptr_t fio_listen FIO_IGNORE_MACRO(struct fio_listen_args args) {
  // ...
  if ((!args.on_open && (!args.tls || !fio_tls_alpn_count(args.tls))) ||
      (!args.address && !args.port)) {
    errno = EINVAL;
    goto error;
  }

  size_t addr_len = 0;
  size_t port_len = 0;
  if (args.address)
    addr_len = strlen(args.address);
  if (args.port) {
    port_len = strlen(args.port);
    char *tmp = (char *)args.port;
    if (!fio_atol(&tmp)) {
      port_len = 0;
      args.port = NULL;
    }
    if (*tmp) {
      /* port format was invalid, should be only numerals */
      errno = EINVAL;
      goto error;
    }
  }
  const intptr_t uuid = fio_socket(args.address, args.port, 1);
  if (uuid == -1)
    goto error;

  fio_listen_protocol_s *pr = malloc(sizeof(*pr) + addr_len + port_len +
                                     ((addr_len + port_len) ? 2 : 0));
  FIO_ASSERT_ALLOC(pr);

  if (args.tls)
    fio_tls_dup(args.tls);

  *pr = (fio_listen_protocol_s){
      .pr =
          {
              .on_close = fio_listen_on_close,
              .ping = mock_ping_eternal,
              .on_data = (args.tls ? (fio_tls_alpn_count(args.tls)
                                          ? fio_listen_on_data_tls_alpn
                                          : fio_listen_on_data_tls)
                                   : fio_listen_on_data),
          },
      .uuid = uuid,
      .udata = args.udata,
      .on_open = args.on_open,
      .on_start = args.on_start,
      .on_finish = args.on_finish,
      .tls = args.tls,
      .addr_len = addr_len,
      .port_len = port_len,
      .addr = (char *)(pr + 1),
      .port = ((char *)(pr + 1) + addr_len + 1),
  };

  if (addr_len)
    memcpy(pr->addr, args.address, addr_len + 1);
  if (port_len)
    memcpy(pr->port, args.port, port_len + 1);

  if (fio_is_running()) {
    fio_attach(pr->uuid, &pr->pr);
  } else {
    fio_state_callback_add(FIO_CALL_ON_START, fio_listen_on_startup, pr);
    fio_state_callback_add(FIO_CALL_ON_SHUTDOWN, fio_listen_cleanup_task, pr);
  }

  if (args.port)
    FIO_LOG_INFO("Listening on port %s", args.port);
  else
    FIO_LOG_INFO("Listening on Unix Socket at %s", args.address);

  return uuid;
error:
  if (args.on_finish) {
    args.on_finish(-1, args.udata);
  }
  return -1;
}

/* *****************************************************************************
Section Start Marker















                   Connecting to remote servers as a client
















***************************************************************************** */

/* *****************************************************************************
The connection protocol (use the facil.io API to make a socket and attach it)
***************************************************************************** */

typedef struct {
  fio_protocol_s pr;
  intptr_t uuid;
  void *udata;
  void *tls;
  void (*on_connect)(intptr_t uuid, void *udata);
  void (*on_fail)(intptr_t uuid, void *udata);
} fio_connect_protocol_s;

static void fio_connect_on_close(intptr_t uuid, fio_protocol_s *pr_) {
  fio_connect_protocol_s *pr = (fio_connect_protocol_s *)pr_;
  if (pr->on_fail)
    pr->on_fail(uuid, pr->udata);
  if (pr->tls)
    fio_tls_destroy(pr->tls);
  fio_free(pr);
  (void)uuid;
}

static void fio_connect_on_ready(intptr_t uuid, fio_protocol_s *pr_) {
  fio_connect_protocol_s *pr = (fio_connect_protocol_s *)pr_;
  if (pr->pr.on_ready == mock_on_ev)
    return; /* Don't call on_connect more than once */
  pr->pr.on_ready = mock_on_ev;
  pr->on_fail = NULL;
  pr->on_connect(uuid, pr->udata);
  fio_poll_add(fio_uuid2fd(uuid));
  (void)uuid;
}

static void fio_connect_on_ready_tls(intptr_t uuid, fio_protocol_s *pr_) {
  fio_connect_protocol_s *pr = (fio_connect_protocol_s *)pr_;
  if (pr->pr.on_ready == mock_on_ev)
    return; /* Don't call on_connect more than once */
  pr->pr.on_ready = mock_on_ev;
  pr->on_fail = NULL;
  fio_tls_connect(uuid, pr->tls, pr->udata);
  pr->on_connect(uuid, pr->udata);
  fio_poll_add(fio_uuid2fd(uuid));
  (void)uuid;
}

static void fio_connect_on_ready_tls_alpn(intptr_t uuid, fio_protocol_s *pr_) {
  fio_connect_protocol_s *pr = (fio_connect_protocol_s *)pr_;
  if (pr->pr.on_ready == mock_on_ev)
    return; /* Don't call on_connect more than once */
  pr->pr.on_ready = mock_on_ev;
  pr->on_fail = NULL;
  fio_tls_connect(uuid, pr->tls, pr->udata);
  fio_poll_add(fio_uuid2fd(uuid));
  (void)uuid;
}

/* stub for sublime text function navigation */
intptr_t fio_connect___(struct fio_connect_args args);

intptr_t fio_connect FIO_IGNORE_MACRO(struct fio_connect_args args) {
  if ((!args.on_connect && (!args.tls || !fio_tls_alpn_count(args.tls))) ||
      (!args.address && !args.port)) {
    errno = EINVAL;
    goto error;
  }
  const intptr_t uuid = fio_socket(args.address, args.port, 0);
  if (uuid == -1)
    goto error;
  fio_timeout_set(uuid, args.timeout);

  fio_connect_protocol_s *pr = fio_malloc(sizeof(*pr));
  FIO_ASSERT_ALLOC(pr);

  if (args.tls)
    fio_tls_dup(args.tls);

  *pr = (fio_connect_protocol_s){
      .pr =
          {
              .on_ready = (args.tls ? (fio_tls_alpn_count(args.tls)
                                           ? fio_connect_on_ready_tls_alpn
                                           : fio_connect_on_ready_tls)
                                    : fio_connect_on_ready),
              .on_close = fio_connect_on_close,
          },
      .uuid = uuid,
      .tls = args.tls,
      .udata = args.udata,
      .on_connect = args.on_connect,
      .on_fail = args.on_fail,
  };
  fio_attach(uuid, &pr->pr);
  return uuid;
error:
  if (args.on_fail)
    args.on_fail(-1, args.udata);
  return -1;
}

/* *****************************************************************************
URL address parsing
***************************************************************************** */

/**
 * Parses the URI returning it's components and their lengths (no decoding
 * performed, doesn't accept decoded URIs).
 *
 * The returned string are NOT NUL terminated, they are merely locations within
 * the original string.
 *
 * This function expects any of the following formats:
 *
 * * `/complete_path?query#target`
 *
 *   i.e.: /index.html?page=1#list
 *
 * * `host:port/complete_path?query#target`
 *
 *   i.e.:
 *      example.com/index.html
 *      example.com:8080/index.html
 *
 * * `schema://user:password@host:port/path?query#target`
 *
 *   i.e.: http://example.com/index.html?page=1#list
 *
 * Invalid formats might produce unexpected results. No error testing performed.
 */
fio_url_s fio_url_parse(const char *url, size_t length) {
  /*
  Intention:
  [schema://][user[:]][password[@]][host.com[:/]][:port/][/path][?quary][#target]
  */
  const char *end = url + length;
  const char *pos = url;
  fio_url_s r = {.scheme = {.data = (char *)url}};
  if (length == 0) {
    goto finish;
  }

  if (pos[0] == '/') {
    /* start at path */
    goto start_path;
  }

  while (pos < end && pos[0] != ':' && pos[0] != '/' && pos[0] != '@' &&
         pos[0] != '#' && pos[0] != '?')
    ++pos;

  if (pos == end) {
    /* was only host (path starts with '/') */
    r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    goto finish;
  }
  switch (pos[0]) {
  case '@':
    /* username@[host] */
    r.user = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    ++pos;
    goto start_host;
  case '/':
    /* host[/path] */
    r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    goto start_path;
  case '?':
    /* host?[query] */
    r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    ++pos;
    goto start_query;
  case '#':
    /* host#[target] */
    r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    ++pos;
    goto start_target;
  case ':':
    if (pos + 2 <= end && pos[1] == '/' && pos[2] == '/') {
      /* scheme:// */
      r.scheme.len = pos - url;
      pos += 3;
    } else {
      /* username:[password] OR */
      /* host:[port] */
      r.user = (fio_str_info_s){.data = (char *)url, .len = pos - url};
      ++pos;
      goto start_password;
    }
    break;
  }

  // start_username:
  url = pos;
  while (pos < end && pos[0] != ':' && pos[0] != '/' && pos[0] != '@'
         /* && pos[0] != '#' && pos[0] != '?' */)
    ++pos;

  if (pos >= end) { /* scheme://host */
    r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    goto finish;
  }

  switch (pos[0]) {
  case '/':
    /* scheme://host[/path] */
    r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    goto start_path;
  case '@':
    /* scheme://username@[host]... */
    r.user = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    ++pos;
    goto start_host;
  case ':':
    /* scheme://username:[password]@[host]... OR */
    /* scheme://host:[port][/...] */
    r.user = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    ++pos;
    break;
  }

start_password:
  url = pos;
  while (pos < end && pos[0] != '/' && pos[0] != '@')
    ++pos;

  if (pos >= end) {
    /* was host:port */
    r.port = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    r.host = r.user;
    r.user.len = 0;
    goto finish;
    ;
  }

  switch (pos[0]) {
  case '/':
    r.port = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    r.host = r.user;
    r.user.len = 0;
    goto start_path;
  case '@':
    r.password = (fio_str_info_s){.data = (char *)url, .len = pos - url};
    ++pos;
    break;
  }

start_host:
  url = pos;
  while (pos < end && pos[0] != '/' && pos[0] != ':' && pos[0] != '#' &&
         pos[0] != '?')
    ++pos;

  r.host = (fio_str_info_s){.data = (char *)url, .len = pos - url};
  if (pos >= end) {
    goto finish;
  }
  switch (pos[0]) {
  case '/':
    /* scheme://[...@]host[/path] */
    goto start_path;
  case '?':
    /* scheme://[...@]host?[query] (bad)*/
    ++pos;
    goto start_query;
  case '#':
    /* scheme://[...@]host#[target] (bad)*/
    ++pos;
    goto start_target;
    // case ':':
    /* scheme://[...@]host:[port] */
  }
  ++pos;

  // start_port:
  url = pos;
  while (pos < end && pos[0] != '/' && pos[0] != '#' && pos[0] != '?')
    ++pos;

  r.port = (fio_str_info_s){.data = (char *)url, .len = pos - url};

  if (pos >= end) {
    /* scheme://[...@]host:port */
    goto finish;
  }
  switch (pos[0]) {
  case '?':
    /* scheme://[...@]host:port?[query] (bad)*/
    ++pos;
    goto start_query;
  case '#':
    /* scheme://[...@]host:port#[target] (bad)*/
    ++pos;
    goto start_target;
    // case '/':
    /* scheme://[...@]host:port[/path] */
  }

start_path:
  url = pos;
  while (pos < end && pos[0] != '#' && pos[0] != '?')
    ++pos;

  r.path = (fio_str_info_s){.data = (char *)url, .len = pos - url};

  if (pos >= end) {
    goto finish;
  }
  ++pos;
  if (pos[-1] == '#')
    goto start_target;

start_query:
  url = pos;
  while (pos < end && pos[0] != '#')
    ++pos;

  r.query = (fio_str_info_s){.data = (char *)url, .len = pos - url};
  ++pos;

  if (pos >= end)
    goto finish;

start_target:
  r.target = (fio_str_info_s){.data = (char *)pos, .len = end - pos};

finish:

  /* set any empty values to NULL */
  if (!r.scheme.len)
    r.scheme.data = NULL;
  if (!r.user.len)
    r.user.data = NULL;
  if (!r.password.len)
    r.password.data = NULL;
  if (!r.host.len)
    r.host.data = NULL;
  if (!r.port.len)
    r.port.data = NULL;
  if (!r.path.len)
    r.path.data = NULL;
  if (!r.query.len)
    r.query.data = NULL;
  if (!r.target.len)
    r.target.data = NULL;

  return r;
}

/* *****************************************************************************
Section Start Marker


























                       Cluster Messaging Implementation



























***************************************************************************** */

#if FIO_PUBSUB_SUPPORT

/* *****************************************************************************
 * Data Structures - Channel / Subscriptions data
 **************************************************************************** */

typedef enum fio_cluster_message_type_e {
  FIO_CLUSTER_MSG_FORWARD,
  FIO_CLUSTER_MSG_JSON,
  FIO_CLUSTER_MSG_ROOT,
  FIO_CLUSTER_MSG_ROOT_JSON,
  FIO_CLUSTER_MSG_PUBSUB_SUB,
  FIO_CLUSTER_MSG_PUBSUB_UNSUB,
  FIO_CLUSTER_MSG_PATTERN_SUB,
  FIO_CLUSTER_MSG_PATTERN_UNSUB,
  FIO_CLUSTER_MSG_SHUTDOWN,
  FIO_CLUSTER_MSG_ERROR,
  FIO_CLUSTER_MSG_PING,
} fio_cluster_message_type_e;

typedef struct fio_collection_s fio_collection_s;

#ifndef __clang__ /* clang might misbehave by assumming non-alignment */
#pragma pack(1)   /* https://gitter.im/halide/Halide/archives/2018/07/24 */
#endif
typedef struct {
  size_t name_len;
  char *name;
  volatile size_t ref;
  fio_ls_embd_s subscriptions;
  fio_collection_s *parent;
  fio_match_fn match;
  fio_lock_i lock;
} channel_s;
#ifndef __clang__
#pragma pack()
#endif

struct subscription_s {
  fio_ls_embd_s node;
  channel_s *parent;
  void (*on_message)(fio_msg_s *msg);
  void (*on_unsubscribe)(void *udata1, void *udata2);
  void *udata1;
  void *udata2;
  /** reference counter. */
  volatile uintptr_t ref;
  /** prevents the callback from running concurrently for multiple messages. */
  fio_lock_i lock;
  fio_lock_i unsubscribed;
};

/* Use `malloc` / `free`, because channles might have a long life. */

/** Used internally by the Set object to create a new channel. */
static channel_s *fio_channel_copy(channel_s *src) {
  channel_s *dest = malloc(sizeof(*dest) + src->name_len + 1);
  FIO_ASSERT_ALLOC(dest);
  dest->name_len = src->name_len;
  dest->match = src->match;
  dest->parent = src->parent;
  dest->name = (char *)(dest + 1);
  if (src->name_len)
    memcpy(dest->name, src->name, src->name_len);
  dest->name[src->name_len] = 0;
  dest->subscriptions = (fio_ls_embd_s)FIO_LS_INIT(dest->subscriptions);
  dest->ref = 1;
  dest->lock = FIO_LOCK_INIT;
  return dest;
}
/** Frees a channel (reference counting). */
static void fio_channel_free(channel_s *ch) {
  if (!ch)
    return;
  if (fio_atomic_sub(&ch->ref, 1))
    return;
  free(ch);
}
/** Increases a channel's reference count. */
static void fio_channel_dup(channel_s *ch) {
  if (!ch)
    return;
  fio_atomic_add(&ch->ref, 1);
}
/** Tests if two channels are equal. */
static int fio_channel_cmp(channel_s *ch1, channel_s *ch2) {
  return ch1->name_len == ch2->name_len && ch1->match == ch2->match &&
         !memcmp(ch1->name, ch2->name, ch1->name_len);
}
/* pub/sub channels and core data sets have a long life, so avoid fio_malloc */
#define FIO_FORCE_MALLOC_TMP 1
#define FIO_SET_NAME fio_ch_set
#define FIO_SET_OBJ_TYPE channel_s *
#define FIO_SET_OBJ_COMPARE(o1, o2) fio_channel_cmp((o1), (o2))
#define FIO_SET_OBJ_DESTROY(obj) fio_channel_free((obj))
#define FIO_SET_OBJ_COPY(dest, src) ((dest) = fio_channel_copy((src)))
#include <fio.h>

#define FIO_FORCE_MALLOC_TMP 1
#define FIO_ARY_NAME fio_meta_ary
#define FIO_ARY_TYPE fio_msg_metadata_fn
#include <fio.h>

#define FIO_FORCE_MALLOC_TMP 1
#define FIO_SET_NAME fio_engine_set
#define FIO_SET_OBJ_TYPE fio_pubsub_engine_s *
#define FIO_SET_OBJ_COMPARE(k1, k2) ((k1) == (k2))
#include <fio.h>

struct fio_collection_s {
  fio_ch_set_s channels;
  fio_lock_i lock;
};

#define COLLECTION_INIT                                                        \
  { .channels = FIO_SET_INIT, .lock = FIO_LOCK_INIT }

static struct {
  fio_collection_s filters;
  fio_collection_s pubsub;
  fio_collection_s patterns;
  struct {
    fio_engine_set_s set;
    fio_lock_i lock;
  } engines;
  struct {
    fio_meta_ary_s ary;
    fio_lock_i lock;
  } meta;
} fio_postoffice = {
    .filters = COLLECTION_INIT,
    .pubsub = COLLECTION_INIT,
    .patterns = COLLECTION_INIT,
    .engines.lock = FIO_LOCK_INIT,
    .meta.lock = FIO_LOCK_INIT,
};

/** used to contain the message before it's passed to the handler */
typedef struct {
  fio_msg_s msg;
  size_t marker;
  size_t meta_len;
  fio_msg_metadata_s *meta;
} fio_msg_client_s;

/** used to contain the message internally while publishing */
typedef struct {
  fio_str_info_s channel;
  fio_str_info_s data;
  uintptr_t ref; /* internal reference counter */
  int32_t filter;
  int8_t is_json;
  size_t meta_len;
  fio_msg_metadata_s meta[];
} fio_msg_internal_s;

/** The default engine (settable). */
fio_pubsub_engine_s *FIO_PUBSUB_DEFAULT = FIO_PUBSUB_CLUSTER;

/* *****************************************************************************
Internal message object creation
***************************************************************************** */

/** returns a temporary fio_meta_ary_s with a copy of the metadata array */
static fio_meta_ary_s fio_postoffice_meta_copy_new(void) {
  fio_meta_ary_s t = FIO_ARY_INIT;
  if (!fio_meta_ary_count(&fio_postoffice.meta.ary)) {
    return t;
  }
  fio_lock(&fio_postoffice.meta.lock);
  fio_meta_ary_concat(&t, &fio_postoffice.meta.ary);
  fio_unlock(&fio_postoffice.meta.lock);
  return t;
}

/** frees a temporary copy created by postoffice_meta_copy_new */
static inline void fio_postoffice_meta_copy_free(fio_meta_ary_s *cpy) {
  fio_meta_ary_free(cpy);
}

static void fio_postoffice_meta_update(fio_msg_internal_s *m) {
  if (m->filter || !m->meta_len)
    return;
  fio_meta_ary_s t = fio_postoffice_meta_copy_new();
  if (t.end > m->meta_len)
    t.end = m->meta_len;
  m->meta_len = t.end;
  while (t.end) {
    --t.end;
    m->meta[t.end] = t.arry[t.end](m->channel, m->data, m->is_json);
  }
  fio_postoffice_meta_copy_free(&t);
}

static fio_msg_internal_s *
fio_msg_internal_create(int32_t filter, uint32_t type, fio_str_info_s ch,
                        fio_str_info_s data, int8_t is_json, int8_t cpy) {
  fio_meta_ary_s t = FIO_ARY_INIT;
  if (!filter)
    t = fio_postoffice_meta_copy_new();
  fio_msg_internal_s *m = fio_malloc(sizeof(*m) + (sizeof(*m->meta) * t.end) +
                                     (ch.len) + (data.len) + 16 + 2);
  FIO_ASSERT_ALLOC(m);
  *m = (fio_msg_internal_s){
      .filter = filter,
      .channel = (fio_str_info_s){.data = (char *)(m->meta + t.end) + 16,
                                  .len = ch.len},
      .data = (fio_str_info_s){.data = ((char *)(m->meta + t.end) + ch.len +
                                        16 + 1),
                               .len = data.len},
      .is_json = is_json,
      .ref = 1,
      .meta_len = t.end,
  };
  fio_u2str32((uint8_t *)(m + 1) + (sizeof(*m->meta) * t.end), ch.len);
  fio_u2str32((uint8_t *)(m + 1) + (sizeof(*m->meta) * t.end) + 4, data.len);
  fio_u2str32((uint8_t *)(m + 1) + (sizeof(*m->meta) * t.end) + 8, type);
  fio_u2str32((uint8_t *)(m + 1) + (sizeof(*m->meta) * t.end) + 12,
              (uint32_t)filter);
  // m->channel.data[ch.len] = 0; /* redundant, fio_malloc is all zero */
  // m->data.data[data.len] = 0; /* redundant, fio_malloc is all zero */
  if (cpy) {
    memcpy(m->channel.data, ch.data, ch.len);
    memcpy(m->data.data, data.data, data.len);
    while (t.end) {
      --t.end;
      m->meta[t.end] = t.arry[t.end](m->channel, m->data, is_json);
    }
  }
  fio_postoffice_meta_copy_free(&t);
  return m;
}

/** frees the internal message data */
static inline void fio_msg_internal_finalize(fio_msg_internal_s *m) {
  if (!m->channel.len)
    m->channel.data = NULL;
  if (!m->data.len)
    m->data.data = NULL;
}

/** frees the internal message data */
static inline void fio_msg_internal_free(fio_msg_internal_s *m) {
  if (fio_atomic_sub(&m->ref, 1))
    return;
  while (m->meta_len) {
    --m->meta_len;
    if (m->meta[m->meta_len].on_finish) {
      fio_msg_s tmp_msg = {
          .channel = m->channel,
          .msg = m->data,
      };
      m->meta[m->meta_len].on_finish(&tmp_msg, m->meta[m->meta_len].metadata);
    }
  }
  fio_free(m);
}

static void fio_msg_internal_free2(void *m) { fio_msg_internal_free(m); }

/* add reference count to fio_msg_internal_s */
static inline fio_msg_internal_s *fio_msg_internal_dup(fio_msg_internal_s *m) {
  fio_atomic_add(&m->ref, 1);
  return m;
}

/** internal helper */

static inline ssize_t fio_msg_internal_send_dup(intptr_t uuid,
                                                fio_msg_internal_s *m) {
  return fio_write2(uuid, .data.buffer = fio_msg_internal_dup(m),
                    .offset = (sizeof(*m) + (m->meta_len * sizeof(*m->meta))),
                    .length = 16 + m->data.len + m->channel.len + 2,
                    .after.dealloc = fio_msg_internal_free2);
}

/**
 * A mock pub/sub callback for external subscriptions.
 */
static void fio_mock_on_message(fio_msg_s *msg) { (void)msg; }

/* *****************************************************************************
Channel Subscription Management
***************************************************************************** */

static void fio_pubsub_on_channel_create(channel_s *ch);
static void fio_pubsub_on_channel_destroy(channel_s *ch);

/* some comon tasks extracted */
static inline channel_s *fio_filter_dup_lock_internal(channel_s *ch,
                                                      uint64_t hashed,
                                                      fio_collection_s *c) {
  fio_lock(&c->lock);
  ch = fio_ch_set_insert(&c->channels, hashed, ch);
  fio_channel_dup(ch);
  fio_lock(&ch->lock);
  fio_unlock(&c->lock);
  return ch;
}

/** Creates / finds a filter channel, adds a reference count and locks it. */
static channel_s *fio_filter_dup_lock(uint32_t filter) {
  channel_s ch = (channel_s){
      .name = (char *)&filter,
      .name_len = (sizeof(filter)),
      .parent = &fio_postoffice.filters,
      .ref = 8, /* avoid freeing stack memory */
  };
  return fio_filter_dup_lock_internal(&ch, filter, &fio_postoffice.filters);
}

/** Creates / finds a pubsub channel, adds a reference count and locks it. */
static channel_s *fio_channel_dup_lock(fio_str_info_s name) {
  channel_s ch = (channel_s){
      .name = name.data,
      .name_len = name.len,
      .parent = &fio_postoffice.pubsub,
      .ref = 8, /* avoid freeing stack memory */
  };
  uint64_t hashed_name = FIO_HASH_FN(
      name.data, name.len, &fio_postoffice.pubsub, &fio_postoffice.pubsub);
  channel_s *ch_p =
      fio_filter_dup_lock_internal(&ch, hashed_name, &fio_postoffice.pubsub);
  if (fio_ls_embd_is_empty(&ch_p->subscriptions)) {
    fio_pubsub_on_channel_create(ch_p);
  }
  return ch_p;
}

/** Creates / finds a pattern channel, adds a reference count and locks it. */
static channel_s *fio_channel_match_dup_lock(fio_str_info_s name,
                                             fio_match_fn match) {
  channel_s ch = (channel_s){
      .name = name.data,
      .name_len = name.len,
      .parent = &fio_postoffice.patterns,
      .match = match,
      .ref = 8, /* avoid freeing stack memory */
  };
  uint64_t hashed_name = FIO_HASH_FN(
      name.data, name.len, &fio_postoffice.pubsub, &fio_postoffice.pubsub);
  channel_s *ch_p =
      fio_filter_dup_lock_internal(&ch, hashed_name, &fio_postoffice.patterns);
  if (fio_ls_embd_is_empty(&ch_p->subscriptions)) {
    fio_pubsub_on_channel_create(ch_p);
  }
  return ch_p;
}

/* to be used for reference counting (subtructing) */
static inline void fio_subscription_free(subscription_s *s) {
  if (fio_atomic_sub(&s->ref, 1)) {
    return;
  }
  if (s->on_unsubscribe) {
    s->on_unsubscribe(s->udata1, s->udata2);
  }
  fio_channel_free(s->parent);
  fio_free(s);
}

/** SublimeText 3 marker */
subscription_s *fio_subscribe___(subscribe_args_s args);

/** Subscribes to a filter, pub/sub channle or patten */
subscription_s *fio_subscribe FIO_IGNORE_MACRO(subscribe_args_s args) {
  if (!args.on_message)
    goto error;
  channel_s *ch;
  subscription_s *s = fio_malloc(sizeof(*s));
  FIO_ASSERT_ALLOC(s);
  *s = (subscription_s){
      .on_message = args.on_message,
      .on_unsubscribe = args.on_unsubscribe,
      .udata1 = args.udata1,
      .udata2 = args.udata2,
      .ref = 1,
      .lock = FIO_LOCK_INIT,
  };
  if (args.filter) {
    ch = fio_filter_dup_lock(args.filter);
  } else if (args.match) {
    ch = fio_channel_match_dup_lock(args.channel, args.match);
  } else {
    ch = fio_channel_dup_lock(args.channel);
  }
  s->parent = ch;
  fio_ls_embd_push(&ch->subscriptions, &s->node);
  fio_unlock((&ch->lock));
  return s;
error:
  if (args.on_unsubscribe)
    args.on_unsubscribe(args.udata1, args.udata2);
  return NULL;
}

/** Unsubscribes from a filter, pub/sub channle or patten */
void fio_unsubscribe(subscription_s *s) {
  if (!s)
    return;
  if (fio_trylock(&s->unsubscribed))
    goto finish;
  fio_lock(&s->lock);
  channel_s *ch = s->parent;
  uint8_t removed = 0;
  fio_lock(&ch->lock);
  fio_ls_embd_remove(&s->node);
  /* check if channel is done for */
  if (fio_ls_embd_is_empty(&ch->subscriptions)) {
    fio_collection_s *c = ch->parent;
    uint64_t hashed = FIO_HASH_FN(
        ch->name, ch->name_len, &fio_postoffice.pubsub, &fio_postoffice.pubsub);
    /* lock collection */
    fio_lock(&c->lock);
    /* test again within lock */
    if (fio_ls_embd_is_empty(&ch->subscriptions)) {
      fio_ch_set_remove(&c->channels, hashed, ch, NULL);
      removed = (c != &fio_postoffice.filters);
    }
    fio_unlock(&c->lock);
  }
  fio_unlock(&ch->lock);
  if (removed) {
    fio_pubsub_on_channel_destroy(ch);
  }

  /* promise the subscription will be inactive */
  s->on_message = NULL;
  fio_unlock(&s->lock);
finish:
  fio_subscription_free(s);
}

/**
 * This helper returns a temporary String with the subscription's channel (or a
 * string representing the filter).
 *
 * To keep the string beyond the lifetime of the subscription, copy the string.
 */
fio_str_info_s fio_subscription_channel(subscription_s *subscription) {
  return (fio_str_info_s){.data = subscription->parent->name,
                          .len = subscription->parent->name_len};
}

/* *****************************************************************************
Engine handling and Management
***************************************************************************** */

/* implemented later, informs root process about pub/sub subscriptions */
static inline void fio_cluster_inform_root_about_channel(channel_s *ch,
                                                         int add);

/* runs in lock(!) let'm all know */
static void fio_pubsub_on_channel_create(channel_s *ch) {
  fio_lock(&fio_postoffice.engines.lock);
  FIO_SET_FOR_LOOP(&fio_postoffice.engines.set, pos) {
    if (!pos->hash)
      continue;
    pos->obj->subscribe(pos->obj,
                        (fio_str_info_s){.data = ch->name, .len = ch->name_len},
                        ch->match);
  }
  fio_unlock(&fio_postoffice.engines.lock);
  fio_cluster_inform_root_about_channel(ch, 1);
}

/* runs in lock(!) let'm all know */
static void fio_pubsub_on_channel_destroy(channel_s *ch) {
  fio_lock(&fio_postoffice.engines.lock);
  FIO_SET_FOR_LOOP(&fio_postoffice.engines.set, pos) {
    if (!pos->hash)
      continue;
    pos->obj->unsubscribe(
        pos->obj, (fio_str_info_s){.data = ch->name, .len = ch->name_len},
        ch->match);
  }
  fio_unlock(&fio_postoffice.engines.lock);
  fio_cluster_inform_root_about_channel(ch, 0);
}

/**
 * Attaches an engine, so it's callback can be called by facil.io.
 *
 * The `subscribe` callback will be called for every existing channel.
 *
 * NOTE: the root (master) process will call `subscribe` for any channel in any
 * process, while all the other processes will call `subscribe` only for their
 * own channels. This allows engines to use the root (master) process as an
 * exclusive subscription process.
 */
void fio_pubsub_attach(fio_pubsub_engine_s *engine) {
  fio_lock(&fio_postoffice.engines.lock);
  fio_engine_set_insert(&fio_postoffice.engines.set, (uintptr_t)engine, engine);
  fio_unlock(&fio_postoffice.engines.lock);
  fio_pubsub_reattach(engine);
}

/** Detaches an engine, so it could be safely destroyed. */
void fio_pubsub_detach(fio_pubsub_engine_s *engine) {
  fio_lock(&fio_postoffice.engines.lock);
  fio_engine_set_remove(&fio_postoffice.engines.set, (uintptr_t)engine, engine,
                        NULL);
  fio_unlock(&fio_postoffice.engines.lock);
}

/** Returns true (1) if the engine is attached to the system. */
int fio_pubsub_is_attached(fio_pubsub_engine_s *engine) {
  fio_pubsub_engine_s *addr;
  fio_lock(&fio_postoffice.engines.lock);
  addr = fio_engine_set_find(&fio_postoffice.engines.set, (uintptr_t)engine,
                             engine);
  fio_unlock(&fio_postoffice.engines.lock);
  return addr != NULL;
}

/**
 * Engines can ask facil.io to call the `subscribe` callback for all active
 * channels.
 *
 * This allows engines that lost their connection to their Pub/Sub service to
 * resubscribe all the currently active channels with the new connection.
 *
 * CAUTION: This is an evented task... try not to free the engine's memory while
 * resubscriptions are under way...
 *
 * NOTE: the root (master) process will call `subscribe` for any channel in any
 * process, while all the other processes will call `subscribe` only for their
 * own channels. This allows engines to use the root (master) process as an
 * exclusive subscription process.
 */
void fio_pubsub_reattach(fio_pubsub_engine_s *eng) {
  fio_lock(&fio_postoffice.pubsub.lock);
  FIO_SET_FOR_LOOP(&fio_postoffice.pubsub.channels, pos) {
    if (!pos->hash)
      continue;
    eng->subscribe(
        eng,
        (fio_str_info_s){.data = pos->obj->name, .len = pos->obj->name_len},
        NULL);
  }
  fio_unlock(&fio_postoffice.pubsub.lock);
  fio_lock(&fio_postoffice.patterns.lock);
  FIO_SET_FOR_LOOP(&fio_postoffice.patterns.channels, pos) {
    if (!pos->hash)
      continue;
    eng->subscribe(
        eng,
        (fio_str_info_s){.data = pos->obj->name, .len = pos->obj->name_len},
        pos->obj->match);
  }
  fio_unlock(&fio_postoffice.patterns.lock);
}

/* *****************************************************************************
 * Message Metadata handling
 **************************************************************************** */

void fio_message_metadata_callback_set(fio_msg_metadata_fn callback,
                                       int enable) {
  if (!callback)
    return;
  fio_lock(&fio_postoffice.meta.lock);
  fio_meta_ary_remove2(&fio_postoffice.meta.ary, callback, NULL);
  if (enable)
    fio_meta_ary_push(&fio_postoffice.meta.ary, callback);
  fio_unlock(&fio_postoffice.meta.lock);
}

/** Finds the message's metadata by it's type ID. */
void *fio_message_metadata(fio_msg_s *msg, intptr_t type_id) {
  fio_msg_metadata_s *meta = ((fio_msg_client_s *)msg)->meta;
  size_t len = ((fio_msg_client_s *)msg)->meta_len;
  while (len) {
    --len;
    if (meta[len].type_id == type_id)
      return meta[len].metadata;
  }
  return NULL;
}

/* *****************************************************************************
 * Publishing to the subsriptions
 **************************************************************************** */

/* common internal tasks */
static channel_s *fio_channel_find_dup_internal(channel_s *ch_tmp,
                                                uint64_t hashed,
                                                fio_collection_s *c) {
  fio_lock(&c->lock);
  channel_s *ch = fio_ch_set_find(&c->channels, hashed, ch_tmp);
  if (!ch) {
    fio_unlock(&c->lock);
    return NULL;
  }
  fio_channel_dup(ch);
  fio_unlock(&c->lock);
  return ch;
}

/** Finds a filter channel, increasing it's reference count if it exists. */
static channel_s *fio_filter_find_dup(uint32_t filter) {
  channel_s tmp = {.name = (char *)(&filter), .name_len = sizeof(filter)};
  channel_s *ch =
      fio_channel_find_dup_internal(&tmp, filter, &fio_postoffice.filters);
  return ch;
}

/** Finds a pubsub channel, increasing it's reference count if it exists. */
static channel_s *fio_channel_find_dup(fio_str_info_s name) {
  channel_s tmp = {.name = name.data, .name_len = name.len};
  uint64_t hashed_name = FIO_HASH_FN(
      name.data, name.len, &fio_postoffice.pubsub, &fio_postoffice.pubsub);
  channel_s *ch =
      fio_channel_find_dup_internal(&tmp, hashed_name, &fio_postoffice.pubsub);
  return ch;
}

/* defers the callback (mark only) */
void fio_message_defer(fio_msg_s *msg_) {
  fio_msg_client_s *cl = (fio_msg_client_s *)msg_;
  cl->marker = 1;
}

/* performs the actual callback */
static void fio_perform_subscription_callback(void *s_, void *msg_) {
  subscription_s *s = s_;
  if (fio_trylock(&s->lock)) {
    fio_defer_push_task(fio_perform_subscription_callback, s_, msg_);
    return;
  }
  fio_msg_internal_s *msg = (fio_msg_internal_s *)msg_;
  fio_msg_client_s m = {
      .msg =
          {
              .channel = msg->channel,
              .msg = msg->data,
              .filter = msg->filter,
              .udata1 = s->udata1,
              .udata2 = s->udata2,
          },
      .meta_len = msg->meta_len,
      .meta = msg->meta,
      .marker = 0,
  };
  if (s->on_message) {
    /* the on_message callback is removed when a subscription is canceled. */
    s->on_message(&m.msg);
  }
  fio_unlock(&s->lock);
  if (m.marker) {
    fio_defer_push_task(fio_perform_subscription_callback, s_, msg_);
    return;
  }
  fio_msg_internal_free(msg);
  fio_subscription_free(s);
}

/** UNSAFE! publishes a message to a channel, managing the reference counts */
static void fio_publish2channel(channel_s *ch, fio_msg_internal_s *msg) {
  FIO_LS_EMBD_FOR(&ch->subscriptions, pos) {
    subscription_s *s = FIO_LS_EMBD_OBJ(subscription_s, node, pos);
    if (!s || s->on_message == fio_mock_on_message) {
      continue;
    }
    fio_atomic_add(&s->ref, 1);
    fio_atomic_add(&msg->ref, 1);
    fio_defer_push_task(fio_perform_subscription_callback, s, msg);
  }
  fio_msg_internal_free(msg);
}
static void fio_publish2channel_task(void *ch_, void *msg) {
  channel_s *ch = ch_;
  if (!ch_)
    return;
  if (!msg)
    goto finish;
  if (fio_trylock(&ch->lock)) {
    fio_defer_push_urgent(fio_publish2channel_task, ch, msg);
    return;
  }
  fio_publish2channel(ch, msg);
  fio_unlock(&ch->lock);
finish:
  fio_channel_free(ch);
}

/** Publishes the message to the current process and frees the strings. */
static void fio_publish2process(fio_msg_internal_s *m) {
  fio_msg_internal_finalize(m);
  channel_s *ch;
  if (m->filter) {
    ch = fio_filter_find_dup(m->filter);
    if (!ch) {
      goto finish;
    }
  } else {
    ch = fio_channel_find_dup(m->channel);
  }
  /* exact match */
  if (ch) {
    fio_defer_push_urgent(fio_publish2channel_task, ch,
                          fio_msg_internal_dup(m));
  }
  if (m->filter == 0) {
    /* pattern matching match */
    fio_lock(&fio_postoffice.patterns.lock);
    FIO_SET_FOR_LOOP(&fio_postoffice.patterns.channels, p) {
      if (!p->hash) {
        continue;
      }

      if (p->obj->match(
              (fio_str_info_s){.data = p->obj->name, .len = p->obj->name_len},
              m->channel)) {
        fio_channel_dup(p->obj);
        fio_defer_push_urgent(fio_publish2channel_task, p->obj,
                              fio_msg_internal_dup(m));
      }
    }
    fio_unlock(&fio_postoffice.patterns.lock);
  }
finish:
  fio_msg_internal_free(m);
}

/* *****************************************************************************
 * Data Structures - Core Structures
 **************************************************************************** */

#define CLUSTER_READ_BUFFER 16384

#define FIO_SET_NAME fio_sub_hash
#define FIO_SET_OBJ_TYPE subscription_s *
#define FIO_SET_KEY_TYPE fio_str_s
#define FIO_SET_KEY_COPY(k1, k2)                                               \
  (k1) = FIO_STR_INIT;                                                         \
  fio_str_concat(&(k1), &(k2))
#define FIO_SET_KEY_COMPARE(k1, k2) fio_str_iseq(&(k1), &(k2))
#define FIO_SET_KEY_DESTROY(key) fio_str_free(&(key))
#define FIO_SET_OBJ_DESTROY(obj) fio_unsubscribe(obj)
#include <fio.h>

#define FIO_CLUSTER_NAME_LIMIT 255

typedef struct cluster_pr_s {
  fio_protocol_s protocol;
  fio_msg_internal_s *msg;
  void (*handler)(struct cluster_pr_s *pr);
  void (*sender)(void *data, intptr_t avoid_uuid);
  fio_sub_hash_s pubsub;
  fio_sub_hash_s patterns;
  intptr_t uuid;
  uint32_t exp_channel;
  uint32_t exp_msg;
  uint32_t type;
  int32_t filter;
  uint32_t length;
  fio_lock_i lock;
  uint8_t buffer[CLUSTER_READ_BUFFER];
} cluster_pr_s;

static struct cluster_data_s {
  intptr_t uuid;
  fio_ls_s clients;
  fio_lock_i lock;
  char name[FIO_CLUSTER_NAME_LIMIT + 1];
} cluster_data = {.clients = FIO_LS_INIT(cluster_data.clients),
                  .lock = FIO_LOCK_INIT};

static void fio_cluster_data_cleanup(int delete_file) {
  if (delete_file && cluster_data.name[0]) {
#if DEBUG
    FIO_LOG_DEBUG("(%d) unlinking cluster's Unix socket.", (int)getpid());
#endif
    unlink(cluster_data.name);
  }
  while (fio_ls_any(&cluster_data.clients)) {
    intptr_t uuid = (intptr_t)fio_ls_pop(&cluster_data.clients);
    if (uuid > 0) {
      fio_close(uuid);
    }
  }
  cluster_data.uuid = 0;
  cluster_data.lock = FIO_LOCK_INIT;
  cluster_data.clients = (fio_ls_s)FIO_LS_INIT(cluster_data.clients);
}

static void fio_cluster_cleanup(void *ignore) {
  /* cleanup the cluster data */
  fio_cluster_data_cleanup(fio_parent_pid() == getpid());
  (void)ignore;
}

static void fio_cluster_init(void) {
  fio_cluster_data_cleanup(0);
  /* create a unique socket name */
  char *tmp_folder = getenv("TMPDIR");
  uint32_t tmp_folder_len = 0;
  if (!tmp_folder || ((tmp_folder_len = (uint32_t)strlen(tmp_folder)) >
                      (FIO_CLUSTER_NAME_LIMIT - 28))) {
#ifdef P_tmpdir
    tmp_folder = (char *)P_tmpdir;
    if (tmp_folder)
      tmp_folder_len = (uint32_t)strlen(tmp_folder);
#else
    tmp_folder = "/tmp/";
    tmp_folder_len = 5;
#endif
  }
  if (tmp_folder_len >= (FIO_CLUSTER_NAME_LIMIT - 28)) {
    tmp_folder_len = 0;
  }
  if (tmp_folder_len) {
    memcpy(cluster_data.name, tmp_folder, tmp_folder_len);
    if (cluster_data.name[tmp_folder_len - 1] != '/')
      cluster_data.name[tmp_folder_len++] = '/';
  }
  memcpy(cluster_data.name + tmp_folder_len, "facil-io-sock-", 14);
  tmp_folder_len += 14;
  tmp_folder_len +=
      snprintf(cluster_data.name + tmp_folder_len,
               FIO_CLUSTER_NAME_LIMIT - tmp_folder_len, "%d", (int)getpid());
  cluster_data.name[tmp_folder_len] = 0;

  /* remove if existing */
  unlink(cluster_data.name);
  /* add cleanup callback */
  fio_state_callback_add(FIO_CALL_AT_EXIT, fio_cluster_cleanup, NULL);
}

/* *****************************************************************************
 * Cluster Protocol callbacks
 **************************************************************************** */

static inline void fio_cluster_protocol_free(void *pr) { fio_free(pr); }

static uint8_t fio_cluster_on_shutdown(intptr_t uuid, fio_protocol_s *pr_) {
  cluster_pr_s *p = (cluster_pr_s *)pr_;
  p->sender(fio_msg_internal_create(0, FIO_CLUSTER_MSG_SHUTDOWN,
                                    (fio_str_info_s){.len = 0},
                                    (fio_str_info_s){.len = 0}, 0, 1),
            -1);
  return 255;
  (void)pr_;
  (void)uuid;
}

static void fio_cluster_on_data(intptr_t uuid, fio_protocol_s *pr_) {
  cluster_pr_s *c = (cluster_pr_s *)pr_;
  ssize_t i =
      fio_read(uuid, c->buffer + c->length, CLUSTER_READ_BUFFER - c->length);
  if (i <= 0)
    return;
  c->length += i;
  i = 0;
  do {
    if (!c->exp_channel && !c->exp_msg) {
      if (c->length - i < 16)
        break;
      c->exp_channel = fio_str2u32(c->buffer + i) + 1;
      c->exp_msg = fio_str2u32(c->buffer + i + 4) + 1;
      c->type = fio_str2u32(c->buffer + i + 8);
      c->filter = (int32_t)fio_str2u32(c->buffer + i + 12);
      if (c->exp_channel) {
        if (c->exp_channel >= (1024 * 1024 * 16) + 1) {
          FIO_LOG_FATAL("(%d) cluster message name too long (16Mb limit): %u\n",
                        (int)getpid(), (unsigned int)c->exp_channel);
          exit(1);
          return;
        }
      }
      if (c->exp_msg) {
        if (c->exp_msg >= (1024 * 1024 * 64) + 1) {
          FIO_LOG_FATAL("(%d) cluster message data too long (64Mb limit): %u\n",
                        (int)getpid(), (unsigned int)c->exp_msg);
          exit(1);
          return;
        }
      }
      c->msg = fio_msg_internal_create(
          c->filter, c->type,
          (fio_str_info_s){.data = (char *)(c->msg + 1),
                           .len = c->exp_channel - 1},
          (fio_str_info_s){.data = ((char *)(c->msg + 1) + c->exp_channel + 1),
                           .len = c->exp_msg - 1},
          (int8_t)(c->type == FIO_CLUSTER_MSG_JSON ||
                   c->type == FIO_CLUSTER_MSG_ROOT_JSON),
          0);
      i += 16;
    }
    if (c->exp_channel) {
      if (c->exp_channel + i > c->length) {
        memcpy(c->msg->channel.data +
                   ((c->msg->channel.len + 1) - c->exp_channel),
               (char *)c->buffer + i, (size_t)(c->length - i));
        c->exp_channel -= (c->length - i);
        i = c->length;
        break;
      } else {
        memcpy(c->msg->channel.data +
                   ((c->msg->channel.len + 1) - c->exp_channel),
               (char *)c->buffer + i, (size_t)(c->exp_channel));
        i += c->exp_channel;
        c->exp_channel = 0;
      }
    }
    if (c->exp_msg) {
      if (c->exp_msg + i > c->length) {
        memcpy(c->msg->data.data + ((c->msg->data.len + 1) - c->exp_msg),
               (char *)c->buffer + i, (size_t)(c->length - i));
        c->exp_msg -= (c->length - i);
        i = c->length;
        break;
      } else {
        memcpy(c->msg->data.data + ((c->msg->data.len + 1) - c->exp_msg),
               (char *)c->buffer + i, (size_t)(c->exp_msg));
        i += c->exp_msg;
        c->exp_msg = 0;
      }
    }
    fio_postoffice_meta_update(c->msg);
    c->handler(c);
    fio_msg_internal_free(c->msg);
    c->msg = NULL;
  } while (c->length > i);
  c->length -= i;
  if (c->length && i) {
    memmove(c->buffer, c->buffer + i, c->length);
  }
  (void)pr_;
}

static void fio_cluster_ping(intptr_t uuid, fio_protocol_s *pr_) {
  fio_msg_internal_s *m = fio_msg_internal_create(
      0, FIO_CLUSTER_MSG_PING, (fio_str_info_s){.len = 0},
      (fio_str_info_s){.len = 0}, 0, 1);
  fio_msg_internal_send_dup(uuid, m);
  fio_msg_internal_free(m);
  (void)pr_;
}

static void fio_cluster_on_close(intptr_t uuid, fio_protocol_s *pr_) {
  cluster_pr_s *c = (cluster_pr_s *)pr_;
  if (!fio_data->is_worker) {
    /* a child was lost, respawning is handled elsewhere. */
    fio_lock(&cluster_data.lock);
    FIO_LS_FOR(&cluster_data.clients, pos) {
      if (pos->obj == (void *)uuid) {
        fio_ls_remove(pos);
        break;
      }
    }
    fio_unlock(&cluster_data.lock);
  } else if (fio_data->active) {
    /* no shutdown message received - parent crashed. */
    if (c->type != FIO_CLUSTER_MSG_SHUTDOWN && fio_is_running()) {
      FIO_LOG_FATAL("(%d) Parent Process crash detected!", (int)getpid());
      fio_state_callback_force(FIO_CALL_ON_PARENT_CRUSH);
      fio_state_callback_clear(FIO_CALL_ON_PARENT_CRUSH);
      fio_cluster_data_cleanup(1);
      kill(getpid(), SIGINT);
    }
  }
  if (c->msg)
    fio_msg_internal_free(c->msg);
  c->msg = NULL;
  fio_sub_hash_free(&c->pubsub);
  fio_cluster_protocol_free(c);
  (void)uuid;
}

static inline fio_protocol_s *
fio_cluster_protocol_alloc(intptr_t uuid,
                           void (*handler)(struct cluster_pr_s *pr),
                           void (*sender)(void *data, intptr_t auuid)) {
  cluster_pr_s *p = fio_mmap(sizeof(*p));
  if (!p) {
    FIO_LOG_FATAL("Cluster protocol allocation failed.");
    exit(errno);
  }
  p->protocol = (fio_protocol_s){
      .ping = fio_cluster_ping,
      .on_close = fio_cluster_on_close,
      .on_shutdown = fio_cluster_on_shutdown,
      .on_data = fio_cluster_on_data,
  };
  p->uuid = uuid;
  p->handler = handler;
  p->sender = sender;
  p->pubsub = (fio_sub_hash_s)FIO_SET_INIT;
  p->patterns = (fio_sub_hash_s)FIO_SET_INIT;
  p->lock = FIO_LOCK_INIT;
  return &p->protocol;
}

/* *****************************************************************************
 * Master (server) IPC Connections
 **************************************************************************** */

static void fio_cluster_server_sender(void *m_, intptr_t avoid_uuid) {
  fio_msg_internal_s *m = m_;
  fio_lock(&cluster_data.lock);
  FIO_LS_FOR(&cluster_data.clients, pos) {
    if ((intptr_t)pos->obj != -1) {
      if ((intptr_t)pos->obj != avoid_uuid) {
        fio_msg_internal_send_dup((intptr_t)pos->obj, m);
      }
    }
  }
  fio_unlock(&cluster_data.lock);
  fio_msg_internal_free(m);
}

static void fio_cluster_server_handler(struct cluster_pr_s *pr) {
  /* what to do? */
  // fprintf(stderr, "-");
  switch ((fio_cluster_message_type_e)pr->type) {

  case FIO_CLUSTER_MSG_FORWARD: /* fallthrough */
  case FIO_CLUSTER_MSG_JSON: {
    fio_cluster_server_sender(fio_msg_internal_dup(pr->msg), pr->uuid);
    fio_publish2process(fio_msg_internal_dup(pr->msg));
    break;
  }

  case FIO_CLUSTER_MSG_PUBSUB_SUB: {
    subscription_s *s =
        fio_subscribe(.on_message = fio_mock_on_message, .match = NULL,
                      .channel = pr->msg->channel);
    fio_str_s tmp = FIO_STR_INIT_EXISTING(
        pr->msg->channel.data, pr->msg->channel.len, 0); // don't free
    fio_lock(&pr->lock);
    fio_sub_hash_insert(&pr->pubsub,
                        FIO_HASH_FN(pr->msg->channel.data, pr->msg->channel.len,
                                    &fio_postoffice.pubsub,
                                    &fio_postoffice.pubsub),
                        tmp, s, NULL);
    fio_unlock(&pr->lock);
    break;
  }
  case FIO_CLUSTER_MSG_PUBSUB_UNSUB: {
    fio_str_s tmp = FIO_STR_INIT_EXISTING(
        pr->msg->channel.data, pr->msg->channel.len, 0); // don't free
    fio_lock(&pr->lock);
    fio_sub_hash_remove(&pr->pubsub,
                        FIO_HASH_FN(pr->msg->channel.data, pr->msg->channel.len,
                                    &fio_postoffice.pubsub,
                                    &fio_postoffice.pubsub),
                        tmp, NULL);
    fio_unlock(&pr->lock);
    break;
  }

  case FIO_CLUSTER_MSG_PATTERN_SUB: {
    uintptr_t match = fio_str2u64(pr->msg->data.data);
    subscription_s *s = fio_subscribe(.on_message = fio_mock_on_message,
                                      .match = (fio_match_fn)match,
                                      .channel = pr->msg->channel);
    fio_str_s tmp = FIO_STR_INIT_EXISTING(
        pr->msg->channel.data, pr->msg->channel.len, 0); // don't free
    fio_lock(&pr->lock);
    fio_sub_hash_insert(&pr->patterns,
                        FIO_HASH_FN(pr->msg->channel.data, pr->msg->channel.len,
                                    &fio_postoffice.pubsub,
                                    &fio_postoffice.pubsub),
                        tmp, s, NULL);
    fio_unlock(&pr->lock);
    break;
  }

  case FIO_CLUSTER_MSG_PATTERN_UNSUB: {
    fio_str_s tmp = FIO_STR_INIT_EXISTING(
        pr->msg->channel.data, pr->msg->channel.len, 0); // don't free
    fio_lock(&pr->lock);
    fio_sub_hash_remove(&pr->patterns,
                        FIO_HASH_FN(pr->msg->channel.data, pr->msg->channel.len,
                                    &fio_postoffice.pubsub,
                                    &fio_postoffice.pubsub),
                        tmp, NULL);
    fio_unlock(&pr->lock);
    break;
  }

  case FIO_CLUSTER_MSG_ROOT_JSON:
    pr->type = FIO_CLUSTER_MSG_JSON; /* fallthrough */
  case FIO_CLUSTER_MSG_ROOT:
    fio_publish2process(fio_msg_internal_dup(pr->msg));
    break;

  case FIO_CLUSTER_MSG_SHUTDOWN: /* fallthrough */
  case FIO_CLUSTER_MSG_ERROR:    /* fallthrough */
  case FIO_CLUSTER_MSG_PING:     /* fallthrough */
  default:
    break;
  }
}

/** Called when a ne client is available */
static void fio_cluster_listen_accept(intptr_t uuid, fio_protocol_s *protocol) {
  (void)protocol;
  /* prevent `accept` backlog in parent */
  intptr_t client;
  while ((client = fio_accept(uuid)) != -1) {
    fio_attach(client,
               fio_cluster_protocol_alloc(client, fio_cluster_server_handler,
                                          fio_cluster_server_sender));
    fio_lock(&cluster_data.lock);
    fio_ls_push(&cluster_data.clients, (void *)client);
    fio_unlock(&cluster_data.lock);
  }
}

/** Called when the connection was closed, but will not run concurrently */
static void fio_cluster_listen_on_close(intptr_t uuid,
                                        fio_protocol_s *protocol) {
  free(protocol);
  cluster_data.uuid = -1;
  if (fio_parent_pid() == getpid()) {
#if DEBUG
    FIO_LOG_DEBUG("(%d) stopped listening for cluster connections",
                  (int)getpid());
#endif
    if (fio_data->active)
      fio_stop();
  }
  (void)uuid;
}

static void fio_listen2cluster(void *ignore) {
  /* this is called for each `fork`, but we only need this to run once. */
  fio_lock(&cluster_data.lock);
  cluster_data.uuid = fio_socket(cluster_data.name, NULL, 1);
  fio_unlock(&cluster_data.lock);
  if (cluster_data.uuid < 0) {
    FIO_LOG_FATAL("(facil.io cluster) failed to open cluster socket.");
    perror("             check file permissions. errno:");
    exit(errno);
  }
  fio_protocol_s *p = malloc(sizeof(*p));
  FIO_ASSERT_ALLOC(p);
  *p = (fio_protocol_s){
      .on_data = fio_cluster_listen_accept,
      .on_shutdown = mock_on_shutdown_eternal,
      .ping = mock_ping_eternal,
      .on_close = fio_cluster_listen_on_close,
  };
  FIO_LOG_DEBUG("(%d) Listening to cluster: %s", (int)getpid(),
                cluster_data.name);
  fio_attach(cluster_data.uuid, p);
  (void)ignore;
}

/* *****************************************************************************
 * Worker (client) IPC connections
 **************************************************************************** */

static void fio_cluster_client_handler(struct cluster_pr_s *pr) {
  /* what to do? */
  switch ((fio_cluster_message_type_e)pr->type) {
  case FIO_CLUSTER_MSG_FORWARD: /* fallthrough */
  case FIO_CLUSTER_MSG_JSON:
    fio_publish2process(fio_msg_internal_dup(pr->msg));
    break;
  case FIO_CLUSTER_MSG_SHUTDOWN:
    fio_stop();
  case FIO_CLUSTER_MSG_ERROR:         /* fallthrough */
  case FIO_CLUSTER_MSG_PING:          /* fallthrough */
  case FIO_CLUSTER_MSG_ROOT:          /* fallthrough */
  case FIO_CLUSTER_MSG_ROOT_JSON:     /* fallthrough */
  case FIO_CLUSTER_MSG_PUBSUB_SUB:    /* fallthrough */
  case FIO_CLUSTER_MSG_PUBSUB_UNSUB:  /* fallthrough */
  case FIO_CLUSTER_MSG_PATTERN_SUB:   /* fallthrough */
  case FIO_CLUSTER_MSG_PATTERN_UNSUB: /* fallthrough */

  default:
    break;
  }
}
static void fio_cluster_client_sender(void *m_, intptr_t ignr_) {
  fio_msg_internal_s *m = m_;
  if (!uuid_is_valid(cluster_data.uuid) && fio_data->active) {
    /* delay message delivery until we have a vaild uuid */
    fio_defer_push_task((void (*)(void *, void *))fio_cluster_client_sender, m_,
                        (void *)ignr_);
    return;
  }
  fio_msg_internal_send_dup(cluster_data.uuid, m);
  fio_msg_internal_free(m);
}

/** The address of the server we are connecting to. */
// char *address;
/** The port on the server we are connecting to. */
// char *port;
/**
 * The `on_connect` callback should return a pointer to a protocol object
 * that will handle any connection related events.
 *
 * Should either call `facil_attach` or close the connection.
 */
static void fio_cluster_on_connect(intptr_t uuid, void *udata) {
  cluster_data.uuid = uuid;

  /* inform root about all existing channels */
  fio_lock(&fio_postoffice.pubsub.lock);
  FIO_SET_FOR_LOOP(&fio_postoffice.pubsub.channels, pos) {
    if (!pos->hash) {
      continue;
    }
    fio_cluster_inform_root_about_channel(pos->obj, 1);
  }
  fio_unlock(&fio_postoffice.pubsub.lock);
  fio_lock(&fio_postoffice.patterns.lock);
  FIO_SET_FOR_LOOP(&fio_postoffice.patterns.channels, pos) {
    if (!pos->hash) {
      continue;
    }
    fio_cluster_inform_root_about_channel(pos->obj, 1);
  }
  fio_unlock(&fio_postoffice.patterns.lock);

  fio_attach(uuid, fio_cluster_protocol_alloc(uuid, fio_cluster_client_handler,
                                              fio_cluster_client_sender));
  (void)udata;
}
/**
 * The `on_fail` is called when a socket fails to connect. The old sock UUID
 * is passed along.
 */
static void fio_cluster_on_fail(intptr_t uuid, void *udata) {
  FIO_LOG_FATAL("(facil.io) unknown cluster connection error");
  perror("       errno");
  kill(fio_parent_pid(), SIGINT);
  fio_stop();
  // exit(errno ? errno : 1);
  (void)udata;
  (void)uuid;
}

static void fio_connect2cluster(void *ignore) {
  if (cluster_data.uuid)
    fio_force_close(cluster_data.uuid);
  cluster_data.uuid = 0;
  /* this is called for each child, but not for single a process worker. */
  fio_connect(.address = cluster_data.name, .port = NULL,
              .on_connect = fio_cluster_on_connect,
              .on_fail = fio_cluster_on_fail);
  (void)ignore;
}

static void fio_send2cluster(fio_msg_internal_s *m) {
  if (!fio_is_running()) {
    FIO_LOG_ERROR("facio.io cluster inactive, can't send message.");
    return;
  }
  if (fio_data->workers == 1) {
    /* nowhere to send to */
    return;
  }
  if (fio_is_master()) {
    fio_cluster_server_sender(fio_msg_internal_dup(m), -1);
  } else {
    fio_cluster_client_sender(fio_msg_internal_dup(m), -1);
  }
}

/* *****************************************************************************
 * Propegation
 **************************************************************************** */

static inline void fio_cluster_inform_root_about_channel(channel_s *ch,
                                                         int add) {
  if (!fio_data->is_worker || fio_data->workers == 1 || !cluster_data.uuid ||
      !ch)
    return;
  fio_str_info_s ch_name = {.data = ch->name, .len = ch->name_len};
  fio_str_info_s msg = {.data = NULL, .len = 0};
#if DEBUG
  FIO_LOG_DEBUG("(%d) informing root about: %s (%zu) msg type %d",
                (int)getpid(), ch_name.data, ch_name.len,
                (ch->match ? (add ? FIO_CLUSTER_MSG_PATTERN_SUB
                                  : FIO_CLUSTER_MSG_PATTERN_UNSUB)
                           : (add ? FIO_CLUSTER_MSG_PUBSUB_SUB
                                  : FIO_CLUSTER_MSG_PUBSUB_UNSUB)));
#endif
  char buf[8] = {0};
  if (ch->match) {
    fio_u2str64(buf, (uint64_t)ch->match);
    msg.data = buf;
    msg.len = sizeof(ch->match);
  }

  fio_cluster_client_sender(
      fio_msg_internal_create(0,
                              (ch->match
                                   ? (add ? FIO_CLUSTER_MSG_PATTERN_SUB
                                          : FIO_CLUSTER_MSG_PATTERN_UNSUB)
                                   : (add ? FIO_CLUSTER_MSG_PUBSUB_SUB
                                          : FIO_CLUSTER_MSG_PUBSUB_UNSUB)),
                              ch_name, msg, 0, 1),
      -1);
}

/* *****************************************************************************
 * Initialization
 **************************************************************************** */

static void fio_accept_after_fork(void *ignore) {
  /* prevent `accept` backlog in parent */
  fio_cluster_listen_accept(cluster_data.uuid, NULL);
  (void)ignore;
}

static void fio_cluster_at_exit(void *ignore) {
  /* unlock all */
  fio_pubsub_on_fork();
  /* clear subscriptions of all types */
  while (fio_ch_set_count(&fio_postoffice.patterns.channels)) {
    channel_s *ch = fio_ch_set_last(&fio_postoffice.patterns.channels);
    while (fio_ls_embd_any(&ch->subscriptions)) {
      subscription_s *sub =
          FIO_LS_EMBD_OBJ(subscription_s, node, ch->subscriptions.next);
      fio_unsubscribe(sub);
    }
    fio_ch_set_pop(&fio_postoffice.patterns.channels);
  }

  while (fio_ch_set_count(&fio_postoffice.pubsub.channels)) {
    channel_s *ch = fio_ch_set_last(&fio_postoffice.pubsub.channels);
    while (fio_ls_embd_any(&ch->subscriptions)) {
      subscription_s *sub =
          FIO_LS_EMBD_OBJ(subscription_s, node, ch->subscriptions.next);
      fio_unsubscribe(sub);
    }
    fio_ch_set_pop(&fio_postoffice.pubsub.channels);
  }

  while (fio_ch_set_count(&fio_postoffice.filters.channels)) {
    channel_s *ch = fio_ch_set_last(&fio_postoffice.filters.channels);
    while (fio_ls_embd_any(&ch->subscriptions)) {
      subscription_s *sub =
          FIO_LS_EMBD_OBJ(subscription_s, node, ch->subscriptions.next);
      fio_unsubscribe(sub);
    }
    fio_ch_set_pop(&fio_postoffice.filters.channels);
  }
  fio_ch_set_free(&fio_postoffice.filters.channels);
  fio_ch_set_free(&fio_postoffice.patterns.channels);
  fio_ch_set_free(&fio_postoffice.pubsub.channels);

  /* clear engines */
  FIO_PUBSUB_DEFAULT = FIO_PUBSUB_CLUSTER;
  while (fio_engine_set_count(&fio_postoffice.engines.set)) {
    fio_pubsub_detach(fio_engine_set_last(&fio_postoffice.engines.set));
    fio_engine_set_last(&fio_postoffice.engines.set);
  }
  fio_engine_set_free(&fio_postoffice.engines.set);

  /* clear meta hooks */
  fio_meta_ary_free(&fio_postoffice.meta.ary);
  /* perform newly created tasks */
  fio_defer_perform();
  (void)ignore;
}

static void fio_pubsub_initialize(void) {
  fio_cluster_init();
  fio_state_callback_add(FIO_CALL_PRE_START, fio_listen2cluster, NULL);
  fio_state_callback_add(FIO_CALL_IN_MASTER, fio_accept_after_fork, NULL);
  fio_state_callback_add(FIO_CALL_IN_CHILD, fio_connect2cluster, NULL);
  fio_state_callback_add(FIO_CALL_ON_FINISH, fio_cluster_cleanup, NULL);
  fio_state_callback_add(FIO_CALL_AT_EXIT, fio_cluster_at_exit, NULL);
}

/* *****************************************************************************
Cluster forking handler
***************************************************************************** */

static void fio_pubsub_on_fork(void) {
  fio_postoffice.filters.lock = FIO_LOCK_INIT;
  fio_postoffice.pubsub.lock = FIO_LOCK_INIT;
  fio_postoffice.patterns.lock = FIO_LOCK_INIT;
  fio_postoffice.engines.lock = FIO_LOCK_INIT;
  fio_postoffice.meta.lock = FIO_LOCK_INIT;
  cluster_data.lock = FIO_LOCK_INIT;
  cluster_data.uuid = 0;
  FIO_SET_FOR_LOOP(&fio_postoffice.filters.channels, pos) {
    if (!pos->hash)
      continue;
    pos->obj->lock = FIO_LOCK_INIT;
    FIO_LS_EMBD_FOR(&pos->obj->subscriptions, n) {
      FIO_LS_EMBD_OBJ(subscription_s, node, n)->lock = FIO_LOCK_INIT;
    }
  }
  FIO_SET_FOR_LOOP(&fio_postoffice.pubsub.channels, pos) {
    if (!pos->hash)
      continue;
    pos->obj->lock = FIO_LOCK_INIT;
    FIO_LS_EMBD_FOR(&pos->obj->subscriptions, n) {
      FIO_LS_EMBD_OBJ(subscription_s, node, n)->lock = FIO_LOCK_INIT;
    }
  }
  FIO_SET_FOR_LOOP(&fio_postoffice.patterns.channels, pos) {
    if (!pos->hash)
      continue;
    pos->obj->lock = FIO_LOCK_INIT;
    FIO_LS_EMBD_FOR(&pos->obj->subscriptions, n) {
      FIO_LS_EMBD_OBJ(subscription_s, node, n)->lock = FIO_LOCK_INIT;
    }
  }
}

/* *****************************************************************************
 * External API
 **************************************************************************** */

/** Signals children (or self) to shutdown) - NOT signal safe. */
static void fio_cluster_signal_children(void) {
  if (fio_parent_pid() != getpid()) {
    fio_stop();
    return;
  }
  fio_cluster_server_sender(fio_msg_internal_create(0, FIO_CLUSTER_MSG_SHUTDOWN,
                                                    (fio_str_info_s){.len = 0},
                                                    (fio_str_info_s){.len = 0},
                                                    0, 1),
                            -1);
}

/* Sublime Text marker */
void fio_publish___(fio_publish_args_s args);
/**
 * Publishes a message to the relevant subscribers (if any).
 *
 * See `facil_publish_args_s` for details.
 *
 * By default the message is sent using the FIO_PUBSUB_CLUSTER engine (all
 * processes, including the calling process).
 *
 * To limit the message only to other processes (exclude the calling process),
 * use the FIO_PUBSUB_SIBLINGS engine.
 *
 * To limit the message only to the calling process, use the
 * FIO_PUBSUB_PROCESS engine.
 *
 * To publish messages to the pub/sub layer, the `.filter` argument MUST be
 * equal to 0 or missing.
 */
void fio_publish FIO_IGNORE_MACRO(fio_publish_args_s args) {
  if (args.filter && !args.engine) {
    args.engine = FIO_PUBSUB_CLUSTER;
  } else if (!args.engine) {
    args.engine = FIO_PUBSUB_DEFAULT;
  }
  fio_msg_internal_s *m = NULL;
  switch ((uintptr_t)args.engine) {
  case 0UL: /* fallthrough (missing default) */
  case 1UL: // ((uintptr_t)FIO_PUBSUB_CLUSTER):
    m = fio_msg_internal_create(
        args.filter,
        (args.is_json ? FIO_CLUSTER_MSG_JSON : FIO_CLUSTER_MSG_FORWARD),
        args.channel, args.message, args.is_json, 1);
    fio_send2cluster(m);
    fio_publish2process(m);
    break;
  case 2UL: // ((uintptr_t)FIO_PUBSUB_PROCESS):
    m = fio_msg_internal_create(args.filter, 0, args.channel, args.message,
                                args.is_json, 1);
    fio_publish2process(m);
    break;
  case 3UL: // ((uintptr_t)FIO_PUBSUB_SIBLINGS):
    m = fio_msg_internal_create(
        args.filter,
        (args.is_json ? FIO_CLUSTER_MSG_JSON : FIO_CLUSTER_MSG_FORWARD),
        args.channel, args.message, args.is_json, 1);
    fio_send2cluster(m);
    fio_msg_internal_free(m);
    m = NULL;
    break;
  case 4UL: // ((uintptr_t)FIO_PUBSUB_ROOT):
    m = fio_msg_internal_create(
        args.filter,
        (args.is_json ? FIO_CLUSTER_MSG_ROOT_JSON : FIO_CLUSTER_MSG_ROOT),
        args.channel, args.message, args.is_json, 1);
    if (fio_data->is_worker == 0 || fio_data->workers == 1) {
      fio_publish2process(m);
    } else {
      fio_cluster_client_sender(m, -1);
    }
    break;
  default:
    if (args.filter != 0) {
      FIO_LOG_ERROR("(pub/sub) pub/sub engines can only be used for "
                    "pub/sub messages (no filter).");
      return;
    }
    args.engine->publish(args.engine, args.channel, args.message, args.is_json);
  }
  return;
}

/* *****************************************************************************
 * Glob Matching
 **************************************************************************** */

/** A binary glob matching helper. Returns 1 on match, otherwise returns 0. */
static int fio_glob_match(fio_str_info_s pat, fio_str_info_s ch) {
  /* adapted and rewritten, with thankfulness, from the code at:
   * https://github.com/opnfv/kvmfornfv/blob/master/kernel/lib/glob.c
   *
   * Original version's copyright:
   * Copyright 2015 Open Platform for NFV Project, Inc. and its contributors
   * Under the MIT license.
   */

  /*
   * Backtrack to previous * on mismatch and retry starting one
   * character later in the string.  Because * matches all characters,
   * there's never a need to backtrack multiple levels.
   */
  uint8_t *back_pat = NULL, *back_str = (uint8_t *)ch.data;
  size_t back_pat_len = 0, back_str_len = ch.len;

  /*
   * Loop over each token (character or class) in pat, matching
   * it against the remaining unmatched tail of str.  Return false
   * on mismatch, or true after matching the trailing nul bytes.
   */
  while (ch.len) {
    uint8_t c = *(uint8_t *)ch.data++;
    uint8_t d = *(uint8_t *)pat.data++;
    ch.len--;
    pat.len--;

    switch (d) {
    case '?': /* Wildcard: anything goes */
      break;

    case '*':       /* Any-length wildcard */
      if (!pat.len) /* Optimize trailing * case */
        return 1;
      back_pat = (uint8_t *)pat.data;
      back_pat_len = pat.len;
      back_str = (uint8_t *)--ch.data; /* Allow zero-length match */
      back_str_len = ++ch.len;
      break;

    case '[': { /* Character class */
      uint8_t match = 0, inverted = (*(uint8_t *)pat.data == '^');
      uint8_t *cls = (uint8_t *)pat.data + inverted;
      uint8_t a = *cls++;

      /*
       * Iterate over each span in the character class.
       * A span is either a single character a, or a
       * range a-b.  The first span may begin with ']'.
       */
      do {
        uint8_t b = a;

        if (cls[0] == '-' && cls[1] != ']') {
          b = cls[1];

          cls += 2;
          if (a > b) {
            uint8_t tmp = a;
            a = b;
            b = tmp;
          }
        }
        match |= (a <= c && c <= b);
      } while ((a = *cls++) != ']');

      if (match == inverted)
        goto backtrack;
      pat.len -= cls - (uint8_t *)pat.data;
      pat.data = (char *)cls;

    } break;
    case '\\':
      d = *(uint8_t *)pat.data++;
      pat.len--;
    /* fallthrough */
    default: /* Literal character */
      if (c == d)
        break;
    backtrack:
      if (!back_pat)
        return 0; /* No point continuing */
      /* Try again from last *, one character later in str. */
      pat.data = (char *)back_pat;
      ch.data = (char *)++back_str;
      ch.len = --back_str_len;
      pat.len = back_pat_len;
    }
  }
  return !ch.len && !pat.len;
}

fio_match_fn FIO_MATCH_GLOB = fio_glob_match;

#else /* FIO_PUBSUB_SUPPORT */

static void fio_pubsub_on_fork(void) {}
static void fio_cluster_init(void) {}
static void fio_cluster_signal_children(void) {}

#endif /* FIO_PUBSUB_SUPPORT */

/* *****************************************************************************
Section Start Marker






















                   Memory Allocator Details & Implementation























***************************************************************************** */

/* *****************************************************************************
Allocator default settings
***************************************************************************** */

/* doun't change these */
#undef FIO_MEMORY_BLOCK_SLICES
#undef FIO_MEMORY_BLOCK_HEADER_SIZE
#undef FIO_MEMORY_BLOCK_START_POS
#undef FIO_MEMORY_MAX_SLICES_PER_BLOCK
#undef FIO_MEMORY_BLOCK_MASK

/* The number of blocks pre-allocated each system call, 256 ==8Mb */
#ifndef FIO_MEMORY_BLOCKS_PER_ALLOCATION
#define FIO_MEMORY_BLOCKS_PER_ALLOCATION 256
#endif

#define FIO_MEMORY_BLOCK_MASK (FIO_MEMORY_BLOCK_SIZE - 1) /* 0b0...1... */

#define FIO_MEMORY_BLOCK_SLICES (FIO_MEMORY_BLOCK_SIZE >> 4) /* 16B slices */

/* must be divisable by 16 bytes, bigger than min(sizeof(block_s), 16) */
#define FIO_MEMORY_BLOCK_HEADER_SIZE 32

/* allocation counter position (start) */
#define FIO_MEMORY_BLOCK_START_POS (FIO_MEMORY_BLOCK_HEADER_SIZE >> 4)

#define FIO_MEMORY_MAX_SLICES_PER_BLOCK                                        \
  (FIO_MEMORY_BLOCK_SLICES - FIO_MEMORY_BLOCK_START_POS)

/* *****************************************************************************
FIO_FORCE_MALLOC handler
***************************************************************************** */

#if FIO_FORCE_MALLOC

void *fio_malloc(size_t size) { return calloc(size, 1); }

void *fio_calloc(size_t size_per_unit, size_t unit_count) {
  return calloc(size_per_unit, unit_count);
}

void fio_free(void *ptr) { free(ptr); }

void *fio_realloc(void *ptr, size_t new_size) {
  return realloc((ptr), (new_size));
}

void *fio_realloc2(void *ptr, size_t new_size, size_t copy_length) {
  return realloc((ptr), (new_size));
  (void)copy_length;
}

void *fio_mmap(size_t size) { return calloc(size, 1); }

void fio_malloc_after_fork(void) {}
void fio_mem_destroy(void) {}
void fio_mem_init(void) {}

#else

/* *****************************************************************************
Memory Copying by 16 byte units
***************************************************************************** */

/** used internally, only when memory addresses are known to be aligned */
static inline void fio_memcpy(void *__restrict dest_, void *__restrict src_,
                              size_t units) {
#if __SIZEOF_INT128__ == 9 /* a 128bit type exists... but tests favor 64bit */
  register __uint128_t *dest = dest_;
  register __uint128_t *src = src_;
#elif SIZE_MAX == 0xFFFFFFFFFFFFFFFF /* 64 bit size_t */
  register size_t *dest = dest_;
  register size_t *src = src_;
  units = units << 1;
#elif SIZE_MAX == 0xFFFFFFFF         /* 32 bit size_t */
  register size_t *dest = dest_;
  register size_t *src = src_;
  units = units << 2;
#else                                /* unknow... assume 16 bit? */
  register size_t *dest = dest_;
  register size_t *src = src_;
  units = units << 3;
#endif
  while (units >= 16) { /* unroll loop */
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = src[3];
    dest[4] = src[4];
    dest[5] = src[5];
    dest[6] = src[6];
    dest[7] = src[7];
    dest[8] = src[8];
    dest[9] = src[9];
    dest[10] = src[10];
    dest[11] = src[11];
    dest[12] = src[12];
    dest[13] = src[13];
    dest[14] = src[14];
    dest[15] = src[15];
    dest += 16;
    src += 16;
    units -= 16;
  }
  switch (units) {
  case 15:
    *(dest++) = *(src++); /* fallthrough */
  case 14:
    *(dest++) = *(src++); /* fallthrough */
  case 13:
    *(dest++) = *(src++); /* fallthrough */
  case 12:
    *(dest++) = *(src++); /* fallthrough */
  case 11:
    *(dest++) = *(src++); /* fallthrough */
  case 10:
    *(dest++) = *(src++); /* fallthrough */
  case 9:
    *(dest++) = *(src++); /* fallthrough */
  case 8:
    *(dest++) = *(src++); /* fallthrough */
  case 7:
    *(dest++) = *(src++); /* fallthrough */
  case 6:
    *(dest++) = *(src++); /* fallthrough */
  case 5:
    *(dest++) = *(src++); /* fallthrough */
  case 4:
    *(dest++) = *(src++); /* fallthrough */
  case 3:
    *(dest++) = *(src++); /* fallthrough */
  case 2:
    *(dest++) = *(src++); /* fallthrough */
  case 1:
    *(dest++) = *(src++);
  }
}

/* *****************************************************************************
System Memory wrappers
***************************************************************************** */

/*
 * allocates memory using `mmap`, but enforces block size alignment.
 * requires page aligned `len`.
 *
 * `align_shift` is used to move the memory page alignment to allow for a single
 * page allocation header. align_shift MUST be either 0 (normal) or 1 (single
 * page header). Other values might cause errors.
 */
static inline void *sys_alloc(size_t len, uint8_t is_indi) {
  void *result;
  static void *next_alloc = NULL;
/* hope for the best? */
#ifdef MAP_ALIGNED
  result =
      mmap(next_alloc, len, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_ALIGNED(FIO_MEMORY_BLOCK_SIZE_LOG),
           -1, 0);
#else
  result = mmap(next_alloc, len, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
  if (result == MAP_FAILED)
    return NULL;
  if (((uintptr_t)result & FIO_MEMORY_BLOCK_MASK)) {
    munmap(result, len);
    result = mmap(NULL, len + FIO_MEMORY_BLOCK_SIZE, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
      return NULL;
    }
    const uintptr_t offset =
        (FIO_MEMORY_BLOCK_SIZE - ((uintptr_t)result & FIO_MEMORY_BLOCK_MASK));
    if (offset) {
      munmap(result, offset);
      result = (void *)((uintptr_t)result + offset);
    }
    munmap((void *)((uintptr_t)result + len), FIO_MEMORY_BLOCK_SIZE - offset);
  }
  if (is_indi ==
      0) /* advance by a block's allocation size for next allocation */
    next_alloc =
        (void *)((uintptr_t)result +
                 (FIO_MEMORY_BLOCK_SIZE * (FIO_MEMORY_BLOCKS_PER_ALLOCATION)));
  else /* add 1TB for realloc */
    next_alloc = (void *)((uintptr_t)result + (is_indi * ((uintptr_t)1 << 30)));
  return result;
}

/* frees memory using `munmap`. requires exact, page aligned, `len` */
static inline void sys_free(void *mem, size_t len) { munmap(mem, len); }

static void *sys_realloc(void *mem, size_t prev_len, size_t new_len) {
  if (new_len > prev_len) {
    void *result;
#if defined(__linux__)
    result = mremap(mem, prev_len, new_len, 0);
    if (result != MAP_FAILED)
      return result;
#endif
    result = mmap((void *)((uintptr_t)mem + prev_len), new_len - prev_len,
                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == (void *)((uintptr_t)mem + prev_len)) {
      result = mem;
    } else {
      /* copy and free */
      munmap(result, new_len - prev_len); /* free the failed attempt */
      result = sys_alloc(new_len, 1);     /* allocate new memory */
      if (!result) {
        return NULL;
      }
      fio_memcpy(result, mem, prev_len >> 4); /* copy data */
      // memcpy(result, mem, prev_len);
      munmap(mem, prev_len); /* free original memory */
    }
    return result;
  }
  if (new_len + 4096 < prev_len) /* more than a single dangling page */
    munmap((void *)((uintptr_t)mem + new_len), prev_len - new_len);
  return mem;
}

/** Rounds up any size to the nearest page alignment (assumes 4096 bytes per
 * page) */
static inline size_t sys_round_size(size_t size) {
  return (size & (~4095)) + (4096 * (!!(size & 4095)));
}

/* *****************************************************************************
Data Types
***************************************************************************** */

/* The basic block header. Starts a 32Kib memory block */
typedef struct block_s block_s;

struct block_s {
  block_s *parent;   /* REQUIRED, root == point to self */
  uint16_t ref;      /* reference count (per memory page) */
  uint16_t pos;      /* position into the block */
  uint16_t max;      /* available memory count */
  uint16_t root_ref; /* root reference memory padding */
};

typedef struct block_node_s block_node_s;
struct block_node_s {
  block_s dont_touch; /* prevent block internal data from being corrupted */
  fio_ls_embd_s node; /* next block */
};

/* a per-CPU core "arena" for memory allocations  */
typedef struct {
  block_s *block;
  fio_lock_i lock;
} arena_s;

/* The memory allocators persistent state */
static struct {
  fio_ls_embd_s available; /* free list for memory blocks */
  // intptr_t count;          /* free list counter */
  size_t cores;    /* the number of detected CPU cores*/
  fio_lock_i lock; /* a global lock */
  uint8_t forked;  /* a forked collection indicator. */
} memory = {
    .cores = 1,
    .lock = FIO_LOCK_INIT,
    .available = FIO_LS_INIT(memory.available),
};

/* The per-CPU arena array. */
static arena_s *arenas;

/* The per-CPU arena array. */
static long double on_malloc_zero;

#if DEBUG
/* The per-CPU arena array. */
static size_t fio_mem_block_count_max;
/* The per-CPU arena array. */
static size_t fio_mem_block_count;
#define FIO_MEMORY_ON_BLOCK_ALLOC()                                            \
  do {                                                                         \
    fio_atomic_add(&fio_mem_block_count, 1);                                   \
    if (fio_mem_block_count > fio_mem_block_count_max)                         \
      fio_mem_block_count_max = fio_mem_block_count;                           \
  } while (0)
#define FIO_MEMORY_ON_BLOCK_FREE()                                             \
  do {                                                                         \
    fio_atomic_sub(&fio_mem_block_count, 1);                                   \
  } while (0)
#define FIO_MEMORY_PRINT_BLOCK_STAT()                                          \
  FIO_LOG_INFO(                                                                \
      "(fio) Total memory blocks allocated before cleanup %zu\n"               \
      "       Maximum memory blocks allocated at a single time %zu\n",         \
      fio_mem_block_count, fio_mem_block_count_max)
#define FIO_MEMORY_PRINT_BLOCK_STAT_END()                                      \
  FIO_LOG_INFO("(fio) Total memory blocks allocated "                          \
               "after cleanup (possible leak) %zu\n",                          \
               fio_mem_block_count)
#else
#define FIO_MEMORY_ON_BLOCK_ALLOC()
#define FIO_MEMORY_ON_BLOCK_FREE()
#define FIO_MEMORY_PRINT_BLOCK_STAT()
#define FIO_MEMORY_PRINT_BLOCK_STAT_END()
#endif
/* *****************************************************************************
Per-CPU Arena management
***************************************************************************** */

/* returned a locked arena. Attempts the preffered arena first. */
static inline arena_s *arena_lock(arena_s *preffered) {
  if (!preffered)
    preffered = arenas;
  if (!fio_trylock(&preffered->lock))
    return preffered;
  do {
    arena_s *arena = preffered;
    for (size_t i = (size_t)(arena - arenas); i < memory.cores; ++i) {
      if ((preffered == arenas || arena != preffered) &&
          !fio_trylock(&arena->lock))
        return arena;
      ++arena;
    }
    if (preffered == arenas)
      fio_reschedule_thread();
    preffered = arenas;
  } while (1);
}

static __thread arena_s *arena_last_used;

static void arena_enter(void) { arena_last_used = arena_lock(arena_last_used); }

static inline void arena_exit(void) { fio_unlock(&arena_last_used->lock); }

/** Clears any memory locks, in case of a system call to `fork`. */
void fio_malloc_after_fork(void) {
  arena_last_used = NULL;
  if (!arenas) {
    return;
  }
  memory.lock = FIO_LOCK_INIT;
  memory.forked = 1;
  for (size_t i = 0; i < memory.cores; ++i) {
    arenas[i].lock = FIO_LOCK_INIT;
  }
}

/* *****************************************************************************
Block management / allocation
***************************************************************************** */

static inline void block_init_root(block_s *blk, block_s *parent) {
  *blk = (block_s){
      .parent = parent,
      .ref = 1,
      .pos = FIO_MEMORY_BLOCK_START_POS,
      .root_ref = 1,
  };
}

/* intializes the block header for an available block of memory. */
static inline void block_init(block_s *blk) {
  /* initialization shouldn't effect `parent` or `root_ref`*/
  blk->ref = 1;
  blk->pos = FIO_MEMORY_BLOCK_START_POS;
  /* zero out linked list memory (everything else is already zero) */
  ((block_node_s *)blk)->node.next = NULL;
  ((block_node_s *)blk)->node.prev = NULL;
  /* bump parent reference count */
  fio_atomic_add(&blk->parent->root_ref, 1);
}

/* intializes the block header for an available block of memory. */
static inline void block_free(block_s *blk) {
  if (fio_atomic_sub(&blk->ref, 1))
    return;

  memset(blk + 1, 0, (FIO_MEMORY_BLOCK_SIZE - sizeof(*blk)));
  fio_lock(&memory.lock);
  fio_ls_embd_push(&memory.available, &((block_node_s *)blk)->node);

  blk = blk->parent;

  if (fio_atomic_sub(&blk->root_ref, 1)) {
    fio_unlock(&memory.lock);
    return;
  }
  // fio_unlock(&memory.lock);
  // return;

  /* remove all of the root block's children (slices) from the memory pool */
  for (size_t i = 0; i < FIO_MEMORY_BLOCKS_PER_ALLOCATION; ++i) {
    block_node_s *pos =
        (block_node_s *)((uintptr_t)blk + (i * FIO_MEMORY_BLOCK_SIZE));
    fio_ls_embd_remove(&pos->node);
  }

  fio_unlock(&memory.lock);
  sys_free(blk, FIO_MEMORY_BLOCK_SIZE * FIO_MEMORY_BLOCKS_PER_ALLOCATION);
  FIO_LOG_DEBUG("memory allocator returned %p to the system", (void *)blk);
  FIO_MEMORY_ON_BLOCK_FREE();
}

/* intializes the block header for an available block of memory. */
static inline block_s *block_new(void) {
  block_s *blk = NULL;

  fio_lock(&memory.lock);
  blk = (block_s *)fio_ls_embd_pop(&memory.available);
  if (blk) {
    blk = (block_s *)FIO_LS_EMBD_OBJ(block_node_s, node, blk);
    FIO_ASSERT(((uintptr_t)blk & FIO_MEMORY_BLOCK_MASK) == 0,
               "Memory allocator error! double `fio_free`?\n");
    block_init(blk); /* must be performed within lock */
    fio_unlock(&memory.lock);
    return blk;
  }
  /* collect memory from the system */
  blk = sys_alloc(FIO_MEMORY_BLOCK_SIZE * FIO_MEMORY_BLOCKS_PER_ALLOCATION, 0);
  if (!blk) {
    fio_unlock(&memory.lock);
    return NULL;
  }
  FIO_LOG_DEBUG("memory allocator allocated %p from the system", (void *)blk);
  FIO_MEMORY_ON_BLOCK_ALLOC();
  block_init_root(blk, blk);
  /* the extra memory goes into the memory pool. initialize + linke-list. */
  block_node_s *tmp = (block_node_s *)blk;
  for (int i = 1; i < FIO_MEMORY_BLOCKS_PER_ALLOCATION; ++i) {
    tmp = (block_node_s *)((uintptr_t)tmp + FIO_MEMORY_BLOCK_SIZE);
    block_init_root((block_s *)tmp, blk);
    fio_ls_embd_push(&memory.available, &tmp->node);
  }
  fio_unlock(&memory.lock);
  /* return the root block (which isn't in the memory pool). */
  return blk;
}

/* allocates memory from within a block - called within an arena's lock */
static inline void *block_slice(uint16_t units) {
  block_s *blk = arena_last_used->block;
  if (!blk) {
    /* arena is empty */
    blk = block_new();
    arena_last_used->block = blk;
  } else if (blk->pos + units > FIO_MEMORY_MAX_SLICES_PER_BLOCK) {
    /* not enough memory in the block - rotate */
    block_free(blk);
    blk = block_new();
    arena_last_used->block = blk;
  }
  if (!blk) {
    /* no system memory available? */
    errno = ENOMEM;
    return NULL;
  }
  /* slice block starting at blk->pos and increase reference count */
  const void *mem = (void *)((uintptr_t)blk + ((uintptr_t)blk->pos << 4));
  fio_atomic_add(&blk->ref, 1);
  blk->pos += units;
  if (blk->pos >= FIO_MEMORY_MAX_SLICES_PER_BLOCK) {
    /* ... the block was fully utilized, clear arena */
    block_free(blk);
    arena_last_used->block = NULL;
  }
  return (void *)mem;
}

/* handle's a bock's reference count - called without a lock */
static inline void block_slice_free(void *mem) {
  /* locate block boundary */
  block_s *blk = (block_s *)((uintptr_t)mem & (~FIO_MEMORY_BLOCK_MASK));
  block_free(blk);
}

/* *****************************************************************************
Non-Block allocations (direct from the system)
***************************************************************************** */

/* allocates directly from the system adding size header - no lock required. */
static inline void *big_alloc(size_t size) {
  size = sys_round_size(size + 16);
  size_t *mem = sys_alloc(size, 1);
  if (!mem)
    goto error;
  *mem = size;
  return (void *)(((uintptr_t)mem) + 16);
error:
  return NULL;
}

/* reads size header and frees memory back to the system */
static inline void big_free(void *ptr) {
  size_t *mem = (void *)(((uintptr_t)ptr) - 16);
  sys_free(mem, *mem);
}

/* reallocates memory using the system, resetting the size header */
static inline void *big_realloc(void *ptr, size_t new_size) {
  size_t *mem = (void *)(((uintptr_t)ptr) - 16);
  new_size = sys_round_size(new_size + 16);
  mem = sys_realloc(mem, *mem, new_size);
  if (!mem)
    goto error;
  *mem = new_size;
  return (void *)(((uintptr_t)mem) + 16);
error:
  return NULL;
}

/* *****************************************************************************
Allocator Initialization (initialize arenas and allocate a block for each CPU)
***************************************************************************** */

#if DEBUG
void fio_memory_dump_missing(void) {
  fprintf(stderr, "\n ==== Attempting Memory Dump (will crash) ====\n");
  if (fio_ls_embd_is_empty(&memory.available)) {
    fprintf(stderr, "- Memory dump attempt canceled\n");
    return;
  }
  block_node_s *smallest =
      FIO_LS_EMBD_OBJ(block_node_s, node, memory.available.next);
  FIO_LS_EMBD_FOR(&memory.available, node) {
    block_node_s *tmp = FIO_LS_EMBD_OBJ(block_node_s, node, node);
    if (smallest > tmp)
      smallest = tmp;
  }

  for (size_t i = 0;
       i < FIO_MEMORY_BLOCK_SIZE * FIO_MEMORY_BLOCKS_PER_ALLOCATION; ++i) {
    if ((((uintptr_t)smallest + i) & FIO_MEMORY_BLOCK_MASK) == 0) {
      i += 32;
      fprintf(stderr, "---block jump---\n");
      continue;
    }
    if (((char *)smallest)[i])
      fprintf(stderr, "%c", ((char *)smallest)[i]);
  }
}
#else
#define fio_memory_dump_missing()
#endif

static void fio_mem_init(void) {
  if (arenas)
    return;

  ssize_t cpu_count = 0;
#ifdef _SC_NPROCESSORS_ONLN
  cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#else
#warning Dynamic CPU core count is unavailable - assuming 8 cores for memory allocation pools.
#endif
  if (cpu_count <= 0)
    cpu_count = 8;
  memory.cores = cpu_count;
  arenas = big_alloc(sizeof(*arenas) * cpu_count);
  FIO_ASSERT_ALLOC(arenas);
  block_free(block_new());
  pthread_atfork(NULL, NULL, fio_malloc_after_fork);
}

static void fio_mem_destroy(void) {
  if (!arenas)
    return;

  FIO_MEMORY_PRINT_BLOCK_STAT();

  for (size_t i = 0; i < memory.cores; ++i) {
    if (arenas[i].block)
      block_free(arenas[i].block);
    arenas[i].block = NULL;
  }
  if (!memory.forked && fio_ls_embd_any(&memory.available)) {
    FIO_LOG_WARNING("facil.io detected memory traces remaining after cleanup"
                    " - memory leak?");
    FIO_MEMORY_PRINT_BLOCK_STAT_END();
    size_t count = 0;
    FIO_LS_EMBD_FOR(&memory.available, node) { ++count; }
    FIO_LOG_DEBUG("Memory blocks in pool: %zu (%zu blocks per allocation).",
                  count, (size_t)FIO_MEMORY_BLOCKS_PER_ALLOCATION);
#if FIO_MEM_DUMP
    fio_memory_dump_missing();
#endif
  }
  big_free(arenas);
  arenas = NULL;
}
/* *****************************************************************************
Memory allocation / deacclocation API
***************************************************************************** */

void *fio_malloc(size_t size) {
#if FIO_OVERRIDE_MALLOC
  if (!arenas)
    fio_mem_init();
#endif
  if (!size) {
    /* changed behavior prevents "allocation failed" test for `malloc(0)` */
    return (void *)(&on_malloc_zero);
  }
  if (size >= FIO_MEMORY_BLOCK_ALLOC_LIMIT) {
    /* system allocation - must be block aligned */
    // FIO_LOG_WARNING("fio_malloc re-routed to mmap - big allocation");
    return big_alloc(size);
  }
  /* ceiling for 16 byte alignement, translated to 16 byte units */
  size = (size >> 4) + (!!(size & 15));
  arena_enter();
  void *mem = block_slice(size);
  arena_exit();
  return mem;
}

void *fio_calloc(size_t size, size_t count) {
  return fio_malloc(size * count); // memory is pre-initialized by mmap or pool.
}

void fio_free(void *ptr) {
  if (!ptr || ptr == (void *)&on_malloc_zero)
    return;
  if (((uintptr_t)ptr & FIO_MEMORY_BLOCK_MASK) == 16) {
    /* big allocation - direct from the system */
    big_free(ptr);
    return;
  }
  /* allocated within block */
  block_slice_free(ptr);
}

/**
 * Re-allocates memory. An attept to avoid copying the data is made only for big
 * memory allocations.
 *
 * This variation is slightly faster as it might copy less data
 */
void *fio_realloc2(void *ptr, size_t new_size, size_t copy_length) {
  if (!ptr || ptr == (void *)&on_malloc_zero) {
    return fio_malloc(new_size);
  }
  if (!new_size) {
    goto zero_size;
  }
  if (((uintptr_t)ptr & FIO_MEMORY_BLOCK_MASK) == 16) {
    /* big reallocation - direct from the system */
    return big_realloc(ptr, new_size);
  }
  /* allocated within block - don't even try to expand the allocation */
  /* ceiling for 16 byte alignement, translated to 16 byte units */
  void *new_mem = fio_malloc(new_size);
  if (!new_mem)
    return NULL;
  new_size = ((new_size >> 4) + (!!(new_size & 15)));
  copy_length = ((copy_length >> 4) + (!!(copy_length & 15)));
  fio_memcpy(new_mem, ptr, copy_length > new_size ? new_size : copy_length);

  block_slice_free(ptr);
  return new_mem;
zero_size:
  fio_free(ptr);
  return fio_malloc(0);
}

void *fio_realloc(void *ptr, size_t new_size) {
  const size_t max_old =
      FIO_MEMORY_BLOCK_SIZE - ((uintptr_t)ptr & FIO_MEMORY_BLOCK_MASK);
  return fio_realloc2(ptr, new_size, max_old);
}

/**
 * Allocates memory directly using `mmap`, this is prefered for larger objects
 * that have a long lifetime.
 *
 * `fio_free` can be used for deallocating the memory.
 */
void *fio_mmap(size_t size) {
  if (!size) {
    return NULL;
  }
  return big_alloc(size);
}

/* *****************************************************************************
FIO_OVERRIDE_MALLOC - override glibc / library malloc
***************************************************************************** */
#if FIO_OVERRIDE_MALLOC
void *malloc(size_t size) { return fio_malloc(size); }
void *calloc(size_t size, size_t count) { return fio_calloc(size, count); }
void free(void *ptr) { fio_free(ptr); }
void *realloc(void *ptr, size_t new_size) { return fio_realloc(ptr, new_size); }
#endif

#endif

/* *****************************************************************************







                      Random Generator Functions







***************************************************************************** */

/* tested for randomness using code from: http://xoshiro.di.unimi.it/hwd.php */
uint64_t fio_rand64(void) {
  /* modeled after xoroshiro128+, by David Blackman and Sebastiano Vigna */
  static __thread uint64_t s[2]; /* random state */
  static __thread uint16_t c;    /* seed counter */
  const uint64_t P[] = {0x37701261ED6C16C7ULL, 0x764DBBB75F3B3E0DULL};
  if (c++ == 0) {
    /* re-seed state every 65,536 requests */
#ifdef RUSAGE_SELF
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
    s[0] = fio_risky_hash(&rusage, sizeof(rusage), s[0]);
    s[1] = fio_risky_hash(&rusage, sizeof(rusage), s[0]);
#else
    struct timespec clk;
    clock_gettime(CLOCK_REALTIME, &clk);
    s[0] = fio_risky_hash(&clk, sizeof(clk), s[0]);
    s[1] = fio_risky_hash(&clk, sizeof(clk), s[0]);
#endif
  }
  s[0] += fio_lrot64(s[0], 33) * P[0];
  s[1] += fio_lrot64(s[1], 33) * P[1];
  return fio_lrot64(s[0], 31) + fio_lrot64(s[1], 29);
}

/* copies 64 bits of randomness (8 bytes) repeatedly... */
void fio_rand_bytes(void *data_, size_t len) {
  if (!data_ || !len)
    return;
  uint8_t *data = data_;
  /* unroll 32 bytes / 256 bit writes */
  for (size_t i = (len >> 5); i; --i) {
    const uint64_t t0 = fio_rand64();
    const uint64_t t1 = fio_rand64();
    const uint64_t t2 = fio_rand64();
    const uint64_t t3 = fio_rand64();
    fio_u2str64(data, t0);
    fio_u2str64(data + 8, t1);
    fio_u2str64(data + 16, t2);
    fio_u2str64(data + 24, t3);
    data += 32;
  }
  uint64_t tmp;
  /* 64 bit steps  */
  switch (len & 24) {
  case 24:
    tmp = fio_rand64();
    fio_u2str64(data + 16, tmp);
    /* fallthrough */
  case 16:
    tmp = fio_rand64();
    fio_u2str64(data + 8, tmp);
    /* fallthrough */
  case 8:
    tmp = fio_rand64();
    fio_u2str64(data, tmp);
    data += len & 24;
  }
  if ((len & 7)) {
    tmp = fio_rand64();
    /* leftover bytes */
    switch ((len & 7)) {
    case 7:
      data[6] = (tmp >> 8) & 0xFF;
      /* fallthrough */
    case 6:
      data[5] = (tmp >> 16) & 0xFF;
      /* fallthrough */
    case 5:
      data[4] = (tmp >> 24) & 0xFF;
      /* fallthrough */
    case 4:
      data[3] = (tmp >> 32) & 0xFF;
      /* fallthrough */
    case 3:
      data[2] = (tmp >> 40) & 0xFF;
      /* fallthrough */
    case 2:
      data[1] = (tmp >> 48) & 0xFF;
      /* fallthrough */
    case 1:
      data[0] = (tmp >> 56) & 0xFF;
    }
  }
}

/* *****************************************************************************
Section Start Marker












                             Hash Functions and Base64

                  SipHash / SHA-1 / SHA-2 / Base64 / Hex encoding













***************************************************************************** */

/* *****************************************************************************
SipHash
***************************************************************************** */

#if __BIG_ENDIAN__ /* SipHash is Little Endian */
#define sip_local64(i) fio_bswap64((i))
#else
#define sip_local64(i) (i)
#endif

static inline uint64_t fio_siphash_xy(const void *data, size_t len, size_t x,
                                      size_t y, uint64_t key1, uint64_t key2) {
  /* initialize the 4 words */
  uint64_t v0 = (0x0706050403020100ULL ^ 0x736f6d6570736575ULL) ^ key1;
  uint64_t v1 = (0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL) ^ key2;
  uint64_t v2 = (0x0706050403020100ULL ^ 0x6c7967656e657261ULL) ^ key1;
  uint64_t v3 = (0x0f0e0d0c0b0a0908ULL ^ 0x7465646279746573ULL) ^ key2;
  const uint8_t *w8 = data;
  uint8_t len_mod = len & 255;
  union {
    uint64_t i;
    uint8_t str[8];
  } word;

#define hash_map_SipRound                                                      \
  do {                                                                         \
    v2 += v3;                                                                  \
    v3 = fio_lrot64(v3, 16) ^ v2;                                              \
    v0 += v1;                                                                  \
    v1 = fio_lrot64(v1, 13) ^ v0;                                              \
    v0 = fio_lrot64(v0, 32);                                                   \
    v2 += v1;                                                                  \
    v0 += v3;                                                                  \
    v1 = fio_lrot64(v1, 17) ^ v2;                                              \
    v3 = fio_lrot64(v3, 21) ^ v0;                                              \
    v2 = fio_lrot64(v2, 32);                                                   \
  } while (0);

  while (len >= 8) {
    word.i = sip_local64(fio_str2u64(w8));
    v3 ^= word.i;
    /* Sip Rounds */
    for (size_t i = 0; i < x; ++i) {
      hash_map_SipRound;
    }
    v0 ^= word.i;
    w8 += 8;
    len -= 8;
  }
  word.i = 0;
  uint8_t *pos = word.str;
  switch (len) { /* fallthrough is intentional */
  case 7:
    pos[6] = w8[6];
    /* fallthrough */
  case 6:
    pos[5] = w8[5];
    /* fallthrough */
  case 5:
    pos[4] = w8[4];
    /* fallthrough */
  case 4:
    pos[3] = w8[3];
    /* fallthrough */
  case 3:
    pos[2] = w8[2];
    /* fallthrough */
  case 2:
    pos[1] = w8[1];
    /* fallthrough */
  case 1:
    pos[0] = w8[0];
  }
  word.str[7] = len_mod;

  /* last round */
  v3 ^= word.i;
  hash_map_SipRound;
  hash_map_SipRound;
  v0 ^= word.i;
  /* Finalization */
  v2 ^= 0xff;
  /* d iterations of SipRound */
  for (size_t i = 0; i < y; ++i) {
    hash_map_SipRound;
  }
  hash_map_SipRound;
  hash_map_SipRound;
  hash_map_SipRound;
  hash_map_SipRound;
  /* XOR it all together */
  v0 ^= v1 ^ v2 ^ v3;
#undef hash_map_SipRound
  return v0;
}

uint64_t fio_siphash24(const void *data, size_t len, uint64_t key1,
                       uint64_t key2) {
  return fio_siphash_xy(data, len, 2, 4, key1, key2);
}

uint64_t fio_siphash13(const void *data, size_t len, uint64_t key1,
                       uint64_t key2) {
  return fio_siphash_xy(data, len, 1, 3, key1, key2);
}

/* *****************************************************************************
SHA-1
***************************************************************************** */

static const uint8_t sha1_padding[64] = {0x80, 0};

/**
Process the buffer once full.
*/
static inline void fio_sha1_perform_all_rounds(fio_sha1_s *s,
                                               const uint8_t *buffer) {
  /* collect data */
  uint32_t a = s->digest.i[0];
  uint32_t b = s->digest.i[1];
  uint32_t c = s->digest.i[2];
  uint32_t d = s->digest.i[3];
  uint32_t e = s->digest.i[4];
  uint32_t t, w[16];
  /* copy data to words, performing byte swapping as needed */
  w[0] = fio_str2u32(buffer);
  w[1] = fio_str2u32(buffer + 4);
  w[2] = fio_str2u32(buffer + 8);
  w[3] = fio_str2u32(buffer + 12);
  w[4] = fio_str2u32(buffer + 16);
  w[5] = fio_str2u32(buffer + 20);
  w[6] = fio_str2u32(buffer + 24);
  w[7] = fio_str2u32(buffer + 28);
  w[8] = fio_str2u32(buffer + 32);
  w[9] = fio_str2u32(buffer + 36);
  w[10] = fio_str2u32(buffer + 40);
  w[11] = fio_str2u32(buffer + 44);
  w[12] = fio_str2u32(buffer + 48);
  w[13] = fio_str2u32(buffer + 52);
  w[14] = fio_str2u32(buffer + 56);
  w[15] = fio_str2u32(buffer + 60);
  /* perform rounds */
#undef perform_single_round
#define perform_single_round(num)                                              \
  t = fio_lrot32(a, 5) + e + w[num] + ((b & c) | ((~b) & d)) + 0x5A827999;     \
  e = d;                                                                       \
  d = c;                                                                       \
  c = fio_lrot32(b, 30);                                                       \
  b = a;                                                                       \
  a = t;

#define perform_four_rounds(i)                                                 \
  perform_single_round(i);                                                     \
  perform_single_round(i + 1);                                                 \
  perform_single_round(i + 2);                                                 \
  perform_single_round(i + 3);

  perform_four_rounds(0);
  perform_four_rounds(4);
  perform_four_rounds(8);
  perform_four_rounds(12);

#undef perform_single_round
#define perform_single_round(i)                                                \
  w[(i)&15] = fio_lrot32((w[(i - 3) & 15] ^ w[(i - 8) & 15] ^                  \
                          w[(i - 14) & 15] ^ w[(i - 16) & 15]),                \
                         1);                                                   \
  t = fio_lrot32(a, 5) + e + w[(i)&15] + ((b & c) | ((~b) & d)) + 0x5A827999;  \
  e = d;                                                                       \
  d = c;                                                                       \
  c = fio_lrot32(b, 30);                                                       \
  b = a;                                                                       \
  a = t;

  perform_four_rounds(16);

#undef perform_single_round
#define perform_single_round(i)                                                \
  w[(i)&15] = fio_lrot32((w[(i - 3) & 15] ^ w[(i - 8) & 15] ^                  \
                          w[(i - 14) & 15] ^ w[(i - 16) & 15]),                \
                         1);                                                   \
  t = fio_lrot32(a, 5) + e + w[(i)&15] + (b ^ c ^ d) + 0x6ED9EBA1;             \
  e = d;                                                                       \
  d = c;                                                                       \
  c = fio_lrot32(b, 30);                                                       \
  b = a;                                                                       \
  a = t;

  perform_four_rounds(20);
  perform_four_rounds(24);
  perform_four_rounds(28);
  perform_four_rounds(32);
  perform_four_rounds(36);

#undef perform_single_round
#define perform_single_round(i)                                                \
  w[(i)&15] = fio_lrot32((w[(i - 3) & 15] ^ w[(i - 8) & 15] ^                  \
                          w[(i - 14) & 15] ^ w[(i - 16) & 15]),                \
                         1);                                                   \
  t = fio_lrot32(a, 5) + e + w[(i)&15] + ((b & (c | d)) | (c & d)) +           \
      0x8F1BBCDC;                                                              \
  e = d;                                                                       \
  d = c;                                                                       \
  c = fio_lrot32(b, 30);                                                       \
  b = a;                                                                       \
  a = t;

  perform_four_rounds(40);
  perform_four_rounds(44);
  perform_four_rounds(48);
  perform_four_rounds(52);
  perform_four_rounds(56);
#undef perform_single_round
#define perform_single_round(i)                                                \
  w[(i)&15] = fio_lrot32((w[(i - 3) & 15] ^ w[(i - 8) & 15] ^                  \
                          w[(i - 14) & 15] ^ w[(i - 16) & 15]),                \
                         1);                                                   \
  t = fio_lrot32(a, 5) + e + w[(i)&15] + (b ^ c ^ d) + 0xCA62C1D6;             \
  e = d;                                                                       \
  d = c;                                                                       \
  c = fio_lrot32(b, 30);                                                       \
  b = a;                                                                       \
  a = t;
  perform_four_rounds(60);
  perform_four_rounds(64);
  perform_four_rounds(68);
  perform_four_rounds(72);
  perform_four_rounds(76);

  /* store data */
  s->digest.i[4] += e;
  s->digest.i[3] += d;
  s->digest.i[2] += c;
  s->digest.i[1] += b;
  s->digest.i[0] += a;
}

/**
Initialize or reset the `sha1` object. This must be performed before hashing
data using sha1.
*/
fio_sha1_s fio_sha1_init(void) {
  return (fio_sha1_s){.digest.i[0] = 0x67452301,
                      .digest.i[1] = 0xefcdab89,
                      .digest.i[2] = 0x98badcfe,
                      .digest.i[3] = 0x10325476,
                      .digest.i[4] = 0xc3d2e1f0};
}

/**
Writes data to the sha1 buffer.
*/
void fio_sha1_write(fio_sha1_s *s, const void *data, size_t len) {
  size_t in_buffer = s->length & 63;
  size_t partial = 64 - in_buffer;
  s->length += len;
  if (partial > len) {
    memcpy(s->buffer + in_buffer, data, len);
    return;
  }
  if (in_buffer) {
    memcpy(s->buffer + in_buffer, data, partial);
    len -= partial;
    data = (void *)((uintptr_t)data + partial);
    fio_sha1_perform_all_rounds(s, s->buffer);
  }
  while (len >= 64) {
    fio_sha1_perform_all_rounds(s, data);
    data = (void *)((uintptr_t)data + 64);
    len -= 64;
  }
  if (len) {
    memcpy(s->buffer + in_buffer, data, len);
  }
  return;
}

char *fio_sha1_result(fio_sha1_s *s) {
  size_t in_buffer = s->length & 63;
  if (in_buffer > 55) {
    memcpy(s->buffer + in_buffer, sha1_padding, 64 - in_buffer);
    fio_sha1_perform_all_rounds(s, s->buffer);
    memcpy(s->buffer, sha1_padding + 1, 56);
  } else if (in_buffer != 55) {
    memcpy(s->buffer + in_buffer, sha1_padding, 56 - in_buffer);
  } else {
    s->buffer[55] = sha1_padding[0];
  }
  /* store the length in BITS - alignment should be promised by struct */
  /* this must the number in BITS, encoded as a BIG ENDIAN 64 bit number */
  uint64_t *len = (uint64_t *)(s->buffer + 56);
  *len = s->length << 3;
  *len = fio_lton64(*len);
  fio_sha1_perform_all_rounds(s, s->buffer);

  /* change back to little endian */
  s->digest.i[0] = fio_ntol32(s->digest.i[0]);
  s->digest.i[1] = fio_ntol32(s->digest.i[1]);
  s->digest.i[2] = fio_ntol32(s->digest.i[2]);
  s->digest.i[3] = fio_ntol32(s->digest.i[3]);
  s->digest.i[4] = fio_ntol32(s->digest.i[4]);

  return (char *)s->digest.str;
}

#undef perform_single_round

/* *****************************************************************************
SHA-2
***************************************************************************** */

static const uint8_t sha2_padding[128] = {0x80, 0};

/* SHA-224 and SHA-256 constants */
static uint32_t sha2_256_words[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

/* SHA-512 and friends constants */
static uint64_t sha2_512_words[] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f,
    0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
    0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242,
    0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
    0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275,
    0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
    0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f,
    0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc,
    0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
    0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6,
    0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
    0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99,
    0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
    0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc,
    0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915,
    0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
    0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba,
    0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
    0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
    0x5fcb6fab3ad6faec, 0x6c44198c4a475817};

/* Specific Macros for the SHA-2 processing */

#define Ch(x, y, z) (((x) & (y)) ^ ((~(x)) & z))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define Eps0_32(x)                                                             \
  (fio_rrot32((x), 2) ^ fio_rrot32((x), 13) ^ fio_rrot32((x), 22))
#define Eps1_32(x)                                                             \
  (fio_rrot32((x), 6) ^ fio_rrot32((x), 11) ^ fio_rrot32((x), 25))
#define Omg0_32(x) (fio_rrot32((x), 7) ^ fio_rrot32((x), 18) ^ (((x) >> 3)))
#define Omg1_32(x) (fio_rrot32((x), 17) ^ fio_rrot32((x), 19) ^ (((x) >> 10)))

#define Eps0_64(x)                                                             \
  (fio_rrot64((x), 28) ^ fio_rrot64((x), 34) ^ fio_rrot64((x), 39))
#define Eps1_64(x)                                                             \
  (fio_rrot64((x), 14) ^ fio_rrot64((x), 18) ^ fio_rrot64((x), 41))
#define Omg0_64(x) (fio_rrot64((x), 1) ^ fio_rrot64((x), 8) ^ (((x) >> 7)))
#define Omg1_64(x) (fio_rrot64((x), 19) ^ fio_rrot64((x), 61) ^ (((x) >> 6)))

/**
Process the buffer once full.
*/
static inline void fio_sha2_perform_all_rounds(fio_sha2_s *s,
                                               const uint8_t *data) {
  if (s->type & 1) { /* 512 derived type */
    // process values for the 64bit words
    uint64_t a = s->digest.i64[0];
    uint64_t b = s->digest.i64[1];
    uint64_t c = s->digest.i64[2];
    uint64_t d = s->digest.i64[3];
    uint64_t e = s->digest.i64[4];
    uint64_t f = s->digest.i64[5];
    uint64_t g = s->digest.i64[6];
    uint64_t h = s->digest.i64[7];
    uint64_t t1, t2, w[80];
    w[0] = fio_str2u64(data);
    w[1] = fio_str2u64(data + 8);
    w[2] = fio_str2u64(data + 16);
    w[3] = fio_str2u64(data + 24);
    w[4] = fio_str2u64(data + 32);
    w[5] = fio_str2u64(data + 40);
    w[6] = fio_str2u64(data + 48);
    w[7] = fio_str2u64(data + 56);
    w[8] = fio_str2u64(data + 64);
    w[9] = fio_str2u64(data + 72);
    w[10] = fio_str2u64(data + 80);
    w[11] = fio_str2u64(data + 88);
    w[12] = fio_str2u64(data + 96);
    w[13] = fio_str2u64(data + 104);
    w[14] = fio_str2u64(data + 112);
    w[15] = fio_str2u64(data + 120);

#undef perform_single_round
#define perform_single_round(i)                                                \
  t1 = h + Eps1_64(e) + Ch(e, f, g) + sha2_512_words[i] + w[i];                \
  t2 = Eps0_64(a) + Maj(a, b, c);                                              \
  h = g;                                                                       \
  g = f;                                                                       \
  f = e;                                                                       \
  e = d + t1;                                                                  \
  d = c;                                                                       \
  c = b;                                                                       \
  b = a;                                                                       \
  a = t1 + t2;

#define perform_4rounds(i)                                                     \
  perform_single_round(i);                                                     \
  perform_single_round(i + 1);                                                 \
  perform_single_round(i + 2);                                                 \
  perform_single_round(i + 3);

    perform_4rounds(0);
    perform_4rounds(4);
    perform_4rounds(8);
    perform_4rounds(12);

#undef perform_single_round
#define perform_single_round(i)                                                \
  w[i] = Omg1_64(w[i - 2]) + w[i - 7] + Omg0_64(w[i - 15]) + w[i - 16];        \
  t1 = h + Eps1_64(e) + Ch(e, f, g) + sha2_512_words[i] + w[i];                \
  t2 = Eps0_64(a) + Maj(a, b, c);                                              \
  h = g;                                                                       \
  g = f;                                                                       \
  f = e;                                                                       \
  e = d + t1;                                                                  \
  d = c;                                                                       \
  c = b;                                                                       \
  b = a;                                                                       \
  a = t1 + t2;

    perform_4rounds(16);
    perform_4rounds(20);
    perform_4rounds(24);
    perform_4rounds(28);
    perform_4rounds(32);
    perform_4rounds(36);
    perform_4rounds(40);
    perform_4rounds(44);
    perform_4rounds(48);
    perform_4rounds(52);
    perform_4rounds(56);
    perform_4rounds(60);
    perform_4rounds(64);
    perform_4rounds(68);
    perform_4rounds(72);
    perform_4rounds(76);

    s->digest.i64[0] += a;
    s->digest.i64[1] += b;
    s->digest.i64[2] += c;
    s->digest.i64[3] += d;
    s->digest.i64[4] += e;
    s->digest.i64[5] += f;
    s->digest.i64[6] += g;
    s->digest.i64[7] += h;
    return;
  } else {
    // process values for the 32bit words
    uint32_t a = s->digest.i32[0];
    uint32_t b = s->digest.i32[1];
    uint32_t c = s->digest.i32[2];
    uint32_t d = s->digest.i32[3];
    uint32_t e = s->digest.i32[4];
    uint32_t f = s->digest.i32[5];
    uint32_t g = s->digest.i32[6];
    uint32_t h = s->digest.i32[7];
    uint32_t t1, t2, w[64];

    w[0] = fio_str2u32(data);
    w[1] = fio_str2u32(data + 4);
    w[2] = fio_str2u32(data + 8);
    w[3] = fio_str2u32(data + 12);
    w[4] = fio_str2u32(data + 16);
    w[5] = fio_str2u32(data + 20);
    w[6] = fio_str2u32(data + 24);
    w[7] = fio_str2u32(data + 28);
    w[8] = fio_str2u32(data + 32);
    w[9] = fio_str2u32(data + 36);
    w[10] = fio_str2u32(data + 40);
    w[11] = fio_str2u32(data + 44);
    w[12] = fio_str2u32(data + 48);
    w[13] = fio_str2u32(data + 52);
    w[14] = fio_str2u32(data + 56);
    w[15] = fio_str2u32(data + 60);

#undef perform_single_round
#define perform_single_round(i)                                                \
  t1 = h + Eps1_32(e) + Ch(e, f, g) + sha2_256_words[i] + w[i];                \
  t2 = Eps0_32(a) + Maj(a, b, c);                                              \
  h = g;                                                                       \
  g = f;                                                                       \
  f = e;                                                                       \
  e = d + t1;                                                                  \
  d = c;                                                                       \
  c = b;                                                                       \
  b = a;                                                                       \
  a = t1 + t2;

    perform_4rounds(0);
    perform_4rounds(4);
    perform_4rounds(8);
    perform_4rounds(12);

#undef perform_single_round
#define perform_single_round(i)                                                \
  w[i] = Omg1_32(w[i - 2]) + w[i - 7] + Omg0_32(w[i - 15]) + w[i - 16];        \
  t1 = h + Eps1_32(e) + Ch(e, f, g) + sha2_256_words[i] + w[i];                \
  t2 = Eps0_32(a) + Maj(a, b, c);                                              \
  h = g;                                                                       \
  g = f;                                                                       \
  f = e;                                                                       \
  e = d + t1;                                                                  \
  d = c;                                                                       \
  c = b;                                                                       \
  b = a;                                                                       \
  a = t1 + t2;

    perform_4rounds(16);
    perform_4rounds(20);
    perform_4rounds(24);
    perform_4rounds(28);
    perform_4rounds(32);
    perform_4rounds(36);
    perform_4rounds(40);
    perform_4rounds(44);
    perform_4rounds(48);
    perform_4rounds(52);
    perform_4rounds(56);
    perform_4rounds(60);

    s->digest.i32[0] += a;
    s->digest.i32[1] += b;
    s->digest.i32[2] += c;
    s->digest.i32[3] += d;
    s->digest.i32[4] += e;
    s->digest.i32[5] += f;
    s->digest.i32[6] += g;
    s->digest.i32[7] += h;
  }
}

/**
Initialize/reset the SHA-2 object.

SHA-2 is actually a family of functions with different variants. When
initializing the SHA-2 container, you must select the variant you intend to
apply. The following are valid options (see the fio_sha2_variant_e enum):

- SHA_512 (== 0)
- SHA_384
- SHA_512_224
- SHA_512_256
- SHA_256
- SHA_224

*/
fio_sha2_s fio_sha2_init(fio_sha2_variant_e variant) {
  if (variant == SHA_256) {
    return (fio_sha2_s){
        .type = SHA_256,
        .digest.i32[0] = 0x6a09e667,
        .digest.i32[1] = 0xbb67ae85,
        .digest.i32[2] = 0x3c6ef372,
        .digest.i32[3] = 0xa54ff53a,
        .digest.i32[4] = 0x510e527f,
        .digest.i32[5] = 0x9b05688c,
        .digest.i32[6] = 0x1f83d9ab,
        .digest.i32[7] = 0x5be0cd19,
    };
  } else if (variant == SHA_384) {
    return (fio_sha2_s){
        .type = SHA_384,
        .digest.i64[0] = 0xcbbb9d5dc1059ed8,
        .digest.i64[1] = 0x629a292a367cd507,
        .digest.i64[2] = 0x9159015a3070dd17,
        .digest.i64[3] = 0x152fecd8f70e5939,
        .digest.i64[4] = 0x67332667ffc00b31,
        .digest.i64[5] = 0x8eb44a8768581511,
        .digest.i64[6] = 0xdb0c2e0d64f98fa7,
        .digest.i64[7] = 0x47b5481dbefa4fa4,
    };
  } else if (variant == SHA_512) {
    return (fio_sha2_s){
        .type = SHA_512,
        .digest.i64[0] = 0x6a09e667f3bcc908,
        .digest.i64[1] = 0xbb67ae8584caa73b,
        .digest.i64[2] = 0x3c6ef372fe94f82b,
        .digest.i64[3] = 0xa54ff53a5f1d36f1,
        .digest.i64[4] = 0x510e527fade682d1,
        .digest.i64[5] = 0x9b05688c2b3e6c1f,
        .digest.i64[6] = 0x1f83d9abfb41bd6b,
        .digest.i64[7] = 0x5be0cd19137e2179,
    };
  } else if (variant == SHA_224) {
    return (fio_sha2_s){
        .type = SHA_224,
        .digest.i32[0] = 0xc1059ed8,
        .digest.i32[1] = 0x367cd507,
        .digest.i32[2] = 0x3070dd17,
        .digest.i32[3] = 0xf70e5939,
        .digest.i32[4] = 0xffc00b31,
        .digest.i32[5] = 0x68581511,
        .digest.i32[6] = 0x64f98fa7,
        .digest.i32[7] = 0xbefa4fa4,
    };
  } else if (variant == SHA_512_224) {
    return (fio_sha2_s){
        .type = SHA_512_224,
        .digest.i64[0] = 0x8c3d37c819544da2,
        .digest.i64[1] = 0x73e1996689dcd4d6,
        .digest.i64[2] = 0x1dfab7ae32ff9c82,
        .digest.i64[3] = 0x679dd514582f9fcf,
        .digest.i64[4] = 0x0f6d2b697bd44da8,
        .digest.i64[5] = 0x77e36f7304c48942,
        .digest.i64[6] = 0x3f9d85a86a1d36c8,
        .digest.i64[7] = 0x1112e6ad91d692a1,
    };
  } else if (variant == SHA_512_256) {
    return (fio_sha2_s){
        .type = SHA_512_256,
        .digest.i64[0] = 0x22312194fc2bf72c,
        .digest.i64[1] = 0x9f555fa3c84c64c2,
        .digest.i64[2] = 0x2393b86b6f53b151,
        .digest.i64[3] = 0x963877195940eabd,
        .digest.i64[4] = 0x96283ee2a88effe3,
        .digest.i64[5] = 0xbe5e1e2553863992,
        .digest.i64[6] = 0x2b0199fc2c85b8aa,
        .digest.i64[7] = 0x0eb72ddc81c52ca2,
    };
  }
  FIO_LOG_FATAL("SHA-2 ERROR - variant unknown");
  exit(2);
}

/**
Writes data to the SHA-2 buffer.
*/
void fio_sha2_write(fio_sha2_s *s, const void *data, size_t len) {
  size_t in_buffer;
  size_t partial;
  if (s->type & 1) { /* 512 type derived */
    in_buffer = s->length.words[0] & 127;
    if (s->length.words[0] + len < s->length.words[0]) {
      /* we are at wraping around the 64bit limit */
      s->length.words[1] = (s->length.words[1] << 1) | 1;
    }
    s->length.words[0] += len;
    partial = 128 - in_buffer;

    if (partial > len) {
      memcpy(s->buffer + in_buffer, data, len);
      return;
    }
    if (in_buffer) {
      memcpy(s->buffer + in_buffer, data, partial);
      len -= partial;
      data = (void *)((uintptr_t)data + partial);
      fio_sha2_perform_all_rounds(s, s->buffer);
    }
    while (len >= 128) {
      fio_sha2_perform_all_rounds(s, data);
      data = (void *)((uintptr_t)data + 128);
      len -= 128;
    }
    if (len) {
      memcpy(s->buffer + in_buffer, data, len);
    }
    return;
  }
  /* else... NOT 512 bits derived (64bit base) */

  in_buffer = s->length.words[0] & 63;
  partial = 64 - in_buffer;

  s->length.words[0] += len;

  if (partial > len) {
    memcpy(s->buffer + in_buffer, data, len);
    return;
  }
  if (in_buffer) {
    memcpy(s->buffer + in_buffer, data, partial);
    len -= partial;
    data = (void *)((uintptr_t)data + partial);
    fio_sha2_perform_all_rounds(s, s->buffer);
  }
  while (len >= 64) {
    fio_sha2_perform_all_rounds(s, data);
    data = (void *)((uintptr_t)data + 64);
    len -= 64;
  }
  if (len) {
    memcpy(s->buffer + in_buffer, data, len);
  }
  return;
}

/**
Finalizes the SHA-2 hash, returning the Hashed data.

`sha2_result` can be called for the same object multiple times, but the
finalization will only be performed the first time this function is called.
*/
char *fio_sha2_result(fio_sha2_s *s) {
  if (s->type & 1) {
    /* 512 bits derived hashing */

    size_t in_buffer = s->length.words[0] & 127;

    if (in_buffer > 111) {
      memcpy(s->buffer + in_buffer, sha2_padding, 128 - in_buffer);
      fio_sha2_perform_all_rounds(s, s->buffer);
      memcpy(s->buffer, sha2_padding + 1, 112);
    } else if (in_buffer != 111) {
      memcpy(s->buffer + in_buffer, sha2_padding, 112 - in_buffer);
    } else {
      s->buffer[111] = sha2_padding[0];
    }
    /* store the length in BITS - alignment should be promised by struct */
    /* this must the number in BITS, encoded as a BIG ENDIAN 64 bit number */

    s->length.words[1] = (s->length.words[1] << 3) | (s->length.words[0] >> 61);
    s->length.words[0] = s->length.words[0] << 3;
    s->length.words[0] = fio_lton64(s->length.words[0]);
    s->length.words[1] = fio_lton64(s->length.words[1]);

#if !__BIG_ENDIAN__
    {
      uint_fast64_t tmp = s->length.words[0];
      s->length.words[0] = s->length.words[1];
      s->length.words[1] = tmp;
    }
#endif

    uint64_t *len = (uint64_t *)(s->buffer + 112);
    len[0] = s->length.words[0];
    len[1] = s->length.words[1];
    fio_sha2_perform_all_rounds(s, s->buffer);

    /* change back to little endian */
    s->digest.i64[0] = fio_ntol64(s->digest.i64[0]);
    s->digest.i64[1] = fio_ntol64(s->digest.i64[1]);
    s->digest.i64[2] = fio_ntol64(s->digest.i64[2]);
    s->digest.i64[3] = fio_ntol64(s->digest.i64[3]);
    s->digest.i64[4] = fio_ntol64(s->digest.i64[4]);
    s->digest.i64[5] = fio_ntol64(s->digest.i64[5]);
    s->digest.i64[6] = fio_ntol64(s->digest.i64[6]);
    s->digest.i64[7] = fio_ntol64(s->digest.i64[7]);
    // set NULL bytes for SHA-2 Type
    switch (s->type) {
    case SHA_512_224:
      s->digest.str[28] = 0;
      break;
    case SHA_512_256:
      s->digest.str[32] = 0;
      break;
    case SHA_384:
      s->digest.str[48] = 0;
      break;
    default:
      s->digest.str[64] =
          0; /* sometimes the optimizer messes the NUL sequence */
      break;
    }
    // fprintf(stderr, "result requested, in hex, is:");
    // for (size_t i = 0; i < ((s->type & 1) ? 64 : 32); i++)
    //   fprintf(stderr, "%02x", (unsigned int)(s->digest.str[i] & 0xFF));
    // fprintf(stderr, "\r\n");
    return (char *)s->digest.str;
  }

  size_t in_buffer = s->length.words[0] & 63;
  if (in_buffer > 55) {
    memcpy(s->buffer + in_buffer, sha2_padding, 64 - in_buffer);
    fio_sha2_perform_all_rounds(s, s->buffer);
    memcpy(s->buffer, sha2_padding + 1, 56);
  } else if (in_buffer != 55) {
    memcpy(s->buffer + in_buffer, sha2_padding, 56 - in_buffer);
  } else {
    s->buffer[55] = sha2_padding[0];
  }
  /* store the length in BITS - alignment should be promised by struct */
  /* this must the number in BITS, encoded as a BIG ENDIAN 64 bit number */
  uint64_t *len = (uint64_t *)(s->buffer + 56);
  *len = s->length.words[0] << 3;
  *len = fio_lton64(*len);
  fio_sha2_perform_all_rounds(s, s->buffer);

  /* change back to little endian, if required */

  s->digest.i32[0] = fio_ntol32(s->digest.i32[0]);
  s->digest.i32[1] = fio_ntol32(s->digest.i32[1]);
  s->digest.i32[2] = fio_ntol32(s->digest.i32[2]);
  s->digest.i32[3] = fio_ntol32(s->digest.i32[3]);
  s->digest.i32[4] = fio_ntol32(s->digest.i32[4]);
  s->digest.i32[5] = fio_ntol32(s->digest.i32[5]);
  s->digest.i32[6] = fio_ntol32(s->digest.i32[6]);
  s->digest.i32[7] = fio_ntol32(s->digest.i32[7]);

  // set NULL bytes for SHA_224
  if (s->type == SHA_224)
    s->digest.str[28] = 0;
  // fprintf(stderr, "SHA-2 result requested, in hex, is:");
  // for (size_t i = 0; i < ((s->type & 1) ? 64 : 32); i++)
  //   fprintf(stderr, "%02x", (unsigned int)(s->digest.str[i] & 0xFF));
  // fprintf(stderr, "\r\n");
  return (char *)s->digest.str;
}

#undef perform_single_round

/* ****************************************************************************
Base64 encoding
***************************************************************************** */

/** the base64 encoding array */
static const char base64_encodes_original[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

/** the base64 encoding array */
static const char base64_encodes_url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=";

/**
A base64 decoding array.

Generation script (Ruby):

a = []; a[255] = 0
s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=".bytes;
s.length.times {|i| a[s[i]] = i };
s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,".bytes;
s.length.times {|i| a[s[i]] = i };
s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_".bytes;
s.length.times {|i| a[s[i]] = i }; a.map!{ |i| i.to_i }; a

*/
static unsigned base64_decodes[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  62, 63, 62, 0,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 0,  0,  0,  64, 0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,
    63, 0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,
};
#define BITVAL(x) (base64_decodes[(x)] & 63)

/*
 * The actual encoding logic. The map can be switched for encoding variations.
 */
static inline int fio_base64_encode_internal(char *target, const char *data,
                                             int len,
                                             const char *base64_encodes) {
  /* walk backwards, allowing fo inplace decoding (target == data) */
  int groups = len / 3;
  const int mod = len - (groups * 3);
  const int target_size = (groups + (mod != 0)) * 4;
  char *writer = target + target_size - 1;
  const char *reader = data + len - 1;
  writer[1] = 0;
  switch (mod) {
  case 2: {
    char tmp2 = *(reader--);
    char tmp1 = *(reader--);
    *(writer--) = '=';
    *(writer--) = base64_encodes[((tmp2 & 15) << 2)];
    *(writer--) = base64_encodes[((tmp1 & 3) << 4) | ((tmp2 >> 4) & 15)];
    *(writer--) = base64_encodes[(tmp1 >> 2) & 63];
  } break;
  case 1: {
    char tmp1 = *(reader--);
    *(writer--) = '=';
    *(writer--) = '=';
    *(writer--) = base64_encodes[(tmp1 & 3) << 4];
    *(writer--) = base64_encodes[(tmp1 >> 2) & 63];
  } break;
  }
  while (groups) {
    groups--;
    const char tmp3 = *(reader--);
    const char tmp2 = *(reader--);
    const char tmp1 = *(reader--);
    *(writer--) = base64_encodes[tmp3 & 63];
    *(writer--) = base64_encodes[((tmp2 & 15) << 2) | ((tmp3 >> 6) & 3)];
    *(writer--) = base64_encodes[(((tmp1 & 3) << 4) | ((tmp2 >> 4) & 15))];
    *(writer--) = base64_encodes[(tmp1 >> 2) & 63];
  }
  return target_size;
}

/**
This will encode a byte array (data) of a specified length (len) and
place the encoded data into the target byte buffer (target). The target buffer
MUST have enough room for the expected data.

Base64 encoding always requires 4 bytes for each 3 bytes. Padding is added if
the raw data's length isn't devisable by 3.

Always assume the target buffer should have room enough for (len*4/3 + 4)
bytes.

Returns the number of bytes actually written to the target buffer
(including the Base64 required padding and excluding a NULL terminator).

A NULL terminator char is NOT written to the target buffer.
*/
int fio_base64_encode(char *target, const char *data, int len) {
  return fio_base64_encode_internal(target, data, len, base64_encodes_original);
}

/**
Same as fio_base64_encode, but using Base64URL encoding.
*/
int fio_base64url_encode(char *target, const char *data, int len) {
  return fio_base64_encode_internal(target, data, len, base64_encodes_url);
}

/**
This will decode a Base64 encoded string of a specified length (len) and
place the decoded data into the target byte buffer (target).

The target buffer MUST have enough room for the expected data.

A NULL byte will be appended to the target buffer. The function will return
the number of bytes written to the target buffer.

If the target buffer is NULL, the encoded string will be destructively edited
and the decoded data will be placed in the original string's buffer.

Base64 encoding always requires 4 bytes for each 3 bytes. Padding is added if
the raw data's length isn't devisable by 3. Hence, the target buffer should
be, at least, `base64_len/4*3 + 3` long.

Returns the number of bytes actually written to the target buffer (excluding
the NULL terminator byte).

If an error occurred, returns the number of bytes written up to the error. Test
`errno` for error (will be set to ERANGE).
*/
int fio_base64_decode(char *target, char *encoded, int base64_len) {
  if (!target)
    target = encoded;
  if (base64_len <= 0) {
    target[0] = 0;
    return 0;
  }
  int written = 0;
  uint8_t tmp1, tmp2, tmp3, tmp4;
  // skip unknown data at end
  while (base64_len &&
         !base64_decodes[*(uint8_t *)(encoded + (base64_len - 1))]) {
    base64_len--;
  }
  // skip white space
  while (base64_len && isspace((*(uint8_t *)encoded))) {
    base64_len--;
    encoded++;
  }
  while (base64_len >= 4) {
    if (!base64_len) {
      return written;
    }
    tmp1 = *(uint8_t *)(encoded++);
    tmp2 = *(uint8_t *)(encoded++);
    tmp3 = *(uint8_t *)(encoded++);
    tmp4 = *(uint8_t *)(encoded++);
    if (!base64_decodes[tmp1] || !base64_decodes[tmp2] ||
        !base64_decodes[tmp3] || !base64_decodes[tmp4]) {
      errno = ERANGE;
      goto finish;
    }
    *(target++) = (BITVAL(tmp1) << 2) | (BITVAL(tmp2) >> 4);
    *(target++) = (BITVAL(tmp2) << 4) | (BITVAL(tmp3) >> 2);
    *(target++) = (BITVAL(tmp3) << 6) | (BITVAL(tmp4));
    // make sure we don't loop forever.
    base64_len -= 4;
    // count written bytes
    written += 3;
    // skip white space
    while (base64_len && isspace((*encoded))) {
      base64_len--;
      encoded++;
    }
  }
  // deal with the "tail" of the mis-encoded stream - this shouldn't happen
  tmp1 = 0;
  tmp2 = 0;
  tmp3 = 0;
  tmp4 = 0;
  switch (base64_len) {
  case 1:
    tmp1 = *(uint8_t *)(encoded++);
    if (!base64_decodes[tmp1]) {
      errno = ERANGE;
      goto finish;
    }
    *(target++) = BITVAL(tmp1);
    written += 1;
    break;
  case 2:
    tmp1 = *(uint8_t *)(encoded++);
    tmp2 = *(uint8_t *)(encoded++);
    if (!base64_decodes[tmp1] || !base64_decodes[tmp2]) {
      errno = ERANGE;
      goto finish;
    }
    *(target++) = (BITVAL(tmp1) << 2) | (BITVAL(tmp2) >> 6);
    *(target++) = (BITVAL(tmp2) << 4);
    written += 2;
    break;
  case 3:
    tmp1 = *(uint8_t *)(encoded++);
    tmp2 = *(uint8_t *)(encoded++);
    tmp3 = *(uint8_t *)(encoded++);
    if (!base64_decodes[tmp1] || !base64_decodes[tmp2] ||
        !base64_decodes[tmp3]) {
      errno = ERANGE;
      goto finish;
    }
    *(target++) = (BITVAL(tmp1) << 2) | (BITVAL(tmp2) >> 6);
    *(target++) = (BITVAL(tmp2) << 4) | (BITVAL(tmp3) >> 2);
    *(target++) = BITVAL(tmp3) << 6;
    written += 3;
    break;
  }
finish:
  if (encoded[-1] == '=') {
    target--;
    written--;
    if (encoded[-2] == '=') {
      target--;
      written--;
    }
    if (written < 0)
      written = 0;
  }
  *target = 0;
  return written;
}

/* *****************************************************************************
Section Start Marker





























                                     Testing






























***************************************************************************** */

#if DEBUG

// clang-format off
#if defined(HAVE_OPENSSL)
#  include <openssl/sha.h>
#endif
// clang-format on

/* *****************************************************************************
Testing Linked Lists
***************************************************************************** */

#define FIO_LLIST_TEST_LIMIT 1016

/**
 * Tests linked list functionality.
 */
#ifndef H_FIO_LINKED_LIST_H
#define fio_llist_test()
#else
FIO_FUNC inline void fio_llist_test(void) {
  fio_ls_s list = FIO_LS_INIT(list);
  size_t counter;
  fprintf(stderr, "=== Testing Core Linked List features (fio_ls and "
                  "fio_ls_embs functions)\n");
  /* test push/shift */
  for (uintptr_t i = 0; i < FIO_LLIST_TEST_LIMIT; ++i) {
    fio_ls_push(&list, (void *)i);
  }
  FIO_ASSERT(fio_ls_any(&list), "List should be populated after fio_ls_push");
  counter = 0;
  FIO_LS_FOR(&list, pos) {
    FIO_ASSERT((size_t)pos->obj == counter,
               "`FIO_LS_FOR` value error (%zu != %zu)", (size_t)pos->obj,
               counter);
    ++counter;
  }
  counter = 0;
  while (fio_ls_any(&list)) {
    FIO_ASSERT(counter < FIO_LLIST_TEST_LIMIT,
               "`fio_ls_any` didn't return false when expected %p<=%p=>%p",
               (void *)list.prev, (void *)&list, (void *)list.next);
    size_t tmp = (size_t)fio_ls_shift(&list);
    FIO_ASSERT(tmp == counter, "`fio_ls_shift` value error (%zu != %zu)", tmp,
               counter);
    ++counter;
  }
  FIO_ASSERT(counter == FIO_LLIST_TEST_LIMIT,
             "List item count error (%zu != %zu)", counter,
             (size_t)FIO_LLIST_TEST_LIMIT);
  /* test unshift/pop */
  for (uintptr_t i = 0; i < FIO_LLIST_TEST_LIMIT; ++i) {
    fio_ls_unshift(&list, (void *)i);
  }
  FIO_ASSERT(fio_ls_any(&list),
             "List should be populated after fio_ls_unshift");
  counter = 0;
  while (!fio_ls_is_empty(&list)) {
    FIO_ASSERT(counter < FIO_LLIST_TEST_LIMIT,
               "`fio_ls_is_empty` didn't return true when expected %p<=%p=>%p",
               (void *)list.prev, (void *)&list, (void *)list.next);
    size_t tmp = (size_t)fio_ls_pop(&list);
    FIO_ASSERT(tmp == counter, "`fio_ls_pop` value error (%zu != %zu)", tmp,
               counter);
    ++counter;
  }
  FIO_ASSERT(counter == FIO_LLIST_TEST_LIMIT,
             "List item count error (%zu != %zu)", counter,
             (size_t)FIO_LLIST_TEST_LIMIT);

  /* Re-test for embeded list */

  struct fio_ls_test_s {
    size_t i;
    fio_ls_embd_s node;
  };

  fio_ls_embd_s emlist = FIO_LS_INIT(emlist);

  /* test push/shift */
  for (uintptr_t i = 0; i < FIO_LLIST_TEST_LIMIT; ++i) {
    struct fio_ls_test_s *n = malloc(sizeof(*n));
    FIO_ASSERT_ALLOC(n);
    n->i = i;
    fio_ls_embd_push(&emlist, &n->node);
    FIO_ASSERT(FIO_LS_EMBD_OBJ(struct fio_ls_test_s, node, emlist.next)->i == 0,
               "fio_ls_embd_push should push to the end.");
  }
  FIO_ASSERT(fio_ls_embd_any(&emlist),
             "List should be populated after fio_ls_embd_push");
  counter = 0;
  FIO_LS_EMBD_FOR(&emlist, pos) {
    FIO_ASSERT(FIO_LS_EMBD_OBJ(struct fio_ls_test_s, node, pos)->i == counter,
               "`FIO_LS_EMBD_FOR` value error (%zu != %zu)",
               FIO_LS_EMBD_OBJ(struct fio_ls_test_s, node, pos)->i, counter);
    ++counter;
  }
  counter = 0;
  while (fio_ls_embd_any(&emlist)) {
    FIO_ASSERT(counter < FIO_LLIST_TEST_LIMIT,
               "`fio_ls_embd_any` didn't return false when expected %p<=%p=>%p",
               (void *)emlist.prev, (void *)&emlist, (void *)emlist.next);
    struct fio_ls_test_s *n =
        FIO_LS_EMBD_OBJ(struct fio_ls_test_s, node, fio_ls_embd_shift(&emlist));
    FIO_ASSERT(n->i == counter, "`fio_ls_embd_shift` value error (%zu != %zu)",
               n->i, counter);
    free(n);
    ++counter;
  }
  FIO_ASSERT(counter == FIO_LLIST_TEST_LIMIT,
             "List item count error (%zu != %zu)", counter,
             (size_t)FIO_LLIST_TEST_LIMIT);
  /* test shift/unshift */
  for (uintptr_t i = 0; i < FIO_LLIST_TEST_LIMIT; ++i) {
    struct fio_ls_test_s *n = malloc(sizeof(*n));
    FIO_ASSERT_ALLOC(n)
    n->i = i;
    fio_ls_embd_unshift(&emlist, &n->node);
    FIO_ASSERT(FIO_LS_EMBD_OBJ(struct fio_ls_test_s, node, emlist.next)->i == i,
               "fio_ls_embd_unshift should push to the start.");
  }
  FIO_ASSERT(fio_ls_embd_any(&emlist),
             "List should be populated after fio_ls_embd_unshift");
  counter = 0;
  while (!fio_ls_embd_is_empty(&emlist)) {
    FIO_ASSERT(
        counter < FIO_LLIST_TEST_LIMIT,
        "`fio_ls_embd_is_empty` didn't return true when expected %p<=%p=>%p",
        (void *)emlist.prev, (void *)&emlist, (void *)emlist.next);
    struct fio_ls_test_s *n =
        FIO_LS_EMBD_OBJ(struct fio_ls_test_s, node, fio_ls_embd_pop(&emlist));
    FIO_ASSERT(n->i == counter, "`fio_ls_embd_pop` value error (%zu != %zu)",
               n->i, counter);
    free(n);
    ++counter;
  }
  FIO_ASSERT(counter == FIO_LLIST_TEST_LIMIT,
             "List item count error (%zu != %zu)", counter,
             (size_t)FIO_LLIST_TEST_LIMIT);
  fprintf(stderr, "* passed.\n");
}
#endif

/* *****************************************************************************
Testing Strings
***************************************************************************** */

#ifndef H_FIO_STR_H
#define fio_str_test()
#else

static int fio_str_test_dealloc_counter = 0;

FIO_FUNC void fio_str_test_dealloc(void *s) {
  FIO_ASSERT(!fio_str_test_dealloc_counter,
             "fio_str_s reference count error!\n");
  fio_free(s);
  fprintf(stderr, "* reference counting `fio_str_free2` pass.\n");
}

/**
 * Tests the fio_str functionality.
 */
FIO_FUNC inline void fio_str_test(void) {
#define ROUND_UP_CAPA_2WORDS(num)                                              \
  (((num + 1) & (sizeof(long double) - 1))                                     \
       ? ((num + 1) | (sizeof(long double) - 1))                               \
       : (num))
  fprintf(stderr, "=== Testing Core String features (fio_str_s functions)\n");
  fprintf(stderr, "* String container size: %zu\n", sizeof(fio_str_s));
  fprintf(stderr,
          "* Self-Contained String Capacity (FIO_STR_SMALL_CAPA): %zu\n",
          FIO_STR_SMALL_CAPA);
  fio_str_s str = {.small = 0}; /* test zeroed out memory */
  FIO_ASSERT(fio_str_capa(&str) == FIO_STR_SMALL_CAPA - 1,
             "Small String capacity reporting error!");
  FIO_ASSERT(fio_str_len(&str) == 0, "Small String length reporting error!");
  FIO_ASSERT(fio_str_data(&str) ==
                 (char *)((uintptr_t)(&str + 1) - FIO_STR_SMALL_CAPA),
             "Small String pointer reporting error (%zd offset)!",
             (ssize_t)(((char *)((uintptr_t)(&str + 1) - FIO_STR_SMALL_CAPA)) -
                       fio_str_data(&str)));
  fio_str_write(&str, "World", 4);
  FIO_ASSERT(str.small,
             "Small String writing error - not small on small write!");
  FIO_ASSERT(fio_str_capa(&str) == FIO_STR_SMALL_CAPA - 1,
             "Small String capacity reporting error after write!");
  FIO_ASSERT(fio_str_len(&str) == 4,
             "Small String length reporting error after write!");
  FIO_ASSERT(fio_str_data(&str) ==
                 (char *)((uintptr_t)(&str + 1) - FIO_STR_SMALL_CAPA),
             "Small String pointer reporting error after write!");
  FIO_ASSERT(strlen(fio_str_data(&str)) == 4,
             "Small String NUL missing after write (%zu)!",
             strlen(fio_str_data(&str)));
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Worl"),
             "Small String write error (%s)!", fio_str_data(&str));
  FIO_ASSERT(fio_str_data(&str) == fio_str_info(&str).data,
             "Small String `fio_str_data` != `fio_str_info(s).data` (%p != %p)",
             (void *)fio_str_data(&str), (void *)fio_str_info(&str).data);

  fio_str_capa_assert(&str, sizeof(fio_str_s) - 1);
  FIO_ASSERT(!str.small,
             "Long String reporting as small after capacity update!");
  FIO_ASSERT(fio_str_capa(&str) >= sizeof(fio_str_s) - 1,
             "Long String capacity update error (%zu != %zu)!",
             fio_str_capa(&str), sizeof(fio_str_s));
  FIO_ASSERT(fio_str_data(&str) == fio_str_info(&str).data,
             "Long String `fio_str_data` !>= `fio_str_info(s).data` (%p != %p)",
             (void *)fio_str_data(&str), (void *)fio_str_info(&str).data);

  FIO_ASSERT(
      fio_str_len(&str) == 4,
      "Long String length changed during conversion from small string (%zu)!",
      fio_str_len(&str));
  FIO_ASSERT(fio_str_data(&str) == str.data,
             "Long String pointer reporting error after capacity update!");
  FIO_ASSERT(strlen(fio_str_data(&str)) == 4,
             "Long String NUL missing after capacity update (%zu)!",
             strlen(fio_str_data(&str)));
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Worl"),
             "Long String value changed after capacity update (%s)!",
             fio_str_data(&str));

  fio_str_write(&str, "d!", 2);
  FIO_ASSERT(!strcmp(fio_str_data(&str), "World!"),
             "Long String `write` error (%s)!", fio_str_data(&str));

  fio_str_replace(&str, 0, 0, "Hello ", 6);
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Hello World!"),
             "Long String `insert` error (%s)!", fio_str_data(&str));

  fio_str_resize(&str, 6);
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Hello "),
             "Long String `resize` clipping error (%s)!", fio_str_data(&str));

  fio_str_replace(&str, 6, 0, "My World!", 9);
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Hello My World!"),
             "Long String `replace` error when testing overflow (%s)!",
             fio_str_data(&str));

  str.capa = str.len;
  fio_str_replace(&str, -10, 2, "Big", 3);
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Hello Big World!"),
             "Long String `replace` error when testing splicing (%s)!",
             fio_str_data(&str));

  FIO_ASSERT(
      fio_str_capa(&str) == ROUND_UP_CAPA_2WORDS(strlen("Hello Big World!")),
      "Long String `fio_str_replace` capacity update error (%zu != %zu)!",
      fio_str_capa(&str), ROUND_UP_CAPA_2WORDS(strlen("Hello Big World!")));

  if (str.len < FIO_STR_SMALL_CAPA) {
    fio_str_compact(&str);
    FIO_ASSERT(str.small, "Compacting didn't change String to small!");
    FIO_ASSERT(fio_str_len(&str) == strlen("Hello Big World!"),
               "Compacting altered String length! (%zu != %zu)!",
               fio_str_len(&str), strlen("Hello Big World!"));
    FIO_ASSERT(!strcmp(fio_str_data(&str), "Hello Big World!"),
               "Compact data error (%s)!", fio_str_data(&str));
    FIO_ASSERT(fio_str_capa(&str) == FIO_STR_SMALL_CAPA - 1,
               "Compacted String capacity reporting error!");
  } else {
    fprintf(stderr, "* skipped `compact` test!\n");
  }

  {
    fio_str_freeze(&str);
    fio_str_info_s old_state = fio_str_info(&str);
    fio_str_write(&str, "more data to be written here", 28);
    fio_str_replace(&str, 2, 1, "more data to be written here", 28);
    fio_str_info_s new_state = fio_str_info(&str);
    FIO_ASSERT(old_state.len == new_state.len, "Frozen String length changed!");
    FIO_ASSERT(old_state.data == new_state.data,
               "Frozen String pointer changed!");
    FIO_ASSERT(
        old_state.capa == new_state.capa,
        "Frozen String capacity changed (allowed, but shouldn't happen)!");
    str.frozen = 0;
  }
  fio_str_printf(&str, " %u", 42);
  FIO_ASSERT(!strcmp(fio_str_data(&str), "Hello Big World! 42"),
             "`fio_str_printf` data error (%s)!", fio_str_data(&str));

  {
    fio_str_s str2 = FIO_STR_INIT;
    fio_str_concat(&str2, &str);
    FIO_ASSERT(fio_str_iseq(&str, &str2),
               "`fio_str_concat` error, strings not equal (%s != %s)!",
               fio_str_data(&str), fio_str_data(&str2));
    fio_str_write(&str2, ":extra data", 11);
    FIO_ASSERT(
        !fio_str_iseq(&str, &str2),
        "`fio_str_write` error after copy, strings equal ((%zu)%s == (%zu)%s)!",
        fio_str_len(&str), fio_str_data(&str), fio_str_len(&str2),
        fio_str_data(&str2));

    fio_str_free(&str2);
  }

  fio_str_free(&str);

  fio_str_write_i(&str, -42);
  FIO_ASSERT(fio_str_len(&str) == 3 && !memcmp("-42", fio_str_data(&str), 3),
             "fio_str_write_i output error ((%zu) %s != -42)",
             fio_str_len(&str), fio_str_data(&str));
  fio_str_free(&str);

  {
    fprintf(stderr, "* testing `fio_str_readfile`, and reference counting.\n");
    fio_str_s *s = fio_str_new2();
    FIO_ASSERT(s && s->small,
               "`fio_str_new2` error, string not initialized (%p)!", (void *)s);
    fio_str_s *s2 = fio_str_dup(s);

    ++fio_str_test_dealloc_counter;

    FIO_ASSERT(s2 == s, "`fio_str_dup` error, should return self!");
    FIO_ASSERT(s->ref == 1,
               "`fio_str_dup` error, reference counter not incremented!");

    fprintf(stderr, "* reading a file.\n");
    fio_str_info_s state = fio_str_readfile(s, __FILE__, 0, 0);
    if (!s->small) /* attach deallocation test */
      s->dealloc = fio_str_test_dealloc;

    FIO_ASSERT(state.data,
               "`fio_str_readfile` error, no data was read for file %s!",
               __FILE__);

    FIO_ASSERT(!memcmp(state.data,
                       "/* "
                       "******************************************************"
                       "***********************",
                       80),
               "`fio_str_readfile` content error, header mismatch!\n %s",
               state.data);
    fprintf(stderr, "* testing UTF-8 validation and length.\n");
    FIO_ASSERT(
        fio_str_utf8_valid(s),
        "`fio_str_utf8_valid` error, code in this file should be valid!");
    FIO_ASSERT(fio_str_utf8_len(s) && (fio_str_utf8_len(s) <= fio_str_len(s)) &&
                   (fio_str_utf8_len(s) >= (fio_str_len(s)) >> 1),
               "`fio_str_utf8_len` error, invalid value (%zu / %zu!",
               fio_str_utf8_len(s), fio_str_len(s));

    fprintf(stderr, "* reviewing reference counting `fio_str_free2` (1/2).\n");
    fio_str_free2(s2);
    --fio_str_test_dealloc_counter;
    FIO_ASSERT(s->ref == 0,
               "`fio_str_free2` error, reference counter not subtracted!");
    FIO_ASSERT(s->small == 0, "`fio_str_free2` error, strring reinitialized!");
    FIO_ASSERT(
        fio_str_data(s) == state.data,
        "`fio_str_free2` error, data freed while references exist! (%p != %p)",
        (void *)fio_str_data(s), (void *)state.data);

    if (1) {
      /* String content == whole file (this file) */
      intptr_t pos = -11;
      size_t len = 20;
      fprintf(stderr, "* testing UTF-8 positioning.\n");

      FIO_ASSERT(
          fio_str_utf8_select(s, &pos, &len) == 0,
          "`fio_str_utf8_select` returned error for negative pos! (%zd, %zu)",
          (ssize_t)pos, len);
      FIO_ASSERT(
          pos == (intptr_t)state.len - 10, /* no UTF-8 bytes in this file */
          "`fio_str_utf8_select` error, negative position invalid! (%zd)",
          (ssize_t)pos);
      FIO_ASSERT(len == 10,
                 "`fio_str_utf8_select` error, trancated length invalid! (%zd)",
                 (ssize_t)len);
      pos = 10;
      len = 20;
      FIO_ASSERT(fio_str_utf8_select(s, &pos, &len) == 0,
                 "`fio_str_utf8_select` returned error! (%zd, %zu)",
                 (ssize_t)pos, len);
      FIO_ASSERT(pos == 10,
                 "`fio_str_utf8_select` error, position invalid! (%zd)",
                 (ssize_t)pos);
      FIO_ASSERT(len == 20,
                 "`fio_str_utf8_select` error, length invalid! (%zd)",
                 (ssize_t)len);
    }
    fprintf(stderr, "* reviewing reference counting `fio_str_free2` (2/2).\n");
    fio_str_free2(s);
    fprintf(stderr, "* finished reference counting test.\n");
  }
  fio_str_free(&str);
  if (1) {

    const char *utf8_sample = /* three hearts, small-big-small*/
        "\xf0\x9f\x92\x95\xe2\x9d\xa4\xef\xb8\x8f\xf0\x9f\x92\x95";
    fio_str_write(&str, utf8_sample, strlen(utf8_sample));
    intptr_t pos = -2;
    size_t len = 2;
    FIO_ASSERT(fio_str_utf8_select(&str, &pos, &len) == 0,
               "`fio_str_utf8_select` returned error for negative pos on "
               "UTF-8 data! (%zd, %zu)",
               (ssize_t)pos, len);
    FIO_ASSERT(pos == (intptr_t)fio_str_len(&str) - 4, /* 4 byte emoji */
               "`fio_str_utf8_select` error, negative position invalid on "
               "UTF-8 data! (%zd)",
               (ssize_t)pos);
    FIO_ASSERT(len == 4, /* last utf-8 char is 4 byte long */
               "`fio_str_utf8_select` error, trancated length invalid on "
               "UTF-8 data! (%zd)",
               (ssize_t)len);
    pos = 1;
    len = 20;
    FIO_ASSERT(fio_str_utf8_select(&str, &pos, &len) == 0,
               "`fio_str_utf8_select` returned error on UTF-8 data! (%zd, %zu)",
               (ssize_t)pos, len);
    FIO_ASSERT(
        pos == 4,
        "`fio_str_utf8_select` error, position invalid on UTF-8 data! (%zd)",
        (ssize_t)pos);
    FIO_ASSERT(
        len == 10,
        "`fio_str_utf8_select` error, length invalid on UTF-8 data! (%zd)",
        (ssize_t)len);
    pos = 1;
    len = 3;
    FIO_ASSERT(
        fio_str_utf8_select(&str, &pos, &len) == 0,
        "`fio_str_utf8_select` returned error on UTF-8 data (2)! (%zd, %zu)",
        (ssize_t)pos, len);
    FIO_ASSERT(
        len == 10, /* 3 UTF-8 chars: 4 byte + 4 byte + 2 byte codes == 10 */
        "`fio_str_utf8_select` error, length invalid on UTF-8 data! (%zd)",
        (ssize_t)len);
  }
  fio_str_free(&str);
  if (1) {
    str = FIO_STR_INIT_STATIC("Welcome");
    FIO_ASSERT(fio_str_capa(&str) == 0, "Static string capacity non-zero.");
    FIO_ASSERT(fio_str_len(&str) > 0,
               "Static string length should be automatically calculated.");
    FIO_ASSERT(str.dealloc == NULL,
               "Static string deallocation function should be NULL.");
    fio_str_free(&str);
    str = FIO_STR_INIT_STATIC("Welcome");
    fio_str_info_s state = fio_str_write(&str, " Home", 5);
    FIO_ASSERT(state.capa > 0, "Static string not converted to non-static.");
    FIO_ASSERT(str.dealloc, "Missing static string deallocation function"
                            " after `fio_str_write`.");

    fprintf(stderr, "* reviewing `fio_str_detach`.\n   (%zu): %s\n",
            fio_str_info(&str).len, fio_str_info(&str).data);
    char *cstr = fio_str_detach(&str);
    FIO_ASSERT(cstr, "`fio_str_detach` returned NULL");
    FIO_ASSERT(!memcmp(cstr, "Welcome Home\0", 13),
               "`fio_str_detach` string error: %s", cstr);
    fio_free(cstr);
    FIO_ASSERT(fio_str_len(&str) == 0, "`fio_str_detach` data wasn't cleared.");
    // fio_str_free(&str);
  }
  fprintf(stderr, "* passed.\n");
}
#endif

/* *****************************************************************************
Testing Memory Allocator
***************************************************************************** */

#if FIO_FORCE_MALLOC
#define fio_malloc_test()                                                      \
  fprintf(stderr, "\n=== SKIPPED facil.io memory allocator (bypassed)\n");
#else
FIO_FUNC void fio_malloc_test(void) {
  fprintf(stderr, "\n=== Testing facil.io memory allocator's system calls\n");
  char *mem = sys_alloc(FIO_MEMORY_BLOCK_SIZE, 0);
  FIO_ASSERT(mem, "sys_alloc failed to allocate memory!\n");
  FIO_ASSERT(!((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK),
             "Memory allocation not aligned to FIO_MEMORY_BLOCK_SIZE!");
  mem[0] = 'a';
  mem[FIO_MEMORY_BLOCK_SIZE - 1] = 'z';
  fprintf(stderr, "* Testing reallocation from %p\n", (void *)mem);
  char *mem2 =
      sys_realloc(mem, FIO_MEMORY_BLOCK_SIZE, FIO_MEMORY_BLOCK_SIZE * 2);
  if (mem == mem2)
    fprintf(stderr, "* Performed system realloc without copy :-)\n");
  FIO_ASSERT(mem2[0] == 'a' && mem2[FIO_MEMORY_BLOCK_SIZE - 1] == 'z',
             "Reaclloc data was lost!");
  sys_free(mem2, FIO_MEMORY_BLOCK_SIZE * 2);
  fprintf(stderr, "=== Testing facil.io memory allocator's internal data.\n");
  FIO_ASSERT(arenas, "Missing arena data - library not initialized!");
  fio_free(NULL); /* fio_free(NULL) shouldn't crash... */
  mem = fio_malloc(1);
  FIO_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
  FIO_ASSERT(!((uintptr_t)mem & 15), "fio_malloc memory not aligned!\n");
  FIO_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16,
             "small fio_malloc memory indicates system allocation!\n");
  mem[0] = 'a';
  FIO_ASSERT(mem[0] == 'a', "allocate memory wasn't written to!\n");
  mem = fio_realloc(mem, 1);
  FIO_ASSERT(mem, "fio_realloc failed!\n");
  FIO_ASSERT(mem[0] == 'a', "fio_realloc memory wasn't copied!\n");
  FIO_ASSERT(arena_last_used, "arena_last_used wasn't initialized!\n");
  fio_free(mem);
  block_s *b = arena_last_used->block;

  /* move arena to block's start */
  while (arena_last_used->block == b) {
    mem = fio_malloc(1);
    FIO_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
    fio_free(mem);
  }
  /* make sure a block is assigned */
  fio_free(fio_malloc(1));
  b = arena_last_used->block;
  size_t count = 1;
  /* count allocations within block */
  do {
    FIO_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
    FIO_ASSERT(!((uintptr_t)mem & 15),
               "fio_malloc memory not aligned at allocation #%zu!\n", count);
    FIO_ASSERT((((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16),
               "fio_malloc memory indicates system allocation!\n");
#if __x86_64__
    fio_memcpy((size_t *)mem, (size_t *)"0123456789abcdefg", 1);
#else
    mem[0] = 'a';
#endif
    fio_free(mem); /* make sure we hold on to the block, so it rotates */
    mem = fio_malloc(1);
    ++count;
  } while (arena_last_used->block == b);
  {
    fprintf(stderr, "* Confirm block address: %p, last allocation was %p\n",
            (void *)arena_last_used->block, (void *)mem);
    fprintf(
        stderr,
        "* Performed %zu allocations out of expected %zu allocations per "
        "block.\n",
        count,
        (size_t)((FIO_MEMORY_BLOCK_SLICES - 2) - (sizeof(block_s) >> 4) - 1));
    fio_ls_embd_s old_memory_list = memory.available;
    fio_free(mem);
    FIO_ASSERT(fio_ls_embd_any(&memory.available),
               "memory pool empty (memory block wasn't freed)!\n");
    FIO_ASSERT(old_memory_list.next != memory.available.next ||
                   memory.available.prev != old_memory_list.prev,
               "memory pool not updated after block being freed!\n");
  }
  /* rotate block again */
  b = arena_last_used->block;
  mem = fio_realloc(mem, 1);
  do {
    mem2 = mem;
    mem = fio_malloc(1);
    fio_free(mem2); /* make sure we hold on to the block, so it rotates */
    FIO_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
    FIO_ASSERT(!((uintptr_t)mem & 15),
               "fio_malloc memory not aligned at allocation #%zu!\n", count);
    FIO_ASSERT((((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16),
               "fio_malloc memory indicates system allocation!\n");
#if __x86_64__
    fio_memcpy((size_t *)mem, (size_t *)"0123456789abcdefg", 1);
#else
    mem[0] = 'a';
#endif
    ++count;
  } while (arena_last_used->block == b);

  mem2 = mem;
  mem = fio_calloc(FIO_MEMORY_BLOCK_ALLOC_LIMIT - 64, 1);
  fio_free(mem2);
  FIO_ASSERT(mem,
             "failed to allocate FIO_MEMORY_BLOCK_ALLOC_LIMIT - 64 bytes!\n");
  FIO_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16,
             "fio_calloc (under limit) memory alignment error!\n");
  mem2 = fio_malloc(1);
  FIO_ASSERT(mem2, "fio_malloc(1) failed to allocate memory!\n");
  mem2[0] = 'a';

  for (uintptr_t i = 0; i < (FIO_MEMORY_BLOCK_ALLOC_LIMIT - 64); ++i) {
    FIO_ASSERT(mem[i] == 0,
               "calloc returned memory that wasn't initialized?!\n");
  }
  fio_free(mem);

  mem = fio_malloc(FIO_MEMORY_BLOCK_SIZE);
  FIO_ASSERT(mem, "fio_malloc failed to FIO_MEMORY_BLOCK_SIZE bytes!\n");
  FIO_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) == 16,
             "fio_malloc (big) memory isn't aligned!\n");
  mem = fio_realloc(mem, FIO_MEMORY_BLOCK_SIZE * 2);
  FIO_ASSERT(mem,
             "fio_realloc (big) failed on FIO_MEMORY_BLOCK_SIZE X2 bytes!\n");
  fio_free(mem);
  FIO_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) == 16,
             "fio_realloc (big) memory isn't aligned!\n");

  {
    void *m0 = fio_malloc(0);
    void *rm0 = fio_realloc(m0, 16);
    FIO_ASSERT(m0 != rm0, "fio_realloc(fio_malloc(0), 16) failed!\n");
    fio_free(rm0);
  }
  {
    size_t pool_size = 0;
    FIO_LS_EMBD_FOR(&memory.available, node) { ++pool_size; }
    mem = fio_mmap(512);
    FIO_ASSERT(mem, "fio_mmap allocation failed!\n");
    fio_free(mem);
    size_t new_pool_size = 0;
    FIO_LS_EMBD_FOR(&memory.available, node) { ++new_pool_size; }
    FIO_ASSERT(new_pool_size == pool_size,
               "fio_free of fio_mmap went to memory pool!\n");
  }

  fprintf(stderr, "* passed.\n");
}
#endif

/* *****************************************************************************
Testing Core Callback add / remove / ensure
***************************************************************************** */

FIO_FUNC void fio_state_callback_test_task(void *pi) {
  ((uintptr_t *)pi)[0] += 1;
}

#define FIO_STATE_TEST_COUNT 10
FIO_FUNC void fio_state_callback_order_test_task(void *pi) {
  static uintptr_t start = FIO_STATE_TEST_COUNT;
  --start;
  FIO_ASSERT((uintptr_t)pi == start,
             "Callback order error, expecting %zu, got %zu", (size_t)start,
             (size_t)pi);
}

FIO_FUNC void fio_state_callback_test(void) {
  fprintf(stderr, "=== Testing facil.io workflow state callback system\n");
  uintptr_t result = 0;
  uintptr_t other = 0;
  fio_state_callback_add(FIO_CALL_NEVER, fio_state_callback_test_task, &result);
  FIO_ASSERT(callback_collection[FIO_CALL_NEVER].callbacks.next,
             "Callback list failed to initialize.");
  fio_state_callback_force(FIO_CALL_NEVER);
  FIO_ASSERT(result == 1, "Callback wasn't called!");
  fio_state_callback_force(FIO_CALL_NEVER);
  FIO_ASSERT(result == 2, "Callback wasn't called (second time)!");
  fio_state_callback_remove(FIO_CALL_NEVER, fio_state_callback_test_task,
                            &result);
  fio_state_callback_force(FIO_CALL_NEVER);
  FIO_ASSERT(result == 2, "Callback wasn't removed!");
  fio_state_callback_add(FIO_CALL_NEVER, fio_state_callback_test_task, &result);
  fio_state_callback_add(FIO_CALL_NEVER, fio_state_callback_test_task, &other);
  fio_state_callback_clear(FIO_CALL_NEVER);
  fio_state_callback_force(FIO_CALL_NEVER);
  FIO_ASSERT(result == 2 && other == 0, "Callbacks werent cleared!");
  for (uintptr_t i = 0; i < FIO_STATE_TEST_COUNT; ++i) {
    fio_state_callback_add(FIO_CALL_NEVER, fio_state_callback_order_test_task,
                           (void *)i);
  }
  fio_state_callback_force(FIO_CALL_NEVER);
  fio_state_callback_clear(FIO_CALL_NEVER);
  fprintf(stderr, "* passed.\n");
}
#undef FIO_STATE_TEST_COUNT
/* *****************************************************************************
Testing fio_timers
***************************************************************************** */

FIO_FUNC void fio_timer_test_task(void *arg) { ++(((size_t *)arg)[0]); }

FIO_FUNC void fio_timer_test(void) {
  fprintf(stderr, "=== Testing facil.io timer system\n");
  size_t result = 0;
  const size_t total = 5;
  fio_data->active = 1;
  FIO_ASSERT(fio_timers.next, "Timers not initialized!");
  FIO_ASSERT(fio_run_every(0, 0, fio_timer_test_task, NULL, NULL) == -1,
             "Timers without an interval should be an error.");
  FIO_ASSERT(fio_run_every(1000, 0, NULL, NULL, NULL) == -1,
             "Timers without a task should be an error.");
  FIO_ASSERT(fio_run_every(900, total, fio_timer_test_task, &result,
                           fio_timer_test_task) == 0,
             "Timer creation failure.");
  FIO_ASSERT(fio_ls_embd_any(&fio_timers),
             "Timer scheduling failure - no timer in list.");
  FIO_ASSERT(fio_timer_calc_first_interval() >= 898 &&
                 fio_timer_calc_first_interval() <= 902,
             "next timer calculation error %zu",
             fio_timer_calc_first_interval());

  fio_ls_embd_s *first = fio_timers.next;
  FIO_ASSERT(fio_run_every(10000, total, fio_timer_test_task, &result,
                           fio_timer_test_task) == 0,
             "Timer creation failure (second timer).");
  FIO_ASSERT(fio_timers.next == first, "Timer Ordering error!");

  FIO_ASSERT(fio_timer_calc_first_interval() >= 898 &&
                 fio_timer_calc_first_interval() <= 902,
             "next timer calculation error (after added timer) %zu",
             fio_timer_calc_first_interval());

  fio_data->last_cycle.tv_nsec += 800;
  fio_timer_schedule();
  fio_defer_perform();
  FIO_ASSERT(result == 0, "Timer filtering error (%zu != 0)\n", result);

  for (size_t i = 0; i < total; ++i) {
    fio_data->last_cycle.tv_sec += 1;
    // fio_data->last_cycle.tv_nsec += 1;
    fio_timer_schedule();
    fio_defer_perform();
    FIO_ASSERT(((i != total - 1 && result == i + 1) ||
                (i == total - 1 && result == total + 1)),
               "Timer running and rescheduling error (%zu != %zu)\n", result,
               i + 1);
    FIO_ASSERT(fio_timers.next == first || i == total - 1,
               "Timer Ordering error on cycle %zu!", i);
  }

  fio_data->last_cycle.tv_sec += 10;
  fio_timer_schedule();
  fio_defer_perform();
  FIO_ASSERT(result == total + 2, "Timer # 2 error (%zu != %zu)\n", result,
             total + 2);
  fio_data->active = 0;
  fio_timer_clear_all();
  fio_defer_clear_tasks();
  fprintf(stderr, "* passed.\n");
}

/* *****************************************************************************
Testing listening socket
***************************************************************************** */

FIO_FUNC void fio_socket_test(void) {
  /* initialize unix socket name */
  fio_str_s sock_name = FIO_STR_INIT;
#ifdef P_tmpdir
  fio_str_write(&sock_name, P_tmpdir, strlen(P_tmpdir));
  if (fio_str_len(&sock_name) &&
      fio_str_data(&sock_name)[fio_str_len(&sock_name) - 1] == '/')
    fio_str_resize(&sock_name, fio_str_len(&sock_name) - 1);
#else
  fio_str_write(&sock_name, "/tmp", 4);
#endif
  fio_str_printf(&sock_name, "/fio_test_sock-%d.sock", (int)getpid());

  fprintf(stderr, "=== Testing facil.io listening socket creation (partial "
                  "testing only).\n");
  fprintf(stderr, "* testing on TCP/IP port 8765 and Unix socket: %s\n",
          fio_str_data(&sock_name));
  intptr_t uuid = fio_socket(fio_str_data(&sock_name), NULL, 1);
  FIO_ASSERT(uuid != -1, "Failed to open unix socket\n");
  FIO_ASSERT(uuid_data(uuid).open, "Unix socket not initialized");
  intptr_t client1 = fio_socket(fio_str_data(&sock_name), NULL, 0);
  FIO_ASSERT(client1 != -1, "Failed to connect to unix socket.");
  intptr_t client2 = fio_accept(uuid);
  FIO_ASSERT(client2 != -1, "Failed to accept unix socket connection.");
  fprintf(stderr, "* Unix server addr %s\n", fio_peer_addr(uuid).data);
  fprintf(stderr, "* Unix client1 addr %s\n", fio_peer_addr(client1).data);
  fprintf(stderr, "* Unix client2 addr %s\n", fio_peer_addr(client2).data);
  {
    char tmp_buf[28];
    ssize_t r = -1;
    ssize_t timer_junk;
    fio_write(client1, "Hello World", 11);
    if (0) {
      /* packet may have been sent synchronously, don't test */
      if (!uuid_data(client1).packet)
        unlink(__FILE__ ".sock");
      FIO_ASSERT(uuid_data(client1).packet, "fio_write error, no packet!")
    }
    /* prevent poll from hanging */
    fio_run_every(5, 1, fio_timer_test_task, &timer_junk, fio_timer_test_task);
    errno = EAGAIN;
    for (size_t i = 0; i < 100 && r <= 0 &&
                       (r == 0 || errno == EAGAIN || errno == EWOULDBLOCK);
         ++i) {
      fio_poll();
      fio_defer_perform();
      fio_reschedule_thread();
      errno = 0;
      r = fio_read(client2, tmp_buf, 28);
    }
    if (!(r > 0 && r <= 28) || memcmp("Hello World", tmp_buf, r)) {
      perror("* ernno");
      unlink(__FILE__ ".sock");
    }
    FIO_ASSERT(r > 0 && r <= 28,
               "Failed to read from unix socket " __FILE__ ".sock %zd", r);
    FIO_ASSERT(!memcmp("Hello World", tmp_buf, r),
               "Unix socket Read/Write cycle error (%zd: %.*s)", r, (int)r,
               tmp_buf);
    fprintf(stderr, "* Unix socket Read/Write cycle passed: %.*s\n", (int)r,
            tmp_buf);
    fio_data->last_cycle.tv_sec += 10;
    fio_timer_clear_all();
  }

  fio_force_close(client1);
  fio_force_close(client2);
  fio_force_close(uuid);
  unlink(fio_str_data(&sock_name));
  /* free unix socket name */
  fio_str_free(&sock_name);

  uuid = fio_socket(NULL, "8765", 1);
  FIO_ASSERT(uuid != -1, "Failed to open TCP/IP socket on port 8765");
  FIO_ASSERT(uuid_data(uuid).open, "TCP/IP socket not initialized");
  fprintf(stderr, "* TCP/IP server addr %s\n", fio_peer_addr(uuid).data);
  client1 = fio_socket("Localhost", "8765", 0);
  FIO_ASSERT(client1 != -1, "Failed to connect to TCP/IP socket on port 8765");
  fprintf(stderr, "* TCP/IP client1 addr %s\n", fio_peer_addr(client1).data);
  errno = EAGAIN;
  for (size_t i = 0; i < 100 && (errno == EAGAIN || errno == EWOULDBLOCK);
       ++i) {
    errno = 0;
    fio_reschedule_thread();
    client2 = fio_accept(uuid);
  }
  if (client2 == -1)
    perror("accept error");
  FIO_ASSERT(client2 != -1,
             "Failed to accept TCP/IP socket connection on port 8765");
  fprintf(stderr, "* TCP/IP client2 addr %s\n", fio_peer_addr(client2).data);
  fio_force_close(client1);
  fio_force_close(client2);
  fio_force_close(uuid);
  fio_timer_clear_all();
  fio_defer_clear_tasks();
  fprintf(stderr, "* passed.\n");
}

/* *****************************************************************************
Testing listening socket
***************************************************************************** */

FIO_FUNC void fio_cycle_test_task(void *arg) {
  fio_stop();
  (void)arg;
}
FIO_FUNC void fio_cycle_test_task2(void *arg) {
  fprintf(stderr, "* facil.io cycling test fatal error!\n");
  exit(-1);
  (void)arg;
}

FIO_FUNC void fio_cycle_test(void) {
  fprintf(stderr,
          "=== Testing facil.io cycling logic (partial - only tests timers)\n");
  fio_mark_time();
  fio_timer_clear_all();
  struct timespec start = fio_last_tick();
  fio_run_every(1000, 1, fio_cycle_test_task, NULL, NULL);
  fio_run_every(10000, 1, fio_cycle_test_task2, NULL, NULL);
  fio_start(.threads = 1, .workers = 1);
  struct timespec end = fio_last_tick();
  fio_timer_clear_all();
  FIO_ASSERT(end.tv_sec == start.tv_sec + 1 || end.tv_sec == start.tv_sec + 2,
             "facil.io cycling error?");
  fprintf(stderr, "* passed.\n");
}
/* *****************************************************************************
Testing fio_defer task system
***************************************************************************** */

#define FIO_DEFER_TOTAL_COUNT (512 * 1024)

#ifndef FIO_DEFER_TEST_PRINT
#define FIO_DEFER_TEST_PRINT 0
#endif

FIO_FUNC void sample_task(void *i_count, void *unused2) {
  (void)(unused2);
  fio_atomic_add((uintptr_t *)i_count, 1);
}

FIO_FUNC void sched_sample_task(void *count, void *i_count) {
  for (size_t i = 0; i < (uintptr_t)count; i++) {
    fio_defer(sample_task, i_count, NULL);
  }
}

FIO_FUNC void fio_defer_test(void) {
  const size_t cpu_cores = fio_detect_cpu_cores();
  FIO_ASSERT(cpu_cores, "couldn't detect CPU cores!");
  uintptr_t i_count;
  clock_t start, end;
  fprintf(stderr, "=== Testing facil.io task scheduling (fio_defer)\n");
  FIO_ASSERT(!fio_defer_has_queue(), "facil.io queue always active.")
  i_count = 0;
  start = clock();
  for (size_t i = 0; i < FIO_DEFER_TOTAL_COUNT; i++) {
    sample_task(&i_count, NULL);
  }
  end = clock();
  if (FIO_DEFER_TEST_PRINT) {
    fprintf(stderr,
            "Deferless (direct call) counter: %lu cycles with i_count = %lu, "
            "%lu/%lu free/malloc\n",
            (unsigned long)(end - start), (unsigned long)i_count,
            (unsigned long)fio_defer_count_dealloc,
            (unsigned long)fio_defer_count_alloc);
  }
  size_t i_count_should_be = i_count;

  if (FIO_DEFER_TEST_PRINT) {
    fprintf(stderr, "\n");
  }

  for (size_t i = 1; FIO_DEFER_TOTAL_COUNT >> i; ++i) {
    i_count = 0;
    const size_t per_task = FIO_DEFER_TOTAL_COUNT >> i;
    const size_t tasks = 1 << i;
    start = clock();
    for (size_t j = 0; j < tasks; ++j) {
      fio_defer(sched_sample_task, (void *)per_task, &i_count);
    }
    FIO_ASSERT(fio_defer_has_queue(), "facil.io queue not marked.")
    fio_defer_thread_pool_join(fio_defer_thread_pool_new((i % cpu_cores) + 1));
    end = clock();
    if (FIO_DEFER_TEST_PRINT) {
      fprintf(stderr,
              "- Defer %zu threads, %zu scheduling loops (%zu each):\n"
              "    %lu cycles with i_count = %lu, %lu/%lu "
              "free/malloc\n",
              ((i % cpu_cores) + 1), tasks, per_task,
              (unsigned long)(end - start), (unsigned long)i_count,
              (unsigned long)fio_defer_count_dealloc,
              (unsigned long)fio_defer_count_alloc);
    } else {
      fprintf(stderr, ".");
    }
    FIO_ASSERT(i_count == i_count_should_be, "ERROR: defer count invalid\n");
    FIO_ASSERT(fio_defer_count_dealloc == fio_defer_count_alloc,
               "defer deallocation vs. allocation error, %zu != %zu",
               fio_defer_count_dealloc, fio_defer_count_alloc);
  }
  FIO_ASSERT(task_queue_normal.writer == &task_queue_normal.static_queue,
             "defer library didn't release dynamic queue (should be static)");
  fprintf(stderr, "\n* passed.\n");
}

/* *****************************************************************************
Array data-structure Testing
***************************************************************************** */

typedef struct {
  int i;
  char c;
} fio_ary_test_type_s;

#define FIO_ARY_NAME fio_i_ary
#define FIO_ARY_TYPE uintptr_t
#include "fio.h"

FIO_FUNC intptr_t ary_alloc_counter = 0;
FIO_FUNC void copy_s(fio_ary_test_type_s *d, fio_ary_test_type_s *s) {
  ++ary_alloc_counter;
  *d = *s;
}

#define FIO_ARY_NAME fio_s_ary
#define FIO_ARY_TYPE fio_ary_test_type_s
#define FIO_ARY_COPY(dest, src) copy_s(&(dest), &(src))
#define FIO_ARY_COMPARE(dest, src) ((dest).i == (src).i && (dest).c == (src).c)
#define FIO_ARY_DESTROY(obj) (--ary_alloc_counter)
#include "fio.h"

FIO_FUNC void fio_ary_test(void) {
  /* code */
  fio_i_ary__test();
  fio_s_ary__test();
  FIO_ASSERT(!ary_alloc_counter, "array object deallocation error, %ld != 0",
             ary_alloc_counter);
}

/* *****************************************************************************
Set data-structure Testing
***************************************************************************** */

#define FIO_SET_TEST_COUNT 524288UL

#define FIO_SET_NAME fio_set_test
#define FIO_SET_OBJ_TYPE uintptr_t
#include <fio.h>

#define FIO_SET_NAME fio_hash_test
#define FIO_SET_KEY_TYPE uintptr_t
#define FIO_SET_OBJ_TYPE uintptr_t
#include <fio.h>

#define FIO_SET_NAME fio_set_attack
#define FIO_SET_OBJ_COMPARE(a, b) ((a) == (b))
#define FIO_SET_OBJ_TYPE uintptr_t
#include <fio.h>

FIO_FUNC void fio_set_test(void) {
  fio_set_test_s s = FIO_SET_INIT;
  fio_hash_test_s h = FIO_SET_INIT;
  fprintf(
      stderr,
      "=== Testing Core ordered Set (re-including fio.h with FIO_SET_NAME)\n");
  fprintf(stderr, "* Inserting %lu items\n", FIO_SET_TEST_COUNT);

  FIO_ASSERT(fio_set_test_count(&s) == 0, "empty set should have zero objects");
  FIO_ASSERT(fio_set_test_capa(&s) == 0, "empty set should have no capacity");
  FIO_ASSERT(fio_hash_test_capa(&h) == 0, "empty hash should have no capacity");
  FIO_ASSERT(!fio_set_test_is_fragmented(&s),
             "empty set shouldn't be considered fragmented");
  FIO_ASSERT(!fio_hash_test_is_fragmented(&h),
             "empty hash shouldn't be considered fragmented");
  FIO_ASSERT(!fio_set_test_last(&s), "empty set shouldn't have a last object");
  FIO_ASSERT(!fio_hash_test_last(&h).key && !fio_hash_test_last(&h).obj,
             "empty hash shouldn't have a last object");

  for (uintptr_t i = 1; i < FIO_SET_TEST_COUNT; ++i) {
    fio_set_test_insert(&s, i, i);
    fio_hash_test_insert(&h, i, i, i + 1, NULL);
    FIO_ASSERT(fio_set_test_find(&s, i, i), "set find failed after insert");
    FIO_ASSERT(fio_hash_test_find(&h, i, i), "hash find failed after insert");
    FIO_ASSERT(i == fio_set_test_find(&s, i, i), "set insertion != find");
    FIO_ASSERT(i + 1 == fio_hash_test_find(&h, i, i), "hash insertion != find");
  }
  fprintf(stderr, "* Seeking %lu items\n", FIO_SET_TEST_COUNT);
  for (unsigned long i = 1; i < FIO_SET_TEST_COUNT; ++i) {
    FIO_ASSERT((i == fio_set_test_find(&s, i, i)),
               "set insertion != find (seek)");
    FIO_ASSERT((i + 1 == fio_hash_test_find(&h, i, i)),
               "hash insertion != find (seek)");
  }
  {
    fprintf(stderr, "* Testing order for %lu items in set\n",
            FIO_SET_TEST_COUNT);
    uintptr_t i = 1;
    FIO_SET_FOR_LOOP(&s, pos) {
      FIO_ASSERT(pos->obj == i, "object order mismatch %lu != %lu.",
                 (unsigned long)i, (unsigned long)pos->obj);
      ++i;
    }
  }
  {
    fprintf(stderr, "* Testing order for %lu items in hash\n",
            FIO_SET_TEST_COUNT);
    uintptr_t i = 1;
    FIO_SET_FOR_LOOP(&h, pos) {
      FIO_ASSERT(pos->obj.obj == i + 1 && pos->obj.key == i,
                 "object order mismatch %lu != %lu.", (unsigned long)i,
                 (unsigned long)pos->obj.key);
      ++i;
    }
  }

  fprintf(stderr, "* Removing odd items from %lu items\n", FIO_SET_TEST_COUNT);
  for (unsigned long i = 1; i < FIO_SET_TEST_COUNT; i += 2) {
    fio_set_test_remove(&s, i, i, NULL);
    fio_hash_test_remove(&h, i, i, NULL);
    FIO_ASSERT(!(fio_set_test_find(&s, i, i)),
               "Removal failed in set (still exists).");
    FIO_ASSERT(!(fio_hash_test_find(&h, i, i)),
               "Removal failed in hash (still exists).");
  }
  {
    fprintf(stderr, "* Testing for %lu / 2 holes\n", FIO_SET_TEST_COUNT);
    uintptr_t i = 1;
    FIO_SET_FOR_LOOP(&s, pos) {
      if (pos->hash == 0) {
        FIO_ASSERT((i & 1) == 1, "deleted object wasn't odd");
      } else {
        FIO_ASSERT(pos->obj == i, "deleted object value mismatch %lu != %lu",
                   (unsigned long)i, (unsigned long)pos->obj);
      }
      ++i;
    }
    i = 1;
    FIO_SET_FOR_LOOP(&h, pos) {
      if (pos->hash == 0) {
        FIO_ASSERT((i & 1) == 1, "deleted object wasn't odd");
      } else {
        FIO_ASSERT(pos->obj.key == i,
                   "deleted object value mismatch %lu != %lu", (unsigned long)i,
                   (unsigned long)pos->obj.key);
      }
      ++i;
    }
    {
      fprintf(stderr, "* Poping two elements (testing pop through holes)\n");
      FIO_ASSERT(fio_set_test_last(&s), "Pop `last` 1 failed - no last object");
      uintptr_t tmp = fio_set_test_last(&s);
      FIO_ASSERT(tmp, "Pop set `last` 1 failed to collect object");
      fio_set_test_pop(&s);
      FIO_ASSERT(
          fio_set_test_last(&s) != tmp,
          "Pop `last` 2 in set same as `last` 1 - failed to collect object");
      tmp = fio_hash_test_last(&h).key;
      FIO_ASSERT(tmp, "Pop hash `last` 1 failed to collect object");
      fio_hash_test_pop(&h);
      FIO_ASSERT(
          fio_hash_test_last(&h).key != tmp,
          "Pop `last` 2 in hash same as `last` 1 - failed to collect object");
      FIO_ASSERT(fio_set_test_last(&s), "Pop `last` 2 failed - no last object");
      FIO_ASSERT(fio_hash_test_last(&h).obj,
                 "Pop `last` 2 failed in hash - no last object");
      fio_set_test_pop(&s);
      fio_hash_test_pop(&h);
    }
    if (1) {
      uintptr_t tmp = 1;
      fio_set_test_remove(&s, tmp, tmp, NULL);
      fio_hash_test_remove(&h, tmp, tmp, NULL);
      size_t count = s.count;
      fio_set_test_overwrite(&s, tmp, tmp, NULL);
      FIO_ASSERT(
          count + 1 == s.count,
          "Re-adding a removed item in set should increase count by 1 (%zu + "
          "1 != %zu).",
          count, (size_t)s.count);
      count = h.count;
      fio_hash_test_insert(&h, tmp, tmp, tmp, NULL);
      FIO_ASSERT(
          count + 1 == h.count,
          "Re-adding a removed item in hash should increase count by 1 (%zu + "
          "1 != %zu).",
          count, (size_t)s.count);
      tmp = fio_set_test_find(&s, tmp, tmp);
      FIO_ASSERT(tmp == 1,
                 "Re-adding a removed item should update the item in the set "
                 "(%lu != 1)!",
                 (unsigned long)fio_set_test_find(&s, tmp, tmp));
      fio_set_test_remove(&s, tmp, tmp, NULL);
      fio_hash_test_remove(&h, tmp, tmp, NULL);
      FIO_ASSERT(count == h.count,
                 "Re-removing an item should decrease count (%zu != %zu).",
                 count, (size_t)s.count);
      FIO_ASSERT(!fio_set_test_find(&s, tmp, tmp),
                 "Re-removing a re-added item should update the item!");
    }
  }
  fprintf(stderr, "* Compacting HashMap to %lu\n", FIO_SET_TEST_COUNT >> 1);
  fio_set_test_compact(&s);
  {
    fprintf(stderr, "* Testing that %lu items are continuous\n",
            FIO_SET_TEST_COUNT >> 1);
    uintptr_t i = 0;
    FIO_SET_FOR_LOOP(&s, pos) {
      FIO_ASSERT(pos->hash != 0, "Found a hole after compact.");
      ++i;
    }
    FIO_ASSERT(i == s.count, "count error (%lu != %lu).", i, s.count);
  }

  fio_set_test_free(&s);
  fio_hash_test_free(&h);
  FIO_ASSERT(!s.map && !s.ordered && !s.pos && !s.capa,
             "HashMap not re-initialized after free.");

  fio_set_test_capa_require(&s, FIO_SET_TEST_COUNT);

  FIO_ASSERT(
      s.map && s.ordered && !s.pos && s.capa >= FIO_SET_TEST_COUNT,
      "capa_require changes state in a bad way (%p, %p, %zu, %zu ?>= %zu)",
      (void *)s.map, (void *)s.ordered, s.pos, s.capa, FIO_SET_TEST_COUNT);

  for (unsigned long i = 1; i < FIO_SET_TEST_COUNT; ++i) {
    fio_set_test_insert(&s, i, i);
    FIO_ASSERT(fio_set_test_find(&s, i, i),
               "find failed after insert (2nd round)");
    FIO_ASSERT(i == fio_set_test_find(&s, i, i),
               "insertion (2nd round) != find");
    FIO_ASSERT(i == s.count, "count error (%lu != %lu) post insertion.", i,
               s.count);
  }
  fio_set_test_free(&s);
  /* full/partial collision attack against set and test response */
  if (1) {
    fio_set_attack_s as = FIO_SET_INIT;
    time_t start_ok = clock();
    for (uintptr_t i = 0; i < FIO_SET_TEST_COUNT; ++i) {
      fio_set_attack_insert(&as, i, i + 1);
      FIO_ASSERT(fio_set_attack_find(&as, i, i + 1) == i + 1,
                 "set attack verctor failed sanity test (seek != insert)");
    }
    time_t end_ok = clock();
    FIO_ASSERT(fio_set_attack_count(&as) == FIO_SET_TEST_COUNT,
               "set attack verctor failed sanity test (count error %zu != %zu)",
               fio_set_attack_count(&as), FIO_SET_TEST_COUNT);
    fio_set_attack_free(&as);

    /* full collision attack */
    time_t start_bad = clock();
    for (uintptr_t i = 0; i < FIO_SET_TEST_COUNT; ++i) {
      fio_set_attack_insert(&as, 1, i + 1);
    }
    time_t end_bad = clock();
    FIO_ASSERT(fio_set_attack_count(&as) != FIO_SET_TEST_COUNT,
               "set attack success! too many full-collisions inserts!");
    FIO_LOG_DEBUG("set full-collision attack final count/capa = %zu / %zu",
                  fio_set_attack_count(&as), fio_set_attack_capa(&as));
    FIO_LOG_DEBUG("set full-collision attack timing impact (attack vs. normal) "
                  "%zu vs. %zu",
                  end_bad - start_bad, end_ok - start_ok);
    fio_set_attack_free(&as);

    /* partial collision attack */
    start_bad = clock();
    for (uintptr_t i = 0; i < FIO_SET_TEST_COUNT; ++i) {
      fio_set_attack_insert(&as, ((i << 20) | 1), i + 1);
    }
    end_bad = clock();
    FIO_ASSERT(fio_set_attack_count(&as) == FIO_SET_TEST_COUNT,
               "partial collision resolusion failed, not enough inserts!");
    FIO_LOG_DEBUG("set partial collision attack final count/capa = %zu / %zu",
                  fio_set_attack_count(&as), fio_set_attack_capa(&as));
    FIO_LOG_DEBUG("set partial collision attack timing impact (attack vs. "
                  "normal) %zu vs. %zu",
                  end_bad - start_bad, end_ok - start_ok);
    fio_set_attack_free(&as);
  }
}

/* *****************************************************************************
Bad Hash (risky hash) tests
***************************************************************************** */

FIO_FUNC void fio_riskyhash_speed_test(void) {
  /* test based on code from BearSSL with credit to Thomas Pornin */
  uint8_t buffer[8192];
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  uint64_t hash = 0;
  for (size_t i = 0; i < 4; i++) {
    hash += fio_risky_hash(buffer, 8192, 1);
    memcpy(buffer, &hash, sizeof(hash));
  }
  /* loop until test runs for more than 2 seconds */
  for (uint64_t cycles = 8192;;) {
    clock_t start, end;
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      hash += fio_risky_hash(buffer, 8192, 1);
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    memcpy(buffer, &hash, sizeof(hash));
    if ((end - start) >= (2 * CLOCKS_PER_SEC) ||
        cycles >= ((uint64_t)1 << 62)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "fio_risky_hash",
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 2;
  }
}

FIO_FUNC void fio_riskyhash_test(void) {
  fprintf(stderr, "===================================\n");
#if NODEBUG
  fio_riskyhash_speed_test();
#else
  fprintf(stderr, "fio_risky_hash speed test skipped (debug mode is slow)\n");
  fio_str_info_s str1 =
      (fio_str_info_s){.data = "nothing_is_really_here1", .len = 23};
  fio_str_info_s str2 =
      (fio_str_info_s){.data = "nothing_is_really_here2", .len = 23};
  fio_str_s copy = FIO_STR_INIT;
  FIO_ASSERT(fio_risky_hash(str1.data, str1.len, 1) !=
                 fio_risky_hash(str2.data, str2.len, 1),
             "Different strings should have a different risky hash");
  fio_str_write(&copy, str1.data, str1.len);
  FIO_ASSERT(fio_risky_hash(str1.data, str1.len, 1) ==
                 fio_risky_hash(fio_str_data(&copy), fio_str_len(&copy), 1),
             "Same string values should have the same risky hash");
  fio_str_free(&copy);
  (void)fio_riskyhash_speed_test;
#endif
}

/* *****************************************************************************
SipHash tests
***************************************************************************** */

FIO_FUNC void fio_siphash_speed_test(void) {
  /* test based on code from BearSSL with credit to Thomas Pornin */
  uint8_t buffer[8192];
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  uint64_t hash = 0;
  for (size_t i = 0; i < 4; i++) {
    hash += fio_siphash24(buffer, sizeof(buffer), 0, 0);
    memcpy(buffer, &hash, sizeof(hash));
  }
  /* loop until test runs for more than 2 seconds */
  for (uint64_t cycles = 8192;;) {
    clock_t start, end;
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      hash += fio_siphash24(buffer, sizeof(buffer), 0, 0);
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    memcpy(buffer, &hash, sizeof(hash));
    if ((end - start) >= (2 * CLOCKS_PER_SEC) ||
        cycles >= ((uint64_t)1 << 62)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "fio SipHash24",
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 2;
  }
  /* loop until test runs for more than 2 seconds */
  for (uint64_t cycles = 8192;;) {
    clock_t start, end;
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      hash += fio_siphash13(buffer, sizeof(buffer), 0, 0);
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    memcpy(buffer, &hash, sizeof(hash));
    if ((end - start) >= (2 * CLOCKS_PER_SEC) ||
        cycles >= ((uint64_t)1 << 62)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "fio SipHash13",
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 2;
  }
}

FIO_FUNC void fio_siphash_test(void) {
  fprintf(stderr, "===================================\n");
#if NODEBUG
  fio_siphash_speed_test();
#else
  fprintf(stderr, "fio SipHash speed test skipped (debug mode is slow)\n");
  (void)fio_siphash_speed_test;
#endif
}
/* *****************************************************************************
SHA-1 tests
***************************************************************************** */

FIO_FUNC void fio_sha1_speed_test(void) {
  /* test based on code from BearSSL with credit to Thomas Pornin */
  uint8_t buffer[8192];
  uint8_t result[21];
  fio_sha1_s sha1;
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  for (size_t i = 0; i < 4; i++) {
    sha1 = fio_sha1_init();
    fio_sha1_write(&sha1, buffer, sizeof(buffer));
    memcpy(result, fio_sha1_result(&sha1), 21);
  }
  /* loop until test runs for more than 2 seconds */
  for (size_t cycles = 8192;;) {
    clock_t start, end;
    sha1 = fio_sha1_init();
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      fio_sha1_write(&sha1, buffer, sizeof(buffer));
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    fio_sha1_result(&sha1);
    if ((end - start) >= (2 * CLOCKS_PER_SEC)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "fio SHA-1",
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 1;
  }
}

#ifdef HAVE_OPENSSL
FIO_FUNC void fio_sha1_open_ssl_speed_test(void) {
  /* test based on code from BearSSL with credit to Thomas Pornin */
  uint8_t buffer[8192];
  uint8_t result[21];
  SHA_CTX o_sh1;
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  for (size_t i = 0; i < 4; i++) {
    SHA1_Init(&o_sh1);
    SHA1_Update(&o_sh1, buffer, sizeof(buffer));
    SHA1_Final(result, &o_sh1);
  }
  /* loop until test runs for more than 2 seconds */
  for (size_t cycles = 8192;;) {
    clock_t start, end;
    SHA1_Init(&o_sh1);
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      SHA1_Update(&o_sh1, buffer, sizeof(buffer));
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    SHA1_Final(result, &o_sh1);
    if ((end - start) >= (2 * CLOCKS_PER_SEC)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "OpenSSL SHA-1",
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 1;
  }
}
#endif

FIO_FUNC void fio_sha1_test(void) {
  // clang-format off
  struct {
    char *str;
    uint8_t hash[21];
  } sets[] = {
      {"The quick brown fox jumps over the lazy dog",
       {0x2f, 0xd4, 0xe1, 0xc6, 0x7a, 0x2d, 0x28, 0xfc, 0xed, 0x84, 0x9e,
        0xe1, 0xbb, 0x76, 0xe7, 0x39, 0x1b, 0x93, 0xeb, 0x12, 0}}, // a set with
                                                                   // a string
      {"",
       {
           0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
           0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09,
       }},        // an empty set
      {NULL, {0}} // Stop
  };
  // clang-format on
  int i = 0;
  fio_sha1_s sha1;
  fprintf(stderr, "===================================\n");
  fprintf(stderr, "fio SHA-1 struct size: %zu\n", sizeof(fio_sha1_s));
  fprintf(stderr, "+ fio");
  while (sets[i].str) {
    sha1 = fio_sha1_init();
    fio_sha1_write(&sha1, sets[i].str, strlen(sets[i].str));
    if (strcmp(fio_sha1_result(&sha1), (char *)sets[i].hash)) {
      fprintf(stderr, ":\n--- fio SHA-1 Test FAILED!\nstring: %s\nexpected: ",
              sets[i].str);
      char *p = (char *)sets[i].hash;
      while (*p)
        fprintf(stderr, "%02x", *(p++) & 0xFF);
      fprintf(stderr, "\ngot: ");
      p = fio_sha1_result(&sha1);
      while (*p)
        fprintf(stderr, "%02x", *(p++) & 0xFF);
      fprintf(stderr, "\n");
      FIO_ASSERT(0, "SHA-1 failure.");
      return;
    }
    i++;
  }
  fprintf(stderr, " SHA-1 passed.\n");
#if NODEBUG
  fio_sha1_speed_test();
#else
  fprintf(stderr, "fio SHA1 speed test skipped (debug mode is slow)\n");
  (void)fio_sha1_speed_test;
#endif

#ifdef HAVE_OPENSSL

#if NODEBUG
  fio_sha1_open_ssl_speed_test();
#else
  fprintf(stderr, "OpenSSL SHA1 speed test skipped (debug mode is slow)\n");
  (void)fio_sha1_open_ssl_speed_test;
#endif
  fprintf(stderr, "===================================\n");
  fprintf(stderr, "fio SHA-1 struct size: %lu\n",
          (unsigned long)sizeof(fio_sha1_s));
  fprintf(stderr, "OpenSSL SHA-1 struct size: %lu\n",
          (unsigned long)sizeof(SHA_CTX));
  fprintf(stderr, "===================================\n");
#endif /* HAVE_OPENSSL */
}

/* *****************************************************************************
SHA-2 tests
***************************************************************************** */

FIO_FUNC char *sha2_variant_names[] = {
    "unknown", "SHA_512",     "SHA_256", "SHA_512_256",
    "SHA_224", "SHA_512_224", "none",    "SHA_384",
};

FIO_FUNC void fio_sha2_speed_test(fio_sha2_variant_e var,
                                  const char *var_name) {
  /* test based on code from BearSSL with credit to Thomas Pornin */
  uint8_t buffer[8192];
  uint8_t result[65];
  fio_sha2_s sha2;
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  for (size_t i = 0; i < 4; i++) {
    sha2 = fio_sha2_init(var);
    fio_sha2_write(&sha2, buffer, sizeof(buffer));
    memcpy(result, fio_sha2_result(&sha2), 65);
  }
  /* loop until test runs for more than 2 seconds */
  for (size_t cycles = 8192;;) {
    clock_t start, end;
    sha2 = fio_sha2_init(var);
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      fio_sha2_write(&sha2, buffer, sizeof(buffer));
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    fio_sha2_result(&sha2);
    if ((end - start) >= (2 * CLOCKS_PER_SEC)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", var_name,
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 1;
  }
}

FIO_FUNC void fio_sha2_openssl_speed_test(const char *var_name, int (*init)(),
                                          int (*update)(), int (*final)(),
                                          void *sha) {
  /* test adapted from BearSSL code with credit to Thomas Pornin */
  uint8_t buffer[8192];
  uint8_t result[1024];
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  for (size_t i = 0; i < 4; i++) {
    init(sha);
    update(sha, buffer, sizeof(buffer));
    final(result, sha);
  }
  /* loop until test runs for more than 2 seconds */
  for (size_t cycles = 2048;;) {
    clock_t start, end;
    init(sha);
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      update(sha, buffer, sizeof(buffer));
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    final(result, sha);
    if ((end - start) >= (2 * CLOCKS_PER_SEC)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", var_name,
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 1;
  }
}
FIO_FUNC void fio_sha2_test(void) {
  fio_sha2_s s;
  char *expect;
  char *got;
  char *str = "";
  fprintf(stderr, "===================================\n");
  fprintf(stderr, "fio SHA-2 struct size: %zu\n", sizeof(fio_sha2_s));
  fprintf(stderr, "+ fio");
  // start tests
  s = fio_sha2_init(SHA_224);
  fio_sha2_write(&s, str, 0);
  expect = "\xd1\x4a\x02\x8c\x2a\x3a\x2b\xc9\x47\x61\x02\xbb\x28\x82\x34\xc4"
           "\x15\xa2\xb0\x1f\x82\x8e\xa6\x2a\xc5\xb3\xe4\x2f";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  s = fio_sha2_init(SHA_256);
  fio_sha2_write(&s, str, 0);
  expect =
      "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27"
      "\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  s = fio_sha2_init(SHA_512);
  fio_sha2_write(&s, str, 0);
  expect = "\xcf\x83\xe1\x35\x7e\xef\xb8\xbd\xf1\x54\x28\x50\xd6\x6d"
           "\x80\x07\xd6\x20\xe4\x05\x0b\x57\x15\xdc\x83\xf4\xa9\x21"
           "\xd3\x6c\xe9\xce\x47\xd0\xd1\x3c\x5d\x85\xf2\xb0\xff\x83"
           "\x18\xd2\x87\x7e\xec\x2f\x63\xb9\x31\xbd\x47\x41\x7a\x81"
           "\xa5\x38\x32\x7a\xf9\x27\xda\x3e";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  s = fio_sha2_init(SHA_384);
  fio_sha2_write(&s, str, 0);
  expect = "\x38\xb0\x60\xa7\x51\xac\x96\x38\x4c\xd9\x32\x7e"
           "\xb1\xb1\xe3\x6a\x21\xfd\xb7\x11\x14\xbe\x07\x43\x4c\x0c"
           "\xc7\xbf\x63\xf6\xe1\xda\x27\x4e\xde\xbf\xe7\x6f\x65\xfb"
           "\xd5\x1a\xd2\xf1\x48\x98\xb9\x5b";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  s = fio_sha2_init(SHA_512_224);
  fio_sha2_write(&s, str, 0);
  expect = "\x6e\xd0\xdd\x02\x80\x6f\xa8\x9e\x25\xde\x06\x0c\x19\xd3"
           "\xac\x86\xca\xbb\x87\xd6\xa0\xdd\xd0\x5c\x33\x3b\x84\xf4";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  s = fio_sha2_init(SHA_512_256);
  fio_sha2_write(&s, str, 0);
  expect = "\xc6\x72\xb8\xd1\xef\x56\xed\x28\xab\x87\xc3\x62\x2c\x51\x14\x06"
           "\x9b\xdd\x3a\xd7\xb8\xf9\x73\x74\x98\xd0\xc0\x1e\xce\xf0\x96\x7a";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  s = fio_sha2_init(SHA_512);
  str = "god is a rotten tomato";
  fio_sha2_write(&s, str, strlen(str));
  expect = "\x61\x97\x4d\x41\x9f\x77\x45\x21\x09\x4e\x95\xa3\xcb\x4d\xe4\x79"
           "\x26\x32\x2f\x2b\xe2\x62\x64\x5a\xb4\x5d\x3f\x73\x69\xef\x46\x20"
           "\xb2\xd3\xce\xda\xa9\xc2\x2c\xac\xe3\xf9\x02\xb2\x20\x5d\x2e\xfd"
           "\x40\xca\xa0\xc1\x67\xe0\xdc\xdf\x60\x04\x3e\x4e\x76\x87\x82\x74";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;

  // s = fio_sha2_init(SHA_256);
  // str = "The quick brown fox jumps over the lazy dog";
  // fio_sha2_write(&s, str, strlen(str));
  // expect =
  //     "\xd7\xa8\xfb\xb3\x07\xd7\x80\x94\x69\xca\x9a\xbc\xb0\x08\x2e\x4f"
  //     "\x8d\x56\x51\xe4\x6d\x3c\xdb\x76\x2d\x02\xd0\xbf\x37\xc9\xe5\x92";
  // got = fio_sha2_result(&s);
  // if (strcmp(expect, got))
  //   goto error;

  s = fio_sha2_init(SHA_224);
  str = "The quick brown fox jumps over the lazy dog";
  fio_sha2_write(&s, str, strlen(str));
  expect = "\x73\x0e\x10\x9b\xd7\xa8\xa3\x2b\x1c\xb9\xd9\xa0\x9a\xa2"
           "\x32\x5d\x24\x30\x58\x7d\xdb\xc0\xc3\x8b\xad\x91\x15\x25";
  got = fio_sha2_result(&s);
  if (strcmp(expect, got))
    goto error;
  fprintf(stderr, " SHA-2 passed.\n");

#if NODEBUG
  fio_sha2_speed_test(SHA_224, "fio SHA-224");
  fio_sha2_speed_test(SHA_256, "fio SHA-256");
  fio_sha2_speed_test(SHA_384, "fio SHA-384");
  fio_sha2_speed_test(SHA_512, "fio SHA-512");
#else
  fprintf(stderr, "fio SHA-2 speed test skipped (debug mode is slow)\n");
#endif

#ifdef HAVE_OPENSSL

#if NODEBUG
  {
    SHA512_CTX s2;
    SHA256_CTX s3;
    fio_sha2_openssl_speed_test("OpenSSL SHA512", SHA512_Init, SHA512_Update,
                                SHA512_Final, &s2);
    fio_sha2_openssl_speed_test("OpenSSL SHA256", SHA256_Init, SHA256_Update,
                                SHA256_Final, &s3);
  }
#endif
  fprintf(stderr, "===================================\n");
  fprintf(stderr, "fio SHA-2 struct size: %zu\n", sizeof(fio_sha2_s));
  fprintf(stderr, "OpenSSL SHA-2/256 struct size: %zu\n", sizeof(SHA256_CTX));
  fprintf(stderr, "OpenSSL SHA-2/512 struct size: %zu\n", sizeof(SHA512_CTX));
  fprintf(stderr, "===================================\n");
#endif /* HAVE_OPENSSL */

  return;

error:
  fprintf(stderr,
          ":\n--- fio SHA-2 Test FAILED!\ntype: "
          "%s (%d)\nstring %s\nexpected:\n",
          sha2_variant_names[s.type], s.type, str);
  while (*expect)
    fprintf(stderr, "%02x", *(expect++) & 0xFF);
  fprintf(stderr, "\ngot:\n");
  while (*got)
    fprintf(stderr, "%02x", *(got++) & 0xFF);
  fprintf(stderr, "\n");
  (void)fio_sha2_speed_test;
  (void)fio_sha2_openssl_speed_test;
  FIO_ASSERT(0, "SHA-2 failure.");
}

/* *****************************************************************************
Base64 tests
***************************************************************************** */

FIO_FUNC void fio_base64_speed_test(void) {
  /* test based on code from BearSSL with credit to Thomas Pornin */
  char buffer[8192];
  char result[8192 * 2];
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  for (size_t i = 0; i < 4; i++) {
    fio_base64_encode(result, buffer, sizeof(buffer));
    memcpy(buffer, result, sizeof(buffer));
  }
  /* loop until test runs for more than 2 seconds */
  for (size_t cycles = 8192;;) {
    clock_t start, end;
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      fio_base64_encode(result, buffer, sizeof(buffer));
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    if ((end - start) >= (2 * CLOCKS_PER_SEC)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "fio Base64 Encode",
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 2;
  }

  /* speed test decoding */
  const int encoded_len =
      fio_base64_encode(result, buffer, (int)(sizeof(buffer) - 2));
  /* warmup */
  for (size_t i = 0; i < 4; i++) {
    fio_base64_decode(buffer, result, encoded_len);
    __asm__ volatile("" ::: "memory");
  }
  /* loop until test runs for more than 2 seconds */
  for (size_t cycles = 8192;;) {
    clock_t start, end;
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      fio_base64_decode(buffer, result, encoded_len);
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    if ((end - start) >= (2 * CLOCKS_PER_SEC)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", "fio Base64 Decode",
              (double)(encoded_len * cycles) /
                  (((end - start) * 1000000.0 / CLOCKS_PER_SEC)));
      break;
    }
    cycles <<= 2;
  }
}

FIO_FUNC void fio_base64_test(void) {
  struct {
    char *str;
    char *base64;
  } sets[] = {
      {"Man is distinguished, not only by his reason, but by this singular "
       "passion from other animals, which is a lust of the mind, that by a "
       "perseverance of delight in the continued "
       "and indefatigable generation "
       "of knowledge, exceeds the short vehemence of any carnal pleasure.",
       "TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB"
       "0aGlzIHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIG"
       "x1c3Qgb2YgdGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpb"
       "iB0aGUgY29udGludWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xl"
       "ZGdlLCBleGNlZWRzIHRoZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3V"
       "yZS4="},
      {"any carnal pleasure.", "YW55IGNhcm5hbCBwbGVhc3VyZS4="},
      {"any carnal pleasure", "YW55IGNhcm5hbCBwbGVhc3VyZQ=="},
      {"any carnal pleasur", "YW55IGNhcm5hbCBwbGVhc3Vy"},
      {"", ""},
      {"f", "Zg=="},
      {"fo", "Zm8="},
      {"foo", "Zm9v"},
      {"foob", "Zm9vYg=="},
      {"fooba", "Zm9vYmE="},
      {"foobar", "Zm9vYmFy"},
      {NULL, NULL} // Stop
  };
  int i = 0;
  char buffer[1024];
  fprintf(stderr, "===================================\n");
  fprintf(stderr, "+ fio");
  while (sets[i].str) {
    fio_base64_encode(buffer, sets[i].str, strlen(sets[i].str));
    if (strcmp(buffer, sets[i].base64)) {
      fprintf(stderr,
              ":\n--- fio Base64 Test FAILED!\nstring: %s\nlength: %lu\n "
              "expected: %s\ngot: %s\n\n",
              sets[i].str, strlen(sets[i].str), sets[i].base64, buffer);
      FIO_ASSERT(0, "Base64 failure.");
    }
    i++;
  }
  if (!sets[i].str)
    fprintf(stderr, " Base64 encode passed.\n");

  i = 0;
  fprintf(stderr, "+ fio");
  while (sets[i].str) {
    fio_base64_decode(buffer, sets[i].base64, strlen(sets[i].base64));
    if (strcmp(buffer, sets[i].str)) {
      fprintf(stderr,
              ":\n--- fio Base64 Test FAILED!\nbase64: %s\nexpected: "
              "%s\ngot: %s\n\n",
              sets[i].base64, sets[i].str, buffer);
      FIO_ASSERT(0, "Base64 failure.");
    }
    i++;
  }
  fprintf(stderr, " Base64 decode passed.\n");

#if NODEBUG
  fio_base64_speed_test();
#else
  fprintf(stderr,
          "* Base64 speed test skipped (debug speeds are always slow).\n");
  (void)fio_base64_speed_test;
#endif
}

/*******************************************************************************
Random Testing
***************************************************************************** */

FIO_FUNC void fio_test_random(void) {
  fprintf(stderr, "=== Testing random generator\n");
  uint64_t rnd = fio_rand64();
  FIO_ASSERT((rnd != fio_rand64() && rnd != fio_rand64()),
             "fio_rand64 returned the same result three times in a row.");
#if NODEBUG
  uint64_t buffer1[8];
  uint8_t buffer2[8192];
  clock_t start, end;
  start = clock();
  for (size_t i = 0; i < (8388608 / (64 / 8)); i++) {
    buffer1[i & 7] = fio_rand64();
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr,
          "+ Random generator available\n+ created 8Mb using 64bits "
          "Random %lu CPU clock count ~%.2fMb/s\n",
          end - start, (8.0) / (((double)(end - start)) / CLOCKS_PER_SEC));
  start = clock();
  for (size_t i = 0; i < (8388608 / (8192)); i++) {
    fio_rand_bytes(buffer2, 8192);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr,
          "+ created 8Mb using 8,192 Bytes "
          "Random %lu CPU clock count ~%.2fMb/s\n",
          end - start, (8.0) / (((double)(end - start)) / CLOCKS_PER_SEC));
  (void)buffer1;
  (void)buffer2;
#endif
}

/* *****************************************************************************
Poll (not kqueue or epoll) tests
***************************************************************************** */
#if FIO_ENGINE_POLL
FIO_FUNC void fio_poll_test(void) {
  fprintf(stderr, "=== Testing poll add / remove fd\n");
  fio_poll_add(5);
  FIO_ASSERT(fio_data->poll[5].fd == 5, "fio_poll_add didn't set used fd data");
  FIO_ASSERT(fio_data->poll[5].events ==
                 (FIO_POLL_READ_EVENTS | FIO_POLL_WRITE_EVENTS),
             "fio_poll_add didn't set used fd flags");
  fio_poll_add(7);
  FIO_ASSERT(fio_data->poll[6].fd == -1,
             "fio_poll_add didn't reset unused fd data %d",
             fio_data->poll[6].fd);
  fio_poll_add(6);
  fio_poll_remove_fd(6);
  FIO_ASSERT(fio_data->poll[6].fd == -1,
             "fio_poll_remove_fd didn't reset unused fd data");
  FIO_ASSERT(fio_data->poll[6].events == 0,
             "fio_poll_remove_fd didn't reset unused fd flags");
  fio_poll_remove_read(7);
  FIO_ASSERT(fio_data->poll[7].events == (FIO_POLL_WRITE_EVENTS),
             "fio_poll_remove_read didn't remove read flags");
  fio_poll_add_read(7);
  fio_poll_remove_write(7);
  FIO_ASSERT(fio_data->poll[7].events == (FIO_POLL_READ_EVENTS),
             "fio_poll_remove_write didn't remove read flags");
  fio_poll_add_write(7);
  fio_poll_remove_read(7);
  FIO_ASSERT(fio_data->poll[7].events == (FIO_POLL_WRITE_EVENTS),
             "fio_poll_add_write didn't add the write flag?");
  fio_poll_remove_write(7);
  FIO_ASSERT(fio_data->poll[7].fd == -1,
             "fio_poll_remove (both) didn't reset unused fd data");
  FIO_ASSERT(fio_data->poll[7].events == 0,
             "fio_poll_remove (both) didn't reset unused fd flags");
  fio_poll_remove_fd(5);
  fprintf(stderr, "\n* passed.\n");
}
#else
#define fio_poll_test()
#endif

/* *****************************************************************************
Test UUID Linking
***************************************************************************** */

FIO_FUNC void fio_uuid_link_test_on_close(void *obj) {
  fio_atomic_add((uintptr_t *)obj, 1);
}

FIO_FUNC void fio_uuid_link_test(void) {
  fprintf(stderr, "=== Testing fio_uuid_link\n");
  uintptr_t called = 0;
  uintptr_t removed = 0;
  intptr_t uuid = fio_socket(NULL, "8765", 1);
  FIO_ASSERT(uuid != -1, "fio_uuid_link_test failed to create a socket!");
  fio_uuid_link(uuid, &called, fio_uuid_link_test_on_close);
  FIO_ASSERT(called == 0,
             "fio_uuid_link failed - on_close callback called too soon!");
  fio_uuid_link(uuid, &removed, fio_uuid_link_test_on_close);
  fio_uuid_unlink(uuid, &removed);
  fio_close(uuid);
  fio_defer_perform();
  FIO_ASSERT(called, "fio_uuid_link failed - on_close callback wasn't called!");
  FIO_ASSERT(called, "fio_uuid_unlink failed - on_close callback was called "
                     "(wasn't removed)!");
  fprintf(stderr, "* passed.\n");
}

/* *****************************************************************************
Byte Order Testing
***************************************************************************** */

FIO_FUNC void fio_str2u_test(void) {
  fprintf(stderr, "=== Testing fio_u2strX and fio_u2strX functions.\n");
  char buffer[32];
  for (int64_t i = -1024; i < 1024; ++i) {
    fio_u2str64(buffer, i);
    __asm__ volatile("" ::: "memory");
    FIO_ASSERT((int64_t)fio_str2u64(buffer) == i,
               "fio_u2str64 / fio_str2u64  mismatch %zd != %zd",
               (ssize_t)fio_str2u64(buffer), (ssize_t)i);
  }
  for (int32_t i = -1024; i < 1024; ++i) {
    fio_u2str32(buffer, i);
    __asm__ volatile("" ::: "memory");
    FIO_ASSERT((int32_t)fio_str2u32(buffer) == i,
               "fio_u2str32 / fio_str2u32  mismatch %zd != %zd",
               (ssize_t)(fio_str2u32(buffer)), (ssize_t)i);
  }
  for (int16_t i = -1024; i < 1024; ++i) {
    fio_u2str16(buffer, i);
    __asm__ volatile("" ::: "memory");
    FIO_ASSERT((int16_t)fio_str2u16(buffer) == i,
               "fio_u2str16 / fio_str2u16  mismatch %zd != %zd",
               (ssize_t)(fio_str2u16(buffer)), (ssize_t)i);
  }
  fprintf(stderr, "* passed.\n");
}

/* *****************************************************************************
Pub/Sub partial tests
***************************************************************************** */

#if FIO_PUBSUB_SUPPORT

FIO_FUNC void fio_pubsub_test_on_message(fio_msg_s *msg) {
  fio_atomic_add((uintptr_t *)msg->udata1, 1);
}
FIO_FUNC void fio_pubsub_test_on_unsubscribe(void *udata1, void *udata2) {
  fio_atomic_add((uintptr_t *)udata1, 1);
  (void)udata2;
}

FIO_FUNC void fio_pubsub_test(void) {
  fprintf(stderr, "=== Testing pub/sub (partial)\n");
  fio_data->active = 1;
  fio_data->is_worker = 1;
  fio_data->workers = 1;
  subscription_s *s = fio_subscribe(.filter = 1, .on_message = NULL);
  uintptr_t counter = 0;
  uintptr_t expect = 0;
  FIO_ASSERT(!s, "fio_subscribe should fail without a callback!");
  char buffer[8];
  fio_u2str32((uint8_t *)buffer + 1, 42);
  FIO_ASSERT(fio_str2u32((uint8_t *)buffer + 1) == 42,
             "fio_u2str32 / fio_str2u32 not reversible (42)!");
  fio_u2str32((uint8_t *)buffer, 4);
  FIO_ASSERT(fio_str2u32((uint8_t *)buffer) == 4,
             "fio_u2str32 / fio_str2u32 not reversible (4)!");
  subscription_s *s2 =
      fio_subscribe(.filter = 1, .udata1 = &counter,
                    .on_message = fio_pubsub_test_on_message,
                    .on_unsubscribe = fio_pubsub_test_on_unsubscribe);
  FIO_ASSERT(s2, "fio_subscribe FAILED on filtered subscription.");
  fio_publish(.filter = 1);
  ++expect;
  fio_defer_perform();
  FIO_ASSERT(counter == expect, "publishing failed to filter 1!");
  fio_publish(.filter = 2);
  fio_defer_perform();
  FIO_ASSERT(counter == expect, "publishing to filter 2 arrived at filter 1!");
  fio_unsubscribe(s);
  fio_unsubscribe(s2);
  ++expect;
  fio_defer_perform();
  FIO_ASSERT(counter == expect, "unsubscribe wasn't called for filter 1!");
  s = fio_subscribe(.channel = {0, 4, "name"}, .udata1 = &counter,
                    .on_message = fio_pubsub_test_on_message,
                    .on_unsubscribe = fio_pubsub_test_on_unsubscribe);
  FIO_ASSERT(s, "fio_subscribe FAILED on named subscription.");
  fio_publish(.channel = {0, 4, "name"});
  ++expect;
  fio_defer_perform();
  FIO_ASSERT(counter == expect, "publishing failed to named channel!");
  fio_publish(.channel = {0, 4, "none"});
  fio_defer_perform();
  FIO_ASSERT(counter == expect,
             "publishing arrived to named channel with wrong name!");
  fio_unsubscribe(s);
  ++expect;
  fio_defer_perform();
  FIO_ASSERT(counter == expect, "unsubscribe wasn't called for named channel!");
  fio_data->is_worker = 0;
  fio_data->active = 0;
  fio_data->workers = 0;
  fio_defer_perform();
  (void)fio_pubsub_test_on_message;
  (void)fio_pubsub_test_on_unsubscribe;
  fprintf(stderr, "* passed.\n");
}
#else
#define fio_pubsub_test()
#endif

/* *****************************************************************************
String 2 Number and Number 2 String (partial) testing
***************************************************************************** */

#if NODEBUG
#define FIO_ATOL_TEST_MAX_CYCLES 3145728
#else
#define FIO_ATOL_TEST_MAX_CYCLES 4096
#endif
FIO_FUNC void fio_atol_test(void) {
  fprintf(stderr, "=== Testing fio_ltoa and fio_atol (partial)\n");
#ifndef NODEBUG
  fprintf(stderr,
          "Note: No optimizations - facil.io performance will be slow.\n");
#endif
  fprintf(stderr,
          "      Test with make test/optimized for realistic results.\n");
  time_t start, end;

#define TEST_ATOL(s, n)                                                        \
  do {                                                                         \
    char *p = (char *)(s);                                                     \
    int64_t r = fio_atol(&p);                                                  \
    FIO_ASSERT(r == (n), "fio_atol test error! %s => %zd (not %zd)",           \
               ((char *)(s)), (size_t)r, (size_t)n);                           \
    FIO_ASSERT((s) + strlen((s)) == p,                                         \
               "fio_atol test error! %s reading position not at end (%zu)",    \
               (s), (size_t)(p - (s)));                                        \
    char buf[72];                                                              \
    buf[fio_ltoa(buf, n, 2)] = 0;                                              \
    p = buf;                                                                   \
    FIO_ASSERT(fio_atol(&p) == (n),                                            \
               "fio_ltoa base 2 test error! "                                  \
               "%s != %s (%zd)",                                               \
               buf, ((char *)(s)), (size_t)((p = buf), fio_atol(&p)));         \
    buf[fio_ltoa(buf, n, 8)] = 0;                                              \
    p = buf;                                                                   \
    FIO_ASSERT(fio_atol(&p) == (n),                                            \
               "fio_ltoa base 8 test error! "                                  \
               "%s != %s (%zd)",                                               \
               buf, ((char *)(s)), (size_t)((p = buf), fio_atol(&p)));         \
    buf[fio_ltoa(buf, n, 10)] = 0;                                             \
    p = buf;                                                                   \
    FIO_ASSERT(fio_atol(&p) == (n),                                            \
               "fio_ltoa base 10 test error! "                                 \
               "%s != %s (%zd)",                                               \
               buf, ((char *)(s)), (size_t)((p = buf), fio_atol(&p)));         \
    buf[fio_ltoa(buf, n, 16)] = 0;                                             \
    p = buf;                                                                   \
    FIO_ASSERT(fio_atol(&p) == (n),                                            \
               "fio_ltoa base 16 test error! "                                 \
               "%s != %s (%zd)",                                               \
               buf, ((char *)(s)), (size_t)((p = buf), fio_atol(&p)));         \
  } while (0)
  TEST_ATOL("0x1", 1);
  TEST_ATOL("-0x1", -1);
  TEST_ATOL("-0xa", -10);                                /* sign before hex */
  TEST_ATOL("0xe5d4c3b2a1908770", -1885667171979196560); /* sign within hex */
  TEST_ATOL("0b00000000000011", 3);
  TEST_ATOL("-0b00000000000011", -3);
  TEST_ATOL("0b0000000000000000000000000000000000000000000000000", 0);
  TEST_ATOL("0", 0);
  TEST_ATOL("1", 1);
  TEST_ATOL("2", 2);
  TEST_ATOL("-2", -2);
  TEST_ATOL("0000000000000000000000000000000000000000000000042", 34); /* oct */
  TEST_ATOL("9223372036854775807", 9223372036854775807LL); /* INT64_MAX */
  TEST_ATOL("9223372036854775808",
            9223372036854775807LL); /* INT64_MAX overflow protection */
  TEST_ATOL("9223372036854775999",
            9223372036854775807LL); /* INT64_MAX overflow protection */

  char number_hex[128] = "0xe5d4c3b2a1908770"; /* hex with embedded sign */
  // char number_hex[128] = "-0x1a2b3c4d5e6f7890";
  char number[128] = "-1885667171979196560";
  intptr_t expect = -1885667171979196560;
  intptr_t result = 0;

  result = 0;

  start = clock();
  for (size_t i = 0; i < FIO_ATOL_TEST_MAX_CYCLES; ++i) {
    __asm__ volatile("" ::: "memory");
    char *pos = number;
    result = fio_atol(&pos);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr, "fio_atol base 10 (%ld): %zd CPU cycles\n", result,
          end - start);

  result = 0;
  start = clock();
  for (size_t i = 0; i < FIO_ATOL_TEST_MAX_CYCLES; ++i) {
    __asm__ volatile("" ::: "memory");
    result = strtol(number, NULL, 0);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr, "native strtol base 10 (%ld): %zd CPU cycles\n", result,
          end - start);

  result = 0;
  start = clock();
  for (size_t i = 0; i < FIO_ATOL_TEST_MAX_CYCLES; ++i) {
    __asm__ volatile("" ::: "memory");
    char *pos = number_hex;
    result = fio_atol(&pos);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr, "fio_atol base 16 (%ld): %zd CPU cycles\n", result,
          end - start);

  result = 0;
  start = clock();
  for (size_t i = 0; i < FIO_ATOL_TEST_MAX_CYCLES; ++i) {
    __asm__ volatile("" ::: "memory");
    result = strtol(number_hex, NULL, 0);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr, "native strtol base 16 (%ld): %zd CPU cycles%s\n", result,
          end - start, (result != expect ? " (!?stdlib overflow?!)" : ""));

  result = 0;
  start = clock();
  for (size_t i = 0; i < FIO_ATOL_TEST_MAX_CYCLES; ++i) {
    __asm__ volatile("" ::: "memory");
    fio_ltoa(number, expect, 10);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  {
    char *buf = number;
    FIO_ASSERT(fio_atol(&buf) == expect,
               "fio_ltoa with base 10 returned wrong result (%s != %ld)",
               number, expect);
  }
  fprintf(stderr, "fio_ltoa base 10 (%s): %zd CPU cycles\n", number,
          end - start);

  result = 0;
  start = clock();
  for (size_t i = 0; i < FIO_ATOL_TEST_MAX_CYCLES; ++i) {
    __asm__ volatile("" ::: "memory");
    sprintf(number, "%ld", expect);
    __asm__ volatile("" ::: "memory");
  }
  end = clock();
  fprintf(stderr, "native sprintf base 10 (%s): %zd CPU cycles\n", number,
          end - start);
  FIO_ASSERT(fio_ltoa(number, 0, 0) == 1,
             "base 10 zero should be single char.");
  FIO_ASSERT(memcmp(number, "0", 2) == 0, "base 10 zero should be \"0\" (%s).",
             number);
  fprintf(stderr, "* passed.\n");
#undef TEST_ATOL
}

/* *****************************************************************************
String 2 Float and Float 2 String (partial) testing
***************************************************************************** */

FIO_FUNC void fio_atof_test(void) {
  fprintf(stderr, "=== Testing fio_ftoa and fio_ftoa (partial)\n");
#define TEST_DOUBLE(s, d, must)                                                \
  do {                                                                         \
    char *p = (char *)(s);                                                     \
    double r = fio_atof(&p);                                                   \
    if (r != (d)) {                                                            \
      FIO_LOG_DEBUG("Double Test Error! %s => %.19g (not %.19g)",              \
                    ((char *)(s)), r, d);                                      \
      if (must) {                                                              \
        FIO_ASSERT(0, "double test failed on %s", ((char *)(s)));              \
        exit(-1);                                                              \
      }                                                                        \
    }                                                                          \
  } while (0)
  /* The numbers were copied from https://github.com/miloyip/rapidjson */
  TEST_DOUBLE("0.0", 0.0, 1);
  TEST_DOUBLE("-0.0", -0.0, 1);
  TEST_DOUBLE("1.0", 1.0, 1);
  TEST_DOUBLE("-1.0", -1.0, 1);
  TEST_DOUBLE("1.5", 1.5, 1);
  TEST_DOUBLE("-1.5", -1.5, 1);
  TEST_DOUBLE("3.1416", 3.1416, 1);
  TEST_DOUBLE("1E10", 1E10, 1);
  TEST_DOUBLE("1e10", 1e10, 1);
  TEST_DOUBLE("1E+10", 1E+10, 1);
  TEST_DOUBLE("1E-10", 1E-10, 1);
  TEST_DOUBLE("-1E10", -1E10, 1);
  TEST_DOUBLE("-1e10", -1e10, 1);
  TEST_DOUBLE("-1E+10", -1E+10, 1);
  TEST_DOUBLE("-1E-10", -1E-10, 1);
  TEST_DOUBLE("1.234E+10", 1.234E+10, 1);
  TEST_DOUBLE("1.234E-10", 1.234E-10, 1);
  TEST_DOUBLE("1.79769e+308", 1.79769e+308, 1);
  TEST_DOUBLE("2.22507e-308", 2.22507e-308, 1);
  TEST_DOUBLE("-1.79769e+308", -1.79769e+308, 1);
  TEST_DOUBLE("-2.22507e-308", -2.22507e-308, 1);
  TEST_DOUBLE("4.9406564584124654e-324", 4.9406564584124654e-324, 0);
  TEST_DOUBLE("2.2250738585072009e-308", 2.2250738585072009e-308, 0);
  TEST_DOUBLE("2.2250738585072014e-308", 2.2250738585072014e-308, 1);
  TEST_DOUBLE("1.7976931348623157e+308", 1.7976931348623157e+308, 1);
  TEST_DOUBLE("1e-10000", 0.0, 0);
  TEST_DOUBLE("18446744073709551616", 18446744073709551616.0, 0);

  TEST_DOUBLE("-9223372036854775809", -9223372036854775809.0, 0);

  TEST_DOUBLE("0.9868011474609375", 0.9868011474609375, 0);
  TEST_DOUBLE("123e34", 123e34, 1);
  TEST_DOUBLE("45913141877270640000.0", 45913141877270640000.0, 1);
  TEST_DOUBLE("2.2250738585072011e-308", 2.2250738585072011e-308, 0);
  TEST_DOUBLE("1e-214748363", 0.0, 1);
  TEST_DOUBLE("1e-214748364", 0.0, 1);
  TEST_DOUBLE("0.017976931348623157e+310, 1", 1.7976931348623157e+308, 0);

  TEST_DOUBLE("2.2250738585072012e-308", 2.2250738585072014e-308, 0);
  TEST_DOUBLE("2.22507385850720113605740979670913197593481954635164565e-308",
              2.2250738585072014e-308, 0);

  TEST_DOUBLE("0.999999999999999944488848768742172978818416595458984375", 1.0,
              0);
  TEST_DOUBLE("0.999999999999999944488848768742172978818416595458984376", 1.0,
              0);
  TEST_DOUBLE("1.00000000000000011102230246251565404236316680908203125", 1.0,
              0);
  TEST_DOUBLE("1.00000000000000011102230246251565404236316680908203124", 1.0,
              0);

  TEST_DOUBLE("72057594037927928.0", 72057594037927928.0, 0);
  TEST_DOUBLE("72057594037927936.0", 72057594037927936.0, 0);
  TEST_DOUBLE("72057594037927932.0", 72057594037927936.0, 0);
  TEST_DOUBLE("7205759403792793200001e-5", 72057594037927936.0, 0);

  TEST_DOUBLE("9223372036854774784.0", 9223372036854774784.0, 0);
  TEST_DOUBLE("9223372036854775808.0", 9223372036854775808.0, 0);
  TEST_DOUBLE("9223372036854775296.0", 9223372036854775808.0, 0);
  TEST_DOUBLE("922337203685477529600001e-5", 9223372036854775808.0, 0);

  TEST_DOUBLE("10141204801825834086073718800384",
              10141204801825834086073718800384.0, 0);
  TEST_DOUBLE("10141204801825835211973625643008",
              10141204801825835211973625643008.0, 0);
  TEST_DOUBLE("10141204801825834649023672221696",
              10141204801825835211973625643008.0, 0);
  TEST_DOUBLE("1014120480182583464902367222169600001e-5",
              10141204801825835211973625643008.0, 0);

  TEST_DOUBLE("5708990770823838890407843763683279797179383808",
              5708990770823838890407843763683279797179383808.0, 0);
  TEST_DOUBLE("5708990770823839524233143877797980545530986496",
              5708990770823839524233143877797980545530986496.0, 0);
  TEST_DOUBLE("5708990770823839207320493820740630171355185152",
              5708990770823839524233143877797980545530986496.0, 0);
  TEST_DOUBLE("5708990770823839207320493820740630171355185152001e-3",
              5708990770823839524233143877797980545530986496.0, 0);
  fprintf(stderr, "\n* passed.\n");
}
/* *****************************************************************************
Run all tests
***************************************************************************** */

void fio_test(void) {
  FIO_ASSERT(fio_capa(), "facil.io initialization error!");
  fio_malloc_test();
  fio_state_callback_test();
  fio_str_test();
  fio_atol_test();
  fio_atof_test();
  fio_str2u_test();
  fio_llist_test();
  fio_ary_test();
  fio_set_test();
  fio_defer_test();
  fio_timer_test();
  fio_poll_test();
  fio_socket_test();
  fio_uuid_link_test();
  fio_cycle_test();
  fio_riskyhash_test();
  fio_siphash_test();
  fio_sha1_test();
  fio_sha2_test();
  fio_base64_test();
  fio_test_random();
  fio_pubsub_test();
  (void)fio_sentinel_task;
  (void)deferred_on_shutdown;
  (void)fio_poll;
}

#endif /* DEBUG */
