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

#define USING_LOG_PREFIX SHARE

#include "share/ob_timer_task_background_source.h"
#include "lib/ob_running_mode.h"
#include "lib/task/ob_timer_service.h"

namespace oceanbase
{
namespace share
{

ObTimerTaskBackgroundSource::ObTimerTaskBackgroundSource()
  : source_lock_(),
    is_inited_(false),
    use_shared_executor_(false),
    background_executor_(nullptr),
    source_handle_()
{
}

ObTimerTaskBackgroundSource::~ObTimerTaskBackgroundSource()
{
  destroy();
}

int ObTimerTaskBackgroundSource::init(
    ObBackgroundTaskExecutor *background_executor)
{
  int ret = OB_SUCCESS;
  bool has_pending = false;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (!use_shared_executor_) {
    is_inited_ = true;
  } else if (OB_ISNULL(background_executor)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBackgroundTaskSourceConfig config;
    config.name_ = "TimerService";
    // Mini mode accepts bounded callback delay in exchange for a smaller idle
    // footprint. Keep two callback consumers so same-period timer bursts do not
    // build a multi-second backlog, while still bounding shared-pool expansion.
    config.max_concurrency_ = 2;
    config.max_concurrency_by_priority_[BG_TASK_HIGH] = 2;
    if (OB_FAIL(background_executor->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("fail to register timer service background source", K(ret));
    } else {
      background_executor_ = background_executor;
      is_inited_ = true;
      if (OB_FAIL(common::ObTimerService::get_instance()
          .attach_shared_worker_notifier(
              notify_callback_, this, has_pending))) {
        LOG_WARN("fail to attach timer service shared worker", K(ret));
      } else if (has_pending && OB_FAIL(notify_())) {
        LOG_WARN("fail to notify pending timer task", K(ret));
      }
    }
  }
  if (OB_FAIL(ret)) {
    (void)common::ObTimerService::get_instance()
        .detach_shared_worker_notifier(this);
    (void)unregister_source_(true);
    is_inited_ = false;
    use_shared_executor_ = false;
  }
  return ret;
}

void ObTimerTaskBackgroundSource::stop()
{
  if (is_inited_ && use_shared_executor_) {
    const int unregister_ret = unregister_source_(true);
    if (OB_SUCCESS != unregister_ret) {
      LOG_WARN_RET(unregister_ret,
          "fail to unregister timer service background source",
          K(unregister_ret));
    }
    const int detach_ret = common::ObTimerService::get_instance()
        .detach_shared_worker_notifier(this);
    if (OB_SUCCESS != detach_ret && OB_NOT_RUNNING != detach_ret) {
      LOG_WARN_RET(detach_ret,
          "fail to detach timer service shared worker", K(detach_ret));
    }
  }
}

void ObTimerTaskBackgroundSource::wait()
{
  if (is_inited_ && use_shared_executor_) {
    const int tmp_ret = unregister_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret,
          "fail to wait timer service background source", K(tmp_ret));
    }
  }
}

void ObTimerTaskBackgroundSource::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    lib::ObMutexGuard guard(source_lock_);
    background_executor_ = nullptr;
    source_handle_.reset();
    use_shared_executor_ = false;
    is_inited_ = false;
  }
}

int ObTimerTaskBackgroundSource::process_one_quantum(
    const ObBackgroundTaskPriority priority,
    ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  bool processed = false;
  bool has_more = false;
  const int64_t saved_worker_timeout_ts = THIS_WORKER.get_timeout_ts();
  if (!is_inited_ || !use_shared_executor_) {
    ret = OB_NOT_INIT;
  } else if (BG_TASK_HIGH != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(common::ObTimerService::get_instance()
      .process_one_shared_worker_task(processed, has_more))) {
    LOG_WARN("fail to process timer service background quantum", K(ret));
  } else {
    result.processed_count_ = processed ? 1 : 0;
    result.has_more_ready_ = has_more;
  }
  THIS_WORKER.set_timeout_ts(saved_worker_timeout_ts);
  return ret;
}

void ObTimerTaskBackgroundSource::notify_callback_(void *arg)
{
  if (OB_NOT_NULL(arg)) {
    ObTimerTaskBackgroundSource *source =
        static_cast<ObTimerTaskBackgroundSource *>(arg);
    const int tmp_ret = source->notify_();
    if (OB_SUCCESS != tmp_ret
        && OB_NOT_RUNNING != tmp_ret
        && OB_IN_STOP_STATE != tmp_ret
        && OB_ENTRY_NOT_EXIST != tmp_ret) {
      LOG_WARN_RET(tmp_ret,
          "fail to notify timer service background source", K(tmp_ret));
    }
  }
}

int ObTimerTaskBackgroundSource::notify_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(source_lock_);
  if (!is_inited_
      || !use_shared_executor_
      || OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, BG_TASK_HIGH))) {
    LOG_WARN("fail to notify timer service background source", K(ret));
  }
  return ret;
}

int ObTimerTaskBackgroundSource::unregister_source_(
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
        background_executor_ = nullptr;
      }
    }
    if (need_retry) {
      ob_usleep(1000);
    }
  } while (need_retry);
  return ret;
}

} // namespace share
} // namespace oceanbase
