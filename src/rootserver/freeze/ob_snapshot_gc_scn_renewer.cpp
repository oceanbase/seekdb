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

#define USING_LOG_PREFIX RS_COMPACTION

#include "rootserver/freeze/ob_snapshot_gc_scn_renewer.h"

#include "rootserver/freeze/ob_major_merge_info_manager.h"
#include "share/rc/ob_module_provider.h"
#include "storage/compaction/ob_freeze_info_mgr.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace rootserver
{

ObSnapshotGcScnRenewer::ObSnapshotGcScnRenewer()
{
}

ObSnapshotGcScnRenewer::~ObSnapshotGcScnRenewer()
{
  (void)destroy();
}

int ObSnapshotGcScnRenewer::init(
    const bool is_primary_service,
    ObMajorMergeInfoManager &major_merge_info_mgr)
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    is_primary_service_ = is_primary_service;
    ATOMIC_STORE(&is_paused_, false);
    is_primary_active_ = false;
    need_primary_catchup_ = false;
    next_renew_ts_ = 0;
    last_renew_attempt_ts_ = 0;
    last_renewed_snapshot_gc_scn_ = 0;
    major_merge_info_mgr_ = &major_merge_info_mgr;
    is_inited_ = true;
  }
  return ret;
}

int ObSnapshotGcScnRenewer::destroy()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  ATOMIC_STORE(&is_paused_, false);
  is_primary_active_ = false;
  need_primary_catchup_ = false;
  next_renew_ts_ = 0;
  last_renew_attempt_ts_ = 0;
  last_renewed_snapshot_gc_scn_ = 0;
  major_merge_info_mgr_ = nullptr;
  is_inited_ = false;
  return OB_SUCCESS;
}

void ObSnapshotGcScnRenewer::pause()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  is_primary_active_ = false;
  ATOMIC_STORE(&is_paused_, true);
}

void ObSnapshotGcScnRenewer::resume()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  ATOMIC_STORE(&is_paused_, false);
}

int ObSnapshotGcScnRenewer::on_become_primary()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!is_primary_service_) {
    ret = OB_NOT_SUPPORTED;
  } else if (is_primary_active_) {
    LOG_INFO("snapshot gc renewer is already primary active");
  } else {
    need_primary_catchup_ = true;
    next_renew_ts_ = 0;
    last_renew_attempt_ts_ = 0;
    is_primary_active_ = true;
    LOG_INFO("snapshot gc renewer becomes primary");
  }
  return ret;
}

int ObSnapshotGcScnRenewer::try_renew()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  int ret = OB_SUCCESS;
  const int64_t now = ObTimeUtility::current_time();
  storage::ObFreezeInfoMgr *freeze_info_mgr = nullptr;
  int64_t renew_target_scn = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (ATOMIC_LOAD(&is_paused_)
      || !is_primary_service_
      || !is_primary_active_) {
    // nothing
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(freeze_info_mgr = share::g_mp->freeze_info_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("freeze info mgr is null", KR(ret));
  } else if (!need_renew_(now)) {
    // IDLE or waiting for the next scheduled renewal time.
  } else {
    SCN new_snapshot_gc_scn;
    last_renew_attempt_ts_ = now;
    next_renew_ts_ = now + RENEW_INTERVAL_US;
    if (OB_FAIL(major_merge_info_mgr_->renew_snapshot_gc_scn(new_snapshot_gc_scn))) {
      // Keep ACTIVE and retry after the same fixed interval.
    } else {
      last_renewed_snapshot_gc_scn_ = new_snapshot_gc_scn.get_val_for_tx();
      if (need_primary_catchup_) {
        freeze_info_mgr->get_snapshot_gc_scn_renewal_state()
            .update_target_scn(last_renewed_snapshot_gc_scn_);
        need_primary_catchup_ = false;
      }
      renew_target_scn = freeze_info_mgr->get_snapshot_gc_scn_renewal_state()
          .get_target_scn();
      const int64_t undo_retention_s = GCONF.undo_retention;
      const int64_t gc_boundary = calc_gc_boundary_(
          last_renewed_snapshot_gc_scn_, undo_retention_s);
      if (renew_target_scn <= 0 || gc_boundary >= renew_target_scn) {
        next_renew_ts_ = 0;
        LOG_INFO("snapshot gc renewal target is covered",
            K(new_snapshot_gc_scn), K(gc_boundary), K(renew_target_scn),
            K(undo_retention_s));
      } else if (last_renewed_snapshot_gc_scn_ < renew_target_scn) {
        schedule_next_renew_(now, now);
      } else {
        schedule_next_renew_(
            calc_next_renew_ts_(renew_target_scn, undo_retention_s), now);
      }
    }
  }
  return ret;
}

bool ObSnapshotGcScnRenewer::need_renew_(const int64_t now)
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  storage::ObFreezeInfoMgr *freeze_info_mgr = nullptr;
  bool need_renew = false;
  if (!ATOMIC_LOAD(&is_paused_)
      && is_primary_service_
      && is_primary_active_) {
    if (need_primary_catchup_) {
      schedule_next_renew_(now, now);
      need_renew = now >= next_renew_ts_;
    } else if (OB_NOT_NULL(share::g_mp)
        && OB_NOT_NULL(freeze_info_mgr = share::g_mp->freeze_info_mgr())) {
      const int64_t renew_target_scn =
          freeze_info_mgr->get_snapshot_gc_scn_renewal_state().get_target_scn();
      const int64_t gc_boundary = calc_gc_boundary_(
          last_renewed_snapshot_gc_scn_, GCONF.undo_retention);
      if (renew_target_scn <= 0 || gc_boundary >= renew_target_scn) {
        next_renew_ts_ = 0;
      } else if (last_renewed_snapshot_gc_scn_ < renew_target_scn) {
        schedule_next_renew_(now, now);
        need_renew = now >= next_renew_ts_;
      } else {
        schedule_next_renew_(
            calc_next_renew_ts_(renew_target_scn, GCONF.undo_retention), now);
        need_renew = now >= next_renew_ts_;
      }
    }
  }
  return need_renew;
}

int64_t ObSnapshotGcScnRenewer::calc_next_renew_ts_(
    const int64_t renew_target_scn,
    const int64_t undo_retention_s)
{
  int64_t next_renew_ts = 0;
  if (renew_target_scn > 0
      && INT64_MAX != renew_target_scn
      && undo_retention_s >= 0) {
    // renew_target_scn uses nanoseconds, while the timer uses microseconds.
    next_renew_ts = renew_target_scn / 1000L
        + undo_retention_s * 1000L * 1000L;
  }
  return next_renew_ts;
}

int64_t ObSnapshotGcScnRenewer::calc_gc_boundary_(
    const int64_t snapshot_gc_scn,
    const int64_t undo_retention_s)
{
  int64_t gc_boundary = 0;
  if (snapshot_gc_scn > 0 && undo_retention_s >= 0) {
    gc_boundary = MAX(0,
        snapshot_gc_scn - undo_retention_s * 1000L * 1000L * 1000L);
  }
  return gc_boundary;
}

void ObSnapshotGcScnRenewer::schedule_next_renew_(
    const int64_t desired_renew_ts,
    const int64_t now)
{
  const int64_t next_attempt_ts = last_renew_attempt_ts_ > 0
      ? last_renew_attempt_ts_ + RENEW_INTERVAL_US
      : now;
  next_renew_ts_ = MAX(desired_renew_ts, next_attempt_ts);
}

} // namespace rootserver
} // namespace oceanbase
