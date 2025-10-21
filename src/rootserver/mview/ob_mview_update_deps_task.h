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

#pragma once

#include "lib/task/ob_timer.h"
#include "rootserver/mview/ob_mview_timer_task.h"

namespace oceanbase
{
namespace rootserver
{
class ObMViewUpdateDepsTask : public ObMViewTimerTask
{
public:
  ObMViewUpdateDepsTask();
  virtual ~ObMViewUpdateDepsTask();
  DISABLE_COPY_ASSIGN(ObMViewUpdateDepsTask);
  int init();
  int start();
  void stop();
  void wait();
  // for TimerTask
  void runTimerTask() override;
  int need_schedule(bool &need_sche);
  static const int64_t MVIEW_UPDATE_DEPS_INTERVAL = 5 * 1000 * 1000; // 5s
private:
  bool is_inited_;
  bool in_sched_;
  bool is_stop_;
  uint64_t tenant_id_;
  int64_t last_sched_ts_;
};

} // namespace rootserver
} // namespace oceanbase
