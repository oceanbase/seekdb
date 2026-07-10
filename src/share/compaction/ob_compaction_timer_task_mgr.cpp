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
#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "share/compaction/ob_compaction_timer_task_mgr.h"
namespace oceanbase
{
namespace compaction
{
int ObCompactionTimerTask::restart_schedule_timer_task(
  const int64_t schedule_interval,
  common::ObTimer &timer,
  common::ObTimerTask &timer_task,
  const bool immediate)
{
  int ret = OB_SUCCESS;
  if (timer.task_exist(timer_task) && OB_FAIL(timer.cancel(timer_task))) {
    LOG_WARN("failed to cancel task", K(ret));
  } else if (OB_FAIL(timer.schedule(timer_task, schedule_interval, true/*repeat*/, immediate))) {
    LOG_WARN("Fail to schedule timer task", K(ret));
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
