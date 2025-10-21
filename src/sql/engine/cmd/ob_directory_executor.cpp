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

#include "sql/engine/cmd/ob_directory_executor.h"
#include "sql/resolver/ddl/ob_create_directory_stmt.h"
#include "sql/resolver/ddl/ob_drop_directory_stmt.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
int ObCreateDirectoryExecutor::execute(ObExecContext &ctx, ObCreateDirectoryStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  const obrpc::ObCreateDirectoryArg &create_directory_arg = stmt.get_create_directory_arg();
  if (OB_ISNULL(ctx.get_stmt_factory()) || OB_ISNULL(ctx.get_stmt_factory()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "query ctx is null", K(ret));
  } else {
    const_cast<obrpc::ObCreateDirectoryArg&>(create_directory_arg).ddl_stmt_str_ =
                                         ctx.get_stmt_factory()->get_query_ctx()->get_sql_stmt();
  }

  if (OB_FAIL(ret)) {
    // do nothing.
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "get task executor context failed");
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    SQL_ENG_LOG(WARN, "get common rpc proxy failed", K(ret));
   } else if (OB_ISNULL(common_rpc_proxy) || OB_ISNULL(ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "fail to get physical plan ctx", K(ret), K(ctx), K(common_rpc_proxy));
   } else if (OB_FAIL(common_rpc_proxy->create_directory(create_directory_arg))) {
    SQL_ENG_LOG(WARN, "rpc proxy create directory failed", K(ret), K(create_directory_arg));
   } else {
    ctx.get_physical_plan_ctx()->set_affected_rows(1);
  }
  SQL_ENG_LOG(INFO, "finish execute create directory.", K(ret), K(stmt));
  return ret;
}

int ObDropDirectoryExecutor::execute(ObExecContext &ctx, ObDropDirectoryStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  const obrpc::ObDropDirectoryArg &drop_directory_arg = stmt.get_drop_directory_arg();
  if (OB_ISNULL(ctx.get_stmt_factory()) || OB_ISNULL(ctx.get_stmt_factory()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "query ctx is null", K(ret));
  } else {
    const_cast<obrpc::ObDropDirectoryArg&>(drop_directory_arg).ddl_stmt_str_ =
                                         ctx.get_stmt_factory()->get_query_ctx()->get_sql_stmt();
  }
  if (OB_FAIL(ret)) {
    // do nothing.
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "get task executor context failed");
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    SQL_ENG_LOG(WARN, "get common rpc proxy failed", K(ret));
   } else if (OB_ISNULL(common_rpc_proxy) || OB_ISNULL(ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "fail to get physical plan ctx", K(ret), K(ctx), K(common_rpc_proxy));
   } else if (OB_FAIL(common_rpc_proxy->drop_directory(drop_directory_arg))) {
    SQL_ENG_LOG(WARN, "rpc proxy drop directory failed", K(ret), K(drop_directory_arg));
   } else {
    ctx.get_physical_plan_ctx()->set_affected_rows(1);
  }
  SQL_ENG_LOG(INFO, "finish execute drop directory.", K(ret), K(stmt));
  return ret;
}
} // namespace sql
} // namespace oceanbase
