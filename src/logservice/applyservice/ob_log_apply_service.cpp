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

#include "ob_log_apply_service.h"
#include "logservice/ob_append_callback.h"
#include "logservice/ob_i_log_storage.h"
#include "logservice/palf/palf_env.h"
namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace storage;
using namespace share;
namespace logservice
{
//---------------ObApplyFsCb---------------//
ObApplyFsCb::ObApplyFsCb()
  : apply_status_(NULL)
{
}

ObApplyFsCb::ObApplyFsCb(ObApplyStatus *apply_status)
  : apply_status_(apply_status)
{
}

ObApplyFsCb::~ObApplyFsCb()
{
  destroy();
}

void ObApplyFsCb::destroy()
{
  apply_status_ = NULL;
}

int ObApplyFsCb::update_end_lsn(const LSN &end_lsn,
                                const SCN &end_scn)
{
  return apply_status_->update_palf_committed_end_lsn(end_lsn, end_scn);
}

//---------------ObApplyServiceTask---------------//
ObApplyServiceTask::ObApplyServiceTask()
  : lease_()
{
  reset();
}

ObApplyServiceTask::~ObApplyServiceTask()
{
  reset();
}

void ObApplyServiceTask::reset()
{
  type_ = ObApplyServiceTaskType::INVALID_LOG_TASK;
  apply_status_ = NULL;
}

ObApplyStatus *ObApplyServiceTask::get_apply_status()
{
  return apply_status_;
}

bool ObApplyServiceTask::acquire_lease()
{
  return lease_.acquire();
}
bool ObApplyServiceTask::revoke_lease()
{
  return lease_.revoke();
}

ObApplyServiceTaskType ObApplyServiceTask::get_type() const
{
  return type_;
}

//---------------ObApplyServiceSubmitTask---------------//
ObApplyServiceSubmitTask::ObApplyServiceSubmitTask()
  : ObApplyServiceTask()
{
  type_ = ObApplyServiceTaskType::SUBMIT_LOG_TASK;
}

ObApplyServiceSubmitTask::~ObApplyServiceSubmitTask()
{
  reset();
}

void ObApplyServiceSubmitTask::reset()
{
  ObApplyServiceTask::reset();
}

int ObApplyServiceSubmitTask::init(ObApplyStatus *apply_status)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(apply_status)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(type_), K(apply_status), K(ret));
  } else {
    apply_status_ = apply_status;
    type_ = ObApplyServiceTaskType::SUBMIT_LOG_TASK;
    CLOG_LOG(INFO, "ObApplyServiceSubmitTask init success", K(type_), K(apply_status));
  }
  return ret;
}

//---------------ObApplyServiceQueueTask---------------//
ObApplyServiceQueueTask::ObApplyServiceQueueTask()
  : ObApplyServiceTask(),
    total_submit_cb_cnt_(0),
    last_check_submit_cb_cnt_(0),
    total_apply_cb_cnt_(0),
    idx_(-1)
{
  type_ = ObApplyServiceTaskType::APPLY_LOG_TASK;
}

ObApplyServiceQueueTask::~ObApplyServiceQueueTask()
{
  reset();
}

void ObApplyServiceQueueTask::reset()
{
  if (!queue_.is_empty()) {
    //Defensive check, default apply status destructor queue must be empty
    CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "queue is not empty when reset task", KPC(apply_status_));
  }
  ObApplyServiceTask::reset();
  total_submit_cb_cnt_ = 0;
  last_check_submit_cb_cnt_ = 0;
  total_apply_cb_cnt_ = 0;
  idx_ = -1;
}

int ObApplyServiceQueueTask::init(ObApplyStatus *apply_status,
                                  const int64_t idx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(apply_status) || idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(type_), K(apply_status), K(idx), K(ret));
  } else {
    apply_status_ = apply_status;
    type_ = ObApplyServiceTaskType::APPLY_LOG_TASK;
    total_submit_cb_cnt_ = 0;
    last_check_submit_cb_cnt_ = 0;
    total_apply_cb_cnt_ = 0;
    idx_ = idx;
    CLOG_LOG(INFO, "ObApplyServiceQueueTask init success", K(type_), K(apply_status), K(idx_));
  }
  return ret;
}

ObLink *ObApplyServiceQueueTask::top()
{
  ObLink *p = NULL;
  queue_.top(p);
  return p;
}

int ObApplyServiceQueueTask::pop()
{
  ObLink *p = NULL;
  return queue_.pop(p);
}

int ObApplyServiceQueueTask::push(ObLink *p)
{
  return queue_.push(p);
}

void ObApplyServiceQueueTask::inc_total_submit_cb_cnt()
{
  ATOMIC_INC(&total_submit_cb_cnt_);
}

void ObApplyServiceQueueTask::inc_total_apply_cb_cnt()
{
  ATOMIC_INC(&total_apply_cb_cnt_);
}

int64_t ObApplyServiceQueueTask::get_total_submit_cb_cnt() const
{
  return ATOMIC_LOAD(&total_submit_cb_cnt_);
}

int64_t ObApplyServiceQueueTask::get_total_apply_cb_cnt() const
{
  return ATOMIC_LOAD(&total_apply_cb_cnt_);
}

void ObApplyServiceQueueTask::set_snapshot_check_submit_cb_cnt()
{
  last_check_submit_cb_cnt_ = total_submit_cb_cnt_;
}

int ObApplyServiceQueueTask::is_snapshot_apply_done(bool &is_done)
{
  int ret = OB_SUCCESS;
  is_done = (total_apply_cb_cnt_ >= last_check_submit_cb_cnt_);
  CLOG_LOG(TRACE, "is_snapshot_apply_done check", K(total_apply_cb_cnt_), K(last_check_submit_cb_cnt_));
  return ret;
}

int ObApplyServiceQueueTask::is_apply_done(bool &is_done)
{
  int ret = OB_SUCCESS;
  int64_t total_apply_cb_cnt = ATOMIC_LOAD(&total_apply_cb_cnt_);
  int64_t total_submit_cb_cnt = ATOMIC_LOAD(&total_submit_cb_cnt_);
  if (total_apply_cb_cnt > total_submit_cb_cnt) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "applied cb count larger than submitted", K(total_apply_cb_cnt),
               K(total_submit_cb_cnt), KPC(apply_status_));
  } else {
    is_done = (total_apply_cb_cnt == total_submit_cb_cnt);
    CLOG_LOG(TRACE, "check is_apply_done", K(is_done), K(total_apply_cb_cnt),
               K(total_submit_cb_cnt), KPC(apply_status_));
  }
  return ret;
}

int64_t ObApplyServiceQueueTask::idx() const
{
  return idx_;
}

//---------------ObApplyStatus---------------//
ObApplyStatus::ObApplyStatus()
    : is_inited_(false),
      is_in_stop_state_(true),
      ref_cnt_(0),
      accepting_append_(false),
      ap_sv_(NULL),
      palf_committed_end_lsn_(0),
      palf_committed_end_scn_(),
      last_check_scn_(),
      max_applied_cb_scn_(),
      submit_task_(),
      palf_env_(NULL),
      palf_handle_(),
      fs_cb_(),
      lock_(ObLatchIds::APPLY_STATUS_LOCK),
      mutex_(common::ObLatchIds::MAX_APPLY_SCN_LOCK),
      get_info_debug_time_(OB_INVALID_TIMESTAMP),
      try_wrlock_debug_time_(OB_INVALID_TIMESTAMP),
      cb_append_stat_("[APPLY STAT CB APPEND COST TIME]", 5 * 1000 * 1000),
      cb_wait_thread_stat_("[APPLY STAT CB IN QUEUE TIME]", 5 * 1000 * 1000),
      cb_wait_commit_stat_("[APPLY STAT CB WAIT COMMIT TIME]", 5 * 1000 * 1000),
      cb_execute_stat_("[APPLY STAT CB EXECUTE TIME]", 5 * 1000 * 1000),
      cb_stat_("[APPLY STAT CB TOTAL TIME]", 5 * 1000 * 1000)
{
}

ObApplyStatus::~ObApplyStatus()
{
  destroy();
}

int ObApplyStatus::init(palf::PalfEnv *palf_env,
                        ObLogApplyService *ap_sv)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "apply status has already been inited", K(ret));
  } else if (OB_ISNULL(palf_env) || OB_ISNULL(ap_sv)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", KP(palf_env), K(ap_sv), K(ret));
  } else if (OB_FAIL(palf_env->open(palf_handle_))) {
    CLOG_LOG(ERROR, "failed to open palf handle", K(palf_env));
  } else if (OB_FAIL(submit_task_.init(this))) {
    CLOG_LOG(WARN, "failed to init submit_task", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < APPLY_TASK_QUEUE_SIZE; ++i) {
      if (OB_FAIL(cb_queues_[i].init(this, i))) {
        CLOG_LOG(WARN, "failed to init cb_queues", K(ret));
      }
    }
    if (OB_SUCCESS == ret) {
      accepting_append_ = false;
      ap_sv_ = ap_sv;
      palf_env_ = palf_env;
      last_check_scn_.reset();
      max_applied_cb_scn_.reset();
      get_info_debug_time_ = OB_INVALID_TIMESTAMP;
      try_wrlock_debug_time_ = OB_INVALID_TIMESTAMP;
      IGNORE_RETURN new (&fs_cb_) ObApplyFsCb(this);
      is_in_stop_state_ = false;
      if (OB_FAIL(palf_handle_.register_file_size_cb(&fs_cb_))) {
        CLOG_LOG(ERROR, "failed to register cb", K(ret));
      } else {
        is_inited_ = true;
        CLOG_LOG(INFO, "apply status init success", K(ret), KPC(this), KP(&cb_append_stat_),
                 KP(&cb_wait_thread_stat_), KP(&cb_wait_commit_stat_), KP(&cb_execute_stat_));
      }
    }
  }
  if (OB_FAIL(ret) && (OB_INIT_TWICE != ret)) {
    destroy();
  }
  return ret;
}

void ObApplyStatus::destroy()
{
  CLOG_LOG(INFO, "destuct apply status", KPC(this));
  close_palf_handle();
  is_inited_ = false;
  is_in_stop_state_ = true;
  accepting_append_ = false;
  fs_cb_.destroy();
  palf_committed_end_scn_.reset();
  palf_committed_end_lsn_.reset();
  last_check_scn_.reset();
  max_applied_cb_scn_.reset();
  get_info_debug_time_ = OB_INVALID_TIMESTAMP;
  try_wrlock_debug_time_ = OB_INVALID_TIMESTAMP;
  palf_env_ = NULL;
  ap_sv_ = NULL;
  submit_task_.reset();
  for (int64_t i = 0; i < APPLY_TASK_QUEUE_SIZE; ++i) {
    cb_queues_[i].reset();
  }
}

int ObApplyStatus::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    CLOG_LOG(WARN, "stop apply status after destroy", KPC(this));
  } else {
    int64_t abs_timeout_us = ObTimeUtility::current_time() + WRLOCK_RETRY_INTERVAL_US;
    if (OB_SUCC(lock_.wrlock(abs_timeout_us))) {
      is_in_stop_state_ = true;
      close_palf_handle();
      stop_local_append_();
      lock_.unlock();
    } else {
      ret = OB_EAGAIN;
      if (palf_reach_time_interval(1000 * 1000, try_wrlock_debug_time_)) {
        CLOG_LOG(INFO, "try lock failed in stop", KPC(this), KR(ret));
      }
    }
    CLOG_LOG(INFO, "stop apply status", KPC(this));
  }
  return ret;
}

void ObApplyStatus::inc_ref()
{
  ATOMIC_INC(&ref_cnt_);
}
int64_t ObApplyStatus::dec_ref()
{
  return ATOMIC_SAF(&ref_cnt_, 1);
}

int ObApplyStatus::push_append_cb(AppendCb *cb)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else {
    const int64_t start_ts = ObClockGenerator::getClock();
    LSN palf_committed_end_lsn;
    const LSN cb_lsn = cb->__get_lsn();
    const SCN cb_scn = cb->__get_scn();
    const uint64_t cb_sign = cb_scn.get_val_for_logservice();
    int64_t thread_index = cb_sign & (APPLY_TASK_QUEUE_SIZE - 1);
    ObLink *link = AppendCb::__get_member_address(cb);
    cb_queues_[thread_index].inc_total_submit_cb_cnt();
    if (OB_FAIL(cb_queues_[thread_index].push(link))) {
      cb_queues_[thread_index].inc_total_apply_cb_cnt();
      CLOG_LOG(ERROR, "push_append_cb failed", K(thread_index), K(cb_lsn), K(cb_sign),
               K(cb_queues_[thread_index]), KPC(this));
    } else {
      CLOG_LOG(TRACE, "push_append_cb", K(thread_index), K(cb_lsn), K(cb_sign), K(cb_queues_[thread_index]), KPC(this));
      palf_committed_end_lsn.val_ = ATOMIC_LOAD(&palf_committed_end_lsn_.val_);
      if (cb_lsn < palf_committed_end_lsn) {
        // The cb that needs to call on_success should actively trigger the push into the thread pool when entering the queue
        if (OB_FAIL(submit_task_to_apply_service_(cb_queues_[thread_index]))) {
          CLOG_LOG(ERROR, "apply service push_task failed", K(thread_index), K(cb_lsn), K(cb_sign), KPC(this));
        } else {
          CLOG_LOG(TRACE, "apply service push_task success", K(thread_index), K(cb_lsn), K(cb_sign), KPC(this));
        }
      }
    }
    const int64_t push_cost = ObClockGenerator::getClock() - start_ts;
    if (push_cost > 1 * 1000) { //1ms
      CLOG_LOG(INFO, "apply service push_task cost too much time", K(thread_index), K(cb_lsn),
               K(cb_sign), KPC(this), K(push_cost));
    }
  }
  return ret;
}

int ObApplyStatus::try_submit_cb_queues()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < APPLY_TASK_QUEUE_SIZE; ++i) {
      if (cb_queues_[i].get_total_apply_cb_cnt() >= cb_queues_[i].get_total_submit_cb_cnt()) {
        // do nothing
      } else if (OB_FAIL(submit_task_to_apply_service_(cb_queues_[i]))) {
        CLOG_LOG(ERROR, "apply service push_task failed", K(i), KPC(this));
      }
    }
    if (OB_SUCC(ret)) {
      CLOG_LOG(TRACE, "try_submit_cb_queues success", KPC(this));
    }
  }
  return ret;
}

int ObApplyStatus::try_handle_cb_queue(ObApplyServiceQueueTask *cb_queue,
                                       bool &is_timeslice_run_out)
{
  int ret = OB_SUCCESS;
  bool is_queue_empty = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply status not init", K(ret));
  } else if (OB_ISNULL(cb_queue)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "cb_queue is NULL", KPC(this), KR(ret));
  } else {
    int64_t start_time = ObTimeUtility::current_time();
    LSN lsn;
    SCN scn;
    int64_t append_start_time = OB_INVALID_TIMESTAMP;
    int64_t append_finish_time = OB_INVALID_TIMESTAMP;
    int64_t cb_first_handle_time = OB_INVALID_TIMESTAMP;
    int64_t cb_start_time = OB_INVALID_TIMESTAMP;
    int64_t idx = cb_queue->idx();
    RLockGuard guard(lock_);
    do {
      ObLink *link = NULL;
      AppendCb *cb = NULL;
      if (NULL == (link = cb_queue->top())) {
        CLOG_LOG(TRACE, "cb_queue empty", KPC(cb_queue), KPC(this));
        ret = OB_SUCCESS;
        is_queue_empty = true;
      } else if (OB_ISNULL(cb = AppendCb::__get_class_address(link))) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(ERROR, "cb is NULL", KPC(cb_queue), KPC(this), K(ret));
      } else if ((lsn = cb->__get_lsn()).val_ < ATOMIC_LOAD(&palf_committed_end_lsn_.val_)) {
        // callbacks with log position less than the confirmed log position can callback on_success
        if (OB_FAIL(cb_queue->pop())) {
          CLOG_LOG(ERROR, "cb_queue pop failed", KPC(cb_queue), KPC(this), K(ret));
        } else {
          scn = cb->__get_scn();
          get_cb_trace_(cb, append_start_time, append_finish_time, cb_first_handle_time, cb_start_time);
          CLOG_LOG(TRACE, "cb on_success", K(lsn), K(scn), KP(link->next_), KPC(cb_queue), KPC(this));
          if (OB_FAIL(cb->on_success())) {
            // Do not handle this type of failure case
            CLOG_LOG(ERROR, "cb on_success failed", KP(cb), K(ret), KPC(this));
            ret = OB_SUCCESS;
          }
          statistics_cb_cost_(lsn, scn, append_start_time, append_finish_time,
                              cb_first_handle_time, cb_start_time, idx);
          cb_queue->inc_total_apply_cb_cnt();
        }
      } else if (!accepting_append_) {
        // Fail callbacks that remain beyond the confirmed position after local append stops.
        if (OB_FAIL(cb_queue->pop())) {
          CLOG_LOG(ERROR, "cb_queue pop failed", KPC(cb_queue), KPC(this), K(ret));
        } else {
          scn = cb->__get_scn();
          get_cb_trace_(cb, append_start_time, append_finish_time, cb_first_handle_time, cb_start_time);
          CLOG_LOG(INFO, "cb on_failure", K(lsn), K(scn), KP(link->next_), KPC(cb_queue), KPC(this));
          if (OB_FAIL(cb->on_failure())) {
            CLOG_LOG(ERROR, "cb on_failure failed", KP(cb), K(ret), KPC(this));
            ret = OB_SUCCESS;
          }
          statistics_cb_cost_(lsn, scn, append_start_time, append_finish_time,
                              cb_first_handle_time, cb_start_time, idx);
          cb_queue->inc_total_apply_cb_cnt();
        }
      } else {
        cb->set_cb_first_handle_ts(ObTimeUtility::fast_current_time());
        CLOG_LOG(TRACE, "cb on_wait", K(lsn), K(cb->__get_scn()), KPC(cb_queue), KPC(this));
        // Wait for log position advancement or local append shutdown.
        ret = OB_EAGAIN;
      }
      if (OB_SUCC(ret) && !is_queue_empty) {
        int64_t used_time = ObTimeUtility::current_time() - start_time;
        if (used_time > MAX_HANDLE_TIME_US_PER_ROUND_US) {
          is_timeslice_run_out = true;
        }
      }
    } while (OB_SUCC(ret) && (!is_queue_empty) && (!is_timeslice_run_out));
    if (OB_EAGAIN == ret) {
      // wait for file_size_cb to push this task into the thread pool when end_lsn is not large enough, so errors need to be masked externally
      ret = OB_SUCCESS;
    }
  }
  CLOG_LOG(DEBUG, "try_handle_cb_queue finish", KPC(this), KPC(cb_queue), K(ret), K(is_queue_empty), K(is_timeslice_run_out));
  return ret;
}

int ObApplyStatus::is_apply_done(bool &is_done,
                                 palf::LSN &end_lsn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply status has not been inited");
  } else {
    is_done = true;
    for (int64_t i = 0; OB_SUCC(ret) && is_done && i < APPLY_TASK_QUEUE_SIZE; ++i) {
      if (OB_FAIL(cb_queues_[i].is_apply_done(is_done))) {
        CLOG_LOG(WARN, "check is_apply_done failed", K(ret), K(i));
      }
    }
    if (is_done) {
      end_lsn.val_ = ATOMIC_LOAD(&palf_committed_end_lsn_.val_);
    }
  }
  return ret;
}

int ObApplyStatus::start_local_append()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(lock_, WRLOCK_RETRY_INTERVAL_US, WRLOCK_RETRY_INTERVAL_US);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply status has not been inited");
  } else if (is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
    CLOG_LOG(INFO, "apply status has been stopped");
  } else if (accepting_append_) {
    ret = OB_STATE_NOT_MATCH;
    CLOG_LOG(WARN, "local append has already started", KPC(this));
  } else {
    accepting_append_ = true;
    CLOG_LOG(INFO, "apply status start_local_append success", KPC(this));
  }
  return ret;
}

int ObApplyStatus::stop_local_append()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(lock_, WRLOCK_RETRY_INTERVAL_US, WRLOCK_RETRY_INTERVAL_US);
  if (OB_FAIL(stop_local_append_())) {
    CLOG_LOG(WARN, "ObApplyStatus stop_local_append_ failed", K(ret));
  }
  return ret;
}
//Needs lock protection
int ObApplyStatus::stop_local_append_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply status has not been inited");
  } else if (!accepting_append_) {
    CLOG_LOG(INFO, "local append has already stopped", KPC(this), K(ret));
  } else {
    lib::ObMutexGuard guard(mutex_);
    last_check_scn_.reset();
    accepting_append_ = false;
    if (OB_FAIL(submit_task_to_apply_service_(submit_task_))) {
      CLOG_LOG(ERROR, "submit_task_to_apply_service_ failed", KPC(this), K(ret));
    } else {
      CLOG_LOG(INFO, "stop_local_append submit_task_to_apply_service_ success", KPC(this), K(ret));
    }
  }
  return ret;
}
//Single-threaded call
int ObApplyStatus::update_palf_committed_end_lsn(const palf::LSN &end_lsn,
                                                 const SCN &end_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(IS_NOT_INIT)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply status has not been inited", K(ret));
  } else {
    {
      RLockGuard guard(lock_);
      if (is_in_stop_state_) {
        //skip
        CLOG_LOG(WARN, "apply status has been stopped", K(ret), KPC(this));
      } else if (accepting_append_) {
        if (palf_committed_end_lsn_ >= end_lsn) {
          CLOG_LOG(ERROR, "invalid new end_lsn", KPC(this), K(end_lsn));
        } else {
          palf_committed_end_scn_.atomic_store(end_scn);
          palf_committed_end_lsn_ = end_lsn;
          if (OB_FAIL(submit_task_to_apply_service_(submit_task_))) {
            CLOG_LOG(ERROR, "submit_task_to_apply_service_ failed", KPC(this), K(ret), K(end_lsn));
          }
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(ERROR, "invalid new end_lsn", KPC(this), K(end_lsn));
      }
    }
    CLOG_LOG(TRACE, "update_palf_committed_end_lsn", KPC(this), K(end_lsn), KR(ret));
  }
  return ret;
}

share::SCN ObApplyStatus::get_palf_committed_end_scn() const
{
  share::SCN scn = palf_committed_end_scn_.atomic_load();
  return scn;
}

int ObApplyStatus::unregister_file_size_cb()
{
  int ret = OB_SUCCESS;
  if (palf_handle_.is_valid()) {
    if (OB_FAIL(palf_handle_.unregister_file_size_cb())) {
      CLOG_LOG(ERROR, "failed to unregister cb", K(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

void ObApplyStatus::close_palf_handle()
{
  if (palf_handle_.is_valid()) {
    palf_env_->close(palf_handle_);
  } else {
    // do nothing
  }
}

int ObApplyStatus::get_max_applied_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  //Ensure this interface is not called concurrently, the order of the two locks cannot be changed
  RLockGuard rguard(lock_);
  lib::ObMutexGuard guard(mutex_);
  const SCN last_check_scn = last_check_scn_;
  if (OB_UNLIKELY(IS_NOT_INIT)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply status has not been inited", K(ret));
  } else if (OB_UNLIKELY(is_in_stop_state_)) {
    // After stopping, return the last cached callback point.
  } else if (!accepting_append_) {
    palf::LSN apply_end_lsn;
    bool is_done = false;
    const SCN cur_palf_committed_end_scn = palf_committed_end_scn_.atomic_load();
    if (max_applied_cb_scn_ == cur_palf_committed_end_scn) {
      //no need to push up
    } else if (OB_FAIL(is_apply_done(is_done, apply_end_lsn))) {
      CLOG_LOG(WARN, "check is_apply_done failed", K(ret), KPC(this));
    } else if (!is_done) {
      // Do not update until all callbacks are complete.
      // All cb callbacks completed, attempt to advance the maximum consecutive callback point once
    } else if (max_applied_cb_scn_ < cur_palf_committed_end_scn) {
      max_applied_cb_scn_ = cur_palf_committed_end_scn;
      CLOG_LOG(INFO, "update max_applied_cb_scn_", K(cur_palf_committed_end_scn), KPC(this));
    }
  } else if ((!last_check_scn.is_valid()) || last_check_scn == max_applied_cb_scn_) {
    if (OB_FAIL(update_last_check_scn_())) {
      CLOG_LOG(ERROR, "update_last_check_scn_ failed", K(ret), KPC(this));
    } else {
      //do nothing
    }
  } else if (!max_applied_cb_scn_.is_valid() || last_check_scn > max_applied_cb_scn_) {
    // Check if last_check_scn_ has already been callback completed
    bool is_done = true;
    for (int64_t i = 0; OB_SUCC(ret) && is_done && i < APPLY_TASK_QUEUE_SIZE; ++i) {
      if (OB_FAIL(cb_queues_[i].is_snapshot_apply_done(is_done))) {
        CLOG_LOG(WARN, "check is_snapshot_apply_done failed", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret) && is_done) {
      max_applied_cb_scn_ = last_check_scn;
      CLOG_LOG(TRACE, "is_snapshot_apply_done", K(ret), KPC(this));
      if (OB_FAIL(update_last_check_scn_())) {
        CLOG_LOG(ERROR, "update_last_check_scn_ failed", K(ret), KPC(this));
      } else {
        //do nothing
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "max_applied_cb_scn_ larger than last_check_scn_, unexpected", K(ret), KPC(this));
  }
  scn = max_applied_cb_scn_;
  CLOG_LOG(TRACE, "get_max_applied_scn finish", K(ret), KPC(this), K(scn));
  if (palf_reach_time_interval(5 * 1000 * 1000, get_info_debug_time_)) {
    CLOG_LOG(INFO, "get_max_applied_scn", K(scn), KPC(this));
  }
  return ret;
}

int ObApplyStatus::stat(LSApplyStat &stat) const
{
  int ret = OB_SUCCESS;
  RLockGuard rlock(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    stat.end_lsn_ = palf_committed_end_lsn_;
    stat.pending_cnt_ = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < APPLY_TASK_QUEUE_SIZE; ++i) {
      stat.pending_cnt_ -= (cb_queues_[i].get_total_apply_cb_cnt() - cb_queues_[i].get_total_submit_cb_cnt());
    }
  }
  return ret;
}

int ObApplyStatus::handle_drop_cb()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < APPLY_TASK_QUEUE_SIZE; ++i) {
    if (OB_FAIL(handle_drop_cb_queue_(cb_queues_[i]))) {
      CLOG_LOG(ERROR, "handle_drop_cb_queue_ failed", KPC(this), K(i), K(ret));
    }
  }
  CLOG_LOG(INFO, "handle_drop_cb finish", KPC(this), K(ret));
  return ret;
}

int ObApplyStatus::diagnose(ApplyDiagnoseInfo &diagnose_info)
{
  int ret = OB_SUCCESS;
  SCN max_applied_scn;
  if (OB_FAIL(get_max_applied_scn(max_applied_scn))) {
    CLOG_LOG(WARN, "get_max_applied_scn failed", KPC(this), K(ret));
  } else {
    diagnose_info.max_applied_scn_ = max_applied_scn;
  }
  return ret;
}

void ObApplyStatus::reset_meta()
{
  RLockGuard rlock(lock_);
  lib::ObMutexGuard guard(mutex_);
  last_check_scn_.reset();
  max_applied_cb_scn_.reset();
  // palf_committed_end_scn_ also should be reset along with palf_committed_end_lsn_.
  palf_committed_end_lsn_.val_ = 0;
  palf_committed_end_scn_.reset();
}

int ObApplyStatus::submit_task_to_apply_service_(ObApplyServiceTask &task)
{
  int ret = OB_SUCCESS;
  if (task.acquire_lease()) {
    inc_ref(); //Add the reference count first, if the task push failed, dec_ref() is required
    if (OB_FAIL(ap_sv_->push_task(&task))) {
      CLOG_LOG(ERROR, "failed to submit task to apply service", KPC(this),
               K(task), K(ret));
      dec_ref();
    } else {
      CLOG_LOG(TRACE, "push task to apply service", KPC(this),
               K(task), K(ret));
    }
  } else {
    CLOG_LOG(TRACE, "acquire lease failed", KPC(this),
             K(task), K(ret));
  }
  return ret;
}

int ObApplyStatus::update_last_check_scn_()
{
  int ret = OB_SUCCESS;
  SCN palf_max_scn;
  if (OB_FAIL(palf_handle_.get_max_scn(palf_max_scn))) {
    CLOG_LOG(WARN, "get_max_scn failed", K(ret));
  } else if (max_applied_cb_scn_.is_valid() && palf_max_scn < max_applied_cb_scn_) {
    //Defensive check, palf's max_scn should not regress to before the agreed max_applied_cb_scn_
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "invalid palf_max_scn", K(ret), K(palf_max_scn), KPC(this));
  } else if (OB_FAIL(ap_sv_->wait_append_sync())) {
    CLOG_LOG(WARN, "wait_append_sync failed", K(ret), KPC(this));
  } else {
    for (int64_t i = 0; i < APPLY_TASK_QUEUE_SIZE; ++i) {
      cb_queues_[i].set_snapshot_check_submit_cb_cnt();
    }
    last_check_scn_ = palf_max_scn;
  }
  return ret;
}

int ObApplyStatus::handle_drop_cb_queue_(ObApplyServiceQueueTask &cb_queue)
{
  int ret = OB_SUCCESS;
  bool is_queue_empty = false;
  LSN lsn;
  SCN scn;
  int64_t append_start_time = OB_INVALID_TIMESTAMP;
  int64_t append_finish_time = OB_INVALID_TIMESTAMP;
  int64_t cb_first_handle_time = OB_INVALID_TIMESTAMP;
  int64_t cb_start_time = OB_INVALID_TIMESTAMP;
  int64_t idx = cb_queue.idx();
  do {
    ObLink *link = NULL;
    AppendCb *cb = NULL;
    if (NULL == (link = cb_queue.top())) {
      CLOG_LOG(TRACE, "cb_queue empty", K(cb_queue));
      ret = OB_SUCCESS;
      is_queue_empty = true;
    } else if (OB_ISNULL(cb = AppendCb::__get_class_address(link))) {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(ERROR, "cb is NULL", K(cb_queue), KPC(this), K(ret));
    } else if (OB_FAIL(cb_queue.pop())) {
      CLOG_LOG(ERROR, "cb_queue pop failed", K(cb_queue), K(ret), KPC(this));
    } else {
      lsn = cb->__get_lsn();
      scn = cb->__get_scn();
      get_cb_trace_(cb, append_start_time, append_finish_time, cb_first_handle_time, cb_start_time);
      CLOG_LOG(INFO, "cb on_failure", K(lsn), K(scn), KP(link->next_), K(cb_queue), KPC(this));
      if (OB_FAIL(cb->on_failure())) {
        CLOG_LOG(ERROR, "cb on_failure failed", KP(cb), K(ret), KPC(this));
        ret = OB_SUCCESS;
      }
      statistics_cb_cost_(lsn, scn, append_start_time, append_finish_time,
                          cb_first_handle_time, cb_start_time, idx);
      cb_queue.inc_total_apply_cb_cnt();
    }
  } while (OB_SUCC(ret) && (!is_queue_empty));
  CLOG_LOG(TRACE, "handle_drop_cb_queue_ finish", K(cb_queue), K(ret), K(is_queue_empty), KPC(this));
  return ret;
}

void ObApplyStatus::get_cb_trace_(AppendCb *cb,
                                  int64_t &append_start_time,
                                  int64_t &append_finish_time,
                                  int64_t &cb_first_handle_time,
                                  int64_t &cb_start_time)
{
  if (NULL != cb) {
    cb_start_time = ObTimeUtility::fast_current_time();
    append_start_time = cb->get_append_start_ts();
    append_finish_time = cb->get_append_finish_ts();
    if (common::OB_INVALID_TIMESTAMP == cb->get_cb_first_handle_ts()) {
      cb_first_handle_time = cb_start_time;
    } else {
      cb_first_handle_time = cb->get_cb_first_handle_ts();
    }
  }
}

void ObApplyStatus::statistics_cb_cost_(const LSN &lsn,
                                        const SCN &scn,
                                        const int64_t append_start_time,
                                        const int64_t append_finish_time,
                                        const int64_t cb_first_handle_time,
                                        const int64_t cb_start_time,
                                        const int64_t idx)
{
  // no need to print debug log when config [default value is true] is false;
  if (REACH_TIME_INTERVAL(10 * 1000)) {
    const int64_t cb_finish_time = common::ObClockGenerator::getClock();
    int64_t total_cost_time = cb_finish_time - append_start_time;
    int64_t append_cost_time = append_finish_time - append_start_time;
    int64_t cb_wait_thread_time = cb_first_handle_time - append_finish_time;
    int64_t cb_wait_commit_time = cb_start_time - cb_first_handle_time;
    int64_t cb_cost_time = cb_finish_time - cb_start_time;
    cb_append_stat_.stat(append_cost_time);
    cb_wait_thread_stat_.stat(cb_wait_thread_time);
    cb_wait_commit_stat_.stat(cb_wait_commit_time);
    cb_execute_stat_.stat(cb_cost_time);
    cb_stat_.stat(total_cost_time);
    if (total_cost_time > 1000 * 1000) { //1s
      CLOG_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "cb cost too much time", K(lsn), K(scn), K(idx), K(total_cost_time), K(append_cost_time),
               K(cb_wait_thread_time), K(cb_wait_commit_time), K(cb_cost_time), K(append_start_time), K(append_finish_time),
               K(cb_first_handle_time), K(cb_finish_time));
    }
  }
}

//---------------ObApplyStatusGuard---------------//
ObApplyStatusGuard::ObApplyStatusGuard()
    : apply_status_(NULL)
{}

ObApplyStatusGuard::~ObApplyStatusGuard()
{
  if (NULL != apply_status_) {
    if (0 == apply_status_->dec_ref()) {
      CLOG_LOG(INFO, "free apply status", KPC(apply_status_));
      apply_status_->~ObApplyStatus();
      server_free(apply_status_);
    }
    apply_status_ = NULL;
  }
}

ObApplyStatus *ObApplyStatusGuard::get_apply_status()
{
  return apply_status_;
}

void ObApplyStatusGuard::set_apply_status_(ObApplyStatus *apply_status)
{
  apply_status_ = apply_status;
  apply_status_->inc_ref();
}

//---------------ObLogApplyService---------------//
ObLogApplyService::ObLogApplyService()
    : is_inited_(false),
      is_running_(false),
      palf_env_(NULL),
      log_storage_(NULL),
      apply_status_(NULL),
      lock_()
  {}

ObLogApplyService::~ObLogApplyService()
{
  destroy();
}

int ObLogApplyService::init(PalfEnv *palf_env,
                            ObILogStorage *log_storage)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "ObLogApplyService init twice", K(ret));
  } else if (OB_ISNULL(palf_env_= palf_env)
             || OB_ISNULL(log_storage_ = log_storage)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(ret), KP(palf_env), KP(log_storage));
  } else if (OB_FAIL(common::ObLinkQueueThreadPool::init(
      1,
      common::APPLY_TASK_QUEUE_SIZE + 1,
      "ApplySrv"))) {
    CLOG_LOG(WARN, "fail to init apply service thread pool", K(ret));
  } else if (OB_FAIL(common::ObLinkQueueThreadPool::set_adaptive_thread(1, 1))) {
    CLOG_LOG(WARN, "fail to set apply service thread count", K(ret));
  } else if (OB_FAIL(lock_.init(ObMemAttr("ApplyStatus")))) {
    CLOG_LOG(WARN, "apply status lock init error", K(ret));
  } else {
    is_inited_ = true;
    CLOG_LOG(INFO, "ObLogApplyService init success", K(is_inited_));
  }
  if ((OB_FAIL(ret)) && (OB_INIT_TWICE != ret)) {
    destroy();
    CLOG_LOG(WARN, "ObLogApplyService init failed", K(ret));
  }
  return ret;
}

void ObLogApplyService::destroy()
{
  if (is_inited_) {
    (void)remove_status();
  }
  is_inited_ = false;
  is_running_ = false;
  CLOG_LOG(INFO, "apply service destroy");
  common::ObLinkQueueThreadPool::stop();
  common::ObLinkQueueThreadPool::wait();
  common::ObLinkQueueThreadPool::destroy();
  palf_env_ = NULL;
  log_storage_ = NULL;
  lock_.destroy();
}

int ObLogApplyService::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(IS_NOT_INIT)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "ObLogApplyService has not been initialized", K(ret));
  } else if (common::ObLinkQueueThreadPool::get_thread_count() <= 0
      && !common::ObLinkQueueThreadPool::try_expand_one(1)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "start ObLogApplyService failed", K(ret));
  } else {
    ATOMIC_STORE(&is_running_, true);
    CLOG_LOG(INFO, "start ObLogApplyService success", K(ret));
  }
  return ret;
}

void ObLogApplyService::stop()
{
  CLOG_LOG(INFO, "ObLogApplyService stop begin");
  ATOMIC_STORE(&is_running_, false);
  //Ensure that no new tasks will enter the thread pool when handle_drop is called
  common::ObLinkQueueThreadPool::stop();
  CLOG_LOG(INFO, "ObLogApplyService stop finish");
}

void ObLogApplyService::wait()
{
  CLOG_LOG(INFO, "ObLogApplyService wait begin");
  int64_t num = 0;
  while ((num = common::ObLinkQueueThreadPool::get_queue_num()) > 0) {
    PAUSE();
  }
  common::ObLinkQueueThreadPool::wait();
  //At this point, it can be guaranteed that no new queue tasks will enter the thread pool,
  //At the same time, all other modules have called stop, so no new cb will enter the queue
  //It is safe to clean up all remaining cbs
  (void)remove_status();
  CLOG_LOG(INFO, "ObLogApplyService wait finish");
}

int ObLogApplyService::create_status()
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObMemAttr attr(ObModIds::OB_LOG_APPLY_STATUS);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (false == ATOMIC_LOAD(&is_running_)) {
    ret = OB_STATE_NOT_MATCH;
    CLOG_LOG(ERROR, "apply service has been stopped", K(ret));
  } else if (NULL == (apply_status = static_cast<ObApplyStatus*>(server_malloc(sizeof(ObApplyStatus), attr)))){
    ret = OB_ALLOCATE_MEMORY_FAILED;
    CLOG_LOG(WARN, "failed to alloc apply status", K(ret));
  } else {
    new (apply_status) ObApplyStatus();
    if (OB_FAIL(apply_status->init(palf_env_, this))) {
      server_free(apply_status);
      apply_status = NULL;
      CLOG_LOG(WARN, "failed to init apply status", K(ret), K(palf_env_), K(this));
    } else {
      apply_status->inc_ref();
      {
        ObQSyncLockWriteGuard guard(lock_);
        if (OB_NOT_NULL(apply_status_)) {
          ret = OB_ENTRY_EXIST;
        } else {
          apply_status_ = apply_status;
          apply_status = NULL;
        }
      }
      if (OB_NOT_NULL(apply_status)) {
        revert_apply_status(apply_status);
      }
    }
  }
  return ret;
}

int ObLogApplyService::remove_status()
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else {
    {
      ObQSyncLockWriteGuard guard(lock_);
      apply_status = apply_status_;
      apply_status_ = NULL;
    }
    if (OB_NOT_NULL(apply_status)) {
      if (OB_FAIL(apply_status->handle_drop_cb())) {
        CLOG_LOG(ERROR, "apply status handle drop cb failed", K(ret));
      } else if (OB_FAIL(apply_status->unregister_file_size_cb())) {
        CLOG_LOG(ERROR, "apply status unregister file size cb failed", K(ret));
      }
      revert_apply_status(apply_status);
    }
  }
  return ret;
}

int ObLogApplyService::is_apply_done(bool &is_done,
                                     palf::LSN &end_lsn)
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "guard get apply status failed", K(ret));
  } else if (NULL == (apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(apply_status->is_apply_done(is_done, end_lsn))) {
    CLOG_LOG(WARN, "apply status check is_apply_done failed", K(ret), K(is_done));
  } else {
    CLOG_LOG(TRACE, "apply service check is_apply_done", K(is_done), K(end_lsn));
  }
  return ret;
}

int ObLogApplyService::start_local_append()
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "guard get apply status failed", K(ret));
  } else if (NULL == (apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(apply_status->start_local_append())) {
    CLOG_LOG(WARN, "apply status start_local_append failed", K(ret));
  } else {
    CLOG_LOG(INFO, "apply service start_local_append success");
  }
  return ret;
}

int ObLogApplyService::stop_local_append()
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "guard get apply status failed", K(ret));
  } else if (NULL == (apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(apply_status->stop_local_append())) {
    CLOG_LOG(WARN, "apply status stop_local_append failed", K(ret));
  } else {
    CLOG_LOG(INFO, "apply service stop_local_append success");
  }
  return ret;
}

int ObLogApplyService::get_max_applied_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "guard get apply status failed", K(ret));
  } else if (NULL == (apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(apply_status->get_max_applied_scn(scn))) {
    CLOG_LOG(WARN, "apply status get_max_applied_scn failed", K(ret));
  } else {
    CLOG_LOG(TRACE, "apply service get_max_applied_scn success");
  }
  return ret;
}

int ObLogApplyService::get_palf_committed_end_scn(share::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "guard get apply status failed", K(ret));
  } else if (NULL == (apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else {
    scn = apply_status->get_palf_committed_end_scn();
    CLOG_LOG(TRACE, "apply service get palf_committed_end_lsn success", K(scn));
  }
  return ret;
}

int ObLogApplyService::push_task(ObApplyServiceTask *task)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "apply service not init", K(ret));
  } else if (OB_ISNULL(task)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "task is NULL", K(ret));
  } else {
    while (OB_FAIL(common::ObLinkQueueThreadPool::push(task)) && OB_EAGAIN == ret) {
      //Expected not to fail
      ob_throttle_usleep(1000, ret); //1ms
      CLOG_LOG(ERROR, "failed to push", K(ret));
    }
  }
  return ret;
}

int ObLogApplyService::get_apply_status(ObApplyStatusGuard &guard)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObLogApplyService not init", K(ret));
  } else {
    ObQSyncLockReadGuard lock_guard(lock_);
    if (OB_ISNULL(apply_status_)) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      guard.set_apply_status_(apply_status_);
    }
  }
  return ret;
}

void ObLogApplyService::revert_apply_status(ObApplyStatus *apply_status)
{
  if (NULL != apply_status) {
    if (0 == apply_status->dec_ref()) {
      CLOG_LOG(INFO, "free apply status", KPC(apply_status));
      apply_status->~ObApplyStatus();
      server_free(apply_status);
      apply_status = NULL;
    }
  }
}

void ObLogApplyService::handle(common::LinkTask *task)
{
  int ret = OB_SUCCESS;
  ObApplyServiceTask *task_to_handle = static_cast<ObApplyServiceTask *>(task);
  ObApplyStatus *apply_status = NULL;
  bool need_push_back = false;
  if (OB_ISNULL(task_to_handle)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "task is null", K(ret));
  } else if (OB_ISNULL(apply_status = task_to_handle->get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "apply status is NULL", K(ret), K(task_to_handle));
  } else if (OB_UNLIKELY(IS_NOT_INIT)) {
    ret = OB_NOT_INIT;
    revert_apply_status(apply_status);
    task_to_handle = NULL;
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      CLOG_LOG(ERROR, "apply service is not inited", K(ret));
    }
  } else {
    bool is_timeslice_run_out = false;
    ObApplyServiceTaskType task_type = task_to_handle->get_type();
    if (ObApplyServiceTaskType::APPLY_LOG_TASK == task_type) {
      ObApplyServiceQueueTask *cb_queue = static_cast<ObApplyServiceQueueTask *>(task_to_handle);
      ret = handle_cb_queue_(apply_status, cb_queue, is_timeslice_run_out);
    } else if (ObApplyServiceTaskType::SUBMIT_LOG_TASK == task_type) {
      ObApplyServiceSubmitTask *submit_task = static_cast<ObApplyServiceSubmitTask *>(task_to_handle);
      ret = handle_submit_task_(apply_status);
    } else {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(ERROR, "invalid task_type", K(ret), K(task_type), KPC(apply_status));
    }

    if (OB_FAIL(ret) || is_timeslice_run_out) {
      need_push_back = true;
    } else if (task_to_handle->revoke_lease()) {
      //success to set state to idle, no need to push back
      revert_apply_status(apply_status);
    } else {
      need_push_back = true;
    }
  }
  if ((OB_FAIL(ret) || need_push_back) && (NULL != task_to_handle)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = push_task(task_to_handle))) {
      CLOG_LOG(ERROR, "push task back after handle failed", K(tmp_ret), KPC(task_to_handle), KPC(apply_status), K(ret));
      revert_apply_status(apply_status);
    } else {
      //do nothing
    }
  }
}

void ObLogApplyService::handle_drop(common::LinkTask *task)
{
  int ret = OB_SUCCESS;
  ObApplyServiceTask *task_to_handle = static_cast<ObApplyServiceTask *>(task);
  ObApplyStatus *apply_status = NULL;
  bool need_push_back = false;
  if (OB_UNLIKELY(IS_NOT_INIT)) {
    ret = OB_NOT_INIT;
    revert_apply_status(apply_status);
    task_to_handle = NULL;
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      CLOG_LOG(ERROR, "apply service is not inited", K(ret));
    }
  } else if (OB_ISNULL(task_to_handle)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "task is null", K(ret));
  } else if (OB_ISNULL(apply_status = task_to_handle->get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "apply status is NULL", K(ret), K(task_to_handle));
  } else {
    revert_apply_status(apply_status);
  }
}

int ObLogApplyService::wait_append_sync()
{
  int ret = OB_SUCCESS;
  int64_t start_ts = ObTimeUtility::fast_current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObLogApplyService not init", K(ret));
  } else if (OB_FAIL(log_storage_->wait_append_sync())) {
    CLOG_LOG(WARN, "wait_append_sync failed", K(ret));
  // TODO:@keqing.llt Remove before release
  } else {
    int64_t cost_time = ObTimeUtility::fast_current_time() - start_ts;
    if (cost_time > 10 * 1000) { //10ms
      CLOG_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "wait_append_sync cost too much time", K(ret), K(cost_time));
    }
  }
  return ret;
}

int ObLogApplyService::stat(LSApplyStat &apply_stat)
{
  int ret = OB_SUCCESS;
  ObApplyStatusGuard guard;
  ObApplyStatus *apply_status = NULL;
  if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "failed to get apply status", K(ret));
  } else if (OB_ISNULL(apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is null", K(ret));
  } else if (OB_FAIL(apply_status->stat(apply_stat))) {
    CLOG_LOG(WARN, "stat apply failed", K(ret), KPC(apply_status));
  }
  return ret;
}

int ObLogApplyService::diagnose(ApplyDiagnoseInfo &diagnose_info)
{
  int ret = OB_SUCCESS;
  ObApplyStatus *apply_status = NULL;
  ObApplyStatusGuard guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "apply service not init", K(ret));
  } else if (OB_FAIL(get_apply_status(guard))) {
    CLOG_LOG(WARN, "guard get apply status failed", K(ret));
  } else if (NULL == (apply_status = guard.get_apply_status())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "apply status is not exist", K(ret));
  } else if (OB_FAIL(apply_status->diagnose(diagnose_info))) {
    CLOG_LOG(WARN, "apply status diagnose failed", K(ret));
  } else {
    CLOG_LOG(TRACE, "apply service diagnose success");
  }
  return ret;
}

int ObLogApplyService::handle_cb_queue_(ObApplyStatus *apply_status,
                                        ObApplyServiceQueueTask *cb_queue,
                                        bool &is_timeslice_run_out)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(apply_status->try_handle_cb_queue(cb_queue, is_timeslice_run_out))) {
    CLOG_LOG(WARN, "handle_cb_queue_ failed", KPC(apply_status), KPC(cb_queue), K(ret));
  }
  return ret;
}

int ObLogApplyService::handle_submit_task_(ObApplyStatus *apply_status)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(apply_status->try_submit_cb_queues())) {
    CLOG_LOG(WARN, "handle_submit_task_ failed", KPC(apply_status), K(ret));
  } else {
    CLOG_LOG(TRACE, "handle_submit_task_ success", KPC(apply_status), K(ret));
  }
  return ret;
}
} // namespace logservice
} // namespace oceanbase
