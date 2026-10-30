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

#define USING_LOG_PREFIX  SQL_ENG
#include "sql/engine/cmd/ob_get_diagnostics_executor.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "sql/session/ob_inner_sql_connection.h"
#include "share/ob_lob_access_utils.h"
#include "sql/session/ob_basic_session_info.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace sql
{

namespace {
int make_diagnostic_value(SqlValueType type, int64_t integer, const ObString &text,
                          bool has_lob_header, ObCollationType collation,
                          ObIAllocator &allocator, DatumParameter &output)
{
  int ret = OB_SUCCESS;
  DatumParameter value;
  switch (type) {
    case ObTinyIntType: ret = value.set_tinyint(integer, allocator); break;
    case ObSmallIntType: ret = value.set_smallint(integer, allocator); break;
    case ObMediumIntType: ret = value.set_mediumint(integer, allocator); break;
    case ObInt32Type: ret = value.set_int32(integer, allocator); break;
    case ObIntType: ret = value.set_int(integer, allocator); break;
    case ObUTinyIntType: ret = value.set_utinyint(integer, allocator); break;
    case ObUSmallIntType: ret = value.set_usmallint(integer, allocator); break;
    case ObUMediumIntType: ret = value.set_umediumint(integer, allocator); break;
    case ObUInt32Type: ret = value.set_uint32(integer, allocator); break;
    case ObUInt64Type: ret = value.set_uint64(integer, allocator); break;
    case ObFloatType: ret = value.set_float(integer, allocator); break;
    case ObDoubleType: ret = value.set_double(integer, allocator); break;
    case ObUFloatType:
      ret = value.set_float(integer, allocator); value.set_type(type); break;
    case ObUDoubleType:
      ret = value.set_double(integer, allocator); value.set_type(type); break;
    case ObNumberType:
    case ObUNumberType: {
      number::ObNumber number;
      if (OB_FAIL(number.from(integer, allocator))) {
      } else if (OB_FAIL(value.set_number(number, allocator))) {
      } else { value.set_type(type); }
      break;
    }
    case ObVarcharType:
    case ObCharType:
      // text may point at the caller's formatting buffer.
      ret = value.set_string(type, text, allocator);
      value.set_collation_level(CS_LEVEL_COERCIBLE);
      value.set_collation_type(collation);
      break;
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType:
    case ObJsonType: {
      ObTextStringResult lob(type, has_lob_header, &allocator);
      if (OB_FAIL(lob.init(text.length()))) {
      } else if (OB_FAIL(lob.append(text))) {
      } else {
        ObString data;
        lob.get_result_buffer(data);
        // ObTextStringResult leaves its allocator-owned buffer alive.
        value.set_string_view(type, data);
        value.set_collation_level(CS_LEVEL_COERCIBLE);
        value.set_collation_type(collation);
        if (lob.has_lob_header()) { value.set_has_lob_header(); }
      }
      break;
    }
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "not supported pl var type");
      break;
  }
  if (OB_SUCC(ret)) { value.set_param_meta(); output = value; }
  return ret;
}
} // namespace

ObGetDiagnosticsExecutor::ObGetDiagnosticsExecutor() {}

ObGetDiagnosticsExecutor::~ObGetDiagnosticsExecutor() {}

int ObGetDiagnosticsExecutor::assign_condition_val(ObExecContext &ctx, ObGetDiagnosticsStmt &stmt,
                          ObSQLSessionInfo *session_info,
                          sqlclient::ObISQLConnection *conn,
                          int64_t err_ret, ObString err_msg_c, ObString sql_state_c)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session_info));
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_info_argument().count(); ++i) {
    int64_t affected_rows = 0;
    ObString var;
    ObSqlString set_sql;
    const ObString &val = stmt.get_info_argument().at(i);
    const ObRawExpr* var_expr = stmt.get_params().at(i + 1);
    CK (OB_NOT_NULL(var_expr));
    if (OB_FAIL(ret)) {
    } else if (T_OP_GET_USER_VAR == var_expr->get_expr_type()) {
      const ObSysFunRawExpr *func_expr = static_cast<const ObSysFunRawExpr*>(var_expr);
      if (OB_ISNULL(func_expr->get_param_expr(0))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(!func_expr->get_param_expr(0)->is_const_raw_expr()
        || !static_cast<const ObConstRawExpr*>(func_expr->get_param_expr(0))->get_value().is_varchar())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        var = static_cast<const ObConstRawExpr*>(func_expr->get_param_expr(0))->get_value().get_varchar();
      }

      DIAG_INFO_TYPE info_type;
      OZ (stmt.get_diag_info_type_by_name(val, info_type));
      if (OB_SUCC(ret)) {
        switch (info_type) {
          case MYSQL_ERRNO_TYPE:
            OZ(set_sql.assign_fmt("set %s%s=\"%d\";", "@", var.ptr(), ob_errpkt_errno(err_ret)));
            break;
          case MESSAGE_TEXT_TYPE: {
            OZ(set_sql.assign_fmt("set %s%s=", "@", var.ptr()));
            OZ(sql_append_hex_escape_str(err_msg_c, set_sql));
            break;
          }
          case RETURNED_SQLSTATE_TYPE: {
            OZ(set_sql.assign_fmt("set %s%s=", "@", var.ptr()));
            OZ(sql_append_hex_escape_str(err_ret > 0 ? sql_state_c : ob_sqlstate(err_ret), set_sql));
            break;
          }
          case CLASS_ORIGIN_TYPE:
          case SUBCLASS_ORIGIN_TYPE:
            OZ(set_sql.assign_fmt("set %s%s=\"%s\";", "@", var.ptr(), "ISO 9075"));
            break;
          case TABLE_NAME_TYPE:
          case COLUMN_NAME_TYPE:
          case CONSTRAINT_CATALOG_TYPE:
          case CONSTRAINT_SCHEMA_TYPE:
          case CONSTRAINT_NAME_TYPE:
          case CATALOG_NAME_TYPE:
          case SCHEMA_NAME_TYPE:
          case CURSOR_NAME_TYPE:
            /* do nothing */
            break;
          default:
            ret = OB_ERR_UNEXPECTED;
            break;
        }
      }

      CK (OB_NOT_NULL(conn));
      if (OB_SUCC(ret) && set_sql.length() != 0) {
        if (OB_FAIL(conn->execute_write(set_sql.ptr(), affected_rows))) {
        }
      }
    } else if (OB_LIKELY(var_expr->is_const_raw_expr())) {
      const ObConstRawExpr *const_expr = static_cast<const ObConstRawExpr *>(var_expr);
      if (T_QUESTIONMARK == const_expr->get_expr_type()) {
        ObCollationType collation_type;
        DatumParameter result;
        pl::ObPLContext *pl_context = nullptr;
        pl::ObPLExecState *pl_state = nullptr;
        char str[50] = {0};
        CK (OB_NOT_NULL(pl_context = session_info->get_pl_context()));
        CK (OB_NOT_NULL(pl_state = pl_context->get_current_state()));
        OZ (session_info->get_collation_connection(collation_type));
        DIAG_INFO_TYPE info_type;
        OZ (stmt.get_diag_info_type_by_name(val, info_type));
        int64_t idx = const_expr->get_value().get_unknown();
        idx = stmt.get_origin_param_index(idx);
        CK (idx != OB_INVALID_INDEX);
        CK (idx < pl_state->get_function().get_variables().count());
        if (OB_SUCC(ret)) {
          pl::ObPLDataType pl_data_type = pl_state->get_function().get_variables().at(idx);
          CK (nullptr != pl_data_type.get_data_type());
          if (OB_SUCC(ret)) {
            SqlValueType expect_type = pl_data_type.get_data_type()->get_obj_type();
            bool has_lob_header = pl_data_type.get_data_type()->get_meta_type().has_lob_header();
            switch (info_type) {
              case MYSQL_ERRNO_TYPE:
                {
                  int64_t errnum = ob_errpkt_errno(err_ret);
                  sprintf(str, "%ld", errnum);
                  OZ(make_diagnostic_value(expect_type, errnum, ObString(str), has_lob_header, collation_type, ctx.get_allocator(), result));
                }
                break;
              case MESSAGE_TEXT_TYPE:
                {
                  OZ(make_diagnostic_value(expect_type, (int64_t)0, err_msg_c, has_lob_header, collation_type, ctx.get_allocator(), result));
                }
                break;
              case RETURNED_SQLSTATE_TYPE:
                {
                  ObString sqlstate(ob_sqlstate(err_ret));
                  if (err_ret > 0) {
                    OZ(make_diagnostic_value(expect_type, (int64_t)0, sql_state_c, has_lob_header, collation_type, ctx.get_allocator(), result));
                  } else {
                    OZ(make_diagnostic_value(expect_type, (int64_t)0, sqlstate, has_lob_header, collation_type, ctx.get_allocator(), result));
                  }
                }
                break;
              case CLASS_ORIGIN_TYPE:
              case SUBCLASS_ORIGIN_TYPE:
                {
                  OZ(make_diagnostic_value(expect_type, (int64_t)0, ObString("ISO 9075"), has_lob_header, collation_type, ctx.get_allocator(), result));
                }
                break;
              case COLUMN_NAME_TYPE:
              case CONSTRAINT_CATALOG_TYPE:
              case CONSTRAINT_SCHEMA_TYPE:
              case CONSTRAINT_NAME_TYPE:
              case CATALOG_NAME_TYPE:
              case SCHEMA_NAME_TYPE:
              case CURSOR_NAME_TYPE:
                /* do nothing */
                break;
              default:
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "diag info type");
                break;
            }
          }
        }

        if (OB_SUCC(ret)) {
          OZ (pl_state->set_var(idx, result));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObGetDiagnosticsExecutor::get_condition_num(ObExecContext &ctx, ObGetDiagnosticsStmt &stmt, int64_t &num)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  pl::ObPLContext *pl_context = nullptr;
  pl::ObPLExecState *pl_state = nullptr;
  const ObRawExpr* cond_argument = nullptr;

  CK(OB_NOT_NULL(session_info));
  if (0 == stmt.get_params().count()) {
    /* An OB_ERR_BAD_FIELD_ERROR error occurred, but the statement can still execute normally */
    ret = OB_ERR_BAD_FIELD_ERROR;
  } else {
    CK (stmt.get_params().count() == stmt.get_info_argument().count() + 1);
    OX (cond_argument = stmt.get_params().at(0));
    CK (OB_NOT_NULL(cond_argument));
    if (OB_SUCC(ret)) {
      if (OB_LIKELY(cond_argument->is_const_raw_expr())) {
        const ObConstRawExpr *const_expr = static_cast<const ObConstRawExpr *>(cond_argument);
        if (T_QUESTIONMARK == const_expr->get_expr_type()) {
          CK (OB_NOT_NULL(pl_context = session_info->get_pl_context()));
          CK (OB_NOT_NULL(pl_state = pl_context->get_current_state()));
          DatumParameter value;
          int64_t idx = const_expr->get_value().get_unknown();
          idx = stmt.get_origin_param_index(idx);
          CK (idx != OB_INVALID_INDEX);
          OZ (pl_state->get_var(idx, value));
          OX (num = value.get_int());
          if (OB_SUCC(ret)) {
            ret = (num <= 0 || num >= MAX_BUFFER_SIZE) ? OB_ERR_INVALID_CONDITION_NUMBER : ret;
          }
        } else if (T_INT == const_expr->get_expr_type() && const_expr->get_value().is_integer_type()) {
          num = const_expr->get_value().get_int();
          if (OB_SUCC(ret)) {
            ret = (num <= 0 || num >= MAX_BUFFER_SIZE) ? OB_ERR_INVALID_CONDITION_NUMBER : ret;
          }
        } else if (T_VARCHAR == cond_argument->get_expr_type()) {
          //to make '12321' string be a number, 'as2da3' string still be a string.
          ObString arg_str = const_expr->get_value().get_string();
          if (arg_str.ptr() == NULL) {
            ret = OB_ERR_INVALID_CONDITION_NUMBER;
          } else {
            int64_t number = strtoll(arg_str.ptr(), NULL, 10);
            if (INT64_MAX == number || number <= 0 || number >= MAX_BUFFER_SIZE) {
              ret = OB_ERR_INVALID_CONDITION_NUMBER;
            } else {
              num = number;
            }
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
        }
      } else if (T_OP_GET_USER_VAR == cond_argument->get_expr_type()) {
        const ObSysFunRawExpr *func_expr = static_cast<const ObSysFunRawExpr*>(cond_argument);
        if (OB_ISNULL(func_expr->get_param_expr(0))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_UNLIKELY(!func_expr->get_param_expr(0)->is_const_raw_expr()
          || !static_cast<const ObConstRawExpr*>(func_expr->get_param_expr(0))->get_value().is_varchar())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObString var_name = static_cast<const ObConstRawExpr*>(func_expr->get_param_expr(0))->get_value().get_varchar();
          DatumValue value;
          int64_t number = 0;
          if (OB_FAIL(session_info->get_user_variable_value(var_name, value))) {
          } else if (value.is_integer_type()) {
            OZ (value.get_int(num));
          } else if (value.is_string_type()) {
            ObString str;
            if (OB_FAIL(value.get_varchar(str))) {
            } else if (FALSE_IT(number = strtoll(str.ptr(), NULL, 10))) {
            } else if (INT64_MAX == number || number <= 0 || number >= MAX_BUFFER_SIZE) {
              ret = OB_ERR_INVALID_CONDITION_NUMBER;
            } else {
              num = number;
            }
          }
        }
      } else if (T_OP_EXCEPT == cond_argument->get_expr_type()) {
        ret = OB_ERR_USER_VARIABLE_UNKNOWN;
      }
    }
  }

  return ret;
}
 
int ObGetDiagnosticsExecutor::execute(ObExecContext &ctx, ObGetDiagnosticsStmt &stmt) {
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = ctx.get_my_session();
  ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
  sqlclient::ObISQLConnection *conn = NULL;
  sqlclient::ObISQLConnectionGuard conn_guard;
   
  int64_t warning_count = 0;
  ObSqlString query_virtual;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(
                 query::ObInnerSQLConnectionAccess::
                     create_connection_with_external_session(
                         session_info, conn_guard))) {
  } else if (OB_ISNULL(conn = conn_guard.get_ptr())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(query_virtual.assign_fmt("select count(*) from %s.%s", 
                      OB_SYS_DATABASE_NAME, OB_ALL_VIRTUAL_WARNING_TNAME))) {
  } else {
    SMART_VAR(ObISQLClient::ReadResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(conn->execute_read(query_virtual.ptr(), res))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_SUCC(result->next())) {
        EXTRACT_INT_FIELD_MYSQL(*result, "count(*)", warning_count, int64_t);
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (stmt.get_diagnostics_type() == DiagnosticsType::GET_CURRENT_COND) {
    int64_t restored_arg;
    OZ (get_condition_num(ctx, stmt, restored_arg));
    if (OB_ERR_INVALID_CONDITION_NUMBER == ret) {
      ret = OB_SUCCESS;
      LOG_USER_WARN(OB_ERR_INVALID_CONDITION_NUMBER);
    } else if (OB_ERR_USER_VARIABLE_UNKNOWN == ret || OB_OBJ_TYPE_ERROR == ret) {
      ret = OB_SUCCESS;
      LOG_USER_WARN(OB_ERR_INVALID_CONDITION_NUMBER);
    } else if (OB_ERR_BAD_FIELD_ERROR == ret) {
      ret = OB_SUCCESS;
      ObString scope_name = ObString::make_string("field_list");
      ObString invalid_condition_name = stmt.get_invalid_condition_name();
      LOG_USER_WARN(OB_ERR_BAD_FIELD_ERROR,
                    invalid_condition_name.length(), invalid_condition_name.ptr(),
                    static_cast<int32_t>(scope_name.length()), scope_name.ptr());
    } else if (OB_FAIL(ret)) {
    } else if (warning_count < restored_arg || restored_arg < 1) {
      LOG_USER_WARN(OB_ERR_INVALID_CONDITION_NUMBER);
    } else {
      int err_ret;
      ObString err_msg, err_msg_c, sqlstate, sqlstate_c;
      ObSqlString query_virtual;
      if (OB_FAIL(query_virtual.assign_fmt(
        "select message, ori_code, sql_state from %s.%s limit %ld, 1", 
        OB_SYS_DATABASE_NAME, OB_ALL_VIRTUAL_WARNING_TNAME, restored_arg - 1))) {
      } else {
        SMART_VAR(ObISQLClient::ReadResult, res) {
          common::sqlclient::ObMySQLResult *result = NULL;
          if (OB_FAIL(conn->execute_read(query_virtual.ptr(), res))) {
          } else if (OB_ISNULL(result = res.get_result())) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_SUCC(result->next())) {
            EXTRACT_VARCHAR_FIELD_MYSQL(*result, "message", err_msg);
            EXTRACT_INT_FIELD_MYSQL(*result, "ori_code", err_ret, int);
            EXTRACT_VARCHAR_FIELD_MYSQL(*result, "sql_state", sqlstate);
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), err_msg, err_msg_c, true))) {
            } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), sqlstate, sqlstate_c, true))) {
            }
          }
        }
      }
      OZ (assign_condition_val(ctx, stmt, session_info, conn, err_ret, err_msg_c, sqlstate_c));
    }
  } else if (stmt.get_diagnostics_type() == DiagnosticsType::GET_STACKED_COND) {
    int64_t restored_arg = 0;
    OZ (get_condition_num(ctx, stmt, restored_arg));
    if (OB_ERR_INVALID_CONDITION_NUMBER == ret) {
      ret = OB_SUCCESS;
      LOG_USER_WARN(OB_ERR_INVALID_CONDITION_NUMBER);
    } else if (OB_ERR_USER_VARIABLE_UNKNOWN == ret || OB_OBJ_TYPE_ERROR == ret) {
      ret = OB_SUCCESS;
      LOG_USER_WARN(OB_ERR_INVALID_CONDITION_NUMBER);
    } else if (OB_ERR_BAD_FIELD_ERROR == ret) {
      ret = OB_SUCCESS;
      ObString scope_name = ObString::make_string("field_list");
      ObString invalid_condition_name = stmt.get_invalid_condition_name();
      LOG_USER_WARN(OB_ERR_BAD_FIELD_ERROR,
                    invalid_condition_name.length(), invalid_condition_name.ptr(),
                    static_cast<int32_t>(scope_name.length()), scope_name.ptr());
    } else if (OB_FAIL(ret)) {
    } else if (restored_arg > 1) { /* todo:hr-the current stack diagnostic area only supports storing one piece of information */
      LOG_USER_WARN(OB_ERR_INVALID_CONDITION_NUMBER);
    } else {
      int err_ret;
      int mysql_errcode;
      ObString err_msg_c, sqlstate_c;
      pl::ObPLSqlCodeInfo *sqlcode_info = NULL;
      int64_t idx;
      CK (OB_NOT_NULL(session_info->get_pl_sqlcode_info()));
      OX (sqlcode_info = session_info->get_pl_sqlcode_info());
      CK (sqlcode_info->get_stack_warning_buf().count() > 0);
      OX (idx = sqlcode_info->get_stack_warning_buf().count() - 1);
      if (OB_SUCC(ret)) {
        err_ret = sqlcode_info->get_stack_warning_buf().at(idx).get_err_code();
        err_msg_c = sqlcode_info->get_stack_warning_buf().at(idx).get_err_msg();
        sqlstate_c = sqlcode_info->get_stack_warning_buf().at(idx).get_sql_state();
      }
      OZ (assign_condition_val(ctx, stmt, session_info, conn, err_ret, err_msg_c, sqlstate_c));
    }
  } else if (stmt.get_diagnostics_type() == DiagnosticsType::GET_CURRENT_INFO ||
             stmt.get_diagnostics_type() == DiagnosticsType::GET_STACKED_INFO) {
    CK (stmt.get_params().count() == stmt.get_info_argument().count());
    int64_t old_affected_rows = session_info->get_affected_rows();
    int64_t number;
    if (stmt.get_diagnostics_type() == DiagnosticsType::GET_CURRENT_INFO) {
      number = warning_count;
    } else {
      int64_t idx;
      pl::ObPLSqlCodeInfo *sqlcode_info = NULL;
      CK (OB_NOT_NULL(session_info->get_pl_sqlcode_info()));
      OX (sqlcode_info = session_info->get_pl_sqlcode_info());
      CK (sqlcode_info->get_stack_warning_buf().count() > 0);
      OX (idx = sqlcode_info->get_stack_warning_buf().count() - 1);
      OX (number = 1 + sqlcode_info->get_stack_warning_buf().at(idx).get_readable_warning_count());
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_info_argument().count(); ++i) {
      ObString var;
      const ObString &val = stmt.get_info_argument().at(i);
      int64_t affected_rows = 0;
      ObSqlString set_sql;
      const ObRawExpr* var_expr = stmt.get_params().at(i);
      CK (OB_NOT_NULL(var_expr));
      if (OB_FAIL(ret)) {
      } else if (T_OP_GET_USER_VAR == var_expr->get_expr_type()) {
        const ObSysFunRawExpr *func_expr = static_cast<const ObSysFunRawExpr*>(var_expr);
        if (OB_ISNULL(func_expr->get_param_expr(0))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_UNLIKELY(!func_expr->get_param_expr(0)->is_const_raw_expr()
          || !static_cast<const ObConstRawExpr*>(func_expr->get_param_expr(0))->get_value().is_varchar())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          var = static_cast<const ObConstRawExpr*>(func_expr->get_param_expr(0))->get_value().get_varchar();
        }
        if (OB_FAIL(ret)) {
        } else if (val == "NUMBER") {
          if (OB_FAIL(set_sql.assign_fmt("set %s%s=%ld;", "@", var.ptr(), number))) {
          }
        } else if (val == "ROW_COUNT") {
          if (OB_FAIL(set_sql.assign_fmt("set %s%s=%ld;", "@", var.ptr(), old_affected_rows))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(conn->execute_write(set_sql.ptr(), affected_rows))) {
          }
        }
      } else if (OB_LIKELY(var_expr->is_const_raw_expr())) {
        const ObConstRawExpr *const_expr = static_cast<const ObConstRawExpr *>(var_expr);
        if (T_QUESTIONMARK == const_expr->get_expr_type()) {
          pl::ObPLContext *pl_context = NULL;
          pl::ObPLExecState *pl_state = NULL;
          CK (OB_NOT_NULL(pl_context = session_info->get_pl_context()));
          CK (OB_NOT_NULL(pl_state = pl_context->get_current_state()));

          DatumParameter result;
          DatumParameter origin_obj;
          ObCollationType collation_type;
          char str[50] = {0};
          int64_t idx = const_expr->get_value().get_unknown();
          idx = stmt.get_origin_param_index(idx);
          CK (idx != OB_INVALID_INDEX);
          CK (idx < pl_state->get_function().get_variables().count());
          OZ (session_info->get_collation_connection(collation_type));
          if (OB_SUCC(ret)) {
            pl::ObPLDataType pl_data_type = pl_state->get_function().get_variables().at(idx);
            CK (nullptr != pl_data_type.get_data_type());
            if (OB_SUCC(ret)) {
              SqlValueType expect_type = pl_data_type.get_data_type()->get_obj_type();
              bool has_lob_header = pl_data_type.get_data_type()->get_meta_type().has_lob_header();
              if (val == "NUMBER") {
                sprintf(str, "%ld", number);
                OZ(make_diagnostic_value(expect_type, number, ObString(str), has_lob_header, collation_type, ctx.get_allocator(), result));
              } else if (val == "ROW_COUNT") {
                sprintf(str, "%ld", old_affected_rows);
                OZ(make_diagnostic_value(expect_type, old_affected_rows, ObString(str), has_lob_header, collation_type, ctx.get_allocator(), result));
              } else {
                ret = OB_ERR_UNEXPECTED;
              }
            }
          }

          if (OB_SUCC(ret)) {
            OZ (pl_state->set_var(idx, result));
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}
}/* ns sql*/
}/* ns oceanbase */
