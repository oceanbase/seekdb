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

#include "ob_replay_status.h"
#include "logservice/ob_log_service.h"
#include "share/ob_structured_event_logger.h"

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace share;
namespace logservice
{
//---------------ObReplayServiceTask---------------//
ObReplayServiceTask::ObReplayServiceTask()
  : lock_(common::ObLatchIds::REPLAY_STATUS_TASK_LOCK),
    type_(ObReplayServiceTaskType::INVALID_LOG_TASK),
    replay_status_(NULL),
    lease_()
{
  reset();
}

ObReplayServiceTask::~ObReplayServiceTask()
{
  destroy();
}

void ObReplayServiceTask::reset()
{
  //Considering the reuse requirement, lease cannot be reset, otherwise the same task may be pushed multiple times
  enqueue_ts_ = 0;
  err_info_.reset();
}

void ObReplayServiceTask::destroy()
{
  reset();
  type_ = ObReplayServiceTaskType::INVALID_LOG_TASK;
  replay_status_ = NULL;
}

void ObReplayServiceTask::TaskErrInfo::reset()
{
  has_fatal_error_ = false;
  ret_code_ = common::OB_SUCCESS;
  fail_ts_ = 0;
  fail_cost_ = 0;
}

void ObReplayServiceTask::clear_err_info(const int64_t cur_ts)
{
  err_info_.has_fatal_error_ = false;
  err_info_.ret_code_ = common::OB_SUCCESS;
  if (0 != err_info_.fail_ts_) {
    err_info_.fail_cost_ += (cur_ts - err_info_.fail_ts_);
    err_info_.fail_ts_ = 0;
  }
}

void ObReplayServiceTask::set_simple_err_info(const int ret_code,
                                              const int64_t fail_ts)
{
  err_info_.ret_code_ = ret_code;
  if (0 == err_info_.fail_ts_) {
    err_info_.fail_ts_ = fail_ts;
  }
}

bool ObReplayServiceTask::need_replay_immediately() const
{
  return (OB_SUCCESS == err_info_.ret_code_);
}

//---------------ObReplayServiceSubmitTask---------------//
int ObReplayServiceSubmitTask::init(const palf::LSN &base_lsn,
                                    const SCN &base_scn,
                                    ObReplayStatus *replay_status)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_ISNULL(replay_status)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(type_), K(ret), K(replay_status));
  } else if (OB_FAIL(seek_log_iterator_no_shared_storage(
                 replay_status->palf_env_, base_lsn, iterator_))) {
  } else if (OB_FAIL(iterator_.set_io_context(palf::LogIOContext(palf::LogIOUser::REPLAY)))) {
  } else if (OB_UNLIKELY(!base_scn.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "base_scn is invalid", K(type_), K(base_lsn), K(base_scn), KR(ret));
  } else {
    replay_status_ = replay_status;
    type_ = ObReplayServiceTaskType::SUBMIT_LOG_TASK;
    if (OB_SUCCESS != (tmp_ret = iterator_.next())) {
    }
    CLOG_LOG(INFO, "submit log task init success", K(type_), K(base_lsn), K(base_scn), K(replay_status_));
  }
  return ret;
}

void ObReplayServiceSubmitTask::reset()
{
  ObLockGuard<ObSpinLock> guard(lock_);
  ObReplayServiceTask::reset();
}

void ObReplayServiceSubmitTask::destroy()
{
  reset();
  //iterator does not support reset semantics, cannot call destroy interface in interfaces that may be reused later
  iterator_.destroy();
  ObReplayServiceTask::destroy();
}

int ObReplayServiceSubmitTask::get_next_to_submit_log_info(LSN &lsn, SCN &scn) const
{
  replay_status_->get_replay_info().get_next_to_submit_log_info(lsn, scn);
  return OB_SUCCESS;
}

int ObReplayServiceSubmitTask::get_base_lsn(LSN &lsn) const
{
  lsn = replay_status_->get_replay_info().get_base_lsn();
  return OB_SUCCESS;
}

int ObReplayServiceSubmitTask::get_base_scn(SCN &scn) const
{
  scn = replay_status_->get_replay_info().get_base_scn();
  return OB_SUCCESS;
}

bool ObReplayServiceSubmitTask::has_remained_submit_log(const SCN &replayable_point,
                                                        bool &iterate_end_by_replayable_point)
{
  // next interface is only called in the submit task in a single thread, and the reset interface is mutually exclusive with it through the big lock of replay status
  // Therefore, there is no need for submit task lock protection here
  if (false == iterator_.is_valid()) {
    // maybe new logs is written after last check
    next_log(replayable_point, iterate_end_by_replayable_point);
  }
  return iterator_.is_valid();
}
//Only the scenario with padding logs can call this interface

int ObReplayServiceSubmitTask::update_submit_log_meta_info(const LSN &lsn, const SCN &scn)
{
  return replay_status_->get_replay_info().update_submit_log_meta_info(lsn, scn);
}

int ObReplayServiceSubmitTask::need_skip(const SCN &scn, bool &need_skip)
{
  int ret = OB_SUCCESS;
  const SCN base_scn = replay_status_->get_replay_info().get_base_scn();
  if (OB_UNLIKELY(!base_scn.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    need_skip = scn < base_scn;
  }
  return ret;
}

int ObReplayServiceSubmitTask::get_log(const char *&buffer, int64_t &nbytes, SCN &scn, palf::LSN &offset)
{
  return iterator_.get_entry(buffer, nbytes, scn, offset);
}

int ObReplayServiceSubmitTask::next_log(const SCN &replayable_point,
                                        bool &iterate_end_by_replayable_point)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  SCN next_min_scn;
  LSN unused_cursor;
  SCN next_to_submit_scn;
  get_next_to_submit_log_info(unused_cursor, next_to_submit_scn);
  const SCN base_scn = replay_status_->get_replay_info().get_base_scn();
  if (OB_SUCCESS != (tmp_ret = iterator_.next(replayable_point, next_min_scn,
                                              iterate_end_by_replayable_point))) {
    if (OB_ITER_END == tmp_ret) {
      if (next_min_scn == next_to_submit_scn
          || !replayable_point.is_valid()) {
        // do nothing
      } else if (!next_min_scn.is_valid()) {
        // should only occurs when palf has no log
        CLOG_LOG(INFO, "next_min_scn is invalid", K(type_), K(replayable_point),
                 K(next_min_scn), K(next_to_submit_scn), K(ret), K(iterator_));
      } else if (next_min_scn < base_scn) {
        // More block-aligned prefix logs may arrive later. Keep the checkpoint SCN as the
        // replay lower bound until the iterator reaches that checkpoint.
      } else if (OB_UNLIKELY(next_min_scn < next_to_submit_scn)) {
        ret = OB_ERR_UNEXPECTED;
        LSN unused_lsn;
        // updating next to submit log info is failed, set fatal error for replay status.
        replay_status_->set_err_info(unused_lsn, next_min_scn, ObLogBaseType::INVALID_LOG_BASE_TYPE,
                                     0, true, ObClockGenerator::getClock(), ret);
        CLOG_LOG(ERROR, "failed to update next_to_submit_scn", K(type_), K(replayable_point),
                 K(next_min_scn), K(next_to_submit_scn), K(ret), K(iterator_));
      } else {
        ret = replay_status_->get_replay_info().update_submit_scn(next_min_scn);
        CLOG_LOG(INFO, "update next_to_submit_scn", K(type_), K(replayable_point),
                 K(next_min_scn), K(next_to_submit_scn), K(ret), K(iterator_));
      }
    } else {
      // ignore other err ret of iterator
    }
  } else {
    // do nothing
  }
  return ret;
}

//---------------ObReplayServiceReplayTask---------------//
int ObReplayServiceReplayTask::init(ObReplayStatus *replay_status,
                                    const int64_t idx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(replay_status) || idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(type_), KP(replay_status), K(idx));
  } else {
    replay_status_ = replay_status;
    idx_ = idx;
    type_ = ObReplayServiceTaskType::REPLAY_LOG_TASK;
    CLOG_LOG(INFO, "ObReplayServiceReplayTask init success", K(type_), K(replay_status_), K(idx_));
  }
  return ret;
}

void ObReplayServiceReplayTask::reset()
{
  //attention: type_ and replay_status_ can not be reset
  ObLink *top_item = NULL;
  if (NULL != replay_status_) {
    ObLockGuard<ObSpinLock> guard(lock_);
    while (NULL != (top_item = pop_()))
    {
      ObLogReplayTask *replay_task = static_cast<ObLogReplayTask *>(top_item);
      //This task that reduces the reference count to zero must release log_buff
      if (replay_task->is_pre_barrier_) {
        ObLogReplayBuffer *replay_buf = static_cast<ObLogReplayBuffer *>(replay_task->read_log_buf_);
        if (NULL == replay_buf) {
          CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "replay_buf is NULL when reset", KPC(replay_task));
        } else if (0 == replay_buf->dec_replay_ref()) {
          replay_status_->free_replay_task_log_buf(replay_task);
          replay_status_->dec_pending_task(replay_task->get_replay_payload_size());
        }
      } else {
        replay_status_->dec_pending_task(replay_task->get_replay_payload_size());
      }
      replay_status_->free_replay_task(replay_task);
    };
    if (idx_ >= 0) {
      update_progress_();
    }
  }
  idx_ = -1;
  ObReplayServiceTask::reset();
}

void ObReplayServiceReplayTask::destroy()
{
  reset();
  ObReplayServiceTask::destroy();
}

int64_t ObReplayServiceReplayTask::idx() const
{
  return idx_;
}

void ObReplayServiceReplayTask::update_progress_()
{
  ReplayInfo::LogInfo head;
  if (auto *task = static_cast<ObLogReplayTask *>(top())) {
    head.lsn_ = task->lsn_;
    head.scn_ = task->scn_;
    head.replay_hint_ = task->replay_hint_;
    head.log_type_ = task->log_type_;
    head.first_handle_ts_ = task->first_handle_ts_;
    head.replay_cost_ = task->replay_cost_;
    head.retry_cost_ = task->retry_cost_;
  }
  replay_status_->get_replay_info().set_queue_head(idx_, head);
}

ObLink *ObReplayServiceReplayTask::pop()
{
  ObLockGuard<ObSpinLock> guard(lock_);
  ObLink *task = pop_();
  update_progress_();
  return task;
}

void ObReplayServiceReplayTask::push(Link *p)
{
  ObLockGuard<ObSpinLock> guard(lock_);
  const bool was_empty = nullptr == top();
  need_batch_push_ = true;
  queue_.push(p);
  if (was_empty) {
    // Publish before the submit cursor advances; pop uses the same queue lock.
    update_progress_();
  }
}

bool ObReplayServiceReplayTask::need_batch_push()
{
  return need_batch_push_;
}

void ObReplayServiceReplayTask::set_batch_push_finish()
{
  need_batch_push_ = false;
}

//---------------ObLogReplayBuffer---------------//
void ObLogReplayBuffer::reset()
{
  ref_ = 0;
  log_buf_ = NULL;
}

int64_t ObLogReplayBuffer::dec_replay_ref()
{
  return ATOMIC_SAF(&ref_, 1);
}

void ObLogReplayBuffer::inc_replay_ref()
{
  ATOMIC_INC(&ref_);
}

int64_t ObLogReplayBuffer::get_replay_ref()
{
  return ATOMIC_LOAD(&ref_);
}

//---------------ObLogReplayTask---------------//
int ObLogReplayTask::init(void *log_buf)
{
  int ret = OB_SUCCESS;
  read_log_buf_ = log_buf;
  if (is_pre_barrier_) {
    ObLogReplayBuffer *replay_log_buffer = static_cast<ObLogReplayBuffer *>(log_buf);
    replay_log_buffer->ref_ = REPLAY_TASK_QUEUE_SIZE;
  }
  init_task_ts_ = ObTimeUtility::fast_current_time();
  return ret;
}

void ObLogReplayTask::reset()
{
  scn_.reset();
  lsn_.reset();
  read_log_size_ = 0;
  is_pre_barrier_ = false;
  is_post_barrier_ = false;
  replay_hint_ = 0;
  log_type_ = ObLogBaseType::INVALID_LOG_BASE_TYPE;
  init_task_ts_ = common::OB_INVALID_TIMESTAMP;
  first_handle_ts_ = common::OB_INVALID_TIMESTAMP;
  print_error_ts_ = common::OB_INVALID_TIMESTAMP;
  replay_cost_ = common::OB_INVALID_TIMESTAMP;
  retry_cost_ = common::OB_INVALID_TIMESTAMP;
  read_log_buf_ = NULL;
}

bool ObLogReplayTask::is_valid()
{
  bool b_ret = false;
  b_ret = scn_.is_valid()
      && lsn_.is_valid()
      && read_log_size_ > 0
      && NULL != read_log_buf_;
  return b_ret;
}
void *ObLogReplayTask::get_replay_payload() const
{
  return read_log_buf_;
}

int64_t ObLogReplayTask::get_replay_payload_size() const
{
  return read_log_size_;
}

void ObLogReplayTask::shallow_copy(const ObLogReplayTask &other)
{
  log_type_ = other.log_type_;
  lsn_ = other.lsn_;
  scn_ = other.scn_;
  is_pre_barrier_ = other.is_pre_barrier_;
  is_post_barrier_ = other.is_post_barrier_;
  read_log_size_ = other.read_log_size_;
  replay_hint_ = other.replay_hint_;
  init_task_ts_ = other.init_task_ts_;
  read_log_buf_ = other.read_log_buf_;
}

int64_t ObLogReplayTask::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  char log_base_type_str[logservice::OB_LOG_BASE_TYPE_STR_MAX_LEN] = {'\0'};
  (void) log_base_type_to_string(log_type_, log_base_type_str, logservice::OB_LOG_BASE_TYPE_STR_MAX_LEN);
  J_OBJ_START();
  J_KV(K_(log_type),
       "log_type", log_base_type_str,
       K(lsn_),
       K(scn_),
       K(is_pre_barrier_),
       K(is_post_barrier_),
       K(read_log_size_),
       K(replay_hint_),
       K(first_handle_ts_),
       K(replay_cost_),
       K(retry_cost_),
       KP(read_log_buf_));
  J_OBJ_END();
  return pos;
}

//---------------ObReplayFsCb---------------//
int ObReplayFsCb::update_end_lsn(const LSN &end_offset,
                                 const SCN &end_scn)
{
  UNUSED(end_scn);
  return replay_status_->update_end_offset(end_offset);
}

//---------------ObReplayStatus---------------//
ObReplayStatus::ObReplayStatus():
    is_inited_(false),
    is_active_(false),
    is_submit_blocked_(true),
    ref_cnt_(0),
    post_barrier_lsn_(),
    last_check_memstore_lsn_(),
    rwlock_(common::ObLatchIds::REPLAY_STATUS_LOCK),
    local_replay_lock_(common::ObLatchIds::REPLAY_STATUS_LOCK),
    rp_sv_(NULL),
    replay_info_(nullptr),
    submit_log_task_(),
    palf_env_(NULL),
    palf_handle_(),
    fs_cb_(),
    get_log_info_debug_time_(OB_INVALID_TIMESTAMP),
    try_wrlock_debug_time_(OB_INVALID_TIMESTAMP),
    check_enable_debug_time_(OB_INVALID_TIMESTAMP)
{
}

ObReplayStatus::~ObReplayStatus()
{
  destroy();
}

int ObReplayStatus::init(PalfEnv *palf_env,
                         ObLogReplayService *rp_sv, ReplayInfo *replay_info)
{
  //TODO: use replica type init need_replay
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "replay status has already been inited", K(ret));
  } else if (OB_ISNULL(palf_env) || OB_ISNULL(rp_sv) || OB_ISNULL(replay_info)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(rp_sv), KP(palf_env), K(ret));
  } else if (OB_FAIL(palf_env->open(palf_handle_))) {
  } else {
    get_log_info_debug_time_ = OB_INVALID_TIMESTAMP;
    try_wrlock_debug_time_ = OB_INVALID_TIMESTAMP;
    check_enable_debug_time_ = OB_INVALID_TIMESTAMP;
    palf_env_ = palf_env;
    rp_sv_ = rp_sv;
    replay_info_ = replay_info;
    IGNORE_RETURN new (&fs_cb_) ObReplayFsCb(this);
    is_inited_ = true;
    if (OB_FAIL(palf_handle_.register_file_size_cb(&fs_cb_))) {
    } else {
      CLOG_LOG(INFO, "replay status init success", K(ret), KPC(this));
    }
  }
  if (OB_FAIL(ret) && (OB_INIT_TWICE != ret))
  {
    destroy();
  }
  return ret;
}

void ObReplayStatus::destroy()
{
  int ret = OB_SUCCESS;
  // Note: Although the reference count of replay status has been set to 0, fs_cb_ may still access replay status at this time, so unregister_file_size_cb must be called first
  if (OB_FAIL(palf_handle_.unregister_file_size_cb())) {
  }
  WLockGuard wlock_guard(rwlock_);
  CLOG_LOG(INFO, "destuct replay status", KPC(this));
  // Must be in disable state before destruction
  if (is_active_) {
    CLOG_LOG(ERROR, "is_enable when destucting", K(this));
  } else {
    is_inited_ = false;
    if (palf_handle_.is_valid()) {
      palf_env_->close(palf_handle_);
    }
    submit_log_task_.destroy();
    for (int64_t i = 0; i < REPLAY_TASK_QUEUE_SIZE; ++i) {
      task_queues_[i].destroy();
    }
    is_submit_blocked_ = true;
    post_barrier_lsn_.reset();
    last_check_memstore_lsn_.reset();
    fs_cb_.destroy();
    get_log_info_debug_time_ = OB_INVALID_TIMESTAMP;
    try_wrlock_debug_time_ = OB_INVALID_TIMESTAMP;
    check_enable_debug_time_ = OB_INVALID_TIMESTAMP;
    palf_env_ = NULL;
    rp_sv_ = NULL;
  }
}
//Non-reentrant, if failed needs to be manually disabled externally
int ObReplayStatus::enable(const LSN &base_lsn, const SCN &base_scn)
{
  int ret = OB_SUCCESS;
  if (is_active()) {
    ret = OB_STATE_NOT_MATCH;
    CLOG_LOG(WARN, "replay status already enable", K(ret));
  } else {
    WLockGuard wlock_guard(rwlock_);
    if (OB_FAIL(enable_(base_lsn, base_scn))) {
    } else {
      CLOG_LOG(INFO, "enable replay status success", K(ret), K(base_lsn), K(base_scn));
    }
  }
  return ret;
}
// submit the current submit_log_task and register callback
int ObReplayStatus::enable_(const LSN &base_lsn, const SCN &base_scn)
{
  int ret = OB_SUCCESS;
  // Processing submit_task requires setting the enable state first
  is_active_ = true;
  is_submit_blocked_ = false;
  if (0 != replay_info_->get_pending_count()) {
    //Defense check for reuse scenario
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "remain pending task when enable replay status", K(ret), KPC(this));
  } else if (FALSE_IT(replay_info_->init_progress(base_lsn, base_scn))) {
  } else if (OB_FAIL(submit_log_task_.init(base_lsn, base_scn, this))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < REPLAY_TASK_QUEUE_SIZE; ++i) {
      if (OB_FAIL(task_queues_[i].init(this, i))) {
      }
    }
    if (OB_SUCCESS == ret) {
      set_last_check_memstore_lsn(base_lsn);
      if (OB_FAIL(submit_task_to_replay_service_(submit_log_task_))) {
      }
    }
  }
  if (OB_SUCCESS != ret) {
    disable_();
    is_submit_blocked_ = true;
  }
  return ret;
}
//Reentrant
int ObReplayStatus::disable()
{
  int ret = OB_SUCCESS;
  if (!is_active()) {
    CLOG_LOG(INFO, "replay status already disable");
  } else {
    do {
      WLockGuard guard(local_replay_lock_);
      is_submit_blocked_ = true;
    } while (0);
    int64_t abs_timeout_us = WRLOCK_TRY_THRESHOLD + ObTimeUtility::current_time();
    if (OB_SUCC(rwlock_.wrlock(abs_timeout_us))) {
      if (OB_FAIL(disable_())) {
      } else {
        CLOG_LOG(INFO, "disable replay status success");
      }
      rwlock_.unlock();
    } else {
      ret = OB_EAGAIN;
      if (palf_reach_time_interval(1000 * 1000, try_wrlock_debug_time_)) {
        CLOG_LOG(INFO, "try lock failed in disable", KPC(this), K(ret));
      }
    }
  }
  return ret;
}

int ObReplayStatus::disable_()
{
  int ret = OB_SUCCESS;
  is_active_ = false;
  submit_log_task_.reset();
  for (int64_t i = 0; i < REPLAY_TASK_QUEUE_SIZE; ++i) {
    task_queues_[i].reset();
  }
  last_check_memstore_lsn_.reset();
  get_log_info_debug_time_ = OB_INVALID_TIMESTAMP;
  ATOMIC_STORE(&post_barrier_lsn_.val_, LOG_INVALID_LSN_VAL);
  return ret;
}

bool ObReplayStatus::is_active() const
{
  RLockGuard rlock_guard(rwlock_);
  return is_active_;
}

bool ObReplayStatus::is_active_without_lock() const
{
  return is_active_;
}

void ObReplayStatus::block_submit()
{
  WLockGuard guard(local_replay_lock_);
  is_submit_blocked_ = true;
  CLOG_LOG(INFO, "replay status block submit", KPC(this));
}

void ObReplayStatus::unblock_submit()
{
  int ret = OB_SUCCESS;
  do {
    WLockGuard guard(local_replay_lock_);
    is_submit_blocked_ = false;
    CLOG_LOG(INFO, "replay status unblock submit", KPC(this));
  } while (0);

  RLockGuard rlock_guard(rwlock_);
  if (!is_active_) {
    // do nothing
  } else if (OB_FAIL(submit_task_to_replay_service_(submit_log_task_))) {
  }
}

bool ObReplayStatus::need_submit_log() const
{
  RLockGuard guard(local_replay_lock_);
  return !is_submit_blocked_;
}

bool ObReplayStatus::is_idle() const
{
  RLockGuard guard(rwlock_);
  bool idle = replay_info_->get_pending_count() == 0 && submit_log_task_.is_idle();
  for (int64_t i = 0; idle && i < REPLAY_TASK_QUEUE_SIZE; ++i) {
    idle = task_queues_[i].is_idle();
  }
  return idle;
}

int ObReplayStatus::stop_fetching()
{
  block_submit();
  // PALF dispatch holds its callback lock before entering replay status.
  // Unregister outside rwlock_ to preserve that lock order.
  return palf_handle_.unregister_file_size_cb();
}

int ObReplayStatus::update_end_offset(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  //check when log slide out
  RLockGuard rlock_guard(rwlock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "replay status is not init", K(lsn), K(ret));
  } else if (!is_active_) {
    if (palf_reach_time_interval(100 * 1000, check_enable_debug_time_)) {
      CLOG_LOG(INFO, "replay status is not enabled", K(this), K(ret), K(lsn));
    }
  } else if (OB_UNLIKELY(!lsn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "invalid arguments", K(lsn), K(ret));
  } else if (!need_submit_log()) {
    // Local append handles new entries while replay submission is disabled.
  } else if (OB_FAIL(submit_task_to_replay_service_(submit_log_task_))) {
  }
  return ret;
}

int ObReplayStatus::get_min_unreplayed_lsn(LSN &lsn)
{
  return replay_info_->get_min_unreplayed_lsn(lsn);
}

int ObReplayStatus::get_replay_process(int64_t &submitted_log_size,
                                       int64_t &unsubmitted_log_size,
                                       int64_t &replayed_log_size,
                                       int64_t &unreplayed_log_size)
{
  int ret = OB_SUCCESS;
  LSN base_lsn;
  LSN min_unreplayed_lsn;
  LSN next_to_submit_lsn;
  SCN next_to_submit_scn;
  LSN committed_end_lsn;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "replay status is not inited", K(ret));
  } else if (!is_active_) {
    submitted_log_size = 0;
    unsubmitted_log_size = 0;
    replayed_log_size = 0;
    unreplayed_log_size = 0;
    CLOG_LOG(INFO, "replay status is not enabled", KPC(this));
  } else if (OB_FAIL(submit_log_task_.get_base_lsn(base_lsn))) {
  } else if (OB_FAIL(submit_log_task_.get_next_to_submit_log_info(next_to_submit_lsn, next_to_submit_scn))) {
  } else if (OB_FAIL(get_min_unreplayed_lsn(min_unreplayed_lsn))) {
  } else if (!need_submit_log()) {
    submitted_log_size = next_to_submit_lsn.val_ - base_lsn.val_;
    unsubmitted_log_size = 0;
    replayed_log_size = min_unreplayed_lsn.val_ - base_lsn.val_;
    unreplayed_log_size = 0;
    CLOG_LOG(INFO, "local replay is disabled", K(min_unreplayed_lsn), K(base_lsn), KPC(this));
  } else if (OB_FAIL(palf_handle_.get_end_lsn(committed_end_lsn))) {
  } else {
    submitted_log_size = next_to_submit_lsn.val_ - base_lsn.val_;
    unsubmitted_log_size = committed_end_lsn.val_ - base_lsn.val_;
    replayed_log_size = min_unreplayed_lsn.val_ - base_lsn.val_;
    unreplayed_log_size = committed_end_lsn.val_ - min_unreplayed_lsn.val_;
    if (replayed_log_size < 0 || unreplayed_log_size < 0) {
      CLOG_LOG(WARN, "get_replay_process failed", K(committed_end_lsn), K(min_unreplayed_lsn), K(base_lsn), KPC(this));
    }
  }
  return ret;
}

int ObReplayStatus::push_log_replay_task(ObLogReplayTask &task)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "replay service is NULL", K(task), K(ret));
  } else if (task.is_pre_barrier_) {
    //Broadcast to all queues, if memory allocation fails when allocating multiple memory blocks, all need to be released
    const int64_t task_size = sizeof(ObLogReplayTask);
    common::ObSEArray<ObLogReplayTask*, REPLAY_TASK_QUEUE_SIZE> broadcast_task_array;
    //The input parameter task itself occupies one slot
    broadcast_task_array.push_back(&task);
    for (int64_t i = 1; OB_SUCC(ret) && i < REPLAY_TASK_QUEUE_SIZE; ++i) {
      void *task_buf = NULL;
      if (OB_UNLIKELY(NULL == (task_buf = rp_sv_->alloc_replay_task(task_size)))) {
        ret = OB_EAGAIN;
        if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
          CLOG_LOG(WARN, "failed to alloc replay task buf when broadcast pre barrier", K(i), K(ret));
        }
      } else {
        ObLogReplayTask *replay_task = new (task_buf) ObLogReplayTask();
        replay_task->shallow_copy(task);
        if (OB_FAIL(broadcast_task_array.push_back(replay_task))) {
          free_replay_task(replay_task);
          CLOG_LOG(ERROR, "broadcast_task_array push back replay_task failed", K(task), K(i), K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      int index = 0;
      ObLogBaseType log_type = task.log_type_;
      palf::LSN lsn = task.lsn_;
      share::SCN scn = task.scn_;
      bool is_pre_barrier = task.is_pre_barrier_;
      bool is_post_barrier = task.is_post_barrier_;
      int64_t log_size = task.read_log_size_;
      for (index = 0; OB_SUCC(ret) && index < REPLAY_TASK_QUEUE_SIZE; ++index) {
        ObLogReplayTask *replay_task = broadcast_task_array[index];
        task_queues_[index].push(replay_task);
        //Failure to retry as a whole will cause the reference count of this task to become inconsistent, must retry in place
        int retry_count = 0;
        while (OB_FAIL(submit_task_to_replay_service_(task_queues_[index]))) {
          //print interval 100ms
          if (0 == retry_count) {
            CLOG_LOG(ERROR, "failed to push replay task queue to replay service", KPC(replay_task),
                     K(ret), KPC(this), K(index));
          }
          retry_count = (retry_count + 1) % 1000;
          ob_usleep(100);
        }
        task_queues_[index].set_batch_push_finish();
      }
      CLOG_LOG(INFO, "submit pre barrier log success", K(log_type), K(lsn), K(scn),
               K(is_pre_barrier), K(is_post_barrier), K(log_size));
    } else {
      for (int64_t i = 1; i < broadcast_task_array.count(); ++i) {
        free_replay_task(broadcast_task_array[i]);
      }
    }
  } else {
    const uint64_t queue_idx = calc_replay_queue_idx(task.replay_hint_);
    ObReplayServiceReplayTask &task_queue = task_queues_[queue_idx];
    task_queue.push(&task);
  }
  return ret;
}
//This interface will not fail
int ObReplayStatus::batch_push_all_task_queue()
{
  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < REPLAY_TASK_QUEUE_SIZE; ++i) {
    ObReplayServiceReplayTask &task_queue = task_queues_[i];
    if (!task_queue.need_batch_push()) {
      // do nothing
    } else if (OB_FAIL(submit_task_to_replay_service_(task_queue))) {
    } else {
      task_queue.set_batch_push_finish();
    }
  }
  return ret;
}

void ObReplayStatus::inc_pending_task(const int64_t log_size)
{
  if (log_size < 0) {
    CLOG_LOG_RET(ERROR, OB_INVALID_ERROR, "task is invalid", K(log_size), KPC(this));
  } else if (OB_ISNULL(rp_sv_)) {
    CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "rp sv is NULL", K(log_size), KPC(this));
  } else {
    rp_sv_->inc_pending_task_size(log_size);
  }
}

void ObReplayStatus::dec_pending_task(const int64_t log_size)
{
  if (log_size < 0) {
    CLOG_LOG_RET(ERROR, OB_INVALID_ERROR, "task is invalid", K(log_size), KPC(this));
  } else if (OB_ISNULL(rp_sv_)) {
    CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "rp sv is NULL", K(log_size), KPC(this));
  } else {
    rp_sv_->dec_pending_task_size(log_size);
  }
}

void ObReplayStatus::free_replay_task(ObLogReplayTask *task)
{
  rp_sv_->free_replay_task(task);
}

void ObReplayStatus::free_replay_task_log_buf(ObLogReplayTask *task)
{
  rp_sv_->free_replay_task_log_buf(task);
}

void ObReplayStatus::set_post_barrier_submitted(const palf::LSN &lsn)
{
  ATOMIC_STORE(&post_barrier_lsn_.val_, lsn.val_);
}

int ObReplayStatus::set_post_barrier_finished(const palf::LSN &lsn)
{
  int ret = OB_SUCCESS;
  offset_t post_barrier_lsn_val = ATOMIC_LOAD(&post_barrier_lsn_.val_);
  if (OB_UNLIKELY(post_barrier_lsn_val != lsn.val_)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "post_barrier_lsn_ not match", K(post_barrier_lsn_val), K(lsn), K(ret));
  } else {
    ATOMIC_STORE(&post_barrier_lsn_.val_, LOG_INVALID_LSN_VAL);
  }
  return ret;
}

int ObReplayStatus::check_submit_barrier()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "replay status not inited", K(ret));
  } else {
    offset_t post_barrier_lsn_val = ATOMIC_LOAD(&post_barrier_lsn_.val_);
    // If there is already a backward barrier in the queue, then the new task can only be submitted after the backward barrier log replay is complete
    if (LOG_INVALID_LSN_VAL == post_barrier_lsn_val) {
      ret = OB_SUCCESS;
    } else {
      ret = OB_EAGAIN;
    }
  }
  return ret;
}
// Forward barrier log is only replayed by the thread that reduces the reference count to 0
int ObReplayStatus::check_replay_barrier(ObLogReplayTask *replay_task,
                                         ObLogReplayBuffer *&replay_log_buf,
                                         bool &need_replay,
                                         const int64_t replay_queue_idx)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "replay status not inited", K(ret));
  } else if (NULL == replay_task
            || replay_queue_idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "check_replay_barrier invalid argument", KP(replay_task), K(replay_queue_idx));
  } else if (replay_task->is_pre_barrier_) {
    int64_t replay_hint = replay_task->replay_hint_;
    int64_t nv = -1;
    if (NULL == (replay_log_buf = static_cast<ObLogReplayBuffer *>(replay_task->read_log_buf_))) {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(ERROR, "pre barrier log buff is NULL", K(ret), KPC(replay_task));
    } else if (NULL == replay_log_buf->log_buf_) {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(ERROR, "pre barrier log real log buff is NULL", K(ret), KPC(replay_task));
    } else if (replay_queue_idx == calc_replay_queue_idx(replay_hint)
               && 1 != replay_log_buf->get_replay_ref()) {
      ret = OB_EAGAIN;
      //The forward barrier log within a certain transaction can only be replayed in this queue
    //TODO(yaoying.yyy):Refactor this part
    } else if ((0 == (nv = replay_log_buf->dec_replay_ref()))) {
      if (replay_queue_idx != calc_replay_queue_idx(replay_hint)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(ERROR, "pre barrier log need replay but replay_queue_idx not match", K(ret), K(replay_task),
                 KPC(replay_task), K(replay_queue_idx), KPC(this));
      } else {
        need_replay = true;
      }
    } else if (nv < 0) {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(ERROR, "dec pre barrier log ref less than 0", K(ret), K(replay_task), KPC(replay_task),
               K(nv), KPC(this));
    } else {
      //skip
      need_replay = false;
    }
  } else {
    need_replay = true;
  }

  return ret;
}

void ObReplayStatus::set_err_info(const palf::LSN &lsn,
                                  const SCN &scn,
                                  const ObLogBaseType &log_type,
                                  const int64_t replay_hint,
                                  const bool is_submit_err,
                                  const int64_t err_ts,
                                  const int err_ret)
{
  replay_info_->set_err_info(lsn, scn, log_type, replay_hint, is_submit_err, err_ts, err_ret);
}

int ObReplayStatus::submit_task_to_replay_service_(ObReplayServiceTask &task)
{
  int ret = OB_SUCCESS;
  if (task.acquire_lease()) {
    /* The thread that gets the lease is responsible for encapsulating the task  as a request
     * and placing it into replay Service*/
    inc_ref(); //Add the reference count first, if the task push fails, dec_ref() is required
    if (OB_FAIL(rp_sv_->submit_task(&task))) {
      CLOG_LOG(ERROR, "failed to submit task to replay service", KPC(this), K(task));
      dec_ref();
    }
  }
  return ret;
}

int ObReplayStatus::trigger_fetch_log()
{
  int ret = OB_SUCCESS;
  RLockGuard rlock_guard(rwlock_);
  if (is_active_ && need_submit_log()) {
    if (OB_FAIL(submit_task_to_replay_service_(submit_log_task_))) {
    }
  } else {
    // do nothing
  }
  return ret;
}

} // namespace logservice
}
