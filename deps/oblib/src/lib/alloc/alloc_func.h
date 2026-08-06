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
constexpr int64_t DEFAULT_MEMORY_BASE_PERCENTAGE = 40;
constexpr int64_t KV_CACHE_MEMORY_PERCENTAGE = 25;
constexpr int64_t MAX_KV_CACHE_MEMORY_LIMIT = 1LL << 40;
constexpr int64_t LOW_RESOURCE_MEMORY_BUDGET = 4LL << 30;
constexpr int64_t MEMSTORE_MEMORY_PERCENTAGE = 20;
constexpr int64_t VECTOR_MEMORY_PERCENTAGE = 10;
constexpr int64_t TX_DATA_MEMORY_PERCENT = 40;
constexpr int64_t MDS_MEMORY_PERCENT = 20;
constexpr int64_t SMALL_TX_SHARE_MEMORY_PERCENT = 110;
constexpr int64_t LARGE_TX_SHARE_MEMORY_PERCENT = 130;
constexpr int64_t TX_DATA_FREEZE_MEMORY_PERCENT = 10;
constexpr int64_t MDS_FREEZE_MEMORY_PERCENT = 4;
constexpr int64_t COMPACTION_MEMORY_PERCENT = 40;
void set_memory_budget(int64_t bytes);
int64_t get_memory_budget();
void set_kvcache_memory_limit(int64_t bytes);
int64_t get_kvcache_memory_limit();
void set_kvcache_memory_capacity(int64_t bytes);
int64_t get_kvcache_memory_capacity();
void set_memstore_memory_limit(int64_t bytes);
int64_t get_memstore_memory_limit();
void set_vector_memory_limit(int64_t bytes);
int64_t get_vector_memory_limit();
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
inline int64_t get_memory_budget_by_percentage(const int64_t percentage)
{
  return get_memory_by_percentage(get_memory_budget(), percentage);
}
// Use the minimum budget until effective system memory has been detected during
// server config reload.
inline int64_t get_default_module_memory_base(const int64_t system_memory)
{
  return system_memory > 0 ? system_memory : DEFAULT_MEMORY_BUDGET;
}
inline int64_t calculate_default_memory_base(const int64_t system_memory)
{
  const int64_t automatic_base =
      get_memory_by_percentage(system_memory, DEFAULT_MEMORY_BASE_PERCENTAGE);
  return automatic_base > DEFAULT_MEMORY_BUDGET
      ? automatic_base
      : DEFAULT_MEMORY_BUDGET;
}
inline int64_t resolve_kvcache_memory_limit(const int64_t configured_limit,
                                            const int64_t system_memory)
{
  const int64_t requested_limit = configured_limit > 0
      ? configured_limit
      : get_memory_by_percentage(get_default_module_memory_base(system_memory),
                                 KV_CACHE_MEMORY_PERCENTAGE);
  return requested_limit < MAX_KV_CACHE_MEMORY_LIMIT
      ? requested_limit
      : MAX_KV_CACHE_MEMORY_LIMIT;
}
inline int64_t resolve_memstore_memory_limit(const int64_t configured_limit,
                                             const int64_t system_memory)
{
  return configured_limit > 0
      ? configured_limit
      : get_memory_by_percentage(get_default_module_memory_base(system_memory),
                                 MEMSTORE_MEMORY_PERCENTAGE);
}
inline int64_t resolve_vector_memory_limit(const int64_t configured_limit,
                                           const int64_t system_memory)
{
  return configured_limit > 0
      ? configured_limit
      : get_memory_by_percentage(get_default_module_memory_base(system_memory),
                                 VECTOR_MEMORY_PERCENTAGE);
}
inline int64_t get_tx_data_memory_limit()
{ return get_memory_budget_by_percentage(TX_DATA_MEMORY_PERCENT); }
inline int64_t get_mds_memory_limit()
{ return get_memory_budget_by_percentage(MDS_MEMORY_PERCENT); }
inline int64_t get_tx_data_freeze_trigger_memory()
{ return get_memory_budget_by_percentage(TX_DATA_FREEZE_MEMORY_PERCENT); }
inline int64_t get_mds_freeze_trigger_memory()
{ return get_memory_budget_by_percentage(MDS_FREEZE_MEMORY_PERCENT); }
// The budget-derived TxShare throttle retains its old 55%/65% policy, but must
// not undercut the independently configured Memstore module limit.
inline int64_t get_tx_share_memory_limit()
{
  const int64_t percentage = get_memory_budget() <= LOW_RESOURCE_MEMORY_BUDGET
      ? SMALL_TX_SHARE_MEMORY_PERCENT
      : LARGE_TX_SHARE_MEMORY_PERCENT;
  const int64_t budget_derived_limit = get_memory_budget_by_percentage(percentage);
  const int64_t memstore_limit = get_memstore_memory_limit();
  return budget_derived_limit > memstore_limit
      ? budget_derived_limit
      : memstore_limit;
}
inline int64_t get_compaction_memory_limit()
{ return get_memory_budget_by_percentage(COMPACTION_MEMORY_PERCENT); }
int64_t get_allocator_memory_hold();
int64_t get_allocator_memory_hold(const uint64_t ctx_id);
int64_t get_allocator_cache_hold();
void get_label_memory(
  ObLabel &label, common::ObLabelItem &item);
void ob_set_reserved_memory(const int64_t bytes);
int64_t ob_get_reserved_memory();

int set_ctx_limit(uint64_t ctx_id, const int64_t limit);

// Set the metadata-object memory limit.
int set_meta_obj_limit(int64_t meta_obj_pct_lmt);

bool errsim_alloc(const ObMemAttr &attr);

int set_req_chunkmgr_parallel(uint64_t ctx_id, int32_t parallel);
} // end of namespace lib
} // end of namespace oceanbase

#endif /* _ALLOC_FUNC_H_ */
