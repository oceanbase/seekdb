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
#include "ob_replay_status.h"

namespace oceanbase
{
namespace logservice
{
using namespace common;
using namespace share;
using namespace palf;

ReplayInfo::ReplayInfo()
  : status_(nullptr), enabled_(false), finished_(false), final_stat_(), final_replayed_lsn_(),
    final_min_unreplayed_scn_(), replayable_point_(SCN::min_scn()), pending_size_(0)
{
  final_stat_.enabled_ = false;
  final_stat_.pending_cnt_ = 0;
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
  final_stat_.enabled_ = enabled;
}

int ReplayInfo::attach(ObReplayStatus *status)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  int ret = OB_SUCCESS;
  if (nullptr == status) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr != status_) {
    ret = OB_ENTRY_EXIST;
  } else {
    // The caller transfers its owning reference to ReplayInfo.
    status_ = status;
    enabled_ = false;
    finished_ = false;
    ATOMIC_STORE(&pending_size_, 0);
    replayable_point_.atomic_store(SCN::min_scn());
  }
  return ret;
}

int ReplayInfo::get_replay_status(ObReplayStatusGuard &guard) const
{
  ObLockGuard<ObSpinLock> lock(lock_);
  int ret = OB_SUCCESS;
  if (nullptr == status_) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    guard.set_replay_status(status_);
  }
  return ret;
}

int ReplayInfo::detach(const bool finished, ObReplayStatus *&status)
{
  int ret = OB_SUCCESS;
  ObReplayStatusGuard guard;
  status = nullptr;
  if (OB_ENTRY_NOT_EXIST == (ret = get_replay_status(guard))) {
    ret = OB_SUCCESS;
  } else if (OB_SUCC(ret)) {
    LSReplayStat stat;
    LSN replayed_lsn;
    SCN min_scn;
    int64_t hint = 0, first_ts = 0, cost = 0, retry = 0;
    ObLogBaseType type = ObLogBaseType::INVALID_LOG_BASE_TYPE;
    ObReplayStatus *active = guard.get_replay_status();
    if (finished && OB_FAIL(active->stat(stat))) {
    } else if (finished && OB_FAIL(active->get_min_unreplayed_log_info(
        replayed_lsn, min_scn, hint, type, first_ts, cost, retry))) {
    } else {
      ObLockGuard<ObSpinLock> lock(lock_);
      if (finished) {
        final_stat_ = stat;
        final_stat_.enabled_ = enabled_;
        final_replayed_lsn_ = replayed_lsn;
        final_min_unreplayed_scn_ = min_scn;
      }
      finished_ = finished;
      status = status_;
      status_ = nullptr;
    }
  }
  return ret;
}

int ReplayInfo::get_min_unreplayed_scn(SCN &scn) const
{
  int ret = OB_SUCCESS;
  ObReplayStatusGuard guard;
  if (OB_SUCCESS == get_replay_status(guard)) {
    LSN lsn;
    int64_t hint = 0, first_ts = 0, cost = 0, retry = 0;
    ObLogBaseType type = ObLogBaseType::INVALID_LOG_BASE_TYPE;
    ret = guard.get_replay_status()->get_min_unreplayed_log_info(
        lsn, scn, hint, type, first_ts, cost, retry);
    if (OB_SUCCESS == ret) {
      return ret;
    }
  }
  // A query may retain the old status across completion and observe its disable.
  ObLockGuard<ObSpinLock> lock(lock_);
  if (finished_) {
    scn = final_min_unreplayed_scn_;
    ret = OB_SUCCESS;
  } else if (OB_SUCCESS == ret) {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

int ReplayInfo::is_replay_done(const LSN &end_lsn, bool &done) const
{
  ObReplayStatusGuard guard;
  if (OB_SUCCESS == get_replay_status(guard)) {
    return guard.get_replay_status()->is_replay_done(end_lsn, done);
  }
  ObLockGuard<ObSpinLock> lock(lock_);
  done = finished_ && final_replayed_lsn_ >= end_lsn;
  return finished_ ? OB_SUCCESS : OB_STATE_NOT_MATCH;
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
  ObReplayStatusGuard guard;
  int ret = OB_STATE_NOT_MATCH;
  if (OB_SUCCESS == get_replay_status(guard)) {
    ret = guard.get_replay_status()->stat(stat);
    if (OB_SUCCESS == ret && stat.enabled_) {
      return ret;
    }
  }
  ObLockGuard<ObSpinLock> lock(lock_);
  if (finished_) {
    stat = final_stat_;
    ret = OB_SUCCESS;
  }
  return ret;
}

int ReplayInfo::diagnose(ReplayDiagnoseInfo &info) const
{
  ObReplayStatusGuard guard;
  if (OB_SUCCESS == get_replay_status(guard)) {
    const int ret = guard.get_replay_status()->diagnose(info);
    if (OB_SUCCESS == ret && info.max_replayed_scn_.is_valid()) {
      return ret;
    }
  }
  {
    ObLockGuard<ObSpinLock> lock(lock_);
    if (!finished_) {
      return OB_STATE_NOT_MATCH;
    }
    info.max_replayed_lsn_ = final_replayed_lsn_;
    info.max_replayed_scn_ = final_min_unreplayed_scn_ > SCN::base_scn()
        ? SCN::scn_dec(final_min_unreplayed_scn_) : SCN::min_scn();
  }
  return info.diagnose_str_.append("replay finished; pending:0;");
}

int ReplayInfo::has_fatal_error(bool &has_error) const
{
  ObReplayStatusGuard guard;
  has_error = false;
  if (OB_SUCCESS == get_replay_status(guard)) {
    ObReplayStatus *status = guard.get_replay_status();
    if (!status->try_rdlock()) {
      return OB_EAGAIN;
    }
    has_error = status->has_fatal_error();
    status->unlock();
  }
  return OB_SUCCESS;
}

void ReplayInfo::inc_pending_size(const int64_t size)
{
  ATOMIC_AAF(&pending_size_, size);
}

void ReplayInfo::dec_pending_size(const int64_t size)
{
  const int64_t remaining = ATOMIC_SAF(&pending_size_, size);
  if (remaining < 0) {
    CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "negative pending replay size", K(remaining), K(size));
  }
}

int64_t ReplayInfo::get_pending_size() const
{
  return ATOMIC_LOAD(&pending_size_);
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
