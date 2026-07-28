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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_SNAPSHOT_GC_SCN_RENEWER_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_SNAPSHOT_GC_SCN_RENEWER_

#include "lib/lock/ob_recursive_mutex.h"

namespace oceanbase
{
namespace rootserver
{
class ObMajorMergeInfoManager;

// Renews the tenant snapshot_gc_scn according to published mini merge progress.
//
// Successful mini merge and upper_trans_version resolution publish one
// monotonically increasing target SCN. The shared major-freeze timer calls
// try_renew(), which keeps the renew decision and transaction inside this
// class. When the latest snapshot_gc_scn has not reached the target, renewer
// runs at the next timer opportunity. Once it catches up, renewer waits until
// target + undo_retention and renews again to cover the target with its GC
// boundary. Failures and concurrent targets are rate-limited to the fixed
// renewal interval.
//
// Restore services, paused services, and inactive primary services never renew.
// Role transitions and the complete renew transaction are serialized by
// role_lock_.
class ObSnapshotGcScnRenewer
{
public:
  ObSnapshotGcScnRenewer();
  ~ObSnapshotGcScnRenewer();

  int init(const bool is_primary_service,
           ObMajorMergeInfoManager &major_merge_info_mgr);
  int destroy();

  void pause();
  void resume();
  bool is_paused() const { return ATOMIC_LOAD(&is_paused_); }
  int on_become_primary();

  int try_renew();
  int64_t get_renew_interval() const { return RENEW_INTERVAL_US; }

private:
  bool need_renew_(const int64_t now);
  static int64_t calc_next_renew_ts_(
      const int64_t renew_target_scn,
      const int64_t undo_retention_s);
  static int64_t calc_gc_boundary_(
      const int64_t snapshot_gc_scn,
      const int64_t undo_retention_s);
  void schedule_next_renew_(const int64_t desired_renew_ts, const int64_t now);

private:
  // Fixed minimum interval between actual renewal attempts.
  static const int64_t RENEW_INTERVAL_US = 10 * 1000 * 1000; // 10s

  // Whether init() has completed successfully.
  bool is_inited_ = false;
  // Atomically accessed lifecycle gate for lock-free external status queries.
  // try_renew() does nothing while the renewer is paused.
  bool is_paused_ = false;
  // True for the APPEND/primary service and false for the RAW_WRITE/restore service.
  bool is_primary_service_ = true;
  // Whether this primary service has been activated and is allowed to renew.
  bool is_primary_active_ = false;
  // Whether becoming primary still requires an immediate catch-up renewal.
  bool need_primary_catchup_ = false;
  // The next wall-clock time in microseconds at which renewal may run.
  // Zero means no renewal is scheduled; primary catch-up treats zero as immediate.
  int64_t next_renew_ts_ = 0;
  // Wall-clock time in microseconds of the most recent renewal attempt.
  // It prevents a new target from increasing the fixed attempt frequency.
  int64_t last_renew_attempt_ts_ = 0;
  // The snapshot_gc_scn written by the most recent successful renewal.
  // Its GC boundary is compared directly with the latest renewal target.
  int64_t last_renewed_snapshot_gc_scn_ = 0;
  // Executes the transactional snapshot_gc_scn renewal.
  ObMajorMergeInfoManager *major_merge_info_mgr_ = nullptr;
  // Serialize primary activation/deactivation with the complete renew transaction.
  // Once pause() returns, no renewal from the old APPEND service can still be running.
  common::ObRecursiveMutex role_lock_{
      common::ObLatchIds::MAJOR_FREEZE_SWITCH_LOCK};

private:
  DISALLOW_COPY_AND_ASSIGN(ObSnapshotGcScnRenewer);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_SNAPSHOT_GC_SCN_RENEWER_
