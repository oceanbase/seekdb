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

#include "lib/net/ob_addr.h"
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
  ObAddr server_;
  bool is_leader_;
  ObAddr peer_server_;
  int64_t parallel_servers_target_;
  int64_t peer_target_used_;
  int64_t local_target_used_;
  int64_t local_parallel_session_count_;
  TO_STRING_KV(K_(server), K_(is_leader), K_(peer_server),
               K_(parallel_servers_target), K_(peer_target_used), K_(local_target_used),
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
  int init(const ObAddr &server);
  void reset();

  // for monitor
  void set_parallel_servers_target(int64_t parallel_servers_target);
  int64_t get_parallel_servers_target();
  int64_t get_parallel_session_count();


  // for px_admission
  int apply_target(hash::ObHashMap<ObAddr, int64_t> &worker_map,
                   int64_t wait_time_us, int64_t session_target, int64_t req_cnt,
                   int64_t &admit_count);
  int release_target(hash::ObHashMap<ObAddr, int64_t> &worker_map);

  // for virtual_table iter
  int get_all_target_info(common::ObIArray<ObPxTargetInfo> &target_info_array);

  TO_STRING_KV(K_(is_init), K_(server), K_(px_target_used));

private:
  bool is_init_;
  ObAddr server_;
  int64_t parallel_servers_target_;
  int64_t px_target_used_;
  // Protects px_target_used_ from concurrent apply/release/reset.
  // apply_target and release_target serialize on this lock since both read-modify-write
  // the single px_target_used_ counter (unlike the old hash map where per-server entries
  // allowed concurrent updates).
  SpinRWLock spin_lock_;
  int64_t parallel_session_count_;
  ObPxTargetCond target_cond_;
};

#define OB_PX_TARGET_MONITOR (::oceanbase::sql::ObPxTargetMonitor::get_instance())

}
}

#endif /* __SQL_ENG_PX_TARGET_MONITOR_H__ */
//// end of header file
