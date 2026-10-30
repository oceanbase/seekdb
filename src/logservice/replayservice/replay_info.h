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

#pragma once

#include "lib/lock/ob_spin_lock.h"
#include "lib/string/ob_sql_string.h"
#include "share/log/palf/lsn.h"
#include "share/scn.h"
#include "share/ob_define.h"
#include "share/log/ob_log_base_type.h"

namespace oceanbase
{
namespace logservice
{
class ObReplayStatus;
class ObLogReplayService;
class ObLogService;
class ObReplayServiceSubmitTask;
class ObReplayServiceReplayTask;

struct LSReplayStat
{
  palf::LSN end_lsn_;
  bool enabled_;
  palf::LSN unsubmitted_lsn_;
  share::SCN unsubmitted_scn_;
  int64_t pending_cnt_;
  TO_STRING_KV(K(end_lsn_), K(enabled_), K(unsubmitted_lsn_),
               K(unsubmitted_scn_), K(pending_cnt_));
};

struct ReplayDiagnoseInfo
{
  ReplayDiagnoseInfo() { reset(); }
  ~ReplayDiagnoseInfo() { reset(); }
  palf::LSN max_replayed_lsn_;
  share::SCN max_replayed_scn_;
  common::ObSqlString diagnose_str_;
  TO_STRING_KV(K(max_replayed_lsn_), K(max_replayed_scn_));
  void reset() {
    max_replayed_lsn_.reset();
    max_replayed_scn_.reset();
  }
};

// Replay state and progress are owned by LogService. Execution tasks update
// these values directly; readers never retain the replay executor or its status.
class ReplayInfo
{
public:
  ReplayInfo();
  bool is_enabled() const;
  int is_replay_done(const palf::LSN &end_lsn, bool &done) const;
  int get_max_replayed_scn(share::SCN &scn) const;
  int get_min_unreplayed_scn(share::SCN &scn) const;
  int stat(LSReplayStat &stat) const;
  int diagnose(ReplayDiagnoseInfo &info) const;
  int has_fatal_error(bool &has_error) const;
  static bool is_fatal_error(int ret);
  void inc_pending_size(int64_t size);
  void dec_pending_size(int64_t size);
  int64_t get_pending_size() const;
  int64_t get_pending_count() const;
  void update_replayable_point(const share::SCN &scn);
  share::SCN get_replayable_point() const;
private:
  friend class ObLogReplayService;
  friend class ObLogService;
  friend class ObReplayStatus;
  friend class ObReplayServiceSubmitTask;
  friend class ObReplayServiceReplayTask;
  struct LogInfo
  {
    palf::LSN lsn_;
    share::SCN scn_;
    int64_t replay_hint_ = 0;
    ObLogBaseType log_type_ = ObLogBaseType::INVALID_LOG_BASE_TYPE;
    int64_t first_handle_ts_ = 0;
    int64_t replay_cost_ = 0;
    int64_t retry_cost_ = 0;
  };
  struct ErrorInfo
  {
    LogInfo log_;
    bool is_submit_err_ = false;
    int err_ret_ = common::OB_SUCCESS;
  };
  void init_progress(const palf::LSN &base_lsn, const share::SCN &base_scn);
  void set_enabled(bool enabled);
  void finish();
  void get_next_to_submit_log_info(palf::LSN &lsn, share::SCN &scn) const;
  palf::LSN get_base_lsn() const;
  share::SCN get_base_scn() const;
  int update_submit_log_meta_info(const palf::LSN &lsn, const share::SCN &scn);
  int update_submit_scn(const share::SCN &scn);
  void set_queue_head(int64_t index, const LogInfo &head);
  void set_queue_first_handle_ts(int64_t index, int64_t timestamp);
  LogInfo get_min_unreplayed_log_info_() const; // caller holds lock_
  int get_min_unreplayed_lsn(palf::LSN &lsn) const;
  void set_err_info(const palf::LSN &lsn, const share::SCN &scn,
                    ObLogBaseType type, int64_t hint, bool is_submit_err,
                    int64_t timestamp, int ret);
  mutable common::ObSpinLock lock_;
  bool enabled_;
  bool finished_;
  palf::LSN next_to_submit_lsn_;
  share::SCN next_to_submit_scn_;
  palf::LSN base_lsn_;
  share::SCN base_scn_;
  LogInfo queue_heads_[common::REPLAY_TASK_QUEUE_SIZE];
  ErrorInfo error_;
  share::SCN replayable_point_;
  int64_t pending_size_;
  int64_t pending_count_;
  DISALLOW_COPY_AND_ASSIGN(ReplayInfo);
};
} // namespace logservice
} // namespace oceanbase
