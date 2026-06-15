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
#include "sql/engine/cmd/ob_tablegroup_executor.h"
#include "rootserver/ob_rs_serial_call.h"

#include "sql/resolver/ddl/ob_create_tablegroup_stmt.h"
#include "sql/resolver/ddl/ob_alter_tablegroup_stmt.h"
#include "sql/resolver/ddl/ob_drop_tablegroup_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/cmd/ob_partition_executor_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
int ObCreateTablegroupExecutor::execute(ObExecContext &ctx, ObCreateTablegroupStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obcall::ObCreateTablegroupArg &create_tablegroup_arg = stmt.get_create_tablegroup_arg();

  ObTablegroupSchema &tablegroup_schema = create_tablegroup_arg.tablegroup_schema_;
  tablegroup_schema.set_part_func_expr_num(stmt.get_part_func_expr_num());
  tablegroup_schema.set_sub_part_func_expr_num(stmt.get_sub_part_func_expr_num());

  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    const_cast<obcall::ObCreateTablegroupArg&>(create_tablegroup_arg).ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(ObPartitionExecutorUtils::calc_values_exprs(ctx, stmt))) {
    LOG_WARN("compare range parition expr fail", K(ret));
  } else {
    obcall::UInt64 tablegroup_id(0);
    if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_tablegroup(create_tablegroup_arg, tablegroup_id); }))) {
      LOG_WARN("rpc proxy create tablegroup failed", K(ret));
    }
  }
  LOG_INFO("finish execute create tablegroup.", K(stmt), K(ret));
  return ret;
}

int ObDropTablegroupExecutor::execute(ObExecContext &ctx, ObDropTablegroupStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  const obcall::ObDropTablegroupArg &drop_tablegroup_arg = stmt.get_drop_tablegroup_arg();
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    const_cast<obcall::ObDropTablegroupArg&>(drop_tablegroup_arg).ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->drop_tablegroup(drop_tablegroup_arg); }))) {
    LOG_WARN("rpc proxy drop tablegroup failed", K(ret));
  }
  LOG_INFO("finish execute drop tablegroup.", K(stmt), K(ret));
  return ret;
}

int ObAlterTablegroupExecutor::execute(ObExecContext &ctx, ObAlterTablegroupStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obcall::ObAlterTablegroupArg &alter_tablegroup_arg = stmt.get_alter_tablegroup_arg();
  alter_tablegroup_arg.alter_tablegroup_schema_.set_part_func_expr_num(stmt.get_part_func_expr_num());
  alter_tablegroup_arg.alter_tablegroup_schema_.set_sub_part_func_expr_num(stmt.get_sub_part_func_expr_num());

  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    const_cast<obcall::ObAlterTablegroupArg&>(alter_tablegroup_arg).ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->alter_tablegroup(alter_tablegroup_arg); }))) {
    LOG_WARN("rpc proxy alter table group failed", "dst", GCTX.self_addr(), K(ret), K(alter_tablegroup_arg));
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
