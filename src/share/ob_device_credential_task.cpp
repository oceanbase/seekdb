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

#include "share/ob_device_credential_task.h"
#include "lib/time/ob_time_utility.h"
#include "share/ob_thread_mgr.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;

ObDeviceCredentialTask::ObDeviceCredentialTask() : is_inited_(false), schedule_interval_us_(0)
{}

ObDeviceCredentialTask::~ObDeviceCredentialTask()
{}

int ObDeviceCredentialTask::init(const int64_t interval_us)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObDeviceCredentialTask has already been inited", K(ret));
  } else if (interval_us <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(ret), K(interval_us));
  } else {
    schedule_interval_us_ = interval_us;
    if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::ServerGTimer, *this, schedule_interval_us_, true /*schedule repeatly*/))) {
      LOG_ERROR("fail to schedule task ObDeviceCredentialTask", K(ret), K(interval_us), KPC(this));
    } else {
      is_inited_ = true;
    }
    if (OB_FAIL(ret)) {
      reset();
    }
  }
  return ret;
}

void ObDeviceCredentialTask::reset()
{
  is_inited_ = false;
  schedule_interval_us_ = 0;
}

void ObDeviceCredentialTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  const int64_t start_us = common::ObTimeUtility::fast_current_time();
  LOG_INFO("device credential task start", K(start_us));
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("device credential task not init", K(ret));
  } else if (OB_FAIL(do_work_())) {
    LOG_WARN("fail to do work", K(ret));
  }
  const int64_t cost_us = common::ObTimeUtility::fast_current_time() - start_us;
  LOG_INFO("device credential task finish", K(cost_us));
}

int ObDeviceCredentialTask::do_work_()
{
  int ret = OB_SUCCESS;
  ObCurTraceId::init(GCONF.self_addr_);
  if (OB_FAIL(ObDeviceCredentialMgr::get_instance().refresh())) {
    OB_LOG(WARN, "failed to refresh device credentials", K(ret));
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
