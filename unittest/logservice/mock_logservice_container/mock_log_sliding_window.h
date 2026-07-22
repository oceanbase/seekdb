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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_SLIDING_WINDOW_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_SLIDING_WINDOW_

#include "logservice/palf/log_sliding_window.h"

namespace oceanbase
{
namespace palf
{

class MockLogSlidingWindow : public LogSlidingWindow
{
public:
  MockLogSlidingWindow()
    : pending_end_lsn_(PALF_INITIAL_LSN_VAL),
      last_slide_end_lsn_(PALF_INITIAL_LSN_VAL),
      max_lsn_(PALF_INITIAL_LSN_VAL),
      committed_end_lsn_(PALF_INITIAL_LSN_VAL),
      start_id_(1),
      is_empty_(true),
      all_log_flushed_(true),
      all_committed_slided_(true),
      activate_ret_(OB_SUCCESS),
      activate_called_(false)
  {}
  ~MockLogSlidingWindow() override = default;

  void destroy() override {}
  int init(const common::ObAddr &self,
           LogStateMgr *state_mgr,
           LogModeMgr *mode_mgr,
           LogEngine *log_engine,
           PalfFSCbWrapper *palf_fs_cb,
           common::ObILogAllocator *alloc_mgr,
           const PalfBaseInfo &palf_base_info) override
  {
    UNUSEDx(self, state_mgr, mode_mgr, log_engine, palf_fs_cb,
            alloc_mgr, palf_base_info);
    return OB_SUCCESS;
  }
  int64_t get_max_log_id() const override { return start_id_ - 1; }
  const share::SCN get_max_scn() const override { return share::SCN::min_scn(); }
  LSN get_max_lsn() const override { return max_lsn_; }
  int64_t get_start_id() const override { return start_id_; }
  int get_committed_end_lsn(LSN &committed_end_lsn) const override
  {
    committed_end_lsn = committed_end_lsn_;
    return OB_SUCCESS;
  }
  bool is_empty() const override { return is_empty_; }
  bool check_all_log_has_flushed() override { return all_log_flushed_; }
  bool is_all_committed_log_slided_out(LSN &prev_lsn,
                                       int64_t &prev_log_id,
                                       LSN &committed_end_lsn) const override
  {
    prev_lsn = last_slide_end_lsn_;
    prev_log_id = start_id_ - 1;
    committed_end_lsn = committed_end_lsn_;
    return all_committed_slided_;
  }
  int report_log_task_trace(const int64_t log_id) override
  {
    UNUSED(log_id);
    return OB_SUCCESS;
  }
  void get_max_flushed_end_lsn(LSN &end_lsn) const override
  {
    end_lsn = max_lsn_;
  }
  int clean_log() override { return OB_SUCCESS; }
  int activate() override
  {
    activate_called_ = true;
    return activate_ret_;
  }
  int try_advance_committed_end_lsn(const LSN &end_lsn) override
  {
    committed_end_lsn_ = end_lsn;
    return OB_SUCCESS;
  }
  int get_last_slide_end_lsn(LSN &out_end_lsn) const override
  {
    out_end_lsn = last_slide_end_lsn_;
    return OB_SUCCESS;
  }
  int inc_update_scn_base(const share::SCN &scn) override
  {
    UNUSED(scn);
    return OB_SUCCESS;
  }

public:
  LSN pending_end_lsn_;
  LSN last_slide_end_lsn_;
  LSN max_lsn_;
  LSN committed_end_lsn_;
  int64_t start_id_;
  bool is_empty_;
  bool all_log_flushed_;
  bool all_committed_slided_;
  int activate_ret_;
  bool activate_called_;
};

} // namespace palf
} // namespace oceanbase

#endif
