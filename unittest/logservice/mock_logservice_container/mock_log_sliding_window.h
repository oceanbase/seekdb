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

#define private public
#include "logservice/palf/log_sliding_window.h"
#include "share/scn.h"
#include "mock_log_state_mgr.h"
#undef private

namespace oceanbase
{
namespace palf
{
class PalfFSCbWrapper;

class MockLogSlidingWindow : public LogSlidingWindow
{
public:
  MockLogSlidingWindow()
    : all_log_flushed_(true),
      all_committed_slided_out_(true),
      activated_(false),
      pending_end_lsn_(0),
      mock_start_id_(1)
  {}
  virtual ~MockLogSlidingWindow() {}
public:
  void destroy() {}
  int sliding_cb(const int64_t sn, const FixedSlidingWindowSlot *data)
  {
    int ret = OB_SUCCESS;
    UNUSED(sn);
    UNUSED(data);
    return ret;
  }
  int64_t get_max_log_id() const
  {
    return 1;
  }
  int64_t get_max_log_ts() const
  {
    return 1;
  }
  LSN get_max_lsn() const
  {
    LSN lsn;
    lsn.val_ = 0;
    return lsn;
  }
  int64_t get_start_id() const
  {
    return mock_start_id_;
  }
  int get_committed_end_lsn(LSN &committed_end_lsn) const
  {
    int ret = OB_SUCCESS;
    UNUSED(committed_end_lsn);
    return ret;
  }
  bool is_empty() const
  {
    return true;
  }
  bool check_all_log_has_flushed()
  {
    return all_log_flushed_;
  }
  int get_majority_match_lsn(LSN &majority_match_lsn)
  {
    UNUSED(majority_match_lsn);
    return OB_SUCCESS;
  }
  // ================= log sync part begin
  int submit_log(const char *buf,
                 const int64_t buf_len,
                 const int64_t ref_ts_ns,
                 LSN &lsn,
                 int64_t &log_timestamp)
  {
    int ret = OB_SUCCESS;
    UNUSED(buf);
    UNUSED(buf_len);
    UNUSED(ref_ts_ns);
    UNUSED(lsn);
    UNUSED(log_timestamp);
    return ret;
  }
  int after_flush_log(const FlushLogCbCtx &flush_cb_ctx)
  {
    int ret = OB_SUCCESS;
    UNUSED(flush_cb_ctx);
    return ret;
  }
  int ack_log(const common::ObAddr &src_server, const LSN &end_lsn)
  {
    int ret = OB_SUCCESS;
    UNUSED(src_server);
    UNUSED(end_lsn);
    return ret;
  }
  int truncate_for_rebuild(const PalfBaseInfo &palf_base_info)
  {
    int ret = OB_SUCCESS;
    UNUSED(palf_base_info);
    return ret;
  }
  // ================= log sync part end
  int append_disk_log(const LSN &lsn, const LogGroupEntry &group_entry)
  {
    int ret = OB_SUCCESS;
    UNUSED(lsn);
    UNUSED(group_entry);
    return ret;
  }
  int report_log_task_trace(const int64_t log_id)
  {
    int ret = OB_SUCCESS;
    UNUSED(log_id);
    return ret;
  }
  void get_max_flushed_end_lsn(LSN &end_lsn) const
  {
    end_lsn = max_flushed_end_lsn_;
  }
  int clean_log(const bool need_clear_log_exist_flag)
  {
    int ret = OB_SUCCESS;
    UNUSED(need_clear_log_exist_flag);
    return ret;
  }
  int activate() override
  {
    activated_ = true;
    return OB_SUCCESS;
  }
  int try_advance_committed_end_lsn(const LSN &end_lsn)
  {
    pending_end_lsn_ = end_lsn;
    return OB_SUCCESS;
  }
  int64_t get_last_submit_log_id_() const
  {
    return 1;
  }
  int get_last_slide_end_lsn(LSN &out_end_lsn) const
  {
    int ret = OB_SUCCESS;
    out_end_lsn = last_slide_end_lsn_;
    return ret;
  }
  int64_t get_last_slide_log_ts() const
  {
    return 1;
  }
  int try_freeze_last_log()
  {
    int ret = OB_SUCCESS;
    return ret;
  }
  int inc_update_scn_base(const share::SCN &scn)
  {
    int ret = OB_SUCCESS;
    return ret;
  }
  bool is_all_committed_log_slided_out(LSN &prev_lsn, int64_t &prev_log_id, LSN &committed_end_lsn) const
  {
    prev_lsn = LSN(PALF_INITIAL_LSN_VAL);
    prev_log_id = OB_INVALID_LOG_ID;
    committed_end_lsn = LSN(PALF_INITIAL_LSN_VAL + 1000);
    return all_committed_slided_out_;
  }
public:
  bool all_log_flushed_;
  bool all_committed_slided_out_;
  bool activated_;
  palf::MockLogStateMgr *state_mgr_;
  LSN pending_end_lsn_;
  int64_t mock_start_id_;
  int64_t mock_last_submit_log_id_;
  LSN mock_last_submit_lsn_;
  LSN mock_last_submit_end_lsn_;
  LSN mock_max_flushed_lsn_;
  LSN mock_max_flushed_end_lsn_;
};

} // end of palf
} // end of oceanbase

#endif
