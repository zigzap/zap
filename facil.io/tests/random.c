#include "fio.h"

#define HWD_BITS 64

static uint64_t next(void) { return fio_rand64(); }

/*
 * Copyright (C) 2004-2016 David Blackman.
 * Copyright (C) 2017-2018 David Blackman and Sebastiano Vigna.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <assert.h>
#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef HWD_MMAP
#include <sys/mman.h>
#endif

/*
   HWD 1.1 (2018-05-24)

   This code implements the Hamming-weight dependency test based on z9
   from gjrand 4.2.1.0 and described in detail in

   David Blackman and Sebastiano Vigna, "Scrambled linear pseudorandom number
   generators", 2018.

   Please refer to the paper for details about the test.

   To compile, you must define:

   - HWD_BITS, which is the width of the word tested (parameter w in the paper);
     must be 32, 64, or 128.
   - HWD_PRNG_BITS, which is the number of bits output by the PRNG, and it is by
     default HWD_BITS. Presently legal combinations are 32/32, 32/64,
     64/64 and 128/64.
   - Optionally HWD_DIM, which defines the length of the signatures examined
     (parameter k in the paper). Valid values are between 1 and 19;
     the default value is 8.
   - Optionally, HWD_NOPOPCOUNT, if your compiler does not support gcc's
   builtins.
   - Optionally, HWD_NUMCATS, if you want to override the default number
     of categories. Valid values are between 1 and HWD_DIM; the default value
     is HWD_DIM/2 + 1.
   - Optionally, HWD_MMAP if you want to allocate memory in huge pages using
   mmap().

   You must insert the code for your PRNG, providing a suitable next()
   method (returning a uint32_t or a uint64_t, depending on HWD_PRNG_BITS)
   at the HERE comment below. You may additionally initialize his state in
   the main() if necessary.
*/

#ifndef HWD_DIM
// This must be at most 19
#define DIM (8)
#else
#define DIM (HWD_DIM)
#endif

#ifndef HWD_NUMCATS
// This must be at most DIM
#define NUMCATS (DIM / 2 + 1)
#else
#define NUMCATS (HWD_NUMCATS)
#endif

// Number of bits used for the sum in cs[] (small counters/sums).
#define SUM_BITS (19)

// Compile-time computation of 3^DIM
#define SIZE                                                                   \
  ((DIM >= 1 ? UINT64_C(3) : UINT64_C(1)) * (DIM >= 2 ? 3 : 1) *               \
   (DIM >= 3 ? 3 : 1) * (DIM >= 4 ? 3 : 1) * (DIM >= 5 ? 3 : 1) *              \
   (DIM >= 6 ? 3 : 1) * (DIM >= 7 ? 3 : 1) * (DIM >= 8 ? 3 : 1) *              \
   (DIM >= 9 ? 3 : 1) * (DIM >= 10 ? 3 : 1) * (DIM >= 11 ? 3 : 1) *            \
   (DIM >= 12 ? 3 : 1) * (DIM >= 13 ? 3 : 1) * (DIM >= 14 ? 3 : 1) *           \
   (DIM >= 15 ? 3 : 1) * (DIM >= 16 ? 3 : 1) * (DIM >= 17 ? 3 : 1) *           \
   (DIM >= 18 ? 3 : 1) * (DIM >= 19 ? 3 : 1))

// Fast division by 3; works up to DIM = 19.
#define DIV3(x) ((x)*UINT64_C(1431655766) >> 32)

#ifndef HWD_PRNG_BITS
#define HWD_PRNG_BITS HWD_BITS
#endif

// batch_size values MUST be even. P is the probability of a 1 trit.

#if HWD_BITS == 32

#define P (0.40338510414585471153)
const int64_t batch_size[] = {-1,
                              UINT64_C(16904),
                              UINT64_C(37848),
                              UINT64_C(88680),
                              UINT64_C(213360),
                              UINT64_C(520784),
                              UINT64_C(1280664),
                              UINT64_C(3160976),
                              UINT64_C(7815952),
                              UINT64_C(19342248),
                              UINT64_C(47885112),
                              UINT64_C(118569000),
                              UINT64_C(293614056),
                              UINT64_C(727107408),
                              UINT64_C(1800643824),
                              UINT64_C(4459239480),
                              UINT64_C(11043223056),
                              UINT64_C(27348419104),
                              UINT64_C(67728213816),
                              UINT64_C(167728896072)};

#if HWD_PRNG_BITS == 64
static uint64_t next(void);
#define TEST_ITERATIONS(b) ((b) / 2)
#elif HWD_PRNG_BITS == 32
#define TEST_ITERATIONS(b) (b)
static uint32_t next(void);
#else
#error "Test 32-bit test supports PRNG of size 32 or 64"
#endif

#elif HWD_BITS == 64

#define P (0.46769122397215788544)
const int64_t batch_size[] = {-1,
                              UINT64_C(14744),
                              UINT64_C(28320),
                              UINT64_C(56616),
                              UINT64_C(116264),
                              UINT64_C(242784),
                              UINT64_C(512040),
                              UINT64_C(1086096),
                              UINT64_C(2311072),
                              UINT64_C(4926224),
                              UINT64_C(10510376),
                              UINT64_C(22435504),
                              UINT64_C(47903280),
                              UINT64_C(102294608),
                              UINT64_C(218459240),
                              UINT64_C(466556056),
                              UINT64_C(996427288),
                              UINT64_C(2128099936),
                              UINT64_C(4545075936),
                              UINT64_C(9707156552)};

#if HWD_PRNG_BITS == 64
#define TEST_ITERATIONS(b) (b)
static uint64_t next(void);
#else
#error "Test 64-bit test supports PRNGs of size 64"
#endif

#elif HWD_BITS == 128

#define P (0.46373128592889397439)
const int64_t batch_size[] = {-1,
                              UINT64_C(14856),
                              UINT64_C(28792),
                              UINT64_C(58088),
                              UINT64_C(120392),
                              UINT64_C(253680),
                              UINT64_C(539816),
                              UINT64_C(1155104),
                              UINT64_C(2479360),
                              UINT64_C(5330680),
                              UINT64_C(11471256),
                              UINT64_C(24696808),
                              UINT64_C(53183328),
                              UINT64_C(114541856),
                              UINT64_C(246706584),
                              UINT64_C(531387952),
                              UINT64_C(1144590984),
                              UINT64_C(2465432776),
                              UINT64_C(5310537968),
                              UINT64_C(11438933136)};

#if HWD_PRNG_BITS == 64
#define TEST_ITERATIONS(b) (b)
static uint64_t next(void);
#else
#error "Test 128-bit test supports PRNG of size 64"
#endif

#else
#error "Please define HWD_BITS as 32, 64, or 128"
#endif

#if HWD_BITS == 64 || HWD_BITS == 128

#define WTYPE uint64_t
#ifdef HWD_NO_POPCOUNT
static inline int popcount64(uint64_t x) {
  x = x - ((x >> 1) & 0x5555555555555555);
  x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
  x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f;
  x = x + (x >> 8);
  x = x + (x >> 16);
  x = x + (x >> 32);
  return x & 0x7f;
}
#else
#define popcount64(x) __builtin_popcountll(x)
#endif

#else /* HWD_BITS == 32 */

#define WTYPE uint32_t
#ifdef HWD_NO_POPCOUNT
static inline int popcount32(uint32_t x) {
  x = x - ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0f0f0f0f;
  x = x + (x >> 8);
  x = x + (x >> 16);
  return x & 0x7f;
}
#else
#define popcount32(x) __builtin_popcount((uint32_t)x)
#endif

#endif

/* Probability that the smallest of n numbers in [0..1) is <= x . */
static double pco_scale(double x, double n) {
  if (x >= 1.0 || x <= 0.0)
    return x;

  /* This is the result we want: return 1.0 - pow(1.0 - x, n); except the
     important cases are with x very small so this method gives better
     accuracy. */

  return -expm1(log1p(-x) * n);
}

/* The idea of the test is based around Hamming weights. We calculate the
   average number of bits per BITS-bit word and how it depends on the
   weights of the previous DIM words. There are SIZE different categories
   for the previous words. For each one accumulate number of samples
   (get_count(cs[j]) and count_sum[j].c) and number of bits per sample
   (get_sum(cs[j]) and count_sum[j].s) .

   To increase cache hits, we pack a 13-bit unsigned counter (upper bits)
   and a and a 19-bit unsigned sum of Hamming weights (lower bits) into a
   uint32_t. It would make sense to use bitfields, but in this way
   update_cs() can update both fields with a single sum. */

static inline int get_count(uint32_t cs) { return cs >> SUM_BITS; }

static inline int get_sum(uint32_t cs) { return cs & ((1 << SUM_BITS) - 1); }

/* We add bc to the sum field of *p then add 1 to the count field. */
static inline void update_cs(int bc, uint32_t *p) {
  *p += bc + (1 << SUM_BITS);
}

#ifdef HWD_MMAP
// "Small" counters/sums
static uint32_t *cs;

// "Large" counters/sums
static struct {
  uint64_t c;
  int64_t s;
} * count_sum;
#else
// "Small" counters/sums
static uint32_t cs[SIZE];

// "Large" counters/sums
static struct {
  uint64_t c;
  int64_t s;
} count_sum[SIZE];
#endif

#if HWD_BITS == 128

/* Keeps track of the sum of values, as is in this case it is not
   guaranteed not to overflow (but probability is infinitesimal if the
   source is random). */
static int64_t tot_sums;

/* Copy accumulated numbers out of cs[] into count_sum, then zero the ones
   in cs[]. We have to check explicitly that values do not overflow. */

static void desat(const int64_t next_batch_size) {
  int64_t c = 0, s = 0;

  for (int i = 0; i < SIZE; i++) {
    const int32_t st = cs[i];

    const int count = get_count(st);
    const int sum = get_sum(st);

    c += count;
    s += sum;

    count_sum[i].c += count;
    /* In cs[] the total Hamming weight is stored as actual weight. In
       count_sum, it is stored as difference from expected average
       Hamming weight, hence (BITS/2) * count */
    count_sum[i].s += sum - (HWD_BITS / 2) * count;
    cs[i] = 0;
  }

  if (c != next_batch_size || s != tot_sums) {
    fprintf(stderr, "Counters or values overflowed. Seriously non-random.\n");
    printf("p = %.3g\n", 1e-100);
    exit(0);
  }
}

#else

/* Copy accumulated numbers out of cs[] into count_sum, then zero the ones
   in cs[]. Note it is impossible for totals to overflow unless counts do. */

static void desat(const int64_t next_batch_size) {
  int64_t c = 0;

  for (uint64_t i = 0; i < SIZE; i++) {
    const int32_t st = cs[i];
    const int count = get_count(st);

    c += count;

    count_sum[i].c += count;
    /* In cs[] the total Hamming weight is stored as actual weight. In
       count_sum, it is stored as difference from expected average
       Hamming weight, hence (BITS/2) * ct */
    count_sum[i].s += get_sum(st) - (HWD_BITS / 2) * count;
    cs[i] = 0;
  }

  if (c != next_batch_size) {
    fprintf(stderr, "Counters overflowed. Seriously non-random.\n");
    printf("p = %.3g\n", 1e-100);
    exit(0);
  }
}

#endif

/* sig is the last signature from the previous call. At each step it
   contains an index into cs[], derived from the Hamming weights of the
   previous DIM numbers. Considered as a base 3 number, the most
   significant digit is the most recent trit. n is the batch size. */

#if HWD_BITS == 32

static inline uint32_t scan_batch(uint32_t sig, int64_t n, uint32_t *ts) {
  uint32_t t = ts ? *ts : 0;
  int bc;

  for (int64_t i = 0; i < n; i++) {
#if HWD_PRNG_BITS == 64
    const uint64_t w64 = next();
    uint32_t w32 = w64 >> 32;
    if (ts) {
      bc = popcount32(w32 ^ w32 << 1 ^ t);
      t = w32 >> 31;
    } else
      bc = popcount32(w32);

    update_cs(bc, cs + sig);
    sig = DIV3(sig) + ((bc >= 15) + (bc >= 18)) * (SIZE / 3);

    w32 = w64;

    if (ts) {
      bc = popcount32(w32 ^ w32 << 1 ^ t);
      t = w32 >> 31;
    } else
      bc = popcount32(w32);

    update_cs(bc, cs + sig);
    sig = DIV3(sig) + ((bc >= 15) + (bc >= 18)) * (SIZE / 3);
#else
    const uint32_t w = next();
    if (ts) {
      bc = popcount32(w ^ w << 1 ^ t);
      t = w >> 31;
    } else
      bc = popcount32(w);

    update_cs(bc, cs + sig);
    sig = DIV3(sig) + ((bc >= 15) + (bc >= 18)) * (SIZE / 3);
#endif
  }

  if (ts)
    *ts = t;
  /* return the current signature so it can be passed back in on the next batch
   */
  return sig;
}

#elif HWD_BITS == 64

static inline uint32_t scan_batch(uint32_t sig, int64_t n, uint64_t *ts) {
  uint64_t t = ts ? *ts : 0;
  int bc;

  for (int64_t i = 0; i < n; i++) {
    const uint64_t w = next();

    if (ts) {
      bc = popcount64(w ^ w << 1 ^ t);
      t = w >> 63;
    } else
      bc = popcount64(w);

    update_cs(bc, cs + sig);
    sig = DIV3(sig) + ((bc >= 30) + (bc >= 35)) * (SIZE / 3);
  }

  if (ts)
    *ts = t;
  /* return the current signature so it can be passed back in on the next batch
   */
  return sig;
}

#else

static inline uint32_t scan_batch(uint32_t sig, int64_t n, uint64_t *ts) {
  uint64_t t = ts ? *ts : 0;
  int bc;
  tot_sums = 0; // In this case we have to keep track of the values, too

  for (int64_t i = 0; i < n; i++) {
    const uint64_t w0 = next();
    const uint64_t w1 = next();

    if (ts) {
      bc = popcount64(w0 ^ w0 << 1 ^ t);
      bc += popcount64(w1 ^ (w1 << 1) ^ (w0 >> 63));
      t = w1 >> 63;
    } else
      bc = popcount64(w0) + popcount64(w1);

    tot_sums += bc;
    update_cs(bc, cs + sig);
    sig = DIV3(sig) + ((bc >= 61) + (bc >= 68)) * (SIZE / 3);
  }

  if (ts)
    *ts = t;
  /* return the current signature so it can be passed back in on the next batch
   */
  return sig;
}

#endif

/* Now we're out of the the accumulate phase, which is the inside loop.
   Next is analysis. */

/* Mostly a debugging printf, though it can tell you a bit about the
   structure of a prng when it fails. Print sig out in base 3, least
   significant digits first. This means the most recent trit is the
   rightmost. */

static void print_sig(uint32_t sig) {
  for (uint64_t i = DIM; i > 0; i--) {
    putchar(sig % 3 + '0');
    sig /= 3;
  }
}

#ifndef M_SQRT1_2
/* 1.0/sqrt(2.0) */
#define M_SQRT1_2 0.70710678118654752438
#endif
/* 1.0/sqrt(3.0) */
#define CORRECT3 0.57735026918962576451
/* 1.0/sqrt(6.0) */
#define CORRECT6 0.40824829046386301636

/* This is a transform similar in spirit to the Walsh-Hadamard transform
  (see the paper). It's ortho-normal. So with independent normal
  distribution mean 0 standard deviation 1 in, we get independent normal
  distribution mean 0 standard deviation 1 out, except maybe for element 0.
  And of course, for certain kinds of bad prngs when the null hypthosis is
  false, some of these numbers will get extreme. */

static void mix3(double *ct, int sig) {
  double *p1 = ct + sig, *p2 = p1 + sig;
  double a, b, c;

  for (int i = 0; i < sig; i++) {
    a = ct[i];
    b = p1[i];
    c = p2[i];
    ct[i] = (a + b + c) * CORRECT3;
    p1[i] = (a - c) * M_SQRT1_2;
    p2[i] = (2 * b - a - c) * CORRECT6;
  }

  sig = DIV3(sig);
  if (sig) {
    mix3(ct, sig);
    mix3(p1, sig);
    mix3(p2, sig);
  }
}

/* categorise sig based on nonzero ternary digits. */
static int cat(uint32_t sig) {
  int r = 0;

  while (sig) {
    r += (sig % 3) != 0;
    sig /= 3;
  }

  return (r >= NUMCATS ? NUMCATS : r) - 1;
}

/* Apply the transform; then, compute, log and return the resulting p-value. */

#ifdef HWD_MMAP
static double *norm;
#else
static double norm[SIZE]; // This might be large
#endif

static double compute_pvalue(const bool trans) {
  const double db = HWD_BITS * 0.25;

  for (uint64_t i = 0; i < SIZE; i++) {
    /* copy the bit count totals from count_sum[i].s to norm[i] with
       normalisation. We expect mean 0 standard deviation 1 db is the
       expected variance for Hamming weight of BITS-bit words.
       count_sum[i].c is number of samples */
    if (count_sum[i].c == 0)
      norm[i] = 0.0;
    else
      norm[i] = count_sum[i].s / sqrt(count_sum[i].c * db);
  }

  /* The transform. The wonderful transform. After this we expect still
     normalised to mean 0 stdev 1 under the null hypothesis. (But not for
     element 0 which we will ignore.) */
  mix3(norm, SIZE / 3);

  double overall_pvalue = DBL_MAX;

  /* To make the test more sensitive (see the paper) we split the
     elements of norm into NUMCAT categories. These are based only on the
     index into norm, not the content. We go though norm[], decide which
     category each one is in, and record the signature (sig[]) and the
     absolute value (sigma[]) For the most extreme value in each
     category. Also a count (cat_count[]) of how many were in each
     category. */

  double sigma[NUMCATS];
  uint32_t sig[NUMCATS], cat_count[NUMCATS] = {};
  for (int i = 0; i < NUMCATS; i++)
    sigma[i] = DBL_MIN;

  for (uint64_t i = 1; i < SIZE; i++) {
    const int c = cat(i);
    cat_count[c]++;
    const double x = fabs(norm[i]);
    if (x > sigma[c]) {
      sig[c] = i;
      sigma[c] = x;
    }
  }

  /* For each category, calculate a p-value, put the lowest into
     overall_pvalue, and print something out. */
  for (int i = 0; i < NUMCATS; i++) {
    printf("mix3 extreme = %.5f (sig = ", sigma[i]);
    print_sig(sig[i]);
    /* convert absolute value of approximate normal into p-value. */
    double pvalue = erfc(M_SQRT1_2 * sigma[i]);
    /* Ok, that's the lowest p-value cherry picked out of a choice of
       cat_count[i] of them. Must correct for that. */
    pvalue = pco_scale(pvalue, cat_count[i]);
    printf(") weight %s%d (%" PRIu32 "), p-value = %.3g\n",
           i == NUMCATS - 1 ? ">=" : "", i + 1, cat_count[i], pvalue);
    if (pvalue < overall_pvalue)
      overall_pvalue = pvalue;
  }

  printf("bits per word = %d (analyzing %s); min category p-value = %.3g\n\n",
         HWD_BITS, trans ? "transitions" : "bits", overall_pvalue);
  /* again, we're cherry picking worst of NUMCATS, so correct it again. */
  return pco_scale(overall_pvalue, NUMCATS);
}

static time_t tstart;
static double low_pvalue = DBL_MIN;

/* This is the call made when we want to print some analysis. This will be
   done multiple times if --progress is used. */
static void analyze(int64_t pos, bool trans, bool final) {

  if (pos < 2 * pow(2.0 / (1.0 - P), DIM))
    printf("WARNING: p-values are unreliable, you have to wait (insufficient "
           "data for meaningful answer)\n");

  const double pvalue = compute_pvalue(trans);
  const time_t tm = time(0);

  printf("processed %.3g bytes in %.3g seconds (%.4g GB/s, %.4g TB/h). %s\n",
         (double)pos, (double)(tm - tstart), pos * 1E-9 / (double)(tm - tstart),
         pos * (3600 * 1E-12) / (double)(tm - tstart), ctime(&tm));

  if (final)
    printf("final\n");
  printf("p = %.3g\n", pvalue);

  if (pvalue < low_pvalue)
    exit(0);

  if (!final)
    printf("------\n\n");
}

static int64_t progsize[] = {
    100000000, 125000000, 150000000, 175000000, 200000000, 250000000, 300000000,
    400000000, 500000000, 600000000, 700000000, 850000000, 0};

/* We use the all-one signature (the most probable) as initial signature. */
static int64_t pos;
static uint32_t last_sig = (SIZE - 1) / 2;
static WTYPE ts;
static int64_t next_progr = 100000000; // progsize[0]
static int progr_index;

static void run_test(const int64_t n, const bool trans, const bool progress) {

  WTYPE *const p = trans ? &ts : NULL;

  while (n < 0 || pos < n) {
    int64_t next_batch_size = batch_size[DIM];
    if (n >= 0 && (n - pos) / (HWD_BITS / 8) < next_batch_size)
      next_batch_size = (n - pos) / (HWD_BITS / 8) & ~UINT64_C(7);

    if (next_batch_size == 0)
      break;
    /* TEST_ITERATIONS() corrects batch_size depending on HWD_BITS and
     * HWD_PRNG_BITS */
    last_sig = scan_batch(last_sig, TEST_ITERATIONS(next_batch_size), p);
    desat(next_batch_size);
    pos += next_batch_size * (HWD_BITS / 8);

    if (progress && pos >= next_progr) {
      analyze(pos, trans, false);
      progsize[progr_index++] *= 10;
      next_progr = progsize[progr_index];
      if (next_progr == 0) {
        progr_index = 0;
        next_progr = progsize[0];
      }
    }
  }

  analyze(pos, trans, true);
}

int main(int argc, char **argv) {
  double dn;
  int64_t n = -1;
  bool trans = false, progress = false;

#ifdef HWD_MMAP
  fprintf(stderr, "Allocating memory via mmap()... ");
  // (SIZE + 1) is necessary for a correct memory alignment.
  cs = mmap(
      (void *)(0x0UL),
      (SIZE + 1) * sizeof *cs + SIZE * sizeof *norm + SIZE * sizeof *count_sum,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (30 << MAP_HUGE_SHIFT), 0, 0);
  if (cs == MAP_FAILED) {
    fprintf(stderr, "Failed.\n");
    exit(1);
  }
  fprintf(stderr, "OK.\n");
  norm = (void *)(cs + SIZE + 1);
  count_sum = (void *)(norm + SIZE);
#endif

  tstart = time(0);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--progress") == 0)
      progress = true;
    else if (strcmp(argv[i], "-t") == 0)
      trans = true;
    else if (sscanf(argv[i], "%lf", &dn) == 1)
      n = (int64_t)dn;
    else if (sscanf(argv[i], "--low-pv=%lf", &low_pvalue) == 1) {
    } else {
      fprintf(stderr, "Optional arg must be --progress or -t or "
                      "--low-pv=number or numeric\n");
      exit(1);
    }
  }

  if (n <= 0)
    progress = true;

  run_test(n, trans, progress);

  exit(0);
}
