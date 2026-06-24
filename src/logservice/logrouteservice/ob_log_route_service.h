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
#ifndef OCEANBASE_LOG_ROUTE_SERVICE_H_
#define OCEANBASE_LOG_ROUTE_SERVICE_H_

#include "lib/thread/thread_mgr_interface.h"// TGTaskHandler
#include "logservice/palf/lsn.h"            // LSN
#include "share/ob_server_struct.h"         // GCTX

namespace oceanbase
{
namespace logservice
{
/*
 * LogRouteService is used for OB-CDC and Standby cluster etc to fetch log. Supports multiple policies, such as:
 *
 * 1. Fetch specified Region logs preferentially;
 * 2. Select machines based on multiple replicated type policies;
 * 3. Fetch log to the standby replica preferentially to minimize the impact on the leader.
 *    If logs cannot be obtained from all replica, logs can be obtained from the leader;
 * 4. Complete blacklist mechanism, including internal automatic maintenance of Server blacklist and whitewashing mechanism;
 *    Supports the list of servers for filtering external configurations;
 * 5. Perfect switch server mechanism, detection of serviceable machines, automatic fault switching,
 *    to ensure availability and real-time;
 * 6. Supports filtering encrypts Zone to avoid unnecessary access;
 * 7. Supports filtering inactive servers.
 *    ......
 *
 */
class ObLogRouteService : public lib::TGTaskHandler
{
public:
  ObLogRouteService();
  virtual ~ObLogRouteService();

  // @param [in] proxy             ObMySQLProxy
  // @param [in] prefer_region     Prefer Region to fetch log
  // @param [in] cluster_id        ClusterID
  // @param [in] is_across_cluster Whether SQL queries cross cluster
  //   For Standby Cluster, need to set is_across_cluster=true. So SQL Query will contain cluster_id.
  //   For CDC, need to set is_across_cluster=false. So SQL Query will not contain cluster_id.
  // @param [in] external_server_blacklist  External server blacklist
  // @param [in] background_refresh_time_sec  Background periodic refresh time, in second
  // @param [in] all_server_cache_update_interval_sec AllServer periodic refresh time, in second
  // @param [in] all_zone_cache_update_interval_sec AllZone periodic refresh time, in second
  // @param [in] blacklist_survival_time_sec  The survival time of the Server in the blacklist, in seconds
  // @param [in] blacklist_survival_time_upper_limit_min  Upper threshold of the Server blacklist duration (in minute)
  // @param [in] blacklist_survival_time_penalty_period_min
  //    The server is blacklisted, based on the time of the current server service LS - to decide whether to penalize
  //    the survival time.
  //    When the service time is less than a certain interval, a doubling-live-time policy is adopted (in minutes)
  // @param [in] blacklist_history_overdue_time_min Blacklist history expiration time, used to delete history(in minutes)
  // @param [in] blacklist_history_clear_interval_min  Clear blacklist history period(in minutes)
  //
  // @retval OB_SUCCESS           Success
  // @retval Other return values  Failed
  int init(ObISQLClient *proxy,
      const common::ObRegion &prefer_region,
      const int64_t cluster_id,
      const bool is_across_cluster,
      void *err_handler,
      const char *external_server_blacklist = "|",
      const int64_t background_refresh_time_sec = 1200,
      const int64_t all_server_cache_update_interval_sec = 5,
      const int64_t all_zone_cache_update_interval_sec = 5,
      const int64_t blacklist_survival_time_sec = 30,
      const int64_t blacklist_survival_time_upper_limit_min = 4,
      const int64_t blacklist_survival_time_penalty_period_min = 1,
      const int64_t blacklist_history_overdue_time_min = 30,
      const int64_t blacklist_history_clear_interval_min = 20);
  int start();
  void stop();
  void wait();
  void destroy();
  virtual void handle(void *task);

public:
  // Get the server synchronously, iterate over the server for the next service log.
  // If the local cache does not exist, the SQL query is triggered and construct struct to insert.
  //
  // 1. If the server has completed one round of iteration (all servers have been iterated over), then OB_ITER_END is returned
  // 2. After returning OB_ITER_END, the list of servers will be iterated over from the beginning next time
  // 3. If no servers are available, return OB_ITER_END
  //
  // @param [in] ls_id            LS ID
  // @param [in] next_lsn         next LSN
  // @param [out] svr             return fetch log server
  //
  // @retval OB_SUCCESS           Success
  // @retval OB_ITER_END          Server list iterations complete one round, need to query server
  // @retval Other return values  Failed, may be network anomaly and so on.
  int next_server(
      const share::ObLSID &ls_id,
      const palf::LSN &next_lsn,
      common::ObAddr &svr);

  // @param [in] ls_id            LS ID
  // @param [out] svr_array       return fetch log server
  //
  // @retval OB_SUCCESS           Success
  // @retval Other return values  Failed
  int get_server_array_for_locate_start_lsn(
      const share::ObLSID &ls_id,
      ObIArray<common::ObAddr> &svr_array);

  // Determine if the server needs to be switched
  //
  // @param [in] ls_id            LS ID
  // @param [in] next_lsn         next LSN
  // @param [in] cur_svr          current fetch log server
  //
  // @retval true                 Need to switch server
  // @retval false                Don't need to switch server
  bool need_switch_server(
      const share::ObLSID &ls_id,
      const palf::LSN &next_lsn,
      const common::ObAddr &cur_svr);

  // Get available server count
  //
  // @param [in] ls_id            LS ID
  // @param [out] avail_svr_count available server count
  //
  // @retval OB_SUCCESS           Success
  // @retval Other error codes    Fail
  int get_server_count(
      const share::ObLSID &ls_id,
      int64_t &avail_svr_count) const;

  // Launch an asynchronous update task for the server list of LS
  //
  // @param [in] ls_id            LS ID
  //
  // @retval OB_SUCCESS           Success
  // @retval OB_EAGAIN            thread queue is already full
  // @retval Other return values  Failed
   int async_server_query_req(
       const share::ObLSID &ls_id);

private:
  // Read the configured restore source (log_restore_source = $IP:$RPC_PORT) and return its address.
  int get_restore_source_addr_(common::ObAddr &addr) const;

  class ObLSRouteTimerTask : public common::ObTimerTask
  {
  public:
    explicit ObLSRouteTimerTask(ObLogRouteService &log_route_service);
    virtual ~ObLSRouteTimerTask() {}
    int init(int tg_id);
    void destroy();
    virtual void runTimerTask() override;
  private:
    bool is_inited_;
    ObLogRouteService &log_route_service_;
  };

private:
  bool is_inited_;
  int64_t cluster_id_;
  volatile bool is_stopped_ CACHE_ALIGNED;
  ObLSRouteTimerTask ls_route_timer_task_;
  // For single machine: track if next_server has been called to return OB_ITER_END on second call
  bool next_server_called_;

  DISALLOW_COPY_AND_ASSIGN(ObLogRouteService);
};

} // namespace logservice
} // namespace oceanbase

#endif

