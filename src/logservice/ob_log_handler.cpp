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

#define USING_LOG_PREFIX PALF
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
using namespace palf;

ObLogHandler::ObLogHandler() : self_(),
                               lock_(),
                               palf_handle_(),
                               palf_env_(NULL),
                               is_in_stop_state_(true),
                               is_inited_(false),
                               apply_status_(NULL),
                               apply_service_(NULL),
                               replay_service_(NULL),
                               deps_lock_(),
                               append_cost_stat_("[PALF STAT APPEND COST TIME]", 1 * 1000 * 1000),
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
                       ObLogReplayService *replay_service,
                       PalfEnv *palf_env)
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(palf_env) ||
             OB_ISNULL(apply_service)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", KP(palf_env));
  } else if (OB_FAIL(apply_service->get_apply_status(guard))) {
  } else if (NULL == (apply_status_ = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(palf_env->open(palf_handle_))) {
  } else {
    get_max_decided_scn_debug_time_ = OB_INVALID_TIMESTAMP;
    apply_service_ = apply_service;
    replay_service_ = replay_service;
    apply_status_->inc_ref();
    append_cost_stat_.set_extra_info("");
    local_append_enabled_.store(false, std::memory_order_release);
    self_ = self;
    palf_env_ = palf_env;
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
         true == palf_handle_.is_valid() &&
         NULL != palf_env_ &&
         NULL != apply_status_ &&
         NULL != apply_service_ &&
         NULL != replay_service_;
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
    } else if (false == palf_handle_.is_valid()) {
    } else {
      tg.click("apply stop end");
      palf_env_->close(palf_handle_);
      tg.click("palf close end");
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
  replay_service_ = NULL;
  if (NULL != palf_env_ && true == palf_handle_.is_valid()) {
    palf_env_->close(palf_handle_);
  }
  palf_env_ = NULL;
  get_max_decided_scn_debug_time_ = OB_INVALID_TIMESTAMP;
}

int ObLogHandler::append(const void *buffer,
                         const int64_t nbytes,
                         const SCN &ref_scn,
                         const bool need_nonblock,
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
  } else if (nbytes > MAX_NORMAL_LOG_BODY_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "nbytes is greater than expected size", K(nbytes), K(MAX_NORMAL_LOG_BODY_SIZE));
  } else if (OB_FAIL(append_(buffer, nbytes, ref_scn, need_nonblock, cb, lsn, scn))) {
  }
  return ret;
}

int ObLogHandler::append_big_log(const void *buffer,
                                 const int64_t nbytes,
                                 const SCN &ref_scn,
                                 const bool need_nonblock,
                                 AppendCb *cb,
                                 LSN &lsn,
                                 SCN &scn)
{
  int ret = OB_SUCCESS;
  if (!local_append_enabled_.load(std::memory_order_acquire)) {
    ret = OB_NOT_MASTER;
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      CLOG_LOG(INFO, "local big-log append is disabled", K(ret), K(nbytes), K(ref_scn));
    }
  } else if (nbytes <= MAX_NORMAL_LOG_BODY_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "nbytes is smaller than expected size", K(nbytes), K(MAX_NORMAL_LOG_BODY_SIZE));
  } else if (OB_FAIL(append_(buffer, nbytes, ref_scn, need_nonblock, cb, lsn, scn))) {
  }
  return ret;
}

int ObLogHandler::append_imported_group(const palf::LSN &source_lsn,
                                        const SCN &source_scn,
                                        const void *buffer,
                                        const int64_t nbytes)
{
  int ret = OB_SUCCESS;
  if (!source_lsn.is_valid() || !source_scn.is_valid()
      || OB_ISNULL(buffer) || nbytes <= 0
      || nbytes > palf::MAX_LOG_BUFFER_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid imported group", K(ret), K(source_lsn), K(source_scn),
        KP(buffer), K(nbytes));
  } else {
    RLockGuard guard(lock_);
    CriticalGuard(ls_qs_);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
    } else if (is_in_stop_state_ || is_offline_) {
      ret = OB_NOT_RUNNING;
    } else if (OB_FAIL(palf_handle_.append_imported_group(
        source_lsn, source_scn, buffer, nbytes))) {
      if (OB_EAGAIN != ret) {
        CLOG_LOG(WARN, "appending imported group failed", K(ret), K(source_lsn),
            K(source_scn), K(nbytes));
      }
    }
  }
  return ret;
}

int ObLogHandler::append_owned(palf::PalfLogBuffer &buffer,
                               const SCN &ref_scn,
                               const bool need_nonblock,
                               AppendCb *cb,
                               LSN &lsn,
                               SCN &scn)
{
  int ret = OB_SUCCESS;
  if (!local_append_enabled_.load(std::memory_order_acquire)) {
    ret = OB_NOT_MASTER;
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      CLOG_LOG(INFO, "local owned-buffer append is disabled", K(ret),
          "buffer_size", buffer.get_size(), K(ref_scn));
    }
  } else if (!buffer.is_valid() || !buffer.is_sealed() || buffer.get_size() <= 0
      || buffer.get_size() > MAX_LOG_BODY_SIZE || NULL == cb || !ref_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(append_owned_(buffer, ref_scn, need_nonblock, cb, lsn, scn))) {
    if (OB_EAGAIN != ret) {
      CLOG_LOG(WARN, "appending owned log fails", K(ret), K(buffer), K(ref_scn));
    }
  }
  return ret;
}

int ObLogHandler::get_append_mode_initial_scn(share::SCN &ref_scn) const
{
  int ret = OB_SUCCESS;
  AccessMode access_mode = AccessMode::INVALID_ACCESS_MODE;
  share::SCN curr_ref_scn;
  ref_scn.reset();
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(palf_handle_.get_access_mode_ref_scn(access_mode, curr_ref_scn))) {
  } else if (AccessMode::APPEND == access_mode) {
    ref_scn = curr_ref_scn;
  } else {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

int ObLogHandler::seek(const LSN &lsn, PalfBufferIterator &iter)
{
  int ret = OB_SUCCESS;
  constexpr int64_t default_suggested_max_read_buf_size = PALF_BLOCK_SIZE;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(seek_log_iterator_dispatch_(lsn, default_suggested_max_read_buf_size, iter))) {
  } else {
  }
  return ret;
}

int ObLogHandler::seek(const LSN &lsn, PalfGroupBufferIterator &iter)
{
  int ret = OB_SUCCESS;
  constexpr int64_t default_suggested_max_read_buf_size = PALF_BLOCK_SIZE;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(seek_log_iterator_dispatch_(lsn, default_suggested_max_read_buf_size, iter))) {
  } else {
  }
  return ret;
}

int ObLogHandler::bootstrap()
{
  RLockGuard guard(lock_);
  int ret = palf_handle_.bootstrap();
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int ObLogHandler::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (OB_FAIL(palf_handle_.locate_by_scn_coarsely(scn, result_lsn))) {
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
  } else if (OB_FAIL(palf_handle_.locate_by_lsn_coarsely(lsn, result_scn))) {
  }
  return ret;
}

int ObLogHandler::advance_base_lsn(const LSN &lsn)
{
  return advance_base_lsn_impl_(lsn);
}

int ObLogHandler::get_begin_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  ret = palf_handle_.get_begin_lsn(lsn);
  return ret;
}

int ObLogHandler::get_end_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  return palf_handle_.get_end_lsn(lsn);
}

int ObLogHandler::get_max_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  return palf_handle_.get_max_lsn(lsn);
}

int ObLogHandler::get_max_scn(SCN &scn) const
{
  RLockGuard guard(lock_);
  return palf_handle_.get_max_scn(scn);
}

int ObLogHandler::get_end_scn(SCN &scn) const
{
  RLockGuard guard(lock_);
  return palf_handle_.get_end_scn(scn);
}

int ObLogHandler::get_palf_base_info(const LSN &base_lsn, PalfBaseInfo &palf_base_info)
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
  } else if (FALSE_IT(new_base_lsn.val_ = lsn_2_block(base_lsn, PALF_BLOCK_SIZE) * PALF_BLOCK_SIZE)) {
  } else {
    ret = palf_handle_.get_base_info(new_base_lsn, palf_base_info);
    CLOG_LOG(INFO, "get_palf_base_info finish", KR(ret), K(base_lsn), K(new_base_lsn), K(palf_base_info));
  }
  return ret;
}

int ObLogHandler::append_(const void *buffer,
                          const int64_t nbytes,
                          const share::SCN &ref_scn,
                          const bool need_nonblock,
                          AppendCb *cb,
                          palf::LSN &lsn,
                          share::SCN &scn)
{
  int ret = OB_SUCCESS;
  palf::PalfLogBuffer owned_buffer;
  if (NULL == buffer || nbytes <= 0 || nbytes > MAX_LOG_BODY_SIZE
      || NULL == cb || !ref_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(owned_buffer.copy_from(static_cast<const char *>(buffer), nbytes,
                                            palf::LogEntryHeader::HEADER_SER_SIZE))) {
    CLOG_LOG(WARN, "copy legacy log into owned buffer failed", K(ret), K(nbytes));
  } else {
    ret = append_owned_(owned_buffer, ref_scn, need_nonblock, cb, lsn, scn);
  }
  return ret;
}

int ObLogHandler::append_owned_(palf::PalfLogBuffer &buffer,
                                const share::SCN &ref_scn,
                                const bool need_nonblock,
                                AppendCb *cb,
                                palf::LSN &lsn,
                                share::SCN &scn)
{
  int ret = OB_SUCCESS;
  int64_t wait_times = 0;
  PalfAppendOptions opts;
  opts.need_nonblock = need_nonblock;
  ObTimeGuard tg("ObLogHandler::append", 100000);
  while (true) {
    do {
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
        // already passed the entry check still commits via palf_handle_,
        // advancing end_scn past the cutover_scn captured by
        // prepare_to_standby and tripping the durably-fenced promotion check.
        ret = OB_NOT_MASTER;
        if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
          CLOG_LOG(INFO, "local append is disabled (checked in critical section)",
              K(ret), "buffer_size", buffer.get_size(), K(ref_scn));
        }
      } else if (OB_FAIL(palf_handle_.append(opts, buffer, ref_scn, lsn, scn))) {
        if (REACH_TIME_INTERVAL(1*1000*1000)) {
          CLOG_LOG(WARN, "palf_handle_ append failed", K(ret), KPC(this));
        }
      } else {
        cb->set_append_finish_ts(ObTimeUtility::fast_current_time());
        cb->__set_lsn(lsn);
        cb->__set_scn(scn);
        ret = apply_status_->push_append_cb(cb);
      }
    } while (0);
    // check if need wait and retry append
    if (opts.need_nonblock) {
      // nonblock mode, end loop
      break;
    } else if (OB_EAGAIN == ret && buffer.is_valid()) {
      // block mode, need sleep and retry for -4023 ret code
      static const int64_t MAX_SLEEP_US = 100;
      ++wait_times;
      int64_t sleep_us = wait_times * 10;
      if (sleep_us > MAX_SLEEP_US) {
        sleep_us = MAX_SLEEP_US;
      }
      ob_usleep(sleep_us);
    } else {
      // Other errors, or a defensive post-consumption error, end the loop.
      // A consumed buffer must never be submitted again.
      break;
    }
  }

  append_cost_stat_.stat(tg.get_diff());
  return ret;
}

void ObLogHandler::wait_append_sync() {
  WaitQuiescent(ls_qs_);
}

int ObLogHandler::enable_replay(const palf::LSN &lsn,
                                const SCN &scn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!lsn.is_valid() || !scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(ret), K(lsn), K(scn));
  } else if (OB_FAIL(replay_service_->enable(lsn, scn))) {
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
  } else if (OB_FAIL(replay_service_->disable())) {
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
  } else if (OB_FAIL(replay_service_->block_submit_log())) {
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
  } else if (OB_FAIL(replay_service_->unblock_submit_log())) {
  } else {
    CLOG_LOG(INFO, "unblock_submit_log success", K(ret));
  }
  return ret;
}

bool ObLogHandler::is_replay_enabled() const
{
  bool bool_ret = false;
  int tmp_ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
  } else if (OB_SUCCESS != (tmp_ret = replay_service_->is_enabled(bool_ret))) {
    CLOG_LOG_RET(WARN, tmp_ret, "check replay service is enabled failed", K(tmp_ret));
  } else {
    // do nothing
  }
  return bool_ret;
}

int ObLogHandler::get_max_decided_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  SCN max_replayed_scn;
  SCN max_applied_scn;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_) {
    // and replay service return 4109 uniformly
    ret = OB_STATE_NOT_MATCH;
  } else if (is_offline()) {
    ret = OB_STATE_NOT_MATCH;
    CLOG_LOG(WARN, "log handle is offline");
  } else if (OB_FAIL(apply_service_->get_max_applied_scn(max_applied_scn))) {
  } else if (OB_FAIL(replay_service_->get_max_replayed_scn(max_replayed_scn))) {
    if (OB_STATE_NOT_MATCH != ret) {
      CLOG_LOG(WARN, "failed to get_max_replayed_scn", K(ret));
    } else if (palf_reach_time_interval(1000 * 1000, get_max_decided_scn_debug_time_)) {
      CLOG_LOG(ERROR, "failed to get_max_replayed_scn, replay status is not enabled", K(ret));
    }
    if (OB_STATE_NOT_MATCH == ret && max_applied_scn.is_valid()) {
      //Replay is not enabled, but the maximum consecutive callback point obtained in the apply service is valid
      ret = OB_SUCCESS;
      scn = max_applied_scn > SCN::min_scn() ? max_applied_scn : SCN::min_scn();
      if (palf_reach_time_interval(1000 * 1000, get_max_decided_scn_debug_time_)) {
        CLOG_LOG(INFO, "replay is not enabled, get_max_decided_scn from apply", K(ret),
                K(max_replayed_scn), K(max_applied_scn), K(scn));
      }
    }
  } else {
    scn = std::max(max_replayed_scn, max_applied_scn) > SCN::min_scn() ?
             std::max(max_replayed_scn, max_applied_scn) : SCN::min_scn();
  }
  return ret;
}

// reentrant
int ObLogHandler::offline()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    PALF_LOG(INFO, "ObLogHandler has already been destroyed", K(ret), KPC(this));
  } else if (OB_FAIL(disable_replay())) {
  } else {
    WLockGuard guard(lock_);
    is_offline_ = true;
    CLOG_LOG(INFO, "LogHandler offline success", K(ret), KPC(this));
  }
  return ret;
}

int ObLogHandler::diagnose_palf(palf::PalfDiagnoseInfo &diagnose_info) const
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(palf_handle_.diagnose(diagnose_info))) {
  } else {
    // do nothing
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
    //handled after offline which may refer to palf_committed_end_lsn_
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
    if (OB_FAIL(replay_service_->has_fatal_error(has_fatal_error))) {
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
  } else if (OB_FAIL(palf_handle_.advance_base_lsn(lsn))) {
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
