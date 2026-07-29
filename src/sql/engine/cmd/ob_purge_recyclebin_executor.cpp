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
#include "sql/engine/cmd/ob_purge_recyclebin_executor.h"
#include "rootserver/ob_rs_serial_call.h"
#include "rootserver/ob_local_ddl_service.h"

#include "sql/resolver/ddl/ob_purge_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "observer/ob_inner_sql_connection_pool.h"
namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{
int ObPurgeRecycleBinExecutor::execute(ObExecContext &ctx, ObPurgeRecycleBinStmt &stmt)
{
  int ret = OB_SUCCESS;
  //use to test purge recyclebin objects
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  const obcall::ObPurgeRecycleBinArg &purge_recyclebin_arg = stmt.get_purge_recyclebin_arg();

//  int64_t current_time = ObTimeUtility::current_time();
//  obcall::Int64 expire_time = current_time - GCONF.schema_history_expire_time;
  obcall::Int64 affected_rows = 0;
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    const_cast<obcall::ObPurgeRecycleBinArg&>(purge_recyclebin_arg).ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else {
    bool is_purge_finished = false;
    int64_t total_purge_count = 0;

    while (OB_SUCC(ret) && !is_purge_finished) {
      // Purge a bounded batch to avoid blocking the DDL thread.
      // Each time return the number of purged rows, only when the purge count is less than affected_rows
      int64_t cal_timeout = 0;
      int64_t start_time = ObTimeUtility::current_time();
      if (OB_FAIL(GSCHEMASERVICE.cal_purge_need_timeout(purge_recyclebin_arg, cal_timeout))) {
        LOG_WARN("fail to cal purge time out", KR(ret));
      } else if (0 == cal_timeout) {
        is_purge_finished = true;
      } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.local_ddl_service_->purge_expire_recycle_objects(purge_recyclebin_arg, affected_rows); }))) {
        LOG_WARN("purge reyclebin objects failed", K(ret), K(affected_rows), K(purge_recyclebin_arg));
        // If failure occurs, there is no need to continue
        is_purge_finished = false;
      } else {
        is_purge_finished = obcall::ObPurgeRecycleBinArg::DEFAULT_PURGE_EACH_TIME != affected_rows;
        total_purge_count += affected_rows;
      }
      int64_t cost_time = ObTimeUtility::current_time() - start_time;
      LOG_INFO("purge recycle objects", KR(ret), K(cost_time), K(cal_timeout),
               K(total_purge_count), K(purge_recyclebin_arg), K(affected_rows), K(is_purge_finished));
    }
    LOG_INFO("purge recyclebin success", KR(ret), K(purge_recyclebin_arg), K(total_purge_count));
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
