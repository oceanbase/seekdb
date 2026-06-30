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

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{
LogLoopThread::LogLoopThread()
    : palf_env_impl_(NULL),
      run_interval_(DEFAULT_LOG_LOOP_INTERVAL_US),
      is_inited_(false)
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
    share::ObThreadPool::set_run_wrapper(MTL_CTX());
    run_interval_ = DEFAULT_LOG_LOOP_INTERVAL_US;
    is_inited_ = true;
  }

  if ((OB_FAIL(ret)) && (OB_INIT_TWICE != ret)) {
    destroy();
  }
  PALF_LOG(INFO, "LogLoopThread init finished", K(ret));
  return ret;
}

void LogLoopThread::destroy()
{
  stop();
  wait();
  is_inited_ = false;
  palf_env_impl_ = NULL;
}

void LogLoopThread::run1()
{
  lib::set_thread_name("LogLoop");
  log_loop_();
}

void LogLoopThread::log_loop_()
{
  int64_t last_switch_state_time = OB_INVALID_TIMESTAMP;
  int64_t last_check_freeze_mode_time = OB_INVALID_TIMESTAMP;

  while (!has_set_stop()) {
    const int64_t start_ts = ObTimeUtility::current_time();

    IPalfHandleImpl *handle = nullptr;
    if (OB_SUCCESS != palf_env_impl_->get_palf_handle_impl(SYS_PALF_ID, handle)) {
      ob_usleep(run_interval_, true/*is_idle_sleep*/);
      continue;
    }

    if (start_ts - last_switch_state_time >= 10 * 1000) {
      handle->check_and_switch_state();
      last_switch_state_time = start_ts;
    }

    if (start_ts - last_check_freeze_mode_time >= 1 * 1000 * 1000) {
      handle->check_and_switch_freeze_mode();
      const bool any_in_period_freeze_mode = handle->is_in_period_freeze_mode();
      last_check_freeze_mode_time = start_ts;

      if (any_in_period_freeze_mode) {
        if (run_interval_ > LOG_LOOP_INTERVAL_FOR_PERIOD_FREEZE_US) {
          run_interval_ = LOG_LOOP_INTERVAL_FOR_PERIOD_FREEZE_US;
        }
      } else {
        if (run_interval_ < DEFAULT_LOG_LOOP_INTERVAL_US) {
          run_interval_ = DEFAULT_LOG_LOOP_INTERVAL_US;
        }
      }
    }

    handle->period_freeze_last_log();
    palf_env_impl_->revert_palf_handle_impl(handle);

    palf_env_impl_->period_calc_disk_usage();

    const int64_t round_cost_time = ObTimeUtility::current_time() - start_ts;
    int32_t sleep_ts = run_interval_ - static_cast<const int32_t>(round_cost_time);
    if (sleep_ts < 0) {
      sleep_ts = 0;
    }
    ob_usleep(sleep_ts, true/*is_idle_sleep*/);

    if (REACH_THREAD_TIME_INTERVAL(5 * 1000 * 1000)) {
    }
  }
}
} // namespace palf
} // namespace oceanbase
