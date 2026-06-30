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
#include "sql/engine/cmd/ob_ccl_rule_executor.h"
#include "rootserver/ob_rs_serial_call.h"
#include "rootserver/ob_root_service.h"
#include "sql/engine/cmd/ob_ddl_executor_util.h"
#include "sql/resolver/ddl/ob_create_ccl_rule_stmt.h"
#include "sql/resolver/ddl/ob_drop_ccl_rule_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "observer/ob_ex_rpc.h"
#include "lib/worker.h"
#include "rootserver/ob_root_utils.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "share/schema/ob_ccl_rule_mgr.h"
#include "sql/engine/expr/ob_expr_like.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

static uint64_t ccl_schema_mock_id = 0;
ObCreateCCLRuleExecutor::ObCreateCCLRuleExecutor()
{
}

ObCreateCCLRuleExecutor::~ObCreateCCLRuleExecutor()
{
}

int ObCreateCCLRuleExecutor::execute(ObExecContext &ctx, ObCreateCCLRuleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  const obcall::ObCreateCCLRuleArg &create_ccl_rule_arg = stmt.get_create_ccl_rule_arg();
  obcall::ObCreateCCLRuleArg &tmp_arg = const_cast<obcall::ObCreateCCLRuleArg&>(create_ccl_rule_arg);
  ObString first_stmt;
  obcall::UInt64 database_id(0);
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    tmp_arg.ddl_stmt_str_ = first_stmt;
    tmp_arg.consumer_group_id_ = THIS_WORKER.get_group_id();
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "get task executor context failed");
  } else if (OB_ISNULL(ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "fail to get physical plan ctx", K(ret), K(ctx));
  } else {
    if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_ccl_rule_ddl(create_ccl_rule_arg); }))) {
    }
  }
  SERVER_EVENT_ADD("ddl", "create ccl rule execute finish",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "rpc_dst", GCTX.self_addr(),
    "ccl_rule_id", database_id,
    "schema_version", create_ccl_rule_arg.ccl_rule_schema_.get_schema_version());
  return ret;
}

//////////////////
ObDropCCLRuleExecutor::ObDropCCLRuleExecutor()
{
}

ObDropCCLRuleExecutor::~ObDropCCLRuleExecutor()
{
}

int ObDropCCLRuleExecutor::execute(ObExecContext &ctx, ObDropCCLRuleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  const obcall::ObDropCCLRuleArg &drop_ccl_rule_arg = stmt.get_drop_ccl_rule_arg();
  obcall::ObDropCCLRuleArg &tmp_arg = const_cast<obcall::ObDropCCLRuleArg&>(drop_ccl_rule_arg);
  ObString first_stmt;
  uint64_t database_id = 0;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
  } else {
    tmp_arg.ddl_stmt_str_ = first_stmt;
    tmp_arg.consumer_group_id_ = THIS_WORKER.get_group_id();
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "get task executor context failed");
  } else if (OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "fail to get my session", K(ctx));
  } else {
    if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->drop_ccl_rule_ddl(drop_ccl_rule_arg); }))) {
    }
  }
  SERVER_EVENT_ADD("ddl", "drop ccl rule execute finish",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "rpc_dst", GCTX.self_addr());
  return ret;
}


}
}
