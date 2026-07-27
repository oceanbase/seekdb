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

#ifndef __SQL_ENG_PX_TARGET_MONITOR_H__
#define __SQL_ENG_PX_TARGET_MONITOR_H__

#include "share/ob_define.h"
#include "lib/lock/ob_monitor.h"
#include "lib/lock/mutex.h"
#include "lib/lock/ob_spin_rwlock.h"


namespace oceanbase
{
namespace sql
{

struct ObPxTargetInfo
{
  int64_t local_target_;
  int64_t target_used_;
  int64_t local_parallel_session_count_;
  TO_STRING_KV(K_(local_target), K_(target_used),
               K_(local_parallel_session_count));
};

// PX target usage is tracked by a single counter (px_target_used_),
// consumed by apply_target/release_target.

class ObPxTargetCond
{
public:
  ObPxTargetCond() {}
  ~ObPxTargetCond() {}
public:
  // wait when no resource available
  int wait(const int64_t wait_time_us);
  // notify threads to wakeup and retry
  void notifyAll();
private:
  DISALLOW_COPY_AND_ASSIGN(ObPxTargetCond);
private:
  mutable obutil::ObMonitor<obutil::Mutex> monitor_;
};

class ObPxTargetMonitor
{
public:
  ObPxTargetMonitor() : spin_lock_(common::ObLatchIds::PX_TARGET_LOCK) { reset(); }
  virtual ~ObPxTargetMonitor() {}
  static ObPxTargetMonitor &get_instance();
  int init();
  void reset();

  // for monitor
  void set_parallel_servers_target(int64_t parallel_servers_target);
  int64_t get_parallel_servers_target();
  int64_t get_parallel_session_count();


  // for px_admission
  int apply_target(int64_t wait_time_us, int64_t session_target, int64_t req_cnt,
                   int64_t &admit_count);
  int release_target(int64_t worker_count);

  // for virtual_table iter
  void get_target_info(ObPxTargetInfo &target_info);

  TO_STRING_KV(K_(is_init), K_(parallel_servers_target), K_(px_target_used),
               K_(parallel_session_count));

private:
  bool is_init_;
  int64_t parallel_servers_target_;
  int64_t px_target_used_;
  // Protects the local PX worker quota counters.
  SpinRWLock spin_lock_;
  int64_t parallel_session_count_;
  ObPxTargetCond target_cond_;
};

#define OB_PX_TARGET_MONITOR (::oceanbase::sql::ObPxTargetMonitor::get_instance())

}
}

#endif /* __SQL_ENG_PX_TARGET_MONITOR_H__ */
//// end of header file
