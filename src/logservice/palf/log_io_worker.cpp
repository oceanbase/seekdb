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
#include "share/rc/ob_server_runtime.h"

#include "log_io_worker.h"
#include "palf_env_impl.h"                    // PalfEnvImpl

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{
LogIOWorker::LogIOWorker()
    : cb_thread_pool_(NULL),
      palf_env_impl_(NULL),
      do_task_used_ts_(0),
      do_task_count_(0),
      print_log_interval_(OB_INVALID_TIMESTAMP),
      last_working_time_(OB_INVALID_TIMESTAMP),
      throttle_(NULL),
      log_io_worker_queue_size_stat_("[PALF STAT LOG IO WORKER QUEUE SIZE]", PALF_STAT_PRINT_INTERVAL_US),
      wait_cost_stat_("[PALF STAT IO TASK IN QUEUE TIME]", PALF_STAT_PRINT_INTERVAL_US),
      is_inited_(false)
{
}

LogIOWorker::~LogIOWorker()
{
  destroy();
}

int LogIOWorker::init(const LogIOWorkerConfig &config,
                      LogIOTaskCbThreadPool *cb_thread_pool,
                      ObIAllocator *allocator,
                      LogWritingThrottle *throttle,
                      IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "LogIOWorker has been inited", K(ret));
  } else if (false == config.is_valid() || OB_ISNULL(cb_thread_pool) || OB_ISNULL(allocator)
      || OB_ISNULL(throttle) || OB_ISNULL(palf_env_impl)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid argument!!!", K(ret), K(config), KP(cb_thread_pool), KP(allocator),
        KP(throttle), KP(palf_env_impl));
  } else if (OB_FAIL(queue_.init(config.io_queue_capcity_, "IOWorkerLQ"))) {
  } else if (OB_FAIL(batch_io_task_mgr_.init(config.batch_depth_,
                                             allocator,
                                             &wait_cost_stat_))) {
  } else {
    share::ObThreadPool::set_run_wrapper(share::server_runtime());
    cb_thread_pool_ = cb_thread_pool;
    palf_env_impl_ = palf_env_impl;
    PALF_REPORT_INFO_KV(KP_(cb_thread_pool));
    throttle_ = throttle;
    log_io_worker_queue_size_stat_.set_extra_info(EXTRA_INFOS);
    is_inited_ = true;
    PALF_LOG(INFO, "LogIOWorker init success", K(config), KP(cb_thread_pool));
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

void LogIOWorker::destroy()
{
  (void)stop();
  (void)wait();
  is_inited_ = false;
  last_working_time_ = OB_INVALID_TIMESTAMP;
  throttle_ = NULL;
  cb_thread_pool_ = NULL;
  palf_env_impl_ = NULL;
  queue_.destroy();
  batch_io_task_mgr_.destroy();
}

int LogIOWorker::submit_io_task(LogIOTask *io_task)
{
  if (IS_NOT_INIT) { return OB_NOT_INIT; }
  if (OB_ISNULL(io_task)) { return OB_INVALID_ARGUMENT; }
  return queue_.push(io_task);
}

void LogIOWorker::run1()
{
  lib::set_thread_name("IOWorker");
  (void) run_loop_();
}

int LogIOWorker::handle_io_task_with_throttling_(LogIOTask *io_task)
{
  int ret = OB_SUCCESS;
  const int64_t size = io_task->get_io_size();
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = throttle_->throttling(size, palf_env_impl_))) {
    LOG_ERROR_RET(tmp_ret, "failed to do throttling");
  }
  if (OB_FAIL(io_task->do_task(cb_thread_pool_, palf_env_impl_))) {
  } else if (OB_SUCCESS != (tmp_ret = throttle_->after_append_log(size))) {
    LOG_ERROR_RET(tmp_ret, "after_append failed");
  }
  return ret;
}

int LogIOWorker::handle_io_task_(LogIOTask *io_task)
{
  int ret = OB_SUCCESS;
	int64_t start_ts = ObTimeUtility::current_time();
  wait_cost_stat_.stat(start_ts - io_task->get_init_task_ts());
  if (OB_FAIL(handle_io_task_with_throttling_(io_task))) {
    io_task->free_this(palf_env_impl_);
  }
	int64_t cost_ts = ObTimeUtility::current_time() - start_ts;
	do_task_used_ts_ += cost_ts;
	do_task_count_ ++;
	if (palf_reach_time_interval(5 * 1000 * 1000, print_log_interval_)) {
		PALF_LOG(INFO, "[PALF STAT IO STAT]", K_(do_task_used_ts), K_(do_task_count),
				"average_cost_ts", do_task_used_ts_ / do_task_count_,
				"io_queue_size", queue_.size());
		do_task_count_ = 0;
		do_task_used_ts_ = 0;
	};
  return ret;
}

int LogIOWorker::run_loop_()
{
  int ret = OB_SUCCESS;

  while (false == has_set_stop()
      && false == (OB_NOT_NULL(&lib::Thread::current()) ? lib::Thread::current().has_set_stop() : false)) {
    ObLink *task = NULL;
    if (OB_SUCC(queue_.pop(task, QUEUE_WAIT_TIME))) {
      ATOMIC_STORE(&last_working_time_, common::ObTimeUtility::fast_current_time());
      update_throttling_options_();
      ret = reduce_io_task_(task);
      ATOMIC_STORE(&last_working_time_, OB_INVALID_TIMESTAMP);
    }
    if (queue_.size() > 0) {
      log_io_worker_queue_size_stat_.stat(queue_.size());
    }
  }

  // After IOWorker has stopped, need clear queue_.
  if (true == has_set_stop()) {
    ObLink *task = NULL;
    ObILogAllocator *allocator = palf_env_impl_->get_log_allocator();
    CLOG_LOG(INFO, "before LogIOWorker destory", KPC(this), KPC(allocator));
    while (OB_SUCC(queue_.pop(task))) {
      LogIOTask *io_task = static_cast<LogIOTask *>(task);
      ATOMIC_STORE(&last_working_time_, common::ObTimeUtility::fast_current_time());
      (void)handle_io_task_(io_task);
      ATOMIC_STORE(&last_working_time_, OB_INVALID_TIMESTAMP);
    }
    CLOG_LOG(INFO, "after LogIOWorker destory", KPC(this), KPC(allocator));
  }
  return ret;
}

bool LogIOWorker::need_reduce_(LogIOTask *io_task)
{
  bool bool_ret = false;
  switch (io_task->get_io_task_type()) {
    case LogIOTaskType::FLUSH_LOG_TYPE:
      bool_ret = true;
      break;
    case LogIOTaskType::TRUNCATE_PREFIX_TYPE:
    default:
      break;
  }
  // Consume one write at a time while disk throttling is active.
  return bool_ret && !throttle_->need_writing_throttling_notified();
}

int LogIOWorker::reduce_io_task_(ObLink *task)
{
  OB_ASSERT(true == batch_io_task_mgr_.empty());
  int ret = OB_SUCCESS;
  LogIOTask *io_task = NULL;
  bool last_io_task_has_been_reduced = true;

  // termination conditions for aggregation:
  // 1. the top LogIOTask of 'queue_' can not be aggreated
  // 2. there is no usable BatchLogIOFlushLogTask in 'batch_io_task_mgr_'.
  // 3. there is no LogIOTask in 'queue_'
  int tmp_ret = OB_SUCCESS;
  while (OB_SUCCESS == tmp_ret && true == last_io_task_has_been_reduced) {
    io_task = static_cast<LogIOTask *>(task);
    BatchLogIOFlushLogTask *batch_io_flush_task = NULL;
    if (OB_ISNULL(io_task)) {
      tmp_ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "io task is nullptr, unexpected error!!!", K(ret));
    } else if (false == need_reduce_(io_task)) {
      last_io_task_has_been_reduced = false;
    } else {
      LogIOFlushLogTask *flush_log_task = reinterpret_cast<LogIOFlushLogTask *>(io_task);
      // When insert 'flush_log_task' to batch_io_task_mgr_ failed, need
      // stop aggreating.
      // 1. there is no available BatchLogIOFlushLogTask in 'batch_io_task_mgr_';
      // 2. there is full in each BatchLogIOFlushLogTask in 'batch_io_task_mgr_'.
      if (OB_SUCCESS != (tmp_ret = batch_io_task_mgr_.insert(flush_log_task))) {
        last_io_task_has_been_reduced = false;
      } else if (OB_SUCCESS == (tmp_ret = queue_.pop(task))) {
      // When 'queue_' is empty, stop aggreating.
        update_throttling_options_();
      } else {
      }
    }
  }

  if (OB_FAIL(batch_io_task_mgr_.handle(cb_thread_pool_, palf_env_impl_))) {
  }

  if (false == last_io_task_has_been_reduced && OB_NOT_NULL(io_task)) {
    ret = handle_io_task_(io_task);
  }
  return ret;
}

int LogIOWorker::update_throttling_options_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(throttle_->update_throttling_options(palf_env_impl_))) {
    LOG_WARN("failed to update_throttling_options");
  }
  return ret;
}

LogIOWorker::BatchLogIOFlushLogTaskMgr::BatchLogIOFlushLogTaskMgr()
  : batch_io_task_(), handle_count_(0), wait_cost_stat_(NULL)
{}

LogIOWorker::BatchLogIOFlushLogTaskMgr::~BatchLogIOFlushLogTaskMgr()
{
  destroy();
}

int LogIOWorker::BatchLogIOFlushLogTaskMgr::init(int64_t batch_depth,
                                                 ObIAllocator *allocator,
                                                 ObMiniStat::ObStatItem *wait_cost_stat)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(batch_io_task_.init(batch_depth, allocator))) {
  } else {
    wait_cost_stat_ = wait_cost_stat;
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
  return ret;
}

void LogIOWorker::BatchLogIOFlushLogTaskMgr::destroy()
{
  handle_count_ = 0;
  batch_io_task_.destroy();
  wait_cost_stat_ = NULL;
}

int LogIOWorker::BatchLogIOFlushLogTaskMgr::insert(LogIOFlushLogTask *io_task)
{
  int ret = batch_io_task_.push_back(io_task);
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int LogIOWorker::BatchLogIOFlushLogTaskMgr::handle(LogIOTaskCbThreadPool *cb_thread_pool, IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  BatchLogIOFlushLogTask *io_task = &batch_io_task_;
  if (!empty()) {
    if (OB_FAIL(io_task->do_task(cb_thread_pool, palf_env_impl))) {
    } else {
      if (OB_NOT_NULL(wait_cost_stat_)) {
        wait_cost_stat_->stat(io_task->get_count(), io_task->get_accum_in_queue_time());
      }
      io_task->reset_accum_in_queue_time();
    }
    handle_count_ += io_task->get_count();
    io_task->reuse();
  }
  return ret;
}

bool LogIOWorker::BatchLogIOFlushLogTaskMgr::empty()
{
  return 0 == batch_io_task_.get_count();
}

} // end namespace palf
} // end namespace oceanbase
