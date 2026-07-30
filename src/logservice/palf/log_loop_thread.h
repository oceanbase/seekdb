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

#ifndef OCEANBASE_LOGSERVICE_LOG_LOOP_THREAD_
#define OCEANBASE_LOGSERVICE_LOG_LOOP_THREAD_

#include "share/ob_thread_pool.h"
#include "share/ob_background_task_executor.h"
#include "log_define.h"

namespace oceanbase
{
namespace palf
{
class IPalfEnvImpl;
class LogLoopThread
  : public share::ObThreadPool,
    public share::ObIBackgroundTaskSource
{
public:
  LogLoopThread();
  virtual ~LogLoopThread();
public:
  int init(IPalfEnvImpl *palf_env_impl);
  virtual int start() override;
  virtual void stop() override;
  virtual void wait() override;
  void destroy();
  void run1();
  virtual int process_one_quantum(
      const share::ObBackgroundTaskPriority priority,
      share::ObBackgroundTaskRunResult &result) override;
private:
  void log_loop_();
  void run_one_round_(int64_t &wait_us);
  int notify_background_source_();
  int unregister_background_source_(const bool wait_running);
private:
  IPalfEnvImpl *palf_env_impl_;
  int64_t run_interval_;
  int64_t last_switch_state_time_;
  int64_t last_check_freeze_mode_time_;
  bool is_inited_;
  bool is_running_;
  bool use_shared_executor_;
  share::ObBackgroundTaskExecutor *background_executor_;
  share::ObBackgroundTaskSourceHandle source_handle_;
  lib::ObMutex source_lock_;
private:
  DISALLOW_COPY_AND_ASSIGN(LogLoopThread);
};

} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_LOG_LOOP_THREAD_
