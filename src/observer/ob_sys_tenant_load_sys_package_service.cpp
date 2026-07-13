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

#include "observer/ob_sys_tenant_load_sys_package_service.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace rootserver
{

ObSysTenantLoadSysPackageService::ObSysTenantLoadSysPackageService()
  : inited_(false),
    //is_stopped_(false),
    timer_(),
    task_()
{
}

int ObSysTenantLoadSysPackageService::mtl_init(ObSysTenantLoadSysPackageService *&service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("service is null", KR(ret), KP(service));
  } else if (OB_FAIL(service->init())) {
    LOG_WARN("failed to init ObSysTenantLoadSysPackageService", KR(ret));
  }
  return ret;
}

int ObSysTenantLoadSysPackageService::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(task_.init())) {
    LOG_WARN("failed to init ObSysTenantLoadSysPackageTask", KR(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObSysTenantLoadSysPackageService::start()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret), K_(inited));
  } else if (!timer_.inited()
      && OB_FAIL(timer_.init("SysTntLoadPkg", common::ObMemAttr("SysTntLoadPkg")))) {
    LOG_WARN("fail to init timer", KR(ret));
  } else if (OB_FAIL(timer_.start())) {
    LOG_WARN("fail to start timer", KR(ret));
  } else if (OB_FAIL(task_.start(timer_))) {
    LOG_WARN("failed to start sys tenant load sys package task", KR(ret));
  }
  return ret;
}

void ObSysTenantLoadSysPackageService::stop()
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::fast_current_time();
  FLOG_INFO("start to stop sys package load service");
  //is_stopped_ = true;
  if (timer_.inited()) {
    task_.stop(timer_);
    timer_.stop();
  }
  const int64_t cost = ObTimeUtility::fast_current_time() - start_time;
  FLOG_INFO("finish to stop sys package load service", K(cost));
}

int ObSysTenantLoadSysPackageService::wait()
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::fast_current_time();
  FLOG_INFO("start to wait sys package load service");
  if (timer_.inited()) {
    timer_.wait();
  }
  const int64_t cost = ObTimeUtility::fast_current_time() - start_time;
  FLOG_INFO("finish to wait sys package load service", K(cost));
  return ret;
}

void ObSysTenantLoadSysPackageService::destroy()
{
  FLOG_INFO("start to destroy sys package load service");
  //is_stopped_ = true;
  timer_.destroy();
  if (inited_) {
    task_.destroy();
    inited_ = false;
  }
  FLOG_INFO("finish to destroy sys package load service");
}

int ObSysTenantLoadSysPackageService::switch_to_leader()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret), K_(inited));
  } else if (OB_FAIL(start())) {
    LOG_WARN("failed to switch to leader for sys tenant load sys package service", KR(ret));
  }
  return ret;
}

void ObSysTenantLoadSysPackageService::switch_to_follower_forcedly()
{
  int ret = switch_to_follower_gracefully();
  if (OB_FAIL(switch_to_follower_gracefully())) {
    LOG_WARN("failed to switch to follower gracefully", KR(ret));
  }
}

int ObSysTenantLoadSysPackageService::switch_to_follower_gracefully()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret), K_(inited));
  } else {
    stop();
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
