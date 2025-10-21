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

#include "lib/random/ob_random.h"

namespace oceanbase
{
namespace common
{
ObRandom::ObRandom()
    : seed_(), is_inited(false)
{
}

ObRandom::~ObRandom()
{
}

void ObRandom::gen_seed()
{
  seed_[0] = (uint16_t)rand(0, 65535);
  seed_[1] = (uint16_t)rand(0, 65535);
  seed_[2] = (uint16_t)rand(0, 65535);
  seed48(seed_);
  is_inited = true;
}

void ObRandom::seed(const uint64_t seed)
{
  seed_[0] = (uint16_t)seed;
  seed_[1] = (uint16_t)(seed >> 16);
  seed_[2] = (uint16_t)(seed >> 32);
  seed48(seed_);
  is_inited = true;
}

int64_t ObRandom::rand(const int64_t a, const int64_t b)
{
  struct Wrapper {
    uint16_t v_[3];
  };
  // NOTE: thread local random seed
  RLOCAL(Wrapper, wrapper);
  uint16_t *seed = (&wrapper)->v_;
  if (0 == seed[0] && 0 == seed[1] && 0 == seed[2]) {
    seed[0] = static_cast<uint16_t>(GETTID());
    seed[1] = seed[0];
    seed[2] = seed[1];
    seed48(seed);
  }
  const int64_t r1 = jrand48(seed);
  const int64_t r2 = jrand48(seed);
  int64_t min = a < b ? a : b;
  int64_t max = a < b ? b : a;
  return min + labs((r1 << 32) | r2) % (max - min + 1);
}

int64_t ObRandom::get()
{
  if (!is_inited) {
    gen_seed();
  }
  const int64_t r1 = jrand48(seed_);
  const int64_t r2 = jrand48(seed_);
  return ((r1 << 32) | r2);
}

int64_t ObRandom::get(const int64_t a, const int64_t b)
{
  int64_t min = a < b ? a : b;
  int64_t max = a < b ? b : a;
  return min + labs(get()) % (max - min + 1);
}

int32_t ObRandom::get_int32()
{
  if (!is_inited) {
    gen_seed();
  }
  return static_cast<int32_t>(jrand48(seed_));
}

} /* namespace common */
} /* namespace oceanbase */
