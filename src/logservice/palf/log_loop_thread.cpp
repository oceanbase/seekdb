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

#include "log_loop_thread.h"
#include "palf_env_impl.h"
#include "lib/ob_running_mode.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{
LogLoopThread::LogLoopThread()
    : palf_env_impl_(NULL),
      run_interval_(DEFAULT_LOG_LOOP_INTERVAL_US),
      last_switch_state_time_(OB_INVALID_TIMESTAMP),
      last_check_freeze_mode_time_(OB_INVALID_TIMESTAMP),
      is_inited_(false),
      is_running_(false),
      use_shared_executor_(false),
      background_executor_(NULL),
      source_handle_(),
      source_lock_()
{
}

LogLoopThread::~LogLoopThread()
{
  destroy();
}

int LogLoopThread::init(IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    PALF_LOG(WARN, "LogLoopThread has been inited", K(ret));
  } else if (NULL == palf_env_impl) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), KP(palf_env_impl));
  } else {
    palf_env_impl_ = palf_env_impl;
    share::ObThreadPool::set_run_wrapper(share::server_runtime());
    run_interval_ = DEFAULT_LOG_LOOP_INTERVAL_US;
    last_switch_state_time_ = OB_INVALID_TIMESTAMP;
    last_check_freeze_mode_time_ = OB_INVALID_TIMESTAMP;
    use_shared_executor_ = lib::is_mini_mode();
    is_inited_ = true;
  }

  if ((OB_FAIL(ret)) && (OB_INIT_TWICE != ret)) {
    destroy();
  }
  PALF_LOG(INFO, "LogLoopThread init finished", K(ret));
  return ret;
}

int LogLoopThread::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (ATOMIC_LOAD(&is_running_)) {
    ret = OB_INIT_TWICE;
  } else if (use_shared_executor_) {
    share::ObBackgroundTaskExecutor *background_executor =
        OB_ISNULL(share::g_mp)
            ? NULL
            : share::g_mp->background_task_executor();
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "PALFLogLoop";
    config.max_concurrency_ = 1;
    if (OB_ISNULL(background_executor)) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(WARN, "background executor is null",
          K(ret), KP(share::g_mp), KP(background_executor));
    } else if (OB_FAIL(background_executor->register_source(
        *this, config, source_handle_))) {
      PALF_LOG(WARN, "register PALF log loop source failed", K(ret));
    } else {
      {
        lib::ObMutexGuard guard(source_lock_);
        background_executor_ = background_executor;
        ATOMIC_STORE(&is_running_, true);
        has_set_stop() = false;
      }
      if (OB_FAIL(notify_background_source_())) {
        PALF_LOG(WARN, "notify PALF log loop source failed", K(ret));
      }
    }
  } else if (OB_FAIL(share::ObThreadPool::start())) {
    PALF_LOG(WARN, "start PALF log loop thread failed", K(ret));
  } else {
    ATOMIC_STORE(&is_running_, true);
  }
  if (OB_FAIL(ret) && use_shared_executor_) {
    ATOMIC_STORE(&is_running_, false);
    has_set_stop() = true;
    (void)unregister_background_source_(true);
  } else if (OB_SUCC(ret)) {
    PALF_LOG(INFO, "start PALF log loop success",
        K(use_shared_executor_), KP(background_executor_));
  }
  return ret;
}

void LogLoopThread::stop()
{
  ATOMIC_STORE(&is_running_, false);
  if (use_shared_executor_) {
    has_set_stop() = true;
    const int tmp_ret = unregister_background_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      PALF_LOG_RET(WARN, tmp_ret,
          "unregister PALF log loop source failed", K(tmp_ret));
    }
  } else {
    share::ObThreadPool::stop();
  }
}

void LogLoopThread::wait()
{
  if (use_shared_executor_) {
    const int tmp_ret = unregister_background_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      PALF_LOG_RET(WARN, tmp_ret,
          "wait PALF log loop source failed", K(tmp_ret));
    }
  } else {
    share::ObThreadPool::wait();
  }
}

void LogLoopThread::destroy()
{
  stop();
  PALF_LOG(INFO, "runlin trace stop");
  wait();
  PALF_LOG(INFO, "runlin trace wait");
  is_inited_ = false;
  ATOMIC_STORE(&is_running_, false);
  use_shared_executor_ = false;
  run_interval_ = DEFAULT_LOG_LOOP_INTERVAL_US;
  last_switch_state_time_ = OB_INVALID_TIMESTAMP;
  last_check_freeze_mode_time_ = OB_INVALID_TIMESTAMP;
  {
    lib::ObMutexGuard guard(source_lock_);
    background_executor_ = NULL;
    source_handle_.reset();
  }
  palf_env_impl_ = NULL;
}

void LogLoopThread::run1()
{
  lib::set_thread_name("LogLoop");
  log_loop_();
  PALF_LOG(INFO, "log_loop_thread will stop");
}

void LogLoopThread::log_loop_()
{
  while (!has_set_stop()) {
    int64_t wait_us = 0;
    run_one_round_(wait_us);
    if (wait_us > 0) {
      ob_usleep(wait_us, true/*is_idle_sleep*/);
    }
  }
}

void LogLoopThread::run_one_round_(int64_t &wait_us)
{
  const int64_t start_ts = ObTimeUtility::current_time();
  IPalfHandleImpl *handle = NULL;
  wait_us = run_interval_;

  if (OB_SUCCESS == palf_env_impl_->get_palf_handle_impl(handle)) {
    if (start_ts - last_switch_state_time_ >= 10 * 1000) {
      handle->check_and_switch_state();
      last_switch_state_time_ = start_ts;
    }

    if (start_ts - last_check_freeze_mode_time_ >= 1 * 1000 * 1000) {
      handle->check_and_switch_freeze_mode();
      const bool any_in_period_freeze_mode =
          handle->is_in_period_freeze_mode();
      last_check_freeze_mode_time_ = start_ts;
      if (any_in_period_freeze_mode
          && run_interval_ > LOG_LOOP_INTERVAL_FOR_PERIOD_FREEZE_US) {
        run_interval_ = LOG_LOOP_INTERVAL_FOR_PERIOD_FREEZE_US;
        PALF_LOG(INFO, "LogLoopThread switch run_interval(us)",
            K_(run_interval), K(any_in_period_freeze_mode));
      } else if (!any_in_period_freeze_mode
          && run_interval_ < DEFAULT_LOG_LOOP_INTERVAL_US) {
        run_interval_ = DEFAULT_LOG_LOOP_INTERVAL_US;
        PALF_LOG(INFO, "LogLoopThread switch run_interval(us)",
            K_(run_interval), K(any_in_period_freeze_mode));
      }
    }

    handle->period_freeze_last_log();
    palf_env_impl_->revert_palf_handle_impl(handle);
    palf_env_impl_->period_calc_disk_usage();
  }

  const int64_t round_cost_time =
      ObTimeUtility::current_time() - start_ts;
  wait_us = MAX(static_cast<int64_t>(0),
      run_interval_ - round_cost_time);
  if (REACH_THREAD_TIME_INTERVAL(5 * 1000 * 1000)) {
    PALF_LOG(INFO, "LogLoopThread round_cost_time(us)", K(round_cost_time));
  }
}

int LogLoopThread::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  if (!is_inited_ || !use_shared_executor_) {
    ret = OB_NOT_INIT;
  } else if (share::BG_TASK_HIGH != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!ATOMIC_LOAD(&is_running_)) {
    // stop() may race a quantum already claimed by the executor.
  } else {
    int64_t wait_us = 0;
    run_one_round_(wait_us);
    result.processed_count_ = 1;
    if (wait_us <= 0) {
      result.has_more_ready_ = true;
    } else {
      result.next_ready_ts_ =
          ObTimeUtility::current_time() + wait_us;
    }
  }
  return ret;
}

int LogLoopThread::notify_background_source_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(source_lock_);
  if (!use_shared_executor_
      || !ATOMIC_LOAD(&is_running_)
      || OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_HIGH))) {
    PALF_LOG(WARN, "notify PALF log loop background source failed", K(ret));
  }
  return ret;
}

int LogLoopThread::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  bool need_retry = false;
  do {
    need_retry = false;
    {
      lib::ObMutexGuard guard(source_lock_);
      if (use_shared_executor_
          && OB_NOT_NULL(background_executor_)
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
} // namespace palf
} // namespace oceanbase
