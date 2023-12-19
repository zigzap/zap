/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <time.h>
#endif /* __unix__ */

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

/* *****************************************************************************
If FIO_FORCE_MALLOC is set, use glibc / library malloc
***************************************************************************** */
#if FIO_FORCE_MALLOC

void *fio_malloc(size_t size) { return malloc(size); }

void *fio_calloc(size_t size, size_t count) { return calloc(size, count); }

void fio_free(void *ptr) { free(ptr); }

void *fio_realloc(void *ptr, size_t new_size) { return realloc(ptr, new_size); }
void *fio_realloc2(void *ptr, size_t new_size, size_t valid_len) {
  return realloc(ptr, new_size);
  (void)valid_len;
}

void fio_malloc_after_fork(void) {}

/* *****************************************************************************
facil.io malloc implementation
***************************************************************************** */
#else

#include <fio_mem.h>

#if !defined(__clang__) && !defined(__GNUC__)
#define __thread _Thread_value
#endif

#undef malloc
#undef calloc
#undef free
#undef realloc

/* *****************************************************************************
Memory Copying by 16 byte units
***************************************************************************** */

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
Spinlock for the few locks we need (atomic reference counting & free blocks)
***************************************************************************** */

/* manage the way threads "wait" for the lock to release */
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
/* nanosleep seems to be the most effective and efficient reschedule */
#define reschedule_thread()                                                    \
  {                                                                            \
    const struct timespec tm = {.tv_nsec = 1};                                 \
    nanosleep(&tm, NULL);                                                      \
  }
#define throttle_thread(micosec)                                               \
  {                                                                            \
    const struct timespec tm = {.tv_nsec = (micosec & 0xfffff),                \
                                .tv_sec = (micosec >> 20)};                    \
    nanosleep(&tm, NULL);                                                      \
  }
#else /* no effective rescheduling, just spin... */
#define reschedule_thread()
#define throttle_thread(micosec)
#endif

/** locks use a single byte */
typedef volatile unsigned char spn_lock_i;

/** The initail value of an unlocked spinlock. */
#define SPN_LOCK_INIT 0

/* C11 Atomics are defined? */
#if defined(__ATOMIC_RELAXED)
#define SPN_LOCK_BUILTIN(...) __atomic_exchange_n(__VA_ARGS__, __ATOMIC_SEQ_CST)
/** An atomic addition operation */
#define spn_add(...) __atomic_add_fetch(__VA_ARGS__, __ATOMIC_SEQ_CST)
/** An atomic subtraction operation */
#define spn_sub(...) __atomic_sub_fetch(__VA_ARGS__, __ATOMIC_SEQ_CST)

/* Select the correct compiler builtin method. */
#elif defined(__has_builtin)

#if __has_builtin(__sync_fetch_and_or)
#define SPN_LOCK_BUILTIN(...) __sync_fetch_and_or(__VA_ARGS__)
/** An atomic addition operation */
#define spn_add(...) __sync_add_and_fetch(__VA_ARGS__)
/** An atomic subtraction operation */
#define spn_sub(...) __sync_sub_and_fetch(__VA_ARGS__)

#else
#error Required builtin "__sync_swap" or "__sync_fetch_and_or" missing from compiler.
#endif /* defined(__has_builtin) */

#elif __GNUC__ > 3
#define SPN_LOCK_BUILTIN(...) __sync_fetch_and_or(__VA_ARGS__)
/** An atomic addition operation */
#define spn_add(...) __sync_add_and_fetch(__VA_ARGS__)
/** An atomic subtraction operation */
#define spn_sub(...) __sync_sub_and_fetch(__VA_ARGS__)

#else
#error Required builtin "__sync_swap" or "__sync_fetch_and_or" not found.
#endif

/** returns 1 and 0 if the lock was successfully aquired (TRUE == FAIL). */
static inline int spn_trylock(spn_lock_i *lock) {
  return SPN_LOCK_BUILTIN(lock, 1);
}

/** Releases a lock. */
static inline __attribute__((unused)) int spn_unlock(spn_lock_i *lock) {
  return SPN_LOCK_BUILTIN(lock, 0);
}

/** returns a lock's state (non 0 == Busy). */
static inline __attribute__((unused)) int spn_is_locked(spn_lock_i *lock) {
  __asm__ volatile("" ::: "memory");
  return *lock;
}

/** Busy waits for the lock. */
static inline __attribute__((unused)) void spn_lock(spn_lock_i *lock) {
  while (spn_trylock(lock)) {
    reschedule_thread();
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
  next_alloc =
      (void *)((uintptr_t)result + FIO_MEMORY_BLOCK_SIZE +
               (is_indi * ((uintptr_t)1 << 30))); /* add 1TB for realloc */
  return result;
}

/* frees memory using `munmap`. requires exact, page aligned, `len` */
static inline void sys_free(void *mem, size_t len) { munmap(mem, len); }

static void *sys_realloc(void *mem, size_t prev_len, size_t new_len) {
  if (new_len > prev_len) {
#if defined(__linux__) && defined(MREMAP_MAYMOVE)
    void *result = mremap(mem, prev_len, new_len, MREMAP_MAYMOVE);
    if (result == MAP_FAILED)
      return NULL;
#else
    void *result =
        mmap((void *)((uintptr_t)mem + prev_len), new_len - prev_len,
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
#endif
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
typedef struct block_s {
  uint16_t ref; /* reference count (per memory page) */
  uint16_t pos; /* position into the block */
  uint16_t max; /* available memory count */
  uint16_t pad; /* memory padding */
} block_s;

/* a per-CPU core "arena" for memory allocations  */
typedef struct {
  block_s *block;
  spn_lock_i lock;
} arena_s;

/* The memory allocators persistent state */
static struct {
  size_t active_size; /* active array size */
  block_s *available; /* free list for memory blocks */
  intptr_t count;     /* free list counter */
  size_t cores;       /* the number of detected CPU cores*/
  spn_lock_i lock;    /* a global lock */
} memory = {
    .cores = 1,
    .lock = SPN_LOCK_INIT,
};

/* The per-CPU arena array. */
static arena_s *arenas;

/* The per-CPU arena array. */
static long double on_malloc_zero;

/* *****************************************************************************
Per-CPU Arena management
***************************************************************************** */

/* returned a locked arena. Attempts the preffered arena first. */
static inline arena_s *arena_lock(arena_s *preffered) {
  if (!preffered)
    preffered = arenas;
  if (!spn_trylock(&preffered->lock))
    return preffered;
  do {
    arena_s *arena = preffered;
    for (size_t i = (size_t)(arena - arenas); i < memory.cores; ++i) {
      if ((preffered == arenas || arena != preffered) &&
          !spn_trylock(&arena->lock))
        return arena;
      ++arena;
    }
    if (preffered == arenas)
      reschedule_thread();
    preffered = arenas;
  } while (1);
}

static __thread arena_s *arena_last_used;

static void arena_enter(void) { arena_last_used = arena_lock(arena_last_used); }

static inline void arena_exit(void) { spn_unlock(&arena_last_used->lock); }

/** Clears any memory locks, in case of a system call to `fork`. */
void fio_malloc_after_fork(void) {
  arena_last_used = NULL;
  if (!arenas) {
    return;
  }
  memory.lock = SPN_LOCK_INIT;
  for (size_t i = 0; i < memory.cores; ++i) {
    arenas[i].lock = SPN_LOCK_INIT;
  }
}

/* *****************************************************************************
Block management
***************************************************************************** */

// static inline block_s **block_find(void *mem_) {
//   const uintptr_t mem = (uintptr_t)mem_;
//   block_s *blk = memory.active;
// }

/* intializes the block header for an available block of memory. */
static inline block_s *block_init(void *blk_) {
  block_s *blk = blk_;
  *blk = (block_s){
      .ref = 1,
      .pos = (2 + (sizeof(block_s) >> 4)),
      .max = (FIO_MEMORY_BLOCK_SLICES - 1) -
             (sizeof(block_s) >> 4), /* count available units of 16 bytes */
  };
  return blk;
}

/* intializes the block header for an available block of memory. */
static inline void block_free(block_s *blk) {
  if (spn_sub(&blk->ref, 1))
    return;

  if (spn_add(&memory.count, 1) >
      (intptr_t)(FIO_MEM_MAX_BLOCKS_PER_CORE * memory.cores)) {
    /* TODO: return memory to the system */
    spn_sub(&memory.count, 1);
    sys_free(blk, FIO_MEMORY_BLOCK_SIZE);
    return;
  }
  memset(blk, 0, FIO_MEMORY_BLOCK_SIZE);
  spn_lock(&memory.lock);
  *(block_s **)blk = memory.available;
  memory.available = (block_s *)blk;
  spn_unlock(&memory.lock);
}

/* intializes the block header for an available block of memory. */
static inline block_s *block_new(void) {
  block_s *blk = NULL;

  if (memory.available) {
    spn_lock(&memory.lock);
    blk = (block_s *)memory.available;
    if (blk) {
      memory.available = ((block_s **)blk)[0];
    }
    spn_unlock(&memory.lock);
  }
  if (blk) {
    spn_sub(&memory.count, 1);
    ((block_s **)blk)[0] = NULL;
    ((block_s **)blk)[1] = NULL;
    return block_init(blk);
  }
  /* TODO: collect memory from the system */
  blk = sys_alloc(FIO_MEMORY_BLOCK_SIZE, 0);
  if (!blk)
    return NULL;
  return block_init(blk);
  ;
}

static inline void *block_slice(uint16_t units) {
  block_s *blk = arena_last_used->block;
  if (!blk) {
    /* arena is empty */
    blk = block_new();
    arena_last_used->block = blk;
  } else if (blk->pos + units > blk->max) {
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
  spn_add(&blk->ref, 1);
  blk->pos += units;
  if (blk->pos >= blk->max) {
    /* it's true that a 16 bytes slice remains, but statistically... */
    /* ... the block was fully utilized, clear arena */
    block_free(blk);
    arena_last_used->block = NULL;
  }
  return (void *)mem;
}

static inline void block_slice_free(void *mem) {
  /* locate block boundary */
  block_s *blk = (block_s *)((uintptr_t)mem & (~FIO_MEMORY_BLOCK_MASK));
  block_free(blk);
}

/* *****************************************************************************
Non-Block allocations (direct from the system)
***************************************************************************** */

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

static inline void big_free(void *ptr) {
  size_t *mem = (void *)(((uintptr_t)ptr) - 16);
  sys_free(mem, *mem);
}

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
Library Initialization (initialize arenas and allocate a block for each CPU)
***************************************************************************** */

static void __attribute__((constructor)) fio_mem_init(void) {
  if (arenas)
    return;

#ifdef _SC_NPROCESSORS_ONLN
  ssize_t cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#else
#warning Dynamic CPU core count is unavailable - assuming 8 cores for memory allocation pools.
  ssize_t cpu_count = 8; /* fallback */
#endif
  memory.cores = cpu_count;
  memory.count = 0 - (intptr_t)cpu_count;
  arenas = big_alloc(sizeof(*arenas) * cpu_count);
  if (!arenas) {
    perror("FATAL ERROR: Couldn't initialize memory allocator");
    exit(errno);
  }
  size_t pre_pool = cpu_count > 32 ? 32 : cpu_count;
  for (size_t i = 0; i < pre_pool; ++i) {
    void *block = sys_alloc(FIO_MEMORY_BLOCK_SIZE, 0);
    if (block) {
      block_init(block);
      block_free(block);
    }
  }
  pthread_atfork(NULL, NULL, fio_malloc_after_fork);
}

static void __attribute__((destructor)) fio_mem_destroy(void) {
  if (!arenas)
    return;

  arena_s *arena = arenas;
  for (size_t i = 0; i < memory.cores; ++i) {
    if (arena->block)
      block_free(arena->block);
    arena->block = NULL;
    ++arena;
  }
  while (memory.available) {
    block_s *b = memory.available;
    memory.available = *(block_s **)b;
    sys_free(b, FIO_MEMORY_BLOCK_SIZE);
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
  if (!ptr || ptr == (void *)&on_malloc_zero)
    return fio_malloc(new_size);
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
  // memcpy(new_mem, ptr, (copy_length > new_size ? new_size : copy_length) <<
  // 4);
  fio_memcpy(new_mem, ptr, (copy_length > new_size ? new_size : copy_length));
  block_slice_free(ptr);
  return new_mem;
zero_size:
  fio_free(ptr);
  return malloc(0);
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
Some Tests
***************************************************************************** */

#if DEBUG && !FIO_FORCE_MALLOC

void fio_malloc_test(void) {
#define TEST_ASSERT(cond, ...)                                                 \
  if (!(cond)) {                                                               \
    fprintf(stderr, "* " __VA_ARGS__);                                         \
    fprintf(stderr, "\nTesting failed.\n");                                    \
    exit(-1);                                                                  \
  }

  fprintf(stderr, "=== Testing facil.io memory allocator's system calls\n");
  char *mem = sys_alloc(FIO_MEMORY_BLOCK_SIZE, 0);
  TEST_ASSERT(mem, "sys_alloc failed to allocate memory!\n");
  TEST_ASSERT(!((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK),
              "Memory allocation not aligned to FIO_MEMORY_BLOCK_SIZE!");
  mem[0] = 'a';
  mem[FIO_MEMORY_BLOCK_SIZE - 1] = 'z';
  fprintf(stderr, "* Testing reallocation from %p\n", (void *)mem);
  char *mem2 =
      sys_realloc(mem, FIO_MEMORY_BLOCK_SIZE, FIO_MEMORY_BLOCK_SIZE * 2);
  if (mem == mem2)
    fprintf(stderr, "* Performed system realloc without copy :-)\n");
  TEST_ASSERT(mem2[0] = 'a' && mem2[FIO_MEMORY_BLOCK_SIZE - 1] == 'z',
              "Reaclloc data was lost!");
  sys_free(mem2, FIO_MEMORY_BLOCK_SIZE * 2);
  fprintf(stderr, "=== Testing facil.io memory allocator's internal data.\n");
  TEST_ASSERT(arenas, "Missing arena data - library not initialized!");
  fio_free(NULL); /* fio_free(NULL) shouldn't crash... */
  mem = fio_malloc(1);
  TEST_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
  TEST_ASSERT(!((uintptr_t)mem & 15), "fio_malloc memory not aligned!\n");
  TEST_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16,
              "small fio_malloc memory indicates system allocation!\n");
  mem[0] = 'a';
  TEST_ASSERT(mem[0] == 'a', "allocate memory wasn't written to!\n");
  mem = fio_realloc(mem, 1);
  TEST_ASSERT(mem[0] == 'a', "fio_realloc memory wasn't copied!\n");
  TEST_ASSERT(arena_last_used, "arena_last_used wasn't initialized!\n");
  block_s *b = arena_last_used->block;
  size_t count = 2;
  intptr_t old_memory_pool_count = memory.count;
  do {
    TEST_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
    TEST_ASSERT(!((uintptr_t)mem & 15),
                "fio_malloc memory not aligned at allocation #%zu!\n", count);
    TEST_ASSERT((((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16),
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
    fprintf(
        stderr,
        "* Performed %zu allocations out of expected %zu allocations per "
        "block.\n",
        count,
        (size_t)((FIO_MEMORY_BLOCK_SLICES - 2) - (sizeof(block_s) >> 4) - 1));
    TEST_ASSERT(memory.available,
                "memory pool empty (memory block wasn't freed)!\n");
    TEST_ASSERT(old_memory_pool_count == memory.count,
                "memory.count == %ld (memory block not counted)!\n",
                (long)old_memory_pool_count);
    fio_free(mem);
  }
  /* rotate block again */
  b = arena_last_used->block;
  mem = fio_realloc(mem, 1);
  do {
    mem2 = mem;
    mem = fio_malloc(1);
    fio_free(mem2); /* make sure we hold on to the block, so it rotates */
    TEST_ASSERT(mem, "fio_malloc failed to allocate memory!\n");
    TEST_ASSERT(!((uintptr_t)mem & 15),
                "fio_malloc memory not aligned at allocation #%zu!\n", count);
    TEST_ASSERT((((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16),
                "fio_malloc memory indicates system allocation!\n");
#if __x86_64__
    fio_memcpy((size_t *)mem, (size_t *)"0123456789abcdefg", 1);
#else
    mem[0] = 'a';
#endif
    ++count;
  } while (arena_last_used->block == b);

  mem = fio_calloc(FIO_MEMORY_BLOCK_ALLOC_LIMIT - 64, 1);
  TEST_ASSERT(mem,
              "failed to allocate FIO_MEMORY_BLOCK_ALLOC_LIMIT - 64 bytes!\n");
  TEST_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) != 16,
              "fio_calloc (under limit) memory alignment error!\n");
  mem2 = fio_malloc(1);
  TEST_ASSERT(mem2, "fio_malloc(1) failed to allocate memory!\n");
  mem2[0] = 'a';

  for (uintptr_t i = 0; i < (FIO_MEMORY_BLOCK_ALLOC_LIMIT - 64); ++i) {
    TEST_ASSERT(mem[i] == 0,
                "calloc returned memory that wasn't initialized?!\n");
  }
  fio_free(mem);

  mem = fio_malloc(FIO_MEMORY_BLOCK_SIZE);
  TEST_ASSERT(mem, "fio_malloc failed to FIO_MEMORY_BLOCK_SIZE bytes!\n");
  TEST_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) == 16,
              "fio_malloc (big) memory isn't aligned!\n");
  mem = fio_realloc(mem, FIO_MEMORY_BLOCK_SIZE * 2);
  TEST_ASSERT(mem,
              "fio_realloc (big) failed on FIO_MEMORY_BLOCK_SIZE X2 bytes!\n");
  fio_free(mem);
  TEST_ASSERT(((uintptr_t)mem & FIO_MEMORY_BLOCK_MASK) == 16,
              "fio_realloc (big) memory isn't aligned!\n");

  {
    void *m0 = fio_malloc(0);
    void *rm0 = fio_realloc(m0, 16);
    TEST_ASSERT(m0 != rm0, "fio_realloc(fio_malloc(0), 16) failed!\n");
  }

  fprintf(stderr, "* passed.\n");
}

#else

void fio_malloc_test(void) {}

#endif
