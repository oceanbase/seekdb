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


#include "ob_share_throttle_define.h"
#include "lib/alloc/alloc_func.h"
#include "storage/throttle/ob_throttle_info.h"
#include "share/config/ob_server_config.h"
#include "share/ob_task_define.h"


namespace oceanbase {

namespace share {

int64_t FakeAllocatorForTxShare::resource_unit_size()
{
  static const int64_t SHARE_RESOURCE_UNIT_SIZE = 2L * 1024L * 1024L; /* 2MB */
  return SHARE_RESOURCE_UNIT_SIZE;
}

int64_t get_tx_share_memory_limit()
{
  static constexpr int64_t LOW_RESOURCE_MEMORY_BUDGET = 4LL << 30;
  static constexpr int64_t SMALL_TX_SHARE_MEMORY_PERCENTAGE = 110;
  static constexpr int64_t LARGE_TX_SHARE_MEMORY_PERCENTAGE = 130;
  const int64_t memory_budget = lib::get_memory_budget();
  const int64_t percentage = memory_budget <= LOW_RESOURCE_MEMORY_BUDGET
      ? SMALL_TX_SHARE_MEMORY_PERCENTAGE
      : LARGE_TX_SHARE_MEMORY_PERCENTAGE;
  return lib::get_memory_by_percentage(memory_budget, percentage);
}

void FakeAllocatorForTxShare::init_throttle_config(int64_t &resource_limit,
                                                   int64_t &trigger_percentage,
                                                   int64_t &max_duration)
{
  resource_limit = get_tx_share_memory_limit();
  trigger_percentage = GCONF.writing_throttling_trigger_percentage;
  max_duration = GCONF.writing_throttling_maximum_duration;
}

void FakeAllocatorForTxShare::adaptive_update_limit(const int64_t holding_size,
                                                    const int64_t config_specify_resource_limit,
                                                    int64_t &resource_limit,
                                                    int64_t &last_update_limit_ts,
                                                    bool &is_updated)
{
  UNUSEDx(holding_size, config_specify_resource_limit, resource_limit,
          last_update_limit_ts);
  is_updated = false;
}

void PrintThrottleUtil::pirnt_throttle_info(const int err_code,
                                            const char *throttle_unit_name,
                                            const int64_t sleep_time,
                                            const int64_t left_interval,
                                            const int64_t expected_wait_time,
                                            const int64_t abs_expire_time,
                                            const ObThrottleInfoGuard &share_ti_guard,
                                            const ObThrottleInfoGuard &module_ti_guard,
                                            bool &has_printed_lbt)
{
  int ret = err_code;
  const int64_t WARN_LOG_INTERVAL = 1LL * 60L * 1000L * 1000L /* one minute */;
  if (sleep_time > (WARN_LOG_INTERVAL) && TC_REACH_TIME_INTERVAL(WARN_LOG_INTERVAL)) {
    SHARE_LOG(WARN,
              "[Throttling] Attention!! Sleep More Than One Minute!!",
              "Throttle Unit Name",
              throttle_unit_name,
              K(sleep_time),
              K(left_interval),
              K(expected_wait_time),
              K(abs_expire_time),
              KPC(share_ti_guard.throttle_info()),
              KPC(module_ti_guard.throttle_info()));
    if (!has_printed_lbt) {
      has_printed_lbt = true;
      oceanbase::share::ObTaskController::get().allow_next_syslog();
      SHARE_LOG(INFO,
                "[Throttling] (report write throttle info) LBT Info",
                "Throttle Unit Name",
                throttle_unit_name,
                K(lbt()));
    }
  }
}

void PrintThrottleUtil::print_throttle_statistic(const int err_code,
                                                 const char *throttle_unit_name,
                                                 const int64_t sleep_time,
                                                 const int64_t throttle_memory_size)
{
  int ret = err_code;
  const int64_t THROTTLE_LOG_INTERVAL = 1L * 1000L * 1000L; /*one seconds*/
  if (sleep_time > 0 && REACH_TIME_INTERVAL(THROTTLE_LOG_INTERVAL)) {
    SHARE_LOG(INFO,
              "[Throttling] (report write throttle info) Time Info",
              "Throttle Unit Name",
              throttle_unit_name,
              "Throttle Sleep Time(us)",
              sleep_time,
              "Throttle Memory Size",
              throttle_memory_size);
  }
}

}  // namespace share
}  // namespace oceanbase
