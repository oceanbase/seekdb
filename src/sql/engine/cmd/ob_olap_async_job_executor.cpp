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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/ob_olap_async_job_executor.h"
#include "sql/resolver/cmd/ob_olap_async_job_stmt.h"
#include "sql/engine/ob_exec_context.h"
namespace oceanbase
{
using namespace common;
using namespace obrpc;
using namespace share::schema;
namespace sql
{
int ObOLAPAsyncCancelJobExecutor::execute(ObExecContext &ctx, ObOLAPAsyncCancelJobStmt &stmt)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = stmt.get_tenant_id();
  uint64_t user_id = stmt.get_user_id();
  const ObUserInfo *user_info = nullptr;
  ObArenaAllocator allocator("ASYNC_JOB_TMP");
  dbms_scheduler::ObDBMSSchedJobInfo job_info;
  schema::ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(tenant_id, schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret), K(tenant_id));
  } else if(OB_FAIL(schema_guard.get_user_info(tenant_id, user_id, user_info))) {
    LOG_WARN("fail to get user id", KR(ret), K(tenant_id), K(user_id));
  } else if (OB_ISNULL(user_info)) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user not exist", KR(ret), K(tenant_id), K(user_id));
  } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::get_dbms_sched_job_info(
        *GCTX.sql_proxy_,
        tenant_id,
        false, // is_oracle_tenant
        stmt.get_job_name(),
        allocator,
        job_info))) {
    LOG_WARN("get job info failed", KR(ret), K(tenant_id), K(stmt.get_job_name()));
  } else if (!job_info.is_olap_async_job()) { 
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("cancel not olap async job", KR(ret), K(tenant_id), K(job_info));
  } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::check_dbms_sched_job_priv(user_info, job_info))) {
    LOG_WARN("check user priv failed", KR(ret), K(tenant_id), K(job_info));
  } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::stop_dbms_sched_job(*GCTX.sql_proxy_, job_info, true /* delete after stop */))) {
    if (OB_ENTRY_NOT_EXIST == ret) {//current job is not running, no need to report an error}
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to stop dbms scheduler job", KR(ret));
    }
  }
  return ret;
}

}
}
