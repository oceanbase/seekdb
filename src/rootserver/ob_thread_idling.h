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

#ifndef OCEANBASE_ROOTSERVER_OB_THREAD_IDLING_H_
#define OCEANBASE_ROOTSERVER_OB_THREAD_IDLING_H_

#include "lib/lock/ob_thread_cond.h"

namespace oceanbase
{
namespace rootserver
{

class ObThreadIdling
{
public:
  explicit ObThreadIdling(volatile bool &stop);
  virtual ~ObThreadIdling() {}

  virtual void wakeup();
  virtual int idle();
  virtual int idle(const int64_t max_idle_time_us);

  virtual int64_t get_idle_interval_us() = 0;

private:
  common::ObThreadCond cond_;
  volatile bool &stop_;
  int64_t wakeup_cnt_;
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_THREAD_IDLING_H_
