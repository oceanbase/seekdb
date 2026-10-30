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

namespace oceanbase
{
namespace logservice
{
class ObReplayStatus;
class ObLogReplayService;
class ObLogService;
class ObReplayStatusGuard;

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

// Owned by LogService, independently of the replay executor. While replay is
// active, guards retain its status; after completion, only scalar results remain.
class ReplayInfo
{
public:
  ReplayInfo();
  // Enabling replay is independent of retaining its executor after completion.
  bool is_enabled() const;
  int is_replay_done(const palf::LSN &end_lsn, bool &done) const;
  int get_max_replayed_scn(share::SCN &scn) const;
  int get_min_unreplayed_scn(share::SCN &scn) const;
  int stat(LSReplayStat &stat) const;
  int diagnose(ReplayDiagnoseInfo &info) const;
  int has_fatal_error(bool &has_error) const;
  void inc_pending_size(int64_t size);
  void dec_pending_size(int64_t size);
  int64_t get_pending_size() const;
  void update_replayable_point(const share::SCN &scn);
  share::SCN get_replayable_point() const;
private:
  friend class ObLogReplayService;
  friend class ObLogService;
  void set_enabled(bool enabled);
  int attach(ObReplayStatus *status);
  int detach(bool finished, ObReplayStatus *&status);
  int get_replay_status(ObReplayStatusGuard &guard) const;
  mutable common::ObSpinLock lock_;
  ObReplayStatus *status_;
  bool enabled_;
  bool finished_;
  LSReplayStat final_stat_;
  palf::LSN final_replayed_lsn_;
  share::SCN final_min_unreplayed_scn_;
  share::SCN replayable_point_;
  int64_t pending_size_;
  DISALLOW_COPY_AND_ASSIGN(ReplayInfo);
};
} // namespace logservice
} // namespace oceanbase
