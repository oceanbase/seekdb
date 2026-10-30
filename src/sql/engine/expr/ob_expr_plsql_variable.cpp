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

#include "ob_expr_plsql_variable.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/pl/ob_pl.h"

namespace oceanbase
{
using namespace common;
using namespace pl;

namespace sql
{

OB_SERIALIZE_MEMBER((ObExprPLSQLVariable, ObFuncExprOperator), plsql_line_, plsql_variable_);

ObExprPLSQLVariable::ObExprPLSQLVariable(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_PLSQL_VARIABLE, N_PLSQL_VARIABLE, 0, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, false),
      plsql_line_(OB_INVALID_INDEX),
      plsql_variable_(),
      allocator_(alloc)
{}

int ObExprPLSQLVariable::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  const ObExprPLSQLVariable *tmp = dynamic_cast<const ObExprPLSQLVariable *>(&other);
  if (OB_UNLIKELY(OB_ISNULL(tmp))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_LIKELY(this != tmp)) {
    if (OB_FAIL(ObExprOperator::assign(other))) {
    } else {
      OX (this->plsql_line_ = tmp->plsql_line_);
      OZ (deep_copy_plsql_variable(tmp->plsql_variable_));
    }
  }
  return ret;
}

int ObExprPLSQLVariable::calc_result_type0(
  ObExprResType &type, common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (0 == plsql_variable_.case_compare("PLSQL_LINE")) {
    type.set_int32();
    type.set_precision(4);
    type.set_scale(0);
  } else if (0 == plsql_variable_.case_compare("PLSQL_UNIT")
    || 0 == plsql_variable_.case_compare("PLSQL_UNIT_OWNER")) {
    type.set_varchar();
    type.set_default_collation_type();
    type.set_length(common::OB_MAX_VARCHAR_LENGTH);
    type.set_length_semantics(LS_BYTE);
  } else {
    const sql::ObSQLSessionInfo *session_info = type_ctx.get_session();
    DatumValue value;
    CK (OB_NOT_NULL(session_info));
    // may plsql relative system variables, try it.
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(session_info->get_sys_variable_by_name(plsql_variable_, value))) {
      // not system variable
      if (OB_ERR_SYS_VARIABLE_UNKNOWN == ret) {
        ret = OB_SUCCESS;
        value.set_null();
        OX (type.set_meta(value.get_meta()));
        OX (type.set_collation_type(ObCharset::get_system_collation()));
        if (OB_SUCC(ret) && value.is_string_type()) {
          type.set_length(value.get_string().length());
          type.set_length_semantics(LS_BYTE);
        }
      } else {
      }
    } else {
      type.set_meta(value.get_meta());
      if (value.is_string_type()) {
        type.set_length(value.get_string().length());
        type.set_length_semantics(LS_BYTE);
      }
    }
  }
  return ret;
}

int ObExprPLSQLVariable::get_plsql_unit(common::ObString &result,
                                        ObSQLSessionInfo &session_info,
                                        const ObString &plsql_variable)
{
  int ret = OB_SUCCESS;
  pl::ObPLContext *pl_context = NULL;
  ObPLExecState *pl_state = NULL;
  ObString plsql_unit;
  CK (OB_NOT_NULL(pl_context = session_info.get_pl_context()));
  CK (pl_context->is_top_stack());
  CK (pl_context->get_exec_stack().count() > 0);
  CK (OB_NOT_NULL(pl_state = pl_context
    ->get_exec_stack().at(pl_context->get_exec_stack().count() - 1)));
  if (OB_FAIL(ret)) {
  } else if (0 == plsql_variable.case_compare("PLSQL_UNIT")) {
    if (pl_state->get_function().get_package_name().empty()) {
      plsql_unit = pl_state->get_function().get_function_name();
    } else {
      plsql_unit = pl_state->get_function().get_package_name();
    }
  } else if (0 == plsql_variable.case_compare("PLSQL_UNIT_OWNER")) {
    if (!pl_state->get_function().get_database_name().empty()) {
      plsql_unit = pl_state->get_function().get_database_name();
    }
  }
  OX (result = plsql_unit);
  return ret;
}

int ObExprPLSQLVariable::cg_expr(ObExprCGCtx &op_cg_ctx,
                                 const ObRawExpr &raw_expr,
                                 ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  ObIAllocator &alloc = *op_cg_ctx.allocator_;
  const ObPLSQLVariableRawExpr &fun_sys = static_cast<const ObPLSQLVariableRawExpr &>(raw_expr);
  ObPLSQLVariableInfo *info = OB_NEWx(ObPLSQLVariableInfo, (&alloc), alloc, T_FUN_PLSQL_VARIABLE);
  if (NULL == info) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    OZ(info->from_raw_expr(fun_sys, alloc));
    info->result_type_ = result_type_;
    rt_expr.extra_info_ = info;
  }
  rt_expr.eval_func_ = eval_plsql_variable;
  return ret;
}
int ObExprPLSQLVariable::eval_plsql_variable(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  const ObPLSQLVariableInfo *info = static_cast<ObPLSQLVariableInfo *>(expr.extra_info_);
  ObDatum result;
  result.set_null();
  int64_t line = 0;
  char extended_payload[DATUM_EXTEND_PAYLOAD_SIZE];
  if (OB_ISNULL(info) || OB_ISNULL(ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (0 == info->plsql_variable_.case_compare("PLSQL_LINE")) {
    line = static_cast<int32_t>(info->plsql_line_);
    result.ptr_ = reinterpret_cast<const char *>(&line);
    // Int32 expression values use the common eight-byte integer row Datum.
    result.set_int(line);
  } else if (0 == info->plsql_variable_.case_compare("PLSQL_UNIT")
             || 0 == info->plsql_variable_.case_compare("PLSQL_UNIT_OWNER")) {
    ObString unit;
    OZ (get_plsql_unit(unit, *ctx.exec_ctx_.get_my_session(),
                       info->plsql_variable_));
    OX (result.set_string(unit));
  } else {
    // may plsql relative system variables, try it.
    DatumValue value;
    if (OB_FAIL(ctx.exec_ctx_.get_my_session()->get_sys_variable_by_name(
            info->plsql_variable_, value))) {
      // not system variable
      if (OB_ERR_SYS_VARIABLE_UNKNOWN == ret) {
        ret = OB_SUCCESS;
        value.set_null();
      } else {
      }
    }
    OZ (value.to_row_datum(result, extended_payload));
    if (OB_SUCC(ret) && is_lob_storage(value.get_type())) {
      OZ (ob_adjust_lob_datum(ctx.exec_ctx_, result, value.get_meta(),
                             expr.obj_meta_, ctx.exec_ctx_.get_allocator()));
    }
  }
  // System-variable values and PL names borrow their owning scope; normalized
  // Extend bytes and PLSQL_LINE borrow this stack. Publish only the frame copy.
  OZ(expr.deep_copy_datum(ctx, result));
  OX(res.set_datum(expr.locate_expr_datum(ctx)));
  return ret;
}

OB_DEF_SERIALIZE(ObPLSQLVariableInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              plsql_line_,
              plsql_variable_,
              result_type_);
  return ret;
}

OB_DEF_DESERIALIZE(ObPLSQLVariableInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              plsql_line_,
              plsql_variable_,
              result_type_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPLSQLVariableInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              plsql_line_,
              plsql_variable_,
              result_type_);
  return len;
}

int ObPLSQLVariableInfo::deep_copy(common::ObIAllocator &allocator,
                         const ObExprOperatorType type,
                         ObIExprExtraInfo *&copied_info) const
{
  int ret = common::OB_SUCCESS;
  OZ(ObExprExtraInfoFactory::alloc(allocator, type, copied_info));
  ObPLSQLVariableInfo &other = *static_cast<ObPLSQLVariableInfo *>(copied_info);
  other.plsql_line_ = plsql_line_;
  other.result_type_ = result_type_;
  OZ(ob_write_string(allocator, plsql_variable_, other.plsql_variable_));
  return ret;
}

template <typename RE>
int ObPLSQLVariableInfo::from_raw_expr(RE &raw_expr, ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  ObPLSQLVariableRawExpr &expr
    = const_cast<ObPLSQLVariableRawExpr &> (static_cast<const ObPLSQLVariableRawExpr&>(raw_expr));
  plsql_line_ = expr.get_plsql_line();
  OZ(ob_write_string(alloc, expr.get_plsql_variable(), plsql_variable_));
  return ret;
}

}
}
