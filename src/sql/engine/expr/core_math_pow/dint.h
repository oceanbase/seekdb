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

Copyright (c) 2022, 2023 CERN and Inria
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

/*
  This file contains type definition and functions to manipulate the dint64_t
  data type used in the second iteration of Ziv's method. It is composed of two
  uint64_t values for the mantissa and the exponent is represented by a signed
  int64_t value.
*/

#ifndef DINT_H
#define DINT_H

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

/*
  Type and structure definitions
*/

#ifndef UINT128_T
#define UINT128_T

#if (defined(__clang__) && __clang_major__ >= 14) || (defined(__GNUC__) && __GNUC__ >= 14 && __BITINT_MAXWIDTH__ && __BITINT_MAXWIDTH__ >= 128)
typedef unsigned _BitInt(128) u128;
#else
typedef unsigned __int128 u128;
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef union {
  u128 r;
  struct {
    uint64_t l;
    uint64_t h;
  };
} uint128_t;
#else
typedef union {
  u128 r;
  struct {
    uint64_t h;
    uint64_t l;
  };
} uint128_t;
#endif

// Add two 128-bit integers and return 1 if a carry occurred
static inline uint64_t addu_128 (uint128_t a, uint128_t b, uint128_t *r) {
  r->r = a.r + b.r;
  // Return the overflow
  return r->r < a.r;
}

// Subtract two 128 bit integers and return 1 if a borrow occurred
static inline int subu_128 (uint128_t a, uint128_t b, uint128_t *r) {
  r->r = a.r - b.r;
  // Return the borrow
  return r->r > a.r;
}

static inline int cmp(int64_t a, int64_t b) { return (a > b) - (a < b); }

static inline int cmpu(uint64_t a, uint64_t b) { return (a > b) - (a < b); }

static inline int cmpu128 (u128 a, u128 b) { return (a > b) - (a < b); }

#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef union {
  struct {
    u128 r;
    int64_t _ex;
    uint64_t _sgn;
  };
  struct {
    uint64_t lo;
    uint64_t hi;
    int64_t ex;
    uint64_t sgn;
  };
} dint64_t;
#else
typedef union {
  struct {
    u128 r;
    int64_t _ex;
    uint64_t _sgn;
  };
  struct {
    uint64_t hi;
    uint64_t lo;
    int64_t ex;
    uint64_t sgn;
  };
} dint64_t;
#endif

/*
  Constants
*/

/* ONE is a dint64_t representation of 1 */
static const dint64_t ONE = {
    .hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0};

/* M_ONE is a dint64_t representation of -1 */
static const dint64_t M_ONE = {
    .hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x1};

/* LOG2 is a dint64_t approximation of log(2) to nearest, with absolute error
   bounded by 2^-129.97 */
static const dint64_t LOG2 = {
    .hi = 0xb17217f7d1cf79ab, .lo = 0xc9e3b39803f2f6af, .ex = -1, .sgn = 0x0};

/* LOG2_INV approximates 2^12/log(2), with absolute error < 2^-52.96 */
static const dint64_t LOG2_INV = {
    .hi = 0xb8aa3b295c17f0bc, .lo = 0x0, .ex = 12, .sgn = 0x0};

/* ZERO is a dint64_t representation of 0, which ensures that
   dint_tod(ZERO) = 0 */
static const dint64_t ZERO = {.hi = 0x0, .lo = 0x0, .ex = -1076, .sgn = 0x0};

/*
  Base functions
*/

// Copy a dint64_t value
static inline void cp_dint(dint64_t *r, const dint64_t *a) {
  r->ex = a->ex;
  r->r = a->r;
  r->sgn = a->sgn;
}

// Return non-zero if a = 0
static inline int
dint_zero_p (const dint64_t *a)
{
  return a->hi == 0;
}

// Compare the absolute values of a and b
// Return -1 if |a| < |b|
// Return  0 if |a| = |b|
// Return +1 if |a| > |b|
static inline signed char
cmp_dint_abs (const dint64_t *a, const dint64_t *b) {
  if (dint_zero_p (a))
    return dint_zero_p (b) ? 0 : -1;
  if (dint_zero_p (b))
    return +1;
  char c1 = cmp (a->ex, b->ex);
  return c1 ? c1 : cmpu128 (a->r, b->r);
}

static inline signed char cmp_dint_11(const dint64_t *a, const dint64_t *b) {
  char c1 = cmp (a->ex, b->ex);
  return c1 ? c1 : cmpu (a->hi, b->hi);
}

// Prints a dint64_t value for debugging purposes
static inline void print_dint(const dint64_t *a) {
  printf("{.hi=0x%"PRIx64", .lo=0x%"PRIx64", .ex=%"PRId64", .sgn=0x%"PRIx64"}\n", a->hi, a->lo, a->ex,
         a->sgn);
}

// Add two dint64_t values, with error bounded by 2 ulps (ulp_128)
// (more precisely 1 ulp when a and b have same sign, 2 ulps otherwise)
// Moreover, when Sterbenz theorem applies, i.e., |b| <= |a| <= 2|b|
// and a,b are of different signs, there is no error, i.e., r = a-b.
static inline void
add_dint (dint64_t *r, const dint64_t *a, const dint64_t *b) {
  if (!(a->hi | a->lo)) {
    cp_dint (r, b);
    return;
  }

  switch (cmp_dint_abs (a, b)) {
  case 0:
    if (a->sgn ^ b->sgn) {
      cp_dint (r, &ZERO);
      return;
    }

    cp_dint (r, a);
    r->ex++;
    return;

  case -1: // |A| < |B|
    {
      // swap operands
      const dint64_t *tmp = a; a = b; b = tmp;
      break; // fall through the case |A| > |B|
    }
  }

  // From now on, |A| > |B| thus a->ex >= b->ex

  u128 A = a->r, B = b->r;
  uint64_t k = a->ex - b->ex;

  if (k > 0) {
    /* Warning: the right shift x >> k is only defined for 0 <= k < n
       where n is the bit-width of x. See for example
       https://developer.arm.com/documentation/den0024/a/The-A64-instruction-set/Data-processing-instructions/Shift-operations
       where it is said that k is interpreted modulo n. */
    B = (k < 128) ? B >> k : 0;
  }

  u128 C;
  unsigned char sgn = a->sgn;

  r->ex = a->ex; /* tentative exponent for the result */

  if (a->sgn ^ b->sgn) {
    /* a and b have different signs C = A + (-B)
       Sterbenz case |a|/2 <= |b| <= |a| can occur only when:
       * k=0: then B is not truncated, and C is exact below
       * k=1 and ex>0 below: then we ensure C is exact
     */
    C = A - B;
    uint64_t ch = C >> 64;
    /* We can't have C=0 here since we excluded the case |A| = |B|,
       thus __builtin_clzll(C) is well-defined below. */
    uint64_t ex = ch ? __builtin_clzll(ch) : 64 + __builtin_clzll(C);
    /* The error from the truncated part of B (1 ulp) is multiplied by 2^ex,
       thus by 2 ulps when ex <= 1. */
    if (ex > 0)
    {
      if (k == 1) /* Sterbenz case */
        C = (A << ex) - (b->r << (ex - 1));
      else
        C = (A << ex) - (B << ex);
      /* If C0 is the previous value of C, we have:
         (C0-1)*2^ex < A*2^ex-B*2^ex <= C0*2^ex
         since some neglected bits from B might appear which contribute
         a value less than ulp(C0)=1.
         As a consequence since 2^(127-ex) <= C0 < 2^(128-ex), because C0 had
         ex leading zero bits, we have 2^127-2^ex <= A*2^ex-B*2^ex < 2^128.
         Thus the value of C, which is truncated to 128 bits, is the right
         one (as if no truncation); moreover in some rare cases we need to
         shift by 1 bit to the left. */
      r->ex -= ex;
      ex = __builtin_clzll (C >> 64);
      /* Fall through with the code for ex = 0. */
    }
    C = C << ex;
    r->ex -= ex;
    /* The neglected part of B is bounded by 2 ulp(C) when ex=0, 1 ulp
       when ex > 0 but ex=0 at the end, and by 2*ulp(C) when ex > 0 and there
       is an extra shift at the end (in that case necessarily ex=1). */
  } else {
    C = A + B;
    if (C < A)
    {
      C = ((u128) 1 << 127) | (C >> 1);
      r->ex ++;
    }
  }

  /* In the addition case, we loose the truncated part of B, which
     contributes to at most 1 ulp. If there is an exponent shift, we
     might also loose the least significant bit of C, which counts as
     1/2 ulp, but the truncated part of B is now less than 1/2 ulp too,
     thus in all cases the error is less than 1 ulp(r). */

  r->sgn = sgn;
  r->r = C;
}

// same as add_dint, but assumes the lower limbs and a and b are zero
// error is bounded by 2 ulps (ulp_64)
static inline void
add_dint_11 (dint64_t *r, const dint64_t *a, const dint64_t *b) {
  if (!a->hi) {
    cp_dint (r, b);
    return;
  }

  if (!b->hi) {
    cp_dint (r, a);
    return;
  }

  switch (cmp_dint_11 (a, b)) {
  case 0:
    if (a->sgn ^ b->sgn) {
      cp_dint (r, &ZERO);
      return;
    }

    cp_dint (r, a);
    r->ex++;
    return;

  case -1: // |A| < |B|
    {
      // swap operands
      const dint64_t *tmp = a; a = b; b = tmp;
      break; // fall through the case |A| > |B|
    }
  }

  // From now on, |A| > |B| thus a->ex >= b->ex

  uint64_t A = a->hi, B = b->hi;

  if (a->ex > b->ex) {
    /* Warning: the right shift x >> k is only defined for 0 <= k < n
       where n is the bit-width of x. See for example
       https://developer.arm.com/documentation/den0024/a/The-A64-instruction-set/Data-processing-instructions/Shift-operations
       where it is said that k is interpreted modulo n. */
    uint64_t k = a->ex - b->ex;
    B = (k < 64) ? B >> k : 0;
  }

  u128 C;
  unsigned char sgn = a->sgn;

  r->ex = a->ex; /* tentative exponent for the result */

  if (a->sgn ^ b->sgn) {
    // a and b have different signs C = A + (-B)
    C = A - B;
    /* we can't have C=0 here since we excluded the case |A| = |B|,
       thus __builtin_clzll(C) is well-defined below */
    uint64_t ex = __builtin_clzll (C);
    /* The error from the truncated part of B (1 ulp) is multiplied by 2^ex.
       Thus for ex <= 2, we get an error bounded by 4 ulps in the final result.
       For ex >= 3, we pre-shift the operands. */
    if (ex > 0)
    {
      C = (A << ex) - (B << ex);
      /* If C0 is the previous value of C, we have:
         (C0-1)*2^ex < A*2^ex-B*2^ex <= C0*2^ex
         since here some neglected bits from B might appear which contribute
         a value less than ulp(C0)=1.
         As a consequence since 2^(63-ex) <= C0 < 2^(64-ex), because C0 had
         ex leading zero bits, we have 2^63-2^ex <= A*2^ex-B*2^ex < 2^64.
         Thus the value of C, which is truncated to 64 bits, is the right
         one (as if no truncation); moreover in some rare cases we need to
         shift by 1 bit to the left. */
      r->ex -= ex;
      ex = __builtin_clzll (C);
      /* Fall through with the code for ex = 0. */
    }
    C = C << ex;
    r->ex -= ex;
    /* The neglected part of B is bounded by ulp(C) when ex=0, or when
       ex > 0 but the ex=0 at the end, and by 2*ulp(C) when ex>0 and there
       is an extra shift at the end (in that case necessarily ex=1). */
  } else {
    C = A + B;
    if (C < A)
    {
      C = ((uint64_t) 1 << 63) | (C >> 1);
      r->ex ++;
    }
  }

  /* In the addition case, we loose the truncated part of B, which
     contributes to at most 1 ulp. If there is an exponent shift, we
     might also loose the least significant bit of C, which counts as
     1/2 ulp, but the truncated part of B is now less than 1/2 ulp too,
     thus in all cases the error is less than 1 ulp(r). */

  r->sgn = sgn;
  r->hi = C;
}

// Multiply two dint64_t numbers, with error bounded by 6 ulps
// on the 128-bit floating-point numbers.
// Overlap between r and a is allowed
static inline void
mul_dint (dint64_t *r, const dint64_t *a, const dint64_t *b) {
  u128 bh = b->hi, bl = b->lo;

  /* compute the two middle terms */
  u128 m1 = (u128)(a->hi) * bl;
  u128 m2 = (u128)(a->lo) * bh;

  /* put the 128-bit product of the high terms in r */
  r->r = (u128)(a->hi) * bh;

  /* there can be no overflow in the following addition since r <= (B-1)^2
     with B=2^64, (m1>>64) <= B-1 and (m2>>64) <= B-1, thus the sum is
     bounded by (B-1)^2+2*(B-1) = B^2-1 */
  r->r += (m1 >> 64) + (m2 >> 64);

  // Ensure that r->hi starts with a 1
  uint64_t ex = r->hi >> 63;
  r->r = r->r << (1 - ex);

  // Exponent and sign
  r->ex = a->ex + b->ex + ex;
  r->sgn = a->sgn ^ b->sgn;

  /* The ignored part can be as large as 3 ulps before the shift (one
     for the low part of a->hi * bl, one for the low part of a->lo * bh,
     and one for the neglected a->lo * bl term). After the shift this can
     be as large as 6 ulps. */
}

// Multiply two dint64_t numbers, assuming the low part of b is zero
// with error bounded by 2 ulps
static inline void
mul_dint_21 (dint64_t *r, const dint64_t *a, const dint64_t *b) {
  u128 bh = b->hi;
  u128 hi = (u128) (a->hi) * bh;
  u128 lo = (u128) (a->lo) * bh;

  /* put the 128-bit product of the high terms in r */
  r->r = hi;

  /* add the middle term */
  r->r += lo >> 64;

  // Ensure that r->hi starts with a 1
  uint64_t ex = r->hi >> 63;
  r->r = r->r << (1 - ex);

  // Exponent and sign
  r->ex = a->ex + b->ex + ex;
  r->sgn = a->sgn ^ b->sgn;

  /* The ignored part can be as large as 1 ulp before the shift (truncated
     part of lo). After the shift this can be as large as 2 ulps. */
}

// Multiply an integer with a dint64_t variable
static inline void mul_dint_2(dint64_t *r, int64_t b, const dint64_t *a) {
  uint128_t t;

  if (!b) {
    cp_dint(r, &ZERO);
    return;
  }

  uint64_t c = b < 0 ? -b : b;
  r->sgn = b < 0 ? !a->sgn : a->sgn;

  t.r = (u128)(a->hi) * (u128)c;

  int m = t.h ? __builtin_clzll(t.h) : 64;
  t.r = (t.r << m);

  // Will pose issues if b is too large but for now we assume it never happens
  // TODO: FIXME
  uint128_t l = {.r = (u128)(a->lo) * (u128)c};
  l.r = (l.r << (m - 1)) >> 63;

  if (addu_128(l, t, &t)) {
    t.r += t.r & 0x1;
    t.r = ((u128)1 << 127) | (t.r >> 1);
    m--;
  }

  r->hi = t.h;
  r->lo = t.l;
  r->ex = a->ex + 64 - m;
}

/* Same as mul_dint_21, but assumes the low part of a and b is zero.
   This operation is exact. */
static inline void
mul_dint_11 (dint64_t *r, const dint64_t *a, const dint64_t *b) {
  /* put the 128-bit product of the high terms in r */
  r->r = (u128)(a->hi) * (u128)(b->hi);

  // Ensure that r->hi starts with a 1
  uint64_t ex = r->hi >> 63;
  r->r = r->r << (1 - ex);

  // Exponent and sign
  r->ex = a->ex + b->ex + ex;
  r->sgn = a->sgn ^ b->sgn;
}

// Multiply an integer with a dint64_t variable, with error < 1 ulp
// r and b should not overlap
static inline void
mul_dint_int64 (dint64_t *r, const dint64_t *a, int64_t b) {
  if (!b) {
    cp_dint (r, &ZERO);
    return;
  }

  uint64_t c = b < 0 ? -b : b;
  r->sgn = b < 0 ? !a->sgn : a->sgn;
  r->ex = a->ex + 64;

  r->r = (u128) (a->hi) * (u128) c;

  // Warning: if c=1, we might have r->hi=0
  int m = r->hi ? __builtin_clzll (r->hi) : 64;
  r->r = r->r << m;
  r->ex -= m;

  // Will pose issues if b is too large but for now we assume it never happens
  // TODO: FIXME
  u128 l = (u128) a->lo * (u128) c;
  /* We have to shift l by 64 bits to the right to align with hi*c,
     and by m bits to the left to align with t.r << m. Since hi*c < 2^(128-m)
     and hi >= 2^63, we know that c < 2^(65-m) thus
     l*2^(m-1) < 2^64*2^(65-m)*2^(m-1) = 2^128, and l << (m - 1) will
     not overflow. */
  l = (l << (m - 1)) >> 63;

  r->r += l;
  if (r->r < l) {
    r->r = ((u128) 1 << 127) | (r->r >> 1);
    r->ex ++;
  }

  /* The ignored part of a->lo*c is at most 1 ulp(r), even in the overflow
     case "r->r < l", since before the right shift, the error was at most
     1 ulp, thus 1/2 ulp after the shift, and the ignored least significant
     bit of r->r which is discarded counts also as 1/2 ulp. */
}

// Convert a non-zero double to the corresponding dint64_t value
static inline void dint_fromd (dint64_t *a, double b) {
  fast_extract (&a->ex, &a->hi, b);

  /* |b| = 2^(ex-52)*hi */

  uint32_t t = __builtin_clzll (a->hi);

  a->sgn = b < 0.0;
  a->hi = a->hi << t;
  a->ex = a->ex - (t > 11 ? t - 12 : 0);
  /* b = 2^ex*hi/2^63 where 1 <= hi/2^63 < 2 */
  a->lo = 0;
}

/* put in r an approximation of 1/a, assuming a is not zero */
static inline void inv_dint (dint64_t *r, double a)
{
  dint64_t q, A;
  dint_fromd (r, 1.0 / a); /* accurate to about 53 bits */
  /* we use Newton's iteration: r -> r + r*(1-a*r) */
  dint_fromd (&A, -a);
  mul_dint (&q, &A, r);    /* -a*r */
  add_dint (&q, &ONE, &q); /* 1-a*r */
  mul_dint (&q, r, &q);    /* r*(1-a*r) */
  add_dint (r, r, &q);
}

/* put in r an approximation of b/a, assuming a is not zero */
static inline void div_dint (dint64_t *r, double b, double a)
{
  dint64_t B;
  inv_dint (r, a);
  dint_fromd (&B, b);
  mul_dint (r, r, &B);
}

/*
  Approximation tables
*/

/* For 90 <= i <= 181, _INVERSE_2_1[i-90] is an approximation of the inverse
   of x for i/2^7 <= x < (i+1)/2^7, where an entry (hi,lo,ex,sgn) represents
   (-1)^sgn*(hi+lo/2^64)*2^(ex-63)
   (the binary point is after the most significant bit of hi).
   For i=127 and i=128, we force _INVERSE_2_1[i-90]=1.
   If was generated with output_inverse_2_1(7,9,90,181) from the
   accompanying file dint.sage.
   There is no rounding error here, the only approximation error is in
   _LOG_INV_2_1[]. */
static const dint64_t _INVERSE_2_1[] = {
    {.hi = 0xb500000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=90 */
    {.hi = 0xb300000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=91 */     
    {.hi = 0xb100000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=92 */     
    {.hi = 0xaf00000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=93 */     
    {.hi = 0xad80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=94 */     
    {.hi = 0xab80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=95 */     
    {.hi = 0xaa00000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=96 */     
    {.hi = 0xa800000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=97 */     
    {.hi = 0xa680000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=98 */     
    {.hi = 0xa480000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=99 */     
    {.hi = 0xa300000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=100 */    
    {.hi = 0xa180000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=101 */    
    {.hi = 0xa000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=102 */    
    {.hi = 0x9e80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=103 */    
    {.hi = 0x9d00000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=104 */    
    {.hi = 0x9b80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=105 */    
    {.hi = 0x9a00000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=106 */    
    {.hi = 0x9880000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=107 */    
    {.hi = 0x9700000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=108 */    
    {.hi = 0x9580000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=109 */    
    {.hi = 0x9480000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=110 */    
    {.hi = 0x9300000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=111 */    
    {.hi = 0x9180000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=112 */    
    {.hi = 0x9080000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=113 */    
    {.hi = 0x8f00000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=114 */    
    {.hi = 0x8e00000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=115 */
    {.hi = 0x8c80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=116 */
    {.hi = 0x8b80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=117 */
    {.hi = 0x8a80000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=118 */
    {.hi = 0x8900000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=119 */
    {.hi = 0x8800000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=120 */
    {.hi = 0x8700000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=121 */
    {.hi = 0x8580000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=122 */
    {.hi = 0x8480000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=123 */
    {.hi = 0x8380000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=124 */
    {.hi = 0x8280000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=125 */
    {.hi = 0x8180000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=126 */
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=127 */
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* i=128 */
    {.hi = 0xfd00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=129 */
    {.hi = 0xfb00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=130 */
    {.hi = 0xf900000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=131 */
    {.hi = 0xf780000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=132 */
    {.hi = 0xf580000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=133 */
    {.hi = 0xf380000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=134 */
    {.hi = 0xf200000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=135 */
    {.hi = 0xf000000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=136 */
    {.hi = 0xee80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=137 */
    {.hi = 0xec80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=138 */
    {.hi = 0xeb00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=139 */
    {.hi = 0xe900000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=140 */
    {.hi = 0xe780000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=141 */
    {.hi = 0xe600000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=142 */
    {.hi = 0xe480000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=143 */
    {.hi = 0xe300000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=144 */
    {.hi = 0xe100000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=145 */
    {.hi = 0xdf80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=146 */
    {.hi = 0xde00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=147 */
    {.hi = 0xdc80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=148 */
    {.hi = 0xdb00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=149 */
    {.hi = 0xd980000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=150 */
    {.hi = 0xd880000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=151 */
    {.hi = 0xd700000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=152 */
    {.hi = 0xd580000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=153 */
    {.hi = 0xd400000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=154 */
    {.hi = 0xd280000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=155 */
    {.hi = 0xd180000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=156 */
    {.hi = 0xd000000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=157 */
    {.hi = 0xce80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=158 */
    {.hi = 0xcd80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=159 */
    {.hi = 0xcc00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=160 */
    {.hi = 0xcb00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=161 */
    {.hi = 0xc980000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=162 */
    {.hi = 0xc880000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=163 */
    {.hi = 0xc700000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=164 */
    {.hi = 0xc600000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=165 */
    {.hi = 0xc500000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=166 */
    {.hi = 0xc380000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=167 */
    {.hi = 0xc280000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=168 */
    {.hi = 0xc180000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=169 */
    {.hi = 0xc000000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=170 */
    {.hi = 0xbf00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=171 */
    {.hi = 0xbe00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=172 */
    {.hi = 0xbd00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=173 */
    {.hi = 0xbc00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=174 */
    {.hi = 0xba80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=175 */
    {.hi = 0xb980000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=176 */
    {.hi = 0xb880000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=177 */
    {.hi = 0xb780000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=178 */
    {.hi = 0xb680000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=179 */
    {.hi = 0xb580000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=180 */
    {.hi = 0xb480000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* i=181 */
};

/* For 8128 <= j <= 8256, _INVERSE_2_2[j-8128] is an approximation of the
   inverse of j/2^13, where an entry (hi,lo,ex,sgn) represents
   (-1)^sgn*(hi+lo/2^64)*2^(ex-63)
   (the binary point is after the most significant bit of hi).
   For j=8191 and j=8192, we force _INVERSE_2_2[j-8128]=1.
   If was generated with output_inverse_2_2(6,14,8128,8256,7,62) from the
   accompanying file dint.sage.
   There is no rounding error here, the only approximation error is in
   _LOG_INV_2_2[]. */
static const dint64_t _INVERSE_2_2[] = {
    {.hi = 0x8100000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8128 */
    {.hi = 0x80fc000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8129 */
    {.hi = 0x80f8000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8130 */
    {.hi = 0x80f4000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8131 */
    {.hi = 0x80f0000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8132 */
    {.hi = 0x80ec000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8133 */
    {.hi = 0x80e8000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8134 */
    {.hi = 0x80e4000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8135 */
    {.hi = 0x80e0000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8136 */
    {.hi = 0x80dc000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8137 */
    {.hi = 0x80d8000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8138 */
    {.hi = 0x80d4000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8139 */
    {.hi = 0x80d0000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8140 */
    {.hi = 0x80cc000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8141 */
    {.hi = 0x80c8000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8142 */
    {.hi = 0x80c4000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8143 */
    {.hi = 0x80c0000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8144 */
    {.hi = 0x80bc000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8145 */
    {.hi = 0x80b8000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8146 */
    {.hi = 0x80b4000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8147 */
    {.hi = 0x80b0000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8148 */
    {.hi = 0x80ac000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8149 */
    {.hi = 0x80a8000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8150 */
    {.hi = 0x80a4000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8151 */
    {.hi = 0x80a0000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8152 */
    {.hi = 0x809c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8153 */
    {.hi = 0x8098000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8154 */
    {.hi = 0x8094000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8155 */
    {.hi = 0x8090000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8156 */
    {.hi = 0x808c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8157 */
    {.hi = 0x8088000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8158 */
    {.hi = 0x8084000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8159 */
    {.hi = 0x8080000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8160 */
    {.hi = 0x807c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8161 */
    {.hi = 0x8078000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8162 */
    {.hi = 0x8074000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8163 */
    {.hi = 0x8070000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8164 */
    {.hi = 0x806c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8165 */
    {.hi = 0x8068000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8166 */
    {.hi = 0x8064000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8167 */
    {.hi = 0x8060000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8168 */
    {.hi = 0x805c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8169 */
    {.hi = 0x8058000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8170 */
    {.hi = 0x8054000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8171 */
    {.hi = 0x8050000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8172 */
    {.hi = 0x804c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8173 */
    {.hi = 0x8048000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8174 */
    {.hi = 0x8044000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8175 */
    {.hi = 0x8040000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8176 */
    {.hi = 0x803c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8177 */
    {.hi = 0x8038000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8178 */
    {.hi = 0x8034000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8179 */
    {.hi = 0x8030000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8180 */
    {.hi = 0x802c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8181 */
    {.hi = 0x8028000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8182 */
    {.hi = 0x8024000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8183 */
    {.hi = 0x8020000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8184 */
    {.hi = 0x801c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8185 */
    {.hi = 0x8018000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8186 */
    {.hi = 0x8014000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8187 */
    {.hi = 0x8010000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8188 */
    {.hi = 0x800c000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8189 */
    {.hi = 0x8008000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8190 */
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8191 */
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0}, /* j=8192 */
    {.hi = 0xfff4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8193 */
    {.hi = 0xffec000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8194 */
    {.hi = 0xffe4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8195 */
    {.hi = 0xffdc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8196 */
    {.hi = 0xffd4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8197 */
    {.hi = 0xffcc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8198 */
    {.hi = 0xffc4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8199 */
    {.hi = 0xffbc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8200 */
    {.hi = 0xffb4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8201 */
    {.hi = 0xffac000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8202 */
    {.hi = 0xffa4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8203 */
    {.hi = 0xff9c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8204 */
    {.hi = 0xff94000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8205 */
    {.hi = 0xff8c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8206 */
    {.hi = 0xff84000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8207 */
    {.hi = 0xff7c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8208 */
    {.hi = 0xff74000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8209 */
    {.hi = 0xff6c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8210 */
    {.hi = 0xff64000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8211 */
    {.hi = 0xff5c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8212 */
    {.hi = 0xff54000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8213 */
    {.hi = 0xff4c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8214 */
    {.hi = 0xff44000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8215 */
    {.hi = 0xff3c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8216 */
    {.hi = 0xff34000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8217 */
    {.hi = 0xff2c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8218 */
    {.hi = 0xff24000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8219 */
    {.hi = 0xff1c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8220 */
    {.hi = 0xff14000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8221 */
    {.hi = 0xff0c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8222 */
    {.hi = 0xff04000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8223 */
    {.hi = 0xfefc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8224 */
    {.hi = 0xfef4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8225 */
    {.hi = 0xfeec000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8226 */
    {.hi = 0xfee4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8227 */
    {.hi = 0xfedc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8228 */
    {.hi = 0xfed4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8229 */
    {.hi = 0xfecc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8230 */
    {.hi = 0xfec4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8231 */
    {.hi = 0xfebc000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8232 */
    {.hi = 0xfeb4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8233 */
    {.hi = 0xfeac000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8234 */
    {.hi = 0xfea4000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8235 */
    {.hi = 0xfe9c000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8236 */
    {.hi = 0xfe98000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8237 */
    {.hi = 0xfe90000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8238 */
    {.hi = 0xfe88000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8239 */
    {.hi = 0xfe80000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8240 */
    {.hi = 0xfe78000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8241 */
    {.hi = 0xfe70000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8242 */
    {.hi = 0xfe68000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8243 */
    {.hi = 0xfe60000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8244 */
    {.hi = 0xfe58000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8245 */
    {.hi = 0xfe50000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8246 */
    {.hi = 0xfe48000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8247 */
    {.hi = 0xfe40000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8248 */
    {.hi = 0xfe38000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8249 */
    {.hi = 0xfe30000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8250 */
    {.hi = 0xfe28000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8251 */
    {.hi = 0xfe20000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8252 */
    {.hi = 0xfe18000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8253 */
    {.hi = 0xfe10000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8254 */
    {.hi = 0xfe08000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8255 */
    {.hi = 0xfe00000000000000, .lo = 0x0, .ex = -1, .sgn = 0x0}, /* j=8256 */
};

/* For 90 <= i <= 181, _LOG_INV_2_1[i-90] is an approximation of
   -log(_INVERSE_2_1[i-90]), where an entry (hi,lo,ex,sgn) represents
   (-1)^sgn*(hi+lo/2^64)*2^(ex-63)
   (the binary point is after the most significant bit of hi).
   If was generated with output_log_inv_2_1(7,9,90,181) from the
   accompanying file dint.sage.
   The approximation error is bounded by 2^-130 (absolute) and 2^-128 (rel). */
static const dint64_t _LOG_INV_2_1[] = {
    {.hi = 0xb1641795ce3ca97b, .lo = 0x7af915300e517391, .ex = -2, .sgn = 0x1}, /* i=90 */
    {.hi = 0xabb3b8ba2ad362a4, .lo = 0xd5b6506cc17a01f1, .ex = -2, .sgn = 0x1}, /* i=91 */
    {.hi = 0xa5f2fcabbbc506da, .lo = 0x64ca4fb7ec323d73, .ex = -2, .sgn = 0x1}, /* i=92 */
    {.hi = 0xa0218434353f1de8, .lo = 0x6093efa632530ac8, .ex = -2, .sgn = 0x1}, /* i=93 */
    {.hi = 0x9bb93315fec2d792, .lo = 0xa7589fba0865790e, .ex = -2, .sgn = 0x1}, /* i=94 */
    {.hi = 0x95c981d5c4e924ed, .lo = 0x29404f5aa577d6b2, .ex = -2, .sgn = 0x1}, /* i=95 */
    {.hi = 0x914a0fde7bcb2d12, .lo = 0x1429ed3aea197a5d, .ex = -2, .sgn = 0x1}, /* i=96 */
    {.hi = 0x8b3ae55d5d30701c, .lo = 0xe63eab883717047e, .ex = -2, .sgn = 0x1}, /* i=97 */
    {.hi = 0x86a35abcd5ba5903, .lo = 0xec81c3cbd925cccf, .ex = -2, .sgn = 0x1}, /* i=98 */
    {.hi = 0x8073622d6a80e634, .lo = 0x6a97009015316071, .ex = -2, .sgn = 0x1}, /* i=99 */
    {.hi = 0xf7856e5ee2c9b290, .lo = 0xc6f2a1b84190a7d7, .ex = -3, .sgn = 0x1}, /* i=100 */
    {.hi = 0xee0de5055f63eb06, .lo = 0x98a33316df83ba57, .ex = -3, .sgn = 0x1}, /* i=101 */
    {.hi = 0xe47fbe3cd4d10d61, .lo = 0x2ec0f797fdcd1257, .ex = -3, .sgn = 0x1}, /* i=102 */
    {.hi = 0xdada8cf47dad2374, .lo = 0x4ffb833c3409ee78, .ex = -3, .sgn = 0x1}, /* i=103 */
    {.hi = 0xd11de0ff15ab18c9, .lo = 0xb88d83d4cc613f20, .ex = -3, .sgn = 0x1}, /* i=104 */
    {.hi = 0xc74946f4436a0552, .lo = 0xc4f5cb531201c0d1, .ex = -3, .sgn = 0x1}, /* i=105 */
    {.hi = 0xbd5c481086c848df, .lo = 0x1b596b5030403240, .ex = -3, .sgn = 0x1}, /* i=106 */
    {.hi = 0xb3566a13956a86f6, .lo = 0xff1b1e1574d9fd54, .ex = -3, .sgn = 0x1}, /* i=107 */
    {.hi = 0xa9372f1d0da1bd17, .lo = 0x200eb71e58cd36de, .ex = -3, .sgn = 0x1}, /* i=108 */
    {.hi = 0x9efe158766314e54, .lo = 0xc571827efe892fc4, .ex = -3, .sgn = 0x1}, /* i=109 */
    {.hi = 0x981eb8c723fe97f4, .lo = 0xa31c134fb702d432, .ex = -3, .sgn = 0x1}, /* i=110 */
    {.hi = 0x8db956a97b3d0148, .lo = 0x3023472cd739f9de, .ex = -3, .sgn = 0x1}, /* i=111 */
    {.hi = 0x8338a89652cb7150, .lo = 0xc647eb86498c2ce1, .ex = -3, .sgn = 0x1}, /* i=112 */
    {.hi = 0xf85186008b15330b, .lo = 0xe64b8b775997898d, .ex = -4, .sgn = 0x1}, /* i=113 */
    {.hi = 0xe2f2a47ade3a18ae, .lo = 0xb0bf7c0b0d8bb4ed, .ex = -4, .sgn = 0x1}, /* i=114 */
    {.hi = 0xd49369d256ab1b28, .lo = 0x5e9154e1d5263cd5, .ex = -4, .sgn = 0x1}, /* i=115 */
    {.hi = 0xbed3b36bd8966422, .lo = 0x240644d7d9ed08af, .ex = -4, .sgn = 0x1}, /* i=116 */
    {.hi = 0xb032c549ba861d8e, .lo = 0xf74e27bc92ce336a, .ex = -4, .sgn = 0x1}, /* i=117 */
    {.hi = 0xa176e5f5323781dd, .lo = 0xd4f935996c92e8cc, .ex = -4, .sgn = 0x1}, /* i=118 */
    {.hi = 0x8b29b7751bd70743, .lo = 0x12e0b9ee992f236d, .ex = -4, .sgn = 0x1}, /* i=119 */
    {.hi = 0xf85186008b15330b, .lo = 0xe64b8b775997898d, .ex = -5, .sgn = 0x1}, /* i=120 */
    {.hi = 0xda16eb88cb8df614, .lo = 0x68a63ecfb66e94ac, .ex = -5, .sgn = 0x1}, /* i=121 */
    {.hi = 0xac52dd7e4726a463, .lo = 0x547a963a91bb3012, .ex = -5, .sgn = 0x1}, /* i=122 */
    {.hi = 0x8d86cc491ecbfe16, .lo = 0x51776453b7e8254d, .ex = -5, .sgn = 0x1}, /* i=123 */
    {.hi = 0xdcfe013d7c8cbfde, .lo = 0xa32dbac46f30cfff, .ex = -6, .sgn = 0x1}, /* i=124 */
    {.hi = 0x9e75221a352ba779, .lo = 0xa52b7ea62f2198d0, .ex = -6, .sgn = 0x1}, /* i=125 */
    {.hi = 0xbee23afc0853b6e9, .lo = 0x289782c20df350a1, .ex = -7, .sgn = 0x1}, /* i=126 */
    {.hi = 0x0, .lo = 0x0, .ex = 127, .sgn = 0x1}, /* i=127 */
    {.hi = 0x0, .lo = 0x0, .ex = 127, .sgn = 0x1}, /* i=128 */
    {.hi = 0xc122451c45155104, .lo = 0xb16137f09a002b3c, .ex = -7, .sgn = 0x0}, /* i=129 */
    {.hi = 0xa195492cc06604e6, .lo = 0x4a18dff7cdb4ae5c, .ex = -6, .sgn = 0x0}, /* i=130 */
    {.hi = 0xe31e9760a5578c63, .lo = 0xf9eb2f284f31c35c, .ex = -6, .sgn = 0x0}, /* i=131 */
    {.hi = 0x8a4f1f2002d46756, .lo = 0x5be970314148c645, .ex = -5, .sgn = 0x0}, /* i=132 */
    {.hi = 0xab8ae2601e777722, .lo = 0x3b89d7f254f8d4d, .ex = -5, .sgn = 0x0}, /* i=133 */
    {.hi = 0xcd0c3dab9ef3dd1b, .lo = 0x13b26f298aa357c8, .ex = -5, .sgn = 0x0}, /* i=134 */
    {.hi = 0xe65b9e6eed965c36, .lo = 0xe09f5fe2058d6006, .ex = -5, .sgn = 0x0}, /* i=135 */
    {.hi = 0x842cc5acf1d03445, .lo = 0x1fecdfa819b96098, .ex = -4, .sgn = 0x0}, /* i=136 */
    {.hi = 0x9103dae3c2a4ec67, .lo = 0xe0863df62ab5671a, .ex = -4, .sgn = 0x0}, /* i=137 */
    {.hi = 0xa242f01edefd6a37, .lo = 0x469355b78dc796e3, .ex = -4, .sgn = 0x0}, /* i=138 */
    {.hi = 0xaf4ad26cbc8e5be7, .lo = 0xe8b8b88a14ff0ce, .ex = -4, .sgn = 0x0}, /* i=139 */
    {.hi = 0xc0cbf17a071f80dc, .lo = 0xf96ffdf76a147ccc, .ex = -4, .sgn = 0x0}, /* i=140 */
    {.hi = 0xce06196a692a41fb, .lo = 0xbe3ccc15326765f, .ex = -4, .sgn = 0x0}, /* i=141 */
    {.hi = 0xdb56446d6ad8deff, .lo = 0xa8112e35a60e6375, .ex = -4, .sgn = 0x0}, /* i=142 */
    {.hi = 0xe8bcbc410c9b219d, .lo = 0xaf7df76ad29e5b60, .ex = -4, .sgn = 0x0}, /* i=143 */
    {.hi = 0xf639cc185088fe5d, .lo = 0x4066e87f2c0f7340, .ex = -4, .sgn = 0x0}, /* i=144 */
    {.hi = 0x842cc5acf1d03445, .lo = 0x1fecdfa819b96098, .ex = -3, .sgn = 0x0}, /* i=145 */
    {.hi = 0x8b064012593d85a5, .lo = 0x52013c7a80ad089b, .ex = -3, .sgn = 0x0}, /* i=146 */
    {.hi = 0x91eb89524e100d23, .lo = 0x8fd3df5c52d67e7b, .ex = -3, .sgn = 0x0}, /* i=147 */
    {.hi = 0x98dcca69d27c263b, .lo = 0x8e94203f336fc8c5, .ex = -3, .sgn = 0x0}, /* i=148 */
    {.hi = 0x9fda2d2cc9465c4f, .lo = 0x32b9565f5355182, .ex = -3, .sgn = 0x0}, /* i=149 */
    {.hi = 0xa6e3dc4bde0e3cdb, .lo = 0x570ff874170d2a9, .ex = -3, .sgn = 0x0}, /* i=150 */
    {.hi = 0xab9be6480c66ea9e, .lo = 0x9ae21fd871b8d27c, .ex = -3, .sgn = 0x0}, /* i=151 */
    {.hi = 0xb2ba75f46099cf8b, .lo = 0x2c3c2e77904afa78, .ex = -3, .sgn = 0x0}, /* i=152 */
    {.hi = 0xb9e5c83a7e8a655b, .lo = 0xcbffe9661fe72421, .ex = -3, .sgn = 0x0}, /* i=153 */
    {.hi = 0xc11e0b2a8d1e0ddb, .lo = 0x9a631e830fd30904, .ex = -3, .sgn = 0x0}, /* i=154 */
    {.hi = 0xc8636dcfe5e6ca0a, .lo = 0x88e72835b3292d50, .ex = -3, .sgn = 0x0}, /* i=155 */
    {.hi = 0xcd43bc6f5d51c3e8, .lo = 0xfbfb0e3f0fd23074, .ex = -3, .sgn = 0x0}, /* i=156 */
    {.hi = 0xd49f69e456cf1b79, .lo = 0x5f53bd2e406e66e7, .ex = -3, .sgn = 0x0}, /* i=157 */
    {.hi = 0xdc08b985c11e9068, .lo = 0x3b9cd767c3b1ac53, .ex = -3, .sgn = 0x0}, /* i=158 */
    {.hi = 0xe1014558bfcda3e2, .lo = 0x35470a74be1230ec, .ex = -3, .sgn = 0x0}, /* i=159 */
    {.hi = 0xe881bf932af3dac0, .lo = 0xc524848e3443e040, .ex = -3, .sgn = 0x0}, /* i=160 */
    {.hi = 0xed89ed86a44a01aa, .lo = 0x11d49f96cb88317b, .ex = -3, .sgn = 0x0}, /* i=161 */
    {.hi = 0xf52224f82557a459, .lo = 0x8dcca8d7f17fa2a9, .ex = -3, .sgn = 0x0}, /* i=162 */
    {.hi = 0xfa3a589a6f9146d8, .lo = 0x388212895529a6fb, .ex = -3, .sgn = 0x0}, /* i=163 */
    {.hi = 0x80f572b1363487b9, .lo = 0xf5bd0b5b3479d5f4, .ex = -2, .sgn = 0x0}, /* i=164 */
    {.hi = 0x8389c3026ac3139b, .lo = 0x62dda9d2270fa1f4, .ex = -2, .sgn = 0x0}, /* i=165 */
    {.hi = 0x86216b3b0b17188b, .lo = 0x163ceae88f720f1e, .ex = -2, .sgn = 0x0}, /* i=166 */
    {.hi = 0x8a0b3f79b3bc180f, .lo = 0x49b55ea7d3730d7, .ex = -2, .sgn = 0x0}, /* i=167 */
    {.hi = 0x8cab69dcde17d2f7, .lo = 0x3ad1aa142b94f16a, .ex = -2, .sgn = 0x0}, /* i=168 */
    {.hi = 0x8f4f0b3c44cfa2a2, .lo = 0x586e9343c9cfdbac, .ex = -2, .sgn = 0x0}, /* i=169 */
    {.hi = 0x934b1089a6dc93c1, .lo = 0xdf5bb3b60554e152, .ex = -2, .sgn = 0x0}, /* i=170 */
    {.hi = 0x95f783e6e49a9cfa, .lo = 0x4a5004f3ef063313, .ex = -2, .sgn = 0x0}, /* i=171 */
    {.hi = 0x98a78f0e9ae71d85, .lo = 0x2cdec34784707839, .ex = -2, .sgn = 0x0}, /* i=172 */
    {.hi = 0x9b5b3bb5f088b766, .lo = 0xd878bbe3d392be25, .ex = -2, .sgn = 0x0}, /* i=173 */
    {.hi = 0x9e1293b9998c1daa, .lo = 0x5b035eae273a855f, .ex = -2, .sgn = 0x0}, /* i=174 */
    {.hi = 0xa22c8f029cfa45a9, .lo = 0xdb5b709e0b69e773, .ex = -2, .sgn = 0x0}, /* i=175 */
    {.hi = 0xa4ed3f9de620f666, .lo = 0x9b5e973353638c11, .ex = -2, .sgn = 0x0}, /* i=176 */
    {.hi = 0xa7b1bf5dd4c07d4e, .lo = 0x699db68db75e9a7f, .ex = -2, .sgn = 0x0}, /* i=177 */
    {.hi = 0xaa7a18dbdf0d44aa, .lo = 0x604884a8dd76d08a, .ex = -2, .sgn = 0x0}, /* i=178 */
    {.hi = 0xad4656ddf6fd070c, .lo = 0x9ea10260fe452ba2, .ex = -2, .sgn = 0x0}, /* i=179 */
    {.hi = 0xb0168457848f5f48, .lo = 0xbb6f9fb246068d52, .ex = -2, .sgn = 0x0}, /* i=180 */
    {.hi = 0xb2eaac6a67005513, .lo = 0xf4b716f6fec8156b, .ex = -2, .sgn = 0x0}, /* i=181 */
};

/* For 8128 <= j <= 8256, _LOG_INV_2_2[j-8128] is an approximation of
   -log(_INVERSE_2_2[j-8128]), where an entry (hi,lo,ex,sgn) represents
   (-1)^sgn*(hi+lo/2^64)*2^(ex-63)
   (the binary point is after the most significant bit of hi).
   If was generated with output_log_inv_2_2(6,14,8128,8256,7,62) from the
   accompanying file dint.sage.
   The approximation error is bounded by 2^-136 (absolute, attained for j=8256)
   and 2^-128 (relative, attained for j=8209). */
static const dint64_t _LOG_INV_2_2[] = {
    {.hi = 0xff015358833c47e1, .lo = 0xbb481c8ee141695a, .ex = -8, .sgn = 0x1}, /* j=8128 */
    {.hi = 0xfb0933b732572a6d, .lo = 0x214cca3dd1d4796a, .ex = -8, .sgn = 0x1}, /* j=8129 */
    {.hi = 0xf710f492711d9d26, .lo = 0xfbc7b38b17b2019, .ex = -8, .sgn = 0x1}, /* j=8130 */
    {.hi = 0xf31895e84b1a6be6, .lo = 0xb76782b9e88c84cb, .ex = -8, .sgn = 0x1}, /* j=8131 */
    {.hi = 0xef2017b6cba9cf9a, .lo = 0x2dc85881664025b5, .ex = -8, .sgn = 0x1}, /* j=8132 */
    {.hi = 0xeb2779fbfdf96874, .lo = 0xce4ab4e678d0ed03, .ex = -8, .sgn = 0x1}, /* j=8133 */
    {.hi = 0xe72ebcb5ed08382b, .lo = 0xb60585f4c4bb6062, .ex = -8, .sgn = 0x1}, /* j=8134 */
    {.hi = 0xe335dfe2a3a69c2b, .lo = 0x59bcffe9d5650564, .ex = -8, .sgn = 0x1}, /* j=8135 */
    {.hi = 0xdf3ce3802c7647cd, .lo = 0x3602021fa93b1e18, .ex = -8, .sgn = 0x1}, /* j=8136 */
    {.hi = 0xdb43c78c91ea3e8c, .lo = 0x9944002534d09b3d, .ex = -8, .sgn = 0x1}, /* j=8137 */
    {.hi = 0xd74a8c05de46ce3a, .lo = 0x87aa95782311a277, .ex = -8, .sgn = 0x1}, /* j=8138 */
    {.hi = 0xd35130ea1ba18930, .lo = 0xb88be10313a1303d, .ex = -8, .sgn = 0x1}, /* j=8139 */
    {.hi = 0xcf57b63753e14083, .lo = 0xad54bc31433dddba, .ex = -8, .sgn = 0x1}, /* j=8140 */
    {.hi = 0xcb5e1beb90bdfe33, .lo = 0xe1b7d813e3f825e1, .ex = -8, .sgn = 0x1}, /* j=8141 */
    {.hi = 0xc7646204dbc0ff5e, .lo = 0x14f8c1be7370f219, .ex = -8, .sgn = 0x1}, /* j=8142 */
    {.hi = 0xc36a88813e44ae6a, .lo = 0xac27c5a6139cd30c, .ex = -8, .sgn = 0x1}, /* j=8143 */
    {.hi = 0xbf708f5ec1749d3c, .lo = 0x2d23a0744e00f594, .ex = -8, .sgn = 0x1}, /* j=8144 */
    {.hi = 0xbb76769b6e4d7f5c, .lo = 0xd235e25fb9644c31, .ex = -8, .sgn = 0x1}, /* j=8145 */
    {.hi = 0xb77c3e354d9d242b, .lo = 0x361ee0bcb5db0449, .ex = -8, .sgn = 0x1}, /* j=8146 */
    {.hi = 0xb381e62a68027106, .lo = 0x18660815da3d7963, .ex = -8, .sgn = 0x1}, /* j=8147 */
    {.hi = 0xaf876e78c5ed5b77, .lo = 0x39c357b6bfdf81b5, .ex = -8, .sgn = 0x1}, /* j=8148 */
    {.hi = 0xab8cd71e6f9ee35d, .lo = 0x5076c62c951204f6, .ex = -8, .sgn = 0x1}, /* j=8149 */
    {.hi = 0xa79220196d290d15, .lo = 0x146244d643f7fa2b, .ex = -8, .sgn = 0x1}, /* j=8150 */
    {.hi = 0xa3974967c66edba1, .lo = 0x62bb0f3208d9a1bb, .ex = -8, .sgn = 0x1}, /* j=8151 */
    {.hi = 0x9f9c530783244ad2, .lo = 0x7926e92808bd580d, .ex = -8, .sgn = 0x1}, /* j=8152 */
    {.hi = 0x9ba13cf6aace496c, .lo = 0x4819e620d5fcc068, .ex = -8, .sgn = 0x1}, /* j=8153 */
    {.hi = 0x97a6073344c2b34b, .lo = 0xdc494943d427214e, .ex = -8, .sgn = 0x1}, /* j=8154 */
    {.hi = 0x93aab1bb58284b8b, .lo = 0xdf0805c4161e404c, .ex = -8, .sgn = 0x1}, /* j=8155 */
    {.hi = 0x8faf3c8cebf6b6a8, .lo = 0x2d615caaa0514c3c, .ex = -8, .sgn = 0x1}, /* j=8156 */
    {.hi = 0x8bb3a7a606f674a0, .lo = 0x85c60c12eca0aedc, .ex = -8, .sgn = 0x1}, /* j=8157 */
    {.hi = 0x87b7f304afc0db1a, .lo = 0x4c207a522524f8de, .ex = -8, .sgn = 0x1}, /* j=8158 */
    {.hi = 0x83bc1ea6ecc00f81, .lo = 0x64243e02c6215a4f, .ex = -8, .sgn = 0x1}, /* j=8159 */
    {.hi = 0xff805515885e0250, .lo = 0x435ab4da6a5bb48d, .ex = -9, .sgn = 0x1}, /* j=8160 */
    {.hi = 0xf7882d5c7832c6cc, .lo = 0x9e06fc84b6ea5e24, .ex = -9, .sgn = 0x1}, /* j=8161 */
    {.hi = 0xef8fc61eb4b74f6e, .lo = 0x91ab122ee427cfb5, .ex = -9, .sgn = 0x1}, /* j=8162 */
    {.hi = 0xe7971f584945efae, .lo = 0x5f832513e3211643, .ex = -9, .sgn = 0x1}, /* j=8163 */
    {.hi = 0xdf9e390540da5fbe, .lo = 0x5e7b48cfeeb85aa8, .ex = -9, .sgn = 0x1}, /* j=8164 */
    {.hi = 0xd7a51321a611b0c1, .lo = 0xb36a9f58eb4ccd08, .ex = -9, .sgn = 0x1}, /* j=8165 */
    {.hi = 0xcfabada9832a4101, .lo = 0x3360751e43c7af35, .ex = -9, .sgn = 0x1}, /* j=8166 */
    {.hi = 0xc7b20898e203b01e, .lo = 0x6fab78aca91193cb, .ex = -9, .sgn = 0x1}, /* j=8167 */
    {.hi = 0xbfb823ebcc1ed344, .lo = 0xeb432409cffdad8d, .ex = -9, .sgn = 0x1}, /* j=8168 */
    {.hi = 0xb7bdff9e4a9da959, .lo = 0x793b5acf3a336462, .ex = -9, .sgn = 0x1}, /* j=8169 */
    {.hi = 0xafc39bac66434f27, .lo = 0xc3ea2cd93f316b34, .ex = -9, .sgn = 0x1}, /* j=8170 */
    {.hi = 0xa7c8f8122773f38d, .lo = 0xfc679a28e9d9f212, .ex = -9, .sgn = 0x1}, /* j=8171 */
    {.hi = 0x9fce14cb9634cba6, .lo = 0xb20f215bd3b58c61, .ex = -9, .sgn = 0x1}, /* j=8172 */
    {.hi = 0x97d2f1d4ba2c06f0, .lo = 0xd1aacedcefe9d377, .ex = -9, .sgn = 0x1}, /* j=8173 */
    {.hi = 0x8fd78f299aa0c375, .lo = 0xcbef6fac33691e95, .ex = -9, .sgn = 0x1}, /* j=8174 */
    {.hi = 0x87dbecc63e7b01ed, .lo = 0xe2f1775134c8da75, .ex = -9, .sgn = 0x1}, /* j=8175 */
    {.hi = 0xffc0154d588733c5, .lo = 0x3c742a7c76356396, .ex = -10, .sgn = 0x1}, /* j=8176 */
    {.hi = 0xefc7d18dd4485b9e, .lo = 0xca47c52b7d7ffce2, .ex = -10, .sgn = 0x1}, /* j=8177 */
    {.hi = 0xdfcf0e45fbce3e80, .lo = 0x7e4cfbd830393b88, .ex = -10, .sgn = 0x1}, /* j=8178 */
    {.hi = 0xcfd5cb6dd9ef05dd, .lo = 0x7370ae83f9e72748, .ex = -10, .sgn = 0x1}, /* j=8179 */
    {.hi = 0xbfdc08fd78c229b9, .lo = 0xe6dbb624f9739782, .ex = -10, .sgn = 0x1}, /* j=8180 */
    {.hi = 0xafe1c6ece1a058dd, .lo = 0x97fa2fd0c9dc723e, .ex = -10, .sgn = 0x1}, /* j=8181 */
    {.hi = 0x9fe705341d236102, .lo = 0x7199cd06ae5d39b3, .ex = -10, .sgn = 0x1}, /* j=8182 */
    {.hi = 0x8febc3cb332616ff, .lo = 0x7b6d1248c3e1fd40, .ex = -10, .sgn = 0x1}, /* j=8183 */
    {.hi = 0xffe0055455887de0, .lo = 0x26828c92649a3a39, .ex = -11, .sgn = 0x1}, /* j=8184 */
    {.hi = 0xdfe7839214b4e8ae, .lo = 0xda6959f7f0e01bf0, .ex = -11, .sgn = 0x1}, /* j=8185 */
    {.hi = 0xbfee023faf0c2480, .lo = 0xb47505bfa5a03b06, .ex = -11, .sgn = 0x1}, /* j=8186 */
    {.hi = 0x9ff3814d2e4a36b2, .lo = 0xa8740b91c95df537, .ex = -11, .sgn = 0x1}, /* j=8187 */
    {.hi = 0xfff0015535588833, .lo = 0x3c56c598c659c2a3, .ex = -12, .sgn = 0x1}, /* j=8188 */
    {.hi = 0xbff7008ff5e0c257, .lo = 0x379eba7e6465ff63, .ex = -12, .sgn = 0x1}, /* j=8189 */
    {.hi = 0xfff8005551558885, .lo = 0xde026e271ee0549d, .ex = -13, .sgn = 0x1}, /* j=8190 */
    {.hi = 0x0, .lo = 0x0, .ex = 127, .sgn = 0x1}, /* j=8191 */
    {.hi = 0x0, .lo = 0x0, .ex = 127, .sgn = 0x1}, /* j=8192 */
    {.hi = 0xc004802401440c26, .lo = 0xdfeb485085f6f454, .ex = -13, .sgn = 0x0}, /* j=8193 */
    {.hi = 0xa00640535a37a37a, .lo = 0x6bc1e20eac8448b4, .ex = -12, .sgn = 0x0}, /* j=8194 */
    {.hi = 0xe00c40e4bd6e4efd, .lo = 0xc72446cc1bf728bd, .ex = -12, .sgn = 0x0}, /* j=8195 */
    {.hi = 0x900a20f319a3e273, .lo = 0x569b26aaa485ea5c, .ex = -11, .sgn = 0x0}, /* j=8196 */
    {.hi = 0xb00f21bbe3e388ee, .lo = 0x5f69768284463b9b, .ex = -11, .sgn = 0x0}, /* j=8197 */
    {.hi = 0xd01522dcc4f87991, .lo = 0x14d9d76196d8043a, .ex = -11, .sgn = 0x0}, /* j=8198 */
    {.hi = 0xf01c2465c5e61b6f, .lo = 0x661e135f49a47c40, .ex = -11, .sgn = 0x0}, /* j=8199 */
    {.hi = 0x881213337898871e, .lo = 0x9a31ba0cbc030353, .ex = -10, .sgn = 0x0}, /* j=8200 */
    {.hi = 0x98169478296fad41, .lo = 0x7ad1e9c315328f7e, .ex = -10, .sgn = 0x0}, /* j=8201 */
    {.hi = 0xa81b9608fc3c50ec, .lo = 0xf105b66ec4703ede, .ex = -10, .sgn = 0x0}, /* j=8202 */
    {.hi = 0xb82117edf8832797, .lo = 0xd6aef30cd312169a, .ex = -10, .sgn = 0x0}, /* j=8203 */
    {.hi = 0xc8271a2f2689e388, .lo = 0xe6e2acf8f4d4c24a, .ex = -10, .sgn = 0x0}, /* j=8204 */
    {.hi = 0xd82d9cd48f574c00, .lo = 0x28bb3cd9f2a65fb5, .ex = -10, .sgn = 0x0}, /* j=8205 */
    {.hi = 0xe8349fe63cb35564, .lo = 0x224a96f5a7471c46, .ex = -10, .sgn = 0x0}, /* j=8206 */
    {.hi = 0xf83c236c39273972, .lo = 0xd462b63756c87e80, .ex = -10, .sgn = 0x0}, /* j=8207 */
    {.hi = 0x842213b747fec7bb, .lo = 0x3ff51287882500ed, .ex = -9, .sgn = 0x0}, /* j=8208 */
    {.hi = 0x8c2655faa6a1323f, .lo = 0x1ab9679b55f78a6b, .ex = -9, .sgn = 0x0}, /* j=8209 */
    {.hi = 0x942ad8843ee1a9cd, .lo = 0x17e4b7ac6c600cb4, .ex = -9, .sgn = 0x0}, /* j=8210 */
    {.hi = 0x9c2f9b581787cf0d, .lo = 0xfd1a09c848e3950e, .ex = -9, .sgn = 0x0}, /* j=8211 */
    {.hi = 0xa4349e7a37bc21ed, .lo = 0x318b2ddd9d0a33b4, .ex = -9, .sgn = 0x0}, /* j=8212 */
    {.hi = 0xac39e1eea7080dbc, .lo = 0x9dd91e52c79fd070, .ex = -9, .sgn = 0x0}, /* j=8213 */
    {.hi = 0xb43f65b96d55f55a, .lo = 0x72de1d99ce252efd, .ex = -9, .sgn = 0x0}, /* j=8214 */
    {.hi = 0xbc4529de92f13f58, .lo = 0xd7bd1d62ef25480d, .ex = -9, .sgn = 0x0}, /* j=8215 */
    {.hi = 0xc44b2e6220866227, .lo = 0x7f921124f1ecb59e, .ex = -9, .sgn = 0x0}, /* j=8216 */
    {.hi = 0xcc5173481f22f03f, .lo = 0x271ee1cd6d5cdf9e, .ex = -9, .sgn = 0x0}, /* j=8217 */
    {.hi = 0xd457f8949835a44e, .lo = 0xfad0cc8b5faea8cc, .ex = -9, .sgn = 0x0}, /* j=8218 */
    {.hi = 0xdc5ebe4b958e6d6b, .lo = 0xe57a0acb9d5cd4df, .ex = -9, .sgn = 0x0}, /* j=8219 */
    {.hi = 0xe465c471215e7b41, .lo = 0xc81bb5a8d789f444, .ex = -9, .sgn = 0x0}, /* j=8220 */
    {.hi = 0xec6d0b0946384a46, .lo = 0x9b1beb40437575f5, .ex = -9, .sgn = 0x0}, /* j=8221 */
    {.hi = 0xf47492180f0fafef, .lo = 0x7944509046652d99, .ex = -9, .sgn = 0x0}, /* j=8222 */
    {.hi = 0xfc7c59a18739e6e7, .lo = 0x94e51ebff53a2f15, .ex = -9, .sgn = 0x0}, /* j=8223 */
    {.hi = 0x824230d4dd36cda4, .lo = 0x8bbc7f765b13ebbe, .ex = -8, .sgn = 0x0}, /* j=8224 */
    {.hi = 0x8646551a5a617b6b, .lo = 0xf61305ef7390939c, .ex = -8, .sgn = 0x0}, /* j=8225 */
    {.hi = 0x8a4a99a34159d69f, .lo = 0x3abc32a78afd4b7b, .ex = -8, .sgn = 0x0}, /* j=8226 */
    {.hi = 0x8e4efe71988d8426, .lo = 0x17596a598cb29436, .ex = -8, .sgn = 0x0}, /* j=8227 */
    {.hi = 0x92538387669afa1b, .lo = 0x1c890bee9a9d743c, .ex = -8, .sgn = 0x0}, /* j=8228 */
    {.hi = 0x965828e6b25185ec, .lo = 0xeaafbd07b543145d, .ex = -8, .sgn = 0x0}, /* j=8229 */
    {.hi = 0x9a5cee9182b15280, .lo = 0x6517bc4112d64b17, .ex = -8, .sgn = 0x0}, /* j=8230 */
    {.hi = 0x9e61d489deeb6e53, .lo = 0xdb94a1dfd653d3a5, .ex = -8, .sgn = 0x0}, /* j=8231 */
    {.hi = 0xa266dad1ce61d1a3, .lo = 0x2ada01ce7ed36080, .ex = -8, .sgn = 0x0}, /* j=8232 */
    {.hi = 0xa66c016b58a7648c, .lo = 0xd3b36c029ea7bb5d, .ex = -8, .sgn = 0x0}, /* j=8233 */
    {.hi = 0xaa71485885800538, .lo = 0x94c529f32403828, .ex = -8, .sgn = 0x0}, /* j=8234 */
    {.hi = 0xae76af9b5ce08dfb, .lo = 0xb6b6676248bba139, .ex = -8, .sgn = 0x0}, /* j=8235 */
    {.hi = 0xb27c3735e6eedb86, .lo = 0x7bdd0c2a9c7a679a, .ex = -8, .sgn = 0x0}, /* j=8236 */
    {.hi = 0xb47f0724b1906935, .lo = 0x23deb274e953a259, .ex = -8, .sgn = 0x0}, /* j=8237 */
    {.hi = 0xb884bf4697559ffa, .lo = 0xdae7e343fa859415, .ex = -8, .sgn = 0x0}, /* j=8238 */
    {.hi = 0xbc8a97c544fdd5eb, .lo = 0x17759bff5c717993, .ex = -8, .sgn = 0x0}, /* j=8239 */
    {.hi = 0xc09090a2c35aa070, .lo = 0x52e7e4dde874dace, .ex = -8, .sgn = 0x0}, /* j=8240 */
    {.hi = 0xc496a9e11b6eb30c, .lo = 0xa88971f8277a4d11, .ex = -8, .sgn = 0x0}, /* j=8241 */
    {.hi = 0xc89ce382566de587, .lo = 0x269de85f0df92588, .ex = -8, .sgn = 0x0}, /* j=8242 */
    {.hi = 0xcca33d887dbd3a1a, .lo = 0x180d255422c3377c, .ex = -8, .sgn = 0x0}, /* j=8243 */
    {.hi = 0xd0a9b7f59af2e3a2, .lo = 0x46da70925ee85c05, .ex = -8, .sgn = 0x0}, /* j=8244 */
    {.hi = 0xd4b052cbb7d64bcf, .lo = 0x37968ceafaf7b453, .ex = -8, .sgn = 0x0}, /* j=8245 */
    {.hi = 0xd8b70e0cde601954, .lo = 0x5dfba4cfdd38a059, .ex = -8, .sgn = 0x0}, /* j=8246 */
    {.hi = 0xdcbde9bb18ba361b, .lo = 0x4ae21abe75d5a19b, .ex = -8, .sgn = 0x0}, /* j=8247 */
    {.hi = 0xe0c4e5d8713fd576, .lo = 0xd3bd4fd98a1e6fe5, .ex = -8, .sgn = 0x0}, /* j=8248 */
    {.hi = 0xe4cc0266f27d7a57, .lo = 0x33cf7d5ebfb93ad3, .ex = -8, .sgn = 0x0}, /* j=8249 */
    {.hi = 0xe8d33f68a730fd7f, .lo = 0x2743c805a4928087, .ex = -8, .sgn = 0x0}, /* j=8250 */
    {.hi = 0xecda9cdf9a4993ba, .lo = 0x5dbeb9795455a5, .ex = -8, .sgn = 0x0}, /* j=8251 */
    {.hi = 0xf0e21acdd6e7d412, .lo = 0xb6ed80852ae6fd63, .ex = -8, .sgn = 0x0}, /* j=8252 */
    {.hi = 0xf4e9b935685dbe0b, .lo = 0xf237cff1acb306b3, .ex = -8, .sgn = 0x0}, /* j=8253 */
    {.hi = 0xf8f178185a2ebfd9, .lo = 0xd81648249cece4c, .ex = -8, .sgn = 0x0}, /* j=8254 */
    {.hi = 0xfcf95778b80fbc98, .lo = 0x176cd56887ac7fe9, .ex = -8, .sgn = 0x0}, /* j=8255 */
    {.hi = 0x8080abac46f38946, .lo = 0x662d417ced007a46, .ex = -7, .sgn = 0x0}, /* j=8256 */
};

/* for 0 <= i < 64, T1_2[i] is a 128-bit nearest approximation of 2^(i/64),
   with error bounded by 2^-128 (both absolutely and relatively).
   Table generated by output_T1_2() from the accompanying dint.sage file. */
static const dint64_t T1_2[] = {
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0},
    {.hi = 0x8164d1f3bc030773, .lo = 0x7be56527bd14def5, .ex = 0, .sgn = 0x0},
    {.hi = 0x82cd8698ac2ba1d7, .lo = 0x3e2a475b46520bff, .ex = 0, .sgn = 0x0},
    {.hi = 0x843a28c3acde4046, .lo = 0x1af92eca13fd1582, .ex = 0, .sgn = 0x0},
    {.hi = 0x85aac367cc487b14, .lo = 0xc5c95b8c2154c1b2, .ex = 0, .sgn = 0x0},
    {.hi = 0x871f61969e8d1010, .lo = 0x3a1727c57b52a956, .ex = 0, .sgn = 0x0},
    {.hi = 0x88980e8092da8527, .lo = 0x5df8d76c98c67563, .ex = 0, .sgn = 0x0},
    {.hi = 0x8a14d575496efd9a, .lo = 0x80ca1d92c3680c2, .ex = 0, .sgn = 0x0},
    {.hi = 0x8b95c1e3ea8bd6e6, .lo = 0xfbe4628758a53c90, .ex = 0, .sgn = 0x0},
    {.hi = 0x8d1adf5b7e5ba9e5, .lo = 0xb4c7b4968e41ad36, .ex = 0, .sgn = 0x0},
    {.hi = 0x8ea4398b45cd53c0, .lo = 0x2dc0144c8783d4c6, .ex = 0, .sgn = 0x0},
    {.hi = 0x9031dc431466b1dc, .lo = 0x775814a8494e87e2, .ex = 0, .sgn = 0x0},
    {.hi = 0x91c3d373ab11c336, .lo = 0xfd6d8e0ae5ac9d8, .ex = 0, .sgn = 0x0},
    {.hi = 0x935a2b2f13e6e92b, .lo = 0xd339940e9d924ee7, .ex = 0, .sgn = 0x0},
    {.hi = 0x94f4efa8fef70961, .lo = 0x2e8afad12551de54, .ex = 0, .sgn = 0x0},
    {.hi = 0x96942d3720185a00, .lo = 0x48ea9b683a9c22c5, .ex = 0, .sgn = 0x0},
    {.hi = 0x9837f0518db8a96f, .lo = 0x46ad23182e42f6f6, .ex = 0, .sgn = 0x0},
    {.hi = 0x99e0459320b7fa64, .lo = 0xe43086cb34b5fcaf, .ex = 0, .sgn = 0x0},
    {.hi = 0x9b8d39b9d54e5538, .lo = 0xa2a817a2a3cc3f1f, .ex = 0, .sgn = 0x0},
    {.hi = 0x9d3ed9a72cffb750, .lo = 0xde494cf050e99b0b, .ex = 0, .sgn = 0x0},
    {.hi = 0x9ef5326091a111ad, .lo = 0xa0911f09ebb9fdd1, .ex = 0, .sgn = 0x0},
    {.hi = 0xa0b0510fb9714fc2, .lo = 0x192dc79edb0fd9a9, .ex = 0, .sgn = 0x0},
    {.hi = 0xa27043030c496818, .lo = 0x9b7a04ef80cfdea8, .ex = 0, .sgn = 0x0},
    {.hi = 0xa43515ae09e6809e, .lo = 0xd1db4831781e1ef, .ex = 0, .sgn = 0x0},
    {.hi = 0xa5fed6a9b15138ea, .lo = 0x1cbd7f621710701b, .ex = 0, .sgn = 0x0},
    {.hi = 0xa7cd93b4e9653569, .lo = 0x9ec5b4d5039f72af, .ex = 0, .sgn = 0x0},
    {.hi = 0xa9a15ab4ea7c0ef8, .lo = 0x541e24ec3531fa73, .ex = 0, .sgn = 0x0},
    {.hi = 0xab7a39b5a93ed337, .lo = 0x658023b2759e0079, .ex = 0, .sgn = 0x0},
    {.hi = 0xad583eea42a14ac6, .lo = 0x4980a8c8f59a2ec4, .ex = 0, .sgn = 0x0},
    {.hi = 0xaf3b78ad690a4374, .lo = 0xdf26101ccbb35033, .ex = 0, .sgn = 0x0},
    {.hi = 0xb123f581d2ac258f, .lo = 0x87d037e96d215d8e, .ex = 0, .sgn = 0x0},
    {.hi = 0xb311c412a9112489, .lo = 0x3ecf14dc798a519c, .ex = 0, .sgn = 0x0},
    {.hi = 0xb504f333f9de6484, .lo = 0x597d89b3754abe9f, .ex = 0, .sgn = 0x0},
    {.hi = 0xb6fd91e328d17791, .lo = 0x7165f0ddd541a5a, .ex = 0, .sgn = 0x0},
    {.hi = 0xb8fbaf4762fb9ee9, .lo = 0x1b879778566b65a2, .ex = 0, .sgn = 0x0},
    {.hi = 0xbaff5ab2133e45fb, .lo = 0x74d519d24593838c, .ex = 0, .sgn = 0x0},
    {.hi = 0xbd08a39f580c36be, .lo = 0xa8811fb66d0faf7a, .ex = 0, .sgn = 0x0},
    {.hi = 0xbf1799b67a731082, .lo = 0xe815d0abcbf0b851, .ex = 0, .sgn = 0x0},
    {.hi = 0xc12c4cca66709456, .lo = 0x7c457d59a50087b5, .ex = 0, .sgn = 0x0},
    {.hi = 0xc346ccda24976407, .lo = 0x20ec856128b83a42, .ex = 0, .sgn = 0x0},
    {.hi = 0xc5672a115506dadd, .lo = 0x3e2ad0c964dd9f37, .ex = 0, .sgn = 0x0},
    {.hi = 0xc78d74c8abb9b15c, .lo = 0xc13a2e3976c0277e, .ex = 0, .sgn = 0x0},
    {.hi = 0xc9b9bd866e2f27a2, .lo = 0x80e1f92a0511697e, .ex = 0, .sgn = 0x0},
    {.hi = 0xcbec14fef2727c5c, .lo = 0xf4907c8f45ebf6dd, .ex = 0, .sgn = 0x0},
    {.hi = 0xce248c151f8480e3, .lo = 0xe235838f95f2c6ed, .ex = 0, .sgn = 0x0},
    {.hi = 0xd06333daef2b2594, .lo = 0xd6d45c6559a4d502, .ex = 0, .sgn = 0x0},
    {.hi = 0xd2a81d91f12ae45a, .lo = 0x12248e57c3de4028, .ex = 0, .sgn = 0x0},
    {.hi = 0xd4f35aabcfedfa1f, .lo = 0x5921deffa6262c5b, .ex = 0, .sgn = 0x0},
    {.hi = 0xd744fccad69d6af4, .lo = 0x39a68bb9902d3fde, .ex = 0, .sgn = 0x0},
    {.hi = 0xd99d15c278afd7b5, .lo = 0xfe873deca3e12bac, .ex = 0, .sgn = 0x0},
    {.hi = 0xdbfbb797daf23755, .lo = 0x3d840d5a9e29aa64, .ex = 0, .sgn = 0x0},
    {.hi = 0xde60f4825e0e9123, .lo = 0xdd07a2d9e8466859, .ex = 0, .sgn = 0x0},
    {.hi = 0xe0ccdeec2a94e111, .lo = 0x65895048dd333ca, .ex = 0, .sgn = 0x0},
    {.hi = 0xe33f8972be8a5a51, .lo = 0x9bfe90795980eed, .ex = 0, .sgn = 0x0},
    {.hi = 0xe5b906e77c8348a8, .lo = 0x1e5e8f4a4edbb0ed, .ex = 0, .sgn = 0x0},
    {.hi = 0xe8396a503c4bdc68, .lo = 0x791790d0ac70c7de, .ex = 0, .sgn = 0x0},
    {.hi = 0xeac0c6e7dd24392e, .lo = 0xd02d75b3706e54fb, .ex = 0, .sgn = 0x0},
    {.hi = 0xed4f301ed9942b84, .lo = 0x600d2db6a64bfb12, .ex = 0, .sgn = 0x0},
    {.hi = 0xefe4b99bdcdaf5cb, .lo = 0x46561cf6948db913, .ex = 0, .sgn = 0x0},
    {.hi = 0xf281773c59ffb139, .lo = 0xe8980a9cc8f47a4b, .ex = 0, .sgn = 0x0},
    {.hi = 0xf5257d152486cc2c, .lo = 0x7b9d0c7aed980fc3, .ex = 0, .sgn = 0x0},
    {.hi = 0xf7d0df730ad13bb8, .lo = 0xfe90d496d60fb6eb, .ex = 0, .sgn = 0x0},
    {.hi = 0xfa83b2db722a033a, .lo = 0x7c25bb14315d7fcd, .ex = 0, .sgn = 0x0},
    {.hi = 0xfd3e0c0cf486c174, .lo = 0x853f3a5931e0ee03, .ex = 0, .sgn = 0x0},
};

/* for 0 <= i < 64, T2_2[i] is a 128-bit nearest approximation of 2^(i/2^12),
   with error bounded by 2^-128 (both absolutely and relatively).
   Table generated by output_T2_2() from the accompanying dint.sage file. */
static const dint64_t T2_2[] = {
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0},
    {.hi = 0x80058baf7fee3b5d, .lo = 0x1c718b38e549cb93, .ex = 0, .sgn = 0x0},
    {.hi = 0x800b179c82028fd0, .lo = 0x945e54e2ae18f2f0, .ex = 0, .sgn = 0x0},
    {.hi = 0x8010a3c708e73282, .lo = 0x2b96d62d51c15a07, .ex = 0, .sgn = 0x0},
    {.hi = 0x8016302f17467628, .lo = 0x3690dfe44d11d008, .ex = 0, .sgn = 0x0},
    {.hi = 0x801bbcd4afcacb08, .lo = 0xe23a986bd3e626f0, .ex = 0, .sgn = 0x0},
    {.hi = 0x802149b7d51ebefb, .lo = 0x7bdbadbc888aeb29, .ex = 0, .sgn = 0x0},
    {.hi = 0x8026d6d889ecfd69, .lo = 0xb904bbfb40d3a2b7, .ex = 0, .sgn = 0x0},
    {.hi = 0x802c6436d0e04f50, .lo = 0xff8ce94a6797b3ce, .ex = 0, .sgn = 0x0},
    {.hi = 0x8031f1d2aca39b43, .lo = 0xad9db772901d96b6, .ex = 0, .sgn = 0x0},
    {.hi = 0x80377fac1fe1e56a, .lo = 0x61cd0bffd7cfc683, .ex = 0, .sgn = 0x0},
    {.hi = 0x803d0dc32d464f85, .lo = 0x43456f71b96affd4, .ex = 0, .sgn = 0x0},
    {.hi = 0x80429c17d77c18ed, .lo = 0x49fc841afba9c3c6, .ex = 0, .sgn = 0x0},
    {.hi = 0x80482aaa212e9e95, .lo = 0x86f7b54f6c45c85e, .ex = 0, .sgn = 0x0},
    {.hi = 0x804db97a0d095b0c, .lo = 0x6c9f1f7d1efcfe68, .ex = 0, .sgn = 0x0},
    {.hi = 0x805348879db7e67d, .lo = 0x171eb1ceef1d1f28, .ex = 0, .sgn = 0x0},
    {.hi = 0x8058d7d2d5e5f6b0, .lo = 0x94d589f608ee4aa2, .ex = 0, .sgn = 0x0},
    {.hi = 0x805e675bb83f5f0f, .lo = 0x2ed38ab8472b2144, .ex = 0, .sgn = 0x0},
    {.hi = 0x8063f722477010a1, .lo = 0xb1652de1378af1a1, .ex = 0, .sgn = 0x0},
    {.hi = 0x8069872686241a12, .lo = 0xb4ad9233a0390cad, .ex = 0, .sgn = 0x0},
    {.hi = 0x806f17687707a7af, .lo = 0xe54ec5f966eb1872, .ex = 0, .sgn = 0x0},
    {.hi = 0x8074a7e81cc7036b, .lo = 0x4d204ecfc11f4aab, .ex = 0, .sgn = 0x0},
    {.hi = 0x807a38a57a0e94dc, .lo = 0x9bf3ef4d9be2d1e4, .ex = 0, .sgn = 0x0},
    {.hi = 0x807fc9a0918ae142, .lo = 0x7068ab2230585d13, .ex = 0, .sgn = 0x0},
    {.hi = 0x80855ad965e88b83, .lo = 0xa0cc0a49c10ea66b, .ex = 0, .sgn = 0x0},
    {.hi = 0x808aec4ff9d45430, .lo = 0x84099bf6830f2768, .ex = 0, .sgn = 0x0},
    {.hi = 0x80907e044ffb1984, .lo = 0x3aa8b9cbbc65a8ab, .ex = 0, .sgn = 0x0},
    {.hi = 0x80960ff66b09d765, .lo = 0xf7d88c0928ba3947, .ex = 0, .sgn = 0x0},
    {.hi = 0x809ba2264dada76a, .lo = 0x4a8a4f44bb703db6, .ex = 0, .sgn = 0x0},
    {.hi = 0x80a13493fa93c0d4, .lo = 0x6699dc50dd96b774, .ex = 0, .sgn = 0x0},
    {.hi = 0x80a6c73f74697897, .lo = 0x6e0472ed4ccfa2e0, .ex = 0, .sgn = 0x0},
    {.hi = 0x80ac5a28bddc4157, .lo = 0xba2dc7e0c72e51ba, .ex = 0, .sgn = 0x0},
    {.hi = 0x80b1ed4fd999ab6c, .lo = 0x25335719b6e6fd20, .ex = 0, .sgn = 0x0},
    {.hi = 0x80b780b4ca4f64df, .lo = 0x534dfa7417846aa4, .ex = 0, .sgn = 0x0},
    {.hi = 0x80bd145792ab3970, .lo = 0xfc41c5c2d5336ccc, .ex = 0, .sgn = 0x0},
    {.hi = 0x80c2a838355b1297, .lo = 0x34dc28baed8f3fde, .ex = 0, .sgn = 0x0},
    {.hi = 0x80c83c56b50cf77f, .lo = 0xb880575ea03548c1, .ex = 0, .sgn = 0x0},
    {.hi = 0x80cdd0b3146f0d11, .lo = 0x32c1f98704428c71, .ex = 0, .sgn = 0x0},
    {.hi = 0x80d3654d562f95ec, .lo = 0x890e222a5eb95372, .ex = 0, .sgn = 0x0},
    {.hi = 0x80d8fa257cfcf26e, .lo = 0x24628efd9ca9d59b, .ex = 0, .sgn = 0x0},
    {.hi = 0x80de8f3b8b85a0af, .lo = 0x3b13310f5ad57fb1, .ex = 0, .sgn = 0x0},
    {.hi = 0x80e4248f84783c87, .lo = 0x1a9dfefaeb616564, .ex = 0, .sgn = 0x0},
    {.hi = 0x80e9ba216a837f8c, .lo = 0x718d1151d109bf98, .ex = 0, .sgn = 0x0},
    {.hi = 0x80ef4ff140564116, .lo = 0x996709da2e25f04c, .ex = 0, .sgn = 0x0},
    {.hi = 0x80f4e5ff089f763e, .lo = 0xe0adc640acaa6b0b, .ex = 0, .sgn = 0x0},
    {.hi = 0x80fa7c4ac60e31e1, .lo = 0xd4eb5edc6b341283, .ex = 0, .sgn = 0x0},
    {.hi = 0x810012d47b51a4a0, .lo = 0x8ccd7223820719e3, .ex = 0, .sgn = 0x0},
    {.hi = 0x8105a99c2b191ce1, .lo = 0xf24ebd6eb9ca4292, .ex = 0, .sgn = 0x0},
    {.hi = 0x810b40a1d81406d4, .lo = 0xcef03ab14a66550, .ex = 0, .sgn = 0x0},
    {.hi = 0x8110d7e584f1ec6d, .lo = 0x4bf94297d1519822, .ex = 0, .sgn = 0x0},
    {.hi = 0x81166f673462756d, .lo = 0xd0d8372f966cf15e, .ex = 0, .sgn = 0x0},
    {.hi = 0x811c0726e9156760, .lo = 0xb97931db7b7be2ec, .ex = 0, .sgn = 0x0},
    {.hi = 0x81219f24a5baa59d, .lo = 0x6abd3b0eab9c7048, .ex = 0, .sgn = 0x0},
    {.hi = 0x812737606d023148, .lo = 0xdaf888e96508151a, .ex = 0, .sgn = 0x0},
    {.hi = 0x812ccfda419c2956, .lo = 0xdc8046821f46122e, .ex = 0, .sgn = 0x0},
    {.hi = 0x813268922638ca8b, .lo = 0x6846ad73a8d9027f, .ex = 0, .sgn = 0x0},
    {.hi = 0x813801881d886f7b, .lo = 0xe885724f14131287, .ex = 0, .sgn = 0x0},
    {.hi = 0x813d9abc2a3b9090, .lo = 0x83768490519df895, .ex = 0, .sgn = 0x0},
    {.hi = 0x8143342e4f02c405, .lo = 0x661b22b45e25de18, .ex = 0, .sgn = 0x0},
    {.hi = 0x8148cdde8e8ebdec, .lo = 0xf11430fef78c6ee, .ex = 0, .sgn = 0x0},
    {.hi = 0x814e67cceb90502c, .lo = 0x99775205944eadc4, .ex = 0, .sgn = 0x0},
    {.hi = 0x815401f968b86a87, .lo = 0x7de463a40d18261, .ex = 0, .sgn = 0x0},
    {.hi = 0x81599c6408b81a94, .lo = 0x8f4a0b6748df7960, .ex = 0, .sgn = 0x0},
    {.hi = 0x815f370cce408bc8, .lo = 0xe2404468cfe5ab9f, .ex = 0, .sgn = 0x0},
};

#ifdef CORE_MATH_POW
/* The following is a degree-9 polynomial generated by Sollya, with zero
   constant coefficient, which approximates log(1+z) for |z| < 0.0001221,
   see sollya/approximations_r2.sollya.
   The coefficients of largest degree are first.
   The relative error is bounded by 2^-128.316.
   Table generated by output_P2() in the accompanying dint.sage file.
*/
static const dint64_t P_2[] = {
    {.hi = 0xe38e3954a09e560e, .lo = 0x0, .ex = -4, .sgn = 0x0},
    {.hi = 0x800000399d09d767, .lo = 0x0, .ex = -3, .sgn = 0x1},
    {.hi = 0x9249249249248676, .lo = 0x0, .ex = -3, .sgn = 0x0},
    {.hi = 0xaaaaaaaaaaaa9fdd, .lo = 0x0, .ex = -3, .sgn = 0x1},
    {.hi = 0xcccccccccccccccc, .lo = 0xcccdc5fe0ef93b8d, .ex = -3, .sgn = 0x0},
    {.hi = 0x8000000000000000, .lo = 0x600135b960d8, .ex = -2, .sgn = 0x1},
    {.hi = 0xaaaaaaaaaaaaaaaa, .lo = 0xaaaaaaaaaaa77b5e, .ex = -2, .sgn = 0x0},
    {.hi = 0xffffffffffffffff, .lo = 0xfffffffffffe33ca, .ex = -2, .sgn = 0x1},
    {.hi = 0x8000000000000000, .lo = 0x0, .ex = 0, .sgn = 0x0},
};
#endif

/* The following is a degree-7 polynomial generated by Sollya,
   which approximates exp(z) for |z| < 0.00016923,
   see sollya/approximations_r2.sollya.
   The coefficients of largest degree are first.
   The relative error is bounded by 2^-122.415.
   Table generated by output_Q2() in the accompanying dint.sage file.
*/
static const dint64_t Q_2[] = {
    {.hi = 0xd00d00cd98416862, .lo = 0x0, .ex = -13, .sgn = 0x0},
    {.hi = 0xb60b60b932146a54, .lo = 0x0, .ex = -10, .sgn = 0x0},
    {.hi = 0x8888888888888897, .lo = 0x0, .ex = -7, .sgn = 0x0},
    {.hi = 0xaaaaaaaaaaaaaaa3, .lo = 0x0, .ex = -5, .sgn = 0x0},
    {.hi = 0xaaaaaaaaaaaaaaaa, .lo = 0xaaaaaa6a1e0776ae, .ex = -3, .sgn = 0x0},
    {.hi = 0x8000000000000000, .lo = 0xc06f3cd29, .ex = -1, .sgn = 0x0},
    {.hi = 0x8000000000000000, .lo = 0x88, .ex = 0, .sgn = 0x0},
    {.hi = 0xffffffffffffffff, .lo = 0xffffffffffffffd0, .ex = -1, .sgn = 0x0},
};

#endif
