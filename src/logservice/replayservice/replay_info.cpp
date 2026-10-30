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

#include "replay_info.h"

namespace oceanbase
{
namespace logservice
{
using namespace common;
using namespace share;
using namespace palf;

ReplayInfo::ReplayInfo()
  : enabled_(false), finished_(false), next_to_submit_lsn_(), next_to_submit_scn_(),
    base_lsn_(), base_scn_(), queue_heads_(), error_(),
    replayable_point_(SCN::min_scn()), pending_size_(0), pending_count_(0)
{
}

void ReplayInfo::init_progress(const LSN &lsn, const SCN &scn)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  enabled_ = false;
  finished_ = false;
  next_to_submit_lsn_ = base_lsn_ = lsn;
  next_to_submit_scn_ = SCN::min_scn();
  base_scn_ = scn;
  for (auto &head : queue_heads_) {
    head = LogInfo();
  }
  error_ = ErrorInfo();
  replayable_point_.atomic_store(SCN::min_scn());
}

bool ReplayInfo::is_enabled() const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  return enabled_;
}

void ReplayInfo::set_enabled(const bool enabled)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  enabled_ = enabled;
}

void ReplayInfo::finish()
{
  ObLockGuard<ObSpinLock> guard(lock_);
  finished_ = true;
}

void ReplayInfo::get_next_to_submit_log_info(LSN &lsn, SCN &scn) const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  lsn = next_to_submit_lsn_;
  scn = next_to_submit_scn_;
}

LSN ReplayInfo::get_base_lsn() const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  return base_lsn_;
}

SCN ReplayInfo::get_base_scn() const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  return base_scn_;
}

int ReplayInfo::update_submit_log_meta_info(const LSN &lsn, const SCN &scn)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  int ret = OB_SUCCESS;
  if (lsn <= next_to_submit_lsn_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // Publish the cursor only after the corresponding task's queue head.
    next_to_submit_lsn_ = lsn;
    if (scn < base_scn_) {
      // A block-aligned base may contain prefix logs covered by the checkpoint.
    } else if (SCN::scn_inc(scn) <= next_to_submit_scn_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      next_to_submit_scn_ = SCN::scn_inc(scn);
    }
  }
  return ret;
}

int ReplayInfo::update_submit_scn(const SCN &scn)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  int ret = OB_SUCCESS;
  if (scn < next_to_submit_scn_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    next_to_submit_scn_ = scn;
  }
  return ret;
}

void ReplayInfo::set_queue_head(const int64_t index, const LogInfo &head)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  queue_heads_[index] = head;
}

void ReplayInfo::set_queue_first_handle_ts(const int64_t index, const int64_t timestamp)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  queue_heads_[index].first_handle_ts_ = timestamp;
}

ReplayInfo::LogInfo ReplayInfo::get_min_unreplayed_log_info_() const
{
  LogInfo result;
  result.lsn_ = next_to_submit_lsn_;
  result.scn_ = std::max(next_to_submit_scn_, base_scn_);
  if (next_to_submit_scn_ > base_scn_) {
    for (const auto &head : queue_heads_) {
      if (head.lsn_.is_valid() && head.lsn_ < result.lsn_ && head.scn_ < result.scn_) {
        result = head;
      }
    }
  }
  return result;
}

int ReplayInfo::get_min_unreplayed_lsn(LSN &lsn) const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  lsn = get_min_unreplayed_log_info_().lsn_;
  return OB_SUCCESS;
}

int ReplayInfo::get_min_unreplayed_scn(SCN &scn) const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  if (!enabled_ && !finished_) {
    return OB_STATE_NOT_MATCH;
  }
  scn = get_min_unreplayed_log_info_().scn_;
  return OB_SUCCESS;
}

int ReplayInfo::is_replay_done(const LSN &end_lsn, bool &done) const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  done = false;
  if (!enabled_ && !finished_) {
    return OB_STATE_NOT_MATCH;
  }
  done = get_min_unreplayed_log_info_().lsn_ >= end_lsn;
  return OB_SUCCESS;
}

int ReplayInfo::get_max_replayed_scn(SCN &scn) const
{
  int ret = OB_SUCCESS;
  SCN min_scn;
  if (OB_SUCC(ret = get_min_unreplayed_scn(min_scn))) {
    scn = min_scn > SCN::base_scn() ? SCN::scn_dec(min_scn) : SCN::min_scn();
  }
  return ret;
}

int ReplayInfo::stat(LSReplayStat &stat) const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  if (!enabled_ && !finished_) {
    return OB_STATE_NOT_MATCH;
  }
  stat.enabled_ = enabled_;
  stat.pending_cnt_ = ATOMIC_LOAD(&pending_count_);
  stat.unsubmitted_lsn_ = next_to_submit_lsn_;
  stat.unsubmitted_scn_ = next_to_submit_scn_;
  // LogService fills the live PALF tail without retaining the replay executor.
  stat.end_lsn_.reset();
  return OB_SUCCESS;
}

void ReplayInfo::set_err_info(const LSN &lsn, const SCN &scn,
                             const ObLogBaseType type, const int64_t hint,
                             const bool is_submit_err, const int64_t timestamp,
                             const int ret)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  error_.log_.lsn_ = lsn;
  error_.log_.scn_ = scn;
  error_.log_.log_type_ = type;
  error_.log_.replay_hint_ = hint;
  error_.log_.first_handle_ts_ = timestamp;
  error_.is_submit_err_ = is_submit_err;
  error_.err_ret_ = ret;
}

int ReplayInfo::diagnose(ReplayDiagnoseInfo &info) const
{
  LogInfo log;
  bool enabled = false, submit_error = false;
  int replay_ret = OB_SUCCESS;
  {
    ObLockGuard<ObSpinLock> guard(lock_);
    if (!enabled_ && !finished_) {
      return OB_STATE_NOT_MATCH;
    }
    enabled = enabled_;
    log = get_min_unreplayed_log_info_();
    info.max_replayed_lsn_ = log.lsn_;
    info.max_replayed_scn_ = log.scn_ > SCN::base_scn()
        ? SCN::scn_dec(log.scn_) : SCN::min_scn();
    if (OB_SUCCESS != error_.err_ret_) {
      log = error_.log_;
      replay_ret = error_.err_ret_;
      submit_error = error_.is_submit_err_;
    } else if (log.retry_cost_ > 0 || log.replay_cost_ > 0) {
      replay_ret = OB_EAGAIN;
    }
  }
  char type[MAX_SERVICE_TYPE_BUF_LENGTH] = {};
  int ret = log_base_type_to_string(log.log_type_, type, sizeof(type));
  info.diagnose_str_.reset();
  if (OB_SUCC(ret)) {
    ret = info.diagnose_str_.append_fmt(
        "is_enabled:%s; ret:%d; min_unreplayed_lsn:%ld; min_unreplayed_scn:%lu; "
        "replay_hint:%ld; log_type:%s; replay_cost:%ld; retry_cost:%ld; first_handle_time:%ld;",
        enabled ? "true" : "false", replay_ret, log.lsn_.val_,
        log.scn_.get_val_for_inner_table_field(), log.replay_hint_,
        submit_error ? "REPLAY_SUBMIT" : type, log.replay_cost_, log.retry_cost_, log.first_handle_ts_);
  }
  return ret;
}

bool ReplayInfo::is_fatal_error(const int ret)
{
  return OB_SUCCESS != ret && OB_ALLOCATE_MEMORY_FAILED != ret && OB_EAGAIN != ret
      && OB_NOT_RUNNING != ret && OB_IO_ERROR != ret && OB_DISK_HUNG != ret;
}

int ReplayInfo::has_fatal_error(bool &has_error) const
{
  ObLockGuard<ObSpinLock> guard(lock_);
  has_error = enabled_ && is_fatal_error(error_.err_ret_);
  return OB_SUCCESS;
}

void ReplayInfo::inc_pending_size(const int64_t size)
{
  ATOMIC_INC(&pending_count_);
  ATOMIC_AAF(&pending_size_, size);
}

void ReplayInfo::dec_pending_size(const int64_t size)
{
  ATOMIC_DEC(&pending_count_);
  const int64_t remaining = ATOMIC_SAF(&pending_size_, size);
  if (remaining < 0) {
    CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "negative pending replay size", K(remaining), K(size));
  }
}

int64_t ReplayInfo::get_pending_size() const
{
  return ATOMIC_LOAD(&pending_size_);
}

int64_t ReplayInfo::get_pending_count() const
{
  return ATOMIC_LOAD(&pending_count_);
}

void ReplayInfo::update_replayable_point(const SCN &scn)
{
  replayable_point_.inc_update(scn);
}

SCN ReplayInfo::get_replayable_point() const
{
  return replayable_point_.atomic_load();
}
} // namespace logservice
} // namespace oceanbase
