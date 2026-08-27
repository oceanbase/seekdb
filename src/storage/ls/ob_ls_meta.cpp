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
#include "ob_ls_meta.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace logservice;
using namespace share;
using namespace transaction;
namespace storage
{
typedef ObFunction<int(const int64_t, const ObLSMeta &)> WriteSlog;
WriteSlog ObLSMeta::write_slog_ = [](const int64_t ls_epoch, const ObLSMeta &ls_meta) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.update_ls_meta(ls_epoch, ls_meta))) {
  }
  return ret;
};

ObLSMeta::ObLSMeta()
  : rw_lock_(),
    update_lock_(),
    ls_persistent_state_(),
    clog_checkpoint_scn_(ObScnRange::MIN_SCN),
    clog_base_lsn_(PALF_INITIAL_LSN_VAL),
    restore_status_(ObRestoreStatus::Status::RESTORE_STATUS_MAX),
    replayable_point_(),
    tablet_change_checkpoint_scn_(SCN::min_scn()),
    all_id_meta_(),
    saved_info_()
{
}

ObLSMeta::ObLSMeta(const ObLSMeta &ls_meta)
  : rw_lock_(),
    update_lock_(),
    ls_persistent_state_(ls_meta.ls_persistent_state_),
    clog_checkpoint_scn_(ls_meta.clog_checkpoint_scn_),
    clog_base_lsn_(ls_meta.clog_base_lsn_),
    restore_status_(ls_meta.restore_status_),
    replayable_point_(ls_meta.replayable_point_),
    tablet_change_checkpoint_scn_(ls_meta.tablet_change_checkpoint_scn_),
    saved_info_(ls_meta.saved_info_)
{
  int ret = OB_SUCCESS;
  all_id_meta_.update_all_id_meta(ls_meta.all_id_meta_);
}

int ObLSMeta::set_start_work_state()
{
  ObReentrantWLockGuard update_guard(update_lock_);
  ObReentrantWLockGuard guard(rw_lock_);
  return ls_persistent_state_.start_work();
}

int ObLSMeta::set_start_restore_state()
{
  ObReentrantWLockGuard update_guard(update_lock_);
  ObReentrantWLockGuard guard(rw_lock_);
  return ls_persistent_state_.start_restore();
}

int ObLSMeta::set_remove_state()
{
  ObReentrantWLockGuard update_guard(update_lock_);
  ObReentrantWLockGuard guard(rw_lock_);
  return ls_persistent_state_.remove();
}

const ObLSPersistentState &ObLSMeta::get_persistent_state() const
{
  return ls_persistent_state_;
}

ObLSMeta &ObLSMeta::operator=(const ObLSMeta &other)
{
  ObReentrantWLockGuard update_guard_myself(update_lock_);
  ObReentrantRLockGuard guard(other.rw_lock_);
  ObReentrantWLockGuard guard_myself(rw_lock_);
  if (this != &other) {

    ls_persistent_state_ = other.ls_persistent_state_;
    clog_base_lsn_ = other.clog_base_lsn_;
    clog_checkpoint_scn_ = other.clog_checkpoint_scn_;
    restore_status_ = other.restore_status_;
    replayable_point_ = other.replayable_point_;
    tablet_change_checkpoint_scn_ = other.tablet_change_checkpoint_scn_;
    all_id_meta_.update_all_id_meta(other.all_id_meta_);
    saved_info_ = other.saved_info_;
  }
  return *this;
}

void ObLSMeta::reset()
{
  ObReentrantWLockGuard update_guard(update_lock_);
  ObReentrantWLockGuard guard(rw_lock_);

  clog_base_lsn_.reset();
  clog_checkpoint_scn_ = ObScnRange::MIN_SCN;
  restore_status_ = ObRestoreStatus::Status::RESTORE_STATUS_MAX;
  replayable_point_.reset();
  tablet_change_checkpoint_scn_ = SCN::min_scn();
  saved_info_.reset();
}

LSN ObLSMeta::get_clog_base_lsn() const
{
  ObReentrantRLockGuard guard(rw_lock_);
  return clog_base_lsn_;
}

SCN ObLSMeta::get_clog_checkpoint_scn() const
{
  ObReentrantRLockGuard guard(rw_lock_);
 	return clog_checkpoint_scn_;
}

int ObLSMeta::set_clog_checkpoint(const int64_t ls_epoch,
                                  const LSN &clog_checkpoint_lsn,
                                  const SCN &clog_checkpoint_scn,
                                  const bool write_slog)
{
  int ret = OB_SUCCESS;
  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
  } else {
    ObLSMeta tmp(*this);
    tmp.clog_base_lsn_ = clog_checkpoint_lsn;
    tmp.clog_checkpoint_scn_ = clog_checkpoint_scn;

    if (write_slog) {
      if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
      }
    }

    ObReentrantWLockGuard guard(rw_lock_);
    clog_base_lsn_ = clog_checkpoint_lsn;
    clog_checkpoint_scn_ = clog_checkpoint_scn;
  }

  return ret;
}

SCN ObLSMeta::get_tablet_change_checkpoint_scn() const
{
 	return tablet_change_checkpoint_scn_;
}

int ObLSMeta::set_tablet_change_checkpoint_scn(
    const int64_t ls_epoch, const SCN &tablet_change_checkpoint_scn)
{
  ObReentrantWLockGuard update_guard(update_lock_);
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_can_update_())) {
  } else if (tablet_change_checkpoint_scn_ > tablet_change_checkpoint_scn) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet_change_checkpoint_ts is small", KR(ret), K(tablet_change_checkpoint_scn),
             K_(tablet_change_checkpoint_scn));
  } else {
    ObLSMeta tmp(*this);
    tmp.tablet_change_checkpoint_scn_ = tablet_change_checkpoint_scn;

    if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
    } else {
      ObReentrantWLockGuard guard(rw_lock_);
      LOG_INFO("update tablet change checkpoint scn",
          "old_scn", tablet_change_checkpoint_scn_, "new_scn", tablet_change_checkpoint_scn);
      tablet_change_checkpoint_scn_ = tablet_change_checkpoint_scn;
    }
  }

  return ret;
}

bool ObLSMeta::is_valid() const
{
  return restore_status_.is_valid();
}

int ObLSMeta::set_restore_status(const int64_t ls_epoch, const ObRestoreStatus &restore_status)
{
  int ret = OB_SUCCESS;
  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
  } else if (!restore_status.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore status", K(ret), K(restore_status_), K(restore_status));
  } else if (restore_status_ == restore_status) {
    //do nothing
  } else {
    ObLSMeta tmp(*this);
    tmp.restore_status_ = restore_status;
    if (restore_status.is_none() && !tmp.ls_persistent_state_.is_normal_state()
        && OB_FAIL(tmp.ls_persistent_state_.finish_restore())) {
      LOG_WARN("failed to switch tmp ls meta to finish restore state", KR(ret), K(tmp));
    } else if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
    } else {
      ObReentrantWLockGuard guard(rw_lock_);
      ls_persistent_state_ = tmp.ls_persistent_state_;
      ObRestoreStatus original_status = restore_status_;
      restore_status_ = restore_status;
      FLOG_INFO("succeed to set ls restore status", "original status",
                original_status, "current status", restore_status);
    }
  }
  return ret;
}

int ObLSMeta::get_restore_status(ObRestoreStatus &restore_status) const
{
  int ret = OB_SUCCESS;
  ObReentrantRLockGuard guard(rw_lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("log stream meta is not valid, cannot get restore status", K(ret), K(*this));
  } else {
    restore_status = restore_status_;
  }
  return ret;
}

int ObLSMeta::update_ls_replayable_point(const int64_t ls_epoch, const SCN &replayable_point)
{
  int ret = OB_SUCCESS;
  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
  } else if (!replayable_point.is_valid()
             || (replayable_point_.is_valid() && replayable_point < replayable_point_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("replayable_point invalid", K(ret), K(replayable_point), K(replayable_point_));
  } else if (replayable_point_ == replayable_point) {
    // do nothing
  } else {
    ObLSMeta tmp(*this);
    tmp.replayable_point_ = replayable_point;
    if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
    } else {
      ObReentrantWLockGuard guard(rw_lock_);
      replayable_point_ = replayable_point;
    }
  }
  return ret;
}

int ObLSMeta::get_ls_replayable_point(SCN &replayable_point)
{
  int ret = OB_SUCCESS;
  ObReentrantRLockGuard guard(rw_lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get_gc_state", K(ret), K(*this));
  } else {
    replayable_point = replayable_point_;
  }
  return ret;
}

int ObLSMeta::get_saved_info(ObLSSavedInfo &saved_info)
{
  int ret = OB_SUCCESS;
  ObReentrantRLockGuard guard(rw_lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get_offline_ts_ns", K(ret), K(*this));
  } else {
    saved_info = saved_info_;
  }
  return ret;
}

int ObLSMeta::update_for_physical_restore(
    const int64_t ls_epoch,
    const ObLSMeta &source_meta)
{
  int ret = OB_SUCCESS;
  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
    LOG_WARN("ls meta cannot update for physical restore", K(ret), K(*this));
  } else if (!source_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid source ls meta for physical restore", K(ret), K(source_meta));
  } else {
    ObLSMeta tmp(*this);
    tmp.replayable_point_ = source_meta.replayable_point_;
    tmp.tablet_change_checkpoint_scn_ = source_meta.tablet_change_checkpoint_scn_;
    tmp.all_id_meta_.update_all_id_meta(source_meta.all_id_meta_);
    if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
      LOG_WARN("failed to persist physical restore ls meta", K(ret), K(ls_epoch), K(tmp));
    } else {
      ObReentrantWLockGuard guard(rw_lock_);
      replayable_point_ = tmp.replayable_point_;
      tablet_change_checkpoint_scn_ = tmp.tablet_change_checkpoint_scn_;
      all_id_meta_.update_all_id_meta(tmp.all_id_meta_);
      LOG_INFO("updated ls meta for physical restore",
          K(ls_epoch), K(source_meta), "local_meta", *this);
    }
  }
  return ret;
}


int ObLSMeta::build_saved_info(const int64_t ls_epoch)
{
  int ret = OB_SUCCESS;
  ObLSSavedInfo saved_info;

  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
  } else if (!saved_info_.is_empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("saved info is not empty, can not build saved info", K(ret), K(*this));
  } else {
    saved_info.clog_checkpoint_scn_ = clog_checkpoint_scn_;
    saved_info.clog_base_lsn_ = clog_base_lsn_;
    saved_info.tablet_change_checkpoint_scn_ = tablet_change_checkpoint_scn_;
    ObLSMeta tmp(*this);
    tmp.saved_info_ = saved_info;
    if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
    } else {
      ObReentrantWLockGuard guard(rw_lock_);
      saved_info_ = saved_info;
    }
  }
  return ret;
}

int ObLSMeta::clear_saved_info(const int64_t ls_epoch)
{
  int ret = OB_SUCCESS;
  ObLSSavedInfo saved_info;

  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
  } else {
    saved_info.reset();
    ObLSMeta tmp(*this);
    tmp.saved_info_ = saved_info;
    if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
    } else {
      ObReentrantWLockGuard guard(rw_lock_);
      saved_info_ = saved_info;
    }
  }
  return ret;
}

int ObLSMeta::init(
    const ObRestoreStatus &restore_status,
    const SCN &create_scn,
    const palf::LSN &clog_base_lsn)
{
  int ret = OB_SUCCESS;
  if (!restore_status.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init ls meta get invalid argument", K(ret), K(restore_status));
  } else {
    ls_persistent_state_ = ObLSPersistentState::State::LS_INIT;
    clog_checkpoint_scn_ = create_scn;
    clog_base_lsn_ = clog_base_lsn;
    restore_status_ = restore_status;
  }
  return ret;
}

int ObLSMeta::update_id_meta(const int64_t ls_epoch,
                             const int64_t service_type,
                             const int64_t limited_id,
                             const SCN &latest_scn,
                             const bool write_slog)
{
  int ret = OB_SUCCESS;

  ObReentrantWLockGuard update_guard(update_lock_);
  if (OB_FAIL(check_can_update_())) {
  } else {
    // TODO: write slog may failed, but the content is updated.
    ObLSMeta tmp(*this);
    tmp.all_id_meta_.update_id_meta(service_type, limited_id, latest_scn);
    update_guard.click();
    if (write_slog) {
      if (OB_FAIL(write_slog_(ls_epoch, tmp))) {
      }
    }
    ObReentrantWLockGuard guard(rw_lock_);
    update_guard.click();
    all_id_meta_.update_id_meta(service_type, limited_id, latest_scn);
  }
  LOG_INFO("update id meta", K(ret), K(service_type), K(limited_id), K(latest_scn),
           K(*this));

  return ret;
}

int ObLSMeta::get_all_id_meta(ObAllIDMeta &all_id_meta) const
{
  int ret = OB_SUCCESS;

  ObReentrantRLockGuard guard(rw_lock_);
  all_id_meta.update_all_id_meta(all_id_meta_);
  return ret;
}

int ObLSMeta::check_can_update_()
{
  int ret = OB_SUCCESS;
  if (!ls_persistent_state_.can_update_ls_meta()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("state not match, cannot update ls meta", K(ret), KPC(this));
  } else if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls meta is not valid, cannot update", K(ret), K(*this));
  } else {
  }
  return ret;
}

int ObLSMeta::check_ls_need_online(bool &need_online) const
{
  int ret = OB_SUCCESS;
  need_online = true;
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid", K(ret), K(*this));
  } else if (!restore_status_.need_online()) {
    need_online = false;
  }
  return ret;
}

ObLSMeta::ObReentrantWLockGuard::ObReentrantWLockGuard(ObLatch &lock,
                                                       const bool try_lock,
                                                       const int64_t warn_threshold)
  : first_locked_(false),
    time_guard_("ls_meta", warn_threshold),
    lock_(lock),
    ret_(OB_SUCCESS)
{
  if (lock_.is_wrlocked_by()) {
    // I have locked with W, do nothing
  } else if (try_lock) {
    if (OB_UNLIKELY(OB_SUCCESS !=
                    (ret_ = lock_.try_wrlock(ObLatchIds::LS_META_LOCK)))) {
    } else {
      first_locked_ = true;
    }
  } else {
    if (OB_UNLIKELY(OB_SUCCESS !=
                    (ret_ = lock_.wrlock(ObLatchIds::LS_META_LOCK)))) {
      LOG_ERROR_RET(ret_, "Fail to lock");
    } else {
      first_locked_ = true;
    }
  }

  time_guard_.click("after lock");
}

ObLSMeta::ObReentrantWLockGuard::~ObReentrantWLockGuard()
{
  if (OB_LIKELY(OB_SUCCESS == ret_) && first_locked_) {
    if (OB_UNLIKELY(OB_SUCCESS != (ret_ = lock_.unlock()))) {
      LOG_ERROR_RET(ret_, "Fail to unlock");
    }
  }
}

ObLSMeta::ObReentrantRLockGuard::ObReentrantRLockGuard(ObLatch &lock,
                                                       const bool try_lock,
                                                       const int64_t warn_threshold)
  : first_locked_(false),
    time_guard_("ls_meta", warn_threshold),
    lock_(lock),
    ret_(OB_SUCCESS)
{
  if (lock_.is_wrlocked_by()) {
    // I have locked with W, do nothing
  } else if (try_lock) {
    if (OB_UNLIKELY(OB_SUCCESS !=
                    (ret_ = lock_.try_rdlock(ObLatchIds::LS_META_LOCK)))) {
    } else {
      first_locked_ = true;
    }
  } else {
    if (OB_UNLIKELY(OB_SUCCESS !=
                    (ret_ = lock_.rdlock(ObLatchIds::LS_META_LOCK)))) {
      LOG_ERROR_RET(ret_, "Fail to lock");
    } else {
      first_locked_ = true;
    }
  }

  time_guard_.click("after lock");
}

ObLSMeta::ObReentrantRLockGuard::~ObReentrantRLockGuard()
{
  if (OB_LIKELY(OB_SUCCESS == ret_) && first_locked_) {
    if (OB_UNLIKELY(OB_SUCCESS != (ret_ = lock_.unlock()))) {
      LOG_ERROR_RET(ret_, "Fail to unlock");
    }
  }
}

OB_SERIALIZE_MEMBER(ObLSMeta,
                    ls_persistent_state_,   // FARM COMPAT WHITELIST
                    clog_checkpoint_scn_,
                    clog_base_lsn_,
                    restore_status_,
                    replayable_point_,
                    tablet_change_checkpoint_scn_,
                    all_id_meta_,
                    saved_info_);

}
}
