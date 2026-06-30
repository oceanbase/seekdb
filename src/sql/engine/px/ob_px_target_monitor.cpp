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
#include "share/ob_server_struct.h"

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

int ObPxTargetMonitor::init(const ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_init_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(server));
  } else {
    server_ = server;
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
  server_.reset();
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

int ObPxTargetMonitor::apply_target(hash::ObHashMap<ObAddr, int64_t> &worker_map,
                              int64_t wait_time_us, int64_t session_target, int64_t req_cnt,
                              int64_t &admit_count)
{
  int ret = OB_SUCCESS;
  admit_count = 0;
  bool need_wait = false;
  {
    // Serialize on spin_lock_: apply and release both read-modify-write the
    // single px_target_used_ counter, so they must be mutually exclusive.
    SpinWLockGuard guard(spin_lock_);
    int64_t target = session_target;
    int64_t total_use = px_target_used_;
    int64_t total_req = 0;
    for (hash::ObHashMap<ObAddr, int64_t>::iterator it = worker_map.begin();
        OB_SUCC(ret) && it != worker_map.end(); it++) {
      total_req += it->second;
    }
    if (total_use == 0 || total_use + total_req <= target) {
      // Clamp each worker_map entry to target and WRITE IT BACK, so that
      // release_target() -- which subtracts the same worker_map values -- removes
      // exactly what we add here. This restores the pre-refactor behavior; the
      // single-counter refactor kept the clamp but dropped the write-back, so apply
      // added min(total_req,target) while release subtracted the full total_req,
      // drifting px_target_used_ negative on idle over-sized admissions.
      int64_t acquired = 0;
      for (hash::ObHashMap<ObAddr, int64_t>::iterator it = worker_map.begin();
          OB_SUCC(ret) && it != worker_map.end(); it++) {
        it->second = std::min(it->second, target);
        acquired += it->second;
      }
      px_target_used_ += acquired;
      admit_count = acquired;
      parallel_session_count_++;
    } else {
      need_wait = true;
    }
  }
  if (OB_SUCC(ret) && need_wait) {
    LOG_DEBUG("wait begin", K(wait_time_us), K(session_target), K(req_cnt));
    int64_t wait_us = min(wait_time_us, static_cast<int64_t>(1000000));
    target_cond_.wait(wait_us);
    LOG_DEBUG("wait finish");
  }
  return ret;
}

int ObPxTargetMonitor::release_target(hash::ObHashMap<ObAddr, int64_t> &worker_map)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(spin_lock_);
  int64_t total_rel = 0;
  for (hash::ObHashMap<ObAddr, int64_t>::iterator it = worker_map.begin();
      OB_SUCC(ret) && it != worker_map.end(); it++) {
    total_rel += it->second;
  }
  px_target_used_ -= total_rel;
  target_cond_.notifyAll();
  parallel_session_count_--;
  return ret;
}

int ObPxTargetMonitor::get_all_target_info(common::ObIArray<ObPxTargetInfo> &target_info_array)
{
  int ret = OB_SUCCESS;
  target_info_array.reset();
  ObPxTargetInfo monitor_info;
  monitor_info.server_ = server_;
  monitor_info.is_leader_ = true;
  monitor_info.parallel_servers_target_ = parallel_servers_target_;
  monitor_info.peer_server_ = server_;
  monitor_info.peer_target_used_ = px_target_used_;
  monitor_info.local_target_used_ = px_target_used_;
  monitor_info.local_parallel_session_count_ = parallel_session_count_;
  if (OB_FAIL(target_info_array.push_back(monitor_info))) {
  }
  return ret;
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
