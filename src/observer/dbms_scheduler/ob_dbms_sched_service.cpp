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
#include "observer/dbms_scheduler/ob_dbms_sched_job_utils.h"
#include "share/rc/ob_server_runtime.h"
#include "share/rc/ob_server_runtime.h"
#define USING_LOG_PREFIX SERVER

namespace oceanbase
{
using namespace common;
using namespace oceanbase::share;
namespace rootserver
{

int ObDBMSSchedService::server_module_init(ObDBMSSchedService *&dbms_sched_service)
{
  return dbms_sched_service->init();
}

int ObDBMSSchedService::allocate_job_id(int64_t &job_id)
{
  return dbms_scheduler::ObDBMSSchedJobUtils::generate_job_id(job_id);
}

int ObDBMSSchedService::create_job(
    common::ObISQLClient &sql_client,
    int64_t job_id,
    const dbms_scheduler::ObDBMSSchedJobInfo &job_info)
{
  return dbms_scheduler::ObDBMSSchedJobUtils::create_dbms_sched_job(
      sql_client, job_id, job_info);
}

int ObDBMSSchedService::init()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", KR(ret));
  } else if (OB_FAIL(job_master_.init(GCTX.sql_proxy_, GCTX.schema_service_))) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master init failed");
  } else if (OB_FAIL(ObServerThreadHelper::create(
      "DBMSSched",
      1))) {
    LOG_WARN("[DBMS_SCHED_SERVICE] fail to create thread", KR(ret));
  } else {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService init success");
  }
  return ret;
}

int ObDBMSSchedService::start()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.start())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master start failed", K(ret));
  } else if (OB_FAIL(ObServerThreadHelper::start())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] failed to start thread", KR(ret));
  } else {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService start success");
  }
  return ret;
}

void ObDBMSSchedService::do_work()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.scheduler())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master sched failed", K(ret));
  }
}

void ObDBMSSchedService::stop()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.stop())) {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService stop failure");
  } else {
    ObServerThreadHelper::stop();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService stop success");
  }
}

void ObDBMSSchedService::wait()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else {
    ObServerThreadHelper::wait();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService wait success");
  }
}

void ObDBMSSchedService::destroy()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    if (OB_FAIL(job_master_.destroy())) {
      LOG_WARN("[DBMS_SCHED_SERVICE] job master destroy failed", K(ret));
    } else {
      LOG_INFO("[DBMS_SCHED_SERVICE] job master destroy success");
    }
    ObServerThreadHelper::destroy();
  }
  LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService destroy success");
}

void ObDBMSSchedService::deactivate()
{
  if (job_master_.is_inited()) {
    job_master_.switch_to_follower();
    ObServerThreadHelper::switch_to_follower_gracefully();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService switch follower");
  }
}
int ObDBMSSchedService::activate()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    job_master_.switch_to_leader();
    if (OB_FAIL(ObServerThreadHelper::switch_to_leader())) {
      LOG_WARN("[DBMS_SCHED_SERVICE] failed to switch helper thread to leader", KR(ret));
    }
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService switch leader");
  }
  return ret;
}
void ObDBMSSchedService::wakeup_scheduler()
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    rootserver::ObDBMSSchedService *svc = ::oceanbase::share::server_service<::oceanbase::rootserver::ObDBMSSchedService>();
    if (OB_NOT_NULL(svc)) {
      svc->job_master_.wakeup();
    }
  }
}

}  // namespace rootserver
}  // namespace oceanbase
