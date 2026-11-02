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

#ifndef OCEANBASE_LOGSERVICE_LOG_SHARED_QUEUE_THREAD_
#define OCEANBASE_LOGSERVICE_LOG_SHARED_QUEUE_THREAD_

#include "lib/thread/thread_mgr_interface.h"
#include "lib/utility/ob_print_utils.h"
#include "palf_callback.h"

namespace oceanbase
{
namespace palf
{
class IPalfEnvImpl;
class LogSharedTask;
class LogHandleSubmitTask;
class LogFillCacheTask;

class LogSharedQueueTh : public lib::TGTaskHandler
{
public:
  LogSharedQueueTh();
  ~LogSharedQueueTh();
public:
  int init(IPalfEnvImpl *palf_env_impl);
  int start();
  int stop();
  int wait();
  void destroy();
  int push_submit_log_task(LogHandleSubmitTask *task);
  int push_task(LogSharedTask *task);
  virtual void handle(void *task);
  int get_tg_id() const;
public:
  static constexpr int64_t THREAD_NUM = 1;
  static constexpr int64_t MINI_MODE_THREAD_NUM = 1;
  static constexpr int64_t MAX_LOG_HANDLE_TASK_NUM = 10 * OB_MAX_LS_NUM_PER_TENANT_PER_SERVER;
private:
  DISALLOW_COPY_AND_ASSIGN(LogSharedQueueTh);
private:
  int submit_log_tg_id_;
  int shared_tg_id_;
  IPalfEnvImpl *palf_env_impl_;
  bool is_inited_;
};
} // end namespace palf
} // end namespace oceanbase

#endif
