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

#include "ob_monitor_info_elimination_task.h"
#include "sql/monitor/ob_monitor_info_manager.h"
namespace oceanbase
{
namespace sql
{
int ObMonitorInfoEliminationTask::init(ObMonitorInfoManager *info)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(info)) {
    ret = common::OB_INVALID_ARGUMENT;
    SQL_MONITOR_LOG(WARN, "invalid argument", K(ret), K(info));
  } else {
    monitor_info_ = info;
  }
  return ret;
}
void ObMonitorInfoEliminationTask::runTimerTask()
{
  if (OB_ISNULL(monitor_info_)) {
    SQL_MONITOR_LOG_RET(ERROR, OB_INVALID_ARGUMENT, "invalid history info", K(monitor_info_));
  } else {
    monitor_info_->print_memory_size();
    monitor_info_->gc();
  }
}
} //namespace sql
} //namespace oceanbase


