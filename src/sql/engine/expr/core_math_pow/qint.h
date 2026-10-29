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
  This file contains type definition and functions to manipulate the qint64_t
  data type used in the third iteration of Ziv's method. It is composed of four
  uint64_t values for the mantissa and the exponent is represented by a signed
  int64_t value.
*/

#ifndef QINT_H
#define QINT_H

#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

/*
  Type definition
*/

#ifndef UINT128_T
#define UINT128_T

#if (defined(__clang__) && __clang_major__ >= 14) || (defined(__GNUC__) && __GNUC__ >= 14)
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

// Add two 128-bit integers and return 1 if a carry occured
static inline int addu_128 (uint128_t a, uint128_t b, uint128_t *r) {
  r->r = a.r + b.r;
  // Return the carry
  return r->r < a.r;
}

// Subtract two 128-bit integers and return 1 if a borrow occured
static inline int subu_128 (uint128_t a, uint128_t b, uint128_t *r) {
  r->r = a.r - b.r;
  // Return the borrow
  return r->r > a.r;
}

// Compare two 64-bit signed integers
// Return +1 if a > b, 0 if a=b, -1 if a < b
static inline signed char cmp (int64_t a, int64_t b) {
  return (a > b) - (a < b);
}

// Compare two 64-bit unsigned integers
// Return +1 if a > b, 0 if a=b, -1 if a < b
static inline signed char cmpu (uint64_t a, uint64_t b) {
  return (a > b) - (a < b);
}

#endif

// Add two 128-bit integers and return 1 if a carry occured
static inline int addu128 (u128 a, u128 b, u128 *r) {
  *r = a + b;
  // Return the carry
  return *r < a;
}

// Subtract two 128-bit integers and return 1 if a borrow occured
static inline int subu128 (u128 a, u128 b, u128 *r) {
  *r = a - b;
  // Return the borrow
  return *r > a;
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef union {
  /* Use a little-endian representation.
     FIXME: adapt for big-endian processors. */
  struct {
    u128 rl;
    u128 rh;
    int64_t _ex;
    uint64_t _sgn;
  };
  struct {
    uint64_t ll; /* lower low part */
    uint64_t lh; /* upper low part */
    uint64_t hl; /* lower high part */
    uint64_t hh; /* upper high part */
    int64_t ex;
    uint64_t sgn;
  };
} qint64_t;
#else
typedef union {
  struct {
    u128 rl;
    u128 rh;
    int64_t _ex;
    uint64_t _sgn;
  };
  struct {
    uint64_t lh; /* upper low part */
    uint64_t ll; /* lower low part */
    uint64_t hh; /* upper high part */
    uint64_t hl; /* lower high part */
    int64_t ex;
    uint64_t sgn;
  };
} qint64_t;
#endif

/*
  Constants
*/

// this encodes 1 (exact)
static const qint64_t ONE_Q = {.hh = 0x8000000000000000,
                               .hl = 0x0,
                               .lh = 0x0,
                               .ll = 0x0,
                               .ex = 0,
                               .sgn = 0x0};

// this encodes -1 (exact)
static const qint64_t M_ONE_Q = {.hh = 0x8000000000000000,
                                 .hl = 0x0,
                                 .lh = 0x0,
                                 .ll = 0x0,
                                 .ex = 0,
                                 .sgn = 0x1};

// LOG2_Q approximates log(2), with absolute error < 2^-256.14
static const qint64_t LOG2_Q = {.hh = 0xb17217f7d1cf79ab,
                                .hl = 0xc9e3b39803f2f6af,
                                .lh = 0x40f343267298b62d,
                                .ll = 0x8a0d175b8baafa2b,
                                .ex = -1,
                                .sgn = 0x0};

// LOG2_INV_Q approximates 2^12/log(2), with absolute error < 2^-52.96
static const qint64_t LOG2_INV_Q = {.hh = 0xb8aa3b295c17f0bc,
                                    .hl = 0x0,
                                    .lh = 0x0,
                                    .ll = 0x0,
                                    .ex = 12,
                                    .sgn = 0x0};

// this encodes 0 (exact)
static const qint64_t ZERO_Q = {
    .hh = 0x0, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0};

/*
  Base functions
*/

// Copy a qint64_t value
static inline void cp_qint (qint64_t *r, const qint64_t *a) {
  r->ex = a->ex;
  r->rh = a->rh;
  r->rl = a->rl;
  r->sgn = a->sgn;
}

/* Compare the absolute values of a and b:
   return +1 if |a| > |b|, 0 if |a| = |b|, -1 if |a| < |b| */
static inline signed char cmp_qint(const qint64_t *a, const qint64_t *b) {
  return cmp(a->ex, b->ex)  ? cmp(a->ex, b->ex)
    : cmpu128(a->rh, b->rh) ? cmpu128(a->rh, b->rh)
    : cmpu128(a->rl, b->rl);
}

/* same as cmp_qint, but only compare the upper 2 limbs */
static inline signed char cmp_qint_22(const qint64_t *a, const qint64_t *b) {
  return cmp(a->ex, b->ex)  ? cmp(a->ex, b->ex)
    : cmpu128(a->rh, b->rh);
}

/* Add two qint64_t values, with error bounded by 2 ulps (ulp_256).
   If Sterbenz theorem applies, i.e., a and b are of opposite signs
   with |a|/2 <= |b| <= |a|, then the operation is exact. */
static inline void
add_qint (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  if (a->rh == 0 && a->rl == 0) {
    cp_qint (r, b); // exact
    return;
  }

    if (b->rh == 0 && b->rl == 0) {
    cp_qint (r, a); // exact
    return;
  }

  /* compare the absolute values of a and b */
  switch (cmp_qint (a, b)) {
  case 0: /* |a| = |b| */
    if (a->sgn ^ b->sgn) { /* signs differ */
      cp_qint (r, &ZERO_Q); // exact
      return;
    }

    cp_qint (r, a);
    r->ex++; // exact
    return;

  case -1: /* |a| < |b| */
    add_qint (r, b, a);
    return;
  }

  // From now on, |A| > |B|

  u128 ah = a->rh, al = a->rl, bh = b->rh, bl = b->rl;

  int64_t m_ex = a->ex;
  int64_t k = a->ex - b->ex;

  if (k > 0) {
    if (k >= 128) {
      bl = (k < 256) ? bh >> (k - 128) : 0;
      bh = 0;
    } else { /* 1 <= k <= 127 */
      bl = (bl >> k) | (bh << (128 - k));
      bh = bh >> k;
    }
  }

  /* now we have to add (ah,al) + (bh,bl), with error <= 1 ulp
     corresponding to the ignored part of (bh,bl) */

  unsigned char sgn = a->sgn;
  uint64_t ex;
  u128 ch, cl;

  r->ex = m_ex;

  if (a->sgn ^ b->sgn) { // subtraction case
    /* a and b have different signs: C = A + (-B) */
    ch = ah - bh;

    if (subu128 (al, bl, &cl))
      ch --;
    /* we cannot have C=0 since |A| > |B| */
    uint64_t chh = ch >> 64, clh = cl >> 64;
    ex =
      chh ? __builtin_clzll(chh)
      : 64 + (ch ? __builtin_clzll(ch)
              : 64 + (clh ? __builtin_clzll(clh)
                      : 64 + __builtin_clzll(cl)));
    /* ex < 256 since |A| > |B| */

    /* If ex=0 or ex=1, the rounding error is bounded by 2 ulps. */
    if (ex > 0)
      {
        /* shift A by ex bits to the left, and B by ex-k bits to the left */
        if (ex >= 128)
          {
            ah = al << (ex - 128);
            al = 0;
          }
        else /* 1 <= ex < 128 */
          {
            ah = (ah << ex) | (al >> (128 - ex));
            al = al << ex;
          }
        int sh = ex - k;
        bh = b->rh;
        bl = b->rl;
        if (sh >= 0) {
          if (sh >= 128) {
            bh = bl << (sh - 128);
            bl = 0;
          }
          else if (sh > 0) { /* 1 <= sh < 128 */
            bh = (bh << sh) | (bl >> (128 - sh));
            bl = bl << sh;
          }
        }
        else { /* sh < 0: shift b by -sh bits to the right */
          int j = -sh;
          if (j >= 128) {
            bl = bh >> (j - 128);
            bh = 0;
          }
          else { /* 0 < j < 128 (j cannot be 0 since sh < 0) */
            bl = (bh << (128 - j)) | (bl >> j);
            bh = bh >> j;
          }
        }
        r->ex -= ex;
        ch = ah - bh;

        if (subu128 (al, bl, &cl))
          ch --;
        /* we cannot have C=0 since |A| > |B| */
        chh = ch >> 64;
        clh = cl >> 64;
        ex =
          chh ? __builtin_clzll(chh)
          : 64 + (ch ? __builtin_clzll(ch)
                  : 64 + (clh ? __builtin_clzll(clh)
                          : 64 + __builtin_clzll(cl)));
      }
    if (ex) {
      ch = (ch << ex) | (cl >> (128 - ex));
      cl = cl << ex;
    }
    r->ex -= ex;
    /* We distinguish three cases according to the first value of ex:
       If ex=0, the error is bounded by 1 ulp (ignored part of B).
       If ex=1, the error is bounded by 2 ulps (ignored part of B
         multiplied by 2).
       In the case ex>1, the error is bounded by 1 ulp (truncated part of B),
       which might be multiplied by 2 if the final value of ex is 1.
    */
  } else { // addition case
    char cy = addu128 (ah, bh, &ch);

    if (addu128 (al, bl, &cl))
      cy += !(++ch);

    /* 0 <= cy <= 1 */

    if (cy) { // carry in the 256-bit addition
      cl = (ch << 127) | (cl >> 1);
      ch = ((u128)1 << 127) | (ch >> 1);
      r->ex ++;
    }
    /* In the addition case, the rounding error is bounded by 1 ulp. */
  }

  r->sgn = sgn;
  r->rh = ch;
  r->rl = cl;
}

/* same as add_qint, but only considers the upper 2 limbs of a and b,
   with rounding error < 2 ulps(128) */
static inline void
add_qint_22 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  if (a->rh == 0) {
    cp_qint (r, b); // exact
    return;
  }

    if (b->rh == 0) {
    cp_qint (r, a); // exact
    return;
  }

  /* compare the absolute values of a and b */
  switch (cmp_qint_22 (a, b)) {
  case 0: /* |a| = |b| */
    if (a->sgn ^ b->sgn) { /* signs differ */
      cp_qint (r, &ZERO_Q); // exact
      return;
    }

    cp_qint (r, a);
    r->ex++; // exact
    return;

  case -1: /* |a| < |b| */
    add_qint_22 (r, b, a);
    return;
  }

  // From now on, |A| > |B|

  u128 ah = a->rh, bh = b->rh;

  int64_t m_ex = a->ex;
  uint64_t k = a->ex - b->ex;

  if (k > 0)
    bh = (k >= 128) ? 0 : bh >> k;

  /* now we have to add ah + bh, with error <= 1 ulp
     corresponding to the ignored part of bh */

  unsigned char sgn = a->sgn;
  uint64_t ex;
  u128 ch;

  r->ex = m_ex;

  if (a->sgn ^ b->sgn) { // subtraction case
    /* a and b have different signs: C = A + (-B) */
    ch = ah - bh;

    /* we cannot have ch=0 since |A| > |B| */
    uint64_t chh = ch >> 64;
    ex = chh ? __builtin_clzll(chh) : 64 + __builtin_clzll(ch);

    /* ex < 128 since |A| > |B| */

    if (ex > 0)
      {
        /* shift A and B by ex bits to the left */
        ah = ah << ex;
        /* for B, we have to shift by k bits to the right and ex to the left */
        if (ex >= k)
          bh = b->rh << (ex - k); // since ex < 128, the shift is well defined
        else
          bh = b->rh >> (k - ex);
        /* since k < 128 (otherwise bh=0 and ch=ah thus ex=0), this shift is
           also well defined */
        r->ex -= ex;
        ch = ah - bh;

        /* we cannot have C=0 since |A| > |B| */
        chh = ch >> 64;
        ex = chh ? __builtin_clzll(chh) : 64 + __builtin_clzll(ch);
        /* rounding error is bounded by 1 ulp(128) */
      }
    ch = ch << ex;
    /* if ex=1, the rounding error is multiplied by 2, thus < 2 ulp(128) */
    r->ex -= ex;
  } else { // addition case
    char cy = addu128 (ah, bh, &ch);

    if (cy) { // carry in the 128-bit addition
      ch = ((u128) 1 << 127) | (ch >> 1);
      r->ex ++;
    }
    /* In the addition case, the rounding error is bounded by 1 ulp(128) */
  }

  r->sgn = sgn;
  r->rh = ch;
  r->rl = 0;
}

// Multiply two dint64_t numbers, with error < 14 ulps
static inline void
mul_qint (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 r33 = (u128)(a->hh) * (u128)(b->hh);

  u128 r32 = (u128)(a->hh) * (u128)(b->hl);
  u128 r23 = (u128)(a->hl) * (u128)(b->hh);

  u128 r31 = (u128)(a->hh) * (u128)(b->lh);
  u128 r13 = (u128)(a->lh) * (u128)(b->hh);
  u128 r22 = (u128)(a->hl) * (u128)(b->hl);

  u128 r30 = (u128)(a->hh) * (u128)(b->ll);
  u128 r03 = (u128)(a->ll) * (u128)(b->hh);
  u128 r21 = (u128)(a->hl) * (u128)(b->lh);
  u128 r12 = (u128)(a->lh) * (u128)(b->hl);

  u128 t6, t5, t4, t3;
  u128 c5, c4;

  t3 = (r12 >> 64) + (r21 >> 64) + (r03 >> 64) + (r30 >> 64);
  /* no overflow since each term is < 2^64, thus the sum < 2^66 */

  /* t3 is the sum of the terms of "degree" 3 divided by 2^64 */

  c4 = addu128 (r22, t3, &t4);
  c4 += addu128 (r13, t4, &t4);
  c4 += addu128 (r31, t4, &t4);

  /* (c4:1,t4:128) is the sum of the terms of "degree" 3 and 4 */

  c5 = addu128 (r23, t4 >> 64, &t5);
  c5 += addu128 (r32, t5, &t5);

  /* (c5:1,t5:128,low(t4):64) is the sum of the terms of "degree" 3 to 5 */

  t6 = r33 + ((c5 << 64) | (t5 >> 64)) + c4;

  /* (t6:128,low(t5):64,low(t4):64) is the sum of the terms of "degree" 3-6 */

  /* No carry can happen since the full product of the significands is
     bounded by 2^512.
     The approximated sum is:
     t6 (128 bits) + low(t5) (64 bits) + low(t4) (64 bits) + low(t3) (64 bits)
     with error bounded by:
     * 3*(B-1)^2/B^2 ulp for the neglected terms of "degree" 2: r20 + r11 + r02
     * 2*(B-1)^2/B^3 ulp for the neglected terms of "degree" 1: r10 + r01
     * 1*(B-1)^2/B^4 ulp for the neglected term of "degree" 0: r00
     * 1 ulp for each of the neglected low parts of r12, r21, r03 and r30
       thus 4 ulps in total
     The sum of the first three terms is less than 3, thus bounded by 3 ulps.
     This yields an error bound of 7 ulps so far.
  */

  uint64_t ex = !(t6 >> 127);

  t5 = (t5 << 64) | (t4 & (u128) 0xffffffffffffffff);
  if (ex) { /* ex=1 */
    r->rh = (t6 << 1) | (t5 >> 127);
    r->rl = t5 << 1;
    /* the previous rounding error is multiplied by 2, thus < 14 ulps now */
  }
  else { /* ex=0 */
    r->rh = t6;
    r->rl = t5;
    /* error < 7 ulps */
  }

  r->ex = a->ex + b->ex + 1 - ex;

  r->sgn = a->sgn ^ b->sgn;
}

/* same as mul_qint, but considering only the upper 3 limbs from a and b,
   and with error < 6 ulps */
static inline void
mul_qint_33 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 r33 = (u128)(a->hh) * (u128)(b->hh);

  u128 r32 = (u128)(a->hh) * (u128)(b->hl);
  u128 r23 = (u128)(a->hl) * (u128)(b->hh);

  u128 r31 = (u128)(a->hh) * (u128)(b->lh);
  u128 r13 = (u128)(a->lh) * (u128)(b->hh);
  u128 r22 = (u128)(a->hl) * (u128)(b->hl);

  u128 r21 = (u128)(a->hl) * (u128)(b->lh);
  u128 r12 = (u128)(a->lh) * (u128)(b->hl);

  u128 t6, t5, t4, t3;
  u128 c5, c4;

  t3 = (r12 >> 64) + (r21 >> 64);
  /* no overflow since each term is < 2^64, thus the sum < 2*2^64 */

  /* t3 is the sum of the terms of "degree" 3 divided by 2^64 */

  c4 = addu128 (r22, t3, &t4);
  c4 += addu128 (r13, t4, &t4);
  c4 += addu128 (r31, t4, &t4);

  /* (c4:1,t4:128) is the sum of the terms of "degree" 3 and 4 */

  c5 = addu128 (r23, t4 >> 64, &t5);
  c5 += addu128 (r32, t5, &t5);

  /* (c5:1,t5:128,low(t4):64) is the sum of the terms of "degree" 3 to 5 */

  t6 = r33 + ((c5 << 64) | (t5 >> 64)) + c4;

  /* (t6:128,low(t5):64,low(t4):64) is the sum of the terms of "degree" 3-6 */

  /* No carry can happen since the full product of the significands is
     bounded by 2^512.
     The approximated sum is:
     t6 (128 bits) + low(t5) (64 bits) + low(t4) (64 bits) + low(t3) (64 bits)
     with error bounded by:
     * 1 ulp for the neglected term of "degree" 2: r11
     * 1 ulp for each of the neglected low parts of r12, r21
       thus 2 ulps in total
     This yields an error bound of 3 ulps so far.
  */

  uint64_t ex = !(t6 >> 127);

  t5 = (t5 << 64) | (t4 & (u128) 0xffffffffffffffff);
  if (ex) { /* ex=1 */
    r->rh = (t6 << 1) | (t5 >> 127);
    r->rl = t5 << 1;
    /* the previous rounding error is multiplied by 2, thus < 6 ulps now */
  }
  else { /* ex=0 */
    r->rh = t6;
    r->rl = t5;
    /* error < 3 ulps */
  }

  r->ex = a->ex + b->ex + 1 - ex;

  r->sgn = a->sgn ^ b->sgn;
}

/* same as mul_qint, but considering only the upper limb from b,
   and with error < 2 ulps */
static inline void
mul_qint_41 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 r33 = (u128)(a->hh) * (u128)(b->hh);

  u128 r23 = (u128)(a->hl) * (u128)(b->hh);

  u128 r13 = (u128)(a->lh) * (u128)(b->hh);

  u128 r03 = (u128)(a->ll) * (u128)(b->hh);

  u128 t6, t5, t4, t3;
  u128 c5, c4;

  t3 = r03 >> 64;

  /* t3 is the term of "degree" 3 divided by 2^64 */

  c4 = addu128 (r13, t3, &t4);

  /* (c4:1,t4:128) is the sum of the terms of "degree" 3 and 4 */

  c5 = addu128 (r23, t4 >> 64, &t5);

  /* (c5:1,t5:128,low(t4):64) is the sum of the terms of "degree" 3 to 5 */

  t6 = r33 + ((c5 << 64) | (t5 >> 64)) + c4;

  /* (t6:128,low(t5):64,low(t4):64) is the sum of the terms of "degree" 3-6 */

  /* No carry can happen since the full product of the significands is
     bounded by 2^512.
     The approximated sum is:
     t6 (128 bits) + low(t5) (64 bits) + low(t4) (64 bits) + low(t3) (64 bits)
     with error bounded by 1 ulp for the neglected low part of r03.
  */

  uint64_t ex = !(t6 >> 127);

  t5 = (t5 << 64) | (t4 & (u128) 0xffffffffffffffff);
  if (ex) { /* ex=1 */
    r->rh = (t6 << 1) | (t5 >> 127);
    r->rl = t5 << 1;
    /* the previous rounding error is multiplied by 2, thus < 2 ulps now */
  }
  else { /* ex=0 */
    r->rh = t6;
    r->rl = t5;
    /* error < 1 ulp */
  }

  r->ex = a->ex + b->ex + 1 - ex;

  r->sgn = a->sgn ^ b->sgn;
}

/* same as mul_qint, but considering only the 3 upper limbs from a,
   and the upper limb from b, with no error (exact product) */
static inline void
mul_qint_31 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 r33 = (u128)(a->hh) * (u128)(b->hh);
  u128 r23 = (u128)(a->hl) * (u128)(b->hh);
  u128 r13 = (u128)(a->lh) * (u128)(b->hh);

  u128 t6, t5, t4;
  u128 c5;

  t4 = r13;

  /* t4 is the only term of "degree" 4 */

  c5 = addu128 (r23, t4 >> 64, &t5);

  /* (c5:1,t5:128,low(t4):64) is the sum of the terms of "degree" 4 to 5 */

  t6 = r33 + ((c5 << 64) | (t5 >> 64));

  /* (t6:128,low(t5):64,low(t4):64) is the sum of the terms of "degree" 4-6 */

  /* No carry can happen since the full product of the significands is
     bounded by 2^512.
     The approximated sum is:
     t6 (128 bits) + low(t5) (64 bits) + low(t4) (64 bits)
     with no error.
  */

  uint64_t ex = !(t6 >> 127);

  t5 = (t5 << 64) | (t4 & (u128) 0xffffffffffffffff);
  if (ex) { /* ex=1 */
    r->rh = (t6 << 1) | (t5 >> 127);
    r->rl = t5 << 1;
    /* the previous rounding error is multiplied by 2, thus < 2 ulps now */
  }
  else { /* ex=0 */
    r->rh = t6;
    r->rl = t5;
    /* error < 1 ulp */
  }

  r->ex = a->ex + b->ex + 1 - ex;

  r->sgn = a->sgn ^ b->sgn;
}

/* same as mul_qint, but considering only the 2 upper limbs from a and b,
   with no error (exact product) */
static inline void
mul_qint_22 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 r33 = (u128)(a->hh) * (u128)(b->hh);

  u128 r32 = (u128)(a->hh) * (u128)(b->hl);
  u128 r23 = (u128)(a->hl) * (u128)(b->hh);

  u128 r22 = (u128)(a->hl) * (u128)(b->hl);

  u128 t6, t5, t4;
  u128 c5;

  t4 = r22;

  c5 = addu128 (r23, t4 >> 64, &t5);
  c5 += addu128 (r32, t5, &t5);

  /* (c5:1,t5:128,low(t4):64) is the sum of the terms of "degree" 3 to 5 */

  t6 = r33 + ((c5 << 64) | (t5 >> 64));

  /* (t6:128,low(t5):64,low(t4):64) is the sum of the terms of "degree" 3-6 */

  /* No carry can happen since the full product of the significands is
     bounded by 2^512.
     The exact sum is:
     t6 (128 bits) + low(t5) (64 bits) + low(t4) (64 bits)
  */

  uint64_t ex = !(t6 >> 127);

  t5 = (t5 << 64) | (t4 & (u128) 0xffffffffffffffff);
  if (ex) { /* ex=1 */
    r->rh = (t6 << 1) | (t5 >> 127);
    r->rl = t5 << 1;
  }
  else { /* ex=0 */
    r->rh = t6;
    r->rl = t5;
  }

  r->ex = a->ex + b->ex + 1 - ex;

  r->sgn = a->sgn ^ b->sgn;
}

/* same as mul_qint, but considering only the 2 upper limbs from a, and the
   upper limb of b, with no error (exact product) */
static inline void
mul_qint_21 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 r33 = (u128)(a->hh) * (u128)(b->hh);
  u128 r23 = (u128)(a->hl) * (u128)(b->hh);

  u128 t6 = r33 + (r23 >> 64);

  /* No carry can happen since the full product of the significands is
     bounded by 2^512.
  */

  uint64_t ex = !(t6 >> 127);

  u128 t5 = r23 << 64;
  if (ex) { /* ex=1 */
    r->rh = (t6 << 1) | (t5 >> 127);
    r->rl = t5 << 1;
  }
  else { /* ex=0 */
    r->rh = t6;
    r->rl = t5;
  }

  r->ex = a->ex + b->ex + 1 - ex;

  r->sgn = a->sgn ^ b->sgn;
}

/* same as mul_qint, but considering only the upper limb from a and b,
   with no error (exact product) */
static inline void
mul_qint_11 (qint64_t *r, const qint64_t *a, const qint64_t *b) {
  u128 t6 = (u128)(a->hh) * (u128)(b->hh);
  uint64_t ex = !(t6 >> 127);

  /* ex can be 0 or 1 */

  r->rh = t6 << ex;
  r->rl = 0;
  r->ex = a->ex + b->ex + 1 - ex;
  r->sgn = a->sgn ^ b->sgn;
}

// Multiply an integer with a qint64_t variable, with error < 2 ulps
static inline void mul_qint_2 (qint64_t *r, int64_t b, const qint64_t *a) {
  if (!b) {
    cp_qint (r, &ZERO_Q); // exact
    return;
  }

  uint64_t c = b < 0 ? -b : b;
  if (c == 1) {
    cp_qint (r, a); // exact
    r->sgn = (b < 0) ^ a->sgn;

    return;
  }

  r->sgn = (b < 0) ^ a->sgn;
  r->ex = a->ex + 64;

  /* scale c so that 2^63 <= c < 2^64 */
  int k = __builtin_clzll (c);
  c = c << k;
  r->ex -= k;

  u128 t3 = (u128) a->hh * (u128) c;
  u128 t2 = (u128) a->hl * (u128) c;
  u128 t1 = (u128) a->lh * (u128) c;
  u128 t0 = (u128) a->ll * (u128) c;

  u128 cy;
  u128 t = t0 >> 64;

  /* t:64 is the term of degree 0 (divided by 2^64) */

  cy = addu128 (t, t1, &t1);
  /* (cy:1,t1:128) is the sum of the terms of degree 0 and 1 */

  t = ((u128) cy << 64) | (t1 >> 64);
  cy = addu128 (t, t2, &t2);
  /* (cy:1,t2:128,low(t1):64) is the sum of the terms of degree 0 to 2 */

  t3 += (((u128) cy << 64) | (t2 >> 64));
  /* (t3,low(t2):64,low(t1):64) is the sum of the terms of degree 0 to 3 */

  uint32_t ex = __builtin_clzll (t3 >> 64);

  t2 = (t2 << 64) | (t1 & (u128) 0xffffffffffffffff);

  /* ex is 0 or 1 because a and c are normalized (2^63 <= a->hh, c < 2^64) */

  /* we only ignore the low part of t0 which contributes less than 1 ulp */

  if (ex)
    {
      r->rh = (t3 << 1) | (t2 >> 127);
      r->rl = t2 << 1;
      /* the error is scaled by 2, thus less than 2 ulps */
      r->ex --;
    }
  else
    {
      r->rh = t3;
      r->rl = t2;
      /* error less than 1 ulp in that case */
    }
}

// Prints a qint64_t value for debugging purposes
static inline void print_qint(const qint64_t *a) {
  printf("{.hh=0x%"PRIx64", .hl=0x%"PRIx64", .lh=0x%"PRIx64", .ll=0x%"PRIx64", .ex=%"PRId64", .sgn=0x%"PRIx64"}\n",
         a->hh, a->hl, a->lh, a->ll, a->ex, a->sgn);
}
/*
  Approximation tables
*/

/* For 90 <= i <= 181, _INVERSE_3_1[i-90] is an approximation of the inverse
   of x for i/2^7 <= x < (i+1)/2^7, where an entry (hh,hl,lh,ll,ex,sgn)
   represents (-1)^sgn*(hh+hl/2^64+lh/2^128+ll/2^192)*2^(ex-63)
   (the binary point is after the most significant bit of hh).
   For i=127 and i=128, we force _INVERSE_3_1[i-90]=1.
   If was generated with output_inverse_3_1(7,9,90,181) from the
   accompanying file qint.sage.
   There is no rounding error here, the only approximation error is in
   _LOG_INV_3_1[]. */
static const qint64_t _INVERSE_3_1[] = {
    {.hh = 0xb500000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=90 */
    {.hh = 0xb300000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=91 */
    {.hh = 0xb100000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=92 */
    {.hh = 0xaf00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=93 */
    {.hh = 0xad80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=94 */
    {.hh = 0xab80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=95 */
    {.hh = 0xaa00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=96 */
    {.hh = 0xa800000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=97 */
    {.hh = 0xa680000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=98 */
    {.hh = 0xa480000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=99 */
    {.hh = 0xa300000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=100 */
    {.hh = 0xa180000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=101 */
    {.hh = 0xa000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=102 */
    {.hh = 0x9e80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=103 */
    {.hh = 0x9d00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=104 */
    {.hh = 0x9b80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=105 */
    {.hh = 0x9a00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=106 */
    {.hh = 0x9880000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=107 */
    {.hh = 0x9700000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=108 */
    {.hh = 0x9580000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=109 */
    {.hh = 0x9480000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=110 */
    {.hh = 0x9300000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=111 */
    {.hh = 0x9180000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=112 */
    {.hh = 0x9080000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=113 */
    {.hh = 0x8f00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=114 */
    {.hh = 0x8e00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=115 */
    {.hh = 0x8c80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=116 */
    {.hh = 0x8b80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=117 */
    {.hh = 0x8a80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=118 */
    {.hh = 0x8900000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=119 */
    {.hh = 0x8800000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=120 */
    {.hh = 0x8700000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=121 */
    {.hh = 0x8580000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=122 */
    {.hh = 0x8480000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=123 */
    {.hh = 0x8380000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=124 */
    {.hh = 0x8280000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=125 */
    {.hh = 0x8180000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=126 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=127 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* i=128 */
    {.hh = 0xfd00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=129 */
    {.hh = 0xfb00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=130 */
    {.hh = 0xf900000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=131 */
    {.hh = 0xf780000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=132 */
    {.hh = 0xf580000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=133 */
    {.hh = 0xf380000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=134 */
    {.hh = 0xf200000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=135 */
    {.hh = 0xf000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=136 */
    {.hh = 0xee80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=137 */
    {.hh = 0xec80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=138 */
    {.hh = 0xeb00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=139 */
    {.hh = 0xe900000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=140 */
    {.hh = 0xe780000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=141 */
    {.hh = 0xe600000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=142 */
    {.hh = 0xe480000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=143 */
    {.hh = 0xe300000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=144 */
    {.hh = 0xe100000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=145 */
    {.hh = 0xdf80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=146 */
    {.hh = 0xde00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=147 */
    {.hh = 0xdc80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=148 */
    {.hh = 0xdb00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=149 */
    {.hh = 0xd980000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=150 */
    {.hh = 0xd880000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=151 */
    {.hh = 0xd700000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=152 */
    {.hh = 0xd580000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=153 */
    {.hh = 0xd400000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=154 */
    {.hh = 0xd280000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=155 */
    {.hh = 0xd180000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=156 */
    {.hh = 0xd000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=157 */
    {.hh = 0xce80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=158 */
    {.hh = 0xcd80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=159 */
    {.hh = 0xcc00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=160 */
    {.hh = 0xcb00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=161 */
    {.hh = 0xc980000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=162 */
    {.hh = 0xc880000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=163 */
    {.hh = 0xc700000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=164 */
    {.hh = 0xc600000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=165 */
    {.hh = 0xc500000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=166 */
    {.hh = 0xc380000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=167 */
    {.hh = 0xc280000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=168 */
    {.hh = 0xc180000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=169 */
    {.hh = 0xc000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=170 */
    {.hh = 0xbf00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=171 */
    {.hh = 0xbe00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=172 */
    {.hh = 0xbd00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=173 */
    {.hh = 0xbc00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=174 */
    {.hh = 0xba80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=175 */
    {.hh = 0xb980000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=176 */
    {.hh = 0xb880000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=177 */
    {.hh = 0xb780000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=178 */
    {.hh = 0xb680000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=179 */
    {.hh = 0xb580000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=180 */
    {.hh = 0xb480000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* i=181 */
};

/* For 8128 <= j <= 8256, _INVERSE_3_2[j-8128] is an approximation of the
   inverse of j/2^13, where an entry (hh,hl,lh,ll,ex,sgn) represents
   (-1)^sgn*(hh+hl/2^64+lh/2^128+ll/2^192)*2^(ex-63)
   (the binary point is after the most significant bit of hh).
   For j=8191 and j=8192, we force _INVERSE_3_2[j-8128]=1.
   If was generated with output_inverse_3_2(6,14,8128,8256,7,62) from the
   accompanying file qint.sage.
   There is no rounding error here, the only approximation error is in
   _LOG_INV_3_2[]. */
static const qint64_t _INVERSE_3_2[] = {
    {.hh = 0x8100000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8128 */
    {.hh = 0x80fc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8129 */
    {.hh = 0x80f8000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8130 */
    {.hh = 0x80f4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8131 */
    {.hh = 0x80f0000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8132 */
    {.hh = 0x80ec000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8133 */
    {.hh = 0x80e8000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8134 */
    {.hh = 0x80e4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8135 */
    {.hh = 0x80e0000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8136 */
    {.hh = 0x80dc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8137 */
    {.hh = 0x80d8000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8138 */ 
   {.hh = 0x80d4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8139 */
    {.hh = 0x80d0000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8140 */
    {.hh = 0x80cc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8141 */
    {.hh = 0x80c8000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8142 */
    {.hh = 0x80c4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8143 */
    {.hh = 0x80c0000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8144 */
    {.hh = 0x80bc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8145 */
    {.hh = 0x80b8000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8146 */
    {.hh = 0x80b4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8147 */
    {.hh = 0x80b0000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8148 */
    {.hh = 0x80ac000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8149 */
    {.hh = 0x80a8000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8150 */
    {.hh = 0x80a4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8151 */
    {.hh = 0x80a0000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8152 */
    {.hh = 0x809c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8153 */
    {.hh = 0x8098000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8154 */
    {.hh = 0x8094000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8155 */
    {.hh = 0x8090000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8156 */
    {.hh = 0x808c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8157 */
    {.hh = 0x8088000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8158 */
    {.hh = 0x8084000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8159 */
    {.hh = 0x8080000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8160 */
    {.hh = 0x807c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8161 */
    {.hh = 0x8078000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8162 */
    {.hh = 0x8074000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8163 */
    {.hh = 0x8070000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8164 */
    {.hh = 0x806c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8165 */
    {.hh = 0x8068000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8166 */
    {.hh = 0x8064000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8167 */
    {.hh = 0x8060000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8168 */
    {.hh = 0x805c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8169 */
    {.hh = 0x8058000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8170 */
    {.hh = 0x8054000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8171 */
    {.hh = 0x8050000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8172 */
    {.hh = 0x804c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8173 */
    {.hh = 0x8048000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8174 */
    {.hh = 0x8044000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8175 */
    {.hh = 0x8040000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8176 */
    {.hh = 0x803c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8177 */
    {.hh = 0x8038000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8178 */
    {.hh = 0x8034000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8179 */
    {.hh = 0x8030000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8180 */
    {.hh = 0x802c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8181 */
    {.hh = 0x8028000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8182 */
    {.hh = 0x8024000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8183 */
    {.hh = 0x8020000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8184 */
    {.hh = 0x801c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8185 */
    {.hh = 0x8018000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8186 */
    {.hh = 0x8014000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8187 */
    {.hh = 0x8010000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8188 */
    {.hh = 0x800c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8189 */
    {.hh = 0x8008000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8190 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8191 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* j=8192 */
    {.hh = 0xfff4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8193 */
    {.hh = 0xffec000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8194 */
    {.hh = 0xffe4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8195 */
    {.hh = 0xffdc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8196 */
    {.hh = 0xffd4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8197 */
    {.hh = 0xffcc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8198 */
    {.hh = 0xffc4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8199 */
    {.hh = 0xffbc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8200 */
    {.hh = 0xffb4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8201 */
    {.hh = 0xffac000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8202 */
    {.hh = 0xffa4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8203 */
    {.hh = 0xff9c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8204 */
    {.hh = 0xff94000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8205 */
    {.hh = 0xff8c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8206 */
    {.hh = 0xff84000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8207 */
    {.hh = 0xff7c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8208 */
    {.hh = 0xff74000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8209 */
    {.hh = 0xff6c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8210 */
    {.hh = 0xff64000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8211 */
    {.hh = 0xff5c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8212 */
    {.hh = 0xff54000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8213 */
    {.hh = 0xff4c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8214 */
    {.hh = 0xff44000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8215 */
    {.hh = 0xff3c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8216 */
    {.hh = 0xff34000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8217 */
    {.hh = 0xff2c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8218 */
    {.hh = 0xff24000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8219 */
    {.hh = 0xff1c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8220 */
    {.hh = 0xff14000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8221 */
    {.hh = 0xff0c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8222 */
    {.hh = 0xff04000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8223 */
    {.hh = 0xfefc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8224 */
    {.hh = 0xfef4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8225 */
    {.hh = 0xfeec000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8226 */
    {.hh = 0xfee4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8227 */
    {.hh = 0xfedc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8228 */
    {.hh = 0xfed4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8229 */
    {.hh = 0xfecc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8230 */
    {.hh = 0xfec4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8231 */
    {.hh = 0xfebc000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8232 */
    {.hh = 0xfeb4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8233 */
    {.hh = 0xfeac000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8234 */
    {.hh = 0xfea4000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8235 */
    {.hh = 0xfe9c000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8236 */
    {.hh = 0xfe98000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8237 */
    {.hh = 0xfe90000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8238 */
    {.hh = 0xfe88000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8239 */
    {.hh = 0xfe80000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8240 */
    {.hh = 0xfe78000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8241 */
    {.hh = 0xfe70000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8242 */
    {.hh = 0xfe68000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8243 */
    {.hh = 0xfe60000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8244 */
    {.hh = 0xfe58000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8245 */
    {.hh = 0xfe50000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8246 */
    {.hh = 0xfe48000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8247 */
    {.hh = 0xfe40000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8248 */
    {.hh = 0xfe38000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8249 */
    {.hh = 0xfe30000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8250 */
    {.hh = 0xfe28000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8251 */
    {.hh = 0xfe20000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8252 */
    {.hh = 0xfe18000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8253 */
    {.hh = 0xfe10000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8254 */
    {.hh = 0xfe08000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8255 */
    {.hh = 0xfe00000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -1, .sgn = 0x0}, /* j=8256 */
};
    
/* For 90 <= i <= 181, _LOG_INV_3_1[i-90] is an approximation of
   -log(_INVERSE_3_1[i-90]), where an entry (hh,hl,lh,ll,ex,sgn) represents
   (-1)^sgn*(hh+hl/2^64+lh/2^128+ll/2^192)*2^(ex-63)
   (the binary point is after the most significant bit of hh).
   If was generated with output_log_inv_3_1(7,9,90,181) from the
   accompanying file qint.sage.
   The approximation error is bounded by 2^-258 (absolute) and 2^-256 (rel). */
static const qint64_t _LOG_INV_3_1[] = {
    {.hh = 0xb1641795ce3ca97b, .hl = 0x7af915300e517391, .lh = 0x362aee92bfa25a80, .ll = 0x1646679ea2568305, .ex = -2, .sgn = 0x1}, /* i=90 */
    {.hh = 0xabb3b8ba2ad362a4, .hl = 0xd5b6506cc17a01f1, .lh = 0x706866327ef7c050, .ll = 0x628f2d55f109eac9, .ex = -2, .sgn = 0x1}, /* i=91 */
    {.hh = 0xa5f2fcabbbc506da, .hl = 0x64ca4fb7ec323d72, .lh = 0xa68b0ce7a5e0a7ea, .ll = 0xd6cad0f0b6c847ef, .ex = -2, .sgn = 0x1}, /* i=92 */
    {.hh = 0xa0218434353f1de8, .hl = 0x6093efa632530ac8, .lh = 0x304fb3b2345b41a9, .ll = 0xe440d92b32eac488, .ex = -2, .sgn = 0x1}, /* i=93 */
    {.hh = 0x9bb93315fec2d792, .hl = 0xa7589fba0865790d, .lh = 0x82b75e91fcdfa14e, .ll = 0x98d12c3138e33333, .ex = -2, .sgn = 0x1}, /* i=94 */
    {.hh = 0x95c981d5c4e924ed, .hl = 0x29404f5aa577d6b1, .lh = 0xba0ea3f2ae1e1d07, .ll = 0x758e4ab7f718ea9f, .ex = -2, .sgn = 0x1}, /* i=95 */
    {.hh = 0x914a0fde7bcb2d12, .hl = 0x1429ed3aea197a5d, .lh = 0x355a6f4f0ec5ce8f, .ll = 0xc4eca5fff76cbf20, .ex = -2, .sgn = 0x1}, /* i=96 */
    {.hh = 0x8b3ae55d5d30701c, .hl = 0xe63eab883717047e, .lh = 0xcfa09487833ea69, .ll = 0x8791b8732b281e2f, .ex = -2, .sgn = 0x1}, /* i=97 */
    {.hh = 0x86a35abcd5ba5903, .hl = 0xec81c3cbd925cccf, .lh = 0x6a2f869f2c41ea0a, .ll = 0x261913d1bbc49faf, .ex = -2, .sgn = 0x1}, /* i=98 */
    {.hh = 0x8073622d6a80e634, .hl = 0x6a97009015316070, .lh = 0x9f1d0d49f7cf8122, .ll = 0xc0baf08f2dd617cf, .ex = -2, .sgn = 0x1}, /* i=99 */
    {.hh = 0xf7856e5ee2c9b290, .hl = 0xc6f2a1b84190a7d6, .lh = 0x94261a0e91f0e8f2, .ll = 0x960d286867d7da8d, .ex = -3, .sgn = 0x1}, /* i=100 */
    {.hh = 0xee0de5055f63eb06, .hl = 0x98a33316df83ba56, .lh = 0xa28f0225cea42f20, .ll = 0xc8c87785c07b059d, .ex = -3, .sgn = 0x1}, /* i=101 */
    {.hh = 0xe47fbe3cd4d10d61, .hl = 0x2ec0f797fdcd1257, .lh = 0x1d97a9d046b706c5, .ll = 0xc3c4cfd592ff1d1b, .ex = -3, .sgn = 0x1}, /* i=102 */
    {.hh = 0xdada8cf47dad2374, .hl = 0x4ffb833c3409ee78, .lh = 0x3713df786be7d79f, .ll = 0xdfdccbdb9cc5e4fc, .ex = -3, .sgn = 0x1}, /* i=103 */
    {.hh = 0xd11de0ff15ab18c9, .hl = 0xb88d83d4cc613f1f, .lh = 0x8db36c5996f30e02, .ll = 0x67caaac70b1e203f, .ex = -3, .sgn = 0x1}, /* i=104 */
    {.hh = 0xc74946f4436a0552, .hl = 0xc4f5cb531201c0d0, .lh = 0xe377c62941756dda, .ll = 0xbf31ff26e7952aa7, .ex = -3, .sgn = 0x1}, /* i=105 */
    {.hh = 0xbd5c481086c848df, .hl = 0x1b596b503040323f, .lh = 0xf0a4a6c408595abb, .ll = 0x18b2e81de5a7413d, .ex = -3, .sgn = 0x1}, /* i=106 */
    {.hh = 0xb3566a13956a86f6, .hl = 0xff1b1e1574d9fd53, .lh = 0xd790e4993973cb21, .ll = 0xb570c1978023c83, .ex = -3, .sgn = 0x1}, /* i=107 */
    {.hh = 0xa9372f1d0da1bd17, .hl = 0x200eb71e58cd36de, .lh = 0x631daa222aa1cc5e, .ll = 0xc53df36a99bd161e, .ex = -3, .sgn = 0x1}, /* i=108 */
    {.hh = 0x9efe158766314e54, .hl = 0xc571827efe892fc4, .lh = 0x5fb87ab4717a500, .ll = 0x73890974d65b5cfd, .ex = -3, .sgn = 0x1}, /* i=109 */
    {.hh = 0x981eb8c723fe97f4, .hl = 0xa31c134fb702d431, .lh = 0xa1267633d7a950a6, .ll = 0xf1d435478be2e98d, .ex = -3, .sgn = 0x1}, /* i=110 */
    {.hh = 0x8db956a97b3d0148, .hl = 0x3023472cd739f9de, .lh = 0x3f642654cbb04a9b, .ll = 0x501b839196278b37, .ex = -3, .sgn = 0x1}, /* i=111 */
    {.hh = 0x8338a89652cb7150, .hl = 0xc647eb86498c2ce1, .lh = 0x6fdaaacd24ed99fc, .ll = 0x73619b3ac0a2580e, .ex = -3, .sgn = 0x1}, /* i=112 */
    {.hh = 0xf85186008b15330b, .hl = 0xe64b8b775997898d, .lh = 0x3474d3375b525967, .ll = 0x1851f0a96f698496, .ex = -4, .sgn = 0x1}, /* i=113 */
    {.hh = 0xe2f2a47ade3a18ae, .hl = 0xb0bf7c0b0d8bb4ec, .lh = 0xb357c6e1bb965608, .ll = 0xb1f3fb65de3326ac, .ex = -4, .sgn = 0x1}, /* i=114 */
    {.hh = 0xd49369d256ab1b28, .hl = 0x5e9154e1d5263cd4, .lh = 0xfb3f11769cc680ef, .ll = 0x5588fd21488d3117, .ex = -4, .sgn = 0x1}, /* i=115 */
    {.hh = 0xbed3b36bd8966422, .hl = 0x240644d7d9ed08ae, .lh = 0x8bd331e0f0163a57, .ll = 0xad0c8b665d0ba662, .ex = -4, .sgn = 0x1}, /* i=116 */
    {.hh = 0xb032c549ba861d8e, .hl = 0xf74e27bc92ce336a, .lh = 0x476c441f8cbfb247, .ll = 0xb421b4cceddb6dec, .ex = -4, .sgn = 0x1}, /* i=117 */
    {.hh = 0xa176e5f5323781dd, .hl = 0xd4f935996c92e8cb, .lh = 0xb1ed0cd9e5eb16c4, .ll = 0xd070037b7a65dbb6, .ex = -4, .sgn = 0x1}, /* i=118 */
    {.hh = 0x8b29b7751bd70743, .hl = 0x12e0b9ee992f236d, .lh = 0x21482d3342d35569, .ll = 0xbf365c4132567724, .ex = -4, .sgn = 0x1}, /* i=119 */
    {.hh = 0xf85186008b15330b, .hl = 0xe64b8b775997898d, .lh = 0x3474d3375b525967, .ll = 0x1851f0a96f698496, .ex = -5, .sgn = 0x1}, /* i=120 */
    {.hh = 0xda16eb88cb8df614, .hl = 0x68a63ecfb66e94ab, .lh = 0xce26340fc53dc9e7, .ll = 0x7778bea9e4485112, .ex = -5, .sgn = 0x1}, /* i=121 */
    {.hh = 0xac52dd7e4726a463, .hl = 0x547a963a91bb3012, .lh = 0x6146c24c8704d774, .ll = 0x9181c8e24fdd9bf3, .ex = -5, .sgn = 0x1}, /* i=122 */
    {.hh = 0x8d86cc491ecbfe16, .hl = 0x51776453b7e8254d, .lh = 0x1fe3399d400c4228, .ll = 0x939ae69b03a586bc, .ex = -5, .sgn = 0x1}, /* i=123 */
    {.hh = 0xdcfe013d7c8cbfde, .hl = 0xa32dbac46f30cffe, .lh = 0xda998fa29b9bb98b, .ll = 0x121d35ae45b4e2e1, .ex = -6, .sgn = 0x1}, /* i=124 */
    {.hh = 0x9e75221a352ba779, .hl = 0xa52b7ea62f2198d0, .lh = 0x797189a4ceffb772, .ll = 0x67b1205aea8ed5f1, .ex = -6, .sgn = 0x1}, /* i=125 */
    {.hh = 0xbee23afc0853b6e9, .hl = 0x289782c20df350a1, .lh = 0x4943001d3f0647d1, .ll = 0x907db46b91a9be11, .ex = -7, .sgn = 0x1}, /* i=126 */
    {.hh = 0x0, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 255, .sgn = 0x1}, /* i=127 */
    {.hh = 0x0, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 255, .sgn = 0x1}, /* i=128 */
    {.hh = 0xc122451c45155104, .hl = 0xb16137f09a002b3c, .lh = 0x114425f06f494d45, .ll = 0xe2fa6c6fbca3a43a, .ex = -7, .sgn = 0x0}, /* i=129 */
    {.hh = 0xa195492cc06604e6, .hl = 0x4a18dff7cdb4ae5c, .lh = 0x1b120e15ca3dceb7, .ll = 0x62148cc46c40db43, .ex = -6, .sgn = 0x0}, /* i=130 */
    {.hh = 0xe31e9760a5578c63, .hl = 0xf9eb2f284f31c35c, .lh = 0x739276a47bc0067f, .ll = 0x13d3be097b445a02, .ex = -6, .sgn = 0x0}, /* i=131 */
    {.hh = 0x8a4f1f2002d46756, .hl = 0x5be970314148c644, .lh = 0xd7177b23dafc1e78, .ll = 0x62496477de5b9b70, .ex = -5, .sgn = 0x0}, /* i=132 */
    {.hh = 0xab8ae2601e777722, .hl = 0x3b89d7f254f8d4d, .lh = 0x7fea49aded4406bd, .ll = 0xbd68cbc383b2b959, .ex = -5, .sgn = 0x0}, /* i=133 */
    {.hh = 0xcd0c3dab9ef3dd1b, .hl = 0x13b26f298aa357c8, .lh = 0x307b8ee396d79ef6, .ll = 0x801ddc72263552e3, .ex = -5, .sgn = 0x0}, /* i=134 */
    {.hh = 0xe65b9e6eed965c36, .hl = 0xe09f5fe2058d6005, .lh = 0xb58f9a65c1043b41, .ll = 0xe2faca238c300fa9, .ex = -5, .sgn = 0x0}, /* i=135 */
    {.hh = 0x842cc5acf1d03445, .hl = 0x1fecdfa819b96097, .lh = 0xe362c7f8dd18e5cb, .ll = 0x2c885ebb0a67bc24, .ex = -4, .sgn = 0x0}, /* i=136 */
    {.hh = 0x9103dae3c2a4ec67, .hl = 0xe0863df62ab56719, .lh = 0xe0c7d4d12db021b8, .ll = 0x55776e4da79f922a, .ex = -4, .sgn = 0x0}, /* i=137 */
    {.hh = 0xa242f01edefd6a37, .hl = 0x469355b78dc796e2, .lh = 0xb3c575a2031956ec, .ll = 0x395fd54d8695b022, .ex = -4, .sgn = 0x0}, /* i=138 */
    {.hh = 0xaf4ad26cbc8e5be7, .hl = 0xe8b8b88a14ff0cd, .lh = 0x9ad6b7f2deaa8ae6, .ll = 0x47e37e4affafd5a1, .ex = -4, .sgn = 0x0}, /* i=139 */
    {.hh = 0xc0cbf17a071f80dc, .hl = 0xf96ffdf76a147ccc, .lh = 0x3700761ca4fb5278, .ll = 0xf6961709acabf991, .ex = -4, .sgn = 0x0}, /* i=140 */
    {.hh = 0xce06196a692a41fb, .hl = 0xbe3ccc15326765f, .lh = 0x733187c6d6f39bb8, .ll = 0xe3f54edc6e2e0350, .ex = -4, .sgn = 0x0}, /* i=141 */
    {.hh = 0xdb56446d6ad8deff, .hl = 0xa8112e35a60e6374, .lh = 0xdd62571dda9ce602, .ll = 0xe44b58c81d361f18, .ex = -4, .sgn = 0x0}, /* i=142 */
    {.hh = 0xe8bcbc410c9b219d, .hl = 0xaf7df76ad29e5b5f, .lh = 0xe6a09f8913389334, .ll = 0x7de7a2535752b786, .ex = -4, .sgn = 0x0}, /* i=143 */
    {.hh = 0xf639cc185088fe5d, .hl = 0x4066e87f2c0f733f, .lh = 0x8296a39b87519924, .ll = 0x5a18333a98b0adbc, .ex = -4, .sgn = 0x0}, /* i=144 */
    {.hh = 0x842cc5acf1d03445, .hl = 0x1fecdfa819b96097, .lh = 0xe362c7f8dd18e5cb, .ll = 0x2c885ebb0a67bc24, .ex = -3, .sgn = 0x0}, /* i=145 */
    {.hh = 0x8b064012593d85a5, .hl = 0x52013c7a80ad089b, .lh = 0x42ada32f6b02af2d, .ll = 0x88cfab3b8ffc09bc, .ex = -3, .sgn = 0x0}, /* i=146 */
    {.hh = 0x91eb89524e100d23, .hl = 0x8fd3df5c52d67e7b, .lh = 0x2024f18ebc9b8af6, .ll = 0x81f936979eefae26, .ex = -3, .sgn = 0x0}, /* i=147 */
    {.hh = 0x98dcca69d27c263b, .hl = 0x8e94203f336fc8c4, .lh = 0xcfe4e777e9932f10, .ll = 0x9ed7ba1820b6ff5, .ex = -3, .sgn = 0x0}, /* i=148 */
    {.hh = 0x9fda2d2cc9465c4f, .hl = 0x32b9565f5355181, .lh = 0xdc751798a72b3dc9, .ll = 0x56eca518e7d2fe44, .ex = -3, .sgn = 0x0}, /* i=149 */
    {.hh = 0xa6e3dc4bde0e3cdb, .hl = 0x570ff874170d2a8, .lh = 0xc7be23c834886156, .ll = 0xbd706a5a2627f5aa, .ex = -3, .sgn = 0x0}, /* i=150 */
    {.hh = 0xab9be6480c66ea9e, .hl = 0x9ae21fd871b8d27c, .lh = 0x7851f4e516e5c9bf, .ll = 0xc1c5a79f56e96b76, .ex = -3, .sgn = 0x0}, /* i=151 */
    {.hh = 0xb2ba75f46099cf8b, .hl = 0x2c3c2e77904afa78, .lh = 0x77fa400e7e689a3, .ll = 0x30a5f043d61bad59, .ex = -3, .sgn = 0x0}, /* i=152 */
    {.hh = 0xb9e5c83a7e8a655b, .hl = 0xcbffe9661fe72421, .lh = 0x2096b17331fac5dc, .ll = 0x7775b110db2591bd, .ex = -3, .sgn = 0x0}, /* i=153 */
    {.hh = 0xc11e0b2a8d1e0ddb, .hl = 0x9a631e830fd30903, .lh = 0xd59edb68f6b3f63b, .ll = 0x9ede162ed215bb6b, .ex = -3, .sgn = 0x0}, /* i=154 */
    {.hh = 0xc8636dcfe5e6ca0a, .hl = 0x88e72835b3292d4f, .lh = 0xd07fb98b088395ee, .ll = 0x10b2726db3c15971, .ex = -3, .sgn = 0x0}, /* i=155 */
    {.hh = 0xcd43bc6f5d51c3e8, .hl = 0xfbfb0e3f0fd23074, .lh = 0x435c6598be364fab, .ll = 0x938d2827ce902e72, .ex = -3, .sgn = 0x0}, /* i=156 */
    {.hh = 0xd49f69e456cf1b79, .hl = 0x5f53bd2e406e66e7, .lh = 0x7188af8f4f45b9ee, .ll = 0x7ec0d7bfb6b65f0d, .ex = -3, .sgn = 0x0}, /* i=157 */
    {.hh = 0xdc08b985c11e9068, .hl = 0x3b9cd767c3b1ac52, .lh = 0x81fdd139ee15996c, .ll = 0xdf9da9ae69110ecb, .ex = -3, .sgn = 0x0}, /* i=158 */
    {.hh = 0xe1014558bfcda3e2, .hl = 0x35470a74be1230ec, .lh = 0x7ea4f73313d9cef6, .ll = 0x7a6dd1127f07bf9a, .ex = -3, .sgn = 0x0}, /* i=159 */
    {.hh = 0xe881bf932af3dac0, .hl = 0xc524848e3443e03f, .lh = 0xc22bd8fede6ee351, .ll = 0x93f48308bc589a07, .ex = -3, .sgn = 0x0}, /* i=160 */
    {.hh = 0xed89ed86a44a01aa, .hl = 0x11d49f96cb88317a, .lh = 0xb09cac07eab378a8, .ll = 0xe6342851611cc8be, .ex = -3, .sgn = 0x0}, /* i=161 */
    {.hh = 0xf52224f82557a459, .hl = 0x8dcca8d7f17fa2a9, .lh = 0x330d7e7fe8c1c62a, .ll = 0x18d25c613e0e9a6d, .ex = -3, .sgn = 0x0}, /* i=162 */
    {.hh = 0xfa3a589a6f9146d8, .hl = 0x388212895529a6fa, .lh = 0x937d820ed16d615e, .ll = 0x6bf1e0ae92585a10, .ex = -3, .sgn = 0x0}, /* i=163 */
    {.hh = 0x80f572b1363487b9, .hl = 0xf5bd0b5b3479d5f4, .lh = 0x501b8b4a63fd6f67, .ll = 0x6e9f54a7361289b3, .ex = -2, .sgn = 0x0}, /* i=164 */
    {.hh = 0x8389c3026ac3139b, .hl = 0x62dda9d2270fa1f4, .lh = 0x29aec44c9ebb0731, .ll = 0xee2b9479c54b01fb, .ex = -2, .sgn = 0x0}, /* i=165 */
    {.hh = 0x86216b3b0b17188b, .hl = 0x163ceae88f720f1d, .lh = 0x9a8ffa0ca490b651, .ll = 0x815baea454ee87ca, .ex = -2, .sgn = 0x0}, /* i=166 */
    {.hh = 0x8a0b3f79b3bc180f, .hl = 0x49b55ea7d3730d7, .lh = 0x1f95d048ae9871b6, .ll = 0xb7eb0007a2dfe5c0, .ex = -2, .sgn = 0x0}, /* i=167 */
    {.hh = 0x8cab69dcde17d2f7, .hl = 0x3ad1aa142b94f169, .lh = 0x82d7e38d0ce7657c, .ll = 0x800bbe6769b633e0, .ex = -2, .sgn = 0x0}, /* i=168 */
    {.hh = 0x8f4f0b3c44cfa2a2, .hl = 0x586e9343c9cfdbac, .lh = 0x1fee2e686760d584, .ll = 0xc0243088b56a6c0f, .ex = -2, .sgn = 0x0}, /* i=169 */
    {.hh = 0x934b1089a6dc93c1, .hl = 0xdf5bb3b60554e151, .lh = 0x87a486e65aa1bcd5, .ll = 0xad047f998c197d96, .ex = -2, .sgn = 0x0}, /* i=170 */
    {.hh = 0x95f783e6e49a9cfa, .hl = 0x4a5004f3ef063312, .lh = 0xcac9f0589aff46a5, .ll = 0x3b2136f4975a446e, .ex = -2, .sgn = 0x0}, /* i=171 */
    {.hh = 0x98a78f0e9ae71d85, .hl = 0x2cdec34784707839, .lh = 0x4861cab8c5ee1c94, .ll = 0xb0ddc91e9b86138f, .ex = -2, .sgn = 0x0}, /* i=172 */
    {.hh = 0x9b5b3bb5f088b766, .hl = 0xd878bbe3d392be25, .lh = 0x24f04843d0f8f41, .ll = 0xd27746bfed0adcfe, .ex = -2, .sgn = 0x0}, /* i=173 */
    {.hh = 0x9e1293b9998c1daa, .hl = 0x5b035eae273a855e, .lh = 0xf58182e4db06261c, .ll = 0x73db477d896b83f5, .ex = -2, .sgn = 0x0}, /* i=174 */
    {.hh = 0xa22c8f029cfa45a9, .hl = 0xdb5b709e0b69e773, .lh = 0x5e171935b5381c36, .ll = 0x930e7dec83978f45, .ex = -2, .sgn = 0x0}, /* i=175 */
    {.hh = 0xa4ed3f9de620f666, .hl = 0x9b5e973353638c10, .lh = 0xd81763d28db5c039, .ll = 0xaef301c1c91f3649, .ex = -2, .sgn = 0x0}, /* i=176 */
    {.hh = 0xa7b1bf5dd4c07d4e, .hl = 0x699db68db75e9a7e, .lh = 0x8e98852150ea76b5, .ll = 0x2d4a158846053fd7, .ex = -2, .sgn = 0x0}, /* i=177 */
    {.hh = 0xaa7a18dbdf0d44aa, .hl = 0x604884a8dd76d08a, .lh = 0x6c40e044972eeeb2, .ll = 0x8d4e70541a83bec5, .ex = -2, .sgn = 0x0}, /* i=178 */
    {.hh = 0xad4656ddf6fd070c, .hl = 0x9ea10260fe452ba2, .lh = 0x59b0b64abac9cb07, .ll = 0x30a999bf35a66756, .ex = -2, .sgn = 0x0}, /* i=179 */
    {.hh = 0xb0168457848f5f48, .hl = 0xbb6f9fb246068d52, .lh = 0x3e567a3312c2443d, .ll = 0xe963d8ddfd9f7f8b, .ex = -2, .sgn = 0x0}, /* i=180 */
    {.hh = 0xb2eaac6a67005513, .hl = 0xf4b716f6fec8156b, .lh = 0x2c74b8f4ed61d394, .ll = 0x1166335bb2b54fe0, .ex = -2, .sgn = 0x0}, /* i=181 */
};

/* For 8128 <= j <= 8256, _LOG_INV_3_2[j-8128] is an approximation of
   -log(_INVERSE_3_2[j-8128]), where an entry (hh,hl,lh,ll,ex,sgn) represents
   (-1)^sgn*(hh+hl/2^64+lh/2^128+ll/2^192)*2^(ex-63)
   (the binary point is after the most significant bit of hh).
   If was generated with output_log_inv_3_2(6,14,8128,8256,7,62) from the
   accompanying file qint.sage.
   The approximation error is bounded by 2^-263 (absolute) and 2^-256 (rel). */
static const qint64_t _LOG_INV_3_2[] = {
    {.hh = 0xff015358833c47e1, .hl = 0xbb481c8ee1416959, .lh = 0xed961f7cd039d43b, .ll = 0x3813c435abc461e9, .ex = -8, .sgn = 0x1}, /* j=8128 */
    {.hh = 0xfb0933b732572a6d, .hl = 0x214cca3dd1d4796a, .lh = 0x63275973180916, .ll = 0xce33d61e9d12b379, .ex = -8, .sgn = 0x1}, /* j=8129 */
    {.hh = 0xf710f492711d9d26, .hl = 0xfbc7b38b17b2019, .lh = 0x53eb4b80e74f4d9f, .ll = 0xec0f1ece69a0881, .ex = -8, .sgn = 0x1}, /* j=8130 */
    {.hh = 0xf31895e84b1a6be6, .hl = 0xb76782b9e88c84cb, .lh = 0x3765e2cb07bc7842, .ll = 0x1b2970050550b17b, .ex = -8, .sgn = 0x1}, /* j=8131 */
    {.hh = 0xef2017b6cba9cf9a, .hl = 0x2dc85881664025b4, .lh = 0xce96efdd54fdcc41, .ll = 0xc953aa9864a2c806, .ex = -8, .sgn = 0x1}, /* j=8132 */
    {.hh = 0xeb2779fbfdf96874, .hl = 0xce4ab4e678d0ed03, .lh = 0x7c9ecdfd8f89db96, .ll = 0xa637f86e778c350c, .ex = -8, .sgn = 0x1}, /* j=8133 */
    {.hh = 0xe72ebcb5ed08382b, .hl = 0xb60585f4c4bb6062, .lh = 0x1caec15031f4dc54, .ll = 0x197d0a42fdccbac4, .ex = -8, .sgn = 0x1}, /* j=8134 */
    {.hh = 0xe335dfe2a3a69c2b, .hl = 0x59bcffe9d5650564, .lh = 0x50c342f5fce295a8, .ll = 0xac842dc0defcda11, .ex = -8, .sgn = 0x1}, /* j=8135 */
    {.hh = 0xdf3ce3802c7647cd, .hl = 0x3602021fa93b1e18, .lh = 0x4f178f6a8fbb9ca5, .ll = 0x4f07c79076670299, .ex = -8, .sgn = 0x1}, /* j=8136 */
    {.hh = 0xdb43c78c91ea3e8c, .hl = 0x9944002534d09b3d, .lh = 0x2354c6de776e85ab, .ll = 0x7bfb018bb7288d07, .ex = -8, .sgn = 0x1}, /* j=8137 */
    {.hh = 0xd74a8c05de46ce3a, .hl = 0x87aa95782311a277, .lh = 0x2ba2456733804a75, .ll = 0x33512ca3a3f6fe2b, .ex = -8, .sgn = 0x1}, /* j=8138 */
    {.hh = 0xd35130ea1ba18930, .hl = 0xb88be10313a1303c, .lh = 0xc17153c5ab3b0225, .ll = 0xe1f11ff3205d196e, .ex = -8, .sgn = 0x1}, /* j=8139 */
    {.hh = 0xcf57b63753e14083, .hl = 0xad54bc31433dddba, .lh = 0x3d675807b776c8c3, .ll = 0x50075ac879e03385, .ex = -8, .sgn = 0x1}, /* j=8140 */
    {.hh = 0xcb5e1beb90bdfe33, .hl = 0xe1b7d813e3f825e0, .lh = 0xd24faf76303783f0, .ll = 0x55ff6c2398186fbe, .ex = -8, .sgn = 0x1}, /* j=8141 */
    {.hh = 0xc7646204dbc0ff5e, .hl = 0x14f8c1be7370f218, .lh = 0xb165fd239443b62c, .ll = 0x237e837cc1f0bdfb, .ex = -8, .sgn = 0x1}, /* j=8142 */
    {.hh = 0xc36a88813e44ae6a, .hl = 0xac27c5a6139cd30c, .lh = 0x3e814a96bc97e05e, .ll = 0xa2417a23566b2cd7, .ex = -8, .sgn = 0x1}, /* j=8143 */
    {.hh = 0xbf708f5ec1749d3c, .hl = 0x2d23a0744e00f594, .lh = 0x68f9c68b2e5248, .ll = 0x1d701478d6f6c2b6, .ex = -8, .sgn = 0x1}, /* j=8144 */
    {.hh = 0xbb76769b6e4d7f5c, .hl = 0xd235e25fb9644c30, .lh = 0x869c4dd8468b27de, .ll = 0x605b72cc1d8025bc, .ex = -8, .sgn = 0x1}, /* j=8145 */
    {.hh = 0xb77c3e354d9d242b, .hl = 0x361ee0bcb5db0449, .lh = 0x567cab7f031b369, .ll = 0xd9bec487ee14afeb, .ex = -8, .sgn = 0x1}, /* j=8146 */
    {.hh = 0xb381e62a68027106, .hl = 0x18660815da3d7962, .lh = 0xd14621e31d9f0de1, .ll = 0x8fbb93f3b4e5b10e, .ex = -8, .sgn = 0x1}, /* j=8147 */
    {.hh = 0xaf876e78c5ed5b77, .hl = 0x39c357b6bfdf81b4, .lh = 0xf2591df10cea40d0, .ll = 0x730872806a501e6e, .ex = -8, .sgn = 0x1}, /* j=8148 */
    {.hh = 0xab8cd71e6f9ee35d, .hl = 0x5076c62c951204f5, .lh = 0xb058f99c8186daa4, .ll = 0xae0d7dcbc2a675ab, .ex = -8, .sgn = 0x1}, /* j=8149 */
    {.hh = 0xa79220196d290d15, .hl = 0x146244d643f7fa2a, .lh = 0x90fee0b93d40db31, .ll = 0xe67bce480f351d7b, .ex = -8, .sgn = 0x1}, /* j=8150 */
    {.hh = 0xa3974967c66edba1, .hl = 0x62bb0f3208d9a1ba, .lh = 0xcdd5aab43dfdb463, .ll = 0x9338ef1dac85e113, .ex = -8, .sgn = 0x1}, /* j=8151 */
    {.hh = 0x9f9c530783244ad2, .hl = 0x7926e92808bd580c, .lh = 0x9a874314df180c72, .ll = 0x2272c79abfdb422c, .ex = -8, .sgn = 0x1}, /* j=8152 */
    {.hh = 0x9ba13cf6aace496c, .hl = 0x4819e620d5fcc067, .lh = 0x9066b677760637d4, .ll = 0x308f6e39ffa6ca4c, .ex = -8, .sgn = 0x1}, /* j=8153 */
    {.hh = 0x97a6073344c2b34b, .hl = 0xdc494943d427214e, .lh = 0x423f0610339ed04b, .ll = 0x185c41879a1a0e44, .ex = -8, .sgn = 0x1}, /* j=8154 */
    {.hh = 0x93aab1bb58284b8b, .hl = 0xdf0805c4161e404b, .lh = 0xb467a7c6839cc262, .ll = 0x831c3f31eb48d551, .ex = -8, .sgn = 0x1}, /* j=8155 */
    {.hh = 0x8faf3c8cebf6b6a8, .hl = 0x2d615caaa0514c3b, .lh = 0xf729c68cd270f129, .ll = 0xaab9342f19cba76c, .ex = -8, .sgn = 0x1}, /* j=8156 */
    {.hh = 0x8bb3a7a606f674a0, .hl = 0x85c60c12eca0aedb, .lh = 0xa3fb0e5c1d39e0e8, .ll = 0x1dca7781b6e7202f, .ex = -8, .sgn = 0x1}, /* j=8157 */
    {.hh = 0x87b7f304afc0db1a, .hl = 0x4c207a522524f8de, .lh = 0x3d54277ac7f378a6, .ll = 0xa5d1bbca5392d277, .ex = -8, .sgn = 0x1}, /* j=8158 */
    {.hh = 0x83bc1ea6ecc00f81, .hl = 0x64243e02c6215a4e, .lh = 0xb38c33565546e35c, .ll = 0x19646b6a5fd02489, .ex = -8, .sgn = 0x1}, /* j=8159 */
    {.hh = 0xff805515885e0250, .hl = 0x435ab4da6a5bb48c, .lh = 0xcd29dd6d72582491, .ll = 0xba6e335a1a33227f, .ex = -9, .sgn = 0x1}, /* j=8160 */
    {.hh = 0xf7882d5c7832c6cc, .hl = 0x9e06fc84b6ea5e24, .lh = 0x699801dab452e328, .ll = 0x96dc3d1e75ba8032, .ex = -9, .sgn = 0x1}, /* j=8161 */
    {.hh = 0xef8fc61eb4b74f6e, .hl = 0x91ab122ee427cfb4, .lh = 0x8862e24ccd48f678, .ll = 0x95fa76270a5d0366, .ex = -9, .sgn = 0x1}, /* j=8162 */
    {.hh = 0xe7971f584945efae, .hl = 0x5f832513e3211642, .lh = 0xa65998dde4dd76e0, .ll = 0xbbb102dc658c60, .ex = -9, .sgn = 0x1}, /* j=8163 */
    {.hh = 0xdf9e390540da5fbe, .hl = 0x5e7b48cfeeb85aa7, .lh = 0xbcaca74cb74df3d2, .ll = 0x55bfb0c4fdd76008, .ex = -9, .sgn = 0x1}, /* j=8164 */
    {.hh = 0xd7a51321a611b0c1, .hl = 0xb36a9f58eb4ccd07, .lh = 0x8770a2e82b32cf54, .ll = 0xc9fdca283fb5d971, .ex = -9, .sgn = 0x1}, /* j=8165 */
    {.hh = 0xcfabada9832a4101, .hl = 0x3360751e43c7af35, .lh = 0x5d10db5217ec2ab8, .ll = 0xc60248447782dce7, .ex = -9, .sgn = 0x1}, /* j=8166 */
    {.hh = 0xc7b20898e203b01e, .hl = 0x6fab78aca91193cb, .lh = 0x5e4d97ba155a9de6, .ll = 0xcdd79f978d1afd7b, .ex = -9, .sgn = 0x1}, /* j=8167 */
    {.hh = 0xbfb823ebcc1ed344, .hl = 0xeb432409cffdad8d, .lh = 0x568dc2013b32ced6, .ll = 0x72b53615be59209e, .ex = -9, .sgn = 0x1}, /* j=8168 */
    {.hh = 0xb7bdff9e4a9da959, .hl = 0x793b5acf3a336461, .lh = 0x9cffddada1c113a2, .ll = 0xb54d414e644993a8, .ex = -9, .sgn = 0x1}, /* j=8169 */
    {.hh = 0xafc39bac66434f27, .hl = 0xc3ea2cd93f316b33, .lh = 0xbfcaabcf0318ef95, .ll = 0x85a480f694e7b857, .ex = -9, .sgn = 0x1}, /* j=8170 */
    {.hh = 0xa7c8f8122773f38d, .hl = 0xfc679a28e9d9f212, .lh = 0x487785d971aec0af, .ll = 0x61cbdae5b7255821, .ex = -9, .sgn = 0x1}, /* j=8171 */
    {.hh = 0x9fce14cb9634cba6, .hl = 0xb20f215bd3b58c60, .lh = 0xd8ba6eedf272eeb0, .ll = 0x574dfcf4f87b33e8, .ex = -9, .sgn = 0x1}, /* j=8172 */
    {.hh = 0x97d2f1d4ba2c06f0, .hl = 0xd1aacedcefe9d376, .lh = 0xb21c7fe4cdbc5967, .ll = 0xd35c35c4241c3714, .ex = -9, .sgn = 0x1}, /* j=8173 */
    {.hh = 0x8fd78f299aa0c375, .hl = 0xcbef6fac33691e95, .lh = 0x466fab846a1e3e70, .ll = 0xef6ef4000ffc2f4b, .ex = -9, .sgn = 0x1}, /* j=8174 */
    {.hh = 0x87dbecc63e7b01ed, .hl = 0xe2f1775134c8da75, .lh = 0x134f09715ee9c6cf, .ll = 0x48be7593379645aa, .ex = -9, .sgn = 0x1}, /* j=8175 */
    {.hh = 0xffc0154d588733c5, .hl = 0x3c742a7c76356395, .lh = 0xb1d845d134023d8e, .ll = 0x66ad982559cdd0ce, .ex = -10, .sgn = 0x1}, /* j=8176 */
    {.hh = 0xefc7d18dd4485b9e, .hl = 0xca47c52b7d7ffce2, .lh = 0x13c8ea71e3d8fe30, .ll = 0x8edd8e108ea7f135, .ex = -10, .sgn = 0x1}, /* j=8177 */
    {.hh = 0xdfcf0e45fbce3e80, .hl = 0x7e4cfbd830393b87, .lh = 0x83304a61505642d8, .ll = 0xceeccccf5069ebf8, .ex = -10, .sgn = 0x1}, /* j=8178 */
    {.hh = 0xcfd5cb6dd9ef05dd, .hl = 0x7370ae83f9e72748, .lh = 0x140f4a016e0c0d28, .ll = 0x5aea5efc41fa18fd, .ex = -10, .sgn = 0x1}, /* j=8179 */
    {.hh = 0xbfdc08fd78c229b9, .hl = 0xe6dbb624f9739781, .lh = 0xbbd85b81581c98f8, .ll = 0xe1340de77cd6a600, .ex = -10, .sgn = 0x1}, /* j=8180 */
    {.hh = 0xafe1c6ece1a058dd, .hl = 0x97fa2fd0c9dc723d, .lh = 0x998985ef1e4636e0, .ll = 0x306c5597c846f0b9, .ex = -10, .sgn = 0x1}, /* j=8181 */
    {.hh = 0x9fe705341d236102, .hl = 0x7199cd06ae5d39b3, .lh = 0x67da60b1a110ff9, .ll = 0x14c940c27f248ced, .ex = -10, .sgn = 0x1}, /* j=8182 */
    {.hh = 0x8febc3cb332616ff, .hl = 0x7b6d1248c3e1fd3f, .lh = 0xd8ceb8a313143c9c, .ll = 0xe1449e20d24508b2, .ex = -10, .sgn = 0x1}, /* j=8183 */
    {.hh = 0xffe0055455887de0, .hl = 0x26828c92649a3a38, .lh = 0xc3585d8bbd3ac1b8, .ll = 0xd315929badc83115, .ex = -11, .sgn = 0x1}, /* j=8184 */
    {.hh = 0xdfe7839214b4e8ae, .hl = 0xda6959f7f0e01bf0, .lh = 0x3ff9151061ec91aa, .ll = 0x8f307c96ffa40bba, .ex = -11, .sgn = 0x1}, /* j=8185 */
    {.hh = 0xbfee023faf0c2480, .hl = 0xb47505bfa5a03b06, .lh = 0x2b8ff7c8377c9037, .ll = 0x6cd9392c0d17528b, .ex = -11, .sgn = 0x1}, /* j=8186 */
    {.hh = 0x9ff3814d2e4a36b2, .hl = 0xa8740b91c95df537, .lh = 0x5526081fe3d93a56, .ll = 0x3f68c3cd00e45f8b, .ex = -11, .sgn = 0x1}, /* j=8187 */
    {.hh = 0xfff0015535588833, .hl = 0x3c56c598c659c2a2, .lh = 0xf5c74f2f07e4f272, .ll = 0xc451b2e04ebd63ee, .ex = -12, .sgn = 0x1}, /* j=8188 */
    {.hh = 0xbff7008ff5e0c257, .hl = 0x379eba7e6465ff63, .lh = 0x3535a7e74bbb0089, .ll = 0x937a324b4307d36, .ex = -12, .sgn = 0x1}, /* j=8189 */
    {.hh = 0xfff8005551558885, .hl = 0xde026e271ee0549c, .lh = 0x8cd0b8002d083c9b, .ll = 0x2e9198222f25f83c, .ex = -13, .sgn = 0x1}, /* j=8190 */
    {.hh = 0x0, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 255, .sgn = 0x1}, /* j=8191 */
    {.hh = 0x0, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 255, .sgn = 0x1}, /* j=8192 */
    {.hh = 0xc004802401440c26, .hl = 0xdfeb485085f6f453, .lh = 0xb62f8fe41e621f91, .ll = 0x274178d2188f9ea7, .ex = -13, .sgn = 0x0}, /* j=8193 */
    {.hh = 0xa00640535a37a37a, .hl = 0x6bc1e20eac8448b4, .lh = 0xdf9f60ec72f7062, .ll = 0x6f190de53deca5e8, .ex = -12, .sgn = 0x0}, /* j=8194 */
    {.hh = 0xe00c40e4bd6e4efd, .hl = 0xc72446cc1bf728bd, .lh = 0x5e7d3be7e456c8a7, .ll = 0x180e4b0fc869da77, .ex = -12, .sgn = 0x0}, /* j=8195 */
    {.hh = 0x900a20f319a3e273, .hl = 0x569b26aaa485ea5b, .lh = 0xf5e4d6d9243bca81, .ll = 0x2714208ba14d04c8, .ex = -11, .sgn = 0x0}, /* j=8196 */
    {.hh = 0xb00f21bbe3e388ee, .hl = 0x5f69768284463b9b, .lh = 0x18baa187613466f, .ll = 0x5b94041491764cd7, .ex = -11, .sgn = 0x0}, /* j=8197 */
    {.hh = 0xd01522dcc4f87991, .hl = 0x14d9d76196d8043a, .lh = 0x179e870f7485c2eb, .ll = 0x7931552a9aec92fb, .ex = -11, .sgn = 0x0}, /* j=8198 */
    {.hh = 0xf01c2465c5e61b6f, .hl = 0x661e135f49a47c40, .lh = 0x42882a135aa4aa82, .ll = 0x82166812bed98c2d, .ex = -11, .sgn = 0x0}, /* j=8199 */
    {.hh = 0x881213337898871e, .hl = 0x9a31ba0cbc030352, .lh = 0xdc58134f3ce2ed6e, .ll = 0x6c0826bdc4127939, .ex = -10, .sgn = 0x0}, /* j=8200 */
    {.hh = 0x98169478296fad41, .hl = 0x7ad1e9c315328f7d, .lh = 0xcc0b6a758f391573, .ll = 0xa0bdfad5f77c4a7f, .ex = -10, .sgn = 0x0}, /* j=8201 */
    {.hh = 0xa81b9608fc3c50ec, .hl = 0xf105b66ec4703ede, .lh = 0x76406288c82a0e9c, .ll = 0xc06e5c3eaccc9b65, .ex = -10, .sgn = 0x0}, /* j=8202 */
    {.hh = 0xb82117edf8832797, .hl = 0xd6aef30cd312169a, .lh = 0x24ed9892618f8da1, .ll = 0x6091c8f53e42724e, .ex = -10, .sgn = 0x0}, /* j=8203 */
    {.hh = 0xc8271a2f2689e388, .hl = 0xe6e2acf8f4d4c249, .lh = 0x830dad0cbcb297de, .ll = 0x6f8eeb67afc8978d, .ex = -10, .sgn = 0x0}, /* j=8204 */
    {.hh = 0xd82d9cd48f574c00, .hl = 0x28bb3cd9f2a65fb4, .lh = 0x9c8f264a305434a2, .ll = 0x719267af576b8da7, .ex = -10, .sgn = 0x0}, /* j=8205 */
    {.hh = 0xe8349fe63cb35564, .hl = 0x224a96f5a7471c45, .lh = 0xd39ed746e5b2ba94, .ll = 0xdb742c4b8aeaa82e, .ex = -10, .sgn = 0x0}, /* j=8206 */
    {.hh = 0xf83c236c39273972, .hl = 0xd462b63756c87e80, .lh = 0x157ee3bffb879ef4, .ll = 0x9e384ccad23b6068, .ex = -10, .sgn = 0x0}, /* j=8207 */
    {.hh = 0x842213b747fec7bb, .hl = 0x3ff51287882500ed, .lh = 0x124d4848d57cf1e2, .ll = 0xca3575b044a7e9d6, .ex = -9, .sgn = 0x0}, /* j=8208 */
    {.hh = 0x8c2655faa6a1323f, .hl = 0x1ab9679b55f78a6a, .lh = 0x84963a91b59a785c, .ll = 0xf77577a5e4380677, .ex = -9, .sgn = 0x0}, /* j=8209 */
    {.hh = 0x942ad8843ee1a9cd, .hl = 0x17e4b7ac6c600cb4, .lh = 0x6767118f1a71745b, .ll = 0x98a85668eda9496f, .ex = -9, .sgn = 0x0}, /* j=8210 */
    {.hh = 0x9c2f9b581787cf0d, .hl = 0xfd1a09c848e3950d, .lh = 0xe0e66fad558345db, .ll = 0xd0914651b6655eb2, .ex = -9, .sgn = 0x0}, /* j=8211 */
    {.hh = 0xa4349e7a37bc21ed, .hl = 0x318b2ddd9d0a33b3, .lh = 0x9dd7a4359a20f4ae, .ll = 0x176ea6faed5bf263, .ex = -9, .sgn = 0x0}, /* j=8212 */
    {.hh = 0xac39e1eea7080dbc, .hl = 0x9dd91e52c79fd070, .lh = 0x184596be172aa3d1, .ll = 0x8a86b1bb367fee8, .ex = -9, .sgn = 0x0}, /* j=8213 */
    {.hh = 0xb43f65b96d55f55a, .hl = 0x72de1d99ce252efd, .lh = 0x149ec47368e2b10f, .ll = 0xb294ccdf83577dc0, .ex = -9, .sgn = 0x0}, /* j=8214 */
    {.hh = 0xbc4529de92f13f58, .hl = 0xd7bd1d62ef25480d, .lh = 0x7b1a44d708e0875b, .ll = 0x83a3462cb58566da, .ex = -9, .sgn = 0x0}, /* j=8215 */
    {.hh = 0xc44b2e6220866227, .hl = 0x7f921124f1ecb59e, .lh = 0x7696655fbc6715a6, .ll = 0xb820b79a7e7be96f, .ex = -9, .sgn = 0x0}, /* j=8216 */
    {.hh = 0xcc5173481f22f03f, .hl = 0x271ee1cd6d5cdf9d, .lh = 0x808228274a503b4b, .ll = 0x3ec027918b227262, .ex = -9, .sgn = 0x0}, /* j=8217 */
    {.hh = 0xd457f8949835a44e, .hl = 0xfad0cc8b5faea8cb, .lh = 0xa39dee1517d770f7, .ll = 0x87b62eb6a5071ae6, .ex = -9, .sgn = 0x0}, /* j=8218 */
    {.hh = 0xdc5ebe4b958e6d6b, .hl = 0xe57a0acb9d5cd4de, .lh = 0xd926113c7cf9cae4, .ll = 0x2cc6a626c5980669, .ex = -9, .sgn = 0x0}, /* j=8219 */
    {.hh = 0xe465c471215e7b41, .hl = 0xc81bb5a8d789f443, .lh = 0x8c30a4efa2085a91, .ll = 0xd8416fa14d8a0531, .ex = -9, .sgn = 0x0}, /* j=8220 */
    {.hh = 0xec6d0b0946384a46, .hl = 0x9b1beb40437575f4, .lh = 0x86e839aee0bd3623, .ll = 0x447c2a52014276c5, .ex = -9, .sgn = 0x0}, /* j=8221 */
    {.hh = 0xf47492180f0fafef, .hl = 0x7944509046652d98, .lh = 0xb65fa4f60aca7f76, .ll = 0x4229769700e69546, .ex = -9, .sgn = 0x0}, /* j=8222 */
    {.hh = 0xfc7c59a18739e6e7, .hl = 0x94e51ebff53a2f15, .lh = 0x12a476e601733b9f, .ll = 0xe7eb2f0e037ceedb, .ex = -9, .sgn = 0x0}, /* j=8223 */
    {.hh = 0x824230d4dd36cda4, .hl = 0x8bbc7f765b13ebbe, .lh = 0x68772abd1ce3258c, .ll = 0xe7cf18b488213d56, .ex = -8, .sgn = 0x0}, /* j=8224 */
    {.hh = 0x8646551a5a617b6b, .hl = 0xf61305ef7390939c, .lh = 0x1819673e1d680b66, .ll = 0x3bb660c119045dcb, .ex = -8, .sgn = 0x0}, /* j=8225 */
    {.hh = 0x8a4a99a34159d69f, .hl = 0x3abc32a78afd4b7a, .lh = 0xea68f0383acd9ff4, .ll = 0xe7d3305627ebab51, .ex = -8, .sgn = 0x0}, /* j=8226 */
    {.hh = 0x8e4efe71988d8426, .hl = 0x17596a598cb29436, .lh = 0x3dddd5d01649b409, .ll = 0x314d41b7d5759903, .ex = -8, .sgn = 0x0}, /* j=8227 */
    {.hh = 0x92538387669afa1b, .hl = 0x1c890bee9a9d743c, .lh = 0x7139a25d67ed976c, .ll = 0x715b9888be5228e7, .ex = -8, .sgn = 0x0}, /* j=8228 */
    {.hh = 0x965828e6b25185ec, .hl = 0xeaafbd07b543145c, .lh = 0xb28914b789a13376, .ll = 0xc286d314221430a0, .ex = -8, .sgn = 0x0}, /* j=8229 */
    {.hh = 0x9a5cee9182b15280, .hl = 0x6517bc4112d64b17, .lh = 0x3e099f46da82a60f, .ll = 0x5ac8c579eeb8644c, .ex = -8, .sgn = 0x0}, /* j=8230 */
    {.hh = 0x9e61d489deeb6e53, .hl = 0xdb94a1dfd653d3a5, .lh = 0x46503739bf42fb2e, .ll = 0xeabea04e3ce90eed, .ex = -8, .sgn = 0x0}, /* j=8231 */
    {.hh = 0xa266dad1ce61d1a3, .hl = 0x2ada01ce7ed3607f, .lh = 0x99d41a958f292523, .ll = 0x2b08613964628dc6, .ex = -8, .sgn = 0x0}, /* j=8232 */
    {.hh = 0xa66c016b58a7648c, .hl = 0xd3b36c029ea7bb5d, .lh = 0x36f49e73617ad152, .ll = 0x967313472d10eeee, .ex = -8, .sgn = 0x0}, /* j=8233 */
    {.hh = 0xaa71485885800538, .hl = 0x94c529f32403828, .lh = 0x253f332743a0ff31, .ll = 0x6548b3ed5877119a, .ex = -8, .sgn = 0x0}, /* j=8234 */
    {.hh = 0xae76af9b5ce08dfb, .hl = 0xb6b6676248bba138, .lh = 0xb6c98155115da0b7, .ll = 0x6fcf6732c378b707, .ex = -8, .sgn = 0x0}, /* j=8235 */
    {.hh = 0xb27c3735e6eedb86, .hl = 0x7bdd0c2a9c7a679a, .lh = 0x5fb1c32c4c750e98, .ll = 0xb5b84cc7549d184a, .ex = -8, .sgn = 0x0}, /* j=8236 */
    {.hh = 0xb47f0724b1906935, .hl = 0x23deb274e953a258, .lh = 0x990d7e42e0f2ed1b, .ll = 0xe6c4e93f7ba138fe, .ex = -8, .sgn = 0x0}, /* j=8237 */
    {.hh = 0xb884bf4697559ffa, .hl = 0xdae7e343fa859415, .lh = 0x5e4e41c1b5e85e4a, .ll = 0xf82d9009ecaad498, .ex = -8, .sgn = 0x0}, /* j=8238 */
    {.hh = 0xbc8a97c544fdd5eb, .hl = 0x17759bff5c717992, .lh = 0x949294749166f218, .ll = 0x780bea6b173a668d, .ex = -8, .sgn = 0x0}, /* j=8239 */
    {.hh = 0xc09090a2c35aa070, .hl = 0x52e7e4dde874dacd, .lh = 0xa952b40de6b303a6, .ll = 0x46e9f250087d1466, .ex = -8, .sgn = 0x0}, /* j=8240 */
    {.hh = 0xc496a9e11b6eb30c, .hl = 0xa88971f8277a4d10, .lh = 0xcc6e1825f856fe70, .ll = 0x18193291e2b36381, .ex = -8, .sgn = 0x0}, /* j=8241 */
    {.hh = 0xc89ce382566de587, .hl = 0x269de85f0df92587, .lh = 0xe65771fd08853e31, .ll = 0xa9b4b0681339900, .ex = -8, .sgn = 0x0}, /* j=8242 */
    {.hh = 0xcca33d887dbd3a1a, .hl = 0x180d255422c3377c, .lh = 0x35564b2dfc6a51cc, .ll = 0xad4c9d66d29442ba, .ex = -8, .sgn = 0x0}, /* j=8243 */
    {.hh = 0xd0a9b7f59af2e3a2, .hl = 0x46da70925ee85c05, .lh = 0x7b96716bbe5fc916, .ll = 0x949df14f9331856d, .ex = -8, .sgn = 0x0}, /* j=8244 */
    {.hh = 0xd4b052cbb7d64bcf, .hl = 0x37968ceafaf7b452, .lh = 0x866ac7791182f8a4, .ll = 0x879a1365f37550b8, .ex = -8, .sgn = 0x0}, /* j=8245 */
    {.hh = 0xd8b70e0cde601954, .hl = 0x5dfba4cfdd38a058, .lh = 0xc04093a8c6018f91, .ll = 0x343b553466b61759, .ex = -8, .sgn = 0x0}, /* j=8246 */
    {.hh = 0xdcbde9bb18ba361b, .hl = 0x4ae21abe75d5a19a, .lh = 0xd62f6d57c561e135, .ll = 0x49b45337d2f46eb8, .ex = -8, .sgn = 0x0}, /* j=8247 */
    {.hh = 0xe0c4e5d8713fd576, .hl = 0xd3bd4fd98a1e6fe5, .lh = 0x625d637fcaa83aa9, .ll = 0x32bb34856444782c, .ex = -8, .sgn = 0x0}, /* j=8248 */
    {.hh = 0xe4cc0266f27d7a57, .hl = 0x33cf7d5ebfb93ad3, .lh = 0x6a8d8f7f711ab809, .ll = 0xe15b1d297f6f796c, .ex = -8, .sgn = 0x0}, /* j=8249 */
    {.hh = 0xe8d33f68a730fd7f, .hl = 0x2743c805a4928086, .lh = 0x8923300474737280, .ll = 0xd02a66f2f0de4bef, .ex = -8, .sgn = 0x0}, /* j=8250 */
    {.hh = 0xecda9cdf9a4993ba, .hl = 0x5dbeb9795455a5, .lh = 0x5b168ca67fe36520, .ll = 0xaf0629427a97e44f, .ex = -8, .sgn = 0x0}, /* j=8251 */
    {.hh = 0xf0e21acdd6e7d412, .hl = 0xb6ed80852ae6fd62, .lh = 0xdfbedfa9caad16a4, .ll = 0x5f822584035372e8, .ex = -8, .sgn = 0x0}, /* j=8252 */
    {.hh = 0xf4e9b935685dbe0b, .hl = 0xf237cff1acb306b3, .lh = 0x8557e05d2318ee0, .ll = 0x5453667bec383296, .ex = -8, .sgn = 0x0}, /* j=8253 */
    {.hh = 0xf8f178185a2ebfd9, .hl = 0xd81648249cece4c, .lh = 0x3f4609735b1102e2, .ll = 0x295ae12732b36daf, .ex = -8, .sgn = 0x0}, /* j=8254 */
    {.hh = 0xfcf95778b80fbc98, .hl = 0x176cd56887ac7fe8, .lh = 0xaa02e8447626768d, .ll = 0x553f90bb90928bc4, .ex = -8, .sgn = 0x0}, /* j=8255 */
    {.hh = 0x8080abac46f38946, .hl = 0x662d417ced007a45, .lh = 0xc0be1062bd88c8e8, .ll = 0xe925964e76028722, .ex = -7, .sgn = 0x0}, /* j=8256 */
};

/* for 0 <= i < 64, T1_3[i] is a 256-bit nearest approximation of 2^(i/64),
   with error bounded by 2^-256 (both absolutely and relatively) */
static const qint64_t T1_3[] = {
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0},
    {.hh = 0x8164d1f3bc030773, .hl = 0x7be56527bd14def4, .lh = 0x9eb851655e2e5c4d, .ll = 0xd08075ac1f200e4c, .ex = 0, .sgn = 0x0},
    {.hh = 0x82cd8698ac2ba1d7, .hl = 0x3e2a475b46520bff, .lh = 0x29f1a4afbefa5d7c, .ll = 0x2502f15067378a17, .ex = 0, .sgn = 0x0},
    {.hh = 0x843a28c3acde4046, .hl = 0x1af92eca13fd1582, .lh = 0xd96b414ec4c9d06, .ll = 0x806bddad09d9c4a3, .ex = 0, .sgn = 0x0},
    {.hh = 0x85aac367cc487b14, .hl = 0xc5c95b8c2154c1b2, .lh = 0x148a0459e7585151, .ll = 0x5d42b362af1ee859, .ex = 0, .sgn = 0x0},
    {.hh = 0x871f61969e8d1010, .hl = 0x3a1727c57b52a956, .lh = 0x259ac58894f4fcb3, .ll = 0x5229a7352c9b247b, .ex = 0, .sgn = 0x0},
    {.hh = 0x88980e8092da8527, .hl = 0x5df8d76c98c67562, .lh = 0xe623d58b3772ba13, .ll = 0x8bc3587fb118c94d, .ex = 0, .sgn = 0x0},
    {.hh = 0x8a14d575496efd9a, .hl = 0x80ca1d92c3680c2, .lh = 0x259c4df53d76e910, .ll = 0xe9c32d22e935007d, .ex = 0, .sgn = 0x0},
    {.hh = 0x8b95c1e3ea8bd6e6, .hl = 0xfbe4628758a53c90, .lh = 0x1aa84ffbebac349f, .ll = 0x91e135ee84a3f734, .ex = 0, .sgn = 0x0},
    {.hh = 0x8d1adf5b7e5ba9e5, .hl = 0xb4c7b4968e41ad36, .lh = 0x183926ae7d718dc2, .ll = 0x724a166325437476, .ex = 0, .sgn = 0x0},
    {.hh = 0x8ea4398b45cd53c0, .hl = 0x2dc0144c8783d4c5, .lh = 0xa11037230b367828, .ll = 0xeb90ce3700bf59b6, .ex = 0, .sgn = 0x0},
    {.hh = 0x9031dc431466b1dc, .hl = 0x775814a8494e87e2, .lh = 0x43e90e15c2002132, .ll = 0x6f398dfe3f7903f1, .ex = 0, .sgn = 0x0},
    {.hh = 0x91c3d373ab11c336, .hl = 0xfd6d8e0ae5ac9d8, .lh = 0x1942b34816fb4f26, .ll = 0xf1203caf65bfb9b9, .ex = 0, .sgn = 0x0},
    {.hh = 0x935a2b2f13e6e92b, .hl = 0xd339940e9d924ee7, .lh = 0x2748c36eeaffa273, .ll = 0x583eab6852a22bb1, .ex = 0, .sgn = 0x0},
    {.hh = 0x94f4efa8fef70961, .hl = 0x2e8afad12551de54, .lh = 0x4856046901ff6c05, .ll = 0x35fb634c2e63a0f, .ex = 0, .sgn = 0x0},
    {.hh = 0x96942d3720185a00, .hl = 0x48ea9b683a9c22c4, .lh = 0xe0e68d9f200c5358, .ll = 0x9a22b1526bb6a2e4, .ex = 0, .sgn = 0x0},
    {.hh = 0x9837f0518db8a96f, .hl = 0x46ad23182e42f6f6, .lh = 0x5e139a1b14fa8178, .ll = 0xd78b65cbefa7bb70, .ex = 0, .sgn = 0x0},
    {.hh = 0x99e0459320b7fa64, .hl = 0xe43086cb34b5fcae, .lh = 0x8ac981ca9ceca6b3, .ll = 0x1560e51a5df911dc, .ex = 0, .sgn = 0x0},
    {.hh = 0x9b8d39b9d54e5538, .hl = 0xa2a817a2a3cc3f1f, .lh = 0x928b5fce34cdf21, .ll = 0x9769d9b0a908a786, .ex = 0, .sgn = 0x0},
    {.hh = 0x9d3ed9a72cffb750, .hl = 0xde494cf050e99b0b, .lh = 0x1ff17c29677589a0, .ll = 0x33a6fe2d4fd53e8a, .ex = 0, .sgn = 0x0},
    {.hh = 0x9ef5326091a111ad, .hl = 0xa0911f09ebb9fdd1, .lh = 0x65c15c122133e2a2, .ll = 0x21f977fe7c7fa118, .ex = 0, .sgn = 0x0},
    {.hh = 0xa0b0510fb9714fc2, .hl = 0x192dc79edb0fd9a9, .lh = 0x782a0735d02b1a20, .ll = 0x9f33f7bc78dc629f, .ex = 0, .sgn = 0x0},
    {.hh = 0xa27043030c496818, .hl = 0x9b7a04ef80cfdea7, .lh = 0x9da4384dbc2c8eae, .ll = 0x5a7a799221808de9, .ex = 0, .sgn = 0x0},
    {.hh = 0xa43515ae09e6809e, .hl = 0xd1db4831781e1ee, .lh = 0xbae743abfbc07376, .ll = 0x4c72418596cc5bd0, .ex = 0, .sgn = 0x0},
    {.hh = 0xa5fed6a9b15138ea, .hl = 0x1cbd7f621710701b, .lh = 0x1dd170ace2bcfc17, .ll = 0x2589c98a8290d3f0, .ex = 0, .sgn = 0x0},
    {.hh = 0xa7cd93b4e9653569, .hl = 0x9ec5b4d5039f72af, .lh = 0x1424bd194d3999e, .ll = 0xdd30939a1d1e929c, .ex = 0, .sgn = 0x0},
    {.hh = 0xa9a15ab4ea7c0ef8, .hl = 0x541e24ec3531fa73, .lh = 0x3951f214c02d824a, .ll = 0x325c9e2203504517, .ex = 0, .sgn = 0x0},
    {.hh = 0xab7a39b5a93ed337, .hl = 0x658023b2759e0079, .lh = 0x7ad59ec00ebe6393, .ll = 0x967357d6b36df9f8, .ex = 0, .sgn = 0x0},
    {.hh = 0xad583eea42a14ac6, .hl = 0x4980a8c8f59a2ec4, .lh = 0x6be409407034fded, .ll = 0xb165f141833a67da, .ex = 0, .sgn = 0x0},
    {.hh = 0xaf3b78ad690a4374, .hl = 0xdf26101ccbb35032, .lh = 0xa4502c14f429ded9, .ll = 0x5a8c73beaa946990, .ex = 0, .sgn = 0x0},
    {.hh = 0xb123f581d2ac258f, .hl = 0x87d037e96d215d8e, .lh = 0x757cfb9913adc577, .ll = 0x97ced890d5b0b0c0, .ex = 0, .sgn = 0x0},
    {.hh = 0xb311c412a9112489, .hl = 0x3ecf14dc798a519b, .lh = 0xfa6e051d6f8bc3ff, .ll = 0xba1e54cf684354df, .ex = 0, .sgn = 0x0},
    {.hh = 0xb504f333f9de6484, .hl = 0x597d89b3754abe9f, .lh = 0x1d6f60ba893ba84c, .ll = 0xed17ac8583339915, .ex = 0, .sgn = 0x0},
    {.hh = 0xb6fd91e328d17791, .hl = 0x7165f0ddd541a59, .lh = 0xf88abbe777df360e, .ll = 0x20850e774a86cd8f, .ex = 0, .sgn = 0x0},
    {.hh = 0xb8fbaf4762fb9ee9, .hl = 0x1b879778566b65a1, .lh = 0xa5ab16cf451056ed, .ll = 0x322d7893ed4da9a8, .ex = 0, .sgn = 0x0},
    {.hh = 0xbaff5ab2133e45fb, .hl = 0x74d519d24593838c, .lh = 0x2f30d0bdcaa516d, .ll = 0x6c373a75c2828202, .ex = 0, .sgn = 0x0},
    {.hh = 0xbd08a39f580c36be, .hl = 0xa8811fb66d0faf7a, .lh = 0x15b34bbcb0298f41, .ll = 0xd9a4be023ece032, .ex = 0, .sgn = 0x0},
    {.hh = 0xbf1799b67a731082, .hl = 0xe815d0abcbf0b850, .lh = 0xa13fc7e6faf9c830, .ll = 0x83ea957596be426d, .ex = 0, .sgn = 0x0},
    {.hh = 0xc12c4cca66709456, .hl = 0x7c457d59a50087b5, .lh = 0x6b2e5dd607a9969c, .ll = 0xdefefee72ae7a33d, .ex = 0, .sgn = 0x0},
    {.hh = 0xc346ccda24976407, .hl = 0x20ec856128b83a42, .lh = 0x6b9f89b7dabbcb2b, .ll = 0x5b718d616c4fef19, .ex = 0, .sgn = 0x0},
    {.hh = 0xc5672a115506dadd, .hl = 0x3e2ad0c964dd9f37, .lh = 0x6b0f939998251a36, .ll = 0xc7686006e4e6c093, .ex = 0, .sgn = 0x0},
    {.hh = 0xc78d74c8abb9b15c, .hl = 0xc13a2e3976c0277e, .lh = 0x4da570a2c574a304, .ll = 0xcea65224bc9900d0, .ex = 0, .sgn = 0x0},
    {.hh = 0xc9b9bd866e2f27a2, .hl = 0x80e1f92a0511697e, .lh = 0x257ac0db1f419377, .ll = 0xf4dd023ff93c7ffb, .ex = 0, .sgn = 0x0},
    {.hh = 0xcbec14fef2727c5c, .hl = 0xf4907c8f45ebf6dc, .lh = 0xeb8a25b7b40c0426, .ll = 0x639aa6f940962626, .ex = 0, .sgn = 0x0},
    {.hh = 0xce248c151f8480e3, .hl = 0xe235838f95f2c6ed, .lh = 0x6f28610b8c36485a, .ll = 0x2bbd398af35c079f, .ex = 0, .sgn = 0x0},
    {.hh = 0xd06333daef2b2594, .hl = 0xd6d45c6559a4d502, .lh = 0x11546d3ea28976d6, .ll = 0x2a33269ab05c3e5d, .ex = 0, .sgn = 0x0},
    {.hh = 0xd2a81d91f12ae45a, .hl = 0x12248e57c3de4028, .lh = 0x52029c0b81f7be57, .ll = 0xfa7663033f05357b, .ex = 0, .sgn = 0x0},
    {.hh = 0xd4f35aabcfedfa1f, .hl = 0x5921deffa6262c5a, .lh = 0xb8e7a32e5783da5c, .ll = 0xfa628009459a2417, .ex = 0, .sgn = 0x0},
    {.hh = 0xd744fccad69d6af4, .hl = 0x39a68bb9902d3fde, .lh = 0x1d733af522058b16, .ll = 0xb5c13ada0e77829a, .ex = 0, .sgn = 0x0},
    {.hh = 0xd99d15c278afd7b5, .hl = 0xfe873deca3e12bab, .lh = 0xc0edda4d891be43d, .ll = 0xb70cfbb1bdf6eb5d, .ex = 0, .sgn = 0x0},
    {.hh = 0xdbfbb797daf23755, .hl = 0x3d840d5a9e29aa64, .lh = 0x481e1ab725b12d56, .ll = 0x613b0d1dbfa0d717, .ex = 0, .sgn = 0x0},
    {.hh = 0xde60f4825e0e9123, .hl = 0xdd07a2d9e8466859, .lh = 0x1438495eacdf256, .ll = 0xcc2490c8643ef6b4, .ex = 0, .sgn = 0x0},
    {.hh = 0xe0ccdeec2a94e111, .hl = 0x65895048dd333ca, .lh = 0x224b251b33092002, .ll = 0x1cb99d3f1ff298a2, .ex = 0, .sgn = 0x0},
    {.hh = 0xe33f8972be8a5a51, .hl = 0x9bfe90795980eec, .lh = 0xf358a8d368fceaea, .ll = 0xfa8fcbb2e85b853f, .ex = 0, .sgn = 0x0},
    {.hh = 0xe5b906e77c8348a8, .hl = 0x1e5e8f4a4edbb0ec, .lh = 0xaacd6065b6e9f6ac, .ll = 0xcefcd5b62a14b818, .ex = 0, .sgn = 0x0},
    {.hh = 0xe8396a503c4bdc68, .hl = 0x791790d0ac70c7dd, .lh = 0xfe312f84fa665204, .ll = 0x3a1c6473409c261d, .ex = 0, .sgn = 0x0},
    {.hh = 0xeac0c6e7dd24392e, .hl = 0xd02d75b3706e54fa, .lh = 0xc4faace043b7f91c, .ll = 0x17d8d1e8ca31880b, .ex = 0, .sgn = 0x0},
    {.hh = 0xed4f301ed9942b84, .hl = 0x600d2db6a64bfb12, .lh = 0x3787630a764ae4c9, .ll = 0xc8e7c95b06416e6d, .ex = 0, .sgn = 0x0},
    {.hh = 0xefe4b99bdcdaf5cb, .hl = 0x46561cf6948db912, .lh = 0xd4a277eaddaa925c, .ll = 0x9392870834f21a53, .ex = 0, .sgn = 0x0},
    {.hh = 0xf281773c59ffb139, .hl = 0xe8980a9cc8f47a4b, .lh = 0x2cf0b49df0bd70e9, .ll = 0x7c43b0ea5d43228d, .ex = 0, .sgn = 0x0},
    {.hh = 0xf5257d152486cc2c, .hl = 0x7b9d0c7aed980fc3, .lh = 0x6f510308677709f5, .ll = 0xbdd80329364aa2a0, .ex = 0, .sgn = 0x0},
    {.hh = 0xf7d0df730ad13bb8, .hl = 0xfe90d496d60fb6ea, .lh = 0xe914ffb4723793f1, .ll = 0xef6797b5a11efb7c, .ex = 0, .sgn = 0x0},
    {.hh = 0xfa83b2db722a033a, .hl = 0x7c25bb14315d7fcc, .lh = 0x8006fe21a95d14dc, .ll = 0x4844b29bf4af18e8, .ex = 0, .sgn = 0x0},
    {.hh = 0xfd3e0c0cf486c174, .hl = 0x853f3a5931e0ee03, .lh = 0x61b7bb285a60791, .ll = 0x9d2285b6754edd61, .ex = 0, .sgn = 0x0},
};

/* for 0 <= i < 64, T2_3[i] is a 256-bit nearest approximation of 2^(i/2^12),
   with error bounded by 2^-256 (both absolutely and relatively) */
static const qint64_t T2_3[] = {
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0},
    {.hh = 0x80058baf7fee3b5d, .hl = 0x1c718b38e549cb93, .lh = 0x34a318717a85d198, .ll = 0x945b3ca6120b7d55, .ex = 0, .sgn = 0x0},
    {.hh = 0x800b179c82028fd0, .hl = 0x945e54e2ae18f2f0, .lh = 0x36ee988aaff03620, .ll = 0x76cc37ff9584ce15, .ex = 0, .sgn = 0x0},
    {.hh = 0x8010a3c708e73282, .hl = 0x2b96d62d51c15a07, .lh = 0x68b51f6090715cda, .ll = 0xe7a99fea0d150e10, .ex = 0, .sgn = 0x0},
    {.hh = 0x8016302f17467628, .hl = 0x3690dfe44d11d008, .lh = 0x403605216aed73f0, .ll = 0x49b8f71dcaa49423, .ex = 0, .sgn = 0x0},
    {.hh = 0x801bbcd4afcacb08, .hl = 0xe23a986bd3e626f0, .lh = 0x5bdd95c213fb273c, .ll = 0x9e66ef0d411e38d0, .ex = 0, .sgn = 0x0},
    {.hh = 0x802149b7d51ebefb, .hl = 0x7bdbadbc888aeb29, .lh = 0x201cf874aa8cafc4, .ll = 0x92f52199af16c4de, .ex = 0, .sgn = 0x0},
    {.hh = 0x8026d6d889ecfd69, .hl = 0xb904bbfb40d3a2b6, .lh = 0x84a6d5d525029ce2, .ll = 0x32bdce1e8420f0a8, .ex = 0, .sgn = 0x0},
    {.hh = 0x802c6436d0e04f50, .hl = 0xff8ce94a6797b3ce, .lh = 0x345f82f5b1fae20e, .ll = 0x3d0b18c06975c162, .ex = 0, .sgn = 0x0},
    {.hh = 0x8031f1d2aca39b43, .hl = 0xad9db772901d96b5, .lh = 0x8f6321e8e84c97d3, .ll = 0x3cc84ae246bf5abb, .ex = 0, .sgn = 0x0},
    {.hh = 0x80377fac1fe1e56a, .hl = 0x61cd0bffd7cfc682, .lh = 0xc0432e96c959387b, .ll = 0xb9c4ef247a66c427, .ex = 0, .sgn = 0x0},
    {.hh = 0x803d0dc32d464f85, .hl = 0x43456f71b96affd4, .lh = 0x34c51656768b5277, .ll = 0xb09b272d97fcc8, .ex = 0, .sgn = 0x0},
    {.hh = 0x80429c17d77c18ed, .hl = 0x49fc841afba9c3c5, .lh = 0xaedee98517f79365, .ll = 0x836514d2d81fdf14, .ex = 0, .sgn = 0x0},
    {.hh = 0x80482aaa212e9e95, .hl = 0x86f7b54f6c45c85e, .lh = 0x14747b1b6977fb14, .ll = 0xbec69e11682e0863, .ex = 0, .sgn = 0x0},
    {.hh = 0x804db97a0d095b0c, .hl = 0x6c9f1f7d1efcfe68, .lh = 0x6b994b07993e3561, .ll = 0x9e86080e1001781d, .ex = 0, .sgn = 0x0},
    {.hh = 0x805348879db7e67d, .hl = 0x171eb1ceef1d1f28, .lh = 0x5629bb4d6d20a74a, .ll = 0x7407003ab22ffa82, .ex = 0, .sgn = 0x0},
    {.hh = 0x8058d7d2d5e5f6b0, .hl = 0x94d589f608ee4aa2, .lh = 0x2adc0c3f864ba0f5, .ll = 0x9dc70119154b8f9a, .ex = 0, .sgn = 0x0},
    {.hh = 0x805e675bb83f5f0f, .hl = 0x2ed38ab8472b2143, .lh = 0xc40f99da125c266f, .ll = 0x94d103f4365ed44b, .ex = 0, .sgn = 0x0},
    {.hh = 0x8063f722477010a1, .hl = 0xb1652de1378af1a0, .lh = 0x8e5b66f89923f0ce, .ll = 0x4c9bb4d5be541cc3, .ex = 0, .sgn = 0x0},
    {.hh = 0x8069872686241a12, .hl = 0xb4ad9233a0390cac, .lh = 0x930d2b4079a002bd, .ll = 0x5cf73638639bbbd6, .ex = 0, .sgn = 0x0},
    {.hh = 0x806f17687707a7af, .hl = 0xe54ec5f966eb1872, .lh = 0x76754509f037248a, .ll = 0xd762ffd79b46d451, .ex = 0, .sgn = 0x0},
    {.hh = 0x8074a7e81cc7036b, .hl = 0x4d204ecfc11f4aaa, .lh = 0xf02c00376690ea79, .ll = 0x233e0911cc8de5f9, .ex = 0, .sgn = 0x0},
    {.hh = 0x807a38a57a0e94dc, .hl = 0x9bf3ef4d9be2d1e4, .lh = 0x6dbfe64309a2b072, .ll = 0x3c5cacc11f785d53, .ex = 0, .sgn = 0x0},
    {.hh = 0x807fc9a0918ae142, .hl = 0x7068ab2230585d12, .lh = 0x9fe6067d9e828773, .ll = 0xdc2ac4e4e300cb2c, .ex = 0, .sgn = 0x0},
    {.hh = 0x80855ad965e88b83, .hl = 0xa0cc0a49c10ea66a, .lh = 0xf0eb8fefacaf32d8, .ll = 0x78899d5679c5b99e, .ex = 0, .sgn = 0x0},
    {.hh = 0x808aec4ff9d45430, .hl = 0x84099bf6830f2767, .lh = 0x9a875f4408858619, .ll = 0xdfa0d299eae4aa3a, .ex = 0, .sgn = 0x0},
    {.hh = 0x80907e044ffb1984, .hl = 0x3aa8b9cbbc65a8aa, .lh = 0x8b22713e014be438, .ll = 0x47da7d37d3e079f6, .ex = 0, .sgn = 0x0},
    {.hh = 0x80960ff66b09d765, .hl = 0xf7d88c0928ba3946, .lh = 0xd1441da0989f9760, .ll = 0xff7e8daa7390655a, .ex = 0, .sgn = 0x0},
    {.hh = 0x809ba2264dada76a, .hl = 0x4a8a4f44bb703db6, .lh = 0x212bb24b9d533796, .ll = 0xfcdbcb683daab7f0, .ex = 0, .sgn = 0x0},
    {.hh = 0x80a13493fa93c0d4, .hl = 0x6699dc50dd96b773, .lh = 0x8712128a139dc866, .ll = 0xab9445c9a773244f, .ex = 0, .sgn = 0x0},
    {.hh = 0x80a6c73f74697897, .hl = 0x6e0472ed4ccfa2df, .lh = 0xc2857930dae5bef1, .ll = 0x95df5640f17d2dbe, .ex = 0, .sgn = 0x0},
    {.hh = 0x80ac5a28bddc4157, .hl = 0xba2dc7e0c72e51ba, .lh = 0x6765fb22ac558aca, .ll = 0x9a33e936c809a0e7, .ex = 0, .sgn = 0x0},
    {.hh = 0x80b1ed4fd999ab6c, .hl = 0x25335719b6e6fd20, .lh = 0x1f60261b05f1202, .ll = 0x3c355acba4df4fa, .ex = 0, .sgn = 0x0},
    {.hh = 0x80b780b4ca4f64df, .hl = 0x534dfa7417846aa4, .lh = 0x68164a4ae2414ea4, .ll = 0x2c64cb5808ef6fa6, .ex = 0, .sgn = 0x0},
    {.hh = 0x80bd145792ab3970, .hl = 0xfc41c5c2d5336ccc, .lh = 0x65250abea5b33d49, .ll = 0xad82dbaac7bfa2e3, .ex = 0, .sgn = 0x0},
    {.hh = 0x80c2a838355b1297, .hl = 0x34dc28baed8f3fde, .lh = 0x533c9eca3a17497d, .ll = 0xa1b8b14b109d4838, .ex = 0, .sgn = 0x0},
    {.hh = 0x80c83c56b50cf77f, .hl = 0xb880575ea03548c1, .lh = 0x4704388d9f1b3cd2, .ll = 0x86f4e188e2ca8a59, .ex = 0, .sgn = 0x0},
    {.hh = 0x80cdd0b3146f0d11, .hl = 0x32c1f98704428c71, .lh = 0x7e5ed5955b2d4887, .ll = 0x5b7e292a686df542, .ex = 0, .sgn = 0x0},
    {.hh = 0x80d3654d562f95ec, .hl = 0x890e222a5eb95372, .lh = 0x1197e58ebf689d43, .ll = 0x3896d92dd4431f8b, .ex = 0, .sgn = 0x0},
    {.hh = 0x80d8fa257cfcf26e, .hl = 0x24628efd9ca9d59a, .lh = 0xc5f4be776ef6a61a, .ll = 0x101735de189170ec, .ex = 0, .sgn = 0x0},
    {.hh = 0x80de8f3b8b85a0af, .hl = 0x3b13310f5ad57fb0, .lh = 0x9bad68937edd6b38, .ll = 0xeae4250b29447d4b, .ex = 0, .sgn = 0x0},
    {.hh = 0x80e4248f84783c87, .hl = 0x1a9dfefaeb616563, .lh = 0x94426c99024f23f0, .ll = 0x235b5252cafbaa02, .ex = 0, .sgn = 0x0},
    {.hh = 0x80e9ba216a837f8c, .hl = 0x718d1151d109bf97, .lh = 0x85189bdd7ac4b012, .ll = 0x9bea88f10391b325, .ex = 0, .sgn = 0x0},
    {.hh = 0x80ef4ff140564116, .hl = 0x996709da2e25f04b, .lh = 0xe18453f8dafeabf1, .ll = 0xf5867174289d8d94, .ex = 0, .sgn = 0x0},
    {.hh = 0x80f4e5ff089f763e, .hl = 0xe0adc640acaa6b0a, .lh = 0x8b6d28b5eb20d2f2, .ll = 0xb125fb6305bf7e6d, .ex = 0, .sgn = 0x0},
    {.hh = 0x80fa7c4ac60e31e1, .hl = 0xd4eb5edc6b341283, .lh = 0x370761b5ce7d7e44, .ll = 0xf1eb5df89b727f7c, .ex = 0, .sgn = 0x0},
    {.hh = 0x810012d47b51a4a0, .hl = 0x8ccd7223820719e3, .lh = 0x118525e07f78529c, .ll = 0x97f6dffe47385081, .ex = 0, .sgn = 0x0},
    {.hh = 0x8105a99c2b191ce1, .hl = 0xf24ebd6eb9ca4292, .lh = 0x70f4efb7d5c90568, .ll = 0x5301745d3b39c4d0, .ex = 0, .sgn = 0x0},
    {.hh = 0x810b40a1d81406d4, .hl = 0xcef03ab14a6654f, .lh = 0xa9c9ffc2ca67ffde, .ll = 0xada5b6f36036c85a, .ex = 0, .sgn = 0x0},
    {.hh = 0x8110d7e584f1ec6d, .hl = 0x4bf94297d1519822, .lh = 0x9ee96b903910b0f, .ll = 0x45fe2b1237a101fc, .ex = 0, .sgn = 0x0},
    {.hh = 0x81166f673462756d, .hl = 0xd0d8372f966cf15d, .lh = 0xb70c0ef050a08aa9, .ll = 0xd499953c6b9aa8f0, .ex = 0, .sgn = 0x0},
    {.hh = 0x811c0726e9156760, .hl = 0xb97931db7b7be2ec, .lh = 0x135c526104fa1c29, .ll = 0x215ef11d179cc996, .ex = 0, .sgn = 0x0},
    {.hh = 0x81219f24a5baa59d, .hl = 0x6abd3b0eab9c7047, .lh = 0xa7712808fe956328, .ll = 0x717f9b1d39438323, .ex = 0, .sgn = 0x0},
    {.hh = 0x812737606d023148, .hl = 0xdaf888e965081519, .lh = 0xada38ad7502e18a9, .ll = 0x6ebf0e93981c95f2, .ex = 0, .sgn = 0x0},
    {.hh = 0x812ccfda419c2956, .hl = 0xdc8046821f46122d, .lh = 0x8b2f742bd9d4370a, .ll = 0x5718a10a231edabf, .ex = 0, .sgn = 0x0},
    {.hh = 0x813268922638ca8b, .hl = 0x6846ad73a8d9027f, .lh = 0x1163a8bcf6bffce3, .ll = 0x2e1b37721d94b76, .ex = 0, .sgn = 0x0},
    {.hh = 0x813801881d886f7b, .hl = 0xe885724f14131286, .lh = 0x9cea3c3530355654, .ll = 0xf9c7f1fa9145fa7f, .ex = 0, .sgn = 0x0},
    {.hh = 0x813d9abc2a3b9090, .hl = 0x83768490519df895, .lh = 0x605362ea89eb07d4, .ll = 0x4794c4e3bcb98244, .ex = 0, .sgn = 0x0},
    {.hh = 0x8143342e4f02c405, .hl = 0x661b22b45e25de17, .lh = 0xa82b3121936ae61d, .ll = 0x91fe35aa6124aefb, .ex = 0, .sgn = 0x0},
    {.hh = 0x8148cdde8e8ebdec, .hl = 0xf11430fef78c6ee, .lh = 0x932801def6b0fb, .ll = 0x8aa35adbcc33b28e, .ex = 0, .sgn = 0x0},
    {.hh = 0x814e67cceb90502c, .hl = 0x99775205944eadc4, .lh = 0x2555ab2151b96f7c, .ll = 0x1ab6ca4ae6eda941, .ex = 0, .sgn = 0x0},
    {.hh = 0x815401f968b86a87, .hl = 0x7de463a40d18260, .lh = 0xdc941f1fd7a051c0, .ll = 0xdddfd0f8f59dec56, .ex = 0, .sgn = 0x0},
    {.hh = 0x81599c6408b81a94, .hl = 0x8f4a0b6748df795f, .lh = 0x988da3f28bde163d, .ll = 0x951855dd23786b9c, .ex = 0, .sgn = 0x0},
    {.hh = 0x815f370cce408bc8, .hl = 0xe2404468cfe5ab9f, .lh = 0x4db5f07dc6319207, .ll = 0x221ce2379e877086, .ex = 0, .sgn = 0x0},
};

/* The following is a degree-18 polynomial generated by Sollya, with zero
   constant coefficient, which approximates log(1+z) for |z| < 2^-13,
   see sollya/approximations_r3.sollya.
   The coefficients of largest degree are first.
   The relative error is bounded by 2^-255.0786, and the absolute error by
   2^-268.1653 (according to Sollya).
   The coefficient of degree 18 has only one non-zero limb (hh),
   those of degree 11-17 have only two non-zero limbs (hh and hl),
   and the other ones have full 256-bit precision.
   Table obtained by output_P3() from the accompanying file qint.sage.
*/
static const qint64_t P_3[] = {
    {.hh = 0xe38e39d490f62b2f, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -5, .sgn = 0x1}, /* degree 18 */
    {.hh = 0xf0f0f1e1e1d4e1cf, .hl = 0xbbb343000334fd0f, .lh = 0x0, .ll = 0x0, .ex = -5, .sgn = 0x0}, /* degree 17 */
    {.hh = 0xffffffffffff88b8, .hl = 0xc17633c5a3181e76, .lh = 0x0, .ll = 0x0, .ex = -5, .sgn = 0x1}, /* degree 16 */
    {.hh = 0x8888888888885088, .hl = 0x8f6a4426b02f93be, .lh = 0x0, .ll = 0x0, .ex = -4, .sgn = 0x0}, /* degree 15 */
    {.hh = 0x9249249249249249, .hl = 0x24a2676c009fc980, .lh = 0x0, .ll = 0x0, .ex = -4, .sgn = 0x1}, /* degree 14 */
    {.hh = 0x9d89d89d89d89d89, .hl = 0xd8ab89d5a96621f1, .lh = 0x0, .ll = 0x0, .ex = -4, .sgn = 0x0}, /* degree 13 */
    {.hh = 0xaaaaaaaaaaaaaaaa, .hl = 0xaaaaaaaaa815192a, .lh = 0x0, .ll = 0x0, .ex = -4, .sgn = 0x1}, /* degree 12 */
    {.hh = 0xba2e8ba2e8ba2e8b, .hl = 0xa2e8ba2e899ae964, .lh = 0x0, .ll = 0x0, .ex = -4, .sgn = 0x0}, /* degree 11 */
    {.hh = 0xcccccccccccccccc, .hl = 0xcccccccccccccccd, .lh = 0xcc491481418dc51, .ll = 0x1cebff4e21be093e, .ex = -4, .sgn = 0x1}, /* degree 10 */
    {.hh = 0xe38e38e38e38e38e, .hl = 0x38e38e38e38e38e3, .lh = 0xba38ce3dcedbed7d, .ll = 0xfa2cc6f77565683f, .ex = -4, .sgn = 0x0}, /* degree 9 */
    {.hh = 0xffffffffffffffff, .hl = 0xffffffffffffffff, .lh = 0xfffffffc6072860a, .ll = 0x786bd58754911c58, .ex = -4, .sgn = 0x1}, /* degree 8 */
    {.hh = 0x9249249249249249, .hl = 0x2492492492492492, .lh = 0x492492481c930545, .ll = 0xf0298bcd6e1b2310, .ex = -3, .sgn = 0x0}, /* degree 7 */
    {.hh = 0xaaaaaaaaaaaaaaaa, .hl = 0xaaaaaaaaaaaaaaaa, .lh = 0xaaaaaaaaaaaaaab8, .ll = 0xd8b61a619485f089, .ex = -3, .sgn = 0x1}, /* degree 6 */
    {.hh = 0xcccccccccccccccc, .hl = 0xcccccccccccccccc, .lh = 0xccccccccccccccd2, .ll = 0xccc65d183d01d5ef, .ex = -3, .sgn = 0x0}, /* degree 5 */
    {.hh = 0xffffffffffffffff, .hl = 0xffffffffffffffff, .lh = 0xffffffffffffffff, .ll = 0xffffffcdc3a6a23c, .ex = -3, .sgn = 0x1}, /* degree 4 */
    {.hh = 0xaaaaaaaaaaaaaaaa, .hl = 0xaaaaaaaaaaaaaaaa, .lh = 0xaaaaaaaaaaaaaaaa, .ll = 0xaaaaaaa4aab50b70, .ex = -2, .sgn = 0x0}, /* degree 3 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0xd, .ex = -1, .sgn = 0x1}, /* degree 2 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* degree 1 */
};

/* The following is a degree-14 polynomial generated by Sollya,
   which approximates exp(z) for |z| < 0.00016923,
   see sollya/approximations_r3.sollya.
   The coefficients of largest degree are first.
   The absolute error is bounded by 2^-242.181, thus relative error bounded
   by 2^-242.181/exp(-0.00016923) < 2^-242.180.
*/
static const qint64_t Q_3[] = {
    {.hh = 0xc9cba547af749429, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -37, .sgn = 0x0}, /* degree 14 */
    {.hh = 0xb092309ec73dd7db, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = -33, .sgn = 0x0}, /* degree 13 */
    {.hh = 0x8f76c77fc6c4bda8, .hl = 0xcd9aab7578033f6d, .lh = 0x0, .ll = 0x0, .ex = -29, .sgn = 0x0}, /* degree 12 */
    {.hh = 0xd7322b3faa271c7d, .hl = 0xb3537cbfd60dcb9, .lh = 0x0, .ll = 0x0, .ex = -26, .sgn = 0x0}, /* degree 11 */
    {.hh = 0x93f27dbbc4fae397, .hl = 0x780b69f6554de3d9, .lh = 0x0, .ll = 0x0, .ex = -22, .sgn = 0x0}, /* degree 10 */
    {.hh = 0xb8ef1d2ab6399c7d, .hl = 0x560e44741a6a8e66, .lh = 0x0, .ll = 0x0, .ex = -19, .sgn = 0x0}, /* degree 9 */
    {.hh = 0xd00d00d00d00d00d, .hl = 0xd00d00d00d00cf, .lh = 0x0, .ll = 0x0, .ex = -16, .sgn = 0x0}, /* degree 8 */
    {.hh = 0xd00d00d00d00d00d, .hl = 0xd00d00d00d00ce, .lh = 0xca5a80878f19216b, .ll = 0xd236a7fa15252936, .ex = -13, .sgn = 0x0}, /* degree 7 */
    {.hh = 0xb60b60b60b60b60b, .hl = 0x60b60b60b60b60b6, .lh = 0xb60b60be6ac2e60, .ll = 0x800a9987617257e3, .ex = -10, .sgn = 0x0}, /* degree 6 */
    {.hh = 0x8888888888888888, .hl = 0x8888888888888888, .lh = 0x888888890ac16c5a, .ll = 0xf78e687c535a714, .ex = -7, .sgn = 0x0}, /* degree 5 */
    {.hh = 0xaaaaaaaaaaaaaaaa, .hl = 0xaaaaaaaaaaaaaaaa, .lh = 0xaaaaaaaaaaaaaaaa, .ll = 0x6b3ad4c251cd03d5, .ex = -5, .sgn = 0x0}, /* degree 4 */
    {.hh = 0xaaaaaaaaaaaaaaaa, .hl = 0xaaaaaaaaaaaaaaaa, .lh = 0xaaaaaaaaaaaaaaaa, .ll = 0x4df8c3de374c499e, .ex = -3, .sgn = 0x0}, /* degree 3 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x1446e270, .ex = -1, .sgn = 0x0}, /* degree 2 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x262ce809, .ex = 0, .sgn = 0x0}, /* degree 1 */
    {.hh = 0x8000000000000000, .hl = 0x0, .lh = 0x0, .ll = 0x0, .ex = 0, .sgn = 0x0}, /* degree 0 */
};

#endif
