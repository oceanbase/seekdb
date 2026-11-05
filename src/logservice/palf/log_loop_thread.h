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
#include "log_define.h"

namespace oceanbase
{
namespace palf
{
class IPalfEnvImpl;
class LogLoopThread : public share::ObThreadPool
{
public:
  LogLoopThread();
  virtual ~LogLoopThread();
public:
  int init(IPalfEnvImpl *palf_env_impl);
  void destroy();
  void run1();
private:
  void log_loop_();
private:
  IPalfEnvImpl *palf_env_impl_;
  int64_t run_interval_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(LogLoopThread);
};

} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_LOG_LOOP_THREAD_
