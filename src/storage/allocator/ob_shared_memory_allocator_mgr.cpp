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

#define USING_LOG_PREFIX SHARE

#include "ob_shared_memory_allocator_mgr.h"

namespace oceanbase {
namespace share {

#define THROTTLE_CONFIG_LOG(ALLOCATOR, LIMIT, TRIGGER_PERCENTAGE, MAX_DURATION) \
          "Unit Name",                                                          \
          ALLOCATOR::throttle_unit_name(),                                      \
          "Memory Limit(MB)",                                                   \
          LIMIT / 1024 / 1024,                                                  \
          "Throttle Trigger(MB)",                                               \
          LIMIT * trigger_percentage / 100 / 1024 / 1024,                       \
          "Trigger Percentage",                                                 \
          TRIGGER_PERCENTAGE,                                                   \
          "Max Alloc Duration",                                                 \
          MAX_DURATION

void ObSharedMemAllocMgr::get_tx_data_memory_info(int64_t &hold,
                                                   int64_t &used,
                                                   int64_t &mem_limit)
{
  // Keep HOLD in the same accounting domain as MEM_LIMIT. TX_DATA metadata is
  // tracked separately and is not charged to the TX_DATA throttle quota.
  hold = tx_data_quota_used();
  used = hold;
  mem_limit = share_resource_throttle_tool_.get_resource_limit<ObTxDataAllocator>();
}

void ObSharedMemAllocMgr::get_mds_memory_info(int64_t &hold,
                                               int64_t &used,
                                               int64_t &mem_limit)
{
  hold = mds_allocator_.hold();
  used = hold;
  mem_limit = share_resource_throttle_tool_.get_resource_limit<ObMdsAllocator>();
}

void ObSharedMemAllocMgr::get_vector_memory_info(int64_t &hold,
                                                  int64_t &used,
                                                  int64_t &mem_limit)
{
  hold = vector_allocator_.hold();
  used = vector_allocator_.used();
  mem_limit = share_resource_throttle_tool_.get_resource_limit<ObVectorAllocator>();
}

}  // namespace share
}  // namespace oceanbase
