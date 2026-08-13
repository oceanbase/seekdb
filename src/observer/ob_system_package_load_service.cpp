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
#define USING_LOG_PREFIX RS

#include "observer/ob_system_package_load_service.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace rootserver
{

ObSystemPackageLoadService::ObSystemPackageLoadService()
  : inited_(false),
    timer_(),
    task_()
{
}

int ObSystemPackageLoadService::server_module_init(ObSystemPackageLoadService *&service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("service is null", KR(ret), KP(service));
  } else if (OB_FAIL(service->init())) {
  }
  return ret;
}

int ObSystemPackageLoadService::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(task_.init())) {
  } else {
    inited_ = true;
  }
  return ret;
}

int ObSystemPackageLoadService::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret), K_(inited));
  } else if (!timer_.inited()
      && OB_FAIL(timer_.init("SystemPkgLoad", common::ObMemAttr("SystemPkgLoad")))) {
    LOG_WARN("fail to init timer", KR(ret));
  } else if (OB_FAIL(timer_.start())) {
  } else if (OB_FAIL(task_.start(timer_))) {
  }
  return ret;
}

void ObSystemPackageLoadService::stop()
{
  const int64_t start_time = ObTimeUtility::fast_current_time();
  FLOG_INFO("start to stop system package load service");
  if (timer_.inited()) {
    task_.stop(timer_);
    timer_.stop();
  }
  const int64_t cost = ObTimeUtility::fast_current_time() - start_time;
  FLOG_INFO("finish to stop system package load service", K(cost));
}

int ObSystemPackageLoadService::wait()
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::fast_current_time();
  FLOG_INFO("start to wait system package load service");
  if (timer_.inited()) {
    timer_.wait();
  }
  const int64_t cost = ObTimeUtility::fast_current_time() - start_time;
  FLOG_INFO("finish to wait system package load service", K(cost));
  return ret;
}

void ObSystemPackageLoadService::destroy()
{
  FLOG_INFO("start to destroy system package load service");
  timer_.destroy();
  if (inited_) {
    task_.destroy();
    inited_ = false;
  }
  FLOG_INFO("finish to destroy system package load service");
}

int ObSystemPackageLoadService::activate()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret), K_(inited));
  } else if (OB_FAIL(start())) {
  }
  return ret;
}

void ObSystemPackageLoadService::deactivate()
{
  if (OB_UNLIKELY(!inited_)) {
    LOG_WARN_RET(OB_NOT_INIT, "service not inited", K_(inited));
  } else {
    stop();
  }
}

} // namespace rootserver
} // namespace oceanbase
