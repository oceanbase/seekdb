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

#include "sql/resolver/dml/ob_inlist_resolver.h"
#include "sql/resolver/dml/ob_dml_resolver.h"
#include "sql/resolver/parameterization_result.h"
#include "sql/engine/expr/ob_expr_version.h"
#include "sql/resolver/expr/literal_decode_options.h"
#include "rust/sql_parser.h"
#include "sql/rewrite/ob_transform_pre_process.h"

namespace oceanbase
{

using namespace common;

namespace sql
{
namespace {
const peg::Node &unparenthesized(const peg::Node &node)
{
  const peg::Node *result = &node;
  while (result->kind() == peg::NodeKind::Parenthesized) result = &peg::parenthesized_expression(*result);
  return *result;
}

const peg::Node &membership_element(const peg::Node &row, int64_t columns, int64_t index)
{
  const auto &value = unparenthesized(row);
  return unparenthesized(columns == 1 ? value : peg::row_elements(value)[index]);
}

bool membership_row(const peg::Node &node)
{
  const auto kind = unparenthesized(node).kind();
  return kind == peg::NodeKind::ExprList || kind == peg::NodeKind::ValueVector;
}

}

int64_t InListRows::row_count() const
{
  int64_t count = 0;
  for (int64_t i = 0; i < segments_.count(); ++i) {
    count += peg::membership_expressions(*segments_.at(i)).size();
  }
  return count;
}

struct InListRewriteInfo {
  bool is_valid_as_values_table_;
  bool is_question_mark_;
  // the start_param_idx_ and end_param_idx_ are valid only if is_question_mark_ == true
  // the start_param_idx_ is the start index of question marks in inlist
  int64_t start_param_idx_;
  // the end_param_idx_ would be updated during calling `get_inlist_rewrite_info`
  // it would be set as the index of the last processed question mark
  // so after done calling `get_inlist_rewrite_info` for all columns of an inlist
  // it would be set as the last index of question marks in inlist
  int64_t end_param_idx_;
  ObSEArray<DistinctObjMeta, 4> param_types_; // for each column

  InListRewriteInfo(): is_valid_as_values_table_(true),
                       is_question_mark_(false),
                       start_param_idx_(-1),
                       end_param_idx_(-1) {}

  void reset() {
    is_valid_as_values_table_ = true;
    is_question_mark_ = false;
    start_param_idx_ = -1;
    end_param_idx_ = -1;
    param_types_.reuse();
  }
};

int ObInListResolver::resolve_inlist(ObInListInfo &inlist_info)
{
  int ret = OB_SUCCESS;
  const int64_t column_cnt = inlist_info.column_cnt_;
  const int64_t row_cnt = inlist_info.row_cnt_;
  ObValuesTableDef *table_def = NULL;
  if (OB_UNLIKELY(row_cnt <= 0 || column_cnt <= 0) || OB_ISNULL(cur_resolver_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObResolverParams &params = cur_resolver_->params_;
    if (OB_ISNULL(params.session_info_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(resolve_values_table_from_inlist(inlist_info.rows_,
                                                        column_cnt,
                                                        row_cnt,
                                                        inlist_info.is_question_mark_,
                                                        params.is_prepare_stage_,
                                                        NULL == params.session_info_->get_pl_context(),
                                                        params.param_list_,
                                                        params.session_info_,
                                                        params.allocator_,
                                                        table_def))) {
    } else if (OB_FAIL(resolve_subquery_from_values_table(params.stmt_factory_,
                                                          params.session_info_,
                                                          params.allocator_,
                                                          params.query_ctx_,
                                                          params.expr_factory_,
                                                          table_def,
                                                          params.is_prepare_protocol_ && params.is_prepare_stage_,
                                                          column_cnt,
                                                          inlist_info.in_list_expr_))) {
    }
  }
  return ret;
}

int ObInListResolver::resolve_values_table_from_inlist(const InListRows &rows,
                                                const int64_t column_cnt,
                                                const int64_t row_cnt,
                                                const bool is_question_mark,
                                                const bool is_prepare_stage,
                                                const bool is_called_in_sql,
                                                const ParamStore *param_store,
                                                ObSQLSessionInfo *session_info,
                                                ObIAllocator *allocator,
                                                ObValuesTableDef *&table_def)
{
  int ret = OB_SUCCESS;
  char *table_buf = NULL;
  // we treat question marks in prepare stmt as objs instead of params
  ObValuesTableDef::TableAccessType access_type = is_question_mark && !is_prepare_stage
                                                  ? ObValuesTableDef::ACCESS_PARAM
                                                  : ObValuesTableDef::ACCESS_OBJ;
  if (OB_ISNULL(allocator) || OB_ISNULL(session_info) || rows.segments_.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(rows.row_count() != row_cnt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(table_buf = static_cast<char*>(allocator->alloc(sizeof(ObValuesTableDef))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    table_def = new (table_buf) ObValuesTableDef();
    table_def->column_cnt_ = column_cnt;
    table_def->row_cnt_ = row_cnt;
    table_def->access_type_ = access_type;
    table_def->is_const_ = true;
  }
  if (OB_FAIL(ret)) {
  } else if (ObValuesTableDef::ACCESS_PARAM == access_type &&
             OB_FAIL(resolve_access_param_values_table(rows, column_cnt, param_store,
                                                 session_info, allocator, is_called_in_sql, *table_def))) {
  } else if (ObValuesTableDef::ACCESS_OBJ == access_type &&
             OB_FAIL(resolve_access_obj_values_table(rows, column_cnt, session_info,
                                                     allocator, is_prepare_stage, is_called_in_sql, *table_def))) {
  } else if (OB_FAIL(cur_resolver_->estimate_values_table_stats(*table_def))) {
  }
  return ret;
}

int ObInListResolver::resolve_subquery_from_values_table(ObStmtFactory *stmt_factory,
                                                         ObSQLSessionInfo *session_info,
                                                         ObIAllocator *allocator,
                                                         ObQueryCtx *query_ctx,
                                                         ObRawExprFactory *expr_factory,
                                                         ObValuesTableDef *table_def,
                                                         const bool is_prepare_stmt,
                                                         const int64_t column_cnt,
                                                         ObQueryRefRawExpr *query_ref)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *subquery = NULL;
  if (OB_ISNULL(allocator) || OB_ISNULL(session_info) || OB_ISNULL(stmt_factory) ||
      OB_ISNULL(query_ctx) || OB_ISNULL(expr_factory) || OB_ISNULL(query_ref) ||
      OB_ISNULL(table_def)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(stmt_factory->create_stmt(subquery))) {
  } else if (OB_ISNULL(subquery)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    subquery->set_query_ctx(query_ctx);
    subquery->get_query_ctx()->set_is_prepare_stmt(is_prepare_stmt);
    subquery->get_query_ctx()->set_timezone_info(get_timezone_info(session_info));
    subquery->get_query_ctx()->set_sql_stmt_coll_type(get_obj_print_params(session_info).cs_type_);
    subquery->assign_distinct();
    query_ref->set_ref_stmt(subquery);
    query_ref->set_is_set(true);
    query_ref->set_output_column(column_cnt);
    if (OB_FAIL(subquery->set_stmt_id())) {
    } else if (OB_FAIL(ObResolverUtils::create_values_table_query(session_info, allocator,
                                                                  expr_factory, query_ctx, subquery,
                                                                  table_def))) {
    } else if (OB_FAIL(cur_resolver_->get_stmt()->add_subquery_ref(query_ref))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt; i++) {
        if (OB_FAIL(query_ref->add_column_type(table_def->column_types_.at(i)))) {
        }
      }
    }
  }
  return ret;
}

// notice that if:
// 1. rewrite_info.is_valid_as_values_table_ == false, or
// 2. rewrite_info.is_question_mark_ == true && helper.is_prepare_stmt_ == true
// then rewrite_info.param_types_ will not be filled up as expected
int ObInListResolver::get_inlist_rewrite_info(const InListRows &rows,
                                              const int64_t column_cnt,
                                              int64_t col_idx,
                                              ObInListsResolverHelper &helper,
                                              InListRewriteInfo &rewrite_info)
{
  int64_t ret = OB_SUCCESS;
  const int64_t row_cnt = rows.row_count();
  if (row_cnt <= 0 || column_cnt <= 0 || col_idx >= column_cnt) {
    // delay error later
    rewrite_info.is_valid_as_values_table_ = false;
  } else {
    rewrite_info.is_valid_as_values_table_ = true;
    while (OB_SUCC(ret) && rewrite_info.param_types_.count() < column_cnt) {
      if (OB_FAIL(rewrite_info.param_types_.push_back(DistinctObjMeta()))) {
      }
    }
    int64_t row_idx = 0;
    for (int64_t segment_idx = 0; OB_SUCC(ret) && rewrite_info.is_valid_as_values_table_
         && segment_idx < rows.segments_.count(); ++segment_idx) {
      const auto in_list = peg::membership_expressions(*rows.segments_.at(segment_idx));
      for (int64_t i = 0; OB_SUCC(ret) && rewrite_info.is_valid_as_values_table_
           && i < in_list.size(); ++i, ++row_idx) {
        if (OB_UNLIKELY(column_cnt > 1 && (!membership_row(in_list[i])
            || peg::row_elements(unparenthesized(in_list[i])).size() != column_cnt))) {
          // delay error later
          rewrite_info.is_valid_as_values_table_ = false;
        } else {
          DistinctObjMeta cur_param_type = DistinctObjMeta();
          const peg::Node *node = &membership_element(in_list[i], column_cnt, col_idx);
          const ParameterBinding *binding = helper.parameterization_ == nullptr
              ? nullptr : helper.parameterization_->find(node);
          if (OB_ISNULL(node)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (FALSE_IT(rewrite_info.is_question_mark_ = binding != nullptr || node->kind() == peg::NodeKind::Parameter)) {
          } else if (rewrite_info.is_question_mark_) {
            const int64_t parameter_index = binding != nullptr ? binding->parameter_index : peg::placeholder_index(*node);
            // check if param indexes are continuous for values table
            if (-1 == rewrite_info.start_param_idx_
                && FALSE_IT(rewrite_info.start_param_idx_ = parameter_index)) {
            } else if (FALSE_IT(rewrite_info.end_param_idx_ = parameter_index)) {
            } else if (OB_UNLIKELY(parameter_index != rewrite_info.start_param_idx_ + col_idx + row_idx * column_cnt
                                   || parameter_index < 0)) {
              rewrite_info.is_valid_as_values_table_ = false;
            }
          } else { /*do nothing*/ }
          if (OB_FAIL(ret) || !rewrite_info.is_valid_as_values_table_) {
          } else if (helper.is_prepare_stmt_ && rewrite_info.is_question_mark_) {
            // skip getting types for question marks in prepare stmt, because they have no type
          } else if (!((binding != nullptr || node->kind() == peg::NodeKind::Parameter) && rewrite_info.is_question_mark_) &&
                     !(peg::is_literal(*node) && !rewrite_info.is_question_mark_)) {
            // not const type
            rewrite_info.is_valid_as_values_table_ = false;
          } else if (OB_FAIL(ObResolverUtils::fast_get_param_type(*node,
                                                                  binding,
                                                                  helper.param_store_,
                                                                  helper.connect_collation_,
                                                                  helper.nchar_collation_,
                                                                  helper.server_collation_,
                                                                  helper.enable_decimal_int_,
                                                                  helper.alloc_,
                                                                  cur_param_type.obj_type_,
                                                                  cur_param_type.coll_type_,
                                                                  cur_param_type.coll_level_))) {
            // not const type
            ret = OB_SUCCESS;
            rewrite_info.is_valid_as_values_table_ = false;
          } else if (0 == row_idx) {
            rewrite_info.param_types_.at(col_idx) = cur_param_type;
          } else if (rewrite_info.param_types_.at(col_idx) == cur_param_type) {
          } else if (ObNullType == cur_param_type.obj_type_) {
            // ignore null type
          } else if (ObNullType == rewrite_info.param_types_.at(col_idx).obj_type_) {
            rewrite_info.param_types_.at(col_idx) = cur_param_type;
          } else {
            rewrite_info.is_valid_as_values_table_ = false;
            LOG_ERROR("get inconsistent param types in big inlist", K(column_cnt), K(row_cnt), K(row_idx),
                     K(col_idx), K(cur_param_type), K(rewrite_info.param_types_.at(col_idx)));
          }
        }
      }
    }
  }
  return ret;
}

int ObInListResolver::check_inlist_rewrite_enable(const InListRows &rows,
                                                  const ObItemType op_type,
                                                  const ObRawExpr &left_expr,
                                                  const ObStmtScope &scope,
                                                  const bool is_root_condition,
                                                  const bool is_need_print,
                                                  const bool is_in_pl_prepare,
                                                  const ObSQLSessionInfo *session_info,
                                                  const ParamStore *param_store,
                                                  const ParameterizationResult *parameter_bindings,
                                                  const ObStmt *stmt,
                                                  ObIAllocator &alloc,
                                                  bool &is_question_mark,
                                                  bool &is_enable)
{
  int ret = OB_SUCCESS;
  is_enable = false;
  int64_t threshold = INT64_MAX;
  bool is_prepare_stmt = false;
  // 1. check basic requests
  if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_WHERE_SCOPE != scope
             || !is_root_condition
             || (T_OP_IN != op_type && T_OP_NOT_IN != op_type)
             || rows.segments_.empty()
             || is_need_print
             || is_in_pl_prepare) {
  } else {
    if (NULL == stmt) {
      threshold = session_info->get_inlist_rewrite_threshold();
    } else {
      if (OB_ISNULL(stmt->get_query_ctx())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        is_prepare_stmt = stmt->get_query_ctx()->is_prepare_stmt();
        threshold = session_info->get_inlist_rewrite_threshold();
        const ObGlobalHint &global_hint = stmt->get_query_ctx()->get_global_hint();
        if (OB_FAIL(global_hint.opt_params_.get_integer_opt_param(
                                              ObOptParamHint::INLIST_REWRITE_THRESHOLD, threshold))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (rows.row_count() < threshold) {
      } else {
        is_enable = true;
      }
    }
  }
  // 2. check same node type requests
  //    and whether parameter indexes correspond to what a values table expected
  if (OB_SUCC(ret) && is_enable) {
    const int64_t row_cnt = rows.row_count();
    const auto first_list = peg::membership_expressions(*rows.segments_.at(0));
    const int64_t column_cnt = T_OP_ROW == left_expr.get_expr_type() ? left_expr.get_param_count() :
                                                                       1;
    ObCollationType connect_collation = CS_TYPE_INVALID;
    ObCollationType nchar_collation = session_info->get_nls_collation_nation();
    int64_t server_collation = CS_TYPE_INVALID;
    bool enable_decimal_int = false;
    if (OB_UNLIKELY(row_cnt <= 0 || column_cnt <= 0 || first_list.empty())
        || OB_UNLIKELY(column_cnt > 1 && (!membership_row(first_list[0])
            || peg::row_elements(unparenthesized(first_list[0])).size() != column_cnt))) {
      is_enable = false;  /* delay return error code */
    } else if (OB_FAIL(session_info->get_collation_connection(connect_collation))) {
    } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session_info, enable_decimal_int))) {
    } else {
      ObInListsResolverHelper helper(alloc,
                                     param_store,
                                     parameter_bindings,
                                     connect_collation,
                                     nchar_collation,
                                     static_cast<ObCollationType>(server_collation),
                                     enable_decimal_int,
                                     is_prepare_stmt);
      InListRewriteInfo rewrite_info;
      for (int64_t j = 0; OB_SUCC(ret) && is_enable && j < column_cnt; ++j) {
        if (OB_FAIL(get_inlist_rewrite_info(rows, column_cnt, j, helper, rewrite_info))) {
        } else if (!rewrite_info.is_valid_as_values_table_) {
          is_enable = false;
        } else if (helper.is_prepare_stmt_ && rewrite_info.is_question_mark_) {
          // skip additional check for prepare stmt with question mark
        } else if (rewrite_info.param_types_.count() <= j) {
          ret = OB_ERR_UNEXPECTED;
        } else if (ob_is_enum_or_set_type(rewrite_info.param_types_.at(j).obj_type_)) {
          is_enable = false;
        }
      }
      is_question_mark = rewrite_info.is_question_mark_;
    }
  }
  return ret;
}

int ObInListResolver::resolve_access_param_values_table(const InListRows &rows,
                                                        const int64_t column_cnt,
                                                        const ParamStore *param_store,
                                                        ObSQLSessionInfo *session_info,
                                                        ObIAllocator *allocator,
                                                        const bool is_called_in_sql,
                                                        ObValuesTableDef &table_def)
{
  int ret = OB_SUCCESS;
  const peg::Node *row_node = NULL;
  const ParameterizationResult *parameter_bindings = cur_resolver_->params_.parameterization_;
  ObCollationType coll_type = CS_TYPE_INVALID;
  if (OB_ISNULL(allocator) || OB_ISNULL(session_info) || OB_ISNULL(param_store)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(session_info->get_collation_connection(coll_type))) {
  }

  int64_t row_idx = 0;
  for (int64_t segment_idx = 0; OB_SUCC(ret) && segment_idx < rows.segments_.count(); ++segment_idx) {
    const auto in_list = peg::membership_expressions(*rows.segments_.at(segment_idx));
    for (int64_t i = 0; OB_SUCC(ret) && i < in_list.size(); ++i, ++row_idx) {
      row_node = &in_list[i];
      if (OB_ISNULL(row_node)) {
        ret = OB_ERR_UNEXPECTED;
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < column_cnt; j++) {
        const peg::Node *element = &membership_element(*row_node, column_cnt, j);
        const ParameterBinding *binding = parameter_bindings == nullptr
            ? nullptr : parameter_bindings->find(element);
        const int64_t parameter_index = binding != nullptr ? binding->parameter_index : peg::placeholder_index(*element);
        const ObObjParam &obj_param = param_store->at(parameter_index);
        ObRawExprResType res_type;
        res_type.set_meta(obj_param.get_param_meta());
        res_type.set_accuracy(obj_param.get_accuracy());
        res_type.set_result_flag(obj_param.get_result_flag());
        if (row_idx == 0) {
          if (OB_FAIL(table_def.column_types_.push_back(res_type))) {
          }
        } else {
          // is not same ObExprResType, than compute a new one
          ObExprResType new_res_type;
          ObExprVersion dummy_op(*allocator);
          ObSEArray<ObExprResType, 2> tmp_res_types;
          ObExprTypeCtx type_ctx;
          ObSQLUtils::init_type_ctx(session_info, type_ctx);
          if (OB_FAIL(tmp_res_types.push_back(table_def.column_types_.at(j)))) {
          } else if (OB_FAIL(tmp_res_types.push_back(res_type))) {
          } else if (OB_FAIL(dummy_op.aggregate_result_type_for_merge(new_res_type,
                             &tmp_res_types.at(0), 2, type_ctx,
                             true, false, is_called_in_sql))) {
          } else {
            table_def.column_types_.at(j) = new_res_type;
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    const auto first_list = peg::membership_expressions(*rows.segments_.at(0));
    const auto last_list = peg::membership_expressions(*rows.segments_.at(rows.segments_.count() - 1));
    const peg::Node *first = &membership_element(first_list[0], column_cnt, 0);
    const peg::Node *last = &membership_element(last_list[last_list.size() - 1], column_cnt, column_cnt - 1);
    const ParameterBinding *first_binding = parameter_bindings == nullptr
        ? nullptr : parameter_bindings->find(first);
    const ParameterBinding *last_binding = parameter_bindings == nullptr
        ? nullptr : parameter_bindings->find(last);
    table_def.start_param_idx_ = first_binding != nullptr ? first_binding->parameter_index : peg::placeholder_index(*first);
    table_def.end_param_idx_ = last_binding != nullptr ? last_binding->parameter_index : peg::placeholder_index(*last);
  }
  return ret;
}

int ObInListResolver::resolve_access_obj_values_table(const InListRows &rows,
                                                      const int64_t column_cnt,
                                                      ObSQLSessionInfo *session_info,
                                                      ObIAllocator *allocator,
                                                      const bool is_prepare_stage,
                                                      const bool is_called_in_sql,
                                                      ObValuesTableDef &table_def)
{
  int ret = OB_SUCCESS;
  const bool is_parameterize = false;
  const ObTimeZoneInfo *timezone_info = NULL;
  const bool is_from_pl = nullptr != cur_resolver_->params_.secondary_namespace_;
  ObCollationType coll_type = CS_TYPE_INVALID;
  ObLengthSemantics length_semantics = LS_DEFAULT;
  int64_t server_collation = CS_TYPE_INVALID;
  stmt::StmtType stmt_type = stmt::T_NONE;
  ObExprInfo parents_expr_info;
  ObString literal_prefix;
  ObCollationType nchar_collation = CS_TYPE_INVALID;
  const peg::Node *row_node = NULL;
  bool enable_decimal_int = false;
  share::ObCompatType compat_type = share::COMPAT_MYSQL57;
  bool enable_mysql_compatible_dates = false;
  if (OB_ISNULL(allocator) || OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(session_info->get_collation_connection(coll_type))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session_info, enable_decimal_int))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(session_info, false,
                       enable_mysql_compatible_dates))) {
  } else {
    length_semantics = session_info->get_actual_length_semantics();
    timezone_info = session_info->get_timezone_info();
    stmt_type = stmt::T_NONE;
    nchar_collation = session_info->get_nls_collation_nation();
  }

  int64_t row_idx = 0;
  for (int64_t segment_idx = 0; OB_SUCC(ret) && segment_idx < rows.segments_.count(); ++segment_idx) {
    const auto in_list = peg::membership_expressions(*rows.segments_.at(segment_idx));
    for (int64_t i = 0; OB_SUCC(ret) && i < in_list.size(); ++i, ++row_idx) {
      row_node = &in_list[i];
      if (OB_ISNULL(row_node)) {
        ret = OB_ERR_UNEXPECTED;
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < column_cnt; j++) {
        const peg::Node *element = &membership_element(*row_node, column_cnt, j);
        ObObjParam obj_param;
        ObRawExprResType res_type;
        const LiteralDecodeOptions options{{stmt_type, enable_decimal_int, is_from_pl, false,
            session_info->get_min_const_integer_precision()}, coll_type, timezone_info,
            session_info->get_sql_mode(), compat_type, enable_mysql_compatible_dates,
            false, nullptr, NumericSignMode::Source};
        if (OB_FAIL(ObResolverUtils::resolve_expression_literal(*element, options, *allocator,
                                                               obj_param, literal_prefix))) {
        } else if (OB_FAIL(table_def.access_objs_.push_back(obj_param))) {
        } else {
          res_type.set_meta(obj_param.get_param_meta());
          res_type.set_accuracy(obj_param.get_accuracy());
          res_type.set_result_flag(obj_param.get_result_flag());
          if (row_idx == 0) {
            if (OB_FAIL(table_def.column_types_.push_back(res_type))) {
            }
          } else if (is_prepare_stage
                     && (ObUnknownType == res_type.get_type()
                         || ObUnknownType == table_def.column_types_.at(j).get_type())) {
            // in prepare stage, question marks are resolved as unknown type
            // since unknown type cannot be merged with other normal types
            // we ignore type aggregation if:
            // 1. the current node is a question mark
            // 2. the values table contains a question mark
            // this is safe because correct column types will be deduced later in execute stage
          } else if (!ObSQLUtils::is_same_type(res_type, table_def.column_types_.at(j))) {
            ObExprResType new_res_type;
            ObExprVersion dummy_op(*allocator);
            ObSEArray<ObExprResType, 2> tmp_res_types;
            ObExprTypeCtx type_ctx;
            ObSQLUtils::init_type_ctx(session_info, type_ctx);
            if (OB_FAIL(tmp_res_types.push_back(table_def.column_types_.at(j)))) {
            } else if (OB_FAIL(tmp_res_types.push_back(res_type))) {
            } else if (OB_FAIL(dummy_op.aggregate_result_type_for_merge(new_res_type,
                              &tmp_res_types.at(0), 2,
                              type_ctx, true, false, is_called_in_sql))) {
            } else {
              table_def.column_types_.at(j) = new_res_type;
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObInListResolver::check_can_merge_inlists(const peg::Node *last_in_node,
                                              const peg::Node *cur_in_node,
                                              ObInListsResolverHelper &helper,
                                              InListRewriteInfo &last_info,
                                              bool &can_merge)
{
  int ret = OB_SUCCESS;
  InListRewriteInfo cur_info;
  InListRows previous_rows;
  InListRows current_rows;
  const auto &subject = unparenthesized(peg::predicate_subject(*cur_in_node));
  const int64_t column_cnt = membership_row(subject) ? peg::row_elements(subject).size() : 1;
  const bool inspect_previous = last_info.param_types_.empty();
  if (OB_FAIL(previous_rows.segments_.push_back(last_in_node))) {
  } else if (OB_FAIL(current_rows.segments_.push_back(cur_in_node))) {
  }
  for (int64_t j = 0; OB_SUCC(ret) && can_merge && j < column_cnt; ++j) {
    if (inspect_previous && OB_FAIL(get_inlist_rewrite_info(previous_rows,
                                                             column_cnt, j, helper, last_info))) {
    } else if (OB_FAIL(get_inlist_rewrite_info(current_rows, column_cnt, j, helper, cur_info))) {
    } else if (!last_info.is_valid_as_values_table_ || !cur_info.is_valid_as_values_table_) {
      can_merge = false;
    } else if (j == 0 && last_info.is_question_mark_ && cur_info.is_question_mark_
               && last_info.end_param_idx_ + column_cnt != cur_info.start_param_idx_) {
      can_merge = false;
    } else if (OB_UNLIKELY(last_info.param_types_.count() <= j
                           || cur_info.param_types_.count() <= j)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      can_merge = last_info.param_types_.at(j) == cur_info.param_types_.at(j);
    }
  }
  if (OB_SUCC(ret) && can_merge) {
    last_info.end_param_idx_ = cur_info.end_param_idx_;
  }
  return ret;
}

int ObInListResolver::group_inlists(ObExprResolveContext &resolve_ctx,
                                   bool is_root_condition,
                                   const peg::Node &root,
                                   ObIArray<InListGroup> &groups)
{
  int ret = OB_SUCCESS;
  groups.reset();
  const ObSQLSessionInfo *session_info = resolve_ctx.session_info_;
  const ObStmt *stmt = resolve_ctx.stmt_;
  ObIAllocator &alloc = resolve_ctx.expr_factory_.get_allocator();
  ObCollationType connect_collation = CS_TYPE_INVALID;
  bool enable_decimal_int = false;
  bool is_prepare_stmt = false;
  bool enabled = false;
  const auto logical_operator = peg::binary_operator(root);
  if (OB_ISNULL(session_info)
      || (logical_operator != peg::BinaryOperator::LogicalAnd && logical_operator != peg::BinaryOperator::LogicalOr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (resolve_ctx.current_scope_ == T_WHERE_SCOPE && is_root_condition
             && !resolve_ctx.is_need_print_ && resolve_ctx.secondary_namespace_ == nullptr) {
    if (stmt != nullptr && OB_ISNULL(stmt->get_query_ctx())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(session_info->get_collation_connection(connect_collation))) {
    } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session_info, enable_decimal_int))) {
    } else {
      enabled = true;
      is_prepare_stmt = stmt != nullptr && stmt->get_query_ctx()->is_prepare_stmt();
    }
  }
  if (OB_SUCC(ret)) {
    ObInListsResolverHelper helper(alloc, resolve_ctx.param_list_, resolve_ctx.parameterization_,
                                   connect_collation, session_info->get_nls_collation_nation(),
                                   CS_TYPE_INVALID, enable_decimal_int, is_prepare_stmt);
    InListRewriteInfo previous_info;
    bool previous_is_membership = false;
    const auto operands = peg::logical_operands(root);
    for (const auto &written_operand : operands) {
      if (OB_FAIL(ret)) break;
      const peg::Node *operand = &written_operand.node;
      bool can_merge = false;
      const bool is_membership = enabled && operand->kind() == peg::NodeKind::In
          && peg::membership_source(*operand) == peg::MembershipSource::Expressions
          && ((logical_operator == peg::BinaryOperator::LogicalOr && !peg::predicate_negated(*operand))
              || (logical_operator == peg::BinaryOperator::LogicalAnd && peg::predicate_negated(*operand)));
      if (OB_SUCC(ret) && is_membership && previous_is_membership) {
        const peg::Node *previous = groups.at(groups.count() - 1).node_;
        can_merge = peg::equivalent_expressions(peg::predicate_subject(*previous), peg::predicate_subject(*operand));
        if (can_merge && OB_FAIL(check_can_merge_inlists(previous, operand, helper,
                                                        previous_info, can_merge))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (can_merge) {
        InListGroup &group = groups.at(groups.count() - 1);
        if (group.rows_.segments_.empty()
            && OB_FAIL(group.rows_.segments_.push_back(group.node_))) {
        } else if (OB_FAIL(group.rows_.segments_.push_back(operand))) {
        }
      } else if (OB_FAIL(groups.push_back(InListGroup(operand)))) {
      } else {
        previous_info.reset();
      }
      previous_is_membership = is_membership;
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
