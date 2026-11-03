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
#include "sql/resolver/cmd/ob_get_diagnostics_resolver.h"
namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{

ObGetDiagnosticsResolver::ObGetDiagnosticsResolver(ObResolverParams &params)
  : ObStmtResolver(params)
{
}

ObGetDiagnosticsResolver::~ObGetDiagnosticsResolver()
{
}

int ObGetDiagnosticsResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObGetDiagnosticsStmt *diagnostics = nullptr;
  const bool condition = peg::diagnostics_has_condition(statement);
  const bool stacked = peg::diagnostics_stacked(statement);
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(diagnostics = create_stmt<ObGetDiagnosticsStmt>())) {
    ret = OB_SQL_RESOLVER_NO_MEMORY;
  } else if (session_info_->get_pl_context() == nullptr && stacked) {
    ret = OB_ERR_GET_STACKED_DIAGNOSTICS;
  } else {
    params_.query_ctx_->set_literal_stmt_type(stmt::T_DIAGNOSTICS);
    OZ (set_diagnostics_type(diagnostics, !stacked, condition));
    if (condition) {
      const auto &number = peg::diagnostics_condition(statement);
      ObRawExpr *expression = nullptr;
      if (number.kind() == peg::NodeKind::Ident) {
        ObString name;
        const auto number_text = number.text();
        OZ (ob_write_string(*allocator_, syntax_text(number_text), name));
        if (OB_SUCC(ret)) {
          diagnostics->set_invalid_condition_name(name);
          ret = OB_ERR_BAD_FIELD_ERROR;
        }
      } else if (number.kind() == peg::NodeKind::UserVariable) {
        const auto name = peg::variable_name(number);
        OZ (ObRawExprUtils::build_get_user_var(*params_.expr_factory_,
            syntax_text(name), expression, session_info_));
      } else {
        OZ (ObResolverUtils::resolve_const_expr(params_, number, expression, nullptr));
        if (number.kind() == peg::NodeKind::Parameter) {
          const auto parameter = peg::diagnostics_parameter(number);
          CK (parameter.valid);
          OZ (diagnostics->set_origin_param_index(parameter.bind_index, parameter.local_index));
        }
      }
      OZ (diagnostics->add_param(expression));
    }
    for (const auto &item : peg::diagnostics_items(statement)) {
      if (OB_FAIL(ret)) break;
      const auto &target = peg::diagnostics_target(item);
      const auto item_name = peg::diagnostics_item_name(item);
      ObRawExpr *expression = nullptr;
      if (target.kind() == peg::NodeKind::Ident) {
        ret = OB_ERR_SP_UNDECLARED_VAR;
        const auto target_text = target.text();
        LOG_USER_ERROR(OB_ERR_SP_UNDECLARED_VAR, static_cast<int>(target_text.size()), syntax_text(target_text).ptr());
      } else if (target.kind() == peg::NodeKind::Parameter) {
        OZ (ObResolverUtils::resolve_const_expr(params_, target, expression, nullptr));
        const auto parameter = peg::diagnostics_parameter(target);
        CK (parameter.valid);
        OZ (diagnostics->set_origin_param_index(parameter.bind_index, parameter.local_index));
      } else {
        const auto name = peg::variable_name(target);
        OZ (ObRawExprUtils::build_get_user_var(*params_.expr_factory_,
            syntax_text(name), expression, session_info_));
      }
      ObString name;
      OZ (ob_write_string(*allocator_, syntax_text(item_name), name));
      OZ (diagnostics->add_param(expression));
      OZ (diagnostics->add_info_argument(name));
    }
  }
  // A non-variable condition name becomes a warning when the statement executes.
  if (ret == OB_ERR_BAD_FIELD_ERROR) ret = OB_SUCCESS;
  return ret;
}

int ObGetDiagnosticsResolver::set_diagnostics_type(ObGetDiagnosticsStmt *diagnostics_stmt, 
                                                   const bool &is_current, const bool &is_condition)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(diagnostics_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (is_condition == 1 && is_current== 1) {
    diagnostics_stmt->set_diagnostics_type(DiagnosticsType::GET_CURRENT_COND);
  } else if (is_condition == 0 && is_current == 1) {
    diagnostics_stmt->set_diagnostics_type(DiagnosticsType::GET_CURRENT_INFO);
  } else if (is_condition == 1 && is_current == 0) {
    diagnostics_stmt->set_diagnostics_type(DiagnosticsType::GET_STACKED_COND);
  } else if (is_condition == 0 && is_current == 0) {
    diagnostics_stmt->set_diagnostics_type(DiagnosticsType::GET_STACKED_INFO);
  }
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */
