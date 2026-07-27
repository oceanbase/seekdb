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

#ifndef OB_SERVER_DUTY_TASK_H
#define OB_SERVER_DUTY_TASK_H
#include <stdint.h>
#include "lib/task/ob_timer.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase {
namespace observer {

class ObServerDutyTask
    : private common::ObTimerTask
{
  static constexpr int64_t SCHEDULE_PERIOD = 10 * 1000L * 1000L;
public:
  int schedule(common::ObTimer &timer);
  ObServerDutyTask();
private:
  void runTimerTask() override;
  void update_runtime_settings();

private:
  // Update work-area settings.
  int update_wa_percentage();
  // Apply context-memory limits from the runtime configuration.
  int update_ctx_memory_throttle();
  // Read the work-area memory setting.
  int read_wa_percentage(int64_t &pctg);
private:
  common::ObArenaAllocator allocator_;
};

class ObSqlMemoryTimerTask : private common::ObTimerTask
{
public:
  int schedule(common::ObTimer &timer);
private:
  void runTimerTask() override;
private:
  static constexpr int64_t SCHEDULE_PERIOD = 3 * 1000L * 1000L;
};

}  // observer
}  // oceanbase

#endif /* OB_SERVER_DUTY_TASK_H */
