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

#ifndef OCEANBASE_SHARE_OB_SHARED_TIMER_H_
#define OCEANBASE_SHARE_OB_SHARED_TIMER_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObTimerTask;
}
namespace share
{

class ObISharedTimer
{
public:
  virtual ~ObISharedTimer() = default;
  virtual int schedule(
      common::ObTimerTask &task,
      int64_t delay,
      bool repeat = false,
      bool immediate = false) = 0;
  virtual int cancel_task(const common::ObTimerTask &task) = 0;
  virtual int wait_task(const common::ObTimerTask &task) = 0;
  virtual bool task_exist(const common::ObTimerTask &task) = 0;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_SHARED_TIMER_H_
