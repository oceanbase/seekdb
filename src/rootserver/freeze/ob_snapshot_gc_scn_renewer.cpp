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
#include "rootserver/ob_root_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/compaction/ob_tenant_freeze_info_mgr.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace rootserver
{

ObSnapshotGcScnRenewer::ObSnapshotGcScnRenewer()
  : is_inited_(false),
    is_paused_(false),
    is_primary_service_(true),
    is_primary_active_(false),
    need_primary_catchup_(false),
    next_renew_ts_(0),
    refreshed_scn_(0),
    major_merge_info_mgr_(nullptr),
    role_lock_(common::ObLatchIds::MAJOR_FREEZE_SWITCH_LOCK)
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
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    is_primary_service_ = is_primary_service;
    ATOMIC_STORE(&is_paused_, false);
    ATOMIC_STORE(&is_primary_active_, false);
    ATOMIC_STORE(&need_primary_catchup_, false);
    ATOMIC_STORE(&next_renew_ts_, 0);
    ATOMIC_STORE(&refreshed_scn_, 0);
    major_merge_info_mgr_ = &major_merge_info_mgr;
    is_inited_ = true;
  }
  return ret;
}

int ObSnapshotGcScnRenewer::destroy()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  ATOMIC_STORE(&is_paused_, false);
  ATOMIC_STORE(&is_primary_active_, false);
  ATOMIC_STORE(&need_primary_catchup_, false);
  ATOMIC_STORE(&next_renew_ts_, 0);
  ATOMIC_STORE(&refreshed_scn_, 0);
  major_merge_info_mgr_ = nullptr;
  is_inited_ = false;
  return OB_SUCCESS;
}

void ObSnapshotGcScnRenewer::pause()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  ATOMIC_STORE(&is_primary_active_, false);
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
  } else if (!is_primary_service()) {
    ret = OB_NOT_SUPPORTED;
  } else if (ATOMIC_LOAD(&is_primary_active_)) {
    LOG_INFO("snapshot gc renewer is already primary active");
  } else {
    ATOMIC_STORE(&need_primary_catchup_, true);
    ATOMIC_STORE(&next_renew_ts_, 0);
    ATOMIC_STORE(&is_primary_active_, true);
    LOG_INFO("snapshot gc renewer becomes primary");
  }
  return ret;
}

int ObSnapshotGcScnRenewer::try_renew()
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  int ret = OB_SUCCESS;
  const int64_t now = ObTimeUtility::current_time();
  storage::ObTenantFreezeInfoMgr *freeze_info_mgr = nullptr;
  int64_t renew_target_scn = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (ATOMIC_LOAD(&is_paused_) || !ATOMIC_LOAD(&is_primary_active_)) {
    // nothing
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(freeze_info_mgr = share::g_mp->tenant_freeze_info_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant freeze info mgr is null", KR(ret));
  } else if (!need_renew(now)) {
    // IDLE or waiting for the next scheduled renewal time.
  } else {
    const bool need_primary_catchup = ATOMIC_LOAD(&need_primary_catchup_);
    SCN new_snapshot_gc_scn;
    ATOMIC_STORE(&next_renew_ts_, now + RENEW_INTERVAL_US);
    if (OB_FAIL(major_merge_info_mgr_->renew_snapshot_gc_scn(new_snapshot_gc_scn))) {
      // Keep ACTIVE and retry after the same fixed interval.
    } else {
      if (need_primary_catchup && ATOMIC_LOAD(&is_primary_active_)) {
        const int64_t primary_history_scn = new_snapshot_gc_scn.get_val_for_tx();
        freeze_info_mgr->get_snapshot_gc_scn_renewal_state()
            .update_target_scn(primary_history_scn);
        ATOMIC_STORE(&next_renew_ts_,
            calc_next_renew_ts_(primary_history_scn, GCONF.undo_retention));
        ATOMIC_STORE(&need_primary_catchup_, false);
      }
      renew_target_scn = freeze_info_mgr->get_snapshot_gc_scn_renewal_state()
          .get_target_scn();
      if (renew_target_scn > ATOMIC_LOAD(&refreshed_scn_)) {
        const int64_t undo_retention_s = GCONF.undo_retention;
        const int64_t gc_boundary = MAX(0,
            new_snapshot_gc_scn.get_val_for_tx()
                - undo_retention_s * 1000L * 1000L * 1000L);
        if (gc_boundary >= renew_target_scn) {
          ATOMIC_STORE(&refreshed_scn_, renew_target_scn);
          ATOMIC_STORE(&next_renew_ts_, 0);
          LOG_INFO("snapshot gc renewal target is refreshed",
              K(new_snapshot_gc_scn), K(gc_boundary), K(renew_target_scn),
              K(undo_retention_s));
        }
      } else {
        ATOMIC_STORE(&next_renew_ts_, 0);
      }
    }
  }
  return ret;
}

bool ObSnapshotGcScnRenewer::need_renew(const int64_t now)
{
  ObRecursiveMutexGuard role_guard(role_lock_);
  storage::ObTenantFreezeInfoMgr *freeze_info_mgr = nullptr;
  int64_t next_renew_ts = ATOMIC_LOAD(&next_renew_ts_);
  bool need_renew = false;
  if (!ATOMIC_LOAD(&is_paused_)
      && is_primary_service()
      && ATOMIC_LOAD(&is_primary_active_)) {
    if (ATOMIC_LOAD(&need_primary_catchup_)) {
      need_renew = next_renew_ts <= 0 || now >= next_renew_ts;
    } else if (OB_NOT_NULL(share::g_mp)
        && OB_NOT_NULL(freeze_info_mgr = share::g_mp->tenant_freeze_info_mgr())) {
      const int64_t renew_target_scn =
          freeze_info_mgr->get_snapshot_gc_scn_renewal_state().get_target_scn();
      if (renew_target_scn > ATOMIC_LOAD(&refreshed_scn_)) {
        if (next_renew_ts <= 0) {
          next_renew_ts = latch_next_renew_ts_(
              renew_target_scn, GCONF.undo_retention);
        }
        need_renew = next_renew_ts > 0 && now >= next_renew_ts;
      } else {
        ATOMIC_STORE(&next_renew_ts_, 0);
      }
    }
  }
  return need_renew;
}

int64_t ObSnapshotGcScnRenewer::calc_next_renew_ts_(
    const int64_t renew_target_scn,
    const int64_t undo_retention_s)
{
  static const int64_t NS_PER_US = 1000L;
  static const int64_t US_PER_SECOND = 1000L * 1000L;
  int64_t next_renew_ts = 0;
  if (renew_target_scn > 0
      && INT64_MAX != renew_target_scn
      && undo_retention_s >= 0) {
    const int64_t history_ts_us = renew_target_scn / NS_PER_US
        + (renew_target_scn % NS_PER_US > 0 ? 1 : 0);
    next_renew_ts = undo_retention_s
        > (INT64_MAX - history_ts_us) / US_PER_SECOND
        ? INT64_MAX
        : history_ts_us + undo_retention_s * US_PER_SECOND;
  }
  return next_renew_ts;
}

int64_t ObSnapshotGcScnRenewer::latch_next_renew_ts_(
    const int64_t renew_target_scn,
    const int64_t undo_retention_s)
{
  int64_t next_renew_ts = ATOMIC_LOAD(&next_renew_ts_);
  if (next_renew_ts <= 0) {
    const int64_t new_next_renew_ts = calc_next_renew_ts_(
        renew_target_scn, undo_retention_s);
    if (new_next_renew_ts > 0) {
      (void)ATOMIC_BCAS(&next_renew_ts_, 0, new_next_renew_ts);
      next_renew_ts = ATOMIC_LOAD(&next_renew_ts_);
    }
  }
  return next_renew_ts;
}

} // namespace rootserver
} // namespace oceanbase
