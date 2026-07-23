/*
 * Copyright (c) 2025 OceanBase.
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

#include "sql/engine/expr/ob_deterministic_distribution.h"

#include <cmath>
#include <cstring>

namespace oceanbase
{
namespace sql
{

uint64_t ObDeterministicDistribution::uniform_below(std::mt19937_64 &gen, uint64_t range)
{
  // Lemire's unbiased nearly divisionless downscaling. This is the mapping
  // used by GCC 12.3 libstdc++ for a 64-bit URBG.
  unsigned __int128 product = static_cast<unsigned __int128>(gen()) * range;
  uint64_t low = static_cast<uint64_t>(product);
  if (low < range) {
    const uint64_t threshold = (uint64_t(0) - range) % range;
    while (low < threshold) {
      product = static_cast<unsigned __int128>(gen()) * range;
      low = static_cast<uint64_t>(product);
    }
  }
  return static_cast<uint64_t>(product >> 64);
}

int64_t ObDeterministicDistribution::uniform_int(
    std::mt19937_64 &gen, int64_t min_value, int64_t max_value)
{
  const uint64_t range = static_cast<uint64_t>(max_value)
                       - static_cast<uint64_t>(min_value) + 1;
  // A zero range represents all 2^64 possible int64_t bit patterns.
  const uint64_t offset = range == 0 ? gen() : uniform_below(gen, range);
  const uint64_t result_bits = static_cast<uint64_t>(min_value) + offset;
  int64_t result = 0;
  static_assert(sizeof(result) == sizeof(result_bits), "unexpected int64_t size");
  std::memcpy(&result, &result_bits, sizeof(result));
  return result;
}

double ObDeterministicDistribution::canonical_double(std::mt19937_64 &gen)
{
  // Reproduce GCC 12.3's pre-P0952 generate_canonical<double, 53> mapping for
  // mt19937_64. The cast intentionally happens before the scaling.
  double result = static_cast<double>(gen()) / 0x1p64;
  if (result >= 1.0) {
    // The uint64_t-to-double conversion can round values near UINT64_MAX up
    // to 2^64. Keep the distribution in [0, 1).
    result = 0x1.fffffffffffffp-1;
  }
  return result;
}

double ObDeterministicDistribution::uniform_real(
    std::mt19937_64 &gen, double min_value, double max_value)
{
  const double unit = canonical_double(gen);
  const double range = max_value - min_value;
  const double scaled = unit * range;
  return scaled + min_value;
}

double ObDeterministicDistribution::normal(
    std::mt19937_64 &gen, double mean, double stddev)
{
  // Marsaglia polar method, matching GCC 12.3 libstdc++. The current NORMAL
  // expression resets the distribution for every seed, so the paired x value
  // that libstdc++ would cache is deliberately discarded here.
  double x = 0.0;
  double y = 0.0;
  double radius_squared = 0.0;
  do {
    x = 2.0 * canonical_double(gen) - 1.0;
    y = 2.0 * canonical_double(gen) - 1.0;
    const double x_squared = x * x;
    const double y_squared = y * y;
    radius_squared = x_squared + y_squared;
  } while (radius_squared > 1.0 || radius_squared == 0.0);

  const double log_radius = std::log(radius_squared);
  const double scaled_log = -2.0 * log_radius;
  const double multiplier = std::sqrt(scaled_log / radius_squared);
  const double standard_normal = y * multiplier;
  const double scaled = standard_normal * stddev;
  return scaled + mean;
}

} // namespace sql
} // namespace oceanbase
