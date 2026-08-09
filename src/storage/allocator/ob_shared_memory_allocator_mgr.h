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

#ifndef OCEANBASE_ALLOCATOR_OB_SHARED_MEMORY_ALLOCATOR_MGR_H_
#define OCEANBASE_ALLOCATOR_OB_SHARED_MEMORY_ALLOCATOR_MGR_H_

#include "storage/allocator/ob_memstore_allocator.h"
#include "storage/allocator/ob_tx_data_allocator.h"
#include "storage/allocator/ob_mds_allocator.h"
#include "storage/allocator/ob_vector_allocator.h"
#include "storage/throttle/ob_share_resource_throttle_tool.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "storage/ls/ob_ls.h"
#include "share/config/ob_server_config.h"

namespace oceanbase {
namespace share {

class ObSharedMemAllocMgr {
public:
  ObSharedMemAllocMgr(): share_resource_throttle_tool_(),
        memstore_allocator_(),
        tx_data_allocator_(),
        mds_allocator_(),
        vector_allocator_() {}
  ObSharedMemAllocMgr(ObSharedMemAllocMgr &rhs) = delete;
  ObSharedMemAllocMgr &operator=(ObSharedMemAllocMgr &rhs) = delete;
  ~ObSharedMemAllocMgr() {}

  static int server_module_init(ObSharedMemAllocMgr *&shared_mem_alloc_mgr) { return shared_mem_alloc_mgr->init(); }

  int init()
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(tx_data_allocator_.init("TX_DATA_SLICE"))) {
    } else if (OB_FAIL(memstore_allocator_.init())) {
    } else if (OB_FAIL(mds_allocator_.init())) {
    } else if (OB_FAIL(tx_data_op_allocator_.init())) {
    } else if (OB_FAIL(vector_allocator_.init())) {
    } else if (OB_FAIL(
                   share_resource_throttle_tool_.init(&memstore_allocator_, &tx_data_allocator_, &mds_allocator_))) {
    } else if (OB_FAIL(vector_throttle_tool_.init(&vector_allocator_))) {
    } else {
      SHARE_LOG(INFO, "finish init runtime shared memory allocator mgr", KP(this));
    }
    return ret;
  }

  int start() { return OB_SUCCESS; }
  void stop() {}
  void wait() {}
  void destroy() {}
  void update_throttle_config();

  ObMemstoreAllocator &memstore_allocator() { return memstore_allocator_; }
  ObTxDataAllocator &tx_data_allocator() { return tx_data_allocator_; }
  ObMdsAllocator &mds_allocator() { return mds_allocator_; }
  TxShareThrottleTool &share_resource_throttle_tool() { return share_resource_throttle_tool_; }
  VectorThrottleTool &vector_throttle_tool() { return vector_throttle_tool_; }
  ObTxDataOpAllocator &tx_data_op_allocator() { return tx_data_op_allocator_; }
  ObVectorAllocator &vector_allocator() { return vector_allocator_; }
  common::MemoryUsageTracker &tx_data_memtable_tracker()
  { return tx_data_memtable_tracker_; }
  common::MemoryUsageTracker &tx_data_metadata_tracker()
  { return tx_data_metadata_tracker_; }
  int64_t tx_data_quota_used() const
  {
    return tx_data_allocator_.slice_backing()
        + tx_data_op_allocator_.hold()
        + tx_data_memtable_tracker_.used();
  }
  int64_t tx_data_managed_used() const
  { return tx_data_quota_used() + tx_data_metadata_tracker_.used(); }

private:
  void update_share_throttle_config_(const int64_t total_memory, common::ObServerConfig *config);
  void update_memstore_throttle_config_(const int64_t total_memory, common::ObServerConfig *config);
  void update_tx_data_throttle_config_(const int64_t total_memory, common::ObServerConfig *config);
  void update_mds_throttle_config_(const int64_t total_memory, common::ObServerConfig *config);

private:
  
  TxShareThrottleTool share_resource_throttle_tool_;
  VectorThrottleTool vector_throttle_tool_;
  ObMemstoreAllocator memstore_allocator_;
  ObTxDataAllocator tx_data_allocator_;
  ObMdsAllocator mds_allocator_;
  ObTxDataOpAllocator tx_data_op_allocator_;
  ObVectorAllocator vector_allocator_;
  common::MemoryUsageTracker tx_data_memtable_tracker_;
  common::MemoryUsageTracker tx_data_metadata_tracker_;
};

class TxShareMemThrottleUtil
{
public:
  static const int64_t SLEEP_INTERVAL_PER_TIME = 20 * 1000; // 20ms;
  template <typename ALLOCATOR>
  static int do_throttle(const bool for_replay,
                         const int64_t abs_expire_time,
                         const int64_t throttle_memory_size,
                         const bool replay_throttle_skip,
                         const bool is_ls_offline,
                         TxShareThrottleTool &throttle_tool,
                         ObThrottleInfoGuard &share_ti_guard,
                         ObThrottleInfoGuard &module_ti_guard)
  {
    int ret = OB_SUCCESS;
    bool has_printed_lbt = false;
    int64_t sleep_time = 0;
    int64_t left_interval = share::ObThrottleUnit<ALLOCATOR>::DEFAULT_MAX_THROTTLE_TIME;

    if (!for_replay) {
      left_interval = min(left_interval, abs_expire_time - ObClockGenerator::getClock());
    }

    uint64_t timeout = 10000;  // 10s

    while (throttle_tool.still_throttling<ALLOCATOR>(share_ti_guard, module_ti_guard) &&
           (left_interval > 0)) {
      int64_t expected_wait_time = 0;
      if ((for_replay && replay_throttle_skip) || is_ls_offline) {  // upper-layer decision is injected by the caller(removes share→storage dependency)
        // skip throttle if : 1) throttle need skipping; 2) this logstream offline
        break;
      } else if ((expected_wait_time =
                      throttle_tool.expected_wait_time<ALLOCATOR>(share_ti_guard, module_ti_guard)) <= 0) {
        if (expected_wait_time < 0) {
          SHARE_LOG(ERROR,
                    "expected wait time should not smaller than 0",
                    K(expected_wait_time),
                    KPC(share_ti_guard.throttle_info()),
                    KPC(module_ti_guard.throttle_info()),
                    K(clock),
                    K(left_interval));
        }
        break;
      }

      // do sleep when expected_wait_time and left_interval are not equal to 0
      int64_t sleep_interval = min(SLEEP_INTERVAL_PER_TIME, expected_wait_time);
      if (0 < sleep_interval) {
        sleep_time += sleep_interval;
        left_interval -= sleep_interval;
        ob_usleep<ObWaitEventIds::STORAGE_WRITING_THROTTLE_SLEEP>(sleep_interval, sleep_interval, sleep_time, 0);
      }

      PrintThrottleUtil::pirnt_throttle_info(ret,
                                             ALLOCATOR::throttle_unit_name(),
                                             sleep_time,
                                             left_interval,
                                             expected_wait_time,
                                             abs_expire_time,
                                             share_ti_guard,
                                             module_ti_guard,
                                             has_printed_lbt);
    }
    PrintThrottleUtil::print_throttle_statistic(ret, ALLOCATOR::throttle_unit_name(), sleep_time, throttle_memory_size);

    if (for_replay && sleep_time > 0) {
      // avoid print replay_timeout
      get_replay_is_writing_throttling() = true;
    }
    return ret;
  }
};

}  // namespace share
}  // namespace oceanbase

#endif
