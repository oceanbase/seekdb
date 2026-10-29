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

/* For reference, the files in this directory were copied from the private
   git repository gitlab.inria.fr:zimmerma/core-math-power.git, branch
   two_step, revision f153eb68, before edition in the CORE-MATH git
   repository. */

/* References:
   [1] Note on FastTwoSum with Directed Rounding, Paul Zimmermann,
       https://hal.inria.fr/hal-03798376/, 2022.
   [2] An efficient rounding boundary test for pow(x,y) in double precision,
       Christoph Lauter, Vincent Lefèvre,
       https://hal-ens-lyon.archives-ouvertes.fr/ensl-00169409, 2007.
   [3] Arrondi correct de fonctions mathématiques, Fonctions univariées et
       bivariées, certification et automatisation, Christoph Quirin Lauter,
       PhD thesis, 2008, in french,
       http://www.ens-lyon.fr/LIP/Pub/Rapports/PhD/PhD2008/PhD2008-07.pdf
   [4] An efficient rounding boundary test for pow(x,y) in double precision,
       Christoph Lauter, Vincent Lefèvre, IEEE Transactions on Computers,
       volume 58, number 2, 197-207, 2009.
   [5] Towards a correctly-rounded and fast power function in binary64
       arithmetic, Tom Hubrecht, Claude-Pierre Jeannerod, Paul Zimmermann,
       ARITH 2023 - 30th IEEE Symposium on Computer Arithmetic, 2023.
       Detailed version (with full proofs) available at
       https://inria.hal.science/hal-04159652.
   [6] On Ziv's rounding test, F. De Dinechin, C. Lauter, J.-M. Muller,
       S. Torres, ACM Trans. Math. Soft., volume 39, number 3, 2013.

   This code corresponds to reference [5].       
*/

#include <stdio.h> // needed in case of rounding-test failure
#include <stdint.h>
#include <stdlib.h> // for exit
#ifdef CORE_MATH_SUPPORT_ERRNO
#include <errno.h>
#endif
#include <fenv.h> // for fegetround, FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, ...
#ifdef __x86_64__
#include <x86intrin.h>
#define FLAG_T uint32_t
#else
#define FLAG_T fexcept_t
#endif

#include "pow.h"

// Warning: clang also defines __GNUC__
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

#pragma STDC FENV_ACCESS ON

#ifndef POW_ITERATION
#define POW_ITERATION 15
#endif

#define ENABLE_FP (POW_ITERATION & 0x1)
#define ENABLE_ZIV2 (POW_ITERATION & 0x2)
#define ENABLE_EXACT (POW_ITERATION & 0x4)
#define ENABLE_ZIV3 (POW_ITERATION & 0x8)

static FLAG_T
get_flag (void)
{
#if defined(__x86_64__)
  return _mm_getcsr ();
#else
  fexcept_t flag;
  fegetexceptflag (&flag, FE_INEXACT | FE_UNDERFLOW);
  return flag;
#endif
}

static void
set_flag (FLAG_T flag)
{
#if defined(__x86_64__)
  _mm_setcsr (flag);
#else
  fesetexceptflag (&flag, FE_INEXACT | FE_UNDERFLOW);
#endif
}

/***************** polynomial approximations of exp(z) ***********************/

/* Given z such that |z| < 2^-12.905,
   this routine puts in qh+ql an approximation of exp(z) such that

   | (qh+ql) / exp(z) - 1 | < 2^-64.902632

   and |ql| <= 2^-51.999. See Lemma 6 from reference [5].
*/
static inline void q_1 (double *qh, double *ql, double z) {
  double q, h0, h1, l1;

  q = __builtin_fma (Q_1[4], z, Q_1[3]);

  q = __builtin_fma (q, z, Q_1[2]);

  h0 = __builtin_fma (q, z, Q_1[1]);

  a_mul (&h1, &l1, z, h0);

  fast_sum (qh, ql, Q_1[0], h1, l1);
}

/* Given |y| < 0.00016923 < 2^-12.52, put in r an approximation of exp(y),
   with relative error bounded by 2^-122.29.
 */
static inline void q_2 (dint64_t *r, dint64_t *y) {
  mul_dint_11 (r, y, &Q_2[0]);
  /* |y| < 2^-12.52 and |Q_2[0]| < 2^-12.29 thus |r| < 2^-24.81;
     mul_dint_11() is exact, and the low part of Q_2[0] is 0,
     but we ignore y.lo * Q_2[0], which is bounded by
     ulp64(2^-12.52)*2^-12.29 < 2^-88.29 */
  add_dint_11 (r, &Q_2[1], r);
  /* |Q_2[1]| < 2^-9.49 and |r_in| < 2^-24.81 thus |r| < 2^-9.48;
     the rounding error on add_dint_11() is bounded by 2 ulps,
     thus < 2^-72. We also ignore the low part of r_in, which is
     bounded by ulp64(2^-24.81) = 2^-88.
     The total error on r is thus < 2^-88.29 + 2^-72 + 2^-88 < 2^-71.99.
     This error is multiplied by y^6 below, thus contributes to at most
     err1 = 2^-71.99*y^6 < 2^-147.16 */

  mul_dint_11 (r, y, r);
  /* |y| < 2^-12.52 and |r_in| < 2^-9.48 thus |r| < 2^-22.00;
     mul_dint_11() is exact;
     we ignore y * low(r_in) < 2^-12.52*ulp64(2^-9.48) = 2^-85.52;
     we also ignore low(y) * r_in < ulp64(2^-12.52)*2^-9.48 = 2^-85.48 */
  add_dint_11 (r, &Q_2[2], r);
  /* |Q_2[2]| < 2^-6.90 and |r_in| < 2^-22.00 thus |r| < 2^-6.89;
     the rounding error on add_dint_11() is bounded by 2 ulps,
     thus < 2^-69. We also ignore the low part of r_in, which is
     bounded by ulp64(2^-22.00-eps) = 2^-86.
     The total error on r is thus < 2^-85.52+2^-85.48+2^-69+2^-86 < 2^-68.99.
     This error is multiplied by y^5 below, thus contributes to at most
     err2 = 2^-68.99*y^5 < 2^-131.64 */

  mul_dint_11 (r, y, r);
  /* |y| < 2^-12.52 and |r_in| < 2^-6.89 thus |r| < 2^-19.41;
     mul_dint_11() is exact;
     we ignore y * low(r_in) < 2^-12.52*ulp64(2^-6.89) = 2^-82.52;
     we also ignore low(y) * r_in < ulp64(2^-12.52)*2^-6.89 = 2^-82.89 */
  add_dint (r, &Q_2[3], r);
  /* |Q_2[3]| < 2^-4.58 and |r_in| < 2^-19.41 thus |r| < 2^-4.57;
     the rounding error on add_dint() is bounded by 2 ulps,
     thus < 2^-131.
     The total error on r is thus < 2^-82.52+2^-82.89+2^-131 < 2^-81.69.
     This error is multiplied by y^4 below, thus contributes to at most
     err3 = 2^-81.69*y^4 < 2^-131.80 */

  mul_dint (r, y, r);
  /* |y| < 2^-12.52 and |r_in| < 2^-4.57 thus |r| < 2^-17.09;
     the rounding error of mul_dint() is bounded by 6 ulps,
     thus < 2^-142.41 */
  add_dint (r, &Q_2[4], r);
  /* |Q_2[4]| < 2^-2.58 and |r_in| < 2^-17.09 thus |r| < 2^-2.57;
     the rounding error on add_dint() is bounded by 2 ulps,
     thus < 2^-129.
     The total error on r is thus < 2^-142.41+2^-129 < 2^-128.99.
     This error is multiplied by y^3 below, thus contributes to at most
     err4 = 2^-128.99*y^3 < 2^-166.57 */

  mul_dint (r, y, r);
  /* |y| < 2^-12.52 and |r_in| < 2^-2.57 thus |r| < 2^-15.09;
     the rounding error of mul_dint() is bounded by 6 ulps,
     thus < 2^-140.41 */
  add_dint (r, &Q_2[5], r);
  /* |Q_2[5]| < 2^-0.99 and |r_in| < 2^-15.09 thus |r| < 2^-0.98;
     the rounding error on add_dint() is bounded by 2 ulps,
     thus < 2^-127.
     The total error on r is thus < 2^-140.41+2^-127 < 2^-126.99.
     This error is multiplied by y^2 below, thus contributes to at most
     err5 = 2^-126.99*y^2 < 2^-152.04 */

  mul_dint (r, y, r);
  /* |y| < 2^-12.52 and |r_in| < 2^-0.98 thus |r| < 2^-13.50;
     the rounding error of mul_dint() is bounded by 6 ulps,
     thus < 2^-138.41 */
  add_dint (r, &Q_2[6], r);
  /* |Q_2[6]| < 1.01 and |r_in| < 2^-13.50 thus |r| < 1.02;
     the rounding error on add_dint() is bounded by 2 ulps,
     thus < 2^-126.
     The total error on r is thus < 2^-138.41+2^-126 < 2^-125.99.
     This error is multiplied by y below, thus contributes to at most
     err6 = 2^-125.99*y < 2^-138.51 */

  mul_dint (r, y, r);
  /* |y| < 2^-12.52 and |r_in| < 1.02 thus |r| < 2^-12.49;
     the rounding error of mul_dint() is bounded by 6 ulps,
     thus < 2^-137.41 */
  add_dint (r, &Q_2[7], r);
  /* |Q_2[7]| < 1 and |r_in| < 2^-12.49 thus |r| < 1.01;
     the rounding error on add_dint() is bounded by 2 ulps,
     thus < 2^-126.
     The total error on r is thus err7 < 2^-137.41+2^-126 < 2^-125.99. */

  /* Total absolute errors:
     err1 < 2^-147.16
     err2 < 2^-131.64
     err3 < 2^-131.80
     err4 < 2^-166.57
     err5 < 2^-152.04
     err6 < 2^-138.51
     err7 < 2^-125.99
     Total err1+...+err7 < 2^-125.93. Since |y| < 0.00016923, this translates
     into a relative error < 2^-125.93/exp(-0.00016923) < 2^-125.92.
     We also have the approximation error from the Sollya polynomial,
     which is bounded by 2^-122.415 (relative).
     The total relative error is thus bounded with e1=2^-125.92 and
     e2=2^-122.415 by (1+e1)*(1+e2)-1 < 2^-122.29.
  */
}

/* Given |y| < 0.00016923 < 2^-12.52, put in r an approximation of exp(y),
   with 0.999830 < r < 1.000170, absolute/relative error bounded by 2^-241.11.
   The error analysis is from the analyze_q3() function in the accompanying
   file qint.sage. */
static inline void q_3 (qint64_t *r, qint64_t *y) {
  /* the absolute error from the Sollya polynomial is at most 2^-242.181 */

  /* the coefficients of degree 13 and 14 (Q_3[0-1]) have 64 bits only */

  mul_qint_11 (r, y, &Q_3[0]);
  /* here |y| < 0.00016923 and |Q_3[0]| < 2^-36.34 thus |r| < 2^-48.86;
     mul_qint_11() is exact (providing a 128-bit product), but we ignore
     the low part of y, which contributes to at most ulp64(y)*Q_3[0]
     < 2^-112.34. */
  add_qint_22 (r, &Q_3[1], r);
  /* here |Q_3[1]| < 2^-32.53 and |r_in| < 2^-48.86, thus |r| < 2^-32.52;
     the rounding error of add_qint_22() is bounded by 2 ulps_128, thus 2^-159;
     the low part of Q_3[1] is 0, but we ignore the low part of r_in, which
     contributes to at most ulp128(r_in) = 2^-176.
     The total error on for the two above instructions is thus bounded by
     2^-112.34 + 2^-159 + 2^-176 < 2^-112.34.
     This error is multiplied by y^13 below, thus contributes to at most
     err1 < 2^-275.21 (confirmed by analyze_q3()).
  */

  /* the coefficients of degree 8 to 12 (Q_3[2-6]) have 128 bits only */

  for (int32_t k = 2; k < 7; k++) {
    mul_qint_22 (r, y, r);
    /* mul_qint_22() is exact (with a 256-bit result), but we might ignore
       as input y*low(r) and low(y)*r, which accounts for y*ulp128(r_in)
       + ulp128(y)*r */
    add_qint_22 (r, &Q_3[k], r);
    /* the rounding error in add_qint_22() is less than 2 ulps_128,
       we have no ignored part of Q_3[k],
       but we ignore the low 128 bits from r_in,
       and the error of these two instructions is multiplied by y^(15-k) */
  }

  /* the coefficients of degree 0 to 7 (Q_3[7-14]) have 256 bits,
     thus we use the full add_qint() routine to add them, but for
     the larger degrees, we truncate to 3 limbs */

  for (int32_t k = 7; k < 12; k++) {
    mul_qint_33 (r, y, r);
    /* the rounding error of mul_qint_33() is at most 6 ulps(256),
       and we ignore as input y*low(r) and low(y)*r,
       which accounts for y*ulp192(r_in) + ulp192(y)*r */
    add_qint (r, &Q_3[k], r);
    /* the rounding error of add_qint() is bounded by 2 ulps(256) */
  }

  for (int32_t k = 12; k < 15; k++) {
    mul_qint (r, y, r);
    /* the rounding error of mul_qint() is at most 14 ulps, and there is
       no ignored part */
    add_qint (r, &Q_3[k], r);
    /* the rounding error of add_qint() is bounded by 2 ulps(256) */
  }

  /* The function analyze_q3() from the accompanying qint.sage file gives
     a total absolute error bounded by 2^-241.113. Since r > exp(-0.00016923),
     this corresponds to a relative error < 2^-241.113/exp(-0.00016923)
     < 2^-241.11. */
}

/**************** polynomial approximations of log(1+x) **********************/

/* Given |z| <= 33*2^-13, with z an integer multiple of 2^-61,
   this routine puts in ph+pl an approximation of log(1+z)-z such that

   | ph + pl - (log(1 + z) - z) | < 2^-75.492

   with |ph| < 2^-16.9, |pl| < 2^-25.446.
   Moreover if z<>0, and assuming further |z| < 32*2^-13, the relative error
   satisfies:

   | (z + ph + pl) / log(1+z) - 1 | < 2^-67.441

   See Lemma 2 from reference [5].
*/
static inline void p_1 (double *ph, double *pl, double z) {
  double wh, wl;
  a_mul (&wh, &wl, z, z);
  double t = __builtin_fma (P_1[5], z, P_1[4]);
  double u = __builtin_fma (P_1[3], z, P_1[2]);
  double v = __builtin_fma (P_1[1], z, P_1[0]);
  u = __builtin_fma (t, wh, u);
  v = __builtin_fma (u, wh, v);
  u = v * wh;
  *ph = -0.5 * wh;
  *pl = __builtin_fma (u, z, -0.5 * wl);
}

// Approximation for the second iteration
// Return in r an approximation of log(1+z) for |z| <= 2^-13
// The low part of z is assumed to be 0.
// with relative error bounded by 2^-124.82 and absolute error by 2^-137.95
// The coefficients of degree 6 to 9 (P_2[0] to P_2[3]) have precision 64 bits only.
// For the error analysis, see the analyze_p2() function in the
// accompanying dint.sage file.
static inline void p_2 (dint64_t *r, dint64_t *z) {
  /* the error analysis below first consider the absolute error, then we
     switch to the relative error after the final multiplication by z */
  mul_dint_11 (r, z, &P_2[0]);
  /* here |z| <= 2^-13 and |P_2[0]| < 2^-3.16 thus |r| < 2^-16.16;
     mul_dint_11() is exact (the low part of z and P_2[0] is 0),
     but we ignore the low part of r below, which contributes to
     at most ulp64(2^-16.16) < 2^-80. */
  add_dint_11 (r, &P_2[1], r);
  /* here |P_2[1]| < 2^-2.99 and |r_in| < 2^-16.16, thus |r| < 2^-2.98;
     the rounding error of add_dint_11() is bounded by 2 ulps_64, thus 2^-65;
     the low part of P_2[1] is 0, but we ignore the low part of r_in (see
     above).
     The total error for the two above instructions is thus bounded by
     2^-80+2^-65 < 2^-64.99. This error is multiplied by z^7
     below (not counting the final multiplication by z),
     thus contributes to at most err1 < 2^-155.99. */

  mul_dint_11 (r, z, r);
  /* here |z| <= 2^-13 and |r_in| < 2^-2.98 thus |r| < 2^-15.98;
     mul_dint_11() is exact (the low part of z and r_in is 0) */
  add_dint_11 (r, &P_2[2], r);
  /* here |P_2[2]| < 2^-2.80 and |r_in| < 2^-15.98, thus |r| < 2^-2.79;
     the rounding error of add_dint_11() is bounded by 2 ulps_64, thus 2^-65;
     the low part of P_2[2] is 0, but we ignore the low part of r_in, which
     contributes to at most ulp_64(2^-15.98) < 2^-79.
     The total error for the two above instructions is thus bounded by
     2^-65+2^-79 < 2^-64.99. This error is multiplied by z^6
     below (not counting the final multiplication by z),
     thus contributes to at most err2 < 2^-142.99. */

  mul_dint_11 (r, z, r);
  /* here |z| <= 2^-13 and |r_in| < 2^-2.79 thus |r| < 2^-15.79;
     mul_dint_11() is exact (the low part of z and r_in is 0) */
  add_dint_11 (r, &P_2[3], r);
  /* here |P_2[3]| < 2^-2.58 and |r_in| < 2^-15.79, thus |r| < 2^-2.57;
     the rounding error of add_dint_11() is bounded by 2 ulps_64, thus 2^-65;
     the low part of P_2[3] is 0, but we ignore the low part of r_in, which
     contributes to at most ulp_64(2^-15.79) < 2^-79.
     The total error for the two above instructions is thus bounded by
     2^-65+2^-79 < 2^-64.99. This error is multiplied by z^5
     below (not counting the final multiplication by z),
     thus contributes to at most err3 < 2^-129.99. */

  mul_dint_11 (r, z, r);
  /* here |z| <= 2^-13 and |r_in| < 2^-2.57 thus |r| < 2^-15.57;
     mul_dint_11() is exact (the low part of z and r_in is 0) */
  add_dint (r, &P_2[4], r);
  /* here |P_2[4]| < 2^-2.32 and |r_in| < 2^-15.57, thus |r| < 2^-2.31;
     the rounding error of add_dint() is bounded by 2 ulps_128, thus 2^-129.
     The total error for the two above instructions is thus bounded by 2^-129.
     This error is multiplied by z^4 below (not counting the final
     multiplication by z), thus contributes to at most err4 < 2^-181. */

  mul_dint_21 (r, r, z);
  /* here |z| <= 2^-13 and |r_in| < 2^-2.31 thus |r| < 2^-15.31;
     the rounding error of mul_dint_21() is bounded by 2 ulps, thus 2^-142. */
  add_dint (r, &P_2[5], r);
  /* here |P_2[5]| < 2^-1.999 and |r_in| < 2^-15.31, thus |r| < 2^-1.99;
     the rounding error of add_dint() is bounded by 2 ulps_128, thus 2^-128.
     The total error for the two above instructions is thus bounded by
     2^-142+2^-128 < 2^-127.99. This error is multiplied by z^3
     below (not counting the final multiplication by z),
     thus contributes to at most err5 < 2^-166.99. */

  mul_dint_21 (r, r, z);
  /* here |z| <= 2^-13 and |r_in| < 2^-1.99 thus |r| < 2^-14.99;
     the rounding error of mul_dint_21() is bounded by 2 ulps, thus 2^-141. */
  add_dint (r, &P_2[6], r);
  /* here |P_2[6]| < 2^-1.58 and |r_in| < 2^-14.99, thus |r| < 2^-1.57;
     the rounding error of add_dint() is bounded by 2 ulps_128, thus 2^-128.
     The total error for the two above instructions is thus bounded by
     2^-141+2^-128 < 2^-127.99. This error is multiplied by z^2
     below (not counting the final multiplication by z),
     thus contributes to at most err6 < 2^-153.99. */

  mul_dint_21 (r, r, z);
  /* here |z| <= 2^-13 and |r_in| < 2^-1.57 thus |r| < 2^-14.57;
     the rounding error of mul_dint_21() is bounded by 2 ulps, thus 2^-141. */
  add_dint (r, &P_2[7], r);
  /* here |P_2[7]| < 2^-1 and |r_in| < 2^-14.57, thus |r| < 2^-0.99;
     the rounding error of add_dint() is bounded by 2 ulps_128, thus 2^-127.
     The total error for the two above instructions is thus bounded by
     2^-141+2^-127 < 2^-126.99. This error is multiplied by z
     below (not counting the final multiplication by z),
     thus contributes to at most err7 < 2^-133.99. */

  mul_dint_21 (r, r, z);
  /* here |z| <= 2^-13 and |r_in| < 2^-0.99 thus |r| < 2^-13.99;
     the rounding error of mul_dint_21() is bounded by 6 ulps, thus 2^-140. */
  add_dint (r, &P_2[8], r);
  /* here P_2[8] = 1 and |r_in| < 2^-13.99, thus |r| < 1.0001;
     the rounding error of add_dint() is bounded by 2 ulps_128, thus 2^-126.
     The total error for the two above instructions is thus bounded by
     err8 = 2^-140+2^-126 < 2^-125.99. */

  /* Maximal absolute error on r up to here:
     err1 < 2^-155.99
     err2 < 2^-142.99
     err3 < 2^-129.99
     err4 < 2^-181
     err5 < 2^-166.99
     err6 < 2^-153.99
     err7 < 2^-133.99
     err8 < 2^-125.99
     Total absolute error < err1+...+err8 < 2^-125.89.
     We now transform this into relative error, knowing that
     |r| >= 1 - 2^-13.99, this gives a relative error < 2^-125.88
     (2^-125.90 with analyze_p2()).
  */

  mul_dint_21 (r, r, z);
  /* We bound both the absolute and relative error.

     Absolute error:
     here |z| <= 2^-13 and |r_in| < 1.01 thus |r| < 2^-12.98;
     the rounding error of mul_dint_21() is bounded by 2 ulps, thus 2^-139.
     We add the previous total absolute error on r_in multiplied by z,
     which gives 2^-139+2^-125.88*2^-13 < 2^-137.93
     (2^-137.95 with analyze_p2()).

     The relative error on the Sollya polynomial is bounded by
     eps0 = 2^-128.316.
     The relative error on r_in is bounded by eps1=2^-125.88
     and the error from mul_dint_21 is bounded by 2 ulps
     (which yields a relative error less than eps2=2*2^-127).
     This yields a relative error on r less than:
     |(1 + eps0) * (1 + eps1) * (1 + eps2) - 1| < 2^-124.80
     (2^-124.82 with analyze_p2()). */
}

/* Approximation of log(1+z) for the last iteration, with |z| <= 2^-13
   and z having only its upper limb being non-zero.
   For the error analysis, see function analyze_p3() in accompanying file
   qint.sage.
   Maximal relative error: 2^-252.66, maximal absolute error: 2^-265.67,
   and |r| < 0.0001221. */
static inline void
p_3 (qint64_t *r, qint64_t *z) {
  mul_qint_11 (r, &P_3[0], z); /* coefficient of degree 18 */
  /* here |P_3[0]| < 2^-4.16 and |z| <= 2^-13 thus |r| < 2^-17.16;
     mul_qint_11() is exact giving a 128-bit product
     (note that P_3[0] and z have only their upper limb non-zero). */
  add_qint_22 (r, &P_3[1], r); /* coefficient of degree 17 */
  /* here |P_3[1]| < 2^-4.08 and |r_in| < 2^-17.16, thus |r| < 2^-4.07;
     the rounding error of add_qint_22() is bounded by 2 ulps_128, thus 2^-131;
     and the low part of P_3[1] is 0.
     The total error for the two above instructions is thus bounded by
     2^-131. This error is multiplied by z^16
     below (not counting the final multiplication by z),
     thus contributes to at most err1 < 2^-339. */

  /* the coefficients of degree 15-16 (P_3[2] to P_3[3]) have two non-zero
     limbs */
  for (int32_t k = 2; k < 4 ; k++) {
    mul_qint_11 (r, r, z);
    /* mul_qint_11() is exact (giving a 128-bit product), but we ignore
       as input (r_in->hl)*z, which accounts for ulp64(r_in)*z */
    add_qint_22 (r, &P_3[k], r);
    /* the rounding error in add_qint_22() is less than 2 ulps_128,
       and the error of these two instructions is multiplied by z^(17-k)
       (not counting the final multiplication by z). */
  }

  /* the coefficients of degree 11-14 (P_3[4] to P_3[7]) have two non-zero
     limbs */
  for (int32_t k = 4; k < 8 ; k++) {
    mul_qint_21 (r, r, z);
    /* mul_qint_21() is exact (giving a 192-bit product), but we ignore the
       "lh" limb of r in add_qint_22() below, which accounts for ulp128(r) */
    add_qint_22 (r, &P_3[k], r);
    /* the rounding error in add_qint_22() is less than 2 ulps_128,
       and the error of these two instructions is multiplied by z^(17-k)
       (not counting the final multiplication by z). */
  }

  /* the coefficients of degree 5-10 (P_3[8] to P_3[13]) have full 256-bit
     accuracy */
  for (int32_t k = 8; k < 14 ; k++) {
    mul_qint_31 (r, r, z);
    /* mul_qint_31() is exact, but we ignore in input the 4th
       limb of r, which accounts for ulp192(r_in)*z (except for k=8
       since r was computed by add_qint_22() above, and we already took
       into account the truncated part of r) */
    add_qint (r, &P_3[k], r);
    /* the rounding error in add_qint() is less than 2 ulps_256,
       and the error of these two instructions is multiplied by z^(17-k)
       (not counting the final multiplication by z). */
  }

  for (int32_t k = 14; k < 18; k++) {
    mul_qint_41 (r, r, z);
    /* mul_qint_41() has an error < 2 ulps */
    add_qint (r, &P_3[k], r);
    /* the rounding error in add_qint() is less than 2 ulps_256,
       and the error of these two instructions is multiplied by z^(17-k)
       (not counting the final multiplication by z). */
  }

  /* The total absolute error up to here is bounded by 2^-253.92 (see
     function analyze_p3 in accompanyng file qint.sage).
     We have r > 0.99993896, thus the relative error is bounded by
     2^-253.92/0.99993896 < 2^-253.91. */

  /* since the polynomial has zero constant coefficient, we multiply by z */
  mul_qint_41 (r, r, z);
  /* We bound both the absolute and relative error.

     Absolute error:
     here |z| <= 2^-13 and |r_in| < 1.01 thus |r| < 2^-12.98;
     the rounding error of mul_qint_41() is bounded by 2 ulps, thus 2^-267.
     We add the previous total absolute error on r_in multiplied by z,
     which gives 2^-267+2^-253.92*2^-13 < 2^-265.95.
     The absolute error on the Sollya polynomial is bounded by 2^-268.1653.
     This yields a total absolute < 2^-265.95+2^-268.1653 < 2^-265.66.
     (The analyze_p3() routine in the accompanying file yields 2^-265.67.)

     The relative error on the Sollya polynomial is bounded by
     eps0 = 2^-255.0786,
     the relative error on r_in is bounded by eps1=2^-253.91
     and the error from mul_dint_41 is bounded by 2 ulps
     (which yields a relative error less than eps2=2*2^-255).
     This yields a relative error on r less than:
     |(1 + eps0) * (1 + eps1) * (1 + eps2) - 1| < 2^-252.65.
     (The analyze_p3() routine in the accompanying file yields 2^-252.66.) */
}

/* Given 2^-1074 <= x <= 0x1.fffffffffffffp+1023, this routine puts in h+l
   an approximation of log(x) such that |l| < 2^-23.89*|h| and

   | h + l - log(x) | <= elog * |log x|

   with elog = 2^-73.527  if x < 1/sqrt(2) or sqrt(2) < x,
   and  elog = 2^-67.0544 if 1/sqrt(2) < x < sqrt(2)
   (note that x cannot equal 1/sqrt(2) nor sqrt(2)).

   See Lemma 4 from reference [5].
*/
static inline int log_1 (double *h, double *l, double x) {
  f64_u _x = {.f = x};
  uint64_t _m = _x.u & (~0ull >> 12);
  int64_t _e = (_x.u >> 52) & 0x7ff;

  f64_u _t;

  if (__builtin_expect(_e,1)) {
    _t.u = _m | (0x3ffll << 52);
    _m += 1ull << 52;
    _e -= 0x3ff;
  } else { /* x is a subnormal double  */
    uint32_t k = __builtin_clzll (_m) - 11;

    _e = -0x3fell - k;
    _m <<= k;
    _t.u = _m | (0x3ffll << 52);
  }

  /* now |x| = 2^_e*_t = 2^(_e-52)*m with 1 <= _t < 2,
     and 2^52 <= _m < 2^53 */

  //   log(x) = log(t) + E · log(2)
  double t = _t.f;

  // Find the lookup index
  uint64_t i;

  // If m > sqrt(2) we divide it by 2 so ensure 1/sqrt(2) < t < sqrt(2)
  uint64_t c = _m >= 0x16a09e667f3bcd;
  static const double cy[] = {1.0, 0.5};
  static const uint64_t cm[] = {44, 45};

  _e += c;
  double E = _e;
  i = _m >> cm[c]; /* i/2^8 <= t < (i+1)/2^8 */
  /* when c=1, we have 0x16a09e667f3bcd <= m < 2^53, thus 90 <= i <= 127;
     when c=0, we have 2^52 <= m < 0x16a09e667f3bcd, thus 128 <= i <= 181 */
  t *= cy[c];
  /* now 0x1.6a09e667f3bcdp-1 <= t < 0x1.6a09e667f3bcdp+0,
     and log(x) = E * log(2) + log(t) */

  double r = _INVERSE[i-181];
  double l1 = _LOG_INV[i-181][0];
  double l2 = _LOG_INV[i-181][1];

  double z = __builtin_fma (r, t, -1.0);

#define LOG2_H 0x1.62e42fefa38p-1
#define LOG2_L 0x1.ef35793c7673p-45

  double th, tl;
  th = __builtin_fma (E, LOG2_H, l1);
  tl = __builtin_fma (E, LOG2_L, l2);

  fast_sum (h, l, th, z, tl);
  double ph, pl;
  p_1 (&ph, &pl, z);
  fast_sum (h, l, *h, ph, *l + pl);

  if (_e == 0 && __builtin_fabs (*l) > __builtin_fabs (*h) * 0x1p-24)
  {
    fast_two_sum (h, l, *h, *l);
    return 1;
  }

  return 0;
}

/* Put in r an approximation of log(x), with relative error bounded by
   2^-122.88. */
static void log_2 (dint64_t *r, dint64_t *x) {
  int64_t E = x->ex;
  uint16_t i, j;

  /* x = 2^(E-63) * hi */

  // find the 1st lookup index i = floor(x*2^7)
  if (x->hi > 0xb504f333f9de6484) { /* hi/2^63 > sqrt(2) */
    E++;
    i = x->hi >> (63 + 1 - 7); // the +1 accounts for the division x/2
  }
  else
    i = x->hi >> (63 - 7);

  /* now 90 <= i <= 181 */

  x->ex = x->ex - E;

  /* now sqrt(2)/2 < x < sqrt(2) */

  dint64_t z;
  mul_dint_11 (&z, x, &_INVERSE_2_1[i - 90]); /* exact */
  /* The low limb of z is zero, since x has 53 significant bits, and
     _INVERSE_2_1[i-90] has 9 significant bits, thus the product fits
     in 64 bits. We have 0.9921875 <= z <= 1.0078125 here. */

  // find the 2nd lookup index j = floor(x*2^13)
  j = z.hi >> (63 - 13 - z.ex);

  mul_dint_11 (&z, &z, &_INVERSE_2_2[j - 8128]); /* exact */
  /* here z.lo might be non-zero */
  /* we have 0.9998779296875 <= z <= 1.0001220703125 here */
  
  // subtract 1, since 1/2 < z < 2 this is exact */
  add_dint (&z, &M_ONE, &z);

  /* _INVERSE_2_2[j-8128] has at most 14 significant bits, thus in principle
     before the subtraction of 1, z might have up to 53+9+14 = 76 significant
     bits. However for sqrt(2)/2 < x < 1, since x has 53 significant
     bits, x is an integer multiple of 2^-53; then _INVERSE_2_1[i-90] is an
     integer multiple of 2^-8, then x*_INVERSE_2_1[i-90] is an integer multiple
     of 2^-61. If 1 <= x < sqrt(2), x is an integer multiple of 2^-52 and
     _INVERSE_2_1[i-90] an integer multiple of 2^-9, thus again
     x*_INVERSE_2_1[i-90] an integer multiple of 2^-61.
     Now _INVERSE_2_2[j-8128] is an integer multiple of 2^-14, thus
     z0 := x*_INVERSE_2_1[i-90]*_INVERSE_2_2[j-8128] is an integer multiple of
     2^-61*2^-14=2^-75.
     Write z0 = 1 + t*2^-75 with t integer.
     Since 0.9998779296875 <= z0 <= 1.0001220703125, we deduce |t| <= 2^62,
     thus z0-1 is exactly representable on 64 bits. */

  /* now |z| <= 0.0001220703125 = 2^-13 */

  // E·log(2)
  mul_dint_int64 (r, &LOG2, E);
  /* The rounding error in mul_dint_int64() is at most 1 ulp.
     Since |E| <= 1074, this is at most ulp(1074*log(2))=2^-118,
     and |r| <= 1074*log(2) < 2^9.55.
     We also have the approximation error on log(2), which is < 2^-129.97,
     and multiplied by |E| <= 1074, thus < 2^-119.90.
     This yields an absolute error < 2^-118+2^-119.90 < 2^-117.65.
     The relative rounding error in mul_dint_int64() is bounded by 1 ulp thus
     2^-127, and that on log(2) is bounded by 1/2 ulp thus 2^-128, which
     yields (1+2^-127)*(1+2^-128)-1 < 2^-126.41.
  */

  dint64_t p;

  p_2 (&p, &z); /* relative error < 2^-124.82, absolute error < 2^-137.95,
                   and |p| < 0.0001221 */

  add_dint (&p, &_LOG_INV_2_2[j - 8128], &p);
  /* here we have |_LOG_INV_2_2[j-8128]| < 2^-6.99 and |p_in| < 0.0001221,
     thus |p| < 2^-6.96, and the rounding error of add_dint() is bounded
     by 2 ulps, thus < 2^-133 (absolute) and < 2^-126 (relative). */

  add_dint (&p, &_LOG_INV_2_1[i - 90], &p);
  /* here we have |_LOG_INV_2_1[i-90]| < 2^-1.51 and |p_in| < 2^-6.96,
     thus |p| < 2^-1.47, and the rounding error of add_dint() is bounded
     by 2 ulps, thus < 2^-128 (absolute) and < 2^-126 (relative). */

  add_dint (r, &p, r);
  /* here |p| < 2^-1.47 and |r_in| < 2^9.55, thus |r| < 2^9.56.
     The rounding error of add_dint() is bounded is bounded by 2 ulps,
     thus < 2^-117 (absolute) or 2^-126 (relative). */

  /* We have the following rounding errors:
    (a) rounding error on E*log(2) which is bounded by 2^-117.65 (absolute)
        and 2^-126.41 (relative)
    (b) error on p_2() bounded by 2^-137.95 (absolute) and 2^-124.82 (relative)
    (c1) approximation error on _LOG_INV_2_1[i-90],
        bounded by 2^-130 (absolute) and 2^-128 (relative)
    (c2) approximation error on _LOG_INV_2_2[j-8128],
        bounded by 2^-136 (absolute) and 2^-128 (relative)
    (d1) rounding error on _LOG_INV_2_1[i-90]+p, bounded by 2^-128 (absolute)
         and 2^-126 (relative)
    (d2) rounding error on _LOG_INV_2_2[j-8128]+p, bounded by 2^-133 (absolute)
         and 2^-126 (relative)
    (e) rounding error on p + r, bounded by 2^-117 (absolute) and 2^-126
        (relative)
     We distinguish two cases here: E<>0 and E=0.

     If E<>0, then |r_in| > 2^-0.53 and |p| < 2^-1.47, thus |r| > 2^-1.59.
     The absolute errors (b), (c1), (c2), (d1), (d2) are bounded by
     2^-137.95 + 2^-130 + 2^-136 + 2^-128 + 2^-133 < 2^-127.63, which thus
     converts to 2^-127.63/2^-1.59 < 2^-126.04 as relative error.
     Together with the relative error (a) e1=2^-126.41 on E*log(2) and
     (e) e2=2^-126 on p+r, this yields with e3=2^-126.04:
     |(1+e1)*(1+e2)*(1+e3)-1| < 2^-124.55.

     If E=0, then the rounding error (a) on E*log(2) vanishes, and likewise
     the rounding error (e) on p + r, since r=0. We further distinguish two
     cases: either i <> {127, 127}, or 127 <= i <= 128.

     If E=0 and i <> {127, 128}: the errors (a) and (e) vanish since r=0.
     Using interval arithmetic to analyze each of the possible (i,j) pairs,
     using the fact that the errors (c1) and (c2) are bounded by 1/2 ulp
     of the corresponding values, and that the errors (d1) and (d2) are
     bounded by 2 ulps of the corresponding result, we find (see routine
     max_rel_err_p2_case1 in the accompanying file dint.sage) that the
     maximal relative error is bounded by 2^-123.89, obtained for i=126
     and j=8256.

     If E=0 and (i=127 or i=128), then we have _INVERSE_2_1[i-90]=1 thus
     _LOG_INV_2_1[i-90]=0, and the errors (c1) and (d1) also vanish.
     It only remains errors (b), (c2) and (d2). We further distinguish
     two sub-cases: either j <> {8191, 8192}, or 8191 <= j <= 8192.

     If E=0, i in {127,128} and j <> {8191, 8192}, then using the same method
     as in case E=0 and i <> {127, 128}, we find that the relative error
     is bounded by 2^-122.88 (attained for j=8193). See routine
     max_rel_err_p2_case2() in the accompanying file dint.sage.

     It remains the case E=0, i in {127,128} and j in {8191, 8192}. In this
     case the errors (c2) and (d2) are also zero, it only remains error (b),
     which is bounded by 2^-124.82 (relative).

     In summary, the relative error is bounded:
     * if E<>0, by 2^-124.55
     * if E=0 and i <> {127, 128}, by 2^-123.89
     * if E=0, i in {127, 128}, j <> {8191, 8192}, by 2^-122.88
     * if E=0, i in {127, 128}, j in {8191, 8192}, by 2^-124.82

     In all cases, the relative error is bounded by 2^-122.88,
     where the largest bound comes from the case E=0, i in {127, 128},
     and j <> {8191, 8192}.
  */
}

/* put in r an approximation of log(x), with relative error < 2^-250.74 */
static void log_3 (qint64_t *r, qint64_t *x) {
  int64_t E = x->ex;

  // Find the lookup index: upper 8 bits of x
  uint16_t i, j;

  /* x = 2^(E-63) * hh */

  if (x->hh > 0xb504f333f9de6484) {
    E++;
    i = x->hh >> (63 + 1 - 7); // the +1 accounts for the division x/2
  }
  else
    i = x->hh >> (63 - 7);

  /* now 90 <= i <= 181 */

  x->ex = x->ex - E;

  /* now sqrt(2)/2 < x < sqrt(2) */

  qint64_t z;
  mul_qint (&z, x, &_INVERSE_3_1[i - 90]);
  /* The above operation is exact since x has 53 significant bits,
     and _INVERSE_3_1[i - 90] has 9 significant bits.
     We have 0.9921875 <= z <= 1.0078125. */

    // find the 2nd lookup index j = floor(x*2^13)
  j = z.hh >> (63 - 13 - z.ex);

  mul_qint (&z, &z, &_INVERSE_3_2[j - 8128]); /* exact */
  /* here z.hl might be non-zero */
  /* we have 0.9998779296875 <= z <= 1.0001220703125 here */

  // subtract 1, since 1/2 < z < 2 this is exact */
  add_qint (&z, &M_ONE_Q, &z);
  /* z fits into 64 bits (same analysis as in log_2) */

  /* now |z| <= 0.0001220703125 = 2^-13 */

  // E·log(2)
  mul_qint_2 (r, E, &LOG2_Q);
  /* The rounding error in mul_qint_2() is at most 2 ulps.
     Since |E| <= 1074, this is at most 2*ulp_256(1074*log(2))=2^-245,
     and |r| <= 1074*log(2) < 2^9.55.
     We also have the approximation error on log(2), which is < 2^-256.14,
     and multiplied by |E| <= 1074, thus < 2^-246.07.
     This yields an absolute error < 2^-245+2^-246.07 < 2^-244.43.
     The relative rounding error in mul_qint_2() is bounded by 2 ulps thus
     2^-254, and that on log(2) is bounded by 1/2 ulp thus 2^-256, which
     yields (1+2^-254)*(1+2^-256)-1 < 2^-253.67. */

  qint64_t p;
  p_3 (&p, &z); /* relative error < 2^-252.66, and |p| < 0.0001221 */

  /* We have to accumulate r which approximates E*log(2),
     _LOG_INV_3_1[i-90] which approximates -log(_INVERSE_3_1[i-90]),
     _LOG_INV_3_2[j-8128] which approximates -log(_INVERSE_3_2[i-8128]),
     and p which approximates log (z). We start by the smallest
     values to minimize the rounding error. */

  add_qint (&p, &_LOG_INV_3_2[j - 8128], &p);
  /* here we have |_LOG_INV_3_2[j-8128]| < 0.0078432 and |p_in| < 0.0001221,
     thus |p| < 0.0079653, and the rounding error of add_qint() is bounded
     by 2 ulps, thus < 2^-261 (absolute) and 2^-254 (relative). */

  add_qint (&p, &_LOG_INV_3_1[i - 90], &p);
  /* here we have |_LOG_INV_3_1[i-90]| < 0.34945 and |p_in| < 0.0079653,
     thus |p| < 0.3574153, and the rounding error of add_qint() is bounded
     by 2 ulps, thus < 2^-256 (absolute) and 2^-254 (relative). */

  add_qint (r, &p, r);
  /* here |p| < 0.3574153 and |r_in| < 2^9.55, thus |r| < 2^9.56.
     The rounding error of add_qint() is bounded by 2 ulps,
     thus < 2^-245 (absolute) or 2^-254 (relative). */

  /* We have the following rounding errors:
     (a) rounding error on E*log(2) which is bounded by 2^-244.43 (absolute)
         and 2^-253.67 (relative)
     (b) error on p_3() bounded by 2^-265.67 (absolute) and 2^-252.66 (rel)
     (c1) approximation error on _LOG_INV_3_1[i-90], bounded by 2^-258
         (absolute) and 2^-256 (relative) [1/2 ulp]
     (c2) approximation error on _LOG_INV_3_2[j-8128], bounded by 2^-263
         (absolute) and 2^-256 (relative) [1/2 ulp]
     (d1) rounding error on _LOG_INV_3_1[i-90] + p, bounded by 2^-256
          (absolute) and 2^-254 (relative)
     (d2) rounding error on _LOG_INV_3_2[j-8128] + p, bounded by 2^-261
          (absolute) and 2^-254 (relative)
     (e) rounding error on p + r, bounded by 2^-245 (absolute) and
         2^-254 (relative)
     We distinguish two cases here: E<>0 and E=0.

     If E<>0, then |r_in| > 0.69314 and |p| < 0.3574153, thus |r| > 0.33572.
     The absolute errors (b), (c1), (c2), (d1), (d2) are bounded by
     2^-265.67 + 2^-258 + 2^-263 + 2^-256 + 2^-261 < 2^-255.63, which thus
     converts to 2^-255.63/0.33572 < 2^-254.05 as relative error.
     Together with the relative error e1=2^-253.67 on E*log(2) and e2=2^-254
     on p+r, this yields with e3=2^-254.05:
     |(1+e1)*(1+e2)*(1+e3)-1| < 2^-252.31.

     If E=0, then the rounding error (a) on E*log(2) vanishes, and likewise
     the rounding error (e) on p + r, since r=0. We further distinguish two
     cases: either i <> {127, 127}, or 127 <= i <= 128.

     If E=0 and i <> {127, 128}: the errors (a) and (e) vanish since r=0.
     Using interval arithmetic to analyze each of the possible (i,j) pairs,
     using the fact that the errors (c1) and (c2) are bounded by 1/2 ulp
     of the corresponding values, and that the errors (d1) and (d2) are
     bounded by 2 ulps of the corresponding result, we find (see routine
     max_rel_err_p3_case1 in the accompanying file qint.sage) that the
     maximal relative error is bounded by 2^-251.88, obtained for i=126
     and j=8256.

     If E=0 and (i=127 or i=128), then we have _INVERSE_3_1[i-90]=1 thus
     _LOG_INV_3_1[i-90]=0, and the errors (c1) and (d1) also vanish.
     It only remains errors (b), (c2) and (d2). We further distinguish
     two sub-cases: either j <> {8191, 8192}, or 8191 <= j <= 8192.

     If E=0, i in {127,128} and j <> {8191, 8192}, then using the same method
     as in case E=0 and i <> {127, 128}, we find that the relative error
     is bounded by 2^-250.74 (attained for j=8193). See routine
     max_rel_err_p3_case2() in the accompanying file dint.sage.

     It remains the case E=0, i in {127,128} and j in {8191, 8192}. In this
     case the errors (c2) and (d2) are also zero, it only remains error (b),
     which is bounded by 2^-252.66 (relative).

     In summary, the relative error is bounded:
     * if E<>0, by 2^-252.31
     * if E=0 and i <> {127, 128}, by 2^-251.88
     * if E=0, i in {127, 128}, j <> {8191, 8192}, by 2^-250.74
     * if E=0, i in {127, 128}, j in {8191, 8192}, by 2^-252.66

     In all cases, the relative error is bounded by 2^-250.74
     where the largest bound comes from the case E=0, i in {127, 128},
     and j <> {8191, 8192}.
  */
}

/* Given RHO1 <= rh <= RHO2, |rl/rh| < 2^-23.8899 and |rl| < 2^-14.4187,
   this routine computes an approximation eh+el of exp(rh+rl) such that:

   | (eh+el) / exp(rh+rl) - 1 | < 2^-63.78597.

   Moreover |el/eh| <= 2^-49.2999.

   See Lemma 7 from reference [5].

   The result eh+el is multiplied by s (which is +1 or -1),
   where s=-1 can only happen when x < 0 and y is an integer.
*/
static inline void
exp_1 (double *eh, double *el, double rh, double rl, double s) {
#define RHO0 -0x1.74910ee4e8a27p+9
// #define RHO1 -0x1.577453f1799a6p+9
/* We increase the initial value of RHO1 to avoid spurious underflow in
   the result value el. However, it is not possible to obtain a lower
   bound on |el| from the input value rh, thus this modified value of RHO1
   is obtained experimentally. */
#define RHO1 -0x1.483b8cca421afp+9
#define RHO2 0x1.62e42e709a95bp+9
#define RHO3 0x1.62e4316ea5df9p+9

  /* Section 7.12.17 from the C standard (N3220) says: "Relational operators
     may raise the "invalid" floating-point exception when argument
     values are NaNs". We thus first check rh != rh to detect NaNs,
     hoping this will not raise invalid. */
  if (__builtin_expect(rh != rh || rh > RHO2, 0)) {
    // again, first check rh == rh to detect NaNs
    if (rh == rh && rh > RHO3) {
      /* If rh > RHO3, we are sure there is overflow,
         For s=1 we return eh = el = DBL_MAX, which yields
         res_min = res_max = +Inf for rounding up or to nearest,
         and res_min = res_max = DBL_MAX for rounding down or toward zero,
         which will yield the correct rounding.
         For s=-1 we return eh = el = -DBL_MAX, which similarly gives
         res_min = res_max = -Inf or res_min = res_max = -DBL_MAX,
         which is the correct rounding. */
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = ERANGE;
#endif

      *eh = 0x1.fffffffffffffp+1023 * s;
      *el = 0x1.fffffffffffffp+1023 * s;
    }
    else
      /* If RHO2 < rh <= RHO3, we are in the intermediate region
         where there might be overflow or not, thus we set eh = el = NaN,
         which will set res_min = res_max = NaN, the comparison
         res_min == res_max will fail: we defer to the 2nd phase. */
      *eh = *el = NAN;
    return;
  }

  if (__builtin_expect(rh < RHO1, 0)) {
    if (rh < RHO0)
    {
      *eh = +0.0 * s;
      *el = 0x1p-1074 * (0.5 * s);
      /* For s=1, we have eh=el=+0 except for rounding up,
         thus res_min=+0 or -0, res_max=+0 in the main code,
         the rounding test succeeds, and we return res_max which is the
         expected result in the underflow case.
         For s=1 and rounding up, we have eh=+0, el=2^-1074,
         thus res_min = res_max = 2^-1074, which is the expected result too.
         For s=-1, we have eh=el=-0 except for rounding down,
         thus res_min=-0 or +0, res_max=-0 in the main code,
         the rounding test succeeds, and we return res_max which is the
         expected result in the underflow case.
         For s=-1 and rounding down, we have eh=-0, el=-2^-1074,
         thus res_min = res_max = -2^-1074, which is the expected result too.
      */
    }
    else /* RHO0 <= rh < RHO1 or s < 0: we defer to the 2nd phase */
      *eh = *el = NAN;
    return;
  }

#define INVLOG2 0x1.71547652b82fep+12
  /* Note: if the rounding mode is to nearest, we can save about 2 cycles
     (on an i7-8700) by replacing the computation of k by the following
     classical trick:
     const double magic = 0x1.8p+52;
     double k = __builtin_fma (rh, INVLOG2, magic) - magic;
  */
  double k = roundeven_finite (rh * INVLOG2);

#define LOG2H 0x1.62e42fefa39efp-13
#define LOG2L 0x1.abc9e3b39803fp-68

  double zh, zl;
  zh = __builtin_fma (LOG2H, -k, rh);
  zl = __builtin_fma (LOG2L, -k, rl);

  int64_t K = k; /* Note: k is an integer, this is just a conversion. */
  int64_t M = (K >> 12) + 0x3ff;
  int64_t i2 = (K >> 6) & 0x3f;
  int64_t i1 = K & 0x3f;

  double t1h = T1[i2][0], t1l = T1[i2][1], t2h = T2[i1][0], t2l = T2[i1][1];
  d_mul (eh, el, t2h, t2l, t1h, t1l);

  double qh, ql;
  q_1 (&qh, &ql, zh + zl);

  d_mul (eh, el, *eh, *el, qh, ql);
  f64_u _d;

  /* we should have 1 < M < 2047 here, since we filtered out
     potential underflow/overflow cases at the beginning of this function */

  _d.u = (uint64_t) M << 52;
  _d.f *= s;
  *eh *= _d.f;
  *el *= _d.f;
}

/* put in r an approximation of exp(x), for |x| < 744.45,
   with relative error < 2^-121.70 */
static void exp_2 (dint64_t *r, dint64_t *x) {
  dint64_t K, y;

  if (x->ex >= 10) // underflow or overflow
  {
    cp_dint (r, x);
    r->ex = (x->sgn == 0x1) ? -1076 : 1025;
    r->sgn = 0;
    return;
  }

  /* the following multiplication does not need to be fully accurate,
     since it is only used to round x*2^12/log(2) to the nearest integer k */
  mul_dint_11 (&K, x, &LOG2_INV);
  /* mul_dint_11() is exact, assuming the low part of x is zero, but it is not
     here, thus we ignore low(x)*LOG2_INV < ulp64(744.45)*LOG2_INV < 2^-41.47.
     Since LOG2_INV approximates 2^12/log(2) with absolute error < 2^-52.96
     and |x| < 744.45, the error from LOG2_INV is bounded by 2^-43.41.
     This gives a total error < 2^-41.13. This error is multiplied by LOG2
     below, and divided by 2^12, thus yields an error < 2^-53.65 on K in
     the add_dint() call. */

  int64_t k = dint_toi (&K); /* k = trunc(K) [rounded towards zero, exact] */

  /* |k| <= 4399162 */

  /* the following multiplication needs to be fully accurate, since we need
     to compute x - k*log(2)/2^12 to full accuracy */
  mul_dint_int64 (&K, &LOG2, k);
  /* The rounding error of mul_dint_int64() is bounded by 6 ulps, thus since
     |K| <= 4399162*log(2) < 3049267, the error on K is bounded by 2^-103.41.
     This error is divided by 2^12 below, thus yields < 2^-115.41. */
  K.ex -= 12;
  K.sgn = !K.sgn;

  add_dint (&y, x, &K); /* exact because Sterbenz theorem applies */
  /* If all computations were exact above, we would have
     K = -(x/log(2)-eps/2^12)*log(2) with |eps| < 1 and eps of the same
     sign as x, thus x+K = eps*log(2)/2^12, and thus |y| < log(2)/2^12,
     with y of same sign as x.
     But we have a total rounded error < 2^-53.65 + 2^-115.41 < 2^-53.64
     thus |y| < log(2)/2^12 + 2^-53.64 < 0.00016923. */

  int64_t M = k >> 12;
  int64_t i2 = (k >> 6) & 0x3f;
  int64_t i1 = k & 0x3f;

  q_2 (r, &y); /* relative error bounded by 2^-122.29, with |r| < 1.0002 */

  mul_dint (r, &T1_2[i2], r);
  /* the rounding error of mul_dint() is bounded by 6 ulps, which translates
     into 6*2^-127 for the relative error;
     the approximation error for T1_2[i2] is bounded by 2^-128 relatively. */

  mul_dint (r, &T2_2[i1], r);
  /* the rounding error of mul_dint() is bounded by 6 ulps, which translates
     into 6*2^-127 for the relative error;
     the approximation error for T2_2[i2] is bounded by 2^-128 relatively. */

  /* Total relative errors:
     2^-122.29 from q_2()
     6*2^-127 and 2^-128 from the multiplication by T1_2[i2]
     6*2^-127 and 2^-128 from the multiplication by T2_2[i1].
     With e1=2^-122.29, e2=6*2^-127 and e3=2^-128, this gives:
     (1+e1)*(1+e2)^2*(1+e3)^2 - 1 < 2^-121.70. */

  r->ex = r->ex + M; /* exact */
}

/* put in r an approximation of exp(x), for |x| < 744.45,
   with relative error < 2^-241.10 */
static void exp_3 (qint64_t *r, qint64_t *x) {
  qint64_t K, y;


  /* the following multiplication does not need to be fully accurate,
     since it is only used to round x*2^12/log(2) to the nearest integer k */
  mul_qint_11 (&K, x, &LOG2_INV_Q);
  /* mul_qint_11() is exact, assuming the low part of x is zero, but it is not
     here, thus we ignore low(x)*LOG2_INV_Q < ulp64(744.45)*LOG2_INV_Q
     < 2^-41.47.
     Since LOG2_INV_Q approximates 2^12/log(2) with absolute error < 2^-52.96
     and |x| < 744.45, the error from LOG2_INV_Q is bounded by 2^-43.41.
     This gives a total error < 2^-41.47 + 2^-43.41 < 2^-41.13. This error is
     multiplied by LOG2_Q below, thus yields an error < 2^-53.65 on K in
     the add_qint() call. */

  int64_t k = qint_toi (&K); /* k = trunc(K) [rounded towards zero, exact] */

  /* |k| <= 4399162 */

  /* the following multiplication needs to be fully accurate, since we need
     to compute x - k*log(2)/2^12 to full accuracy */
  mul_qint_2 (&K, k, &LOG2_Q);
  /* The rounding error of mul_qint_2() is bounded by 2 ulps, thus since
     |K| <= 4399162*log(2) < 3049267, the error on K is bounded by 2^-233.
     This error is divided by 2^12 below, thus yields < 2^-245. */
  K.ex -= 12;
  K.sgn = !K.sgn;

  add_qint (&y, x, &K); /* exact because Sterbenz theorem applies */
  /* If all computations were exact above, we would have
     K = -(x/log(2)-eps/2^12)*log(2) with |eps| < 1 and eps of the same
     sign as x, thus x+K = eps*log(2)/2^12, and thus |y| < log(2)/2^12,
     with y of same sign as x.
     But we have a total rounded error < 2^-53.65 + 2^-245 < 2^-53.64
     thus |y| < log(2)/2^12 + 2^-53.64 < 0.00016923. */

  int64_t M = k >> 12;
  int64_t i2 = (k >> 6) & 0x3f;
  int64_t i1 = k & 0x3f;

  q_3 (r, &y); /* relative error bounded by 2^-241.11, with |r| < 1.0002 */

  mul_qint (r, &T1_3[i2], r);
  /* the rounding error of mul_qint() is bounded by 14 ulps, which translates
     into 14*2^-255 for the relative error;
     the approximation error for T1_3[i2] is bounded by 2^-256 relatively. */

  mul_qint (r, &T2_3[i1], r);
  /* the rounding error of mul_qint() is bounded by 14 ulps, which translates
     into 14*2^-127 for the relative error;
     the approximation error for T2_3[i2] is bounded by 2^-128 relatively. */

  /* Total relative errors:
     2^-241.11 from q_3()
     14*2^-255 and 2^-256 from the multiplication by T1_3[i2]
     14*2^-255 and 2^-256 from the multiplication by T2_3[i1].
     With e1=2^-241.11, e2=14*2^-255 and e3=2^-256, this gives:
     (1+e1)*(1+e2)^2*(1+e3)^2 - 1 < 2^-241.10. */

  r->ex = r->ex + M; /* exact */
}

/* The following are pairs (m,y) from the set S defined in [4]
   such that m^y is not exact nor a midpoint, but is at relative
   distance < 2^-112.55 from an exact value or a midpoint.
   All these values should fail the 2nd rounding test, and thus enter
   the exact_pow() routine.

   Note: in [4] the 2nd part of the set S is (x,y) = (m,2^F*n)
   for F integer, -5 <= F < 0, n odd integer, 3 <= n <= 34, m odd integer.
   We believe it should be read instead:
   (x,y) = (2^E*m,2^F*n) for E,F integers, -5 <= F < 0, n odd integer,
   3 <= n <= 34, m odd integer.
   The bound 35 from [2,3] was improved to 34 in [4].

   All these inputs are in the F < 0 case.

   The last entry is the denominator q of y as exact rational.
   If (x,y) is a worst case, any (2^(j*q)*x,y) is also one.
   For some x=2^E*n with n odd, if E is not divisible by q=2^-F,
   then no E+j*q will be, so the corresponding entry will be rejected
   by the "check that E is divisible by 2^-F" test.
   The only remaining one is the first one, where 2^-F divides E.
*/

/*
  Computes x^y and returns 1 if the result fits into 54 bits, i.e. computes
  exactly x^y for exact and midpoint cases.
  Implements Algorithm detectRoundingBoundaryCase from [4].
  Requires that the relative error between x^y and the approximation z of
  x^y is less than 2^-117: z = x^y * (1 + eps) with |eps| < 2^-117.
  Can return 1 only when (x,y) are in the set S from [4]:
  (a) either y is an integer, 2 <= y <= 34, or
  (b) x=2^E*m with m odd and y = 2^F*n with -5 <= F < 0, n odd, 3 <= n <= 34
  exact is non-zero iff x^y is exactly representable in binary64.
  Note: [2] says 2 <= y <= 35 and 3 <= n <= 35, but the value 35 is not
  possible, since 3^35 has 56 bits.
*/
static char
exact_pow (double *r, double x, double y, const dint64_t *z,
           __attribute__((unused)) int exact)
{
  int64_t _s = z->sgn ? -1 : 1;

  // Check if x = 2^E
  uint64_t m;
  int64_t E;
  extract (&E, &m, x); /* x = 2^E*m with m odd */

  /* x is a power of 2 */
  if (m == 1) {
    double G = (double) E * y;

    if (is_int (G)) {
      *r = z->sgn ? -1.0 : 1.0;
      int64_t g = (int64_t) G;
      pow2(r, g);
#ifdef CORE_MATH_SUPPORT_ERRNO
      if (g >= 1024 || g <= -1075)
        errno = ERANGE;
#endif
      return 1;
    }
    return 0;
  }

  if (y < 0.0 || y > 34.0)
    return 0;

  uint64_t n;
  int64_t F;
  extract (&F, &n, y); /* y = 2^F*n with n odd */

  /* since y <= 34, if F >= 0, we have 2^F*n <= 34 */

  if (n > 34 || F < -5)
    return 0;

  if (F < 0) { /* case (b) */
    /* check that E is divisible by 2^-F */
    if ((E & (~0ull >> (64 + F))))
      return 0;

    int64_t G, g = (E >> -F) * n; // since F < 0, the shift by -F is ok
    /* g = E*y */
    int64_t k;
    round_54 (&G, &k, z); /* z is rounded to k*2^G */

    /* In case exact=0, check condition at line 2 from [4]:
       if |2^G*k-z| >= 2^-116*z, then return false
       This test is not needed if the relative error from
       the 2nd phase is less than 2^-116, since if |2^G*k-z| >= 2^-116*z
       the rounding test from the 2nd phase did succeed.
    */
    if (!exact) {
      int cnt = __builtin_clzll (k);
      dint64_t d = { .hi = (uint64_t)k << cnt, .lo = 0, .ex = G + 63 - cnt,
                     .sgn = 1 - z->sgn };
      add_dint (&d, z, &d); /* exact by Sterbenz theorem */
      /* multiply d by 2^116 */
      d.ex += 116;
    /* compare in absolute value with z */
      if (cmp_dint_abs (&d, z) >= 0)
        return 0;
    }

    if (G > g)
      return 0;

    /* The following code is used when k is a multiple of a power of 2,
       to reduce to 2^X*r with odd r. It checks whether k is an odd number
       multiplied by 2^(g-G). */
    if (((k & ~(~1ull << (g - G))) == (1ull << (g - G)))) {
      *r = (double)((k >> (g - G)) * _s);
      pow2(r, g);
      goto end;
    }
    return 0;
  }

  /* case (a) */

  /* no overflow in n << F since 2^F*n <= 34 */
  uint64_t t = n << F;
  int64_t k = 1;

  /* Compute k = m^t which should fit into 54 bits.
     Invariant: k*m^t */
  while (t) {
    if (t & 0x1) {
      // k = m * k;
      if (__builtin_mul_overflow (m, k, &k)) /* m*k overflows */
        return 0;
    }
    t = t >> 1;
    // m *= m;
    if (t != 0 && __builtin_mul_overflow (m, m, &m)) /* m*m overflows */
      return 0;
  }

  /* check k has at most 54 bits */
  if (k >> 54)
    return 0;

  *r = (double)(k * _s);
  int64_t G = E * (n << F);
  pow2(r, G);

 end:
#ifdef CORE_MATH_SUPPORT_ERRNO
  if (!exact && __builtin_fabs (*r) < 0x1p-1022)
    errno = ERANGE; // underflow
#endif

  return 1;
}

// return non-zero if x^y is exact (and exactly representable as a double)
static int
is_exact (double x, double y)
{
  /* All cases such that x^y might be exact are:
     (a) |x| = 1
     (b) y integer, 0 <= y <= 33
     (c) y<0: x=1 or (x=2^e and |y|=n*2^-k with 2^k dividing e)
     (d) y>0: y=n*2^f with -5 <= f <= -1 and 1 <= n <= 33
     In cases (b)-(d), the low 42 bits of the encoding of y are zero,
     thus we use that for an early exit test. */

  f64_u v = {.f = x}, w = {.f = y};
  if (__builtin_expect ((v.u << 1) != 0x7fe0000000000000ull &&
                        (w.u << 22) != 0, 1))
    return 0;

  if (__builtin_expect ((v.u << 1) == 0x7fe0000000000000ull, 0)) // |x| = 1
    return 1;

  // xmax[y] for 1<=y<=33 is the largest odd m such that m^y fits in 53 bits
  static const uint64_t xmax[] = { 0, 0xffffffffffffffff,
                                   94906265, 208063, 9741, 1551, 455, 189, 97,
                                   59, 39, 27, 21, 15, 13, 11, 9, 7, 7, 5, 5,
                                   5, 5, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
  if (y >= 0 && is_int (y)) {
    /* let x = m*2^e with m an odd integer, x^y is exact when
       - y = 0 or y = 1
       - m = 1 or -1 and -1074 <= e*y < 1024
       - if |x| is not a power of 2, 2 <= y <= 33 and
         m^y should fit in 53 bits
    */
    uint64_t m = v.u & 0xfffffffffffffull;
    int64_t e = ((v.u << 1) >> 53) - 0x433;
    if (e >= -1074)
      m |= 0x10000000000000ull;
    else // subnormal numbers
      e++;
    int t = __builtin_ctzll (m);
    m = m >> t;
    e += t;
    /* For normal numbers, we have x = m*2^e. */
    if (y == 0 || y == 1)
      return 1;
    if (m == 1)
      return -1074 <= y * e && y * e < 1024;
    // now for y < 0 or 33 < y it cannot be exact
    if (y < 0 || 33 < y)
      return 0;
    // now 2 <= y <= 33
    int y_int = (int) y;
    if (m > xmax[y_int])
      return 0;
    // |x^y| = m^y * 2^(e*y)
    uint64_t my = m * m;
    for (int i = 2; i < y_int; i++)
      my = my * m;
    // my = m^y
    t = 64 - __builtin_clzll (m);
    // 2^(t-1) <= m^y < 2^t thus 2^(e*y + t - 1) <= |x^y| < 2^(e*y + t)
    int64_t ez = e * y_int + t;
    if (ez <= -1074 || 1024 < ez)
      return 0;
    // since m is odd, x^y is an odd multiple of 2^(e*y)
    return e * y_int >= -1074;
  }

  uint64_t n = w.u & 0xfffffffffffffull;
  int64_t f = ((w.u << 1) >> 53) - 0x433;
  if (f >= -1074)
    n |= 0x10000000000000ull;
  else // subnormal numbers
    f++;
  int t = __builtin_ctzll (n);
  n = n >> t;
  f += t;
  // |y| = n*2^f with n odd

  uint64_t m = v.u & 0xfffffffffffffull;
  int64_t e = ((v.u << 1) >> 53) - 0x433;
  if (e >= -1074)
    m |= 0x10000000000000ull;
  else // subnormal numbers
    e++;
  t = __builtin_ctzll (m);
  m = m >> t;
  e += t;
  // |x| = m*2^e with m odd

  /* if y < 0 and y is not an integer, the only case where x^y might be
     exact is when x = 2^e and n*e*2^f is an integer */
  if (y < 0)
  {
    if (m != 1) return 0;
    // now e <> 0 since the case |x|=1 has already been treated
    int64_t ez;
    if (f >= 0)
      // if f >= 12, since n*e <> 0, (n*e)<<f cannot be in [-1074,1024)
      ez = (f < 12) ? (-n * e) << f : 1024;
    else { // f < 0 thus 2^-f should divide e
      t = __builtin_ctzll (e);
      if (-f > t) return 0; // 2^-f does not divide e
      ez = (-e >> (-f)) * n;
    }
    return -1074 <= ez && ez < 1024;
  }

  /* now y > 0, y is not a integer, y = n*2^f with n odd and f < 0.
     Since x^(n*2^f) = (x^(2^f))^n, and n is odd, necessarily
     x is an exact (2^k)th power with k=-f.
     This implies x is a square. Since x = m*2^e with m odd,
     necessarily m is a square, and e is even. */
  while (f++) {
    // try to extract a square from m*2^e
    if (e&1) return 0;
    e = e / 2;
    double dm = (double) m;
    double s = __builtin_round (__builtin_sqrt (dm));
    if (s * s != dm)
      return 0;
    /* The above call of sqrt() might set the inexact flag, but in case
       it happens, m is not a square, thus x^y cannot be exact. */
    m = (uint64_t) s; // m remains odd (square root of an odd number)
  }

  // Now |x^y| = (m*2^e)^n with m, n odd integers
  // now for 33 < n it cannot be exact, unless m=1
  if (m > 1)
  {
    if (33 < n)
      return 0;
    // now n <= 33
    if (m > xmax[n])
      return 0;
  }
  // |x^y| = m^n * 2^(e*n) with m odd
  uint64_t my = m, n0 = n;
  while (n0-- > 1)
    my = my * m;
  // |x^y| = my * 2^(e*n)
  t = 64 - __builtin_clzll (my); // number of significant bits of m^n
  /* x^y is an odd multiple of 2^(e*n) thus we should have e*n >= -1074,
     we also have 2^(t-1) <= m^n thus 2^(e*n+t-1) <= |x^y| < 2^(e*n+t)
     and we need e*n+t <= 1024 */
  return -1074 <= e * (int) n && e * (int) n + t <= 1024;
}

// Correctly rounded power function
double cr_pow (double x, double y, bool &resolved) {
  resolved = true;
  double s = 1.0; /* sign of the result */
  double x0 = x; // original value of x

  f64_u _x = {.f = x};
  f64_u _y = {.f = y};

  if (__builtin_expect((_x.u >= 0x7ff0000000000000 || _y.u >= 0x7ff0000000000000), 0)) {

    if (__builtin_isnan(x)) {
      // IEEE 754-2019: pow(x,+/-0) = 1 if x is not a signaling NaN
      if (y == 0.0 && !is_signaling(x))
        return 1.0;

      /* pow(sNaN, y) = qNaN. This is implicit in IEEE 754-2019,
         Section 7.2: "the default result of an operation that signals the
         invalid operation exception shall be a quiet NaN" and "These
         operations are: a) any general-computational operation on a signaling
         NaN".

         Moreover, in 6.2.3:
         "An operation that propagates a NaN operand to its result and has a
         single NaN as an input should produce a NaN with the payload of the
         input NaN if representable in the destination format. If two or more
         inputs are NaN, then the payload of the resulting NaN should be
         identical to the payload of one of the input NaNs if representable in
         the destination format. This standard does not specify which of the
         input NaNs will provide the payload."

         Returning x+x has the double effect to quiet the signaling bit
         and to raise the invalid exception if x=sNaN. */
      return x + x;
    }

    if (__builtin_isnan(y)) {
      // IEEE 754-2019: pow(1,y) = 1 for any y (even a quiet NaN)
      if (x == 1.0 && !is_signaling(y))
        return 1.0;

      // pow(x, sNaN) = qNaN (see above)
      return y + y;
    }

    switch (_x.u) {

    // x = +inf
    case 0x7ff0000000000000:
      if (y == 0.0)
        return 1.0;

      if (y < 0.0)
        return 0.0;

      if (y > 0.0)
        return INFINITY;

      break;

    // x = -inf
    case 0xfff0000000000000:

      /* first check y=+/-inf since is_int uses roundeven_finite
         which might raise spurious invalid for Inf input */
      if (_y.u == 0x7ff0000000000000ull)
        return y; // -Inf^Inf = Inf

      if (_y.u == 0xfff0000000000000ull)
        return +0.0; // -Inf^-Inf = +0

      // y is an odd integer
      if (is_int(y) && !is_int(y * 0.5)) {

        // y is a negative odd integer
        if (y < 0.0)
          return -0.0;

        // y is a positive odd integer
        else
          return -INFINITY;
      }

      // y is a negative even integer or is negative non-integer
      if (y < 0.0)
        return 0.0;

      // y is a positive even integer or is positive non-integer
      if (y > 0.0)
        return INFINITY;

      break;
    }

    switch (_y.u) {

    // y = +inf
    case 0x7ff0000000000000:
      if (x == 0.0)
        return 0.0;

      if (x == -1.0 || x == 1.0)
        return 1.0;

      if (-1.0 < x && x < 1.0)
        return 0.0;

      if (x < -1.0 || 1.0 < x)
        return INFINITY;

      break;

    // y = -inf
    case 0xfff0000000000000:
        if (x == 0.0)
          return INFINITY;

      if (x == -1.0 || x == 1.0)
        return 1.0;

      if (-1.0 < x && x < 1.0)
        return INFINITY;

      if (x < -1.0 || 1.0 < x)
        return 0.0;

      break;
    }
  } // From now on, x and y are finite values

  /* first deal with the case x <= 0 */
  if (x <= 0.0) {
    /* pow(x,+/-0) is 1 if x is not a signaling NaN. */
    if (y == 0.0)
      return 1.0;

    switch (_x.u) {

    // x = +0.0
    case 0x0:

      // y is an odd integer
      if (is_int(y) && !is_int(y * 0.5)) {

        // y is a negative odd integer
        if (y < 0.0) {
          feraiseexcept(FE_DIVBYZERO);
#ifdef CORE_MATH_SUPPORT_ERRNO
          errno = ERANGE;
#endif
          return INFINITY;
        }

        // y is a positive odd integer
        return 0.0;
      }

      // y is positive (non-integer or a positive even integer)
      if (y > 0.0)
        return 0.0;

      // y is negative, finite and an even integer or a non-integer
      feraiseexcept(FE_DIVBYZERO);
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = ERANGE;
#endif
      return INFINITY;

    // x = -0.0
    case 0x8000000000000000:

      // y is an odd integer
      if (is_int(y) && !is_int(y * 0.5)) {

        // y is a negative odd integer
        if (y < 0.0) {
#ifdef CORE_MATH_SUPPORT_ERRNO
          errno = ERANGE;
#endif
          feraiseexcept(FE_DIVBYZERO);
          return -INFINITY;
        }

        // y is a positive odd integer
        return -0.0;
      }

      // y is positive (non-integer or a positive even integer)
      if (y > 0.0)
        return 0.0;


      // y is negative, finite and an even integer or a non-integer
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = ERANGE;
#endif
      feraiseexcept(FE_DIVBYZERO);
      return INFINITY;

    }

    if (!is_int(y)) {
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = EDOM;
#endif
      feraiseexcept(FE_INVALID);
      return NAN;
    }

    double cs[] = {1.0, -1.0};

    // set sign to 1 for y even, to -1 for y odd
    int y_parity = __builtin_fabs (y) >= 0x1p53 ? 0 : ((int64_t) y & 0x1);
    s = cs[y_parity];

    // Set x to |x| for the rest of the computation
    x = -x;
  } /* end of case x <= 0 */

#if ENABLE_FP > 0
  /* This is Algorithm phase_1 from reference [5]. */
  double res_h, res_l;

  double lh, ll;

  FLAG_T flag = get_flag ();

  // approximate log(x)
  int cancel = log_1 (&lh, &ll, x);

  /* We should avoid a spurious underflow/overflow in y*log(x).
     Underflow: for x<>1, the smallest absolute value of log(x) is obtained
     for x=1-2^-53, with |log(x)| ~ 2^-53. Thus to avoid a spurious underflow
     we require |y| >= 2^-969.
     Overflow: the largest absolute value of log(x) is obtained for x=2^-1074,
     with |log(x)| < 745. Thus to avoid a spurious overflow we require
     |y| < 2^1014. */
  int ey = (_y.u >> 52) & 0x7ff;
  if (__builtin_expect (ey < 0x36 || ey >= 0x7f5, 0))
    lh = ll = NAN;

  // approximate y * log(x)
  double rh, rl;
  s_mul (&rh, &rl, y, lh, ll);

  /* We prove in Lemma 5 from reference [5] that if the exact product y*lh
     satisfies 2^-969 <= |y*lh| <= 709.7827, then 2^-970 <= |rh| <= 709.79,
     |rl| <= 2^-14.4187, |rl/rh| <= 2^-23.8899, |rh+rl| <= 709.79 and:

     |rh + rl - y log(x)| <= emul

     with emul = 2^-63.799 if x is not in (1/sqrt(2), sqrt(2))
     and  emul = 2^-57.580 if 1/sqrt(2) < x < sqrt(2)
  */

  exp_1 (&res_h, &res_l, rh, rl, s); /* 1 <= res_h < 2 */
  /* See Lemma 7 from reference [5] for the error analysis of exp_1(). */

  /* avoid a spurious underflow: if |rh| < 2^-511, then exp(rh+rl) will
     round to 1 */
  f64_u _rh = {.f = rh};
  if (__builtin_expect (((_rh.u >> 52) & 0x7ff) < 0x200, 0))
    set_flag (flag);

  /* The error bounds 2^-63.797 and 2^-57.579 are those from Algorithm
     phase_1 from reference [5]. */
  static const double err[] = { 0x1.27p-64, /* 2^-63.797 < 0x1.27p-64 */
                                0x1.57p-58, /* 2^-57.579 < 0x1.57p-58 */
  };
  double res_min, res_max;
  res_min = res_h + __builtin_fma (err[cancel], -res_h, res_l);
  res_max = res_h + __builtin_fma (err[cancel], res_h, res_l);
  /* if res_h < 0, we have res_max < res_min, but since we only check
     equality between res_min and res_max, it does not matter */

  int exact = is_exact (x, y);
  if (exact)
    // restore inexact flag
    set_flag (flag);

  if (__builtin_expect (res_min == res_max, 1)) {
    /* when res_min * ex is in the subnormal range, exp_1() returns NaN
       to avoid double-rounding issues */
#ifdef CORE_MATH_SUPPORT_ERRNO
    if (__builtin_fabs (res_max) < 0x1p-1022)
      errno = ERANGE; // underflow
#endif
    return res_max;
  }
  /* the idea of returning res_max instead of res_min is due to Laurent
     Théry: it is better in case of underflow since res_max = +0 always. */

  // Easy cases
  if (y == 1.0)
    return s * x;
  
  if (y == 2.0) {
    double z = x * x;
#ifdef CORE_MATH_SUPPORT_ERRNO
    if (__builtin_fabs (x) >= 0x1p512)
      errno = ERANGE; // overflow
    /* we have underflow for |x| < 2^-511, since even for RNDU and
       x = nextbelow(2^-511), x^2 would be rounded to 0x1.fffffffffffffp-1023
       with unbouded exponent range, which is not representable */
    if (__builtin_fabs (x) < 0x1p-511 && !exact)
      errno = ERANGE; // underflow
#endif
      return z;
  }

  if (y == 0.5)
    return __builtin_sqrt (x);

  if (y == 0.0)
    return 1.0;
#endif /* ENABLE_FP */

  uint64_t rd; // used in the 2nd and 3rd phases

// Second iteration of rounding
#if ENABLE_ZIV2 > 0
  dint64_t X, Y;
  dint_fromd (&X, x); /* exact: |x| = 2^(X->ex-63) * X->hi (X->lo = 0) */
  dint_fromd (&Y, y); /* exact: |y| = 2^(Y->ex-63) * Y->hi (Y->lo = 0) */

  X.sgn = 0x0; /* force the sign of X to +1 */

  dint64_t R;
  log_2 (&R, &X); /* relative error bounded by 2^-122.88 */

  mul_dint_21 (&R, &R, &Y);
  /* The rounding error of mul_dint_21() is bounded by 2 ulps, which is at most
     2*2^-127 in terms of relative error. Thus the relative error on R is
     bounded by (1+e1)*(1+e2)-1 with e1=2^-122.88 and e2=2*2^-127,
     which gives: R = y*log|x| * (1+eps1) with |eps1| < 2^-122.72 */

  exp_2 (&R, &R); /* relative error < 2^-121.70:
                     R = exp(R_in) * (1+eps2) with |eps2| < 2^-121.70 */

  /* We thus have R = |x|^y * exp(y*log|x|*eps1) * (1+eps2).
     Since y*log|x| < 744.45, we have |y*log|x|*eps1| < 744.45*2^-122.72
     < eps3 = 2^-113.179 thus the relative error is bounded by
     exp(eps3)*(1+eps2)-1 < 2^-113.17.
     This corresponds to an error of at most 2^-113.17*2^128 < 29126 ulps. */

  /* Remark: since eps3 = 2^-113.179, it would suffice to get about 113 bits
     of accuracy in exp_2(). We tried to reduce the degree of the minimax
     polynomial in q_2() from 7 to 6, or to use only 64 bits for the degree-3
     coefficient, but in both cases the accuracy was too small. */

  // Rounding test

  // 2^R.ex <= R < 2^(R.ex+1)

  /* case R < 2^-1075: underflow case */
  if (R.ex < -1075) {
#ifdef CORE_MATH_SUPPORT_ERRNO
    errno = ERANGE; // underflow
#endif
    return 0.5 * (s * 0x1p-1074);
  }

  if (R.ex < -1022) { /* subnormal case */
    // for x^y = 2^-1022, we can have R < 2^-1022 here

    /* -1075 <= R.ex <= -1023 thus 2^-1075 <= R < 2^-1022 */
    uint64_t ex = -(1022 + R.ex); /* 1 <= ex <= 53 */
    // the significand has to be shifted right by ex bits
    uint64_t m = R.lo >> (10 + ex) | R.hi << (54 - ex);

    /* We always have underflow when ex >= 2. However for ex=1,
       where 2^-1023 <= R < 2^-1022, we might not have underflow
       for rounding up if R >= 2^-1022 - 2^-1075, and for rounding
       to nearest for R >= 2^-1022 - 2^-1076. */

    /* In principle, the bound 28 which holds for the normal case below
       should be replaced by ceil(28/2^ex) since the relative error bound
       is the same as for the normal case, but since the round bit is 'shifted'
       by 'ex' bits to the left, we get 'ex' extra bits after the round bit.
       Since ex>=1 we replace 28 by 14. */

    rd = m + 14 > (2*14);

  } else {
#define ERR_BND_2 28
    uint64_t lo = R.lo >> 10 | R.hi << 54;
    /* lo contains the 64 bits after the round bit */
    rd = lo + ERR_BND_2 > (2*ERR_BND_2);
    /* The value 28 comes from floor(29126/2^10): rd is 0 when lo <= 28 or
       lo >= 2^64 - 28, which means that the approximation R is at distance
       < 29*2^10 = 29696 ulps of a rounding boundary */
  }

  R.sgn = s == -1.0;

  if (rd)
    // dint_tod should deal with underflow/overflow/errno issues
    return dint_tod (&R, exact);

#if ENABLE_EXACT > 0
  // Detect rounding boundary cases
  double e;

  if (exact_pow (&e, x0, y, &R, exact))
    return e;
#endif /* ENABLE_EXACT */
#endif /* ENABLE_ZIV2 */

  /* Note: exact and midpoint cases should be filtered by
     exact_pow() above, and should not enter the 2nd iteration.
     Thus check.sh might fail when POW_ITERATION | 2 == 0
     or POW_ITERATION | 4 == 0. */

#if ENABLE_ZIV3
  // Hard to round cases
  qint64_t qX, qY;
  qint_fromd (&qX, x); /* exact */
  qint_fromd (&qY, y); /* exact */

  qX.sgn = 0x0; /* force the sign of X to +1 */

  qint64_t qR;
  log_3 (&qR, &qX); /* relative error bounded by 2^-250.74 */

  mul_qint_41 (&qR, &qR, &qY);
  /* The rounding error of mul_qint_41() is bounded by 2 ulps, which is at most
     2*2^-255 in terms of relative error. Thus the relative error on qR is
     bounded by (1+e1)*(1+e2)-1 with e1=2^-250.74 and e2=2*2^-255,
     which gives a relative error less than 2^-250.59:
     qR = y*log|x| * (1+eps1) with |eps1| < 2^-250.59 */

  qint64_t qZ;
  exp_3 (&qZ, &qR); /* relative error < 2^-241.10:
                       qZ = exp(qR) * (1+eps2) with |eps2| < 2^-241.10 */

  /* We thus have qZ = |x|^y * exp(y*log|x|*eps1) * (1+eps2).
     Since y*log|x| < 744.45, we have |y*log|x|*eps1| < 744.45*2^-250.59
     < eps3 = 2^-241.049 thus the relative error is bounded by
     exp(eps3)*(1+eps2)-1 < 2^-240.07.
     This corresponds to an error of at most 2^-240.07*2^256 < 62433 ulps. */

  /* extra rounding test */
#define ERR_BND_3 60 /* floor(62433/2^10) */
  uint64_t r1 = qZ.hh << 54 | qZ.hl >> 10;
  uint64_t r2 = qZ.hl << 54 | qZ.lh >> 10;
  uint64_t r3 = qZ.lh << 54 | qZ.ll >> 10;
  rd = !((r1 == 0 && r2 == 0 && r3 <= ERR_BND_3) ||
         (~r1 == 0 && ~r2 == 0 && r3 + (2*ERR_BND_3) <= ERR_BND_3));

  if (rd)
  {
    qZ.sgn = s == -1.0;
    qZ.ll = qZ.ll & (~0ull << 10);

    return qint_tod (&qZ);
  }

  /* We can end up here for x^y very close to 1. For |qR| < 2^-55,
     we have 1-2^-54 < exp(qR) < 1+2^-53, thus exp(qR) rounds either
     to nextbelow(1), to 1 or to nextabove(1). */
  if (qR.ex < -56) /* the upper limb h of qR encodes h/2^63, thus a number
                      in [1, 2) */
    return (qR.sgn == 0x0) ? 1.0 + 0x1p-100 : 1.0 - 0x1p-100;

  // Embedding cannot terminate the database on an unresolved rounding test.
  resolved = false;
  return 0.0; // The wrapper ignores this placeholder when unresolved.

#else
  return -0.0;
#endif /* ENABLE_ZIV3 */
}
