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

#define USING_LOG_PREFIX  SQL_ENG
#include "ob_px_target_monitor.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace obutil;

namespace sql
{

ObPxTargetMonitor &ObPxTargetMonitor::get_instance()
{
  static ObPxTargetMonitor instance;
  return instance;
}

int ObPxTargetMonitor::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_init_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    parallel_servers_target_ = INT64_MAX;
    px_target_used_ = 0;
    is_init_ = true;
    parallel_session_count_ = 0;
  }
  return ret;
}

void ObPxTargetMonitor::reset()
{
  is_init_ = false;
  parallel_servers_target_ = INT64_MAX;
  px_target_used_ = 0;
  parallel_session_count_ = 0;
}

void ObPxTargetMonitor::set_parallel_servers_target(int64_t parallel_servers_target)
{
  parallel_servers_target_ = parallel_servers_target;
}

int64_t ObPxTargetMonitor::get_parallel_servers_target()
{
  return parallel_servers_target_;
}

int64_t ObPxTargetMonitor::get_parallel_session_count()
{
  return parallel_session_count_;
}

int ObPxTargetMonitor::apply_target(int64_t wait_time_us, int64_t session_target,
                                   int64_t minimal_req_cnt, int64_t req_cnt,
                                   int64_t &admit_count)
{
  int ret = OB_SUCCESS;
  admit_count = 0;
  bool need_wait = false;
  {
    SpinWLockGuard guard(spin_lock_);
    const int64_t target = session_target;
    const int64_t total_use = px_target_used_;
    const int64_t available = total_use < target ? target - total_use : 0;
    const int64_t acquired = std::min(req_cnt, available);
    if (acquired >= minimal_req_cnt) {
      px_target_used_ += acquired;
      admit_count = acquired;
      parallel_session_count_++;
    } else {
      need_wait = true;
    }
  }
  if (OB_SUCC(ret) && need_wait) {
    int64_t wait_us = min(wait_time_us, static_cast<int64_t>(1000000));
    target_cond_.wait(wait_us);
  }
  return ret;
}

int ObPxTargetMonitor::release_target(int64_t worker_count)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(spin_lock_);
  px_target_used_ -= worker_count;
  target_cond_.notifyAll();
  parallel_session_count_--;
  return ret;
}

void ObPxTargetMonitor::get_target_info(ObPxTargetInfo &target_info)
{
  SpinRLockGuard guard(spin_lock_);
  target_info.local_target_ = parallel_servers_target_;
  target_info.target_used_ = px_target_used_;
  target_info.local_parallel_session_count_ = parallel_session_count_;
}

int ObPxTargetCond::wait(const int64_t wait_time_us)
{
  int ret = OB_SUCCESS; 
  if (wait_time_us < 0) {
    TRANS_LOG(WARN, "invalid argument", K(wait_time_us));
    ret = OB_INVALID_ARGUMENT;
  } else {
    THIS_WORKER.sched_wait();
    {
      ObMonitor<Mutex>::Lock guard(monitor_);
      if (!monitor_.timed_wait(ObSysTime(wait_time_us))) { // timeout
        ret = OB_TIMEOUT;
      }
    }
    THIS_WORKER.sched_run();
  }
  return ret;
}

void ObPxTargetCond::notifyAll()
{
  ObMonitor<Mutex>::Lock guard(monitor_);
  monitor_.notify_all();
}


}
}
