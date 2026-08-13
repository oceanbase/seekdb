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
#include "ob_trigger_executor.h"
#include "query/command/ob_root_service_serialization.h"
#include "query/command/ob_root_command_service.h"
#include "sql/pl/ob_pl_package.h"
#include "sql/resolver/ddl/ob_trigger_resolver.h"

namespace oceanbase
{
using namespace obcall;
using namespace pl;
using namespace common;
using namespace share::schema;

namespace sql
{
int ObCreateTriggerExecutor::execute(ObExecContext &ctx, ObCreateTriggerStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  ObCreateTriggerArg &arg = stmt.get_trigger_arg();
  
  ObString first_stmt;
  obcall::ObCreateTriggerRes res;
  OZ (stmt.get_first_stmt(first_stmt));
  arg.ddl_stmt_str_ = first_stmt;
  OV (OB_NOT_NULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx)), OB_NOT_INIT);
  OZ (query::serialize_root_service_call(
      [&]{ return ctx.root_command_service().create_trigger_with_res(arg, res); }),
      GCTX.self_addr());
  // Here needs to refresh schema, otherwise may not get the latest trigger_info
  OZ (ObSPIService::force_refresh_schema());
  CK (OB_NOT_NULL(ctx.get_sql_ctx()));
  CK (OB_NOT_NULL(ctx.get_sql_ctx()->schema_guard_));
  CK (OB_NOT_NULL(ctx.get_my_session()));
  CK (OB_NOT_NULL(ctx.get_sql_proxy()));
  CK (OB_NOT_NULL(ctx.get_sql_exec_ctx().schema_service_));
  OZ (ctx.get_sql_exec_ctx().schema_service_->
      get_runtime_schema_guard(*ctx.get_sql_ctx()->schema_guard_));
  OZ (analyze_dependencies(*ctx.get_sql_ctx()->schema_guard_,
                           ctx.get_my_session(),
                           *ctx.get_plan_cache(),
                           ctx.get_pl_sql_runtime(),
                           ctx.get_pl_engine(),
                           ctx.get_sql_proxy(),
                           ctx.get_allocator(),
                           arg));
  OZ (ctx.get_sql_ctx()->schema_guard_->reset());
  if (OB_SUCC(ret)) {
    arg.ddl_stmt_str_.reset();
    arg.based_schema_object_infos_.reset();
    OZ (arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(arg.trigger_info_.get_base_object_id(),
                                                  arg.trigger_info_.is_dml_type() ? TABLE_SCHEMA : USER_SCHEMA,
                                                  res.table_schema_version_)));
    OZ (arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(arg.trigger_info_.get_trigger_id(),
                                                                          TRIGGER_SCHEMA,
                                                                          res.trigger_schema_version_)));
    OZ (query::serialize_root_service_call(
        [&]{ return ctx.root_command_service().create_trigger_with_res(arg, res); }),
        GCTX.self_addr());
    if (OB_ERR_PARALLEL_DDL_CONFLICT == ret) {
      LOG_WARN("trigger or base table maybe changed by other session, ignore the error", K(ret), K(res));
      ret = OB_SUCCESS;
    }
  }
  if(arg.with_if_not_exist_ && ret == OB_ERR_TRIGGER_ALREADY_EXIST) {
    const ObString &trigger_name = arg.trigger_info_.get_trigger_name();
    LOG_WARN("trigger with if not exist grammar, ignore the error", K(ret), K(arg.with_if_not_exist_), K(trigger_name));
    LOG_USER_WARN(OB_ERR_TRIGGER_ALREADY_EXIST, trigger_name.length(), trigger_name.ptr());
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObDropTriggerExecutor::execute(ObExecContext &ctx, ObDropTriggerStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  ObDropTriggerArg &arg = stmt.get_trigger_arg();
  ObString first_stmt;
  OZ (stmt.get_first_stmt(first_stmt));
  arg.ddl_stmt_str_ = first_stmt;
  OV (OB_NOT_NULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx)), OB_NOT_INIT);
  OZ (query::serialize_root_service_call(
      [&]{ return ctx.root_command_service().drop_trigger(arg); }),
      GCTX.self_addr());
  return ret;
}

int ObAlterTriggerExecutor::execute(ObExecContext &ctx, ObAlterTriggerStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  ObAlterTriggerArg &arg = stmt.get_trigger_arg();
  ObString first_stmt;
  OZ (stmt.get_first_stmt(first_stmt));
  if (OB_SUCC(ret)) {
    arg.ddl_stmt_str_ = first_stmt;
    OV (OB_NOT_NULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx)), OB_NOT_INIT);
    if (OB_FAIL(ret)) {
    } else {
      OZ (query::serialize_root_service_call(
          [&]{ return ctx.root_command_service().alter_trigger(arg); }),
          GCTX.self_addr());
    }
  }
  return ret;
}

int ObCreateTriggerExecutor::analyze_dependencies(ObSchemaGetterGuard &schema_guard,
                                                  ObSQLSessionInfo *session_info,
                                                  ObPlanCache &plan_cache,
                                                  ObIPLSqlRuntime *pl_sql_runtime,
                                                  pl::ObPL *pl_engine,
                                                  ObMySQLProxy *sql_proxy,
                                                  ObIAllocator &allocator,
                                                  ObCreateTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  
  const ObString &trigger_name = arg.trigger_info_.get_trigger_name();
  const ObString &db_name = arg.trigger_database_;
  const ObTriggerInfo *trigger_info = NULL;
  if (OB_FAIL(schema_guard.get_trigger_info( arg.trigger_info_.get_database_id(),
                                            trigger_name, trigger_info))) {
  } else if (NULL == trigger_info) {
    ret = OB_ERR_TRIGGER_NOT_EXIST;
    LOG_WARN("trigger not exist", K(db_name), K(trigger_name), K(ret));
  } else {
    if (OB_FAIL(ObTriggerResolver::analyze_trigger(schema_guard, session_info, plan_cache,
                                                   pl_sql_runtime, pl_engine, sql_proxy,
                                                   allocator, *trigger_info, db_name, arg.dependency_infos_))) {
    }
    if (OB_FAIL(ret) && ret != OB_ERR_UNEXPECTED) {
        LOG_USER_WARN(OB_ERR_TRIGGER_COMPILE_ERROR, "TRIGGER",
                      db_name.length(), db_name.ptr(),
                      trigger_name.length(), trigger_name.ptr());
        ObPL::insert_error_msg(ret);
        ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret)) {
      arg.trigger_info_.deep_copy(*trigger_info);
      arg.error_info_.collect_error_info(&arg.trigger_info_);
      arg.in_second_stage_ = true;
      arg.with_replace_ = true;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
