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

#define USING_LOG_PREFIX WAL
#include "ob_log_handler.h"
#include "logservice/ob_i_log_storage.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace obcall;
namespace logservice
{
using namespace wal;

ObLogHandler::ObLogHandler() : self_(),
                               lock_(),
                               wal_handle_(),
                               wal_env_(NULL),
                               is_in_stop_state_(true),
                               is_inited_(false),
                               apply_status_(NULL),
                               apply_service_(NULL),
                               log_service_(NULL),
                               deps_lock_(),
                               append_cost_stat_("[WAL STAT APPEND COST TIME]", 1 * 1000 * 1000),
                               local_append_enabled_(false),
                               is_offline_(false),
                               get_max_decided_scn_debug_time_(OB_INVALID_TIMESTAMP)
{
}

ObLogHandler::~ObLogHandler()
{
  destroy();
}

int ObLogHandler::init(const common::ObAddr &self,
                       ObLogApplyService *apply_service,
                       ObLogService *log_service,
                       Env *wal_env)
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(wal_env) ||
             OB_ISNULL(apply_service) || OB_ISNULL(log_service)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", KP(wal_env));
  } else if (OB_FAIL(apply_service->get_apply_status(guard))) {
  } else if (NULL == (apply_status_ = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(wal_env->open(wal_handle_))) {
  } else {
    get_max_decided_scn_debug_time_ = OB_INVALID_TIMESTAMP;
    apply_service_ = apply_service;
    log_service_ = log_service;
    apply_status_->inc_ref();
    append_cost_stat_.set_extra_info("");
    local_append_enabled_.store(false, std::memory_order_release);
    self_ = self;
    wal_env_ = wal_env;
    is_in_stop_state_ = false;
    is_offline_ = true; // offline at default.
    is_inited_ = true;
    FLOG_INFO("ObLogHandler init success");
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

bool ObLogHandler::is_valid() const
{
  return true == is_inited_ &&
         false == is_in_stop_state_ &&
         self_.is_valid() &&
         true == wal_handle_.is_valid() &&
         NULL != wal_env_ &&
         NULL != apply_status_ &&
         NULL != apply_service_ &&
         NULL != log_service_;
}

int ObLogHandler::stop()
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("ObLogHandler::stop", 5 * 1000000);
  WLockGuard guard(lock_);
  tg.click("wrlock succ");
  if (IS_INIT) {
    is_in_stop_state_ = true;
    common::TCWLockGuard deps_guard(deps_lock_);
    //unregister_file_size_cb cannot be inside the apply status lock, it may cause a deadlock
    apply_status_->unregister_file_size_cb();
    tg.click("unreg cb end");
    if (OB_FAIL(apply_status_->stop())) {
    } else if (false == wal_handle_.is_valid()) {
    } else {
      tg.click("apply stop end");
      wal_env_->close(wal_handle_);
      tg.click("wal close end");
    }
    CLOG_LOG(INFO, "stop log handler finish", KPC(this), KPC(apply_status_), KR(ret), K(tg));
  }
  return ret;
}
void ObLogHandler::destroy()
{
  WLockGuard guard(lock_);
  local_append_enabled_.store(false, std::memory_order_release);
  is_inited_ = false;
  is_offline_ = false;
  is_in_stop_state_ = true;
  common::TCWLockGuard deps_guard(deps_lock_);
  if (NULL != apply_service_ && NULL != apply_status_) {
    apply_service_->revert_apply_status(apply_status_);
  }
  apply_status_ = NULL;
  apply_service_ = NULL;
  log_service_ = NULL;
  if (NULL != wal_env_ && true == wal_handle_.is_valid()) {
    wal_env_->close(wal_handle_);
  }
  wal_env_ = NULL;
  get_max_decided_scn_debug_time_ = OB_INVALID_TIMESTAMP;
}

int ObLogHandler::append(const void *buffer,
                         const int64_t nbytes,
                         const SCN &ref_scn,
                         AppendCb *cb,
                         LSN &lsn,
                         SCN &scn)
{
  int ret = OB_SUCCESS;
  if (!local_append_enabled_.load(std::memory_order_acquire)) {
    ret = OB_NOT_MASTER;
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      CLOG_LOG(INFO, "local append is disabled", K(ret), K(nbytes), K(ref_scn));
    }
  } else if (OB_FAIL(append_(buffer, nbytes, ref_scn, cb, lsn, scn))) {
  }
  return ret;
}

int ObLogHandler::append_imported_groups(const wal::ImportedGroups &groups)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  CriticalGuard(ls_qs_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_ || is_offline_) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(wal_handle_.append_imported_groups(groups))) {
    if (OB_EAGAIN != ret) {
      CLOG_LOG(WARN, "appending imported groups failed", K(ret), K(groups.size()));
    }
  }
  return ret;
}

int ObLogHandler::seek(const LSN &lsn, EntryIterator &iter)
{
  int ret = OB_SUCCESS;
  constexpr int64_t default_suggested_max_read_buf_size = WAL_BLOCK_SIZE;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(seek_log_iterator_dispatch_(lsn, default_suggested_max_read_buf_size, iter))) {
  } else {
  }
  return ret;
}

int ObLogHandler::seek(const LSN &lsn, GroupIterator &iter)
{
  int ret = OB_SUCCESS;
  constexpr int64_t default_suggested_max_read_buf_size = WAL_BLOCK_SIZE;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(seek_log_iterator_dispatch_(lsn, default_suggested_max_read_buf_size, iter))) {
  } else {
  }
  return ret;
}

int ObLogHandler::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (OB_FAIL(wal_handle_.locate_by_scn_coarsely(scn, result_lsn))) {
  }

  return ret;
}

int ObLogHandler::locate_by_lsn_coarsely(const LSN &lsn, SCN &result_scn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(wal_handle_.locate_by_lsn_coarsely(lsn, result_scn))) {
  }
  return ret;
}

int ObLogHandler::advance_base_lsn(const LSN &lsn)
{
  return advance_base_lsn_impl_(lsn);
}

int ObLogHandler::get_base_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  return wal_handle_.get_base_lsn(lsn);
}

int ObLogHandler::get_end_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  return wal_handle_.get_end_lsn(lsn);
}

int ObLogHandler::get_end_scn(SCN &scn) const
{
  RLockGuard guard(lock_);
  return wal_handle_.get_end_scn(scn);
}

int ObLogHandler::get_wal_base_info(const LSN &base_lsn, BaseInfo &wal_base_info)
{
  int ret = OB_SUCCESS;
  LSN new_base_lsn;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
  } else if (false == base_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), K(base_lsn), K(lbt()));
  } else if (FALSE_IT(new_base_lsn.val_ = lsn_2_block(base_lsn, WAL_BLOCK_SIZE) * WAL_BLOCK_SIZE)) {
  } else {
    ret = wal_handle_.get_base_info(new_base_lsn, wal_base_info);
    CLOG_LOG(INFO, "get_wal_base_info finish", KR(ret), K(base_lsn), K(new_base_lsn), K(wal_base_info));
  }
  return ret;
}

int ObLogHandler::append_(const void *buffer,
                          const int64_t nbytes,
                          const share::SCN &ref_scn,
                          AppendCb *cb,
                          wal::LSN &lsn,
                          share::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("ObLogHandler::append", 100000);
  {
    RLockGuard guard(lock_);
    CriticalGuard(ls_qs_);
    cb->set_append_start_ts(ObTimeUtility::fast_current_time());
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
    } else if (is_in_stop_state_ || is_offline_) {
      ret = OB_NOT_RUNNING;
    } else if (!local_append_enabled_.load(std::memory_order_acquire)) {
      // Re-check inside the quiescent critical section so that
      // fence_local_append_ (set_local_append_enabled(false) +
      // WaitQuiescent(ls_qs_)) closes the window where a caller that
      // already passed the entry check still commits via wal_handle_,
      // advancing end_scn past the cutover_scn captured by
      // prepare_to_standby and tripping the durably-fenced promotion check.
      ret = OB_NOT_MASTER;
      if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
        CLOG_LOG(INFO, "local append is disabled (checked in critical section)",
            K(ret), K(nbytes), K(ref_scn));
      }
    } else if (OB_FAIL(wal_handle_.append(buffer, nbytes, ref_scn, lsn, scn))) {
      if (REACH_TIME_INTERVAL(1*1000*1000)) {
        CLOG_LOG(WARN, "wal_handle_ append failed", K(ret), KPC(this));
      }
    } else {
      cb->set_append_finish_ts(ObTimeUtility::fast_current_time());
      cb->__set_lsn(lsn);
      cb->__set_scn(scn);
      ret = apply_status_->push_append_cb(cb);
    }
  }

  append_cost_stat_.stat(tg.get_diff());
  return ret;
}

void ObLogHandler::wait_append_sync() {
  WaitQuiescent(ls_qs_);
}

int ObLogHandler::enable_replay(const wal::LSN &lsn,
                                const SCN &scn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!lsn.is_valid() || !scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(ret), K(lsn), K(scn));
  } else if (OB_FAIL(log_service_->enable_replay(lsn, scn))) {
  } else {
    CLOG_LOG(INFO, "enable replay success", K(ret), K(lsn), K(scn));
  }
  return ret;
}

int ObLogHandler::disable_replay()
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(log_service_->disable_replay())) {
  } else {
    CLOG_LOG(INFO, "disable replay success", K(ret));
  }
  return ret;
}

int ObLogHandler::pend_submit_replay_log()
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(log_service_->block_submit_replay())) {
  } else {
    CLOG_LOG(INFO, "block_submit_log success", K(ret));
  }
  return ret;
}

int ObLogHandler::restore_submit_replay_log()
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(log_service_->unblock_submit_replay())) {
  } else {
    CLOG_LOG(INFO, "unblock_submit_log success", K(ret));
  }
  return ret;
}

bool ObLogHandler::is_replay_enabled() const
{
  RLockGuard guard(lock_);
  return is_inited_ && log_service_->is_replay_enabled();
}

int ObLogHandler::get_max_decided_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_ || is_offline()) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    ret = log_service_->get_max_decided_scn(scn);
  }
  return ret;
}

// reentrant
int ObLogHandler::offline()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    WAL_LOG(INFO, "ObLogHandler has already been destroyed", K(ret), KPC(this));
  } else if (OB_FAIL(disable_replay())) {
  } else {
    WLockGuard guard(lock_);
    is_offline_ = true;
    CLOG_LOG(INFO, "LogHandler offline success", K(ret), KPC(this));
  }
  return ret;
}

int ObLogHandler::online(const LSN &lsn, const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (true == is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(enable_replay(lsn, scn))) {
  } else {
    WLockGuard guard(lock_);
    //reset_meta to avoid contributing excessively large max_decided_scn
    //reset_meta is placed here rather than offline() because after offline, callbacks will be
    //handled after offline which may refer to wal_committed_end_lsn_
    apply_status_->reset_meta();
    is_offline_ = false;
    CLOG_LOG(INFO, "LogHander online success", K(ret), KPC(this), K(lsn), K(scn));
  }
  return ret;
}

bool ObLogHandler::is_offline() const
{
  return true == ATOMIC_LOAD(&is_offline_);
}

int ObLogHandler::is_replay_fatal_error(bool &has_fatal_error)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (true == is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
  } else {
    RLockGuard guard(lock_);
    if (OB_FAIL(log_service_->has_replay_fatal_error(has_fatal_error))) {
    }
  }
  return ret;
}

int ObLogHandler::advance_base_lsn_impl_(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
    CLOG_LOG(WARN, "ObLogHandler is not running", KR(ret));
  } else if (OB_FAIL(wal_handle_.advance_base_lsn(lsn))) {
  } else {}
  return ret;
}

int __get_log_handler(
    ObILogStorage &log_storage,
    ObLogHandler *&log_handler)
{
  return log_storage.get_log_handler(log_handler);
}

} // end namespace logservice
} // end napespace oceanbase
