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
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_package_info.h"

namespace oceanbase
{
namespace rootserver
{

namespace
{
int scheduler_package_needs_refresh(bool &needs_refresh)
{
  int ret = OB_SUCCESS;
  needs_refresh = false;
  share::schema::ObSchemaGetterGuard guard;
  const common::ObString package_name = common::ObString::make_string("DBMS_SCHEDULER");
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(guard))) {
  } else {
    const share::schema::ObPackageType parts[] = {
        share::schema::PACKAGE_TYPE, share::schema::PACKAGE_BODY_TYPE};
    for (const auto type : parts) {
      const share::schema::ObPackageInfo *package_info = NULL;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(guard.get_package_info(common::OB_SYS_DATABASE_ID,
                                                package_name, type, package_info))) {
      } else if (OB_ISNULL(package_info) || !package_info->is_invoker_right()
                 || (package_info->get_flag() & share::schema::PKG_FLAG_INVALID) != 0) {
        needs_refresh = true;
      }
    }
  }
  return ret;
}

int ensure_scheduler_invoker_rights(common::ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  bool needs_refresh = false;
  if (OB_FAIL(scheduler_package_needs_refresh(needs_refresh))) {
  } else if (needs_refresh) {
    common::ObString package_name = common::ObString::make_string("DBMS_SCHEDULER");
    LOG_INFO("refresh scheduler invoker rights before marking system packages ready");
    if (OB_FAIL(pl::ObPLPackageManager::load_sys_package(sql_proxy, package_name, false))) {
    } else if (OB_FAIL(scheduler_package_needs_refresh(needs_refresh))) {
    } else if (needs_refresh) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("scheduler package refresh did not install valid invoker-rights spec and body", K(ret));
    }
  }
  return ret;
}
} // namespace


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
  } else {
    if (timer.task_exist(*this)) {
      // ignore duplicate schedule
    } else if (OB_FAIL(timer.schedule(*this, SCHEDULE_INTERVAL_US, did_repeat))) {
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
    }
    if (OB_FAIL(timer.wait_task(*this))) {
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
  bool packages_loaded = false;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(GET_ADMIN_JOB_COUNT(LOAD_MYSQL_SYS_PACKAGE, job_count))) {
  } else if (0 == job_count) {
    // job not exists, try insert inprogress job
    if (OB_FAIL(ADMIN_JOB_CREATE_WITH_RET(job_id, JOB_TYPE_LOAD_MYSQL_SYS_PACKAGE))) {
    }
  } else if (OB_FAIL(ADMIN_JOB_FIND(LOAD_MYSQL_SYS_PACKAGE, job_id))) {
    if (ret == OB_ENTRY_NOT_EXIST) {
      ret = OB_SUCCESS;
      packages_loaded = true;
      LOG_INFO("find a success job or job_id not exist", KR(ret), K(job_id));
    } else {
    }
  } else if (OB_FAIL(pl::ObPLPackageManager::load_all_common_sys_package(
                         *sql_proxy,
                         false/*from_file*/))) {
  } else if (OB_FAIL(ADMIN_JOB_COMPLETE(job_id, 0/*result_code*/))) {
  } else {
    packages_loaded = true;
  }
  if (OB_SUCC(ret) && packages_loaded) {
    // Old data directories keep a completed common-package job. Use the
    // persisted spec AND body properties as the migration marker, so a failed
    // body reload is retried rather than mistaken for a completed refresh.
    if (OB_FAIL(ensure_scheduler_invoker_rights(*sql_proxy))) {
    } else {
      GCTX.sys_package_ready_ = true;
    }
  }
  return ret;
}

void ObSystemPackageLoadTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (!share::server_is_write_enabled()) {
    LOG_INFO("read-only server skips loading sys package");
  } else if (GCTX.sys_package_ready_) {
    LOG_INFO("sys package already loaded");
  } else if (OB_FAIL(load_system_package_())) {
    fail_count_++;
    if (fail_count_ >= 5) {
      LOG_ERROR("failed to execute system package load task, will retry", KR(ret), K_(fail_count));
    } else {
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
