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
#include "log_io_worker_wrapper.h"

namespace oceanbase
{
namespace palf
{

LogIOWorkerWrapper::LogIOWorkerWrapper()
    : log_io_worker_(), throttle_(), is_inited_(false)
{}


LogIOWorkerWrapper::~LogIOWorkerWrapper()
{
  destroy();
}

void LogIOWorkerWrapper::destroy()
{
  is_inited_ = false;
  throttle_.reset();
  log_io_worker_.destroy();
}
int LogIOWorkerWrapper::init(const LogIOWorkerConfig &config,
                             LogIOTaskCbThreadPool *cb_thread_pool,
                             ObIAllocator *allocator,
                             IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("LogIOWorkerWrapper has inited twice", K(config));
  } else if (!config.is_valid() || OB_ISNULL(cb_thread_pool)
             || OB_ISNULL(allocator) || OB_ISNULL(palf_env_impl)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(config), KP(cb_thread_pool), KP(allocator),
             KP(palf_env_impl));
  } else if (OB_FAIL(log_io_worker_.init(config, cb_thread_pool, allocator,
                                         &throttle_, false, palf_env_impl))) {
    LOG_WARN("init log io worker failed", K(config));
  } else {
    throttle_.reset();
    is_inited_ = true;
    LOG_INFO("success to init LogIOWorkerWrapper", K(config), KPC(this));
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int LogIOWorkerWrapper::start()
{
  int ret = log_io_worker_.start();
  if (OB_FAIL(ret)) {
    LOG_WARN("failed to start log_io_workers_");
  } else {
    LOG_INFO("success to start LogIOWorkerWrapper", KPC(this));
  }
  return ret;
}

void LogIOWorkerWrapper::stop()
{
  PALF_LOG(INFO, "LogIOWorkerWrapper starts stopping", KPC(this));
  log_io_worker_.stop();
  PALF_LOG(INFO, "LogIOWorkerWrapper has finished stopping", KPC(this));
}

void LogIOWorkerWrapper::wait()
{
  PALF_LOG(INFO, " LogIOWorkerWrapper starts waiting", KPC(this));
  log_io_worker_.wait();
  PALF_LOG(INFO, "LogIOWorkerWrapper has finished waiting", KPC(this));
}

int LogIOWorkerWrapper::notify_need_writing_throttling(const bool &need_throttling)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    throttle_.notify_need_writing_throttling(need_throttling);
    if (need_throttling) {
      LOG_INFO("success to notify_need_writing_throttling True");
    }
  }
  return ret;
}

int64_t LogIOWorkerWrapper::get_last_working_time() const
{
  int64_t last_working_time = OB_INVALID_TIMESTAMP;
  if (IS_NOT_INIT) {
    PALF_LOG_RET(ERROR, OB_NOT_INIT, "LogIOWorkerWrapper not inited", KPC(this));
  } else {
    last_working_time = log_io_worker_.get_last_working_time();
  }
  return last_working_time;
}
}//end of namespace palf
}//end of namespace oceanbase
