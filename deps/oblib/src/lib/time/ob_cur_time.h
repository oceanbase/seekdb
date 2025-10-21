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

#ifndef _OB_CUR_TIME_H
#define _OB_CUR_TIME_H 1

#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace common
{
extern volatile int64_t g_cur_time;

class TimeUpdateDuty : public common::ObTimerTask
{
public:
  static const int64_t SCHEDULE_PERIOD = 2000;
  TimeUpdateDuty() {};
  virtual ~TimeUpdateDuty() {};
  virtual void runTimerTask();
};
}
}

#endif /* _OB_CUR_TIME_H */

