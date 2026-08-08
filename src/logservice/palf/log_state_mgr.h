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

#ifndef OCEANBASE_LOGSERVICE_LOG_STATE_MGR_
#define OCEANBASE_LOGSERVICE_LOG_STATE_MGR_

#include "share/log/palf/log_define.h"
#include "log_meta_info.h"
#include "palf_callback_wrapper.h"

namespace oceanbase
{
namespace palf
{
class LogModeMgr;
class LogSlidingWindow;

class LogStateMgr
{
public:
  LogStateMgr();
  virtual ~LogStateMgr() { destroy(); }

  virtual int init(const common::ObAddr &self,
                   LogSlidingWindow *sw,
                   LogModeMgr *mode_mgr);
  virtual void destroy();
  virtual bool is_state_changed();
  virtual int switch_state();
  virtual int set_scan_disk_log_finished();

  virtual bool can_append() const;
  virtual bool can_slide_sw() const;
  virtual LogState get_state() const { return static_cast<LogState>(ATOMIC_LOAD(&state_)); }
  virtual bool need_freeze_group_buffer() const;
  virtual bool is_active() const;
  virtual bool is_recovering() const;

  TO_STRING_KV(KP(this), K_(self), "state", log_state_to_string(get_state()),
      K_(scan_disk_log_finished),
      K_(recovery_start_time_us));

private:
  bool is_state_(const LogState state) const;
  void set_state_(const LogState state);
  int start_recovery_();
  int recover_();

private:
  common::ObAddr self_;
  LogSlidingWindow *sw_;
  LogModeMgr *mode_mgr_;
  int32_t state_;
  int64_t recovery_start_time_us_;
  bool scan_disk_log_finished_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(LogStateMgr);
};

} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_LOG_STATE_MGR_
