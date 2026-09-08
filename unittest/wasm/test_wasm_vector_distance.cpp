// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "simd/simd.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <source_location>
#include <thread>
#include <vector>

namespace {
void near(float actual, double expected, double scale = 1.0,
          std::source_location caller = std::source_location::current())
{
  assert(std::isfinite(actual));
  const double tolerance = 2e-5 * std::max({1.0, scale, std::abs(expected)});
  if (std::abs(static_cast<double>(actual) - expected) > tolerance) {
    std::fprintf(stderr, "line %u: actual %.9g expected %.17g tolerance %.9g\n",
                 caller.line(), actual, expected, tolerance);
    std::abort();
  }
}

void distances(unsigned seed)
{
  constexpr uint64_t dimensions[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32,
                                    33, 63, 64, 65, 127, 128, 129, 384, 768, 1536};
  for (uint64_t dim : dimensions) {
    // Float aligned, deliberately not necessarily SIMD aligned. Each candidate
    // has distinct contents; batch lane swaps must not pass unnoticed.
    std::vector<float> query(dim + 2, -9999.0f);
    std::array<std::vector<float>, 4> codes;
    for (auto &code : codes) { code.assign(dim + 2, -9999.0f); }
    std::array<double, 4> ip{}, l2{}, ip_scale{};
    double norm2 = 0;
    for (uint64_t i = 0; i < dim; ++i) {
      query[i + 1] = (static_cast<int>((i * 17 + seed) % 101) - 50) / 16.0f;
      norm2 += static_cast<double>(query[i + 1]) * query[i + 1];
      for (unsigned lane = 0; lane < 4; ++lane) {
        codes[lane][i + 1] =
            (static_cast<int>((i * 11 + lane * 23 + seed) % 83) - 41) / 8.0f;
        double q = query[i + 1], c = codes[lane][i + 1];
        ip[lane] += q * c;
        ip_scale[lane] += std::abs(q * c);
        l2[lane] += (q - c) * (q - c);
      }
    }
    const auto original_query = query;
    const auto original_codes = codes;
    const float *q = query.data() + 1;
    for (unsigned lane = 0; lane < 4; ++lane) {
      const float *c = codes[lane].data() + 1;
      near(vsag::L2Sqr(q, c, &dim), l2[lane]);
      near(vsag::InnerProduct(q, c, &dim), ip[lane], ip_scale[lane]);
      near(vsag::InnerProductDistance(q, c, &dim), 1.0 - ip[lane], ip_scale[lane]);
      near(vsag::FP32ComputeL2Sqr(q, c, dim), l2[lane]);
      near(vsag::FP32ComputeIP(q, c, dim), ip[lane], ip_scale[lane]);
    }
    // The generic batch helpers accumulate into caller-owned result slots.
    std::array<float, 4> batch{};
    vsag::FP32ComputeIPBatch4(q, dim, codes[0].data() + 1, codes[1].data() + 1,
                            codes[2].data() + 1, codes[3].data() + 1,
                            batch[0], batch[1], batch[2], batch[3]);
    for (unsigned lane = 0; lane < 4; ++lane) { near(batch[lane], ip[lane], ip_scale[lane]); }
    batch.fill(0);
    vsag::FP32ComputeL2SqrBatch4(q, dim, codes[0].data() + 1, codes[1].data() + 1,
                               codes[2].data() + 1, codes[3].data() + 1,
                               batch[0], batch[1], batch[2], batch[3]);
    for (unsigned lane = 0; lane < 4; ++lane) { near(batch[lane], l2[lane]); }
    assert(query == original_query && codes == original_codes);

    std::vector<float> normalized(dim + 2, -7777.0f);
    near(vsag::Normalize(q, normalized.data() + 1, dim), std::sqrt(norm2));
    for (uint64_t i = 0; i < dim; ++i) {
      near(normalized[i + 1], norm2 == 0 ? 0 : q[i] / std::sqrt(norm2));
    }
    assert(normalized.front() == -7777.0f && normalized.back() == -7777.0f);

    // Quantized inputs include both int8 extrema, and the signed distance API's
    // negative-dot convention differs from the float API's 1-dot convention.
    std::vector<int8_t> a(dim + 2, 42), b(dim + 2, 43);
    int64_t dot = 0, squared = 0;
    for (uint64_t i = 0; i < dim; ++i) {
      a[i + 1] = static_cast<int8_t>(static_cast<int>(i % 256) - 128);
      b[i + 1] = static_cast<int8_t>(127 - static_cast<int>((i * 7) % 256));
      int64_t x = a[i + 1], y = b[i + 1];
      dot += x * y;
      squared += (x - y) * (x - y);
    }
    near(vsag::INT8L2Sqr(a.data() + 1, b.data() + 1, &dim), squared);
    near(vsag::INT8InnerProduct(a.data() + 1, b.data() + 1, &dim), dot);
    near(vsag::INT8InnerProductDistance(a.data() + 1, b.data() + 1, &dim), -dot);
    near(vsag::INT8ComputeL2Sqr(a.data() + 1, b.data() + 1, dim), squared);
    near(vsag::INT8ComputeIP(a.data() + 1, b.data() + 1, dim), dot);
    assert(a.front() == 42 && a.back() == 42 && b.front() == 43 && b.back() == 43);
  }
}

void special_values()
{
  const uint64_t dim = 4;
  float zero[4] = {}, normalized[4] = {1, 2, 3, 4};
  assert(vsag::Normalize(zero, normalized, dim) == 0);
  for (float x : normalized) { assert(x == 0); }
  float q[4] = {1, -2, 3, -4};
  float c[4] = {std::numeric_limits<float>::infinity(), 0, 0, 0};
  assert(std::isinf(vsag::FP32ComputeL2Sqr(q, c, dim)));
  c[0] = std::numeric_limits<float>::quiet_NaN();
  assert(std::isnan(vsag::FP32ComputeIP(q, c, dim)));
  assert(std::isnan(vsag::FP32ComputeL2Sqr(q, c, dim)));
  // Exact binary fractions ensure this fixture has an unambiguous ordering.
  std::vector<std::pair<float, int>> ranked;
  for (int i = 15; i >= 0; --i) {
    float candidate[4] = {1.0f + i / 4.0f, -2, 3, -4};
    ranked.emplace_back(vsag::FP32ComputeL2Sqr(q, candidate, dim), i);
  }
  std::sort(ranked.begin(), ranked.end());
  for (int i = 0; i < 16; ++i) {
    assert(ranked[i].second == i && ranked[i].first == i * i / 16.0f);
  }
}
} // namespace

int main()
{
  assert(cpuinfo_initialize());
#ifdef __EMSCRIPTEN__
  const auto status = vsag::setup_simd();
  assert(!status.dist_support_sse && !status.dist_support_avx && !status.dist_support_avx2
         && !status.dist_support_neon && !status.dist_support_sve);
  assert(vsag::FP32ComputeIP == vsag::generic::FP32ComputeIP);
  assert(vsag::FP32ComputeL2Sqr == vsag::generic::FP32ComputeL2Sqr);
#endif
  distances(0);
  special_values();
  std::array<std::thread, 3> threads;
  for (unsigned i = 0; i < threads.size(); ++i) {
    threads[i] = std::thread([i] { distances(i + 1); special_values(); });
  }
  for (auto &thread : threads) { thread.join(); }
  std::puts("VSAG distance, batch, normalization and concurrent checks passed");
}
