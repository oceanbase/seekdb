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

#ifndef _ALLOC_FUNC_H_
#define _ALLOC_FUNC_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
struct ObLabelItem;
} // end of namespace common

namespace lib
{
// statistic relating
struct ObLabel;
struct ObMemAttr;
constexpr int64_t DEFAULT_MEMORY_BUDGET = 1L << 30;
void set_memory_budget(int64_t bytes);
int64_t get_memory_budget();
inline int64_t get_memory_by_percentage(const int64_t memory,
                                        const int64_t percentage)
{
  int64_t result = 0;
  if (memory > 0 && percentage > 0) {
    const int64_t quotient = memory / 100;
    if (percentage > INT64_MAX / 99) {
      result = INT64_MAX;
    } else {
      const int64_t remainder_charge = memory % 100 * percentage / 100;
      result = quotient > (INT64_MAX - remainder_charge) / percentage
          ? INT64_MAX
          : quotient * percentage + remainder_charge;
    }
  }
  return result;
}
int64_t get_allocator_memory_hold();
int64_t get_allocator_memory_hold(const uint64_t ctx_id);
int64_t get_allocator_cache_hold();
void get_label_memory(
  ObLabel &label, common::ObLabelItem &item);
void ob_set_reserved_memory(const int64_t bytes);
int64_t ob_get_reserved_memory();

int set_ctx_limit(uint64_t ctx_id, const int64_t limit);

bool errsim_alloc(const ObMemAttr &attr);

int set_req_chunkmgr_parallel(uint64_t ctx_id, int32_t parallel);
} // end of namespace lib
} // end of namespace oceanbase

#endif /* _ALLOC_FUNC_H_ */
