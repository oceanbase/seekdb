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
           const int64_t tenant_id,
           int cb_thread_pool_tg_id,
           ObIAllocator *allocaotr,
           IPalfEnvImpl *palf_env_impl);
  void destroy();
  int start();
  void stop();
  void wait();
  // NB: nowdays, this interface can only be called when create_palf_handle_impl!!! otherwise, round_robin_idx_
  // will not be correct.
  LogIOWorker *get_log_io_worker(const int64_t palf_id);
  int notify_need_writing_throttling(const bool &need_throtting);
  int64_t get_last_working_time() const;
  TO_STRING_KV(K_(is_inited), K_(is_user_tenant), K_(log_writer_parallelism), KP(log_io_workers_), K_(round_robin_idx));
  
private:
  int create_and_init_log_io_workers_(const LogIOWorkerConfig &config,
                                      const int64_t tenant_id,
                                      int cb_thread_pool_tg_id,
                                      ObIAllocator *allocaotr,
                                      IPalfEnvImpl *palf_env_impl);
  int start_();
  void stop_();
  void wait_();
  void destory_and_free_log_io_workers_();
  int64_t palf_id_to_index_(const int64_t palf_id);
  constexpr static int64_t SYS_LOG_IO_WORKER_INDEX = 0;
  
private:
  bool is_user_tenant_;
  // 'log_writer_parallelism_' has include LogIOWorker which is used for sys log stream.
  int64_t log_writer_parallelism_;
  // The layout of LogIOWorker: | sys log ioworker | others |
  LogIOWorker *log_io_workers_;
  LogWritingThrottle throttle_;
  int64_t round_robin_idx_;
  bool is_inited_;
};

}//end of namespace palf
}//end of namespace oceanbase
#endif
