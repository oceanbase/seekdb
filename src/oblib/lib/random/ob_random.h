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

#ifndef OB_RANDOM_H_
#define OB_RANDOM_H_

#include <stdint.h>
#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{

class ObRandom
{
public:
  ObRandom();
  virtual ~ObRandom();
  inline void gen_seed();
  //get a random int64_t number in [min(a,b), max(a,b)]
  void seed(const uint64_t seed);
  static int64_t rand(const int64_t a, const int64_t b);
  //get a random int64_t number
  int64_t get();
  //get a random int64_t number in [min(a,b), max(a,b)]
  int64_t get(const int64_t a, const int64_t b);
  //get a random int32_t number
  int32_t get_int32();
private:
  uint16_t seed_[3];
  bool is_inited;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRandom);
};

} /* namespace common */
} /* namespace oceanbase */

#endif /* OB_RANDOM_H_ */
