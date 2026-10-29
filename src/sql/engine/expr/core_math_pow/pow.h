/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// The original upstream implementation and its MIT notice follow.
/* Correctly-rounded power function for two binary64 values.

Copyright (c) 2022-2025 CERN and Inria
Authors: Tom Hubrecht and Paul Zimmermann

This file is part of the CORE-MATH project
(https://core-math.gitlabpages.inria.fr/).

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef CR_POW_H
#define CR_POW_H

#include <stdint.h>
#include <math.h> // needed for NAN and INFINITY
#include <errno.h>

/*
  Type definition
*/

typedef union {
  double f;
  uint64_t u;
} f64_u;

// Extract both the mantissa and exponent of a double
static inline void fast_extract (int64_t *e, uint64_t *m, double x) {
  f64_u _x = {.f = x};

  *e = (_x.u >> 52) & 0x7ff;
  *m = (_x.u & (~0ull >> 12)) + (*e ? (1ull << 52) : 0);
  *e = *e - 0x3ff;
}

#define CORE_MATH_POW
#include "dint.h"
#include "qint.h"

double cr_pow(double x, double y);

/* __builtin_roundeven was introduced in gcc 10:
   https://gcc.gnu.org/gcc-10/changes.html,
   and in clang 17 */
#if ((defined(__GNUC__) && __GNUC__ >= 10) || (defined(__clang__) && __clang_major__ >= 17)) && !defined(_MSC_VER) && (defined(__aarch64__) || defined(__x86_64__) || defined(__i386__))
# define roundeven_finite(x) __builtin_roundeven (x)
#else
/* round x to nearest integer, breaking ties to even */
static double
roundeven_finite (double x)
{
  double ix;
# if (defined(__GNUC__) || defined(__clang__)) && (defined(__AVX__) || defined(__SSE4_1__) || (__ARM_ARCH >= 8))
#  if defined __AVX__
   __asm__("vroundsd $0x8,%1,%1,%0":"=x"(ix):"x"(x));
#  elif __ARM_ARCH >= 8
   __asm__ ("frintn %d0, %d1":"=w"(ix):"w"(x));
#  else /* __SSE4_1__ */
   __asm__("roundsd $0x8,%1,%0":"=x"(ix):"x"(x));
#  endif
# else
  ix = __builtin_round (x); /* nearest, away from 0 */
  if (__builtin_fabs (ix - x) == 0.5)
  {
    /* if ix is odd, we should return ix-1 if x>0, and ix+1 if x<0 */
    union { double f; uint64_t n; } u, v;
    u.f = ix;
    v.f = ix - __builtin_copysign (1.0, x);
    /* Warning: v.n is 0 when x=0.5; while u.n cannot be zero since ix
       is rounded away from zero. */
    if (v.n == 0 || __builtin_ctzll (v.n) > __builtin_ctzll (u.n))
      ix = v.f;
  }
# endif
  return ix;
}
#endif

/*
  Utility functions
*/

// When x is a NaN, returns 1 if x is an sNaN and 0 if it is a qNaN
static inline int is_signaling(double x) {
  f64_u _x = {.f = x};

  return !(_x.u & (1ull << 51));
}

/* Add a + b, such that *hi + *lo approximates a + b.
   Assumes |a| >= |b|.
   For rounding to nearest we have hi + lo = a + b exactly.
   For directed rounding, we have
   (a) hi + lo = a + b exactly when the exponent difference between a and b
       is at most 53 (the binary64 precision)
   (b) otherwise |(a+b)-(hi+lo)| <= 2^-105 min(|a+b|,|hi|)
       (see https://hal.inria.fr/hal-03798376)
   We also have |lo| < ulp(hi). */
static inline void fast_two_sum(double *hi, double *lo, double a, double b) {
  double e;

  // assert (a == 0 || __builtin_fabs (a) >= __builtin_fabs (b));
  *hi = a + b;
  e = *hi - a; /* exact */
  *lo = b - e; /* exact */
}

/* Algorithm 2 from https://hal.science/hal-01351529 */
static inline void two_sum (double *s, double *t, double a, double b)
{
  *s = a + b;
  double a_prime = *s - b;
  double b_prime = *s - a_prime;
  double delta_a = a - a_prime;
  double delta_b = b - b_prime;
  *t = delta_a + delta_b;
}

// Add a + (bh + bl), assuming |a| >= |bh|
static inline void fast_sum(double *hi, double *lo, double a, double bh,
                            double bl) {
  fast_two_sum(hi, lo, a, bh);
  /* |(a+bh)-(hi+lo)| <= 2^-105 |hi| and |lo| < ulp(hi) */
  *lo += bl;
  /* |(a+bh+bl)-(hi+lo)| <= 2^-105 |hi| + ulp(lo),
     where |lo| <= ulp(hi) + |bl|. */
}

// Multiply exactly a and b, such that *hi + *lo = a * b.
static inline void a_mul(double *hi, double *lo, double a, double b) {
  *hi = a * b;
  *lo = __builtin_fma (a, b, -*hi);
}

// Multiply a double with a double double : a * (bh + bl)
static inline void s_mul (double *hi, double *lo, double a, double bh,
                          double bl) {
  double s;

  a_mul (hi, &s, a, bh); /* exact */
  *lo = __builtin_fma (a, bl, s);
  /* the error is bounded by ulp(lo), where |lo| < |a*bl| + ulp(hi) */
}

// Returns (ah + al) * (bh + bl) - (al * bl)
// We can ignore al * bl when assuming al <= ulp(ah) and bl <= ulp(bh)
static inline void d_mul(double *hi, double *lo, double ah, double al,
                         double bh, double bl) {
  double s, t;

  a_mul(hi, &s, ah, bh);
  t = __builtin_fma(al, bh, s);
  *lo = __builtin_fma(ah, bl, t);
}

static inline void d_square(double *hi, double *lo, double ah, double al) {
  double s, b = al + al;

  a_mul(hi, &s, ah, ah);
  *lo = __builtin_fma(ah, b, s);
}

static inline long dtoi(double x) { return (long)x; }

// Returns 1 if x is an integer
static inline int is_int(double x) { return x == roundeven_finite (x); }

// Returns (e, m) such that m is odd and x = 2^E \times m
static inline void extract(int64_t *e, uint64_t *m, double x) {
  f64_u _x = {.f = x};

  *e = (_x.u >> 52) & 0x7ff;
  *m = (_x.u & (~0ull >> 12)) + (*e ? (1ull << 52) : 0);
  int32_t t = __builtin_ctzll(*m);
  *m = *m >> t;
  *e = *e + t - (0x433 - !*e);
}

// Rounds a dint64_t value to 54 bits, a shortcut is taken as in `exact_pow`, we
// only consider numbers that end with only ones or only zeroes
static inline void round_54(int64_t *G, int64_t *k, const dint64_t *x) {
  *G = x->ex - 53;
  *k = (x->hi >> 10) + ((x->hi >> 9) & 0x1);
}

// Multiply x by 2^e
static inline void pow2(double *x, int64_t e) {
  if (e & 0x1)
    *x *= 0x1p+1;

  f64_u e2 = {.u = ((uint64_t)((e >> 1) + 0x3ff) & 0x7ff) << 52};
  *x = (*x * e2.f) * e2.f;
}

// Convert a dint64_t value to an integer, rounding towards zero
static inline int64_t dint_toi(const dint64_t *a) {
  if (a->ex < 0)
    return 0ll;

  int64_t r = a->hi >> (63 - a->ex);

  return a->sgn ? -r : r;
}

// round a, assuming a is in the subnormal range
// exact is non-zero iff x^y is exact
static inline double dint_tod_subnormal(dint64_t *a, int exact) {
  int underflow = 1;
  double ret = 0;

  uint64_t ex = -(1011 + a->ex); // ex >= 12
  // we have to shift right hi,lo by ex bits so that the least significant
  // bit of hi corresponds to 2^-1074 (the number of extra bits is
  // -1022 - a->ex, and we add 11 = 64 - 53 since hi has 64 bits)

  uint64_t rb, sb;

  if (ex >= 64) { // all bits disappear: |a| < 2^-1074
    switch (fegetround()) {
    case FE_TONEAREST:
      rb = (a->hi >> 63);        // only used when e=64
      sb = (a->hi << 1) | a->lo; // idem
      ret = (ex > 64 || rb == 0 || sb == 0) ? +0.0 : 0x1p-1074;
      ret = (a->sgn) ? -ret : ret;
      break;
    case FE_DOWNWARD:
      ret = (a->sgn) ? -0x1p-1074 : +0.0;
      break;
    case FE_UPWARD:
      ret = (!a->sgn) ? 0x1p-1074 : -0.0;
      break;
    case FE_TOWARDZERO:
      ret = (a->sgn) ? -0.0 : +0.0;
    }
    goto end;
  }

  // now ex < 64
  uint64_t hi;
  hi = a->hi >> ex;
  rb = (a->hi >> (ex - 1)) & 0x1; // round bit
  sb = (a->hi << (65 - ex)) || a->lo; // sticky bit

  switch (fegetround()) {
  case FE_TONEAREST:
    // if ex=12 there is no underflow when hi rounds to 2^52 and rb=1
    // and the next bit is 1 too
    hi += sb ? rb : hi & rb;
    if (ex == 12 && (hi >> 52) && rb)
    {
      uint64_t rbb = (a->hi >> (ex - 2)) & 0x1; // next bit after the round bit
      if (rbb)
        underflow = 0;
    }
    break;
  case FE_DOWNWARD:
    // if ex=12 there is no underflow when hi rounds to 2^52 and rb=1
    hi += a->sgn & (sb | rb);
    if (ex == 12 && (hi >> 52) && rb)
      underflow = 0;
    break;
  case FE_UPWARD:
    // if ex=12 there is no underflow when hi rounds to 2^52 and rb=1
    hi += (!a->sgn) & (sb | rb);
    if (ex == 12 && (hi >> 52) && rb)
      underflow = 0;
    break;
  // for rounding towards zero, don't do anything
  }

  // now hi <= 2^52 stores the low bits of the result (up to sign)
  // (if hi has overflowed in 2^52 this is exactly what we want)

  f64_u v = {.u = hi};
  v.u |= a->sgn << 63;
  ret = v.f;

 end:
  if (underflow && !exact) {
    feraiseexcept (FE_UNDERFLOW); // raise underflow
#ifdef CORE_MATH_SUPPORT_ERRNO
    errno = ERANGE; // underflow
#endif
  }

  return ret;
}

// Convert a dint64_t value to a double
// exact is non-zero iff x^y is exact
static inline double dint_tod(dint64_t *a, int exact) {
  if (__builtin_expect (a->ex < -1022, 0))
    return dint_tod_subnormal (a, exact);

  // r is the significand in [1,2)
  f64_u r = {.u = (a->hi >> 11) | (0x3ffll << 52)};

  // round r
  double rd = 0.0;
  if ((a->hi >> 10) & 0x1)
    rd += 0x1p-53;

  if (a->hi & 0x3ff || a->lo)
    rd += 0x1p-54;

  if (a->sgn)
    rd = -rd;

  r.u = r.u | a->sgn << 63;
  r.f += rd;

  f64_u e;

  if (a->ex > -1023) { // The result is a normal double
    if (a->ex > 1023) {
      if (a->ex == 1024) { // 2^1024 <= |a| < 2^1025
        r.f = r.f * 0x1p+1;
        e.f = 0x1p+1023;
      } else { // |a| >= 2^1025
        r.f = 0x1.fffffffffffffp+1023;
        e.f = 0x1.fffffffffffffp+1023;
      }
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = ERANGE;
#endif
    }
    else
      e.u = ((a->ex + 1023) & 0x7ff) << 52;
  } else { // subnormal case
    if (!exact) {
      feraiseexcept (FE_UNDERFLOW); // raise underflow
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = ERANGE; // underflow
#endif
    }
    if (a->ex < -1074) {
      if (a->ex == -1075) {
        r.f = r.f * 0x1p-1;
        e.f = 0x1p-1074;
      } else {
        r.f = 0x0.0000000000001p-1022;
        e.f = 0x0.0000000000001p-1022;
      }
    } else {
      e.u = 1ll << (a->ex + 1074);
    }
  }

#ifdef CORE_MATH_SUPPORT_ERRNO
  if (r.f == 0x1p+1 && e.f == 0x1p+1023)
    errno = ERANGE; // overflow
#endif

  return r.f * e.f;
}

// Convert a double to the corresponding qint64_t value
static inline void qint_fromd (qint64_t *a, double b) {
  fast_extract (&a->ex, &a->hh, b);

  /* |b| = 2^(ex-52)*hi */

  uint32_t t = __builtin_clzll (a->hh);

  a->sgn = b < 0.0;
  a->ex = a->ex - (t > 11 ? t - 12 : 0);
  a->hh = a->hh << t;
  a->lh = 0;
  a->hl = 0;
  a->ll = 0;
  /* b = 2^ex*hh/2^64 where 1 <= hh/2^63 < 2 */
}

// Convert a qint64_t value to an integer
static inline int64_t qint_toi(const qint64_t *a) {
  if (a->ex < 0)
    return 0ll;

  int64_t r = a->hh >> (63 - a->ex);

  return a->sgn ? -r : r;
}

static inline void subnormalize_qint(qint64_t *a) {
  if (a->ex > -1023)
    return;

  uint64_t ex = -(1011 + a->ex);

  uint64_t hi = a->hh >> ex;
  uint64_t md = (a->hh >> (ex - 1)) & 0x1;
  uint64_t lo = (a->hh & (~0ull >> ex)) || a->hl || a->lh || a->ll;

  switch (fegetround()) {
  case FE_TONEAREST:
    hi += lo ? md : hi & md;
    break;
  case FE_DOWNWARD:
    hi += a->sgn & (md | lo);
    break;
  case FE_UPWARD:
    hi += (!a->sgn) & (md | lo);
    break;
  }

  a->hh = hi << ex;
  a->hl = 0;
  a->lh = 0;
  a->ll = 0;

  if (!a->hh) {
    a->ex++;
    a->hh = (1ull << 63);
  }
}

// Convert a dint64_t value to a double
static inline double qint_tod(qint64_t *a) {
  subnormalize_qint(a);

  f64_u r = {.u = (a->hh >> 11) | (0x3ffll << 52)};

  double rd = 0.0;
  if (a->hh & 0x400)
    rd += 0x1p-53;

  if (a->hh & 0x3ff || a->hl || a->lh || a->ll)
    rd += 0x1p-54;

  if (a->sgn)
    rd = -rd;

  r.u = r.u | a->sgn << 63;
  r.f += rd;

  f64_u e;

  if (a->ex > -1023) { // The result is a normal double
    if (a->ex > 1023)
      if (a->ex == 1024) {
        r.f = r.f * 0x1p+1;
        e.f = 0x1p+1023;
      } else {
        r.f = 0x1.fffffffffffffp+1023;
        e.f = 0x1.fffffffffffffp+1023;
      }
    else
      e.u = ((a->ex + 1023) & 0x7ff) << 52;
  } else { // subnormal case
    feraiseexcept (FE_UNDERFLOW); // raise underflow
#ifdef CORE_MATH_SUPPORT_ERRNO
    errno = ERANGE; // underflow
#endif
    if (a->ex < -1074) {
      if (a->ex == -1075) {
        r.f = r.f * 0x1p-1;
        e.f = 0x1p-1074;
      } else {
        r.f = 0x0.0000000000001p-1022;
        e.f = 0x0.0000000000001p-1022;
      }
    } else {
      e.u = 1ll << (a->ex + 1074);
    }
  }

  return r.f * e.f;
}

/*
  Approximation tables
*/

/* for 181 <= i <= 362, r[i] = _INVERSE[i-181] is a 9-bit approximation of
   1/x[i], where i*2^-8 <= x[i] < (i+1)*2^-8.
   More precisely r[i] is a 9-bit value such that r[i]*y-1 is representable
   exactly on 53 bits for for any y, i*2^-8 <= y < (i+1)*2^-8.
   Moreover |r[i]*y-1| < 0.0040283203125.
   Table generated with the accompanying pow.sage file,
   with l=inverse_centered(k=8,prec=9,maxbits=53,verbose=false) */
static const double _INVERSE[182]= {
    0x1.69p+0, 0x1.67p+0, 0x1.65p+0, 0x1.63p+0, 0x1.61p+0, 0x1.5fp+0, 0x1.5ep+0,
    0x1.5cp+0, 0x1.5ap+0, 0x1.58p+0, 0x1.56p+0, 0x1.54p+0, 0x1.53p+0, 0x1.51p+0,
    0x1.4fp+0, 0x1.4ep+0, 0x1.4cp+0, 0x1.4ap+0, 0x1.48p+0, 0x1.47p+0, 0x1.45p+0,
    0x1.44p+0, 0x1.42p+0, 0x1.4p+0, 0x1.3fp+0, 0x1.3dp+0, 0x1.3cp+0, 0x1.3ap+0,
    0x1.39p+0, 0x1.37p+0, 0x1.36p+0, 0x1.34p+0, 0x1.33p+0, 0x1.32p+0, 0x1.3p+0,
    0x1.2fp+0, 0x1.2dp+0, 0x1.2cp+0, 0x1.2bp+0, 0x1.29p+0, 0x1.28p+0, 0x1.27p+0,
    0x1.25p+0, 0x1.24p+0, 0x1.23p+0, 0x1.21p+0, 0x1.2p+0, 0x1.1fp+0, 0x1.1ep+0,
    0x1.1cp+0, 0x1.1bp+0, 0x1.1ap+0, 0x1.19p+0, 0x1.17p+0, 0x1.16p+0, 0x1.15p+0,
    0x1.14p+0, 0x1.13p+0, 0x1.12p+0, 0x1.1p+0, 0x1.0fp+0, 0x1.0ep+0, 0x1.0dp+0,
    0x1.0cp+0, 0x1.0bp+0, 0x1.0ap+0, 0x1.09p+0, 0x1.08p+0, 0x1.07p+0, 0x1.06p+0,
    0x1.05p+0, 0x1.04p+0, 0x1.03p+0, 0x1.02p+0, 0x1.00p+0, 0x1.00p+0, 0x1.fdp-1,
    0x1.fbp-1, 0x1.f9p-1, 0x1.f7p-1, 0x1.f5p-1, 0x1.f3p-1, 0x1.f1p-1, 0x1.fp-1,
    0x1.eep-1, 0x1.ecp-1, 0x1.eap-1, 0x1.e8p-1, 0x1.e6p-1, 0x1.e5p-1, 0x1.e3p-1,
    0x1.e1p-1, 0x1.dfp-1, 0x1.ddp-1, 0x1.dcp-1, 0x1.dap-1, 0x1.d8p-1, 0x1.d7p-1,
    0x1.d5p-1, 0x1.d3p-1, 0x1.d2p-1, 0x1.dp-1, 0x1.cep-1, 0x1.cdp-1, 0x1.cbp-1,
    0x1.c9p-1, 0x1.c8p-1, 0x1.c6p-1, 0x1.c5p-1, 0x1.c3p-1, 0x1.c2p-1, 0x1.cp-1,
    0x1.bfp-1, 0x1.bdp-1, 0x1.bcp-1, 0x1.bap-1, 0x1.b9p-1, 0x1.b7p-1, 0x1.b6p-1,
    0x1.b4p-1, 0x1.b3p-1, 0x1.b1p-1, 0x1.bp-1, 0x1.aep-1, 0x1.adp-1, 0x1.acp-1,
    0x1.aap-1, 0x1.a9p-1, 0x1.a7p-1, 0x1.a6p-1, 0x1.a5p-1, 0x1.a3p-1, 0x1.a2p-1,
    0x1.a1p-1, 0x1.9fp-1, 0x1.9ep-1, 0x1.9dp-1, 0x1.9cp-1, 0x1.9ap-1, 0x1.99p-1,
    0x1.98p-1, 0x1.96p-1, 0x1.95p-1, 0x1.94p-1, 0x1.93p-1, 0x1.91p-1, 0x1.9p-1,
    0x1.8fp-1, 0x1.8ep-1, 0x1.8dp-1, 0x1.8bp-1, 0x1.8ap-1, 0x1.89p-1, 0x1.88p-1,
    0x1.87p-1, 0x1.86p-1, 0x1.84p-1, 0x1.83p-1, 0x1.82p-1, 0x1.81p-1, 0x1.8p-1,
    0x1.7fp-1, 0x1.7ep-1, 0x1.7cp-1, 0x1.7bp-1, 0x1.7ap-1, 0x1.79p-1, 0x1.78p-1,
    0x1.77p-1, 0x1.76p-1, 0x1.75p-1, 0x1.74p-1, 0x1.73p-1, 0x1.72p-1, 0x1.71p-1,
    0x1.7p-1, 0x1.6fp-1, 0x1.6ep-1, 0x1.6dp-1, 0x1.6cp-1, 0x1.6bp-1, 0x1.6ap-1,
};

/* For 181 <= i <= 362, (h,l) = _LOG_INV[i-181] is a double-double nearest
   approximation of -log(r) for r=_INVERSE[i-181], h being an integer
   multiple of 2^-42.
   Since |l| < 2^-43, the maximal error is 1/2 ulp(l) <= 2^-97. */
static const double _LOG_INV[182][2] = {
    {-0x1.5ff3070a79p-2, -0x1.e9e439f105039p-45},
    {-0x1.5a42ab0f4dp-2, 0x1.e63af2df7ba69p-50},
    {-0x1.548a2c3addp-2, -0x1.3167e63081cf7p-45},
    {-0x1.4ec97326p-2, -0x1.34d7aaf04d104p-45},
    {-0x1.4900680401p-2, 0x1.8bccffe1a0f8cp-44},
    {-0x1.432ef2a04fp-2, 0x1.fb129931715adp-44},
    {-0x1.404308686ap-2, -0x1.f8ef43049f7d3p-44},
    {-0x1.3a64c55694p-2, -0x1.7a71cbcd735dp-44},
    {-0x1.347dd9a988p-2, 0x1.5594dd4c58092p-45},
    {-0x1.2e8e2bae12p-2, 0x1.67b1e99b72bd8p-45},
    {-0x1.2895a13de8p-2, -0x1.a8d7ad24c13fp-44},
    {-0x1.22941fbcf8p-2, 0x1.a6976f5eb0963p-44},
    {-0x1.1f8ff9e48ap-2, -0x1.7946c040cbe77p-45},
    {-0x1.1980d2dd42p-2, -0x1.b7b3a7a361c9ap-45},
    {-0x1.136870293bp-2, 0x1.d3e8499d67123p-44},
    {-0x1.1058bf9ae5p-2, 0x1.4ab9d817d52cdp-44},
    {-0x1.0a324e2739p-2, -0x1.c6bee7ef4030ep-47},
    {-0x1.0402594b4dp-2, -0x1.036b89ef42d7fp-48},
    {-0x1.fb9186d5e4p-3, 0x1.d572aab993c87p-47},
    {-0x1.f550a564b8p-3, 0x1.323e3a09202fep-45},
    {-0x1.e8c0252aa6p-3, 0x1.6805b80e8e6ffp-45},
    {-0x1.e27076e2bp-3, 0x1.a342c2af0003cp-44},
    {-0x1.d5c216b4fcp-3, 0x1.1ba91bbca681bp-45},
    {-0x1.c8ff7c79aap-3, 0x1.7794f689f8434p-45},
    {-0x1.c2968558c2p-3, 0x1.cfd73dee38a4p-45},
    {-0x1.b5b519e8fcp-3, 0x1.4b722ec011f31p-44},
    {-0x1.af3c94e80cp-3, 0x1.a4e633fcd9066p-52},
    {-0x1.a23bc1fe2cp-3, 0x1.539cd91dc9f0bp-44},
    {-0x1.9bb362e7ep-3, 0x1.1f2a8a1ce0ffcp-45},
    {-0x1.8e928de886p-3, -0x1.a8154b13d72d5p-44},
    {-0x1.87fa06520cp-3, -0x1.22120401202fcp-44},
    {-0x1.7ab890210ep-3, 0x1.bdb9072534a58p-45},
    {-0x1.740f8f5404p-3, 0x1.0b66c99018aa1p-44},
    {-0x1.6d60fe719ep-3, 0x1.bc6e557134767p-44},
    {-0x1.5ff3070a7ap-3, 0x1.8586f183bebf2p-44},
    {-0x1.59338d9982p-3, -0x1.0ba68b7555d4ap-48},
    {-0x1.4ba36f39a6p-3, 0x1.4354bb3f219e5p-44},
    {-0x1.44d2b6ccb8p-3, 0x1.70cc16135783cp-46},
    {-0x1.3dfc2b0eccp-3, -0x1.8a72a62b8c13fp-45},
    {-0x1.303d718e48p-3, 0x1.680b5ce3ecb05p-50},
    {-0x1.29552f82p-3, 0x1.5b967f4471dfcp-44},
    {-0x1.2266f190a6p-3, 0x1.4d20ab840e7f6p-45},
    {-0x1.1478584674p-3, -0x1.563451027c75p-46},
    {-0x1.0d77e7cd08p-3, -0x1.cb2cd2ee2f482p-44},
    {-0x1.0671512ca6p-3, 0x1.a47579cdc0a3dp-45},
    {-0x1.f0a30c0118p-4, 0x1.d599e83368e91p-44},
    {-0x1.e27076e2bp-4, 0x1.a342c2af0003cp-45},
    {-0x1.d4313d66ccp-4, 0x1.9454379135713p-45},
    {-0x1.c5e548f5bcp-4, -0x1.d0c57585fbe06p-46},
    {-0x1.a926d3a4acp-4, -0x1.563650bd22a9cp-44},
    {-0x1.9ab4246204p-4, 0x1.8a64826787061p-45},
    {-0x1.8c345d6318p-4, -0x1.b20f5acb42a66p-44},
    {-0x1.7da766d7bp-4, -0x1.2cc844480c89bp-44},
    {-0x1.60658a9374p-4, -0x1.0c3b1dee9c4f8p-44},
    {-0x1.51b073f06p-4, -0x1.83f69278e686ap-44},
    {-0x1.42edcbea64p-4, -0x1.bc0eeea7c9acdp-46},
    {-0x1.341d7961bcp-4, -0x1.1d0929983761p-44},
    {-0x1.253f62f0ap-4, -0x1.416f8fb69a701p-44},
    {-0x1.16536eea38p-4, 0x1.47c5e768fa309p-46},
    {-0x1.f0a30c0118p-5, 0x1.d599e83368e91p-45},
    {-0x1.d276b8adbp-5, -0x1.6a423c78a64bp-46},
    {-0x1.b42dd71198p-5, 0x1.c827ae5d6704cp-46},
    {-0x1.95c830ec9p-5, 0x1.c148297c5feb8p-45},
    {-0x1.77458f633p-5, 0x1.181dce586af09p-44},
    {-0x1.58a5bafc9p-5, 0x1.b2b739570ad39p-45},
    {-0x1.39e87b9fe8p-5, -0x1.eafd480ad9015p-44},
    {-0x1.1b0d98924p-5, 0x1.3401e9ae889bbp-44},
    {-0x1.f829b0e78p-6, -0x1.980267c7e09e4p-45},
    {-0x1.b9fc027bp-6, 0x1.b9a010ae6922ap-44},
    {-0x1.7b91b07d6p-6, 0x1.3b955b602ace4p-44},
    {-0x1.3cea44347p-6, 0x1.6a2c432d6a40bp-44},
    {-0x1.fc0a8b0fcp-7, -0x1.f1e7cf6d3a69cp-50},
    {-0x1.7dc475f82p-7, 0x1.eb1245b5da1f5p-44},
    {-0x1.fe02a6b1p-8, -0x1.9e23f0dda40e4p-46},
    {0, 0},
    {0, 0},
    {0x1.812121458p-8, 0x1.ad50382973f27p-46},
    {0x1.41929f968p-7, 0x1.977c755d01368p-46},
    {0x1.c317384c8p-7, -0x1.41f33fcefb9fep-44},
    {0x1.228fb1feap-6, 0x1.713e3284991fep-45},
    {0x1.63d617869p-6, 0x1.7abf389596542p-47},
    {0x1.a55f548c6p-6, -0x1.de0709f2d03c9p-45},
    {0x1.e72bf2814p-6, -0x1.8d75149774d47p-45},
    {0x1.0415d89e78p-5, -0x1.dddc7f461c516p-44},
    {0x1.252f32f8dp-5, 0x1.83e9ae021b67bp-45},
    {0x1.466aed42ep-5, -0x1.c167375bdfd28p-45},
    {0x1.67c94f2d48p-5, 0x1.dac20827cca0cp-44},
    {0x1.894aa149f8p-5, 0x1.9a19a8be97661p-44},
    {0x1.aaef2d0fbp-5, 0x1.0fc1a353bb42ep-45},
    {0x1.bbcebfc69p-5, -0x1.7bf868c317c2ap-46},
    {0x1.dda8adc68p-5, -0x1.1b1ac64d9e42fp-45},
    {0x1.ffa6911ab8p-5, 0x1.3008c98381a8fp-45},
    {0x1.10e45b3cbp-4, -0x1.7cf69284a3465p-44},
    {0x1.2207b5c784p-4, 0x1.49d8cfc10c7bfp-44},
    {0x1.2aa04a447p-4, 0x1.7a48ba8b1cb41p-44},
    {0x1.3bdf5a7d2p-4, -0x1.19bd0ad125895p-44},
    {0x1.4d3115d208p-4, -0x1.53a2582f4e1efp-48},
    {0x1.55e10050ep-4, 0x1.c1d740c53c72ep-47},
    {0x1.674f089364p-4, 0x1.a79994c9d3302p-44},
    {0x1.78d02263d8p-4, 0x1.69b5794b69fb7p-47},
    {0x1.8197e2f41p-4, -0x1.c0fe460d20041p-44},
    {0x1.9335e5d594p-4, 0x1.3115c3abd47dap-45},
    {0x1.a4e7640b1cp-4, -0x1.e42b6b94407c8p-47},
    {0x1.adc77ee5bp-4, -0x1.573b209c31904p-44},
    {0x1.bf968769fcp-4, 0x1.4218c8d824283p-45},
    {0x1.d179788218p-4, 0x1.36433b5efbeedp-44},
    {0x1.da72763844p-4, 0x1.a89401fa71733p-46},
    {0x1.ec739830ap-4, 0x1.11fcba80cdd1p-44},
    {0x1.f57bc7d9p-4, 0x1.76a6c9ea8b04ep-46},
    {0x1.03cdc0a51ep-3, 0x1.81a9cf169fc5cp-44},
    {0x1.08598b59e4p-3, -0x1.7e5dd7009902cp-45},
    {0x1.1178e8227ep-3, 0x1.1ef78ce2d07f2p-45},
    {0x1.160c8024b2p-3, 0x1.ec2d2a9009e3dp-45},
    {0x1.1f3b925f26p-3, -0x1.5f74e9b083633p-46},
    {0x1.23d712a49cp-3, 0x1.00d238fd3df5cp-46},
    {0x1.2d1610c868p-3, 0x1.39d6ccb81b4a1p-47},
    {0x1.31b994d3a4p-3, 0x1.f098ee3a5081p-44},
    {0x1.3b08b6758p-3, -0x1.aade8f29320fbp-44},
    {0x1.3fb45a5992p-3, 0x1.19713c0cae559p-44},
    {0x1.4913d8333cp-3, -0x1.53e43558124c4p-44},
    {0x1.4dc7b897bcp-3, 0x1.c79b60ae1ff0fp-47},
    {0x1.5737cc9018p-3, 0x1.9baa7a6b887f6p-44},
    {0x1.5bf406b544p-3, -0x1.27023eb68981cp-46},
    {0x1.6574ebe8c2p-3, -0x1.98c1d34f0f462p-44},
    {0x1.6a399dabbep-3, -0x1.8f934e66a15a6p-44},
    {0x1.6f0128b756p-3, 0x1.577390d31ef0fp-44},
    {0x1.7898d85444p-3, 0x1.8e67be3dbaf3fp-44},
    {0x1.7d6903caf6p-3, -0x1.4c06b17c301d7p-45},
    {0x1.871213750ep-3, 0x1.328eb42f9af75p-44},
    {0x1.8beafeb39p-3, -0x1.73d54aae92cd1p-47},
    {0x1.90c6db9fccp-3, -0x1.935f57718d7cap-46},
    {0x1.9a8778debap-3, 0x1.470fa3efec39p-44},
    {0x1.9f6c40708ap-3, -0x1.337d94bcd3f43p-44},
    {0x1.a454082e6ap-3, 0x1.60a77c81f7171p-44},
    {0x1.ae2ca6f672p-3, 0x1.7a8d5ae54f55p-44},
    {0x1.b31d8575bcp-3, 0x1.c794e562a63cbp-44},
    {0x1.b811730b82p-3, 0x1.e90683b9cd768p-46},
    {0x1.bd087383bep-3, -0x1.d4bc4595412b6p-45},
    {0x1.c6ffbc6fp-3, 0x1.ee138d3a69d43p-44},
    {0x1.cc000c9db4p-3, -0x1.d6d585d57aff9p-46},
    {0x1.d1037f2656p-3, -0x1.84a7e75b6f6e4p-47},
    {0x1.db13db0d48p-3, 0x1.2806a847527e6p-44},
    {0x1.e020cc6236p-3, -0x1.52b00adb91424p-45},
    {0x1.e530effe72p-3, -0x1.fdbdbb13f7c18p-44},
    {0x1.ea4449f04ap-3, 0x1.5e91663732a36p-44},
    {0x1.f474b134ep-3, -0x1.bae49f1df7b5ep-44},
    {0x1.f991c6cb3cp-3, -0x1.90d04cd7cc834p-44},
    {0x1.feb2233eap-3, 0x1.f3418de00938bp-45},
    {0x1.01eae5626cp-2, 0x1.a43dcfade85aep-44},
    {0x1.047e60cde8p-2, 0x1.dbdf10d397f3cp-45},
    {0x1.09aa572e6cp-2, 0x1.b50a1e1734342p-44},
    {0x1.0c42d67616p-2, 0x1.7188b163ceae9p-45},
    {0x1.0edd060b78p-2, 0x1.019b52d8435f5p-47},
    {0x1.1178e8227ep-2, 0x1.1ef78ce2d07f2p-44},
    {0x1.14167ef367p-2, 0x1.e0c07824daaf5p-44},
    {0x1.16b5ccbadp-2, -0x1.23299042d74bfp-44},
    {0x1.1bf99635a7p-2, -0x1.1ac89575c2125p-44},
    {0x1.1e9e16788ap-2, -0x1.82eaed3c8b65ep-44},
    {0x1.214456d0ecp-2, -0x1.caf0428b728a3p-44},
    {0x1.23ec5991ecp-2, -0x1.6dbe448a2e522p-44},
    {0x1.269621134ep-2, -0x1.1b61f10522625p-44},
    {0x1.2941afb187p-2, -0x1.210c2b730e28bp-44},
    {0x1.2bef07cdc9p-2, 0x1.a9cfa4a5004f4p-45},
    {0x1.314f1e1d36p-2, -0x1.8e27ad3213cb8p-45},
    {0x1.3401e12aedp-2, -0x1.17c73556e291dp-44},
    {0x1.36b6776be1p-2, 0x1.16ecdb0f177c8p-46},
    {0x1.396ce359bcp-2, -0x1.5839c5663663dp-47},
    {0x1.3c25277333p-2, 0x1.83b54b606bd5cp-46},
    {0x1.3edf463c17p-2, -0x1.f067c297f2c3fp-44},
    {0x1.419b423d5fp-2, -0x1.ce379226de3ecp-44},
    {0x1.44591e053ap-2, -0x1.6e95892923d88p-47},
    {0x1.4718dc271cp-2, 0x1.06c18fb4c14c5p-44},
    {0x1.49da7f3bccp-2, 0x1.07b334daf4b9ap-44},
    {0x1.4c9e09e173p-2, -0x1.e20891b0ad8a4p-45},
    {0x1.4f637ebbaap-2, -0x1.fc158cb3124b9p-44},
    {0x1.522ae0738ap-2, 0x1.ebe708164c759p-45},
    {0x1.54f431b7bep-2, 0x1.a8954c0910952p-46},
    {0x1.57bf753c8dp-2, 0x1.fadedee5d40efp-46},
    {0x1.5a8cadbbeep-2, -0x1.7c79b0af7ecf8p-48},
    {0x1.5d5bddf596p-2, -0x1.a0b2a08a465dcp-47},
    {0x1.602d08af09p-2, 0x1.ebe9176df3f65p-46},
    {0x1.630030b3abp-2, -0x1.db623e731aep-45},
};

/* For 0 <= i < 64, T1[i] = (h,l) such that h+l is the best double-double
   approximation of 2^(i/64). The approximation error is bounded as follows:
   |h + l - 2^(i/64)| < 2^-107. */
static const double T1[][2] = {
    {              0x1p+0,                 0x0p+0},
    {0x1.02c9a3e778061p+0, -0x1.19083535b085dp-56},
    {0x1.059b0d3158574p+0,  0x1.d73e2a475b465p-55},
    {0x1.0874518759bc8p+0,  0x1.186be4bb284ffp-57},
    {0x1.0b5586cf9890fp+0,  0x1.8a62e4adc610bp-54},
    {0x1.0e3ec32d3d1a2p+0,  0x1.03a1727c57b53p-59},
    {0x1.11301d0125b51p+0, -0x1.6c51039449b3ap-54},
    { 0x1.1429aaea92dep+0, -0x1.32fbf9af1369ep-54},
    {0x1.172b83c7d517bp+0, -0x1.19041b9d78a76p-55},
    {0x1.1a35beb6fcb75p+0,  0x1.e5b4c7b4968e4p-55},
    {0x1.1d4873168b9aap+0,  0x1.e016e00a2643cp-54},
    {0x1.2063b88628cd6p+0,  0x1.dc775814a8495p-55},
    {0x1.2387a6e756238p+0,  0x1.9b07eb6c70573p-54},
    {0x1.26b4565e27cddp+0,  0x1.2bd339940e9d9p-55},
    {0x1.29e9df51fdee1p+0,  0x1.612e8afad1255p-55},
    {0x1.2d285a6e4030bp+0,  0x1.0024754db41d5p-54},
    {0x1.306fe0a31b715p+0,  0x1.6f46ad23182e4p-55},
    {0x1.33c08b26416ffp+0,  0x1.32721843659a6p-54},
    {0x1.371a7373aa9cbp+0, -0x1.63aeabf42eae2p-54},
    {0x1.3a7db34e59ff7p+0, -0x1.5e436d661f5e3p-56},
    {0x1.3dea64c123422p+0,  0x1.ada0911f09ebcp-55},
    {0x1.4160a21f72e2ap+0, -0x1.ef3691c309278p-58},
    {0x1.44e086061892dp+0,   0x1.89b7a04ef80dp-59},
    { 0x1.486a2b5c13cdp+0,   0x1.3c1a3b69062fp-56},
    {0x1.4bfdad5362a27p+0,  0x1.d4397afec42e2p-56},
    {0x1.4f9b2769d2ca7p+0, -0x1.4b309d25957e3p-54},
    {0x1.5342b569d4f82p+0, -0x1.07abe1db13cadp-55},
    {0x1.56f4736b527dap+0,  0x1.9bb2c011d93adp-54},
    {0x1.5ab07dd485429p+0,  0x1.6324c054647adp-54},
    {0x1.5e76f15ad2148p+0,  0x1.ba6f93080e65ep-54},
    {0x1.6247eb03a5585p+0, -0x1.383c17e40b497p-54},
    {0x1.6623882552225p+0, -0x1.bb60987591c34p-54},
    {0x1.6a09e667f3bcdp+0, -0x1.bdd3413b26456p-54},
    {0x1.6dfb23c651a2fp+0, -0x1.bbe3a683c88abp-57},
    {0x1.71f75e8ec5f74p+0, -0x1.16e4786887a99p-55},
    {0x1.75feb564267c9p+0, -0x1.0245957316dd3p-54},
    {0x1.7a11473eb0187p+0, -0x1.41577ee04992fp-55},
    {0x1.7e2f336cf4e62p+0,  0x1.05d02ba15797ep-56},
    {0x1.82589994cce13p+0, -0x1.d4c1dd41532d8p-54},
    {0x1.868d99b4492edp+0, -0x1.fc6f89bd4f6bap-54},
    {0x1.8ace5422aa0dbp+0,  0x1.6e9f156864b27p-54},
    {0x1.8f1ae99157736p+0,  0x1.5cc13a2e3976cp-55},
    {0x1.93737b0cdc5e5p+0, -0x1.75fc781b57ebcp-57},
    { 0x1.97d829fde4e5p+0, -0x1.d185b7c1b85d1p-54},
    { 0x1.9c49182a3f09p+0,  0x1.c7c46b071f2bep-56},
    {0x1.a0c667b5de565p+0, -0x1.359495d1cd533p-54},
    {0x1.a5503b23e255dp+0, -0x1.d2f6edb8d41e1p-54},
    {0x1.a9e6b5579fdbfp+0,  0x1.0fac90ef7fd31p-54},
    {0x1.ae89f995ad3adp+0,  0x1.7a1cd345dcc81p-54},
    {0x1.b33a2b84f15fbp+0, -0x1.2805e3084d708p-57},
    {0x1.b7f76f2fb5e47p+0, -0x1.5584f7e54ac3bp-56},
    {0x1.bcc1e904bc1d2p+0,  0x1.23dd07a2d9e84p-55},
    {0x1.c199bdd85529cp+0,  0x1.11065895048ddp-55},
    {0x1.c67f12e57d14bp+0,  0x1.2884dff483cadp-54},
    {0x1.cb720dcef9069p+0,  0x1.503cbd1e949dbp-56},
    {0x1.d072d4a07897cp+0, -0x1.cbc3743797a9cp-54},
    {0x1.d5818dcfba487p+0,  0x1.2ed02d75b3707p-55},
    {0x1.da9e603db3285p+0,  0x1.c2300696db532p-54},
    {0x1.dfc97337b9b5fp+0, -0x1.1a5cd4f184b5cp-54},
    {0x1.e502ee78b3ff6p+0,  0x1.39e8980a9cc8fp-55},
    {0x1.ea4afa2a490dap+0, -0x1.e9c23179c2893p-54},
    {0x1.efa1bee615a27p+0,   0x1.dc7f486a4b6bp-54},
    { 0x1.f50765b6e454p+0,  0x1.9d3e12dd8a18bp-54},
    {0x1.fa7c1819e90d8p+0,  0x1.74853f3a5931ep-55},
};

/* For 0 <= i < 64, T2[i] = (h,l) such that h+l is the best double-double
   approximation of 2^(i/2^12). The approximation error is bounded as follows:
   |h + l - 2^(i/2^12)| < 2^-107. */
static const double T2[][2] = {
    {              0x1p+0,                 0x0p+0},
    {0x1.000b175effdc7p+0,  0x1.ae8e38c59c72ap-54},
    {0x1.00162f3904052p+0, -0x1.7b5d0d58ea8f4p-58},
    {0x1.0021478e11ce6p+0,  0x1.4115cb6b16a8ep-54},
    {0x1.002c605e2e8cfp+0, -0x1.d7c96f201bb2fp-55},
    {0x1.003779a95f959p+0,  0x1.84711d4c35e9fp-54},
    {0x1.0042936faa3d8p+0, -0x1.0484245243777p-55},
    { 0x1.004dadb113dap+0, -0x1.4b237da2025f9p-54},
    {0x1.0058c86da1c0ap+0, -0x1.5e00e62d6b30dp-56},
    {0x1.0063e3a559473p+0,  0x1.a1d6cedbb9481p-54},
    {0x1.006eff583fc3dp+0, -0x1.4acf197a00142p-54},
    {0x1.007a1b865a8cap+0, -0x1.eaf2ea42391a5p-57},
    {0x1.0085382faef83p+0,  0x1.da93f90835f75p-56},
    {0x1.00905554425d4p+0, -0x1.6a79084ab093cp-55},
    {0x1.009b72f41a12bp+0,  0x1.86364f8fbe8f8p-54},
    {0x1.00a6910f3b6fdp+0, -0x1.82e8e14e3110ep-55},
    {0x1.00b1afa5abcbfp+0, -0x1.4f6b2a7609f71p-55},
    {0x1.00bcceb7707ecp+0, -0x1.e1a258ea8f71bp-56},
    {0x1.00c7ee448ee02p+0,  0x1.4362ca5bc26f1p-56},
    {0x1.00d30e4d0c483p+0,  0x1.095a56c919d02p-54},
    {0x1.00de2ed0ee0f5p+0, -0x1.406ac4e81a645p-57},
    { 0x1.00e94fd0398ep+0,  0x1.b5a6902767e09p-54},
    {0x1.00f4714af41d3p+0, -0x1.91b2060859321p-54},
    {0x1.00ff93412315cp+0,  0x1.427068ab22306p-55},
    {0x1.010ab5b2cbd11p+0,  0x1.c1d0660524e08p-54},
    {0x1.0115d89ff3a8bp+0, -0x1.e7bdfb3204be8p-54},
    {0x1.0120fc089ff63p+0,  0x1.843aa8b9cbbc6p-55},
    {0x1.012c1fecd613bp+0, -0x1.34104ee7edae9p-56},
    {0x1.0137444c9b5b5p+0, -0x1.2b6aeb6176892p-56},
    {0x1.01426927f5278p+0,  0x1.a8cd33b8a1bb3p-56},
    {0x1.014d8e7ee8d2fp+0,  0x1.2edc08e5da99ap-56},
    {0x1.0158b4517bb88p+0,  0x1.57ba2dc7e0c73p-55},
    {0x1.0163da9fb3335p+0,  0x1.b61299ab8cdb7p-54},
    {0x1.016f0169949edp+0, -0x1.90565902c5f44p-54},
    {0x1.017a28af25567p+0,  0x1.70fc41c5c2d53p-55},
    {0x1.018550706ab62p+0,  0x1.4b9a6e145d76cp-54},
    {0x1.019078ad6a19fp+0, -0x1.008eff5142bf9p-56},
    {0x1.019ba16628de2p+0, -0x1.77669f033c7dep-54},
    {0x1.01a6ca9aac5f3p+0, -0x1.09bb78eeead0ap-54},
    {0x1.01b1f44af9f9ep+0,  0x1.371231477ece5p-54},
    {0x1.01bd1e77170b4p+0,  0x1.5e7626621eb5bp-56},
    {0x1.01c8491f08f08p+0, -0x1.bc72b100828a5p-54},
    { 0x1.01d37442d507p+0, -0x1.ce39cbbab8bbep-57},
    {0x1.01de9fe280ac8p+0,  0x1.16996709da2e2p-55},
    {0x1.01e9cbfe113efp+0, -0x1.c11f5239bf535p-55},
    {0x1.01f4f8958c1c6p+0,  0x1.e1d4eb5edc6b3p-55},
    {0x1.020025a8f6a35p+0, -0x1.afb99946ee3fp-54},
    {0x1.020b533856324p+0, -0x1.8f06d8a148a32p-54},
    {0x1.02168143b0281p+0, -0x1.2bf310fc54eb6p-55},
    {0x1.0221afcb09e3ep+0, -0x1.c95a035eb4175p-54},
    {0x1.022cdece68c4fp+0, -0x1.491793e46834dp-54},
    {0x1.02380e4dd22adp+0, -0x1.3e8d0d9c49091p-56},
    {0x1.02433e494b755p+0, -0x1.314aa16278aa3p-54},
    {0x1.024e6ec0da046p+0,  0x1.48daf888e9651p-55},
    {0x1.02599fb483385p+0,  0x1.56dc8046821f4p-55},
    {0x1.0264d1244c719p+0,  0x1.45b42356b9d47p-54},
    {0x1.027003103b10ep+0, -0x1.082ef51b61d7ep-56},
    {0x1.027b357854772p+0,  0x1.2106ed0920a34p-56},
    {0x1.0286685c9e059p+0, -0x1.fd4cf26ea5d0fp-54},
    {0x1.02919bbd1d1d8p+0, -0x1.09f8775e78084p-54},
    {0x1.029ccf99d720ap+0,  0x1.64cbba902ca27p-58},
    {0x1.02a803f2d170dp+0,  0x1.4383ef231d207p-54},
    {0x1.02b338c811703p+0,  0x1.4a47a505b3a47p-54},
    {0x1.02be6e199c811p+0,  0x1.e47120223467fp-54},
};

/* The following is a degree-8 polynomial generated by Sollya for
   log(1+x)-x+x^2/2 over [-0.0040283203125,0.0040283203125]
   with absolute error < 2^-81.63
   and relative error < 2^-72.423 (see sollya/P_1.sollya).
   The relative error is for x - x^2/2 + P(x) with respect to log(1+x). */
static const double P_1[] = {0x1.5555555555558p-2,  /* degree 3 */
                             -0x1.0000000000003p-2, /* degree 4 */
                             0x1.999999981f535p-3,  /* degree 5 */
                             -0x1.55555553d1eb4p-3, /* degree 6 */
                             0x1.2494526fd4a06p-3,  /* degree 7 */
                             -0x1.0001f0c80e8cep-3, /* degree 8 */
};

/* The following is a degree-4 polynomial generated by Sollya for exp(x)
   over [-2^-12.905,2^-12.905]
   with absolute error < 2^-74.34 (see sollya/Q_1.sollya). */
static const double Q_1[] = {0x1p0,                 /* degree 0 */
                             0x1p0,                 /* degree 1 */
                             0x1p-1,                /* degree 2 */
                             0x1.5555555997996p-3,  /* degree 3 */
                             0x1.5555555849d8dp-5   /* degree 4 */
};

#endif
