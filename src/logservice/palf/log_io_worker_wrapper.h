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

#ifndef OCEANBASE_LOGSERVIVE_LOG_IO_WORKER_WRAPPER_
#define OCEANBASE_LOGSERVIVE_LOG_IO_WORKER_WRAPPER_

#include "log_throttle.h"
#include "log_io_worker.h"

namespace oceanbase
{
namespace palf
{
class LogIOWorkerWrapper
{
public:
  LogIOWorkerWrapper();
  ~LogIOWorkerWrapper();
  int init(const LogIOWorkerConfig &config,
           LogIOTaskCbThreadPool *cb_thread_pool,
           ObIAllocator *allocaotr,
           IPalfEnvImpl *palf_env_impl);
  void destroy();
  int start();
  void stop();
  void wait();
  LogIOWorker *get_log_io_worker() { return &log_io_worker_; }
  int notify_need_writing_throttling(const bool &need_throtting);
  int64_t get_last_working_time() const;
  TO_STRING_KV(K_(is_inited), K_(log_io_worker));
  
private:
  LogIOWorker log_io_worker_;
  LogWritingThrottle throttle_;
  bool is_inited_;
};

}//end of namespace palf
}//end of namespace oceanbase
#endif
