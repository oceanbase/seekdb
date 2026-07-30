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

#include "share/ob_memory_dump_background_source.h"
#include "lib/alloc/memory_dump.h"
#include "lib/ob_running_mode.h"

namespace oceanbase
{
namespace share
{

ObMemoryDumpBackgroundSource::ObMemoryDumpBackgroundSource()
  : source_lock_(),
    is_inited_(false),
    use_shared_executor_(false),
    background_executor_(nullptr),
    source_handle_()
{
}

ObMemoryDumpBackgroundSource::~ObMemoryDumpBackgroundSource()
{
  destroy();
}

int ObMemoryDumpBackgroundSource::init(
    ObBackgroundTaskExecutor *background_executor)
{
  int ret = OB_SUCCESS;
  bool has_pending = false;
  common::ObMemoryDump &memory_dump =
      common::ObMemoryDump::get_instance();
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (FALSE_IT(use_shared_executor_ =
      lib::is_mini_mode() && memory_dump.is_using_shared_worker())) {
  } else if (!use_shared_executor_) {
    is_inited_ = true;
  } else if (OB_ISNULL(background_executor)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBackgroundTaskSourceConfig config;
    config.name_ = "MemoryDump";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("fail to register memory dump background source", K(ret));
    } else {
      background_executor_ = background_executor;
      is_inited_ = true;
      if (OB_FAIL(memory_dump.set_shared_worker_notifier(
          notify_callback_, this, has_pending))) {
        LOG_WARN("fail to attach memory dump notifier", K(ret));
      } else if (has_pending && OB_FAIL(notify_())) {
        LOG_WARN("fail to notify pending memory dump task", K(ret));
      }
    }
  }
  if (OB_FAIL(ret)) {
    memory_dump.clear_shared_worker_notifier();
    (void)unregister_source_(true);
    is_inited_ = false;
    use_shared_executor_ = false;
  }
  return ret;
}

void ObMemoryDumpBackgroundSource::stop()
{
  if (is_inited_ && use_shared_executor_) {
    bool has_registered_source = false;
    {
      lib::ObMutexGuard guard(source_lock_);
      has_registered_source = source_handle_.is_valid();
    }
    if (has_registered_source) {
      common::ObMemoryDump::get_instance().clear_shared_worker_notifier();
    }
    const int tmp_ret = unregister_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret,
          "fail to unregister memory dump background source", K(tmp_ret));
    }
  }
}

void ObMemoryDumpBackgroundSource::wait()
{
  if (is_inited_ && use_shared_executor_) {
    const int tmp_ret = unregister_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret,
          "fail to wait memory dump background source", K(tmp_ret));
    }
  }
}

void ObMemoryDumpBackgroundSource::destroy()
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

int ObMemoryDumpBackgroundSource::process_one_quantum(
    const ObBackgroundTaskPriority priority,
    ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  int64_t processed_count = 0;
  bool has_more = false;
  if (!is_inited_ || !use_shared_executor_) {
    ret = OB_NOT_INIT;
  } else if (BG_TASK_LOW != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(
      common::ObMemoryDump::get_instance().process_one_pending_batch(
          processed_count, has_more))) {
    LOG_WARN("fail to process memory dump background quantum", K(ret));
  } else {
    result.processed_count_ = processed_count;
    result.has_more_ready_ = has_more;
  }
  return ret;
}

void ObMemoryDumpBackgroundSource::notify_callback_(void *arg)
{
  if (OB_NOT_NULL(arg)) {
    ObMemoryDumpBackgroundSource *source =
        static_cast<ObMemoryDumpBackgroundSource *>(arg);
    const int tmp_ret = source->notify_();
    if (OB_SUCCESS != tmp_ret
        && OB_NOT_RUNNING != tmp_ret
        && OB_IN_STOP_STATE != tmp_ret
        && OB_ENTRY_NOT_EXIST != tmp_ret) {
      LOG_WARN_RET(tmp_ret,
          "fail to notify memory dump background source", K(tmp_ret));
    }
  }
}

int ObMemoryDumpBackgroundSource::notify_()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(source_lock_);
  if (!is_inited_
      || !use_shared_executor_
      || OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, BG_TASK_LOW))) {
    LOG_WARN("fail to notify memory dump background source", K(ret));
  }
  return ret;
}

int ObMemoryDumpBackgroundSource::unregister_source_(
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
