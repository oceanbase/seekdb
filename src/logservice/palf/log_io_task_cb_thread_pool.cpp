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

#include "log_io_task_cb_thread_pool.h"
#include "lib/ob_running_mode.h"
#include "palf_env_impl.h"                    // PalfEnvImpl

namespace oceanbase
{
namespace palf
{
LogIOTaskCbThreadPool::LogIOTaskCbThreadPool()
    : common::ObLinkQueueThreadPool(),
      thread_num_(0),
      palf_env_impl_(NULL),
      is_inited_(false),
      use_shared_executor_(false),
      source_lock_(),
      rescue_lock_(),
      background_executor_(NULL),
      source_handle_()
{
}

LogIOTaskCbThreadPool::~LogIOTaskCbThreadPool()
{
  destroy();
}

int LogIOTaskCbThreadPool::init(const int64_t log_io_cb_num,
                                IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  const int64_t thread_num = lib::is_mini_mode() ? MINI_MODE_THREAD_NUM : THREAD_NUM;
  const int64_t task_num_limit = log_io_cb_num;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "LogIOTaskCbThreadPool has inited!!!", K(ret));
  } else if (0 >= task_num_limit || NULL == palf_env_impl) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(task_num_limit), KPC(palf_env_impl));
  } else if (OB_FAIL(common::ObLinkQueueThreadPool::init(
                 thread_num, task_num_limit, "LogIOCB"))) {
    PALF_LOG(WARN, "LogIOTaskCbThreadPool init failed", K(ret), K(thread_num), K(task_num_limit));
  } else {
    thread_num_ = thread_num;
    palf_env_impl_ = palf_env_impl;
    is_inited_ = true;
    PALF_LOG(INFO, "LogIOTaskCbThreadPool init success", K(ret),
        K(thread_num_), KP(palf_env_impl_), KP(palf_env_impl), K(task_num_limit));
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int LogIOTaskCbThreadPool::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogIOTaskCbThreadPool not inited!!!", K(ret));
  } else if (!use_shared_executor_) {
    while (OB_SUCC(ret) && common::ObLinkQueueThreadPool::get_thread_count() < thread_num_) {
      if (!common::ObLinkQueueThreadPool::try_expand_one(thread_num_)) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(ERROR, "start LogIOTaskCbThreadPool failed", K(ret),
                 K(thread_num_), "cur_thread_cnt", common::ObLinkQueueThreadPool::get_thread_count());
      }
    }
  }
  if (OB_SUCC(ret)) {
    PALF_LOG(INFO, "start LogIOTaskCbThreadPool success", K(ret), K(thread_num_));
  } else {
    common::ObLinkQueueThreadPool::stop();
    common::ObLinkQueueThreadPool::wait();
  }
  return ret;
}

int LogIOTaskCbThreadPool::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "LogIOTaskCbThreadPool not inited!!!", K(ret));
  } else {
    const int tmp_ret = detach_background_executor();
    if (OB_SUCCESS != tmp_ret) {
      PALF_LOG(WARN, "detach log io callback source failed", K(tmp_ret));
      ret = tmp_ret;
    }
    common::ObLinkQueueThreadPool::stop();
    PALF_LOG(INFO, "stop LogIOTaskCbThreadPool success", K(thread_num_));
  }
  return ret;
}

int LogIOTaskCbThreadPool::wait()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "LogIOTaskCbThreadPool not inited!!!", K(ret));
  } else {
    common::ObLinkQueueThreadPool::wait();
    PALF_LOG(INFO, "wait LogIOTaskCbThreadPool success", K(thread_num_));
  }
  return ret;
}

void LogIOTaskCbThreadPool::destroy()
{
  stop();
  wait();
  is_inited_ = false;
  common::ObLinkQueueThreadPool::destroy();
  thread_num_ = 0;
  palf_env_impl_ = NULL;
  use_shared_executor_ = false;
  background_executor_ = NULL;
  source_handle_.reset();
  PALF_LOG(INFO, "destroy LogIOTaskCbThreadPool success", K(thread_num_));
}

int LogIOTaskCbThreadPool::push(common::LinkTask *task)
{
  int ret = common::ObLinkQueueThreadPool::push(task);
  if (OB_SUCC(ret) && use_shared_executor_) {
    const int notify_ret = notify_background_source_();
    if (OB_SUCCESS != notify_ret) {
      PALF_LOG(WARN, "notify log io callback source failed",
          K(notify_ret), KP(task));
      ensure_rescue_worker_(true);
    } else {
      ensure_rescue_worker_(false);
    }
  }
  return ret;
}

int LogIOTaskCbThreadPool::attach_background_executor(
    share::ObBackgroundTaskExecutor *background_executor)
{
  int ret = OB_SUCCESS;
  if (!lib::is_mini_mode()) {
  } else if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(background_executor)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "LogIOCallback";
    config.max_concurrency_ = 1;
    {
      lib::ObMutexGuard guard(source_lock_);
      if (use_shared_executor_ || source_handle_.is_valid()) {
        ret = OB_INIT_TWICE;
      } else if (OB_FAIL(background_executor->register_source(
          *this, config, source_handle_))) {
        PALF_LOG(WARN, "register log io callback source failed", K(ret));
      } else {
        background_executor_ = background_executor;
        use_shared_executor_ = true;
        common::ObLinkQueueThreadPool::set_external_driver(true);
      }
    }
    if (OB_SUCC(ret)
        && common::ObLinkQueueThreadPool::get_queue_num() > 0
        && OB_FAIL(notify_background_source_())) {
      PALF_LOG(WARN, "notify pending log io callback failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
    (void)detach_background_executor();
  }
  return ret;
}

int LogIOTaskCbThreadPool::detach_background_executor()
{
  int ret = OB_SUCCESS;
  bool was_shared = false;
  {
    lib::ObMutexGuard guard(source_lock_);
    was_shared = use_shared_executor_ || source_handle_.is_valid();
    use_shared_executor_ = false;
    common::ObLinkQueueThreadPool::set_external_driver(false);
  }
  if (was_shared && OB_FAIL(unregister_background_source_(true))) {
    PALF_LOG(WARN, "unregister log io callback source failed", K(ret));
  }
  {
    lib::ObMutexGuard guard(rescue_lock_);
    if (!common::ObLinkQueueThreadPool::has_set_stop()
        && common::ObLinkQueueThreadPool::get_queue_num() > 0
        && common::ObLinkQueueThreadPool::get_thread_count() <= 0
        && !common::ObLinkQueueThreadPool::try_expand_one(1)) {
      const int tmp_ret = OB_ERR_UNEXPECTED;
      PALF_LOG(WARN, "failed to restore log io callback worker",
          K(tmp_ret));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
  }
  return ret;
}

int LogIOTaskCbThreadPool::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  common::LinkTask *task = NULL;
  if (IS_NOT_INIT || !use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (share::BG_TASK_HIGH != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(
      common::ObLinkQueueThreadPool::pop_task_for_external_driver(task))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    }
  } else if (OB_ISNULL(task)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    handle(task);
    result.processed_count_ = 1;
    result.has_more_ready_ =
        common::ObLinkQueueThreadPool::get_queue_num() > 0;
  }
  return ret;
}

int LogIOTaskCbThreadPool::notify_background_source_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(source_lock_);
  if (!use_shared_executor_
      || OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_HIGH))) {
    PALF_LOG(WARN, "notify log io callback source failed", K(ret));
  }
  return ret;
}

int LogIOTaskCbThreadPool::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  bool need_retry = false;
  do {
    need_retry = false;
    {
      lib::ObMutexGuard guard(source_lock_);
      if (OB_NOT_NULL(background_executor_)
          && source_handle_.is_valid()) {
        ret = background_executor_->unregister_source(source_handle_);
        if (OB_EAGAIN == ret && wait_running) {
          need_retry = true;
        } else if (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret) {
          source_handle_.reset();
          ret = OB_SUCCESS;
        }
      }
      if (!source_handle_.is_valid()) {
        background_executor_ = NULL;
      }
    }
    if (need_retry) {
      ob_usleep(1000);
    }
  } while (need_retry);
  return ret;
}

void LogIOTaskCbThreadPool::ensure_rescue_worker_(const bool force)
{
  bool shared_pool_saturated = false;
  {
    lib::ObMutexGuard guard(source_lock_);
    shared_pool_saturated =
        use_shared_executor_
        && OB_NOT_NULL(background_executor_)
        && background_executor_->get_worker_count()
            >= share::ObBackgroundTaskExecutor::MAX_WORKER_COUNT
        && background_executor_->get_idle_worker_count() <= 0;
  }
  if (force || shared_pool_saturated) {
    lib::ObMutexGuard guard(rescue_lock_);
    if (common::ObLinkQueueThreadPool::get_queue_num() > 0
        && common::ObLinkQueueThreadPool::get_thread_count() <= 0
        && !common::ObLinkQueueThreadPool::try_expand_one(1)) {
      const int ret = OB_ERR_UNEXPECTED;
      PALF_LOG(WARN, "failed to start rescue log io callback worker",
          K(ret), K(force), K(shared_pool_saturated));
    }
  }
}

void LogIOTaskCbThreadPool::handle(common::LinkTask *task)
{
  int ret = OB_SUCCESS;
  LogIOTask *log_io_task = static_cast<LogIOTask*>(task);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogIOTaskCbThreadPool not inited!!!", K(ret));
  } else if (OB_ISNULL(log_io_task)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(log_io_task));
  } else if (OB_FAIL(log_io_task->after_consume(palf_env_impl_))) {
    PALF_LOG(WARN, "LogIOTask after_consume failed", K(ret), KP(log_io_task));
  } else {
    PALF_LOG(TRACE, "LogIOTaskCbThreadPool handle success");
  }
  if (OB_NOT_NULL(log_io_task)) {
    log_io_task->free_this(palf_env_impl_);
  }
}
} // end namespace palf
} // end namespace oceanbase
