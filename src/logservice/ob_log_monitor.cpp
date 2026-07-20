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

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_log_monitor.h"
#include "observer/ob_server_event_history_table_operator.h"   // SERVER_EVENT_ADD_WITH_RETRY

namespace oceanbase
{
namespace logservice
{
#define LOG_MONITOR_EVENT_FMT_PREFIX "LOG", type_to_string_(event)

// =========== PALF Event Reporting ===========
int ObLogMonitor::record_set_base_lsn_event(const palf::LSN &new_base_lsn)
{
  int ret = OB_SUCCESS;
  const EventType event = EventType::SET_BASE_LSN;
  SERVER_EVENT_ADD_WITH_RETRY(LOG_MONITOR_EVENT_FMT_PREFIX,
      "NEW_BASE_LSN", new_base_lsn);
  return ret;
}

int ObLogMonitor::record_advance_base_info_event(const palf::PalfBaseInfo &palf_base_info)
{
  int ret = OB_SUCCESS;
  const EventType event = EventType::ADVANCE_BASE_INFO;
  SERVER_EVENT_ADD_WITH_RETRY(LOG_MONITOR_EVENT_FMT_PREFIX,
      "PALF_BASE_INFO", palf_base_info);
  return ret;
}

int ObLogMonitor::record_truncate_event(const palf::LSN &lsn,
                                        const int64_t min_block_id,
                                        const int64_t max_block_id,
                                        const int64_t truncate_end_block_id)
{
  int ret = OB_SUCCESS;
  const EventType event = EventType::TRUNCATE;
  SERVER_EVENT_ADD_WITH_RETRY(LOG_MONITOR_EVENT_FMT_PREFIX,
      "LSN", lsn,
      "MIN_BLOCK_ID", min_block_id,
      "MAX_BLOCK_ID", max_block_id,
      "TRUNCATE_END_BLOCK_ID", truncate_end_block_id);
  return ret;
}

// =========== PALF Event Reporting ===========


#undef LOG_MONITOR_EVENT_FMT_PREFIX
} // end namespace logservice
} // end namespace oceanbase
