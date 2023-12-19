#define FIO_INCLUDE_STR
#include <fio.h>
#include <fio_cli.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline int seek1(register uint8_t **buffer,
                        register uint8_t *const limit, const uint8_t c) {
  while (*buffer < limit) {
    if (**buffer == c)
      return 1;
    (*buffer)++;
  }
  return 0;
}

static inline int seek_memchr(uint8_t **buffer, uint8_t *const limit,
                              const uint8_t c) {
  if (limit - *buffer == 0)
    return 0;
  void *tmp = memchr(*buffer, c, limit - (*buffer));
  if (tmp) {
    *buffer = tmp;
    return 1;
  }
  *buffer = (uint8_t *)limit;
  return 0;
}

/**
 * This seems to be faster on some systems, especially for smaller distances.
 *
 * On newer systems, `memchr` should be faster.
 */
static inline int seek3(uint8_t **buffer, register uint8_t *const limit,
                        const uint8_t c) {
  if (**buffer == c)
    return 1;

#if !__x86_64__ && !__aarch64__
  /* too short for this mess */
  if ((uintptr_t)limit <= 16 + ((uintptr_t)*buffer & (~(uintptr_t)7)))
    goto finish;

  /* align memory */
  {
    const uint8_t *alignment =
        (uint8_t *)(((uintptr_t)(*buffer) & (~(uintptr_t)7)) + 8);
    if (limit >= alignment) {
      while (*buffer < alignment) {
        if (**buffer == c)
          return 1;
        *buffer += 1;
      }
    }
  }
  const uint8_t *limit64 = (uint8_t *)((uintptr_t)limit & (~(uintptr_t)7));
#else
  const uint8_t *limit64 = (uint8_t *)limit - 7;
#endif
  uint64_t wanted1 = 0x0101010101010101ULL * c;
  for (; *buffer < limit64; *buffer += 8) {
    const uint64_t eq1 = ~((*((uint64_t *)*buffer)) ^ wanted1);
    const uint64_t t0 = (eq1 & 0x7f7f7f7f7f7f7f7fllu) + 0x0101010101010101llu;
    const uint64_t t1 = (eq1 & 0x8080808080808080llu);
    if ((t0 & t1)) {
      break;
    }
  }
#if !__x86_64__ && !__aarch64__
finish:
#endif
  while (*buffer < limit) {
    if (**buffer == c)
      return 1;
    (*buffer)++;
  }

  return 0;
}

static inline int seek4(register uint8_t **buffer,
                        register uint8_t *const limit, const uint8_t c) {
#ifndef __SIZEOF_INT128__
  return 0;
#else
  if (**buffer == c)
    return 1;

  /* move by step until memory unalignment */
  {
    const uint8_t *alignment =
        (uint8_t *)(((uintptr_t)(*buffer) & (~(uintptr_t)15)) + 16);
    if (limit >= alignment) {
      while (*buffer < alignment) {
        if (**buffer == c)
          return 1;
        *buffer += 1;
      }
    }
  }
  const __uint128_t just_1_bit = ((((__uint128_t)0x0101010101010101ULL) << 64) |
                                  (__uint128_t)0x0101010101010101ULL);
  const __uint128_t is_7bit_set =
      ((((__uint128_t)0x7f7f7f7f7f7f7f7fULL) << 64) |
       (__uint128_t)0x7f7f7f7f7f7f7f7fULL);
  const __uint128_t is_1bit_set =
      ((((__uint128_t)0x8080808080808080ULL) << 64) |
       (__uint128_t)0x8080808080808080ULL);

  __uint128_t wanted1 = just_1_bit * c;
  __uint128_t *lpos = (__uint128_t *)*buffer;
  __uint128_t *llimit = ((__uint128_t *)limit) - 1;

  for (; lpos < llimit; lpos++) {
    const __uint128_t eq1 = ~((*lpos) ^ wanted1);
    const __uint128_t t0 = (eq1 & is_7bit_set) + just_1_bit;
    const __uint128_t t1 = (eq1 & is_1bit_set);
    if ((t0 & t1)) {
      break;
    }
  }

  *buffer = (uint8_t *)lpos;

  while (*buffer < limit) {
    if (**buffer == c)
      return 1;
    (*buffer)++;
  }
  return 0;
#endif
}

#define RUNS 8
int main(int argc, char const **argv) {

  fio_cli_start(
      argc, argv, 1, 0,
      "This program tests the memchr speed against a custom implementation. "
      "It's meant to be used against defferent data to test how seeking "
      "performs in different circumstances.\n use: appname <filename>",
      "-c the char to be tested against (only the fist char in the string");
  if (fio_cli_unnamed_count()) {
    fio_cli_set_default("-f", fio_cli_unnamed(0));
  } else {
    fio_cli_set_default("-f", __FILE__);
  }
  fio_cli_set_default("-c", "\n");
  // fio_cli_set_default(name, value)
  clock_t start, end;
  uint8_t *pos;
  uint8_t *stop;
  size_t count;

  fprintf(stderr, "Size of longest word found %lu\n",
          sizeof(unsigned long long));

  struct {
    int (*func)(uint8_t **buffer, uint8_t *const limit, const uint8_t c);
    const char *name;
  } seek_funcs[] = {
      {.func = seek1, .name = "seek1 (basic loop)"},
      {.func = seek_memchr, .name = "memchr (system)"},
      {.func = seek3, .name = "seek3 (64 bit word at a time)"},
#ifdef __SIZEOF_INT128__
      {.func = seek4, .name = "seek4 (128 bit word at a time)"},
#endif
      {.func = NULL, .name = NULL},
  };
  size_t func_pos = 0;

  uint8_t char2find = fio_cli_get("-c")[0];
  fio_str_s str = FIO_STR_INIT;
  fio_str_info_s data = fio_str_readfile(&str, fio_cli_get("-f"), 0, 0);
  if (!data.len) {
    fprintf(stderr, "ERROR: Couldn't open file %s\n", fio_cli_get("-f"));
    exit(-1);
  }
  fprintf(stderr, "Starting to test file with %lu bytes\n",
          (unsigned long)data.len);

  while (seek_funcs[func_pos].func) {
    fprintf(stderr, "\nTesting %s:\n  (", seek_funcs[func_pos].name);
    size_t avrg = 0;
    for (size_t i = 0; i < RUNS; i++) {
      if (i)
        fprintf(stderr, " + ");
      pos = (uint8_t *)data.data;
      stop = (uint8_t *)data.data + data.len;
      count = 0;
      if (!pos || !stop)
        perror("WTF?!"), exit(errno);

      start = clock();
      while (pos < stop && seek_funcs[func_pos].func(&pos, stop, char2find)) {
        if (!pos)
          perror("WTF?!2!"), exit(errno);
        pos++;
        count++;
      }
      end = clock();
      avrg += end - start;
      fprintf(stderr, "%lu", end - start);
    }
    fprintf(stderr, ")/%d\n === finding %lu items in %zu bytes took %lfs\n",
            RUNS, count, data.len, (avrg / RUNS) / (1.0 * CLOCKS_PER_SEC));
    func_pos++;
  }
  fprintf(stderr, "\n");

  fio_str_free(&str);
  fio_cli_end();
}
