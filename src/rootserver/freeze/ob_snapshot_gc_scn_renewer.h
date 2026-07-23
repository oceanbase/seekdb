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

  bool need_renew(const int64_t now);
  int try_renew();
  int64_t get_renew_interval() const { return RENEW_INTERVAL_US; }

private:
  static int64_t calc_next_renew_ts_(
      const int64_t renew_target_scn,
      const int64_t undo_retention_s);
  int64_t latch_next_renew_ts_(
      const int64_t renew_target_scn,
      const int64_t undo_retention_s);
  bool is_primary_service() const { return is_primary_service_; }

private:
  static const int64_t RENEW_INTERVAL_US = 10 * 1000 * 1000; // 10s

  bool is_inited_;
  bool is_paused_;
  bool is_primary_service_;
  bool is_primary_active_;
  bool need_primary_catchup_;
  // The next wall-clock time in microseconds at which renewal may run.
  // Zero means no renewal is scheduled; primary catch-up treats zero as immediate.
  int64_t next_renew_ts_;
  // The greatest renewal target SCN covered by a successful refresh.
  int64_t refreshed_scn_;
  ObMajorMergeInfoManager *major_merge_info_mgr_;
  // Serialize primary activation/deactivation with the complete renew transaction.
  // Once pause() returns, no renewal from the old APPEND service can still be running.
  common::ObRecursiveMutex role_lock_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObSnapshotGcScnRenewer);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_SNAPSHOT_GC_SCN_RENEWER_
