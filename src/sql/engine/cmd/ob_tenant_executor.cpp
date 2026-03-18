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
#include "sql/engine/cmd/ob_tenant_executor.h"

#include "sql/resolver/ddl/ob_flashback_stmt.h"
#include "sql/resolver/ddl/ob_purge_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "sql/resolver/cmd/ob_create_restore_point_stmt.h"
#include "sql/resolver/cmd/ob_drop_restore_point_stmt.h"
#include "observer/ob_inner_sql_connection_pool.h"
#include "rootserver/ob_tenant_ddl_service.h"
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
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  const obrpc::ObPurgeRecycleBinArg &purge_recyclebin_arg = stmt.get_purge_recyclebin_arg();

//  int64_t current_time = ObTimeUtility::current_time();
//  obrpc::Int64 expire_time = current_time - GCONF.schema_history_expire_time;
  obrpc::Int64 affected_rows = 0;
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    const_cast<obrpc::ObPurgeRecycleBinArg&>(purge_recyclebin_arg).ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed");
  } else {
    bool is_tenant_finish = false;
    int64_t total_purge_count = 0;
    uint64_t tenant_id = purge_recyclebin_arg.tenant_id_;
    while (OB_SUCC(ret) && !is_tenant_finish) {
      // A tenant only purges 10 objects from the recycle station to prevent blocking the RS's ddl thread
      // Each time return the number of purged rows, only when the purge count is less than affected_rows
      int64_t cal_timeout = 0;
      int64_t start_time = ObTimeUtility::current_time();
      if (OB_FAIL(GSCHEMASERVICE.cal_purge_need_timeout(purge_recyclebin_arg, cal_timeout))) {
        LOG_WARN("fail to cal purge time out", KR(ret), K(tenant_id));
      } else if (0 == cal_timeout) {
        is_tenant_finish = true;
      } else if (OB_FAIL(common_rpc_proxy->timeout(cal_timeout).purge_expire_recycle_objects(purge_recyclebin_arg, affected_rows))) {
        LOG_WARN("purge reyclebin objects failed", K(ret), K(affected_rows), K(purge_recyclebin_arg));
        // If failure occurs, there is no need to continue
        is_tenant_finish = false;
      } else {
        is_tenant_finish = obrpc::ObPurgeRecycleBinArg::DEFAULT_PURGE_EACH_TIME == affected_rows ? false : true;
        total_purge_count += affected_rows;
      }
      int64_t cost_time = ObTimeUtility::current_time() - start_time;
      LOG_INFO("purge recycle objects", KR(ret), K(cost_time), K(cal_timeout),
               K(total_purge_count), K(purge_recyclebin_arg), K(affected_rows), K(is_tenant_finish));
    }
    LOG_INFO("purge recyclebin success", KR(ret), K(purge_recyclebin_arg), K(total_purge_count));
  }
  return ret;
}

int ObCreateRestorePointExecutor::execute(ObExecContext &ctx, ObCreateRestorePointStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  const int64_t tenant_id = ctx.get_my_session()->get_effective_tenant_id();
  stmt.set_tenant_id(tenant_id);
  const obrpc::ObCreateRestorePointArg &create_restore_point_arg = stmt.get_create_restore_point_arg();
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed");
  } else if (OB_FAIL(common_rpc_proxy->create_restore_point(create_restore_point_arg))) {
    LOG_WARN("rpc proxy create restore point failed", K(ret));
  }
  return ret;
}
int ObDropRestorePointExecutor::execute(ObExecContext &ctx, ObDropRestorePointStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  const int64_t tenant_id = ctx.get_my_session()->get_effective_tenant_id();
  stmt.set_tenant_id(tenant_id);

  const obrpc::ObDropRestorePointArg &drop_restore_point_arg = stmt.get_drop_restore_point_arg();

  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed");
  } else if (OB_FAIL(common_rpc_proxy->drop_restore_point(drop_restore_point_arg))) {
    LOG_WARN("rpc proxy drop restore point failed", K(ret));
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
