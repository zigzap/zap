/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_FIO_MEM_H

/**
 * This is a custom memory allocator the utilizes memory pools to allow for
 * concurrent memory allocations across threads.
 *
 * Allocated memory is always zeroed out and aligned on a 16 byte boundary.
 *
 * Reallocated memory is always aligned on a 16 byte boundary but it might be
 * filled with junk data after the valid data (this is true also for
 * `fio_realloc2`).
 *
 * The memory allocator assumes multiple concurrent allocation/deallocation,
 * short life spans (memory is freed shortly, but not immediately, after it was
 * allocated) as well as small allocations (realloc almost always copies data).
 *
 * These assumptions allow the allocator to avoid lock contention by ignoring
 * fragmentation within a memory "block" and waiting for the whole "block" to be
 * freed before it's memory is recycled (no per-allocation "free list").
 *
 * An "arena" is allocated per-CPU core during initialization - there's no
 * dynamic allocation of arenas. This allows threads to minimize lock contention
 * by cycling through the arenas until a free arena is detected.
 *
 * There should be a free arena at any given time (statistically speaking) and
 * the thread will only be deferred in the unlikely event in which there's no
 * available arena.
 *
 * By avoiding the "free-list", the need for allocation "headers" is also
 * avoided and allocations are performed with practically zero overhead (about
 * 32 bytes overhead per 32KB memory, that's 1 bit per 1Kb).
 *
 * However, the lack of a "free list" means that memory "leaks" are more
 * expensive and small long-life allocations could cause fragmentation if
 * performed periodically (rather than performed during startup).
 *
 * This allocator should NOT be used for objects with a long life-span, because
 * even a single persistent object will prevent the re-use of the whole memory
 * block from which it was allocated (see FIO_MEMORY_BLOCK_SIZE for size).
 *
 * Some more details:
 *
 * Allocation and deallocations and (usually) managed by "blocks".
 *
 * A memory "block" can include any number of memory pages that are a multiple
 * of 2 (up to 1Mb of memory). However, the default value, set by the value of
 * FIO_MEMORY_BLOCK_SIZE_LOG, is 32Kb (see value at the end of this header).
 *
 * Each block includes a 32 byte header that uses reference counters and
 * position markers (24 bytes are required padding).
 *
 * The block's position marker (`pos`) marks the next available byte (counted in
 * multiples of 16 bytes).
 *
 * The block's reference counter (`ref`) counts how many allocations reference
 * memory in the block (including the "arena" that "owns" the block).
 *
 * Except for the position marker (`pos`) that acts the same as `sbrk`, there's
 * no way to know which "slices" are allocated and which "slices" are available.
 *
 * The allocator uses `mmap` when requesting memory from the system and for
 * allocations bigger than MEMORY_BLOCK_ALLOC_LIMIT (37.5% of the block).
 *
 * Small allocations are differentiated from big allocations by their memory
 * alignment.
 *
 * If a memory allocation is placed 16 bytes after whole block alignment (within
 * a block's padding zone), the memory was allocated directly using `mmap` as a
 * "big allocation". The 16 bytes include an 8 byte header and an 8 byte
 * padding.
 *
 * To replace the system's `malloc` function family compile with the
 * `FIO_OVERRIDE_MALLOC` defined (`-DFIO_OVERRIDE_MALLOC`).
 *
 * When using tcmalloc or jemalloc, define `FIO_FORCE_MALLOC` to prevent
 * `fio_mem` from compiling (`-DFIO_FORCE_MALLOC`). Function wrappers will be
 * compiled just in case, so calls to `fio_malloc` will be routed to `malloc`.
 *
 */
#define H_FIO_MEM_H

#include <stdlib.h>

/**
 * Allocates memory using a per-CPU core block memory pool.
 * Memory is zeroed out.
 *
 * Allocations above FIO_MEMORY_BLOCK_ALLOC_LIMIT (12,288 bytes when using 32Kb
 * blocks) will be redirected to `mmap`, as if `fio_mmap` was called.
 */
void *fio_malloc(size_t size);

/**
 * same as calling `fio_malloc(size_per_unit * unit_count)`;
 *
 * Allocations above FIO_MEMORY_BLOCK_ALLOC_LIMIT (12,288 bytes when using 32Kb
 * blocks) will be redirected to `mmap`, as if `fio_mmap` was called.
 */
void *fio_calloc(size_t size_per_unit, size_t unit_count);

/** Frees memory that was allocated using this library. */
void fio_free(void *ptr);

/**
 * Re-allocates memory. An attept to avoid copying the data is made only for big
 * memory allocations (larger than FIO_MEMORY_BLOCK_ALLOC_LIMIT).
 */
void *fio_realloc(void *ptr, size_t new_size);

/**
 * Re-allocates memory. An attept to avoid copying the data is made only for big
 * memory allocations (larger than FIO_MEMORY_BLOCK_ALLOC_LIMIT).
 *
 * This variation is slightly faster as it might copy less data.
 */
void *fio_realloc2(void *ptr, size_t new_size, size_t copy_length);

/**
 * Allocates memory directly using `mmap`, this is prefered for objects that
 * both require almost a page of memory (or more) and expect a long lifetime.
 *
 * However, since this allocation will invoke the system call (`mmap`), it will
 * be inherently slower.
 *
 * `fio_free` can be used for deallocating the memory.
 */
void *fio_mmap(size_t size);

/** Clears any memory locks, in case of a system call to `fork`. */
void fio_malloc_after_fork(void);

/** Tests the facil.io memory allocator. */
void fio_malloc_test(void);

/** If defined, `malloc` will be used instead of the fio_malloc functions */
#if FIO_FORCE_MALLOC
#define fio_malloc malloc
#define fio_calloc calloc
#define fio_mmap malloc
#define fio_free free
#define fio_realloc realloc
#define fio_realloc2(ptr, new_size, old_data_len) realloc((ptr), (new_size))
#define fio_malloc_test()
#define fio_malloc_after_fork()

/* allows local override as well as global override */
#elif FIO_OVERRIDE_MALLOC
#define malloc fio_malloc
#define free fio_free
#define realloc fio_realloc
#define calloc fio_calloc

#endif

/** Allocator default settings. */
#ifndef FIO_MEMORY_BLOCK_SIZE_LOG
#define FIO_MEMORY_BLOCK_SIZE_LOG (15) /*15 == 32Kb, 16 == 64Kb, 17 == 128Kb*/
#endif
#ifndef FIO_MEMORY_BLOCK_SIZE
#define FIO_MEMORY_BLOCK_SIZE ((uintptr_t)1 << FIO_MEMORY_BLOCK_SIZE_LOG)
#endif
#ifndef FIO_MEMORY_BLOCK_MASK
#define FIO_MEMORY_BLOCK_MASK (FIO_MEMORY_BLOCK_SIZE - 1) /* 0b111... */
#endif
#ifndef FIO_MEMORY_BLOCK_SLICES
#define FIO_MEMORY_BLOCK_SLICES (FIO_MEMORY_BLOCK_SIZE >> 4) /* 16B slices */
#endif
#ifndef FIO_MEMORY_BLOCK_ALLOC_LIMIT
/* defaults to 37.5% of the block, after which `mmap` is used instead */
#define FIO_MEMORY_BLOCK_ALLOC_LIMIT                                           \
  ((FIO_MEMORY_BLOCK_SIZE >> 2) + (FIO_MEMORY_BLOCK_SIZE >> 3))
#endif

#ifndef FIO_MEM_MAX_BLOCKS_PER_CORE
/**
 * The maximum number of available memory blocks that will be pooled before
 * memory is returned to the system.
 */
#define FIO_MEM_MAX_BLOCKS_PER_CORE                                            \
  (1 << (22 - FIO_MEMORY_BLOCK_SIZE_LOG)) /* 22 == 4Mb per CPU core (1<<22) */
#endif

#endif /* H_FIO_MEM_H */
