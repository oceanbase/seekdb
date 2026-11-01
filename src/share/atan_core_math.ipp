/* Correctly-rounded arctangent of binary64 value.

Copyright (c) 2023-2026 Alexei Sibidanov <sibid@uvic.ca>.

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

#include <stdint.h>
#ifdef CORE_MATH_SUPPORT_ERRNO
#include <errno.h>
#endif

// Warning: clang also defines __GNUC__
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

#pragma STDC FENV_ACCESS ON

typedef union {double f; uint64_t u;} b64u64_u;
typedef uint64_t u64;

static inline double fasttwosum(double x, double y, double *e){
  double s = x + y, z = s - x;
  *e = y - z;
  return s;
}

static inline double adddd(double xh, double xl, double ch, double cl, double *l) {
  double s = xh + ch, d = s - xh;
  *l = ((ch - d) + (xh + (d - s))) + (xl + cl);
  return s;
}

static inline double muldd_acc(double xh, double xl, double ch, double cl, double *l){
  double ahlh = ch*xl, alhh = cl*xh, ahhh = ch*xh, ahhl = __builtin_fma(ch, xh, -ahhh);
  ahhl += alhh + ahlh;
  return fasttwosum (ahhh, ahhl, l);
}

static double polydd(double xh, double xl, int n, const double c[][2], double *l){
  int i = n-1;
  double ch = c[i][0] + *l, cl = (c[i][0] - ch) + *l + c[i][1];
  while(--i>=0){
    ch = muldd_acc(xh,xl,ch,cl,&cl);
    double th = ch + c[i][0], tl = (c[i][0] - th) + ch;
    ch = th;
    cl += tl + c[i][1];
  }
  *l = cl;
  return ch;
}

// this table contains 129 entries
// A[j][0] approximates tan(pi/256*j)
// A[j][1] approximates atan(A[j][0]) - pi/256*j
static const double A[][2] = {
  {0x0p+0, 0x0p+0}, {0x1.9224e047e368ep-7, 0x1.a3ca6c727c59dp-62},
  {0x1.92346247a91fp-6, 0x1.138b0ef96a186p-64}, {0x1.2dbaae9a05dbp-5, 0x1.36e7f8a3f5e42p-59},
  {0x1.927278a3b1162p-5, -0x1.ac986efb92662p-64}, {0x1.f7495ea3f3783p-5, 0x1.06ec8011ee816p-59},
  {0x1.2e239ccff3831p-4, -0x1.858437d431332p-58}, {0x1.60b9f7597fdecp-4, -0x1.cebd13eb7c513p-60},
  {0x1.936bb8c5b2da2p-4, -0x1.840cac0d81db5p-58}, {0x1.c63ce377fc802p-4, 0x1.400b0fdaa109ep-58},
  {0x1.f93183a8db9e9p-4, 0x1.0e04e06c86e72p-59}, {0x1.1626d85a91e7p-3, 0x1.f7ad829163ca7p-59},
  {0x1.2fcac73a6064p-3, -0x1.2680735ce2cd8p-58}, {0x1.4986a74cf4e57p-3, -0x1.90559690b42e4p-57},
  {0x1.635c990ce0d36p-3, 0x1.91d29110b41aap-58}, {0x1.7d4ec54fb5968p-3, -0x1.ea90e2718278p-59},
  {0x1.975f5e0553158p-3, -0x1.dc82ac14e3e1cp-61}, {0x1.b1909efd8b762p-3, -0x1.73a10fd13daafp-58},
  {0x1.cbe4ceb4b4cf2p-3, -0x1.3a7ffbeabda0bp-57}, {0x1.e65e3f27c9f2ap-3, -0x1.db6627a24d523p-57},
  {0x1.007fa758626aep-2, -0x1.45f97dd3099f6p-57}, {0x1.0de53475f3b3cp-2, -0x1.6293f68741816p-57},
  {0x1.1b6103d3597e9p-2, -0x1.ab240d40633e9p-57}, {0x1.28f459ecad74dp-2, -0x1.de34d14e832ep-61},
  {0x1.36a08355c63dcp-2, 0x1.af540d9fb4926p-57}, {0x1.4466d542bac92p-2, 0x1.da60fdbc82ac4p-57},
  {0x1.5248ae1701b17p-2, -0x1.92a601170138ap-56}, {0x1.604775fbb27dfp-2, -0x1.7f1fca1d5d15bp-57},
  {0x1.6e649f7d78649p-2, -0x1.4e223ea716c7bp-57}, {0x1.7ca1a832d0f84p-2, 0x1.b24c824ac51fcp-56},
  {0x1.8b00196b3d022p-2, 0x1.4314cd132ba43p-57}, {0x1.998188e816bfp-2, -0x1.11f1e0817879ap-56},
  {0x1.a827999fcef32p-2, -0x1.c3dea4dbad538p-57}, {0x1.b6f3fc8c61e5bp-2, 0x1.60d1b780ee3ebp-57},
  {0x1.c5e87185e67b6p-2, -0x1.ab5edb7dfa545p-59}, {0x1.d506c82a2c8p-2, -0x1.8e1437048b5bdp-57},
  {0x1.e450e0d273e7ap-2, -0x1.06951c97b050fp-56}, {0x1.f3c8ad985d9eep-2, -0x1.14af9522ab518p-59},
  {0x1.01b819b5a7cf7p-1, -0x1.aba0d7d97d1f2p-56}, {0x1.09a4c59bd0d4dp-1, 0x1.095bc4ebc2c42p-59},
  {0x1.11ab7190834ecp-1, 0x1.798826fa27774p-55}, {0x1.19cd3fe8e405dp-1, 0x1.008f6258fc98fp-55},
  {0x1.220b5ef047825p-1, -0x1.462af7ceb7de6p-58}, {0x1.2a6709a74f289p-1, -0x1.1184dfd78b472p-56},
  {0x1.32e1889047ffdp-1, 0x1.9141876dc40c5p-56}, {0x1.3b7c3289ed6f3p-1, 0x1.481c20189726cp-55},
  {0x1.44386db9ce5dbp-1, 0x1.2e851bd025441p-55}, {0x1.4d17b087b265dp-1, 0x1.13ada9b8bc419p-56},
  {0x1.561b82ab7f99p-1, -0x1.05b4c3c4cbee8p-55}, {0x1.5f457e4f4812ep-1, -0x1.5619249bd96f1p-55},
  {0x1.6897514751db6p-1, -0x1.b0a0fbcafc671p-57}, {0x1.7212be621be6dp-1, -0x1.19ff2dc66da45p-55},
  {0x1.7bb99ed2990cfp-1, 0x1.1320449592d92p-55}, {0x1.858de3b716571p-1, -0x1.1fddcd2f3da8ep-55},
  {0x1.8f9197bf85eebp-1, 0x1.d44a42e35cc97p-57}, {0x1.99c6e0f634394p-1, -0x1.585a178b4a18dp-56},
  {0x1.a43002ae4285p-1, 0x1.f95a531b3a97p-57}, {0x1.aecf5f9ba35a6p-1, -0x1.96c2d43ca3392p-60},
  {0x1.b9a77c18c1af2p-1, -0x1.a5bed94b05defp-57}, {0x1.c4bb009e77983p-1, 0x1.54509d2bff511p-59},
  {0x1.d00cbc7384d2ep-1, -0x1.b4c867cef300cp-57}, {0x1.db9fa89953fcfp-1, -0x1.ddfac663d6bc6p-62},
  {0x1.e776eafc91706p-1, -0x1.a510683ff7cb6p-56}, {0x1.f395d9f0e3c92p-1, 0x1.4fdcd8e4e871p-59},
  {0x1p+0, 0x0p+0}, {0x1.065c900aaf2d8p+0, -0x1.deec7fc9042adp-55},
  {0x1.0ce29d0883c99p+0, -0x1.395ae45e0657dp-55}, {0x1.139447e6a86eep+0, 0x1.332cf301a97f3p-55},
  {0x1.1a73d55278c4bp+0, -0x1.6cc8c4b78213bp-55}, {0x1.2183b0c4573ffp+0, 0x1.70a90841da57ap-55},
  {0x1.28c66fdaf8f09p+0, -0x1.ba39bad450eep-57}, {0x1.303ed61109e2p+0, -0x1.8692946d9f93cp-55},
  {0x1.37efd8d87607ep+0, 0x1.3b711bf765b58p-57}, {0x1.3fdca42847507p+0, 0x1.c21387985b081p-56},
  {0x1.48089f8bf42ccp+0, -0x1.7ddb19d3d0efcp-55}, {0x1.507773c537eadp+0, -0x1.f5e354cf971f3p-56},
  {0x1.592d11142fa55p+0, -0x1.00f0ad675330dp-56}, {0x1.622db63c8ecc2p+0, -0x1.2c93f50ab2c0ep-55},
  {0x1.6b7df862652p+0, 0x1.bec391adc37d5p-56}, {0x1.7522cbdd428a8p+0, -0x1.9686ddc9ffcf5p-57},
  {0x1.7f218e25a7461p+0, -0x1.8d16529514246p-56}, {0x1.89801106cc709p+0, -0x1.092f51e9c2803p-55},
  {0x1.9444a7462122ap+0, -0x1.07c06755404c4p-55}, {0x1.9f7632fa9e871p+0, 0x1.02e0d43abc92bp-55},
  {0x1.ab1c35d8a74eap+0, 0x1.d0184e48af6f7p-58}, {0x1.b73ee3c3ef16ap+0, 0x1.73be957380bc2p-56},
  {0x1.c3e738086bc0fp+0, -0x1.02b6e26c84462p-56}, {0x1.d11f0dae40609p+0, 0x1.25c4f3ffa6e1fp-58},
  {0x1.def13b73c1406p+0, -0x1.e302db3c6823fp-58}, {0x1.ed69b4153a45dp+0, 0x1.3207830326c0ep-56},
  {0x1.fc95abad6cf4ap+0, -0x1.6308cee7927bfp-57}, {0x1.0641e192ceab3p+1, -0x1.0147ebf0df4c5p-56},
  {0x1.0ea21d716fbf7p+1, -0x1.168533cc41d8bp-56}, {0x1.17749711a6679p+1, -0x1.52a0b0333e9c5p-57},
  {0x1.20c36c6a7f38ep+1, 0x1.8659eece35395p-57}, {0x1.2a99f50fd4f4fp+1, 0x1.20fcad18cb36fp-55},
  {0x1.3504f333f9de6p+1, -0x1.52afdbd5a8c74p-56}, {0x1.4012ce2586a17p+1, -0x1.9747a792907d7p-56},
  {0x1.4bd3d87fe065p+1, 0x1.90c59393b52c8p-56}, {0x1.585aa4e1530fap+1, 0x1.af6934f13a3a8p-56},
  {0x1.65bc6cc825147p+1, -0x1.8534dcab5ad3ep-59}, {0x1.74118e4b6a7c8p+1, -0x1.555aa8bfca9a1p-56},
  {0x1.837626d70fdb8p+1, -0x1.56b3fee9ca72bp-58}, {0x1.940ad30abc792p+1, 0x1.4b3fdd4fdc06cp-58},
  {0x1.a5f59e90600ddp+1, 0x1.285d367c55ddcp-57}, {0x1.b9633283b6d14p+1, -0x1.8712976f17a16p-59},
  {0x1.ce885653127e7p+1, -0x1.abe8ab65d49fcp-60}, {0x1.e5a3de972a377p+1, 0x1.cd9be81ad764bp-58},
  {0x1.ff01305ecd8dcp+1, 0x1.742c2922656fap-59}, {0x1.0d7dc7cff4c9ep+2, -0x1.7c842978bee09p-56},
  {0x1.1d0143e71565fp+2, 0x1.7bc7dea7c3c03p-57}, {0x1.2e4ff1626b949p+2, 0x1.aefbe25b404e9p-59},
  {0x1.41bfee2424771p+2, -0x1.4bcfaaa95cb2cp-60}, {0x1.57be4eaa5e11bp+2, 0x1.0fe741e4ec679p-58},
  {0x1.70d751908c1b1p+2, 0x1.fe74a5b0ec709p-58}, {0x1.8dc25c117782bp+2, 0x1.0ca1c19f710efp-58},
  {0x1.af73f4ca3310fp+2, 0x1.2867b40ba77d6p-58}, {0x1.d7398d15e70dbp+2, 0x1.0fd4e0d4b1547p-57},
  {0x1.0372fb36b87e2p+3, 0x1.c16c9ecc1621dp-58}, {0x1.208dbdae055efp+3, 0x1.6b81a36e75e8cp-58},
  {0x1.44e6c595afdccp+3, -0x1.7c22045771848p-58}, {0x1.7398c57f3f1adp+3, 0x1.970503be105cp-58},
  {0x1.b1d03c03d2f7fp+3, -0x1.f299d010aead2p-60}, {0x1.046e9fe60a77ep+4, 0x1.d2b61deff33ecp-58},
  {0x1.45affed201b55p+4, 0x1.0e84d9567203ap-64}, {0x1.b267195b1ffaep+4, -0x1.ad44b44b92653p-64},
  {0x1.45e2455e4aaa7p+5, -0x1.296d577b5e21dp-60}, {0x1.45eed6854ce99p+6, 0x1.2db53886013cap-63},
  {0,0}
};

// c[] is an array with coefficients of a crude piecewise quadratic
// approximation of atan for a range reduction purpose (to determine index j)
static const uint16_t c[31][3] = {
  {419, 81, 0}, {500, 81, 0}, {582, 163, 0}, {745, 163, 0}, {908, 326, 0}, {1234, 326, 0},
  {1559, 651, 0}, {2210, 650, 1}, {2860, 1299, 3}, {4156, 1293, 4}, {5444, 2569, 24},
  {7989, 2520, 32}, {10476, 4917, 168}, {15224, 4576, 200}, {19601, 8341, 838},
  {27105, 6648, 731}, {33036, 10210, 1998}, {41266, 6292, 1117}, {46469, 7926, 2048},
  {52375, 4038, 849}, {55587, 4591, 1291}, {58906, 2172, 479}, {60612, 2390, 688},
  {62325, 1107, 247}, {63192, 1207, 349}, {64056, 556, 124}, {64491, 605, 175},
  {64923, 278, 62}, {65141, 303, 88}, {65358, 139, 31}, {65467, 151, 44}
};

// this routine might be called for 0x1p-27 <= |x|
// a is the approximation of atan(x) from the fast path
// thus 0x1p-27 <= |a| <= pi/2
static double __attribute__((cold,noinline)) as_atan_refine2(double x, double a){
  static const double ch[][2] = {
    {-0x1.5555555555555p-2, -0x1.5555555555555p-56}, {0x1.999999999999ap-3, -0x1.999999999bcb8p-57},
    {-0x1.2492492492492p-3, -0x1.249242093c016p-57}};
  static const double cl[] = {
    0x1.c71c71c71c71cp-4, -0x1.745d1745d1265p-4, 0x1.3b13b115bcbc4p-4, -0x1.1107c41ad3253p-4};
  b64u64_u phi = {.f = __builtin_fabs(a)*0x1.45f306dc9c883p6 + 256.5};
  // 256.5 < phi < 384.5
  int64_t i = (phi.u>>(52-8))&0xff; // 0 <= i <= 128
  double h,hl;
  if(i==128) {
    h = -1.0/x;
    hl = __builtin_fma(h,x,1)*h;
  } else {
    double ta = __builtin_copysign(A[i][0], x);
    double zta = x*ta, ztal = __builtin_fma(x, ta, -zta), zmta = x - ta;
    double v = 1 + zta, d = 1 - v, ev = (d + zta) - ((d + v) - 1) + ztal;
    double r = 1.0/v, rl = (__builtin_fma(r, -v, 1.0) - ev*r)*r;
    h = r*zmta;
    hl = __builtin_fma(r,zmta,-h) + rl*zmta;
  }
  double h2l, h2 = muldd_acc(h, hl, h, hl, &h2l), h4 = h2*h2;
  double h3l, h3 = muldd_acc(h, hl, h2, h2l, &h3l);
  double fl = h2*((cl[0] + h2*cl[1]) + h4*(cl[2] + h2*cl[3])), f = polydd(h2, h2l, 3, ch, &fl);
  f = muldd_acc(h3,h3l,f,fl,&fl);
  double ah, al, at;
  if(i==0){
    ah = h;
    al = f;
    at = fl;    
  } else { 
    double df = 0;
    if(i<128) df = __builtin_copysign(1.0,x)*A[i][1];
    double id = __builtin_copysign(i,x);
    ah = 0x1.921fb54442dp-7*id; al = 0x1.8469898cc518p-55*id, at = -0x1.fc8f8cbb5bf8p-104*id;
    al = adddd(al, at, df, 0, &at);
    al = adddd(al, at, h, hl, &at);
    al = adddd(al, at, f, fl, &at);
  }
  double v2, v0 = fasttwosum(ah, al, &v2), v1 = fasttwosum(v2, at, &v2);
  double ax = __builtin_fabs(x);
  b64u64_u t0 = {.f = v0}, t1 = {.f = v1};
  if(__builtin_expect(((t1.u+1)&(~(u64)0>>12))<=2 || ((t0.u>>52)&0x7ff)-((t1.u>>52)&0x7ff)>103, 0)){
    static const double db[][3] = {
      {0x1.0dc89a3b5501p-7, 0x1.0dc70ac228717p-7, 0x1p-61},
      {0x1.e3fb41d2d226p-8, 0x1.e3f9013a852f8p-8, 0x1p-62},
      {0x1.7ba49f739829fp-1, 0x1.46ac372243536p-1, 0x1p-109},
      {0x1.a933fe176b375p-3, 0x1.a33f32ac5ceb5p-3, -0x1p-112},
      {0x1.bb04a79820063p-8, 0x1.bb02ed5c5e956p-8, -0x1p-115},
      {0x1.cd30a9499618bp-8, 0x1.cd2eb65f92a46p-8, -0x1p-112},
      {0x1.f44aa37b8e66bp-7, 0x1.f440b04187c87p-7, -0x1p-112},
      {0x1.fd2ac95e57ef9p-8, 0x1.fd2829febc03ap-8, -0x1p-112},
      {0x1.6419079bbf601p-6, 0x1.640aade8f5427p-6, 0x1p-114},
      {0x1.7ba49f739829fp-1, 0x1.46ac372243536p-1, 0x1p-110},
      {0x1.d768804487b07p-3, 0x1.cf5676f373ec1p-3, -0x1p-110},
      {0x1.bb04a79820063p-8, 0x1.bb02ed5c5e956p-8, -0x1p-115}
    };
    for(unsigned j=0;j<sizeof(db)/sizeof(db[0]);j++)
      if(ax == db[j][0]) return __builtin_copysign(db[j][1],x) + __builtin_copysign(1.0,x)*db[j][2];
    if(!(t1.u&(~(u64)0>>12))){
      b64u64_u w = {.f = v2};
      if((w.u^t1.u)>>63)
	t1.u--;
      else
	t1.u++;
      v1 = t1.f;
    }
  }
  return v1 + v0;
}

double cr_atan(double x){
  static const double ch[] = {0x1p+0, -0x1.555555555552bp-2, 0x1.9999999069c2p-3, -0x1.248d2c8444ac6p-3};
  b64u64_u t = {.f = x};
  u64 at = t.u&(~(u64)0>>1); // at encodes |x|
  int64_t i = (at>>51) - 2030l; // -2030 <= i <= 2065
  if (__builtin_expect(at < 0x3f7b21c475e6362aull, 0)) {
    // |x| < 0x1.b21c475e6362ap-8
    if(__builtin_expect(at == 0, 0)) return x; // atan(+/-0) = +/-0
    static const double ch2[] = {
      -0x1.5555555555555p-2, 0x1.99999999998c1p-3, -0x1.249249176aecp-3, 0x1.c711fd121ae8p-4};
    if (at<(u64)0x3e40000000000000ull) { // |x| < 0x1p-27
      /* We have underflow when 0 < |x| < 2^-1022 or when |x| = 2^-1022
         and rounding towards zero. */
      double res = __builtin_fma (-0x1p-54, x, x);
#ifdef CORE_MATH_SUPPORT_ERRNO
      if (__builtin_fabs (x) < 0x1p-1022 || __builtin_fabs (res) < 0x1p-1022)
        errno = ERANGE; // underflow
#endif
      return res;
    }
    double x2 = x*x, x3 = x*x2, x4 = x2*x2;
    double f = x3*((ch2[0] + x2*ch2[1]) + x4*(ch2[2] + x2*ch2[3]));
    double epsp = f*0x1.6p-50, epsm = f*0x1.6p-51;
    double ub = (f + epsp) + x, lb = (f - epsm) + x;
    if(__builtin_expect(ub == lb, 1)) return ub;
    return as_atan_refine2(x, ub);
  }
  double h, ah, al;
  if(__builtin_expect(at>0x4062ded8e34a9035ull, 0)) {
    // |x| > 0x1.2ded8e34a9035p+7
    ah = __builtin_copysign(0x1.921fb54442d18p+0, x);
    al = __builtin_copysign(0x1.1a62633145c07p-54, x);
    if (__builtin_expect(at >= 0x434d02967c31cdb5ull, 0)) {
      // |x| >= 0x1.d02967c31cdb5p+53
      if (__builtin_expect(at > ((u64)0x7ff<<52), 0)) return x + x; // NaN
      return ah + al;
    }
    h = -1.0/x;
  } else {
    // now 0x1.b21c475e6362ap-8 <= |x| <= 0x1.2ded8e34a9035p+7 thus 1<=i<=30
    u64 u = t.u & (~(u64)0>>13);
    u64 ut = u>>(51-16), ut2 = ut*ut>>16;
    i = (((u64)c[i][0]<<16) + ut*c[i][1] - ut2*c[i][2])>>(16+9);
    double ta = __builtin_copysign(1.0, x)*A[i][0], id = __builtin_copysign(1.0, x)*(double)i;
    al = __builtin_copysign(1.0, x)*A[i][1] + 0x1.8469898cc517p-55*id;
    h = (x - ta)/(1 + x*ta);
    ah = 0x1.921fb54442dp-7*id;
  }
  double h2 = h*h, h4 = h2*h2;
  double f = (ch[0] + h2*ch[1]) + h4*(ch[2] + h2*ch[3]);
  al = __builtin_fma(h, f, al);
  double e = h*0x3.fp-52;
  double ub = (al + e) + ah, lb = (al - e) + ah;
  if(__builtin_expect(ub == lb, 1)) return ub;
  return as_atan_refine2(x, ub);
}
