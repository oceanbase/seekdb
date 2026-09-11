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

#include "embed_warm_manifest_timer_task.h"
#include "palf_env_impl.h"
namespace oceanbase
{
namespace palf
{
EmbedWarmManifestTimerTask::EmbedWarmManifestTimerTask()
  : palf_env_impl_(NULL), warn_time_(OB_INVALID_TIMESTAMP), timer_(), is_inited_(false)
{}

EmbedWarmManifestTimerTask::~EmbedWarmManifestTimerTask()
{
  palf_env_impl_ = NULL;
  is_inited_ = false;
}

int EmbedWarmManifestTimerTask::init(PalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  if (NULL == palf_env_impl) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(timer_.init("PalfWarmMfst", common::ObMemAttr("PalfWarmMfst")))) {
  } else {
    palf_env_impl_ = palf_env_impl;
    is_inited_ = true;
  }
  return ret;
}

int EmbedWarmManifestTimerTask::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(timer_.schedule(*this, EMBED_WARM_MANIFEST_TIMER_INTERVAL_MS, true))) {
  }
  return ret;
}

void EmbedWarmManifestTimerTask::stop()
{
  if (IS_INIT) {
    timer_.stop();
  }
}

void EmbedWarmManifestTimerTask::wait()
{
  if (IS_INIT) {
    timer_.wait();
  }
}

void EmbedWarmManifestTimerTask::destroy()
{
  is_inited_ = false;
  timer_.destroy();
  palf_env_impl_ = NULL;
}

void EmbedWarmManifestTimerTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (NULL == palf_env_impl_) {
    PALF_LOG(ERROR, "palf_env_impl_ is NULL, unexpected error");
  } else if (OB_FAIL(palf_env_impl_->save_embed_warm_manifest())) {
    if (palf_reach_time_interval(10 * 1000 * 1000, warn_time_)) {
      PALF_LOG(WARN, "periodic embed warm manifest save failed", K(ret));
    }
  }
}
} // end namespace palf
} // end namespace oceanbase
