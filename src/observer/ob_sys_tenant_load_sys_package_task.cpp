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

#include "observer/ob_sys_tenant_load_sys_package_task.h"
#include "pl/ob_pl_package_manager.h"
#include "rootserver/ob_rs_job_table_operator.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/thread/thread_mgr.h" // for TG_SCHEDULE/TG_CANCEL_TASK/TG_WAIT_TASK/TG_TASK_EXIST

namespace oceanbase
{
namespace rootserver
{

const int64_t ObSysTenantLoadSysPackageTask::SCHEDULE_INTERVAL_US;

ObSysTenantLoadSysPackageTask::ObSysTenantLoadSysPackageTask()
  : inited_(false),
    fail_count_(0)
{
}

int ObSysTenantLoadSysPackageTask::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_UNLIKELY(!is_sys_tenant(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ObSysTenantLoadSysPackageTask should only run on sys tenant", KR(ret), K(tenant_id));
  } else {
    inited_ = true;
    fail_count_ = 0;
  }
  return ret;
}

int ObSysTenantLoadSysPackageTask::start(const int tg_id)
{
  int ret = OB_SUCCESS;
  const bool did_repeat = true;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task not inited", KR(ret), K_(inited));
  } else if (OB_UNLIKELY(-1 == tg_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tg id", KR(ret), K(tg_id));
  } else {
    bool is_exist = false;
    if (OB_FAIL(TG_TASK_EXIST(tg_id, *this, is_exist))) {
      LOG_WARN("failed to check task existence", KR(ret), K(tg_id));
    } else if (is_exist) {
      // ignore duplicate schedule
      LOG_TRACE("timer task already exist", K(tg_id));
    } else if (OB_FAIL(TG_SCHEDULE(tg_id, *this, SCHEDULE_INTERVAL_US, did_repeat))) {
      LOG_WARN("failed to schedule timer task", KR(ret), K(tg_id), K(SCHEDULE_INTERVAL_US), K(did_repeat));
    } else {
      LOG_INFO("finish schedule timer task", K(tg_id), K(SCHEDULE_INTERVAL_US));
    }
  }
  return ret;
}

void ObSysTenantLoadSysPackageTask::stop(const int tg_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(-1 == tg_id)) {
    LOG_WARN("invalid tg id when cancel timer task", K(tg_id));
  } else {
    TG_CANCEL_TASK(tg_id, *this);
    TG_WAIT_TASK(tg_id, *this);
  }
}

void ObSysTenantLoadSysPackageTask::destroy()
{
  inited_ = false;
  fail_count_ = 0;
}

ERRSIM_POINT_DEF(ERRSIM_LOAD_PACKAGE_ERROR);
int ObSysTenantLoadSysPackageTask::do_sys_tenant_load_sys_package_()
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  int64_t job_id = OB_INVALID_ID;
  int64_t job_count = 0;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", KR(ret), KP(sql_proxy));
  } else if (OB_FAIL(GET_RS_JOB_COUNT(LOAD_MYSQL_SYS_PACKAGE, job_count))) {
    LOG_WARN("fail to get rs job count", KR(ret), K(job_count));
  } else if (0 == job_count) {
    // job not exists, try insert inprogress job
    if (OB_FAIL(RS_JOB_CREATE_WITH_RET(job_id, JOB_TYPE_LOAD_MYSQL_SYS_PACKAGE))) {
      LOG_WARN("failed to create load mysql sys package job", KR(ret));
    }
  } else if (OB_FAIL(RS_JOB_FIND(LOAD_MYSQL_SYS_PACKAGE, job_id))) {
    if (ret == OB_ENTRY_NOT_EXIST) {
      ret = OB_SUCCESS;
      GCTX.sys_package_ready_ = true;
      LOG_INFO("find a success job or job_id not exist", KR(ret), K(job_id));
    } else {
      LOG_WARN("failed to get INPROGRESS rs job", KR(ret));
    }
  } else if (OB_FAIL(pl::ObPLPackageManager::load_all_common_sys_package(
                         *sql_proxy,
                         ObCompatibilityMode::MYSQL_MODE,
                         false/*from_file*/))) {
    LOG_WARN("failed to load package", KR(ret));
  } else if (OB_FAIL(ERRSIM_LOAD_PACKAGE_ERROR)) {
    LOG_WARN("ERRSIM_LOAD_PACKAGE_ERROR", KR(ret));
  } else if (OB_FAIL(RS_JOB_COMPLETE(job_id, 0/*result_code*/))) {
    LOG_WARN("failed to complete rs job", KR(ret), K(job_id));
  } else {
    GCTX.sys_package_ready_ = true;
  }
  return ret;
}

void ObSysTenantLoadSysPackageTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task not inited", KR(ret), K_(inited));
  } else if (GCTX.is_standby_cluster()) {
    LOG_INFO("standby cluster skip loading sys package");
    usleep(60 * 1000 * 1000); // 1minute
  } else if (GCTX.sys_package_ready_) {
    LOG_INFO("sys package already loaded");
    usleep(60 * 1000 * 1000); // 1minute
  } else if (OB_FAIL(do_sys_tenant_load_sys_package_())) {
    fail_count_++;
    if (fail_count_ >= 5) {
      LOG_ERROR("failed to execute sys tenant load sys package task, will retry", KR(ret), K_(fail_count));
    } else {
      LOG_WARN("failed to execute sys tenant load sys package task, will retry", KR(ret), K_(fail_count));
    }
  } else {
    fail_count_ = 0;
    LOG_INFO("finish loading sys packages");
  }
}

int ObSysTenantLoadSysPackageTask::wait_sys_package_ready(
    const common::ObTimeoutCtx &ctx,
    ObCompatibilityMode mode)
{
  int ret = OB_SUCCESS;
  const int64_t retry_interval_us = 500l * 1000l;
  int64_t job_id = OB_INVALID_ID;
  bool finish = false;
  int64_t inprogress_job_count = 0;
  while (OB_SUCC(ret) && !finish) {
    int tmp_ret = OB_SUCCESS;
    if (ctx.is_timeouted()) {
      ret = OB_TIMEOUT;
      LOG_WARN("wait sys package ready failed", KR(ret), K(mode));
    } else {
      inprogress_job_count = 0;
      if (mode != ObCompatibilityMode::ORACLE_MODE
          && OB_ENTRY_NOT_EXIST != (tmp_ret = RS_JOB_FIND(LOAD_MYSQL_SYS_PACKAGE, job_id))) {
        inprogress_job_count++;
      }
      if (inprogress_job_count == 0) {
        finish = true;
      } else {
        ob_usleep(retry_interval_us);
      }
    }
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
