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

#include "observer/ob_system_package_load_task.h"
#include "pl/ob_pl_package_manager.h"
#include "rootserver/ob_admin_job_table_operator.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace rootserver
{

const int64_t ObSystemPackageLoadTask::SCHEDULE_INTERVAL_US;

ObSystemPackageLoadTask::ObSystemPackageLoadTask()
  : inited_(false),
    fail_count_(0)
{
}

int ObSystemPackageLoadTask::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    inited_ = true;
    fail_count_ = 0;
  }
  return ret;
}

int ObSystemPackageLoadTask::start(common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  const bool did_repeat = true;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task not inited", KR(ret), K_(inited));
  } else {
    if (timer.task_exist(*this)) {
      // ignore duplicate schedule
      LOG_TRACE("timer task already exist");
    } else if (OB_FAIL(timer.schedule(*this, SCHEDULE_INTERVAL_US, did_repeat))) {
      LOG_WARN("failed to schedule timer task", KR(ret), K(SCHEDULE_INTERVAL_US), K(did_repeat));
    } else {
      LOG_INFO("finish schedule timer task", K(SCHEDULE_INTERVAL_US));
    }
  }
  return ret;
}

void ObSystemPackageLoadTask::stop(common::ObTimer &timer)
{
  if (timer.inited()) {
    int ret = OB_SUCCESS;
    if (OB_FAIL(timer.cancel_task(*this))) {
      LOG_WARN("failed to cancel timer task", KR(ret));
    }
    if (OB_FAIL(timer.wait_task(*this))) {
      LOG_WARN("failed to wait timer task", KR(ret));
    }
  }
}

void ObSystemPackageLoadTask::destroy()
{
  inited_ = false;
  fail_count_ = 0;
}

int ObSystemPackageLoadTask::load_system_package_()
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  int64_t job_id = OB_INVALID_ID;
  int64_t job_count = 0;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", KR(ret), KP(sql_proxy));
  } else if (OB_FAIL(GET_ADMIN_JOB_COUNT(LOAD_MYSQL_SYS_PACKAGE, job_count))) {
    LOG_WARN("fail to get rs job count", KR(ret), K(job_count));
  } else if (0 == job_count) {
    // job not exists, try insert inprogress job
    if (OB_FAIL(ADMIN_JOB_CREATE_WITH_RET(job_id, JOB_TYPE_LOAD_MYSQL_SYS_PACKAGE))) {
      LOG_WARN("failed to create system package load job", KR(ret));
    }
  } else if (OB_FAIL(ADMIN_JOB_FIND(LOAD_MYSQL_SYS_PACKAGE, job_id))) {
    if (ret == OB_ENTRY_NOT_EXIST) {
      ret = OB_SUCCESS;
      GCTX.sys_package_ready_ = true;
      LOG_INFO("find a success job or job_id not exist", KR(ret), K(job_id));
    } else {
      LOG_WARN("failed to get INPROGRESS rs job", KR(ret));
    }
  } else if (OB_FAIL(pl::ObPLPackageManager::load_all_common_sys_package(
                         *sql_proxy,
                         false/*from_file*/))) {
    LOG_WARN("failed to load package", KR(ret));
  } else if (OB_FAIL(ADMIN_JOB_COMPLETE(job_id, 0/*result_code*/))) {
    LOG_WARN("failed to complete rs job", KR(ret), K(job_id));
  } else {
    GCTX.sys_package_ready_ = true;
  }
  return ret;
}

void ObSystemPackageLoadTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("task not inited", KR(ret), K_(inited));
  } else if (GCTX.is_standby_server()) {
    LOG_INFO("standby cluster skip loading sys package");
  } else if (GCTX.sys_package_ready_) {
    LOG_INFO("sys package already loaded");
  } else if (OB_FAIL(load_system_package_())) {
    fail_count_++;
    if (fail_count_ >= 5) {
      LOG_ERROR("failed to execute system package load task, will retry", KR(ret), K_(fail_count));
    } else {
      LOG_WARN("failed to execute system package load task, will retry", KR(ret), K_(fail_count));
    }
  } else {
    fail_count_ = 0;
    LOG_INFO("finish loading sys packages");
  }
}

int ObSystemPackageLoadTask::wait_system_package_ready(const common::ObTimeoutCtx &ctx)
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
      LOG_WARN("wait sys package ready failed", KR(ret));
    } else {
      inprogress_job_count = 0;
      if (OB_ENTRY_NOT_EXIST != (tmp_ret = ADMIN_JOB_FIND(LOAD_MYSQL_SYS_PACKAGE, job_id))) {
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
