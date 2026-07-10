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
#ifndef OB_SHARE_COMPACTION_COMPACTION_TIMER_TASK_MGR_H_
#define OB_SHARE_COMPACTION_COMPACTION_TIMER_TASK_MGR_H_
#include "lib/task/ob_timer.h"
namespace oceanbase
{
namespace compaction
{
#define DEFINE_TIMER_TASK(TaskName)                                            \
  class TaskName : public common::ObTimerTask {                                \
  public:                                                                      \
    TaskName() = default;                                                      \
    virtual ~TaskName() = default;                                             \
    virtual void runTimerTask() override;                                      \
  };
#define DEFINE_TIMER_TASK_WITHOUT_TIMEOUT_CHECK(TaskName)                      \
  class TaskName : public common::ObTimerTask {                                \
  public:                                                                      \
    TaskName() { disable_timeout_check(); }                                    \
    virtual ~TaskName() = default;                                             \
    virtual void runTimerTask() override;                                      \
  };

struct ObCompactionTimerTask
{
  ObCompactionTimerTask() {}
  ~ObCompactionTimerTask() {}
  virtual void destroy() = 0;
  virtual int start() = 0;
  virtual void stop() = 0;
  virtual void wait() = 0;
  static int restart_schedule_timer_task(
    const int64_t interval,
    common::ObTimer &timer,
    common::ObTimerTask &timer_task,
    const bool immediate = false);
};


} // namespace compaction
} // namespace oceanbase

#endif // OB_SHARE_COMPACTION_COMPACTION_TIMER_TASK_MGR_H_
