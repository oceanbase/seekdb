/* Correctly-rounded cosine function for binary64 value.

Copyright (c) 2026 Paul Zimmermann

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

/* code adapted from sin.c */

#include <stdint.h>
#include <fenv.h> // for feraiseexcept and FE_INVALID
#ifdef CORE_MATH_SUPPORT_ERRNO
#include <errno.h>
#endif

// Warning: clang also defines __GNUC__
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

#pragma STDC FENV_ACCESS ON

#if (defined(__clang__) && __clang_major__ >= 14) || (defined(__GNUC__) && __GNUC__ >= 14 && __BITINT_MAXWIDTH__ && __BITINT_MAXWIDTH__ >= 128)
typedef unsigned _BitInt(128) u128;
#else
typedef unsigned __int128 u128;
#endif

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

typedef union {double f; uint64_t u;} b64u64_u;
typedef uint64_t u64;

#define U128(l,h) (((u128)h)<<64 | (u128)l) // little endian

/* The following is a degree-7 polynomial with odd coefficients
   approximating sin(2*pi/8*x) for 0 <= x < 2^-11 with absolute error
   < 2^-128.605. Coefficients of degree 1, 3, 5 are PS[i]/2^128,
   with that of degree 7 is PS[i]/2^64 (fixed point).
   Generated with sinlarge_acc.sollya. */
static const u128 PS[] = {
  // little-endian format
  U128(0xc4c6628b80dbed80,0xc90fdaa22168c234), // degree 1
  U128(0xabb8e5506ca45b03,0x14abbce625be52be), // degree 3, implicit - sign
  U128(0x44c54995bb97635,0xa335e33bad570e),    // degree 5
  U128(0x265a5999d06c4,0),                     // degree 7, implicit - sign
};

/* The following is a degree-8 polynomial with even coefficients
   approximating cos(2*pi/8*x) for 0 <= x < 2^-11, with absolute error < 2^-128.
   Coefficients are PC[i]/2^128 (fixed precision),
   except degree-8 coefficient which is PC[i]/2^64.
   Generated with coslarge_acc.sollya. */
static const u128 PC[] = {
  U128(0xffffffffffffffff,0xffffffffffffffff), // degree 0
  U128(0x2b71366cbcf0b096,0x4ef4f326f9177969), // degree 2, implicit - sign
  U128(0xd4c9076e3db78fed,0x40f07c206d6b0ec),  // degree 4
  U128(0xeec6962368fb7c41,0x155d3c7e3cbef9),   // degree 6, implicit - sign
  U128(0x3c3e845b5510,0),                      // degree 8
};

static inline double fasttwosum (double x, double y, double *e){
  double s = x + y, z = s - x;
  *e = y - z;
  return s;
}

static inline u128 mhUU (u128 a, u128 b){
  u64 ah = a>>64, al = a;
  u64 bh = b>>64, bl = b;
  u128 ahbh = (u128)ah*bh;
  u128 ahbl = (u128)ah*bl;
  u128 albh = (u128)al*bh;
  return ahbh += (ahbl>>64)+(albh>>64);
}

/* Return Sr such that Sr/2^128 approximates sin2pi(r), for 0 <= r < 2^-14,
   where u/2^128 approximates r'=8*r, u2/2^128 approximates r'^2,
   u4/2^128 approximates r'^4, and u2h = floor(u2/2^64). */
static inline u128
evalPS (u128 u, u128 u2, u128 u2h, u128 u4)
{
  u128 s;
  /* we perform the computation in fixed point, where each variable a is
     interpreted as a/2^128, thus multiplying two variables a and b mean
     taking floor(a*b/2^128) */
  // since signs of coefficients are alternating, and each new coefficient
  // dominates the lower terms, we subtract each time the lower terms from
  // the (absolute value of) the new coefficient
  // use Estrin's scheme: evaluate separately the degree-{1,3} part,
  // and the degree-{5,7} part, multiplied by r^4
  u128 sh = PS[2] - PS[3] * u2h;
  sh = mhUU (sh, u4);
  s = mhUU (PS[1], u2);
  s = PS[0] - s;
  s = s + sh;
  return mhUU (s, u); // multiply by r
}

/* Return Cr such that Cr/2^128 approximates cos2pi(r), for 0 <= r < 2^-14,
   where u2/2^128 approximates r'^2, u4/2^128 approximates r'^4, and
   u2h = floor(u2/2^64), with r'=8*r. */
static inline u128
evalPC (u128 u2, u128 u2h, u128 u4)
{
  // use Estrin's scheme
  u128 sh = PC[3] - u2h * PC[4];
  sh = PC[2] - mhUU (sh, u2);
  u128 s = mhUU (PC[1], u2);
  s = PC[0] - s;
  return s + mhUU (sh, u4);
}

static inline double fastsum (double xh, double xl, double yh, double yl, double *e){
  double sl, sh = fasttwosum (xh, yh, &sl);
  *e = (xl + yl) + sl;
  return sh;
}

static inline double muldd (double xh, double xl, double ch, double cl, double *l){
  double ahhh = xh*ch;
  *l = (xh*cl + xl*ch) + __builtin_fma (xh, ch, -ahhh);
  return ahhh;
}

/* for each j, 0 <= j < 128, U1[j] contains sh, sl, ch, cl where
   sh+sl is a double-double approximation of sin(j*pi/2^7) and
   ch+cl is a double-double approximation of cos(j*pi/2^7).
   Generated by U1() from sin.sage.
   There are some symmetries: U1[i][0,1] = U1[128-i][0,1] and
   U1[i][2,3] = -U1[128-i][2,3], but reducing the size of U1 to
   the first half to exploit symmetries leads to slower code. */
static const double U1[128][4] = {
  {0x0p+0, 0x0p+0, 0x1p+0, 0x0p+0},
  {0x1.92155f7a3667ep-6, -0x1.b1d63091a013p-64, 0x1.ffd886084cd0dp-1, -0x1.1354d4556e4cbp-55},
  {0x1.91f65f10dd814p-5, -0x1.912bd0d569a9p-61, 0x1.ff621e3796d7ep-1, -0x1.c57bc2e24aa15p-57},
  {0x1.2d52092ce19f6p-4, -0x1.9a088a8bf6b2cp-59, 0x1.fe9cdad01883ap-1, 0x1.521ecd0c67e35p-57},
  {0x1.917a6bc29b42cp-4, -0x1.e2718d26ed688p-60, 0x1.fd88da3d12526p-1, -0x1.87df6378811c7p-55},
  {0x1.f564e56a9730ep-4, 0x1.a2704729ae56dp-59, 0x1.fc26470e19fd3p-1, 0x1.1ec8668ecaceep-55},
  {0x1.2c8106e8e613ap-3, 0x1.13000a89a11ep-58, 0x1.fa7557f08a517p-1, -0x1.7a0a8ca13571fp-55},
  {0x1.5e214448b3fc6p-3, 0x1.531ff779ddac6p-57, 0x1.f8764fa714ba9p-1, 0x1.ab256778ffcb6p-56},
  {0x1.8f8b83c69a60bp-3, -0x1.26d19b9ff8d82p-57, 0x1.f6297cff75cbp-1, 0x1.562172a361fd3p-56},
  {0x1.c0b826a7e4f63p-3, -0x1.af1439e521935p-62, 0x1.f38f3ac64e589p-1, -0x1.d7bafb51f72e6p-56},
  {0x1.f19f97b215f1bp-3, -0x1.42deef11da2c4p-57, 0x1.f0a7efb9230d7p-1, 0x1.52c7adc6b4989p-56},
  {0x1.111d262b1f677p-2, 0x1.824c20ab7aa9ap-56, 0x1.ed740e7684963p-1, 0x1.e82c791f59cc2p-56},
  {0x1.294062ed59f06p-2, -0x1.5d28da2c4612dp-56, 0x1.e9f4156c62ddap-1, 0x1.760b1e2e3f81ep-55},
  {0x1.4135c94176601p-2, 0x1.0c97c4afa2518p-56, 0x1.e6288ec48e112p-1, -0x1.16b56f2847754p-57},
  {0x1.58f9a75ab1fddp-2, -0x1.efdc0d58cf62p-62, 0x1.e212104f686e5p-1, -0x1.014c76c126527p-55},
  {0x1.7088530fa459fp-2, -0x1.44b19e0864c5dp-56, 0x1.ddb13b6ccc23cp-1, 0x1.83c37c6107db3p-55},
  {0x1.87de2a6aea963p-2, -0x1.72cedd3d5a61p-57, 0x1.d906bcf328d46p-1, 0x1.457e610231ac2p-56},
  {0x1.9ef7943a8ed8ap-2, 0x1.6da81290bdbabp-57, 0x1.d4134d14dc93ap-1, -0x1.4ef5295d25af2p-55},
  {0x1.b5d1009e15ccp-2, 0x1.5b362cb974183p-57, 0x1.ced7af43cc773p-1, -0x1.e7b6bb5ab58aep-58},
  {0x1.cc66e9931c45ep-2, 0x1.6850e59c37f8fp-58, 0x1.c954b213411f5p-1, -0x1.2fb761e946603p-58},
  {0x1.e2b5d3806f63bp-2, 0x1.e0d891d3c6841p-58, 0x1.c38b2f180bdb1p-1, -0x1.6e0b1757c8d07p-56},
  {0x1.f8ba4dbf89abap-2, -0x1.2ec1fc1b776b8p-60, 0x1.bd7c0ac6f952ap-1, -0x1.825a732ac700ap-55},
  {0x1.073879922ffeep-1, -0x1.a5a014347406cp-55, 0x1.b728345196e3ep-1, -0x1.bc69f324e6d61p-55},
  {0x1.11eb3541b4b23p-1, -0x1.ef23b69abe4f1p-55, 0x1.b090a581502p-1, -0x1.926da300ffccep-55},
  {0x1.1c73b39ae68c8p-1, 0x1.b25dd267f66p-55, 0x1.a9b66290ea1a3p-1, 0x1.9f630e8b6dac8p-60},
  {0x1.26d054cdd12dfp-1, -0x1.5da743ef3770cp-55, 0x1.a29a7a0462782p-1, -0x1.128bb015df175p-56},
  {0x1.30ff7fce17035p-1, -0x1.efcc626f74a6fp-57, 0x1.9b3e047f38741p-1, -0x1.30ee286712474p-55},
  {0x1.3affa292050b9p-1, 0x1.e3e25e3954964p-56, 0x1.93a22499263fbp-1, 0x1.3d419a920df0bp-55},
  {0x1.44cf325091dd6p-1, 0x1.8076a2cfdc6b3p-57, 0x1.8bc806b151741p-1, -0x1.2c5e12ed1336dp-55},
  {0x1.4e6cabbe3e5e9p-1, 0x1.3c293edceb327p-57, 0x1.83b0e0bff976ep-1, -0x1.6f420f8ea3475p-56},
  {0x1.57d69348cecap-1, -0x1.75720992bfbb2p-55, 0x1.7b5df226aafafp-1, -0x1.0f537acdf0ad7p-56},
  {0x1.610b7551d2cdfp-1, -0x1.251b352ff2a37p-56, 0x1.72d0837efff96p-1, 0x1.0d4ef0f1d915cp-55},
  {0x1.6a09e667f3bcdp-1, -0x1.bdd3413b26456p-55, 0x1.6a09e667f3bcdp-1, -0x1.bdd3413b26456p-55},
  {0x1.72d0837efff96p-1, 0x1.0d4ef0f1d915cp-55, 0x1.610b7551d2cdfp-1, -0x1.251b352ff2a37p-56},
  {0x1.7b5df226aafafp-1, -0x1.0f537acdf0ad7p-56, 0x1.57d69348cecap-1, -0x1.75720992bfbb2p-55},
  {0x1.83b0e0bff976ep-1, -0x1.6f420f8ea3475p-56, 0x1.4e6cabbe3e5e9p-1, 0x1.3c293edceb327p-57},
  {0x1.8bc806b151741p-1, -0x1.2c5e12ed1336dp-55, 0x1.44cf325091dd6p-1, 0x1.8076a2cfdc6b3p-57},
  {0x1.93a22499263fbp-1, 0x1.3d419a920df0bp-55, 0x1.3affa292050b9p-1, 0x1.e3e25e3954964p-56},
  {0x1.9b3e047f38741p-1, -0x1.30ee286712474p-55, 0x1.30ff7fce17035p-1, -0x1.efcc626f74a6fp-57},
  {0x1.a29a7a0462782p-1, -0x1.128bb015df175p-56, 0x1.26d054cdd12dfp-1, -0x1.5da743ef3770cp-55},
  {0x1.a9b66290ea1a3p-1, 0x1.9f630e8b6dac8p-60, 0x1.1c73b39ae68c8p-1, 0x1.b25dd267f66p-55},
  {0x1.b090a581502p-1, -0x1.926da300ffccep-55, 0x1.11eb3541b4b23p-1, -0x1.ef23b69abe4f1p-55},
  {0x1.b728345196e3ep-1, -0x1.bc69f324e6d61p-55, 0x1.073879922ffeep-1, -0x1.a5a014347406cp-55},
  {0x1.bd7c0ac6f952ap-1, -0x1.825a732ac700ap-55, 0x1.f8ba4dbf89abap-2, -0x1.2ec1fc1b776b8p-60},
  {0x1.c38b2f180bdb1p-1, -0x1.6e0b1757c8d07p-56, 0x1.e2b5d3806f63bp-2, 0x1.e0d891d3c6841p-58},
  {0x1.c954b213411f5p-1, -0x1.2fb761e946603p-58, 0x1.cc66e9931c45ep-2, 0x1.6850e59c37f8fp-58},
  {0x1.ced7af43cc773p-1, -0x1.e7b6bb5ab58aep-58, 0x1.b5d1009e15ccp-2, 0x1.5b362cb974183p-57},
  {0x1.d4134d14dc93ap-1, -0x1.4ef5295d25af2p-55, 0x1.9ef7943a8ed8ap-2, 0x1.6da81290bdbabp-57},
  {0x1.d906bcf328d46p-1, 0x1.457e610231ac2p-56, 0x1.87de2a6aea963p-2, -0x1.72cedd3d5a61p-57},
  {0x1.ddb13b6ccc23cp-1, 0x1.83c37c6107db3p-55, 0x1.7088530fa459fp-2, -0x1.44b19e0864c5dp-56},
  {0x1.e212104f686e5p-1, -0x1.014c76c126527p-55, 0x1.58f9a75ab1fddp-2, -0x1.efdc0d58cf62p-62},
  {0x1.e6288ec48e112p-1, -0x1.16b56f2847754p-57, 0x1.4135c94176601p-2, 0x1.0c97c4afa2518p-56},
  {0x1.e9f4156c62ddap-1, 0x1.760b1e2e3f81ep-55, 0x1.294062ed59f06p-2, -0x1.5d28da2c4612dp-56},
  {0x1.ed740e7684963p-1, 0x1.e82c791f59cc2p-56, 0x1.111d262b1f677p-2, 0x1.824c20ab7aa9ap-56},
  {0x1.f0a7efb9230d7p-1, 0x1.52c7adc6b4989p-56, 0x1.f19f97b215f1bp-3, -0x1.42deef11da2c4p-57},
  {0x1.f38f3ac64e589p-1, -0x1.d7bafb51f72e6p-56, 0x1.c0b826a7e4f63p-3, -0x1.af1439e521935p-62},
  {0x1.f6297cff75cbp-1, 0x1.562172a361fd3p-56, 0x1.8f8b83c69a60bp-3, -0x1.26d19b9ff8d82p-57},
  {0x1.f8764fa714ba9p-1, 0x1.ab256778ffcb6p-56, 0x1.5e214448b3fc6p-3, 0x1.531ff779ddac6p-57},
  {0x1.fa7557f08a517p-1, -0x1.7a0a8ca13571fp-55, 0x1.2c8106e8e613ap-3, 0x1.13000a89a11ep-58},
  {0x1.fc26470e19fd3p-1, 0x1.1ec8668ecaceep-55, 0x1.f564e56a9730ep-4, 0x1.a2704729ae56dp-59},
  {0x1.fd88da3d12526p-1, -0x1.87df6378811c7p-55, 0x1.917a6bc29b42cp-4, -0x1.e2718d26ed688p-60},
  {0x1.fe9cdad01883ap-1, 0x1.521ecd0c67e35p-57, 0x1.2d52092ce19f6p-4, -0x1.9a088a8bf6b2cp-59},
  {0x1.ff621e3796d7ep-1, -0x1.c57bc2e24aa15p-57, 0x1.91f65f10dd814p-5, -0x1.912bd0d569a9p-61},
  {0x1.ffd886084cd0dp-1, -0x1.1354d4556e4cbp-55, 0x1.92155f7a3667ep-6, -0x1.b1d63091a013p-64},
  {0x1p+0, 0x0p+0, 0x0p+0, 0x0p+0},
  {0x1.ffd886084cd0dp-1, -0x1.1354d4556e4cbp-55, -0x1.92155f7a3667ep-6, 0x1.b1d63091a013p-64},
  {0x1.ff621e3796d7ep-1, -0x1.c57bc2e24aa15p-57, -0x1.91f65f10dd814p-5, 0x1.912bd0d569a9p-61},
  {0x1.fe9cdad01883ap-1, 0x1.521ecd0c67e35p-57, -0x1.2d52092ce19f6p-4, 0x1.9a088a8bf6b2cp-59},
  {0x1.fd88da3d12526p-1, -0x1.87df6378811c7p-55, -0x1.917a6bc29b42cp-4, 0x1.e2718d26ed688p-60},
  {0x1.fc26470e19fd3p-1, 0x1.1ec8668ecaceep-55, -0x1.f564e56a9730ep-4, -0x1.a2704729ae56dp-59},
  {0x1.fa7557f08a517p-1, -0x1.7a0a8ca13571fp-55, -0x1.2c8106e8e613ap-3, -0x1.13000a89a11ep-58},
  {0x1.f8764fa714ba9p-1, 0x1.ab256778ffcb6p-56, -0x1.5e214448b3fc6p-3, -0x1.531ff779ddac6p-57},
  {0x1.f6297cff75cbp-1, 0x1.562172a361fd3p-56, -0x1.8f8b83c69a60bp-3, 0x1.26d19b9ff8d82p-57},
  {0x1.f38f3ac64e589p-1, -0x1.d7bafb51f72e6p-56, -0x1.c0b826a7e4f63p-3, 0x1.af1439e521935p-62},
  {0x1.f0a7efb9230d7p-1, 0x1.52c7adc6b4989p-56, -0x1.f19f97b215f1bp-3, 0x1.42deef11da2c4p-57},
  {0x1.ed740e7684963p-1, 0x1.e82c791f59cc2p-56, -0x1.111d262b1f677p-2, -0x1.824c20ab7aa9ap-56},
  {0x1.e9f4156c62ddap-1, 0x1.760b1e2e3f81ep-55, -0x1.294062ed59f06p-2, 0x1.5d28da2c4612dp-56},
  {0x1.e6288ec48e112p-1, -0x1.16b56f2847754p-57, -0x1.4135c94176601p-2, -0x1.0c97c4afa2518p-56},
  {0x1.e212104f686e5p-1, -0x1.014c76c126527p-55, -0x1.58f9a75ab1fddp-2, 0x1.efdc0d58cf62p-62},
  {0x1.ddb13b6ccc23cp-1, 0x1.83c37c6107db3p-55, -0x1.7088530fa459fp-2, 0x1.44b19e0864c5dp-56},
  {0x1.d906bcf328d46p-1, 0x1.457e610231ac2p-56, -0x1.87de2a6aea963p-2, 0x1.72cedd3d5a61p-57},
  {0x1.d4134d14dc93ap-1, -0x1.4ef5295d25af2p-55, -0x1.9ef7943a8ed8ap-2, -0x1.6da81290bdbabp-57},
  {0x1.ced7af43cc773p-1, -0x1.e7b6bb5ab58aep-58, -0x1.b5d1009e15ccp-2, -0x1.5b362cb974183p-57},
  {0x1.c954b213411f5p-1, -0x1.2fb761e946603p-58, -0x1.cc66e9931c45ep-2, -0x1.6850e59c37f8fp-58},
  {0x1.c38b2f180bdb1p-1, -0x1.6e0b1757c8d07p-56, -0x1.e2b5d3806f63bp-2, -0x1.e0d891d3c6841p-58},
  {0x1.bd7c0ac6f952ap-1, -0x1.825a732ac700ap-55, -0x1.f8ba4dbf89abap-2, 0x1.2ec1fc1b776b8p-60},
  {0x1.b728345196e3ep-1, -0x1.bc69f324e6d61p-55, -0x1.073879922ffeep-1, 0x1.a5a014347406cp-55},
  {0x1.b090a581502p-1, -0x1.926da300ffccep-55, -0x1.11eb3541b4b23p-1, 0x1.ef23b69abe4f1p-55},
  {0x1.a9b66290ea1a3p-1, 0x1.9f630e8b6dac8p-60, -0x1.1c73b39ae68c8p-1, -0x1.b25dd267f66p-55},
  {0x1.a29a7a0462782p-1, -0x1.128bb015df175p-56, -0x1.26d054cdd12dfp-1, 0x1.5da743ef3770cp-55},
  {0x1.9b3e047f38741p-1, -0x1.30ee286712474p-55, -0x1.30ff7fce17035p-1, 0x1.efcc626f74a6fp-57},
  {0x1.93a22499263fbp-1, 0x1.3d419a920df0bp-55, -0x1.3affa292050b9p-1, -0x1.e3e25e3954964p-56},
  {0x1.8bc806b151741p-1, -0x1.2c5e12ed1336dp-55, -0x1.44cf325091dd6p-1, -0x1.8076a2cfdc6b3p-57},
  {0x1.83b0e0bff976ep-1, -0x1.6f420f8ea3475p-56, -0x1.4e6cabbe3e5e9p-1, -0x1.3c293edceb327p-57},
  {0x1.7b5df226aafafp-1, -0x1.0f537acdf0ad7p-56, -0x1.57d69348cecap-1, 0x1.75720992bfbb2p-55},
  {0x1.72d0837efff96p-1, 0x1.0d4ef0f1d915cp-55, -0x1.610b7551d2cdfp-1, 0x1.251b352ff2a37p-56},
  {0x1.6a09e667f3bcdp-1, -0x1.bdd3413b26456p-55, -0x1.6a09e667f3bcdp-1, 0x1.bdd3413b26456p-55},
  {0x1.610b7551d2cdfp-1, -0x1.251b352ff2a37p-56, -0x1.72d0837efff96p-1, -0x1.0d4ef0f1d915cp-55},
  {0x1.57d69348cecap-1, -0x1.75720992bfbb2p-55, -0x1.7b5df226aafafp-1, 0x1.0f537acdf0ad7p-56},
  {0x1.4e6cabbe3e5e9p-1, 0x1.3c293edceb327p-57, -0x1.83b0e0bff976ep-1, 0x1.6f420f8ea3475p-56},
  {0x1.44cf325091dd6p-1, 0x1.8076a2cfdc6b3p-57, -0x1.8bc806b151741p-1, 0x1.2c5e12ed1336dp-55},
  {0x1.3affa292050b9p-1, 0x1.e3e25e3954964p-56, -0x1.93a22499263fbp-1, -0x1.3d419a920df0bp-55},
  {0x1.30ff7fce17035p-1, -0x1.efcc626f74a6fp-57, -0x1.9b3e047f38741p-1, 0x1.30ee286712474p-55},
  {0x1.26d054cdd12dfp-1, -0x1.5da743ef3770cp-55, -0x1.a29a7a0462782p-1, 0x1.128bb015df175p-56},
  {0x1.1c73b39ae68c8p-1, 0x1.b25dd267f66p-55, -0x1.a9b66290ea1a3p-1, -0x1.9f630e8b6dac8p-60},
  {0x1.11eb3541b4b23p-1, -0x1.ef23b69abe4f1p-55, -0x1.b090a581502p-1, 0x1.926da300ffccep-55},
  {0x1.073879922ffeep-1, -0x1.a5a014347406cp-55, -0x1.b728345196e3ep-1, 0x1.bc69f324e6d61p-55},
  {0x1.f8ba4dbf89abap-2, -0x1.2ec1fc1b776b8p-60, -0x1.bd7c0ac6f952ap-1, 0x1.825a732ac700ap-55},
  {0x1.e2b5d3806f63bp-2, 0x1.e0d891d3c6841p-58, -0x1.c38b2f180bdb1p-1, 0x1.6e0b1757c8d07p-56},
  {0x1.cc66e9931c45ep-2, 0x1.6850e59c37f8fp-58, -0x1.c954b213411f5p-1, 0x1.2fb761e946603p-58},
  {0x1.b5d1009e15ccp-2, 0x1.5b362cb974183p-57, -0x1.ced7af43cc773p-1, 0x1.e7b6bb5ab58aep-58},
  {0x1.9ef7943a8ed8ap-2, 0x1.6da81290bdbabp-57, -0x1.d4134d14dc93ap-1, 0x1.4ef5295d25af2p-55},
  {0x1.87de2a6aea963p-2, -0x1.72cedd3d5a61p-57, -0x1.d906bcf328d46p-1, -0x1.457e610231ac2p-56},
  {0x1.7088530fa459fp-2, -0x1.44b19e0864c5dp-56, -0x1.ddb13b6ccc23cp-1, -0x1.83c37c6107db3p-55},
  {0x1.58f9a75ab1fddp-2, -0x1.efdc0d58cf62p-62, -0x1.e212104f686e5p-1, 0x1.014c76c126527p-55},
  {0x1.4135c94176601p-2, 0x1.0c97c4afa2518p-56, -0x1.e6288ec48e112p-1, 0x1.16b56f2847754p-57},
  {0x1.294062ed59f06p-2, -0x1.5d28da2c4612dp-56, -0x1.e9f4156c62ddap-1, -0x1.760b1e2e3f81ep-55},
  {0x1.111d262b1f677p-2, 0x1.824c20ab7aa9ap-56, -0x1.ed740e7684963p-1, -0x1.e82c791f59cc2p-56},
  {0x1.f19f97b215f1bp-3, -0x1.42deef11da2c4p-57, -0x1.f0a7efb9230d7p-1, -0x1.52c7adc6b4989p-56},
  {0x1.c0b826a7e4f63p-3, -0x1.af1439e521935p-62, -0x1.f38f3ac64e589p-1, 0x1.d7bafb51f72e6p-56},
  {0x1.8f8b83c69a60bp-3, -0x1.26d19b9ff8d82p-57, -0x1.f6297cff75cbp-1, -0x1.562172a361fd3p-56},
  {0x1.5e214448b3fc6p-3, 0x1.531ff779ddac6p-57, -0x1.f8764fa714ba9p-1, -0x1.ab256778ffcb6p-56},
  {0x1.2c8106e8e613ap-3, 0x1.13000a89a11ep-58, -0x1.fa7557f08a517p-1, 0x1.7a0a8ca13571fp-55},
  {0x1.f564e56a9730ep-4, 0x1.a2704729ae56dp-59, -0x1.fc26470e19fd3p-1, -0x1.1ec8668ecaceep-55},
  {0x1.917a6bc29b42cp-4, -0x1.e2718d26ed688p-60, -0x1.fd88da3d12526p-1, 0x1.87df6378811c7p-55},
  {0x1.2d52092ce19f6p-4, -0x1.9a088a8bf6b2cp-59, -0x1.fe9cdad01883ap-1, -0x1.521ecd0c67e35p-57},
  {0x1.91f65f10dd814p-5, -0x1.912bd0d569a9p-61, -0x1.ff621e3796d7ep-1, 0x1.c57bc2e24aa15p-57},
  {0x1.92155f7a3667ep-6, -0x1.b1d63091a013p-64, -0x1.ffd886084cd0dp-1, 0x1.1354d4556e4cbp-55},
};

/* for each j, 0 <= j < 128, U2[j] contains sh, sl, ch, cl where
   sh+sl is a double-double approximation of sin(j*pi/2^14) and
   ch+cl is a double-double approximation of cos(j*pi/2^14).
   Generated by U2() from sin.sage */
static const double U2[128][4] = {
  {0x0p+0, 0x0p+0, 0x1p+0, 0x0p+0},
  {0x1.921fb51aeb57cp-13, -0x1.a6e1d4916c435p-67, 0x1.ffffff621619cp-1, -0x1.7507dbbbd8fe6p-55},
  {0x1.921fb49ee4ea6p-12, 0x1.e894d744a453ep-66, 0x1.fffffd8858675p-1, -0x1.79f0e54748eabp-55},
  {0x1.2d97c6dc23a75p-11, -0x1.1c27727253849p-66, 0x1.fffffa72c6e9dp-1, 0x1.ffa0e2a0e5c29p-56},
  {0x1.921fb2aecb36p-11, 0x1.876157e566b4cp-65, 0x1.fffff62161a34p-1, -0x1.136dcb1f9b9c4p-57},
  {0x1.f6a79d8965ebap-11, -0x1.a2a951670a396p-65, 0x1.fffff09428963p-1, 0x1.2f505fba9c0bp-55},
  {0x1.2d97c396f8497p-10, -0x1.45cb4cc3d0fb1p-66, 0x1.ffffe9cb1bc62p-1, 0x1.0fe9fa3478ec9p-56},
  {0x1.5fdbb7af33fbbp-10, 0x1.c0c37a5d5101dp-65, 0x1.ffffe1c63b373p-1, 0x1.a50f155e87e56p-55},
  {0x1.921faaee6472ep-10, -0x1.ee52e284a9df8p-64, 0x1.ffffd88586ee6p-1, 0x1.1af64f173ae5bp-55},
  {0x1.c4639d358815ap-10, 0x1.0ffa0e86b4dccp-69, 0x1.ffffce08fef16p-1, 0x1.92e420a03bf59p-58},
  {0x1.f6a78e659d4b6p-10, 0x1.cb8a9b355ac8p-64, 0x1.ffffc250a346ap-1, 0x1.d099c02280c58p-56},
  {0x1.1475bf2fd13e2p-9, -0x1.4ae6ded9797ccp-63, 0x1.ffffb55c73f56p-1, 0x1.ee2ae1596963ap-55},
  {0x1.2d97b6824b087p-9, -0x1.9dae69dd4f97fp-63, 0x1.ffffa72c7105bp-1, -0x1.564ec4a452371p-55},
  {0x1.46b9ad1abb397p-9, -0x1.ac1b1950e9dc6p-64, 0x1.ffff97c09a803p-1, -0x1.961ec991858f1p-56},
  {0x1.5fdba2e9a1066p-9, 0x1.f1ed8abaaa608p-64, 0x1.ffff8718f06e7p-1, 0x1.793938ee3fc9ap-57},
  {0x1.78fd97df7ba51p-9, -0x1.2f2580c9ba856p-63, 0x1.ffff753572dacp-1, -0x1.5df4d689b3227p-57},
  {0x1.921f8becca4bap-9, 0x1.2ba407bcab5b2p-63, 0x1.ffff621621d02p-1, -0x1.6acfcebc82813p-56},
  {0x1.ab417f020c31p-9, 0x1.7fb6c9ae3781ap-65, 0x1.ffff4dbafd5a6p-1, -0x1.d017de84ebc3fp-55},
  {0x1.c463710fc08c9p-9, -0x1.f621bbef801cfp-64, 0x1.ffff38240586p-1, -0x1.f949383d834ep-61},
  {0x1.dd85620666965p-9, 0x1.5a1a4ecd8345ap-65, 0x1.ffff21513a606p-1, 0x1.ee48b39cc31cfp-56},
  {0x1.f6a751d67d871p-9, -0x1.66bdef183ff59p-63, 0x1.ffff09429bf7ap-1, -0x1.cabd3ded13a63p-55},
  {0x1.07e4a038424c1p-8, 0x1.7e6886ed9bec7p-63, 0x1.fffeeff82a5a7p-1, 0x1.6efc1cf23e273p-55},
  {0x1.147596e27d81fp-8, -0x1.bbfe9edba714ap-62, 0x1.fffed571e5989p-1, 0x1.151c0e15503cfp-55},
  {0x1.21068ce230028p-8, 0x1.6ce44ca79798dp-63, 0x1.fffeb9afcdc25p-1, 0x1.32c5af9456b9fp-57},
  {0x1.2d97822f996bcp-8, 0x1.3e5a15ed6aa3ep-62, 0x1.fffe9cb1e2e8dp-1, -0x1.10f44663fd601p-55},
  {0x1.3a2876c2f95cp-8, 0x1.03f5805a36f7ep-63, 0x1.fffe7e78251dep-1, 0x1.89b8834c8800cp-55},
  {0x1.46b96a948f72p-8, -0x1.3fbff884c87dap-65, 0x1.fffe5f0294744p-1, 0x1.5e809bdc855fcp-55},
  {0x1.534a5d9c9b4dp-8, -0x1.8653d3b3f3bf5p-62, 0x1.fffe3e5130ff5p-1, 0x1.8d94f3d6ca2d6p-57},
  {0x1.5fdb4fd35c8cbp-8, -0x1.4b73d73e9b437p-62, 0x1.fffe1c63fad33p-1, 0x1.429d08eb02c47p-55},
  {0x1.6c6c413112d15p-8, -0x1.c66913985b2a8p-63, 0x1.fffdf93af204ep-1, -0x1.4ade22d9f9483p-56},
  {0x1.78fd31adfdbbap-8, -0x1.03bf7bee2893dp-64, 0x1.fffdd4d616aap-1, -0x1.422710e8c8595p-55},
  {0x1.858e21425cecfp-8, -0x1.5af2befb31c3cp-63, 0x1.fffdaf3568d9p-1, 0x1.99c62dc3a22acp-58},
  {0x1.921f0fe670071p-8, 0x1.ab967fe6b7a9bp-64, 0x1.fffd8858e8a92p-1, 0x1.359c71883bcf7p-55},
  {0x1.9eaffd9276ac8p-8, -0x1.558e4e5ccafc7p-62, 0x1.fffd604096326p-1, -0x1.7d6bdd1c1f436p-60},
  {0x1.ab40ea3eb0803p-8, -0x1.7fe2a8e15f257p-67, 0x1.fffd36ec718d7p-1, -0x1.5c59ffcfba7e4p-56},
  {0x1.b7d1d5e35d25dp-8, 0x1.9cfe67f192cd7p-62, 0x1.fffd0c5c7ad3dp-1, -0x1.19d99906d21e9p-55},
  {0x1.c462c078bc41bp-8, 0x1.9f6db9a6ada3cp-62, 0x1.fffce090b21fcp-1, -0x1.0389138f7e5efp-55},
  {0x1.d0f3a9f70d78cp-8, -0x1.a08e48b732df4p-65, 0x1.fffcb389178c4p-1, 0x1.2883b1fe7d578p-56},
  {0x1.dd84925690709p-8, -0x1.0441c939bd6dap-62, 0x1.fffc8545ab352p-1, 0x1.61157c68f984bp-55},
  {0x1.ea15798f84cf7p-8, -0x1.bd41d0fe0c595p-62, 0x1.fffc55c66d36fp-1, -0x1.ab98d6fcf9cd6p-58},
  {0x1.f6a65f9a2a3c6p-8, -0x1.de8c48783f3aep-62, 0x1.fffc250b5daefp-1, -0x1.13b48657081cdp-55},
  {0x1.019ba237602f9p-7, -0x1.2479c7145927dp-61, 0x1.fffbf3147cbb3p-1, -0x1.6a078c1216e21p-55},
  {0x1.07e41402c3701p-7, -0x1.93e58a2a04d27p-67, 0x1.fffbbfe1ca7a8p-1, -0x1.698b82e41cfaep-56},
  {0x1.0e2c852b5eb46p-7, -0x1.9b8495e4ec41dp-61, 0x1.fffb8b73470c8p-1, -0x1.bc4e4be396442p-55},
  {0x1.1474f5ad51d17p-7, -0x1.a0fb850f9330dp-62, 0x1.fffb55c8f2917p-1, 0x1.6af540576eb51p-55},
  {0x1.1abd6584bc9cbp-7, 0x1.21b927d9670ccp-61, 0x1.fffb1ee2cd2a9p-1, -0x1.3ee83959a5f1dp-56},
  {0x1.2105d4adbeecp-7, 0x1.4e6ed5742dcf7p-61, 0x1.fffae6c0d6f9ap-1, -0x1.102fd2cb5b72p-56},
  {0x1.274e43247895ap-7, -0x1.46ca3539434eap-63, 0x1.fffaad6310214p-1, 0x1.b8629443a759ap-55},
  {0x1.2d96b0e509703p-7, -0x1.1e9131ff52dc9p-63, 0x1.fffa72c978c4fp-1, -0x1.22cb000328f91p-55},
  {0x1.33df1deb9152dp-7, 0x1.f5fb935dbf892p-62, 0x1.fffa36f41108bp-1, 0x1.55d1c829905c6p-57},
  {0x1.3a278a3430152p-7, -0x1.2c26d82855518p-63, 0x1.fff9f9e2d9118p-1, 0x1.102f90f3495ep-57},
  {0x1.406ff5bb058f1p-7, 0x1.6e66ff4bcd327p-61, 0x1.fff9bb95d105p-1, 0x1.7fb680db05f83p-55},
  {0x1.46b8607c31993p-7, 0x1.91d1ac2a2ced6p-61, 0x1.fff97c0cf909bp-1, -0x1.b418b1f88cf02p-57},
  {0x1.4d00ca73d40c8p-7, -0x1.fae7469c8dbd1p-61, 0x1.fff93b485146bp-1, -0x1.43e02406fb373p-55},
  {0x1.5349339e0cc25p-7, -0x1.ec016ea8e8f27p-64, 0x1.fff8f947d9e3fp-1, -0x1.2cefd02ebe75cp-59},
  {0x1.59919bf6fb94bp-7, -0x1.1ca7261e51e18p-62, 0x1.fff8b60b930a3p-1, 0x1.97dbe1fdcab9p-56},
  {0x1.5fda037ac05e1p-7, -0x1.ff59bf4b574eep-61, 0x1.fff871937ce2fp-1, -0x1.38dae49f0be32p-57},
  {0x1.66226a257af95p-7, 0x1.3cd7a66dfafc8p-61, 0x1.fff82bdf97986p-1, -0x1.507b1e2913e3bp-57},
  {0x1.6c6acff34b421p-7, 0x1.b03798bbe7197p-63, 0x1.fff7e4efe3558p-1, 0x1.ebe425d873a16p-57},
  {0x1.72b334e051144p-7, 0x1.caf2df425ef35p-62, 0x1.fff79cc460462p-1, -0x1.6c8d94412b92ap-55},
  {0x1.78fb98e8ac4c8p-7, -0x1.555202816f3a5p-61, 0x1.fff7535d0e96bp-1, -0x1.c5727ff41299bp-56},
  {0x1.7f43fc087cc7dp-7, 0x1.00a2a3a85d253p-61, 0x1.fff708b9ee748p-1, -0x1.7ff8f5cefe85ap-60},
  {0x1.858c5e3be264p-7, -0x1.ee9c7105192d8p-62, 0x1.fff6bcdb000dap-1, -0x1.65348e4f5cd16p-57},
  {0x1.8bd4bf7efcff3p-7, 0x1.66640846e6109p-63, 0x1.fff66fc04390dp-1, 0x1.77f5704f2756cp-55},
  {0x1.921d1fcdec784p-7, 0x1.9878ebe836d9dp-61, 0x1.fff62169b92dbp-1, 0x1.5dda3c81fbd0dp-55},
  {0x1.98657f24d0aeap-7, 0x1.11d4d1d1806d2p-61, 0x1.fff5d1d761149p-1, 0x1.e7b86f31e2875p-63},
  {0x1.9eaddd7fc9825p-7, -0x1.5adb5adfd3669p-62, 0x1.fff581093b768p-1, -0x1.3eeee9513ae4cp-55},
  {0x1.a4f63adaf6d3ep-7, -0x1.f838d8dd726dcp-64, 0x1.fff52eff48855p-1, -0x1.514532e82de3fp-57},
  {0x1.ab3e973278849p-7, 0x1.35b145a18353cp-61, 0x1.fff4dbb98873ap-1, 0x1.857663dc92252p-55},
  {0x1.b186f2826e765p-7, -0x1.6b3b32921e88cp-61, 0x1.fff48737fb74ep-1, -0x1.d462b0c1681e7p-58},
  {0x1.b7cf4cc6f88b8p-7, -0x1.32c6a4623533p-62, 0x1.fff4317aa1bd2p-1, -0x1.6a0039355b637p-55},
  {0x1.be17a5fc36a75p-7, 0x1.c404548edfc51p-63, 0x1.fff3da817b814p-1, -0x1.2b464e14730cp-55},
  {0x1.c45ffe1e48ad9p-7, 0x1.4060e4bd32e79p-63, 0x1.fff3824c88f6fp-1, -0x1.ed820f3fe698p-55},
  {0x1.caa855294e82bp-7, 0x1.eca8fa79834dep-66, 0x1.fff328dbca549p-1, -0x1.6ca1af321dc1cp-55},
  {0x1.d0f0ab19680bdp-7, -0x1.e9c612f8a4102p-64, 0x1.fff2ce2f3fd15p-1, -0x1.62ccf8bdb122fp-56},
  {0x1.d738ffeab52ecp-7, -0x1.c110bc5257c8cp-62, 0x1.fff27246e9a52p-1, -0x1.16d6346f3f55fp-59},
  {0x1.dd81539955d2p-7, -0x1.eaf6d880c7f01p-61, 0x1.fff21522c808bp-1, 0x1.a190e2d2eaf6ep-56},
  {0x1.e3c9a62169dcbp-7, 0x1.6666333e35e32p-61, 0x1.fff1b6c2db358p-1, -0x1.f4664a9ad833cp-56},
  {0x1.ea11f77f1136ep-7, -0x1.7a617506aacb8p-62, 0x1.fff157272365bp-1, 0x1.44ef64386476p-57},
  {0x1.f05a47ae6bc91p-7, -0x1.0935bb936be66p-64, 0x1.fff0f64fa0d45p-1, -0x1.ac9acb9bb8d2dp-56},
  {0x1.f6a296ab997cbp-7, -0x1.f2943d8fe7033p-61, 0x1.fff0943c53bd1p-1, -0x1.47399f361d158p-55},
  {0x1.fceae472ba3bcp-7, -0x1.3a08b1804396p-64, 0x1.fff030ed3c5c7p-1, -0x1.2504b1dc047e8p-55},
  {0x1.0199987ff6f89p-6, 0x1.8daccef2e2556p-60, 0x1.ffefcc625aefbp-1, 0x1.fbb0dd6af1603p-59},
  {0x1.04bdbe27aa444p-6, 0x1.0c87a06c6956dp-60, 0x1.ffef669bafb4ep-1, -0x1.b5a4e5318177bp-58},
  {0x1.07e1e32e86f72p-6, -0x1.f40145dde0463p-60, 0x1.ffeeff993aeacp-1, -0x1.99db334be163bp-58},
  {0x1.0b0607929d07ap-6, 0x1.f989b780d54e7p-61, 0x1.ffee975afcd0ep-1, -0x1.2df82b9320ecdp-55},
  {0x1.0e2a2b51fc6cep-6, -0x1.2ca118aaa212fp-61, 0x1.ffee2de0f5a78p-1, 0x1.9daa217acc1bp-58},
  {0x1.114e4e6ab51e2p-6, 0x1.10d53d5e0ffc5p-62, 0x1.ffedc32b25afcp-1, -0x1.bb89d7b0d4487p-64},
  {0x1.147270dad7133p-6, 0x1.8769e00e018p-63, 0x1.ffed57398d2b7p-1, -0x1.0bb0db6384cf3p-55},
  {0x1.179692a072443p-6, 0x1.d40c977f2fe27p-60, 0x1.ffecea0c2c5d2p-1, -0x1.7a22a10e4cb4fp-55},
  {0x1.1abab3b996a9dp-6, -0x1.e21a2ef2391d3p-60, 0x1.ffec7ba303882p-1, 0x1.b209ec7100e1ap-56},
  {0x1.1dded424543cep-6, 0x1.2ecdb3d03a70bp-61, 0x1.ffec0bfe12f0ap-1, 0x1.8b1483b4090edp-56},
  {0x1.2102f3debaf6fp-6, -0x1.45a1ea37e10eap-62, 0x1.ffeb9b1d5adb7p-1, 0x1.d5b363c2f437p-55},
  {0x1.242712e6dad1cp-6, 0x1.0317d8ce1d1b9p-60, 0x1.ffeb2900db8e4p-1, 0x1.18f6a66301d6p-57},
  {0x1.274b313ac3c7ap-6, 0x1.b29e49acf7e7ep-60, 0x1.ffeab5a8954f6p-1, 0x1.0652cfe4cebfap-55},
  {0x1.2a6f4ed885d35p-6, -0x1.5a8a13d8f9966p-60, 0x1.ffea41148866p-1, 0x1.b77e2735c69f9p-55},
  {0x1.2d936bbe30efdp-6, 0x1.b5f91ee371d64p-61, 0x1.ffe9cb44b51a1p-1, 0x1.5b43366df667p-56},
  {0x1.30b787e9d518dp-6, 0x1.b620c940df42ap-60, 0x1.ffe954391bb43p-1, 0x1.ddd5377beef1ap-56},
  {0x1.33dba359824a6p-6, -0x1.6c4020b5f32a8p-61, 0x1.ffe8dbf1bc7dep-1, -0x1.cc391831cbcdp-55},
  {0x1.36ffbe0b4880ep-6, 0x1.4b88026f4e07bp-61, 0x1.ffe8626e97c13p-1, 0x1.cdb27525fcaf7p-56},
  {0x1.3a23d7fd37b96p-6, -0x1.dd9e849a0a5d5p-60, 0x1.ffe7e7afadc94p-1, -0x1.db1225b06b02cp-55},
  {0x1.3d47f12d5ff12p-6, 0x1.add51eab25dd4p-60, 0x1.ffe76bb4fee1ap-1, -0x1.0c43dae9e89c6p-57},
  {0x1.406c0999d1263p-6, -0x1.cf7cbeea1ddcfp-61, 0x1.ffe6ee7e8b56ep-1, 0x1.7ecac48a7b7bcp-58},
  {0x1.439021409b56cp-6, 0x1.81352c74c7ae2p-62, 0x1.ffe6700c53764p-1, -0x1.4f502302afd49p-55},
  {0x1.46b4381fce81bp-6, 0x1.ff9f89fb65be3p-60, 0x1.ffe5f05e578dbp-1, -0x1.b71f7469ecc13p-56},
  {0x1.49d84e357aa66p-6, -0x1.9143373e36698p-61, 0x1.ffe56f7497ecp-1, -0x1.ddf92a8d40b3p-55},
  {0x1.4cfc637fafc48p-6, -0x1.a3541bb15d5bap-60, 0x1.ffe4ed4f14e0ap-1, 0x1.e48478b2f0ba1p-56},
  {0x1.502077fc7ddc5p-6, 0x1.25236a8347d96p-60, 0x1.ffe469edcebbfp-1, 0x1.8fc7557d6bc53p-55},
  {0x1.53448ba9f4eebp-6, 0x1.e6adde30ec695p-61, 0x1.ffe3e550c5cfp-1, -0x1.58fb3ae72192ep-55},
  {0x1.56689e8624fcep-6, -0x1.b967a2d3ee5b3p-61, 0x1.ffe35f77fa6b8p-1, -0x1.a7661b121429ap-57},
  {0x1.598cb08f1e089p-6, 0x1.56f9c34c216b4p-60, 0x1.ffe2d8636ce41p-1, 0x1.b4243168ab5a4p-57},
  {0x1.5cb0c1c2f0142p-6, 0x1.c2c42ac4d436cp-60, 0x1.ffe250131d8cp-1, 0x1.ed2d09d5789d3p-55},
  {0x1.5fd4d21fab226p-6, -0x1.0c0a91c37851cp-61, 0x1.ffe1c6870cb77p-1, 0x1.89aa14768323ep-55},
  {0x1.62f8e1a35f369p-6, -0x1.44ab93e20133fp-60, 0x1.ffe13bbf3abb3p-1, 0x1.6721eee289b1dp-55},
  {0x1.661cf04c1c548p-6, 0x1.3d6cc7ce6ff57p-60, 0x1.ffe0afbba7ecep-1, 0x1.6ecd981464044p-57},
  {0x1.6940fe17f280bp-6, -0x1.84bf8988cc42p-60, 0x1.ffe0227c54a2dp-1, 0x1.eeed1d70e13e2p-55},
  {0x1.6c650b04f1bfep-6, 0x1.53e382471fa0cp-69, 0x1.ffdf940141344p-1, -0x1.a63fc248c2dacp-55},
  {0x1.6f8917112a179p-6, 0x1.b6de0ad6e9445p-60, 0x1.ffdf044a6df8fp-1, -0x1.798cfb498d8b1p-55},
  {0x1.72ad223aab8ddp-6, -0x1.cf17a0ad13b4ap-60, 0x1.ffde7357db499p-1, 0x1.09337deb3ff5ap-59},
  {0x1.75d12c7f8629p-6, -0x1.07d8473495aaep-60, 0x1.ffdde129897f9p-1, 0x1.44e8786dedb6bp-55},
  {0x1.78f535ddc9f04p-6, 0x1.b194ad9b1aa97p-61, 0x1.ffdd4dbf78f52p-1, 0x1.216679a26323dp-55},
  {0x1.7c193e5386eb4p-6, 0x1.725118c2860e7p-63, 0x1.ffdcb919aa053p-1, -0x1.55c1ce37ae6f6p-56},
  {0x1.7f3d45decd222p-6, 0x1.2fe8cfe80809dp-60, 0x1.ffdc23381d0b6p-1, 0x1.00d15f9603fedp-57},
  {0x1.82614c7dac9dbp-6, 0x1.e95f011ac18c6p-63, 0x1.ffdb8c1ad2643p-1, 0x1.e79471744a0cbp-56},
  {0x1.8585522e35674p-6, -0x1.d5a766c5894c1p-60, 0x1.ffdaf3c1ca6cep-1, -0x1.9c0f0834fa422p-56},
  {0x1.88a956ee7788ap-6, 0x1.19dfbd12ff726p-61, 0x1.ffda5a2d05835p-1, 0x1.6dbc405ff8e25p-55},
  {0x1.8bcd5abc830c7p-6, -0x1.f7d609f14a311p-60, 0x1.ffd9bf5c84066p-1, -0x1.35167a6ce828dp-55},
  {0x1.8ef15d9667fdap-6, -0x1.668dc9ba82a7p-60, 0x1.ffd9235046557p-1, -0x1.c3c6e16616fb2p-56},
};

static const double pih = -0x1.921fb54442d18p-13;
static const double pil = -0x1.1a62633145c07p-67;

/* This table approximates 1/(2pi) downwards with precision 1280:
   1/(2*pi) ~ T[0]/2^0 + T[1]/2^64 + ... + T[i]/2^(i*64) + ...
   Computed with computeT() from sin.sage, and manually added entry 0. */
static const uint64_t _T[20] = {
  0,
  0x28be60db9391054a, // i=1
   0x7f09d5f47d4d3770,
   0x36d8a5664f10e410,
   0x7f9458eaf7aef158,
   0x6dc91b8e909374b8,
   0x1924bba82746487, // i=6
   0x3f877ac72c4a69cf,
   0xba208d7d4baed121,
   0x3a671c09ad17df90,
   0x4e64758e60d4ce7d,
   0x272117e2ef7e4a0e, // i=11
   0xc7fe25fff7816603,
   0xfbcbc462d6829b47,
   0xdb4d9fb3c9f2c26d,
   0xd3d18fd9a797fa8b,
   0x5d49eeb1faf97c5e, // i=16
   0xcf41ce7de294a4ba,
   0x9afed7ec47e35742,
   0x1580cc11bf1edaea, // i=19 (only used in reduce_large_acc)
   // 0xfc33ef0826bd0d87, // i=20 (unused)
};

/* For 0 <= i <= 32, S1u[i] contains a 128-bit unsigned integer m
   such that m/2^128 approximates sin(pi*i/2^6) to nearest.
   The entry for i=32 is capped to 2^128-1.
   Generated by computeS1u() from sin.sage (truncated to first 33 entries). */
static const u128 S1u[33] = {
  U128(0x0,0x0),
  U128(0x76a17954b2b7c517,0xc8fb2f886ec09f3),
  U128(0xd8e72d912977ee71,0x1917a6bc29b42be1),
  U128(0xc002a2684781f080,0x259020dd1cc27444),
  U128(0x9732300393f33614,0x31f17078d34c156c),
  U128(0x90887712e9dc9663,0x3e33f2f642be355e),
  U128(0xd725d3b9ed35fbaa,0x4a5018bb567c16a2),
  U128(0x408fca9cc277fc1f,0x563e69d6ac7f73f8),
  U128(0x98916152cf7eee1c,0x61f78a9abaa58b46),
  U128(0x9b165cba0c171818,0x6d744027857300ad),
  U128(0x362474f1a105878f,0x78ad74e01bd8ec78),
  U128(0xbfd79717f2880abf,0x839c3cc917ff6cb4),
  U128(0xbba4cfecbff54867,0x8e39d9cd73464364),
  U128(0x19cec845ac87a5c6,0x987fbfe70b81a708),
  U128(0x3b5167ee359a234e,0xa267992848eeb0c0),
  U128(0x1becda8089c1a94c,0xabeb49a46764fd15),
  U128(0x597d89b3754abe9f,0xb504f333f9de6484),
  U128(0xac85320f528d6d5d,0xbdaef913557d76f0),
  U128(0x43da25d99267326b,0xc5e40358a8ba05a7),
  U128(0x23af31db7179a4aa,0xcd9f023f9c3a059e),
  U128(0xf630e8b6dac83e69,0xd4db3148750d1819),
  U128(0x2c19b63253da43fc,0xdb941a28cb71ec87),
  U128(0xf4e8a8372f8c5810,0xe1c5978c05ed8691),
  U128(0x125129529d48a92f,0xe76bd7a1e63b9786),
  U128(0x7e610231ac1d6181,0xec835e79946a3145),
  U128(0x67127db35b287316,0xf1090827b43725fd),
  U128(0x163c5c7f03b718c5,0xf4fa0ab6316ed2ec),
  U128(0xc7adc6b4988891bb,0xf853f7dc9186b952),
  U128(0x2172a361fd2a722f,0xfb14be7fbae58156),
  U128(0xeae6bd951c1dabbe,0xfd3aabf84528b50b),
  U128(0x41390efdc726e9ef,0xfec46d1e89292cf0),
  U128(0x421e8edaaf59453e,0xffb10f1bcb6bef1d),
  U128(0xffffffffffffffff,0xffffffffffffffff),
};

/* For 0 <= i < 64, S2u[i] contains a 128-bit unsigned integer m
   such that m/2^128 approximates sin(pi*i/2^12) to nearest.
   Generated by computeS2u() from sin.sage. */
static const u128 S2u[64] = {
  U128(0x0,0x0),
  U128(0xc3b0abf2b35a62f8,0x3243f655d966c0),
  U128(0x11ad1d7b5620834a,0x6487eabb991cb6),
  U128(0xc4a32c4560d01e70,0x96cbdb41258434),
  U128(0x57480f7956b64707,0xc90fc5f66525d2),
  U128(0x328421cf8014d698,0xfb53a8eb3ec385),
  U128(0xf96857b5aa8f6208,0x12d97822f996bc4),
  U128(0xd230a30592f25049,0x15fdb4fd35c8caa),
  U128(0xab967fe6b7a9b037,0x1921f0fe6700711),
  U128(0x7db6e69ab68f0d10,0x1c462c078bc41b6),
  U128(0x85cede1f0314607b,0x1f6a65f9a2a3c58),
  U128(0x7c11ebc1b33cafb5,0x228e9eb5aa3a2d9),
  U128(0xc2dd9c015a46e767,0x25b2d61ca12e05d),
  U128(0x8e8d6151676ae94d,0x28d70c0f863326c),
  U128(0x53205a54588f0ab,0x2bfb406f580bc10),
  U128(0x556febf4862da970,0x2f1f731d15898f5),
  U128(0xc3c75f41b6ce7aa8,0x3243a3f9bd8f08c),
  U128(0xad8a2d0c1a9e3a8c,0x3567d2e64f10929),
  U128(0x80c1c97a65cf2291,0x388bffc3c915b22),
  U128(0xa8493bf9c07f7765,0x3bb02a732aba3f0),
  U128(0x6b5e1380c7e6bddb,0x3ed452d5732f950),
  U128(0xbfeba221fb9d05be,0x41f878cba1bdc60),
  U128(0xed3c01c030008bb,0x451c9c36b5c4cc3),
  U128(0xe97857207bc589da,0x4840bcf7aebdbba),
  U128(0xafc8f71b8eb233ee,0x4b64daef8c3bf4d),
  U128(0x2617b65f5a2a8f5a,0x4e88f5ff4dee562),
  U128(0xf9f89fb65be2a455,0x51ad0e07f3a06df),
  U128(0x356ef187634a531d,0x54d122ea7d3bacf),
  U128(0x9fab71e43d71e2b7,0x57f53487eac8977),
  U128(0xa9f1c1238fd05d5,0x5b1942c13c6ff80),
  U128(0x8ca56cd8d54b97f0,0x5e3d4d77727c10d),
  U128(0xa58993a76b3f4ffe,0x6161548b8d59ce2),
  U128(0x4e29cf6e5fed0679,0x648557de8d99f7e),
  U128(0xf1fc3edb7a0f1757,0x67a9575173f263a),
  U128(0x51b86c83422cbab4,0x6acd52c5413f26d),
  U128(0x4e68e062eb177e2d,0x6df14a1af683c83),
  U128(0x9c28010f1c93a252,0x71153d3394ec722),
  U128(0x5bcb8fd41cb1096f,0x74392bf01dcf247),
  U128(0x9ac20c033d7f5cc3,0x775d163192ace62),
  U128(0xb8654aa824578975,0x7a80fbd8f532f78),
  U128(0xb00590e675e4e556,0x7da4dcc7473c03f),
  U128(0x46f0804dae5f13ba,0x80c8b8dd8ad153d),
  U128(0x1db725856ff8c284,0x83ec8ffcc22bfe5),
  U128(0xa3f67ad05a5be69e,0x87106205efb61b6),
  U128(0xede5b1068d174bea,0x8a342eda160bf5a),
  U128(0x6aed92d34c17df54,0x8d57f65a37fd3c2),
  U128(0x7c8c5732d89e910b,0x907bb867588e342),
  U128(0xecc93966728e412d,0x939f74e27af8eb2),
  U128(0x437b2dd49d5fca3c,0x96c32baca2ae68b),
  U128(0xf9a60c93317892c4,0x99e6dca6d357dff),
  U128(0x8a318ba775fd7b04,0x9d0a87b210d7e1f),
  U128(0x5f3d645e787b94f2,0xa02e2caf5f4b8ef),
  U128(0x9b56007d16d4ad5a,0xa351cb7fc30bc88),
  U128(0xbdcd0d6bb4b9cd3e,0xa675640440ae634),
  U128(0x217954ed6093c44c,0xa998f61ddd0758a),
  U128(0x55213c653bfb79b7,0xacbc81ad9d29f88),
  U128(0x4cd34d2751c2e1da,0xafe00694866a1b4),
  U128(0x6b7029d39efd7682,0xb30384b39e5d534),
  U128(0x63a95642f565102f,0xb626fbebeadc1ec),
  U128(0xefb8391d83d6da18,0xb94a6c1e7203198),
  U128(0x5f10bfca3d646401,0xbc6dd52c3a342eb),
  U128(0xf9530f050886d756,0xbf9136f64a17ca4),
  U128(0x35bfac0f965612e2,0xc2b4915da89e0b2),
  U128(0xc6718c1dfd2aa612,0xc5d7e4435cfff45),
};

/* Table C1u is not needed, since cos(x) = sin(pi/2+x) for 0 <= x < pi/2,
   and cos(x) = -sin(x-pi/2) for pi/2 <= x < pi, thus
   C1u[i] = S1u[32+i] for 0 <= i < 32,
   and C1u[i] = -S1u[i-32] for 32 <= i < 64. */

/* For 0 <= i < 64, C2u[i] contains a 128-bit unsigned integer m
   such that m/2^128 approximates cos(pi*i/2^12) to nearest.
   Generated by computeC2u() from sin.sage. */
static const u128 C2u[64] = {
  U128(0xffffffffffffffff,0xffffffffffffffff),
  U128(0x491a703231e0a12e,0xfffffb10b0d19f76),
  U128(0xec9e2e75cb525f2c,0xffffec42c3773235),
  U128(0x6276b75b91de105e,0xffffd3963882d553),
  U128(0x3031437d7eccb9df,0xffffb10b10e80e95),
  U128(0x858425d8b397dee6,0xffff84a14dfbcc6a),
  U128(0x177338053fd93920,0xffff4e58f17465de),
  U128(0x3a11d60588d8b96e,0xffff0e31fd699a85),
  U128(0x38e310779edfec68,0xfffec42c7454926b),
  U128(0xedd8e1034213f22b,0xfffe7048590fddf8),
  U128(0x96f351efc65556cf,0xfffe1285aed775d8),
  U128(0xea80aedd6a19710f,0xfffdaae47948bad5),
  U128(0x69fff9ae0dedb047,0xfffd3964bc6275ba),
  U128(0xf3a703b987eca44a,0xfffcbe067c84d725),
  U128(0x928db07a0e70ba36,0xfffc38c9be717763),
  U128(0x8d800bed6653dcba,0xfffba9ae874b563a),
  U128(0xb47903f7a19f8ee2,0xfffb10b4dc96dabb),
  U128(0xecc7b9244a48eb19,0xfffa6ddcc439d30a),
  U128(0xfbe18032d0016082,0xfff9c126447b7424),
  U128(0x90e2d2eaf6da4d1e,0xfff90a91640459a1),
  U128(0x8cc193c5d508e13f,0xfff84a1e29de8571),
  U128(0x89332d07a713477e,0xfff77fcc9d755f99),
  U128(0x9e4938f661aa140c,0xfff6ab9cc695b5e8),
  U128(0x66c785e86dfbb75f,0xfff5cd8ead6dbbab),
  U128(0x43366df666fd54ff,0xfff4e5a25a8d095b),
  U128(0xdbb49f29fa872a83,0xfff3f3d7d6e49c49),
  U128(0xe08b96133ecce0bd,0xfff2f82f2bc6d648),
  U128(0x98a31bcda3def20,0xfff1f2a862e77d4e),
  U128(0x5428ed0647c9e5d1,0xfff0e343865bbb13),
  U128(0x807b6e7a4a723dae,0xffefca00a09a1cb3),
  U128(0xccf344c647917821,0xffeea6dfbc7a9242),
  U128(0xf0f7cb05bde024f1,0xffed79e0e5366e63),
  U128(0x5657552366961732,0xffec4304266865d9),
  U128(0x9195e99fbca2f10a,0xffeb02498c0c8f12),
  U128(0x191df31aaa6f7f45,0xffe9b7b1228061b6),
  U128(0x3b57790bf77b6b2d,0xffe8633af682b627),
  U128(0x53aa9423bb0adc21,0xffe704e71533c508),
  U128(0x3e71f7d99688082a,0xffe59cb58c1526b9),
  U128(0xbe28fbec7cfb8a6,0xffe42aa66909d2d2),
  U128(0xf1ed54343fe7be24,0xffe2aeb9ba561f99),
  U128(0x7d209f32d42d864e,0xffe128ef8e9fc17a),
  U128(0x8e6ee05573d420,0xffdf9947f4edca6f),
  U128(0x44bd28b8d85b530a,0xffddffc2fca8a970),
  U128(0x75a8951fc304b914,0xffdc5c60b59a29dc),
  U128(0x4fd8f038449ec436,0xffdaaf212fed72db),
  U128(0x8c9611f0b1d8f023,0xffd8f8047c2f06be),
  U128(0x8d3cd437dc7fa9d2,0xffd7370aab4cc25e),
  U128(0x45bd035edb0a65f5,0xffd56c33ce95dc73),
  U128(0x664649b4d541b9c5,0xffd3977ff7bae4e9),
  U128(0xc42aac754bedcfde,0xffd1b8ef38cdc433),
  U128(0x1fd552bf146b4a6,0xffcfd081a441ba99),
  U128(0x76f487bb853a6989,0xffcdde374ceb5f7d),
  U128(0x5595ca3f421ae09c,0xffcbe2104600a0a9),
  U128(0x11b369083a7a62e5,0xffc9dc0ca318c18b),
  U128(0x5c2a6019679e41f,0xffc7cc2c782c5a76),
  U128(0x579207cfe424dcb7,0xffc5b26fd99557dd),
  U128(0x1c676208aa3be545,0xffc38ed6dc0ef98b),
  U128(0xbc8d54e81d94f831,0xffc1616194b5d1d3),
  U128(0x965827f33d906c7c,0xffbf2a101907c4c5),
  U128(0xe0aa07fcb29eef39,0xffbce8e27ee40754),
  U128(0xccfed60a91097c48,0xffba9dd8dc8b1e83),
  U128(0xe907d9a298ab1feb,0xffb848f3489ede86),
  U128(0xbfdfce09aea7ac02,0xffb5ea31da2269e5),
  U128(0xbadfe70a1ef51116,0xffb38194a87a3097),
};

// argument reduction for accurate path, assumes 2^-16 <= x
// return k and r such that
// x/(2pi) mod 1 = k/2^13 + r/2^128 + eps with |r/2^128| <= 2^-14
// and 0 <= eps < 2^-128 + 2^-139 < 2^-127.999
// neg=0 if r >= 0, neg=1 if r < 0
static uint64_t
reduce_large_acc (u128 *r, int *neg, double x)
{
  b64u64_u t = {.f = x};
  int e = t.u >> 52; /* 1054 <= e <= 2046 */
  uint64_t m = (1ull << 52) | (t.u & 0xfffffffffffffull);
  // x = m * 2^(e-1075)
  /* _T[j] corresponds to _T[j]/2^(64*j) thus _T[j]*x corresponds to
     m*_T[j]*2^(e-1075-64*j). To get a non-zero fractional value,
     we need e-1075-64*j < 0, thus 64*j > e-1075 or 64*j >= e-1074. */
  int i = -1 + (e - 947) / 64; // i = ceil((e-1074)/64), -1 <= i <= 16
  int f = (e - 1011) & 0x3f;
  // shift the table entries
  uint64_t V0 = (i >= 0) ? _T[i] : 0, V1 = _T[i+1], V2 = _T[i+2];
  if (f != 0) {
    V0 = (V0 << f) | (V1 >> (64-f));
    V1 = (V1 << f) | (V2 >> (64-f));
    V2 = (V2 << f) | (_T[i+3] >> (64-f));
  }
  /* m*V0 contributes to 64 bits to the fractional part of x/(2pi),
     from weight 2^-1 to 2^-64,
     m*V1 contributes to 53+64 bits from weight 2^-12 to 2^-128,
     m*V2 contributes to 53+64 bits from weight 2^-76 to 2^-192,
     the ignored part m*(V3+V4+...) contributes to less than 2^-139 */
  u128 u = (u128) V1 | (((u128) V0) << 64);
  u = (u128) m * u;
  u128 v = (u128) m * (u128) V2;
  u += v >> 64; // add contribution of m*V2 from 2^-76 to 2^-128
  // the ignored part of v contributes to less than 2^-128
  uint64_t k = u >> (128-13);
  static const u128 mask = U128(0xffffffffffffffffull, 0x7ffffffffffffull);
  u &= mask; // ignore leading 13 bits
  // round k to nearest to have |r/2^128| < 2^-14
  *neg = u >> 114;
  if (*neg) { // add 1 to k and subtract 1/2^13 to r
    k = (k+1) & ((1ull<<13)-1);
    u = mask + 1 - u;
  }
  // now store u in r
  *r = u;
  return k;
}

// round (-1)^s*r/2^128 to double, assuming r is non-zero and not in the
// subnormal region
static inline double u128_tod (u128 r, int s)
{
  uint64_t sh = __builtin_clzll (r >> 64);
  /* since the smallest distance from a binary64 number to a multiple of pi/2
     is 2^-60.888 (see [1]), the smallest value of r/2^128 is about 2^-60.888
     too (taking into account approximation errors), thus sh <= 60.
     This proves that r>>64 cannot be 0, thus __builtin_clzll() is valid. */
  uint64_t h = r >> (75 - sh); // upper 53 non-zero bits
  int rbit = (r >> (74 - sh)) & 1; // round bit
  static const double Sgn[] = { 0x1p-53, -0x1p-53 };
  b64u64_u v = {.f = Sgn[s]};
  v.u -= sh << 52; // scale by 2^-sh
  static const double Low[] = { 0x1.8p-2, 0x1.8p-1 };
  double a = h * v.f, b = Low[rbit] * v.f;
  /* Assume sin(x) > 0, thus s=0. When rbit is 0, we have b < ulp(a)/2,
     and the result is rounded to a to nearest, which is what we want.
     When rbit is 1, we have b > ulp(a)/2, and the result is rounded to
     nextup(a), which is what we want too.
     In both case it is proven in sin.pdf that the approximation error
     cannot make the result cross a rounding boundary, except maybe for
     hard-to-round cases, which are checked by sin.wc. */
  return a + b;
}

static double
exceptions (double x, double ret)
{
  double ax = __builtin_fabs (x);
  if (ax == 0x1.8000000000009p-23) // cos(x) has 88 identical bits after round bit
    return 0x1.fffffffffff7p-1 + 0x1.b56666666666cp-143;
  if (ax == 0x1.8000000000024p-22) // cos(x) has 82 identical bits after round bit
    return 0x1.ffffffffffdcp-1 + 0x1.b56666666667ep-137;
  if (ax == 0x1.800000000009p-21) // cos(x) has 76 identical bits after round bit
    return 0x1.ffffffffff7p-1 + 0x1.b5666666666c4p-131;
  if (ax == 0x1.20000000000f3p-20) // cos(x) has 72 identical bits after round bit
    return 0x1.fffffffffebcp-1 + 0x1.37642666666fdp-127;
  return ret;
}

// accurate path for 2^-27 <= x
static double __attribute__((cold,noinline))
cos_large_accurate (double x)
{
  u128 r;
  int neg;
  uint64_t k = reduce_large_acc (&r, &neg, x);

  r = r << 3; // replace r by 8*r
  u128 u2 = mhUU (r,r), u4 = mhUU (u2,u2), u2h = u2 >> 64;

  /* x/(2*pi) mod 1 = k/2^13 + r + eps with |r| <= 2^-14 and 0 <= eps < 2^-127.999
     then cos(x) ~ cos(pi*k/2^12 + 2*pi*r)
                 ~ cos(pi*k/2^12)*cos(2*pi*r) - sin(pi*k/2^12)*sin(2*pi*r)
     Write k = 2^12*s + 2^6*i1 + i2 and t1=pi*i1/2^6, t2 = pi*i2/2^12
     then sin(pi*k/2^12) = (-1)^s*[sin(t1)*cos(t2)+cos(t1)*sin(t2)]
     and  cos(pi*k/2^12) = (-1)^s*[cos(t1)*cos(t2)-sin(t1)*sin(t2)]
  */
  int sbit = (k >> 12) & 1;
  int i1 = (k >> 6) & 0x3f, i2 = k & 0x3f;

  u128 s1u, c1u, t;
  // approximate |sin(z)| in s1u/2^128
  u128 s1 = (i1 <= 32) ? S1u[i1] : S1u[64-i1]; // use sin(pi-x) = sin(x)
  // since cos(x) = sin(x+pi/2), we have |C1u[i1]| = |S1u[(i1+32) mod 64]|
  u128 c1 = (i1 <= 32) ? S1u[32-i1] : S1u[i1-32];
  s1u = mhUU (s1, C2u[i2]);
  t = mhUU (c1, S2u[i2]);
  // if i1 >= 32, we have to subtract t
  /* s1u/2^128 approximates sin(t1)*cos(t2), and t/2^128 approximates (up to sign)
     sin(t2)*cos(t1), thus s1u +/- t approximates sin(z=t1+t2), but since
     sin(z) >= 0 (0 <= z < pi), we have s1u +/- t >= 0. */
  s1u = (i1 < 32) ? s1u + t : s1u - t;

  // approximate cos(z) in c1u/2^128
  c1u = mhUU (c1, C2u[i2]);
  t = mhUU (s1, S2u[i2]);
  c1u = (i1 < 32) ? c1u - t : c1u + t;

  u128 Sr = evalPS (r, u2, u2h, u4); // Sr/2^128 approximates |sin(2*pi*r)|
  u128 Cr = evalPC (u2, u2h, u4);    // Cr/2^128 approximates cos(2*pi*r)

  // now combine: cos(x) ~ c1*C - s1*S
  c1u = mhUU (c1u,Cr);
  s1u = mhUU (s1u,Sr);

  /* c1u/2^128 approximates cos(z)*cos(r) where cos(z) > 0 for i1 < 32,
     and cos(z) <= 0 for i1 >= 32;
     s1u/2^128 approximates sin(z)*sin(r), which has the sign of r. */
  if ((i1 >= 32) ^ neg) // cos(z) * r < 0
    c1u += s1u;
  else if (c1u <= s1u) {
    c1u = s1u - c1u;
    sbit ^= 1;
  } else
    c1u -= s1u;
  // if i1 >= 32, cos(z) <= 0 thus we should exchange signs
  double ret = u128_tod (c1u, sbit ^ (i1 >= 32)); 
  uint64_t d = (uint64_t) c1u + 5;
  if (d <= 3) return exceptions (x, ret);
  return ret;
}

// fast path for 2^-27 <= x < 2^31
// adapted from sin.c
// see proof of correctness in cos.pdf
static inline double
cr_cos_moderate (double x)
{
  int sbit;
  static const double invpi = 0x1.45f306dc9c883p+12;
  // |invpi/2^14 - 1/pi| < 2^-55.496
  double k = roundeven_finite (invpi * x);
  // |2^14*(pih + pil) + pi| < 2^-108.041
  double rh = __builtin_fma (k, pih, x), rl = k * pil; // rh is exact

  double r = rh + rl; // |r| < 2^-13.339 (see sin.pdf)
  double r2 = r * r;
  int64_t j = k;
  sbit = (j >> 14) & 1; // reduction by an odd multiple of pi?
  int i1 = (j >> 7) & 0x7f, i2 = j & 0x7f;
  double c1h, c1l, c2h, c2l;
  c1h = muldd (U1[i1][2], U1[i1][3], U2[i2][2], U2[i2][3], &c1l);
  // c1h approximates cos(t1)*cos(t2) where t1=i1*pi/2^7 and t2=i2*pi/2^14
  c2h = muldd (U2[i2][0], U2[i2][1], U1[i1][0], U1[i1][1], &c2l);
  // c2h approximates sin(t1)*sin(t2)
  double Ch, Cl;
  Ch = fastsum (c1h, c1l, -c2h, -c2l, &Cl);
  // Ch+Cl approximates cos(t1+t2) = cos(t1)*cos(t2)-sin(t1)*sin(t2)
  double Sh = U1[i1][0] * U2[i2][2] + U1[i1][2] * U2[i2][0];
  // Sh approximates sin(t1+t2) = sin(t1)*cos(t2)+cos(t1)*sin(t2)

  /* for |r| <= 2^-13.339, the polynomial r - 0x1.55555553068fp-3 * r^3
     approximates sin(r) with absolute error < 2^-76.494, and the polynomial
     -0.5 * r^2 + 0x1.55555553bfd3p-5 * r^4 approximates cos(r)-1 with
     absolute error < 2^-92.723 (cf sinmoderate.sollya) */
  double sh =  r * ( 1.0 - 0x1.55555553068fp-3 * r2);
  double ch = r2 * (-0.5 + 0x1.55555553bfd3p-5 * r2);
  double fh = Ch, fl = Cl + Ch*ch - Sh*sh;
  static const double Sgn[] = {1.0, -1.0};
  fh = Sgn[sbit] * fh;
  fl = Sgn[sbit] * fl;
  static const double eps = 0x1.dep-64;
  double lb = fh + (fl - eps), ub = fh + (fl + eps);
  if (__builtin_expect (ub == lb, 1)) return lb;
  return cos_large_accurate (x);
}

// argument reduction for |x| >= 2^31
// return k and r such that
// x/(2pi) mod 1 = k/2^15 + r + s with 0 <= r < 2^-15 and 0 <= s < 2^-67.988
static uint64_t
reduce_large (double *r, double x)
{
  b64u64_u t = {.f = x};
  int e = (t.u >> 52) & 0x7ff; /* 1054 <= e <= 2046 */
  uint64_t m = (1ull << 52) | (t.u & 0xfffffffffffffull);
  // x = m * 2^(e-1075)
  /* _T[j] corresponds to _T[j]/2^(64*j) thus _T[j]*x corresponds to
     m*_T[j]*2^(e-1075-64*j). To get a non-zero fractional value,
     we need e-1075-64*j < 0, thus 64*j > e-1075 or 64*j >= e-1074. */
  int i = (e - 1011) / 64; // i = ceil((e-1074)/64), 0 <= i <= 16
  int f = (e - 1011) & 0x3f;
  /* the number of fractional bits from m*_T[i] is 64-f, thus we have to
     shift _T[i] by f bits to get 64 fractional bits */
  uint64_t V0, V1;
  if (f == 0) {
    V0 = _T[i];
    V1 = _T[i+1];
  } else {
    V0 = (_T[i] << f) | (_T[i+1] >> (64-f));
    V1 = (_T[i+1] << f) | (_T[i+2] >> (64-f));
  }
  /* Remark: computing directly u with 128-bit arithmetic from _T[i],
     _T[i+1] and _T[i+2] is slower (surely because 128-bit arithmetic is
     emulated.) */
  u128 u = (u128) V1 | (((u128) V0) << 64);
  u = (u128) m * u;
  // round r to nearest, where 0x810000000000000 = 2^59 + 2^52
  static const u128 magic = ((u128) 1 << 112) + 0x810000000000000ull;
  u += magic;
  t.f = (uint64_t)((u << 15) >> 75); // next 53 bits of u after the first 15
  *r = t.f * 0x1p-68 - 0x1p-16;
  return u >> 113;
  // since we return 15 bits in i and 53 in h, the accuracy is at most 2^-68
}

// fast path for x >= 2^31
static double __attribute__((noinline))
cr_cos_large (double x)
{
  double r;
  uint64_t j = reduce_large (&r, x);
  // now x/(2pi) ~ k + j/2^15 + r with 0 <= r < 2^-15

  double r2 = r * r;
  int sbit = j >> 14; // reduction by an odd multiple of pi?
  int i1 = (j >> 7) & 0x7f, i2 = j & 0x7f;
  double c1h, c1l, c2h, c2l;
  c1h = muldd (U1[i1][2], U1[i1][3], U2[i2][2], U2[i2][3], &c1l);
  c2h = muldd (U2[i2][0], U2[i2][1], U1[i1][0], U1[i1][1], &c2l);
  double Ch, Cl;
  Ch = fastsum (c1h, c1l, -c2h, -c2l, &Cl);
  double Sh = U1[i1][0] * U2[i2][2] + U1[i1][2] * U2[i2][0];

  double sh = r * (0x1.921fb54442d18p2 - 0x1.4abbcdb6b26d1p5 * r2);
  double ch = r2 * (-0x1.3bd3cc9be45dep4 + 0x1.03c1eee483083p6 * r2);
  double fh = Ch, fl = Cl + Ch*ch - Sh*sh;
  static const double Sgn[] = {1.0, -1.0};
  fh = Sgn[sbit] * fh;
  fl = Sgn[sbit] * fl;
  // fails with eps=0x1.7fp-64 and x=0x1.64dd7934d0fdfp+953 (rndz, no FMA)
  static const double eps = 0x1.01p-63;
  double lb = fh + (fl - eps), ub = fh + (fl + eps);
  if (__builtin_expect (lb == ub, 1)) return lb;
  return cos_large_accurate (x);
}

double
cr_cos (double x)
{
  double z = __builtin_fabs (x);
  b64u64_u t = {.f = z};
  int e = t.u >> 52;

  // deal with tiny x to avoid underflow
  if (__builtin_expect (e < 0x3ff-27, 0)) // |x| < 2^-27
    // for |x| < 2^-27, cos(x) rounds to 1 to nearest
    return __builtin_fma (x, -x, 1.0);

  if (__builtin_expect (e < 0x3ff+31, 1))
    return cr_cos_moderate (z); // |x| < 2^31

  if (__builtin_expect (e == 0x7ff, 0)) /* NaN, +Inf and -Inf. */
    {
#ifdef CORE_MATH_SUPPORT_ERRNO
      if (t.u == 0x7ffull<<52) // +/-Inf
        errno = EDOM;
#endif
      if (t.u < 0x7ff8ull<<48) feraiseexcept (FE_INVALID); // Inf or sNaN
      t.f = x;
      t.u |= (u64)1<<51; // propagate qnan, inf -> qnan, snan -> qnan
      return t.f;
    }

  // now z >= 2^31
  return cr_cos_large (z);
}
