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

#define USING_LOG_PREFIX SQL_RESV

#include "ob_call_procedure_resolver.h"
#include "ob_call_procedure_stmt.h"
#include "src/sql/resolver/dml/ob_dml_resolver.h"
#include "pl/ob_pl_package.h"
#include "pl/pl_cache/ob_pl_cache_mgr.h"
#include "pl/ob_pl_dependency_util.h"
namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
int ObCallProcedureResolver::check_param_expr_legal(ObRawExpr *param)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(param)) {
    if (T_REF_QUERY == param->get_expr_type()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "subqueries or stored function calls here");
    } else if (T_FUN_SYS_PL_SEQ_NEXT_VALUE == param->get_expr_type()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "OBE-06576 : not a valid function or procedure name");
    } /* else if (T_OP_GET_PACKAGE_VAR == param->get_expr_type()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "PLS-221: not procedure or not defined!");
    } */
    for (int64_t i = 0; OB_SUCC(ret) && i < param->get_param_count(); ++i) {
      OZ (check_param_expr_legal(param->get_param_expr(i)));
    }
  }
  return ret;
}
int ObCallProcedureResolver::resolve_cparams(const ParseNode *params_node,
                                             const ObRoutineInfo *routine_info,
                                             ObCallProcedureInfo *call_proc_info,
                                             ObIArray<ObRawExpr*> &params,
                                             pl::ObPLDependencyTable &deps)
{
  int ret = OB_SUCCESS;
  bool has_assign_param = false;

  CK (OB_NOT_NULL(routine_info));
  CK (OB_NOT_NULL(call_proc_info));
  // Step 1: Initialize parameter list
  for (int64_t i = 0; OB_SUCC(ret) && i < routine_info->get_param_count(); ++i) {
    OZ (params.push_back(NULL));
  }
  // Step 2: Parse parameters from ParamsNode
  if (OB_SUCC(ret) && OB_NOT_NULL(params_node)) {
    if (T_SP_CPARAM_LIST != params_node->type_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid params list node", K(ret), K(params_node->type_));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < params_node->num_child_; ++i) {
      if (OB_ISNULL(params_node->children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param node is NULL", K(i), K(ret));
      } else if (T_SP_CPARAM == params_node->children_[i]->type_) {
        has_assign_param = true;
        if (OB_FAIL(resolve_cparam_with_assign(params_node->children_[i], routine_info, params, deps))) {
          LOG_WARN("failed to resolve cparam with assign", K(ret));
        }
      } else if (has_assign_param) {
        ret = OB_ERR_SP_WRONG_ARG_NUM;
        LOG_WARN("can not set param without assign after param with assign", K(ret));
      } else if (OB_FAIL(resolve_cparam_without_assign(params_node->children_[i], i, params, deps))) {
        LOG_WARN("failed to resolve cparam without assign", K(ret), K(i));
      }
    }
  }
  // Step 3: process missing parameters, if there are default values fill with default values, otherwise report an error
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    ObConstRawExpr *default_expr = NULL;
    if (OB_ISNULL(params.at(i))) { // missing parameter
      params_.is_default_param_ = true;
      ObRoutineParam *routine_param = routine_info->get_routine_params().at(i);
      CK (OB_NOT_NULL(routine_param));
      if (OB_SUCC(ret) && routine_param->get_default_value().empty()) {
        ret = OB_ERR_SP_WRONG_ARG_NUM;
        LOG_WARN("routine param dese not has default value", K(ret));
      }
      CK (OB_NOT_NULL(params_.expr_factory_));
      OZ (ObRawExprUtils::build_const_int_expr(
        *(params_.expr_factory_), ObIntType, 0, default_expr));
      CK (OB_NOT_NULL(default_expr));
      OZ (default_expr->add_flag(IS_PL_MOCK_DEFAULT_EXPR));
      OX (params.at(i) = default_expr);
    }
  }

  if (OB_SUCC(ret)) { // Determine that all parameters do not have complex expression parameters
    bool v = (false == has_assign_param);
    for (int64_t i = 0; v && OB_SUCC(ret) && i < params.count(); i ++) {
      if (OB_ISNULL(params.at(i))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret));
      } else if (params.at(i)->is_const_raw_expr()) {
        const ObConstRawExpr *const_expr = static_cast<const ObConstRawExpr *>(params.at(i));
        if (T_QUESTIONMARK != const_expr->get_expr_type()) {
          v = false;
        }
      } else {
        v = false;
      }
    } // for end
    call_proc_info->set_can_direct_use_param(v);
  }
  return ret;
}

int ObCallProcedureResolver::resolve_cparam_without_assign(const ParseNode *param_node,
                                                           const int64_t position,
                                                           ObIArray<ObRawExpr*> &params,
                                                           pl::ObPLDependencyTable &deps)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(param_node));
  ObRawExpr *param = NULL;
  if (OB_FAIL(ret)) {
  } else if (position < 0 || position >= params.count()) {
    ret = OB_ERR_SP_WRONG_ARG_NUM;
    LOG_WARN("wrong argument number", K(ret), K(position), K(params.count()));
  } else if (OB_NOT_NULL(params.at(position))) {
    ret = OB_ERR_SP_DUP_PARAM;
    LOG_WARN("dup params", K(ret), K(position));
  } else if (OB_FAIL(pl::ObPLResolver::resolve_raw_expr(*param_node, params_, param, false, nullptr, &deps))) {
    LOG_WARN("failed to resolve const expr", K(ret));
  } else if (OB_ISNULL(param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param expr is null", K(ret), K(param));
  } else if (OB_FAIL(check_param_expr_legal(param))) {
    LOG_WARN("failed to check param expr legal", K(ret), KPC(param));
  } else if (T_OP_ROW == param->get_expr_type() && 1 != param->get_param_count()) {
    ret = OB_ERR_INVALID_COLUMN_NUM;
    LOG_USER_ERROR(OB_ERR_INVALID_COLUMN_NUM, static_cast<int64_t>(1));
    LOG_WARN("op_row input param count is not 1", K(param->get_param_count()), K(ret));
  } else {
    params.at(position) = param;
  }

  return ret;
}

int ObCallProcedureResolver::resolve_cparam_with_assign(const ParseNode *param_node,
                                                        const ObRoutineInfo* routine_info,
                                                        ObIArray<ObRawExpr*> &params,
                                                        pl::ObPLDependencyTable &deps)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(param_node));
  CK (OB_NOT_NULL(routine_info));
  CK (OB_LIKELY(2 == param_node->num_child_));
  CK (OB_NOT_NULL(param_node->children_[0]));
  CK (OB_NOT_NULL(param_node->children_[1]));
  if (OB_SUCC(ret)) {
    const ParseNode *name_node = NULL;
    if (T_OBJ_ACCESS_REF == param_node->children_[0]->type_) {
      CK (OB_LIKELY(2 == param_node->children_[0]->num_child_));
      CK (OB_NOT_NULL(param_node->children_[0]->children_[0]));
      CK (OB_ISNULL(param_node->children_[0]->children_[1]));
      CK (OB_LIKELY(T_IDENT == param_node->children_[0]->children_[0]->type_));
      name_node = param_node->children_[0]->children_[0];
    } else if (T_IDENT == param_node->children_[0]->type_) {
      name_node = param_node->children_[0];
    } else if (T_COLUMN_REF == param_node->children_[0]->type_ &&
               3 == param_node->children_[0]->num_child_ &&
               NULL == param_node->children_[0]->children_[0] &&
               NULL ==  param_node->children_[0]->children_[1] &&
               T_IDENT == param_node->children_[0]->children_[2]->type_) {
      name_node = param_node->children_[0]->children_[2];
    } else {
      ret = OB_ERR_CALL_WRONG_ARG;
      LOG_WARN("PLS-00306: wrong number or types of arguments in call", K(ret));
      LOG_USER_ERROR(OB_ERR_CALL_WRONG_ARG, routine_info->get_routine_name().length(),
                    routine_info->get_routine_name().ptr());
    }

    if (OB_SUCC(ret)) {
      ObString name = ObString(static_cast<int32_t>(name_node->str_len_), name_node->str_value_);
      int64_t position = -1;
      if (OB_FAIL(routine_info->find_param_by_name(name, position))) {
        LOG_WARN("failed to find param name in proc info", K(ret), K(name), K(*routine_info));
      } else if (OB_UNLIKELY(-1 == position)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid postition value", K(ret), K(position));
      } else if (OB_FAIL(resolve_cparam_without_assign(param_node->children_[1], position, params, deps))) {
        LOG_WARN("failed to resolve cparam without assign", K(ret));
      }
    }
  }
  return ret;
}

int ObCallProcedureResolver::resolve_param_exprs(const ParseNode *params_node,
                                                 ObIArray<ObRawExpr*> &expr_params)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(params_node));
  CK (T_SP_CPARAM_LIST == params_node->type_);
  CK (OB_NOT_NULL(params_.session_info_));
  for (int64_t i = 0; OB_SUCC(ret) && i < params_node->num_child_; ++i) {
    ObRawExpr* raw_expr = NULL;
    OZ (pl::ObPLResolver::resolve_raw_expr(*params_node->children_[i], params_, raw_expr));
    CK (OB_NOT_NULL(raw_expr));
    OZ (check_param_expr_legal(raw_expr));
    OZ (expr_params.push_back(raw_expr));
  }
  return ret;
}

int ObCallProcedureResolver::generate_pl_cache_ctx(pl::ObPLCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(schema_checker_), K(session_info_), K(ret));
  } else if (OB_ISNULL(schema_checker_->get_schema_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    pc_ctx.session_info_ = session_info_;
    pc_ctx.schema_guard_ = schema_checker_->get_schema_mgr();
    pc_ctx.cache_params_ = const_cast<ParamStore *>(params_.param_list_);
    pc_ctx.raw_sql_ = params_.cur_sql_;
    pc_ctx.key_.namespace_ = ObLibCacheNameSpace::NS_CALLSTMT;
    pc_ctx.key_.db_id_ = session_info_->get_database_id();
    pc_ctx.key_.sessid_ = 0;
    pc_ctx.key_.key_id_ = OB_INVALID_ID;
    pc_ctx.key_.name_ = params_.cur_sql_;
    (void)ObSQLUtils::md5(pc_ctx.raw_sql_,
                          pc_ctx.sql_id_,
                          (int32_t)sizeof(pc_ctx.sql_id_));
  }
  return ret;
}

int ObCallProcedureResolver::add_call_proc_info(ObCallProcedureInfo *call_info)
{
  int ret = OB_SUCCESS;
  ObPlanCache *plan_cache = NULL;
  pl::ObPLCacheCtx pc_ctx;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(ret));
  } else if (OB_ISNULL(plan_cache = session_info_->get_plan_cache())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(ret));
  } else if (OB_FAIL(generate_pl_cache_ctx(pc_ctx))) {
    LOG_WARN("generate pl cache ctx failed", K(ret));
  } else if (OB_FAIL(pl::ObPLCacheMgr::add_pl_cache(plan_cache, call_info, pc_ctx))) {
    if (OB_SQL_PC_PLAN_DUPLICATE == ret) {
      ret = OB_SUCCESS;
      LOG_DEBUG("this plan has been added by others, need not add again", KPC(call_info));
    } else if (OB_REACH_MEMORY_LIMIT == ret || OB_SQL_PC_PLAN_SIZE_LIMIT == ret) {
      if (REACH_TIME_INTERVAL(1000000)) { //1s, when memory reaches its limit, this log print will be relatively frequent, so it is printed at an interval of 1s
        LOG_DEBUG("can't add plan to plan cache",
                K(ret), K(call_info->get_mem_size()), K(pc_ctx.key_),
                K(plan_cache->get_mem_used()));
      }
      ret = OB_SUCCESS;
    } else if (is_not_supported_err(ret)) {
      ret = OB_SUCCESS;
      LOG_DEBUG("plan cache don't support add this kind of plan now",  KPC(call_info));
    } else {
      if (OB_REACH_MAX_CONCURRENT_NUM != ret && OB_REACH_MAX_CCL_CONCURRENT_NUM != ret) { // If it reaches the rate limit upper limit, then throw out the error code
        ret = OB_SUCCESS; // add plan error, overwrite error code, ensure that failure of plan cache does not affect normal execution path
        LOG_WARN("Failed to add plan to ObPlanCache", K(ret));
      }
    }
  }
  return ret;
}

int ObCallProcedureResolver::find_call_proc_info(ObCallProcedureStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObPlanCache *plan_cache = NULL;
  ObCallProcedureInfo *call_proc_info = NULL;
  pl::ObPLCacheCtx pc_ctx;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(ret));
  } else if (OB_ISNULL(plan_cache = session_info_->get_plan_cache())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(ret));
  } else if (OB_FAIL(generate_pl_cache_ctx(pc_ctx))) {
    LOG_WARN("generate pl cache ctx failed", K(ret));
  } else if (OB_FAIL(pl::ObPLCacheMgr::get_pl_cache(plan_cache, stmt.get_cacheobj_guard(), pc_ctx))) {
      LOG_INFO("get pl function by sql failed, will ignore this error",
              K(ret), K(pc_ctx.key_));
      HANDLE_PL_CACHE_RET_VALUE(ret);
  } else {
    call_proc_info = static_cast<ObCallProcedureInfo*>(stmt.get_cacheobj_guard().get_cache_obj());
    if (OB_NOT_NULL(call_proc_info)) {
      OX (stmt.set_call_proc_info(call_proc_info));
    }
  }
  return ret;
}

int ObCallProcedureResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObCallProcedureStmt *stmt = NULL;
  ParseNode *name_node = parse_tree.children_[0];
  ParseNode *dblink_node = NULL;
  ParseNode *params_node = parse_tree.children_[1];
  ObString db_name;
  ObString package_name;
  ObString sp_name;
  ObString dblink_name;
  ObCallProcedureInfo *call_proc_info = NULL;
  const ObRoutineInfo *proc_info = NULL;
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(schema_checker_), K(session_info_), K(ret));
  } else if (OB_UNLIKELY(T_SP_CALL_STMT != parse_tree.type_ || OB_ISNULL(name_node))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the children of parse tree is NULL", K(parse_tree.type_), K(name_node), K(ret));
  } else if (OB_ISNULL(stmt = create_stmt<ObCallProcedureStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create call stmt failed", K(ret));
  } else if (FALSE_IT(stmt_ = stmt)) {
  } else if (FALSE_IT(stmt->get_cacheobj_guard().init(CALLSTMT_HANDLE))) {
  } else if (params_.is_execute_call_stmt_ && 0 != params_.cur_sql_.length() &&
             OB_FAIL(find_call_proc_info(*stmt))) {
    LOG_WARN("fail to find call stmt", K(ret));
  } else if (NULL != stmt->get_call_proc_info()) {
    // find call procedure info in pl cache.
  } else {
    if (NULL == params_.package_guard_) {
      pl::ObPLPackageGuard *package_guard = NULL;
      OZ (params_.session_info_->get_cur_exec_ctx()->get_package_guard(package_guard));
      CK (OB_NOT_NULL(package_guard));
      OX (params_.package_guard_ = package_guard);
    }
    int64_t compile_start = ObTimeUtility::current_time();
    OZ (ObCacheObjectFactory::alloc(stmt->get_cacheobj_guard(),
                                  ObLibCacheNameSpace::NS_CALLSTMT,
                                  session_info_->get_effective_tenant_id()));
    OX (call_proc_info = static_cast<ObCallProcedureInfo*>(stmt->get_cacheobj_guard().get_cache_obj()));
    CK (OB_NOT_NULL(call_proc_info));
    // Wait for sys package to be loaded if not ready yet
    OZ (ObResolverUtils::wait_for_sys_package_ready(*session_info_));
    // Parsing process name
    if (OB_SUCC(ret)) {
      if (T_SP_ACCESS_NAME != name_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Invalid procedure name node", K(name_node->type_), K(ret));
      } else if (OB_ISNULL(dblink_node)) {
        if (OB_FAIL(ObResolverUtils::resolve_sp_access_name(*schema_checker_,
                                                            session_info_->get_effective_tenant_id(),
                                                            session_info_->get_database_name(),
                                                            *name_node,
                                                            db_name, package_name, sp_name,
                                                            dblink_name))) {
          LOG_WARN("resolve sp name failed", K(ret));
        } else if (db_name.empty() && session_info_->get_database_name().empty()) {
          ret = OB_ERR_NO_DB_SELECTED;
          LOG_WARN("no database selected", K(ret), K(db_name));
        } else {
          if (!db_name.empty()) {
            OZ (call_proc_info->set_db_name(db_name));
          } else {
            OZ (call_proc_info->set_db_name(session_info_->get_database_name()));
          }
          
        }
      }
    }
    ObSEArray<ObRawExpr*, 16> expr_params;
    pl::ObPLDependencyTable deps;
    // Get routine schema info
    if (OB_SUCC(ret)) {
      if (OB_NOT_NULL(params_node)
          && OB_FAIL(resolve_param_exprs(params_node, expr_params))) {
        LOG_WARN("failed to resolve param exprs", K(ret));
      } else if (OB_FAIL(ObResolverUtils::get_routine(*params_.package_guard_,
                                                      params_,
                                                      (*session_info_).get_effective_tenant_id(),
                                                      (*session_info_).get_database_name(),
                                                      db_name,
                                                      package_name,
                                                      sp_name,
                                                      ROUTINE_PROCEDURE_TYPE,
                                                      expr_params,
                                                      proc_info,
                                                      dblink_name,
                                                      &(call_proc_info->get_allocator())))) {
        LOG_WARN("failed to get routine info", K(ret), K(db_name), K(package_name), K(sp_name));
      } else if (OB_ISNULL(proc_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("proc info is null", K(ret), K(db_name), K(package_name), K(sp_name), K(proc_info));
      } else if (proc_info->has_accessible_by_clause()) {
        ret = OB_ERR_MISMATCH_SUBPROGRAM;
        LOG_WARN("PLS-00263: mismatch between string on a subprogram specification and body",
                K(ret), KPC(proc_info));
      }
      if (OB_SUCC(ret) && proc_info->is_udt_routine() && !proc_info->is_udt_static_routine()) {
        ret = OB_ERR_CALL_WRONG_ARG;
        LOG_USER_ERROR(OB_ERR_CALL_WRONG_ARG, proc_info->get_routine_name().length(),
                                              proc_info->get_routine_name().ptr());
      }
      if (OB_SUCC(ret) && proc_info->is_udt_routine()) {
        call_proc_info->set_is_udt_routine(true);
      }
      if (OB_SUCC(ret)) {
        ObSchemaObjVersion obj_version;
        obj_version.object_id_ = proc_info->get_routine_id();
        obj_version.object_type_ = DEPENDENCY_PROCEDURE;
        obj_version.version_ = proc_info->get_schema_version();
        int64_t tenant_id = session_info_->get_effective_tenant_id();
        int64_t tenant_schema_version = OB_INVALID_VERSION;
        int64_t sys_schema_version = OB_INVALID_VERSION;
        CK (OB_NOT_NULL(schema_checker_->get_schema_mgr()));
        OZ (schema_checker_->get_schema_mgr()->get_schema_version(tenant_id, tenant_schema_version));
        OZ (schema_checker_->get_schema_mgr()->get_schema_version(OB_SYS_TENANT_ID, sys_schema_version));
        OX (call_proc_info->set_tenant_schema_version(tenant_schema_version));
        OX (call_proc_info->set_sys_schema_version(sys_schema_version));
        OZ (deps.push_back(obj_version));
      }
    }
    ObSEArray<ObRawExpr*, 16> params;
    OZ (resolve_cparams(params_node, proc_info, call_proc_info, params, deps));
    OZ (call_proc_info->init_dependency_table_store(deps.count()));
    OZ (call_proc_info->get_dependency_table().assign(deps));

    if (OB_SUCC(ret)) {
      if (OB_INVALID_ID == proc_info->get_package_id()) {
        //standalone procedure
        call_proc_info->set_package_id(proc_info->get_package_id());
        call_proc_info->set_routine_id(proc_info->get_routine_id());
      } else {
        //package procedure
        call_proc_info->set_package_id(proc_info->get_package_id());
        call_proc_info->set_routine_id(proc_info->get_subprogram_id());
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < proc_info->get_param_count(); ++i) {
        const ObRoutineParam *param_info = proc_info->get_routine_params().at(i);
        const ObRawExpr *param_expr = params.at(i);
        pl::ObPLDataType pl_type;
        CK (OB_NOT_NULL(param_info));
        CK (OB_NOT_NULL(param_expr));

        if (OB_SUCC(ret)) {
          CK (OB_NOT_NULL(schema_checker_->get_schema_mgr()));
          CK (OB_NOT_NULL(params_.sql_proxy_));
          CK (OB_NOT_NULL(session_info_));
          OX (pl_type.set_enum_set_ctx(&call_proc_info->get_enum_set_ctx()));
          OZ (pl::ObPLDataType::transform_from_iparam(param_info,
                                                      *(schema_checker_->get_schema_mgr()),
                                                      *(session_info_),
                                                      *(params_.allocator_),
                                                      *(params_.sql_proxy_),
                                                      pl_type,
                                                      NULL));
        }
        if (OB_SUCC(ret)) {
          if (param_info->is_out_sp_param() || param_info->is_inout_sp_param()) {
            const ObRawExpr* param = params.at(i);
            if (lib::is_mysql_mode()
                && param->get_expr_type() != T_OP_GET_USER_VAR
                && param->get_expr_type() != T_OP_GET_SYS_VAR
                && !(param->get_expr_type() == T_QUESTIONMARK && params_.is_prepare_protocol_)) {
              ret = OB_ER_SP_NOT_VAR_ARG;
              LOG_USER_ERROR(OB_ER_SP_NOT_VAR_ARG, static_cast<int32_t>(i), static_cast<int32_t>(sp_name.length()), sp_name.ptr());
              LOG_WARN("OUT or INOUT argument for routine is not a variable", K(param->get_expr_type()), K(ret));
            } else if (param->is_obj_access_expr() && !(static_cast<const ObObjAccessRawExpr *>(param))->for_write()) {
              ret = OB_ERR_OUT_PARAM_NOT_BIND_VAR;
              LOG_WARN("output parameter not a bind variable", K(ret));
            } else if (param_info->is_sys_refcursor_type()
                      || (param_info->is_pkg_type() && pl_type.is_cursor_type())) {
              OZ (call_proc_info->add_out_param(i,
                                      param_info->get_mode(),
                                      param_info->get_param_name(),
                                      pl_type,
                                      ObString("SYS_REFCURSOR"),
                                      ObString("")));
            } else if (pl_type.is_user_type()) {
              // Through Call statement to execute PL and the parameter is a complex type, only supported in PS mode, complex data types cannot be constructed by the client;
              // PS mode only supports UDT as output parameter, here we disable complex type output parameters for other modes;
              ret = OB_NOT_SUPPORTED;
              LOG_WARN("not supported other type as out parameter except udt", K(ret), K(pl_type.is_user_type()));
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "other complex type as out parameter except user define type");
            } else {
              // no need to response parameters for client_non_standard when user/sys variable
              bool is_client_out_param =
                  !(lib::is_mysql_mode()
                    && params_.session_info_->client_non_standard()
                    && (param->get_expr_type() == T_OP_GET_USER_VAR
                        || param->get_expr_type() == T_OP_GET_SYS_VAR));
              OZ (call_proc_info->add_out_param(i,
                                                param_info->get_mode(),
                                                param_info->get_param_name(),
                                                pl_type,
                                                param_info->get_type_name(),
                                                ObString(""),
                                                is_client_out_param));
            }
          }
        }
      }
    }
    // Step 4: cg raw expr
    OX (call_proc_info->set_param_cnt(params.count()));
    OZ (call_proc_info->prepare_expression(params));
    OZ (call_proc_info->final_expression(params, session_info_, schema_checker_->get_schema_mgr()));
    OX (stmt->set_call_proc_info(call_proc_info));
    int64_t compile_end = ObTimeUtility::current_time();
    if (params_.is_execute_call_stmt_ 
        && 0 != params_.cur_sql_.length()
        && NULL == stmt->get_dblink_routine_info()) {
      if (NULL != params_.param_list_) {
        OZ (call_proc_info->set_params_info(*params_.param_list_));
      }
      OX (call_proc_info->get_stat_for_update().type_ = pl::ObPLCacheObjectType::CALL_STMT_TYPE);
      OX (call_proc_info->get_stat_for_update().compile_time_ = compile_end - compile_start);
      OX (call_proc_info->get_stat_for_update().raw_sql_ = params_.cur_sql_);
      OX (session_info_->add_plsql_compile_time(compile_end - compile_start));
      OZ (add_call_proc_info(call_proc_info));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < call_proc_info->get_dependency_table().count(); ++i) {
      OZ (stmt->add_global_dependency_table(call_proc_info->get_dependency_table().at(i)));
    }
  }

  return ret;
}

}
}
