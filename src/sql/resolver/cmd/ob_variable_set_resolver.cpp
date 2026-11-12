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
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/cmd/ob_variable_set_resolver.h"
#include "sql/syntax.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "sql/resolver/cmd/ob_set_names_resolver.h"
#include "sql/resolver/dml/ob_inlist_resolver.h"
namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{

ObVariableSetResolver::ObVariableSetResolver(ObResolverParams &params)
  : ObStmtResolver(params)
{
}

ObVariableSetResolver::~ObVariableSetResolver()
{
}

int ObVariableSetResolver::resolve_set_names(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  ObSetNamesResolver set_names_resolver(params_);
  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(set_names_resolver.resolve(parse_tree))) {
  } else {
    ObVariableSetStmt *variable_set_stmt = static_cast<ObVariableSetStmt*>(stmt_);
    ObVariableSetStmt::VariableSetNode var_node;
    var_node.set_names_stmt_ = static_cast<ObSetNamesStmt *>(set_names_resolver.get_basic_stmt());
    if (OB_FAIL(variable_set_stmt->add_variable_node(var_node))) {
    }
  }
  return ret;
}

int ObVariableSetResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObVariableSetStmt *variable_set_stmt = nullptr;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_) || OB_ISNULL(schema_checker_)
      || OB_ISNULL(params_.query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(variable_set_stmt = create_stmt<ObVariableSetStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (const auto &setting : peg::setting_assignments(statement)) {
      if (OB_FAIL(ret)) break;
      if (setting.kind() == peg::NodeKind::SetNames || setting.kind() == peg::NodeKind::SetCharset) {
        OZ (resolve_set_names(setting));
      } else {
        const auto &target = peg::assignment_target(setting);
        const auto &value = peg::assignment_value(setting);
        const auto name = peg::variable_name(target);
        ObVariableSetStmt::VariableSetNode variable;
        variable.set_scope_ = peg::setting_scope(setting) == peg::VariableScope::Global
            ? ObSetVar::SET_SCOPE_GLOBAL : ObSetVar::SET_SCOPE_SESSION;
        if (variable.set_scope_ == ObSetVar::SET_SCOPE_GLOBAL) variable_set_stmt->set_has_global_variable(true);
        variable.is_system_variable_ = target.kind() != peg::NodeKind::UserVariable;
        OZ (ob_write_string(*allocator_, syntax_text(name), variable.variable_name_));
        if (OB_SUCC(ret)) ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, variable.variable_name_);
        if (value.kind() == peg::NodeKind::Default) {
          variable.is_set_default_ = true;
        } else if (variable.is_system_variable_) {
          OZ (resolve_value_expr(value, variable.value_expr_));
        } else {
          OZ (ObResolverUtils::check_user_variable_length(variable.variable_name_.ptr(),
                                                         variable.variable_name_.length()));
          if (OB_SUCC(ret)) {
            const auto previous_type = session_info_->get_stmt_type();
            session_info_->set_stmt_type(stmt::T_SELECT);
            ret = resolve_value_expr(value, variable.value_expr_);
            session_info_->set_stmt_type(previous_type);
          }
        }
        if (OB_SUCC(ret)) {
          if (variable.value_expr_ != nullptr && variable.value_expr_->has_flag(CNT_AGG)) {
            ret = OB_ERR_INVALID_GROUP_FUNC_USE;
          } else if (variable.value_expr_ != nullptr
                     && variable.value_expr_->get_result_type().get_type() == ObCollectionSQLType) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "Variable value type");
          } else {
            OZ (variable_set_stmt->add_variable_node(variable));
          }
        }
      }
    }
  }
  return ret;
}

int ObVariableSetResolver::resolve_value_expr(const peg::Node &val_node, ObRawExpr *&value_expr)
{
  int ret = OB_SUCCESS;
  ObArray<ObQualifiedName> columns;
  ObArray<ObSubQueryInfo> sub_query_info;
  ObArray<ObAggFunRawExpr*> aggr_exprs;
  ObArray<ObWinFunRawExpr*> win_exprs;
  ObArray<ObUDFInfo> udf_info;
  ObArray<ObVarInfo> sys_vars;
  ObArray<ObOpRawExpr*> op_exprs;
  ObSEArray<ObUserVarIdentRawExpr*, 1> user_var_exprs;
  ObArray<ObInListInfo> inlist_infos;
  ObSEArray<ObMatchFunRawExpr*, 1> match_exprs;
  ObCollationType collation_connection = CS_TYPE_INVALID;
  ObCharsetType character_set_connection = CHARSET_INVALID;
  if (OB_ISNULL(params_.expr_factory_) || OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("resolve status is invalid", K_(params_.expr_factory), K_(params_.session_info));
  } else if (OB_FAIL(params_.session_info_->get_collation_connection(collation_connection))) {
  } else if (OB_FAIL(params_.session_info_->get_character_set_connection(character_set_connection))) {
  } else {
    ObExprResolveContext ctx(*params_.expr_factory_, params_.session_info_->get_timezone_info(),
                             OB_NAME_CASE_INVALID);
    ctx.dest_collation_ = collation_connection;
    ctx.connection_charset_ = character_set_connection;
    ctx.param_list_ = params_.param_list_;
    ctx.parameterization_ = params_.parameterization_;
    ctx.syntax_source_ = params_.syntax_->source();
    ctx.is_extract_param_type_ = !params_.is_prepare_protocol_; //when prepare do not extract
    ctx.schema_checker_ = params_.schema_checker_;
    ctx.session_info_ = params_.session_info_;
    ctx.secondary_namespace_ = params_.secondary_namespace_;
    ctx.query_ctx_ = params_.query_ctx_;
    ObRawExprResolverImpl expr_resolver(ctx);
    if (OB_FAIL(params_.session_info_->get_name_case_mode(ctx.case_mode_))) {
    } else if (OB_FAIL(expr_resolver.resolve(val_node, value_expr, columns, sys_vars,
                                             sub_query_info, aggr_exprs, win_exprs,
                                             udf_info, op_exprs, user_var_exprs, inlist_infos, match_exprs))) {
    } else if (udf_info.count() > 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("UDFInfo should not found be here!!!", K(ret));
    } else if (inlist_infos.count() > 0) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_UNLIKELY(match_exprs.count() > 0)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "fulltext search func");
    } else if (value_expr->get_expr_type() == T_SP_CPARAM) {
      ObCallParamRawExpr *call_expr = static_cast<ObCallParamRawExpr *>(value_expr);
      if (OB_ISNULL(call_expr->get_expr())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(call_expr->get_expr()->formalize(params_.session_info_))) {
      }
    } else if (value_expr->has_flag(CNT_SUB_QUERY)) {
      if (OB_FAIL(resolve_subquery_info(sub_query_info, value_expr))) {
      }
      LOG_TRACE("set user variable with subquery", K(sub_query_info.count()));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObResolverUtils::resolve_columns_for_const_expr(value_expr, columns, params_))) {
    } else if (OB_FAIL(value_expr->formalize(params_.session_info_))) {
    } else {
      params_.prepare_param_count_ += ctx.prepare_param_count_; //prepare param count
    }
  }
  return ret;
}

int ObVariableSetResolver::resolve_subquery_info(const ObIArray<ObSubQueryInfo> &subquery_info,
                                                 ObRawExpr *&value_expr)
{
  int ret = OB_SUCCESS;
  int current_level = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < subquery_info.count(); i++) {
    const ObSubQueryInfo &info = subquery_info.at(i);
    ObSelectResolver subquery_resolver(params_);
    subquery_resolver.set_current_level(current_level + 1);
    subquery_resolver.set_current_view_level(current_level);
    ObSelectStmt *sub_stmt = NULL;
    if (OB_ISNULL(info.sub_query_) || OB_ISNULL(info.ref_expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("subquery info is invalid", K_(info.sub_query), K_(info.ref_expr));
    } else if (OB_UNLIKELY(peg::NodeKind::Select != info.sub_query_->kind())) {
      ret = OB_ERR_ILLEGAL_TYPE;
      LOG_WARN("Unknown statement type in subquery", "kind", static_cast<int>(info.sub_query_->kind()));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(subquery_resolver.resolve_child_stmt(*(info.sub_query_)))) {
    } else if (OB_ISNULL(sub_stmt = subquery_resolver.get_child_stmt())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // for set stmt, the parent stmt of subquery is subquery itself
      // we do this only to make sure that the sub_stmt is not a root stmt
      ObDMLStmt *dml_stmt = subquery_resolver.get_select_stmt();
      info.ref_expr_->set_ref_stmt(sub_stmt);
      info.ref_expr_->set_output_column(sub_stmt->get_select_item_size());
      // the column type of ref_expr stores the target type of subquery
      for (int64_t j = 0; OB_SUCC(ret) && j < sub_stmt->get_select_item_size(); ++j) {
        ObRawExpr *target_expr = sub_stmt->get_select_item(j).expr_;
        if (OB_ISNULL(target_expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObRawExprResType &column_type = target_expr->get_result_type();
          if (OB_FAIL(info.ref_expr_->add_column_type(column_type))) {
          }
        }
      }
    }
  }
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */
