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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_FREEZE_INFO_DETECTOR_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_FREEZE_INFO_DETECTOR_

#include "lib/task/ob_timer.h"
#include "lib/lock/ob_recursive_mutex.h"
#include "common/ob_role.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace rootserver
{
class ObMajorMergeInfoManager;
class ObThreadIdling;

class ObMajorMergeInfoDetector : public common::ObTimerTask
{
public:
  ObMajorMergeInfoDetector();
  virtual ~ObMajorMergeInfoDetector();
  int init(const bool is_primary_service,
           common::ObMySQLProxy &sql_proxy,
           ObMajorMergeInfoManager &major_merge_info_mgr,
           ObThreadIdling &major_scheduler_idling);

  virtual void runTimerTask() override;
  int64_t get_schedule_interval() const;

  int start();
  void stop();
  void wait();
  int destroy();
  void pause();
  void resume();
  bool is_paused() const { return ATOMIC_LOAD(&is_paused_); }
  int on_become_primary();

  int signal();

private:
  int check_need_broadcast(bool &need_broadcast);
  int try_broadcast_freeze_info();
  int try_renew_snapshot_gc_scn();
  int try_minor_freeze();
  int try_update_zone_info();

  int can_start_work(bool &can_work);
  bool is_primary_service() const { return is_primary_service_; }
  int check_tenant_is_restore(bool &is_restore);
  int try_reload_freeze_info();
  // adjust global_merge_info in memory to avoid useless major freezes on restore major_freeze_service
  int try_adjust_global_merge_info();
  int check_global_merge_info(bool &is_initial) const;
  static bool need_reload_freeze_info_(const bool is_primary_service);
  bool need_renew_snapshot_gc_scn_(const int64_t now);
  static bool is_snapshot_gc_history_due_(
      const int64_t current_time_ns,
      const int64_t first_pending_history_scn,
      const int64_t undo_retention_s);
  int64_t latch_first_pending_snapshot_gc_history_scn_(
      const int64_t pending_history_scn);
  void update_last_run_timestamp_();
private:
  static const int64_t UPDATER_INTERVAL_US = 10 * 1000 * 1000; // 10s

  bool is_inited_;
  bool is_paused_;
  bool is_primary_service_;  // identify ObMajorFreezeServiceType::SERVICE_TYPE_PRIMARY
  bool is_primary_active_;
  bool need_primary_catchup_;
  bool is_global_merge_info_adjusted_;
  bool is_gc_scn_inited_;
  common::ObMySQLProxy *sql_proxy_;
  int64_t last_gc_renew_attempt_ts_; // > 0 after renewal starts; retries use the fixed interval
  int64_t first_pending_snapshot_gc_history_scn_; // retained until the first renewal is due
  int64_t last_run_timestamp_;
  ObMajorMergeInfoManager *major_merge_info_mgr_;
  ObThreadIdling *major_scheduler_idling_;
  int64_t last_schedule_ts_;
  bool need_immediate_run_;
  // Serialize primary activation/deactivation with the complete renew transaction.
  // Once pause() returns, no renewal from the old APPEND service can still be running.
  common::ObRecursiveMutex snapshot_gc_role_lock_;
  common::ObTimer timer_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObMajorMergeInfoDetector);
};

}
}
#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_FREEZE_INFO_DETECTOR_
