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
#include "share/scn.h"

namespace oceanbase
{
namespace rootserver
{
class ObMViewTimerTask : public common::ObTimerTask
{
public:
  ObMViewTimerTask() = default;
  virtual ~ObMViewTimerTask() = default;

  int schedule_task(const int64_t delay, bool repeate = false, bool immediate = false);
  void cancel_task();
  void wait_task();

  static int need_schedule_major_refresh_mv_task(const uint64_t tenant_id,
                                                 bool &need_schedule);
  static int need_push_major_mv_merge_scn(const uint64_t tenant_id,
                                          bool &need_push,
                                          share::SCN &latest_merge_scn,
                                          share::SCN &major_mv_merge_scn);
};

} // namespace rootserver
} // namespace oceanbase
