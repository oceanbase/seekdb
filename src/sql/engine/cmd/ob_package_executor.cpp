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
#include "ob_package_executor.h"
#include "rootserver/ob_local_ddl_serial_call.h"
#include "rootserver/ob_local_management_service.h"
#include "sql/resolver/ddl/ob_create_package_stmt.h"
#include "sql/resolver/ddl/ob_drop_package_stmt.h"
#include "pl/ob_pl_resolver.h"

namespace oceanbase
{
namespace sql
{
using namespace common;
using namespace share::schema;

int ObCreatePackageExecutor::execute(ObExecContext &ctx, ObCreatePackageStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  obcall::UInt64 table_id;
  obcall::ObCreatePackageArg &arg = stmt.get_create_package_arg();
  
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    arg.ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return GCTX.local_management_service_->create_package(arg); }))) {
    LOG_WARN("rpc proxy create package failed", K(ret),
             "dst", GCTX.self_addr());
  }
  return ret;
}

int ObDropPackageExecutor::execute(ObExecContext &ctx, ObDropPackageStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  obcall::UInt64 table_id;
  obcall::ObDropPackageArg &arg = stmt.get_drop_package_arg();
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    arg.ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return GCTX.local_management_service_->drop_package(arg); }))) {
    LOG_WARN("rpc proxy drop package failed", K(ret), "dst", GCTX.self_addr());
  }
  return ret;
}

}
}
