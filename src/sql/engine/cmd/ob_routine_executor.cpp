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
#include "ob_routine_executor.h"
#include "sql/resolver/ddl/ob_drop_routine_stmt.h"
#include "sql/resolver/ddl/ob_alter_routine_stmt.h"
#include "sql/resolver/cmd/ob_call_procedure_stmt.h"
#include "sql/resolver/cmd/ob_anonymous_block_stmt.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "pl/ob_pl_package.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "src/pl/pl_cache/ob_pl_cache_mgr.h"
#include "src/pl/ob_pl_compile.h"
#include "src/pl/ob_pl_compile_utils.h"

namespace oceanbase
{
namespace sql
{
int ObCreateRoutineExecutor::execute(ObExecContext &ctx, ObCreateRoutineStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  obrpc::UInt64 table_id;
  obrpc::ObCreateRoutineArg &crt_routine_arg = stmt.get_routine_arg();
  ObString first_stmt;
  uint64_t tenant_id = crt_routine_arg.routine_info_.get_tenant_id();
  uint64_t database_id = crt_routine_arg.routine_info_.get_database_id();
  ObString db_name = crt_routine_arg.db_name_;
  ObString routine_name = crt_routine_arg.routine_info_.get_routine_name();
  ObRoutineType type = crt_routine_arg.routine_info_.get_routine_type();
  obrpc::ObRoutineDDLRes res;
  bool has_error = ERROR_STATUS_HAS_ERROR == crt_routine_arg.error_info_.get_error_status();
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(ctx.get_my_session()->get_effective_tenant_id()));
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    crt_routine_arg.ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(common_rpc_proxy)){
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("common rpc proxy should not be null", K(ret));
  } else if (OB_FAIL(common_rpc_proxy->create_routine_with_res(crt_routine_arg, res))) {
    LOG_WARN("rpc proxy create procedure failed", K(ret), "dst", common_rpc_proxy->get_server());
  }
  if (OB_SUCC(ret)
      && !has_error
      && tenant_config.is_valid()
      && tenant_config->plsql_v2_compatibility) {
    CK (OB_NOT_NULL(ctx.get_sql_ctx()->schema_guard_));
    OZ (ObSPIService::force_refresh_schema(tenant_id, res.store_routine_schema_version_));
    OZ (ctx.get_task_exec_ctx().schema_service_->
      get_tenant_schema_guard(ctx.get_my_session()->get_effective_tenant_id(), *ctx.get_sql_ctx()->schema_guard_));
    OZ (pl::ObPLCompilerUtils::compile(ctx,
                                       tenant_id,
                                       database_id,
                                       routine_name,
                                       pl::ObPLCompilerUtils::get_compile_type(type),
                                       res.store_routine_schema_version_));
  }
  if(crt_routine_arg.with_if_not_exist_ && ret == OB_ERR_SP_ALREADY_EXISTS) {
    LOG_USER_WARN(OB_ERR_SP_ALREADY_EXISTS, "ROUTINE",  crt_routine_arg.routine_info_.get_routine_name().length(), crt_routine_arg.routine_info_.get_routine_name().ptr());
    ret = OB_SUCCESS;
  }
  return ret;
}


int ObCallProcedureExecutor::execute(ObExecContext &ctx, ObCallProcedureStmt &stmt)
{
  int ret = OB_SUCCESS;
  uint64_t package_id = OB_INVALID_ID;
  uint64_t routine_id = OB_INVALID_ID;
  ObCallProcedureInfo *call_proc_info = NULL;
  LOG_DEBUG("call procedure execute", K(stmt));
  if (OB_ISNULL(ctx.get_pl_engine()) || OB_ISNULL(ctx.get_output_row())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pl engine is NULL", K(ctx.get_pl_engine()), K(ret));
  } else if (OB_ISNULL(ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("physical plan ctx is null", K(ret));
  } else if (OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_ISNULL(ctx.get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql context is null", K(ret));
  } else if (OB_ISNULL(ctx.get_stmt_factory()) ||
             OB_ISNULL(ctx.get_stmt_factory()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("query ctx is null", K(ret));
  } else if (OB_FAIL(ob_write_string(ctx.get_allocator(),
                                     ctx.get_my_session()->get_current_query_string(),
                                     ctx.get_stmt_factory()->get_query_ctx()->get_sql_stmt()))) {
    LOG_WARN("fail to set query string");
  } else if (OB_ISNULL(call_proc_info = stmt.get_call_proc_info())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("call procedure info is null", K(ret));
  } else {
    ParamStore params( (ObWrapperAllocator(ctx.get_allocator())) );
    const share::schema::ObRoutineInfo *routine_info = NULL;
    ObArray<bool> null_params;

    if (!call_proc_info->can_direct_use_param()) {
      ObObjParam param;
      const ParamStore &origin_params = ctx.get_physical_plan_ctx()->get_param_store_for_update();
      pl::ExecCtxBak exec_ctx_bak;
      sql::ObPhysicalPlanCtx phy_plan_ctx(ctx.get_allocator());
      phy_plan_ctx.set_timeout_timestamp(ctx.get_physical_plan_ctx()->get_timeout_timestamp());
      exec_ctx_bak.backup(ctx);
      ctx.set_physical_plan_ctx(&phy_plan_ctx);
      if (call_proc_info->get_expr_op_size() > 0)  {
        OZ (ctx.init_expr_op(call_proc_info->get_expr_op_size()));
      }
      OZ (call_proc_info->get_frame_info().pre_alloc_exec_memory(ctx));

      for (int64_t i = 0; OB_SUCC(ret) && i < origin_params.count(); ++i) {
        OZ (phy_plan_ctx.get_param_store_for_update().push_back(origin_params.at(i)));
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < call_proc_info->get_expressions().count(); ++i) {
        const ObSqlExpression *expr = call_proc_info->get_expressions().at(i);
        if (OB_ISNULL(expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("param expr NULL", K(i), K(ret));
        } else {
          param.reset();
          param.ObObj::reset();
          if (OB_FAIL(ObSQLUtils::calc_sql_expression_without_row(ctx, *expr, param))) {
            LOG_WARN("failed to calc exec param expr", K(i), K(*expr), K(ret));
          } else {
            if (expr->get_is_pl_mock_default_expr()) {
              param.set_is_pl_mock_default_param(true);
            }
            if (param.is_pl_extend() && !IS_CONST_TYPE(expr->get_expr_items().at(0).get_item_type())) {
              const ObExprOperator *op = expr->get_expr_items().at(0).get_expr_operator();
              if (OB_ISNULL(op)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unexpected expr operator", K(ret));
              } else {
                param.set_udt_id(op->get_result_type().get_expr_udt_id());
              }
            }
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(params.push_back(param))) {
              LOG_WARN("push back error", K(i), K(*expr), K(ret));
            } else {
              params.at(params.count() - 1).set_param_meta();
              if (call_proc_info->get_output_count() > 0) {
                if (OB_FAIL(null_params.push_back(param.is_null()))) {
                  LOG_WARN("fail to push back", K(ret));
                }
              }
            }
          }
        }
      } // for end

      if (call_proc_info->get_expr_op_size() > 0) {
        ctx.reset_expr_op();
        ctx.get_allocator().free(ctx.get_expr_op_ctx_store());
      }
      exec_ctx_bak.restore(ctx);
    } else {
      LOG_DEBUG("direct use params", K(ret), K(stmt));
      int64_t param_cnt = ctx.get_physical_plan_ctx()->get_param_store().count();
      if (call_proc_info->get_param_cnt() != param_cnt) {
        ret = OB_ERR_SP_WRONG_ARG_NUM;
        LOG_WARN("argument number not equal", K(call_proc_info->get_param_cnt()), K(param_cnt), K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < param_cnt; ++i) {
        LOG_DEBUG("params", "param", ctx.get_physical_plan_ctx()->get_param_store().at(i), K(i));
        ObObjParam param = ctx.get_physical_plan_ctx()->get_param_store().at(i);
        if (OB_FAIL(params.push_back(param))) {
          LOG_WARN("push back error", K(i), K(ret));
        } else if (call_proc_info->get_output_count() > 0) {
          if (OB_FAIL(null_params.push_back(param.is_null()))) {
            LOG_WARN("fail to push back", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      package_id = call_proc_info->get_package_id();
      routine_id = call_proc_info->get_routine_id();
    }
    if (OB_SUCC(ret)) {
      ObArray<int64_t> path;
      ObArray<int64_t> nocopy_params;
      ObObj result;
      int64_t pkg_id = package_id;
      const ObRoutineInfo *dblink_routine_info = NULL;
      uint64_t dblink_id = OB_INVALID_ID;
      if (OB_NOT_NULL(stmt.get_dblink_routine_info())) {
        dblink_routine_info = stmt.get_dblink_routine_info();
        pkg_id = dblink_routine_info->get_package_id();
        routine_id = dblink_routine_info->get_routine_id();
      }
      if (OB_FAIL(ctx.get_pl_engine()->execute(ctx,
                                              ctx.get_allocator(),
                                              pkg_id,
                                              routine_id,
                                              path,
                                              params,
                                              nocopy_params,
                                              result,
                                              NULL,
                                              false,
                                              false,
                                              0,
                                              false,
                                              dblink_id,
                                              dblink_routine_info))) {
        LOG_WARN("failed to execute pl",  K(ret), K(package_id), K(routine_id), K(pkg_id), K(dblink_id));
      }
      if (OB_FAIL(ret)) {
      } else if (call_proc_info->get_output_count() > 0) {
        int64_t client_output_cnt = call_proc_info->get_client_output_count();
        ctx.get_output_row()->count_ = client_output_cnt;
        if (client_output_cnt > 0
            && OB_ISNULL(ctx.get_output_row()->cells_ = static_cast<ObObj *>(
                             ctx.get_allocator().alloc(sizeof(ObObj) * client_output_cnt)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc obj array", K(client_output_cnt), K(ret));
        } else {
          int64_t out_idx = -1;    // index for out params
          int64_t c_out_idx = -1;  // index for out params which would be returned to client
          for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
            ObObj out_value;
            if (call_proc_info->is_out_param(i)) {
              OX (out_idx++);
              if (ob_is_enum_or_set_type(params.at(i).get_type())) {
                common::ObIArray<common::ObString> *type_info = nullptr;
                OZ (call_proc_info->get_out_type().at(out_idx).get_type_info(type_info));
                CK (OB_NOT_NULL(type_info));
                OZ (ObSPIService::cast_enum_set_to_string(ctx, *type_info, params.at(i), out_value));
              } else {
                OX (out_value = params.at(i));
              }

              if (call_proc_info->is_client_out_param_by_param_id(i)) {
                OX (ctx.get_output_row()->cells_[++c_out_idx] = out_value);
              }

              if (OB_FAIL(ret)) {
              } else if (!call_proc_info->can_direct_use_param()) {
                const ObSqlExpression *expr = call_proc_info->get_expressions().at(i);
                ObItemType expr_type = expr->get_expr_items().at(0).get_item_type();
                if (OB_LIKELY(IS_CONST_TYPE(expr_type))) {
                  const ObObj &value = expr->get_expr_items().at(0).get_obj();
                  if (T_QUESTIONMARK == expr_type) {
                    int64_t param_idx = value.get_unknown();
                    ctx.get_physical_plan_ctx()->get_param_store_for_update().at(param_idx) = out_value;
                  } else {
                    /* do nothing */
                  }
                } else if (T_OP_GET_USER_VAR == expr_type) { // Here only user variables may appear
                  ObExprCtx expr_ctx;
                  if (expr->get_expr_items().count() < 2 || T_VARCHAR != expr->get_expr_items().at(1).get_item_type()) {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("Unexpected result expr", K(*expr), K(ret));
                  } else if (OB_FAIL(ObSQLUtils::wrap_expr_ctx(stmt.get_stmt_type(), ctx, ctx.get_allocator(), expr_ctx))) {
                    LOG_WARN("Failed to wrap expr ctx", K(ret));
                  } else {
                    const ObString var_name = expr->get_expr_items().at(1).get_obj().get_string();
                    if (FAILEDx(ObVariableSetExecutor::set_user_variable(out_value, var_name, expr_ctx))) {
                      LOG_WARN("set user variable failed", K(ret));
                    }
                  }
                } else {
                  ret = OB_ERR_OUT_PARAM_NOT_BIND_VAR;
                  LOG_WARN("output parameter not a bind variable", K(ret));
                }
              } else {
                ctx.get_physical_plan_ctx()->get_param_store_for_update().at(i) = out_value;
              }
            }
          }  // for end
        }
      } else { /*do nothing*/ }
      if (OB_FAIL(ret) && call_proc_info->get_output_count() > 0) {
        for (int64_t i = 0; i < null_params.count() && i < params.count(); ++i) {
          if (null_params.at(i) &&
              params.at(i).is_pl_extend() &&
              params.at(i).get_meta().get_extend_type() != pl::PL_REF_CURSOR_TYPE &&
              params.at(i).get_ext() != 0) {
            pl::ObUserDefinedType::destruct_obj(params.at(i), ctx.get_my_session());
          }
        }
      }
    }
    ctx.get_sql_ctx()->cur_stmt_ = &stmt;
  }
  return ret;
}

int ObDropRoutineExecutor::execute(ObExecContext &ctx, ObDropRoutineStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  obrpc::UInt64 table_id;
  obrpc::ObDropRoutineArg &drop_routine_arg = stmt.get_routine_arg();
  ObString first_stmt;
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    drop_routine_arg.ddl_stmt_str_ = first_stmt;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else if (OB_ISNULL(common_rpc_proxy)){
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("common rpc proxy should not be null", K(ret));
  } else if (OB_FAIL(common_rpc_proxy->drop_routine(drop_routine_arg))) {
    LOG_WARN("rpc proxy drop procedure failed", K(ret), "dst", common_rpc_proxy->get_server());
  }
  return ret;
}

int ObAlterRoutineExecutor::execute(ObExecContext &ctx, ObAlterRoutineStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  obrpc::ObCreateRoutineArg &alter_routine_arg = stmt.get_routine_arg();
  uint64_t tenant_id = alter_routine_arg.routine_info_.get_tenant_id();
  uint64_t database_id = alter_routine_arg.routine_info_.get_database_id();
  ObString db_name = alter_routine_arg.db_name_;
  ObString routine_name = alter_routine_arg.routine_info_.get_routine_name();
  ObRoutineType type = alter_routine_arg.routine_info_.get_routine_type();
  bool has_error = ERROR_STATUS_HAS_ERROR == alter_routine_arg.error_info_.get_error_status();
  bool need_create_routine = (lib::is_mysql_mode() && alter_routine_arg.is_need_alter_);
  ObString first_stmt;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(ctx.get_my_session()->get_effective_tenant_id()));
  if (need_create_routine) {
    obrpc::ObRoutineDDLRes res;
    if (OB_ISNULL(ctx.get_pl_engine())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pl engine is null", K(ret));
    } else if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
      LOG_WARN("fail to get first stmt" , K(ret));
    } else {
      alter_routine_arg.ddl_stmt_str_ = first_stmt;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
      ret = OB_NOT_INIT;
      LOG_WARN("get task executor context failed", K(ret));
    } else if (OB_FAIL(task_exec_ctx->get_common_rpc(common_rpc_proxy))) {
      LOG_WARN("get common rpc proxy failed", K(ret));
    } else if (OB_ISNULL(common_rpc_proxy)){
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("common rpc proxy should not be null", K(ret));
    } else if (OB_FAIL(common_rpc_proxy->alter_routine_with_res(alter_routine_arg, res))) {
      LOG_WARN("rpc proxy alter procedure failed", K(ret), "dst", common_rpc_proxy->get_server());
    }
    if (OB_SUCC(ret)
        && !has_error
        && tenant_config.is_valid()
        && tenant_config->plsql_v2_compatibility) {
      CK (OB_NOT_NULL(ctx.get_sql_ctx()->schema_guard_));
      OZ (ObSPIService::force_refresh_schema(tenant_id, res.store_routine_schema_version_));
      OZ (ctx.get_task_exec_ctx().schema_service_->
        get_tenant_schema_guard(ctx.get_my_session()->get_effective_tenant_id(), *ctx.get_sql_ctx()->schema_guard_));
      OZ (pl::ObPLCompilerUtils::compile(ctx,
                                         tenant_id,
                                         database_id,
                                         routine_name,
                                         pl::ObPLCompilerUtils::get_compile_type(type),
                                         res.store_routine_schema_version_));
    }
  } else {
    ObMySQLTransaction trans;
    const ObRoutineInfo *routine_info = nullptr;
    ObSchemaGetterGuard schema_guard;
    int64_t new_schema_version = OB_INVALID_VERSION;
    ObSArray<ObDependencyInfo> &dep_infos =
                      const_cast<ObSArray<ObDependencyInfo> &>(alter_routine_arg.dependency_infos_);
    OZ (ctx.get_task_exec_ctx().schema_service_->
        get_tenant_schema_guard(ctx.get_my_session()->get_effective_tenant_id(), schema_guard));
    OZ(schema_guard.get_routine_info(tenant_id, alter_routine_arg.routine_info_.get_routine_id(), routine_info));
    CK (OB_NOT_NULL(routine_info));
    OZ (trans.start(GCTX.sql_proxy_, tenant_id, true));
    OZ (alter_routine_arg.error_info_.handle_error_info(trans, routine_info));
    OZ (ObDependencyInfo::delete_schema_object_dependency(trans, tenant_id,
                                    routine_info->get_routine_id(),
                                    new_schema_version,
                                    routine_info->get_object_type()));
    OZ (ObDependencyInfo::insert_dependency_infos(trans, dep_infos, tenant_id, 
                              routine_info->get_routine_id(),
                              routine_info->get_schema_version(),
                              routine_info->get_owner_id()));                     
    if (trans.is_started()) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCCESS == ret))) {
        LOG_WARN("trans end failed", K(ret), K(tmp_ret));
        ret = OB_SUCCESS == ret ? tmp_ret : ret;
      }
    }
    if (OB_SUCC(ret)
        && !has_error
        && tenant_config.is_valid()
        && tenant_config->plsql_v2_compatibility) {
      CK (OB_NOT_NULL(ctx.get_sql_ctx()->schema_guard_));
      OZ (ctx.get_task_exec_ctx().schema_service_->
        get_tenant_schema_guard(ctx.get_my_session()->get_effective_tenant_id(), *ctx.get_sql_ctx()->schema_guard_));
      OZ (pl::ObPLCompilerUtils::compile(ctx,
                                         tenant_id,
                                         database_id,
                                         routine_name,
                                         pl::ObPLCompilerUtils::get_compile_type(type),
                                         routine_info->get_schema_version()));
    }
  }
  return ret;
}

int ObAnonymousBlockExecutor::execute(ObExecContext &ctx, ObAnonymousBlockStmt &stmt)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx.get_pl_engine()));
  CK (OB_NOT_NULL(stmt.get_params()));
  CK (OB_NOT_NULL(ctx.get_my_session()));

  if (OB_FAIL(ret)) {
  } else if (stmt.is_prepare_protocol()) {
    ObArray<bool> null_params;
    ObBitSet<OB_DEFAULT_BITSET_SIZE> out_args;
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_params()->count(); ++i) {
      OZ (null_params.push_back(stmt.get_params()->at(i).is_null()));
    }
    OZ (ctx.get_pl_engine()->execute(
      ctx, *stmt.get_params(), stmt.get_stmt_id(), stmt.get_sql(), out_args),
      K(stmt), KPC(stmt.get_params()));
    // Handle the scenario of anonymous block out parameters, if the anonymous block is executed via execute immediate, need to overwrite the modifications of parameters in the anonymous block to param_store of the parent call
    if (OB_SUCC(ret)
        && stmt.get_params()->count() > 0
        && OB_NOT_NULL(ctx.get_my_session()->get_pl_context())) {
      CK (OB_NOT_NULL(ctx.get_physical_plan_ctx()));
      CK (ctx.get_physical_plan_ctx()->get_param_store().count() == stmt.get_params()->count());
      for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_params()->count(); ++i) {
        ctx.get_physical_plan_ctx()->get_param_store_for_update().at(i) = stmt.get_params()->at(i);
      }
    }
    // Process the output parameters of the top-level anonymous block, the output parameters of the top-level anonymous block need to be returned to the client
    if (OB_SUCC(ret)
        && stmt.get_params()->count() > 0
        && OB_ISNULL(ctx.get_my_session()->get_pl_context())
        && !out_args.is_empty()) {
      bool need_push = false;
      ctx.get_output_row()->count_ = out_args.num_members();
      if (OB_ISNULL(ctx.get_output_row()->cells_ =
        static_cast<ObObj *>(ctx.get_allocator().alloc(sizeof(ObObj) * out_args.num_members())))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc obj array", K(ret), K(stmt.get_params()->count()));
      }
      CK (OB_NOT_NULL(ctx.get_field_columns()));

      // prepare of prexecue protocol fill "field_columns" with question mark count.
      // prepare of ps protocol fill "field_columns" with out_args count.
      // if count of "field_columns" not equal to out_args, that is legal, reset "field_columns", and refill it.
      if (OB_SUCC(ret) && ctx.get_field_columns()->count() != out_args.num_members()) {
        CK (ctx.get_field_columns()->count() == stmt.get_params()->count());
        OX (ctx.get_field_columns()->reset());
        OZ (ctx.get_field_columns()->reserve(out_args.num_members()));
        OX (need_push = true);
      }

      int64_t out_idx = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_params()->count(); ++i) {
        if (out_args.has_member(i)) {
          ObField field;
          ObObjParam &value = stmt.get_params()->at(i);
          ctx.get_output_row()->cells_[out_idx] = stmt.get_params()->at(i);
          field.charsetnr_ = CS_TYPE_UTF8MB4_GENERAL_CI;
          field.type_.set_type(value.get_type());
          field.accuracy_ = value.get_accuracy();
          if (value.get_type() != ObExtendType) { // basic data type
            if (ObVarcharType == value.get_type()
                || ObCharType == value.get_type()) {
              if (-1 == field.accuracy_.get_length()) {
                field.length_ = ObCharType == value.get_type()
                  ? OB_MAX_ORACLE_CHAR_LENGTH_BYTE : OB_MAX_ORACLE_VARCHAR_LENGTH;
              } else {
                field.length_ = field.accuracy_.get_length();
              }
            } else {
              OZ (common::ObField::get_field_mb_length(
                field.type_.get_type(), field.accuracy_, common::CS_TYPE_INVALID, field.length_));
            }
            ObCollationType collation = CS_TYPE_INVALID;
            OZ (ObCharset::get_default_collation(value.get_collation_type(), collation));
            OX (field.charsetnr_ = collation);
          } else { // complex data type
            field.length_ = field.accuracy_.get_length();
            if (value.is_ref_cursor_type()) {
              OZ (ob_write_string(ctx.get_allocator(), ObString("SYS_REFCURSOR"), field.type_name_));
            } else {
              ret = OB_NOT_SUPPORTED;
              LOG_WARN("anonymous out parameter type is not anonymous collection",
                       K(ret), K(value));
              LOG_USER_ERROR(OB_NOT_SUPPORTED,
                             "anonymous out parameter type is not anonymous collection");
            }
          }
          if (need_push) {
            OZ (ctx.get_field_columns()->push_back(field));
          } else {
            OX (ctx.get_field_columns()->at(out_idx) = field);
          }
          OX (out_idx ++);
        }
      }
    }
    if (OB_FAIL(ret) && !out_args.is_empty()) {
      for (int64_t i = 0; i < null_params.count() && i < stmt.get_params()->count(); ++i) {
        if (null_params.at(i) &&
            stmt.get_params()->at(i).is_pl_extend() &&
            stmt.get_params()->at(i).get_meta().get_extend_type() != pl::PL_REF_CURSOR_TYPE &&
            stmt.get_params()->at(i).get_ext() != 0) {
          pl::ObUserDefinedType::destruct_obj(stmt.get_params()->at(i), ctx.get_my_session());
        }
      }
    }
  } else {
    CK (OB_NOT_NULL(stmt.get_params()));
    OZ (ctx.get_pl_engine()->execute(ctx, *stmt.get_params(), stmt.get_body()));
  }
  return ret;
}

}
}
