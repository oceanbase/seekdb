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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_MONITOR_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_MONITOR_H_

#include "palf/palf_callback.h"

namespace oceanbase
{
namespace logservice
{

class ObLogMonitor : public palf::PalfMonitorCb
{
public:
  ObLogMonitor() { }
  virtual ~ObLogMonitor() { }
public:
  // =========== PALF Event Reporting ===========
  int record_set_base_lsn_event(const palf::LSN &new_base_lsn) override final;
  int record_advance_base_info_event(const palf::PalfBaseInfo &palf_base_info) override final;
  int record_truncate_event(const palf::LSN &lsn,
                            const int64_t min_block_id,
                            const int64_t max_block_id,
                            const int64_t truncate_end_block_id) override final;
  // =========== PALF Event Reporting ===========
private:
  enum EventType
  {
    UNKNOWN = 0,
    SET_BASE_LSN,
    ADVANCE_BASE_INFO,
    TRUNCATE
  };

  const char *type_to_string_(const EventType &event) const
  {
    #define CHECK_LOG_EVENT_TYPE_STR(x) case(EventType::x): return #x
    switch (event)
    {
      case (EventType::SET_BASE_LSN):
        return "SET BASE LSN";
      case (EventType::ADVANCE_BASE_INFO):
        return "ADVANCE BASE INFO";
      CHECK_LOG_EVENT_TYPE_STR(TRUNCATE);
      default:
        return "UNKNOWN";
    }
    #undef CHECK_LOG_EVENT_TYPE_STR
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogMonitor);
};

} // logservice
} // oceanbase

#endif
