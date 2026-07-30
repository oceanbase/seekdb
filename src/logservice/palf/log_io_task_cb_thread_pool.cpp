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
#include "palf_env_impl.h"                    // PalfEnvImpl

namespace oceanbase
{
namespace palf
{
LogIOTaskCbThreadPool::LogIOTaskCbThreadPool()
    : common::ObLinkQueueThreadPool(),
      thread_num_(0),
      palf_env_impl_(NULL),
      is_inited_(false)
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
  const int64_t thread_num = THREAD_NUM;
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
  } else {
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
  PALF_LOG(INFO, "destroy LogIOTaskCbThreadPool success", K(thread_num_));
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
