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
#include "log_sliding_window.h"
#include "log_engine.h"
#include "log_io_task_cb_utils.h"
#include "log_state_mgr.h"
#include "log_mode_mgr.h"
namespace oceanbase
{
using namespace share;
namespace palf
{

LogSlidingWindow::LogSlidingWindow()
  : self_(),
    sw_(),
    checksum_(),
    state_mgr_(NULL),
    mode_mgr_(NULL),
    log_engine_(NULL),
    lsn_allocator_(),
    group_buffer_(),
    last_submit_info_lock_(common::ObLatchIds::PALF_SW_SUBMIT_INFO_LOCK),
    last_submit_lsn_(),
    last_submit_end_lsn_(),
    last_submit_log_id_(OB_INVALID_LOG_ID),
    max_flushed_info_lock_(),
    max_flushed_lsn_(),
    max_flushed_end_lsn_(),
    committed_end_lsn_(),
    last_slide_info_lock_(common::ObLatchIds::PALF_SW_SLIDE_INFO_LOCK),
    last_slide_log_id_(OB_INVALID_LOG_ID),
    last_slide_scn_(),
    last_slide_lsn_(),
    last_slide_log_accum_checksum_(-1),
    cannot_freeze_log_warn_time_(OB_INVALID_TIMESTAMP),
    larger_log_warn_time_(OB_INVALID_TIMESTAMP),
    log_life_long_warn_time_(OB_INVALID_TIMESTAMP),
    commit_log_handling_lease_(),
    submit_log_handling_lease_(),
    end_lsn_stat_time_us_(OB_INVALID_TIMESTAMP),
    last_record_end_lsn_(PALF_INITIAL_LSN_VAL),
    fs_cb_cost_stat_("[PALF STAT FS CB EXCUTE COST TIME]", PALF_STAT_PRINT_INTERVAL_US),
    log_life_time_stat_("[PALF STAT LOG LIFE TIME]", PALF_STAT_PRINT_INTERVAL_US),
    accum_slide_log_cnt_(0),
    accum_log_gen_to_freeze_cost_(0),
    accum_log_gen_to_submit_cost_(0),
    accum_log_submit_to_flush_cost_(0),
    accum_log_submit_to_first_ack_cost_(0),
    accum_log_submit_to_commit_cost_(0),
    accum_log_submit_to_slide_cost_(0),
    group_log_stat_time_us_(OB_INVALID_TIMESTAMP),
    accum_log_cnt_(0),
    accum_group_log_size_(0),
    last_record_group_log_id_(FIRST_VALID_LOG_ID - 1),
    freeze_mode_(FEEDBACK_FREEZE_MODE),
    has_pending_handle_submit_task_(false),
    is_inited_(false)
{}

void LogSlidingWindow::destroy()
{
  is_inited_ = false;
  int tmp_ret = OB_SUCCESS;
  sw_.destroy();
  group_buffer_.destroy();
  state_mgr_ = NULL;
  log_engine_ = NULL;
  mode_mgr_ = NULL;
}

int LogSlidingWindow::init(const common::ObAddr &self,
                           LogStateMgr *state_mgr,
                           LogModeMgr *mode_mgr,
                           LogEngine *log_engine,
                           palf::PalfFSCbWrapper *palf_fs_cb,
                           common::ObILogAllocator *alloc_mgr,
                           const PalfBaseInfo &palf_base_info)
{
  int ret = OB_SUCCESS;
  const LogInfo &prev_log_info = palf_base_info.prev_log_info_;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (false == self.is_valid()
             || false == palf_base_info.is_valid()
             || NULL == state_mgr
             || NULL == mode_mgr
             || NULL == log_engine
             || NULL == palf_fs_cb) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K(self), K(palf_base_info),
        KP(state_mgr), KP(mode_mgr), KP(log_engine), KP(palf_fs_cb));
  } else if (OB_FAIL(do_init_mem_(palf_base_info, alloc_mgr))) {
  } else {
    self_ = self;
    state_mgr_ = state_mgr;
    mode_mgr_ = mode_mgr;
    log_engine_ = log_engine;
    palf_fs_cb_ = palf_fs_cb;

    last_submit_lsn_ = prev_log_info.lsn_;
    last_submit_end_lsn_ = palf_base_info.curr_lsn_;
    last_submit_log_id_ = prev_log_info.log_id_;

    max_flushed_lsn_ = prev_log_info.lsn_;
    max_flushed_end_lsn_ = palf_base_info.curr_lsn_;

    last_slide_log_id_ = prev_log_info.log_id_;
    last_slide_scn_ = prev_log_info.scn_;
    last_slide_lsn_ = prev_log_info.lsn_;
    last_slide_end_lsn_ = palf_base_info.curr_lsn_;
    last_slide_log_accum_checksum_ = prev_log_info.accum_checksum_;

    committed_end_lsn_ = palf_base_info.curr_lsn_;

    MEMSET(append_cnt_array_, 0, APPEND_CNT_ARRAY_SIZE * sizeof(int64_t));


    is_inited_ = true;
    LogGroupEntryHeader group_header;
    LogEntryHeader log_header;
    PALF_LOG(INFO, "sw init success", K(ret), K_(self), K(palf_base_info),
        "group header size", LogGroupEntryHeader::HEADER_SER_SIZE, "log entry size",
        LogEntryHeader::HEADER_SER_SIZE, "group_header ser size", group_header.get_serialize_size(),
        "log header ser size", log_header.get_serialize_size());
  }

  if (OB_SUCCESS != ret) {
    destroy();
  }
  return ret;
}

int LogSlidingWindow::do_init_mem_(const PalfBaseInfo &palf_base_info,
                                   common::ObILogAllocator *alloc_mgr)
{
  int ret = OB_SUCCESS;
  const LogInfo &prev_log_info = palf_base_info.prev_log_info_;
  if (OB_FAIL(sw_.init(prev_log_info.log_id_ + 1, PALF_SLIDING_WINDOW_SIZE, alloc_mgr))) {
  } else if (OB_FAIL(lsn_allocator_.init(prev_log_info.log_id_,
          prev_log_info.scn_, palf_base_info.curr_lsn_))) {
  } else if (OB_FAIL(group_buffer_.init(palf_base_info.curr_lsn_))) {
  } else if (OB_FAIL(checksum_.init(prev_log_info.accum_checksum_))) {
  }
  return ret;
}

bool LogSlidingWindow::can_receive_larger_log_(const int64_t log_id) const
{
  bool bool_ret = true;
  const int64_t start_log_id = get_start_id();
  const int64_t sw_end_log_id = sw_.get_end_sn();
  if (log_id - start_log_id >= PALF_SLIDING_WINDOW_SIZE
      || log_id >= sw_end_log_id) {
    // sw_end_log_id may be less than (start_log_id + PALF_SLIDING_WINDOW_SIZE),
    // because it is updated after the last slid log_task's ref_cnt decrease to 0.
    bool_ret = false;
    if (palf_reach_time_interval(5 * 1000 * 1000, larger_log_warn_time_)) {
      PALF_LOG(INFO, "sw is full, cannot submit larger log", K_(self), K(start_log_id), \
          K(sw_end_log_id), K(log_id));
    }
  }
  return bool_ret;
}

bool LogSlidingWindow::can_submit_larger_log_(const int64_t log_id) const
{
  // Bound the number of in-flight local logs.
  bool bool_ret = true;
  const int64_t start_log_id = get_start_id();
  // sw_end_log_id may be less than (start_log_id + PALF_SLIDING_WINDOW_SIZE),
  // because it is updated after the last slid log_task's ref_cnt decrease to 0.
  const int64_t sw_end_log_id = sw_.get_end_sn();
  if (log_id - start_log_id >= PALF_MAX_SUBMIT_LOG_COUNT
      || log_id >= sw_end_log_id) {
    // The local sliding window is full.
    bool_ret = false;
    if (palf_reach_time_interval(5 * 1000 * 1000, larger_log_warn_time_)) {
      PALF_LOG(INFO, "sw is full, cannot submit larger log", K_(self), K(start_log_id), \
          K(sw_end_log_id), K(log_id));
    }
  }
  return bool_ret;
}

bool LogSlidingWindow::can_submit_new_log_(const int64_t valid_log_size, LSN &lsn_upper_bound)
{
  // Check whether the local writer can submit a new log.
  // The valid_log_size does not consider group_header for generating new group log case.
  bool bool_ret = false;
  int tmp_ret = OB_SUCCESS;
  LSN curr_end_lsn;
  LSN curr_committed_end_lsn;
  get_committed_end_lsn_(curr_committed_end_lsn);
  // calculate lsn_upper_bound
  LSN buffer_reuse_lsn;
  (void) group_buffer_.get_reuse_lsn(buffer_reuse_lsn);
  const int64_t group_buffer_size = group_buffer_.get_available_buffer_size();
  LSN reuse_base_lsn = MIN(curr_committed_end_lsn, buffer_reuse_lsn);
  lsn_upper_bound = reuse_base_lsn + group_buffer_size;

  if (OB_SUCCESS != (tmp_ret = lsn_allocator_.get_curr_end_lsn(curr_end_lsn))) {
    PALF_LOG_RET(WARN, tmp_ret, "get_curr_end_lsn failed", K(tmp_ret), K_(self), K(valid_log_size));
  // Use committed_lsn as the lower bound of the reusable starting point.
  } else if (!group_buffer_.can_handle_new_log(curr_end_lsn, valid_log_size, curr_committed_end_lsn)) {
    if (REACH_TIME_INTERVAL(1000 * 1000)) {
      PALF_LOG_RET(WARN, OB_ERR_UNEXPECTED, "group_buffer_ cannot handle new log now", K(tmp_ret), K_(self),
          K(valid_log_size), K(curr_end_lsn), K(curr_committed_end_lsn),
          "start_id", get_start_id(), "max_log_id", get_max_log_id());
    }
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

int LogSlidingWindow::wait_sw_slot_ready_(const int64_t log_id)
{
  // wait for sw slot ready
  // Because log_id is allocated when sw might be full
  int ret = OB_SUCCESS;
  LogTask *log_task = NULL;
  LogTaskGuard guard(this);
  do {
    if (false == can_submit_larger_log_(log_id)) {
      // Multiple submitters may pass the pre-check with the same max_log_id.
      ret = OB_EAGAIN;
    } else if (OB_FAIL(guard.get_log_task(log_id, log_task))) {
      if (OB_ERR_OUT_OF_UPPER_BOUND == ret) {
        ret = OB_EAGAIN;
      } else {
        PALF_LOG(ERROR, "get_log_task failed", K(ret), K_(self), K(log_id));
      }
    } else {
      // get success, end loop
    }
    if (OB_EAGAIN == ret) {
      ob_usleep(100);  // sleep 100us
    }
  } while(OB_EAGAIN == ret);
  return ret;
}

int LogSlidingWindow::submit_log(const char *buf,
                                 const int64_t buf_len,
                                 const SCN &ref_scn,
                                 LSN &lsn,
                                 SCN &result_scn)
{
  int ret = OB_SUCCESS;
  int64_t log_id = OB_INVALID_LOG_ID;
  SCN scn;
  // whether need generate new log task
  bool is_new_log = false;
  // whether need generate a padding entry at the end of block
  bool need_gen_padding_entry = false;
  // length of padding part
  int64_t padding_size = 0;
  // group log valid size (without padding part)
  const int64_t valid_log_size = LogEntryHeader::HEADER_SER_SIZE + buf_len;
  const int64_t start_log_id = get_start_id();
  const int64_t log_id_upper_bound = start_log_id + PALF_MAX_SUBMIT_LOG_COUNT - 1;
  LSN tmp_lsn, lsn_upper_bound;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (NULL == buf || buf_len <= 0 || buf_len > MAX_LOG_BODY_SIZE || (!ref_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(ret), K_(self), K(buf_len), KP(buf));
  } else if (!can_submit_new_log_(valid_log_size, lsn_upper_bound)
             || !can_submit_larger_log_(get_max_log_id() + 1)) {
    ret = OB_EAGAIN;
    if (REACH_TIME_INTERVAL(1000 * 1000)) {
      PALF_LOG(ERROR, "cannot submit new log now, try again", K(ret), K_(self),
          K(valid_log_size), K(buf_len), "start_id", get_start_id(), "max_log_id", get_max_log_id());
    }
    // sw_ cannot submit larger log
  } else if (OB_FAIL(lsn_allocator_.alloc_lsn_scn(ref_scn, valid_log_size, log_id_upper_bound, lsn_upper_bound,
            tmp_lsn, log_id, scn, is_new_log, need_gen_padding_entry, padding_size))) {
  } else if (OB_FAIL(wait_sw_slot_ready_(log_id))) {
  } else {
    bool is_need_handle_next = false;
    bool is_need_handle = false;
    if (need_gen_padding_entry) {
      // need generate padding entry
      const int64_t padding_entry_body_size = padding_size - LogGroupEntryHeader::HEADER_SER_SIZE;
      if (OB_FAIL(try_freeze_prev_log_(log_id, tmp_lsn, is_need_handle))) {
      } else if (is_need_handle && FALSE_IT(is_need_handle_next |= is_need_handle)) {
      } else if (OB_FAIL(generate_new_group_log_(tmp_lsn, log_id, scn, padding_entry_body_size, LOG_PADDING, \
              NULL, padding_entry_body_size, is_need_handle))) {
      } else if (is_need_handle && FALSE_IT(is_need_handle_next |= is_need_handle)) {
      } else {
        PALF_LOG(INFO, "generate_new_group_log_ for padding log success", K_(self), K(log_id),
            K(padding_size), K(tmp_lsn), K(scn), K(is_need_handle), K(is_need_handle_next));
        // after gen padding_entry, update lsn to next block
        tmp_lsn.val_ += padding_size;
        log_id++;  // inc log_id for following new log
        scn = SCN::plus(scn, 1);
      }
    }
    result_scn = scn;
    lsn = tmp_lsn;
    if (OB_SUCC(ret)) {
      if (is_new_log) {
        // output lsn does not contains log_group_entry_header
        lsn.val_ += LogGroupEntryHeader::HEADER_SER_SIZE;
        if (OB_FAIL(try_freeze_prev_log_(log_id, tmp_lsn, is_need_handle))) {
        } else if (is_need_handle && FALSE_IT(is_need_handle_next |= is_need_handle)) {
        } else if (OB_FAIL(generate_new_group_log_(tmp_lsn, log_id, scn, valid_log_size, LOG_SUBMIT, \
                buf, buf_len, is_need_handle))) {
        } else if (is_need_handle && FALSE_IT(is_need_handle_next |= is_need_handle)) {
        } else {
          int tmp_ret = OB_SUCCESS;
          if (OB_SUCCESS != (tmp_ret = try_feedback_freeze_log_task_(log_id))) {
          }
        }
      } else {
        // this log need to be appended to last log
        if (OB_FAIL(append_to_group_log_(lsn, log_id, scn, valid_log_size, buf, buf_len, is_need_handle))) {
        } else if (is_need_handle && FALSE_IT(is_need_handle_next |= is_need_handle)) {
        } else {
        }
      }
      // inc append count
      const int64_t array_idx = get_itid() & APPEND_CNT_ARRAY_MASK;
      OB_ASSERT(0 <= array_idx && array_idx < APPEND_CNT_ARRAY_SIZE);
      ATOMIC_INC(&append_cnt_array_[array_idx]);
    }
    if (OB_SUCC(ret) && is_need_handle_next) {
      // Here log_id cannot be used as an exact invocation condition, because the processing of the previous log and this log may be concurrent
      // For example, the previous log was immediately processed by another thread after being triggered to freeze by this log, at which point this log has not yet completed fill and thus cannot be processed consecutively
      // Then this log entry needs to trigger handle itself, at this time prev_log_id is less than this log's log_id
      // With thread lease, here we can call directly without the log_id condition
      bool is_committed_lsn_updated = false;
      (void) handle_next_submit_log_(is_committed_lsn_updated);
    }
  }
  return ret;
}

int LogSlidingWindow::submit_imported_group(const char *buf,
                                            const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t group_log_checksum = 0;
  SCN min_scn;
  LSN curr_end_lsn;
  LogGroupEntryHeader header;
  LogTask *log_task = nullptr;
  LogTaskGuard task_guard(this);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(buf) || buf_len <= LogGroupEntryHeader::HEADER_SER_SIZE
             || buf_len > MAX_LOG_BUFFER_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid imported group", K(ret), K_(self), KP(buf), K(buf_len));
  } else if (OB_FAIL(header.deserialize(buf, buf_len, pos))) {
    PALF_LOG(WARN, "failed to deserialize imported group header", K(ret), K_(self));
  } else if (pos != LogGroupEntryHeader::HEADER_SER_SIZE
             || pos + header.get_data_len() != buf_len
             || !header.check_integrity(buf + pos, header.get_data_len(), group_log_checksum)) {
    ret = OB_INVALID_DATA;
    PALF_LOG(ERROR, "invalid imported group data", K(ret), K_(self), K(buf_len), K(header));
  } else if (OB_FAIL(lsn_allocator_.get_curr_end_lsn(curr_end_lsn))) {
    PALF_LOG(WARN, "failed to get local palf end", K(ret), K_(self));
  } else if (header.get_log_id() != get_max_log_id() + 1) {
    ret = OB_STATE_NOT_MATCH;
    PALF_LOG(ERROR, "imported group is not continuous with local palf", K(ret), K_(self),
        K(curr_end_lsn), K(header), "max_log_id", get_max_log_id());
  } else if (OB_FAIL(header.update_committed_end_lsn(curr_end_lsn))) {
    PALF_LOG(WARN, "failed to rebase imported group on local palf", K(ret), K_(self),
        K(curr_end_lsn), K(header));
  } else if (!can_submit_larger_log_(header.get_log_id())) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(wait_sw_slot_ready_(header.get_log_id()))) {
    PALF_LOG(WARN, "failed to wait imported group slot", K(ret), K_(self), K(header));
  } else if (OB_FAIL(task_guard.get_log_task(header.get_log_id(), log_task))) {
    PALF_LOG(WARN, "failed to get imported group task", K(ret), K_(self), K(header));
  } else if (log_task->is_valid()) {
    ret = OB_STATE_NOT_MATCH;
    PALF_LOG(ERROR, "imported group task is already occupied", K(ret), K_(self), K(curr_end_lsn), K(header));
  } else if (OB_FAIL(get_min_scn_from_buf_(
      header, buf + pos, header.get_data_len(), min_scn))) {
    PALF_LOG(WARN, "failed to get imported group min scn", K(ret), K_(self), K(header));
  } else if (OB_FAIL(wait_group_buffer_ready_(curr_end_lsn, buf_len))) {
    PALF_LOG(WARN, "failed to wait imported group buffer", K(ret), K_(self), K(curr_end_lsn), K(buf_len));
  } else if (OB_FAIL(group_buffer_.fill(curr_end_lsn, buf, buf_len))) {
    PALF_LOG(WARN, "failed to fill imported group buffer", K(ret), K_(self), K(curr_end_lsn), K(buf_len));
  } else {
    log_task->lock();
    if (OB_FAIL(log_task->set_group_header(curr_end_lsn, min_scn, header))) {
      PALF_LOG(WARN, "failed to install imported group header", K(ret), K_(self), K(curr_end_lsn), K(header));
    } else {
      log_task->set_group_log_checksum(group_log_checksum);
      (void)log_task->set_submit_log_exist();
      (void)log_task->set_freezed();
      log_task->set_freeze_ts(ObTimeUtility::current_time());
    }
    log_task->unlock();

    if (OB_SUCC(ret) && OB_FAIL(try_update_max_lsn_(curr_end_lsn, header))) {
      PALF_LOG(WARN, "failed to advance imported group position", K(ret), K_(self), K(curr_end_lsn), K(header));
    } else if (OB_SUCC(ret)) {
      bool committed_lsn_updated = false;
      if (OB_FAIL(handle_next_submit_log_(committed_lsn_updated))) {
        PALF_LOG(WARN, "failed to submit imported group", K(ret), K_(self), K(curr_end_lsn), K(header));
      }
    }
  }
  return ret;
}

int LogSlidingWindow::try_freeze_prev_log_(const int64_t next_log_id, const LSN &lsn, bool &is_need_handle)
{
  int ret = OB_SUCCESS;
  is_need_handle = false;
  if (OB_INVALID_LOG_ID == next_log_id || !lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(next_log_id), K(lsn));
  } else if (FIRST_VALID_LOG_ID == next_log_id) {
    // prev log_id is 0, skip
    PALF_LOG(INFO, "next log_id is 1, no need freeze prev log", K_(self), K(next_log_id), K(lsn));
  } else {
    const int64_t log_id = next_log_id - 1;
    LogTask *log_task = NULL;
    LogTaskGuard guard(this);
    if (OB_FAIL(guard.get_log_task(log_id, log_task))) {
      if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
        // this log has slide out, ignore
        ret = OB_SUCCESS;
      } else {
        PALF_LOG(ERROR, "get_log_task failed", K(ret), K_(self), K(log_id));
      }
    } else {
      log_task->lock();
      if (!log_task->is_valid()) {
        // Setting end_lsn for prev log_task, in case it can be freezed later by itself but not here.
        log_task->set_end_lsn(lsn);
        PALF_LOG(INFO, "log_task is invalid, its first log may has not filled, set end_lsn and skip freeze",
            K(ret), K(log_id), K_(self), KPC(log_task));
      } else {
        log_task->try_freeze(lsn);
      }
      log_task->unlock();
      // check if this log_task can be submitted
      if (log_task->is_freezed()) {
        log_task->set_freeze_ts(ObTimeUtility::current_time());
        is_need_handle = (0 == log_task->get_ref_cnt()) ? true : false;
      }
    }
  }
  return ret;
}

int LogSlidingWindow::wait_group_buffer_ready_(const LSN &lsn, const int64_t data_len)
{
  int ret = OB_SUCCESS;
  // NB: Although 'committed_end_lsn_' has been used to limit 'can_submit_new_log_', we still need to determine if 'group_buffer_' can be reused:
  // 1. Concurrent submission of logs will result in all logs entering the submission process;
  // 2. Cannot use 'committed_end_lsn_' to determine if 'group_buffer_' can be reused, because 'committed_end_lsn_' may be greater than 'max_flushed_end_lsn'.
  int64_t wait_times = 0;
  LSN curr_committed_end_lsn;
  get_committed_end_lsn_(curr_committed_end_lsn);
  while (false == group_buffer_.can_handle_new_log(lsn, data_len, curr_committed_end_lsn)) {
    // The endpoint to be filled exceeds the range of the buffer that can be reused
    // Need to retry until reusable endpoint can push large
    static const int64_t MAX_SLEEP_US = 100;
    ++wait_times;
    int64_t sleep_us = wait_times * 10;
    if (sleep_us > MAX_SLEEP_US) {
      sleep_us = MAX_SLEEP_US;
    }
    ob_usleep(sleep_us);
    PALF_LOG(WARN, "usleep wait", K_(self), K(lsn), K(data_len), K(curr_committed_end_lsn));
    get_committed_end_lsn_(curr_committed_end_lsn);
  }
  return ret;
}

int LogSlidingWindow::append_to_group_log_(const LSN &lsn,
                                           const int64_t log_id,
                                           const SCN &scn,
                                           const int64_t log_entry_size, // log_entry_header + log_data
                                           const char *log_data,
                                           const int64_t data_len,
                                           bool &is_need_handle)
{
  int ret = OB_SUCCESS;
  is_need_handle = false;
  LogTaskGuard guard(this);
  LogTask *log_task = NULL;
  if (!lsn.is_valid() || !scn.is_valid() || OB_INVALID_LOG_ID == log_id || log_entry_size <= 0
      || NULL == log_data || data_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(lsn), K(scn), K(log_id), K(log_entry_size),
        KP(log_data), K(data_len));
  } else if (OB_FAIL(guard.get_log_task(log_id, log_task))) {
  } else {
    // Note: There is no need to check if log_task is valid here, because in the concurrent submit scenario, the first log_entry may not have updated log_task yet
    LogEntryHeader log_entry_header;
    // Firstly, we need update log_task info, so that later alloc_log_id() can succeed as soon as possible.
    log_task->inc_update_max_scn(scn);
    log_task->update_data_len(log_entry_size);

    const LSN log_entry_data_lsn = lsn + LogEntryHeader::HEADER_SER_SIZE;
    int64_t pos = 0;
    assert(LogEntryHeader::HEADER_SER_SIZE < TMP_HEADER_SER_BUF_LEN);
    char tmp_buf[TMP_HEADER_SER_BUF_LEN];
    // wait group buffer ready
    if (OB_FAIL(wait_group_buffer_ready_(lsn, log_entry_size))) {
    } else if (OB_FAIL(group_buffer_.fill(log_entry_data_lsn, log_data, data_len))) {
    } else if (OB_FAIL(log_entry_header.generate_header(log_data, data_len, scn))) {
    } else if (OB_FAIL(log_entry_header.serialize(tmp_buf, TMP_HEADER_SER_BUF_LEN, pos))) {
    } else if (OB_FAIL(group_buffer_.fill(lsn, tmp_buf, pos))) {
    } else {
      assert(LogEntryHeader::HEADER_SER_SIZE == pos);
      // inc ref by log_entry_size(LOG_HEADER_SIZE + date_len)
      log_task->ref(log_entry_size);
      // check if this log_task can be submitted
      if (log_task->is_freezed()) {
        is_need_handle = (0 == log_task->get_ref_cnt()) ? true : false;
      }
    }
  }
  return ret;
}

int LogSlidingWindow::generate_new_group_log_(const LSN &lsn,
                                              const int64_t log_id,
                                              const SCN &scn,
                                              const int64_t log_body_size,  // log_entry_header_size + log_data_len
                                              const LogType &log_type,
                                              const char *log_data,
                                              const int64_t data_len,
                                              bool &is_need_handle)
{
  int ret = OB_SUCCESS;
  is_need_handle = false;
  LogTaskGuard guard(this);
  LogTask *log_task = NULL;
  if (!lsn.is_valid() || !scn.is_valid()
      || log_body_size <= 0 || OB_INVALID_LOG_ID == log_id
      || LOG_UNKNOWN == log_type
      || (LOG_PADDING != log_type && (NULL == log_data || data_len <= 0))) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(lsn), K(scn), K(log_id), K(log_body_size),
        K(log_type), KP(log_data), K(data_len));
  } else if (OB_FAIL(guard.get_log_task(log_id, log_task))) {
  } else {
    LogEntryHeader log_entry_header;
    LogGroupEntryHeader header;
    const bool is_padding_log = (LOG_PADDING == log_type);

    LogTaskHeaderInfo header_info;
    header_info.begin_lsn_ = lsn;
    header_info.is_padding_log_ = is_padding_log;
    header_info.log_id_ = log_id;
    header_info.min_scn_= scn;
    header_info.max_scn_ = scn;
    header_info.data_len_ = log_body_size;

    log_task->lock();
    if (log_task->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "log_task is valid, unexpected", K(ret), K(log_id), K_(self), K(lsn), K(scn),
          K(log_body_size), K(log_type), K(data_len), KPC(log_task));
    } else if (OB_FAIL(log_task->set_initial_header_info(header_info))) {
    } else {
      // The first log is responsible to try freezing self, if its end_lsn_ has been set by next log.
      log_task->try_freeze_by_myself();
    }
    log_task->unlock();

    if (OB_SUCC(ret)) {
      const LSN log_entry_data_lsn = lsn + LogGroupEntryHeader::HEADER_SER_SIZE + LogEntryHeader::HEADER_SER_SIZE;
      if (OB_FAIL(wait_group_buffer_ready_(lsn, log_body_size + LogGroupEntryHeader::HEADER_SER_SIZE))) {
      } else if (is_padding_log) {
        const int64_t padding_log_body_size = log_body_size - LogEntryHeader::HEADER_SER_SIZE;
        const int64_t padding_valid_data_len = LogEntryHeader::PADDING_LOG_ENTRY_SIZE;
        // padding_valid_data only include LogEntryHeader and ObLogBaseHeader
        // The format like follow:
        // | LogEntryHeader | ObLogBaseHeader|
        // and the format of padding log entry like follow:
        // | LogEntryHeader | ObLogBaseHeader| PADDING_LOG_CONTENT_CHAR |
        // |   32 BYTE      |   16 BYTE      | padding_log_body_size - 48 BYTE |
        char padding_valid_data[padding_valid_data_len];
        memset(padding_valid_data, 0, padding_valid_data_len);
        if (OB_FAIL(LogEntryHeader::generate_padding_log_buf(padding_log_body_size, scn, padding_valid_data, padding_valid_data_len))) {
        } 
        // padding log, fill log body with PADDING_LOG_CONTENT_CHAR.
        else if (OB_FAIL(group_buffer_.fill_padding_body(lsn + LogGroupEntryHeader::HEADER_SER_SIZE, padding_valid_data, padding_valid_data_len, log_body_size))) {
        } else {
          // inc ref
          log_task->ref(log_body_size);
          const bool set_submit_tag_res = log_task->set_submit_log_exist();
          assert(true == set_submit_tag_res);
        }
      } else {
        int64_t pos = 0;
        assert(LogEntryHeader::HEADER_SER_SIZE < TMP_HEADER_SER_BUF_LEN);
        char tmp_buf[TMP_HEADER_SER_BUF_LEN];
        if (OB_FAIL(group_buffer_.fill(log_entry_data_lsn, log_data, data_len))) {
        } else if (OB_FAIL(log_entry_header.generate_header(log_data, data_len, scn))) {
        } else if (OB_FAIL(log_entry_header.serialize(tmp_buf, TMP_HEADER_SER_BUF_LEN, pos))) {
        } else if (OB_FAIL(group_buffer_.fill(lsn + LogGroupEntryHeader::HEADER_SER_SIZE, tmp_buf, pos))) {
        } else {
          assert(LogEntryHeader::HEADER_SER_SIZE == pos);
          log_task->ref(log_body_size);
          const bool set_submit_tag_res = log_task->set_submit_log_exist();
          assert(true == set_submit_tag_res);
        }
      }
      // check if this log_task can be submitted
      if (log_task->is_freezed()) {
        log_task->set_freeze_ts(ObTimeUtility::current_time());
        is_need_handle = (0 == log_task->get_ref_cnt()) ? true : false;
      }
    }
  }
  return ret;
}

int LogSlidingWindow::handle_committed_log_()
{
  int ret = OB_SUCCESS;
  if (commit_log_handling_lease_.acquire()) {
    do {
      LSN unused_lsn, unused_start_lsn;
      int64_t unused_id = OB_INVALID_LOG_ID;
      LSN committed_end_lsn;
      if (is_all_committed_log_slided_out_(unused_lsn, unused_id, unused_start_lsn, committed_end_lsn)) {
        // all logs have slided out, no need continue
      } else {
        LSN max_flushed_end_lsn;
        bool need_check_next = true;
        while(OB_SUCC(ret) && need_check_next) {
          need_check_next = false;
          const int64_t tmp_log_id = get_start_id();
          LogTask *log_task = NULL;
          LogTaskGuard guard(this);
          get_max_flushed_end_lsn(max_flushed_end_lsn);
          if (OB_FAIL(guard.get_log_task(tmp_log_id, log_task))) {
            if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
              // this log has slided out, retry
              ret = OB_SUCCESS;
              need_check_next = true;
            }
          } else if (!log_task->is_valid()) {
            // log_task is not valid, end loop
            break;
          } else {
            LogGroupEntryHeader header;
            LSN log_begin_lsn;
            LSN log_end_lsn;
            int64_t data_len = 0;
            LogTaskHeaderInfo log_task_header;
            log_task->lock();
            // Notice: the following lines' order is vital, it should execute is_freezed() firstly.
            // This order can ensure log_end_lsn is correct and decided.
            const bool is_freezed = log_task->is_freezed();
            const int64_t ref_cnt = log_task->get_ref_cnt();
            log_task_header = log_task->get_header_info();
            log_begin_lsn = log_task->get_begin_lsn();
            data_len = log_task->get_data_len();
            log_end_lsn = log_task->get_begin_lsn() + LogGroupEntryHeader::HEADER_SER_SIZE + data_len;
            log_task->unlock();

            PALF_LOG(TRACE, "handle_committed_log", K_(self), K(log_end_lsn), K(committed_end_lsn),
                K(max_flushed_end_lsn), K(tmp_log_id), KPC(log_task), K(need_check_next), "can_slide_sw", state_mgr_->can_slide_sw());

            if (is_freezed
                && max_flushed_end_lsn >= log_end_lsn
                && committed_end_lsn >= log_end_lsn
                && state_mgr_->can_slide_sw()) {
              if (log_task->try_pre_slide()) {
                if (OB_FAIL(sw_.slide(PALF_MAX_REPLAY_TIMEOUT, this))) {
                  // slide failed, reset tag
                  (void) log_task->reset_pre_slide();
                  PALF_LOG(ERROR, "sw slide failed", K_(self), K(ret), K(tmp_log_id), KPC(log_task));
                } else {
                  // pop successfully, check next log
                  need_check_next = true;
                }
              }
            }
          }
        }
      }
    } while (!commit_log_handling_lease_.revoke());
  }
  return ret;
}

int LogSlidingWindow::try_handle_next_submit_log()
{
  int ret = OB_SUCCESS;
  // Set has_pending_handle_submit_task_ to false forcedly.
  (void) ATOMIC_STORE(&has_pending_handle_submit_task_, false);
  bool unused_bool = false;
  ret = handle_next_submit_log_(unused_bool);
  return ret;
}

bool LogSlidingWindow::is_handle_thread_lease_expired(const int64_t thread_lease_begin_ts) const
{
  // The thread lease time for handle_next_submit_log_ is 50ms.
  static const int64_t THREAD_LEASE_US = 50 * 1000L;
  bool bool_ret = false;
  if (OB_INVALID_TIMESTAMP != thread_lease_begin_ts
      && ObTimeUtility::current_time() - thread_lease_begin_ts > THREAD_LEASE_US) {
    bool_ret = true;
  }
  return bool_ret;
}

int LogSlidingWindow::handle_next_submit_log_(bool &is_committed_lsn_updated)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard time_guard("handle_next_submit_log", 100 * 1000);
  if (submit_log_handling_lease_.acquire()) {
    // record handle_thread_lease_begin_ts with current time
    const int64_t thread_lease_begin_ts = ObTimeUtility::current_time();
    bool is_lease_expired = false;
    bool need_submit_async_task = false;
    do {
      // If it revoke fails when thread lease expired, this thread need submit an async task.
      if (is_lease_expired) {
        need_submit_async_task = true;
      }
      while (OB_SUCC(ret) && !is_lease_expired) {
        LSN last_submit_lsn;
        LSN last_submit_end_lsn;
        int64_t last_submit_log_id = OB_INVALID_LOG_ID;
        get_last_submit_log_info_(last_submit_lsn, last_submit_end_lsn, last_submit_log_id);
        const int64_t tmp_log_id = last_submit_log_id + 1;
        SCN scn;
        LogTask *log_task = NULL;
        LogTaskGuard guard(this);
        if (OB_FAIL(guard.get_log_task(tmp_log_id, log_task))) {
          // get log task failed, exit loop
        } else if (!log_task->is_valid()) {
          // this log is invalid, end loop
          break;
        } else {
          LSN begin_lsn;
          LSN log_end_lsn;
          bool is_need_submit = false;
          bool is_submitted = false;
          // log count of this group log
          int64_t log_cnt = 0;
          int64_t group_log_size = 0;

          log_task->lock();
          // Notice: the following lines' order is vital, it should execute try_pre_submit() firstly.
          // This order can ensure log_end_lsn is correct and decided.
          is_need_submit = log_task->try_pre_submit();
          begin_lsn = log_task->get_begin_lsn();
          log_end_lsn = begin_lsn + LogGroupEntryHeader::HEADER_SER_SIZE + log_task->get_data_len();
          log_cnt = log_task->get_log_cnt();
          log_task->unlock();

          group_log_size = log_end_lsn - begin_lsn;

          LogGroupEntryHeader group_entry_header;
          int64_t group_log_data_checksum = -1;
          bool is_accum_checksum_acquired = false;
          if (is_need_submit) {
            // generate group_entry_header
            log_task->lock();
            const LSN prev_lsn = log_task->get_prev_lsn();
            log_task->unlock();
            if (OB_UNLIKELY(last_submit_end_lsn != begin_lsn)) {
              ret = OB_ERR_UNEXPECTED;
              PALF_LOG(ERROR, "Current log's begin_lsn is not continuous with last_submit_end_lsn, unexpected",
                  K_(self), K(prev_lsn), K(last_submit_log_id), K(last_submit_lsn),
                  K(last_submit_end_lsn), K(tmp_log_id), KPC(log_task));
            } else if (OB_FAIL(generate_group_entry_header_(tmp_log_id, log_task, group_entry_header,
                    group_log_data_checksum, is_accum_checksum_acquired))) {
            } else {
              log_task->lock();
              log_task->set_group_log_checksum(group_log_data_checksum);
              if (OB_FAIL(log_task->update_header_info(group_entry_header.get_committed_end_lsn(),
                    group_entry_header.get_accum_checksum()))) {
              }
              scn = log_task->get_min_scn();
              log_task->unlock();
            }
          } else {
            break;
          }
          // serialize group_entry_header without log_task's lock
          if (OB_SUCC(ret) && is_need_submit) {
            int64_t pos = 0;
            const int64_t group_entry_size = LogGroupEntryHeader::HEADER_SER_SIZE + group_entry_header.get_data_len();

            FlushLogCbCtx flush_log_cb_ctx;
            flush_log_cb_ctx.log_id_ = tmp_log_id;
            flush_log_cb_ctx.scn_ = scn;
            flush_log_cb_ctx.lsn_ = begin_lsn;
            flush_log_cb_ctx.total_len_ = group_entry_size;
            flush_log_cb_ctx.begin_ts_ = ObTimeUtility::current_time();

            LogWriteBuf log_write_buf;
            assert(LogGroupEntryHeader::HEADER_SER_SIZE < TMP_HEADER_SER_BUF_LEN);
            char tmp_buf[TMP_HEADER_SER_BUF_LEN];
            if (OB_FAIL(group_entry_header.serialize(tmp_buf, TMP_HEADER_SER_BUF_LEN, pos))) {
            } else if (OB_FAIL(group_buffer_.fill(begin_lsn, tmp_buf, pos))) {
            } else if (OB_FAIL(group_buffer_.get_log_buf(begin_lsn, group_entry_size, log_write_buf))) {
            }

            log_task->set_submit_ts(ObTimeUtility::current_time());
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(log_engine_->submit_flush_log_task(flush_log_cb_ctx, log_write_buf))) {
            } else {
              is_submitted = true;
              // statistics info for group log
              const int64_t total_log_cnt = ATOMIC_AAF(&accum_log_cnt_, log_cnt);
              const int64_t total_group_log_size = ATOMIC_AAF(&accum_group_log_size_, group_log_size);
              if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, group_log_stat_time_us_)) {
                const int64_t total_group_log_cnt = tmp_log_id - last_record_group_log_id_;
                if (total_group_log_cnt > 0) {
                  const int64_t avg_log_batch_cnt = total_log_cnt / total_group_log_cnt;
                  const int64_t avg_group_log_size = total_group_log_size / total_group_log_cnt;
                  PALF_LOG(INFO, "[PALF STAT GROUP LOG INFO]", K_(self),
                      K(total_group_log_cnt), K(avg_log_batch_cnt), K(total_group_log_size), K(avg_group_log_size));
                }
                ATOMIC_STORE(&accum_log_cnt_, 0);
                ATOMIC_STORE(&accum_group_log_size_, 0);
                ATOMIC_STORE(&last_record_group_log_id_, tmp_log_id);
              }
              // submit success, update last_submit_log info
              (void) set_last_submit_log_info_(begin_lsn, log_end_lsn, tmp_log_id);
            }
          }
          if (is_need_submit && !is_submitted) {
            // Submitting log failed, reset its tag,
            // this log may need to be truncated later.
            (void) log_task->reset_pre_submit();
            // rollcack accum_checksum
            if (is_accum_checksum_acquired
                && OB_FAIL(checksum_.rollback_accum_checksum(group_entry_header.get_accum_checksum()))) {
              PALF_LOG(ERROR, "rollback_accum_checksum failed", K(ret), K_(self), KPC(log_task),
                  K(group_entry_header));
            }
            PALF_LOG(ERROR, "submit log failed", K(ret), K_(self), KPC(log_task), K(is_accum_checksum_acquired));
          }
        }
        is_lease_expired = is_handle_thread_lease_expired(thread_lease_begin_ts);
      }
    } while (!submit_log_handling_lease_.revoke());

    // Try push handle_submit_task into queue when lease revoke failed(lease expired).
    if (OB_SUCC(ret) && need_submit_async_task) {
      // This CAS is used to control only one task can be submitted into queue at any time.
      if (ATOMIC_BCAS(&has_pending_handle_submit_task_, false, true)) {
        // push task into queue until success
        int tmp_ret = OB_SUCCESS;
        while (OB_TMP_FAIL(log_engine_->submit_handle_submit_task())) {
          if (REACH_TIME_INTERVAL(100 * 1000)) {
            PALF_LOG(WARN, "submit_handle_submit_task failed", K(tmp_ret), K_(self));
          }
          if (OB_IN_STOP_STATE == tmp_ret) {
            // The thread pool has been stopped, no need retry.
            break;
          } else {
            // sleep 100us when submit task failed
            ob_usleep(100);
          }
        }
      } else {
        // no need push task into queue
      }
    }
  }
  return ret;
}

int LogSlidingWindow::generate_group_entry_header_(const int64_t log_id,
                                                   LogTask *log_task,
                                                   LogGroupEntryHeader &group_header,
                                                   int64_t &group_log_data_checksum,
                                                   bool &is_accum_checksum_acquired)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_LOG_ID == log_id
      || NULL == log_task) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(log_id), KPC(log_task));
  } else {
    LSN global_committed_end_lsn;
    get_committed_end_lsn_(global_committed_end_lsn);
    LogTaskHeaderInfo header_info;
    log_task->lock();
    header_info = log_task->get_header_info();
    log_task->unlock();
    const bool is_padding_log = header_info.is_padding_log_;
    const LSN begin_lsn = header_info.begin_lsn_;
    LSN log_committed_end_lsn = header_info.committed_end_lsn_;
    if (!log_committed_end_lsn.is_valid()) {
      // New local entries use the current committed boundary.
      log_committed_end_lsn = global_committed_end_lsn;
    }
    const int64_t data_len = header_info.data_len_;
    const SCN max_scn = header_info.max_scn_;
    const int64_t group_entry_size = LogGroupEntryHeader::HEADER_SER_SIZE + data_len;
    LogWriteBuf log_write_buf;
    int64_t accum_checksum = 0;
    if (log_committed_end_lsn > begin_lsn) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "log_committed_end_lsn is larger than begin_lsn", K(ret), K_(self), K(global_committed_end_lsn),
          K(header_info));
    } else if (OB_FAIL(group_buffer_.get_log_buf(begin_lsn, group_entry_size, log_write_buf))) {
    } else if (OB_FAIL(group_header.generate(is_padding_log, log_write_buf, data_len, max_scn,
            log_id, log_committed_end_lsn, group_log_data_checksum))) {
    } else if (OB_FAIL(checksum_.acquire_accum_checksum(group_log_data_checksum, accum_checksum))) {
    } else {
      // set flag for rollback accum_checksum
      is_accum_checksum_acquired = true;
      (void) group_header.update_accumulated_checksum(accum_checksum);
      (void) group_header.update_header_checksum();
    }
  }
  return ret;
}

int LogSlidingWindow::try_freeze_last_log_task_(const int64_t expected_log_id,
                                                const LSN &expected_end_lsn,
                                                bool &is_need_handle)
{
  int ret = OB_SUCCESS;
  is_need_handle = false;
  if (OB_INVALID_LOG_ID == expected_log_id || !expected_end_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(expected_log_id), K(expected_end_lsn));
  } else {
    LogTask *log_task = NULL;
    LogTaskGuard guard(this);
    if (OB_FAIL(guard.get_log_task(expected_log_id, log_task))) {
      if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
        // this log has slide out, ignore
        ret = OB_SUCCESS;
      } else {
        PALF_LOG(ERROR, "get_log_task failed", K(ret), K_(self), K(expected_log_id));
      }
    } else {
      log_task->lock();
      // Current log_end_lsn of log_task is maybe less than expected_end_lsn, because there is maybe some log entry
      // submitting concurrently and it has not been filled into this log_task.
      const LSN log_end_lsn = log_task->get_begin_lsn() + LogGroupEntryHeader::HEADER_SER_SIZE
        + log_task->get_data_len();
      if (!log_task->is_valid()) {
        if (palf_reach_time_interval(1 * 1000 * 1000, cannot_freeze_log_warn_time_)) {
          PALF_LOG(INFO, "this log_task is invalid, cannot freeze", K_(self),
              K(expected_log_id), K(expected_end_lsn), KPC(log_task));
        }
      } else if (log_end_lsn > expected_end_lsn) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(ERROR, "last log's end_lsn is larger than expected", K(ret), K_(self),
            K(log_end_lsn), K(expected_log_id), K(expected_end_lsn), KPC(log_task));
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = log_task->try_freeze(expected_end_lsn))) {
        } else {
        }
      }
      log_task->unlock();
      // check if this log_task can be submitted
      if (log_task->is_freezed()) {
        log_task->set_freeze_ts(ObTimeUtility::current_time());
        is_need_handle = (0 == log_task->get_ref_cnt()) ? true : false;
      }
    }
  }
  return ret;
}

int LogSlidingWindow::feedback_freeze_last_log_()
{
  int ret = OB_SUCCESS;
  LSN last_log_end_lsn;
  int64_t last_log_id = OB_INVALID_LOG_ID;
  bool is_need_handle = false;
  if (FEEDBACK_FREEZE_MODE != freeze_mode_) {
    // Only FEEDBACK_FREEZE_MODE need exec this fucntion
    PALF_LOG(TRACE, "current freeze mode is not feedback", K_(self), "freeze_mode", freeze_mode_2_str(freeze_mode_));
  } else if (OB_FAIL(lsn_allocator_.try_freeze(last_log_end_lsn, last_log_id))) {
  } else if (last_log_id <= 0) {
    // no log, no need freeze
  } else if (OB_FAIL(try_freeze_last_log_task_(last_log_id, last_log_end_lsn, is_need_handle))) {
  } else {
    bool is_committed_lsn_updated = false;
    (void) handle_next_submit_log_(is_committed_lsn_updated);
    (void) handle_committed_log_();
  }
  return ret;
}

int LogSlidingWindow::try_feedback_freeze_log_task_(const int64_t expected_log_id)
{
  int ret = OB_SUCCESS;
  LSN log_task_begin_lsn, max_flushed_end_lsn;
  LogTask *log_task = NULL;
  LogTaskGuard guard(this);
  if (OB_FAIL(guard.get_log_task(expected_log_id, log_task))) {
    if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
      // this log has slide out, ignore
      ret = OB_SUCCESS;
    } else if (OB_ERR_OUT_OF_UPPER_BOUND == ret) {
      // sliding window is full
      ret = OB_SUCCESS;
    } else {
      PALF_LOG(ERROR, "get_log_task failed", KR(ret), K(expected_log_id));
    }
  } else if (OB_ISNULL(log_task)) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "log_task is NULL", KR(ret), K(expected_log_id));
  } else if (log_task->is_freezed()) {
  } else {
    log_task_begin_lsn = log_task->get_begin_lsn();
    get_max_flushed_end_lsn(max_flushed_end_lsn);
    if (log_task_begin_lsn.is_valid() && max_flushed_end_lsn >= log_task_begin_lsn) {
      // all logs have been flushed, freeze last log in feedback mode
      (void) feedback_freeze_last_log_();
    }
  }
  return ret;
}

bool LogSlidingWindow::is_in_period_freeze_mode() const
{
  return (PERIOD_FREEZE_MODE == freeze_mode_);
}

int LogSlidingWindow::check_and_switch_freeze_mode()
{
  int ret = OB_SUCCESS;
  int64_t total_append_cnt = 0;
  for (int i = 0; i < APPEND_CNT_ARRAY_SIZE; ++i) {
    total_append_cnt += ATOMIC_LOAD(&append_cnt_array_[i]);
    ATOMIC_STORE(&append_cnt_array_[i], 0);
  }
  if (FEEDBACK_FREEZE_MODE == freeze_mode_) {
    if (total_append_cnt >= APPEND_CNT_LB_FOR_PERIOD_FREEZE) {
      freeze_mode_ = PERIOD_FREEZE_MODE;
      PALF_LOG(INFO, "switch freeze_mode to period", K_(self), K(total_append_cnt));
    }
  } else if (PERIOD_FREEZE_MODE == freeze_mode_) {
    if (total_append_cnt < APPEND_CNT_LB_FOR_PERIOD_FREEZE) {
      freeze_mode_ = FEEDBACK_FREEZE_MODE;
      PALF_LOG(INFO, "switch freeze_mode to feedback", K_(self), K(total_append_cnt));
      (void) feedback_freeze_last_log_();
    }
  } else {}
  PALF_LOG(TRACE, "finish check_and_switch_freeze_mode", K_(self), K(total_append_cnt), "freeze_mode", freeze_mode_2_str(freeze_mode_));
  return ret;
}

int LogSlidingWindow::period_freeze_last_log()
{
  int ret = OB_SUCCESS;
  LSN last_log_end_lsn;
  int64_t last_log_id = OB_INVALID_LOG_ID;
  bool is_need_handle = false;
  if (PERIOD_FREEZE_MODE != freeze_mode_) {
    // Only PERIOD_FREEZE_MODE need exec this fucntion
    PALF_LOG(TRACE, "current freeze mode is not period", K_(self), "freeze_mode", freeze_mode_2_str(freeze_mode_));
  } else if (OB_FAIL(lsn_allocator_.try_freeze(last_log_end_lsn, last_log_id))) {
  } else if (last_log_id <= 0) {
    // no log, no need freeze
  } else if (OB_FAIL(try_freeze_last_log_task_(last_log_id, last_log_end_lsn, is_need_handle))) {
  } else {
  }
  if (get_max_log_id() > get_last_submit_log_id_()) {
    // try handle next submit log
    bool is_committed_lsn_updated = false;
    (void) handle_next_submit_log_(is_committed_lsn_updated);
  }
  // Handle logs committed by local recovery or flushing.
  (void) handle_committed_log_();
  return ret;
}

int LogSlidingWindow::after_rebuild(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    LSN committed_end_lsn;
    get_committed_end_lsn_(committed_end_lsn);
    if (lsn >= committed_end_lsn) {
      (void) try_advance_committed_lsn_(lsn);
    }
  }
  return ret;
}

int LogSlidingWindow::after_flush_log(const FlushLogCbCtx &flush_cb_ctx)
{
  int ret = OB_SUCCESS;
  bool can_exec_cb = false;
  const int64_t log_id = flush_cb_ctx.log_id_;
  const LSN log_end_lsn = flush_cb_ctx.lsn_ + flush_cb_ctx.total_len_;
  const int64_t cb_begin_ts = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!flush_cb_ctx.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(flush_cb_ctx));
  } else {
    can_exec_cb = true;
    // update log_task's flushed_ts
    LogTask *log_task = NULL;
    LogTaskGuard guard(this);
    if (OB_FAIL(guard.get_log_task(log_id, log_task))) {
    } else {
      log_task->set_flushed_ts(cb_begin_ts);
    }
  }

  common::ObTimeGuard time_guard("after flush log", 100 * 1000);
  if (OB_SUCC(ret) && can_exec_cb) {
    (void) inc_update_max_flushed_log_info_(flush_cb_ctx.lsn_, log_end_lsn);
    time_guard.click();
    if (state_mgr_->is_active()) {
      LSN new_committed_end_lsn;
      (void) gen_committed_end_lsn_(new_committed_end_lsn);
    } else {}

    time_guard.click("before handle log");

    if (OB_SUCC(ret)) {
      const int64_t last_submit_log_id = get_last_submit_log_id_();
      const int64_t next_log_id = log_id + 1;
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = try_feedback_freeze_log_task_(next_log_id))) {
      }

      if (log_id == last_submit_log_id) {
        // Non-feedback mode needs to trigger handle next log
        bool is_committed_lsn_updated = false;
        (void) handle_next_submit_log_(is_committed_lsn_updated);
      }
      time_guard.click("after handle next log");
      (void) handle_committed_log_();
      time_guard.click("after handle committed log");
    }
  }
  return ret;
}

int64_t LogSlidingWindow::get_last_submit_log_id_() const
{
  ObSpinLockGuard guard(last_submit_info_lock_);
  return last_submit_log_id_;
}

void LogSlidingWindow::get_last_submit_end_lsn_(LSN &end_lsn) const
{
  end_lsn.val_ = ATOMIC_LOAD(&last_submit_end_lsn_.val_);
}

void LogSlidingWindow::get_last_submit_log_info_(LSN &lsn, LSN &end_lsn,
    int64_t &log_id) const
{
  ObSpinLockGuard guard(last_submit_info_lock_);
  lsn = last_submit_lsn_;
  end_lsn = last_submit_end_lsn_;
  log_id = last_submit_log_id_;
}

void LogSlidingWindow::get_max_flushed_end_lsn(LSN &end_lsn) const
{
  end_lsn.val_ = ATOMIC_LOAD(&max_flushed_end_lsn_.val_);
}

int LogSlidingWindow::get_last_slide_end_lsn(LSN &out_end_lsn) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    get_last_slide_end_lsn_(out_end_lsn);
  }
  return ret;
}

int64_t LogSlidingWindow::get_last_slide_log_id() const
{
  return ATOMIC_LOAD(&last_slide_log_id_);
}

int64_t LogSlidingWindow::get_last_slide_log_id_() const
{
  return ATOMIC_LOAD(&last_slide_log_id_);
}

const SCN LogSlidingWindow::get_last_slide_scn() const
{
  return last_slide_scn_;
}

void LogSlidingWindow::get_last_slide_end_lsn_(LSN &out_end_lsn) const
{
  int64_t last_slide_log_id = OB_INVALID_LOG_ID;
  SCN last_slide_scn;
  LSN last_slide_lsn;
  LSN last_slide_end_lsn;
  int64_t last_slide_accum_checksum = -1;
  get_last_slide_log_info_(last_slide_log_id, last_slide_scn, \
          last_slide_lsn, last_slide_end_lsn, last_slide_accum_checksum);
  out_end_lsn = last_slide_end_lsn;
}

void LogSlidingWindow::get_last_slide_log_info_(int64_t &log_id,
                                                SCN &scn,
                                                LSN &lsn,
                                                LSN &end_lsn,
                                                int64_t &accum_checksum) const
{
  ObSpinLockGuard guard(last_slide_info_lock_);
  log_id = last_slide_log_id_;
  scn = last_slide_scn_;
  lsn = last_slide_lsn_;
  end_lsn = last_slide_end_lsn_;
  accum_checksum = last_slide_log_accum_checksum_;
}

int LogSlidingWindow::set_last_submit_log_info_(const LSN &lsn,
                                                const LSN &end_lsn,
                                                const int64_t log_id)
{
  int ret = OB_SUCCESS;
  if (!lsn.is_valid() || !end_lsn.is_valid() || OB_INVALID_LOG_ID == log_id) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(lsn), K(end_lsn), K(log_id));
  } else {
    ObSpinLockGuard guard(last_submit_info_lock_);
    const int64_t old_submit_log_id = last_submit_log_id_;
    last_submit_lsn_ = lsn;
    ATOMIC_STORE(&last_submit_end_lsn_.val_, end_lsn.val_);
    last_submit_log_id_ = log_id;
  }
  return ret;
}

int LogSlidingWindow::try_update_last_slide_log_info_(
    const int64_t log_id,
    const SCN &scn,
    const LSN &lsn,
    const LSN &end_lsn,
    const int64_t accum_checksum)
{
  int ret = OB_SUCCESS;
  if (!lsn.is_valid() ||
      !end_lsn.is_valid() ||
      OB_INVALID_LOG_ID == log_id ||
      !scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K_(self), K(lsn), K(end_lsn), K(log_id), K(scn));
  } else {
    ObSpinLockGuard guard(last_slide_info_lock_);
    ATOMIC_STORE(&last_slide_log_id_, log_id);
    last_slide_scn_ = scn;
    last_slide_lsn_ = lsn;
    last_slide_end_lsn_ = end_lsn;
    last_slide_log_accum_checksum_ = accum_checksum;
  }
  return ret;
}

int LogSlidingWindow::try_advance_committed_end_lsn(const LSN &end_lsn)
{
  return try_advance_committed_lsn_(end_lsn);
}

int LogSlidingWindow::try_advance_committed_lsn_(const LSN &end_lsn)
{
  int ret = OB_SUCCESS;
  if (!end_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K_(self), K(end_lsn));
  } else {
    LSN old_committed_end_lsn;
    get_committed_end_lsn_(old_committed_end_lsn);
    while (end_lsn > old_committed_end_lsn) {
      if (ATOMIC_BCAS(&committed_end_lsn_.val_, old_committed_end_lsn.val_, end_lsn.val_)) {
        break;
      } else {
        get_committed_end_lsn_(old_committed_end_lsn);
      }
    }
    if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, end_lsn_stat_time_us_)) {
      LSN curr_end_lsn;
      get_committed_end_lsn_(curr_end_lsn);
      PALF_LOG(INFO, "[PALF STAT COMMITTED LOG SIZE]", K_(self), "committed size", curr_end_lsn.val_ - last_record_end_lsn_.val_);
      last_record_end_lsn_ = curr_end_lsn;
    }
  }
  return ret;
}

int LogSlidingWindow::inc_update_scn_base(const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(lsn_allocator_.inc_update_scn_base(scn))) {
  }
  return ret;
}

int LogSlidingWindow::inc_update_max_flushed_log_info_(const LSN &lsn,
                                                       const LSN &end_lsn)
{
  int ret = OB_SUCCESS;
  LSN curr_max_flushed_end_lsn;
  get_max_flushed_end_lsn(curr_max_flushed_end_lsn);
  if (!lsn.is_valid() || !end_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K_(self), K(lsn), K(end_lsn));
  } else if (curr_max_flushed_end_lsn.is_valid() && curr_max_flushed_end_lsn >= end_lsn) {
    // no need update max_flushed_end_lsn_
  } else {
    common::ObSpinLockGuard guard(max_flushed_info_lock_);
    // double check
    if (max_flushed_end_lsn_.is_valid() && max_flushed_end_lsn_ >= end_lsn) {
      PALF_LOG(WARN, "arg end lsn is not larger than current, no need update", K_(self),
          K_(max_flushed_lsn), K_(max_flushed_end_lsn), K(lsn), K(end_lsn));
    } else {
      max_flushed_lsn_ = lsn;
      ATOMIC_STORE(&max_flushed_end_lsn_.val_, end_lsn.val_);
    }
  }
  return ret;
}

bool LogSlidingWindow::is_all_committed_log_slided_out(LSN &prev_lsn, int64_t &prev_log_id, LSN &committed_end_lsn) const
{
  LSN unused_lsn;
  return is_all_committed_log_slided_out_(prev_lsn, prev_log_id, unused_lsn, committed_end_lsn);
}

bool LogSlidingWindow::is_all_committed_log_slided_out_(
    LSN &prev_lsn,
    int64_t &prev_log_id,
    LSN &start_lsn,
    LSN &committed_end_lsn) const
{
  bool bool_ret = false;
  int64_t last_slide_log_id = OB_INVALID_LOG_ID;
  SCN last_slide_scn;
  LSN last_slide_lsn;
  LSN last_slide_end_lsn;
  int64_t last_slide_accum_checksum = -1;
  get_last_slide_log_info_(last_slide_log_id, last_slide_scn, last_slide_lsn, \
      last_slide_end_lsn, last_slide_accum_checksum);
  get_committed_end_lsn_(committed_end_lsn);
  if (committed_end_lsn <= last_slide_end_lsn) {
    bool_ret = true;
  } else {
    bool_ret = false;
  }
  prev_lsn = last_slide_lsn;
  prev_log_id = last_slide_log_id;
  start_lsn = last_slide_end_lsn;
  return bool_ret;
}

int LogSlidingWindow::sliding_cb(const int64_t sn, const FixedSlidingWindowSlot *data)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(data)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid argument", K_(self), K(sn), K(ret));
  } else if (!state_mgr_->can_slide_sw()) {
    // can_slide_sw() returns false
    ret = OB_EAGAIN;
  } else {
    LSN log_begin_lsn;
    LSN log_end_lsn;
    const int64_t log_id = static_cast<int64_t>(sn);
    const LogTask *log_task = dynamic_cast<const LogTask *>(data);
    if (NULL == log_task) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "dynamic_cast return NULL", K_(self), K(ret));
    } else {
      LogGroupEntryHeader tmp_header;
      LogTaskHeaderInfo log_task_header;

      log_task->lock();
      log_begin_lsn = log_task->get_begin_lsn();
      const SCN log_max_scn = log_task->get_max_scn();
      const int64_t log_size = LogGroupEntryHeader::HEADER_SER_SIZE + log_task->get_data_len();
      log_end_lsn = log_begin_lsn + log_size;
      log_task_header = log_task->get_header_info();
      const int64_t log_accum_checksum = log_task->get_accum_checksum();
      const int64_t log_gen_ts = log_task->get_gen_ts();
      const int64_t log_freeze_ts = log_task->get_freeze_ts();
      const int64_t log_submit_ts = log_task->get_submit_ts();
      const int64_t log_flush_ts = log_task->get_flushed_ts();
      log_task->unlock();

      // Verifying accum_checksum firstly.
      if (OB_FAIL(checksum_.verify_accum_checksum(log_task_header.data_checksum_,
                                                  log_task_header.accum_checksum_))) {
        PALF_LOG(ERROR, "verify_accum_checksum failed", KR(ret), KPC(this), K(log_id), KPC(log_task));
        LOG_DBA_ERROR_V2(OB_LOG_CHECKSUM_MISMATCH, ret, "verify_accum_checksum failed");
      } else {
        // Call fs_cb.
        int tmp_ret = OB_SUCCESS;
        const int64_t fs_cb_begin_ts = ObTimeUtility::current_time();
        if (OB_SUCCESS != (tmp_ret = palf_fs_cb_->update_end_lsn(log_end_lsn, log_max_scn))) {
          if (OB_EAGAIN == tmp_ret) {
            if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
              PALF_LOG(WARN, "update_end_lsn eagain", K(tmp_ret), K_(self), K(log_id), KPC(log_task));
            }
          } else {
            PALF_LOG(WARN, "update_end_lsn failed", K(tmp_ret), K_(self), K(log_id), KPC(log_task));
          }
        }
        const int64_t fs_cb_cost = ObTimeUtility::current_time() - fs_cb_begin_ts;
        fs_cb_cost_stat_.stat(fs_cb_cost);
        if (fs_cb_cost > 1 * 1000) {
          PALF_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "fs_cb->update_end_lsn() cost too much time", K(tmp_ret), K_(self),
              K(fs_cb_cost), K(log_id), K(log_begin_lsn), K(log_end_lsn));
        }

        const int64_t log_life_time = fs_cb_begin_ts - log_gen_ts;
        log_life_time_stat_.stat(log_life_time);

        const int64_t total_slide_log_cnt = ATOMIC_AAF(&accum_slide_log_cnt_, 1);
        const int64_t total_log_gen_to_freeze_cost = ATOMIC_AAF(&accum_log_gen_to_freeze_cost_, log_freeze_ts - log_gen_ts);
        const int64_t total_log_gen_to_submit_cost = ATOMIC_AAF(&accum_log_gen_to_submit_cost_, log_submit_ts - log_gen_ts);
        const int64_t total_log_submit_to_flush_cost = ATOMIC_AAF(&accum_log_submit_to_flush_cost_, log_flush_ts - log_submit_ts);
        const int64_t total_log_submit_to_slide_cost = ATOMIC_AAF(&accum_log_submit_to_slide_cost_, fs_cb_begin_ts - log_submit_ts);
        if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, log_slide_stat_time_)) {
          const int64_t avg_log_gen_to_freeze_time = total_log_gen_to_freeze_cost / total_slide_log_cnt;
          const int64_t avg_log_gen_to_submit_time = total_log_gen_to_submit_cost / total_slide_log_cnt;
          const int64_t avg_log_submit_to_flush_time = total_log_submit_to_flush_cost / total_slide_log_cnt;
          const int64_t avg_log_submit_to_slide_time = total_log_submit_to_slide_cost / total_slide_log_cnt;
          PALF_LOG(INFO, "[PALF STAT LOG TASK TIME]", K_(self), K(total_slide_log_cnt),
              K(avg_log_gen_to_freeze_time), K(avg_log_gen_to_submit_time), K(avg_log_submit_to_flush_time),
              K(avg_log_submit_to_slide_time));
          ATOMIC_STORE(&accum_slide_log_cnt_, 0);
          ATOMIC_STORE(&accum_log_gen_to_freeze_cost_, 0);
          ATOMIC_STORE(&accum_log_gen_to_submit_cost_, 0);
          ATOMIC_STORE(&accum_log_submit_to_flush_cost_, 0);
          ATOMIC_STORE(&accum_log_submit_to_slide_cost_, 0);
        }

        if (log_life_time > 100 * 1000) {
          if (palf_reach_time_interval(100 * 1000, log_life_long_warn_time_)) {
            PALF_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "log_task life cost too much time", K_(self), K(log_id), KPC(log_task),
                K(fs_cb_begin_ts), K(log_life_time));
          }
        }   

        // update last_slide_lsn_
        if (OB_SUCC(ret)) {
          (void) try_update_last_slide_log_info_(log_id, log_max_scn, log_begin_lsn, log_end_lsn,
              log_accum_checksum);
        }

      }
    }
  }
  return ret;
}


bool LogSlidingWindow::is_all_log_flushed_()
{
  // Check if all logs have been flushed
  bool bool_ret = false;
  int tmp_ret = OB_SUCCESS;
  LSN max_flushed_end_lsn;
  get_max_flushed_end_lsn(max_flushed_end_lsn);
  LSN curr_end_lsn;
  if (OB_SUCCESS != (tmp_ret = lsn_allocator_.get_curr_end_lsn(curr_end_lsn))) {
    PALF_LOG_RET(WARN, tmp_ret, "get_curr_end_lsn failed", K(tmp_ret), K_(self));
  } else if (max_flushed_end_lsn < curr_end_lsn) {
    PALF_LOG_RET(WARN, OB_EAGAIN, "there is some log has not been flushed", K_(self), K(curr_end_lsn),
        K(max_flushed_end_lsn), K_(max_flushed_lsn));
  } else {
    bool_ret = true;
  }
  PALF_LOG(INFO, "is_all_log_flushed_", K(bool_ret), K_(self), K(curr_end_lsn), K(max_flushed_end_lsn));
  return bool_ret;
}

int LogSlidingWindow::clean_log()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = clean_log_();
  }
  return ret;
}

int LogSlidingWindow::clean_log_()
{
  // Caller holds palf_handle's wrlock.
  // This func is used to clear log tasks beyond the last_submit_log_id in sw.
  int ret = OB_SUCCESS;
  const int64_t start_log_id = get_start_id();
  const int64_t max_log_id = get_max_log_id();
  LSN curr_end_lsn;
  (void) lsn_allocator_.get_curr_end_lsn(curr_end_lsn);

  int64_t last_slide_log_id = OB_INVALID_LOG_ID;
  SCN last_slide_scn;
  LSN last_slide_lsn;
  LSN last_slide_end_lsn;
  int64_t last_slide_accum_checksum = -1;
  get_last_slide_log_info_(last_slide_log_id, last_slide_scn, last_slide_lsn, \
      last_slide_end_lsn, last_slide_accum_checksum);
  LSN last_submit_lsn;
  LSN last_submit_end_lsn;
  int64_t last_submit_log_id = OB_INVALID_LOG_ID;
  get_last_submit_log_info_(last_submit_lsn, last_submit_end_lsn, last_submit_log_id);
  // new_last_log_xxx are used to truncate lsn_allocator.
  int64_t new_last_log_id = OB_INVALID_LOG_ID;
  SCN new_last_scn;
  LSN new_last_log_end_lsn;
  if (last_slide_end_lsn == last_submit_end_lsn) {
    new_last_log_id = last_slide_log_id;
    new_last_scn = last_slide_scn;
    new_last_log_end_lsn = last_slide_end_lsn;
    PALF_LOG(INFO, "record last slide log info", K(ret), K(last_slide_log_id),
        K(last_slide_scn), K(last_slide_end_lsn), K_(self));
  }

  int64_t first_empty_log_id = OB_INVALID_LOG_ID;  // record the first hole in sw, just for debug
  LogTask *log_task = NULL;
  for (int64_t tmp_log_id = start_log_id; OB_SUCC(ret) && tmp_log_id <= max_log_id; ++tmp_log_id) {
    LogTaskGuard guard(this);
    if (OB_FAIL(guard.get_log_task(tmp_log_id, log_task))) {
    } else {
      log_task->lock();
      if (!log_task->is_valid()) {
        PALF_LOG(INFO, "log_task is invalid", K(ret), K(tmp_log_id), K_(self), K(first_empty_log_id),
            K(max_log_id), KPC(log_task));
        if (OB_INVALID_LOG_ID == first_empty_log_id) {
          first_empty_log_id = tmp_log_id;
          PALF_LOG(INFO, "found first empty log slot", K(ret), K(tmp_log_id), K_(self));
        }
      } else {
        const SCN curr_scn = log_task->get_max_scn();
        const LSN log_end_lsn = log_task->get_begin_lsn() + LogGroupEntryHeader::HEADER_SER_SIZE + log_task->get_data_len();
        PALF_LOG(INFO, "log_task is valid, check if need clean", K(ret), K(tmp_log_id), K_(self), KPC(log_task));
        if (log_end_lsn == last_submit_end_lsn) {
          if (OB_INVALID_LOG_ID == new_last_log_id) {
            // record max flushed log_task info
            new_last_log_id = tmp_log_id;
            new_last_scn = curr_scn;
            new_last_log_end_lsn = log_end_lsn;
            PALF_LOG(INFO, "find last submit log_task", K(ret), K(tmp_log_id), K_(self),
                KPC(log_task), K(last_submit_log_id));
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_INVALID_LOG_ID != last_submit_log_id && tmp_log_id > last_submit_log_id) {
            // Drop stale tasks beyond the last locally submitted log.
            PALF_LOG(INFO, "clean log task beyond last_submit_log_id", K(ret), K_(self), K(max_log_id), K(tmp_log_id),
                K(first_empty_log_id), K(last_submit_log_id), KPC(log_task));
            log_task->reset();
          }
        }
      }
      log_task->unlock();
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_INVALID_LOG_ID == new_last_log_id
        || !new_last_scn.is_valid()
        || !new_last_log_end_lsn.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "last_log info is invalid", K(ret), K(max_log_id), K(first_empty_log_id), K(last_submit_log_id),
           K_(self), K(new_last_log_end_lsn), K(new_last_log_id), K(new_last_scn), K(start_log_id), K(max_log_id));
    } else if (new_last_log_end_lsn <= curr_end_lsn
               && OB_FAIL(truncate_lsn_allocator_(new_last_log_end_lsn, new_last_log_id, new_last_scn))) {
      // truncate lsn_allocator_ by new_last_log info
      PALF_LOG(ERROR, "truncate_lsn_allocator_ failed", K(ret), K_(self), K(new_last_log_id), K(new_last_log_end_lsn),
          K(new_last_scn));
    } else {
      // do nothing
    }
  }
  PALF_LOG(INFO, "clean log finished", K(ret), K_(self), K(max_log_id), K(first_empty_log_id), K(last_submit_log_id),
      K(start_log_id), K(max_log_id), K(new_last_log_id), K(new_last_scn), K(new_last_log_end_lsn));
  return ret;
}

int LogSlidingWindow::activate()
{
  // Check if all group entries have been flushed
  // Reset log_tasks' IS_SUBMIT_LOG_EXIST flag
  // Resize group_buffer
  int ret = OB_SUCCESS;
  SCN ref_scn;
  AccessMode access_mode = AccessMode::INVALID_ACCESS_MODE;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(mode_mgr_->get_access_mode_ref_scn(access_mode, ref_scn))) {
  } else if (!is_all_log_flushed_()) {
    ret = OB_EAGAIN;
    PALF_LOG(WARN, "activate need retry, because there is some log has not been flushed", K(ret),
        K_(self));
  } else if (OB_FAIL(clean_log_())) {
  } else if (OB_FAIL(group_buffer_.activate())) {
  } else if (ref_scn.is_valid() && AccessMode::APPEND == access_mode &&
             OB_FAIL(lsn_allocator_.inc_update_scn_base(ref_scn))) {
    PALF_LOG(ERROR, "inc_update_scn_base failed", K(ret), K_(self), K(ref_scn));
  } else {
    PALF_LOG(INFO, "activate sliding window success", K(ret), K_(self));
  }
  return ret;
}

int64_t LogSlidingWindow::get_start_id() const
{
  return sw_.get_begin_sn();
}

int LogSlidingWindow::get_committed_end_lsn(LSN &committed_end_lsn) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    get_committed_end_lsn_(committed_end_lsn);
  }
  return ret;
}

void LogSlidingWindow::get_committed_end_lsn_(LSN &out_lsn) const
{
  out_lsn.val_ = ATOMIC_LOAD(&committed_end_lsn_.val_);
}

bool LogSlidingWindow::is_empty() const
{
  return get_max_log_id() == (sw_.get_begin_sn() - 1);
}

int64_t LogSlidingWindow::get_max_log_id() const
{
  return lsn_allocator_.get_max_log_id();
}

LSN LogSlidingWindow::get_max_lsn() const
{
  LSN max_lsn;
  (void)lsn_allocator_.get_curr_end_lsn(max_lsn);
  return max_lsn;
}

const SCN LogSlidingWindow::get_max_scn() const
{
  return lsn_allocator_.get_max_scn();
}

bool LogSlidingWindow::check_all_log_has_flushed()
{
  return is_all_log_flushed_();
}

int LogSlidingWindow::gen_committed_end_lsn_(LSN &new_committed_end_lsn)
{
  get_max_flushed_end_lsn(new_committed_end_lsn);
  return try_advance_committed_lsn_(new_committed_end_lsn);
}

int LogSlidingWindow::append_disk_log(const LSN &lsn,
                                      const LogGroupEntry &group_entry)
{
  int ret = OB_SUCCESS;
  const LogGroupEntryHeader &group_entry_header = group_entry.get_header();
  const int64_t group_entry_len = group_entry_header.get_serialize_size() + group_entry_header.get_data_len();
  const LSN log_end_lsn = lsn + group_entry_len;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (false == lsn.is_valid() || false == group_entry.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(ret), K_(self), K(lsn), K(group_entry));
  } else if (OB_FAIL(append_disk_log_to_sw_(lsn, group_entry))) {
  } else if (OB_FAIL(try_update_max_lsn_(lsn, group_entry_header))){
  } else if (OB_FAIL(group_buffer_.inc_update_readable_begin_lsn(log_end_lsn))) {
  } else if (OB_FAIL(group_buffer_.inc_update_reuse_lsn(log_end_lsn))) {
  } else {
    // update max_flushed log info
    (void) inc_update_max_flushed_log_info_(lsn, log_end_lsn);
    (void) set_last_submit_log_info_(lsn, log_end_lsn, group_entry_header.get_log_id());
    // update saved accum_checksum_
    (void) checksum_.set_accum_checksum(group_entry_header.get_accum_checksum());
    (void) try_advance_committed_lsn_(group_entry_header.get_committed_end_lsn());
    (void) handle_committed_log_();
    PALF_LOG(INFO, "append_disk_log success", K(ret), K_(self), K(lsn), K(group_entry));
  }
  return ret;
}

int LogSlidingWindow::report_log_task_trace(const int64_t log_id)
{
  int ret = OB_SUCCESS;
  LogTask *log_task = NULL;
  LogTaskGuard guard(this);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_SUCC(guard.get_log_task(log_id, log_task))) {
    PALF_LOG(INFO, "current log_task status", K_(self), K(log_id), KPC(log_task));
  } else {
    // do nothing
  }
  return ret;
}

int LogSlidingWindow::append_disk_log_to_sw_(const LSN &lsn,
                                             const LogGroupEntry &entry)
{
  int ret = OB_SUCCESS;
  LogTask *log_task = NULL;
  LogTaskGuard guard(this);
  const LogGroupEntryHeader &header = entry.get_header();
  SCN min_scn;
  const int64_t log_id = header.get_log_id();
  const char *buf = entry.get_data_buf();
  const int64_t buf_len = entry.get_data_len();
  int64_t group_log_data_checksum = 0;
  if (false == header.check_integrity(buf, buf_len, group_log_data_checksum)) {
    ret = OB_INVALID_DATA;
    PALF_LOG(ERROR, "group_entry_header check_integrity failed", K(ret), K_(self));
  } else if (OB_FAIL(guard.get_log_task(log_id, log_task))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "get log task failed", K(ret), K_(self), K(log_id), K(lsn), K(header), "start id", sw_.get_begin_sn());
  } else if (log_task->is_valid()) {
    PALF_LOG(ERROR, "it's not possible to get valid log_task from sw successfully in scan disk phase", K(ret), K_(self),
        K(lsn), K(header), "start_id", sw_.get_begin_sn());
  } else if (OB_FAIL(get_min_scn_from_buf_(header, buf, buf_len, min_scn))) {
  } else {
    LSN max_flushed_lsn;
    {
      ObSpinLockGuard guard(max_flushed_info_lock_);
      max_flushed_lsn = max_flushed_lsn_;
    }
    log_task->lock();
    if (OB_FAIL(log_task->set_group_header(lsn, min_scn, header))) {
    } else {
      log_task->set_group_log_checksum(group_log_data_checksum);
      log_task->set_prev_lsn(max_flushed_lsn);
      log_task->set_freezed();
      log_task->set_freeze_ts(ObTimeUtility::current_time());
      log_task->try_pre_submit();
    }
    log_task->unlock();
  }
  return ret;
}

int LogSlidingWindow::try_update_max_lsn_(const LSN &lsn, const LogGroupEntryHeader &header)
{
  int ret = OB_SUCCESS;
  const SCN &scn = header.get_max_scn();
  const int64_t log_id = header.get_log_id();
  const int64_t group_entry_len = header.get_serialize_size() + header.get_data_len();
  const LSN end_lsn = lsn + group_entry_len;
  if (OB_FAIL(lsn_allocator_.inc_update_last_log_info(end_lsn, log_id, scn))) {
  } else {
  }
  return ret;
}

int LogSlidingWindow::truncate_lsn_allocator_(const LSN &last_lsn, const int64_t last_log_id,
    const SCN &last_scn)
{
  int ret = OB_SUCCESS;
  if (!last_lsn.is_valid() || OB_INVALID_LOG_ID == last_log_id || (!last_scn.is_valid() && 0 != last_log_id)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(lsn_allocator_.truncate(last_lsn, last_log_id, last_scn))) {
  } else {
    PALF_LOG(INFO, "lsn_allocator_.truncate success", K(ret), K_(self), K(last_lsn),
        K(last_log_id), K(last_scn));
  }
  return ret;
}

int LogSlidingWindow::LogTaskGuard::get_log_task(const int64_t log_id, LogTask *&log_task) {
  int ret = OB_SUCCESS;
  LogTask *log_data = NULL;
  if (NULL == sw_) {
    ret = OB_NOT_INIT;
  } else if (!is_valid_log_id(log_id)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(sw_->sw_.get(log_id, log_data))) {
    log_task = log_data;
    log_id_ = log_id;
  } else {
    // get failed
  }
  return ret;
}

void LogSlidingWindow::LogTaskGuard::revert_log_task() {
  int ret = OB_SUCCESS;
  if (NULL != sw_ && is_valid_log_id(log_id_)) {
    if (OB_FAIL(sw_->sw_.revert(log_id_))) {
      const int64_t begin_sn = sw_->sw_.get_begin_sn();
      PALF_LOG(ERROR, "revert failed", K(ret), K_(log_id), K(begin_sn));
    }
  }
  sw_ = NULL;
  log_id_ = -1;
}

int LogSlidingWindow::get_min_scn_from_buf_(const LogGroupEntryHeader &header,
                                            const char *buf,
                                            const int64_t buf_len,
                                            SCN &min_scn)
{
  int ret = OB_SUCCESS;
  LogEntryHeader log_entry_header;
  int64_t pos = 0;
  if (true == header.is_padding_log()) {
    min_scn = header.get_max_scn();
  } else if (OB_FAIL(log_entry_header.deserialize(buf, buf_len, pos))) {
  } else {
    min_scn = log_entry_header.get_scn();
  }
  return ret;
}

int LogSlidingWindow::advance_reuse_lsn(const LSN &flush_log_end_lsn)
{
  // Do not hold lock here.
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!flush_log_end_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(group_buffer_.inc_update_reuse_lsn(flush_log_end_lsn))) {
  } else {
  }
  return ret;
}

int LogSlidingWindow::read_data_from_buffer(const LSN &read_begin_lsn,
                                            const int64_t in_read_size,
                                            char *buf,
                                            int64_t &out_read_size) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!read_begin_lsn.is_valid() || in_read_size <= 0 || OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argumetns", K(ret), K(read_begin_lsn), K(in_read_size), KP(buf));
  } else {
    if (OB_FAIL(group_buffer_.read_data(read_begin_lsn, in_read_size, buf, out_read_size))) {
      if (OB_ERR_OUT_OF_LOWER_BOUND != ret) {
        PALF_LOG(WARN, "read_data failed", K(ret), K(read_begin_lsn), K(in_read_size));
      }
    } else {
    }
  }
  return ret;
}

}  // namespace palf
}  // namespace oceanbase
