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

#include "ob_ddl_task_executor.h"
#include "lib/ob_running_mode.h"
#include "lib/thread/ob_thread_name.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "share/rc/ob_module_provider.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_force_print_log.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase::share;
using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{
ObDDLTaskQueue::ObDDLTaskQueue()
  : task_list_(), task_set_(), lock_(ObLatchIds::DDL_LOCK), is_inited_(false), allocator_()
{
  allocator_.set_label(common::ObModIds::OB_BUILD_INDEX_SCHEDULER);
}

ObDDLTaskQueue::~ObDDLTaskQueue()
{
}

void ObDDLTaskQueue::destroy()
{
  int ret = OB_SUCCESS;
  ObIDDLTask *task = NULL;
  common::ObSpinLockGuard guard(lock_);
  while (OB_SUCC(ret) && task_list_.get_size() > 0) {
    if (OB_ISNULL(task = task_list_.remove_first())) {
      ret = common::OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "fail to remove first task", K(ret));
    } else {
      allocator_.free(task);
      task = NULL;
    }
  }
  task_set_.destroy();
  allocator_.reset();
  is_inited_ = false;
}

int ObDDLTaskQueue::init(const int64_t bucket_num, const int64_t total_mem_limit,
    const int64_t hold_mem_limit, const int64_t page_size)
{
  int ret = OB_SUCCESS;
  common::ObSpinLockGuard guard(lock_);
  if (bucket_num <= 0 || total_mem_limit <= 0
      || hold_mem_limit <= 0 || page_size <= 0) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(bucket_num), K(total_mem_limit),
        K(hold_mem_limit), K(page_size));
  } else if (OB_FAIL(task_set_.create(bucket_num))) {
    STORAGE_LOG(WARN, "fail to create task set", K(ret), K(bucket_num));
  } else if (OB_FAIL(allocator_.init(total_mem_limit, hold_mem_limit, page_size))) {
    STORAGE_LOG(WARN, "fail to init allocator", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObDDLTaskQueue::push_task(const ObIDDLTask &task)
{
  int ret = OB_SUCCESS;
  ObIDDLTask *task_copy = NULL;
  char *buf = NULL;
  bool task_add_to_list = false;
  common::ObSpinLockGuard guard(lock_);
  const int64_t deep_copy_size = task.get_deep_copy_size();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = common::OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObBuildIndexTaskQueue has not been inited", K(ret));
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(deep_copy_size)))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "fail to allocate memory for ObBuildIndexTask", K(ret));
  } else if (OB_ISNULL(task_copy = task.deep_copy(buf, deep_copy_size))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "fail to deep copy task", K(ret));
  } else if (!task_list_.add_last(task_copy)) {
    ret = common::OB_ERR_UNEXPECTED;
    STORAGE_LOG(ERROR, "unexpected error, add build index task failed", K(ret));
  } else {
    int is_overwrite = 0; // do not overwrite
    task_add_to_list = true;
    if (OB_FAIL(task_set_.set_refactored(task_copy, is_overwrite))) {
      if (common::OB_HASH_EXIST == ret) {
        ret = common::OB_ENTRY_EXIST;
      } else {
        STORAGE_LOG(WARN, "fail to set task to task set", K(ret));
      }
    } else {
      STORAGE_LOG(INFO, "add task", K(*task_copy), KP(task_copy), K(common::lbt()));
    }
  }
  if (OB_FAIL(ret) && NULL != buf) {
    if (task_add_to_list) {
      int tmp_ret = OB_SUCCESS;
      if (!task_list_.remove(task_copy)) {
        tmp_ret = common::OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "fail to remove task", K(tmp_ret), K(*task_copy));
      }
    }
    allocator_.free(buf);
    buf = NULL;
    task_copy = NULL;
  }
  return ret;
}

int ObDDLTaskQueue::get_next_task(ObIDDLTask *&task)
{
  int ret = OB_SUCCESS;
  common::ObSpinLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = common::OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObBuildIndexTaskQueue has not been inited", K(ret));
  } else if (0 == task_list_.get_size()) {
    ret = common::OB_EAGAIN;
  } else if (OB_ISNULL(task = task_list_.remove_first())) {
    ret = common::OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "error unexpected, task must not be NULL", K(ret));
  }
  return ret;
}

int ObDDLTaskQueue::remove_task(ObIDDLTask *task)
{
  int ret = OB_SUCCESS;
  common::ObSpinLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = common::OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObBuildIndexTaskQueue has not been inited", K(ret));
  } else if (OB_ISNULL(task)) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(task));
  } else if (OB_FAIL(task_set_.erase_refactored(task))) {
    STORAGE_LOG(WARN, "fail to erase from task set", K(ret));
  } else {
    STORAGE_LOG(INFO, "succ to remove task", K(*task), KP(task));
  }
  if (NULL != task) {
    allocator_.free(task);
    task = NULL;
  }
  return ret;
}

int ObDDLTaskQueue::add_task_to_last(ObIDDLTask *task)
{
  int ret = OB_SUCCESS;
  common::ObSpinLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = common::OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObBuildIndexTaskQueue has not been inited", K(ret));
  } else if (OB_ISNULL(task)) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(task));
  } else if (!task_list_.add_last(task)) {
    ret = common::OB_ERR_UNEXPECTED;
    STORAGE_LOG(ERROR, "error unexpected, fail to move task to last", K(ret));
  }
  return ret;
}

ObDDLTaskExecutor::ObDDLTaskExecutor()
  : lib::ThreadPool(THREAD_NUM), is_inited_(false), task_queue_(), cond_()
{
}

ObDDLTaskExecutor::~ObDDLTaskExecutor()
{
}





void ObDDLTaskExecutor::run1()
{
  int ret = OB_SUCCESS;
  int64_t executed_task_count = 0;
  ObIDDLTask *task = NULL;
  ObIDDLTask *first_retry_task = NULL;
  ObDIActionGuard ag("DDLService", "DDLTaskExecutor", "detect task");
  lib::set_thread_name("DDLTaskExecutor");
  while (!has_set_stop()) {
    while (!has_set_stop() && executed_task_count < BATCH_EXECUTE_COUNT) {
      if (OB_FAIL(task_queue_.get_next_task(task))) {
        if (common::OB_EAGAIN == ret) {
          break;
        } else {
          STORAGE_LOG(WARN, "fail to get next task", K(ret));
          break;
        }
      } else if (OB_ISNULL(task)) {
        ret = OB_ERR_SYS;
        STORAGE_LOG(WARN, "error unexpected, task must not be NULL", K(ret));
      } else if (task == first_retry_task) {
        // add the task back to the queue
        if (OB_FAIL(task_queue_.add_task_to_last(task))) {
          STORAGE_LOG(ERROR, "fail to add task to last, which should not happen", K(ret), K(*task));
        }
        break;
      } else {
        ObDIActionGuard(ObDIActionGuard::NS_ACTION, "TaskType:%d", task->get_type());
        task->process();
        ++executed_task_count;
        if (task->need_retry()) {
          if (OB_FAIL(task_queue_.add_task_to_last(task))) {
            STORAGE_LOG(ERROR, "fail to add task to last, which should not happen", K(ret), K(*task));
          }
          first_retry_task = task;
        } else {
          if (OB_FAIL(task_queue_.remove_task(task))) {
            STORAGE_LOG(WARN, "fail to remove task, which should not happen", K(ret), K(*task), KP(task));
          }
        }
      }
    }
    cond_.lock();
    {
      cond_.wait(CHECK_TASK_INTERVAL);
    }
    cond_.unlock();
    executed_task_count = 0;
    first_retry_task = NULL;
  }
}

ObDDLLocalBuilder::ObDDLLocalBuilder()
  : is_thread_started_(false),
    is_stopped_(true),
    use_shared_executor_(false),
    task_queue_(),
    background_executor_(nullptr),
    source_handle_()
{

}

ObDDLLocalBuilder::~ObDDLLocalBuilder()
{
  destroy();
}

int ObDDLLocalBuilder::init()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[DDL_LOCAL_BUILDER] begin init ddl local builder",
            K(is_thread_started_), K(is_stopped_));
  if (OB_UNLIKELY(is_thread_started_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl local builder thread is already started", KR(ret), K(is_thread_started_));
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (use_shared_executor_
      && (OB_ISNULL(share::g_mp)
          || OB_ISNULL(background_executor_ =
              share::g_mp->background_task_executor()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("background task executor is null", K(ret),
        KP(share::g_mp), KP(background_executor_));
  } else if (use_shared_executor_
      && OB_FAIL(task_queue_.init_without_thread(4 << 10))) {
    LOG_ERROR("init externally driven ddl local builder queue failed",
        KR(ret));
  } else if (!use_shared_executor_
      && OB_FAIL(task_queue_.init(get_thread_cnt_(), 4 << 10, "DdlBuild"))) {
    LOG_ERROR("init ddl local builder task queue failed", KR(ret));
  } else if (!use_shared_executor_ && OB_FAIL(task_queue_.start())) {
    LOG_WARN("index build thread start failed", KR(ret));
  } else if (use_shared_executor_) {
    ObBackgroundTaskSourceConfig config;
    config.name_ = "DdlBuild";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("register ddl local builder source failed", KR(ret));
    }
  }
  if (OB_SUCC(ret)) {
    is_thread_started_ = true;
    ATOMIC_STORE(&is_stopped_, true);
  } else {
    (void) unregister_source_(true);
    task_queue_.destroy();
  }
  FLOG_INFO("[DDL_LOCAL_BUILDER] finish init ddl local builder",
            KR(ret), K(is_thread_started_), K(is_stopped_));
  return ret;
}

int ObDDLLocalBuilder::start()
{
  FLOG_INFO("[DDL_LOCAL_BUILDER] begin start ddl local builder",
            K(is_thread_started_), K(is_stopped_));
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_thread_started_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl local builder thread is not started", KR(ret), K(is_thread_started_));
  } else {
    ATOMIC_STORE(&is_stopped_, false);
  }
  FLOG_INFO("[DDL_LOCAL_BUILDER] finish start ddl local builder",
            KR(ret), K(is_thread_started_), K(is_stopped_));
  return ret;
}

void ObDDLLocalBuilder::stop()
{
  FLOG_INFO("[DDL_LOCAL_BUILDER] begin stop ddl local builder",
            K(is_thread_started_), K(is_stopped_));
  {
    ATOMIC_STORE(&is_stopped_, true);
  }
  FLOG_INFO("[DDL_LOCAL_BUILDER] finish stop ddl local builder",
            K(is_thread_started_), K(is_stopped_));
}

void ObDDLLocalBuilder::server_module_thread_stop()
{
  FLOG_INFO("[DDL_LOCAL_BUILDER] begin server_module_thread_stop ddl local builder",
            K(is_thread_started_), K(is_stopped_));
  if (is_thread_started_) {
    ATOMIC_STORE(&is_stopped_, true);
    if (use_shared_executor_) {
      const int tmp_ret = unregister_source_(false);
      if (OB_SUCCESS != tmp_ret && OB_EAGAIN != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "failed to stop ddl local builder background source");
      }
    } else {
      task_queue_.stop();
    }
  }
  FLOG_INFO("[DDL_LOCAL_BUILDER] finish server_module_thread_stop ddl local builder",
            K(is_thread_started_), K(is_stopped_));
}

void ObDDLLocalBuilder::server_module_thread_wait()
{
  FLOG_INFO("[DDL_LOCAL_BUILDER] begin server_module_thread_wait ddl local builder",
            K(is_thread_started_), K(is_stopped_));
  if (is_thread_started_) {
    if (use_shared_executor_) {
      const int tmp_ret = unregister_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "failed to wait ddl local builder background source");
      }
    }
    task_queue_.wait();
  }
  FLOG_INFO("[DDL_LOCAL_BUILDER] finish server_module_thread_wait ddl local builder",
            K(is_thread_started_), K(is_stopped_));
}

void ObDDLLocalBuilder::destroy()
{
  FLOG_INFO("[DDL_LOCAL_BUILDER] begin destroy ddl local builder",
            K(is_thread_started_), K(is_stopped_));
  {
    if (is_thread_started_) {
      ATOMIC_STORE(&is_stopped_, true);
      const int tmp_ret = unregister_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "failed to unregister ddl local builder background source");
      }
      task_queue_.destroy();
      is_thread_started_ = false;
    }
  }
  FLOG_INFO("[DDL_LOCAL_BUILDER] finish destroy ddl local builder",
            K(is_thread_started_), K(is_stopped_));
}

int ObDDLLocalBuilder::push_task(ObAsyncTask &task)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_thread_started_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("ddl builder thread not started", K(ret), K(is_thread_started_));
  } else if (ATOMIC_LOAD(&is_stopped_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl builder has stopped", KR(ret), K(is_stopped_));
  } else if (OB_FAIL(task_queue_.push(task))) {
    LOG_WARN("add task to queue failed", KR(ret));
  } else if (use_shared_executor_) {
    const int tmp_ret = background_executor_->notify(
        source_handle_, BG_TASK_NORMAL);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret,
          "failed to notify ddl local builder after accepting task");
    }
  }
  return ret;
}

int ObDDLLocalBuilder::process_one_quantum(
    const ObBackgroundTaskPriority priority,
    ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (BG_TASK_NORMAL != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    bool processed = false;
    int64_t next_ready_ts = 0;
    bool has_more_ready = false;
    if (OB_FAIL(task_queue_.process_one_task(
        processed, next_ready_ts, has_more_ready))) {
      LOG_WARN("failed to process ddl local builder task", K(ret));
    } else {
      result.processed_count_ = processed ? 1 : 0;
      result.has_more_ready_ = has_more_ready;
      result.next_ready_ts_ = next_ready_ts;
    }
  }
  return ret;
}

int ObDDLLocalBuilder::unregister_source_(const bool wait)
{
  int ret = OB_SUCCESS;
  if (use_shared_executor_
      && OB_NOT_NULL(background_executor_)
      && source_handle_.is_valid()) {
    do {
      ret = background_executor_->unregister_source(source_handle_);
      if (wait && OB_EAGAIN == ret) {
        ob_usleep(10 * 1000L);
      }
    } while (wait && OB_EAGAIN == ret);
    if (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret) {
      source_handle_.reset();
      ret = OB_SUCCESS;
    }
  }
  if (!source_handle_.is_valid()) {
    background_executor_ = nullptr;
  }
  return ret;
}

int64_t ObDDLLocalBuilder::get_thread_cnt_() const
{
  return lib::is_mini_mode() ? 1 : 16;
}

}  // end namespace share
}  // end namespace oceanbase
