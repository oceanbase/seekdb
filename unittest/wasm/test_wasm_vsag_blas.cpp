// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "impl/blas/blas_function.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

using Blas = vsag::BlasFunction;
static constexpr float padding = -12345.0F;
static void near(double actual, double expected, double tolerance = 3e-4)
{
  assert(std::isfinite(actual));
  assert(std::abs(actual - expected) <= tolerance * (1 + std::abs(expected)));
}

struct Matrix {
  int order, rows, cols, ld;
  std::vector<float> data;
  Matrix(int layout, int m, int n)
      : order(layout), rows(m), cols(n), ld((layout == Blas::RowMajor ? n : m) + 2),
        data((layout == Blas::RowMajor ? m : n) * ld, padding) {}
  float &at(int r, int c) { return data[order == Blas::RowMajor ? r * ld + c : c * ld + r]; }
  float at(int r, int c) const { return data[order == Blas::RowMajor ? r * ld + c : c * ld + r]; }
  void check_padding() const {
    int count = order == Blas::RowMajor ? rows : cols;
    int used = order == Blas::RowMajor ? cols : rows;
    for (int i = 0; i < count; ++i) for (int j = used; j < ld; ++j) {
      assert(data[i * ld + j] == padding);
    }
  }
};

static void vectors()
{
  float x[]{1, 90, 2, 91, 3}, y[]{4, 80, 5, 81, 6};
  Blas::Saxpy(3, 2, x, -2, y, 2);
  near(y[0], 10); near(y[2], 9); near(y[4], 8);
  assert(y[1] == 80 && y[3] == 81);
  Blas::Sscal(3, -0.5F, y, 2);
  near(y[0], -5); near(y[2], -4.5); near(y[4], -4);
  Blas::Saxpy(0, 1, nullptr, 1, nullptr, 1);
  Blas::Sscal(0, 1, nullptr, 1);
}

static void products(int order)
{
  for (int ta : {Blas::NoTrans, Blas::Trans, Blas::ConjTrans}) {
    for (int tb : {Blas::NoTrans, Blas::Trans, Blas::ConjTrans}) {
      constexpr int m = 3, n = 4, k = 5;
      Matrix a(order, ta == Blas::NoTrans ? m : k, ta == Blas::NoTrans ? k : m);
      Matrix b(order, tb == Blas::NoTrans ? k : n, tb == Blas::NoTrans ? n : k);
      Matrix c(order, m, n);
      for (int i = 0; i < a.rows; ++i) for (int j = 0; j < a.cols; ++j) a.at(i,j) = 0.5F*i-j;
      for (int i = 0; i < b.rows; ++i) for (int j = 0; j < b.cols; ++j) b.at(i,j) = i+0.25F*j;
      for (int i = 0; i < m; ++i) for (int j = 0; j < n; ++j) c.at(i,j) = i-j;
      Blas::Sgemm(order, ta, tb, m, n, k, 1.25F, a.data.data(), a.ld,
                  b.data.data(), b.ld, -0.5F, c.data.data(), c.ld);
      for (int i = 0; i < m; ++i) for (int j = 0; j < n; ++j) {
        double sum = 0;
        for (int t = 0; t < k; ++t) sum += double(ta == Blas::NoTrans ? a.at(i,t) : a.at(t,i)) *
                                                    (tb == Blas::NoTrans ? b.at(t,j) : b.at(j,t));
        near(c.at(i,j), 1.25*sum - 0.5*(i-j));
      }
      a.check_padding(); b.check_padding(); c.check_padding();
    }
  }
  Matrix a(order, 3, 4);
  for (int i = 0; i < a.rows; ++i) for (int j = 0; j < a.cols; ++j) a.at(i,j) = i-j*0.5F;
  for (int trans : {Blas::NoTrans, Blas::Trans, Blas::ConjTrans}) {
    int count_x = trans == Blas::NoTrans ? a.cols : a.rows;
    int count_y = trans == Blas::NoTrans ? a.rows : a.cols;
    std::vector<float> x(2*count_x, 33), y(2*count_y, 44);
    for (int i = 0; i < count_x; ++i) x[2*(count_x-1-i)] = i+1;
    for (int i = 0; i < count_y; ++i) y[2*i] = std::numeric_limits<float>::quiet_NaN();
    Blas::Sgemv(order, trans, a.rows, a.cols, 2, a.data.data(), a.ld,
                x.data(), -2, 0, y.data(), 2);
    for (int i = 0; i < count_y; ++i) {
      double sum = 0;
      for (int j = 0; j < count_x; ++j) sum += (trans == Blas::NoTrans ? a.at(i,j) : a.at(j,i)) * double(j+1);
      near(y[2*i], 2*sum); assert(y[2*i+1] == 44);
    }
  }
  Matrix c(order, 3, 2);
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) c.at(i,j) = std::numeric_limits<float>::quiet_NaN();
  Blas::Sgemm(order, Blas::NoTrans, Blas::NoTrans, 3, 2, 4, 0,
              nullptr, order == Blas::RowMajor ? 4 : 3,
              nullptr, order == Blas::RowMajor ? 2 : 4, 0, c.data.data(), c.ld);
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) near(c.at(i,j), 0);
  c.check_padding();
}

static void decompositions(int order)
{
  Matrix a(order, 5, 3);
  for (int i = 0; i < a.rows; ++i) for (int j = 0; j < a.cols; ++j) {
    a.at(i,j) = (i == j ? 4.0F : 0.0F) + (i-j)*0.25F;
  }
  Matrix original = a;
  float tau[3];
  assert(Blas::Sgeqrf(order, a.rows, a.cols, a.data.data(), a.ld, tau) == 0);
  Matrix r = a;
  assert(Blas::Sorgqr(order, a.rows, a.cols, 3, a.data.data(), a.ld, tau) == 0);
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
    double sum = 0;
    for (int k = 0; k < 5; ++k) sum += double(a.at(k,i))*a.at(k,j);
    near(sum, i == j ? 1 : 0);
  }
  for (int i = 0; i < 5; ++i) for (int j = 0; j < 3; ++j) {
    double sum = 0;
    for (int k = 0; k <= j; ++k) sum += double(a.at(i,k))*r.at(k,j);
    near(sum, original.at(i,j));
  }
  a.check_padding(); r.check_padding();

  Matrix lu(order, 4, 3);
  const float values[]{0,2,1, 4,1,3, 2,5,7, 1,0,2};
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) lu.at(i,j) = values[i*3+j];
  Matrix pa = lu;
  int32_t pivots[3];
  assert(Blas::Sgetrf(order, 4, 3, lu.data.data(), lu.ld, pivots) == 0);
  for (int i = 0; i < 3; ++i) {
    assert(pivots[i] >= i+1 && pivots[i] <= 4);
    for (int j = 0; j < 3; ++j) std::swap(pa.at(i,j), pa.at(pivots[i]-1,j));
  }
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) {
    double sum = 0;
    for (int k = 0; k < 3; ++k) {
      double l = i == k ? 1 : (i > k ? lu.at(i,k) : 0);
      double u = k <= j ? lu.at(k,j) : 0;
      sum += l*u;
    }
    near(sum, pa.at(i,j));
  }
  lu.check_padding();
  for (char uplo : {Blas::Upper, Blas::Lower}) {
    Matrix eigen(order, 3, 3);
    const float symmetric[]{2,1,0, 1,2,0, 0,0,5};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
      eigen.at(i,j) = (uplo == Blas::Upper ? i <= j : i >= j) ? symmetric[3*i+j] : std::numeric_limits<float>::quiet_NaN();
    }
    float w[3];
    assert(Blas::Ssyev(order, Blas::JobV, uplo, 3, eigen.data.data(), eigen.ld, w) == 0);
    near(w[0],1); near(w[1],3); near(w[2],5);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
      double av = 0, vv = 0;
      for (int k = 0; k < 3; ++k) {
        av += double(symmetric[3*i+k])*eigen.at(k,j);
        vv += double(eigen.at(k,i))*eigen.at(k,j);
      }
      near(av, double(eigen.at(i,j))*w[j]); near(vv, i == j ? 1 : 0);
    }
    eigen.check_padding();
  }
}

static void errors()
{
  float a[9]{1,0,0, 0,0,0, 0,0,2}, tau[3];
  int32_t pivots[3];
  assert(Blas::Sgetrf(Blas::RowMajor, 3, 3, a, 3, pivots) == 2);
  assert(Blas::Sgeqrf(-1, 2, 2, a, 2, tau) == -1);
  assert(Blas::Sgeqrf(Blas::RowMajor, 2, 3, a, 2, tau) == -5);
  a[0] = std::numeric_limits<float>::quiet_NaN();
  assert(Blas::Sgeqrf(Blas::RowMajor, 2, 2, a, 2, tau) == -4);
  assert(Blas::Ssyev(Blas::ColMajor, Blas::JobN, Blas::Upper, 0, nullptr, 1, nullptr) == 0);
}

#ifdef __EMSCRIPTEN__
extern "C" void fail_blas_allocation_after(int);
static void memory_failures()
{
  const float original[]{4,1,2, 1,5,1, 0,1,3};
  float a[9], tau[3]{};
  int32_t pivots[3];
  std::copy(original, original+9, a);
  // Invalid strides/dimensions must be diagnosed before LAPACKE's NaN scan.
  assert(Blas::Sgeqrf(Blas::RowMajor, 3, INT32_MAX, a, 1, tau) == -5);
  assert(Blas::Sorgqr(Blas::RowMajor, 3, INT32_MAX, 3, a, 1, tau) == -3);
  assert(Blas::Sgetrf(Blas::ColMajor, INT32_MAX, 3, a, 1, pivots) == -5);
  assert(Blas::Ssyev(Blas::RowMajor, 'V', 'U', INT32_MAX, a, 1, tau) == -6);
  // These dimensions cannot describe an addressable float matrix on wasm32.
  // Rejection must precede scanning the deliberately small input buffer.
  assert(Blas::Sgeqrf(Blas::RowMajor, 3, INT32_MAX, a, INT32_MAX, tau) == -1010);
  assert(Blas::Sorgqr(Blas::ColMajor, INT32_MAX, 3, 3, a, INT32_MAX, tau) == -1010);
  assert(Blas::Sgetrf(Blas::RowMajor, 3, INT32_MAX, a, INT32_MAX, pivots) == -1010);
  assert(Blas::Ssyev(Blas::ColMajor, 'V', 'U', INT32_MAX, a, INT32_MAX, tau) == -1010);
  for (int operation = 0; operation < 4; ++operation) {
    for (int fail_after = 0; fail_after < (operation == 2 ? 1 : 2); ++fail_after) {
      fail_blas_allocation_after(fail_after);
      int result;
      switch (operation) {
        case 0: result = Blas::Sgeqrf(Blas::RowMajor, 3, 3, a, 3, tau); break;
        case 1: result = Blas::Sorgqr(Blas::RowMajor, 3, 3, 3, a, 3, tau); break;
        case 2: result = Blas::Sgetrf(Blas::RowMajor, 3, 3, a, 3, pivots); break;
        default: result = Blas::Ssyev(Blas::RowMajor, 'V', 'U', 3, a, 3, tau); break;
      }
      fail_blas_allocation_after(-1);
      assert(result == (operation == 2 || fail_after == 1 ? -1011 : -1010));
      assert(std::equal(a, a+9, original));
    }
  }
  assert(Blas::Sgeqrf(Blas::RowMajor, 3, 3, a, 3, tau) == 0);
}
#endif

int main()
{
  // First LAPACK use is concurrent, exercising cached-constant initialization.
  std::thread workers[3];
  for (auto &worker : workers) worker = std::thread([] {
    for (int repetition = 0; repetition < 4; ++repetition) {
      vectors();
      for (int layout : {Blas::RowMajor, Blas::ColMajor}) {
        products(layout); decompositions(layout);
      }
    }
  });
  for (auto &worker : workers) worker.join();
  errors();
#ifdef __EMSCRIPTEN__
  memory_failures();
#endif
  std::puts("VSAG BLAS/LAPACK: products, QR, LU, eigensystems and concurrent calls passed");
}
