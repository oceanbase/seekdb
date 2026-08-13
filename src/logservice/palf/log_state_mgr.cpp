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
#include "log_state_mgr.h"
#include "log_mode_mgr.h"
#include "log_sliding_window.h"

namespace oceanbase
{
using namespace common;
namespace palf
{

LogStateMgr::LogStateMgr()
  : self_(),
    sw_(NULL),
    mode_mgr_(NULL),
    state_(INIT),
    recovery_start_time_us_(OB_INVALID_TIMESTAMP),
    scan_disk_log_finished_(false),
    is_inited_(false)
{}

int LogStateMgr::init(const common::ObAddr &self,
                      LogSlidingWindow *sw,
                      LogModeMgr *mode_mgr)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (!self.is_valid() || OB_ISNULL(sw) || OB_ISNULL(mode_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(ret), K(self), KP(sw), KP(mode_mgr));
  } else {
    self_ = self;
    sw_ = sw;
    mode_mgr_ = mode_mgr;
    set_state_(INIT);
    scan_disk_log_finished_ = false;
    is_inited_ = true;
    PALF_LOG(INFO, "LogStateMgr init success", K_(self));
  }
  return ret;
}

void LogStateMgr::destroy()
{
  is_inited_ = false;
  scan_disk_log_finished_ = false;
  sw_ = NULL;
  mode_mgr_ = NULL;
  self_.reset();
  set_state_(INIT);
}

bool LogStateMgr::is_state_changed()
{
  return is_inited_ && ((INIT == state_ && scan_disk_log_finished_) || RECOVERING == state_);
}

int LogStateMgr::switch_state()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (INIT == state_) {
    if (scan_disk_log_finished_) {
      ret = start_recovery_();
    }
  } else if (RECOVERING == state_) {
    ret = recover_();
    if (OB_EAGAIN == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int LogStateMgr::set_scan_disk_log_finished()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    scan_disk_log_finished_ = true;
  }
  return ret;
}

bool LogStateMgr::can_append() const
{
  return is_active() && mode_mgr_->can_append();
}

bool LogStateMgr::can_slide_sw() const
{
  return is_inited_;
}

bool LogStateMgr::need_freeze_group_buffer() const
{
  return is_active();
}

bool LogStateMgr::is_active() const
{
  return is_state_(ACTIVE);
}

bool LogStateMgr::is_recovering() const
{
  return is_state_(RECOVERING);
}

bool LogStateMgr::is_state_(const LogState state) const
{
  return state == get_state();
}

void LogStateMgr::set_state_(const LogState state)
{
  ATOMIC_STORE(&state_, static_cast<int32_t>(state));
}

int LogStateMgr::start_recovery_()
{
  recovery_start_time_us_ = ObTimeUtility::current_time();
  set_state_(RECOVERING);
  return OB_SUCCESS;
}

int LogStateMgr::recover_()
{
  int ret = OB_SUCCESS;
  LSN max_flushed_end_lsn;
  LSN last_slide_lsn;
  LSN committed_end_lsn;
  int64_t last_slide_log_id = OB_INVALID_LOG_ID;
  if (!sw_->check_all_log_has_flushed()) {
    ret = OB_EAGAIN;
  } else {
    sw_->get_max_flushed_end_lsn(max_flushed_end_lsn);
    if (OB_FAIL(sw_->try_advance_committed_end_lsn(max_flushed_end_lsn))) {
    } else if (!sw_->is_all_committed_log_slided_out(
        last_slide_lsn, last_slide_log_id, committed_end_lsn)) {
      ret = OB_EAGAIN;
    } else if (OB_FAIL(sw_->activate())) {
    } else {
      set_state_(ACTIVE);
    }
  }
  return ret;
}

} // namespace palf
} // namespace oceanbase
