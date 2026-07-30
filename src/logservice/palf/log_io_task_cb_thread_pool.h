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

#ifndef OCEANBASE_LOGSERVICE_LOG_IO_TASK_CB_THREAD_POOL_
#define OCEANBASE_LOGSERVICE_LOG_IO_TASK_CB_THREAD_POOL_

#include "lib/thread/ob_simple_thread_pool.h"
#include "share/ob_background_task_executor.h"

namespace oceanbase
{
namespace palf
{
class IPalfEnvImpl;
class LogIOTaskCbThreadPool
    : public common::ObLinkQueueThreadPool,
      public share::ObIBackgroundTaskSource
{
public:
  LogIOTaskCbThreadPool();
  ~LogIOTaskCbThreadPool();

public:
  int init(const int64_t log_io_cb_num,
           IPalfEnvImpl *palf_env_impl);
  int start();
  int stop();
  int wait();
  void destroy();
  int push(common::LinkTask *task);
  int attach_background_executor(
      share::ObBackgroundTaskExecutor *background_executor);
  int detach_background_executor();
  int process_one_quantum(
      const share::ObBackgroundTaskPriority priority,
      share::ObBackgroundTaskRunResult &result) override;
  void handle(common::LinkTask *task) override;

public:
  static constexpr int64_t THREAD_NUM = 1;
  static constexpr int64_t MINI_MODE_THREAD_NUM = 1;
  static constexpr int64_t MAX_LOG_IO_CB_TASK_NUM = 100 * 10000;

private:
  int notify_background_source_();
  int unregister_background_source_(const bool wait_running);
  void ensure_rescue_worker_(const bool force);

  DISALLOW_COPY_AND_ASSIGN(LogIOTaskCbThreadPool);

private:
  int64_t thread_num_;
  IPalfEnvImpl *palf_env_impl_;
  bool is_inited_;
  bool use_shared_executor_;
  lib::ObMutex source_lock_;
  lib::ObMutex rescue_lock_;
  share::ObBackgroundTaskExecutor *background_executor_;
  share::ObBackgroundTaskSourceHandle source_handle_;
};
} // end namespace palf
} // end namespace oceanbase

#endif
