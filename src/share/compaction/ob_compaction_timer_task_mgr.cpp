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
#include "deps/oblib/src/lib/thread/thread_mgr.h"
namespace oceanbase
{
namespace compaction
{
int ObCompactionTimerTask::restart_schedule_timer_task(
  const int64_t schedule_interval,
  const int64_t tg_id,
  common::ObTimerTask &timer_task)
{
  int ret = OB_SUCCESS;
  bool is_exist = false;
  if (OB_FAIL(TG_TASK_EXIST(tg_id, timer_task, is_exist))) {
    LOG_ERROR("failed to check merge schedule task exist", K(ret));
  } else if (is_exist && OB_FAIL(TG_CANCEL_R(tg_id, timer_task))) {
    LOG_WARN("failed to cancel task", K(ret));
  } else if (OB_FAIL(TG_SCHEDULE(tg_id, timer_task, schedule_interval, true/*repeat*/))) {
    LOG_WARN("Fail to schedule timer task", K(ret));
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
