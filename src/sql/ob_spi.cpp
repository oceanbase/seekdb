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

#define USING_LOG_PREFIX SQL
#include "ob_spi.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_sql.h"
#include "sql/ob_query_retry_ctrl.h"
#include "query/protocol/ob_client_protocol.h"
#include "sql/plan_cache/ob_param_value_formatter.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_expr_pl_integer_checker.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/pl/ob_pl_package.h"
#include "sql/pl/ob_pl_server_cursor.h"
#include "sql/engine/expr/ob_expr_obj_access.h"
#include "sql/engine/expr/ob_obj_cast_runtime.h"
#include "sql/pl/ob_pl_exception_handling.h"

namespace oceanbase
{
using namespace sqlclient;
using namespace common;
using namespace share;
using namespace pl;
using namespace share::schema;
namespace sql
{
#define SET_SPI_STATUS \
  do { \
    if (OB_ISNULL(ctx->status_)) { \
      ret = OB_SUCCESS == ret ? OB_INVALID_ARGUMENT : ret; \
      LOG_WARN("status in is NULL", K(ctx->status_), K(ret)); \
    } else { \
      *ctx->status_ = ret; \
    } \
  } while (0)

#define MAKE_EXPR_BUFFER(allocator, expr_idx, expr_count, result)              \
  do {                                                                         \
    CK (OB_NOT_NULL(ctx));                                                     \
    CK (OB_NOT_NULL(ctx->func_));                                              \
    if (OB_SUCC(ret) && expr_idx != nullptr && expr_count > 0) {               \
      int64_t alloc_size = expr_count * sizeof(decltype(*result));             \
      result = static_cast<decltype(result)>(allocator.alloc(alloc_size));     \
      if (OB_ISNULL(result)) {                                                 \
        ret = OB_ALLOCATE_MEMORY_FAILED;                                       \
        LOG_WARN("failed to alloc memory for sql expr buffer", K(expr_count)); \
      } else {                                                                 \
        MEMSET(result, 0, alloc_size);                                         \
        for (int64_t i = 0; OB_SUCC(ret) && i < expr_count; ++i) {             \
          if (OB_UNLIKELY(expr_idx[i] == OB_INVALID_ID)) {                     \
            ret = OB_ERR_UNEXPECTED;                                           \
            LOG_WARN("invalid expr idx", K(expr_idx), K(expr_count), K(i));    \
          } else {                                                             \
            CK (OB_LIKELY(0 <= expr_idx[i]                                     \
                 && expr_idx[i] < ctx->func_->get_expressions().count()));     \
            OX (result[i] = ctx->func_->get_expressions().at(expr_idx[i]);)    \
          }                                                                    \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  } while (0)

#define GET_NULLABLE_EXPR_BY_IDX(ctx, expr_idx, result)                        \
  do {                                                                         \
    CK (OB_NOT_NULL(ctx));                                                     \
    CK (OB_NOT_NULL(ctx->func_));                                              \
    if (OB_FAIL(ret)) {                                                        \
    } else if (expr_idx == OB_INVALID_ID) {                                    \
      result = nullptr;                                                        \
    } else {                                                                   \
      CK (OB_LIKELY(0 <= expr_idx &&                                           \
                   expr_idx < ctx->func_->get_expressions().count()));         \
      OX (result = ctx->func_->get_expressions().at(expr_idx);)                \
    }                                                                          \
  } while (0)

int ObSPIService::PLPrepareResult::init(
    sql::ObSQLSessionInfo &session_info,
    query::ObIPlanCacheAccessService &plan_cache_access_service)
{
  int ret = OB_SUCCESS;
  lib::ContextParam param;
  param.set_mem_attr(ObModIds::OB_PL_TEMP,
                    ObCtxIds::DEFAULT_CTX_ID)
    .set_properties(lib::USE_TL_PAGE_OPTIONAL)
    .set_page_size(OB_MALLOC_MIDDLE_BLOCK_SIZE)
    .set_ablock_size(lib::INTACT_MIDDLE_AOBJECT_SIZE);
  if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
    LOG_WARN("create memory entity failed", K(ret));
  } else {
    result_set_ = new (buf_) ObResultSet(
        session_info, mem_context_->get_arena_allocator(),
        plan_cache_access_service);
  }
  return ret;
}

int ObSPIResultSet::init(
    sql::ObSQLSessionInfo &session_info,
    query::ObIPlanCacheAccessService &plan_cache_access_service)
{
  int ret = OB_SUCCESS;
  lib::ContextParam param;
  param.set_mem_attr(ObModIds::OB_RESULT_SET,
                    ObCtxIds::DEFAULT_CTX_ID)
    .set_properties(lib::USE_TL_PAGE_OPTIONAL)
    .set_page_size(OB_MALLOC_MIDDLE_BLOCK_SIZE)
    .set_ablock_size(lib::INTACT_MIDDLE_AOBJECT_SIZE);
  if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
    LOG_WARN("create memory entity failed", K(ret));
  } else {
    plan_cache_access_service_ = &plan_cache_access_service;
    result_set_ = new (buf_) ObResultSet(
        session_info, mem_context_->get_arena_allocator(),
        plan_cache_access_service);
    is_inited_ = true;
  }
  return ret;
}

int ObSPIResultSet::close_result_set()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    WITH_CONTEXT(mem_context_) {
      if (result_set_->get_errcode() != OB_SUCCESS) {
        IGNORE_RETURN result_set_->close(); // result set already failed before close, ignore error code.
      } else {
        ret = result_set_->close();
      }
    }
  } else {
    LOG_DEBUG("result set is not init", K(ret));
  }
  return ret;
}

int ObSPIResultSet::destruct_exec_params(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (NULL != get_result_set()
      && NULL != get_result_set()->get_exec_context().get_physical_plan_ctx()) {
    ParamStore &params = get_result_set()->get_exec_context().get_physical_plan_ctx()->get_param_store_for_update();
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
      OZ (ObUserDefinedType::destruct_obj(params.at(i), &session));
    }
  }
  return ret;
}

int ObSPIResultSet::store_session(ObSQLSessionInfo *session,
                    sql::ObSQLSessionInfo::StmtSavedValue *&value,
                    int64_t &nested_count)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session));
  OZ (alloc_saved_value(value));
  CK (OB_NOT_NULL(value));
  OZ (session->save_session(*value));
  OX (nested_count = session->get_nested_count());
  OX (session->set_query_start_time(ObTimeUtility::current_time()));
  return ret;
}

int ObSPIResultSet::restore_session(ObSQLSessionInfo *session,
                      sql::ObSQLSessionInfo::StmtSavedValue *value,
                      int64_t nested_count)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session));
  CK (OB_NOT_NULL(value));
  OZ (session->restore_session(*value));
  OX (session->set_nested_count(nested_count));
  return ret;
}

int ObSPIResultSet::store_orign_session(ObSQLSessionInfo *session)
{
  return store_session(session, orign_session_value_, orign_nested_count_);
}

int ObSPIResultSet::store_cursor_session(ObSQLSessionInfo *session)
{
  return store_session(session, cursor_session_value_, cursor_nested_count_);
}

int ObSPIResultSet::restore_orign_session(ObSQLSessionInfo *session)
{
  return ObSPIResultSet::restore_session(session, orign_session_value_, orign_nested_count_);
}

int ObSPIResultSet::restore_cursor_session(ObSQLSessionInfo *session)
{
  return restore_session(session, cursor_session_value_, cursor_nested_count_);
}

int ObSPIResultSet::begin_nested_session(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  OZ (alloc_saved_value(nested_session_value_));
  OV (OB_NOT_NULL(nested_session_value_));
  OZ (session.begin_nested_session(*nested_session_value_));
  return ret;
}

int ObSPIResultSet::end_nested_session(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(nested_session_value_));
  OZ (session.end_nested_session(*nested_session_value_));
  return ret;
}

int ObSPIResultSet::alloc_saved_value(sql::ObSQLSessionInfo::StmtSavedValue *&session_value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_value)) {
    void *ptr = NULL;
    if (OB_ISNULL(ptr = allocator_.alloc(sizeof(sql::ObSQLSessionInfo::StmtSavedValue)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for saved session value", K(ret));
    }
    CK (OB_NOT_NULL(session_value = new(ptr)sql::ObSQLSessionInfo::StmtSavedValue()));
  }
  return ret;
}

int ObSPIResultSet::is_set_global_var(ObSQLSessionInfo &session,
                                      const ObString &sql,
                                      bool &has_global_variable,
                                      bool &has_sys_var)
{
  int ret = OB_SUCCESS;
  has_global_variable = false;
  has_sys_var = false;
  ObArenaAllocator allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_SET_VAR), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ParseResult parse_result;
  ParseMode parse_mode = STD_MODE;
  ObParser parser(allocator, session.get_sql_mode(), session.get_charsets4parser());
  if (sql.empty()) {
  } else if (OB_FAIL(parser.parse(sql,
                            parse_result,
                            parse_mode,
                            false/*is_batched_multi_stmt_split_on*/,
                            false/*no_throw_parser_error*/,
                            true))) {
    LOG_WARN("generate syntax tree failed", K(sql), K(ret));
  } else if (OB_NOT_NULL(parse_result.result_tree_) &&
              parse_result.result_tree_->num_child_ > 0 &&
              OB_NOT_NULL(parse_result.result_tree_->children_[0])) {
    ParseNode *set_node = NULL;
    ParseNode *parse_tree = parse_result.result_tree_->children_[0];
    for (int64_t i = 0; OB_SUCC(ret) && !has_global_variable && i < parse_tree->num_child_; ++i) {
      if (OB_ISNULL(set_node = parse_tree->children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("set node is NULL", K(ret));
      } else if (T_VAR_VAL == set_node->type_ &&
                 1 == set_node->value_) { // global var
        has_global_variable = true;
        has_sys_var = true;
      } else if (set_node->num_child_ > 0 && OB_NOT_NULL(set_node->children_[0])) {
        ParseNode *var = set_node->children_[0];
        ObString name;
        if (T_OBJ_ACCESS_REF == var->type_) { // object access reference
          const ParseNode *name_node = NULL;
          if (OB_ISNULL(name_node = var->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected null", K(ret));
          } else {
            name.assign_ptr(name_node->str_value_, static_cast<int32_t>(name_node->str_len_));
            ObBasicSysVar *sys_var = NULL;
            if (OB_FAIL(session.get_sys_variable_by_name(name, sys_var))) {
              if (OB_ERR_SYS_VARIABLE_UNKNOWN == ret) {
              } else {
                LOG_WARN("get sys variable failed", K(ret), K(name));
              }
            } else if (OB_NOT_NULL(sys_var)) {
              has_sys_var = true;
            }
          }
        } else if (T_SYSTEM_VARIABLE == var->type_) {
          name.assign_ptr(var->str_value_, static_cast<int32_t>(var->str_len_));
          has_sys_var = true;
        }
        if (OB_SUCC(ret) && 0 == name.case_compare("ob_query_timeout")) {
          has_global_variable = true;
        }
      }
    }
  }
  return ret;
}

int ObSPIResultSet::check_nested_stmt_legal(ObExecContext &exec_ctx, const ObString &sql, stmt::StmtType stmt_type, bool for_update)
{
  int ret = OB_SUCCESS;
  bool has_global_variable = false;
  stmt::StmtType parent_stmt_type = stmt::T_NONE;
  ObSqlCtx *sql_ctx = exec_ctx.get_sql_ctx();
  ObPLContext *pl_ctx = exec_ctx.get_pl_stack_ctx();
  if (OB_ISNULL(sql_ctx) || OB_ISNULL(pl_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid sql_ctx or pl_ctx", K(ret), K(sql_ctx), K(pl_ctx));
  } else {
    parent_stmt_type = sql_ctx->stmt_type_;
    LOG_DEBUG("check nested stmt legal", K(parent_stmt_type), K(stmt_type));
  }
  if (stmt::T_VARIABLE_SET == stmt_type && OB_NOT_NULL(exec_ctx.get_my_session())) {
    bool has_sys_var = false;
    OZ (is_set_global_var(*exec_ctx.get_my_session(), sql, has_global_variable, has_sys_var));
  }
  if (OB_SUCC(ret) && ObStmt::is_dml_stmt(parent_stmt_type)) {
    //only when parent stmt is dml or select, it can trigger a nested sql
    if (parent_stmt_type == stmt::T_SELECT && ObStmt::is_dml_write_stmt(stmt_type)) {
      /**
       * CREATE FUNCTION func() RETURNS VARCHAR(128)
       *   BEGIN
       *     INSERT INTO t1 VALUES (4);
       *     return 'implicit commit';
       * END
       *
       * select func() from dual;
       * this function is allowed in mysql
       */
      // select func() from dual is allowed in mysql mode
    } else if (ObStmt::is_ddl_stmt(stmt_type, has_global_variable) || ObStmt::is_tcl_stmt(stmt_type)) {
      ret = OB_ER_COMMIT_NOT_ALLOWED_IN_SF_OR_TRG;
      LOG_ERROR("Cannot Perform a DDL Commit or Rollback Inside a Query or DML tips",
               K(ret), K(stmt_type), K(lbt()));
      if (OB_NOT_SUPPORTED == ret) {
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Cannot Perform a DDL Commit or Rollback Inside a Query or DML tips");
      }
    } else if (exec_ctx.get_my_session()->is_in_user_scope() && ObStmt::is_dml_write_stmt(stmt_type)) {
      ret = OB_ERR_CANT_UPDATE_TABLE_IN_CREATE_TABLE_SELECT;
      LOG_WARN("Can't update table while ctas is being created.", K(ret));
    }
  }
  return ret;
}


int ObSPIResultSet::set_cursor_env(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  OZ (store_orign_session(&session));
  OZ (restore_cursor_session(&session));
  OX (need_end_nested_stmt_ = EST_RESTORE_SESSION);
  return ret;
}

int ObSPIResultSet::reset_cursor_env(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  OZ (store_cursor_session(&session));
  OZ (restore_orign_session(&session));
  OX (need_end_nested_stmt_ = EST_NEED_NOT);
  return ret;
}

int ObSPIResultSet::start_cursor_stmt(
  ObPLExecCtx *pl_ctx, stmt::StmtType stmt_type, bool is_for_update)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  CK (OB_NOT_NULL(pl_ctx));
  CK (OB_NOT_NULL(pl_ctx->exec_ctx_));
  CK (OB_NOT_NULL(session = pl_ctx->exec_ctx_->get_my_session()));
  if (OB_SUCC(ret)) {
    OZ (store_orign_session(session));
    OX (need_end_nested_stmt_ = EST_RESTORE_SESSION);
  }
  LOG_TRACE("call spi start cursor stmt",
            K(ret), K(stmt_type),
            K(is_for_update),
            K(need_end_nested_stmt_),
            K(session->get_stmt_type()));
  return ret;
}

void ObSPIResultSet::end_cursor_stmt(ObPLExecCtx *pl_ctx, int &result)
{
  int ret = OB_SUCCESS;
  if (need_end_nested_stmt_ != EST_NEED_NOT) {
    ObSQLSessionInfo *session = NULL;
    CK (OB_NOT_NULL(pl_ctx));
    CK (OB_NOT_NULL(pl_ctx->exec_ctx_));
    CK (OB_NOT_NULL(session = pl_ctx->exec_ctx_->get_my_session()));
    OZ (reset_cursor_env(*session));
    if (OB_SUCC(ret)
        && OB_NOT_NULL(get_result_set())
        && get_out_params().has_out_param()) {
      OZ (ObSPIService::process_function_out_result(
        pl_ctx, *get_result_set(), get_out_params().get_out_params()));
    }
    if (OB_FAIL(ret)) {
      result = OB_SUCCESS == result ? ret : result;
      LOG_WARN("failed to end cursor stmt", K(ret), K(session), K(need_end_nested_stmt_));
    }
    LOG_TRACE("call spi end cursor stmt", K(ret));
  }
  return;
}

int ObSPIResultSet::start_nested_stmt_if_need(ObPLExecCtx *pl_ctx, const ObString &sql, stmt::StmtType stmt_type, bool for_update)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  CK (OB_NOT_NULL(pl_ctx));
  CK (OB_NOT_NULL(pl_ctx->exec_ctx_));
  CK (OB_NOT_NULL(session = pl_ctx->exec_ctx_->get_my_session()));
  // If no nesting occurs, there is no restriction on the type of execution statement here; judgment is only made in the case of nesting
  if (OB_FAIL(ret)) {
    // do nothing ...
  } else if (OB_NOT_NULL(pl_ctx->exec_ctx_->get_pl_stack_ctx())
             && pl_ctx->exec_ctx_->get_pl_stack_ctx()->in_nested_sql_ctrl()) {
    // The top-level nested statement must be a DML statement, and a transaction has been started, at this point the fast_select process is taken
    OZ (check_nested_stmt_legal(*pl_ctx->exec_ctx_, sql, stmt_type, for_update));
    OZ (begin_nested_session(*session));
    OX (session->set_query_start_time(ObTimeUtility::current_time()));
    OX (need_end_nested_stmt_ = EST_END_NESTED_SESSION);
    LOG_TRACE("call start nested stmt if need", K(ret),
              K(stmt_type),
              K(need_end_nested_stmt_),
              K(session->get_stmt_type()));
  }
  return ret;
}

void ObSPIResultSet::end_nested_stmt_if_need(ObPLExecCtx *pl_ctx, int &result)
{
  int ret = OB_SUCCESS;
  if (need_end_nested_stmt_ > EST_NEED_NOT) {
    ObSQLSessionInfo *session = NULL;
    CK (need_end_nested_stmt_ == EST_RESTORE_SESSION ||
          need_end_nested_stmt_ == EST_END_NESTED_SESSION);
    CK (OB_NOT_NULL(pl_ctx));
    CK (OB_NOT_NULL(pl_ctx->exec_ctx_));
    CK (OB_NOT_NULL(session = pl_ctx->exec_ctx_->get_my_session()));
    CK (OB_NOT_NULL(pl_ctx->exec_ctx_->get_my_session()->get_pl_context()));
    OX (pl_ctx->exec_ctx_->get_my_session()->get_pl_context()->set_exception_handler_illegal());
    switch (need_end_nested_stmt_) {
    case EST_RESTORE_SESSION:
      OZ (restore_orign_session(session));
      break;
    case EST_END_NESTED_SESSION:
      OZ (end_nested_session(*session));
      break;
    default:
      break;
    }
    OX (need_end_nested_stmt_ = EST_NEED_NOT);
    if (OB_FAIL(ret)) {
      result = OB_SUCCESS == result ? ret : result;
      LOG_WARN("failed to end nested stmt", K(ret));
    }
    LOG_TRACE("call end nested stmt if need", K(ret));
  }
  return;
}



int ObSPIService::calc_obj_access_expr(ObPLExecCtx *ctx,
                                       const ObSqlExpression &expr,
                                       ObObjParam &result)
{
  int ret = OB_SUCCESS;
  ObIAllocator *expr_alloc = nullptr;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->params_));
  OX (expr_alloc = ctx->get_top_expr_allocator());
  CK (OB_NOT_NULL(expr_alloc));
  if (OB_SUCC(ret)) {
    const ObExprObjAccess *obj_access = NULL;
    ObEvalCtx eval_ctx(*ctx->exec_ctx_, expr_alloc);
    if (1 == expr.get_expr_items().count()) { // No input parameters, calculate directly
      CK (OB_NOT_NULL(obj_access =
          static_cast<const ObExprObjAccess *>(get_first_expr_item(expr).get_expr_operator())));
      OZ(obj_access->calc_result(result, *expr_alloc, NULL, 0, *(ctx->params_), &eval_ctx));
    } else if (2 == expr.get_expr_items().count()
               && T_OBJ_ACCESS_REF == expr.get_expr_items().at(1).get_item_type()) { // There is one argument, and the argument is ObjAccessExpr
      ObObj first_result;
      CK (OB_NOT_NULL(obj_access =
          static_cast<const ObExprObjAccess *>(expr.get_expr_items().at(1).get_expr_operator())));
      OZ(obj_access->calc_result(first_result, *expr_alloc, NULL, 0, *(ctx->params_), &eval_ctx));
      CK (OB_NOT_NULL(obj_access =
          static_cast<const ObExprObjAccess *>(get_first_expr_item(expr).get_expr_operator())));
      OZ(obj_access->calc_result(result, *expr_alloc, &first_result, 1, *(ctx->params_), &eval_ctx));
    } else {  // Other cases
      LOG_DEBUG("calc_obj_access_expr without row", K(expr));
      OZ (ObSQLUtils::calc_sql_expression_without_row(*ctx->exec_ctx_, expr, result, expr_alloc));
    }
    ObExprResType type;
    OZ (get_result_type(*ctx, expr, type));
    OX (result.set_param_meta(type));
  }
  return ret;
}

int ObSPIService::spi_pad_char_or_varchar(ObSQLSessionInfo *session_info,
                                          const ObSqlExpression *expr,
                                          ObIAllocator *allocator,
                                          ObObj *result)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session_info), OB_NOT_NULL(expr), OB_NOT_NULL(allocator), OB_NOT_NULL(result));
  if (OB_SUCC(ret)
      && result->is_character_type()
      && is_pad_char_to_full_length(session_info->get_sql_mode())
      && T_FUN_COLUMN_CONV == get_expression_type(*expr)) { //TODO:@ryan.ly padding
    const ObSqlFixedArray<ObInfixExprItem>& items = expr->get_expr_items();
    CK(T_INT == items.at(3).get_item_type()); // type
    CK(T_INT32 == items.at(1).get_item_type()); // accuracy
    if (OB_SUCC(ret)) {
      const ObObjType type = static_cast<ObObjType>(items.at(1).get_obj().get_int());
      ObAccuracy accuracy;
      accuracy.set_accuracy(items.at(3).get_obj().get_int());
      OZ (spi_pad_char_or_varchar(session_info, type, accuracy, allocator, result));
    }
  }
  return ret;
}

int ObSPIService::spi_pad_char_or_varchar(ObSQLSessionInfo *session_info,
                                          const ObRawExpr *expr,
                                          ObIAllocator *allocator,
                                          ObObj *result)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session_info), OB_NOT_NULL(expr), OB_NOT_NULL(allocator), OB_NOT_NULL(result));
  if (OB_SUCC(ret)
      && result->is_character_type()
      && is_pad_char_to_full_length(session_info->get_sql_mode())
      && T_FUN_COLUMN_CONV == expr->get_expr_type()) { //TODO:@ryan.ly padding
    CK (expr->get_param_count() >= ObExprColumnConv::PARAMS_COUNT_WITHOUT_COLUMN_INFO);
    if (OB_SUCC(ret)) {
      ObAccuracy accuracy;
      int32_t data_type = 0;
      int64_t accuracy_type = 0;
      const ObConstRawExpr *type_expr =
        static_cast<const ObConstRawExpr*>(expr->get_param_expr(0));
      const ObConstRawExpr *accuracy_expr =
        static_cast<const ObConstRawExpr*>(expr->get_param_expr(2));
      CK (OB_NOT_NULL(type_expr) && OB_NOT_NULL(accuracy_expr));
      OX (type_expr->get_value().get_int32(data_type));
      OX (accuracy_expr->get_value().get_int(accuracy_type));
      OX (accuracy.set_accuracy(accuracy_type));
      OZ (spi_pad_char_or_varchar(session_info, static_cast<ObObjType>(data_type),
          accuracy, allocator, result));
    }
  }
  return ret;
}

int ObSPIService::spi_pad_char_or_varchar(ObSQLSessionInfo *session_info,
                                          const ObObjType &type,
                                          const ObAccuracy &accuracy,
                                          ObIAllocator *allocator,
                                          ObObj *result)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session_info), OB_NOT_NULL(allocator), OB_NOT_NULL(result));
  if (OB_SUCC(ret)
      && result->is_character_type()
      && is_pad_char_to_full_length(session_info->get_sql_mode())) {
    if (OB_SUCC(ret)) {
      if (ObCharType == type) {
        int32_t length = accuracy.get_length(); // byte or char length
        int32_t cell_strlen = 0; // byte or char length
        int32_t pad_whitespace_length = 0; // pad whitespace length
        if (OB_FAIL(result->get_char_length(accuracy, cell_strlen))) {
          LOG_WARN("Fail to get char length, ", K(ret));
        } else {
          if (cell_strlen < length) {
            pad_whitespace_length = length - cell_strlen;
          }
          if (pad_whitespace_length > 0) {
            ObString res_string;
            if (OB_FAIL(ObCharset::whitespace_padding(*allocator,
                                                      result->get_collation_type(),
                                                      result->get_string(),
                                                      pad_whitespace_length,
                                                      res_string))) {
              LOG_WARN("whitespace_padding failed", K(ret), K(pad_whitespace_length));
            } else {
              // watch out !!! in order to deep copy an ObObj instance whose type is char or varchar,
              // set_collation_type() should be revoked. But here no need to set collation type
              result->set_string(result->get_type(), res_string);
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObSPIService::spi_pad_binary(ObSQLSessionInfo *session_info,
                                const ObAccuracy &accuracy,
                                ObIAllocator *allocator,
                                ObObj *result)
{
  int ret = OB_SUCCESS;
  const ObObjType &type = result->get_type();
  CK (OB_NOT_NULL(session_info), OB_NOT_NULL(allocator), OB_NOT_NULL(result));
  if (OB_SUCC(ret)
      && result->is_binary()
      && ObCharType == type) {
    int32_t obj_max_length = accuracy.get_length(); 
    int32_t cell_strlen = result->get_val_len(); 
    int32_t pad_zero_length = 0; // pad '\0' length
    if (cell_strlen < obj_max_length) {
      pad_zero_length = obj_max_length - cell_strlen;
      ObString res_string;
      const ObCollationType coll_type = result->get_collation_type();
      const ObString &input = result->get_string();
      char *buf = NULL;
      int32_t buf_len = input.length() + pad_zero_length;
      if (OB_UNLIKELY(pad_zero_length <= 0)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid len", K(ret), K(pad_zero_length));
      } else if (OB_ISNULL(buf = static_cast<char*>(allocator->alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("no memory", K(ret), K(buf_len));
      } else {
        MEMMOVE(buf, input.ptr(), input.length());
        MEMSET(buf + input.length(), '\0', pad_zero_length);
        res_string = ObString(buf_len, buf_len, buf);
      }
      result->set_string(result->get_type(), res_string);
      }
  }
  return ret;
}

int ObSPIService::spi_cast_enum_set_to_string(pl::ObPLExecCtx *ctx,
                                              uint64_t type_info_id,
                                              ObObj &src,
                                              ObObj &result)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObRawExprFactory *expr_factory = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObConstRawExpr *c_expr = NULL;
  ObSysFunRawExpr *out_expr = NULL;
  ObExprResType result_type;
  ObIArray<common::ObString>* type_info = NULL;
  uint16_t subschema_id = 0;
  ObObj obj = src;
  CK (OB_INVALID_ID != type_info_id);
  CK (OB_NOT_NULL(ctx));
  OX (exec_ctx = ctx->exec_ctx_);
  CK (OB_NOT_NULL(ctx->func_));
  OZ (ctx->func_->get_enum_set_ctx().get_enum_type_info(type_info_id, type_info));
  CK (OB_NOT_NULL(type_info));
  CK (OB_NOT_NULL(exec_ctx));
  OX (expr_factory = exec_ctx->get_expr_factory());
  OX (session_info = exec_ctx->get_my_session());
  CK (OB_NOT_NULL(expr_factory));
  CK (OB_NOT_NULL(session_info));
  OZ (expr_factory->create_raw_expr(static_cast<ObItemType>(src.get_type()), c_expr));
  CK (OB_NOT_NULL(c_expr));
  OZ (ObRawExprUtils::get_subschema_id(src.get_meta(), *type_info, *session_info, subschema_id));
  OX (obj.set_subschema_id(subschema_id));
  OX (c_expr->set_value(obj));
  OX (result_type.reset());
  OX (result_type.set_meta(obj.get_meta()));
  OX (result_type.mark_pl_enum_set_with_subschema());
  OX (c_expr->set_result_type(result_type));
  OZ (ObRawExprUtils::create_type_to_str_expr(*expr_factory, c_expr, out_expr, session_info, true));
  CK (OB_NOT_NULL(out_expr));
  OZ (ObSPIService::spi_calc_raw_expr(session_info, &(exec_ctx->get_allocator()), out_expr, &result));
  return ret;
}

int ObSPIService::cast_enum_set_to_string(ObExecContext &ctx,
                                          const ObIArray<ObString> &enum_set_values,
                                          ObObjParam &src,
                                          ObObj &result)
{
  int ret = OB_SUCCESS;
  ObRawExprFactory *expr_factory = ctx.get_expr_factory();
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObConstRawExpr *c_expr = NULL;
  ObSysFunRawExpr *out_expr = NULL;
  ObExprResType result_type;
  uint16_t subschema_id = 0;
  ObObj obj = src;
  CK (OB_NOT_NULL(expr_factory));
  CK (OB_NOT_NULL(session_info));
  OZ (expr_factory->create_raw_expr(static_cast<ObItemType>(src.get_type()), c_expr));
  CK (OB_NOT_NULL(c_expr));
  OZ (ObRawExprUtils::get_subschema_id(src.get_meta(), enum_set_values, *session_info, subschema_id));
  OX (obj.set_subschema_id(subschema_id));
  OX (c_expr->set_value(obj));
  OX (result_type.set_meta(obj.get_meta()));
  OX (result_type.set_accuracy(src.get_accuracy()));
  OX (result_type.mark_pl_enum_set_with_subschema());
  OX (c_expr->set_result_type(result_type));
  OZ (ObRawExprUtils::create_type_to_str_expr(*expr_factory, c_expr, out_expr, session_info, true));
  CK (OB_NOT_NULL(out_expr));
  OZ (ObSPIService::spi_calc_raw_expr(session_info, &(ctx.get_allocator()), out_expr, &result));
  return ret;
}

int ObSPIService::spi_calc_raw_expr(ObSQLSessionInfo *session,
                                    ObIAllocator *allocator,
                                    const ObRawExpr *expr,
                                    ObObj *result)
{
  int ret = OB_SUCCESS;
  ParamStore param_store((ObWrapperAllocator(*allocator)));
  CK (OB_NOT_NULL(session));
  CK (OB_NOT_NULL(result));
  OZ (ObSQLUtils::se_calc_const_expr(session, expr, param_store, *allocator, session->get_cur_exec_ctx(), *result));
  OX (result->set_collation_level(expr->get_result_type().get_collation_level()));
  if (OB_SUCC(ret) && expr->is_enum_set_with_subschema()) {
    ObObjMeta org_obj_meta;
    OZ (ObRawExprUtils::extract_enum_set_collation(expr->get_result_type(), session, org_obj_meta));
    OX (result->set_collation(org_obj_meta));
  }
  OZ (spi_pad_char_or_varchar(session, expr, allocator, result));
  return ret;
}

int ObSPIService::spi_convert(ObSQLSessionInfo &session,
                              ObIAllocator &allocator,
                              ObObj &src,
                              const ObExprResType &dst_type,
                              ObObj &dst,
                              common::ObISrsProvider *srs_provider,
                              common::ObILobReadService *lob_read_service,
                              bool ignore_fail,
                              const ObIArray<ObString> *type_info)
{
  int ret = OB_SUCCESS;
  ObCastMode cast_mode = CM_NONE;
  OZ (ObSQLUtils::get_default_cast_mode(stmt::T_NONE, &session, cast_mode));
  if (OB_SUCC(ret)) {
    if (ignore_fail) {
      cast_mode |= CM_WARN_ON_FAIL;
    }
    bool is_strict = is_strict_mode(session.get_sql_mode());
    const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(&session);
    ObCastCtx cast_ctx(&allocator, &dtc_params, cast_mode, dst_type.get_collation_type());
    session.configure_obj_cast(cast_ctx, srs_provider, lob_read_service);
    if (dst_type.is_null() || dst_type.is_unknown() || dst_type.is_ext()) {
      OX (dst = src);
    } else if (ob_is_enum_or_set_type(src.get_type()) && ob_is_enum_or_set_type(dst_type.get_type())) {
      OX (dst = src);
    } else {
      OZ (ObExprColumnConv::convert_with_null_check(dst, src, dst_type, is_strict, cast_ctx, type_info));
    }
  }
  return ret;
}

int ObSPIService::spi_convert(ObSQLSessionInfo *session,
                              ObIAllocator *allocator,
                              ObObjParam &src,
                              const ObExprResType &result_type,
                              ObObjParam &result,
                              common::ObISrsProvider *srs_provider,
                              common::ObILobReadService *lob_read_service,
                              const ObIArray<ObString> *type_info)
{
  int ret = OB_SUCCESS;
  ObObj dst;
  CK (OB_NOT_NULL(session));
  CK (OB_NOT_NULL(allocator));
  OZ (spi_convert(*session, *allocator, src, result_type, dst,
                  srs_provider, lob_read_service, false, type_info));
  OX (src.set_param_meta());
  OX (result.set_accuracy(result_type.get_accuracy()));
  OX (result.set_meta_type(result_type.get_obj_meta()));
  OX (result = dst);
  OX (result.set_param_meta());
  return ret;
}

int ObSPIService::spi_convert_objparam(ObPLExecCtx *ctx,
                                       ObObjParam *src,
                                       const int64_t result_idx,
                                       ObObjParam *result,
                                       bool need_set)
{
  int ret = OB_SUCCESS;
  const ObPLDataType *expected_type;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->func_));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->params_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(ctx->allocator_));
  CK (OB_NOT_NULL(src));
  CK (ctx->params_->count() == ctx->func_->get_variables().count());
  CK (result_idx < ctx->params_->count() && result_idx >= 0);
  CK (OB_NOT_NULL(expected_type = &(ctx->func_->get_variables().at(result_idx))));
  CK (OB_NOT_NULL(expected_type->get_data_type()));
  if (OB_SUCC(ret)) {
    ObExprResType result_type;
    ObObjParam result_value;
    result_type.reset();
    result_type.set_meta(expected_type->get_data_type()->get_meta_type());
    result_type.set_accuracy(expected_type->get_data_type()->get_accuracy());
    if (result_type.is_null()) {
      ObObjParam value;
      CK (OB_LIKELY(STANDALONE_ANONYMOUS == ctx->func_->get_proc_type()));
      OX (value = *src);
      OX (value.set_int(0));
      OZ (deep_copy_obj(*ctx->allocator_, *src, value));
      OX (result_value = value);
      if (OB_SUCC(ret) && need_set) {
        void *ptr = ctx->params_->at(result_idx).get_deep_copy_obj_ptr();
        if (nullptr != ptr) {
          ctx->allocator_->free(ptr);
        }
        OX (ctx->params_->at(result_idx) = result_value);
      }
    } else {
      ObObjParam value;
      ObIArray<common::ObString> *type_info = NULL;
      OZ (expected_type->get_type_info(type_info));
      OZ (spi_convert(ctx->exec_ctx_->get_my_session(), &tmp_alloc, *src,
                      result_type, value,
                      ctx->exec_ctx_->get_srs_provider(),
                      ctx->exec_ctx_->get_lob_read_service(),
                      type_info));
      OZ (deep_copy_obj(*ctx->allocator_, value, result_value));
      if (OB_SUCC(ret) && need_set) {
        void *ptr = ctx->params_->at(result_idx).get_deep_copy_obj_ptr();
        if (nullptr != ptr) {
          ctx->allocator_->free(ptr);
        }
        OX (ctx->params_->at(result_idx).apply(result_value));
        OX (ctx->params_->at(result_idx).set_param_meta());
      }
    }
    if (OB_NOT_NULL(result)) {
      OX (*result = result_value);
    }
  }
  return ret;
}

int ObSPIService::spi_calc_expr_at_idx(pl::ObPLExecCtx *ctx,
                                       const int64_t expr_idx,
                                       const int64_t result_idx,
                                       ObObjParam *result)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->func_));
  CK (expr_idx != OB_INVALID_ID);
  CK (OB_LIKELY(0 <= expr_idx && expr_idx < ctx->func_->get_expressions().count()));
  const ObSqlExpression *expr = nullptr;
  OX (expr = ctx->func_->get_expressions().at(expr_idx));
  OZ (SMART_CALL(spi_calc_expr(ctx, expr, result_idx, result)));
  return ret;
}

int ObSPIService::spi_calc_expr(ObPLExecCtx *ctx,
                                const ObSqlExpression *expr,
                                const int64_t result_idx,
                                ObObjParam *result)
{
  int ret = OB_SUCCESS;
  ObIAllocator *expr_allocator = nullptr;
  CK (OB_NOT_NULL(ctx), OB_NOT_NULL(expr), OB_NOT_NULL(result));
  CK (OB_NOT_NULL(ctx->exec_ctx_), OB_NOT_NULL(ctx->params_), OB_NOT_NULL(ctx->allocator_), OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  OX (expr_allocator = ctx->get_top_expr_allocator());
  CK (OB_NOT_NULL(expr_allocator));
  if (OB_SUCC(ret)) {
    ObItemType expr_type = expr->get_expr_items().at(0).get_item_type();
    if (IS_CONST_TYPE(expr_type)) {
      bool need_check = false;
      const ObObj &value = expr->get_expr_items().at(0).get_obj();
      if (T_QUESTIONMARK == expr_type) {
        OZ (ObSQLUtils::get_param_value<ObObjParam>(value, *ctx->params_, *result, need_check));
      } else {
        OZ (deep_copy_obj(*expr_allocator, value, *result));
        result->set_param_meta();
      }
    } else if (T_OBJ_ACCESS_REF == expr_type) {
      OZ (calc_obj_access_expr(ctx, *expr, *result));
    } else {
      LOG_DEBUG("spi_calc_expr without row", K(*expr));
      ObExprResType result_type;
      bool has_implicit_savepoint = false;
      bool explicit_trans = ctx->exec_ctx_->get_my_session()->has_explicit_start_trans();
      ObPLContext *pl_ctx = ctx->exec_ctx_->get_pl_stack_ctx();
      CK (OB_NOT_NULL(pl_ctx));
      if (OB_SUCC(ret) && !pl_ctx->is_function_or_trigger()) {
        if (ctx->exec_ctx_->get_my_session()->is_in_transaction()) {
          OZ (ObSqlTransControl::create_savepoint(*ctx->exec_ctx_, PL_INNER_EXPR_SAVEPOINT));
          OX (has_implicit_savepoint = true);
        }
      }
      if (OB_SUCC(ret)) {
        OZ (ObSQLUtils::calc_sql_expression_without_row(*ctx->exec_ctx_, *expr, *result, expr_allocator),
          KPC(expr), K(result_idx));
        OX(result->set_param_meta());

        OZ (get_result_type(*ctx, *expr, result_type));
        OX (result->set_param_meta(result_type));
        /* If this layer is udf, the expression being calculated in this session contains udf;
          If the inner udf fails, it will be rolled back by the internal mechanism of the udf; if the inner udf succeeds, but issues such as strong conversion failure occur, no rollback will be performed here,
          The rollback will be ensured by the destroy interface of this layer's udf, compatible with mysql */
        if (!pl_ctx->is_function_or_trigger()) {
          if (OB_SUCCESS != ret && ctx->exec_ctx_->get_my_session()->is_in_transaction()) {
            int tmp_ret = OB_SUCCESS;
            if (has_implicit_savepoint) {
              if (OB_SUCCESS !=
                  (tmp_ret = ObSqlTransControl::rollback_savepoint(*ctx->exec_ctx_, PL_INNER_EXPR_SAVEPOINT))) {
                LOG_WARN("failed to rollback current pl to implicit savepoint", K(ret), K(tmp_ret));
              }
            } else if (ctx->exec_ctx_->get_my_session()->get_in_transaction()) {
              tmp_ret = ObPLContext::implicit_end_trans(*ctx->exec_ctx_->get_my_session(), *ctx->exec_ctx_, true);
            }
            ret = OB_SUCCESS == ret ? tmp_ret : ret;
          } else if (ctx->exec_ctx_->get_my_session()->get_local_autocommit() && !explicit_trans) {
            int tmp_ret = OB_SUCCESS;
            if (OB_SUCCESS == ret) {
              if (OB_SUCCESS != (tmp_ret = ObPLContext::implicit_end_trans(*ctx->exec_ctx_->get_my_session(), *ctx->exec_ctx_, false, true))) {
                // Do not overwrite the original error code
                LOG_WARN("failed to explicit end trans", K(ret), K(tmp_ret));
              }
            }
            ret = OB_SUCCESS == ret ? tmp_ret : ret;
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)
      && result->is_binary()
      && result->get_val_len() < result->get_accuracy().get_length()) {
      OZ (spi_pad_binary(ctx->exec_ctx_->get_my_session(), 
        result->get_accuracy(), expr_allocator, result));
  }

  if (OB_SUCC(ret)
      && result->is_character_type()) {
    ObObjType type = result->get_type();
    if (T_QUESTIONMARK == get_expression_type(*expr) &&
        ObCharType == type) {
      if (is_pad_char_to_full_length(ctx->exec_ctx_->get_my_session()->get_sql_mode())) {
        OZ (spi_pad_char_or_varchar(
          ctx->exec_ctx_->get_my_session(), type, result->get_accuracy(), expr_allocator, result));
      } else {
        ObString res = result->get_string();
        OZ (ObCharsetUtils::remove_char_endspace(  // this function only adjust res.data_length_
            res, ObCharset::get_charset(result->get_collation_type())));
        OX (result->val_len_ = res.length());
      }
    } else {
      OZ (spi_pad_char_or_varchar(
        ctx->exec_ctx_->get_my_session(), expr, expr_allocator, result));
    }
  }

  if (OB_SUCC(ret)) {
    if (result->is_null()) {
      result->v_.int64_ = 0;
    }
    if (OB_INVALID_INDEX != result_idx) {
      // local basic var need to be deep copied once using stack allocator
      ObObjParam &param = ctx->params_->at(result_idx);
      ObAccuracy invalid_accuracy;
      if (!result->is_ext()) {
        ObObjParam tmp;
        OZ (deep_copy_obj(*ctx->allocator_, *result, tmp));
        OX (*result = tmp);
        if (OB_SUCC(ret)) {
          bool has_lob_header = result->ObObj::has_lob_header();
          if (param.get_meta().get_scale() != SCALE_UNKNOWN_YET) {
            result->ObObj::set_scale(param.get_meta().get_scale());
            result->set_accuracy(ctx->params_->at(result_idx).get_accuracy());
          } else if (result->get_accuracy() == invalid_accuracy) {
            result->set_accuracy(ctx->params_->at(result_idx).get_accuracy());
          }
          if (result->is_null()) {
            result->set_param_meta(param.get_param_meta());
          }
          if (has_lob_header) {
            result->ObObj::set_has_lob_header();
          }
          if (OB_SUCC(ret)) {
            void *ptr = param.get_deep_copy_obj_ptr();
            if (nullptr != ptr) {
              ctx->allocator_->free(ptr);
            }
            param = *result;
            param.set_param_meta();
          }
        }
      } else {
        int64_t orig_udt_id = ctx->params_->at(result_idx).get_udt_id();
        ObObj tmp;
        OZ (ObUserDefinedType::deep_copy_obj(*ctx->allocator_, *result, tmp));
        if (0 != ctx->params_->at(result_idx).get_ext()) {
          OZ (ObUserDefinedType::destruct_objparam(*ctx->allocator_,
                                                  ctx->params_->at(result_idx),
                                                  ctx->exec_ctx_->get_my_session()));
        }
        OX (ctx->params_->at(result_idx) = tmp);
        OX (ctx->params_->at(result_idx).set_udt_id(orig_udt_id));
        OX (ctx->params_->at(result_idx).set_param_meta());
      }
    }
    result->set_param_meta();
  }
  if (OB_SUCC(ret)) {
    result->set_is_pl_mock_default_param(expr->get_is_pl_mock_default_expr());
  } else {
    ctx->exec_ctx_->get_my_session()->set_show_warnings_buf(ret);
  }
  SET_SPI_STATUS;
  return ret;
}

int ObSPIService::spi_calc_subprogram_expr(ObPLExecCtx *ctx,
                                           uint64_t package_id,
                                           uint64_t routine_id,
                                           int64_t expr_idx,
                                           ObObjParam *result)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObPLExecState *state = NULL;
  ObSqlExpression *expr = NULL;
  CK (OB_NOT_NULL(exec_ctx = ctx->exec_ctx_));
  CK (OB_NOT_NULL(session_info = exec_ctx->get_my_session())); 
  OZ (ObPLContext::get_exec_state_from_local(*session_info, package_id, routine_id, state));
  CK (OB_NOT_NULL(state));
  CK (OB_NOT_NULL(exec_ctx = state->get_exec_ctx().exec_ctx_));
  CK (expr_idx >= 0 && expr_idx < state->get_function().get_expressions().count());
  CK (OB_NOT_NULL(expr = state->get_function().get_expressions().at(expr_idx)));
  if (OB_SUCC(ret)) {
    ExecCtxBak exec_ctx_bak;
    OX (exec_ctx_bak.backup(*exec_ctx));
    OX (state->get_exec_ctx_bak().restore(*exec_ctx));
    OZ (spi_calc_expr(&(state->get_exec_ctx()), expr, OB_INVALID_ID, result), KPC(expr));
    exec_ctx_bak.restore(*exec_ctx);
  }
  return ret;
}

int ObSPIService::spi_calc_package_expr_v1(const pl::ObPLResolveCtx &resolve_ctx,
                                           sql::ObExecContext &exec_ctx,
                                           ObIAllocator &allocator,
                                           uint64_t package_id,
                                           int64_t expr_idx,
                                           ObObjParam *result)
{
  int ret = OB_SUCCESS;
  ObSqlExpression *sql_expr = NULL;
  ObPLPackageManager *pl_manager = NULL;
  CK (OB_NOT_NULL(resolve_ctx.params_.pl_engine_));
  OX (pl_manager = &resolve_ctx.params_.pl_engine_->get_package_manager());
  ObCacheObjGuard *cache_obj_guard = NULL;
  ObPLPackage *package = NULL;
   ObPLPackageGuard &guard = resolve_ctx.package_guard_;
  OZ (pl_manager->get_package_expr(resolve_ctx, package_id, expr_idx, sql_expr));
  CK (OB_NOT_NULL(sql_expr));
  OZ (guard.get(package_id, cache_obj_guard));
  CK (OB_NOT_NULL(cache_obj_guard));
  CK (OB_NOT_NULL(package = static_cast<ObPLPackage*>(cache_obj_guard->get_cache_obj())));
  if (OB_SUCC(ret)) {
    ExecCtxBak exec_ctx_bak;
    sql::ObPhysicalPlanCtx phy_plan_ctx(exec_ctx.get_allocator());
    OX (exec_ctx_bak.backup(exec_ctx));
    OX (exec_ctx.set_physical_plan_ctx(&phy_plan_ctx));
    if (OB_SUCC(ret) && package->get_expr_op_size() > 0)  {
      OZ (exec_ctx.init_expr_op(package->get_expr_op_size()));
    }
    OZ (package->get_frame_info().pre_alloc_exec_memory(exec_ctx));
    OZ (ObSQLUtils::calc_sql_expression_without_row(exec_ctx, *sql_expr, *result, &allocator));
    if (package->get_expr_op_size() > 0) {
      exec_ctx.reset_expr_op();
      exec_ctx.get_allocator().free(exec_ctx.get_expr_op_ctx_store());
    }
    exec_ctx_bak.restore(exec_ctx);
  }
  return ret;
}

int ObSPIService::spi_calc_package_expr(ObPLExecCtx *ctx,
                                        uint64_t package_id, 
                                        int64_t expr_idx,
                                        ObObjParam *result)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObMySQLProxy *sql_proxy = NULL;
  ObPL *pl_engine = NULL;
  share::schema::ObSchemaGetterGuard schema_guard;
  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(GCTX.schema_service_));
  CK (OB_NOT_NULL(exec_ctx = ctx->exec_ctx_));
  CK (OB_NOT_NULL(session_info = exec_ctx->get_my_session()));
  CK (OB_NOT_NULL(sql_proxy = exec_ctx->get_sql_proxy()));
  CK (OB_NOT_NULL(pl_engine = exec_ctx->get_pl_engine()));
  OZ (GCTX.schema_service_->get_runtime_schema_guard(
                            schema_guard));
  if (OB_SUCC(ret)) {
    ObPLPackageGuard package_guard{};
    ObSqlExpression *sql_expr = NULL;
    ObPLPackageManager &pl_manager = pl_engine->get_package_manager();
    ObPLPackageGuard &guard = ctx->guard_ != NULL ? (*ctx->guard_) : package_guard;
    ObCacheObjGuard *cache_obj_guard = NULL;
    ObPLPackage *package = NULL;
    ObPLResolveCtx resolve_ctx(exec_ctx->get_allocator(),
                               *session_info,
                               schema_guard,
                               guard,
                               *sql_proxy,
                               false);
    resolve_ctx.params_.plan_cache_ = exec_ctx->get_plan_cache();
    resolve_ctx.params_.pl_sql_runtime_ = exec_ctx->get_pl_sql_runtime();
    resolve_ctx.params_.pl_engine_ = exec_ctx->get_pl_engine();
    resolve_ctx.params_.srs_provider_ = exec_ctx->get_srs_provider();
    resolve_ctx.params_.lob_read_service_ = exec_ctx->get_lob_read_service();
    OZ (package_guard.init());
    OZ (pl_manager.get_package_expr(resolve_ctx, package_id, expr_idx, sql_expr));
    CK (OB_NOT_NULL(sql_expr));
    OZ (guard.get(package_id, cache_obj_guard));
    CK (OB_NOT_NULL(cache_obj_guard));
    CK (OB_NOT_NULL(package = static_cast<ObPLPackage*>(cache_obj_guard->get_cache_obj())));
    if (OB_SUCC(ret)) {
      ExecCtxBak exec_ctx_bak;
      sql::ObPhysicalPlanCtx phy_plan_ctx(exec_ctx->get_allocator());
      OX (exec_ctx_bak.backup(*exec_ctx));
      OX (exec_ctx->set_physical_plan_ctx(&phy_plan_ctx));
      if (OB_SUCC(ret) && package->get_expr_op_size() > 0)  {
        OZ (exec_ctx->init_expr_op(package->get_expr_op_size()));
      }
      OZ (package->get_frame_info().pre_alloc_exec_memory(*exec_ctx));
      OZ (spi_calc_expr(ctx, sql_expr, OB_INVALID_ID, result));
      if (package->get_expr_op_size() > 0) {
        exec_ctx->reset_expr_op();
        exec_ctx->get_allocator().free(exec_ctx->get_expr_op_ctx_store());
      }
      exec_ctx_bak.restore(*exec_ctx);
    }
  }
  return ret;
}

int ObSPIService::check_and_deep_copy_result(ObIAllocator &alloc,
                                             const ObObj &src,
                                             ObObj &dst)
{
  int ret = OB_SUCCESS;

  if (dst.is_pl_extend()) {
    if (!src.is_pl_extend()) {
      ret =OB_ERR_EXPRESSION_WRONG_TYPE;
      LOG_WARN("expr is wrong type", K(ret));
    } else if (PL_CURSOR_TYPE == src.get_meta().get_extend_type() ||
               PL_OPAQUE_TYPE == src.get_meta().get_extend_type()) {
      OZ (ObUserDefinedType::deep_copy_obj(alloc, src, dst, true));
    } else {
      ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(dst.get_ext());
      if (NULL == composite) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected composite to store datum", KPC(composite), K(ret));
      } else {
        if (OB_SUCC(ret)) {
          ObPLComposite *src_composite = reinterpret_cast<ObPLComposite*>(src.get_ext());
          if (NULL == src_composite || src_composite->get_type() != composite->get_type()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected composite to store datum", KPC(src_composite), K(ret));
          } else if (OB_INVALID_ID == src_composite->get_id() || OB_INVALID_ID ==  composite->get_id()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected composite to store datum", K(src_composite->get_id()), K(composite->get_id()), K(ret));
          } else if (src_composite->get_id() != composite->get_id()) {
            ret = OB_ERR_EXPRESSION_WRONG_TYPE;
            LOG_WARN("src id is not same as dst id", K(src_composite->get_id()), K(composite->get_id()), K(ret));
          } else {
            OZ (pl::ObUserDefinedType::deep_copy_obj(alloc, src, dst));
          }
        }
      }
    }
  } else if (src.is_pl_extend()) {
    ret =OB_ERR_EXPRESSION_WRONG_TYPE;
    LOG_WARN("expr is wrong type", K(ret));
  } else {
    OZ (deep_copy_obj(alloc, src, dst));
  }

  return ret;
}

int ObSPIService::spi_set_package_variable(
  ObExecContext *exec_ctx,
  ObPLPackageGuard *guard,
  uint64_t package_id, int64_t var_idx, const ObObj &value)
{
  int ret = OB_SUCCESS;
  ObPL *pl_engine = NULL;
  ObMySQLProxy *sql_proxy = NULL;
  ObSQLSessionInfo *session_info = NULL;
  CK (OB_NOT_NULL(GCTX.schema_service_));
  CK (OB_NOT_NULL(exec_ctx));
  CK (OB_NOT_NULL(session_info = exec_ctx->get_my_session()));
  CK (OB_NOT_NULL(sql_proxy = exec_ctx->get_sql_proxy()));
  CK (OB_NOT_NULL(pl_engine = exec_ctx->get_pl_engine()));
  if (OB_SUCC(ret)) {
    ObObj result = *const_cast<ObObj *>(&value);
    ObPLPackageManager &pl_manager = pl_engine->get_package_manager();
    share::schema::ObSchemaGetterGuard schema_guard;
    ObPLPackageGuard package_guard{};
    ObPLResolveCtx resolve_ctx(exec_ctx->get_allocator(),
                               *session_info,
                               schema_guard,
                               guard != NULL ? *(guard) : package_guard,
                               *sql_proxy,
                               false); // is_prepare_protocol
    resolve_ctx.params_.plan_cache_ = exec_ctx->get_plan_cache();
    resolve_ctx.params_.pl_sql_runtime_ = exec_ctx->get_pl_sql_runtime();
    resolve_ctx.params_.pl_engine_ = exec_ctx->get_pl_engine();
    resolve_ctx.params_.srs_provider_ = exec_ctx->get_srs_provider();
    resolve_ctx.params_.lob_read_service_ = exec_ctx->get_lob_read_service();
    OZ (GCTX.schema_service_->get_runtime_schema_guard(
        schema_guard));
    OZ (package_guard.init());
    OZ (pl_manager.set_package_var_val(
          resolve_ctx, *exec_ctx, package_id, var_idx, result),
          K(package_id), K(var_idx));
  }
  return ret;
}

int ObSPIService::spi_set_package_variable(
  ObPLExecCtx *ctx, uint64_t package_id, int64_t var_idx, const ObObj &value)
{
  int ret = OB_SUCCESS;

  OZ (spi_set_package_variable(
    ctx->exec_ctx_, ctx->guard_, package_id, var_idx, value));
  return ret;
}

int ObSPIService::spi_set_variable_to_expr(ObPLExecCtx *ctx,
                                          const int64_t expr_idx,
                                           const ObObjParam *value,
                                          bool is_default)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->func_));
  CK (expr_idx != OB_INVALID_ID);
  CK (OB_LIKELY(0 <= expr_idx && expr_idx < ctx->func_->get_expressions().count()));
  const ObSqlExpression *expr = nullptr;
  OX (expr = ctx->func_->get_expressions().at(expr_idx));
  OZ (spi_set_variable(ctx, expr, value, is_default));
  return ret;
}

int ObSPIService::spi_set_variable(ObPLExecCtx *ctx,
                                   const ObSqlExpression* expr,
                                   const ObObjParam *value,
                                   bool is_default)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx) || OB_ISNULL(expr) || OB_ISNULL(value)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(ctx), K(expr), K(value), K(ret));
  } else {
    ObItemType expr_type = expr->get_expr_items().at(0).get_item_type();
    if (T_OP_GET_SYS_VAR == expr_type || T_OP_GET_USER_VAR == expr_type) {
      if (expr->get_expr_items().count() < 2 || T_VARCHAR != expr->get_expr_items().at(1).get_item_type()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected result expr", K(*expr), K(ret));
      } else {
        const ObString &name = expr->get_expr_items().at(1).get_obj().get_string();
        ObSetVar::SetScopeType scope = ObSetVar::SET_SCOPE_NEXT_TRANS;
        if (T_OP_GET_SYS_VAR == expr_type) {
          if (expr->get_expr_items().count() < 3 || T_INT != expr->get_expr_items().at(2).get_item_type()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Unexpected result expr", K(*expr), K(ret));
          } else {
            scope = static_cast<ObSetVar::SetScopeType>(expr->get_expr_items().at(2).get_obj().get_int());
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(set_variable(ctx, scope, name, *value, is_default))) {
          LOG_WARN("Failed to set variable", K(name), K(value), K(ret));
        }
      }
    } else if (T_OP_GET_PACKAGE_VAR == expr_type) {
      OV (5 <= expr->get_expr_items().count(), OB_ERR_UNEXPECTED, expr->get_expr_items().count());
      CK (T_UINT64 == expr->get_expr_items().at(1).get_item_type());
      CK (T_INT == expr->get_expr_items().at(2).get_item_type());
      OZ (spi_set_package_variable(
          ctx,
          expr->get_expr_items().at(1).get_obj().get_uint64(), // pkg id
          expr->get_expr_items().at(2).get_obj().get_int(), // var idx
          *value));
    } else if (T_OP_GET_SUBPROGRAM_VAR == expr_type) {
      ObSQLSessionInfo *session_info = NULL;
      uint64_t package_id = OB_INVALID_ID;
      uint64_t routine_id = OB_INVALID_ID;
      int64_t var_idx = OB_INVALID_INDEX;
      ObPLExecState *state = NULL;
      ObObjParam result = *const_cast<ObObjParam *>(value);
      CK (OB_NOT_NULL(ctx->exec_ctx_));
      CK (OB_NOT_NULL(session_info = ctx->exec_ctx_->get_my_session()));
      OX (package_id = expr->get_expr_items().at(1).get_obj().get_uint64());
      OX (routine_id = expr->get_expr_items().at(2).get_obj().get_uint64());
      OX (var_idx = expr->get_expr_items().at(3).get_obj().get_int());
      OZ (ObPLContext::get_exec_state_from_local(*session_info, package_id, routine_id, state));
      OZ (state->set_var(var_idx, result));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid sys func", K(expr_type), K(ret));
    }
  }

  SET_SPI_STATUS;
  return ret;
}

int ObSPIService::set_variable(ObPLExecCtx *ctx,
                               const ObSetVar::SetScopeType scope,
                               const ObString &name,
                               const ObObjParam &value,
                               bool is_default)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx) || OB_ISNULL(ctx->exec_ctx_)|| name.empty() || value.is_invalid_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(ctx), K(name), K(value), K(ret));
  } else {
    ObSQLSessionInfo *session = ctx->exec_ctx_->get_my_session();
    ObArenaAllocator allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_SET_VAR), OB_MALLOC_NORMAL_BLOCK_SIZE);
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Argument in pl context is NULL", K(session), K(ret));
    } else {
      char *sql = NULL;
      char *new_buf = NULL;
      char buf[OB_SHORT_SQL_LENGTH];
      MEMSET(buf, 0, OB_SHORT_SQL_LENGTH);
      int64_t pos = 0;
      snprintf(buf + pos, OB_SHORT_SQL_LENGTH - pos, "SET ");
      pos += 4;
      if (ObSetVar::SET_SCOPE_SESSION == scope) {
        snprintf(buf + pos, OB_SHORT_SQL_LENGTH - pos, "@@SESSION.");
        pos += 10;
      } else if (ObSetVar::SET_SCOPE_GLOBAL == scope) {
        snprintf(buf + pos, OB_SHORT_SQL_LENGTH - pos, "@@GLOBAL.");
        pos += 9;
      } else {
        snprintf(buf + pos, OB_SHORT_SQL_LENGTH - pos, "@");
        pos += 1;
      }

      if (OB_SUCC(ret)) {
        if (name.length() >= OB_SHORT_SQL_LENGTH - pos) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Argument in pl context is NULL",
                   K(OB_SHORT_SQL_LENGTH - pos), K(name), K(ret));
        } else {
          snprintf(buf + pos, name.length() + 1, "%s", name.ptr());
          pos += name.length();
        }
      }

      if (OB_SUCC(ret)) {
        snprintf(buf + pos, OB_SHORT_SQL_LENGTH - pos, "=");
        pos += 1;
      }

      if (OB_SUCC(ret)) {
        if (is_default) {
          snprintf(buf + pos, OB_SHORT_SQL_LENGTH - pos, "DEFAULT");
          sql = buf;
        } else {
          ObObjPrintParams print_params(session->get_timezone_info(),
                                        session->get_dtc_params().connection_collation_);
          int64_t tmp_pos = 0;
          if (OB_FAIL(value.print_sql_literal(buf + pos,
                                              OB_SHORT_SQL_LENGTH - pos,
                                              tmp_pos,
                                              print_params))) {
            if (OB_SIZE_OVERFLOW == ret) {
              int64_t alloc_len = OB_MAX_SQL_LENGTH;
              while (OB_SIZE_OVERFLOW == ret) {
                ret = OB_SUCCESS;
                if (OB_ISNULL(new_buf)) {
                  new_buf = static_cast<char*>(allocator.alloc(alloc_len));
                } else {
                  if (alloc_len < (1L << 20)) {
                    alloc_len *= 2;
                    allocator.free(new_buf);
                    new_buf = NULL;
                    new_buf = static_cast<char*>(allocator.alloc(alloc_len));
                  } else {
                    ret = OB_SIZE_OVERFLOW;
                    LOG_WARN("failed to print_plain_str_literal",
                              K(buf), K(new_buf), K(pos), K(tmp_pos), K(ret));
                    break;
                  }
                }
                if (OB_ISNULL(new_buf)) {
                  ret = OB_ALLOCATE_MEMORY_FAILED;
                  LOG_WARN("failed to alloc memory for set sql", K(ret), K(OB_MAX_SQL_LENGTH));
                } else {
                  tmp_pos = 0;
                  MEMCPY(new_buf, buf, pos);
                  if (OB_FAIL(value.print_sql_literal(new_buf + pos,
                                                      alloc_len - pos,
                                                      tmp_pos,
                                                      print_params))) {
                    if (OB_SIZE_OVERFLOW != ret) {
                      LOG_WARN("failed to print_plain_str_literal",
                            K(buf), K(new_buf), K(pos), K(tmp_pos), K(ret));
                    }
                  } else {
                    sql = new_buf;
                  }
                }
              }
            } else {
              LOG_WARN("failed to print_plain_str_literal",
                       K(buf), K(new_buf), K(pos), K(tmp_pos), K(ret));
            }
          } else {
            sql = buf;
          }
        }
      }

      if(OB_SUCC(ret)) {
        if (OB_FAIL(spi_query(ctx, ObString::make_string(sql), stmt::T_VARIABLE_SET))) {
          LOG_WARN("Failed to spi_query", K(sql), K(ret));
        }
      }
    }
  }
  return ret;
}


int ObSPIService::recreate_implicit_savapoint_if_need(pl::ObPLExecCtx *ctx, int &result)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()->get_pl_context()));
  OZ (recreate_implicit_savapoint_if_need(*(ctx->exec_ctx_), result));
  return ret;
}

int ObSPIService::recreate_implicit_savapoint_if_need(sql::ObExecContext &ctx, int &result)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx.get_my_session()));
  if (OB_SUCC(ret) // Perform check only if there is an implicit savepoint
      && OB_NOT_NULL(ctx.get_my_session()->get_pl_context())
      && ctx.get_my_session()->has_pl_implicit_savepoint()) {
    ObSQLSessionInfo *session_info = ctx.get_my_session();
    if (!session_info->is_in_transaction()) { // Transaction has ended, clear implicit savepoint marker
      OX (session_info->clear_pl_implicit_savepoint());
    } else if (!data_plane::tx_desc_contains_savepoint(
                   session_info->get_tx_desc(), PL_IMPLICIT_SAVEPOINT)) {
      // PL internal rollback to savepoint statement rolled back to the outer checkpoint, overwriting PL's implicit checkpoint, at this point a new checkpoint needs to be rebuilt
      OZ (ObSqlTransControl::create_savepoint(ctx, PL_IMPLICIT_SAVEPOINT));
    }
  }
  result = OB_SUCCESS == result ? ret : result;
  return ret;
}

int ObSPIService::spi_end_trans(ObPLExecCtx *ctx, const char *sql, bool is_rollback)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  if (OB_SUCC(ret)) {
    ObSQLSessionInfo *my_session = ctx->exec_ctx_->get_my_session();
    if (OB_ISNULL(my_session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("session ptr is null", K(ret));
    } else {
      ObString sqlstr(sql);
      OZ (ObSPIResultSet::check_nested_stmt_legal(*(ctx->exec_ctx_), sqlstr, stmt::T_END_TRANS));
      int64_t saved_query_start_time = my_session->get_query_start_time();
      my_session->set_query_start_time(ObTimeUtility::current_time());
      if (OB_SUCC(ret)) {
        // Internal submissions in PL use synchronous submission
        OZ (sql::ObSqlTransControl::end_trans(ctx->exec_ctx_->get_my_session(),
                                              ctx->exec_ctx_->get_need_disconnect_for_update(),
                                              ctx->exec_ctx_->get_trans_state(),
                                              is_rollback,
                                              true));
        // If a submission ban has occurred, do not retry PL as a whole
        if (!is_rollback) {
          OX (ctx->exec_ctx_->get_my_session()->set_pl_can_retry(false));
        }
      }
      // restore query_start_time
      my_session->set_query_start_time(saved_query_start_time);
    }
  }
  if (OB_SUCC(ret)
      && OB_NOT_NULL(ctx->exec_ctx_->get_my_session()->get_pl_implicit_cursor())) {
    ctx->exec_ctx_->get_my_session()->get_pl_implicit_cursor()->set_rowcount(0);
  }
  recreate_implicit_savapoint_if_need(ctx, ret);
  LOG_DEBUG("spi end trans", K(ret), K(sql), K(is_rollback));

  return ret;
}

ObPLSPITraceIdGuard::ObPLSPITraceIdGuard(const ObString &sql,
                    const ObString &ps_sql,
                    ObSQLSessionInfo &session,
                    int &ret,
                    ObCurTraceId::TraceId *reused_trace_id)
    : sql_(sql), ps_sql_(ps_sql), session_(session), ret_(ret)
{
  if (OB_NOT_NULL(ObCurTraceId::get_trace_id())) {
    origin_trace_id_.set(*ObCurTraceId::get_trace_id());

    if (reused_trace_id != nullptr && reused_trace_id->is_valid()) {
      // do not log when fetching from cursor
      ObCurTraceId::get_trace_id()->set(*reused_trace_id);
    } else {
      ObCurTraceId::TraceId new_trace_id;
      new_trace_id.init(origin_trace_id_.get_addr());

      // log with PL trace_id
      LOG_TRACE("executing sql, trace id changed",
              K(sql_), K(ps_sql_),
              "from", origin_trace_id_,
              "to", new_trace_id);

      ObCurTraceId::get_trace_id()->set(new_trace_id);
    }
  }
}

ObPLSPITraceIdGuard::~ObPLSPITraceIdGuard()
{
  int &ret = ret_;
  ObCurTraceId::TraceId curr_trace_id;
  if (OB_NOT_NULL(ObCurTraceId::get_trace_id())) {
    curr_trace_id.set(*ObCurTraceId::get_trace_id());
    session_.set_last_trace_id(ObCurTraceId::get_trace_id());
    ObCurTraceId::get_trace_id()->set(origin_trace_id_);

    if (OB_FAIL(ret) && ret != OB_READ_NOTHING) {
      LOG_WARN("sql execution finished, trace id restored",
            K(ret), K(sql_), K(ps_sql_),
            "from", curr_trace_id,
            "to", origin_trace_id_);
    } else {
      LOG_TRACE("sql execution finished, trace id restored",
            K(sql_), K(ps_sql_),
            "from", curr_trace_id,
            "to", origin_trace_id_);
    }
  }
}

// Common SQL execute interface for static SQL, dynamic SQL and server cursors.
int ObSPIService::spi_inner_execute(ObPLExecCtx *ctx,
                                    ObIAllocator &out_param_alloc,
                                    const ObString &sql,
                                    const ObString &ps_sql,
                                    int64_t type,
                                    void *params,
                                    int64_t param_count,
                                    const ObSqlExpression **into_exprs,
                                    int64_t into_count,
                                    const ObDataType *column_types,
                                    int64_t type_count,
                                    const bool *exprs_not_null_flag,
                                    const int64_t *pl_integer_ranges,
                                    bool is_type_record,
                                    bool for_update,
                                    bool is_dynamic_sql,
                                    bool is_cursor_params)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->allocator_));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info()));

  if (OB_SUCC(ret)) {
    HEAP_VAR(ObSPIResultSet, spi_result) {
      
      ObSQLSessionInfo *session = ctx->exec_ctx_->get_my_session();
      stmt::StmtType stmt_type = static_cast<stmt::StmtType>(type);

      CK (OB_NOT_NULL(ctx->exec_ctx_->get_plan_cache_access_service()));
      OZ (spi_result.init(
          *session, *ctx->exec_ctx_->get_plan_cache_access_service()));
      OZ (spi_result.start_nested_stmt_if_need(ctx, sql, stmt_type, for_update));

      if (OB_SUCC(ret)) {

        ObQueryRetryCtrl retry_ctrl;
        ObSPIExecEnvGuard env_guard(*session, spi_result);
        int save_sqlcode = session->get_pl_sqlcode_info()->get_sqlcode();
        ObString save_sqlmsg = session->get_pl_sqlcode_info()->get_sqlmsg(); 

        do {

          bool can_retry = true;
          int64_t row_count = 0;
          ObArenaAllocator allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_STATIC_SQL_EXEC), OB_MALLOC_NORMAL_BLOCK_SIZE);
          {
            ObPLSPITraceIdGuard trace_id_guard(sql, ps_sql, *session, ret);
            ObPLSubPLSqlTimeGuard guard(ctx);
            ObSPIRetryCtrlGuard retry_guard(retry_ctrl, spi_result, *session, ret);

            OX (session->get_pl_sqlcode_info()->set_sqlcode(OB_SUCCESS));

            if (OB_SUCC(ret) && !ObStmt::is_diagnostic_stmt(stmt_type)) {
              ob_reset_tsi_warning_buffer();
            }

            OZ (inner_open(ctx,
                           allocator,
                           sql,
                           ps_sql,
                           type,
                           params,
                           param_count,
                           into_exprs,
                           into_count,
                           spi_result,
                           spi_result.get_out_params(),
                           is_dynamic_sql,
                           is_cursor_params), K(sql), K(ps_sql));
            OZ (inner_fetch(ctx,
                            can_retry,
                            spi_result,
                            into_exprs,
                            into_count,
                            column_types,
                            type_count,
                            exprs_not_null_flag,
                            pl_integer_ranges,
                            row_count,
                            NULL,
                            false,
                            false,
                            NULL,
                            0,
                            is_type_record), K(ps_sql), K(sql), K(type));

            if (OB_SUCC(ret) && (is_dynamic_sql || is_cursor_params)) {
              OZ (dynamic_out_params(
                out_param_alloc, spi_result.get_result_set(), params, param_count, is_cursor_params));
            }
            if (spi_result.get_out_params().has_out_param()) {
              OZ (process_function_out_result(ctx, *spi_result.get_result_set(), spi_result.get_out_params().get_out_params()));
            }

            ObPLSqlCodeInfo *sqlcode_info = session->get_pl_sqlcode_info();
            if (sqlcode_info->get_sqlcode() == OB_SUCCESS) {
              sqlcode_info->set_sqlcode(save_sqlcode, save_sqlmsg);
            }

            if (can_retry) {
              retry_guard.test();
            }
            int close_ret = spi_result.close_result_set();
            if (OB_SUCCESS != close_ret) {
              LOG_WARN("close spi result failed", K(ret), K(close_ret));
            }
            ret = OB_SUCCESS == ret ? close_ret : ret;
          }
          spi_result.destruct_exec_params(*session);

        } while (RETRY_TYPE_NONE != retry_ctrl.get_retry_type()); // SPI only does LOCAL retry
      }

      if (OB_SUCC(ret)
          && (ObStmt::is_ddl_stmt(stmt_type, true)
              || ObStmt::is_tcl_stmt(stmt_type)
              || session->get_local_autocommit())) {
        OX (session->set_pl_can_retry(false));
      }

      if (OB_SUCC(ret) && 0 == param_count) {
        if (ObStmt::is_ddl_stmt(stmt_type, true)
            || ObStmt::is_tcl_stmt(stmt_type)
            || ObStmt::is_savepoint_stmt(stmt_type)) {
          if (ObStmt::is_ddl_stmt(stmt_type, true)) {
            OZ (force_refresh_schema(), sql);
          }
          recreate_implicit_savapoint_if_need(ctx, ret);
        }
      }
      if (OB_FAIL(ret) && !ObStmt::is_diagnostic_stmt(stmt_type)) {
        // support `SHOW WARNINGS` in mysql PL
        session->set_show_warnings_buf(ret);
      }

      spi_result.end_nested_stmt_if_need(ctx, ret);
  
      SET_SPI_STATUS;
    }
  }
  return ret;
}

int ObSPIService::spi_get_current_expr_allocator(pl::ObPLExecCtx *ctx, int64_t *addr)
{
  int ret = OB_SUCCESS;
  ObIAllocator *alloc = nullptr;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(addr));
  OX (*addr = 0);
  OX (alloc = ctx->get_top_expr_allocator());
  CK (OB_NOT_NULL(alloc));
  OX (*addr = reinterpret_cast<int64_t>(alloc));
  return ret;
}

int ObSPIService::spi_init_composite(ObIAllocator *current_allcator, int64_t addr, bool is_record, bool need_allocator)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(current_allcator));
  if (OB_SUCC(ret)) {
    if (is_record) {
      ObPLRecord *record = reinterpret_cast<ObPLRecord *>(addr);
      CK(OB_NOT_NULL(record));
      OZ (record->init_data(*current_allcator, need_allocator));
    } else {
      ObPLCollection *collection = reinterpret_cast<ObPLCollection *>(addr);
      CK(OB_NOT_NULL(collection));
      OZ (collection->init_allocator(*current_allcator, need_allocator));
    }
  }
  return ret;
}

int ObSPIService::spi_get_parent_allocator(ObIAllocator *current_allcator,
                                           int64_t *parent_allocator_addr)
{
  int ret = OB_SUCCESS;
  ObPLAllocator1 *pl_allocator = nullptr;
  *parent_allocator_addr = 0;
  CK (OB_NOT_NULL(current_allcator));
  CK (OB_NOT_NULL(parent_allocator_addr));
  OX (pl_allocator = dynamic_cast<ObPLAllocator1 *>(current_allcator));
  CK (OB_NOT_NULL(pl_allocator));
  if (OB_SUCC(ret) && OB_NOT_NULL(pl_allocator->get_parent_allocator())) {
    *parent_allocator_addr = reinterpret_cast<int64_t>(pl_allocator->get_parent_allocator());
  }

  return ret;
}

int ObSPIService::spi_query_into_expr_idx(ObPLExecCtx *ctx,
                                          const ObString &sql,
                                          int64_t type,
                                          const int64_t *into_exprs_idx,
                                          int64_t into_count,
                                          const ObDataType *column_types,
                                          int64_t type_count,
                                          const bool *exprs_not_null_flag,
                                          const int64_t *pl_integer_ranges,
                                          bool is_type_record,
                                          bool for_update)
{
  int ret = OB_SUCCESS;

  ObArenaAllocator alloc("SpiTemp", OB_MALLOC_NORMAL_BLOCK_SIZE);
  const ObSqlExpression **into_exprs = nullptr;

  MAKE_EXPR_BUFFER(alloc, into_exprs_idx, into_count, into_exprs);

  OZ (spi_query(ctx, sql, type, into_exprs, into_count, column_types,
                type_count, exprs_not_null_flag, pl_integer_ranges,
                is_type_record, for_update));

  return ret;
}

int ObSPIService::spi_query(ObPLExecCtx *ctx,
                            const ObString &sql,
                            int64_t type,
                            const ObSqlExpression **into_exprs,
                            int64_t into_count,
                            const ObDataType *column_types,
                            int64_t type_count,
                            const bool *exprs_not_null_flag,
                            const int64_t *pl_integer_ranges,
                            bool is_type_record,
                            bool for_update)
{
  int ret = OB_SUCCESS;
  const ObString empty_sql;
  OZ (SMART_CALL(spi_inner_execute(ctx, *ctx->get_top_expr_allocator(), sql, empty_sql, type, NULL, 0,
                        into_exprs, into_count,
                        column_types, type_count,
                        exprs_not_null_flag,
                        pl_integer_ranges, is_type_record, for_update)),
                        sql, type);
  return ret;
}

int ObSPIService::spi_execute_with_expr_idx(ObPLExecCtx *ctx,
                              const ObString &ps_sql,
                              int64_t type,
                              const int64_t *param_exprs_idx,
                              int64_t param_count,
                              const int64_t *into_exprs_idx,
                              int64_t into_count,
                              const ObDataType *column_types,
                              int64_t type_count,
                              const bool *exprs_not_null_flag,
                              const int64_t *pl_integer_ranges,
                              bool is_type_record,
                              bool for_update)
{
  int ret = OB_SUCCESS;

  ObArenaAllocator alloc("SpiTemp", OB_MALLOC_NORMAL_BLOCK_SIZE);
  const ObSqlExpression **param_exprs = nullptr;
  const ObSqlExpression **into_exprs = nullptr;

  MAKE_EXPR_BUFFER(alloc, param_exprs_idx, param_count, param_exprs);
  MAKE_EXPR_BUFFER(alloc, into_exprs_idx, into_count, into_exprs);

  OZ (spi_execute(ctx, ps_sql, type, param_exprs, param_count, into_exprs,
                  into_count, column_types, type_count, exprs_not_null_flag,
                  pl_integer_ranges, is_type_record,
                  for_update));

  return ret;
}
int ObSPIService::spi_execute(ObPLExecCtx *ctx,
                              const ObString &ps_sql,
                              int64_t type,
                              const ObSqlExpression **param_exprs,
                              int64_t param_count,
                              const ObSqlExpression **into_exprs,
                              int64_t into_count,
                              const ObDataType *column_types,
                              int64_t type_count,
                              const bool *exprs_not_null_flag,
                              const int64_t *pl_integer_ranges,
                              bool is_type_record,
                              bool for_update)
{
  int ret = OB_SUCCESS;
  const ObString empty_sql;
  OZ (SMART_CALL(spi_inner_execute(ctx, *ctx->get_top_expr_allocator(), empty_sql, ps_sql, type, param_exprs, param_count,
                        into_exprs, into_count, column_types, type_count,
                        exprs_not_null_flag, pl_integer_ranges,
                        is_type_record, for_update)));
  return ret;
}

int ObSPIService::spi_prepare(common::ObIAllocator &allocator,
                              ObSQLSessionInfo &session,
                              ObMySQLProxy &sql_proxy,
                              share::schema::ObSchemaGetterGuard &schema_guard,
                              sql::ObRawExprFactory &expr_factory,
                              ObIPLSqlRuntime *pl_sql_runtime,
                              const ObString &sql,
                              bool is_cursor,
                              pl::ObPLBlockNS *secondary_namespace,
                              ObSPIPrepareResult &prepare_result,
                              pl::ObPLAstUnit &func)
{
  int ret = OB_SUCCESS;
  UNUSED(func);
  if (OB_SUCC(ret)) {
    ret = spi_parse_prepare(allocator,
                            session,
                            sql_proxy,
                            schema_guard,
                            expr_factory,
                            pl_sql_runtime,
                            sql,
                            secondary_namespace,
                            prepare_result);
  }
  return ret;
}

int ObSPIService::spi_parse_prepare(common::ObIAllocator &allocator,
                                    ObSQLSessionInfo &session,
                                    ObMySQLProxy &sql_proxy,
                                    share::schema::ObSchemaGetterGuard &schema_guard,
                                    sql::ObRawExprFactory &expr_factory,
                                    ObIPLSqlRuntime *pl_sql_runtime,
                                    const ObString &sql,
                                    pl::ObPLBlockNS *secondary_namespace,
                                    ObSPIPrepareResult &prepare_result)
{
  int ret = OB_SUCCESS;
  if (sql.empty() || OB_ISNULL(secondary_namespace)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(sql), K(secondary_namespace), K(ret));
  } else {
    ObParser parser(allocator, session.get_sql_mode(), session.get_charsets4parser());
    ParseResult parse_result;
    if (OB_FAIL(parser.prepare_parse(sql, static_cast<void*>(secondary_namespace), parse_result))) {
      LOG_WARN("Generate syntax tree failed", K(sql), K(ret));
    } else if (OB_FAIL(ob_write_string(allocator, ObString(parse_result.no_param_sql_len_, parse_result.no_param_sql_), prepare_result.route_sql_))) {
      LOG_WARN("failed to write string", K(sql), K(ret));
    } else {
      PLPrepareCtx pl_prepare_ctx(session, NULL, false, false, false);
      SMART_VAR(PLPrepareResult, pl_prepare_result) {
        CK (OB_NOT_NULL(pl_sql_runtime));
#ifdef ERRSIM
        OX (ret = OB_E(EventTable::EN_SPI_SQL_EXEC) OB_SUCCESS);
#endif
        OZ (pl_sql_runtime->prepare_pl_sql(
          prepare_result.route_sql_, pl_prepare_ctx, pl_prepare_result), K(sql));
        CK (OB_NOT_NULL(pl_prepare_result.result_set_));

        LOG_TRACE("execute sql", K(sql), K(ret));

        if (OB_SUCC(ret)) {
          OZ (ob_write_string(allocator, pl_prepare_result.result_set_->get_stmt_ps_sql(), prepare_result.ps_sql_));
          prepare_result.type_ = pl_prepare_result.result_set_->get_stmt_type();
          prepare_result.for_update_ = pl_prepare_result.result_set_->get_is_select_for_update();
          prepare_result.has_hidden_rowid_ = false;
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(resolve_exec_params(parse_result,
                                          session,
                                          schema_guard,
                                          expr_factory,
                                          *secondary_namespace,
                                          prepare_result,
                                          allocator))) { // resolve PL exec variable
            LOG_WARN("failed to resolve_exec_params", K(ret));
          } else if (OB_FAIL(resolve_into_params(parse_result,
                                                session,
                                                schema_guard,
                                                expr_factory,
                                                *secondary_namespace,
                                                prepare_result))) { // resolve PL into variable
            LOG_WARN("failed to resolve_into_params", K(ret));
          } else if (OB_FAIL(resolve_ref_objects(parse_result,
                                                session,
                                                schema_guard,
                                                prepare_result))) { //resolve ref object
            LOG_WARN("failed to resolve_ref_objects", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObSPIService::spi_build_record_type(common::ObIAllocator &allocator,
                                        ObSQLSessionInfo &session,
                                        share::schema::ObSchemaGetterGuard &schema_guard,
                                        const sql::ObResultSet &result_set,
                                        int64_t hidden_column_count,
                                        pl::ObRecordType *&record_type,
                                        uint64_t &rowid_table_id,
                                        pl::ObPLBlockNS *secondary_namespace,
                                        bool &has_dup_column_name)
{
  int ret = OB_SUCCESS;
  const common::ColumnsFieldIArray *columns = result_set.get_field_columns();
  has_dup_column_name = false;
  if (OB_ISNULL(columns) || OB_ISNULL(record_type) || 0 == columns->count() || OB_ISNULL(secondary_namespace)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(columns), K(record_type), K(ret));
  } else {
    int dup_idx = 0;
    OZ (record_type->record_members_init(&allocator, columns->count() - hidden_column_count));
    for (int64_t i = 0; OB_SUCC(ret) && i < columns->count() - hidden_column_count; ++i) {
      ObPLDataType pl_type;
      if (columns->at(i).type_.is_ext() && columns->at(i).accuracy_.accuracy_ != OB_INVALID_ID) {
        uint64_t udt_id = columns->at(i).accuracy_.accuracy_;
        const ObUserDefinedType *user_type = NULL;
        OZ (secondary_namespace->get_pl_data_type_by_id(udt_id, user_type));
        CK (OB_NOT_NULL(user_type));
        OX (pl_type.set_user_type_id(user_type->get_type(), udt_id));
        OX (pl_type.set_type_from(user_type->get_type_from()));
      } else if (columns->at(i).type_.is_null()) {
        ObDataType data_type;
        ObCollationType collation_type = session.get_nls_collation();
        ObCharsetType charset_type = ObCharset::charset_type_by_coll(collation_type);
        data_type.set_obj_type(ObVarcharType);
        data_type.set_charset_type(charset_type);
        data_type.set_collation_type(collation_type);
        data_type.meta_.set_collation_level(CS_LEVEL_IMPLICIT);
        data_type.set_length(OB_MAX_EXTENDED_PL_CHAR_LENGTH_BYTE);
        data_type.set_length_semantics(LS_BYTE);
        pl_type.set_data_type(data_type);
      }else {
        ObDataType data_type;
        data_type.set_meta_type(columns->at(i).type_.get_meta());
        data_type.set_accuracy(columns->at(i).accuracy_);
        pl_type.set_data_type(data_type);
      }
      if (OB_SUCC(ret)) {
        char* name_buf = NULL;
        if (OB_ISNULL(name_buf = static_cast<char*>(allocator.alloc(columns->at(i).cname_.length() + 10)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc column name buf", K(ret), K(columns->at(i).cname_));
        } else {
          bool duplicate = false;
          for (int64_t j = 0; OB_SUCC(ret) && j < columns->count() - hidden_column_count; ++j) {
            if (i != j && columns->at(j).cname_ == columns->at(i).cname_) {
              duplicate = true;
              has_dup_column_name = true;
              break;
            }
          }
          if (duplicate) {
            sprintf(name_buf, "%.*s&%d",
                    columns->at(i).cname_.length(), columns->at(i).cname_.ptr(), dup_idx);
            dup_idx++;
          } else {
            sprintf(name_buf, "%.*s", columns->at(i).cname_.length(), columns->at(i).cname_.ptr());
          }
          ObString deep_copy_name(name_buf);
          if (OB_FAIL(record_type->add_record_member(deep_copy_name, pl_type))) {
            LOG_WARN("add record member failed", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret) && 1 == hidden_column_count) {
        const common::ObField &field = columns->at(columns->count() - 1);
        uint64_t table_id = OB_INVALID_ID;
        OZ (schema_guard.get_table_id(field.dname_, field.org_tname_,
                                      false, ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES, table_id));
        OX (rowid_table_id = table_id);
    } else {
      rowid_table_id = OB_INVALID_ID;
    }
  }
  return ret;
}

int ObSPIService::calc_dynamic_sqlstr(
  ObPLExecCtx *ctx, const ObSqlExpression *sql, ObSqlString &sql_str)
{
  int ret = OB_SUCCESS;
  ObObjParam result;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(sql));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  OZ (spi_calc_expr(ctx, sql, OB_INVALID_INDEX, &result));
  if (OB_FAIL(ret)) {
  } else if (result.is_null_or_empty_string()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN(
      "statement string in EXECUTE IMMEDIATE is NULL or 0 length", K(ret), K(result));
  } else if (!result.is_string_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("Dynamic sql is not a string", K(ret), K(result), K(sql_str));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Dynamic sql is not a string");
  } else {
    ObArenaAllocator temp_allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_DYNAMIC_SQL_EXEC), OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObString tmp_sql;
    ObString user_sql;
    ObCharsetType client_cs_type = CHARSET_INVALID;

    if (OB_SUCC(ret) && result.is_lob_storage()) {
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(
              *ctx->exec_ctx_, &temp_allocator, result, tmp_sql))) {
        LOG_WARN("fail to read lob data", K(ret), K(result));
      }
    } else {
      OZ (result.get_string(tmp_sql));
    }

    if (OB_SUCC(ret) && tmp_sql.length() > 1 && ';' == tmp_sql[tmp_sql.length() - 1]) {
      tmp_sql.assign_ptr(tmp_sql.ptr(), tmp_sql.length() - 1);
    }
    OZ (ctx->exec_ctx_->get_my_session()->get_character_set_client(client_cs_type));
    OZ (ObCharset::charset_convert(temp_allocator, tmp_sql,
                                   result.get_collation_type(),
                                   ObCharset::get_default_collation(client_cs_type),
                                   user_sql));
    OZ (sql_str.append(user_sql));
    LOG_DEBUG("Dynamic sql", K(ret), K(result), K(sql_str));
  }
  return ret;
}

int ObSPIService::prepare_dynamic(ObPLExecCtx *ctx,
                                  const ObSqlExpression *sql_expr,
                                  ObIAllocator &allocator,
                                  int64_t param_cnt,
                                  ObSqlString &sql_str,
                                  ObString &ps_sql,
                                  stmt::StmtType &stmt_type,
                                  bool &for_update,
                                  bool &hidden_rowid,
                                  int64_t &into_cnt,
                                  bool &skip_locked,
                                  ParamStore *params)
{
  int ret = OB_SUCCESS;
  OZ (calc_dynamic_sqlstr(ctx, sql_expr, sql_str));
  OZ (prepare_dynamic(ctx, allocator, param_cnt, sql_str,
                      ps_sql, stmt_type, for_update, hidden_rowid, into_cnt, skip_locked, params));
  return ret;
}

int ObSPIService::prepare_dynamic(ObPLExecCtx *ctx,
                                  ObIAllocator &allocator,
                                  int64_t param_cnt,
                                  ObSqlString &sql_str,
                                  ObString &ps_sql,
                                  stmt::StmtType &stmt_type,
                                  bool &for_update,
                                  bool &hidden_rowid,
                                  int64_t &into_cnt,
                                  bool &skip_locked,
                                  ParamStore *params)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(ctx->allocator_));
  CK (OB_NOT_NULL(session = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_pl_sql_runtime()));
  stmt_type = stmt::T_NONE;
  bool is_prepare_with_param = OB_NOT_NULL(params);
  if (OB_SUCC(ret)) {
    PLPrepareCtx pl_prepare_ctx(*session, NULL, true, false, is_prepare_with_param);

    SMART_VAR(PLPrepareResult, pl_prepare_result) {
#ifdef ERRSIM
      OX (ret = OB_E(EventTable::EN_SPI_SQL_EXEC) OB_SUCCESS);
#endif
      OZ (ctx->exec_ctx_->get_pl_sql_runtime()->prepare_pl_sql(
          sql_str.string(), pl_prepare_ctx, pl_prepare_result, nullptr));
      CK (OB_NOT_NULL(pl_prepare_result.result_set_));

      OX (stmt_type = static_cast<stmt::StmtType>(pl_prepare_result.result_set_->get_stmt_type()));
      OZ (ob_write_string(allocator, pl_prepare_result.result_set_->get_stmt_ps_sql(), ps_sql, true));
      OX (for_update = pl_prepare_result.result_set_->get_is_select_for_update());
      OX (hidden_rowid = pl_prepare_result.result_set_->has_hidden_rowid());
      OX (into_cnt = pl_prepare_result.result_set_->get_into_exprs().count());
      OX (skip_locked = pl_prepare_result.result_set_->is_skip_locked());

      if (OB_SUCC(ret)) {
        const int64_t exec_param_cnt = ObStmt::is_dml_stmt(stmt_type)
                                         ? pl_prepare_result.result_set_->get_external_params().count()
                                         : pl_prepare_result.result_set_->get_param_fields()->count();
        if (param_cnt != exec_param_cnt) {
          ret = OB_ERR_WRONG_DYNAMIC_PARAM;
          LOG_USER_ERROR(OB_ERR_WRONG_DYNAMIC_PARAM, exec_param_cnt, param_cnt);
        }
      }

      // SELECT INTO targets belong to the PL statement and are not SQL bind
      // parameters. Execute the rewritten route SQL without the INTO clause.
      if (OB_SUCC(ret) && stmt::T_SELECT == stmt_type && into_cnt != 0) {
        CK (!pl_prepare_result.result_set_->get_route_sql().empty());
        CK ((pl_prepare_result.result_set_->get_route_sql().length() + 1) < OB_MAX_SQL_LENGTH);
        OX (sql_str.reset());
        OZ (sql_str.append(pl_prepare_result.result_set_->get_route_sql()));
      }
    }
  }

  return ret;
}

int ObSPIService::dynamic_out_params(
  common::ObIAllocator &allocator, ObResultSet *result_set, void* params, int64_t param_count, bool is_cursor_params)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  const ParamStore *param_store = NULL;
  if (param_count > 0) {
    CK (OB_NOT_NULL(params));
    CK (OB_NOT_NULL(result_set));
    CK (OB_NOT_NULL(
      plan_ctx = result_set->get_exec_context().get_physical_plan_ctx()));
    CK (OB_NOT_NULL(param_store = &(plan_ctx->get_param_store())));
    OV (param_store->count() >= param_count,
        OB_ERR_UNEXPECTED, param_store->count(), param_count);
    for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
      // memory of params_store from result set will released by result close.
      // so we need to deep copy it.
      ObObjParam &obj = is_cursor_params ? reinterpret_cast<ParamStore *>(params)->at(i)
                                    : *(reinterpret_cast<ObObjParam**>(params)[i]);
      if (param_store->at(i).is_pl_extend()
          && param_store->at(i).get_meta().get_extend_type() != PL_CURSOR_TYPE) {
        OZ (pl::ObUserDefinedType::deep_copy_obj(allocator, param_store->at(i), obj, true));
      } else {
        OZ (deep_copy_objparam(allocator, param_store->at(i), obj));
      }
    }
  }
  return ret;
}


int ObSPIService::check_dynamic_sql_legal(ObIAllocator &alloc,
                                          ObSqlString &sql_str,
                                          stmt::StmtType stmt_type,
                                          int64_t into_count,
                                          int64_t param_count)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    if (ObStmt::is_ddl_stmt(stmt_type, false)
        && (into_count > 0 || param_count > 0)) {
      ret = OB_ERR_DDL_IN_ILLEGAL_CONTEXT;
      LOG_WARN("DDL statement is executed in an illegal context",
                K(ret), K(into_count), K(param_count));
    } else { /*do nothing*/ }
  }

  if (OB_SUCC(ret)) {
    ObParser parser(alloc, STD_MODE);
    ObMPParseStat parse_stat;
    ObSEArray<ObString, 1> queries;
    OZ (parser.split_multiple_stmt(sql_str.string(), queries, parse_stat));
    if (OB_SUCC(ret) && queries.count() > 1) {
      ret = OB_ERR_CMD_NOT_PROPERLY_ENDED;
      LOG_WARN("execute immdeidate only support one stmt", K(ret));
    }
  }
  return ret;
}

int ObSPIService::deep_copy_dynamic_param(ObPLExecCtx *ctx,
                                            ObSPIResultSet &spi_result,
                                            ObIAllocator &allocator,
                                            ObObjParam *param,
                                            ParamStore *&exec_params)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(param));
  if (OB_SUCC(ret)) {
    bool need_free_on_fail = false;
    ObObjParam new_param = *param;
    if (param->is_pl_extend()) {
      new_param.set_int_value(0);
      OZ (pl::ObUserDefinedType::deep_copy_obj(allocator, *param, new_param, true));
      OX (need_free_on_fail = true);
    } else {
      OZ (deep_copy_obj(allocator, *param, new_param));
    }
    OX (new_param.set_need_to_check_type(true));
    OZ (exec_params->push_back(new_param), K(new_param));
    if (OB_FAIL(ret) && need_free_on_fail) {
      int tmp = pl::ObUserDefinedType::destruct_obj(new_param, ctx->exec_ctx_->get_my_session());
    }
  }
  return ret;
}

int ObSPIService::prepare_dynamic_sql_params(ObPLExecCtx *ctx,
                                             ObSPIResultSet &spi_result,
                                             ObIAllocator &allocator,
                                             int64_t exec_param_cnt,
                                             ObObjParam **params,
                                             ParamStore *&exec_params)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(exec_params = reinterpret_cast<ParamStore*>(allocator.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for exec params", K(ret));
  } else {
    new (exec_params) ParamStore((ObWrapperAllocator(allocator)));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < exec_param_cnt; ++i) {
    CK (OB_NOT_NULL(params[i]));
    OZ (deep_copy_dynamic_param(ctx, spi_result, allocator, params[i], exec_params));
  }
  OZ (store_params_string(ctx, spi_result, exec_params));
  return ret;
}

int ObSPIService::prepare_cursor_params(ObPLExecCtx *ctx,
                                          ObSPIResultSet &spi_result,
                                          ObIAllocator &allocator,
                                          int64_t exec_param_cnt,
                                          ParamStore *params,
                                          ParamStore *&exec_params)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(exec_params = reinterpret_cast<ParamStore*>(allocator.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for exec params", K(ret));
  } else {
    new (exec_params) ParamStore((ObWrapperAllocator(allocator)));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < exec_param_cnt; ++i) {
    OZ (deep_copy_dynamic_param(ctx, spi_result, allocator, &params->at(i), exec_params));
  }
  OZ (store_params_string(ctx, spi_result, exec_params));
  return ret;
}

int ObSPIService::spi_execute_immediate(ObPLExecCtx *ctx,
                                        const int64_t sql_idx,
                                        common::ObObjParam **params,
                                        int64_t param_count,
                                        const int64_t *into_exprs_idx,
                                        int64_t into_count,
                                        const ObDataType *column_types,
                                        int64_t type_count,
                                        const bool *exprs_not_null_flag,
                                        const int64_t *pl_integer_ranges,
                                        bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  ObSqlString sql_str;
  const ObSqlExpression *sql = nullptr;
  const ObSqlExpression **into_exprs = nullptr;

  ObArenaAllocator allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_DYNAMIC_SQL_EXEC), OB_MALLOC_NORMAL_BLOCK_SIZE);

  stmt::StmtType stmt_type = stmt::T_NONE;
  ObString ps_sql;
  bool for_update = false;
  bool hidden_rowid = false;
  bool skip_locked = false;
  int64_t inner_into_cnt = 0; // number of variables in the into clause of dynamic statements
  ParamStore param_store((ObWrapperAllocator(allocator)));

  stmt::StmtType saved_stmt_type = stmt::T_NONE;
  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(ctx->func_));
  CK ((OB_NOT_NULL(params) && param_count > 0) || (OB_ISNULL(params) && 0 == param_count));
  CK (OB_NOT_NULL(session = ctx->exec_ctx_->get_my_session()));
  CK (sql_idx != OB_INVALID_ID);
  CK (OB_LIKELY(0 <= sql_idx && sql_idx < ctx->func_->get_expressions().count()));
  OX (sql = ctx->func_->get_expressions().at(sql_idx));
  CK (OB_NOT_NULL(sql));
  OX (saved_stmt_type = session->get_stmt_type());
  OZ (param_store.reserve(param_count));
  for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
    CK (OB_NOT_NULL(params[i]));
    if (OB_SUCC(ret)) {
      ObObjParam new_param = *params[i];
      OZ (param_store.push_back(new_param));
    }
  }

  MAKE_EXPR_BUFFER(allocator, into_exprs_idx, into_count, into_exprs);

  // Step: Prepare dynamic SQL! Only prepare once!
  OZ (prepare_dynamic(ctx,
                      sql,
                      allocator,
                      param_count,
                      sql_str,
                      ps_sql,
                      stmt_type,
                      for_update,
                      hidden_rowid,
                      inner_into_cnt,
                      skip_locked,
                      &param_store));

  OZ (check_dynamic_sql_legal(allocator,
                      sql_str,
                      stmt_type,
                      into_count,
                      param_count));

  OX (session->set_stmt_type(saved_stmt_type));

  // Step: execute dynamic SQL now!
  if (OB_FAIL(ret)) {
  } else if (ObStmt::is_select_stmt(stmt_type) && !for_update && into_count <= 0) {
    /* A SELECT without INTO is parsed but not executed. */
    ObPLCursorInfo *implicit_cursor = session->get_pl_implicit_cursor();
    CK (OB_NOT_NULL(implicit_cursor));
    OX (implicit_cursor->set_rowcount(0));
  } else {
    OZ (SMART_CALL(spi_inner_execute(ctx,
                                     *ctx->get_top_expr_allocator(),
                                     sql_str.string(),
                                     ps_sql,
                                     stmt_type,
                                     params,
                                     param_count,
                                     into_exprs,
                                     into_count,
                                     column_types,
                                     type_count,
                                     exprs_not_null_flag,
                                     pl_integer_ranges,
                                     is_type_record,
                                     for_update,
                                     true /*is_dynamic_sql*/)));
  }
  return ret;
}

int ObSPIService::spi_cursor_alloc(ObIAllocator &allocator, ObObj &obj)
{
  int ret = OB_SUCCESS;
  ObPLCursorInfo *cursor = NULL;
  if (OB_ISNULL(cursor =
        static_cast<ObPLCursorInfo*>(allocator.alloc(sizeof(ObPLCursorInfo))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc mysqlresult", K(ret));
  } else {
    new(cursor) ObPLCursorInfo(&allocator);
    LOG_DEBUG("cursor alloc, local cursor", K(cursor));
    cursor->reset();
    obj.set_extend(reinterpret_cast<int64_t>(cursor), PL_CURSOR_TYPE);
  }
  return ret;
}

int ObSPIService::spi_cursor_init(ObPLExecCtx *ctx, int64_t cursor_index)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(ctx));
  CK(OB_NOT_NULL(ctx->allocator_));
  CK (OB_NOT_NULL(ctx->params_));
  CK (cursor_index >= 0 && cursor_index < ctx->params_->count());
  if (OB_SUCC(ret)) {
    ObObjParam &obj = ctx->params_->at(cursor_index);
    LOG_DEBUG("spi cursor init", K(cursor_index), K(obj), K(obj.is_null()));
    ObPLCursorInfo *cursor_info = NULL;
    if (obj.is_null()) {
      OZ (spi_cursor_alloc(*ctx->get_top_expr_allocator(), obj));
    } else {
      CK (obj.is_pl_extend());
      if (OB_SUCC(ret)
          && obj.get_ext() != 0
          && OB_NOT_NULL(cursor_info = reinterpret_cast<ObPLCursorInfo *>(obj.get_ext()))) {
        OX (cursor_info->reset());
      }
    }
  }
  return ret;
}

int ObSPIService::cursor_open_check(ObPLExecCtx *ctx,
                                        int64_t package_id,
                                        int64_t routine_id,
                                        int64_t cursor_index,
                                        ObPLCursorInfo *&cursor,
                                        ObObjParam &obj,
                                        ObCusorDeclareLoc loc)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ctx, package_id, routine_id, cursor_index, obj, loc);
  if (OB_ISNULL(cursor)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("cursor is not initialized", K(ret), K(lbt()));
  }
  return ret;
}

int ObSPIService::spi_get_package_cursor_info(ObPLExecCtx *ctx,
                                              uint64_t package_id,
                                              uint64_t routine_id,
                                              int64_t index,
                                              ObPLCursorInfo *&cursor,
                                              ObObjParam &param)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObMySQLProxy *sql_proxy = NULL;
  ObPL *pl_engine = NULL;
  share::schema::ObSchemaGetterGuard schema_guard;
  UNUSED(routine_id);
  cursor = NULL;
  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(GCTX.schema_service_));
  CK (OB_NOT_NULL(exec_ctx = ctx->exec_ctx_));
  CK (OB_NOT_NULL(session_info = exec_ctx->get_my_session()));
  CK (OB_NOT_NULL(sql_proxy = exec_ctx->get_sql_proxy()));
  CK (OB_NOT_NULL(pl_engine = exec_ctx->get_pl_engine()));
  OZ (GCTX.schema_service_->get_runtime_schema_guard(
                            schema_guard));
  ObPLPackageGuard package_guard{};
  OZ (package_guard.init());
  if (OB_SUCC(ret)) {
    ObObj value;
    ObPLPackageManager &pl_manager = pl_engine->get_package_manager();
    ObPLResolveCtx resolve_ctx(exec_ctx->get_allocator(),
                               *session_info,
                               schema_guard,
                               ctx->guard_ != NULL ? *(ctx->guard_) : package_guard,
                               *sql_proxy,
                               false);
    resolve_ctx.params_.plan_cache_ = exec_ctx->get_plan_cache();
    resolve_ctx.params_.pl_sql_runtime_ = exec_ctx->get_pl_sql_runtime();
    resolve_ctx.params_.pl_engine_ = exec_ctx->get_pl_engine();
    resolve_ctx.params_.srs_provider_ = exec_ctx->get_srs_provider();
    resolve_ctx.params_.lob_read_service_ = exec_ctx->get_lob_read_service();
    OZ (pl_manager.get_package_var_val(
      resolve_ctx, *exec_ctx, package_id, OB_INVALID_VERSION, OB_INVALID_VERSION, index, value));
    CK (value.is_ext() || value.is_null());
    OX (value.copy_value_or_obj(param,true));
    OX (cursor = value.is_ext() ? reinterpret_cast<ObPLCursorInfo*>(value.get_ext())
                                : reinterpret_cast<ObPLCursorInfo *>(NULL));
  }
  return ret;
}

int ObSPIService::spi_get_subprogram_cursor_info(ObPLExecCtx *ctx,
                                                 uint64_t package_id,
                                                 uint64_t routine_id,
                                                 int64_t index,
                                                 ObPLCursorInfo *&cursor,
                                                 ObObjParam &param)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = NULL;
  ObPLContext *context = NULL;
  ObObjParam result;
  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(session_info = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(context = session_info->get_pl_context()));
  OZ (context->get_subprogram_var_from_local(
    *session_info, package_id, routine_id, index, result));
  CK (result.is_ext() || result.is_null());
  // OX (param.copy_value_or_obj(result, true));
  OX (param = result);
  OX (cursor = result.is_ext() ? reinterpret_cast<ObPLCursorInfo *>(result.get_ext())
                                : reinterpret_cast<ObPLCursorInfo*>(NULL));
  return ret;
}

int ObSPIService::spi_set_subprogram_cursor_var(ObPLExecCtx *ctx,
                                                uint64_t package_id,
                                                uint64_t routine_id,
                                                int64_t index,
                                                ObObjParam &param)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = NULL;
  ObPLContext *context = NULL;
  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(session_info = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(context = session_info->get_pl_context()));
  OZ (context->set_subprogram_var_from_local(*session_info, package_id, routine_id, index, param));
  return ret;
}

int ObSPIService::spi_get_cursor_info(ObPLExecCtx *ctx,
                                      uint64_t package_id,
                                      uint64_t routine_id,
                                      int64_t index,
                                      ObPLCursorInfo *&cursor,
                                      ObObjParam &param,
                                      ObSPIService::ObCusorDeclareLoc &location)
{
  int ret = OB_SUCCESS;
  cursor = NULL;
  CK (OB_NOT_NULL(ctx) && ctx->valid());
  if (OB_FAIL(ret)) {
  } else if (package_id != OB_INVALID_ID && OB_INVALID_ID == routine_id) {
    OZ (spi_get_package_cursor_info(ctx, package_id, routine_id, index, cursor, param));
    OX (location = DECL_PKG);
  } else {
    if (ctx->func_->get_routine_id() == routine_id && ctx->func_->get_package_id() == package_id) {
      OZ (spi_get_cursor_info(ctx, index, cursor, param));
      OX (location = DECL_LOCAL);
    } else {
      OZ (spi_get_subprogram_cursor_info(ctx, package_id, routine_id, index, cursor, param),
        routine_id, package_id, index);
      OX (location = DECL_SUBPROG);
    }
  }
  return ret;
}

int ObSPIService::spi_get_cursor_info(ObPLExecCtx *ctx, int64_t index,
                                      ObPLCursorInfo *& cursor,
                                      ObObjParam &param)
{
  int ret = OB_SUCCESS;
  cursor = NULL;
  ObObjParam *obj;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->params_));
  if (OB_SUCC(ret) && (OB_UNLIKELY(index < 0 || index >= ctx->params_->count()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid cursor index", K(ret), K(index), K(ctx->params_->count()));
  }
  OX (obj = &(ctx->params_->at(index)));
  // If cursor has not been init, it is null type, for example, exception causing a change in the execution flow, or goto causing an execution flow change
  // Cause no open, directly go to fetch or close
  CK (obj->is_ext() || obj->is_null());
  OX (param = *obj);
  OX (cursor = obj->is_ext() ? reinterpret_cast<ObPLCursorInfo *>(obj->get_ext())
                         : reinterpret_cast<ObPLCursorInfo *>(NULL));
  return ret;
}

int ObSPIService::spi_dynamic_open(ObPLExecCtx *ctx,
                                  const int64_t sql_idx,
                                  const int64_t *sql_param_exprs_idx,
                                  int64_t sql_param_count,
                                  uint64_t package_id,
                                  uint64_t routine_id,
                                  int64_t cursor_index)
{
  int ret = OB_SUCCESS;
  ObSqlString sql_str;
  stmt::StmtType stmt_type = stmt::T_NONE;
  ObArenaAllocator allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_DYNAMIC_SQL_EXEC), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObString ps_sql;
  bool for_update = false;
  bool hidden_rowid = false;
  int64_t inner_into_cnt = 0;
  bool skip_locked = false;

  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->func_));
  CK (sql_idx != OB_INVALID_ID);
  CK (OB_LIKELY(0 <= sql_idx && sql_idx < ctx->func_->get_expressions().count()));

  OZ (prepare_dynamic(ctx,
                      ctx->func_->get_expressions().at(sql_idx),
                      allocator,
                      sql_param_count,
                      sql_str,
                      ps_sql,
                      stmt_type,
                      for_update,
                      hidden_rowid,
                      inner_into_cnt,
                      skip_locked));
  
  if (OB_SUCC(ret)) {
    const ObSqlExpression **sql_param_exprs = nullptr;
    ObArenaAllocator alloc("SpiTemp", OB_MALLOC_NORMAL_BLOCK_SIZE);
    
    MAKE_EXPR_BUFFER(alloc, sql_param_exprs_idx, sql_param_count, sql_param_exprs);

    OZ (spi_cursor_open(ctx,
                        sql_param_count > 0 ? ObString() : sql_str.string(),
                        ps_sql,
                        stmt_type,
                        for_update,
                        hidden_rowid,
                        sql_param_exprs,
                        sql_param_count,
                        package_id,
                        routine_id,
                        cursor_index,
                        NULL/*formal_param_idxs*/,
                        NULL/*actual_param_exprs*/,
                        0/*cursor_param_count*/,
                        skip_locked));
  }

  return ret;
}
int ObSPIService::open_server_cursor(ObPLExecCtx *pl_ctx,
                                     ObPLServerCursorInfo &cursor,
                                     int64_t max_result_rows)
{
  int ret = OB_SUCCESS;
  stmt::StmtType stmt_type = cursor.get_stmt_type();
  const ObString ps_sql = cursor.get_ps_sql();
  bool for_update = cursor.is_for_update();
  bool hidden_rowid = cursor.has_hidden_rowid();

  OV (OB_NOT_NULL(pl_ctx->exec_ctx_->get_my_session()));
  OV (ObStmt::is_select_stmt(stmt_type), OB_ERR_UNEXPECTED, K(stmt_type));
  OZ (open_server_cursor_impl(
          pl_ctx, cursor, ps_sql, stmt_type, for_update, hidden_rowid, max_result_rows),
      cursor);
  return ret;
}

int ObSPIService::prepare_cursor_parameters(ObPLExecCtx *ctx,
                                            ObSQLSessionInfo &session_info, 
                                            uint64_t package_id,
                                            uint64_t routine_id,
                                            ObCusorDeclareLoc loc,
                                            const int64_t *formal_param_idxs,
                                            const ObSqlExpression **actual_param_exprs,
                                            int64_t cursor_param_count)
{
  int ret = OB_SUCCESS;

  ObObjParam dummy_result;

  for (int64_t i = 0; OB_SUCC(ret) && i < cursor_param_count; ++i) {

    CK (OB_NOT_NULL(actual_param_exprs[i]));
    OX (dummy_result.reset());
    OX (dummy_result.ObObj::reset());
    OZ (spi_calc_expr(ctx, actual_param_exprs[i], OB_INVALID_INDEX, &dummy_result),
                K(i), K(cursor_param_count), KPC(actual_param_exprs[i]), K(dummy_result));

    if (OB_SUCC(ret) && dummy_result.is_pl_mock_default_param()) {
      int64_t idx = dummy_result.get_int();
      ObSqlExpression *actual_param_expr = NULL;
      dummy_result.reset();
      dummy_result.ObObj::reset();
      if (DECL_PKG == loc) {
        OZ (spi_calc_package_expr(ctx, package_id, idx, &dummy_result));
      } else {
        OZ (spi_calc_subprogram_expr(ctx, package_id, routine_id, idx, &dummy_result));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (DECL_PKG == loc) {
      OZ (spi_set_package_variable(ctx, package_id, formal_param_idxs[i], dummy_result));
    } else {
      OZ (ObPLContext::set_subprogram_var_from_local(
        session_info, package_id, routine_id, formal_param_idxs[i], dummy_result));
    }
  }

  return ret;
}

int ObSPIService::spi_cursor_open_with_param_idx(ObPLExecCtx *ctx,
                                  const ObString &sql,
                                  const ObString &ps_sql,
                                  int64_t type,
                                  bool for_update,
                                  bool has_hidden_rowid,
                                  const int64_t *sql_param_idx,
                                  int64_t sql_param_count,
                                  uint64_t package_id,
                                  uint64_t routine_id,
                                  int64_t cursor_index,
                                  const int64_t *formal_param_idxs,
                                  const int64_t *actual_param_idx,
                                  int64_t cursor_param_count,
                                  bool skip_locked)
{
  int ret = OB_SUCCESS;

  const ObSqlExpression **sql_param_exprs = nullptr;
  const ObSqlExpression **actual_param_exprs = nullptr;
  ObArenaAllocator alloc("SpiTemp", OB_MALLOC_NORMAL_BLOCK_SIZE);

  MAKE_EXPR_BUFFER(alloc, sql_param_idx, sql_param_count, sql_param_exprs);
  MAKE_EXPR_BUFFER(alloc, actual_param_idx, cursor_param_count,
                    actual_param_exprs);

  OZ (SMART_CALL(spi_cursor_open(ctx, sql, ps_sql, type, for_update, has_hidden_rowid,
                                 sql_param_exprs, sql_param_count, package_id, routine_id,
                                 cursor_index, formal_param_idxs, actual_param_exprs,
                                 cursor_param_count, skip_locked)));

  return ret;
}

int ObSPIService::streaming_cursor_open(ObPLExecCtx *ctx,
                                        ObPLCursorInfo &cursor,
                                        ObSQLSessionInfo &session_info,
                                        const ObString &sql,
                                        const ObString &ps_sql,
                                        int64_t type,
                                        void *params,
                                        int64_t sql_param_count,
                                        bool is_server_cursor,
                                        bool for_update,
                                        bool has_hidden_rowid,
                                        bool is_prepared_stmt_cursor)
{
  int ret = OB_SUCCESS;

  cursor.set_streaming();
  ObSPIResultSet *spi_result = NULL;
  ObString sql_copy;
  ObString ps_sql_copy;
  
  OZ (cursor.prepare_spi_result(ctx, spi_result));
  CK (OB_NOT_NULL(spi_result));
  CK (OB_NOT_NULL(spi_result->get_memory_ctx()));
  OZ (spi_result->start_cursor_stmt(ctx, static_cast<stmt::StmtType>(type), for_update));

  // in a streaming cursor, lifetime of ps_sql may be shorter than the cursor itself,
  // so we need to open it with a deep copy.
  OZ (ob_write_string(spi_result->get_allocator(), sql, sql_copy));
  OZ (ob_write_string(spi_result->get_allocator(), ps_sql, ps_sql_copy));

  if (OB_SUCC(ret)) {

    ObQueryRetryCtrl retry_ctrl;
    ObSPIExecEnvGuard env_guard(session_info, *spi_result, cursor.is_ps_cursor());

    do {
      {
        ObPLSubPLSqlTimeGuard guard(ctx);
        ObPLSPITraceIdGuard trace_id_guard(sql, ps_sql, session_info, ret);
        ObSPIRetryCtrlGuard retry_guard(retry_ctrl, *spi_result, session_info, ret);

        if (OB_FAIL(ret)) {
        } else if (is_server_cursor) {
          WITH_CONTEXT(cursor.get_cursor_entity()) {
            lib::ContextTLOptGuard guard(false);
            OZ (inner_open(ctx,
                           spi_result->get_memory_ctx()->get_arena_allocator(),
                           sql_copy,
                           ps_sql_copy,
                           type,
                           params,
                           sql_param_count,
                           nullptr,
                           0,
                           *spi_result,
                           spi_result->get_out_params(),
                           false, /*is_dynamic_sql*/
                           is_prepared_stmt_cursor /*is_cursor_params*/));
          }
        } else {
          OZ (inner_open(ctx,
                         spi_result->get_memory_ctx()->get_arena_allocator(),
                         sql_copy,
                         ps_sql_copy,
                         type,
                         params,
                         sql_param_count,
                         nullptr,
                         0,
                         *spi_result,
                         spi_result->get_out_params(),
                         false, /*is_dynamic_sql*/
                         is_prepared_stmt_cursor /*is_cursor_params*/));
        }
        CK (OB_NOT_NULL(spi_result->get_result_set()->get_field_columns()));
        if (OB_SUCC(ret) && is_prepared_stmt_cursor) {
          ObPLServerCursorInfo &server_cursor = static_cast<ObPLServerCursorInfo&>(cursor);
          if (server_cursor.get_field_columns().empty()) {
            const common::ColumnsFieldArray* field_column =
              dynamic_cast<const common::ColumnsFieldArray *>
                (spi_result->get_result_set()->get_field_columns());
            CK (OB_NOT_NULL(field_column));
            OX (server_cursor.get_field_columns().set_allocator(
                &server_cursor.get_sql_entity()->get_arena_allocator()));
            OZ (server_cursor.get_field_columns().assign(*field_column));
          }
        }
        if (OB_SUCC(ret) && OB_INVALID_ID != cursor.get_id()) {
          // If it is a client cursor, set the result set to binary mode
          OX (spi_result->get_result_set()->set_ps_protocol());
        }
        OX (cursor.open(spi_result));
        OX (for_update ? cursor.set_for_update() : (void)NULL);
        OX (for_update ? cursor.set_trans_id(session_info.get_tx_id()) : (void)NULL);
        OX (has_hidden_rowid ? cursor.set_hidden_rowid() : (void)NULL);
        OZ (setup_cursor_snapshot_verify_(&cursor, spi_result));
        if (!cursor.is_ps_cursor()) {
          retry_guard.test();
        }
        if (OB_FAIL(ret)) {
          int close_ret = spi_result->close_result_set();
          if (OB_SUCCESS != close_ret) {
            LOG_WARN("close mysql result set failed", K(ret), K(close_ret));
          }
        }
      }
    } while (RETRY_TYPE_NONE != retry_ctrl.get_retry_type());
    spi_result->end_cursor_stmt(ctx, ret);
    cursor.set_last_execute_time(ObTimeUtility::current_time());
  }
  if (OB_SUCC(ret) && cursor.isopen() && !cursor.is_server_cursor()) {  //non_session cursor opened need to add into non session cursor map
    int add_ret = session_info.add_non_session_cursor(&cursor);
    if (OB_SUCCESS != add_ret) {
      LOG_WARN("add non session cursor failed", K(ret), K(add_ret));
    }
  }
  // if cursor already open, spi_result will released by cursor close.
  // there is not a situation like '!cursor.is_open && spi_result not close'.
  // so do not need call spi_result.close_result_set.
  if (OB_FAIL(ret) && OB_NOT_NULL(spi_result) && !cursor.isopen()) {
    spi_result->~ObSPIResultSet();
  }
  return ret;
}

int ObSPIService::save_unstreaming_cursor_sql(ObPLCursorInfo &cursor, const ObString &sql_text)
{
  int ret = OB_SUCCESS;
  if (!cursor.is_server_cursor()) { //only non session cursor need to save sql text
    CK (cursor.get_allocator() != NULL);
    OZ (ob_write_string(*cursor.get_allocator(), sql_text, cursor.get_sql_text()));
  }
  return ret;
}

int ObSPIService::unstreaming_cursor_open(ObPLExecCtx *ctx,
                                          ObPLCursorInfo &cursor,
                                          ObSQLSessionInfo &session_info,
                                          const ObString &sql,
                                          const ObString &ps_sql,
                                          int64_t type,
                                          void *params,
                                          int64_t sql_param_count,
                                          bool is_server_cursor,
                                          bool for_update,
                                          bool has_hidden_rowid,
                                          bool is_prepared_stmt_cursor,
                                          int64_t max_result_rows)
{
  int ret = OB_SUCCESS;

  cursor.set_unstreaming();
  HEAP_VAR(ObSPIResultSet, spi_result) {
    ObSPICursor* spi_cursor = NULL;
    CK (OB_NOT_NULL(ctx->exec_ctx_->get_plan_cache_access_service()));
    OZ (spi_result.init(
        session_info, *ctx->exec_ctx_->get_plan_cache_access_service()));
    OZ (spi_result.start_nested_stmt_if_need(ctx, sql, static_cast<stmt::StmtType>(type), for_update));
    OZ (save_unstreaming_cursor_sql(cursor, sql.empty() ? ps_sql : sql));

    if (OB_SUCC(ret)) {

      ObQueryRetryCtrl retry_ctrl;
      ObSPIExecEnvGuard env_guard(session_info, spi_result, cursor.is_ps_cursor());

      do {
        ret = OB_SUCCESS;
        uint64_t size = 0;
        {
          ObPLSubPLSqlTimeGuard guard(ctx);
          ObPLSPITraceIdGuard trace_id_guard(sql, ps_sql, session_info, ret);
          ObSPIRetryCtrlGuard retry_guard(retry_ctrl, spi_result, session_info, ret);

          CK (OB_NOT_NULL(spi_result.get_memory_ctx()));
          OZ (inner_open(ctx,
                         spi_result.get_memory_ctx()->get_arena_allocator(),
                         sql,
                         ps_sql,
                         type,
                         params,
                         sql_param_count,
                         NULL,
                         0,
                         spi_result,
                         spi_result.get_out_params(),
                         false, /*is_dynamic_sql*/
                         is_prepared_stmt_cursor /*is_cursor_params*/));
          OZ (session_info.get_tmp_table_size(size));
          OZ (cursor.prepare_spi_cursor(spi_cursor,
                                        size,
                                        (for_update && !is_server_cursor && !is_prepared_stmt_cursor),
                                        &session_info), K(size));
          CK (OB_NOT_NULL(spi_result.get_result_set()));
          OZ (fill_cursor(*spi_result.get_result_set(), spi_cursor, ObTimeUtility::current_time(), max_result_rows));
          OZ (spi_cursor->row_store_.finish_add_row());
          if (OB_FAIL(ret)) {
          } else if (is_prepared_stmt_cursor) {
            ObPLServerCursorInfo &server_cursor = static_cast<ObPLServerCursorInfo&>(cursor);
            OZ (ObPLServerCursorInfo::deep_copy_field_columns(
              server_cursor.get_sql_entity()->get_arena_allocator(),
              spi_result.get_result_set()->get_field_columns(),
              server_cursor.get_field_columns()));
          } else {
            CK (OB_NOT_NULL(cursor.get_allocator()));
            OZ (ObPLServerCursorInfo::deep_copy_field_columns(
              *cursor.get_allocator(), spi_result.get_result_set()->get_field_columns(), spi_cursor->fields_));
          }
          OX (MEMCPY(cursor.get_sql_id(), spi_result.get_sql_ctx().sql_id_, common::OB_MAX_SQL_ID_LENGTH + 1));
          if (OB_SUCC(ret) && OB_NOT_NULL(spi_result.get_result_set()) && OB_NOT_NULL(spi_result.get_result_set()->get_physical_plan())) {
            cursor.set_packed(spi_result.get_result_set()->get_physical_plan()->is_packed());
          }
          OX (cursor.open(spi_cursor));
          OX (for_update ? cursor.set_for_update() : (void)NULL);
          OX (for_update ? cursor.set_trans_id(session_info.get_tx_id()) : (void)NULL);
          OX (has_hidden_rowid ? cursor.set_hidden_rowid() : (void)NULL);
          if (!cursor.is_ps_cursor()) {
            retry_guard.test();
          }
          int close_ret = spi_result.close_result_set();
          if (OB_SUCCESS != close_ret) {
            LOG_WARN("close mysql result failed", K(ret), K(close_ret));
          }
          ret = (OB_SUCCESS == ret ? close_ret : ret);
        }
        spi_result.destruct_exec_params(session_info);

      } while (RETRY_TYPE_NONE != retry_ctrl.get_retry_type());
    }
    if (OB_SUCC(ret) && cursor.isopen() && !cursor.is_server_cursor()) {  //non_session cursor opened need to add into non session cursor map
      int add_ret = session_info.add_non_session_cursor(&cursor);
      if (OB_SUCCESS != add_ret) {
        LOG_WARN("add non session cursor failed", K(ret), K(add_ret));
      }
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(spi_cursor)) {
      spi_cursor->~ObSPICursor();
    }
    spi_result.end_nested_stmt_if_need(ctx, ret);
  }
  return ret;
}

int ObSPIService::spi_cursor_open(ObPLExecCtx *ctx,
                                  const ObString &sql,
                                  const ObString &ps_sql,
                                  int64_t type,
                                  bool for_update,
                                  bool has_hidden_rowid,
                                  const ObSqlExpression **sql_param_exprs,
                                  int64_t sql_param_count,
                                  uint64_t package_id,
                                  uint64_t routine_id,
                                  int64_t cursor_index,
                                  const int64_t *formal_param_idxs,
                                  const ObSqlExpression **actual_param_exprs,
                                  int64_t cursor_param_count,
                                  bool skip_locked)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = NULL;
  ObPLCursorInfo *cursor = NULL;
  ObObjParam cursor_var;
  ObCusorDeclareLoc loc;
  if (OB_ISNULL(ctx)
     || OB_ISNULL(ctx->exec_ctx_)
     || OB_ISNULL(session_info = ctx->exec_ctx_->get_my_session())
     || (sql_param_count > 0 && NULL == sql_param_exprs)
     || (!sql.empty() && sql_param_count > 0)
     || (sql.empty() && 0 == sql_param_count)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(ctx), K(sql), K(ps_sql), K(type), K(sql_param_exprs), K(sql_param_count), K(ret));
  } else if (OB_FAIL(spi_get_cursor_info(ctx, package_id, routine_id, cursor_index, cursor, cursor_var, loc))) {
    LOG_WARN("failed to get cursor info", K(ret), K(cursor_index));
  } else if (OB_FAIL(cursor_open_check(ctx, package_id, routine_id, cursor_index, cursor, cursor_var, loc))) {
    LOG_WARN("cursor info not init", K(ret), K(cursor));
  } else if (cursor->isopen()) {
    ret = OB_ER_SP_CURSOR_ALREADY_OPEN;
    LOG_USER_ERROR(OB_ER_SP_CURSOR_ALREADY_OPEN);
    LOG_WARN("Cursor is already open",
      K(ret), KPC(cursor), K(package_id), K(routine_id), K(cursor_index), K(cursor_var), K(loc));
  } else if (stmt::T_SELECT != static_cast<stmt::StmtType>(type)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only supported select stmt in cursor", K(ret), K(type));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "non-select stmt in cursor");
  } else {
    ParamStore current_params(ObWrapperAllocator(ctx->allocator_));
    bool need_restore_params = false;
    ObPLSubPLSqlTimeGuard guard(ctx);
    if (cursor_param_count > 0 && (NULL == formal_param_idxs || NULL == actual_param_exprs)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cursor params in not valid",
               K(cursor_param_count), K(formal_param_idxs), K(actual_param_exprs), K(ret));
    } else {
      OZ (prepare_cursor_parameters(
        ctx, *session_info, package_id,
        routine_id, loc, formal_param_idxs, actual_param_exprs, cursor_param_count));
    }

    if (OB_SUCC(ret) && DECL_SUBPROG == loc) {
      ParamStore *subprog_params = NULL;
      OZ (current_params.assign(*ctx->params_));
      OZ (ObPLContext::get_param_store_from_local(*session_info, package_id, routine_id, subprog_params));
      CK (OB_NOT_NULL(subprog_params));
      OZ (ctx->params_->assign(*subprog_params));
      OX (need_restore_params = true);
    }

    bool is_server_cursor = false;
    bool use_stream = false;
    if (OB_SUCC(ret)) {
      is_server_cursor = OB_INVALID_ID != cursor->get_id()
        || (package_id != OB_INVALID_ID && OB_INVALID_ID == routine_id);
      if (is_server_cursor) {
        OZ (ObPLCursorInfo::prepare_entity(*session_info, cursor->get_cursor_entity()));
        OX (cursor->set_spi_cursor(NULL));
      }
    }
    OZ (session_info->ps_use_stream_result_set(use_stream));

    if (OB_FAIL(ret)) {
    } else { // MySQL cursor, updated cursor, and package cursor use an unstreaming result.
      OZ (unstreaming_cursor_open(ctx,
                                  *cursor,
                                  *session_info,
                                  sql,
                                  ps_sql,
                                  type,
                                  sql_param_exprs,
                                  sql_param_count,
                                  is_server_cursor,
                                  for_update,
                                  has_hidden_rowid));
    }
    if (need_restore_params) {
      int ret = OB_SUCCESS;
      OZ (ctx->params_->assign(current_params));
    }
    if (OB_SUCC(ret) && DECL_PKG == loc) {
      OX (ctx->exec_ctx_->get_my_session()->set_pl_can_retry(false));
    }
  }
  if (OB_FAIL(ret)) {
    ctx->exec_ctx_->get_my_session()->set_show_warnings_buf(ret);
  }
  SET_SPI_STATUS;
  return ret;
}

int ObSPIService::open_server_cursor_impl(ObPLExecCtx *ctx,
                                          ObPLServerCursorInfo &cursor,
                                          const ObString &ps_sql,
                                          int64_t stmt_type,
                                          bool for_update,
                                          bool hidden_rowid,
                                          int64_t max_result_rows)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  ParamStore &exec_params = cursor.get_exec_params();
  ObString sql_str;
  bool use_stream = false;
  ObPLSubPLSqlTimeGuard guard(ctx);

  OV (OB_NOT_NULL(ctx) &&
      OB_NOT_NULL(ctx->exec_ctx_) &&
      OB_NOT_NULL(ctx->allocator_) &&
      OB_NOT_NULL(session = ctx->exec_ctx_->get_my_session()),
      OB_INVALID_ARGUMENT, KP(ctx), K(ps_sql), K(stmt_type));

  OZ (session->ps_use_stream_result_set(use_stream));

  if (OB_SUCC(ret) && cursor.isopen()) {
    OZ (close_server_cursor(*ctx->exec_ctx_, cursor), K(cursor.get_id()));
    OZ (ObPLCursorInfo::prepare_entity(*session, cursor.get_cursor_entity()));
    OX (cursor.set_spi_cursor(NULL));
  }

  if (OB_FAIL(ret)) {
  } else if (!for_update && use_stream) { // streaming branch
    OZ (streaming_cursor_open(ctx,
                              cursor,
                              *session,
                              sql_str,
                              ps_sql,
                              stmt_type,
                              &exec_params,
                              exec_params.count(),
                              true, /*is_server_cursor*/
                              for_update,
                              hidden_rowid,
                              true /*is_prepared_stmt_cursor*/));
  } else { // unstreaming branch
    OZ (unstreaming_cursor_open(ctx,
                                cursor,
                                *session,
                                sql_str,
                                ps_sql,
                                stmt_type,
                                &exec_params,
                                exec_params.count(),
                                true, /*is_server_cursor*/
                                for_update,
                                hidden_rowid,
                                true, /*is_prepared_stmt_cursor*/
                                max_result_rows));
  }
  SET_SPI_STATUS;
  return ret;
}

int ObSPIService::do_cursor_fetch(ObPLExecCtx *ctx,
                                  ObPLCursorInfo *cursor,
                                  bool is_server_cursor,
                                  const ObSqlExpression **into_exprs,
                                  int64_t into_count,
                                  const ObDataType *column_types,
                                  int64_t type_count,
                                  const bool *exprs_not_null_flag,
                                  const int64_t *pl_integer_ranges,
                                  const ObDataType *return_types,
                                  int64_t return_type_count,
                                  bool is_type_record)
{
  int ret = OB_SUCCESS;

  ObSPIResultSet *spi_result = NULL;
  ObSQLSessionInfo *session = NULL;

  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(session = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(cursor));

  if (OB_FAIL(ret)) {
  } else if (!cursor->isopen()) {
    ret = OB_ER_SP_CURSOR_NOT_OPEN;
    LOG_USER_ERROR(OB_ER_SP_CURSOR_NOT_OPEN);
    LOG_WARN("Cursor is not open", K(cursor), K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (cursor->is_streaming()) {
    CK (OB_NOT_NULL(spi_result = cursor->get_cursor_handler()));
    OZ (spi_result->set_cursor_env(*ctx->exec_ctx_->get_my_session()));
    OZ (adjust_out_params(ctx, into_exprs, into_count, spi_result->get_out_params()));
  } else if (OB_NOT_NULL(cursor->get_spi_cursor())
              && cursor->get_spi_cursor()->row_store_.get_row_cnt() > 0
              && cursor->get_current_row().is_invalid()) { // Data is needed to be processed, avoid duplicate processing
    // Only cursors cached in ObRowStore need to initialize the ObNewRow structure
    CK (OB_NOT_NULL(cursor->get_spi_cursor()));
    int64_t column_count = cursor->get_spi_cursor()->row_desc_.count();
    ObIAllocator *spi_allocator = NULL;
    OX (spi_allocator = NULL == cursor->get_cursor_entity()
                                  ? cursor->get_allocator()
                                  : &cursor->get_cursor_entity()->get_arena_allocator());
    CK (OB_NOT_NULL(spi_allocator));
    if (OB_SUCC(ret)) {
      void *ptr = spi_allocator->alloc(column_count * sizeof(ObObj));
      if (OB_ISNULL(ptr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc memory for row failed", "size", column_count * sizeof(ObObj));
      } else {
        cursor->get_current_row().cells_ = new(ptr) common::ObObj[column_count];
        cursor->get_current_row().count_ = column_count;
      }
    }
  }

  if (OB_SUCC(ret)) {
    int64_t row_count = 0;

#define GET_RESULT                                                                    \
  do {                                                                                \
    if (OB_FAIL(ret)) {                                                               \
    } else if (!cursor->is_streaming()) {                                             \
      bool can_retry = true;                                                          \
      ret = get_result(ctx,                                                           \
                   static_cast<void*>((cursor)->get_spi_cursor()),                    \
                   false,                                                             \
                   into_exprs,                                                        \
                   into_count,                                                        \
                   column_types,                                                      \
                   type_count,                                                        \
                   exprs_not_null_flag,                                               \
                   pl_integer_ranges,                                                 \
                   row_count,                                                         \
                   cursor->get_current_row(),                                         \
                   can_retry,                                                         \
                   cursor->has_hidden_rowid(),                                        \
                   true/*for_cursor*/,                                                \
                   return_types,                                                      \
                   return_type_count,                                                 \
                   is_type_record);                                                   \
    } else {                                                                          \
      ObString sql;                                                                   \
      ObString ps_sql;                                                                \
      if (spi_result->get_sql_ctx().is_prepare_protocol_) {                           \
        ps_sql = spi_result->get_sql_ctx().cur_sql_;                                  \
      } else {                                                                        \
        sql = spi_result->get_sql_ctx().cur_sql_;                                     \
      }                                                                               \
      ObQueryRetryCtrl retry_ctrl;                                                    \
      ObPLSPITraceIdGuard trace_id_guard(sql, ps_sql, *session, ret, cursor->get_sql_trace_id()); \
      ObPLSubPLSqlTimeGuard guard(ctx);                                               \
      if (cursor->get_sql_trace_id()->is_invalid()                                    \
            && OB_NOT_NULL(ObCurTraceId::get_trace_id())) {                           \
        cursor->get_sql_trace_id()->set(*ObCurTraceId::get_trace_id());               \
      }                                                                               \
      ret = inner_fetch_with_retry(ctx,                                               \
                   *cursor->get_cursor_handler(),                                     \
                   into_exprs,                                                        \
                   into_count,                                                        \
                   column_types,                                                      \
                   type_count,                                                        \
                   exprs_not_null_flag,                                               \
                   pl_integer_ranges,                                                 \
                   row_count,                                                         \
                   cursor->get_current_row(),                                         \
                   cursor->has_hidden_rowid(),                                        \
                   true,                                                              \
                   cursor->get_last_execute_time(),                                   \
                   return_types,                                                      \
                   return_type_count,                                                 \
                   is_type_record);                                                   \
    }                                                                                 \
  } while(0)

    if (is_server_cursor) {
      lib::ContextTLOptGuard guard(false);
      GET_RESULT;
    } else {
      GET_RESULT;
    }

#undef GET_RESULT

    if (cursor->is_streaming()) {
      spi_result->end_cursor_stmt(ctx, ret);
      cursor->set_last_execute_time(ObTimeUtility::current_time());
    }
    // Streaming cursors swallow READ_NOTHING errors; print WARN only when needed
    // to avoid excessive invalid logs.
    if (OB_SUCC(ret)) {
      cursor->set_fetched();
      cursor->set_fetched_with_row(ret != OB_READ_NOTHING);
      cursor->set_rowcount(cursor->get_rowcount() + row_count);
    } else {
      LOG_WARN("failed to spi cursor fetch", K(ret));
    }
  }

  return ret;
}

int ObSPIService::fetch_server_cursor(ObPLExecCtx *ctx, ObPLServerCursorInfo &cursor)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(do_cursor_fetch(ctx,
                              &cursor,
                              true,
                              NULL,
                              0,
                              NULL,
                              0,
                              NULL,
                              NULL))) {
    if (ret != OB_READ_NOTHING) {
      LOG_WARN("failed to fetch prepared-statement server cursor", K(ret), K(cursor));
    }
  }
  return ret;
}

int ObSPIService::spi_cursor_fetch(ObPLExecCtx *ctx,
                                   uint64_t package_id,
                                   uint64_t routine_id,
                                   int64_t cursor_index,
                                   const int64_t *into_exprs_idx,
                                   int64_t into_count,
                                   const ObDataType *column_types,
                                   int64_t type_count,
                                   const bool *exprs_not_null_flag,
                                   const int64_t *pl_integer_ranges,
                                   const ObDataType *return_types,
                                   int64_t return_type_count,
                                   bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObPLCursorInfo *cursor = NULL;
  ObObjParam cur_var;
  ObCusorDeclareLoc loc;
  ObArenaAllocator alloc("SpiTemp", OB_MALLOC_NORMAL_BLOCK_SIZE);
  const ObSqlExpression **into_exprs = nullptr;
  MAKE_EXPR_BUFFER(alloc, into_exprs_idx, into_count, into_exprs);
  OZ (spi_get_cursor_info(ctx, package_id, routine_id, cursor_index, cursor, cur_var, loc));
  if (OB_SUCC(ret) && OB_ISNULL(cursor)) {
      ret = OB_ERR_INVALID_CURSOR;
      LOG_WARN("cursor is null", K(ret));
  }
  OZ (do_cursor_fetch(ctx,
                      cursor,
                      OB_INVALID_ID != cursor->get_id()
                        || (package_id != OB_INVALID_ID && OB_INVALID_ID == routine_id),
                      into_exprs,
                      into_count,
                      column_types,
                      type_count,
                      exprs_not_null_flag,
                      pl_integer_ranges,
                      return_types,
                      return_type_count,
                      is_type_record));

  if (OB_FAIL(ret)) {
    ctx->exec_ctx_->get_my_session()->set_show_warnings_buf(ret);
  }

  {
    SET_SPI_STATUS;
  }
  return ret;
}

int ObSPIService::cursor_close_impl(ObSQLSessionInfo *session,
                                        ObPLCursorInfo *cursor,
                                        uint64_t package_id,
                                        uint64_t routine_id,
                                        bool ignore)
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("cursor close", K(cursor), KPC(cursor));
  if (OB_ISNULL(cursor)) {
    if (!ignore) {
      ret = OB_ERR_INVALID_CURSOR;
      LOG_WARN("close null cursor", K(package_id), K(routine_id), K(ret));
    }
  } else {
    bool is_server_cursor = OB_INVALID_ID != cursor->get_id()
      || (package_id != OB_INVALID_ID && OB_INVALID_ID == routine_id);
    if (!cursor->isopen()) {
      if (!ignore) {
        ret = OB_ER_SP_CURSOR_NOT_OPEN;
        LOG_USER_ERROR(OB_ER_SP_CURSOR_NOT_OPEN);
        LOG_WARN("Cursor is not open", K(cursor), K(ret));
      }
      is_server_cursor ? cursor->reuse() : cursor->reset();
    } else {
      CK (OB_NOT_NULL(session))
      OZ (cursor->close(*session, is_server_cursor));
    }
  }
  return ret;
}

int ObSPIService::spi_cursor_close(ObPLExecCtx *ctx,
                                   uint64_t package_id,
                                   uint64_t routine_id,
                                   int64_t cursor_index,
                                   bool ignore)
{
  int ret = OB_SUCCESS;
  ObPLCursorInfo *cursor = nullptr;
  ObObjParam cur_var;
  ObCusorDeclareLoc loc;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(ctx->params_));
  OZ (spi_get_cursor_info(ctx, package_id, routine_id, cursor_index, cursor, cur_var, loc),
      package_id, routine_id, cursor_index, cur_var);
  if (OB_NOT_NULL(cursor)) {
  }
  OZ (cursor_close_impl(ctx->exec_ctx_->get_my_session(), cursor, package_id, routine_id, ignore),
      package_id, routine_id, cursor_index, cur_var);
  if (OB_SUCC(ret) && DECL_PKG == loc) {
    OX (ctx->exec_ctx_->get_my_session()->set_pl_can_retry(false));
  }
  return ret;
}

int ObSPIService::close_server_cursor(ObExecContext &exec_ctx, ObPLCursorInfo &cursor)
{
  int ret = OB_SUCCESS;
  // The server cursor may own a prepared entity before its result set is opened.
  if (cursor.isopen()) {
    OV (OB_NOT_NULL(exec_ctx.get_my_session()));
    OZ (cursor.close(*exec_ctx.get_my_session(),
                     OB_NOT_NULL(cursor.get_cursor_entity()) ? true : false));
  }
  return ret;
}

int ObSPIService::spi_adjust_error_trace(pl::ObPLExecCtx *ctx, int level)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ctx, level);
  return ret;
}

int ObSPIService::spi_set_pl_exception_code(
  pl::ObPLExecCtx *ctx, int64_t code, bool is_pop_warning_buf, int level)
{
  int ret = OB_SUCCESS;
  ObPLContext *pl_ctx = NULL;
  ObPLSqlCodeInfo *sqlcode_info = NULL;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(pl_ctx = ctx->exec_ctx_->get_my_session()->get_pl_context()));
  CK (OB_NOT_NULL(sqlcode_info = ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info()));
  if (OB_SUCC(ret) && code != sqlcode_info->get_sqlcode()) {
    OX (sqlcode_info->set_sqlcode(code));
  }
  if (is_pop_warning_buf && sqlcode_info->get_stack_warning_buf().count() > 0) {
    int64_t idx = sqlcode_info->get_stack_warning_buf().count() - 1;
    OX (sqlcode_info->get_stack_warning_buf().at(idx).~ObWarningBuffer());
    OX (sqlcode_info->get_stack_warning_buf().pop_back());
    OX (ctx->exec_ctx_->get_my_session()->set_show_warnings_buf(OB_SUCCESS));
  }


  return ret;
}

int ObSPIService::spi_get_pl_exception_code(pl::ObPLExecCtx *ctx, int64_t *code)
{
  int ret = OB_SUCCESS;
  ObPLSqlCodeInfo *sqlcode_info = NULL;
  ObWarningBuffer *wb = NULL;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(sqlcode_info = ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info()));
  CK (OB_NOT_NULL(code));
  OX (*code = sqlcode_info->get_sqlcode());
  if (OB_NOT_NULL(wb = common::ob_get_tsi_warning_buffer())) {
    // Snapshot the current condition (errno + sqlstate + message) onto the diagnostic
    // stack BEFORE clearing the TSI buffer, so a bare RESIGNAL in the handler body can
    // recover the original sqlstate (e.g. 23000, not HY000); then reset the buffer so
    // the handler body starts clean. The sqlstate may have been wiped off the live buffer
    // during propagation (errno+message survive), so restore it from the persisted
    // ObPLSqlCodeInfo first -- otherwise the snapshot (and the bare RESIGNAL) falls back to
    // HY000.
    const char *live_ss = wb->get_sql_state();
    if ((OB_ISNULL(live_ss) || '\0' == live_ss[0]) && '\0' != sqlcode_info->get_sqlstate()[0]) {
      wb->set_sql_state(sqlcode_info->get_sqlstate());
    }
    OZ (sqlcode_info->get_stack_warning_buf().push_back(*wb));
    OX (wb->reset_warning());
  }
  return ret;
}

int ObSPIService::spi_check_exception_handler_legal(pl::ObPLExecCtx *ctx, int64_t code)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()->get_pl_context()));
  if (OB_SUCC(ret)
      && (ctx->exec_ctx_->get_my_session()->get_pl_context()->is_exception_handler_illegal()
          || OB_ERR_SP_EXCEPTION_HANDLE_ILLEGAL == code)) {
    ret = OB_ERR_SP_EXCEPTION_HANDLE_ILLEGAL;
    LOG_WARN("implementation restriction: exception handler in nested transaction is illegal",
             K(ret), K(ctx->exec_ctx_->get_my_session()->get_nested_count()),
             K(ctx->exec_ctx_->get_my_session()->get_pl_context()->is_exception_handler_illegal()));
    LOG_USER_ERROR(OB_ERR_SP_EXCEPTION_HANDLE_ILLEGAL);
  }
  return ret;
}

int ObSPIService::spi_check_early_exit(pl::ObPLExecCtx *ctx)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  OZ (spi_check_timeout(*ctx->exec_ctx_));
  return ret;
}

int ObSPIService::spi_check_timeout(sql::ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(exec_ctx.get_my_session()));

  if (OB_SUCC(ret)) {
    ObSQLSessionInfo *session_info = exec_ctx.get_my_session();
    if (OB_FAIL(session_info->check_session_status())) {
      LOG_WARN("spi check session not healthy", K(ret));
    } else if (session_info->is_in_transaction()
               && !data_plane::tx_desc_is_ended(session_info->get_tx_desc())
               && data_plane::tx_desc_is_timed_out(session_info->get_tx_desc())) {
      ret = OB_TRANS_TIMEOUT;
      LOG_WARN("spi check early exit, transaction timeout", K(ret));
    } else if (THIS_WORKER.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("spi check early exit, pl block timeout", K(ret));
    }
  }

  bool is_stack_overflow = false;
  OZ (check_stack_overflow(is_stack_overflow));
  if (OB_SUCC(ret) && is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("stack overflow in loop", K(is_stack_overflow), K(ret));
  }
  return ret;
}

int ObSPIService::spi_alloc_complex_var(pl::ObPLExecCtx *ctx,
                                        int64_t type,
                                        int64_t id,
                                        int64_t var_idx,
                                        int64_t init_size,
                                        int64_t *addr,
                                        ObIAllocator *alloc)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ctx, type, id, var_idx, init_size, addr);
  return ret;
}

int ObSPIService::spi_clear_diagnostic_area(pl::ObPLExecCtx *ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = ctx->exec_ctx_->get_my_session();

  CK (OB_NOT_NULL(session_info));
  OX (session_info->set_show_warnings_buf(OB_SUCCESS));

  return ret;
}

int ObSPIService::spi_construct_collection(
  pl::ObPLExecCtx *ctx, uint64_t package_id, ObObjParam *result)
{
  int ret = OB_SUCCESS;
  ObIAllocator *allocator = NULL;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(allocator = ctx->allocator_));
  CK (OB_NOT_NULL(result));
  if (OB_SUCC(ret) && package_id != OB_INVALID_ID) {
    OZ (spi_get_package_allocator(ctx, package_id, allocator));
    CK (OB_NOT_NULL(allocator));
  }
  if (OB_SUCC(ret)) {
    CK (result->is_ext());
    if (OB_SUCC(ret)) {
      void *ptr = NULL;
      ObPLCollection* coll = NULL;
      CK (OB_NOT_NULL(ptr = reinterpret_cast<void *>(result->get_ext())));
      CK (OB_NOT_NULL(coll = reinterpret_cast<ObPLCollection *>(ptr)));
      OZ (spi_set_collection(ctx,
                             *coll,
                             0,
                             false));
    }
  }
  return ret;
}

#define GET_INTEGER_FROM_OBJ(result, r)               \
CK (result.is_integer_type() || result.is_number());  \
if (OB_SUCC(ret)) {                                   \
  if (result.is_integer_type()) {                     \
    r = result.get_int();                             \
  } else if (result.is_number()) {                    \
    if (!result.get_number().is_valid_int64(r)) {     \
      number::ObNumber num = result.get_number();     \
      OZ (num.round(0));                              \
      if (OB_SUCC(ret) && !num.is_valid_int64(r)) {   \
        ret = OB_ARRAY_OUT_OF_RANGE;                  \
        LOG_WARN("wrong array index", K(ret));        \
      }                                               \
    }                                                 \
  }                                                   \
}

int ObSPIService::spi_process_resignal(pl::ObPLExecCtx *ctx,
                                       const int64_t errcode_expr_idx,
                                       const int64_t errmsg_expr_idx,
                                       const char *sql_state,
                                       int *error_code,
                                       const char *resignal_sql_state,
                                       bool is_signal)
{
  int ret = OB_SUCCESS;
  ObObjParam result;
  ObPLSqlCodeInfo *sqlcode_info = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObWarningBuffer *wb = NULL;
  ObPLConditionType type = ObPLConditionType::INVALID_TYPE;
  int cur_err_code = OB_SUCCESS;
  static const uint32_t STR_LEN = 128;
  char err_msg[STR_LEN] = {0};
  const ObSqlExpression *errcode_expr = nullptr;
  const ObSqlExpression *errmsg_expr = nullptr;

  CK (OB_NOT_NULL(ctx), ctx->valid());
  CK (OB_NOT_NULL(ctx->func_));
  CK (OB_NOT_NULL(session_info = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(sqlcode_info = ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info()));
  OX (wb = common::ob_get_tsi_warning_buffer());
  CK (OB_NOT_NULL(error_code));
  OX (GET_NULLABLE_EXPR_BY_IDX(ctx, errcode_expr_idx, errcode_expr));
  OX (GET_NULLABLE_EXPR_BY_IDX(ctx, errmsg_expr_idx, errmsg_expr));
  CK (OB_NOT_NULL(ctx->get_top_expr_allocator()));

#define CALC(expr, type, result) \
  do { \
    ObObjParam tmp; \
    ObExprResType expected_type; \
    OZ (spi_calc_expr(ctx, expr, OB_INVALID_INDEX, &tmp)); \
    OX (expected_type.set_##type()); \
    OX (expected_type.set_collation_type(tmp.get_collation_type())); \
    OZ (spi_convert(ctx->exec_ctx_->get_my_session(), ctx->get_top_expr_allocator(), tmp, expected_type, result, \
                    ctx->exec_ctx_->get_srs_provider(), ctx->exec_ctx_->get_lob_read_service())); \
  } while(0)

  if (OB_SUCC(ret)) {
    // A bare RESIGNAL carries no explicit sqlstate; classify by the caught condition's
    // sqlstate (resignal_sql_state) so a re-raised warning-class condition ('01000')
    // downgrades to a warning instead of re-raising as a generic exception.
    type = ObPLEH::eh_classify_exception(NULL != sql_state ? sql_state : resignal_sql_state);
    if (OB_NOT_NULL(wb)) {
      cur_err_code = wb->get_err_code();
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(errcode_expr)) {
    if (!is_signal && *error_code != -1 && NULL == sql_state) {
      sqlcode_info->set_sqlcode(*error_code);
    } else {
      if (type == ObPLConditionType::NOT_FOUND) {
        sqlcode_info->set_sqlcode(ER_SIGNAL_NOT_FOUND);
      } else {
        sqlcode_info->set_sqlcode(ER_SIGNAL_EXCEPTION);
      }
    }
  } else {
    CALC(errcode_expr, int32, result);
    if (OB_SUCC(ret)) {
      if (result.is_null()) {
        ret = OB_ERR_WRONG_VALUE_FOR_VAR;
        LOG_WARN("error code is NULL", K(ret));
        LOG_USER_ERROR(OB_ERR_WRONG_VALUE_FOR_VAR, static_cast<int32_t>(STRLEN("MYSQL_ERRNO")), "MYSQL_ERRNO",
                       static_cast<int32_t>(STRLEN("NULL")), "NULL");
      } else if (result.get_int32() <= 0 || result.get_int32() > 65535) {
        char mysql_errno[20] = {0};
        sprintf(mysql_errno, "%d", result.get_int32());
        ret = OB_ERR_WRONG_VALUE_FOR_VAR;
        LOG_WARN("error code is over 65535", K(ret), K(result.get_int32()));
        LOG_USER_ERROR(OB_ERR_WRONG_VALUE_FOR_VAR, static_cast<int32_t>(STRLEN("MYSQL_ERRNO")), "MYSQL_ERRNO",
                       static_cast<int32_t>(STRLEN(mysql_errno)), mysql_errno);
      }
    }
    OX (sqlcode_info->set_sqlcode(result.get_int32()));
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(errmsg_expr)) {
      if (is_signal) {
        if (type == ObPLConditionType::NOT_FOUND) {
          sqlcode_info->set_sqlmsg(ObString("Unhandled user-defined not found condition"));
        } else {
          sqlcode_info->set_sqlmsg(ObString("Unhandled user-defined exception condition"));
        }
      } else {
        int64_t idx;
        CK (sqlcode_info->get_stack_warning_buf().count() > 0);
        OX (idx = sqlcode_info->get_stack_warning_buf().count() - 1);
        OX (sqlcode_info->set_sqlmsg(sqlcode_info->get_stack_warning_buf().at(idx).get_err_msg()));
      }
    } else {
      CALC(errmsg_expr, varchar, result);
      if (OB_SUCC(ret)) {
        if (result.is_null()) {
          ret = OB_ERR_WRONG_VALUE_FOR_VAR;
          LOG_WARN("error code is NULL", K(ret));
          LOG_USER_ERROR(OB_ERR_WRONG_VALUE_FOR_VAR, static_cast<int32_t>(STRLEN("MESSAGE_TEXT")), "MESSAGE_TEXT",
                        static_cast<int32_t>(STRLEN("NULL")), "NULL");
        }
      }
      OX (sqlcode_info->set_sqlmsg(result.get_string()));
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    snprintf(err_msg, STR_LEN, "%.*s", sqlcode_info->get_sqlmsg().length(), sqlcode_info->get_sqlmsg().ptr());
    // A warning-class condition downgrades when it is a SIGNAL, an explicit `RESIGNAL SQLSTATE
    // '01xxx'` (the user pinned a warning sqlstate), or a *bare* RESIGNAL whose caught condition
    // was itself a warning. A bare RESIGNAL re-raises with the original severity, so an
    // error-severity warning-class sqlstate (a strict-mode 1265 with '01000' caught by a
    // SQLWARNING handler) re-raises as an error via the else branch instead of downgrading.
    if (ObPLConditionType::SQL_WARNING == type
        && (is_signal || OB_NOT_NULL(sql_state)
            || OB_ISNULL(sqlcode_info) || !sqlcode_info->is_caught_error())) {
      if (OB_ISNULL(errcode_expr)) {
        if (OB_NOT_NULL(wb)) {
          wb->append_warning(err_msg, OB_ERR_SIGNAL_WARN);
        }
      } else {
        if (OB_NOT_NULL(wb)) {
          wb->append_warning(err_msg, sqlcode_info->get_sqlcode(), sql_state);
        }
      }
      if (is_signal) {
        sqlcode_info->set_sqlcode(OB_SUCCESS);
        if (OB_NOT_NULL(wb)) {
          wb->reset_err();
        }
      } else {
        // A warning-class RESIGNAL downgrades the caught condition to a warning: clear the
        // pending error (sqlcode + *error_code) so the handler continues instead of re-raising.
        sqlcode_info->set_sqlcode(OB_SUCCESS);
        *error_code = OB_SUCCESS;
        if (OB_NOT_NULL(wb)) {
          wb->reset_err();
        }
      }
    } else {
      if (is_signal) {
        LOG_MYSQL_USER_ERROR(OB_ERR_SIGNAL_EXCEPTION,
                            MIN(sqlcode_info->get_sqlmsg().length(), STR_LEN),
                            sqlcode_info->get_sqlmsg().ptr());
        if (OB_NOT_NULL(wb)) {
          wb->set_error_code(sqlcode_info->get_sqlcode());
          wb->set_sql_state(sql_state);
        }
        // Persist the raised sqlstate: error propagation wipes it off the TSI buffer before
        // the handler / client reads it, while errno+message survive.
        sqlcode_info->set_sqlstate(sql_state);
      } else {
        if (OB_NOT_NULL(wb)) {
          wb->set_error(err_msg, sqlcode_info->get_sqlcode());
          wb->set_sql_state(sql_state != NULL
                             ? sql_state
                             : (resignal_sql_state != NULL) ? resignal_sql_state : ob_sqlstate(cur_err_code));
          sqlcode_info->set_sqlstate(wb->get_sql_state());  // persist for the next propagation
        }
      }
      *error_code = sqlcode_info->get_sqlcode();
    }
  }
  return ret;
}

#undef GET_INTEGER_FROM_OBJ
int ObSPIService::spi_destruct_collection(ObPLExecCtx *ctx, int64_t idx)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ctx, idx);
  return ret;
}

int ObSPIService::spi_sub_nestedtable(ObPLExecCtx *ctx,
                                      int64_t src_expr_idx,
                                      int64_t dst_coll_idx,
                                      int64_t index_idx,
                                      int32_t lower,
                                      int32_t upper)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ctx, src_expr_idx, dst_coll_idx, index_idx, lower, upper);
  return ret;
}


int ObSPIService::spi_set_collection(const ObPLINS *ns,
                                       ObPLCollection &coll,
                                       int64_t n,
                                       bool extend_mode)
{
  int ret = OB_SUCCESS;
  UNUSEDx(ns, coll, n, extend_mode);
  return ret;
}

int ObSPIService::spi_reset_composite(ObPLComposite *composite,
                                      bool is_record,
                                      int32_t member_idx)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(composite));
  if (OB_SUCC(ret)) {
    if (is_record) {
      ObObj obj;
      ObIAllocator *pl_allocator = nullptr;
      ObPLRecord *record = static_cast<ObPLRecord *>(composite);
      CK (member_idx >= 0 && member_idx < record->get_count());
      CK (OB_NOT_NULL(pl_allocator = record->get_allocator()));
      OZ (record->get_element(member_idx, obj));
      OZ (ObUserDefinedType::destruct_objparam(*pl_allocator, obj, nullptr));
    } else {
      /*
      * Assigning NULL to a Collection makes the collection uninitialized.
      *
      * Example:
      * CREATE OR REPLACE TYPE ARRYTYPE is table of Varchar2(10);
      * Type created.
      *
      * SQL>
      * declare TYPE ARRYTYPE is table of Varchar2(10);
      * arr ARRYTYPE := ARRYTYPE('a','b','c');
      * begin
      * dbms_output.put_line(arr.count || arr(3));
      * arr := NULL;
      * dbms_output.put_line(arr.count || arr(3));
      * end;
      * /
      *
      * declare TYPE ARRYTYPE is table of Varchar2(10);
      * ERROR at line 1:
      * Reference to uninitialized collection
      * at line 6
      * */
      ObPLCollection *coll = static_cast<ObPLCollection *>(composite);
      if (OB_NOT_NULL(coll->get_allocator())) {
        OZ (spi_set_collection(NULL, *coll, 0, false));
        OX (coll->set_count(OB_INVALID_COUNT));
      } else {
        CK (coll->get_count() == 0 || coll->get_count() == OB_INVALID_COUNT);
        OX (coll->set_count(OB_INVALID_COUNT));
      }
    }
  }
  return ret;
}


int ObSPIService::spi_get_package_allocator(
  ObPLExecCtx *ctx, uint64_t package_id, ObIAllocator *&allocator)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObMySQLProxy *sql_proxy = NULL;
  ObPL *pl_engine = NULL;
  share::schema::ObSchemaGetterGuard schema_guard;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(GCTX.schema_service_));
  CK (OB_NOT_NULL(exec_ctx = ctx->exec_ctx_));
  CK (OB_NOT_NULL(session_info = exec_ctx->get_my_session()));
  CK (OB_NOT_NULL(sql_proxy = exec_ctx->get_sql_proxy()));
  CK (OB_NOT_NULL(pl_engine = exec_ctx->get_pl_engine()));
  OZ (GCTX.schema_service_->get_runtime_schema_guard(
                            schema_guard));
  ObPLPackageGuard package_guard{};
  OZ (package_guard.init());
  OX (allocator = NULL);
  if (OB_SUCC(ret)) {
    ObPLPackageState *package_state = NULL;
    ObPLPackageManager &pl_manager = pl_engine->get_package_manager();
    ObPLResolveCtx resolve_ctx(exec_ctx->get_allocator(),
                               *session_info,
                               schema_guard,
                               ctx->guard_ != NULL ? *(ctx->guard_) : package_guard,
                               *sql_proxy,
                               false);
    resolve_ctx.params_.plan_cache_ = exec_ctx->get_plan_cache();
    resolve_ctx.params_.pl_sql_runtime_ = exec_ctx->get_pl_sql_runtime();
    resolve_ctx.params_.pl_engine_ = exec_ctx->get_pl_engine();
    resolve_ctx.params_.srs_provider_ = exec_ctx->get_srs_provider();
    resolve_ctx.params_.lob_read_service_ = exec_ctx->get_lob_read_service();
    OZ (pl_manager.get_package_state(
      resolve_ctx, *exec_ctx, package_id, package_state), package_id);
    CK (OB_NOT_NULL(package_state));
    OX (allocator = &package_state->get_pkg_allocator());
  }
  return ret;
}

int ObSPIService::spi_convert_anonymous_array(pl::ObPLExecCtx *ctx,
                                        ObObjParam *param, 
                                        uint64_t user_type_id)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  share::schema::ObSchemaGetterGuard *schema_guard = NULL;
  common::ObMySQLProxy *sql_proxy = NULL;
  ObPLPackageGuard *package_guard = NULL;
  const ObUserDefinedType *pl_user_type = NULL;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  CK (OB_NOT_NULL(param));
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(session = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(schema_guard = ctx->exec_ctx_->get_sql_ctx()->schema_guard_));
  CK (OB_NOT_NULL(sql_proxy = ctx->exec_ctx_->get_sql_proxy()));
  CK (OB_NOT_NULL(package_guard = ctx->exec_ctx_->get_package_guard()));
  // here must be dealing with the inout parameter type of the anonymous array
  CK (is_mocked_anonymous_array_id(user_type_id));
  CK (param->is_ext());
  // For inout anonymous array parameters, if the original type is not empty,
  // the result is returned according to the original type.
  if (OB_SUCC(ret)) {
    OZ (ctx->get_user_type(user_type_id, pl_user_type, nullptr));
    CK (OB_NOT_NULL(pl_user_type));
    if (OB_SUCC(ret)) {
      ObObj dst;
      int64_t dst_size = 0;
      ObObj *dst_ptr = &dst;
      ObObj *src_ptr = param;
      ObObj *src_table_data = nullptr;
      ObPLResolveCtx resolve_ctx(
        *ctx->allocator_, *session, *schema_guard, *package_guard, *sql_proxy, false);
      resolve_ctx.params_.plan_cache_ = ctx->exec_ctx_->get_plan_cache();
      resolve_ctx.params_.pl_sql_runtime_ = ctx->exec_ctx_->get_pl_sql_runtime();
      resolve_ctx.params_.pl_engine_ = ctx->exec_ctx_->get_pl_engine();
      resolve_ctx.params_.srs_provider_ = ctx->exec_ctx_->get_srs_provider();
      resolve_ctx.params_.lob_read_service_ = ctx->exec_ctx_->get_lob_read_service();
      OZ (pl_user_type->init_obj(*(schema_guard), tmp_alloc, dst, dst_size));
      OZ (pl_user_type->convert(resolve_ctx, src_ptr, dst_ptr));
      OZ (ObUserDefinedType::destruct_obj(*src_ptr, session, true));
      OZ (pl_user_type->convert(resolve_ctx, dst_ptr, src_ptr));
      OX (param->set_udt_id(pl_user_type->get_user_type_id()));
      int tmp_ret = ObUserDefinedType::destruct_obj(dst, session);
      ret = OB_SUCCESS == ret ? tmp_ret : ret;
    }
  }
  return ret;
} 

/*
 * Because it is possible that the caller calls this function to copy an item from a collection, at which point the passed-in parameter must be the allocator of the collection, so this parameter is necessary.
 * If it can be guaranteed that this function will not be used to copy ordinary data types, then it can be omitted.
 * */
int ObSPIService::spi_copy_datum(ObPLExecCtx *ctx,
                                 ObIAllocator *allocator,
                                 ObObj *src,
                                 ObObj *dest,
                                 ObDataType *dest_type,
                                 uint64_t package_id,
                                 uint64_t type_info_id)
{
  int ret = OB_SUCCESS;
  ObExprResType result_type;
  ObIAllocator *copy_allocator = NULL;
  ObObjParam result;
  ObObjParam src_tmp;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  CK (OB_NOT_NULL(src));
  CK (OB_NOT_NULL(dest));
  if (OB_SUCC(ret)) {
    if (NULL == allocator && OB_INVALID_ID != package_id) {
      OZ (spi_get_package_allocator(ctx, package_id, copy_allocator));
    } else {
      copy_allocator = NULL == allocator ? ctx->allocator_ : allocator;
    }
    CK (OB_NOT_NULL(copy_allocator));
    if (OB_FAIL(ret)) {
    } else if (src->is_null() || ObMaxType == src->get_type()) {
      //ObMaxTC means deleted element in Collection, no need to copy
      if (!dest->is_pl_extend()) {
        OZ (ObUserDefinedType::destruct_objparam(*copy_allocator, *dest));
      }
      OX (*dest = *src);
    } else if (PL_CURSOR_TYPE == src->get_meta().get_extend_type()) {
      ret = OB_ERR_EXPRESSION_WRONG_TYPE;
      LOG_WARN("cursor assignment is not supported", K(ret));
    } else if (src->is_ext() && OB_NOT_NULL(dest_type) && dest_type->get_meta_type().is_ext()) {
      OZ (ObPLComposite::copy_element(
          *src, *dest, *copy_allocator, ctx,
          ctx->exec_ctx_->get_my_session(), NULL,
          ctx->exec_ctx_->get_srs_provider(),
          ctx->exec_ctx_->get_lob_read_service(), true));
    } else if (OB_NOT_NULL(dest_type) && dest_type->get_meta_type().is_ext()) {
      ret = OB_ERR_EXPRESSION_WRONG_TYPE;
      LOG_WARN("src type and dest type is not match", K(ret));
    } else {
      CK (OB_NOT_NULL(ctx));
      CK (ctx->valid());
      CK (OB_NOT_NULL(dest_type));
      OX (result_type.set_meta(dest_type->get_meta_type()));
      OX (result_type.set_accuracy(dest_type->get_accuracy()));
      if (OB_SUCC(ret)) {
        if ((ObEnumType == src->get_type() && ObEnumType == result_type.get_type())
            || (ObSetType == src->get_type() && ObSetType == result_type.get_type())) {
          result = *src;
        } else {
          ObIArray<common::ObString>* type_info = NULL;
          if (OB_INVALID_ID != type_info_id) {
            OZ (ctx->func_->get_enum_set_ctx().get_enum_type_info(type_info_id, type_info));
            CK (OB_NOT_NULL(type_info));
          }
          OX (src_tmp = *src);
          OZ (spi_convert(
            ctx->exec_ctx_->get_my_session(), &tmp_alloc, src_tmp, result_type, result,
            ctx->exec_ctx_->get_srs_provider(), ctx->exec_ctx_->get_lob_read_service(), type_info),
            K(src_tmp), K(result_type),KPC(src));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (NULL != copy_allocator) {
        if (result.get_deep_copy_obj_ptr() != dest->get_deep_copy_obj_ptr()) {
          OZ (ObUserDefinedType::destruct_objparam(*copy_allocator, *dest));
          OZ (deep_copy_obj(*copy_allocator, result, *dest));
        } else {
          *dest = result;
        }
      }
    }
  }
  SET_SPI_STATUS;
  return ret;
}

int ObSPIService::spi_destruct_obj(ObPLExecCtx *ctx,
                                   ObObj *obj)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(obj));
  if (OB_SUCC(ret) &&
      obj->is_pl_extend() &&
      obj->get_ext() != 0) {
    OZ (ObUserDefinedType::destruct_obj(*obj, ctx->exec_ctx_->get_my_session()));
    OX (obj->set_null());
  }

  return ret;
}

int ObSPIService::spi_interface_impl(pl::ObPLExecCtx *ctx, const char *interface_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(interface_name)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Argument passed in is NULL", K(ctx), K(interface_name), K(ret));
  } else if (OB_ISNULL(ctx->exec_ctx_)
      || OB_ISNULL(ctx->params_)
      || OB_ISNULL(ctx->result_)
      || OB_ISNULL(ctx->exec_ctx_->get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid context", K(ctx->exec_ctx_), K(ctx->params_), K(ctx->result_));
  } else {
    ObString name(interface_name);
    pl::ObPL *pl_engine = ctx->exec_ctx_->get_pl_engine();
    PL_C_INTERFACE_t fp = OB_ISNULL(pl_engine)
        ? nullptr : pl_engine->get_interface_service().get_entry(name);
    if (nullptr != fp) {
      ParamStore exec_params( (ObWrapperAllocator(*ctx->get_top_expr_allocator())) );
      for (int64_t i = 0; OB_SUCC(ret) && i < ctx->params_->count(); ++i) {
        ObObjParam new_param = ctx->params_->at(i);
        if (ctx->params_->at(i).is_pl_extend()) {
          // do nothing
        } else if (ctx->params_->at(i).need_deep_copy()) {
          OZ (deep_copy_obj(*ctx->get_top_expr_allocator(), ctx->params_->at(i), new_param));
        }
        OZ (exec_params.push_back(new_param));
      }
      if (OB_SUCC(ret)) {
        ObIAllocator *old = ctx->allocator_;
        ctx->allocator_ = ctx->get_top_expr_allocator();
        ObObj tmp_result(ObMaxType);
        ret = fp(*ctx, exec_params, tmp_result);
        ctx->allocator_ = old;
        if (OB_FAIL(ret)) {
          if (tmp_result.is_pl_extend()) {
            ObUserDefinedType::destruct_obj(tmp_result, ctx->exec_ctx_->get_my_session());
          }
        } else {
          if (ctx->func_->get_ret_type().is_composite_type() ||
              ctx->func_->get_ret_type().is_obj_type() ||
              ctx->func_->get_ret_type().is_cursor_type()) {
            if (tmp_result.is_pl_extend()) {
              ObIAllocator *alloc = ctx->allocator_;
              OZ (ObUserDefinedType::deep_copy_obj(*alloc, tmp_result, *ctx->result_));
              ObUserDefinedType::destruct_obj(tmp_result, ctx->exec_ctx_->get_my_session());
            } else if (!tmp_result.is_ext() && tmp_result.need_deep_copy()) {
              OZ (deep_copy_obj(*ctx->allocator_, tmp_result, *ctx->result_));
            } else {
              *ctx->result_ = tmp_result;
            }
          }

          for (int64_t i = 0; OB_SUCC(ret) && i < ctx->func_->get_arg_count(); ++i) {
            if (ctx->func_->get_out_args().has_member(i)) {
              if (exec_params.at(i).is_pl_extend()) {
                ObIAllocator *alloc = ctx->allocator_;
                // Here the shell of complex data types must exist before executing the interface (init param), not constructed within the interface c interface
                OZ (ObUserDefinedType::deep_copy_obj(*alloc, exec_params.at(i), ctx->params_->at(i)));
                if (exec_params.at(i).get_ext() != ctx->params_->at(i).get_ext()) {
                  ObUserDefinedType::destruct_obj(exec_params.at(i), ctx->exec_ctx_->get_my_session());
                }
              } else if (!exec_params.at(i).is_ext()) {
                void *ptr = ctx->params_->at(i).get_deep_copy_obj_ptr();
                if (nullptr != ptr) {
                  ctx->allocator_->free(ptr);
                }
                ctx->params_->at(i).set_null();
                OZ (deep_copy_obj(*ctx->allocator_, exec_params.at(i), ctx->params_->at(i)));
              }
            }
          }
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Calling C interface which doesn't exist", K(interface_name), K(name));
    }
  }
  return ret;
}

int ObSPIService::adjust_out_params(
  ObResultSet &result_set, ObSPIOutParams &out_params)
{
  int ret = OB_SUCCESS;
  /*observer::ObInnerSQLResult* inner_result =
    reinterpret_cast<observer::ObInnerSQLResult*>(mysql_result.get_result());
  sql::ObPhysicalPlan *plan = NULL;
  if (OB_NOT_NULL(inner_result)
      && OB_NOT_NULL(plan = inner_result->result_set().get_physical_plan())) {
    // TODO:
    // Check if there are Function or Package in the dependent objects, as calls to these two types of dependent objects may have Out parameters;
    // Do not check if there are indeed Out parameters for now, temporarily assume that there are Out parameters;
    // Make it more detailed later;
    bool has_out_param = false;
    const ObIArray<ObSchemaObjVersion> &ref_objects = plan->get_dependency_table();
    for (int64_t i = 0; i < ref_objects.count(); ++i) {
      if (DEPENDENCY_PACKAGE == ref_objects.at(i).object_type_
          || DEPENDENCY_FUNCTION == ref_objects.at(i).object_type_) {
        has_out_param =true;
      }
    }
    out_params.set_has_out_param(has_out_param);
    LOG_DEBUG("debug for adjust_out_params", K(ret), K(out_params), K(ref_objects));
  }*/
  // Functions with OUT parameters cannot be used in SQL.
  UNUSED(result_set);
  out_params.set_has_out_param(false);
  return ret;
}

int ObSPIService::adjust_out_params(ObPLExecCtx *ctx,
                                    const ObSqlExpression **into_exprs,
                                    int64_t into_count,
                                    ObSPIOutParams &out_params)
{
  int ret = OB_SUCCESS;
  ObIArray<ObObj> &params = out_params.get_out_params();
  CK (OB_NOT_NULL(ctx));
  CK (into_count >= 0);
  for (int64_t i = 0;
      OB_SUCC(ret) && out_params.has_out_param() && i < params.count(); ++i) {
    if (params.at(i).is_ext() || params.at(i).is_unknown()) {
      bool exist = false;
      OZ (check_exist_in_into_exprs(ctx, into_exprs, into_count, params.at(i), exist));
      if (OB_SUCC(ret) && exist) {
        params.at(i) = ObObj(ObNullType);
      }
    }
  }
  return ret;
}

int ObSPIService::check_exist_in_into_exprs(ObPLExecCtx *ctx,
                                            const ObSqlExpression **into_exprs,
                                            int64_t into_count,
                                            const ObObj &result,
                                            bool &exist)
{
  int ret = OB_SUCCESS;
  ObPlCompiteWrite *composite_write = nullptr;
  CK (OB_NOT_NULL(ctx));
  CK (into_count >= 0);
  CK (result.is_ext() || result.is_unknown());
  OX (exist = false);
  for (int64_t i = 0; OB_SUCC(ret) && i < into_count; ++i) {
    CK (OB_NOT_NULL(into_exprs[i]));
    if (result.is_ext()) {
      if (is_obj_access_expression(*(into_exprs[i]))) {
        ObObjParam into;
        OZ (spi_calc_expr(ctx, into_exprs[i], OB_INVALID_ID, &into));
        CK (into.is_ext());
        OX (composite_write = reinterpret_cast<ObPlCompiteWrite *>(into.get_ext()));
        CK (OB_NOT_NULL(composite_write));
        if (OB_SUCC(ret) && composite_write->value_addr_ == result.get_ext()) {
          exist = true;
        }
      }
    } else if (is_question_mark_expression(*(into_exprs[i]))) {
      const ObObj &into = get_const_value(*(into_exprs[i]));
      CK (into.is_unknown());
      if (OB_SUCC(ret) && into.get_unknown() == result.get_unknown()) {
        exist = true;
      }
    }
  }
  return ret;
}
// out_params is used to mark possible output parameters calculated by the function
// ObExtendType represents address, ObIntType represents the position in paramstore, ObNullType represents that this parameter does not need to be processed
int ObSPIService::construct_exec_params(ObPLExecCtx *ctx,
                                        ObIAllocator &param_allocator, // used to copy runtime parameters
                                        const ObSqlExpression **param_exprs,
                                        int64_t param_count,
                                        const ObSqlExpression **into_exprs,
                                        int64_t into_count,
                                        ParamStore &exec_params,
                                        ObSPIOutParams &out_params)
{
  int ret = OB_SUCCESS;
  ObObjParam result;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  CK (OB_NOT_NULL(ctx));
  for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
    const ObSqlExpression *expr = static_cast<const ObSqlExpression*>(param_exprs[i]);
    ObObj null_obj(ObNullType);
    result.reset();
    result.ObObj::reset();
    CK (OB_NOT_NULL(expr));
    OZ (spi_calc_expr(
      ctx, expr, OB_INVALID_INDEX, &result), i, param_count, *expr, result);
    OX (result.set_param_meta());
    if OB_SUCC (ret) {
      ObExprResType result_type;
      OZ (get_result_type(*ctx, *expr, result_type));
      if (OB_FAIL(ret)) {
      } else if (result.is_ext()) {
        if (result_type.is_ext()) {
          OX (result.set_udt_id(result_type.get_udt_id()));
          if (OB_SUCC(ret)
              && result_type.get_extend_type() > 0
              && result_type.get_extend_type() < T_EXT_SQL_ARRAY
              && !result.is_pl_extend()) {
            OX (const_cast<ObObjMeta&>(result.get_meta()).set_extend_type(result_type.get_extend_type()));
          }
          OZ (out_params.push_back(null_obj));
        } else {
          bool exist = false;
          ObObj* obj = reinterpret_cast<ObObj*>(result.get_ext());
          OZ (check_exist_in_into_exprs(ctx, into_exprs, into_count, result, exist));
          if (OB_SUCC(ret)) {
            if (!exist) {
              OZ (out_params.push_back(result), result);
            } else {
              OZ (out_params.push_back(null_obj));
            }
          }
          CK (OB_NOT_NULL(obj));
          OX (result = *obj);
          result.set_param_meta();
          OX (result.set_accuracy(result_type.get_accuracy()));
        }
      } else {
        result.set_accuracy(result_type.get_accuracy());
        result.set_param_meta(result_type);
        if (is_question_mark_expression(*expr)) {
          bool exist = false;
          OZ (check_exist_in_into_exprs(ctx, into_exprs, into_count, get_const_value(*expr), exist));
          if (OB_SUCC(ret)) {
            if (!exist) {
              OZ (out_params.push_back(get_const_value(*expr)), get_const_value(*expr));
            } else {
              OZ (out_params.push_back(null_obj));
            }
          }
        } else {
          OZ (out_params.push_back(null_obj));
        }
      }
      if (OB_SUCC(ret)) { // Execution period parameters must be copied from PL's memory space to SQL's own memory space to prevent parameters from being modified during SQL execution }
        ObObjParam new_param = result;
        if (result.is_pl_extend()) {
          new_param.set_int_value(0);
          OZ (pl::ObUserDefinedType::deep_copy_obj(param_allocator, result, new_param, true));
        } else {
          OZ (deep_copy_obj(param_allocator, result, new_param));
        }
        OX (new_param.set_accuracy(result.get_accuracy()));
        OX (new_param.set_need_to_check_type(true));
        if (OB_SUCC(ret)) {
          OZ (exec_params.push_back(new_param));
          if (OB_FAIL(ret)) {
            int ret = OB_SUCCESS;
            OZ (ObUserDefinedType::destruct_obj(new_param, ctx->exec_ctx_->get_my_session()));
          }
        }
      }
    }
  }
  LOG_DEBUG("debug for construct_exec_params", K(ret), K(out_params));
  return ret;
}

int ObSPIService::process_function_out_result(ObPLExecCtx *ctx,
                                              ObResultSet &result_set,
                                              ObIArray<ObObj> &out_params)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = NULL;
  ObPhysicalPlanCtx *pctx = NULL;
  ParamStore *exec_params = NULL;
  CK (OB_NOT_NULL(exec_ctx = &(result_set.get_exec_context())));
  CK (OB_NOT_NULL(pctx = exec_ctx->get_physical_plan_ctx()));
  CK (OB_NOT_NULL(exec_params = &(pctx->get_param_store_for_update())));

  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->allocator_));

  for (int64_t i = 0; OB_SUCC(ret) && i < out_params.count(); ++i) {
    if (out_params.at(i).is_ext()) {
      ObObj* obj = reinterpret_cast<ObObj*>(out_params.at(i).get_ext());
      CK (OB_NOT_NULL(obj));
      OZ (deep_copy_obj(*(ctx->allocator_), exec_params->at(i), *obj));
    } else if (out_params.at(i).is_unknown()) {
      ObObjParam* obj = NULL;
      int64_t idx = out_params.at(i).get_unknown();
      CK (idx >= 0 && idx < ctx->params_->count());
      OX (obj = &(ctx->params_->at(idx)));
      if (OB_SUCC(ret) && *obj != exec_params->at(i)) {
        ObObjParam result;
        OZ (spi_convert_objparam(ctx, &(exec_params->at(i)), idx, &result, false));
        OZ (deep_copy_obj(*(ctx->allocator_), result, *obj));
        obj->set_param_meta();
      }
    } else {
      CK (out_params.at(i).is_null());
    }
  }
  return ret;
}

int ObSPIService::store_params_string(
  ObPLExecCtx *ctx, ObSPIResultSet &spi_result, ParamStore *exec_params)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) 
      && OB_NOT_NULL(ctx)
      && OB_NOT_NULL(ctx->exec_ctx_)
      && OB_NOT_NULL(ctx->exec_ctx_->get_my_session())) {
    char *tmp_ptr = NULL;
    int64_t tmp_len = 0;
    OZ (store_params_value_to_str(spi_result.get_memory_ctx()->get_arena_allocator(),
                                  *ctx->exec_ctx_->get_my_session(),
                                  exec_params,
                                  tmp_ptr,
                                  tmp_len));
    OX (spi_result.get_exec_params_str_ptr()->assign(tmp_ptr, tmp_len));
  }
  return ret;
}

int ObSPIService::prepare_static_sql_params(ObPLExecCtx *ctx,
                                            ObIAllocator &param_allocator, // used to copy runtime parameters
                                            const ObString &sql,
                                            const ObString &ps_sql,
                                            int64_t type,
                                            const ObSqlExpression **params,
                                            int64_t param_count,
                                            const ObSqlExpression **into_exprs,
                                            int64_t into_count,
                                            ObSPIResultSet &spi_result,
                                            ObSPIOutParams &out_params,
                                            ParamStore *&curr_params)
{
  int ret = OB_SUCCESS;
  ParamStore *exec_params = NULL;
  const ObSqlExpression **param_exprs = reinterpret_cast<const ObSqlExpression **>(params);

  curr_params = NULL;

  if (OB_ISNULL(exec_params = reinterpret_cast<ParamStore *>(param_allocator.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for exec params", K(ret));
  }
  OX (new (exec_params) ParamStore( (ObWrapperAllocator(param_allocator)) ) );
  OX (curr_params = exec_params);

  if (NULL == sql) {
    OZ (construct_exec_params(ctx, param_allocator, param_exprs, param_count,
                              into_exprs, into_count, *exec_params, out_params),
      K(sql), K(ps_sql), K(type), K(param_count), K(out_params), KPC(exec_params));

    OZ (store_params_string(ctx, spi_result, exec_params));
  }

  return ret;
}

int ObSPIService::convert_ext_null_params(ParamStore &params, ObSQLSessionInfo *session)
{
  int  ret = OB_SUCCESS;
  bool is_ext_null = false;

  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
    if (params.at(i).is_pl_extend()) {
      if (OB_FAIL(ObPLDataType::obj_is_null(params.at(i), is_ext_null))) {
        LOG_WARN("check obj_is_null failed", K(ret));
      } else if (is_ext_null) {
        ObObjMeta param_meta = params.at(i).get_meta();
        CK (OB_NOT_NULL(session));
        OZ (ObUserDefinedType::destruct_obj(params.at(i), session));
        OX (params.at(i).set_null());
        OX (params.at(i).set_param_meta(param_meta));
      }
    }
  }
  return ret;
}

int ObSPIService::inner_open(ObPLExecCtx *ctx,
                             ObIAllocator &param_allocator,
                             const ObString &sql,
                             const ObString &ps_sql,
                             int64_t type,
                             void *params,
                             int64_t param_count,
                             const ObSqlExpression **into_exprs,
                             int64_t into_count,
                             ObSPIResultSet &spi_result,
                             ObSPIOutParams &out_params,
                             bool is_dynamic_sql,
                             bool is_cursor_params)
{
  int ret = OB_SUCCESS;

  ParamStore *curr_params = NULL;

  if (is_dynamic_sql) {
    ObObjParam **dynamic_params = reinterpret_cast<ObObjParam **>(params);
    OZ (prepare_dynamic_sql_params(ctx, spi_result, param_allocator, param_count, dynamic_params, curr_params));
  } else if (is_cursor_params) {
    ParamStore *cursor_params = reinterpret_cast<ParamStore *>(params);
    OZ (prepare_cursor_params(ctx, spi_result, param_allocator, param_count, cursor_params, curr_params));
  } else {
    const ObSqlExpression **static_params = reinterpret_cast<const ObSqlExpression **>(params);
    OZ (prepare_static_sql_params(ctx,
                                  param_allocator,
                                  sql,
                                  ps_sql,
                                  type,
                                  static_params,
                                  param_count,
                                  into_exprs,
                                  into_count,
                                  spi_result,
                                  out_params,
                                  curr_params));
  }

  CK (OB_NOT_NULL(curr_params));
  OZ (convert_ext_null_params(*curr_params, ctx->exec_ctx_->get_my_session()));
  OZ (inner_open(ctx, sql, ps_sql, type, *curr_params, spi_result, out_params, is_dynamic_sql));

  // if failed, we need release complex parameter memory in here
  if (OB_FAIL(ret) && OB_NOT_NULL(curr_params)) {
    int ret = OB_SUCCESS; // ignore destruct obj error
    for (int64_t i = 0; i < curr_params->count(); ++i) {
      OZ (ObUserDefinedType::destruct_obj(curr_params->at(i), ctx->exec_ctx_->get_my_session()));
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

int ObSPIService::inner_open(ObPLExecCtx *ctx,
                             const ObString &sql,
                             const ObString &ps_sql,
                             int64_t type,
                             ParamStore &exec_params,
                             ObSPIResultSet &spi_result,
                             ObSPIOutParams &out_params,
                             bool is_dynamic_sql)
{
  int ret = OB_SUCCESS;

  OZ (adjust_out_params(*spi_result.get_result_set(), out_params));

  if (OB_ISNULL(ctx)
      || OB_ISNULL(ctx->exec_ctx_)
      || (NULL == ctx->allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(ctx), K(sql), K(ret));
  } else {
#ifndef NDEBUG
    LOG_INFO("spi_execute using", K(sql), K(ps_sql), K(exec_params));
#else
    LOG_TRACE("spi_execute using", K(sql), K(ps_sql), K(exec_params));
#endif
    ObSQLSessionInfo *session = ctx->exec_ctx_->get_my_session();
    ObIPLSqlRuntime *pl_sql_runtime = ctx->exec_ctx_->get_pl_sql_runtime();
    if (OB_ISNULL(session) || OB_ISNULL(pl_sql_runtime)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Argument in pl context is NULL", K(session), K(ret));
    } else {
      bool is_inner_session = session->is_inner();
      ObSQLSessionInfo::SessionType old_session_type = session->get_session_type();
      !is_inner_session ? session->set_inner_session() : (void)NULL;
      session->set_session_type(ObSQLSessionInfo::USER_SESSION);
      if (OB_SUCC(ret)) {
        WITH_CONTEXT(spi_result.get_memory_ctx()) {
          if (exec_params.count() <= 0 && !sql.empty()) {
            spi_result.get_result_set()->set_user_sql(true);
#ifdef ERRSIM
            OX (ret = OB_E(EventTable::EN_SPI_SQL_EXEC) OB_SUCCESS);
#endif
            OZ (pl_sql_runtime->execute_pl_sql(sql,
                                              *session,
                                              exec_params,
                                              *spi_result.get_result_set(),
                                              spi_result.get_sql_ctx(),
                                              false /* is_prepare_protocol */,
                                              false /* is_dynamic_sql */), K(sql), K(ps_sql), K(exec_params));
          } else {
            spi_result.get_result_set()->set_stmt_type(static_cast<stmt::StmtType>(type));
#ifdef ERRSIM
            OX (ret = OB_E(EventTable::EN_SPI_SQL_EXEC) OB_SUCCESS);
#endif
            OZ (pl_sql_runtime->execute_pl_sql(ps_sql,
                                              *session,
                                              exec_params,
                                              *spi_result.get_result_set(),
                                              spi_result.get_sql_ctx(),
                                              true /* is_prepare_protocol */,
                                              is_dynamic_sql /* is_dynamic_sql */), K(sql), K(ps_sql), K(exec_params));
            OZ (adjust_out_params(*spi_result.get_result_set(), out_params));
          }
        }
      }

      !is_inner_session ? session->set_user_session() : (void)NULL;
      session->set_session_type(old_session_type);
    }
  }
  return ret;
}

int ObSPIService::inner_fetch(ObPLExecCtx *ctx,
                              bool &can_retry,
                              ObSPIResultSet &spi_result,
                              const ObSqlExpression **into_exprs,
                              int64_t into_count,
                              const ObDataType *column_types,
                              int64_t type_count,
                              const bool *exprs_not_null_flag,
                              const int64_t *pl_integer_ranges,
                              int64_t &row_count,
                              ObNewRow *current_row,
                              bool has_hidden_rowid,
                              bool for_cursor,
                              const ObDataType *return_types,
                              int64_t return_type_count,
                              bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObResultSet *result_set = spi_result.get_result_set();
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(ctx->exec_ctx_->get_sql_ctx()));
  CK (OB_NOT_NULL(result_set));
  if (OB_SUCC(ret)) {
    ObNewRow dummy_row;
    current_row = NULL != current_row ? current_row : &dummy_row;
    WITH_CONTEXT(spi_result.get_memory_ctx()) {
      if (OB_FAIL((get_result(ctx,
                              result_set,
                              true/*is streaming*/,
                              into_exprs,
                              into_count,
                              column_types,
                              type_count,
                              exprs_not_null_flag,
                              pl_integer_ranges,
                              row_count,
                              *current_row,
                              can_retry,
                              has_hidden_rowid,
                              for_cursor,
                              return_types,
                              return_type_count,
                              is_type_record)))) {
        if (!for_cursor || (for_cursor && ret != OB_READ_NOTHING)) {
          LOG_WARN("failed to get_result, check if need retry", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObSPIService::inner_fetch_with_retry(ObPLExecCtx *ctx,
                                         ObSPIResultSet &spi_result,
                                         const ObSqlExpression **into_exprs,
                                         int64_t into_count,
                                         const ObDataType *column_types,
                                         int64_t type_count,
                                         const bool *exprs_not_null_flag,
                                         const int64_t *pl_integer_ranges,
                                         int64_t &row_count,
                                         ObNewRow &current_row,
                                         bool has_hidden_rowid,
                                         bool for_cursor,
                                         int64_t last_exec_time,
                                         const ObDataType *return_types,
                                         int64_t return_type_count,
                                         bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObQueryRetryCtrl retry_ctrl;
  ObSQLSessionInfo *session = NULL;
  ObResultSet *result_set = spi_result.get_result_set();
  CK (OB_NOT_NULL(ctx));
  CK (OB_NOT_NULL(ctx->exec_ctx_));
  CK (OB_NOT_NULL(session = ctx->exec_ctx_->get_my_session()));
  CK (OB_NOT_NULL(session->get_pl_sqlcode_info()));
  CK (OB_NOT_NULL(result_set));
  if (OB_SUCC(ret)) {
    int64_t time_gap = ObTimeUtility::current_time() - last_exec_time;
    int64_t old_query_start_time = session->get_query_start_time();
    int64_t old_timeout_ts = THIS_WORKER.get_timeout_ts();
    int64_t min_timeout_ts = old_timeout_ts;

    if (result_set->get_errcode() != OB_SUCCESS && result_set->get_errcode() != OB_ITER_END) {
      ret = OB_READ_NOTHING;
      session->get_pl_sqlcode_info()->set_sqlcode(OB_SUCCESS);
      LOG_WARN("cursor result set already failed, will return OB_READ_NOTHING", K(ret), K(result_set->get_errcode()));
    }

    CK (result_set->get_exec_context().get_physical_plan_ctx() != NULL);
    OX (min_timeout_ts = MIN(old_timeout_ts,
        result_set->get_exec_context().get_physical_plan_ctx()->get_timeout_timestamp()));
    OX (session->set_query_start_time(ObTimeUtility::current_time()));
    OX (THIS_WORKER.set_timeout_ts(min_timeout_ts + time_gap));
    OX (result_set->get_exec_context().get_physical_plan_ctx()->
              set_timeout_timestamp(min_timeout_ts + time_gap));
    if (OB_SUCC(ret)) {
      do {
        ret = OB_SUCCESS;
        bool can_retry = true;
        ObSPIRetryCtrlGuard retry_guard(retry_ctrl, spi_result, *session, ret, true);
        if (FAILEDx(inner_fetch(ctx,
                                can_retry,
                                spi_result,
                                into_exprs,
                                into_count,
                                column_types,
                                type_count,
                                exprs_not_null_flag,
                                pl_integer_ranges,
                                row_count,
                                &current_row,
                                has_hidden_rowid,
                                for_cursor,
                                return_types,
                                return_type_count,
                                is_type_record))) {
          if (!for_cursor || (for_cursor && ret != OB_READ_NOTHING)) {
            LOG_WARN("failed to get_result, check if need retry", K(ret));
          }
        }
        if (can_retry) {
          retry_guard.test();
        }
      // NOTE: cursor fetch failed can not retry, we only use this to refresh location cache.
      // actully, this function only called by cursor, so `for_cursor` just in case.
      } while (RETRY_TYPE_NONE != retry_ctrl.get_retry_type() && !for_cursor);
      session->get_retry_info_for_update().clear();
      session->set_query_start_time(old_query_start_time);
      THIS_WORKER.set_timeout_ts(old_timeout_ts);
    }
  }
  return ret;
}

int ObSPIService::store_into_result(ObPLExecCtx *ctx,
                                    ObCastCtx &cast_ctx,
                                    ObNewRow &cur_row,
                                    const ObSqlExpression **into_exprs,
                                    const ObDataType *column_types,
                                    int64_t type_count,
                                    int64_t into_count,
                                    const bool *exprs_not_null,
                                    const int64_t *pl_integer_ranges,
                                    const ObDataType *return_types,
                                    int64_t return_type_count,
                                    int64_t actual_column_count,
                                    ObIArray<ObDataType> &row_desc,
                                    bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = ctx->exec_ctx_;
  bool is_strict = false;
  bool is_null_row = 0 == cur_row.get_count();
  CK (OB_NOT_NULL(exec_ctx), OB_NOT_NULL(exec_ctx->get_my_session()));
  CK (is_null_row ? true : (actual_column_count <= cur_row.get_count()));
  OX (is_strict = is_strict_mode(exec_ctx->get_my_session()->get_sql_mode()));

  /* If into variable has only one:
      1. is a basic type variable
      2. a UDT variable defined by create or replace type
      3. a record defined by type
      2 and 3 are both record types
     If into variable has multiple, it is a combination of the above 1 and 2.
     For record variables, the current behavior is to expand and obtain the values of internal elements, then assign them sequentially rather than deep copy and assign as a whole
     If the internal elements of a record are objects, does the assignment action for the object need deep copy
     For top-level records, basic type attributes need to consider casting,
     Complex data type attributes are strictly restricted during the resolve phase, no casting logic is performed at runtime, only assignment is considered
     According to this logic, complex data types do not need expansion, nor is there a need to pass them to runtime for type conversion
  */
  if (into_count > 1) {
    ObSEArray<ObObj, 1> tmp_result;
    ObSEArray<ObDataType, 1> tmp_desc;
    CK (into_count == actual_column_count);
    CK(return_types != nullptr ? return_type_count == into_count : true);
    for (int64_t i = 0; OB_SUCC(ret) && i < actual_column_count; ++i) {
      // Loop through multiple into variables assignment, each variable assignment needs to check if casting is required, then assign with the casted obj
      tmp_result.reuse();
      tmp_desc.reuse();
      if (is_null_row) {
        OZ (tmp_result.push_back(ObObj(ObNullType)));
      } else {
        OZ (tmp_result.push_back(cur_row.get_cell(i)));
      }
      OZ (tmp_desc.push_back(row_desc.at(i)));
      OZ (store_result(ctx, into_exprs[i], &column_types[i],
                      1, exprs_not_null + i, pl_integer_ranges + i,
                      tmp_desc, is_strict,
                      cast_ctx, tmp_result,
                      return_types != nullptr ? &return_types[i] : nullptr,
                      return_types != nullptr ? 1 : 0));
    }
  } else if (is_type_record) {
    /* The following is type record (as a whole) after into, corresponding to multiple column values before into
        Multiple obj are stored in record, implementation needs to assign values to corresponding attributes in sequence, cast needs to be considered
      into record is expanded by one layer, so column_types == return_type_count == actual_column_count
       Collect all column values and pass them to store result for cast validation & assignment
    */
    ObSEArray<ObObj, OB_DEFAULT_SE_ARRAY_COUNT> tmp_result;
    for (int64_t i = 0; OB_SUCC(ret) && i < actual_column_count; ++i) {
      if (is_null_row) {
        OZ (tmp_result.push_back(ObObj(ObNullType)));
      } else {
        OZ (tmp_result.push_back(cur_row.get_cell(i)));
      }
    }
    OZ (store_result(ctx, into_exprs[0], column_types,
                     type_count, exprs_not_null, pl_integer_ranges,
                     row_desc, is_strict,
                     cast_ctx, tmp_result, return_types, return_type_count,
                     true));
  } else {
    /*
      After into is udt record, considered as a single variable entity, corresponding to a single column value before into
        It is an obj stored in record. In the current implementation, the obj corresponding to record is disassembled into multiple obj, then stored in record,
        The code logic can reuse scenario 1. Theoretically, we should be able to directly find the address of into record, deep copy assignment, no need for strong conversion
    */
    CK (1 == actual_column_count);
    if (OB_SUCC(ret)) {
      ObSEArray<ObObj, 1> tmp_result;
      ObSEArray<ObDataType, 1> tmp_desc;
      if (is_null_row) {
        OZ (tmp_result.push_back(ObObj(ObNullType)));
      } else {
        OZ (tmp_result.push_back(cur_row.get_cell(0)));
      }
      OZ (tmp_desc.push_back(row_desc.at(0)));
      OZ (store_result(ctx, into_exprs[0], column_types,
                      1, exprs_not_null, pl_integer_ranges,
                      tmp_desc, is_strict,
                      cast_ctx, tmp_result,
                      return_types,
                      return_type_count));
    }
  }
  return ret;
}


int ObSPIService::get_package_var_info_by_expr(const ObSqlExpression *expr,
                                              uint64_t &package_id,
                                              uint64_t &var_idx)
{
  int ret = OB_SUCCESS;
  // Identify package variables written through an INTO target.
  CK (OB_NOT_NULL(expr));
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (T_OP_GET_PACKAGE_VAR == get_expression_type(*expr)) {
    OV (5 <= expr->get_expr_items().count(), OB_ERR_UNEXPECTED, expr->get_expr_items().count());
    CK (T_UINT64 == expr->get_expr_items().at(1).get_item_type());
    CK (T_INT == expr->get_expr_items().at(2).get_item_type());
    OX (package_id = expr->get_expr_items().at(1).get_obj().get_uint64());// pkg id
    OX (var_idx = expr->get_expr_items().at(2).get_obj().get_int());// var idx
  } else if (is_obj_access_expression(*expr)
              && expr->get_expr_items().count() > 1
              && T_OP_GET_PACKAGE_VAR == expr->get_expr_items().at(1).get_item_type()) {
    uint16_t param_pos = expr->get_expr_items().at(1).get_param_idx();
    OX (package_id = expr->get_expr_items().at(param_pos).get_obj().get_uint64());
    OX (var_idx = expr->get_expr_items().at(param_pos+1).get_obj().get_int());
  }
  LOG_DEBUG("get_package_var_info_by_expr ", K(package_id), K(var_idx));
  return ret;
}

/***************************************************************************************/
/* Note: The following code is related to memory arrangement. Any modification here must be done with a thorough understanding of the memory arrangement and lifecycle of various data types on the PL runtime side and the SQL side.
 * Implicit assignment (INTO) on the SQL side is relatively simpler than explicit assignment (Assign) because the source data to be stored must be basic data types obtained from query statements,
 * there is no situation where the source is an ADT.
 * For any issues, please contact Ruyan ryan.ly
 ***************************************************************************************/
int ObSPIService::get_result(ObPLExecCtx *ctx,
                             void *result_set, // Store the result set structure, possibly ObSPICursor and ObInnerSQLResult
                             bool is_streaming, // false indicates that the result is ObSPICursor, otherwise it is ObInnerSQLResult
                             const ObSqlExpression **into_exprs, // An array of ObSqlExpression*, indicating where this result is stored: if it is a Question Mark, it indicates the index in params; if it is ObjAccess, its expression result indicates a memory address
                             int64_t into_count, // represents the number of into_exprs
                             const ObDataType *column_types,
                             int64_t type_count,
                             const bool *exprs_not_null,
                             const int64_t *pl_integer_ranges,
                             int64_t &row_count,
                             ObNewRow &current_row, // return the last row
                             bool &can_retry,
                             bool has_hidden_rowid, // If the last column is a hidden rowid, it needs to be skipped
                             bool for_cursor, // whether to check single line and not found
                             const ObDataType *return_types,
                             int64_t return_type_count,
                             bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObIAllocator *allocator = ctx->allocator_;
  ObExecContext *exec_ctx = ctx->exec_ctx_;
  ObPLCursorInfo *implicit_cursor = NULL;
  int64_t hidden_column_count = has_hidden_rowid ? 1 : 0;
  bool fetch_without_into = for_cursor && 0 == into_count; // MySQL protocol server cursor fetch.
  CK (OB_NOT_NULL(allocator), OB_NOT_NULL(exec_ctx), OB_NOT_NULL(exec_ctx->get_my_session()));
  if (OB_SUCC(ret) && !for_cursor) {
    CK (OB_NOT_NULL(implicit_cursor = exec_ctx->get_my_session()->get_pl_implicit_cursor()));
  }
  if (OB_FAIL(ret)) {
  } else if (into_count > 0 || fetch_without_into) {
    // SELECT INTO and FETCH INTO
    if (OB_ISNULL(result_set)
        || ((NULL == column_types || type_count <= 0) && into_count > 0)
        || (NULL == into_exprs && into_count > 0)
        || (!is_streaming && !for_cursor)) { // If not streaming, then it must be cursor
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Argument passed in is NULL",
               K(result_set), K(into_exprs), K(into_count), K(column_types),
               K(type_count), K(for_cursor), K(ret));
    } else if (!is_streaming
               && 0 == static_cast<ObSPICursor*>(result_set)->row_store_.get_row_cnt()) {
      ret = OB_READ_NOTHING;
    } else {
      bool not_found = false;
      int64_t column_count = 0;
      int64_t actual_column_count = 0;
      // Step1: Get result set row description
      ObArray<ObDataType> row_desc;
      if (OB_FAIL(row_desc.reserve(OB_DEFAULT_SE_ARRAY_COUNT))) {
        LOG_WARN("fail to reserve row_desc", K(ret));
      } else if (!is_streaming) { // MySQL mode Cursor or updatable cursor or cursor expression will cache data
        column_count = static_cast<ObSPICursor*>(result_set)->row_desc_.count();
        actual_column_count = column_count - hidden_column_count;
        // TODO: The original column_count was obtained from row_store_, OB_RA_ROW_STORE does not provide row_col_cnt
        // This judgment currently has no meaning, comment it out first
        // OV (column_count == static_cast<ObSPICursor*>(result_set)->row_desc_.count(),
        //     OB_INVALID_ARGUMENT,
        //     K(actual_column_count),
        //     K(static_cast<ObSPICursor*>(result_set)->row_desc_.count()));
        for (int64_t i = 0; OB_SUCC(ret) && i < actual_column_count; ++i) {
          OZ (row_desc.push_back(static_cast<ObSPICursor*>(result_set)->row_desc_.at(i)));
        }
      } else {
        const common::ColumnsFieldIArray *fields = static_cast<ObResultSet*>(result_set)->get_field_columns();
        if (OB_ISNULL(fields)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(ret));
        } else {
          column_count = fields->count();
          actual_column_count = column_count - hidden_column_count;
        }
        bool need_subschema_ctx = false;
        for (int64_t i = 0; OB_SUCC(ret) && i < actual_column_count; ++i) {
          ObDataType type;
          type.set_meta_type(fields->at(i).type_.get_meta());
          type.set_accuracy(fields->at(i).accuracy_);
          if (type.get_meta_type().is_collection_sql_type()) {
            // need subschema ctx to convert sql udt to pl types in convert obj 
            need_subschema_ctx = true;
          }
          if (OB_FAIL(row_desc.push_back(type))) {
            LOG_WARN("push back error", K(i), K(fields->at(i).type_), K(fields->at(i).accuracy_),
                     K(ret));
          }
        }
        if (OB_SUCC(ret) && need_subschema_ctx) {
          CK (OB_NOT_NULL(exec_ctx->get_physical_plan_ctx()));
          if (OB_SUCC(ret)) {
            ObSubSchemaCtx & subschema_ctx = exec_ctx->get_physical_plan_ctx()->get_subschema_ctx();
            if (OB_FAIL(subschema_ctx.assgin(
                  static_cast<ObResultSet*>(result_set)->get_physical_plan()->get_subschema_ctx()))) {
              LOG_WARN("fail to assign subschema ctx", K(ret));
            }
          }
        }
      }
      // Step2: Check type match
      if (OB_SUCC(ret)) {
        if (fetch_without_into) {
          // Protocol fetch returns the current row directly instead of assigning PL variables.
        } else {
          if (actual_column_count != type_count
              && actual_column_count != into_count) {
            ret = OB_ERR_SP_INVALID_FETCH_ARG;
            LOG_WARN("type count is not equal to column count", K(column_count),
                     K(actual_column_count), K(type_count), K(ret));
          }
          if (actual_column_count == type_count) {
            for (int64_t i = 0; OB_SUCC(ret) && i < actual_column_count; ++i) {
              if (!cast_supported(row_desc.at(i).get_obj_type(), row_desc.at(i).get_collation_type(),
                                  column_types[i].get_obj_type(), column_types[i].get_collation_type())) {
                ret = OB_ERR_INVALID_TYPE_FOR_OP;
                LOG_WARN("cast parameter to expected type not supported",
                         K(ret), K(i), K(row_desc.at(i)), K(column_types[i]));
              }
            }
          }
        }
      }
      // Step3: Get collationtype and castmode
      ObCollationType cast_coll_type = CS_TYPE_INVALID;
      ObCastMode cast_mode = CM_NONE;
      const ObDataTypeCastParams dtc_params =
            ObBasicSessionInfo::create_dtc_params(ctx->exec_ctx_->get_my_session());
      bool is_strict = false;
      if (OB_SUCC(ret)) {
        OZ (exec_ctx->get_my_session()->get_collation_connection(cast_coll_type));
        OX (is_strict = is_strict_mode(exec_ctx->get_my_session()->get_sql_mode()));
        OZ (ObSQLUtils::get_default_cast_mode(stmt::T_NONE, exec_ctx->get_my_session(), cast_mode));
      }
      // Step4: Get the result and store it in the variable
      bool package_var_modified = false;
      if (OB_SUCC(ret)) { // [FETCH] INTO x, y, z OR [FETCH] INTO record
        /*
         * If not multiple variables, then it is a single record. It cannot be multiple records or a mix of records and variables.
         * 1、If there are multiple into clauses, the number of into clauses must match the number of select items (this is definitely the case of multiple variables);
         * 2、If the number of into clauses is less than the number of select items, then the number of into clauses must be 1 (this is definitely the case of a single record);
         * 3、If the number of into clauses matches the number of select items and both are 1, then it is uncertain whether it is a single record or a single variable, but it is still valid;
         * Protocol fetch has no PL output variables; only current_row needs to be returned.
         */
        if (!((into_count < actual_column_count && 1 == into_count)
            || into_count == actual_column_count
            || (for_cursor && 0 == into_count))) {
          ret = OB_ERR_COULUMN_VALUE_NOT_MATCH;
          LOG_WARN("into clause not match select items", K(into_count), K(column_count),
                   K(actual_column_count), K(ret));
        } else if (OB_FAIL(fetch_row(result_set, is_streaming, row_count, current_row))) {
          if (OB_ITER_END == ret) {
            not_found = true;
            if (!for_cursor) {
              implicit_cursor->set_rowcount(0);
            }
            if (is_streaming) {
              current_row.reset();
            }
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("read result error", K(ret), K(row_count), K(for_cursor));
          }
        } else {
          if (OB_SUCC(ret) && !for_cursor) { //if not cursor, into can only return one row, need to check error for returning multiple rows
            ObNewRow tmp_row;
            int64_t cnt = row_count;
            if (OB_FAIL(ob_write_row(tmp_allocator, current_row, tmp_row))) {
              LOG_WARN("copy current row fail.", K(ret));
            } else {
              ObNewRow tmp_row2;
              current_row = tmp_row;
              if (OB_FAIL(fetch_row(result_set, is_streaming, cnt, tmp_row2))) {
                if (OB_ITER_END == ret) {
                  ret = OB_SUCCESS;
                } else {
                  LOG_WARN("read result error", K(ret));
                }
              } else {
                ret = OB_ERR_TOO_MANY_ROWS;
              }
            }
            // If a SELECT INTO statement returns multiple rows, raise TOO_MANY_ROWS
            // and keep the implicit cursor row count at 1,
            // not the actual number of rows that satisfy the query.
            implicit_cursor->set_rowcount(1);
          }

          if (OB_FAIL(ret)) {
            // do nothing
          } else if (fetch_without_into) {
            // Protocol fetch returns current_row directly.
          } else {
            ObCastCtx cast_ctx(&tmp_allocator, &dtc_params, cast_mode, cast_coll_type);
            exec_ctx->get_my_session()->configure_obj_cast(
                cast_ctx,
                exec_ctx->get_srs_provider(),
                exec_ctx->get_lob_read_service());
            {
              // Query evaluation warnings belong to the enclosing CALL, but a
              // non-strict cast used only to assign an INTO variable does not.
              // Isolate just the assignment instead of clearing the complete
              // statement warning buffer after SELECT ... INTO.
              ObWarningBufferIgnoreScope ignore_into_assignment_warnings;
              OZ (store_into_result(ctx, cast_ctx, current_row, into_exprs, column_types, type_count,
                               into_count, exprs_not_null, pl_integer_ranges, return_types, return_type_count,
                               actual_column_count, row_desc, is_type_record));
            }
            for (int64_t i = 0; OB_SUCC(ret) && i < into_count; ++i) {
              if (is_obj_access_expression(*into_exprs[i])) {
                std::pair<uint64_t, uint64_t> package_var_info = std::pair<uint64_t, uint64_t>(OB_INVALID_ID, OB_INVALID_ID);
                OZ (get_package_var_info_by_expr(into_exprs[i], package_var_info.first, package_var_info.second));
                if (OB_INVALID_ID != package_var_info.first && OB_INVALID_ID != package_var_info.second) {
                  OX (package_var_modified = true);
                }
              }
            }
          }
        }
      }

      if (OB_SUCC(ret) && package_var_modified) {
        OX (ctx->exec_ctx_->get_my_session()->set_pl_can_retry(false));
      }

      if (OB_SUCC(ret) && not_found) {
        /*
         * Mysql mode:
         * Both static SQL and cursor will throw exceptions, but if the NOT FOUND exception of static SQL is not caught at the end, it will suppress the exception and report a WARNING message;
         * If the NOT FOUND exception of cursor is not caught at the end, it will throw this error.
         */
        ret = OB_READ_NOTHING;
      }
    }
  } else if (!for_cursor) { // Although the result does not need to be stored, get_next still needs to be called once
    ObResultSet *ob_result_set = static_cast<ObResultSet*>(result_set);
    if (ob_result_set->is_with_rows()) { // SELECT or DML RETURNING
      // MYSQL Mode: send query result to client.
      ObSQLSessionInfo *session_info = NULL;
      ObIQueryResultSender *query_sender = NULL;
      CK (OB_NOT_NULL(session_info = exec_ctx->get_my_session()));
      if (OB_SUCC(ret) && OB_NOT_NULL(query_sender = session_info->get_pl_query_sender())) {
        OZ (query_sender->response_query_result(*ob_result_set,
                                                session_info->is_ps_protocol(),
                                                true,
                                                can_retry));
        OZ (query_sender->send_eof_packet(true));
      }
      OX(implicit_cursor->set_rowcount(into_count > 0 ? 1 : 0));
    } else if (stmt::T_ANONYMOUS_BLOCK != ob_result_set->get_stmt_type()) {
      // INSERT, DELETE, UPDATE without Returning
      if (stmt::T_UPDATE == ob_result_set->get_stmt_type()) {
        ObPhysicalPlanCtx *phy_ctx
          = GET_PHY_PLAN_CTX(ob_result_set->get_exec_context());
        CK (OB_NOT_NULL(phy_ctx));
        OX (implicit_cursor->set_rowcount(phy_ctx->get_row_matched_count()));
      } else {
        implicit_cursor->set_rowcount(ob_result_set->get_affected_rows());
      }
    }
  } else { /*do nothing*/ }

  if (OB_SUCC(ret) && !for_cursor) {
    ObResultSet *ob_result_set = static_cast<ObResultSet*>(result_set);
    ObPhysicalPlan *physical_plan = ob_result_set->get_physical_plan();
    ObPhysicalPlanCtx *plan_ctx = nullptr;
    if (OB_NOT_NULL(physical_plan)) {
      if (OB_NOT_NULL(plan_ctx = GET_PHY_PLAN_CTX(ob_result_set->get_exec_context()))) {
        physical_plan->update_cache_access_stat(plan_ctx->get_table_scan_stat());
        plan_ctx->get_table_scan_stat().reset_cache_stat();
      }
    }
  }
  return ret;
}


int ObSPIService::convert_obj(ObPLExecCtx *ctx,
                              ObCastCtx &cast_ctx,
                              bool is_strict,
                              const ObSqlExpression *result_expr,
                              const ObIArray<ObDataType> &current_type,
                              ObIArray<ObObj> &obj_array,
                              const ObDataType *result_types,
                              int64_t type_count,
                              ObIArray<ObObj> &calc_array)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(ctx));
  CK(OB_NOT_NULL(ctx->func_));
  CK(OB_NOT_NULL(ctx->params_));
  CK(OB_NOT_NULL(ctx->exec_ctx_));
  CK(OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK(OB_NOT_NULL(result_types));
  CK(current_type.count() == type_count);
  CK(obj_array.count() == type_count);

  ObSqlObjCastRuntime cast_runtime(
      nullptr == ctx ? nullptr : ctx->exec_ctx_);
  ObIObjCastRuntime *previous_runtime = cast_ctx.runtime_;
  cast_ctx.runtime_ = &cast_runtime;
  ObObj tmp_obj;
  for (int i = 0; OB_SUCC(ret) && i < obj_array.count(); ++i) {
    ObObj &obj = obj_array.at(i);
    tmp_obj.reset();
    obj.set_collation_level(result_types[i].get_collation_level());
    LOG_DEBUG("column convert", K(obj.get_meta()), K(result_types[i].get_meta_type()),
              K(current_type.at(i)), K(result_types[i].get_accuracy()));
    if (obj.is_pl_extend()/* && pl::PL_RECORD_TYPE == obj.get_meta().get_extend_type()*/
        && result_types[i].get_meta_type().is_ext()) {
      // record embedded object scenario, object properties require strong consistency in the resolver phase, no need for casting
      OZ (calc_array.push_back(obj));
    } else if (obj.get_meta() == result_types[i].get_meta_type()
        && (current_type.at(i).get_accuracy() == result_types[i].get_accuracy()
            || (result_types[i].get_meta_type().is_number() // NUMBER target type precision unknown directly do assignment
                && PRECISION_UNKNOWN_YET == result_types[i].get_accuracy().get_precision()
                && FLOATING_NUMBER_SCALE_UNKNOWN_YET == result_types[i].get_accuracy().get_scale())
            || (result_types[i].get_meta_type().is_character_type() // CHAR/VARCHAR length unknown, directly assign
                && -1 == result_types[i].get_accuracy().get_length()))) {
      ObObj tmp_obj;
      if (obj.is_pl_extend()) {
        ObIAllocator *alloc = cast_ctx.allocator_v2_;
        if (PL_CURSOR_TYPE == obj.get_meta().get_extend_type()) {
          alloc = &ctx->exec_ctx_->get_allocator();
        }
        OZ (ObUserDefinedType::deep_copy_obj(*alloc, obj, tmp_obj, true));
        if (OB_SUCC(ret) && obj.get_meta().get_extend_type() != PL_CURSOR_TYPE) {
          OZ (ObUserDefinedType::destruct_obj(obj, ctx->exec_ctx_->get_my_session()));
        }
      } else {
        OZ (deep_copy_obj(*cast_ctx.allocator_v2_, obj, tmp_obj));
      }
      if (OB_SUCC(ret) && tmp_obj.get_meta().is_bit()) {
        tmp_obj.set_scale(result_types[i].get_meta_type().get_scale());
      }
      OZ (calc_array.push_back(tmp_obj));
      if (OB_SUCC(ret)) {
        LOG_DEBUG("same type directyly copy", K(obj), K(tmp_obj), K(result_types[i]), K(i));
      }
    } else if (!obj.is_pl_extend() 
               && !obj.is_geometry()
               && !obj.is_null()
               && result_types[i].get_meta_type().is_ext()) {
      ret = OB_ERR_WRONG_TYPE_FOR_VAR;
      LOG_WARN("expression 'string' in the INTO list is of wrong type", K(ret), K(obj), K(i), K(current_type.at(i)), K(result_types[i]));
    } else {
      LOG_DEBUG("column convert", K(i), K(obj.get_meta()), K(result_types[i].get_meta_type()),
                                  K(current_type.at(i)), K(result_types[i].get_accuracy()));
      ObIArray<ObString> *type_info = NULL;
      // only mysql mode will run this logic
      if (ob_is_enum_or_set_type(result_types[i].get_obj_type())) {
        if (OB_ISNULL(result_expr)) {
          // do nothing
        } else if (!is_question_mark_expression(*result_expr)) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("only can store to local enum set variables", K(ret));
        } else {
          int64_t param_idx = get_const_value(*result_expr).get_unknown();
          if (param_idx >= ctx->func_->get_variables().count() || param_idx < 0) {
            ret = OB_ARRAY_OUT_OF_RANGE;
            LOG_WARN("param idx out of range", K(ret), K(param_idx));
          } else {
            OZ(ctx->func_->get_variables().at(param_idx).get_type_info(type_info));
          }
        }
      }
      ObObj &cur_time = ctx->exec_ctx_->get_physical_plan_ctx()->get_cur_time();
      cast_ctx.cur_time_ = cur_time.get_timestamp();
      ObExprResType result_type;
      OX (result_type.set_meta(result_types[i].get_meta_type()));
      if (result_types[i].get_meta_type().is_number()
          && result_types[i].get_precision() == -1
          && result_types[i].get_scale() == -1
          && current_type.at(i).get_meta_type().is_number()) {
        OX (result_type.set_accuracy(current_type.at(i).get_accuracy()));
      } else {
        OX (result_type.set_accuracy(result_types[i].get_accuracy()));
        if (OB_SUCC(ret) && result_type.is_enum_set_with_subschema()) {
          ObObjMeta org_obj_meta;
          if (OB_FAIL(ObRawExprUtils::extract_enum_set_collation(result_type,
                                                                ctx->exec_ctx_->get_my_session(),
                                                                org_obj_meta))) {
            LOG_WARN("fail to extrac enum set meta", K(ret));
          } else {
            result_type.set_collation(org_obj_meta);
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (result_type.is_null() || result_type.is_unknown()) {
        tmp_obj = obj;
      } else {
        if (OB_FAIL(ObExprColumnConv::convert_with_null_check(tmp_obj, obj, result_type, is_strict, cast_ctx, type_info))) {
          LOG_WARN("fail to convert with null check", K(ret));
        } else if (tmp_obj.is_null()) {
          if (result_types[i].get_meta_type().is_ext() &&
                     (PL_RECORD_TYPE == result_types[i].get_meta_type().get_extend_type() ||
                      PL_NESTED_TABLE_TYPE == result_types[i].get_meta_type().get_extend_type() ||
                      PL_VARRAY_TYPE == result_types[i].get_meta_type().get_extend_type())) {
            int64_t udt_id = result_types[i].get_udt_id();
            const ObUserDefinedType *type = nullptr;
            int64_t ptr = 0;
            int64_t init_size = OB_INVALID_SIZE;
            OZ (ctx->get_user_type(udt_id, type));
            OZ (type->newx(*cast_ctx.allocator_v2_, ctx, ptr));
            OZ (type->get_size(PL_TYPE_INIT_SIZE, init_size));
            OX (tmp_obj.set_extend(ptr, type->get_type(), init_size));
          }
        }
      }
      if (OB_SUCC(ret) && tmp_obj.need_deep_copy() && obj.get_string_ptr() == tmp_obj.get_string_ptr()) {
        // obj may not deep copied in ObExprColumnConv::convert(), do deep copy it if needed.
        ObObj tmp_obj2 = tmp_obj;
        CK (OB_NOT_NULL(cast_ctx.allocator_v2_));
        OZ (deep_copy_obj(*cast_ctx.allocator_v2_, tmp_obj2, tmp_obj));
      }
      OZ (calc_array.push_back(tmp_obj));
      if (OB_SUCC(ret) && tmp_obj.get_meta().is_bit()) {
        calc_array.at(i).set_scale(result_types[i].get_meta_type().get_scale());
      }
    }
  }

  cast_ctx.runtime_ = previous_runtime;
  return ret;
}

bool ObSPIService::is_sql_type_into_pl(ObObj &dest_addr, ObIArray<ObObj> &obj_array)
{
  bool bret = false;
  if (1 == obj_array.count()) {
    // query result is geometry, will convert to pl extend type
    if (dest_addr.is_pl_extend() && obj_array.at(0).is_pl_extend()) {
      ObPLComposite *left = reinterpret_cast<ObPLComposite*>(dest_addr.get_ext());
      ObPLComposite *right = reinterpret_cast<ObPLComposite*>(obj_array.at(0).get_ext());
      if (OB_NOT_NULL(left) && OB_NOT_NULL(right)
          && left->get_id() == right->get_id()
          && ObObjUDTUtil::ob_is_supported_sql_udt(left->get_id())) {
        bret = true;
      }
    }
  } 
  return bret;
}

int ObSPIService::store_result(ObPLExecCtx *ctx,
                               const ObSqlExpression *result_expr,
                               const ObDataType *result_types,
                               int64_t type_count,
                               const bool *not_null_flags,
                               const int64_t *pl_integer_ranges,
                               const ObIArray<ObDataType> &row_desc,
                               bool is_strict,
                               ObCastCtx &cast_ctx,
                               ObIArray<ObObj> &obj_array,
                               const ObDataType *return_types,
                               int64_t return_type_count,
                               bool is_type_record)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  bool need_convert_type = true;
  ObSEArray<ObObj, OB_DEFAULT_SE_ARRAY_COUNT> tmp_obj_array;
  ObSEArray<ObObj, OB_DEFAULT_SE_ARRAY_COUNT> tmp_obj_array1;
  ObIArray<ObObj> *calc_array = &obj_array;
  CK(OB_NOT_NULL(ctx));
  CK(OB_NOT_NULL(ctx->func_));
  CK(OB_NOT_NULL(ctx->params_));
  CK(OB_NOT_NULL(ctx->exec_ctx_));
  CK(OB_NOT_NULL(ctx->exec_ctx_->get_my_session()));
  CK(OB_NOT_NULL(result_expr));
  CK(OB_NOT_NULL(result_types));
  CK(OB_NOT_NULL(not_null_flags));
  CK(OB_NOT_NULL(pl_integer_ranges));
  CK(!obj_array.empty());
  bool is_schema_object = (!is_type_record &&
                           1 == type_count &&
                           1 == obj_array.count() &&
                           ((obj_array.at(0).is_pl_extend() &&
                           obj_array.at(0).get_meta().get_extend_type() != PL_CURSOR_TYPE &&
                           obj_array.at(0).get_meta().get_extend_type() != PL_OPAQUE_TYPE) ||
                           (obj_array.at(0).is_null() &&
                           row_desc.at(0).get_meta_type().is_ext() &&
                           row_desc.at(0).get_meta_type().get_extend_type() > 0 &&
                           row_desc.at(0).get_meta_type().get_extend_type() < T_EXT_SQL_ARRAY &&
                           row_desc.at(0).get_meta_type().get_extend_type() != PL_CURSOR_TYPE &&
                           row_desc.at(0).get_meta_type().get_extend_type() != PL_OPAQUE_TYPE)));
  if (!is_schema_object) {
    if (OB_SUCC(ret) && type_count != obj_array.count()) {
      ret = OB_ERR_SP_INVALID_FETCH_ARG;
      LOG_WARN("type count is not equal to column count",
                K(obj_array.count()), K(type_count), K(ret));
    }
    if (OB_SUCC(ret)
        && (is_get_var_func_expression(*result_expr)
            || row_desc.count() != obj_array.count())) {
      need_convert_type = false;
    }
    //  Check if the not null modifier is effective
    for (int64_t i = 0; OB_SUCC(ret) && i < type_count; ++i) {
      if (not_null_flags[i] && obj_array.at(i).is_null()) {
        ret = OB_ERR_NUMERIC_OR_VALUE_ERROR;
        LOG_WARN("not null check violated",
                K(type_count),
                K(obj_array.at(i).is_null()),
                K(not_null_flags[i]),
                K(i),
                K(ret));
      }
    }
    // Do type conversion
    if (OB_FAIL(ret)) {
    } else if (return_types != nullptr && row_desc.count() == obj_array.count()) {
      OZ(convert_obj(ctx, cast_ctx, is_strict, result_expr, row_desc,
                    obj_array, return_types, return_type_count, tmp_obj_array));
      OX(calc_array = &tmp_obj_array);

      bool is_same = true;
      for (int64_t i = 0; OB_SUCC(ret) && is_same && i < type_count; ++i) {
        if (!(return_types[i] == result_types[i])) {
          is_same = false;
        }
      }

      if (OB_SUCC(ret) && !is_same && need_convert_type) {
        ObSEArray<ObDataType, OB_DEFAULT_SE_ARRAY_COUNT> current_type;
        for (int i = 0; OB_SUCC(ret) && i < return_type_count; ++i) {
          OZ(current_type.push_back(return_types[i]));
        }

        OZ(convert_obj(ctx, cast_ctx, is_strict, result_expr, current_type,
                      tmp_obj_array, result_types, type_count, tmp_obj_array1));
        OX(calc_array = &tmp_obj_array1);
      }
    } else if (need_convert_type) {
      OZ(convert_obj(ctx, cast_ctx, is_strict, result_expr, row_desc,
                    obj_array, result_types, type_count, tmp_obj_array));
      OX(calc_array = &tmp_obj_array);
    }
    // check range
    for (int64_t i = 0; OB_SUCC(ret) && i < calc_array->count(); ++i) {
      OZ (sql::ObExprPLIntegerChecker::check_range(
        calc_array->at(i), calc_array->at(i).get_type(), pl_integer_ranges[i]));
    }
  } else if (obj_array.at(0).is_null()) { //null into extend variable，generate extend null obj
    if (row_desc.at(0).get_meta_type().is_ext()) {
      if (PL_RECORD_TYPE == row_desc.at(0).get_meta_type().get_extend_type() ||
          PL_NESTED_TABLE_TYPE == row_desc.at(0).get_meta_type().get_extend_type() ||
          PL_VARRAY_TYPE == row_desc.at(0).get_meta_type().get_extend_type()) {
        int64_t udt_id = row_desc.at(0).get_udt_id();
        const ObUserDefinedType *type = nullptr;
        int64_t ptr = 0;
        int64_t init_size = OB_INVALID_SIZE;
        ObObj   tmp_obj;
        OX (tmp_obj.reset());
        OZ (ctx->get_user_type(udt_id, type));
        OZ (type->newx(*cast_ctx.allocator_v2_, ctx, ptr));
        OZ (type->get_size(PL_TYPE_INIT_SIZE, init_size));
        OX (tmp_obj.set_extend(ptr, type->get_type(), init_size));
        OZ (tmp_obj_array.push_back(tmp_obj));
        OX (calc_array = &tmp_obj_array);
      }
    }
  }
  // Assign value to variable
  if (OB_SUCC(ret)) {
    ParamStore *params = ctx->params_;
    ObObjParam result_address;
    if (is_obj_access_expression(*result_expr)) { // Accessing the base variable or record through ObjAccess
      ObIAllocator *pkg_allocator = NULL;
      ObIAllocator *composite_allocator = nullptr;
      ObPlCompiteWrite *composite_write = nullptr;
      if (result_expr->get_expr_items().count() > 1
          && T_OP_GET_PACKAGE_VAR == result_expr->get_expr_items().at(1).get_item_type()
          && OB_NOT_NULL(result_expr->get_expr_items().at(1).get_expr_operator())
          && result_expr->get_expr_items().at(1).get_expr_operator()->get_result_type().is_ext()) {
        uint16_t param_pos = result_expr->get_expr_items().at(1).get_param_idx();
        uint64_t package_id = OB_INVALID_ID;
        OX (package_id = result_expr->get_expr_items().at(param_pos).get_obj().get_uint64());
        OZ (spi_get_package_allocator(ctx, package_id, pkg_allocator));
        CK (OB_NOT_NULL(pkg_allocator));
      }
      OZ (spi_calc_expr(ctx, result_expr, OB_INVALID_INDEX, &result_address));
      OX (composite_write = reinterpret_cast<ObPlCompiteWrite *>(result_address.get_ext()));
      CK (OB_NOT_NULL(composite_write));
      OX (result_address.set_extend(composite_write->value_addr_, result_address.get_meta().get_extend_type(), result_address.get_val_len()));
      OX (composite_allocator = reinterpret_cast<ObIAllocator *>(composite_write->allocator_));
      if (OB_SUCC(ret) &&
          !is_schema_object &&
          !is_sql_type_into_pl(result_address, *calc_array) &&
          PL_RECORD_TYPE == result_address.get_meta().get_extend_type()) {
        ObPLRecord *record = reinterpret_cast<ObPLRecord *>(result_address.get_ext());
        CK (OB_NOT_NULL(record));
        OX (composite_allocator = record->get_allocator());
      }
      if (OB_SUCC(ret)) {
        ObIAllocator *alloc = nullptr;
        if (OB_SUCC(ret)) {
          if (nullptr != composite_allocator) {
            alloc = composite_allocator;
          } else if (nullptr != pkg_allocator) {
            alloc = pkg_allocator;
          } else {
            alloc = ctx->allocator_;
          }
        }

        CK (OB_NOT_NULL(alloc));
        // udt will deep copy store datums
        if (OB_SUCC(ret) && !is_schema_object && !is_sql_type_into_pl(result_address, *calc_array)) {
          int64_t i = 0;
          for (; OB_SUCC(ret) && i < calc_array->count(); ++i) {
            ObObj tmp;
            if (calc_array->at(i).is_pl_extend()) {
              ObIAllocator *tmp_alloc = alloc;
              if (PL_CURSOR_TYPE == calc_array->at(i).get_meta().get_extend_type()) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unexcpected cursor type in obj access expression", K(ret), K(calc_array->at(i).get_meta().get_extend_type()));
              }
              OZ (pl::ObUserDefinedType::deep_copy_obj(*tmp_alloc, calc_array->at(i), tmp));
            } else {
              OZ (ob_write_obj(*alloc, calc_array->at(i), tmp));
            }
            OX (calc_array->at(i) = tmp);
          }
          if (OB_FAIL(ret)) {
            for (int64_t j = 0; j < i - 1; ++j) {
              int ret = OB_SUCCESS;
              OZ (ObUserDefinedType::destruct_objparam(*alloc, calc_array->at(j), ctx->exec_ctx_->get_my_session()));
            }
          }
        }
        OZ (store_datums(result_address, *calc_array, alloc, ctx->exec_ctx_->get_my_session(), is_schema_object));
      }
    } else if (is_question_mark_expression(*result_expr)) { // Base variable accessed via question mark
      int64_t param_idx = get_const_value(*result_expr).get_unknown();
      ObAccuracy accuracy;
      accuracy.set_accuracy(result_types[0].accuracy_);
      if (param_idx >= params->count() || param_idx < 0) {
        ret = OB_ARRAY_OUT_OF_RANGE;
        LOG_WARN("param idx out of range", K(ret), K(param_idx), K(params->count()));
      } else {
        if (params->at(param_idx).is_pl_extend()
            && params->at(param_idx).get_ext() != 0
            && params->at(param_idx).get_meta().get_extend_type() != PL_CURSOR_TYPE
            && params->at(param_idx).get_ext() != calc_array->at(0).get_ext()) {
          OZ (ObUserDefinedType::destruct_objparam(*ctx->allocator_, params->at(param_idx), ctx->exec_ctx_->get_my_session()));
        }
        ObObj result;
        const ObPLDataType &var_type = ctx->func_->get_variables().at(param_idx);
        if (OB_FAIL(ret)) {
        } else if (!var_type.is_obj_type() ||
                  (var_type.get_data_type() != NULL && var_type.get_data_type()->get_meta_type().is_ext())) {
          uint64_t dst_id = OB_INVALID_INDEX;
          if (!calc_array->at(0).is_pl_extend()) {
            ret =OB_ERR_EXPRESSION_WRONG_TYPE;
            LOG_WARN("expr is wrong type", K(ret));
          } else if (PL_OPAQUE_TYPE == calc_array->at(0).get_meta().get_extend_type()) {
            OZ (ObUserDefinedType::deep_copy_obj(*ctx->allocator_, calc_array->at(0), result, true));
          } else if (PL_CURSOR_TYPE == calc_array->at(0).get_meta().get_extend_type()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected PL_CURSOR_TYPE to store datum", K(calc_array->at(0).get_meta()), K(ret));
          } else {
            dst_id = var_type.get_user_type_id();
            ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(calc_array->at(0).get_ext());
            if (NULL == composite) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected composite to store datum", KPC(composite), K(ret));
            } else if (OB_INVALID_ID == dst_id || OB_INVALID_ID == composite->get_id()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected composite to store datum", K(dst_id), K(composite->get_id()), K(ret));
            } else if (dst_id != composite->get_id()) {
              ret =OB_ERR_EXPRESSION_WRONG_TYPE;
              LOG_WARN("expr is wrong type", K(dst_id), K(composite->get_id()), K(ret));
            } else {
              OZ (pl::ObUserDefinedType::deep_copy_obj(*ctx->allocator_, calc_array->at(0), result));
            }
          }
        } else if (var_type.is_obj_type() && var_type.get_data_type() != NULL &&
                   var_type.get_data_type()->get_meta_type().is_null() &&
                   calc_array->at(0).is_pl_extend()) {
          OZ (pl::ObUserDefinedType::deep_copy_obj(*ctx->allocator_, calc_array->at(0), result));
        } else if (is_schema_object || calc_array->at(0).is_pl_extend()) {
          ret =OB_ERR_EXPRESSION_WRONG_TYPE;
          LOG_WARN("expr is wrong type", K(ret));
        } else {
          OZ (spi_pad_char_or_varchar(ctx->exec_ctx_->get_my_session(), result_types[0].get_obj_type(),
                                    accuracy, &tmp_alloc, &calc_array->at(0)));
          OZ (deep_copy_obj(*ctx->allocator_, calc_array->at(0), result));
        }
        if (OB_SUCC(ret)) {
          if (params->at(param_idx).is_pl_extend()) {
            //do nothing
          } else {
            void *ptr = params->at(param_idx).get_deep_copy_obj_ptr();
            if (nullptr != ptr) {
              ctx->allocator_->free(ptr);
            }
            params->at(param_idx).set_null();
          }
          OX (params->at(param_idx) = result);
          OX (params->at(param_idx).set_param_meta());
        }
        OX (params->at(param_idx).set_accuracy(accuracy));
      }
    } else if (is_get_var_func_expression(*result_expr)
              || is_get_package_or_subprogram_var_expression(*result_expr)) {
      // Accessing basic variables (user var/sys var) or variables accessed from the package/subprogram
      ObObjParam value;
      OX (value = calc_array->at(0));

      // outrow lob can not be assigned to user var, so convert outrow to inrow lob
      if (OB_SUCC(ret) && value.is_lob_storage()) {
        const common::ObLobReadOptions *lob_read_options = nullptr;
        if (OB_FAIL(ctx->exec_ctx_->get_lob_read_options(lob_read_options))) {
          LOG_WARN("failed to get LOB read options", K(ret));
        } else if (OB_FAIL(ObTextStringIter::convert_outrow_lob_to_inrow_templob(
                       value, value, lob_read_options, cast_ctx.allocator_v2_,
                       true/*allow_persist_inrow*/))) {
          LOG_WARN("convert outrow to inrow lob fail", K(ret), K(value));
        }
      }

      CK (!value.is_pl_extend());
      OX (value.set_param_meta());
      if (OB_SUCC(ret)) {
        // save session value
        SMART_VAR(sql::ObSQLSessionInfo::StmtSavedValue, session_value) {
          int64_t nested_count = 0;
          OX (nested_count = ctx->exec_ctx_->get_my_session()->get_nested_count());
          OZ (ctx->exec_ctx_->get_my_session()->save_session(session_value));
          OZ (spi_set_variable(ctx, static_cast<const ObSqlExpression*>(result_expr), &value, false));
          int tmp_ret = OB_SUCCESS;
          if (OB_SUCCESS != (tmp_ret = ctx->exec_ctx_->get_my_session()->restore_session(session_value))) {
            LOG_ERROR("failed to restore session", K(tmp_ret));
            ret = COVER_SUCC(tmp_ret);
          }
          ctx->exec_ctx_->get_my_session()->set_nested_count(nested_count);
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid result address",
               K(*calc_array), K(*result_expr), K(result_address), K(ret));
    }
  }
  return ret;
}

int ObSPIService::check_and_copy_composite(
  ObObj &result, ObObj &src, ObIAllocator &allocator, ObPLType type, uint64_t dst_udt_id)
{
  int ret = OB_SUCCESS;
  if (PL_OBJ_TYPE == type || PL_INTEGER_TYPE == type) {
    ret = OB_ERR_EXPRESSION_WRONG_TYPE;
    LOG_WARN("expr is wrong type", K(ret));
  } else {
    ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(src.get_ext());
    if (NULL == composite) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected composite to store datum", KPC(composite), K(ret));
    } else if (OB_INVALID_ID == dst_udt_id || OB_INVALID_ID == composite->get_id()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected composite to store datum", K(dst_udt_id), K(composite->get_id()), K(ret));
    } else if (dst_udt_id != composite->get_id()) {
      ret =OB_ERR_EXPRESSION_WRONG_TYPE;
      LOG_WARN("expr is wrong type", K(dst_udt_id), K(composite->get_id()), K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    // Deep-copy the composite value to the destination allocator.
    OZ (pl::ObUserDefinedType::deep_copy_obj(allocator, src, result));
  }
  return ret;
}

int ObSPIService::store_datums(ObObj &dest_addr, ObIArray<ObObj> &obj_array,
                               ObIAllocator *alloc, ObSQLSessionInfo *session_info, bool is_schema_object)
{
  int ret = OB_SUCCESS;
  ObIAllocator *datum_allocator = alloc;
  if (obj_array.empty() || OB_ISNULL(alloc)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(dest_addr), K(obj_array), K(ret));
  } else if (is_schema_object || is_sql_type_into_pl(dest_addr, obj_array)) {
    ObObj src;
    CK (dest_addr.is_pl_extend());
    /* schema record can only exist as a separate into variable
          when copying, src id and dest id must be consistent. */
    CK (1 == obj_array.count());
    OX (src = obj_array.at(0));
    OV (src.is_pl_extend(), OB_ERR_EXPRESSION_WRONG_TYPE);
    OZ (check_and_deep_copy_result(*alloc, src, dest_addr));
  } else {
    int64_t current_datum = 0;
    bool is_opaque = false;
    if (dest_addr.is_pl_extend()) {
      if (PL_OPAQUE_TYPE == dest_addr.get_meta().get_extend_type()) {
        is_opaque = true;
        CK (1 == obj_array.count());
        OX (current_datum = reinterpret_cast<int64_t>(&dest_addr));
      } else {
        ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(dest_addr.get_ext());
        ObPLRecord *record = NULL;
        if (NULL == composite) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected composite to store datum", KPC(composite), K(dest_addr), K(obj_array), K(ret));
        } else if (!composite->is_record()) {
          // user_defined_sql_type can be cast into pl_extend, so it cannot be blocked in the front, 
          // but it is not allowed to be written into varray. 
          // Inserting user_defined_sql_type into the PL_VARRAY_TYPE type will take this part of the logic.
          ret = OB_ERR_WRONG_TYPE_FOR_VAR;
          LOG_WARN("expression 'string' in the INTO list is of wrong type", K(ret));
        } else if (OB_ISNULL(record = static_cast<ObPLRecord*>(composite))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected record to store datum", KPC(record), KPC(composite), K(ret));
        } else if (record->get_count() != obj_array.count() && record->get_id() != T_OBJ_SDO_GEOMETRY) {
          //Example: for idx (select obj(1,2) from dual) loop null; end loop;
          //which is not supported yet!!! Will fixed it later in OB 4.2 version.
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "record`s element count not equal to select item count");
          LOG_WARN("record`s element count not equal to select item count", K(ret), KPC(record), K(obj_array));
        } else {
          ObPLAllocator1 *pl_allocator = dynamic_cast<ObPLAllocator1 *>(composite->get_allocator());
          CK (OB_NOT_NULL(pl_allocator));
          CK (alloc == pl_allocator);
          if (OB_SUCC(ret)) {
            ObPLRecord *record = static_cast<ObPLRecord*>(composite);
            current_datum = reinterpret_cast<int64_t>(record->get_element());
            record->set_is_null(false);
            datum_allocator = composite->get_allocator();
          }
        }
      }
    } else { //must be a single Obj
      CK (1 == obj_array.count());
      OX (current_datum = reinterpret_cast<int64_t>(dest_addr.get_ext()));
    }

    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < obj_array.count(); ++i) {
        int tmp_ret = OB_SUCCESS;
        if ((tmp_ret = ObUserDefinedType::destruct_objparam(*alloc, obj_array.at(i), session_info)) != OB_SUCCESS) {
          LOG_WARN("failed to destruct obj, memory may leak", K(ret), K(tmp_ret), K(i), K(obj_array));
        }
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && !is_opaque && i < obj_array.count(); ++i) {
      if (OB_FAIL(store_datum(current_datum, obj_array.at(i), session_info, datum_allocator))) {
        LOG_WARN("failed to arrange store", K(dest_addr), K(i), K(obj_array.at(i)), K(obj_array), K(ret));
      }
    }
  }
  return ret;
}

int ObSPIService::store_datum(int64_t &current_addr, const ObObj &obj, ObSQLSessionInfo *session_info, ObIAllocator *alloc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 == current_addr) || obj.is_invalid_type() || OB_ISNULL(alloc)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(current_addr), K(obj), K(ret));
  } else {
    ObObj *cur_obj = reinterpret_cast<ObObj*>(current_addr);
    OZ (ObUserDefinedType::destruct_objparam(*alloc, *cur_obj, session_info));
    if (OB_SUCC(ret)) {
      new (cur_obj)ObObj(obj);
      current_addr += sizeof(ObObj);
    }
  }
  return ret;
}

int ObSPIService::fill_cursor(ObResultSet &result_set,
                              ObSPICursor *cursor,
                              int64_t new_query_start_time,
                              int64_t orc_max_ret_rows) {
  int ret = OB_SUCCESS;
  int64_t old_time_out_ts = THIS_WORKER.get_timeout_ts();
  if (OB_ISNULL(cursor) || OB_ISNULL(cursor->allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(cursor), K(ret));
  } else {
    const common::ObNewRow *row = NULL;
    const common::ColumnsFieldIArray *fields = result_set.get_field_columns();
    if (OB_ISNULL(fields)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    }
    if (OB_NOT_NULL(result_set.get_physical_plan()) && new_query_start_time > 0) {
      // fill_dbms_cursor do not need check hint , so only new_query_start_time > 0 will check hint timeout
      ObPhysicalPlan *plan = result_set.get_physical_plan();
      if (plan->get_phy_plan_hint().query_timeout_ > 0) {
        old_time_out_ts = THIS_WORKER.get_timeout_ts();
        THIS_WORKER.set_timeout_ts(new_query_start_time + plan->get_phy_plan_hint().query_timeout_);
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < fields->count(); ++i) {
      ObDataType type;
      type.set_meta_type(fields->at(i).type_.get_meta());
      type.set_accuracy(fields->at(i).accuracy_);
      if (OB_FAIL(cursor->row_desc_.push_back(type))) {
        LOG_WARN("push back error", K(i), K(fields->at(i).type_), K(fields->at(i).accuracy_),
                 K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < orc_max_ret_rows; i++) {
#ifdef ERRSIM
      ret = OB_E(EventTable::EN_SPI_GET_NEXT_ROW) OB_SUCCESS;
#endif
      if (FAILEDx(result_set.get_next_row(row))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("read result error", K(ret));
        }
        break;
      } else if (OB_ISNULL(row)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get a invalud row", K(ret));
      } else {
        ObNewRow tmp_row = *row;
        for (int64_t i = 0; OB_SUCC(ret) && i < tmp_row.get_count(); ++i) {
          ObObj& obj = tmp_row.get_cell(i);
          ObObj tmp;
          if (obj.is_pl_extend()) {
            if (OB_FAIL(pl::ObUserDefinedType::deep_copy_obj(*(cursor->allocator_), obj, tmp))) {
              LOG_WARN("failed to copy pl extend", K(ret));
            } else {
              obj = tmp;
              if (OB_FAIL(cursor->complex_objs_.push_back(tmp))) {
                int tmp_ret = ObUserDefinedType::destruct_obj(tmp, cursor->session_info_);
                LOG_WARN("fail to push back", K(ret), K(tmp_ret));
              }
            }
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(cursor->row_store_.add_row(tmp_row))) {
          LOG_WARN("failed to add row to row store", K(ret));
        }
      }
    }
  }
  THIS_WORKER.set_timeout_ts(old_time_out_ts);
  return ret;
}

int ObSPIService::fetch_row(void *result_set,
                            bool is_streaming,
                            int64_t &row_count,
                            ObNewRow &cur_row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(result_set)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Argument passed in is NULL", K(result_set), K(ret));
  } else if (!is_streaming) {
    ObSPICursor *cursor = static_cast<ObSPICursor*>(result_set);
    if (OB_FAIL(cursor->row_store_.get_row(cursor->cur_, cur_row))) {
      if (OB_INDEX_OUT_OF_RANGE == ret
          && (0 > cursor->cur_ || cursor->cur_ >= cursor->row_store_.get_row_cnt())) {
        ret = OB_ITER_END;
      } else {
        LOG_WARN("read result error", K(cursor->cur_), K(ret));
      }
    } else {
      ++cursor->cur_;
      ++row_count;
    }
  } else {
    ObResultSet *ob_result_set = static_cast<ObResultSet*>(result_set);
    const ObNewRow *row = NULL;
#ifdef ERRSIM
    ret = OB_E(EventTable::EN_SPI_GET_NEXT_ROW) OB_SUCCESS;
#endif
    if (FAILEDx(ob_result_set->get_next_row(row))) {
      // Upper layer checks return value, here no information is printed
    } else {
      cur_row = *row;
      ++row_count;
    }
  }
  LOG_DEBUG("spi fetch row", K(row_count), K(ret));
  return ret;
}

int ObSPIService::get_result_type(ObPLExecCtx &ctx, const ObSqlExpression &expr, ObExprResType &type)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx.params_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Argument is NULL", K(ret));
  } else {
    if (is_const_expression(expr)) {
      if (is_question_mark_expression(expr)) {
        int64_t idx = get_const_value(expr).get_int();
        if (idx >= ctx.params_->count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("idx is invlid", K(ret));
        } else {
          ObObjParam &obj_param = ctx.params_->at(idx);
          if (idx < ctx.func_->get_variables().count() &&
              ctx.func_->get_variables().at(idx).is_obj_type() &&
              OB_NOT_NULL(ctx.func_->get_variables().at(idx).get_data_type())) {
            type.set_meta(ctx.func_->get_variables().at(idx).get_data_type()->get_meta_type());
            type.set_accuracy(ctx.func_->get_variables().at(idx).get_data_type()->get_accuracy());
          } else {
            type.set_meta(obj_param.get_meta());
            type.set_accuracy(obj_param.get_accuracy());
          }
        }
      } else {
        ObObjType obj_type = static_cast<ObObjType>(get_expression_type(expr));
        type.set_type(obj_type);
        type.set_accuracy(get_first_expr_item(expr).get_accuracy());
      }
    } else {
      const ObExprOperator *op = get_first_expr_item(expr).get_expr_operator();
      if (OB_ISNULL(op)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr operator is NULL", K(ret));
      } else {
        type = op->get_result_type();
      }
    }
  }
  return ret;
}


const ObInfixExprItem &ObSPIService::get_first_expr_item(const ObSqlExpression &expr)
{
  return expr.get_expr_items().at(0);
}

ObItemType ObSPIService::get_expression_type(const ObSqlExpression &expr)
{
  return get_first_expr_item(expr).get_item_type();
}

const ObObj &ObSPIService::get_const_value(const ObSqlExpression &expr)
{
  return get_first_expr_item(expr).get_obj();
}

bool ObSPIService::is_question_mark_expression(const ObSqlExpression &expr)
{
  return T_QUESTIONMARK == get_expression_type(expr);
}

bool ObSPIService::is_const_expression(const ObSqlExpression &expr)
{
  return IS_CONST_TYPE(get_expression_type(expr));
}

bool ObSPIService::is_obj_access_expression(const ObSqlExpression &expr)
{
  return T_OBJ_ACCESS_REF == get_expression_type(expr);
}

bool ObSPIService::is_get_var_func_expression(const ObSqlExpression &expr)
{
  return T_OP_GET_USER_VAR == get_expression_type(expr)
         || T_OP_GET_SYS_VAR == get_expression_type(expr);
}

bool ObSPIService::is_get_package_or_subprogram_var_expression(const ObSqlExpression &expr)
{
  return T_OP_GET_PACKAGE_VAR == get_expression_type(expr) || T_OP_GET_SUBPROGRAM_VAR == get_expression_type(expr);
}

int ObSPIService::force_refresh_schema(int64_t refresh_version)
{
  int ret = OB_SUCCESS;
  int64_t local_version = OB_INVALID_VERSION;
  int64_t global_version = OB_INVALID_VERSION;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is NULL", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_refreshed_schema_version(
                     local_version))) {
    LOG_WARN("fail to get local version", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_published_schema_version(
                     global_version))) {
    LOG_WARN("fail to get global version", K(ret));
  }
  if (OB_SUCC(ret)) {
    int64_t need_refresh_version = OB_INVALID_VERSION == refresh_version ? global_version : refresh_version;
    if (local_version >= need_refresh_version) {
      // do nothing
    } else if (OB_FAIL(GCTX.schema_service_->async_refresh_schema(need_refresh_version))) {
      LOG_WARN("failed to refresh schema",
              K(ret), K(local_version), K(global_version), K(refresh_version));
    }
  }
  return ret;
}

int ObSPIService::resolve_exec_params(const ParseResult &parse_result,
                                      ObSQLSessionInfo &session,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      sql::ObRawExprFactory &expr_factory,
                                      pl::ObPLBlockNS &secondary_namespace,
                                      ObSPIPrepareResult &prepare_result,
                                      common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  for (ParamList *current_param = parse_result.param_nodes_;
       OB_SUCC(ret) && NULL != current_param;
       current_param = current_param->next_) {
    ObRawExpr* expr = NULL;
    if (OB_ISNULL(current_param->node_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("param node is NULL", K(ret));
    } else {
      if (session.is_for_trigger_package()
          && ObRawExprUtils::is_new_old_column_ref(current_param->node_)) {
        ParseNode *obj_access_node = NULL;
        OZ (ObRawExprUtils::mock_obj_access_ref_node(allocator, obj_access_node,
                                                     current_param->node_,
                                                     prepare_result.tg_timing_event_));
        CK (OB_NOT_NULL(obj_access_node));
        OZ (ObPLResolver::resolve_raw_expr(*obj_access_node,
                                           allocator,
                                           expr_factory,
                                           secondary_namespace,
                                           false, /*is_prepare_protocol*/
                                           expr));
      } else {
        OZ (ObPLResolver::resolve_local_var(*current_param->node_,
                                            secondary_namespace,
                                            expr_factory,
                                            &session,
                                            &schema_guard,
                                            expr));
      }
    }
    if (OB_FAIL(ret)) {
      // do noting
    } else if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null expr", K(ret));
    // prepare protocol does not support execution parameters being ENUM SET, here we convert ENUM SET to VARCHAR
    } else if (ob_is_enum_or_set_type(expr->get_result_type().get_type())) {
      ObSysFunRawExpr *out_expr = NULL;
      if (OB_FAIL(ObRawExprUtils::create_type_to_str_expr(expr_factory,
                                                          expr,
                                                          out_expr,
                                                          &session,
                                                          false))) {
        LOG_WARN("failed to create type to str expr", K(ret));
      } else if (OB_ISNULL(out_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null raw expr", K(ret));
      } else {
        expr = out_expr;
      }
    }
    if (OB_SUCC(ret)) {
      bool find  = false;
      for (int64_t i = 0; OB_SUCC(ret) && !find && i < prepare_result.exec_params_.count(); ++i) {
        CK (OB_NOT_NULL(prepare_result.exec_params_.at(i)));
        if (OB_SUCC(ret)) {
          if (prepare_result.exec_params_.at(i)->same_as(*expr)) {
            find = true;
          }
        }
      }
      if (OB_SUCC(ret) && !find) {
        OZ (prepare_result.exec_params_.push_back(expr));
      }
    }
  }
  return ret;
}

int ObSPIService::resolve_into_params(const ParseResult &parse_result,
                                      ObSQLSessionInfo &session,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      sql::ObRawExprFactory &expr_factory,
                                      pl::ObPLBlockNS &secondary_namespace,
                                      ObSPIPrepareResult &prepare_result)
{
  int ret = OB_SUCCESS;
  ParseNode *into_node = NULL;
  if (OB_ISNULL(parse_result.result_tree_)
      || OB_UNLIKELY(T_STMT_LIST != parse_result.result_tree_->type_)
      || OB_ISNULL(parse_result.result_tree_->children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("result tree is NULL or invalid result tree", K(ret));
  } else if (T_CREATE_VIEW == parse_result.result_tree_->children_[0]->type_) {
    if (parse_result.result_tree_->children_[0]->num_child_ <= 3
        || OB_ISNULL(parse_result.result_tree_->children_[0]->children_[3])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result tree is NULL or invalid result tree", K(ret));
    } else if (OB_FAIL(ObResolverUtils::get_select_into_node(*parse_result.result_tree_->children_[0]->children_[3],
                                                             into_node,
                                                             true))) {
      LOG_WARN("wrong usage of into clause", K(ret));
    } else if (OB_NOT_NULL(into_node)) {
      ret = OB_ERR_VIEW_SELECT_CONTAIN_INTO;
      LOG_WARN("View's SELECT contains a 'INTO' clause.", K(ret));
    }
  } else if (OB_FAIL(ObResolverUtils::get_select_into_node(*parse_result.result_tree_->children_[0],
                                                           into_node,
                                                           true))) {
      LOG_WARN("wrong usage of into clause", K(ret));
  } else if (T_SELECT == parse_result.result_tree_->children_[0]->type_
      && NULL != into_node
      && T_INTO_VARIABLES == into_node->type_) {
    if (OB_ISNULL(into_node->children_[0])
        || OB_UNLIKELY(T_INTO_VARS_LIST != into_node->children_[0]->type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid into vars", K(ret));
    } else {
      ParseNode *into_list = into_node->children_[0];
      for (int64_t i = 0; OB_SUCC(ret) && i < into_list->num_child_; ++i) {
        ObRawExpr* expr = NULL;
        if (OB_ISNULL(into_list->children_[i])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("param node is NULL", K(ret));
        } else if (T_IDENT == into_list->children_[i]->type_) {
          if (OB_FAIL(ObPLResolver::resolve_local_var(*into_list->children_[i],
                                                      secondary_namespace,
                                                      expr_factory,
                                                      &session,
                                                      &schema_guard,
                                                      expr))) {
            LOG_WARN("failed to resolve_local_var", K(ret));
          }
        } else if (T_USER_VARIABLE_IDENTIFIER == into_list->children_[i]->type_ ) {
          if (OB_FAIL(ObRawExprUtils::build_get_user_var(expr_factory,
                                                         ObString(into_list->children_[i]->str_len_,
                                                                  into_list->children_[i]->str_value_),
                                                         expr,
                                                         &session))) {
            LOG_WARN("Failed to build get user var", K(into_list->children_[i]->str_value_), K(ret));
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid into node", K(ret));
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(prepare_result.into_exprs_.push_back(expr))) {
            LOG_WARN("push_back error", K(ret));
          }
        }
      }
    }
  } else { /*do nothing*/ }
  return ret;
}

int ObSPIService::resolve_ref_objects(const ParseResult &parse_result,
                                      ObSQLSessionInfo &session,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      ObSPIPrepareResult &prepare_result)
{
  int ret = OB_SUCCESS;
  for (RefObjList *current_obj = parse_result.pl_parse_info_.ref_object_nodes_;
       OB_SUCC(ret) && NULL != current_obj;
       current_obj = current_obj->next_) {
    if (OB_ISNULL(current_obj->node_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("param node is NULL", K(ret));
    } else {
      switch (current_obj->type_) {
      case REF_REL: {
        ObString db_name;
        ObString rel_name;
        const ObTableSchema *table_schema = NULL;
        if (OB_FAIL(ObStmtResolver::resolve_ref_factor(current_obj->node_, &session, rel_name, db_name))) {
          LOG_WARN("failed to resolve ref factor", K(ret));
        } else if (OB_FAIL(schema_guard.get_table_schema(
                                                         db_name,
                                                         rel_name,
                                                         false,
                                                         table_schema))) {
          if (OB_TABLE_NOT_EXIST == ret) {
            // Object not found is normal, do nothing
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("relation in pl not exists", K(db_name), K(rel_name), K(ret));
          }
        } else if (NULL ==table_schema) {
          // Object not found is normal, do nothing
        } else {
          ObSchemaObjVersion obj_version(table_schema->get_table_id(), table_schema->get_schema_version(), DEPENDENCY_TABLE);
          if (OB_FAIL(prepare_result.ref_objects_.push_back(obj_version))) {
            LOG_WARN("push_back error", K(ret));
          }
        }
      }
        break;
      case REF_PROC: {
        // procedure will not appear in DML statements
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("should never be here", K(ret));
      }
        break;
      case REF_FUNC: {
        ObUDFInfo udf_info;
        ParseNode *node = current_obj->node_;
        ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
        bool need_check_udf = true;
        if (OB_FAIL(session.get_name_case_mode(case_mode))) {
          LOG_WARN("fail to get name case mode", K(ret));
        } else if (T_FUN_SYS == node->type_) {
          ObString func_name(static_cast<int32_t>(node->children_[0]->str_len_), node->children_[0]->str_value_);
          if (IS_FUN_SYS_TYPE(ObExprOperatorFactory::get_type_by_name(func_name))) {
            need_check_udf = false;
          }
        } else if (IS_FUN_SYS_TYPE(node->type_) || IS_AGGR_FUN(node->type_)) {
          need_check_udf = false;
        } else {}
        if (OB_SUCC(ret) && need_check_udf) {
          ParseNode *udf_node = node;
          if (T_FUN_SYS == node->type_) {
            ObString empty_db_name;
            ObString empty_pkg_name;
            if (OB_FAIL(ObResolverUtils::transform_func_sys_to_udf(reinterpret_cast<ObIAllocator *>(parse_result.malloc_pool_),
                node, empty_db_name, empty_pkg_name, udf_node))) {
              LOG_WARN("transform func sys to udf node failed", K(ret));
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(ObResolverUtils::resolve_udf_name_by_parse_node(udf_node, case_mode, udf_info))) {
              LOG_WARN("fail to resolve udf name", K(ret));
            } else if (udf_info.udf_database_.empty()) {
              udf_info.udf_database_ = session.get_database_name();
            }
            if (OB_SUCC(ret)) {
              ObSchemaChecker schema_checker;
              const ObRoutineInfo *func_info = NULL;
              if (OB_FAIL(schema_checker.init(schema_guard, session.get_server_sid()))) {
                LOG_WARN("fail to init schema checker", K(ret));
              } else if (OB_FAIL(schema_checker.get_standalone_function_info(
                                                  udf_info.udf_database_,
                                                  udf_info.udf_name_,
                                                  func_info))) {
                if (ret == OB_ERR_SP_DOES_NOT_EXIST) {
                  // Object not found is normal, do nothing
                  ret = OB_SUCCESS;
                } else {
                  LOG_WARN("fail to get standalone function info",
                            K(ret), K(udf_info));
                }
              } else if (OB_ISNULL(func_info)) {
                // Object not found is normal, do nothing
              } else {
                ObSchemaObjVersion obj_version(func_info->get_routine_id(),
                                                 func_info->get_schema_version(),
                                                 DEPENDENCY_FUNCTION);
                if (OB_FAIL(prepare_result.ref_objects_.push_back(obj_version))) {
                  LOG_WARN("push_back error", K(ret));
                }
              }
            }
          }
        }
      }
        break;
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexcepted ref object type", K(current_obj->type_), K(ret));
      }
      }
    }
  }
  return ret;
}

int ObSPIService::spi_check_composite_not_null(ObObjParam *v)
{
  int ret = OB_SUCCESS;
  ObPLComposite * composite = NULL;
  if (OB_NOT_NULL(v)
      && v->is_pl_extend()
      && OB_NOT_NULL(composite = reinterpret_cast<ObPLComposite *>(v->get_ext()))
      && composite->is_collection()) {
    ObPLCollection *coll = static_cast<ObPLCollection *>(composite);
    CK (OB_NOT_NULL(coll));
    if (OB_SUCC(ret) && coll->is_collection_null()) {
      ret = OB_ERR_NUMERIC_OR_VALUE_ERROR;
      LOG_WARN("not null check violated", K(coll),
                                          K(coll->is_not_null()),
                                          K(coll->get_count()),
                                          K(ret));
    }
  }
  return ret;
}

int ObSPIService::spi_update_location(pl::ObPLExecCtx *ctx, uint64_t location)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ctx)
      && OB_NOT_NULL(ctx->pl_ctx_)
      && ctx->pl_ctx_->get_exec_stack().count() > 0) {
    ObIArray<ObPLExecState *> &stack = ctx->pl_ctx_->get_exec_stack();
    ObPLExecState *state = stack.at(stack.count() - 1);
    state->set_loc(location);
  }
  return ret;
}


int ObSPIService::spi_opaque_assign_null(int64_t opaque_ptr)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObSPIService::setup_cursor_snapshot_verify_(ObPLCursorInfo *cursor, ObSPIResultSet *spi_result)
{
  int ret = OB_SUCCESS;
  ObExecContext &exec_ctx = spi_result->get_result_set()->get_exec_context();
  transaction::ObTxReadSnapshot &snapshot = exec_ctx.get_das_ctx().get_snapshot();
  transaction::ObTxDesc *tx = exec_ctx.get_my_session()->get_tx_desc();
  bool need_register_snapshot = false;
  if (!snapshot.is_valid()) {
    need_register_snapshot = false;
  } else if (cursor->is_for_update()) {
    if (!tx || !data_plane::tx_desc_id(tx).is_valid() || !snapshot.tx_id().is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("for update cursor opened but not trans id invalid", K(ret),
                "tx", data_plane::ObTxDescLogView(tx), K(snapshot));
    }
    need_register_snapshot = true;
  } else if (cursor->is_streaming() && tx
      && data_plane::tx_desc_is_in_tx(tx)) {
    need_register_snapshot = true;
  }
  if (OB_FAIL(ret)) {
  } else if (need_register_snapshot) {
    OZ (cursor->set_and_register_snapshot(snapshot));
  } else {
    cursor->set_snapshot(snapshot);
  }
  LOG_TRACE("cursor setup snapshot", K(cursor->is_for_update()), K(cursor->is_streaming()),
            K(snapshot), KPC(cursor), K(need_register_snapshot));
  return ret;
}

ObPLSubPLSqlTimeGuard::ObPLSubPLSqlTimeGuard(pl::ObPLExecCtx *ctx) : 
  old_sub_plsql_exec_time_(-1),
  execute_start_(ObTimeUtility::current_time()),
  state_(NULL)
{
  if (OB_NOT_NULL(ctx)
      && OB_NOT_NULL(ctx->pl_ctx_)
      && ctx->pl_ctx_->get_exec_stack().count() > 0) {
    // hold old time and reset sub time
    ObIArray<ObPLExecState *> &stack = ctx->pl_ctx_->get_exec_stack();
    state_ = stack.at(stack.count()-1);
    old_sub_plsql_exec_time_ = state_->get_sub_plsql_exec_time();
    old_pure_sql_exec_time_ = state_->get_pure_sql_exec_time();
    state_->reset_sub_plsql_exec_time();
    state_->reset_pure_sql_exec_time();
  }
}

ObPLSubPLSqlTimeGuard::~ObPLSubPLSqlTimeGuard()
{
  int ret = OB_SUCCESS;
  int64_t sql_exec_time = ObTimeUtility::current_time() - execute_start_;
  if (OB_NOT_NULL(state_)) {
    if (state_->get_sub_plsql_exec_time() > 0) {
      // At this point, the pure SQL time has already been added to the upper layer by add_pl_exec_time
      LOG_DEBUG("<<< sql exec time ", K(sql_exec_time), K(state_->get_sub_plsql_exec_time()), 
        K(sql_exec_time-state_->get_sub_plsql_exec_time()), K(state_->get_pure_sql_exec_time()),
        K(old_sub_plsql_exec_time_), K(old_pure_sql_exec_time_));
    }
    // update sub plsql exec time
    if (-1 != old_sub_plsql_exec_time_) {
      state_->add_sub_plsql_exec_time(old_sub_plsql_exec_time_);
    }
    if (-1 != old_pure_sql_exec_time_) {
      state_->add_pure_sql_exec_time(old_pure_sql_exec_time_);
    }
  }
}

ObSPIRetryCtrlGuard::ObSPIRetryCtrlGuard(
  ObQueryRetryCtrl &retry_ctrl, ObSPIResultSet &spi_result, ObSQLSessionInfo &session_info, int &ret, bool for_fetch)
  : retry_ctrl_(retry_ctrl), spi_result_(spi_result), session_info_(session_info), ret_(ret), init_(false)
{
  int64_t database_schema_version = 0;
  
  if (!for_fetch) {
    spi_result_.get_out_params().reset();
    spi_result_.reset_member_for_retry(session_info_);
  }
  retry_ctrl_.clear_state_before_each_retry(session_info_.get_retry_info_for_update());
  if (THIS_WORKER.is_timeout()) {
    ret = OB_TIMEOUT;
    LOG_WARN("already timeout!", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(spi_result_.get_scheme_guard()))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else if (OB_FAIL(spi_result_.get_scheme_guard().get_schema_version(database_schema_version))) {
    LOG_WARN("fail get schema version", K(ret));
  } else {
    retry_ctrl_.set_current_local_schema_version(database_schema_version);
    spi_result_.get_sql_ctx().schema_guard_ = &spi_result.get_scheme_guard();
    init_ = true;
  }
}

ObSPIRetryCtrlGuard::~ObSPIRetryCtrlGuard()
{
}

void ObSPIRetryCtrlGuard::test()
{
  int &ret = ret_;
  if (init_ && OB_FAIL(ret) && spi_result_.get_result_set() != NULL) {
    int cli_ret = OB_SUCCESS;
    retry_ctrl_.test_and_save_retry_state(GCTX,
                                          spi_result_.get_sql_ctx(),
                                          *(spi_result_.get_result_set()),
                                          ret,
                                          cli_ret,
                                          true,   /*force_local_retry in SPI, we can only do local retry*/
                                          false,  /*is_inner_sql*/
                                          true);  /*is_part_of_pl_sql*/
    ret = cli_ret;
    spi_result_.get_sql_ctx().clear();
    session_info_.set_session_in_retry(retry_ctrl_.need_retry());
  }
}

ObSPIExecEnvGuard::ObSPIExecEnvGuard(
  ObSQLSessionInfo &session_info, ObSPIResultSet &spi_result, bool is_ps_cursor)
  : session_info_(session_info), spi_result_(spi_result), is_ps_cursor_(is_ps_cursor)
{
  if (!is_ps_cursor_) {
    query_start_time_bk_ = session_info.get_query_start_time();
    session_info.set_query_start_time(ObTimeUtility::current_time());
  }
}

ObSPIExecEnvGuard::~ObSPIExecEnvGuard()
{
  session_info_.get_retry_info_for_update().clear();
  if (!is_ps_cursor_) {
    session_info_.set_query_start_time(query_start_time_bk_);
  }
}

int ObSPICursor::release_complex_obj(ObObj &complex_obj)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(pl::ObUserDefinedType::destruct_obj(complex_obj, session_info_))) {
    LOG_WARN("failed to destruct obj", K(ret));
  }
  return ret;
}

}
}
