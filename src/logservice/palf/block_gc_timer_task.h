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

#ifndef OCEANBASE_PALF_FILE_GC_TIMER_TASK_
#define OCEANBASE_PALF_FILE_GC_TIMER_TASK_
#include "lib/task/ob_timer.h"
namespace oceanbase
{
namespace palf
{
class PalfEnvImpl;
class BlockGCTimerTask : public common::ObTimerTask
{
public:
  BlockGCTimerTask();
  virtual ~BlockGCTimerTask();
  int init(PalfEnvImpl *palf_env_impl);
  int start();
  void stop();
  void wait();
  void destroy();
  virtual void runTimerTask();
private:
  static const int64_t BLOCK_GC_TIMER_INTERVAL_MS = 30 * 1000;
  PalfEnvImpl *palf_env_impl_;
  int64_t warn_time_;
  int tg_id_;
  bool is_inited_;
};
} // end namespace palf
} // end namespace oceanbase
#endif
