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

#ifndef _OB_ACCUMULATOR_H
#define _OB_ACCUMULATOR_H 1
#include <stdint.h>
#include "lib/atomic/ob_atomic.h"
#include "lib/thread_local/ob_tsi_utils.h"
namespace oceanbase
{
namespace common
{
class ObAccumulator
{
public:
  ObAccumulator() : freeze_value_(0), tmp_value_(0) {}
  ~ObAccumulator() {}

  void add(int64_t delta = 1) { ATOMIC_AAF(&tmp_value_, delta); }
  void freeze() { ATOMIC_SET(&freeze_value_, tmp_value_); ATOMIC_SET(&tmp_value_, 0); }
  int64_t get_value() const { return ATOMIC_LOAD(&freeze_value_); }
private:
  int64_t freeze_value_;
  int64_t tmp_value_;
};

} // end namespace common
} // end namespace oceanbase

#endif /* _OB_ACCUMULATOR_H */
