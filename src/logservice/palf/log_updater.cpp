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

#include "log_updater.h"
#include "palf_env_impl.h"                    // IPalfEnvImpl
namespace oceanbase
{
namespace palf
{
LogUpdater::LogUpdater() : palf_env_impl_(NULL), timer_(), is_inited_(false) {}

LogUpdater::~LogUpdater()
{
  destroy();
}

int LogUpdater::init(IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (NULL == palf_env_impl) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(timer_.init("LogUpdater", common::ObMemAttr("LogUpdater")))) {
    PALF_LOG(ERROR, "LogUpdater timer init failed", K(ret));
  } else {
    palf_env_impl_ = palf_env_impl;
    is_inited_ = true;
    PALF_LOG(INFO, "LogUpdater init success", KPC(palf_env_impl));
  }
  return ret;
}

int LogUpdater::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(timer_.schedule(*this, PALF_UPDATE_CACHED_STAT_INTERVAL_US, true))) {
    PALF_LOG(WARN, "LogUpdater schedule failed", K(ret));
  } else {
    PALF_LOG(INFO, "LogUpdater start success", KPC(palf_env_impl_));
  }
  return ret;
}

void LogUpdater::stop()
{
  if (IS_INIT) {
    PALF_LOG(INFO, "LogUpdater stop start", KPC(palf_env_impl_));
    timer_.stop();
    PALF_LOG(INFO, "LogUpdater stop finished", KPC(palf_env_impl_));
  }
}

void LogUpdater::wait()
{
  if (IS_INIT) {
    PALF_LOG(INFO, "LogUpdater wait start", KPC(palf_env_impl_));
    timer_.wait();
    PALF_LOG(INFO, "LogUpdater wait finished", KPC(palf_env_impl_));
  }
}

void LogUpdater::destroy()
{
  PALF_LOG(INFO, "LogUpdater destroy start", KPC(palf_env_impl_));
  is_inited_ = false;
  timer_.destroy();
  palf_env_impl_ = NULL;
  PALF_LOG(INFO, "LogUpdater destroy finish", KPC(palf_env_impl_));
}

void LogUpdater::runTimerTask()
{
  int64_t start_time_us = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (NULL == palf_env_impl_) {
    PALF_LOG(ERROR, "palf_env_impl_ is NULL, unexpected error");
  } else {
    IPalfHandleImpl *handle = nullptr;
    if (OB_SUCCESS == palf_env_impl_->get_palf_handle_impl(SYS_PALF_ID, handle)) {
      handle->update_palf_stat();
      palf_env_impl_->revert_palf_handle_impl(handle);
    }
    int64_t cost_time_us = ObTimeUtility::current_time() - start_time_us;
    if (cost_time_us >= PALF_UPDATE_CACHED_STAT_INTERVAL_US) {
      PALF_LOG(WARN, "update_palf cost too much time", K(ret), K(cost_time_us), KPC(palf_env_impl_));
    }
  }
}
} // end namespace palf
} // end namespace oceanbase
