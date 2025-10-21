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
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "storage/tx/ob_location_adapter.h"
#include "storage/tx/ob_ts_mgr.h"
#include "sql/engine/px/ob_px_rpc_proxy.h"
#include "lib/lock/ob_monitor.h"
#include "lib/lock/mutex.h"


namespace oceanbase
{
namespace sql
{
class ObILocationAdapter;

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

struct ServerTargetUsage {
  OB_UNIS_VERSION(1);
public:
  ServerTargetUsage() :
      peer_target_used_(0),
      local_target_used_(0),
      report_target_used_(0) {}
public:
  void set_peer_used(int64_t peer_used) { peer_target_used_ = peer_used; }
  void update_peer_used(int64_t peer_used) { peer_target_used_ += peer_used; }
  int64_t get_peer_used() const { return peer_target_used_; }

  //void set_local_used(int64_t local_used) { local_target_used_ = local_used; }
  void update_local_used(int64_t local_used) { local_target_used_ += local_used; }
  int64_t get_local_used() const { return local_target_used_; }

  void set_report_used(int64_t report_used) { report_target_used_ = report_used; }
  void update_report_used(int64_t report_used) { report_target_used_ += report_used; }
  int64_t get_report_used() const { return report_target_used_; }

  TO_STRING_KV(K_(peer_target_used), K_(local_target_used), K_(report_target_used));
private:
  // Understanding key points:
  // Each follower will report to the leader the local target consumption of each machine from its own perspective. leader
  // Will aggregate these target consumptions from the local perspective to get a global perspective of target consumption, denoted as peer_target_used_
  //
  // Each follower reports to the leader the difference between the local report and the last report: "increment"
  // leader by adding up the "increment" of each follower gets a global view.
  //
  // Consideration: Can each follower directly report the local_target_used_ saved on its own machine? Theoretically, it is possible, but on the leader side
  // Summing up is troublesome, it requires traversing all follower's values and summing them. Reporting "increment" can avoid this summation operation.
  //
  int64_t peer_target_used_;     // leader's perspective data: the aggregated target usage reported by followers is summarized as peer_target_used_
  int64_t local_target_used_;    // follower perspective data: locally recorded resource consumption quantity, this amount may include part that has not yet been reported to leader
  int64_t report_target_used_;   // follower perspective data: the number already reported to leader, so that leader can have an as accurate as possible global view after aggregation
  // Note: The source of the resource consumption recorded locally (local_target_used_) is unrelated to any content applied for in ObPxSubAdmission, and is only changed by the following process:
  //  - Query through ObPxAdmission apply, release
};

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
#define PX_SERVER_TARGET_BUCKET_NUM (hash::cal_next_prime(100))
public:
  ObPxTenantTargetMonitor() : spin_lock_(common::ObLatchIds::PX_TENANT_TARGET_LOCK) { reset(); }
  virtual ~ObPxTenantTargetMonitor() {}
  int init(const uint64_t tenant_id, ObAddr &server);
  void reset();

  // for monitor
  void set_parallel_servers_target(int64_t parallel_servers_target);
  int64_t get_parallel_servers_target();
  int64_t get_parallel_session_count();

  // for rpc
  int refresh_statistics(bool need_refresh_all);
  bool is_leader();
  uint64_t get_version();
  int update_peer_target_used(const ObAddr &server, int64_t peer_used, uint64_t version);
  int get_global_target_usage(const hash::ObHashMap<ObAddr, ServerTargetUsage> *&global_target_usage);
  // if role is follower and find that its version is different with leader's
  // call this function to reset statistics, the param version is from the leader.
  int reset_follower_statistics(uint64_t version);
  // if role is leader and wants to start a new round of statistics, call this function.
  // A new version is generated in this function and will be sync to all followers later.
  int reset_leader_statistics();

  // for px_admission
  int apply_target(hash::ObHashMap<ObAddr, int64_t> &worker_map,
                   int64_t wait_time_us, int64_t session_target, int64_t req_cnt,
                   int64_t &admit_count, uint64_t &admit_version);
  int release_target(hash::ObHashMap<ObAddr, int64_t> &worker_map, uint64_t version);

  // for virtual_table iter
  int get_all_target_info(common::ObIArray<ObPxTargetInfo> &target_info_array);
  static uint64_t get_server_index(uint64_t version);

  TO_STRING_KV(K_(is_init), K_(tenant_id), K_(server), K_(dummy_cache_leader), K_(role));

private:
  int get_dummy_leader(ObAddr &leader);
  int check_dummy_location_credible(bool &need_refresh);
  int get_role(ObRole &role);
  int refresh_dummy_location();
  int query_statistics(ObAddr &leader);
  uint64_t get_new_version();

private:
  static const int64_t SERVER_ID_SHIFT = 48;
  bool is_init_;
  uint64_t tenant_id_;
  ObAddr server_;
  // inner sql without pl no_use_px, so here can depend on all_dummy
  int64_t cluster_id_;
  common::ObAddr dummy_cache_leader_;
  ObRole role_;
  obrpc::ObPxRpcProxy rpc_proxy_;
  int64_t parallel_servers_target_; // equal in every server
  uint64_t version_; // for refresh target_info
  hash::ObHashMap<ObAddr, ServerTargetUsage> global_target_usage_; // include self
  // a lock to handle the concurrent access and modification of version_ and usage map.
  // use write lock before reset map and refresh the version
  // use read lock before other operations of version_ and usage map.
  // That means, there may be multiple threads modify the map concurrently,
  // including insert/update operations, without delete.
  // The basic principle is that:
  // 1. When we update the map and find that the key does not exist, we try to insert an entry first,
  //    if insert failed with OB_HASH_EXIST, that means someone else insert already, we should try update again.
  //    This update operation will always succeed because we have created read lock and this entry cannot be removed.
  // 2. When we insert into the map and failed with OB_HASH_EXIST, skip insert if the usage is empty,
  //    otherwise do a update operation.
  SpinRWLock spin_lock_;
  int64_t parallel_session_count_;
  ObPxTargetCond target_cond_;
  bool print_debug_log_;
  bool need_send_refresh_all_;
};

}
}

#endif /* __SQL_ENG_PX_TENANT_TARGET_MONITOR_H__ */
//// end of header file
