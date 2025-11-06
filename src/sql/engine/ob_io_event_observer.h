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

#ifndef _OB_SQL_IO_EVENT_OBSERVER_H_
#define _OB_SQL_IO_EVENT_OBSERVER_H_

#include "share/diagnosis/ob_sql_plan_monitor_node_list.h"

namespace oceanbase
{
namespace sql
{
class ObIOEventObserver
{
public:
  ObIOEventObserver(ObMonitorNode &monitor_info) : op_monitor_info_(monitor_info) {}
  inline void on_read_io(uint64_t used_time)
  {
    op_monitor_info_.block_time_ += used_time;
  }
  inline void on_write_io(uint64_t used_time)
  {
    op_monitor_info_.block_time_ += used_time;
  }
private:
  ObMonitorNode &op_monitor_info_;
};
}
}
#endif /* _OB_SQL_IO_EVENT_OBSERVER_H_ */
//// end of header file

