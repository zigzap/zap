/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#include <fio_siphash.h>

/* *****************************************************************************

                                    NOTICE

This code won't be linked to the final application when using fio.h and fio.c.

The code is here only to allow the FIOBJ library to be extracted from the
facil.io framework library.

***************************************************************************** */

/* *****************************************************************************
Hashing (SipHash implementation)
***************************************************************************** */

#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__) &&                 \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* the algorithm was designed as little endian... so, byte swap 64 bit. */
#define sip_local64(i)                                                         \
  (((i)&0xFFULL) << 56) | (((i)&0xFF00ULL) << 40) |                            \
      (((i)&0xFF0000ULL) << 24) | (((i)&0xFF000000ULL) << 8) |                 \
      (((i)&0xFF00000000ULL) >> 8) | (((i)&0xFF0000000000ULL) >> 24) |         \
      (((i)&0xFF000000000000ULL) >> 40) | (((i)&0xFF00000000000000ULL) >> 56)
#else
/* no need */
#define sip_local64(i) (i)
#endif

/* 64Bit left rotation, inlined. */
#define lrot64(i, bits)                                                        \
  (((uint64_t)(i) << (bits)) | ((uint64_t)(i) >> (64 - (bits))))

static inline uint64_t fio_siphash_xy(const void *data, size_t len, size_t x,
                                      size_t y, uint64_t key1, uint64_t key2) {
  /* initialize the 4 words */
  uint64_t v0 = (0x0706050403020100ULL ^ 0x736f6d6570736575ULL) ^ key1;
  uint64_t v1 = (0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL) ^ key2;
  uint64_t v2 = (0x0706050403020100ULL ^ 0x6c7967656e657261ULL) ^ key1;
  uint64_t v3 = (0x0f0e0d0c0b0a0908ULL ^ 0x7465646279746573ULL) ^ key2;
  const uint64_t *w64 = data;
  uint8_t len_mod = len & 255;
  union {
    uint64_t i;
    uint8_t str[8];
  } word;

#define hash_map_SipRound                                                      \
  do {                                                                         \
    v2 += v3;                                                                  \
    v3 = lrot64(v3, 16) ^ v2;                                                  \
    v0 += v1;                                                                  \
    v1 = lrot64(v1, 13) ^ v0;                                                  \
    v0 = lrot64(v0, 32);                                                       \
    v2 += v1;                                                                  \
    v0 += v3;                                                                  \
    v1 = lrot64(v1, 17) ^ v2;                                                  \
    v3 = lrot64(v3, 21) ^ v0;                                                  \
    v2 = lrot64(v2, 32);                                                       \
  } while (0);

  while (len >= 8) {
    word.i = sip_local64(*w64);
    v3 ^= word.i;
    /* Sip Rounds */
    for (size_t i = 0; i < x; ++i) {
      hash_map_SipRound;
    }
    v0 ^= word.i;
    w64 += 1;
    len -= 8;
  }
  word.i = 0;
  uint8_t *pos = word.str;
  uint8_t *w8 = (void *)w64;
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

#pragma weak fio_siphash24
uint64_t __attribute__((weak))
fio_siphash24(const void *data, size_t len, uint64_t key1, uint64_t key2) {
  return fio_siphash_xy(data, len, 2, 4, key1, key2);
}

#pragma weak fio_siphash13
uint64_t __attribute__((weak))
fio_siphash13(const void *data, size_t len, uint64_t key1, uint64_t key2) {
  return fio_siphash_xy(data, len, 1, 3, key1, key2);
}

#if DEBUG
#include <stdio.h>
#include <string.h>
#include <time.h>

void fiobj_siphash_test(void) {
  fprintf(stderr, "===================================\n");
  // fio_siphash_speed_test();
  uint64_t result = 0;
  clock_t start;
  start = clock();
  for (size_t i = 0; i < 100000; i++) {
    char *data = "The quick brown fox jumps over the lazy dog ";
    __asm__ volatile("" ::: "memory");
    result += fio_siphash_xy(data, 43, 1, 3, 0, 0);
  }
  fprintf(stderr, "fio 100K SipHash: %lf\n",
          (double)(clock() - start) / CLOCKS_PER_SEC);
}
#endif
