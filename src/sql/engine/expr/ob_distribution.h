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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_DETERMINISTIC_DISTRIBUTION_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_DETERMINISTIC_DISTRIBUTION_H_

#include <random>
#include <stdint.h>

namespace oceanbase
{
namespace sql
{

// std::mt19937_64 has a standardized output sequence, but the mappings in
// std::*_distribution are implementation-defined. Keep those mappings here so
// seeded data-generator functions return the same values on every platform.
class ObDistribution
{
public:
  static int64_t uniform_int(std::mt19937_64 &gen, int64_t min_value, int64_t max_value);
  static double uniform_real(std::mt19937_64 &gen, double min_value, double max_value);
  static double normal(std::mt19937_64 &gen, double mean, double stddev);

private:
  static uint64_t uniform_below(std::mt19937_64 &gen, uint64_t range);
  static double canonical_double(std::mt19937_64 &gen);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_DETERMINISTIC_DISTRIBUTION_H_
