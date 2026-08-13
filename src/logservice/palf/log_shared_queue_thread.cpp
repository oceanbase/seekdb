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

#include "log_shared_queue_thread.h"
#include "log_shared_task.h"
#include "palf_env_impl.h"                    // PalfEnvImpl

namespace oceanbase
{
namespace palf
{
LogSharedQueueTh::LogSharedQueueTh()
    : submit_log_queue_(*this),
      shared_queue_(*this),
      palf_env_impl_(NULL),
      is_inited_(false)
{}

LogSharedQueueTh::~LogSharedQueueTh()
{
  destroy();
}

int LogSharedQueueTh::init(IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "LogSharedQueueTh has inited", K(ret));
  } else if (NULL == palf_env_impl) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument", K(ret), KP(palf_env_impl));
  } else if (OB_FAIL(submit_log_queue_.init(THREAD_NUM, MAX_LOG_HANDLE_TASK_NUM, "LogSubmit"))) {
  } else if (OB_FAIL(shared_queue_.init(THREAD_NUM, MAX_LOG_HANDLE_TASK_NUM, "LogShared"))) {
  } else {
    palf_env_impl_ = palf_env_impl;
    is_inited_ = true;
    PALF_LOG(INFO, "LogSharedQueueTh init success", K(ret), KP(palf_env_impl));
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int LogSharedQueueTh::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogSharedQueueTh not inited", K(ret));
  } else {
    PALF_LOG(INFO, "start LogSharedQueueTh success", K(ret));
  }
  return ret;
}

int LogSharedQueueTh::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "LogSharedQueueTh not inited", K(ret));
  } else {
    submit_log_queue_.stop();
    shared_queue_.stop();
    PALF_LOG(INFO, "stop LogSharedQueueTh success");
  }
  return ret;
}

int LogSharedQueueTh::wait()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "LogSharedQueueTh not inited", K(ret));
  } else {
    submit_log_queue_.wait();
    shared_queue_.wait();
    PALF_LOG(INFO, "wait LogSharedQueueTh success");
  }
  return ret;
}

void LogSharedQueueTh::destroy()
{
  if (IS_INIT) {
    stop();
    wait();
  }
  submit_log_queue_.destroy();
  shared_queue_.destroy();
  palf_env_impl_ = NULL;
  if (IS_INIT) {
    is_inited_ = false;
  }
  PALF_LOG(INFO, "destroy LogSharedQueueTh success");
}

int LogSharedQueueTh::push_submit_log_task(LogHandleSubmitTask *task)
{
  int ret = OB_SUCCESS;
  if (NULL == task) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t print_log_interval = OB_INVALID_TIMESTAMP;
    while (OB_FAIL(submit_log_queue_.push(task))) {
      if (OB_IN_STOP_STATE == ret) {
        PALF_LOG(WARN, "thread_pool has been stopped, skip task", K(ret), KPC(task));
        break;
      } else if (palf_reach_time_interval(5 * 1000 * 1000, print_log_interval)) {
        PALF_LOG(ERROR, "push task failed", K(ret), KPC(task));
      }
      ob_usleep(1000);
    }
  }
  return ret;
}

int LogSharedQueueTh::push_task(LogSharedTask *task)
{
  int ret = OB_SUCCESS;
  if (NULL == task) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_FAIL(shared_queue_.push(task))) {
      if (OB_IN_STOP_STATE == ret) {
        PALF_LOG(WARN, "thread_pool has been stopped, skip task", K(ret), KPC(task));
      }
    }
  }
  return ret;
}

void LogSharedQueueTh::handle(void *task)
{
  int ret = OB_SUCCESS;
  LogSharedTask *log_shared_task = reinterpret_cast<LogSharedTask*>(task);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogSharedQueueTh not inited", K(ret));
  } else if (OB_ISNULL(log_shared_task)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument", K(ret), K(log_shared_task));
  } else if (OB_FAIL(log_shared_task->do_task(palf_env_impl_))) {
  } else {
  }
  if (OB_NOT_NULL(log_shared_task)) {
    log_shared_task->free_this(palf_env_impl_);
  }
}

} // end namespace palf
} // end namespace oceanbase
