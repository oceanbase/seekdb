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

#include "block_gc_timer_task.h"
#include "palf_env_impl.h"                    // PalfEnvImpl
namespace oceanbase
{
namespace palf
{
BlockGCTimerTask::BlockGCTimerTask() : palf_env_impl_(NULL), timer_(), is_inited_(false) {}

BlockGCTimerTask::~BlockGCTimerTask() { palf_env_impl_ = NULL; is_inited_ = false; }

int BlockGCTimerTask::init(PalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (NULL == palf_env_impl) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(timer_.init("PalfBlockGC", common::ObMemAttr("PalfBlockGC")))) {
    PALF_LOG(ERROR, "BlockGCTimerTask timer init failed", K(ret));
  } else {
    palf_env_impl_ = palf_env_impl;
    PALF_LOG(INFO, "BlockGCTimerTask init success", KPC(palf_env_impl));
    is_inited_ = true;
  }
  return ret;
}

int BlockGCTimerTask::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(timer_.schedule(*this, BLOCK_GC_TIMER_INTERVAL_MS, true))) {
    PALF_LOG(WARN, "BlockGCTimerTask schedule failed", K(ret));
  } else {
    PALF_LOG(INFO, "BlockGCTimerTask start success", KPC(palf_env_impl_));
  }
  return ret;
}

void BlockGCTimerTask::stop()
{
  if (IS_INIT) {
    timer_.stop();
    PALF_LOG(INFO, "BlockGCTimerTask stop finished", KPC(palf_env_impl_));
  }
}

void BlockGCTimerTask::wait()
{
  if (IS_INIT) {
    timer_.wait();
    PALF_LOG(INFO, "BlockGCTimerTask wait finished", KPC(palf_env_impl_));
  }
}

void BlockGCTimerTask::destroy()
{
  PALF_LOG(INFO, "BlockGCTimerTask destroy finished", KPC(palf_env_impl_));
  is_inited_ = false;
  timer_.destroy();
  palf_env_impl_ = NULL;
}

void BlockGCTimerTask::runTimerTask()
{
  int64_t start_time_us = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (NULL == palf_env_impl_) {
    PALF_LOG(ERROR, "palf_env_impl_ is NULL, unexpected error");
  } else if (OB_FAIL(palf_env_impl_->try_recycle_blocks())) {
    PALF_LOG(WARN, "PalfEnvImpl try_recycle_blocks failed");
  } else {
    int64_t cost_time_us = ObTimeUtility::current_time() - start_time_us;
    if (cost_time_us >= 1 * 1000) {
      PALF_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "try_recycle_blocks cost too much time", K(ret), K(cost_time_us), KPC(palf_env_impl_));
    }
    if (palf_reach_time_interval(10 * 1000 * 1000, warn_time_)) {
      PALF_LOG(INFO, "BlockGCTimerTask success", K(ret), K(cost_time_us), KPC(palf_env_impl_));
    }
  }
}
} // end namespace palf
} // end namespace oceanbase
