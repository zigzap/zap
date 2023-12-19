#include <fio.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TEST_CYCLES_START 128
#define TEST_CYCLES_END 256
#define TEST_CYCLES_REPEAT 3
#define REPEAT_LIB_TEST 0

static size_t test_mem_functions(void *(*malloc_func)(size_t),
                                 void *(*calloc_func)(size_t, size_t),
                                 void *(*realloc_func)(void *, size_t),
                                 void (*free_func)(void *)) {
  size_t clock_alloc = 0, clock_realloc = 0, clock_free = 0, clock_free2 = 0,
         clock_calloc = 0, fio_optimized = 0, fio_optimized2 = 0, errors = 0;
  for (int i = TEST_CYCLES_START; i < TEST_CYCLES_END; ++i) {
    for (int repeat = 0; repeat < TEST_CYCLES_REPEAT; ++repeat) {
      void **pointers = calloc_func(sizeof(*pointers), 4096);
      clock_t start;

      /* malloc */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        pointers[j] = malloc_func(i << 4);
        if (i) {
          if (!pointers[j])
            ++errors;
          else
            ((char *)pointers[j])[0] = '1';
        }
      }
      clock_alloc += clock() - start;

      /* realloc */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        void *tmp = realloc_func(pointers[j], i << 5);
        if (tmp) {
          pointers[j] = tmp;
          ((char *)pointers[j])[0] = '1';
        } else if (i)
          ++errors;
      }
      clock_realloc += clock() - start;

      /* free (testing) */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        free_func(pointers[j]);
        pointers[j] = NULL;
      }
      clock_free += clock() - start;

      /* calloc */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        pointers[j] = calloc_func(16, i);
        if (i) {
          if (!pointers[j])
            ++errors;
          else
            ((char *)pointers[j])[0] = '1';
        }
      }
      clock_calloc += clock() - start;

      /* free (no test) */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        free_func(pointers[j]);
      }
      clock_free2 += clock() - start;

      /* facil.io use-case */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        pointers[j] = malloc_func(i << 4);
        if (i) {
          if (!pointers[j])
            ++errors;
          else
            ((char *)pointers[j])[0] = '1';
        }
      }
      for (int j = 0; j < 4096; ++j) {
        free_func(pointers[j]);
      }
      fio_optimized += clock() - start;

      /* facil.io use-case */
      start = clock();
      for (int j = 0; j < 4096; ++j) {
        pointers[j] = malloc_func(i << 4);
        if (i) {
          if (!pointers[j])
            ++errors;
          else
            ((char *)pointers[j])[0] = '1';
        }
        free_func(pointers[j]);
      }
      fio_optimized2 += clock() - start;

      free_func(pointers);
    }
  }
  clock_alloc /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  clock_realloc /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  clock_free /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  clock_free2 /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  clock_calloc /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  fio_optimized /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  fio_optimized2 /= (TEST_CYCLES_END - TEST_CYCLES_START) * TEST_CYCLES_REPEAT;
  fprintf(stderr, "* Avrg. clock count for malloc: %zu\n", clock_alloc);
  fprintf(stderr, "* Avrg. clock count for calloc: %zu\n", clock_calloc);
  fprintf(stderr, "* Avrg. clock count for realloc: %zu\n", clock_realloc);
  fprintf(stderr, "* Avrg. clock count for free: %zu\n", clock_free);
  fprintf(stderr, "* Avrg. clock count for free (re-cycle): %zu\n",
          clock_free2);
  fprintf(stderr,
          "* Avrg. clock count for a facil.io use-case round"
          " (medium-short life): %zu\n",
          fio_optimized);
  fprintf(stderr,
          "* Avrg. clock count for a zero-life span"
          " (malloc-free): %zu\n",
          fio_optimized2);
  fprintf(stderr, "* Failed allocations: %zu\n", errors);
  return clock_alloc + clock_realloc + clock_free + clock_calloc + clock_free2;
}

void *test_system_malloc(void *ignr) {
  (void)ignr;
  uintptr_t result = test_mem_functions(malloc, calloc, realloc, free);
  return (void *)result;
}
void *test_facil_malloc(void *ignr) {
  (void)ignr;
  uintptr_t result =
      test_mem_functions(fio_malloc, fio_calloc, fio_realloc, fio_free);
  return (void *)result;
}

int main(void) {
#if DEBUG
  fprintf(stderr, "\n=== WARNING: performance tests using the DEBUG mode are "
                  "invalid. \n");
#endif
  pthread_t thread2;
  void *thrd_result;

  /* test system allocations */
  fprintf(stderr, "===== Performance Testing system memory allocator "
                  "(please wait):\n ");
  FIO_ASSERT(pthread_create(&thread2, NULL, test_system_malloc, NULL) == 0,
             "Couldn't spawn thread.");
  size_t system = test_mem_functions(malloc, calloc, realloc, free);
  FIO_ASSERT(pthread_join(thread2, &thrd_result) == 0, "Couldn't join thread");
  system += (uintptr_t)thrd_result;
  fprintf(stderr, "Total Cycles: %zu\n", system);

  /* test facil.io allocations */
  fprintf(stderr, "\n===== Performance Testing facil.io memory allocator "
                  "(please wait):\n");
  FIO_ASSERT(pthread_create(&thread2, NULL, test_facil_malloc, NULL) == 0,
             "Couldn't spawn thread.");
  size_t fio =
      test_mem_functions(fio_malloc, fio_calloc, fio_realloc, fio_free);
  FIO_ASSERT(pthread_join(thread2, &thrd_result) == 0, "Couldn't join thread");
  fio += (uintptr_t)thrd_result;
  fprintf(stderr, "Total Cycles: %zu\n", fio);

  return 0; // fio > system;
}
