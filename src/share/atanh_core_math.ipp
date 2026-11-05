/* Correctly-rounded inverse hyperbolic tangent function for binary64.

Copyright (c) 2023-2025 Alexei Sibidanov <sibid@uvic.ca>.

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
SOFTWARE. */

#include <stdint.h>
#ifdef CORE_MATH_SUPPORT_ERRNO
#include <errno.h>
#endif

// Warning: clang also defines __GNUC__
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

#pragma STDC FENV_ACCESS ON

typedef uint64_t u64;
typedef union {double f; u64 u;} b64u64_u;
typedef uint16_t ushort;

static inline double fasttwosum(double x, double y, double *e){
  double s = x + y, z = s - x;
  *e = y - z;
  return s;
}

static inline double fasttwosub(double x, double y, double *e){
  double s = x - y, z = x - s;
  *e = z - y;
  return s;
}

static inline double adddd(double xh, double xl, double ch, double cl, double *l) {
  double s = xh + ch, d = s - xh;
  *l = ((ch - d) + (xh + (d - s))) + (xl + cl);
  return s;
}

/* Note: in revision 085972b, we replaced the last two lines
   ch = ahhh + ahhl and *l = (ahhh - ch) + ahhl by fasttwosum (ahhh, ahhl, l).
   Indeed, these last two lines did emulate a FastTwoSum.
   However, they did emulate another variant of fasttwosum, with
   z = x - s and e = z + y.
   Note that these two variants differ when the fasttwosum condition
   |x| >= |y| is not satisfied.
   Take for example with precision 3 and rounding upwards, x=-7 and
   y=28. Then s = RU(x + y) = 24. With the first variant, z = RU(s-x) = 32
   and e = RU(y - z) = -4, thus s + e = 20. With the second variant,
   z = RU(x-s) = -28 and e = RU(z + y) = 0, thus s + e = 24. In this case,
   the first variant is closer to the sum x + y = 21.
   Still with precision 3 and rounding upwards, now take x=7 and
   y=-28. Then s = RU(x + y) = -20. With the first variant, z = RU(s-x) = -24
   and e = RU(y - z) = -4, thus s + e = -28. With the second variant,
   z = RU(x-s) = 28 and e = RU(z + y) = 0, thus s + e = -20. In this case,
   the second variant is closer to the sum x + y = -21.
*/
static inline double muldd_acc(double xh, double xl, double ch, double cl, double *l){
  double ahlh = ch*xl, alhh = cl*xh, ahhh = ch*xh, ahhl = __builtin_fma(ch, xh, -ahhh);
 ahhl += alhh + ahlh;
 return fasttwosum (ahhh, ahhl, l);
}

// same as muldd_acc, but without the fasttwosum() normalization
static inline double muldd(double xh, double xl, double ch, double cl, double *l){
  double ahlh = ch*xl, alhh = cl*xh, ahhh = ch*xh, ahhl = __builtin_fma(ch, xh, -ahhh);
 *l = ahhl + (alhh + ahlh);
 return ahhh;
}

static inline double mulddd(double xh, double xl, double ch, double *l){
  double hh = xh*ch;
  *l = __builtin_fma(ch, xh, -hh) + xl*ch;
  return hh;
}

static inline double polydd(double xh, double xl, int n, const double c[][2], double *l){
  int i = n-1;
  double ch, cl;
  ch = fasttwosum(c[i][0], *l, &cl);
  cl += c[i][1];
  while(--i>=0){
    ch = muldd_acc(xh, xl, ch, cl, &cl);
    double th, tl;
    th = fasttwosum(c[i][0], ch, &tl);
    ch = th;
    cl += tl + c[i][1];
  }
  *l = cl;
  return ch;
}

static double __attribute__((noinline)) as_atanh_refine(double, double, double, double);
static double __attribute__((noinline)) as_atanh_zero(double x){
  static const double ch[][2] = {
    {0x1.5555555555555p-2, 0x1.5555555555555p-56},  // degree 3
    {0x1.999999999999ap-3, -0x1.999999999611cp-57}, // degree 5
    {0x1.2492492492492p-3, 0x1.2492490f76b25p-57},  // degree 7
    {0x1.c71c71c71c71cp-4, 0x1.c71cd5c38a112p-58},  // degree 9
    {0x1.745d1745d1746p-4, -0x1.7556c4165f4cap-59}, // degree 11
    {0x1.3b13b13b13b14p-4, -0x1.b893c3b36052ep-59}, // degree 13
    {0x1.1111111111105p-4, 0x1.4e1afd723ed1fp-59},  // degree 15
    {0x1.e1e1e1e1e2678p-5, -0x1.f86ea96fb1435p-59}, // degree 17
    {0x1.af286bc9f90ccp-5, 0x1.1e51a6e54fde9p-60},  // degree 19
    {0x1.8618618c779b6p-5, -0x1.ab913de95c3bfp-61}, // degree 21
    {0x1.642c84aa383ebp-5, 0x1.632e747641b12p-59},  // degree 23
    {0x1.47ae2d205013cp-5, -0x1.0c9617e7bcff2p-60}, // degree 25
    {0x1.2f664d60473f9p-5, 0x1.3adb3e2b7f35ep-61}}; // degree 27

  static const double cl[] = {
    0x1.1a9a91fd692afp-5, // degree 29
    0x1.06dfbb35e7f44p-5, // degree 31
    0x1.037bed4d7588fp-5, // degree 33
    0x1.5aca6d6d720d6p-6, // degree 35
    0x1.99ea5700d53a5p-5}; // degree 37

  double x2 = x*x , x2l = __builtin_fma(x, x,-x2);
  double y2 = x2 * (cl[0] + x2 * (cl[1] + x2 * (cl[2] + x2 * (cl[3] + x2 * (cl[4])))));
  double y1 = polydd(x2, x2l, 13, ch, &y2);
  y1 = mulddd(y1, y2, x, &y2);
  y1 = muldd_acc(x2, x2l, y1, y2, &y2);
  double y0 = fasttwosum(x, y1, &y1);
  y1 = fasttwosum(y1, y2, &y2);
  /* We have 22 failures (only for RNDN, 11 up to sign) if we disable this
     check, all with 53 or 54 identical bits after the round bit.
     For example x=0x1.cc7092205cfafp-10 we have y0=0x1.cc70b12857108p-10
     and y1=-0x1p-63. */
  b64u64_u t = {.f = y1};
  if(__builtin_expect(!(t.u&(~(uint64_t)0>>12)), 0)){
    b64u64_u w = {.f = y2};
    if((w.u^t.u)>>63)
      t.u--;
    else
      t.u++;
    y1 = t.f;
  }
  return y0 + y1;
}

double cr_atanh(double x){
  double ax = __builtin_fabs(x);
  b64u64_u ix = {.f = ax};
  u64 aix = ix.u;
  if(__builtin_expect(aix>=0x3ff0000000000000ull,0)){ // |x| >= 1
    if(aix==0x3ff0000000000000ull){ // |x| = 1
#ifdef CORE_MATH_SUPPORT_ERRNO
      errno = ERANGE; // pole error
#endif
      return __builtin_copysign(1, x) / 0.0;
    }
    if(aix>0x7ff0000000000000ull) return x + x; // nan
#ifdef CORE_MATH_SUPPORT_ERRNO
    errno = EDOM;
#endif
    return __builtin_sqrt(-1.0);
  }

  if(__builtin_expect(aix<0x3fd0000000000000ull,0)){ // |x| < 0x1p-2
    // atanh(x) rounds to x to nearest for |x| < 0x1.d12ed0af1a27fp-27
    if(__builtin_expect(aix<0x3e4d12ed0af1a27full,0)) {
      // |x| < 0x1.d12ed0af1a27fp-27
      /* We have underflow exactly when 0 < |x| < 2^-1022:
         for RNDU, atanh(2^-1022-2^-1074) would round to 2^-1022-2^-1075
         with unbounded exponent range */
#ifdef CORE_MATH_SUPPORT_ERRNO
      if (x != 0 && __builtin_fabs (x) < 0x1p-1022)
        errno = ERANGE; // underflow
#endif
      return __builtin_fma(x,0x1p-55,x);
    }
    /* checked exhautively this branch (with and without FMA):
     * for 0x1.d12ed0af1a27fp-27 <= x < 2^-24
     */
    double x2 = x * x;
    /* the polynomial c(x) = c[0]+c[1]*x^2+...+c[8]*x^16 is a minimax
       approximation of (atanh(x) - x - x^3/3)/x^5, which yields for
       p(x) = x + (0x1.5555555555555p-2 + 0x1.5555555555555p-56) x^3 + x^5 c(x)
       an approximation of atanh(x) with relative error <= 2^-64.377
       on [0x1.d12ed0af1a27fp-27,1/4], cf atanh.sollya */
    static const double c[] = 
      {0x1.999999999999ap-3, 0x1.2492492492244p-3, 0x1.c71c71c79715fp-4, 0x1.745d16f777723p-4,
       0x1.3b13ca4174634p-4, 0x1.110c9724989bdp-4, 0x1.e2d17608a5b2ep-5, 0x1.a0b56308cba0bp-5, 0x1.fb6341208ad2ep-5};
    double dx2 = __builtin_fma(x,x,-x2);
    double x4 = x2*x2, x3 = x2*x, x8 = x4*x4;
    double dx3 = __builtin_fma(x2,x,-x3) + dx2*x;
    /* following the C standard, the following is equivalent to (a + b) + c,
       where a = c[0] + x2*c[1], b = x4*(...) and c = x8*(...) */
    double p = (c[0] + x2*c[1]) + x4*(c[2] + x2*c[3]) + x8*((c[4] + x2*c[5]) + x4*(c[6] + x2*c[7]) + x8*c[8]);
    double t = __builtin_fma (x2, p, 0x1.5555555555555p-56);
    double pl, ph = fasttwosum(0x1.5555555555555p-2, t, &pl);
    ph = muldd(ph, pl,  x3, dx3, &pl);
    double tl;
    ph = fasttwosum(x,ph,&tl);
    pl += tl;
    double eps = x*(x4*0x1.dp-53 + 0x1p-103);
    // revision 221543c fails with 0.737*eps and x=0x1.2f67d96be6eafp-9 (rndu)
    double lb = ph + (pl - eps), ub = ph + (pl + eps);
    if(__builtin_expect(lb == ub,1)) return lb;
    return as_atanh_zero(x);
  }

  /* We have performed an exhaustive search of branch |x| >= 0.25 on 221543c
     both with default CFLAGS (with FMA) and with CFLAGS="-O3 -march=x86-64"
     (no FMA). We found no failure, except the ones in atanh.wc which are fixed
     in a later commit. */
  double pl, ph = fasttwosum(1,ax,&pl), ql, qh = fasttwosub(1,ax,&ql), iqh = 1/qh, th = ph*iqh,
     tl = __builtin_fma(ph,iqh,-th) + (pl + ph*(__builtin_fma(-qh,iqh,1) - ql*iqh))*iqh;
  
  static const struct {ushort c0; short c1;} B[] = {
    {301, 27565}, {7189, 24786}, {13383, 22167}, {18923, 19696}, {23845, 17361}, {28184, 15150},
    {31969, 13054}, {35231, 11064}, {37996, 9173}, {40288, 7372}, {42129, 5657}, {43542, 4020}, {44546,
    2457}, {45160, 962}, {45399, -468}, {45281, -1838}, {44821, -3151}, {44032, -4412}, {42929, -5622},
    {41522, -6786}, {39825, -7905}, {37848, -8982}, {35602, -10020}, {33097, -11020}, {30341, -11985},
    {27345, -12916}, {24115, -13816}, {20661, -14685}, {16989, -15526}, {13107, -16339}, {9022,
    -17126}, {4740, -17889}};
  static const double r1[] = 
    {0x1p+0, 0x1.f5076p-1, 0x1.ea4bp-1, 0x1.dfc98p-1, 0x1.d5818p-1,
    0x1.cb72p-1, 0x1.c199cp-1, 0x1.b7f76p-1, 0x1.ae8ap-1,
    0x1.a5504p-1, 0x1.9c492p-1, 0x1.93738p-1, 0x1.8ace6p-1,
    0x1.8258ap-1, 0x1.7a114p-1, 0x1.71f76p-1, 0x1.6a09ep-1,
    0x1.6247ep-1, 0x1.5ab08p-1, 0x1.5342cp-1, 0x1.4bfdap-1,
    0x1.44e08p-1, 0x1.3dea6p-1, 0x1.371a8p-1, 0x1.306fep-1,
    0x1.29e9ep-1, 0x1.2387ap-1, 0x1.1d488p-1, 0x1.172b8p-1,
    0x1.11302p-1, 0x1.0b558p-1, 0x1.059bp-1, 0x1p-1};
  static const double r2[] = 
    {0x1p+0, 0x1.ffa74p-1, 0x1.ff4eap-1, 0x1.fef62p-1, 0x1.fe9dap-1,
    0x1.fe452p-1, 0x1.fdeccp-1, 0x1.fd946p-1, 0x1.fd3c2p-1,
    0x1.fce3ep-1, 0x1.fc8bcp-1, 0x1.fc33ap-1, 0x1.fbdbap-1,
    0x1.fb83ap-1, 0x1.fb2bcp-1, 0x1.fad3ep-1, 0x1.fa7c2p-1,
    0x1.fa246p-1, 0x1.f9ccap-1, 0x1.f975p-1, 0x1.f91d8p-1,
    0x1.f8c6p-1, 0x1.f86e8p-1, 0x1.f8172p-1, 0x1.f7bfep-1,
    0x1.f768ap-1, 0x1.f7116p-1, 0x1.f6ba4p-1, 0x1.f6632p-1,
    0x1.f60c2p-1, 0x1.f5b52p-1, 0x1.f55e4p-1, 0x1.f5076p-1};
  static const double l1[][2] = {
    {0x0p+0, 0x0p+0}, {-0x1.532c1269e2038p-27, 0x1.62e5p-7},
    {0x1.ce42d81b54e84p-27, 0x1.62e3cp-6}, {-0x1.25826f815ec3dp-26, 0x1.0a2acp-5},
    {0x1.0db1b1e7cee11p-26, 0x1.62e4ap-5}, {-0x1.1f3a8c6c95003p-26, 0x1.bb9dcp-5},
    {-0x1.774cd4fb8c30dp-26, 0x1.0a2b2p-4}, {0x1.452e56c030a0ap-29, 0x1.3687fp-4},
    {0x1.6b63c4966a79ap-28, 0x1.62e41p-4}, {-0x1.b20a21ccb525ep-28, 0x1.8f40ap-4},
    {0x1.4006cfb3d8f85p-26, 0x1.bb9d1p-4}, {-0x1.cdb026b310c41p-26, 0x1.e7f9bp-4},
    {-0x1.69124fdc0f16dp-26, 0x1.0a2b08p-3}, {-0x1.084656cdc2727p-26, 0x1.205958p-3},
    {-0x1.376fa8b0357fdp-26, 0x1.3687cp-3}, {0x1.e56ae55a47b4ap-28, 0x1.4cb5e8p-3},
    {0x1.070ff8834eeb4p-26, 0x1.62e44p-3}, {0x1.623516109f4fep-26, 0x1.79129p-3},
    {-0x1.ec656b95fbdacp-29, 0x1.8f40bp-3}, {0x1.f0ca2e729f51p-28, 0x1.a56ed8p-3},
    {-0x1.7d260a858354ap-26, 0x1.bb9d68p-3}, {0x1.e7279075503d3p-27, 0x1.d1cb9p-3},
    {0x1.39e1a0a503873p-27, 0x1.e7f9dp-3}, {0x1.cd86d7b87c3d6p-26, 0x1.fe27d8p-3},
    {0x1.060ab88de341ep-26, 0x1.0a2b24p-2}, {0x1.20a860d3f939p-28, 0x1.154244p-2},
    {-0x1.dacee95fc2f1p-27, 0x1.205974p-2}, {0x1.45de3a86e0acap-26, 0x1.2b707p-2},
    {0x1.c164cbfb991afp-27, 0x1.3687bp-2}, {0x1.d3f66b24225efp-26, 0x1.419ec4p-2},
    {0x1.fc023efa144bap-26, 0x1.4cb5f8p-2}, {0x1.086a8af6f26cp-28, 0x1.57cd28p-2},
    {-0x1.05c610ca86c39p-30, 0x1.62e43p-2}};
  static const double l2[][2] = {
    {0x0p+0, 0x0p+0}, {-0x1.37e152a129e4ep-28, 0x1.632p-12},
    {-0x1.3f6c916b8be9cp-26, 0x1.63p-11}, {0x1.20505936739d5p-26, 0x1.0a24p-10},
    {-0x1.23e2e8cb541bap-26, 0x1.62dcp-10}, {-0x1.acb7983ac4f5ep-32, 0x1.bbap-10},
    {0x1.6f7c7689c63aep-28, 0x1.0a2ap-9}, {0x1.f5ca695b4c58bp-30, 0x1.368cp-9},
    {-0x1.c6c18bd953226p-27, 0x1.62e6p-9}, {0x1.7a516c34846bdp-26, 0x1.8f46p-9},
    {-0x1.f3b83dd8b853p-27, 0x1.bbap-9}, {-0x1.c3459046e4e57p-31, 0x1.e8p-9},
    {0x1.b5c7e34cb79f6p-38, 0x1.0a2cp-8}, {-0x1.2487e9af9a692p-27, 0x1.205cp-8},
    {0x1.f21bbc4ad79cep-26, 0x1.3687p-8}, {-0x1.550ffc857b731p-29, 0x1.4cb7p-8},
    {0x1.87458ec1b7b34p-27, 0x1.62e2p-8}, {0x1.103d4fe83ee81p-26, 0x1.7911p-8},
    {0x1.810483d3b398cp-27, 0x1.8f44p-8}, {-0x1.2085cb340608ep-27, 0x1.a573p-8},
    {0x1.12698a119c42fp-26, 0x1.bb9dp-8}, {-0x1.edb8c172b4c33p-26, 0x1.d1ccp-8},
    {-0x1.8b55b87a5e238p-26, 0x1.e7fep-8}, {0x1.be5e17763f78ap-26, 0x1.fe2bp-8},
    {-0x1.c2d496790073ep-30, 0x1.0a2a8p-7}, {0x1.6542f523abeecp-26, 0x1.1541p-7},
    {-0x1.b7fdbe5b193f8p-26, 0x1.205ap-7}, {0x1.fa4d42fe30c7cp-26, 0x1.2b7p-7},
    {0x1.0d46ad04adc86p-26, 0x1.36888p-7}, {-0x1.1c22d02d17c4cp-26, 0x1.419fp-7},
    {0x1.a7d1e330dcccep-30, 0x1.4cb7p-7}, {0x1.187025e656ba3p-31, 0x1.57cdp-7},
    {-0x1.532c1269e2038p-27, 0x1.62e5p-7}};
  static const double c[] = {-0x1p+0, 0x1.555555555553p+0, -0x1.fffffffffffap+0, 0x1.99999e33a6366p+1, -0x1.555559ef9525fp+2};

  b64u64_u t = {.f = th};
  int ex = t.u>>52, e = ex - 0x3ff;
  t.u &= ~(uint64_t)0>>12;
  double ed = e;
  u64 i = t.u>>(52-5);
  int64_t d = t.u & (~(uint64_t)0>>17);
  u64 j = (t.u + ((u64)B[i].c0<<33) + ((int64_t)B[i].c1*(d>>16)))>>(52-10);
  t.u |= (u64)0x3ff<<52;
  int i1 = j>>5, i2 = j&0x1f;
  double r = (0.5*r1[i1])*r2[i2], dx = __builtin_fma(r, t.f, -0.5), dx2 = dx*dx, rx = r*t.f, dxl = __builtin_fma(r,t.f,-rx);
  double f = dx2*((c[0] + dx*c[1]) + dx2*(c[2] + dx*c[3] + dx2*c[4]));
  const double l2h = 0x1.62e42fefa3ap-2, l2l = -0x1.0ca86c3898dp-50;
  double lh = (l1[i1][1] + l2[i2][1]) + l2h*ed, ll;
  lh = fasttwosum(lh,rx-0.5,&ll);
  ll += l2l*ed + (l1[i1][0] + l2[i2][0]) + dxl + 0.5*tl/th;
  ll += f;
  lh *= __builtin_copysign(1,x);
  ll *= __builtin_copysign(1,x);
  double eps = 38e-24 + dx2*0x1p-49;
  // revision bbe3f8c fails with 0.989 * eps and x=0x1.ebf0bfefa727dp-1 (rndz)
  double lb = lh + (ll - eps), ub = lh + (ll + eps);
  if(__builtin_expect(lb==ub, 1)) return lb;
  th = fasttwosum(th, tl, &tl);
  return as_atanh_refine(x, th, tl, 0x1.71547652b82fep+1*__builtin_fabs(lh + ll));
}

static __attribute__((noinline)) double as_atanh_database(double x, double f){
  static const double db[][3] = {
    {0x1.2dbb7b1c91363p-2, 0x1.36f33d51c264dp-2, 0x1p-56},
    {0x1.c493dc899e4a5p-2, 0x1.e611aa58ab608p-2,-0x1p-56},
  };
  double ax = __builtin_fabs(x), sgn = __builtin_copysign(1,x);
  int a = 0, b = sizeof(db)/sizeof(db[0]) - 1, m = (a + b)/2;
  while (a <= b) { // binary search
    if (db[m][0] < ax)
      a = m + 1;
    else if (db[m][0] == ax) {
      f = sgn*db[m][1] + sgn*db[m][2];
      break;
    } else
      b = m - 1;
    m = (a + b)/2;
  }
  return f;
}

static double as_atanh_refine(double x, double zh, double zl, double a){
  static const double t1[] = {
    0x1p+0, 0x1.ea4afap-1, 0x1.d5818ep-1, 0x1.c199bep-1, 0x1.ae89f98p-1, 0x1.9c4918p-1,
    0x1.8ace54p-1, 0x1.7a1147p-1, 0x1.6a09e68p-1, 0x1.5ab07ep-1, 0x1.4bfdad8p-1,
    0x1.3dea65p-1, 0x1.306fe08p-1, 0x1.2387a7p-1, 0x1.172b84p-1, 0x1.0b5587p-1, 0x1p-1};
  static const double t2[] = {
    0x1p+0, 0x1.fe9d968p-1, 0x1.fd3c228p-1, 0x1.fbdba38p-1, 0x1.fa7c18p-1, 0x1.f91d8p-1,
    0x1.f7bfdbp-1, 0x1.f663278p-1, 0x1.f507658p-1, 0x1.f3ac948p-1, 0x1.f252b38p-1,
    0x1.f0f9c2p-1, 0x1.efa1bfp-1, 0x1.ee4aaap-1, 0x1.ecf483p-1, 0x1.eb9f488p-1};
  static const double t3[] = {
    0x1p+0, 0x1.ffe9d2p-1, 0x1.ffd3a58p-1, 0x1.ffbd798p-1, 0x1.ffa74e8p-1, 0x1.ff91248p-1,
    0x1.ff7afb8p-1, 0x1.ff64d38p-1, 0x1.ff4eac8p-1, 0x1.ff38868p-1, 0x1.ff22618p-1,
    0x1.ff0c3dp-1, 0x1.fef61ap-1, 0x1.fedff78p-1, 0x1.fec9d68p-1, 0x1.feb3b6p-1};
  static const double t4[] = {
    0x1p+0, 0x1.fffe9dp-1, 0x1.fffd3ap-1, 0x1.fffbd78p-1, 0x1.fffa748p-1, 0x1.fff9118p-1,
    0x1.fff7ae8p-1, 0x1.fff64cp-1, 0x1.fff4e9p-1, 0x1.fff386p-1, 0x1.fff2238p-1,
    0x1.fff0c08p-1, 0x1.ffef5d8p-1, 0x1.ffedfa8p-1, 0x1.ffec98p-1, 0x1.ffeb35p-1};
  static const double LL[4][17][3] = {
    {{0x0p+0, 0x0p+0, 0x0p+0},
     {0x1.62e432b24p-6, -0x1.745af34bb54b8p-42, -0x1.17e3ec05cde7p-97},
     {0x1.62e42e4a8p-5, 0x1.111a4eadf312p-44, 0x1.cff3027abb119p-93},
     {0x1.0a2b233f1p-4, -0x1.88ac4ec78af8p-42, 0x1.4fa087ca75dfdp-93},
     {0x1.62e43056cp-4, 0x1.6bd65e8b0b7p-46, -0x1.b18e160362c24p-95},
     {0x1.bb9d3cbd6p-4, 0x1.de14aa55ec2bp-42, -0x1.c6ac3f1862a6bp-94},
     {0x1.0a2b244dap-3, 0x1.94def487fea7p-42, -0x1.dead1a4581acfp-94},
     {0x1.3687aa9b78p-3, 0x1.9cec9a50db22p-43, 0x1.34a70684f8e0ep-93},
     {0x1.62e42fabap-3, -0x1.d69047a3aebp-44, -0x1.4e061f79144e2p-95},
     {0x1.8f40b56d28p-3, 0x1.de7d755fd2e2p-42, 0x1.bdc7ecf001489p-94},
     {0x1.bb9d3b61fp-3, 0x1.c14f1445b12p-46, 0x1.a1d78cbdc5b58p-93},
     {0x1.e7f9c11f08p-3, -0x1.6e3e0000dae7p-43, 0x1.6a4559fadde98p-94},
     {0x1.0a2b242ec4p-2, 0x1.bb7cf852a5fe8p-42, 0x1.a6aef11ee43bdp-93},
     {0x1.205966c764p-2, 0x1.ad3a5f214294p-45, 0x1.5cc344fa10652p-93},
     {0x1.3687a98aacp-2, 0x1.1623671842fp-45, -0x1.0b428fe1f9e43p-94},
     {0x1.4cb5ec93f4p-2, 0x1.3d50980ea513p-42, 0x1.67f0ea083b1c4p-93},
     {0x1.62e42fefa4p-2, -0x1.8432a1b0e264p-44, 0x1.803f2f6af40f3p-93},
    },
    {{0x0p+0, 0x0p+0, 0x0p+0},
     {0x1.62e462b4p-10, 0x1.061d003b97318p-42, 0x1.d7faee66a2e1ep-93},
     {0x1.62e44c92p-9, 0x1.95a7bff5e239p-42, -0x1.f7e788a87135p-95},
     {0x1.0a2b1e33p-8, 0x1.2a3a1a65aa3ap-43, -0x1.54599c9605442p-93},
     {0x1.62e4367cp-8, -0x1.4a995b6d9ddcp-45, -0x1.56bb79b254f33p-100},
     {0x1.bb9d449ap-8, 0x1.8a119c42e9bcp-42, -0x1.8ecf7d8d661f1p-93},
     {0x1.0a2b1f19p-7, 0x1.8863771bd10a8p-42, 0x1.e9731de7f0155p-94},
     {0x1.3687ad11p-7, 0x1.e026a347ca1c8p-42, 0x1.fadc62522444dp-97},
     {0x1.62e436f28p-7, 0x1.25b84f71b70b8p-42, -0x1.fcb3f98612d27p-96},
     {0x1.8f40b7b38p-7, -0x1.62a0a4fd4758p-43, 0x1.3cb3c35d9f6a1p-93},
     {0x1.bb9d3abbp-7, -0x1.0ec48f94d786p-42, -0x1.6b47d410e4cc7p-93},
     {0x1.e7f9bb23p-7, 0x1.e4415cbc97ap-43, -0x1.3729fdb677231p-93},
     {0x1.0a2b22478p-6, -0x1.cb73f4505b03p-42, -0x1.1b3b3a3bc370ap-93},
     {0x1.2059691e8p-6, -0x1.abcc3412f264p-43, -0x1.fe6e998e48673p-95},
     {0x1.3687a768p-6, -0x1.43901e5c97a9p-42, 0x1.b54cdd52a5d88p-96},
     {0x1.4cb5eb5d8p-6, -0x1.8f106f00f13b8p-42, -0x1.8f793f5fce148p-93},
     {0x1.62e432b24p-6, -0x1.745af34bb54b8p-42, -0x1.17e3ec05cde7p-97},
    },
    {{0x0p+0, 0x0p+0, 0x0p+0},
     {0x1.62e7bp-14, -0x1.868625640a68p-44, -0x1.34bf0db910f65p-93},
     {0x1.62e35f6p-13, -0x1.2ee3d96b696ap-43, 0x1.a2948cd558655p-94},
     {0x1.0a2b4b2p-12, 0x1.53edbcf1165p-47, -0x1.cfc26ccf6d0e4p-97},
     {0x1.62e4be1p-12, 0x1.783e334614p-52, -0x1.04b96da30e63ap-93},
     {0x1.bb9e085p-12, -0x1.60785f20acb2p-43, -0x1.f33369bf7dff1p-96},
     {0x1.0a2b94dp-11, 0x1.fd4b3a273353p-42, -0x1.685a35575eff1p-96},
     {0x1.368810f8p-11, 0x1.7ded26dc813p-47, -0x1.4c4d1abca79bfp-96},
     {0x1.62e47878p-11, 0x1.7d2bee9a1f63p-42, 0x1.860233b7ad13p-93},
     {0x1.8f40cb48p-11, -0x1.af034eaf471cp-42, 0x1.ae748822d57b7p-94},
     {0x1.bb9d094p-11, -0x1.7a223013a20fp-42, -0x1.1e499087075b6p-93},
     {0x1.e7fa32c8p-11, -0x1.b2e67b1b59bdp-43, -0x1.54a41eda30fa6p-93},
     {0x1.0a2b237p-10, -0x1.7ad97ff4ac7ap-44, 0x1.f932da91371ddp-93},
     {0x1.2059a338p-10, -0x1.96422d90df4p-44, -0x1.90800fbbf2ed3p-94},
     {0x1.36879824p-10, 0x1.0f9054001812p-44, 0x1.9567e01e48f9ap-93},
     {0x1.4cb602cp-10, -0x1.0d709a5ec0b5p-43, 0x1.253dfd44635d2p-94},
     {0x1.62e462b4p-10, 0x1.061d003b97318p-42, 0x1.d7faee66a2e1ep-93},
    },
    {{0x0p+0, 0x0p+0, 0x0p+0},
     {0x1.63007cp-18, -0x1.db0e38e5aaaap-43, 0x1.259a7b94815b9p-93},
     {0x1.6300f6p-17, 0x1.2b1c75580438p-44, 0x1.78cabba01e3e4p-93},
     {0x1.0a2115p-16, -0x1.5ff223730759p-42, 0x1.8074feacfe49dp-95},
     {0x1.62e1ecp-16, -0x1.85d6f6487ce4p-45, 0x1.05485074b9276p-93},
     {0x1.bba301p-16, -0x1.af5d58a7c921p-43, -0x1.30a8c0fd2ff5fp-93},
     {0x1.0a32298p-15, 0x1.590faa0883bdp-43, 0x1.95e9bda999947p-93},
     {0x1.3682f1p-15, 0x1.f0224376efaf8p-42, -0x1.5843c0db50d1p-93},
     {0x1.62e3d8p-15, -0x1.142c13daed4ap-43, 0x1.c68a61183ce87p-93},
     {0x1.8f44dd8p-15, -0x1.aa489f399931p-43, 0x1.11c5c376854eap-94},
     {0x1.bb9601p-15, 0x1.9904d8b6a3638p-42, 0x1.8c89554493c8fp-93},
     {0x1.e7f744p-15, 0x1.5785ddbe7cba8p-42, 0x1.e7ff3cde7d70cp-94},
     {0x1.0a2c53p-14, -0x1.6d9e8780d0d5p-43, 0x1.ad9c178106693p-94},
     {0x1.205d134p-14, -0x1.214a2e893fccp-43, 0x1.548a9500c9822p-93},
     {0x1.3685e28p-14, 0x1.e23588646103p-43, 0x1.2a97b26da2d88p-94},
     {0x1.4cb6c18p-14, 0x1.2b7cfcea9e0d8p-42, -0x1.5095048a6b824p-93},
     {0x1.62e7bp-14, -0x1.868625640a68p-44, -0x1.34bf0db910f65p-93},
    },
  };
  static const double ch[][2] = {
    {0x1p-1, 0x1.24b67ee516e3bp-111}, {-0x1p-2, -0x1.932ce43199a8dp-110}, {0x1.5555555555555p-3, 0x1.55540c15cf91fp-57},
  };
  static const double cl[3] = {-0x1p-3, 0x1.9999999a0754fp-4,-0x1.55555555c3157p-4};
  b64u64_u t = {.f = zh};
  int ex = t.u>>52, e = ex-0x3ff;
  t.u &= ~(uint64_t)0>>12;
  t.u |= (u64)0x3ff<<52;
  double ed = e;
  b64u64_u v = {.f = a - ed + 0x1.00008p+0};
  u64 i = (v.u - ((u64)0x3ff<<52))>>(52-16);
  int i1 = i>>12, i2 = (i>>8)&0xf, i3 = (i>>4)&0xf, i4 = i&0xf;
  const double l20 = 0x1.62e42fefa3ap-2, l21 = -0x1.0ca86c3898dp-50, l22 = 0x1.f97b57a079ap-104;
  double el2 = l22*ed, el1 = l21*ed, el0 = l20*ed;
  double L[3];
  L[0] = LL[0][i1][0] + LL[1][i2][0] + (LL[2][i3][0] + LL[3][i4][0]) + el0;
  L[1] = LL[0][i1][1] + LL[1][i2][1] + (LL[2][i3][1] + LL[3][i4][1]);
  L[2] = LL[0][i1][2] + LL[1][i2][2] + (LL[2][i3][2] + LL[3][i4][2]);
  double t12 = t1[i1]*t2[i2], t34 = t3[i3]*t4[i4];
  double th = t12*t34, tl = __builtin_fma(t12,t34,-th);
  double dh = th*t.f, dl = __builtin_fma(th,t.f,-dh);
  double sh = tl*t.f, sl = __builtin_fma(tl,t.f,-sh);
  double xl, xh = fasttwosum(dh-1, dl, &xl);
  t.f = zl;
  t.u -= (int64_t)e<<52;
  xl += th*t.f;
  xh = adddd(xh, xl, sh, sl, &xl);
  sl = xh*(cl[0] + xh*(cl[1] + xh*cl[2]));
  sh = polydd(xh, xl, 3, ch, &sl);
  sh = muldd_acc(xh, xl, sh, sl, &sl);
  sh = adddd(sh, sl, el1, el2, &sl);
  sh = adddd(sh, sl, L[1], L[2], &sl);
  double v2, v0 = fasttwosum(L[0], sh, &v2), v1 = fasttwosum(v2, sl, &v2);
  t.f = v1;
  if(__builtin_expect(!(t.u&(~(uint64_t)0>>12)), 0)){ // is v1 a power of 2?
    /* we enter here only for 5 values up to sign:
     * 0x1.110e96a6c2d96p-2: 49 identical bits after round bit
     * 0x1.2dbb7b1c91363p-2: 50 identical bits after round bit
     * 0x1.c493dc899e4a5p-2: 50 identical bits after round bit (only with FMA)
     * 0x1.dc3fe1b524821p-2: 49 identical bits after round bit (only without FMA)
     * 0x1.64279e7c7064bp-1: 50 identical bits after round bit (only without FMA)
     In all cases v1 and v2 are of same sign, thus we add sign(v1)*ulp(v1) to v1.
     */
    t.u++;
    v1 = t.f;
  }
  b64u64_u t0 = {.f = v0};
  uint64_t er = ((t.u + 1) & (~(uint64_t)0>>12)), de = ((t0.u>>52)&0x7ff) - ((t.u>>52)&0x7ff);
  v0 *= __builtin_copysign(1,x);
  v1 *= __builtin_copysign(1,x);
  double res = v0 + v1;
  if(__builtin_expect(de>104 || er<3, 0))
    return as_atanh_database(x,res);
  return res;
}
