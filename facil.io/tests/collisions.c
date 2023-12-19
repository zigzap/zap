/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#define FIO_INCLUDE_STR
#include <fio.h>
#include <fio_cli.h>

#ifndef TEST_XXHASH
#define TEST_XXHASH 1
#endif

/* *****************************************************************************
State machine and types
***************************************************************************** */

static uint8_t print_flag = 1;

static inline int fio_str_eq_print(fio_str_s *a, fio_str_s *b) {
  /* always return 1, to avoid internal set collision mitigation. */
  if (print_flag)
    fprintf(stderr, "* Collision Detected: %s vs. %s\n", fio_str_data(a),
            fio_str_data(b));
  return 1;
}

// static inline void destroy_collision_object(fio_str_s *a) {
//   fprintf(stderr, "* Collision Detected: %s\n", fio_str_data(a));
//   fio_str_free2(a);
// }

#define FIO_SET_NAME collisions
#define FIO_SET_OBJ_TYPE fio_str_s *
#define FIO_SET_OBJ_COPY(dest, src) ((dest) = fio_str_new_copy2((src)))
#define FIO_SET_OBJ_COMPARE(a, b) fio_str_eq_print((a), (b))
#define FIO_SET_OBJ_DESTROY(a) fio_str_free2((a))
#include <fio.h>

typedef uintptr_t (*hashing_func_fn)(char *, size_t);
#define FIO_SET_NAME hash_name
#define FIO_SET_OBJ_TYPE hashing_func_fn
#include <fio.h>

#define FIO_ARY_NAME words
#define FIO_ARY_TYPE fio_str_s
#define FIO_ARY_COMPARE(a, b) fio_str_iseq(&(a), &(b))
#define FIO_ARY_COPY(dest, src)                                                \
  do {                                                                         \
    fio_str_clear(&(dest)), fio_str_concat(&(dest), &(src));                   \
  } while (0)
#define FIO_ARY_DESTROY(a) fio_str_free((&a))
#include <fio.h>

static hash_name_s hash_names = FIO_SET_INIT;
static words_s words = FIO_SET_INIT;

/* *****************************************************************************
Main
***************************************************************************** */

static void test_hash_function(hashing_func_fn h);
static void initialize_cli(int argc, char const *argv[]);
static void load_words(void);
static void initialize_hash_names(void);
static void print_hash_names(void);
static char *hash_name(hashing_func_fn fn);
static void cleanup(void);

int main(int argc, char const *argv[]) {
  // FIO_LOG_LEVEL = FIO_LOG_LEVEL_DEBUG;
  initialize_cli(argc, argv);
  load_words();
  initialize_hash_names();
  if (fio_cli_get("-t")) {
    fio_str_s tmp = FIO_STR_INIT_STATIC(fio_cli_get("-t"));
    hashing_func_fn h = hash_name_find(&hash_names, fio_str_hash(&tmp), NULL);
    if (h) {
      test_hash_function(h);
    } else {
      FIO_LOG_ERROR("Test function %s unknown.", tmp.data);
      fprintf(stderr, "Try any of the following:\n");
      print_hash_names();
    }
  } else {
    FIO_SET_FOR_LOOP(&hash_names, pos) { test_hash_function(pos->obj); }
  }
  cleanup();
  return 0;
}

/* *****************************************************************************
CLI
***************************************************************************** */

static void initialize_cli(int argc, char const *argv[]) {
  fio_cli_start(
      argc, argv, 0, 0,
      "This is a Hash algorythm collision test program. It accepts the "
      "following arguments:",
      FIO_CLI_STRING(
          "-test -t test only the specified algorithm. Options include:"),
      FIO_CLI_PRINT("\t\tsiphash13"), FIO_CLI_PRINT("\t\tsiphash24"),
      FIO_CLI_PRINT("\t\tsha1"),
      FIO_CLI_PRINT("\t\trisky (fio_str_hash_risky)"),
      FIO_CLI_PRINT("\t\trisky2 (fio_str_hash_risky alternative)"),
      // FIO_CLI_PRINT("\t\txor (xor all bytes and length)"),
      FIO_CLI_STRING(
          "-dictionary -d a text file containing words separated by an "
          "EOL marker."),
      FIO_CLI_BOOL("-v make output more verbouse (debug mode)"));
  if (fio_cli_get_bool("-v"))
    FIO_LOG_LEVEL = FIO_LOG_LEVEL_DEBUG;
  FIO_LOG_DEBUG("initialized CLI.");
}

/* *****************************************************************************
Dictionary management
***************************************************************************** */

static void add_bad_words(void);
static void load_words(void) {
  add_bad_words();
  fio_str_s filename = FIO_STR_INIT;
  fio_str_s data = FIO_STR_INIT;

  if (fio_cli_get("-d")) {
    fio_str_write(&filename, fio_cli_get("-d"), strlen(fio_cli_get("-d")));
  } else {
    fio_str_info_s tmp = fio_str_write(&filename, __FILE__, strlen(__FILE__));
    while (tmp.len && tmp.data[tmp.len - 1] != '/') {
      --tmp.len;
    }
    fio_str_resize(&filename, tmp.len);
    fio_str_write(&filename, "words.txt", 9);
  }
  fio_str_readfile(&data, fio_str_data(&filename), 0, 0);
  fio_str_info_s d = fio_str_info(&data);
  if (d.len == 0) {
    FIO_LOG_FATAL("Couldn't find / read dictionary file (or no words?)");
    FIO_LOG_FATAL("\tmissing or empty: %s", fio_str_data(&filename));
    cleanup();
    fio_str_free(&filename);
    exit(-1);
  }
  while (d.len) {
    char *eol = memchr(d.data, '\n', d.len);
    if (!eol) {
      /* push what's left */
      words_push(&words, FIO_STR_INIT_STATIC2(d.data, d.len));
      break;
    }
    if (eol == d.data || (eol == d.data + 1 && eol[-1] == '\r')) {
      /* empty line */
      ++d.data;
      --d.len;
      continue;
    }
    words_push(&words, FIO_STR_INIT_STATIC2(
                           d.data, (eol - (d.data + (eol[-1] == '\r')))));
    d.len -= (eol + 1) - d.data;
    d.data = eol + 1;
  }
  fio_free(&filename);
  fio_free(&data);
  FIO_LOG_INFO("Loaded %zu words.", words_count(&words));
}

/* *****************************************************************************
Cleanup
***************************************************************************** */

static void cleanup(void) {
  print_flag = 0;
  hash_name_free(&hash_names);
  words_free(&words);
}

/* *****************************************************************************
Hash functions
***************************************************************************** */

static uintptr_t siphash13(char *data, size_t len) {
  return fio_siphash13(data, len, 0, 0);
}

static uintptr_t siphash24(char *data, size_t len) {
  return fio_siphash24(data, len, 0, 0);
}
static uintptr_t sha1(char *data, size_t len) {
  fio_sha1_s s = fio_sha1_init();
  fio_sha1_write(&s, data, len);
  return ((uintptr_t *)fio_sha1_result(&s))[0];
}
static uintptr_t counter(char *data, size_t len) {
  static uintptr_t counter = 0;
  const size_t len_256 = len & (((size_t)-1) << 5);

  for (size_t i = 0; i < len_256; i += 8) {
    /* vectorized 32 bytes / 256 bit access */
    uint64_t t0 = fio_str2u64(data);
    uint64_t t1 = fio_str2u64(data + 8);
    uint64_t t2 = fio_str2u64(data + 16);
    uint64_t t3 = fio_str2u64(data + 24);
    __asm__ volatile("" ::: "memory");
    (void)t0;
    (void)t1;
    (void)t2;
    (void)t3;
    data += 32;
  }
  uint64_t tmp;
  /* 64 bit words  */
  switch (len & 24) {
  case 24:
    tmp = fio_str2u64(data + 16);
    __asm__ volatile("" ::: "memory");
  case 16: /* overflow */
    tmp = fio_str2u64(data + 8);
    __asm__ volatile("" ::: "memory");
  case 8: /* overflow */
    tmp = fio_str2u64(data);
    __asm__ volatile("" ::: "memory");
    data += len & 24;
  }

  tmp = 0;
  /* leftover bytes */
  switch ((len & 7)) {
  case 7: /* overflow */
    tmp |= ((uint64_t)data[6]) << 8;
  case 6: /* overflow */
    tmp |= ((uint64_t)data[5]) << 16;
  case 5: /* overflow */
    tmp |= ((uint64_t)data[4]) << 24;
  case 4: /* overflow */
    tmp |= ((uint64_t)data[3]) << 32;
  case 3: /* overflow */
    tmp |= ((uint64_t)data[2]) << 40;
  case 2: /* overflow */
    tmp |= ((uint64_t)data[1]) << 48;
  case 1: /* overflow */
    tmp |= ((uint64_t)data[0]) << 56;
  }
  __asm__ volatile("" ::: "memory");
  return ++counter;
}

#if TEST_XXHASH
#include "xxhash.h"
static uintptr_t xxhash_test(char *data, size_t len) {
  return XXH64(data, len, 0);
}
#endif

/**
Working version.
*/
inline FIO_FUNC uintptr_t fio_risky_hash2(const void *data, size_t len,
                                          uint64_t salt);

inline FIO_FUNC uintptr_t risky2(char *data, size_t len) {
  return fio_risky_hash2(data, len, 0);
}

inline FIO_FUNC uintptr_t risky(char *data, size_t len) {
  return fio_risky_hash(data, len, 0);
}

/* *****************************************************************************
Hash setup and testing...
***************************************************************************** */

struct hash_fn_names_s {
  char *name;
  hashing_func_fn fn;
} hash_fn_list[] = {
    {"counter (no hash, RAM access test)", counter},
    {"siphash13", siphash13},
    {"siphash24", siphash24},
    {"sha1", sha1},
#if TEST_XXHASH
    {"xxhash", xxhash_test},
#endif
    {"risky", risky},
    {"risky2", risky2},
    {NULL, NULL},
};

static void initialize_hash_names(void) {
  for (size_t i = 0; hash_fn_list[i].name; ++i) {
    fio_str_s tmp = FIO_STR_INIT_STATIC(hash_fn_list[i].name);
    hash_name_insert(&hash_names, fio_str_hash(&tmp), hash_fn_list[i].fn);
    FIO_LOG_DEBUG("Registered %s hashing function.\n\t\t(%zu registered)",
                  hash_fn_list[i].name, hash_name_count(&hash_names));
  }
}

static char *hash_name(hashing_func_fn fn) {
  for (size_t i = 0; hash_fn_list[i].name; ++i) {
    if (hash_fn_list[i].fn == fn)
      return hash_fn_list[i].name;
  }
  return NULL;
}

static void print_hash_names(void) {
  for (size_t i = 0; hash_fn_list[i].name; ++i) {
    fprintf(stderr, "* %s\n", hash_fn_list[i].name);
  }
}

static void test_hash_function_speed(hashing_func_fn h, char *name) {
  FIO_LOG_DEBUG("Speed testing for %s", name);
  /* test based on code from BearSSL with credit to Thomas Pornin */
  uint8_t buffer[8192];
  memset(buffer, 'T', sizeof(buffer));
  /* warmup */
  uint64_t hash = 0;
  for (size_t i = 0; i < 4; i++) {
    hash += h((char *)buffer, sizeof(buffer));
    memcpy(buffer, &hash, sizeof(hash));
  }
  /* loop until test runs for more than 2 seconds */
  for (uint64_t cycles = (8192 << 4);;) {
    clock_t start, end;
    start = clock();
    for (size_t i = cycles; i > 0; i--) {
      hash += h((char *)buffer, sizeof(buffer));
      __asm__ volatile("" ::: "memory");
    }
    end = clock();
    memcpy(buffer, &hash, sizeof(hash));
    if ((end - start) >= (2 * CLOCKS_PER_SEC) ||
        cycles >= ((uint64_t)1 << 62)) {
      fprintf(stderr, "%-20s %8.2f MB/s\n", name,
              (double)(sizeof(buffer) * cycles) /
                  (((end - start) * (1000000.0 / CLOCKS_PER_SEC))));
      break;
    }
    cycles <<= 2;
  }
}

static void test_hash_function(hashing_func_fn h) {
  size_t best_count = 0, best_capa = 1024;
#define test_for_best()                                                        \
  if (collisions_capa(&c) > 1024 &&                                            \
      (collisions_count(&c) * (double)1 / collisions_capa(&c)) >               \
          (best_count * (double)1 / best_capa)) {                              \
    best_count = collisions_count(&c);                                         \
    best_capa = collisions_capa(&c);                                           \
  }
  char *name = NULL;
  for (size_t i = 0; hash_fn_list[i].name; ++i) {
    if (hash_fn_list[i].fn == h) {
      name = hash_fn_list[i].name;
      break;
    }
  }
  if (!name)
    name = "unknown";
  fprintf(stderr, "======= %s\n", name);
  /* Speed test */
  test_hash_function_speed(h, name);
  /* Collision test */
  collisions_s c = FIO_SET_INIT;
  size_t count = 0;
  FIO_ARY_FOR(&words, w) {
    fio_str_info_s i = fio_str_info(w);
    // fprintf(stderr, "%s\n", i.data);
    printf("\33[2K [%zu] %s\r", ++count, i.data);
    collisions_overwrite(&c, h(i.data, i.len), w, NULL);
    test_for_best();
  }
  printf("\33[2K\r\n");
  fprintf(stderr, "* Total collisions detected for %s: %zu\n", name,
          words_count(&words) - collisions_count(&c));
  fprintf(stderr, "* Final set utilization ratio (over 1024) %zu/%zu\n",
          collisions_count(&c), collisions_capa(&c));
  fprintf(stderr, "* Best set utilization ratio  %zu/%zu\n", best_count,
          best_capa);
  collisions_free(&c);
}

/* *****************************************************************************
Finsing a mod64 inverse
See: https://lemire.me/blog/2017/09/18/computing-the-inverse-of-odd-integers/
***************************************************************************** */

/* will return `inv` if `inv` is inverse of `n` */
static uint64_t inverse64_test(uint64_t n, uint64_t inv) {
  uint64_t result = inv * (2 - (n * inv));
  return result;
}

static uint64_t inverse64(uint64_t x) {
  uint64_t y = (3 * x) ^ 2;
  y = inverse64_test(x, y);
  y = inverse64_test(x, y);
  y = inverse64_test(x, y);
  y = inverse64_test(x, y);
  if (FIO_LOG_LEVEL >= FIO_LOG_LEVEL_DEBUG) {
    char buff[64];
    fio_str_s t = FIO_STR_INIT;
    fio_str_write(&t, "\n\t\tinverse for:\t", 16);
    fio_str_write(&t, buff, fio_ltoa(buff, x, 16));
    fio_str_write(&t, "\n\t\tis:\t\t\t", 8);
    fio_str_write(&t, buff, fio_ltoa(buff, y, 16));
    fio_str_write(&t, "\n\t\tsanity inverse test: 1==", 27);
    fio_str_write_i(&t, x * y);
    FIO_LOG_DEBUG("%s", fio_str_data(&t));
  }

  return y;
}

/* *****************************************************************************
Hash Breaking Word Workshop
***************************************************************************** */

/**
 * Attacking 8 byte words, which follow this code path:
 *      h64 = seed + PRIME64_5;
 *      h64 += len; // len == 8
 *      if (p + 4 <= bEnd) {
 *        h64 ^= (U64)(XXH_get32bits(p)) * PRIME64_1;
 *        h64 = XXH_rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
 *        p += 4;
 *      }
 *
 *      while (p < bEnd) {
 *        h64 ^= (*p) * PRIME64_5;
 *        h64 = XXH_rotl64(h64, 11) * PRIME64_1;
 *        p++;
 *      }
 *
 *      h64 ^= h64 >> 33;
 *      h64 *= PRIME64_2;
 *      h64 ^= h64 >> 29;
 *      h64 *= PRIME64_3;
 *      h64 ^= h64 >> 32;
 */

FIO_FUNC void attack_xxhash2(void) {
  /* POC - forcing XXHash to return seed only data (here, seed = 0) */
  const uint64_t PRIME64_1 = 11400714785074694791ULL;
  const uint64_t PRIME64_2 = 14029467366897019727ULL;
  // const uint64_t PRIME64_3 = 1609587929392839161ULL;
  // const uint64_t PRIME64_4 = 9650029242287828579ULL;
  // const uint64_t PRIME64_5 = 2870177450012600261ULL;
  const uint64_t PRIME64_1_INV = inverse64(PRIME64_1);
  const uint64_t PRIME64_2_INV = inverse64(PRIME64_2);
  // const uint64_t PRIME64_3_INV = inverse64(PRIME64_3);
  // const uint64_t PRIME64_4_INV = inverse64(PRIME64_4);
  // const uint64_t PRIME64_5_INV = inverse64(PRIME64_5);
  const uint64_t seed_manipulation[4] = {PRIME64_1 + PRIME64_2, PRIME64_2, 0,
                                         -PRIME64_1};
  uint64_t v[4] = {0, 0, 0, 0};
  /* attack v *= PRIME64_1 */
  v[0] = v[0] * PRIME64_1_INV;
  v[1] = v[1] * PRIME64_1_INV;
  v[2] = v[2] * PRIME64_1_INV;
  v[3] = v[3] * PRIME64_1_INV;
  /* attack v = XXH_rotl64(v, 31) */
  v[0] = (v[0] >> 31) | (v[0] << (64 - 31));
  v[1] = (v[1] >> 31) | (v[1] << (64 - 31));
  v[2] = (v[2] >> 31) | (v[2] << (64 - 31));
  v[3] = (v[3] >> 31) | (v[3] << (64 - 31));
  /* attack seed manipulation */
  v[0] = v[0] - seed_manipulation[0];
  v[1] = v[1] - seed_manipulation[1];
  v[2] = v[2] - seed_manipulation[2];
  v[3] = v[3] - seed_manipulation[3];
  /* attack v += XXH_get64bits(p) * PRIME64_2 */
  v[0] = v[0] * PRIME64_2_INV;
  v[1] = v[1] * PRIME64_2_INV;
  v[2] = v[2] * PRIME64_2_INV;
  v[3] = v[3] * PRIME64_2_INV;
  uint64_t seed_data = XXH64(v, 32, 0);
  if (seed_data == 0)
    fprintf(stderr, "XXHash seed data extracted for seed == 0!\n");
  else
    fprintf(stderr, "Seed extraction failed %llu\n", seed_data);
}

FIO_FUNC void attack_xxhash(void) {
  /* POC - forcing XXHash to return seed only data (here, seed = 0) */
  const uint64_t PRIME64_1 = 11400714785074694791ULL;
  const uint64_t PRIME64_2 = 14029467366897019727ULL;
  const uint64_t PRIME64_3 = 1609587929392839161ULL;
  const uint64_t PRIME64_4 = 9650029242287828579ULL;
  const uint64_t PRIME64_2_INV = 0x0BA79078168D4BAFULL;
  const uint64_t seed_manipulation[4] = {PRIME64_1 + PRIME64_2, PRIME64_2, 0,
                                         -PRIME64_1};
  uint64_t v[4] = {0, 0, 0, 0};
  /* attack seed manipulation */
  v[0] = v[0] - seed_manipulation[0];
  v[1] = v[1] - seed_manipulation[1];
  v[2] = v[2] - seed_manipulation[2];
  v[3] = v[3] - seed_manipulation[3];
  /* attack v += XXH_get64bits(p) * PRIME64_2 */
  v[0] = v[0] * PRIME64_2_INV;
  v[1] = v[1] * PRIME64_2_INV;
  v[2] = v[2] * PRIME64_2_INV;
  v[3] = v[3] * PRIME64_2_INV;

  uint64_t seed = 2870177450012600261ULL;
  uint64_t expected_seed;

  /* I didn't work out how to extract the seeed from this part */
  expected_seed = fio_lrot(seed, 1) + fio_lrot(seed, 7) + fio_lrot(seed, 12) +
                  fio_lrot(seed, 18);
  uint64_t tmp = seed * PRIME64_2;
  tmp = fio_lrot(tmp, 31);
  tmp *= PRIME64_1;
  expected_seed ^= tmp;
  expected_seed = expected_seed * PRIME64_1 + PRIME64_4;
  expected_seed ^= tmp;
  expected_seed = expected_seed * PRIME64_1 + PRIME64_4;
  expected_seed ^= tmp;
  expected_seed = expected_seed * PRIME64_1 + PRIME64_4;
  expected_seed ^= tmp;
  expected_seed = expected_seed * PRIME64_1 + PRIME64_4;
  expected_seed += 32;
  expected_seed ^= expected_seed >> 33;
  expected_seed *= PRIME64_2;
  expected_seed ^= expected_seed >> 29;
  expected_seed *= PRIME64_3;
  expected_seed ^= expected_seed >> 32;

  uint64_t seed_data = XXH64(v, 32, 0);
  if (seed_data == expected_seed)
    fprintf(stderr, "XXHash extraxted seed data matches expectations!\n");
  else
    fprintf(stderr, "Seed extraction failed %llu\n", seed_data);
  //   char b[128] = {0};
  //   fio_ltoa(b, v[0], 16);
  //   fio_ltoa(b + 32, v[1], 16);
  //   fio_ltoa(b + 64, v[2], 16);
  //   fio_ltoa(b + 96, v[3], 16);
  //   fprintf(stderr, "Message was:\n%s\n%s\n%s\n%s\n", b, b + 32, b + 64, b +
  //           96);
  // Output (message):
  //    0xFB9FE7DB392000B6
  //    0xFFFFFFFFFFFFFFFF
  //    0x0000000000000000
  //    0x04601824C6DFFF49
}

/**
 * Attacking 64 byte messages where the last 32 bytes are the same and the first
 * 32 bytes use rotating 8 byte words. This is attcking the following part in
 * the code:
 *
 *    U64 v1 = seed + PRIME64_1 + PRIME64_2;
 *    U64 v2 = seed + PRIME64_2;
 *    U64 v3 = seed + 0;
 *    U64 v4 = seed - PRIME64_1;
 *
 *    do {
 *      v1 += XXH_get64bits(p) * PRIME64_2;
 *      p += 8;
 *      v1 = XXH_rotl64(v1, 31);
 *      v1 *= PRIME64_1;
 *      //... v2, v3, v4 same;
 *    } while (p <= limit);
 *
 *    h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) +
 *          XXH_rotl64(v4, 18);
 */

FIO_FUNC void add_bad4xxhash(void) {
  attack_xxhash();
  const uint64_t PRIME64_1 = 11400714785074694791ULL;
  const uint64_t PRIME64_2 = 14029467366897019727ULL;
  const uint64_t PRIME64_1_INV = inverse64(PRIME64_1);
  const uint64_t PRIME64_2_INV = inverse64(PRIME64_2);
  const uint64_t seed_manipulation[4] = {PRIME64_1 + PRIME64_2, PRIME64_2, 0,
                                         -PRIME64_1};

  uint64_t rotating[4] = {0x1, 0x20, 0x300, 0x4000};
  uint8_t results[32][16] = {{0}};
  uint8_t results_count = 0;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (i == j) /* all 4 rotating words must be present */
        continue;
      /* mix rotating word order */
      uint64_t v[4] = {rotating[i], rotating[j], rotating[3 - i],
                       rotating[3 - j]};
      /* prepare vector against h64 = XXH_rotl64... */
      v[0] = (v[0] >> 1) | (v[0] << (64 - 1));
      v[1] = (v[1] >> 7) | (v[1] << (64 - 7));
      v[2] = (v[2] >> 12) | (v[2] << (64 - 12));
      v[3] = (v[3] >> 18) | (v[3] << (64 - 18));
      /* attack v *= PRIME64_1 */
      v[0] = v[0] * PRIME64_1_INV;
      v[1] = v[1] * PRIME64_1_INV;
      v[2] = v[2] * PRIME64_1_INV;
      v[3] = v[3] * PRIME64_1_INV;
      /* attack v = XXH_rotl64(v, 31) */
      v[0] = (v[0] >> 31) | (v[0] << (64 - 31));
      v[1] = (v[1] >> 31) | (v[1] << (64 - 31));
      v[2] = (v[2] >> 31) | (v[2] << (64 - 31));
      v[3] = (v[3] >> 31) | (v[3] << (64 - 31));
      /* attack seed manipulation */
      v[0] = v[0] - seed_manipulation[0];
      v[1] = v[1] - seed_manipulation[1];
      v[2] = v[2] - seed_manipulation[2];
      v[3] = v[3] - seed_manipulation[3];
      /* attack v += XXH_get64bits(p) * PRIME64_2 */
      v[0] = v[0] * PRIME64_2_INV;
      v[1] = v[1] * PRIME64_2_INV;
      v[2] = v[2] * PRIME64_2_INV;
      v[3] = v[3] * PRIME64_2_INV;
      /* copy to results, if unique */
      uint8_t unique = 1;
      for (int t = 0; t < results_count; ++t) {
        if (!memcmp(&results[0][t], v, 32))
          unique = 0;
      }
      if (unique) {
        memcpy(&results[0][results_count], v, 32);
        ++results_count;
      }
    }
  }
  if (results_count) {
    fprintf(stderr, "Created %u vectors, now testing...\n", results_count);
    uint64_t origin = XXH64(&results[0][0], 32, 0);
    for (int i = 0; i < results_count; ++i) {
      words_push(&words, FIO_STR_INIT_STATIC2(&results[0][i], 32));
      if (i && origin == XXH64(&results[0][i], 32, 0))
        fprintf(stderr, "Possible collision [%d]\n", i);
    }
    fprintf(stderr, "Done testing.\n");
  }
}

FIO_FUNC void add_bad4risky(void) {}

FIO_FUNC void find_bit_collisions(hashing_func_fn fn, size_t collision_count,
                                  uint8_t bit_count) {
  words_s c = FIO_ARY_INIT;
  const uint64_t mask = (1ULL << bit_count) - 1;
  time_t start = clock();
  while (words_count(&c) < collision_count) {
    uint64_t rnd = fio_rand64();
    if ((fn((char *)&rnd, 8) & mask) == mask) {
      words_push(&c, FIO_STR_INIT_STATIC2((char *)&rnd, 8));
    }
  }
  time_t end = clock();
  char *name = hash_name(fn);
  if (!name)
    name = "unknown";
  fprintf(stderr,
          "* It took %zu cycles to find %zu (%u bit) collisions for %s (brute "
          "fource):\n",
          end - start, words_count(&c), bit_count, name);
  FIO_ARY_FOR(&c, pos) {
    uint64_t tmp = fio_str2u64(fio_str_data(pos));
    fprintf(stderr, "* %p => %p\n", (void *)tmp,
            (void *)fn(fio_str_data(pos), 8));
  }
  words_free(&c);
}

static void add_bad_words(void) {
  if (!fio_cli_get("-t")) {
    find_bit_collisions(risky, 16, 16);
    find_bit_collisions(xxhash_test, 16, 16);
    find_bit_collisions(siphash13, 16, 16);
    find_bit_collisions(sha1, 16, 16);
  }
  add_bad4xxhash();
  add_bad4risky();
}

/* *****************************************************************************
Hash experimentation workspace
***************************************************************************** */
/* Risky Hash consumption round, accepts a state word s and an input word w */
#define fio_risky_consume(s, w)                                                \
  (s) ^= (w);                                                                  \
  (s) = fio_lrot64((s), 33) + (w);                                             \
  (s) *= primes[0];

/*  Computes a facil.io Risky Hash. */
static inline uintptr_t fio_risky_hash2(const void *data_, size_t len,
                                        uint64_t seed) {
  /* The primes used by Risky Hash */
  const uint64_t primes[] = {
      0xFBBA3FA15B22113B, // 1111101110111010001111111010000101011011001000100001000100111011
      0xAB137439982B86C9, // 1010101100010011011101000011100110011000001010111000011011001001
  };
  /* The consumption vectors initialized state */
  uint64_t v[4] = {
      seed ^ primes[1],
      ~seed + primes[1],
      fio_lrot64(seed, 17) ^ (primes[1] + primes[0]),
      fio_lrot64(seed, 33) + (~primes[1]),
  };

  /* reading position */
  const uint8_t *data = (uint8_t *)data_;

  /* consume 256bit blocks */
  for (size_t i = len >> 5; i; --i) {
    fio_risky_consume(v[0], fio_str2u64(data));
    fio_risky_consume(v[1], fio_str2u64(data + 8));
    fio_risky_consume(v[2], fio_str2u64(data + 16));
    fio_risky_consume(v[3], fio_str2u64(data + 24));
    data += 32;
  }
  /* Consume any remaining 64 bit words. */
  switch (len & 24) {
  case 24:
    fio_risky_consume(v[2], fio_str2u64(data + 16));
  case 16: /* overflow */
    fio_risky_consume(v[1], fio_str2u64(data + 8));
  case 8: /* overflow */
    fio_risky_consume(v[0], fio_str2u64(data));
    data += len & 24;
  }

  uint64_t tmp = 0;
  /* consume leftover bytes, if any */
  switch ((len & 7)) {
  case 7: /* overflow */
    tmp |= ((uint64_t)data[6]) << 8;
  case 6: /* overflow */
    tmp |= ((uint64_t)data[5]) << 16;
  case 5: /* overflow */
    tmp |= ((uint64_t)data[4]) << 24;
  case 4: /* overflow */
    tmp |= ((uint64_t)data[3]) << 32;
  case 3: /* overflow */
    tmp |= ((uint64_t)data[2]) << 40;
  case 2: /* overflow */
    tmp |= ((uint64_t)data[1]) << 48;
  case 1: /* overflow */
    tmp |= ((uint64_t)data[0]) << 56;
    /* ((len & 24) >> 3) is a 0-3 value representing the next state vector */
    /* `switch` allows v[i] to be a register without a memory address */
    /* using v[(len & 24) >> 3] forces implementation to use memory */
    switch ((len & 24) >> 3) {
    case 3:
      fio_risky_consume(v[3], tmp);
      break;
    case 2:
      fio_risky_consume(v[2], tmp);
      break;
    case 1:
      fio_risky_consume(v[1], tmp);
      break;
    case 0:
      fio_risky_consume(v[0], tmp);
      break;
    }
  }

  /* merge and mix */
  uint64_t result = fio_lrot64(v[0], 17) + fio_lrot64(v[1], 13) +
                    fio_lrot64(v[2], 47) + fio_lrot64(v[3], 57);
  result += len;
  result += v[0] * primes[1];
  result ^= fio_lrot64(result, 13);
  result += v[1] * primes[1];
  result ^= fio_lrot64(result, 29);
  result += v[2] * primes[1];
  result ^= fio_lrot64(result, 33);
  result += v[3] * primes[1];
  result ^= fio_lrot64(result, 51);

  /* irreversible avalanche... I think */
  result ^= (result >> 29) * primes[0];
  return result;
}

#undef fio_risky_consume

inline FIO_FUNC uintptr_t fio_risky_hash_old(void *data_, size_t len,
                                             uint64_t seed) {
  /* inspired by xxHash: Yann Collet, Maciej Adamczyk... */
  /* so I borrowed their primes as homage ;-) */
  /* more primes at: https://asecuritysite.com/encryption/random3?val=64 */
  const uint64_t primes[] = {
      /* xxHash Primes */
      14029467366897019727ULL, 11400714785074694791ULL, 1609587929392839161ULL,
      9650029242287828579ULL,  2870177450012600261ULL,
  };
  /*
   * 4 x 64 bit vectors for 256bit block consumption.
   * When implementing a streaming variation, more fields might be required.
   */
  struct risky_state_s {
    uint64_t v[4];
  } s = {{
      (seed + primes[0] + primes[1]),
      ((~seed) + primes[0]),
      ((seed << 9) ^ primes[3]),
      ((seed >> 17) ^ primes[2]),
  }};

/* A single data-consuming round, wrd is the data in big-endian 64 bit */
/* the design follows the xxHash basic round scheme and is easy to vectorize */
#define fio_risky_round_single(wrd, i)                                         \
  s.v[(i)] += (wrd)*primes[0];                                                 \
  s.v[(i)] = fio_lrot64(s.v[(i)], 33);                                         \
  s.v[(i)] *= primes[1];

/* an unrolled (vectorizable) 256bit round */
#define fio_risky_round_256(w0, w1, w2, w3)                                    \
  fio_risky_round_single(w0, 0);                                               \
  fio_risky_round_single(w1, 1);                                               \
  fio_risky_round_single(w2, 2);                                               \
  fio_risky_round_single(w3, 3);

  uint8_t *data = (uint8_t *)data_;

  /* loop over 256 bit "blocks" */
  const size_t len_256 = len & (((size_t)-1) << 5);
  for (size_t i = 0; i < len_256; i += 32) {
    /* perform round for block */
    fio_risky_round_256(fio_str2u64(data), fio_str2u64(data + 8),
                        fio_str2u64(data + 16), fio_str2u64(data + 24));
    data += 32;
  }

  /* process last 64bit words in each vector */
  switch (len & 24UL) {
  case 24:
    fio_risky_round_single(fio_str2u64(data), 0);
    fio_risky_round_single(fio_str2u64(data + 8), 1);
    fio_risky_round_single(fio_str2u64(data + 16), 2);
    data += 24;
    break;
  case 16:
    fio_risky_round_single(fio_str2u64(data), 0);
    fio_risky_round_single(fio_str2u64(data + 8), 1);
    data += 16;
    break;
  case 8:
    fio_risky_round_single(fio_str2u64(data), 0);
    data += 8;
    break;
  }

  /* always process the last 64bits, if any, in the 4th vector */
  uint64_t last_bytes = 0;
  switch (len & 7) {
  case 7:
    last_bytes |= ((uint64_t)data[6] & 0xFF) << 56;
  case 6: /* overflow */
    last_bytes |= ((uint64_t)data[5] & 0xFF) << 48;
  case 5: /* overflow */
    last_bytes |= ((uint64_t)data[4] & 0xFF) << 40;
  case 4: /* overflow */
    last_bytes |= ((uint64_t)data[3] & 0xFF) << 32;
  case 3: /* overflow */
    last_bytes |= ((uint64_t)data[2] & 0xFF) << 24;
  case 2: /* overflow */
    last_bytes |= ((uint64_t)data[1] & 0xFF) << 16;
  case 1: /* overflow */
    last_bytes |= ((uint64_t)data[0] & 0xFF) << 8;
    fio_risky_round_single(last_bytes, 3);
  }

  /* mix stage */
  uint64_t result = (fio_lrot64(s.v[3], 63) + fio_lrot64(s.v[2], 57) +
                     fio_lrot64(s.v[1], 52) + fio_lrot64(s.v[0], 46));
  result += len * primes[4];
  result = ((result ^ s.v[0]) * primes[3]) + primes[2];
  result = ((result ^ s.v[1]) * primes[3]) + primes[2];
  result = ((result ^ s.v[2]) * primes[3]) + primes[2];
  result = ((result ^ s.v[3]) * primes[3]) + primes[2];
  /* avalanche */
  result ^= (result >> 33);
  result *= primes[1];
  result ^= (result >> 29);
  result *= primes[2];
  return result;

#undef fio_risky_round_single
#undef fio_risky_round_256
}

#if TEST_XXHASH
#include "xxhash.c"
#endif
