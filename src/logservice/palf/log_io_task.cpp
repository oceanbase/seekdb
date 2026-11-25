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

#include "log_io_task.h"
#include "log_io_task_cb_thread_pool.h"
#include "palf_handle_impl_guard.h" // PalfHandleImplGuard
#include "palf_env_impl.h"          // IPalfEnvImpl

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{

int push_task_into_cb_thread_pool(LogIOTaskCbThreadPool *cb_thread_pool, LogIOTask *io_task)
{
  int ret = OB_SUCCESS;
  if (NULL == cb_thread_pool || NULL == io_task) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = cb_thread_pool->push(io_task);
  }
  return ret;
}

LogIOTask::LogIOTask(const int64_t palf_epoch)
{
	reset();
	palf_epoch_ = palf_epoch;
	init_task_ts_ = ObTimeUtility::current_time();
}

LogIOTask::~LogIOTask() 
{
	reset();
}

void LogIOTask::reset()
{
	palf_epoch_ = -1;
	init_task_ts_ = OB_INVALID_TIMESTAMP;
  push_cb_into_cb_pool_ts_ = OB_INVALID_TIMESTAMP;
}

// On error the worker retains the task until retry, stale epoch, or stop.
int LogIOTask::do_task(LogIOTaskCbThreadPool *cb_thread_pool, IPalfEnvImpl *palf_env_impl, bool &stale)
{ 
	int ret = OB_SUCCESS;
  stale = false;
	int64_t do_task_ts = ObTimeUtility::current_time();
  const int64_t delay_ts = do_task_ts - init_task_ts_;
	constexpr int64_t MAX_DELAY_TIME = 100 * 1000;
	IPalfHandleImplGuard guard;
	int64_t palf_epoch = -1;
	if (delay_ts >= MAX_DELAY_TIME) {
		PALF_LOG(INFO, "[io delay]", K(do_task_ts), K(delay_ts));
	}
	if (OB_FAIL(palf_env_impl->get_palf_handle_impl(guard))) {
    stale = (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret);
	} else if (OB_FAIL(guard.get_palf_handle_impl()->get_palf_epoch(palf_epoch))) {
    stale = (OB_NOT_INIT == ret);
	} else if (palf_epoch != palf_epoch_) {
    stale = true;
	  ret = OB_STATE_NOT_MATCH;
	  PALF_LOG(WARN, "palf_epoch has been changed, drop task", KPC(this), K(palf_epoch));
	} else if (OB_FAIL(do_task_(cb_thread_pool, guard))) {
	} else {}
	return ret;
}

// NB: after after_consume, the caller(LogIOCb) needs free LogIOTask.
int LogIOTask::after_consume(IPalfEnvImpl *palf_env_impl) 
{ 
	int ret = OB_SUCCESS;
	int64_t after_consume_ts = ObTimeUtility::current_time();
  const int64_t delay_ts = after_consume_ts - push_cb_into_cb_pool_ts_;
  int64_t palf_epoch = -1;
  IPalfHandleImplGuard guard;
	constexpr int64_t MAX_DELAY_TIME = 100 * 1000;
	if (delay_ts >= MAX_DELAY_TIME) {
		PALF_LOG(INFO, "[io delay]", K(after_consume_ts), K(delay_ts));
	}
  if (OB_FAIL(palf_env_impl->get_palf_handle_impl(guard))) {
  } else if (OB_FAIL(guard.get_palf_handle_impl()->get_palf_epoch(palf_epoch))) {
  } else if (palf_epoch != palf_epoch_) {
    ret = OB_STATE_NOT_MATCH;
    PALF_LOG(WARN, "palf_epoch has changed, drop task", K(ret), KPC(this));
  } else if (OB_FAIL(after_consume_(guard))) {
	} else {}
	return ret;
}

LogIOTaskType LogIOTask::get_io_task_type()
{
	return get_io_task_type_();
}

int64_t LogIOTask::get_init_task_ts()
{
  return init_task_ts_;
}

void LogIOTask::free_this(IPalfEnvImpl *palf_env_impl)
{
  return free_this_(palf_env_impl);
}
int64_t LogIOTask::get_io_size()
{
  return get_io_size_();
}
int LogIOTask::push_task_into_cb_thread_pool_(LogIOTaskCbThreadPool *cb_thread_pool, LogIOTask *io_task)
{
  int ret = OB_SUCCESS;
  push_cb_into_cb_pool_ts_ = ObTimeUtility::current_time();
  return push_task_into_cb_thread_pool(cb_thread_pool, io_task);
}

LogIOFlushLogTask::LogIOFlushLogTask(const int64_t palf_epoch)
	: LogIOTask(palf_epoch), stage_(FlushStage::Append), flush_log_cb_ctx_(), write_buf_(), is_inited_(false)
{}

LogIOFlushLogTask::~LogIOFlushLogTask()
{
  destroy();
}

int LogIOFlushLogTask::init(const FlushLogCbCtx &flush_log_cb_ctx,
                            const LogWriteBuf &write_buf)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "LogIOFlushLogTask has inited", K(ret));
  } else if (false == flush_log_cb_ctx.is_valid() || false == write_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invaild arguments!!!", K(ret), K(write_buf), K(palf_epoch_));
  } else {
    flush_log_cb_ctx_ = flush_log_cb_ctx;
    write_buf_ = write_buf;
    is_inited_ = true;
  }
  return ret;
}

void LogIOFlushLogTask::destroy()
{
  if (IS_INIT) {
    is_inited_ = false;
    write_buf_.reset();
    flush_log_cb_ctx_.reset();
    stage_ = FlushStage::Append;
  }
}

int LogIOFlushLogTask::do_task_(LogIOTaskCbThreadPool *cb_thread_pool, IPalfHandleImplGuard &guard)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogIOFlushLogTask not inited", K(ret));
  } else {
    if (FlushStage::Append == stage_) {
      if (OB_FAIL(guard.get_palf_handle_impl()->inner_append_log(
              flush_log_cb_ctx_.lsn_, write_buf_, flush_log_cb_ctx_.scn_))) {
      } else {
        stage_ = FlushStage::Reuse;
      }
    }
    if (OB_SUCC(ret)) {
      ret = finish_flush_(cb_thread_pool, guard);
    }
  }
  return ret;
}

int LogIOFlushLogTask::finish_flush_(LogIOTaskCbThreadPool *cb_thread_pool, IPalfHandleImplGuard &guard)
{
  int ret = OB_SUCCESS;
  if (FlushStage::Reuse == stage_) {
    const LSN end_lsn = flush_log_cb_ctx_.lsn_ + flush_log_cb_ctx_.total_len_;
    if (OB_FAIL(guard.get_palf_handle_impl()->advance_reuse_lsn(end_lsn))) {
    } else {
      stage_ = FlushStage::Callback;
    }
  }
  if (OB_SUCC(ret) && FlushStage::Callback == stage_) {
    ret = push_task_into_cb_thread_pool_(cb_thread_pool, this);
  }
  return ret;
}

int LogIOFlushLogTask::after_consume_(IPalfHandleImplGuard &guard)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard time_guard("after_consume log", 10 * 1000);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogIOFlushLogTask not inited", K(ret), KPC(this));
  } else if (OB_FAIL(guard.get_palf_handle_impl()->inner_after_flush_log(flush_log_cb_ctx_))) {
  } else {
  }
  return ret;
}

void LogIOFlushLogTask::free_this_(IPalfEnvImpl *palf_env_impl)
{
  palf_env_impl->get_log_allocator()->free_log_io_flush_log_task(this);
}

int64_t LogIOFlushLogTask::get_io_size_() const
{
  return write_buf_.get_total_size();
}

LogIOTruncatePrefixBlocksTask::LogIOTruncatePrefixBlocksTask(const int64_t palf_epoch)
    : LogIOTask(palf_epoch), truncated_(false), truncate_prefix_blocks_ctx_(), is_inited_(false)
{}

LogIOTruncatePrefixBlocksTask::~LogIOTruncatePrefixBlocksTask()
{
  destroy();
}

int LogIOTruncatePrefixBlocksTask::init(const TruncatePrefixBlocksCbCtx &truncate_prefix_blocks_ctx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "LogIOTruncatePrefixBlocksTask has inited!!!", K(ret));
  } else if (false == truncate_prefix_blocks_ctx.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(truncate_prefix_blocks_ctx));
  } else {
    truncate_prefix_blocks_ctx_ = truncate_prefix_blocks_ctx;
    is_inited_ = true;
  }
  return ret;
}

void LogIOTruncatePrefixBlocksTask::destroy()
{
  if (IS_INIT) {
    is_inited_ = false;
    truncate_prefix_blocks_ctx_.reset();
    truncated_ = false;
  }
}

int LogIOTruncatePrefixBlocksTask::do_task_(LogIOTaskCbThreadPool *cb_thread_pool, IPalfHandleImplGuard &guard)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogIOTruncatePrefixBlocksTask not inited", K(ret));
  } else {
    if (!truncated_) {
      if (OB_FAIL(guard.get_palf_handle_impl()->inner_truncate_prefix_blocks(
              truncate_prefix_blocks_ctx_.lsn_))) {
      } else {
        truncated_ = true;
      }
    }
    if (OB_SUCC(ret)) {
      ret = push_task_into_cb_thread_pool_(cb_thread_pool, this);
    }
  }
  return ret;
}

int LogIOTruncatePrefixBlocksTask::after_consume_(IPalfHandleImplGuard &guard)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogIOTruncatePrefixBlocksTask not inited!!!", K(ret));
  } else if (OB_FAIL(guard.get_palf_handle_impl()->inner_after_truncate_prefix_blocks(
                 truncate_prefix_blocks_ctx_))) {
  } else {
  }
  return ret;
}

void LogIOTruncatePrefixBlocksTask::free_this_(IPalfEnvImpl *palf_env_impl)
{
  palf_env_impl->get_log_allocator()->free_log_io_truncate_prefix_blocks_task(this);
}

int64_t BatchLogIOFlushLogTask::SINGLE_TASK_MAX_SIZE = MAX_LOG_BUFFER_SIZE;

BatchLogIOFlushLogTask::BatchLogIOFlushLogTask()
    : io_task_array_(),
      log_write_buf_array_(),
      scn_array_(),
      lsn_array_(),
      accum_in_queue_time_(0),
      accum_size_(0),
      is_inited_(false)
{}

BatchLogIOFlushLogTask::~BatchLogIOFlushLogTask()
{
  destroy();
}

int BatchLogIOFlushLogTask::init(const int64_t batch_depth, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  io_task_array_.set_allocator(allocator);
  log_write_buf_array_.set_allocator(allocator);
  scn_array_.set_allocator(allocator);
  lsn_array_.set_allocator(allocator);
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "BatchLogIOFlushLogTask init twice", K(ret), KPC(this));
  } else if (OB_FAIL(io_task_array_.init(batch_depth))) {
  } else if (OB_FAIL(log_write_buf_array_.init(batch_depth))) {
  } else if (OB_FAIL(scn_array_.init(batch_depth))) {
  } else if (OB_FAIL(lsn_array_.init(batch_depth))) {
  } else {
    accum_size_ = 0;
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

void BatchLogIOFlushLogTask::reuse()
{
  io_task_array_.clear();
  log_write_buf_array_.clear();
  scn_array_.clear();
  lsn_array_.clear();
  accum_size_ = 0;
}

void BatchLogIOFlushLogTask::destroy()
{
  is_inited_ = false;
  io_task_array_.destroy();
  log_write_buf_array_.destroy();
  scn_array_.destroy();
  lsn_array_.destroy();
}

int BatchLogIOFlushLogTask::push_back(LogIOFlushLogTask *task)
{
  int ret = OB_SUCCESS;
  const int64_t task_size = task->get_io_size_();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "BatchLogIOFlushLogTask not inited!!!", K(ret), KPC(this));
  } else if (accum_size_ > SINGLE_TASK_MAX_SIZE) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(io_task_array_.push_back(task))) {
  } else {
    accum_size_ += task_size;
    accum_in_queue_time_ += ObTimeUtility::fast_current_time() - task->get_init_task_ts();
  }
  return ret;
}

// Failed tasks remain owned by the worker until retry or retirement.
int BatchLogIOFlushLogTask::do_task(LogIOTaskCbThreadPool *cb_thread_pool, IPalfEnvImpl *palf_env_impl, bool &stale)
{
  int ret = OB_SUCCESS;
  stale = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "BatchLogIOFlushLogTask not inited!!!", K(ret), KPC(this));
  } else if (io_task_array_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "BatchLogIOFlushLogTask is empty", K(ret), KPC(this));
  } else if (OB_FAIL(do_task_(cb_thread_pool, palf_env_impl, stale))) {
  } else {
  }
  return ret;
}

int BatchLogIOFlushLogTask::do_task_(LogIOTaskCbThreadPool *cb_thread_pool,
                                     IPalfEnvImpl *palf_env_impl, bool &stale)
{
  int ret = OB_SUCCESS;
  int64_t palf_epoch = -1;
  IPalfHandleImplGuard guard;
  log_write_buf_array_.clear();
  scn_array_.clear();
  lsn_array_.clear();
  if (OB_FAIL(palf_env_impl->get_palf_handle_impl(guard))) {
    stale = (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret);
  } else if (OB_FAIL(guard.get_palf_handle_impl()->get_palf_epoch(palf_epoch))) {
    stale = (OB_NOT_INIT == ret);
  } else {
    const int64_t count = io_task_array_.count();
    for (int64_t i = 0; i < count && OB_SUCC(ret); i++) {
      LogIOFlushLogTask *task = io_task_array_[i];
      if (nullptr == task) {
      } else if (palf_epoch != task->get_palf_epoch()) {
        task->free_this(palf_env_impl);
        io_task_array_[i] = nullptr;
      } else if (LogIOFlushLogTask::FlushStage::Append != task->stage_) {
        if (OB_FAIL(task->finish_flush_(cb_thread_pool, guard))) {
        } else {
          io_task_array_[i] = nullptr;
        }
      } else if (OB_FAIL(log_write_buf_array_.push_back(&task->write_buf_))) {
      } else if (OB_FAIL(scn_array_.push_back(task->flush_log_cb_ctx_.scn_))) {
      } else if (OB_FAIL(lsn_array_.push_back(task->flush_log_cb_ctx_.lsn_))) {
      }
    }
    if (OB_SUCC(ret) && !lsn_array_.empty()) {
      int64_t completed_count = 0;
      ret = guard.get_palf_handle_impl()->inner_append_log(
          lsn_array_, log_write_buf_array_, scn_array_, completed_count);
      int64_t completed_idx = 0;
      for (int64_t i = 0; i < count && completed_idx < completed_count; i++) {
        LogIOFlushLogTask *task = io_task_array_[i];
        if (nullptr != task && LogIOFlushLogTask::FlushStage::Append == task->stage_) {
          task->stage_ = LogIOFlushLogTask::FlushStage::Reuse;
          completed_idx++;
        }
      }
      for (int64_t i = 0; i < count; i++) {
        LogIOFlushLogTask *task = io_task_array_[i];
        if (nullptr != task) {
          if (LogIOFlushLogTask::FlushStage::Append == task->stage_) {
            break;
          }
          const int finish_ret = task->finish_flush_(cb_thread_pool, guard);
          if (OB_SUCCESS != finish_ret) {
            if (OB_SUCC(ret)) ret = finish_ret;
            break;
          }
          io_task_array_[i] = nullptr;
        }
      }
    }
  }
  return ret;
}

void BatchLogIOFlushLogTask::discard(IPalfEnvImpl *palf_env_impl)
{
  const int64_t count = io_task_array_.count();
  for (int64_t i = 0; i < count; i++) {
    LogIOFlushLogTask *task = io_task_array_[i];
    if (NULL != task) {
      task->free_this(palf_env_impl);
      io_task_array_[i] = NULL;
    }
  }
}

} // end namespace palf
} // end namespace oceanbase
