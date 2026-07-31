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

#define USING_LOG_PREFIX STORAGE
#include "ob_freeze_info_mgr.h"
#include "share/rc/ob_module_provider.h"
#include "share/ob_merge_info.h"
#include "share/ob_global_merge_table_operator.h"
#include "storage/compaction/ob_compaction_schedule_util.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/compaction/ob_compaction_schedule_util.h"
#include "share/ob_tablet_local_checksum_operator.h"  // verify_column_checksum real user FreezeInfoMgr

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace share::schema;

using common::hash::ObHashSet;

namespace storage
{
const char *ObStorageSnapshotInfo::ObSnapShotTypeStr[] = {
    "UNDO_RETENTION",
    "SNAPSHOT_FOR_TX",
    "MAJOR_FREEZE_TS",
    "MULTI_VERSION_START_ON_TABLET",
    "SNAPSHOT_ON_TABLET",
    "LS_RESERVED",
    "MIN_MEDIUM"
};

ObStorageSnapshotInfo::ObStorageSnapshotInfo()
  : snapshot_type_(SNAPSHOT_MAX),
    snapshot_(0)
{
  STATIC_ASSERT(SNAPSHOT_MAX - share::ObSnapShotType::MAX_SNAPSHOT_TYPE == ARRAYSIZEOF(ObSnapShotTypeStr), "snapshot type len is mismatch");
}

const char * ObStorageSnapshotInfo::get_snapshot_type_str() const
{
  const char * str = nullptr;
  if (OB_UNLIKELY(snapshot_type_ >= SNAPSHOT_MAX)) {
    str = "invalid_snapshot_type";
  } else if (snapshot_type_ < ObSnapShotType::MAX_SNAPSHOT_TYPE) {
    str = ObSnapshotInfo::get_snapshot_type_str((ObSnapShotType)snapshot_type_);
  } else {
    str = ObSnapShotTypeStr[snapshot_type_ - ObSnapShotType::MAX_SNAPSHOT_TYPE];
  }
  return str;
}

void ObStorageSnapshotInfo::update_by_smaller_snapshot(
  const uint64_t input_snapshot_type,
  const int64_t input_snapshot)
{
  if ((input_snapshot_type < SNAPSHOT_MAX && input_snapshot >= 0) // input info is valid
      && (!is_valid() || snapshot_ > input_snapshot)) {
    // assign to smaller snapshot
    snapshot_ = input_snapshot;
    snapshot_type_ = input_snapshot_type;
  }
}

ObFreezeInfoMgr::ObFreezeInfoMgr()
  : reload_task_(*this),
    update_reserved_snapshot_task_(*this),
    freeze_info_mgr_(),
    snapshots_(),
    lock_(),
    cur_idx_(0),
    snapshot_gc_scn_renewal_state_(),
    reload_timer_(),
    inited_(false)
{
}

ObFreezeInfoMgr::~ObFreezeInfoMgr()
{
  destroy();
}

int ObFreezeInfoMgr::server_module_init(ObFreezeInfoMgr* &freeze_info_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "failed to get sql proxy from GCTX, cannot init FreezeInfoMgr", K(ret));
  } else if (OB_FAIL(freeze_info_mgr->init(*GCTX.sql_proxy_))) {
    STORAGE_LOG(WARN, "failed to init freeze info mgr", K(ret));
  } else {
    STORAGE_LOG(INFO, "success to init freeze info manager");
  }
  return ret;
}

int ObFreezeInfoMgr::init(ObISQLClient &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "init twice", K(ret));
  } else if (OB_FAIL(freeze_info_mgr_.init(*GCTX.sql_proxy_))) {
    STORAGE_LOG(WARN, "fail to init freeze info mgr", K(ret));
  } else if (OB_FAIL(reload_task_.init())) {
    STORAGE_LOG(ERROR, "fail to init reload task", K(ret));
  } else if (OB_FAIL(reload_timer_.init("FreInfoReload", ObMemAttr("FreInfoReload")))) {
    STORAGE_LOG(ERROR, "fail to init timer", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObFreezeInfoMgr::start()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (OB_FAIL(reload_timer_.schedule(reload_task_, RELOAD_INTERVAL, true))) {
    STORAGE_LOG(ERROR, "fail to schedule reload task", K(ret));
  } else if (OB_FAIL(reload_timer_.schedule(update_reserved_snapshot_task_, UPDATE_LS_RESERVED_SNAPSHOT_INTERVAL, true))) {
    STORAGE_LOG(ERROR, "fail to schedule update reserved snapshot task", K(ret));
  }
  return ret;
}

void ObFreezeInfoMgr::wait()
{
  reload_timer_.wait();
}

void ObFreezeInfoMgr::stop()
{
  reload_timer_.stop();
}

void ObFreezeInfoMgr::destroy()
{
  reload_timer_.destroy();
}

int64_t ObFreezeInfoMgr::get_latest_frozen_version()
{
  int64_t frozen_version = 0;

  RLockGuard lock_guard(lock_);
  frozen_version = freeze_info_mgr_.get_latest_frozen_scn().get_val_for_tx();
  return frozen_version;
}

int ObFreezeInfoMgr::get_min_dependent_freeze_info(ObFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = common::ObTimeUtility::current_time() + RLOCK_TIMEOUT_US;
  RLockGuardWithTimeout lock_guard(lock_, abs_timeout_us, ret);

  if (OB_FAIL(ret)) {
    STORAGE_LOG(WARN, "get_lock failed", KR(ret));
  } else if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else {
    const int64_t info_cnt = freeze_info_mgr_.get_freeze_info_count();
    int64_t idx = 0;
    if (info_cnt > MIN_DEPENDENT_FREEZE_INFO_GAP) {
      idx = info_cnt - MIN_DEPENDENT_FREEZE_INFO_GAP;
    }

    if (OB_FAIL(freeze_info_mgr_.get_freeze_info_by_idx(idx, freeze_info))) {
      STORAGE_LOG(WARN, "fail to get frozen status", K(ret), K(idx));
    } else {
      LOG_INFO("get min dependent freeze info", K(ret), K(freeze_info)); // diagnose code for issue 45841468
    }
  }
  return ret;
}

int ObFreezeInfoMgr::get_freeze_info_behind_major_snapshot(
    const int64_t major_snapshot_version,
    const bool include_equal,
    ObIArray<ObFreezeInfo> &freeze_infos)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = common::ObTimeUtility::current_time() + RLOCK_TIMEOUT_US;
  RLockGuardWithTimeout lock_guard(lock_, abs_timeout_us, ret);

  if (OB_FAIL(ret)) {
    STORAGE_LOG(WARN, "get_lock failed", KR(ret));
  } else if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (OB_UNLIKELY(major_snapshot_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument to get freeze info", K(ret), K(major_snapshot_version));
  } else if (OB_FAIL(freeze_info_mgr_.get_freeze_info_behind_snapshot_version(major_snapshot_version, include_equal, freeze_infos))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      STORAGE_LOG(WARN, "failed to get frozen status behind given snapshot version", K(ret), K(major_snapshot_version));
    }
  }
  return ret;
}

int ObFreezeInfoMgr::get_freeze_info_by_snapshot_version(
    const int64_t snapshot_version,
    ObFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = common::ObTimeUtility::current_time() + RLOCK_TIMEOUT_US;
  RLockGuardWithTimeout lock_guard(lock_, abs_timeout_us, ret);

  if (OB_FAIL(ret)) {
    STORAGE_LOG(WARN, "get_lock failed", KR(ret));
  } else if (OB_UNLIKELY(snapshot_version <= 0 || INT64_MAX == snapshot_version)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "snapshot version is invalid", K(ret), K(snapshot_version));
  } else if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (OB_FAIL(freeze_info_mgr_.get_freeze_info_by_major_snapshot(snapshot_version, freeze_info))) {
    STORAGE_LOG(WARN, "failed to get frozen status by snapshot", K(ret), K(snapshot_version));
  }
  return ret;
}

int ObFreezeInfoMgr::get_lower_bound_freeze_info_before_snapshot_version(const int64_t snapshot_version, share::ObFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;
  const int64_t abs_timeout_us = common::ObTimeUtility::current_time() + RLOCK_TIMEOUT_US;
  RLockGuardWithTimeout lock_guard(lock_, abs_timeout_us, ret);
  if (OB_FAIL(ret)) {
    STORAGE_LOG(WARN, "get_lock failed", KR(ret));
  } else if (OB_FAIL(get_freeze_info_compare_with_snapshot_version_(snapshot_version, share::ObFreezeInfoManager::CmpType::LOWER_BOUND, freeze_info))) {
    STORAGE_LOG(WARN, "failed to get freeze info before snapshot version", KR(ret), K(snapshot_version));
  }
  return ret;
}

int ObFreezeInfoMgr::get_freeze_info_compare_with_snapshot_version_(
    const int64_t snapshot_version,
    const share::ObFreezeInfoManager::CmpType cmp_type,
    ObFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(snapshot_version <= 0 || INT64_MAX == snapshot_version)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "snapshot version is invalid", K(ret), K(snapshot_version));
  } else if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (OB_FAIL(freeze_info_mgr_.get_freeze_info_compare_with_major_snapshot(snapshot_version, cmp_type, freeze_info))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      STORAGE_LOG(WARN, "fail to found frozen status compare with major snapshot", K(ret), K(snapshot_version), K(cmp_type));
    }
  }
  return ret;
}

int ObFreezeInfoMgr::get_neighbour_major_freeze(
    const int64_t snapshot_version,
    NeighbourFreezeInfo &info)
{
  int ret = OB_SUCCESS;

  info.reset();
  bool found = false;
  share::ObFreezeInfo prev_frozen_status;
  share::ObFreezeInfo next_frozen_status;
  const int64_t abs_timeout_us = common::ObTimeUtility::current_time() + RLOCK_TIMEOUT_US;
  RLockGuardWithTimeout lock_guard(lock_, abs_timeout_us, ret);

  if (OB_FAIL(ret)) {
    STORAGE_LOG(WARN, "get_lock failed", KR(ret));
  } else if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (OB_FAIL(freeze_info_mgr_.get_neighbour_frozen_status(snapshot_version, prev_frozen_status, next_frozen_status))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      STORAGE_LOG(WARN, "failed to get neighbour frozen status", K(ret), K(snapshot_version));
    }
  } else {
    info.next = next_frozen_status;
    info.prev = prev_frozen_status;
  }
  return ret;
}

static inline
int is_snapshot_related_to_tablet(
    const ObTabletID &tablet_id,
    const ObSnapshotInfo &snapshot,
    bool &related)
{
  int ret = OB_SUCCESS;
  related = false;


  if (!snapshot.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(snapshot));
  } else {
    // A zero tablet id applies the snapshot to every local tablet.
    if (0 == snapshot.tablet_id_
        || snapshot.tablet_id_ == tablet_id.id()) {
      related = true;
    }
  }
  return ret;
}

int ObFreezeInfoMgr::get_multi_version_duration(int64_t &duration) const
{
  int ret = OB_SUCCESS;

  duration = GCONF.undo_retention;

  return ret;
}

int64_t ObFreezeInfoMgr::get_min_reserved_snapshot_for_tx()
{
  int64_t snapshot_version = INT64_MAX;

  // Local disk pressure (or failure to determine disk status) disables the
  // local active-transaction watermark optimization. Sampling failures keep
  // using the last complete local watermark instead.
  bool is_gc_disabled = share::g_mp->multi_version_garbage_collector()->
    is_gc_disabled();

  if (GCONF._mvcc_gc_using_min_txn_snapshot
      && !is_gc_disabled) {
    share::SCN snapshot_for_active_tx =
      share::g_mp->multi_version_garbage_collector()->
      get_reserved_snapshot_for_active_txn();
    snapshot_version = snapshot_for_active_tx.get_val_for_tx();
  }

  return snapshot_version;
}

// get smallest kept snapshot
int ObFreezeInfoMgr::get_min_reserved_snapshot(
    const ObTabletID &tablet_id,
    const int64_t merged_version,
    ObStorageSnapshotInfo &snapshot_info)
{
  int ret = OB_SUCCESS;
  ObFreezeInfo freeze_info;
  int64_t duration = 0;
  bool unused = false;
  snapshot_info.reset();
  const int64_t abs_timeout_us = common::ObTimeUtility::current_time() + RLOCK_TIMEOUT_US;
  RLockGuardWithTimeout lock_guard(lock_, abs_timeout_us, ret);
  ObIArray<ObSnapshotInfo> &snapshots = snapshots_[cur_idx_];
  if (OB_FAIL(ret)) {
    STORAGE_LOG(WARN, "get_lock failed", KR(ret));
  } else if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (OB_FAIL(get_multi_version_duration(duration))) {
    STORAGE_LOG(WARN, "fail to get multi version duration", K(ret), K(tablet_id));
  } else {
    if (merged_version < 1) {
      freeze_info.frozen_scn_.set_min();
    } else if (OB_FAIL(get_freeze_info_compare_with_snapshot_version_(merged_version, share::ObFreezeInfoManager::CmpType::GREATER_THAN, freeze_info))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to get freeze info behind snapshot", K(ret), K(merged_version));
      } else {
        freeze_info.frozen_scn_.set_max();
        ret = OB_SUCCESS;
      }
    }

    const int64_t snapshot_gc_ts = freeze_info_mgr_.get_snapshot_gc_scn().get_val_for_tx();
    const int64_t snapshot_for_undo_retention = MAX(0, snapshot_gc_ts - duration * 1000L * 1000L * 1000L);
    const int64_t snapshot_for_tx = get_min_reserved_snapshot_for_tx();
    snapshot_info.update_by_smaller_snapshot(ObStorageSnapshotInfo::SNAPSHOT_FOR_UNDO_RETENTION, snapshot_for_undo_retention);
    snapshot_info.update_by_smaller_snapshot(ObStorageSnapshotInfo::SNAPSHOT_FOR_TX, snapshot_for_tx);
    snapshot_info.update_by_smaller_snapshot(ObStorageSnapshotInfo::SNAPSHOT_FOR_MAJOR_FREEZE_TS, freeze_info.frozen_scn_.get_val_for_tx());
    for (int64_t i = 0; i < snapshots.count() && OB_SUCC(ret); ++i) {
      bool related = false;
      const ObSnapshotInfo &snapshot = snapshots.at(i);
      if (OB_FAIL(is_snapshot_related_to_tablet(tablet_id, snapshot, related))) {
        STORAGE_LOG(WARN, "fail to check snapshot relation", K(ret), K(tablet_id), K(snapshot));
      } else if (related) {
        snapshot_info.update_by_smaller_snapshot(snapshot.snapshot_type_, snapshot.snapshot_scn_.get_val_for_tx());
      }
    }
    LOG_TRACE("check_freeze_info_mgr", K(ret), K(snapshot_info), K(duration), K(snapshot_for_undo_retention),
      K(freeze_info), K(snapshot_gc_ts), K(snapshot_for_tx));
  }
  return ret;
}

int ObFreezeInfoMgr::update_next_snapshots(const ObIArray<ObSnapshotInfo> &snapshots)
{
  int ret = OB_SUCCESS;
  int64_t next_idx = get_next_idx();
  snapshots_[next_idx].reset();
  ObIArray<ObSnapshotInfo> &next_snapshots = snapshots_[next_idx];

  for (int64_t i = 0; OB_SUCC(ret) && i < snapshots.count(); ++i) {
    if (OB_FAIL(next_snapshots.push_back(snapshots.at(i)))) {
      STORAGE_LOG(WARN, "fail to push back snapshot", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    switch_info();
  }
  return ret;
}

int64_t ObFreezeInfoMgr::get_snapshot_gc_ts()
{
  return get_snapshot_gc_scn().get_val_for_tx();
}

share::SCN ObFreezeInfoMgr::get_snapshot_gc_scn()
{
  RLockGuard lock_guard(lock_);
  return freeze_info_mgr_.get_snapshot_gc_scn();
}

ObFreezeInfoMgr::ReloadTask::ReloadTask(ObFreezeInfoMgr &mgr)
  : inited_(false),
    check_runtime_status_(),
    mgr_(mgr)
{
}

int ObFreezeInfoMgr::ReloadTask::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "init twice", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObFreezeInfoMgr::ReloadTask::refresh_merge_info()
{
  int ret = OB_SUCCESS;



  ObGlobalMergeInfo global_merge_info;

  int64_t cur_broadcast_version = 0;
  int64_t global_broadcast_version = 0;

  if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(*GCTX.sql_proxy_, global_merge_info))) {
    LOG_WARN("failed to load global merge info", KR(ret), K(global_merge_info));
  } else {
    // set merged version
    MERGE_SCHEDULER_PTR->set_inner_table_merged_scn(global_merge_info.last_merged_scn_.get_scn().get_val_for_tx());
    if (global_merge_info.suspend_merging_.get_value()) { // suspend_merge
      MERGE_SCHEDULER_PTR->stop_major_merge();
      LOG_INFO("stop major merge", K(global_merge_info));
    } else {
      if (check_runtime_status_) {
        {
          check_runtime_status_ = false;
        }
      }
      if (!check_runtime_status_) {
        MERGE_SCHEDULER_PTR->resume_major_merge();
        cur_broadcast_version = MERGE_SCHEDULER_PTR->get_frozen_version();
        global_broadcast_version = global_merge_info.global_broadcast_scn_.get_scn().get_val_for_tx();
        if (global_broadcast_version > cur_broadcast_version) {
          FLOG_INFO("try to schedule merge", K(global_broadcast_version), K(cur_broadcast_version));
          if (OB_FAIL(MERGE_SCHEDULER_PTR->schedule_merge(global_broadcast_version))) {
            LOG_WARN("fail to schedule merge", K(ret), K(global_broadcast_version));
          } else if (OB_FAIL(share::g_mp->memstore_freezer()->update_frozen_scn(global_broadcast_version))) {
            LOG_WARN("update frozen scn failed", K(ret), K(global_broadcast_version));
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    LOG_TRACE("refresh merge info", K(global_merge_info));
  }
  return ret;
}

int ObFreezeInfoMgr::try_update_info()
{
  int ret = OB_SUCCESS;

  DEBUG_SYNC(BEFORE_UPDATE_FREEZE_SNAPSHOT_INFO);
  ObSEArray<ObSnapshotInfo, 4> snapshots;
  ObSEArray<ObFreezeInfo, 4> freeze_infos;
  share::SCN new_snapshot_gc_scn;
  share::ObSnapshotTableProxy snapshot_proxy;

  if (OB_FAIL(ObFreezeInfoManager::fetch_new_freeze_info(
        share::SCN::base_scn(), *GCTX.sql_proxy_, freeze_infos, new_snapshot_gc_scn))) {
    STORAGE_LOG(WARN, "failed to load updated info", K(ret));
  } else if (OB_FAIL(snapshot_proxy.get_all_snapshots(*GCTX.sql_proxy_, snapshots))) {
    STORAGE_LOG(WARN, "failed to get snapshots", K(ret));
  } else if (OB_FAIL(inner_update_info(new_snapshot_gc_scn, freeze_infos, snapshots))) {
    STORAGE_LOG(WARN, "failed to update info", K(ret), K(freeze_infos), K(new_snapshot_gc_scn), K(snapshots));
  }
  return ret;
}

int ObFreezeInfoMgr::inner_update_info(
    const share::SCN &new_snapshot_gc_scn,
    const common::ObIArray<share::ObFreezeInfo> &new_freeze_infos,
    const common::ObIArray<share::ObSnapshotInfo> &new_snapshots)
{
  int ret = OB_SUCCESS;
  int64_t snapshot_gc_ts = 0;
  {
    WLockGuard lock_guard(lock_);
    if (OB_FAIL(freeze_info_mgr_.update_freeze_info(new_freeze_infos, new_snapshot_gc_scn))) {
      STORAGE_LOG(WARN, "failed to reload freeze info mgr", K(ret));
    } else if (OB_FAIL(update_next_snapshots(new_snapshots))) {
      STORAGE_LOG(WARN, "fail to update next snapshots", K(ret));
    } else {
      snapshot_gc_ts = freeze_info_mgr_.get_snapshot_gc_scn().get_val_for_tx();
    }
  }
  STORAGE_LOG(DEBUG, "reload freeze info and snapshots", K(snapshot_gc_ts), K(new_snapshots));

  if (OB_SUCC(ret)) {
    if (REACH_THREAD_TIME_INTERVAL(20 * 1000 * 1000 /*20s*/)) {
      STORAGE_LOG(INFO, "ObFreezeInfoMgr success to update infos",
          K(new_snapshot_gc_scn), K(new_freeze_infos), K(new_snapshots), K(freeze_info_mgr_));
    }
  }
  return ret;
}

void ObFreezeInfoMgr::ReloadTask::runTimerTask()
{
  int tmp_ret = OB_SUCCESS;
  if (!SERVER_STORAGE_META_SERVICE.is_started()) {
    if (REACH_TIME_INTERVAL(10 * 1000 * 1000 /* 10s */)) {
      LOG_WARN_RET(tmp_ret, "slog replay hasn't finished, this task can't start");
    }
  } else {
    if (OB_TMP_FAIL(refresh_merge_info())) {
      LOG_WARN_RET(tmp_ret, "fail to refresh merge info", KR(tmp_ret));
    }
    if (OB_TMP_FAIL(mgr_.try_update_info())) {
      LOG_WARN_RET(tmp_ret, "fail to try update info", KR(tmp_ret));
    }
  }
}

void ObFreezeInfoMgr::UpdateLSResvSnapshotTask::runTimerTask()
{
  int tmp_ret = OB_SUCCESS;
  compaction::ObBasicMergeScheduler *scheduler = nullptr;
  if (OB_ISNULL(scheduler = compaction::ObBasicMergeScheduler::get_merge_scheduler())) {
    // may be during the start phase
  } else if (OB_TMP_FAIL(mgr_.try_update_reserved_snapshot())) {
    LOG_WARN_RET(tmp_ret, "fail to try reserved snapshot", KR(tmp_ret));
  }
}

int ObFreezeInfoMgr::try_update_reserved_snapshot()
{
  int ret = OB_SUCCESS;
  int64_t duration = 0;
  int64_t reserved_snapshot = 0;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  {
    RLockGuard lock_guard(lock_);

    if (OB_UNLIKELY(!inited_)) {
      ret = OB_NOT_INIT;
      STORAGE_LOG(WARN, "ObFreezeInfoMgr not init", K(ret));
    } else if (OB_FAIL(get_multi_version_duration(duration))) {
      STORAGE_LOG(WARN, "fail to get multi version duration", K(ret));
    } else {
      int64_t snapshot_gc_ts = freeze_info_mgr_.get_snapshot_gc_scn().get_val_for_tx();
      reserved_snapshot = std::max(static_cast<int64_t>(0), snapshot_gc_ts - duration * 1000L * 1000L * 1000L);
      LOG_INFO("success to update min reserved snapshot", K(reserved_snapshot), K(duration), K(snapshot_gc_ts));
    }
  } // end of lock

  // Try to update the reserved snapshot on the log stream.
  ObLS *ls = nullptr;
  if (OB_FAIL(ret) || reserved_snapshot <= 0) {
  } else if (OB_ISNULL(share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "ls service is null", K(ret));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get single log stream", K(ret));
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(ls->try_sync_reserved_snapshot(reserved_snapshot, true/*update_flag*/))) {
      LOG_WARN("failed to update min reserved snapshot", K(tmp_ret), KPC(ls), K(reserved_snapshot));
    }
  }
  cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
  STORAGE_LOG(INFO, "update reserved snapshot finished", K(cost_ts), K(reserved_snapshot));
  return ret;
}
} // storage
} // oceanbase
