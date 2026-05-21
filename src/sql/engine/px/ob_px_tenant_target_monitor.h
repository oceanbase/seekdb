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

#ifndef __SQL_ENG_PX_TENANT_TARGET_MONITOR_H__
#define __SQL_ENG_PX_TENANT_TARGET_MONITOR_H__

#include "lib/net/ob_addr.h"
#include "share/ob_define.h"
#include "common/ob_role.h"
#include "lib/lock/ob_monitor.h"
#include "lib/lock/mutex.h"
#include "lib/lock/ob_spin_rwlock.h"


namespace oceanbase
{
namespace sql
{

enum PX_TARGET_MONITOR_STATUS {
  MONITOR_READY = 0,
  MONITOR_VERSION_NOT_MATCH,
  MONITOR_NOT_MASTER,
  MONITOR_MAX_STATUS
};

struct ObPxTargetInfo
{
  ObAddr server_;
  uint64_t tenant_id_;
  bool is_leader_;
  uint64_t version_;
  ObAddr peer_server_;
  int64_t parallel_servers_target_;
  int64_t peer_target_used_;
  int64_t local_target_used_;
  int64_t local_parallel_session_count_;
  TO_STRING_KV(K_(server), K_(tenant_id), K_(is_leader), K_(version), K_(peer_server),
               K_(parallel_servers_target), K_(peer_target_used), K_(local_target_used),
               K_(local_parallel_session_count));
};

// In single-server mode, PX target usage is tracked by a simple counter
// instead of a per-server hash map. The legacy ServerTargetUsage struct
// (peer_target_used_ / local_target_used_ / report_target_used_) is removed;
// the only consumer, apply_target/release_target, now uses px_target_used_.

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

class ObPxTenantTargetMonitor
{
public:
  ObPxTenantTargetMonitor() : spin_lock_(common::ObLatchIds::PX_TENANT_TARGET_LOCK) { reset(); }
  virtual ~ObPxTenantTargetMonitor() {}
  int init(const uint64_t tenant_id, ObAddr &server);
  void reset();

  // for monitor
  void set_parallel_servers_target(int64_t parallel_servers_target);
  int64_t get_parallel_servers_target();
  int64_t get_parallel_session_count();

  bool is_leader();
  uint64_t get_version();
  int update_peer_target_used(const ObAddr &server, int64_t peer_used, uint64_t version);
  int64_t get_px_target_used() const { return px_target_used_; }
  int reset_follower_statistics(uint64_t version);
  int reset_leader_statistics();

  // for px_admission
  int apply_target(hash::ObHashMap<ObAddr, int64_t> &worker_map,
                   int64_t wait_time_us, int64_t session_target, int64_t req_cnt,
                   int64_t &admit_count, uint64_t &admit_version);
  int release_target(hash::ObHashMap<ObAddr, int64_t> &worker_map, uint64_t version);

  // for virtual_table iter
  int get_all_target_info(common::ObIArray<ObPxTargetInfo> &target_info_array);
  static uint64_t get_server_index(uint64_t version);

  TO_STRING_KV(K_(is_init), K_(tenant_id), K_(server), K_(role), K_(px_target_used));

private:
  uint64_t get_new_version();

private:
  static const int64_t SERVER_ID_SHIFT = 48;
  bool is_init_;
  uint64_t tenant_id_;
  ObAddr server_;
  ObRole role_;
  int64_t parallel_servers_target_;
  uint64_t version_;
  int64_t px_target_used_;
  // Protects px_target_used_ and version_ from concurrent apply/release/reset.
  // apply_target and release_target serialize on this lock since both read-modify-write
  // the single px_target_used_ counter (unlike the old hash map where per-server entries
  // allowed concurrent updates).
  SpinRWLock spin_lock_;
  int64_t parallel_session_count_;
  ObPxTargetCond target_cond_;
};

}
}

#endif /* __SQL_ENG_PX_TENANT_TARGET_MONITOR_H__ */
//// end of header file
