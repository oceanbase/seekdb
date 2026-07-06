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

#include "observer/dbms_scheduler/ob_dbms_sched_service.h"
#include "share/rc/ob_module_provider.h"
#include "share/rc/ob_tenant_base.h"
#define USING_LOG_PREFIX SERVER

namespace oceanbase
{
using namespace common;
using namespace oceanbase::share;
namespace rootserver
{

int ObDBMSSchedService::mtl_init(ObDBMSSchedService *&dbms_sched_service)
{
  return dbms_sched_service->init();
}

int ObDBMSSchedService::init()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (job_master_.is_inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", KR(ret));
  } else if (OB_FAIL(job_master_.init(GCTX.sql_proxy_, GCTX.schema_service_))) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master init failed");
  } else if (OB_FAIL(ObTenantThreadHelper::create(
      "DBMSSched",
      lib::TGDefIDs::DBMSSchedService,
      *this))) {
    LOG_WARN("[DBMS_SCHED_SERVICE] fail to create thread", KR(ret));
  } else {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService init success");
  }
  return ret;
}

int ObDBMSSchedService::start()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.start())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master start failed", K(ret));
  } else if (OB_FAIL(ObTenantThreadHelper::start())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] failed to start thread", KR(ret));
  } else {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService start success");
  }
  return ret;
}

void ObDBMSSchedService::do_work()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.scheduler())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master sched failed", K(ret));
  }
}

void ObDBMSSchedService::stop()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.stop())) {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService stop failure");
  } else {
    ObTenantThreadHelper::stop();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService stop success");
  }
}

void ObDBMSSchedService::wait()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else {
    ObTenantThreadHelper::wait();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService wait success");
  }
}

void ObDBMSSchedService::destroy()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (job_master_.is_inited()) {
    if (OB_FAIL(job_master_.destroy())) {
      LOG_WARN("[DBMS_SCHED_SERVICE] job master destroy failed", K(ret));
    } else {
      LOG_INFO("[DBMS_SCHED_SERVICE] job master destroy success");
    }
    ObTenantThreadHelper::destroy();
  }
  LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService destroy success");
}

void ObDBMSSchedService::switch_to_follower_forcedly()
{
  switch_to_follower_gracefully();
}
int ObDBMSSchedService::switch_to_leader()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (job_master_.is_inited()) {
    job_master_.switch_to_leader();
    ObTenantThreadHelper::switch_to_leader();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService switch leader");
  }
  return ret;
}
int ObDBMSSchedService::switch_to_follower_gracefully()
{
  int ret = OB_SUCCESS;
  if (false) {
    // do nothing
  } else if (job_master_.is_inited()) {
    job_master_.switch_to_follower();
    ObTenantThreadHelper::switch_to_follower_gracefully();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService switch follower");
  }
  return ret;
}
int ObDBMSSchedService::resume_leader()
{
  int ret = OB_SUCCESS;
  if (!is_leader()) {
    if (OB_FAIL(switch_to_leader())) {
       LOG_INFO("[DBMS_SCHED_SERVICE] resume leader failed");
    }
  }
  return ret;
}

void ObDBMSSchedService::wakeup_scheduler()
{
  int ret = OB_SUCCESS;
  MOD_SCOPE {
    rootserver::ObDBMSSchedService *svc = share::g_mp->dbms_sched_service();
    if (OB_NOT_NULL(svc)) {
      svc->job_master_.wakeup();
    }
  }
}

}  // namespace rootserver
}  // namespace oceanbase
