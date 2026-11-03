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
#include "rust/config.h"
#include "common/udt/ob_collection_type.h"
#include "ob_dml_resolver.h"
#include "sql/resolver/dml/ob_view_table_resolver.h"
#include "sql/hybrid_search/ob_hybrid_search_executor.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "sql/resolver/dml/ob_default_value_utils.h"
#include "share/schema/ob_part_mgr_util.h"
#include "sql/pl/ob_pl_package.h"
#include "sql/optimizer/ob_opt_est_utils.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "query/engine/expr/ob_item_type_str.h"
#include "sql/printer/ob_select_stmt_printer.h"
#include "sql/engine/expr/ob_expr_autoinc_nextval.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_expr_version.h"
#include "sql/resolver/dml/ob_insert_resolver.h"
#include "sql/resolver/dml/ob_inlist_resolver.h"
#include "sql/session/ob_local_session_var.h"
#include "sql/session/ob_basic_session_info.h"
#include "sql/engine/aggregate/ob_aggregate_processor.h"
#include "sql/optimizer/ob_opt_selectivity.h"
#include "sql/das/ob_domain_id.h"
#include "query/vector/ob_vector_index_util.h"
#include "sql/engine/expr/ob_expr_regexp.h"
#include "sql/engine/expr/ob_json_param_type.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace pl;

namespace sql
{
ObDMLResolver::ObDMLResolver(ObResolverParams &params)
    : ObStmtResolver(params),
      current_scope_(T_NONE_SCOPE),
      current_level_(0),
      field_list_first_(false),
      parent_namespace_resolver_(NULL),
      column_namespace_checker_(params),
      gen_col_exprs_(),
      query_ref_exec_params_(NULL),
      with_clause_without_record_(false),
      is_prepare_stage_(params.is_prepare_stage_),
      in_pl_(params.secondary_namespace_ || params.is_dynamic_sql_),
      resolve_alias_for_subquery_(true),
      current_view_level_(0),
      view_ref_id_(OB_INVALID_ID),
      is_resolving_view_(false),
      join_infos_(),
      parent_cte_tables_(),
      current_cte_tables_(),
      expr_resv_ctx_(params.query_ctx_),
      cte_ctx_(),
      upper_insert_resolver_(NULL)
{
  column_namespace_checker_.set_joininfos(&join_infos_);
}

ObDMLResolver::~ObDMLResolver()
{
}

int ResolverJoinInfo::assign(const ResolverJoinInfo &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    //skip
  } else if (OB_FAIL(using_columns_.assign(other.using_columns_))) {
  } else if (OB_FAIL(coalesce_expr_.assign(other.coalesce_expr_))) {
  } else {
    table_id_ = other.table_id_;
  }
  return ret;
}

ObDMLStmt *ObDMLResolver::get_stmt()
{
  return static_cast<ObDMLStmt*>(stmt_);
}
int ObDMLResolver::check_in_sysview(bool &in_sysview) const
{
  in_sysview = false;
  return OB_SUCCESS;
}

int ObDMLResolver::alloc_joined_table_item(JoinedTable *&joined_table)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(ptr = allocator_->alloc(sizeof(JoinedTable)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SQL_RESV_LOG(ERROR, "alloc memory for JoinedTable failed", "size", sizeof(JoinedTable));
  } else {
    joined_table = new (ptr) JoinedTable();
  }
  return ret;
}

int ObDMLResolver::create_joined_table_item(
    const ObJoinType joined_type,
    const TableItem *left_table,
    const TableItem *right_table,
    JoinedTable* &joined_table)
{
  int ret = OB_SUCCESS;
  OZ(alloc_joined_table_item(joined_table));
  CK(OB_NOT_NULL(joined_table));
  if (OB_SUCC(ret)) {
    // If dependency is empty, then use inner join
    joined_table->table_id_ = generate_table_id();
    joined_table->type_ = TableItem::JOINED_TABLE;
    joined_table->joined_type_ = joined_type;
    joined_table->left_table_ = const_cast<TableItem*>(left_table);
    joined_table->right_table_ = const_cast<TableItem*>(right_table);

    // push up single table ids (left deep tree)
    // left table ids
    CK(OB_NOT_NULL(joined_table->left_table_));
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (joined_table->left_table_->is_joined_table()) {
      JoinedTable *cur_joined = static_cast<JoinedTable*>(joined_table->left_table_);
      for (int64_t i = 0; OB_SUCC(ret) && i < cur_joined->single_table_ids_.count(); i++) {
        OZ((joined_table->single_table_ids_.push_back)(
                cur_joined->single_table_ids_.at(i)));
      }
    } else {
      OZ((joined_table->single_table_ids_.push_back)(
              joined_table->left_table_->table_id_));
    }

    // right table id
    CK(OB_NOT_NULL(joined_table->right_table_));
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (joined_table->right_table_->is_joined_table()) {
      JoinedTable *cur_joined = static_cast<JoinedTable*>(joined_table->right_table_);
      for (int64_t i = 0; OB_SUCC(ret) && i < cur_joined->single_table_ids_.count(); i++) {
        OZ((joined_table->single_table_ids_.push_back)(
                cur_joined->single_table_ids_.at(i)));
      }
    } else {
      OZ((joined_table->single_table_ids_.push_back)(
              joined_table->right_table_->table_id_));
    }
  }
  return ret;
}


int ObDMLResolver::replace_col_udt_qname(ObQualifiedName& q_name)
{
  int ret = OB_SUCCESS;
  ObQualifiedName udt_col_func_q_name;
  // Only support:
  // 1. table_alias.col_name.udf_member_func, for example: select a.c2.getclobval() from t1 a;
  // 2. table_alias.col_name.member_func.udf, for example: select a.c2.transfrom(xxx).getclobval() from t1 a;
  // Notice:
  // 1. must have table alias name;
  // 2. table_alias.col_name.static_func.udf is not supported, for example:
  //    select a.c2.createxml(xxx).getclobval() from t1 a; creatxml is an static function, not support
  if (q_name.access_idents_.count() >= 3
        && q_name.access_idents_.at(0).type_ == UNKNOWN
        && q_name.access_idents_.at(1).type_ == UNKNOWN
        && q_name.access_idents_.at(2).type_ == PL_UDF) {
    ObQualifiedName udt_col_candidate;
    ObRawExpr* udt_col_ref_expr = NULL;
    if (OB_FAIL(udt_col_candidate.access_idents_.push_back(q_name.access_idents_.at(0)))) {
    } else if (OB_FAIL(udt_col_candidate.access_idents_.push_back(q_name.access_idents_.at(1)))) {
    } else {
      udt_col_candidate.tbl_name_ = q_name.access_idents_.at(0).access_name_;
      udt_col_candidate.col_name_ = q_name.access_idents_.at(1).access_name_;
      udt_col_candidate.ref_expr_= q_name.ref_expr_;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(resolve_column_ref_expr(udt_col_candidate, udt_col_ref_expr))) {
      // should not return error if not found
      ret = OB_SUCCESS;
    } else {
      // mock new q_name with ref_expr and access_idents_.data_[0].type_ = oceanbase::sql::SYS_FUNC
      udt_col_func_q_name.ref_expr_= q_name.ref_expr_;
      if (OB_FAIL(udt_col_func_q_name.access_idents_.push_back(ObObjAccessIdent(ObString("UDT_REF"), OB_INVALID_INDEX)))) {
      } else {
        for (int64_t i = 2; OB_SUCC(ret) && i < q_name.access_idents_.count(); i++) {
          if (OB_FAIL(udt_col_func_q_name.access_idents_.push_back(q_name.access_idents_.at(i)))) {
          }
        }
        if (OB_SUCC(ret)) {
          udt_col_func_q_name.access_idents_.at(0).type_ = SYS_FUNC;
          udt_col_func_q_name.access_idents_.at(0).sys_func_expr_ = static_cast<ObSysFunRawExpr *>(udt_col_ref_expr);
          q_name = udt_col_func_q_name;
        }
      }
    }
  }

  return ret;
}


int ObDMLResolver::get_target_column_list(ObSEArray<ColumnItem, 4> &target_list, ObString& tab_name, bool all_tab,
                                          bool &tab_has_alias, TableItem *&tab_item, bool is_col)
{
  int ret = OB_SUCCESS;
  int64_t num = 0;
  ObDMLStmt *stmt = static_cast<ObDMLStmt *>(stmt_);
  ObArray<ColumnItem> column_items;

  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    num = stmt->get_table_size();
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < num; i++) {
    const TableItem *tmp_table_item = stmt->get_table_item(i);
    column_items.reuse();
    if (OB_ISNULL(tmp_table_item)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_NOT_NULL(tab_name.ptr()) && !is_col && !all_tab && tmp_table_item->table_name_ != tab_name && tmp_table_item->alias_name_ != tab_name) {
      // other table should chose
    } else {
      if (tmp_table_item->alias_name_ == tab_name ||
          (num > 1 && tmp_table_item->table_name_ == tab_name && stmt->get_stmt_type() == stmt::T_SELECT)) {
        tab_has_alias = true;
      }

      tab_item = const_cast<TableItem *>(tmp_table_item);

      if (tmp_table_item->is_basic_table()) {
        if (OB_FAIL(resolve_all_basic_table_columns(*tmp_table_item, false, &column_items))) {
        }
      } else if (tmp_table_item->is_generated_table() || tmp_table_item->is_temp_table()) {
        if (OB_FAIL(resolve_all_generated_table_columns(*tmp_table_item, column_items))) {
        }
      } else if (tmp_table_item->is_function_table()) {
        if (OB_FAIL(resolve_function_table_column_item(*tmp_table_item, column_items))) {
        }
      } else if (tmp_table_item->is_json_table()) {
        if (OB_FAIL(resolve_json_table_column_all_items(*tmp_table_item, column_items))) {
        }
      } else {
        // not set error code, public routine new type may enter here
        LOG_WARN("needn't process table type, current function mainly process json dot notation or json_object with star",
          K_(tmp_table_item->type));
      }

      for (int64_t j = 0; OB_SUCC(ret) && j < column_items.count(); ++j) {
        const ColumnItem &col_item = column_items.at(j);
        if (!is_col || (is_col && col_item.column_name_ == tab_name)) {
          if (OB_FAIL(target_list.push_back(col_item))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::expand_mvt_arguments(const ObIArray<ObAggFunRawExpr *> &aggregates,
                                        ObIArray<ObQualifiedName> &names)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < aggregates.count(); ++i) {
    ObAggFunRawExpr &aggregate = *aggregates.at(i);
    if (aggregate.get_expr_type() != T_FUN_SYS_ST_ASMVT) continue;
    ObRawExpr *row = aggregate.get_real_param_exprs().at(0);
    int64_t index = OB_INVALID_INDEX;
    for (int64_t j = 0; j < names.count(); ++j) {
      if (names.at(j).ref_expr_ == row) index = j;
    }
    if (index == OB_INVALID_INDEX || !names.at(index).is_star_) {
      ret = OB_ERR_PARSER_SYNTAX;
    } else {
      ObQualifiedName &name = names.at(index);
      ObSEArray<ColumnItem, 4> columns;
      bool has_alias = false;
      TableItem *table = nullptr;
      ObSEArray<ObRawExpr *, 16> arguments;
      ObConstRawExpr *option_count = nullptr;
      const int64_t count = aggregate.get_real_param_count();
      OZ(get_target_column_list(columns, name.tbl_name_, false, has_alias, table));
      if (OB_SUCC(ret) && columns.empty()) ret = OB_ERR_PARSER_SYNTAX;
      OZ(ObRawExprUtils::build_const_int_expr(*params_.expr_factory_, ObIntType, count - 1, option_count));
      OZ(arguments.push_back(option_count));
      for (int64_t j = 1; OB_SUCC(ret) && j < count; ++j) {
        OZ(arguments.push_back(aggregate.get_real_param_exprs().at(j)));
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < columns.count(); ++j) {
        ObConstRawExpr *column_name = nullptr;
        OZ(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_, ObVarcharType,
            columns.at(j).column_name_, CS_TYPE_UTF8MB4_GENERAL_CI, column_name));
        OZ(arguments.push_back(columns.at(j).expr_));
        OZ(arguments.push_back(column_name));
      }
      OZ(aggregate.get_real_param_exprs_for_update().assign(arguments));
      OZ(names.remove(index));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_sql_expr(const peg::Node &node, ObRawExpr *&expr,
    ObArray<ObQualifiedName> *output_columns/* = NULL*/)
{
  int ret = OB_SUCCESS;
  bool tmp_field_list_first = field_list_first_;
  ObSysFunRawExpr *func_expr = NULL;
  field_list_first_ = false;
  ObArray<ObQualifiedName> columns;
  bool need_analyze_aggr = false;
  if (output_columns == NULL) {
    output_columns = &columns;
    need_analyze_aggr = true;
  }
  ObArray<ObSubQueryInfo> sub_query_info;
  ObArray<ObVarInfo> sys_vars;
  ObArray<ObAggFunRawExpr*> aggr_exprs;
  ObArray<ObWinFunRawExpr*> win_exprs;
  ObArray<ObUDFInfo> udf_info;
  ObArray<ObOpRawExpr*> op_exprs;
  ObArray<ObInListInfo> inlist_infos;
  ObCollationType collation_connection = CS_TYPE_INVALID;
  ObCharsetType character_set_connection = CHARSET_INVALID;
  ObSEArray<ObMatchFunRawExpr*, 1> match_exprs;
  CK( OB_NOT_NULL(params_.expr_factory_),
      OB_NOT_NULL(stmt_),
      OB_NOT_NULL(get_stmt()),
      OB_NOT_NULL(session_info_));
  OC( (params_.session_info_->get_collation_connection)(collation_connection) );
  OC( (params_.session_info_->get_character_set_connection)(character_set_connection) );

  if (OB_SUCC(ret)) {
    ObExprResolveContext ctx(*params_.expr_factory_,
                             session_info_->get_timezone_info(),
                             OB_NAME_CASE_INVALID);
    ctx.dest_collation_ = collation_connection;
    ctx.connection_charset_ = character_set_connection;
    ctx.param_list_ = params_.param_list_;
    ctx.parameterization_ = params_.parameterization_;
    ctx.syntax_source_ = params_.syntax_->source();
    ctx.is_extract_param_type_ = !params_.is_prepare_protocol_; //when prepare do not extract
    ctx.external_param_info_ = params_.external_param_info_.need_clear_ ? nullptr : &params_.external_param_info_;
    ctx.current_scope_ = current_scope_;
    ctx.stmt_ = static_cast<ObStmt*>(get_stmt());
    ctx.schema_checker_ = schema_checker_;
    ctx.session_info_ = session_info_;
    ctx.secondary_namespace_ = params_.secondary_namespace_;
    ctx.prepare_param_count_ = params_.prepare_param_count_;
    ctx.query_ctx_ = params_.query_ctx_;
    ctx.is_for_dynamic_sql_ = params_.is_dynamic_sql_;
    ctx.view_ref_id_ = view_ref_id_;
    ctx.is_variable_allowed_ = !params_.is_from_create_view_;
    ctx.is_from_show_resolver_ = params_.is_from_show_resolver_;
    ctx.is_expanding_view_ = params_.is_expanding_view_;
    ctx.is_in_system_view_ = params_.is_in_sys_view_;
    ctx.is_need_print_ = params_.is_from_create_view_ || params_.is_from_create_table_;
    ObRawExprResolverImpl expr_resolver(ctx);
    ObIArray<ObUserVarIdentRawExpr *> &user_var_exprs = get_stmt()->get_user_vars();
    bool is_multi_stmt = session_info_->get_cur_exec_ctx() != NULL &&
        session_info_->get_cur_exec_ctx()->get_sql_ctx() != NULL &&
        session_info_->get_cur_exec_ctx()->get_sql_ctx()->is_batch_params_execute();

    OC( (session_info_->get_name_case_mode)(ctx.case_mode_));
    if (OB_SUCC(ret)) {
      if (OB_FAIL(expr_resolver.resolve(node, expr, *output_columns, sys_vars,
                                       sub_query_info, aggr_exprs, win_exprs, udf_info,
                                       op_exprs, user_var_exprs, inlist_infos, match_exprs))) {
        LOG_WARN("resolve expression failed", K(ret), "kind", static_cast<int32_t>(node.kind()));
      }
    }

    if (OB_SUCC(ret)) {
      params_.prepare_param_count_ = ctx.prepare_param_count_; //prepare param count
    }
    OZ(resolve_inlist_info(inlist_infos));
    OC( (resolve_subquery_info)(sub_query_info));
    if (OB_SUCC(ret)) {
      //are there any user variable assignments?
      get_stmt()->set_contains_assignment(expr_resolver.is_contains_assignment());
    }
    if (OB_SUCC(ret)) {
      if (OB_NOT_NULL(params_.query_ctx_)) {
        params_.query_ctx_->set_has_nested_sql(!udf_info.empty());
      }
    }

    OZ (resolve_partition_id_conditions(expr, *output_columns));

    OZ(expand_mvt_arguments(aggr_exprs, *output_columns));

    // resolve column(s)
    if (OB_SUCC(ret) && output_columns->count() > 0) {
      if (tmp_field_list_first && stmt_->is_select_stmt()) {
        ObSelectStmt *sel_stmt = static_cast<ObSelectStmt *>(stmt_);
        if (OB_FAIL(resolve_columns_field_list_first(expr, *output_columns, sel_stmt))) {
        }
      } else if (OB_FAIL(resolve_columns(expr, *output_columns))) {
      }
    }

    if (OB_SUCC(ret) && udf_info.count() > 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("UDFInfo should not found be here!!!", K(ret));
    }
    //try to convert ObUDFRawExpr to ObAggRawExpr for pl agg udf
    if (OB_SUCC(ret)) {
      if (OB_FAIL(convert_udf_to_agg_expr(expr, NULL, ctx))) {
      }
    }
    // resolve sys var(s)
    if (OB_SUCC(ret) && sys_vars.count() > 0) {
      if (OB_FAIL(resolve_sys_vars(sys_vars))) {
      }
    }
    if (OB_SUCC(ret) && match_exprs.count() > 0) {
      if (OB_FAIL(resolve_match_against_exprs(expr, match_exprs, current_scope_))) {
      }
    }

    // Process implicit casts for operator expressions.
    if (OB_SUCC(ret) && op_exprs.count() > 0) {
      if (OB_FAIL(expr->extract_info())) {
      } else if (OB_FAIL(ObRawExprUtils::resolve_op_exprs_for_comparison_implicit_cast(
                                                                                  ctx.expr_factory_,
                                                                                  ctx.session_info_,
                                                                                  op_exprs))) {
      }
    }
    if (OB_SUCC(ret) && aggr_exprs.count() > 0) {
      if (OB_FAIL(resolve_aggr_exprs(expr, aggr_exprs, need_analyze_aggr))) {
      }
    }
    if (OB_SUCC(ret) && win_exprs.count() > 0) {
      if (OB_FAIL(resolve_win_func_exprs(expr, win_exprs))) {
      }
    }
    // resolve special expression, like functions, e.g abs, concat
    // acutally not so special, hmm...
    if (OB_SUCC(ret)) {
      // update flag info
      if (OB_FAIL(expr->extract_info())) {
      } else if (OB_FAIL(resolve_special_expr(expr, current_scope_))) {
      } else if (OB_FAIL(expr->calc_hash())) {
      }
    }
    if (OB_SUCC(ret) &&
        current_scope_ != T_INSERT_SCOPE &&
        !is_multi_stmt &&
        !params_.is_resolve_table_function_expr_) {
      bool is_new = false;
      bool dummp_bool = false;
      if (OB_FAIL(expr_resv_ctx_.get_shared_instance(expr, expr, is_new, dummp_bool))) {
      }
    }
    //LOG_DEBUG("resolve_sql_expr:5", "usec", ObSQLUtils::get_usec());
    // refresh info again
    if (OB_SUCC(ret)) {
      bool skip_check = false;
      if (OB_FAIL(expr->extract_info())) {
      } else if (OB_FAIL(check_expr_param(*expr))) {
      } else if (OB_FAIL(ObRawExprUtils::check_composite_cast(expr, *schema_checker_, session_info_->is_varparams_sql_prepare(), skip_check))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_partition_id_conditions(ObRawExpr *&expr,
                                                   ObIArray<ObQualifiedName> &columns)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (expr->is_calc_part_expr()) {
    OZ (reset_calc_part_id_param_exprs(expr, columns));
  } else if (expr->get_expr_type() == T_OP_AND || expr->get_expr_type() == T_OP_BOOL) {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      OZ (SMART_CALL(resolve_partition_id_conditions(expr->get_param_expr(i), columns)));
    }
  } else if (expr->get_expr_type() == T_OP_EQ && expr->get_param_count() == 2
             && expr->get_param_expr(0) != nullptr && expr->get_param_expr(0)->is_calc_part_expr()) {
    OZ (reset_calc_part_id_param_exprs(expr->get_param_expr(0), columns));
  }
  return ret;
}

int ObDMLResolver::reset_calc_part_id_param_exprs(ObRawExpr *&expr,
                                                  ObIArray<ObQualifiedName> &columns)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObQualifiedName, 3> arguments;
  if (OB_ISNULL(get_stmt()) || OB_ISNULL(expr) || OB_ISNULL(params_.schema_checker_) ||
      OB_ISNULL(params_.expr_factory_) || OB_ISNULL(params_.session_info_) ||
      OB_UNLIKELY(!expr->is_calc_part_expr() ||
                  (expr->get_param_count() != 2 && expr->get_param_count() != 3))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      bool found = false;
      for (int64_t j = 0; OB_SUCC(ret) && !found && j < columns.count(); ++j) {
        if (columns.at(j).ref_expr_ == expr->get_param_expr(i)) {
          OZ (arguments.push_back(columns.at(j)));
          found = true;
        }
      }
      if (OB_SUCC(ret) && !found) { ret = OB_INVALID_ARGUMENT; }
    }
  }
  if (OB_SUCC(ret)) {
    ObString table_name;
    ObString index_name;
    ObString opt_str;
    if (arguments.count() == 2) {
      table_name = arguments.at(0).col_name_;
      opt_str = arguments.at(1).col_name_;
    } else {
      table_name = arguments.at(0).col_name_;
      index_name = arguments.at(1).col_name_;
      opt_str = arguments.at(2).col_name_;
    }
    int64_t tbl_id = -1;
    int64_t ref_id = -1;
    bool find_it = false;
    ObString subpart_str(7, "SUBPART");
    ObString part_str(4, "PART");
    PartitionIdCalcType calc_type = CALC_NORMAL;
    if (0 == subpart_str.case_compare(opt_str)) {
      calc_type = CALC_NORMAL;
    } else if (0 == part_str.case_compare(opt_str)) {
      calc_type = CALC_IGNORE_SUB_PART;
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
    for(int64_t i = 0; OB_SUCC(ret) && !find_it && i < get_stmt()->get_table_items().count(); ++i) {
      TableItem *table_item = NULL;
      if (OB_ISNULL(table_item = get_stmt()->get_table_items().at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (0 == table_name.case_compare(table_item->table_name_)) {
        if (!index_name.empty()) {
          if (OB_FAIL(find_table_index_infos(index_name, table_item, find_it, tbl_id, ref_id))) {
          } else {/*do nothing*/}
        } else {
          find_it = true;
          tbl_id = table_item->table_id_;
          ref_id = table_item->ref_id_;
        }
      } else {/*do nothing*/}
    }
    CK (OB_NOT_NULL(session_info_));
    if (OB_SUCC(ret)) {
      share::schema::ObSchemaGetterGuard *schema_guard = NULL;
      const share::schema::ObTableSchema *table_schema = NULL;
      if (!find_it) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_ISNULL(schema_guard = params_.schema_checker_->get_schema_guard())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_guard->get_table_schema( ref_id, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        schema::ObPartitionLevel part_level = table_schema->get_part_level();
        ObRawExpr *part_expr = get_stmt()->get_part_expr(tbl_id, ref_id);
        ObRawExpr *subpart_expr = get_stmt()->get_subpart_expr(tbl_id, ref_id);
        ObRawExpr *new_part_expr = NULL;
        ObRawExpr *new_subpart_expr = NULL;
        if (OB_FAIL(copy_schema_expr(*params_.expr_factory_,
                                     part_expr,
                                     new_part_expr))) {
        } else if (OB_FAIL(copy_schema_expr(*params_.expr_factory_,
                                            subpart_expr,
                                            new_subpart_expr))) {
        } else if (OB_FAIL(ObRawExprUtils::build_calc_part_id_expr(*params_.expr_factory_,
                                                                   *params_.session_info_,
                                                                   ref_id,
                                                                   part_level,
                                                                   new_part_expr,
                                                                   new_subpart_expr,
                                                                   expr))) {
        } else {
          expr->set_partition_id_calc_type(calc_type);
          for (int64_t i = columns.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
            for (int64_t j = 0; j < arguments.count(); ++j) {
              if (columns.at(i).ref_expr_ == arguments.at(j).ref_expr_) {
                OZ (columns.remove(i));
                break;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}
// resolve order by items when, first look up in select items.
////create table t1(c1 int,c2 int);
////create table t2(c1 int, c2 int);
////select a.c1, b.c2 from t1 a, t2 b order by (c1+c2); is legal, order byafterofc1andc2respectivelycorrespondingselect itemsina.c1andb.c2
int ObDMLResolver::resolve_columns_field_list_first(ObRawExpr *&expr, ObArray<ObQualifiedName> &columns, ObSelectStmt* sel_stmt)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr*> real_exprs;
  if (OB_ISNULL(sel_stmt) || OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); i++) {
    bool found = false;
    if (columns.at(i).tbl_name_.empty()) {
      for (int64_t j = 0; OB_SUCC(ret) && j < sel_stmt->get_select_item_size(); j++) {
        ObRawExpr *select_item_expr = NULL;
        if (OB_ISNULL(select_item_expr = sel_stmt->get_select_item(j).expr_)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (select_item_expr->is_column_ref_expr()) {
          ObColumnRefRawExpr *column_ref_expr = static_cast<ObColumnRefRawExpr *>(select_item_expr);
          if (ObCharset::case_insensitive_equal(sel_stmt->get_select_item(j).is_real_alias_ ? sel_stmt->get_select_item(j).alias_name_
                                              : column_ref_expr->get_column_name(), columns.at(i).col_name_)) {
            if (found) {
              ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
              ret = OB_NON_UNIQ_ERROR;
              LOG_USER_ERROR(OB_NON_UNIQ_ERROR,
                             columns.at(i).col_name_.length(),
                             columns.at(i).col_name_.ptr(),
                             scope_name.length(),
                             scope_name.ptr());
            } else {
              found = true;
              if (OB_FAIL(real_exprs.push_back(column_ref_expr))) {
              } else if (OB_FAIL(ObRawExprUtils::replace_ref_column(expr, columns.at(i).ref_expr_, column_ref_expr))) {
              } else { /* do nothing */ }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret) && false == found) {
      ObQualifiedName &q_name = columns.at(i);
      ObRawExpr *real_ref_expr = NULL;
      if (OB_FAIL(replace_col_udt_qname(q_name))) {
      } else if (OB_FAIL(resolve_qualified_identifier(q_name, columns, real_exprs, real_ref_expr))) {
        LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve column ref expr failed", K(ret), K(q_name));
        report_user_error_msg(ret, expr, q_name);
      } else if (OB_FAIL(real_exprs.push_back(real_ref_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::replace_ref_column(expr, q_name.ref_expr_, real_ref_expr))) {
      } else { /*do nothing*/ }
    }
  }

  return ret;
}

int ObDMLResolver::generate_pl_data_type(ObRawExpr *expr, pl::ObPLDataType &pl_data_type)
{
  int ret = OB_SUCCESS;

  CK (OB_NOT_NULL(expr));
  CK (OB_NOT_NULL(params_.secondary_namespace_));
  CK (expr->get_result_type().is_valid());
  if (OB_SUCC(ret)) {
    pl::ObPLDataType final_type;
    // T_OBJ_ACCESS_REF expr, access obj type (not user defined type)
    bool access_obj_type = false;
    if (expr->is_obj_access_expr()) {
      // T_OBJ_ACCESS_REF return ObExtendType for object access for writing, get obj type
      // from %final_type;
      // see comment in ObPLInto::add_into
      const auto &access_expr = static_cast<const ObObjAccessRawExpr &>(*expr);
      OZ(access_expr.get_final_type(final_type));
      OX(access_obj_type = !final_type.is_user_type());
    }
    if (OB_FAIL(ret)) {
    } else if (expr->get_result_type().is_ext() && !access_obj_type) {
      CK (expr->is_obj_access_expr());
      OX (pl_data_type = final_type);
      if (OB_SUCC(ret) && NULL != params_.secondary_namespace_) {
        const pl::ObUserDefinedType *user_type = NULL;
        OZ (params_.secondary_namespace_->get_pl_data_type_by_id(final_type.get_user_type_id(), user_type));
      }
    } else {
      ObDataType type;
      if (access_obj_type) {
        type.set_meta_type(final_type.get_data_type()->get_meta_type());
        type.set_accuracy(final_type.get_data_type()->get_accuracy());
      } else {
        type.set_meta_type(expr->get_result_type().get_obj_meta());
        type.set_accuracy(expr->get_result_type().get_accuracy());
      }
      OX (pl_data_type.set_data_type(type));
      OX (pl_data_type.set_type(PL_OBJ_TYPE));
    }
  }

  return ret;
}

int ObDMLResolver::resolve_into_variables(rust::Slice<const peg::Node> variables,
                                          ObIArray<ObString> &user_vars,
                                          ObIArray<ObRawExpr*> &pl_vars,
                                          ObSelectStmt *select_stmt)
{
  int ret = OB_SUCCESS;

  CK(OB_NOT_NULL(get_stmt()));

  if (OB_SUCC(ret)) {
    current_scope_ = T_INTO_SCOPE;
    ObRawExpr *expr = NULL;
    const peg::Node *ch_node = nullptr;
    ObBitSet<> user_var_idx;
    for (int64_t i = 0; OB_SUCC(ret) && i < variables.size(); ++i) {
      ch_node = &variables[i];
      expr = NULL;
      if (OB_SUCC(ret)) {
        if (ch_node->kind() == peg::NodeKind::UserVariable) {
          ObString var_name;
          const auto ch_node_text = ch_node->text();
          OZ(ob_write_string(*allocator_, syntax_text(ch_node_text), var_name));
          ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, var_name);
          OZ (user_vars.push_back(var_name));
          OZ (user_var_idx.add_member(i));
        } else {
          if (OB_NOT_NULL(params_.secondary_namespace_)) { // PL statement's Prepare phase
            CK(OB_NOT_NULL(params_.allocator_), OB_NOT_NULL(params_.expr_factory_));
            const pl::ObPLVar* var = NULL;
            bool need_reset_inout_flag = false;
            if (OB_SUCC(ret) && ch_node->kind() == peg::NodeKind::Parameter) {
              // get var from symbol table
              int64_t idx = peg::placeholder_index(*ch_node);
              const pl::ObPLSymbolTable* symbol_table = NULL;
              CK (OB_NOT_NULL(symbol_table = params_.secondary_namespace_->get_symbol_table()));
              CK (OB_NOT_NULL(var = symbol_table->get_symbol(idx)));
              if (OB_SUCC(ret)) {
                if (var->get_name().prefix_match(pl::ObPLResolver::ANONYMOUS_ARG)) {
                  // This must be a write scenario, but process_datatype_or_questionmark
                  // will mistakenly mark it as read. So if is_referenced != true ,
                  // we need to change back the name_ and is_referenced flag of this var
                  if (!var->is_referenced()) {
                    need_reset_inout_flag = true;
                  }
                }
              }
            }

            OZ (pl::ObPLResolver::resolve_raw_expr(*ch_node,
                                                   params_.syntax_->source(),
                                                   *params_.allocator_,
                                                   *params_.expr_factory_,
                                                   *params_.secondary_namespace_,
                                                   params_.is_prepare_protocol_,
                                                   expr,
                                                   true));
            OZ (pl_vars.push_back(expr));
          } else if (params_.is_prepare_protocol_) { // RETURNING clause in dynamic SQL, followed by QuestionMark
            if (OB_SUCC(ret) && ch_node->kind() != peg::NodeKind::Parameter) {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "dynamic sql into variable not a question mark");
            } else if (params_.is_dynamic_sql_ &&
                       OB_NOT_NULL(params_.param_list_) &&
                       params_.param_list_->count() > 0) {
              ObConstRawExpr *c_expr = NULL;
              OZ (params_.expr_factory_->create_raw_expr(T_QUESTIONMARK, c_expr));
              OX (expr = c_expr);
            } else {
              ObSEArray<ObQualifiedName, 1> columns;
              ObSEArray<ObVarInfo, 1> var_infos;
              OZ (ObResolverUtils::resolve_const_expr(params_,
                                                    *ch_node,
                                                      expr,
                                                      &var_infos));
              CK (0 == var_infos.count());
            }
            OZ (pl_vars.push_back(expr));
          } else {
            /*
             * Directly execute select 1 into a in sql; mysql will report “ERROR 1327 (42000): Undeclared variable: a”
             * */
            ret = OB_ERR_SP_UNDECLARED_VAR;
            const auto ch_node_text = ch_node->text();
            LOG_USER_ERROR(OB_ERR_SP_UNDECLARED_VAR, static_cast<int>(ch_node_text.size()), ch_node_text.data());
          }
        }
      }
    }
    if (OB_SUCC(ret) && !pl_vars.empty() && !user_vars.empty()) {
      CK( OB_NOT_NULL(params_.expr_factory_),
          OB_NOT_NULL(params_.session_info_) );
      if (OB_SUCC(ret)) {
        ObArray<ObRawExpr*> tmp_exprs;
        int64_t pl_var_idx = 0;
        for (int64_t i = 0; OB_SUCC(ret) && i < variables.size(); ++i) {
          expr = NULL;
          ch_node = &variables[i];
          if (user_var_idx.has_member(i)) {
            const auto ch_node_text = ch_node->text();
            OZ( ObRawExprUtils::build_get_user_var(
                  *params_.expr_factory_,
                  syntax_text(ch_node_text),
                  expr,
                  params_.session_info_,
                  params_.query_ctx_,
                  &get_stmt()->get_user_vars()) );
            OZ( tmp_exprs.push_back(expr) );
          } else {
            OZ( tmp_exprs.push_back(pl_vars.at(pl_var_idx++)) );
          }
        }
        OZ (pl_vars.assign(tmp_exprs));
        user_vars.reset();
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (!user_vars.empty()) {
      // into user var in mysql mode
      CK (OB_NOT_NULL(select_stmt));
      if (OB_SUCC(ret) && NULL != params_.secondary_namespace_ &&
          select_stmt->get_select_items().count() != user_vars.count()) {
        ret = OB_ERR_COLUMN_SIZE;
      }
    } else { // into
      bool has_type_record_type = false;
      const ObPLDataType *into_var_type = NULL;
      for (int64_t i = 0; OB_SUCC(ret) && !has_type_record_type && i < pl_vars.count(); ++i) {
        CK (OB_NOT_NULL(pl_vars.at(i)));
        if (OB_SUCC(ret) && pl_vars.at(i)->is_obj_access_expr()) {
          pl::ObPLDataType pl_type;
          ObObjAccessRawExpr* access_expr = static_cast<ObObjAccessRawExpr*>(pl_vars.at(i));
          OZ (access_expr->get_final_type(pl_type));
          if (OB_FAIL(ret)) {
          } else if (pl_type.is_type_record()) {
            has_type_record_type = true;
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (has_type_record_type && pl_vars.count() != 1) {
        ret = OB_ERR_MULTI_RECORD;
      }

      /* If we reach here without errors, there are two possibilities:
        1. The into variable has only one type record.
        2. The into variable has no type record. */
      if (OB_SUCC(ret)) {
        pl::ObPLDataType pl_type;
        const ObUserDefinedType *into_user_type = NULL;
        const ObRecordType *into_record_type = NULL;
        int64_t value_expr_count = 0;
        int64_t into_data_type_count = 0;
        bool is_compatible = true;
        bool skip_comp = false;
        if (has_type_record_type) {
          ObObjAccessRawExpr* access_expr = NULL;
          const pl::ObPLDataType *element_type = NULL;
          CK (1 == pl_vars.count());
          OX (access_expr = static_cast<ObObjAccessRawExpr*>(pl_vars.at(0)));
          OZ (access_expr->get_final_type(pl_type));
          OZ (params_.secondary_namespace_->get_pl_data_type_by_id(pl_type.get_user_type_id(), into_user_type));
          CK (OB_NOT_NULL(into_user_type));
          CK (into_user_type->is_record_type());
          OX (into_record_type = static_cast<const ObRecordType*>(into_user_type));
          OX (into_data_type_count = into_record_type->get_record_member_count());
          for (int64_t i = 0; OB_SUCC(ret) && i < into_data_type_count; ++i) {
            element_type = into_record_type->get_record_member_type(i);
            CK (OB_NOT_NULL(element_type));
            CK (0 < access_expr->get_orig_access_idxs().count());
            if (OB_SUCC(ret) && element_type->is_type_record()) {
              ret = OB_ERR_WRONG_TYPE_FOR_VAR;
              LOG_USER_ERROR(OB_ERR_WRONG_TYPE_FOR_VAR,
                             access_expr->get_orig_access_idxs().at(access_expr->get_orig_access_idxs().count() - 1).var_name_.length(),
                             access_expr->get_orig_access_idxs().at(access_expr->get_orig_access_idxs().count() - 1).var_name_.ptr());
              LOG_ERROR("inconsistent datatypes", K(ret));
            }
          }
        } else {
          into_data_type_count = pl_vars.count();
        }
        CK (OB_NOT_NULL(select_stmt));
        if (OB_SUCC(ret)) {
          OX (value_expr_count = select_stmt->get_select_items().count());
        }
        if (OB_SUCC(ret)) {
          if (value_expr_count > into_data_type_count) {
            ret = OB_ERR_NOT_ENOUGH_VALUES;
          } else if (value_expr_count < into_data_type_count) {
            ret = OB_ERR_TOO_MANY_VALUES;
          }
          CK(OB_NOT_NULL(params_.session_info_));
          for (int64_t i = 0; OB_SUCC(ret) && is_compatible && i < into_data_type_count; ++i) {
            ObRawExpr *value_expr = NULL;
            value_expr = select_stmt->get_select_items().at(i).expr_;
            pl::ObPLDataType into_pl_type;
            if (OB_ISNULL(value_expr)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(value_expr->formalize(params_.session_info_))) {
            } else {
              if (has_type_record_type) {
                CK (OB_NOT_NULL(into_record_type));
                CK (OB_NOT_NULL(into_record_type->get_record_member_type(i)));
                OX (into_pl_type = *(into_record_type->get_record_member_type(i)));
              } else if (pl_vars.at(i)->get_expr_type() == T_QUESTIONMARK) {
                skip_comp = true;
              } else {
                OZ (generate_pl_data_type(pl_vars.at(i), into_pl_type));
              }
            }
            if (OB_SUCC(ret) && !skip_comp) {
              if (value_expr->get_data_type() != ObExtendType && into_pl_type.is_obj_type()) {
                CK (OB_NOT_NULL(into_pl_type.get_data_type()));
                OX (is_compatible = cast_supported(value_expr->get_data_type(),
                                                  value_expr->get_collation_type(),
                                                  into_pl_type.get_data_type()->get_obj_type(),
                                                  into_pl_type.get_data_type()->get_collation_type()));
              } else if ((value_expr->get_data_type() == ObUserDefinedSQLType && into_pl_type.is_opaque_type()) ||
                         (value_expr->get_data_type() == ObGeometryType && into_pl_type.is_record_type())) {
                // xml/gis to pl extend, dest collation type is not used
                OX (is_compatible = cast_supported(value_expr->get_data_type(),
                                                   value_expr->get_collation_type(),
                                                   ObExtendType, CS_TYPE_BINARY));
              } else if (value_expr->get_data_type() == ObUserDefinedSQLType) {
                // sql udt to pl extend
                uint16_t subschema_id = value_expr->get_subschema_id();
                ObSqlUDTMeta udt_meta;
                if (OB_FAIL(session_info_->get_cur_exec_ctx()->get_sqludt_meta_by_subschema_id(subschema_id, udt_meta))) {
                } else if (!ObObjUDTUtil::ob_is_supported_sql_udt(udt_meta.udt_id_)) {
                } else if (into_pl_type.is_udt_type() && into_pl_type.get_user_type_id() != udt_meta.udt_id_) {
                  is_compatible = false;
                } else {
                  is_compatible = cast_supported(value_expr->get_data_type(),
                                                  value_expr->get_collation_type(),
                                                  ObExtendType, CS_TYPE_BINARY);
                }
              } else if (value_expr->get_data_type() == ObExtendType &&
                        (!into_pl_type.is_obj_type() ||
                         (into_pl_type.get_data_type() != NULL && into_pl_type.get_data_type()->get_meta_type().is_ext()))) {
                uint64_t left_udt_id = value_expr->get_udt_id();
                uint64_t right_udt_id = (NULL == into_pl_type.get_data_type()) ? into_pl_type.get_user_type_id()
                                                                                : into_pl_type.get_data_type()->get_udt_id();
                if (left_udt_id != right_udt_id) {
                  is_compatible = false;
                } else {
                  // same composite type, compatible is true, do nothing.
                }
              } else {
                is_compatible = false;
              }
            }
            if (OB_SUCC(ret) && !is_compatible) {
              if (into_pl_type.is_udt_type()) {
                ret = OB_ERR_INVALID_TYPE_FOR_OP;
                LOG_ERROR("inconsistent datatypes", K(ret), K(into_pl_type));
              } else {
                ret = OB_ERR_LOCAL_COLL_IN_SQL;
              }
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret) && NULL != select_stmt) {
    ObIArray<SelectItem> &select_items = select_stmt->get_select_items();
    CK(OB_NOT_NULL(params_.session_info_));
    for (int64_t i = 0; i < select_items.count() && OB_SUCC(ret); i++) {
      SelectItem &item = select_items.at(i);
      ObRawExpr *expr = NULL;
      if (OB_ISNULL(expr = item.expr_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(expr->formalize(params_.session_info_))) {
      } else if (ob_is_temporal_type(expr->get_data_type())) {
        // add implicit cast to varchar type
        ObCastMode cast_mode = CM_NONE;
        ObExprResType cast_dst_type;
        ObRawExpr *new_expr = NULL;
        ObCollationType coll_type = CS_TYPE_INVALID;
        const int64_t temporal_max_len = 64;
        cast_dst_type.set_type(ObVarcharType);
        cast_dst_type.set_collation_level(CS_LEVEL_IMPLICIT);
        cast_dst_type.set_length(temporal_max_len);
        cast_dst_type.set_result_flag(expr->get_result_type().get_result_flag());
        if (T_FUN_SYS_CAST == expr->get_expr_type()) {
          cast_dst_type.add_decimal_int_cast_mode(expr->get_cast_mode());
        }
        if (OB_FAIL(params_.session_info_->get_collation_connection(coll_type))) {
        } else if (FALSE_IT(cast_dst_type.set_collation_type(coll_type))) {
        } else if (OB_FAIL(ObSQLUtils::get_default_cast_mode(params_.session_info_, cast_mode))) {
        } else if (OB_FAIL(ObRawExprUtils::try_add_cast_expr_above(
                                              params_.expr_factory_, params_.session_info_,
                                              *expr,  cast_dst_type, cast_mode, new_expr))) {
        } else if (OB_FAIL(new_expr->add_flag(IS_OP_OPERAND_IMPLICIT_CAST))) {
        } else {
          item.expr_ = new_expr;
        }
      }
    }
  }
  return ret;
}

//used to find column in all namespace
//search column ref in table columns
//update, delete, insert only has basic table column
//select has joined table column and  basic table column
//so select resolver will overwrite it
int ObDMLResolver::resolve_column_ref_for_subquery(const ObQualifiedName &q_name, ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resolve_basic_column_ref(q_name, real_ref_expr))) {
    LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve basic column failed", K(ret), K(q_name));
  }
  return ret;
}

//resolve column ref expr that column in single basic or alias table
// TODO bin.lb:  remove resolve_generated_table_column_item
int ObDMLResolver::resolve_basic_column_ref(const ObQualifiedName &q_name, ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  //check column namespace
  const TableItem *table_item = NULL;
  ColumnItem *column_item = NULL;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_NOT_INIT;
  } else {
    column_namespace_checker_.set_dml_stmt(get_stmt());
    if (OB_FAIL(column_namespace_checker_.check_table_column_namespace(q_name, table_item))) {
      LOG_WARN_IGNORE_COL_NOTFOUND(ret, "check basic column namespace failed", K(ret), K(q_name));
    } else if (OB_ISNULL(table_item)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table item is invalid", KPC(table_item));
    } else {
      if (table_item->is_basic_table()) {
        if (OB_FAIL(resolve_basic_column_item(*table_item, q_name.col_name_, false, column_item))) {
        }
      } else if (table_item->is_generated_table() || table_item->is_temp_table()) {
        if (OB_FAIL(resolve_generated_table_column_item(*table_item, q_name.col_name_, column_item))) {
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret)) {
        real_ref_expr = column_item->expr_;
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_basic_column_item(const TableItem &table_item,
                                             const ObString &column_name,
                                             bool include_hidden,
                                             ColumnItem *&col_item,
                                             ObDMLStmt *stmt /* = NULL */)
{
  int ret = OB_SUCCESS;
  ObColumnRefRawExpr *col_expr = NULL;
  ObSchemaGetterGuard *schema_guard = NULL;
  ColumnItem column_item;
  if (NULL == stmt) {
    stmt = get_stmt();
  }
  if (OB_ISNULL(stmt)
      || OB_ISNULL(schema_checker_)
      || OB_ISNULL(schema_guard = schema_checker_->get_schema_guard())
      || OB_ISNULL(params_.expr_factory_)
      || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!table_item.is_basic_table()
                         && !table_item.is_fake_cte_table())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (NULL != (col_item = stmt->get_column_item(table_item.table_id_, column_name))) {
    if (!include_hidden) {
      if (!ObCharset::case_insensitive_equal(column_name, OB_HIDDEN_PK_INCREMENT_COLUMN_NAME)) {
        //do nothing
      } else if (current_scope_ == T_UPDATE_SCOPE || current_scope_ == T_INSERT_SCOPE) {
        ret = OB_ERR_BAD_FIELD_ERROR;
      }
    }
  } else {
    bool is_uni = false;
    bool is_mul = false;
    //not resolve, so add column item to dml stmt
    const ObColumnSchemaV2 *col_schema = NULL;
    const ObTableSchema *table_schema = NULL;
    uint64_t tid = table_item.ref_id_;
    if (!include_hidden) {
      if (!ObCharset::case_insensitive_equal(column_name, OB_HIDDEN_PK_INCREMENT_COLUMN_NAME)) {
        //do nothing
      } else if (current_scope_ == T_UPDATE_SCOPE || current_scope_ == T_INSERT_SCOPE) {
        //do nothing include is false
      } else if (ObResolverUtils::is_restore_user(*session_info_)
                 || session_info_->is_inner()) {
        include_hidden = true;
      } else {
        include_hidden = true;
        if (T_NONE_SCOPE == params_.hidden_column_scope_) {
          params_.hidden_column_scope_ = current_scope_;
          params_.hidden_column_name_ = OB_HIDDEN_PK_INCREMENT_COLUMN_NAME;
        }
      }
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(schema_checker_->get_table_schema( tid, table_schema))) {
    } else if (OB_FAIL(get_column_schema(tid, column_name, col_schema, include_hidden))) {
    } else if (OB_ISNULL(col_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObRawExprUtils::build_column_expr(*params_.expr_factory_, *col_schema, session_info_, col_expr))) {
    } else if (OB_FAIL(table_schema->is_unique_key_column(*schema_guard,
                                                          col_schema->get_column_id(),
                                                          is_uni))) {
    } else if (OB_FAIL(table_schema->is_multiple_key_column(*schema_guard,
                                                            col_schema->get_column_id(),
                                                            is_mul))) {
    } else {
      col_expr->set_column_attr(table_item.get_table_name(), col_schema->get_column_name_str());
      col_expr->set_from_alias_table(!table_item.alias_name_.empty());
      col_expr->set_database_name(table_item.database_name_);
      //column maybe from alias table, so must reset ref id by table id from table_item
      col_expr->set_ref_id(table_item.table_id_, col_schema->get_column_id());
      col_expr->set_unique_key_column(is_uni);
      col_expr->set_mul_key_column(is_mul);
      if (!table_item.alias_name_.empty()) {
        col_expr->set_table_alias_name();
      }
      bool is_lob_column = is_lob_storage(col_schema->get_data_type());
      col_expr->set_lob_column(is_lob_column);
      column_item.expr_ = col_expr;
      if (session_info_->get_ddl_info().is_ddl()) {
        column_item.set_default_value(col_schema->get_orig_default_value());
      } else {
        column_item.set_default_value(col_schema->get_cur_default_value());
      }
    }
    if (OB_SUCC(ret)) {
      ObString col_def;
      ObRawExpr *ref_expr = NULL;
      bool domain_id_need_column_ref_expr = false;
      if (ObDomainIdUtils::is_domain_id_index_col(col_schema)
          && OB_FAIL(check_domain_id_need_column_ref_expr(*stmt, schema_guard, domain_id_need_column_ref_expr, col_schema))) {
      } else if (col_schema->is_generated_column()) {
        column_item.set_default_value(ObObj()); // set null to generated default value
        if (OB_FAIL(col_schema->get_cur_default_value().get_string(col_def))) {
        } else if (OB_FAIL(ObSQLUtils::convert_sql_text_from_schema_for_resolve(*allocator_,
                                              session_info_->get_dtc_params(), col_def))) {
        } else if (OB_FAIL(resolve_generated_column_expr(col_def, table_item, col_schema,
                                                         *col_expr, ref_expr, true, stmt))) {
        } else {
          col_expr->set_dependant_expr(ref_expr);
          // for view, cid need resolve_basic_column_item in data_table select stmt
          if (domain_id_need_column_ref_expr) {
            // select __doc_id from rowkey doc instead of generated by auto inc seq in tablet.
            col_expr->del_column_flag(VIRTUAL_GENERATED_COLUMN_FLAG);
          }
        }
      } else if (col_schema->is_default_expr_v2_column()) {
        const bool used_for_generated_column = false;
        if (OB_FAIL(col_schema->get_cur_default_value().get_string(col_def))) {
        } else if (OB_FAIL(ObSQLUtils::convert_sql_text_from_schema_for_resolve(*allocator_,
                                              session_info_->get_dtc_params(), col_def))) {
        } else if (OB_FAIL(resolve_generated_column_expr(col_def, table_item, col_schema, *col_expr,
                                                         ref_expr, used_for_generated_column, stmt))) {
        } else {
          column_item.set_default_value_expr(ref_expr);
        }
      }
    }
    //init column item
    if (OB_SUCC(ret)) {
      column_item.table_id_ = col_expr->get_table_id();
      column_item.column_id_ = col_expr->get_column_id();
      column_item.column_name_ = col_expr->get_column_name();
      column_item.base_tid_ = tid;
      column_item.base_cid_ = column_item.column_id_;
      column_item.is_geo_ = col_schema->is_geometry();
      if (OB_FAIL(stmt->add_column_item(column_item))) {
      } else if (OB_FAIL(col_expr->pull_relation_id())) {
      } else {
        col_item = stmt->get_column_item(stmt->get_column_size() - 1);
      }
    }
  }
  return ret;
}

int ObDMLResolver::replace_col_ref_prefix(ObQualifiedName &col_ref, uint64_t idx, ObQualifiedName &q_name, bool &try_success)
{
  int ret = OB_SUCCESS;

  ObRawExpr* col_ref_expr = NULL;

  if (col_ref.tbl_name_.empty()) {
    // do nothing ...
  } else if (OB_FAIL(resolve_column_ref_expr(col_ref, col_ref_expr))) {
    // should not return error if not found
    ret = OB_SUCCESS;
  } else if (OB_ISNULL(col_ref_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (col_ref_expr->is_column_ref_expr()
             && !(static_cast<ObColumnRefRawExpr *>(col_ref_expr))->is_from_alias_table()
             && OB_NOT_NULL(params_.query_ctx_)
             && params_.query_ctx_->available_tb_id_ > (static_cast<ObColumnRefRawExpr *>(col_ref_expr))->get_table_id()) {
    // To reference an attribute or method of a table element, the table must
    // have an alias and the alias must qualify the reference.
    ret = OB_ERR_BAD_FIELD_ERROR;
  } else if (col_ref_expr->get_result_type().is_ext()
              || col_ref_expr->get_result_type().is_geometry()) {
    col_ref.ref_expr_= q_name.ref_expr_;
    col_ref.access_idents_.reset();
    if (OB_FAIL(col_ref.access_idents_.push_back(ObObjAccessIdent(ObString("UDT_REF"), OB_INVALID_INDEX)))) {
    } else {
      for (int64_t i = idx + 1; OB_SUCC(ret) && i < q_name.access_idents_.count(); i++) {
        if (OB_FAIL(col_ref.access_idents_.push_back(q_name.access_idents_.at(i)))) {
        }
      }
      if (OB_SUCC(ret)) {
        col_ref.access_idents_.at(0).type_ = SYS_FUNC;
        col_ref.access_idents_.at(0).sys_func_expr_ = col_ref_expr;
        q_name = col_ref;
        q_name.database_name_.reset();
        q_name.tbl_name_.reset();
        q_name.col_name_.reset();
        try_success = true;
      }
    }
  }
  return ret;
}

int ObDMLResolver::replace_col_ref_prefix(ObQualifiedName &q_name)
{
  int ret = OB_SUCCESS;
  int64_t idx = 2;
  bool try_success = false;
  while (OB_SUCC(ret) && idx >= 0) {
    if (idx >= (q_name.access_idents_.count() - 1) || q_name.access_idents_.at(idx).has_brackets_) {
      idx--;
    } else {
      ObQualifiedName col_ref;
      for (int64_t i = 0; OB_SUCC(ret) && i <= idx; ++i) {
        if (OB_FAIL(col_ref.access_idents_.push_back(q_name.access_idents_.at(i)))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (FALSE_IT(col_ref.ref_expr_ = q_name.ref_expr_)) {
      } else if (FALSE_IT(col_ref.format_qualified_name())) {
      } else if (OB_FAIL(replace_col_ref_prefix(col_ref, idx, q_name, try_success))) {
      } else if (try_success) {
        break;
      } else {
        idx--;
      }
    }
  }
  return ret;
}


int ObDMLResolver::resolve_columns(ObRawExpr *&expr, ObArray<ObQualifiedName> &columns)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr*> real_exprs;
  ObArray<ObRawExpr*> ref_exprs;
  ObArray<ObRawExpr*> replace_ref_exprs;
  for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
    ObQualifiedName& q_name = columns.at(i);
    ObRawExpr* real_ref_expr = NULL;
    params_.is_column_ref_ = expr->is_column_ref_expr();

    if (OB_FAIL(replace_col_ref_prefix(q_name))) {
    } else if (OB_FAIL(resolve_qualified_identifier(q_name, columns, real_exprs, real_ref_expr))) {
      LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve column ref expr failed", K(ret), K(q_name));
      report_user_error_msg(ret, expr, q_name);
    } else if (OB_FAIL(real_exprs.push_back(real_ref_expr))) {
    } else if (OB_FAIL(ref_exprs.push_back(q_name.ref_expr_))
              || OB_FAIL(replace_ref_exprs.push_back(real_ref_expr))) {
    } else { /*do nothing*/ }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObRawExprUtils::replace_ref_column(expr, ref_exprs, replace_ref_exprs))) {
  }

  if (OB_SUCC(ret) && OB_NOT_NULL(expr) && expr->is_sys_func_expr()
      && OB_FAIL(check_col_param_on_expr(expr))) {
  }
  return ret;
}

bool ObDMLResolver::check_expr_has_colref(ObRawExpr *expr)
{
  bool has_colref = false;
  if (OB_ISNULL(expr)) {
  } else if (expr->is_column_ref_expr()) {
    has_colref = true;
  } else {
    for (int64_t i = 0; !has_colref && i < expr->get_param_count(); ++i) {
      has_colref = check_expr_has_colref(expr->get_param_expr(i));
    }
  }
  return has_colref;
}

int ObDMLResolver::replace_pl_relative_expr_to_question_mark(ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(real_ref_expr) || OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (real_ref_expr->is_const_raw_expr() //local variable access
             || (real_ref_expr->is_obj_access_expr() && !check_expr_has_colref(real_ref_expr)) // composite variable access
             || T_OP_GET_PACKAGE_VAR == real_ref_expr->get_expr_type() //package variable access, must not (system/user variable)
             || real_ref_expr->is_sys_func_expr()
             || T_FUN_PL_GET_CURSOR_ATTR == real_ref_expr->get_expr_type()) { // allow CURSOR%ROWID through
    if (OB_FAIL(ObResolverUtils::revert_external_param_info(params_.external_param_info_, *params_.expr_factory_, real_ref_expr))) {
    } else if (OB_FAIL(ObResolverUtils::resolve_external_param_info(params_.external_param_info_,
                                                                    *params_.session_info_,
                                                                    *params_.expr_factory_,
                                                                    params_.prepare_param_count_,
                                                                    real_ref_expr))) {
    }
  }
  return ret;
}

int ObDMLResolver::try_resolve_external_symbol(ObQualifiedName &q_name,
                                               ObIArray<ObQualifiedName> &columns,
                                               ObIArray<ObRawExpr*> &real_exprs,
                                               ObRawExpr *&real_ref_expr,
                                               bool &is_external)
{
  int ret = OB_SUCCESS;
  is_external = true;
  if (OB_FAIL(resolve_external_name(q_name, columns, real_exprs, real_ref_expr))) {
    is_external = false;
  } else if (T_FUN_PL_COLLECTION_CONSTRUCT == real_ref_expr->get_expr_type()
             || T_FUN_PL_OBJECT_CONSTRUCT == real_ref_expr->get_expr_type()) {
    is_external = false;
  }
  return ret;
}

int ObDMLResolver::try_resolve_sql_symbol(ObQualifiedName &q_name,
                                          ObIArray<ObQualifiedName> &columns,
                                          ObIArray<ObRawExpr*> &real_exprs,
                                          ObRawExpr *&real_ref_expr,
                                          bool keey_ret)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resolve_column_ref_expr(q_name, real_ref_expr))) {
    LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve column reference failed", K(ret), K(q_name), K(columns));
  }
  if (OB_ERR_BAD_FIELD_ERROR == ret) {
    if (OB_FAIL(resolve_pseudo_column(q_name, real_ref_expr))) {
      LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve pseudo column failed", K(ret), K(q_name), K(columns));
      ret = keey_ret ? ret : OB_ERR_BAD_FIELD_ERROR;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_qualified_identifier(ObQualifiedName &q_name,
                                                ObIArray<ObQualifiedName> &columns,
                                                ObIArray<ObRawExpr*> &real_exprs,
                                                ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  bool is_external = false; // identifier can not calc in sql context, should replace to question mark.
  if (OB_ISNULL(get_basic_stmt()) || OB_ISNULL(get_basic_stmt()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (q_name.is_sys_func()) { // buildin function.
    if (OB_FAIL(q_name.access_idents_.at(0).check_param_num())) {
    } else {
      real_ref_expr = static_cast<ObRawExpr *>(q_name.access_idents_.at(0).sys_func_expr_);
      is_external = (T_FUN_PL_GET_CURSOR_ATTR == real_ref_expr->get_expr_type());
    }
  } else if (q_name.is_pl_udf() || q_name.is_pl_var() || q_name.is_col_ref_access()) { // must be external symbol.
    if (OB_FAIL(try_resolve_external_symbol(q_name, columns, real_exprs, real_ref_expr, is_external))) {
    }
  } else { // may sql symbol or external symbol, try it.
    if (OB_FAIL(try_resolve_sql_symbol(q_name, columns, real_exprs, real_ref_expr, true))) {
      LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve sql symbol failed", K(ret), K(q_name), K(columns));
    }
    if (OB_ERR_BAD_FIELD_ERROR == ret && !q_name.access_idents_.empty()) {
      if (OB_FAIL(try_resolve_external_symbol(q_name, columns, real_exprs, real_ref_expr, is_external))) {
        LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve external symbol failed", K(ret), K(q_name), K(columns));
        ret = OB_ERR_BAD_FIELD_ERROR;
      }
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(real_ref_expr) && T_FUN_PL_SQLCODE_SQLERRM == real_ref_expr->get_expr_type()) {
      ret = OB_ERR_SP_UNDECLARED_VAR;
      if (0 < q_name.access_idents_.count()) {
        LOG_USER_ERROR(OB_ERR_SP_UNDECLARED_VAR,  q_name.access_idents_.at(q_name.access_idents_.count() - 1).access_name_.length(),
                    q_name.access_idents_.at(q_name.access_idents_.count() - 1).access_name_.ptr());
      }
    }
  }
  // Because obj access parameters are flattened, a(b,c) will be stored as b,c,a in columns, so after explaining one ObQualifiedName,
  // need to take the preceding ObQualifiedName and try to replace parameters

  for (int64_t i = 0; OB_SUCC(ret) && i < real_exprs.count(); ++i) {
    if (OB_FAIL(ObRawExprUtils::replace_ref_column(real_ref_expr, columns.at(i).ref_expr_, real_exprs.at(i)))) {
    }
  }

  if (OB_SUCC(ret)
      && q_name.is_access_root()
      && is_external
      && !params_.is_default_param_
      && T_INTO_SCOPE != current_scope_
      && NULL != params_.secondary_namespace_) { // Only SQL in PL requires external variables to be replaced with QUESTIONMARK, pure SQL context does not need it
    if (OB_FAIL(replace_pl_relative_expr_to_question_mark(real_ref_expr))) {
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
    OZ (columns.at(i).replace_access_ident_params(q_name.ref_expr_, real_ref_expr));
  }

  return ret;
}

void ObDMLResolver::report_user_error_msg(int &ret, const ObRawExpr *root_expr, const ObQualifiedName &q_name) const
{
  if (OB_ERR_BAD_FIELD_ERROR == ret && !q_name.tbl_name_.empty()) {
    if (stmt_ != NULL && stmt_->is_select_stmt()) {
      const ObSelectStmt *select_stmt = static_cast<const ObSelectStmt*>(stmt_);
      if (select_stmt->is_set_stmt()) {
        //can't use table name in union query
        //eg. select c1 from t1 union select c1 from t1 order by t1.c1
        ret = OB_ERR_TABLENAME_NOT_ALLOWED_HERE;
        LOG_USER_ERROR(OB_ERR_TABLENAME_NOT_ALLOWED_HERE, q_name.tbl_name_.length(), q_name.tbl_name_.ptr());
      } else if (select_stmt->get_table_size() <= 0 && current_level_ <= 0) {
        //can't use table name in select from dual
        //eg. select t1.c1
        ret = OB_ERR_UNKNOWN_TABLE;
        ObString tbl_name = concat_table_name(q_name.database_name_, q_name.tbl_name_);
        ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
        LOG_USER_ERROR(OB_ERR_UNKNOWN_TABLE, tbl_name.length(), tbl_name.ptr(), scope_name.length(), scope_name.ptr());
      }
    }
  }
  if (OB_ERR_BAD_FIELD_ERROR == ret) {
    ObString column_name = concat_qualified_name(q_name.database_name_, q_name.tbl_name_, q_name.col_name_);
    ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
    LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(), scope_name.length(), scope_name.ptr());
  } else if (OB_NON_UNIQ_ERROR == ret) {
    ObString column_name = concat_qualified_name(q_name.database_name_, q_name.tbl_name_, q_name.col_name_);
    ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
    LOG_USER_ERROR(OB_NON_UNIQ_ERROR, column_name.length(), column_name.ptr(), scope_name.length(), scope_name.ptr());
  } else if (OB_ILLEGAL_REFERENCE == ret) {
    //compatiable with mysql
    //select max(c1) as c from t1 group by c -> err msg:ERROR 1056 (42000): Can't group on 'c'
    //others: select max(c1) as c from t1 group by c+1 ->
    //err msg:ERROR 1247 (42S22): Reference 'c' not supported (reference to group function)
    ObString column_name = q_name.col_name_;
    if (root_expr == q_name.ref_expr_) {
      ret = OB_WRONG_GROUP_FIELD;
      LOG_USER_ERROR(OB_WRONG_GROUP_FIELD, column_name.length(), column_name.ptr());
    } else {
      LOG_USER_ERROR(OB_ILLEGAL_REFERENCE, column_name.length(), column_name.ptr());
    }
  }
}

//select resolver has namespace more than one layer, select resolve will overwrite it
int ObDMLResolver::resolve_column_ref_expr(const ObQualifiedName &q_name, ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resolve_table_column_expr(q_name, real_ref_expr))) {
    LOG_WARN_IGNORE_COL_NOTFOUND(ret, "resolve table column failed", K(q_name), K(ret));
  }
  return ret;
}

int ObDMLResolver::resolve_aggr_exprs(ObRawExpr *&expr,
    common::ObIArray<ObAggFunRawExpr*> &aggr_exprs, const bool need_analyze/* = true*/)
{
  UNUSED(expr);
  UNUSED(need_analyze);
  int ret = OB_SUCCESS;
  if (aggr_exprs.count() > 0 && !is_select_resolver()) {
    if (OB_UNLIKELY(T_FIELD_LIST_SCOPE != current_scope_)) {
      ret = OB_ERR_INVALID_GROUP_FUNC_USE;
    }
    // for (int64_t i = 0; OB_SUCC(ret) && i < aggr_exprs.count(); i++) {
      // ObAggFunRawExpr *final_aggr = NULL;
      // if (final_aggr != aggr_exprs.at(i)) {
        // if (OB_FAIL(ObRawExprUtils::replace_ref_column(expr, aggr_exprs.at(i), final_aggr))) {
          // LOG_WARN("repalce reference column failed", K(ret));
        // }
      // }
    // }
  }
  return ret;
}

int ObDMLResolver::resolve_win_func_exprs(ObRawExpr *&expr, common::ObIArray<ObWinFunRawExpr*> &win_exprs)
{
  UNUSED(expr);
  UNUSED(win_exprs);
  return OB_ERR_INVALID_WINDOW_FUNC_USE;
}


int ObDMLResolver::resolve_table_relation_factor_wrapper(const peg::Node *table_node,
                                                         uint64_t &database_id,
                                                         ObString &tbl_name,
                                                         ObString &db_name,
                                                         bool &is_db_explicit)
{
  int ret = OB_SUCCESS;

  if (NULL == table_node) {
    ret = OB_INVALID_ARGUMENT;
  }

  if (OB_SUCC(ret) && OB_FAIL(resolve_table_relation_factor(table_node,
                                                            database_id,
                                                            tbl_name,
                                                            db_name,
                                                            is_db_explicit))) {
  }

  return ret;
}

int ObDMLResolver::resolve_sys_vars(ObArray<ObVarInfo> &sys_vars)
{
  int ret = OB_SUCCESS;
  ObQueryCtx *query_ctx = NULL;
  if (OB_ISNULL(stmt_) || OB_ISNULL(query_ctx = stmt_->get_query_ctx())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(ObRawExprUtils::merge_variables(sys_vars, query_ctx->variables_))) {
  }
  return ret;
}

int ObDMLResolver::resolve_basic_table_without_cte(const peg::Node &parse_tree, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  const peg::Node *table_node = &peg::table_source(parse_tree);
  const peg::Node *alias_node = peg::table_has_alias(parse_tree) ? &peg::table_alias(parse_tree) : nullptr;
  const auto index_hints = peg::table_index_hints(parse_tree);
  const auto partitions = peg::table_partitions(parse_tree);
  const peg::Node *sample_node = peg::table_has_sample(parse_tree) ? &peg::table_sample(parse_tree) : nullptr;
  const peg::Node *time_node = peg::table_has_snapshot(parse_tree) ? &peg::table_snapshot(parse_tree) : nullptr;
  bool is_db_explicit = false;
  ObDMLStmt *stmt = get_stmt();
  
  ObString database_name;
  ObString table_name;
  ObString alias_name;
  uint64_t database_id = OB_INVALID_ID;
  const ObTableSchema *table_schema = NULL;

  if (OB_FAIL(resolve_table_relation_factor_wrapper(table_node,
                                                    database_id,
                                                    table_name,
                                                    database_name,
                                                    is_db_explicit))) {
    if (OB_TABLE_NOT_EXIST == ret || OB_ERR_BAD_DATABASE == ret) {
      if (is_information_schema_database_id(database_id)) {
        ret = OB_ERR_UNKNOWN_TABLE;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_TABLE, table_name.length(), table_name.ptr(), database_name.length(), database_name.ptr());
      } else {
        ret = OB_TABLE_NOT_EXIST;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(table_name));
      }
    } else {
    }
  }
  if (OB_SUCC(ret)) {
    if (alias_node != NULL) {
      const auto alias_node_text = alias_node->text();
      OZ(ob_write_string(*allocator_, syntax_text(alias_node_text), alias_name));
    }

    // flag IS to set larger query_timeout.
    if (is_information_schema_database_id(database_id)) {
      params_.query_ctx_->has_is_table_ = true;
    }
    bool cte_table_fisrt = !peg::relation_has_database(*table_node);
    uint64_t real_dep_obj_id = view_ref_id_;
    if (OB_FAIL(resolve_base_or_alias_table_item_normal(database_id,
                                                        database_name,
                                                        is_db_explicit,
                                                        table_name,
                                                        alias_name,
                                                        table_item,
                                                        cte_table_fisrt,
                                                        real_dep_obj_id))) {
    } else {
      if (OB_ISNULL(table_item) || OB_ISNULL(stmt)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(session_info_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_checker_->get_table_schema( table_item->ref_id_, table_schema))) {
        ret = OB_TABLE_NOT_EXIST;
      } else if (!stmt->is_select_stmt() && table_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      } else if (table_schema->is_vir_table() && !stmt->is_select_stmt()) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "DML operation on Virtual Table/Temporary Table");
      } else if (params_.is_from_create_view_ && table_schema->is_mysql_tmp_table()) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "View/Table's column refers to a temporary table");
      } else if (OB_FAIL(resolve_table_partition_expr(*table_item, *table_schema))) {
      } else if (OB_FAIL(resolve_generated_column_expr_temp(table_item))) {
      } else if (OB_FAIL(resolve_table_constraint_items(table_item, table_schema))) {
      } else if (stmt->is_select_stmt() && OB_FAIL(resolve_geo_mbr_column())) {
      } else if (!index_hints.empty() &&
                 OB_FAIL(resolve_index_hint(*table_item, index_hints))) {
      }

      if (OB_SUCCESS == ret && table_item->is_view_table_) {
        if (OB_FAIL(expand_view(*table_item))) {
        }
      }
      if (OB_SUCCESS == ret && !partitions.empty()) {
        if (is_virtual_table(table_item->ref_id_) &&
            table_schema->get_part_option().get_part_num() > 1) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Partitioned virtual table with partition hint");
        } else if (table_item->cte_type_ != TableItem::CTEType::NOT_CTE) {
          // ret = -14109
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Partitioned cte table with partition hint");
        } else if (table_item->is_view_table_) {
        } else if (OB_FAIL(resolve_partitions(partitions, *table_schema, *table_item))) {
        } else { }
      }
      if (OB_SUCCESS == ret && sample_node != NULL) {
        if (is_virtual_table(table_item->ref_id_)) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "sampling virtual table");
        } else if (OB_FAIL(resolve_sample_clause(sample_node, *table_item))) {
        } else { }
      }
      if (OB_SUCC(ret) && NULL == table_item->sample_info_) {
        if (OB_FAIL(generate_ddl_sample_info_if_needed(*table_item))) {
        }
      }
      //resolve snapshot query node
      if (OB_SUCCESS == ret && time_node != NULL) {
        if (OB_FAIL(resolve_snapshot_query_node(time_node, table_item))) {
        } else if (table_item->is_view_table_) {
          if (OB_FAIL(set_snapshot_info_for_view(table_item->ref_query_, table_item))) {
          } else {
            // After set_snapshot_info_for_view is applied to the snapshot attribute of view, it is no longer useful, to avoid affecting subsequent judgments
            // Here it is restored to the default value
            table_item->snapshot_query_expr_ = NULL;
            table_item->snapshot_query_type_ = TableItem::NOT_USING;
          }
        } else {
          /*do nothing*/
        }
      }
      if (OB_SUCCESS == ret && is_virtual_table(table_item->ref_id_)) {
        stmt->get_query_ctx()->is_contain_virtual_table_ = true;
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_table_check_constraint_items(const TableItem *table_item,
                                                        const ObTableSchema *table_schema)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *dml_stmt = NULL;
  ObSEArray<ObRawExpr*, 4> stmt_constr_exprs;
  ObSEArray<int64_t, 4> check_flags;
  if (OB_ISNULL(dml_stmt = get_stmt()) || OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(generate_check_constraint_exprs(table_item, table_schema, stmt_constr_exprs, &check_flags))) {
  } else if (!stmt_constr_exprs.empty()) {
    CheckConstraintItem check_constraint_item;
    check_constraint_item.table_id_ = table_item->table_id_;
    check_constraint_item.ref_table_id_ = table_schema->get_table_id();
    if (OB_FAIL(append(check_constraint_item.check_constraint_exprs_, stmt_constr_exprs))) {
    } else if (OB_FAIL(append(check_constraint_item.check_flags_, check_flags))) {
    } else if (OB_FAIL(dml_stmt->set_check_constraint_item(check_constraint_item))) {
    } else {
      LOG_TRACE("succeed to resolve table check constraint items", K(table_item->table_id_),
                        K(table_schema->get_table_id()), K(check_constraint_item));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_table_constraint_items(const TableItem *table_item,
                                                        const ObTableSchema *table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_item) || OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(resolve_table_check_constraint_items(table_item, table_schema))) {
  } else if (OB_FAIL(resolve_foreign_key_constraint(table_item))) {
  }
  return ret;
}

int ObDMLResolver::check_snapshot_expr_validity(ObRawExpr *expr, bool &has_column)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (expr->is_column_ref_expr()) {
    has_column = true;
  } else if (expr->is_exec_param_expr()) {
    if (OB_FAIL(check_snapshot_expr_validity(
                  static_cast<ObExecParamRawExpr *>(expr)->get_ref_expr(),
                  has_column))) {
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && !has_column && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(check_snapshot_expr_validity(expr->get_param_expr(i),
                                                has_column))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_snapshot_query_node(const peg::Node *time_node, TableItem *table_item)
{
  int ret = OB_SUCCESS;
  const peg::Node *tmp_time_node = time_node;
  bool has_column = false;
  if (OB_ISNULL(time_node) || OB_ISNULL(table_item) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (OB_NOT_NULL(tmp_time_node)) {
      ObRawExpr *expr = nullptr;
      ObQueryCtx *query_ctx = NULL;
      if (OB_FAIL(resolve_sql_expr(*tmp_time_node, expr))) {
      } else if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (T_REF_QUERY == expr->get_expr_type()) {
        ret = OB_ERR_INVALID_SUBQUERY_USE;
      } else if (OB_FAIL(check_snapshot_expr_validity(expr, has_column))) {
      } else if (has_column) {
        ret = OB_ERR_COLUMN_NOT_ALLOWED;
      } else {
        table_item->snapshot_query_expr_ = expr;
        table_item->snapshot_query_type_ = TableItem::USING_SCN;
      }
    }
    if (OB_SUCC(ret)) {
      // try add cast expr in static typing engine.
      ObRawExpr *&expr = table_item->snapshot_query_expr_;
      ObSysFunRawExpr *dst_expr = NULL;
      CK(NULL != expr);
      CK(NULL != params_.expr_factory_);
      OZ(expr->formalize(session_info_));
      const bool use_default_cm = true;
      ObCastMode cm = 0;
      if (TableItem::USING_SCN == table_item->snapshot_query_type_
          && ObUInt64Type != expr->get_result_type().get_type()) {
        ObRawExprResType res_type;
        res_type.set_uint64();
        res_type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY2[0][ObUInt64Type]);
        OZ(ObRawExprUtils::create_cast_expr(*params_.expr_factory_, expr, res_type, dst_expr,
                                            session_info_, use_default_cm, cm));
        if (OB_SUCC(ret)) {
          expr = dst_expr;
        }
      }
    }
  }
  return ret;
}
// For subquery or view snapshot resolution, retain existing snapshot attributes if the table already has them.
// When there are no relevant snapshot attributes, set the table to the outer view or subquery snapshot attribute, for example:
// select * from ((select * from t1 as of snapshot scn1, t2) as of snapshot scn2);
// At this point, table t1 still retains the original snapshot time1, while table t2 is set to the outer snapshot time2
int ObDMLResolver::set_snapshot_info_for_view(ObSelectStmt *select_stmt, TableItem *table_item)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObSelectStmt*, 4> child_stmts;
  bool is_stack_overflow = false;
  if (OB_ISNULL(select_stmt) ||OB_ISNULL(table_item) || OB_ISNULL(table_item->snapshot_query_expr_)
      || OB_UNLIKELY(table_item->snapshot_query_type_ == TableItem::NOT_USING)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
  } else if (OB_UNLIKELY(is_stack_overflow)) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(select_stmt->get_child_stmts(child_stmts))) {
  } else {
    //1.First set the snapshot attribute of this layer's stmt table
    for (int64_t i = 0; OB_SUCC(ret) && i < select_stmt->get_table_size(); ++i) {
      TableItem *cur_table = select_stmt->get_table_item(i);
      if (OB_ISNULL(cur_table)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (cur_table->snapshot_query_expr_ != NULL &&
                 cur_table->snapshot_query_type_ != TableItem::NOT_USING) {
        /*do nothing */
      } else if (cur_table->is_basic_table()) {
        cur_table->snapshot_query_expr_ = table_item->snapshot_query_expr_;
        cur_table->snapshot_query_type_ = table_item->snapshot_query_type_;
      } else {/*do nothing*/}
    }
    //2.Recursively set the table snapshot attribute of subqueries
    for (int64_t i = 0; OB_SUCC(ret) && i < child_stmts.count(); i++) {
      if (OB_FAIL(SMART_CALL(set_snapshot_info_for_view(child_stmts.at(i), table_item)))) {
      } else {/*do nothing*/}
    }
  }
  return ret;
}

int ObDMLResolver::resolve_table(const peg::Node &parse_tree,
                                 TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  const peg::Node *table_node = parse_tree.kind() == peg::NodeKind::Table
      ? &peg::table_source(parse_tree) : &parse_tree;
  const peg::Node *time_node = peg::table_has_snapshot(parse_tree) ? &peg::table_snapshot(parse_tree) : nullptr;
  ObDMLStmt *stmt = get_stmt();
  // Snapshot query does not support delete/update/insert statements.
  if (OB_SUCC(ret)) {
    if (!stmt->is_select_stmt() && OB_NOT_NULL(time_node)) {
      ret = OB_ERR_SNAPSHOT_QUERY_WITH_UPDATE;
    } else {
      switch (table_node->kind()) {
      case peg::NodeKind::Relation: {
        if (OB_FAIL(resolve_basic_table(parse_tree, table_item))) {
        }
        break;
      }
      case peg::NodeKind::Select: {
        bool has_snapshot_query = false;
        const bool tmp_have_same_table = params_.have_same_table_name_;
        params_.have_same_table_name_ = false;
        if (peg::table_lateral(parse_tree) &&
            OB_FAIL(resolve_lateral_generated_table(parse_tree, table_item))) {
        } else if (!peg::table_lateral(parse_tree) &&
                   OB_FAIL(resolve_generate_table(parse_tree, table_item))) {
        }
        params_.have_same_table_name_ = tmp_have_same_table;

        if (OB_FAIL(ret)) {
        } else if (!stmt->is_select_stmt() &&
                  OB_FAIL(check_stmt_has_snapshot_query(table_item->ref_query_, false, has_snapshot_query))) {
        } else if (has_snapshot_query) {
          ret = OB_ERR_SNAPSHOT_QUERY_WITH_UPDATE;
        } else if (OB_NOT_NULL(time_node)) {
          if (OB_FAIL(resolve_snapshot_query_node(time_node, table_item))) {
          } else if (OB_FAIL(set_snapshot_info_for_view(table_item->ref_query_, table_item))) {
          } else {
            // The snapshot attribute of the generated table is already useless after set_snapshot_info_for_view, to avoid affecting subsequent judgments
            // Here it is restored to the default value
            table_item->snapshot_query_expr_ = NULL;
            table_item->snapshot_query_type_ = TableItem::NOT_USING;
          }
        } else {/*do nothing*/}
        break;
      }
      case peg::NodeKind::JoinedTable: {
        JoinedTable *root = NULL;
        if (OB_FAIL(resolve_joined_table(parse_tree, root))) {
        } else if (OB_FAIL(stmt->add_joined_table(root))) {
        } else {
          table_item = root;
        }
        break;
      }
      case peg::NodeKind::TableCollection: {
        if (OB_ISNULL(session_info_)) {
          ret = OB_INVALID_ARGUMENT;
        }
        OZ (resolve_function_table_item(*table_node, table_item));
        break;
      }
      case peg::NodeKind::UnnestTable: {
        OZ(resolve_unnest_table(*table_node, table_item));
        break;
      }
      case peg::NodeKind::HybridSearchTable: {
        OZ(resolve_hybrid_search_item(parse_tree, table_item));
        break;
      }
      case peg::NodeKind::JsonTable: {
        OZ (resolve_json_table_item(*table_node, table_item));
        break;
      }
      case peg::NodeKind::Values: {
        if (OB_FAIL(resolve_values_table_item(*table_node, table_item))) {
        }
        break;
      }
      default:
        /* won't be here */
        ret = OB_ERR_PARSER_SYNTAX;
        break;
      }
      if (OB_SUCC(ret)
       && OB_NOT_NULL(params_.query_ctx_)
       && OB_FAIL(check_table_item_with_gen_col_using_udf(table_item,
                                             params_.query_ctx_->is_table_gen_col_with_udf_))) {
      }
    }
  }

  return ret;
}

int ObDMLResolver::check_table_item_with_gen_col_using_udf(const TableItem *table_item, bool &ans)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_item)) {
    // do nothing
  } else if (ans) {
    // do nothing, some nested stmt may have check it formerly, do not have to check again
  } else if(OB_ISNULL(schema_checker_) ||
      OB_ISNULL(get_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (table_item->is_generated_table()) {
    // generated table should check it when resolve itself.
    // OZ (SMART_CALL(check_table_item_with_gen_col_using_udf(table_item->view_base_item_, ans)), KPC(table_item));
  } else if (table_item->is_basic_table() || table_item->is_fake_cte_table()) {
    /**
     * LINK_TABLE and CTE_TABLE is same as BASIC_TABLE or ALIAS_TABLE
     */
    const ObTableSchema *table_schema = NULL;
    if (OB_FAIL(schema_checker_->get_table_schema(
                                                  table_item->ref_id_,
                                                  table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(table_schema->has_generated_column_using_udf_expr(ans))){
    }
  } else {
    // do nothing
  }
  return ret;
}

int ObDMLResolver::check_stmt_has_snapshot_query(ObDMLStmt *stmt, bool check_all, bool &has_fq)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  ObSEArray<ObSelectStmt*, 4> child_stmts;
  has_fq = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
  } else if (check_all && OB_FAIL(stmt->get_child_stmts(child_stmts))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && !has_fq && i < stmt->get_table_size(); ++i) {
      TableItem *cur_table = stmt->get_table_item(i);
      if (OB_ISNULL(cur_table)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_NOT_NULL(cur_table->snapshot_query_expr_)) {
        has_fq = true;
      } else if ((cur_table->is_generated_table() || cur_table->is_temp_table()) && !check_all &&
                 OB_FAIL(SMART_CALL(check_stmt_has_snapshot_query(cur_table->ref_query_,
                                                                   check_all, has_fq)))) {
      } else {/*do nothing*/}
    }
    // Need to check if the entire query contains the snapshot attribute
    if (check_all) {
      for (int64_t i = 0; OB_SUCC(ret) && !has_fq && i < child_stmts.count(); i++) {
        if (OB_FAIL(SMART_CALL(check_stmt_has_snapshot_query(child_stmts.at(i),
                                                              check_all,
                                                              has_fq)))) {
        } else {/*do nothing*/}
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_joined_table(const peg::Node &parse_node, JoinedTable *&joined_table)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resolve_joined_table_item(parse_node, joined_table))) {
  } else if (OB_FAIL(join_infos_.push_back(ResolverJoinInfo(joined_table->table_id_)))) {
  } else {
    column_namespace_checker_.add_current_joined_table(joined_table);
  }
  if (OB_FAIL(ret)) {
    // do noting;
  } else if (peg::join_condition(parse_node) == peg::JoinCondition::Natural) {
    if (OB_FAIL(resolve_natural_join_columns(*joined_table))) {
    } else if (OB_FAIL(transfer_using_to_on_expr(joined_table))) {
    }
  } else if (peg::join_condition(parse_node) != peg::JoinCondition::None) {
    if (peg::join_condition(parse_node) == peg::JoinCondition::Using) {
      ResolverJoinInfo *join_info = NULL;
      if (!get_joininfo_by_id(joined_table->table_id_, join_info)) {
      } else if (OB_FAIL(resolve_using_columns(peg::join_columns(parse_node), join_info->using_columns_))) {
      } else if (OB_FAIL(transfer_using_to_on_expr(joined_table))) {
      }
    } else {
      //transform join on condition
      ObStmtScope old_scope = current_scope_;
      current_scope_ = T_ON_SCOPE;
      if (OB_FAIL(resolve_and_split_sql_expr_with_bool_expr(peg::join_predicate(parse_node),
                                                      joined_table->join_conditions_))) {
      } else { /*do nothing*/ }
      current_scope_ = old_scope;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_using_columns(rust::Slice<const peg::Node> columns, ObIArray<ObString> &column_names)
{
  int ret = OB_SUCCESS;
  ObString column_name;
  for (const peg::Node &column : columns) {
    if (OB_FAIL(ret)) { break; }
    {
      const auto column_text = column.text();
      OZ(ob_write_string(*allocator_, syntax_text(column_text), column_name));
      // filter out duplicated column
      bool found = false;
      for (int64_t j = 0; j < column_names.count(); j++) {
        if (ObCharset::case_insensitive_equal(column_names.at(j), column_name)) {
          found = true;
          break;
        }
      }
      if (!found) {
        if (OB_FAIL(column_names.push_back(column_name))) {
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_natural_join_columns(const JoinedTable &joined_table)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 8> left_columns;
  ObSEArray<ObString, 8> right_columns;
  ResolverJoinInfo *join_info = nullptr;
  if (OB_ISNULL(joined_table.left_table_) || OB_ISNULL(joined_table.right_table_)
      || !get_joininfo_by_id(joined_table.table_id_, join_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(collect_table_column_names(*joined_table.left_table_, left_columns))) {
  } else if (OB_FAIL(collect_table_column_names(*joined_table.right_table_, right_columns))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < left_columns.count(); ++i) {
      const ObString &column = left_columns.at(i);
      bool common = false;
      for (int64_t j = 0; !common && j < right_columns.count(); ++j) {
        common = ObCharset::case_insensitive_equal(column, right_columns.at(j));
      }
      bool added = false;
      for (int64_t j = 0; common && !added && j < join_info->using_columns_.count(); ++j) {
        added = ObCharset::case_insensitive_equal(column, join_info->using_columns_.at(j));
      }
      if (common && !added) {
        OZ(join_info->using_columns_.push_back(column));
      }
    }
  }
  return ret;
}

int ObDMLResolver::collect_table_column_names(const TableItem &table_item, ObIArray<ObString> &column_names)
{
  int ret = OB_SUCCESS;
  if (table_item.is_joined_table()) {
    const auto &joined_table = static_cast<const JoinedTable &>(table_item);
    if (OB_ISNULL(joined_table.left_table_) || OB_ISNULL(joined_table.right_table_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      OZ(SMART_CALL(collect_table_column_names(*joined_table.left_table_, column_names)));
      OZ(SMART_CALL(collect_table_column_names(*joined_table.right_table_, column_names)));
    }
  } else if (table_item.is_generated_table() || table_item.is_temp_table()) {
    const ObSelectStmt *query = table_item.ref_query_;
    if (OB_ISNULL(query)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < query->get_select_item_size(); ++i) {
        OZ(column_names.push_back(query->get_select_item(i).alias_name_));
      }
    }
  } else if (table_item.is_function_table()) {
    OZ(ObResolverUtils::get_all_function_table_column_names(table_item, params_, column_names));
  } else if (table_item.is_basic_table() || table_item.is_fake_cte_table()) {
    const ObTableSchema *schema = nullptr;
    if (OB_ISNULL(schema_checker_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(schema_checker_->get_table_schema(table_item.ref_id_, schema))) {
    } else if (OB_ISNULL(schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < schema->get_column_count(); ++i) {
        const ObColumnSchemaV2 *column = schema->get_column_schema_by_idx(i);
        if (OB_ISNULL(column)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (!column->is_hidden()) {
          OZ(column_names.push_back(column->get_column_name_str()));
        }
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "current table item");
  }
  return ret;
}

// major logic is refered to
// - resolve_and_split_sql_expr();
// - resolve_sql_expr();
//  As the left_table might be nested, but the right_table not,
//  therefore, the left_table should only have unique column_names
//  to avoid ambiguous column resolving.
//
int ObDMLResolver::transfer_using_to_on_expr(JoinedTable *&joined_table)
{
  int ret = OB_SUCCESS;
  ObArray<ObQualifiedName> columns;
  JoinedTable *cur_table = joined_table;
  if (OB_ISNULL(params_.expr_factory_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("params is invalid", K_(params_.expr_factory));
  }
  // construct AND expr and columns in exprs
  ResolverJoinInfo *join_info = NULL;
  bool found = get_joininfo_by_id(joined_table->table_id_, join_info);
  for (int64_t i = 0; OB_SUCC(ret) && found && i < join_info->using_columns_.count(); ++i) {
    // construct t_left_N.ck = t_right.ck
    ObOpRawExpr *b_expr = NULL;
    ObRawExpr *left_expr = NULL;
    ObRawExpr *right_expr = NULL;

    // make two sub exprs: t_left_N.ck and t_right.ck
    const TableItem *left_table = NULL;
    const TableItem *right_table = NULL;
    const ObString &column_name = join_info->using_columns_.at(i);
    if (OB_FAIL(column_namespace_checker_.check_using_column_namespace(column_name, left_table, right_table))) {
      LOG_WARN("check using column namespace failed", K(column_name));
      if (OB_ERR_BAD_FIELD_ERROR == ret) {
        ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
        LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(), scope_name.length(), scope_name.ptr());
      } else if (OB_NON_UNIQ_ERROR == ret) {
        ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
        LOG_USER_ERROR(OB_NON_UNIQ_ERROR, column_name.length(), column_name.ptr(), scope_name.length(), scope_name.ptr());
      }
    } else if (left_table->is_joined_table()) {
      if (OB_FAIL(resolve_join_table_column_item(static_cast<const JoinedTable&>(*left_table),
                                                 column_name, left_expr))) {
      }
    } else {
      ColumnItem *column = NULL;
      if (OB_FAIL(resolve_single_table_column_item(*left_table, column_name, false, column))) {
      } else {
        left_expr = column->expr_;
      }
    }
    if (OB_SUCC(ret)) {
      if (right_table->is_joined_table()) {
        if (OB_FAIL(resolve_join_table_column_item(static_cast<const JoinedTable&>(*right_table),
                                                   column_name, right_expr))) {
        }
      } else {
        ColumnItem *column = NULL;
        if (OB_FAIL(resolve_single_table_column_item(*right_table, column_name, false, column))) {
        } else {
          right_expr = column->expr_;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_OP_EQ, b_expr))) {
      } else if (OB_ISNULL(b_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(b_expr->set_param_exprs(left_expr, right_expr))) {
      } else if (OB_FAIL(b_expr->formalize(session_info_))) {
      } else if (OB_FAIL(cur_table->join_conditions_.push_back(b_expr))) {
      }
    }
    if (OB_SUCC(ret) && FULL_OUTER_JOIN == cur_table->joined_type_) {
      ObSysFunRawExpr *coalesce_expr = NULL;
      if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_FUN_SYS_COALESCE, coalesce_expr))) {
      } else if (OB_FAIL(coalesce_expr->set_param_exprs(left_expr, right_expr))) {
      } else if (OB_FAIL(coalesce_expr->formalize(session_info_))) {
      } else if (OB_NOT_NULL(join_info) && OB_FAIL(join_info->coalesce_expr_.push_back(coalesce_expr))) {
      }
    }
  } // end of for

  return ret;
}

//transfer (t1,t2) join (t3,t4) to (t1 join t2) join (t3 join t4)

int ObDMLResolver::resolve_table_column_expr(const ObQualifiedName &q_name, ObRawExpr *&real_ref_expr)
{
  //search order
  //1. joined table column
  //2. basic table column or generated table column
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_) || OB_ISNULL(get_stmt())) {
    ret = OB_NOT_INIT;
  } else {
    const TableItem *table_item = NULL;
    column_namespace_checker_.set_dml_stmt(get_stmt());
    if (OB_FAIL(column_namespace_checker_.check_table_column_namespace(q_name, table_item))) {
      LOG_WARN_IGNORE_COL_NOTFOUND(ret, "column not found in table", K(ret), K(q_name));
    } else if (table_item->is_joined_table()) {
      const JoinedTable &joined_table = static_cast<const JoinedTable&>(*table_item);
      if (OB_FAIL(resolve_join_table_column_item(joined_table, q_name.col_name_, real_ref_expr))) {
      }
    } else {
      ColumnItem *col_item = NULL;
      if (OB_FAIL(resolve_single_table_column_item(*table_item, q_name.col_name_, false, col_item))) {
      } else if (OB_ISNULL(col_item)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        real_ref_expr = col_item->expr_;
      }
    }
  }


  return ret;
}

int ObDMLResolver::resolve_single_table_column_item(const TableItem &table_item,
                                                    const ObString &column_name,
                                                    bool include_hidden,
                                                    ColumnItem *&col_item)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt) || OB_ISNULL(schema_checker_) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (table_item.is_basic_table() || table_item.is_fake_cte_table()) {
    if (OB_FAIL(resolve_basic_column_item(table_item, column_name, include_hidden, col_item))) {
    } else { /*do nothing*/ }
  } else if (table_item.is_generated_table() || table_item.is_temp_table() || table_item.is_lateral_table()) {
    if (OB_FAIL(resolve_generated_table_column_item(table_item, column_name, col_item))) {
    }
  } else if (table_item.is_function_table()) {
    if (OB_FAIL(resolve_function_table_column_item(table_item, column_name, col_item))) {
    }
  } else if (table_item.is_json_table()) {
    if (OB_FAIL(resolve_json_table_column_item(table_item, column_name, col_item))) {
    }
  } else if (table_item.is_values_table()) {
    if (OB_ISNULL(col_item = stmt->get_column_item(table_item.table_id_, column_name))) {
      ret = OB_ERR_BAD_FIELD_ERROR;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_join_table_column_item(const JoinedTable &joined_table,
                                                  const ObString &column_name,
                                                  ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *coalesce_expr = NULL;
  if (OB_UNLIKELY(joined_table.joined_type_ != FULL_OUTER_JOIN)) {
    //only when column name hit full join table using name, we would search column expr in joined table
    ret = OB_ERR_UNEXPECTED;
  } else {
    ResolverJoinInfo *join_info = NULL;
    if (get_joininfo_by_id(joined_table.table_id_, join_info) && join_info->coalesce_expr_.count() > 0) {
      for (int i = 0; i < join_info->coalesce_expr_.count(); i++) {
        if (ObCharset::case_insensitive_equal(join_info->using_columns_.at(i), column_name)) {
          coalesce_expr = join_info->coalesce_expr_.at(i);
          break;
        }
      }
    }
    if (NULL == coalesce_expr) {
      ret = OB_ERR_BAD_FIELD_ERROR;
    } else {
      real_ref_expr = coalesce_expr;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_joined_table_item(const peg::Node &parse_node, JoinedTable *&joined_table)
{
  int ret = OB_SUCCESS;
  const peg::Node *table_node = NULL;
  JoinedTable *cur_table = NULL;
  JoinedTable *child_table = NULL;
  TableItem *table_item = NULL;
  bool reverse_parse = false;
  if (OB_ISNULL(stmt_) || OB_UNLIKELY(parse_node.kind() != peg::NodeKind::JoinedTable)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(alloc_joined_table_item(cur_table))) {
  } else if (peg::join_kind(parse_node) == peg::JoinKind::Right &&
             OB_FAIL(check_contain_lateral_node(&parse_node,
                                                reverse_parse))) {
  } else {
    cur_table->table_id_ = generate_table_id();
    cur_table->type_ = TableItem::JOINED_TABLE;
  }
  /* resolve table */
  for (uint64_t index = 1; OB_SUCC(ret) && index <= 2; index++) {
    uint64_t i = reverse_parse ? 3 - index : index;
    table_node = i == 1 ? &peg::join_left(parse_node) : &peg::join_right(parse_node);
    // nested join case or normal join case
    if (peg::NodeKind::JoinedTable == table_node->kind()) {
      if (OB_FAIL(SMART_CALL(resolve_joined_table(*table_node, child_table)))) {
      } else if (1 == i) {
        cur_table->left_table_ = child_table;
      } else {
        cur_table->right_table_ = child_table;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < child_table->single_table_ids_.count(); ++i) {
        uint64_t child_table_id = child_table->single_table_ids_.at(i);
        if (OB_FAIL(cur_table->single_table_ids_.push_back(child_table_id))) {
        }
      }
    } else {
      /*
       * For recursive CTE, if the right branch of union all is a join,
       * CTE cannot appear on the left side of a right join, cannot appear on the right side of a left join, cannot use full join,
       * can appear on both sides of an inner join
       * */
      if ((!reverse_parse && 1 == i) ||
           (reverse_parse && 2 == i)) {
        column_namespace_checker_.add_current_joined_table(NULL);
      }
      if (OB_FAIL(resolve_table(*table_node, table_item))) {
      } else if (OB_FAIL(check_special_join_table(*table_item, 1 == i, peg::join_kind(parse_node)))) {
      } else if (1 == i) {
        cur_table->left_table_ = table_item;
        if (!reverse_parse) {
          column_namespace_checker_.add_current_joined_table(table_item);
        }
      } else {
        cur_table->right_table_ = table_item;
        if (reverse_parse) {
          column_namespace_checker_.add_current_joined_table(table_item);
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(cur_table->single_table_ids_.push_back(table_item->table_id_))) {
        }
      }
    }
  }

  /* resolve join type */
  if (OB_SUCC(ret)) {
    switch (peg::join_kind(parse_node)) {
    case peg::JoinKind::Full:
      cur_table->joined_type_ = FULL_OUTER_JOIN;
      break;
    case peg::JoinKind::Left:
      cur_table->joined_type_ = LEFT_OUTER_JOIN;
      break;
    case peg::JoinKind::Right:
      cur_table->joined_type_ = RIGHT_OUTER_JOIN;
      break;
    case peg::JoinKind::Inner:
      cur_table->joined_type_ = INNER_JOIN;
      break;
    case peg::JoinKind::Straight:
      cur_table->joined_type_ = INNER_JOIN;
      cur_table->is_straight_join_ = true;
      break;
    default:
      /* won't be here */
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }
  if (OB_SUCC(ret)) {
    joined_table = cur_table;
  }

  return ret;
}

int ObDMLResolver::check_special_join_table(const TableItem &join_table, bool is_left_child, peg::JoinKind join_type)
{
  UNUSED(is_left_child);
  UNUSED(join_type);
  int ret = OB_SUCCESS;
  if (join_table.is_fake_cte_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "CTE in dml");
  }
  return ret;
}

int ObDMLResolver::resolve_generate_table(const peg::Node &table_node,
                                          TableItem *&table_item)
{
  return resolve_generated_query(peg::table_source(table_node), table_node, table_item);
}

int ObDMLResolver::resolve_generated_query(const peg::Node &query,
                                           const peg::Node &table_node,
                                           TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  bool is_with_cte = false;
  ObDMLStmt *stmt = get_stmt();
  ObSelectResolver select_resolver(params_);
  // from subquery and current query belong to the same level, therefore current level and current remain consistent
  select_resolver.set_current_level(current_level_);
  select_resolver.set_current_view_level(current_view_level_);
  select_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
  if (upper_insert_resolver_ != nullptr &&
      upper_insert_resolver_->is_row_alias_source(query)) {
    select_resolver.set_upper_insert_resolver(upper_insert_resolver_);
  }
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (stmt->is_select_stmt() ||
             (stmt->is_delete_stmt() || stmt->is_update_stmt())) {
    //resolve with cte table
    select_resolver.set_is_sub_stmt(true);
    if (OB_FAIL(select_resolver.set_cte_ctx(cte_ctx_, true, true))) {
    } else if (OB_FAIL(add_cte_table_to_children(select_resolver))) {
    }
  } else {}

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(select_resolver.add_parent_gen_col_exprs(gen_col_exprs_))) {
  } else if (OB_FAIL(do_resolve_generate_table(query, table_node, select_resolver, table_item))) {
  }
  return ret;
}

int ObDMLResolver::resolve_lateral_generated_table(const peg::Node &table_node,
                                                   TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObExecParamRawExpr*, 4> exec_params;
  ObDMLStmt *stmt = get_stmt();
  ObSelectResolver select_resolver(params_);
  select_resolver.set_current_level(current_level_ + 1);
  select_resolver.set_current_view_level(current_view_level_);
  select_resolver.set_parent_namespace_resolver(this);
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (stmt->is_select_stmt() ||
             (stmt->is_delete_stmt() || stmt->is_update_stmt())) {
    //resolve with cte table
    select_resolver.set_is_sub_stmt(true);
    if (OB_FAIL(select_resolver.set_cte_ctx(cte_ctx_, true, true))) {
    } else if (OB_FAIL(add_cte_table_to_children(select_resolver))) {
    }
  } else { }

  if (OB_FAIL(ret)) {
  } else if (OB_FALSE_IT(set_query_ref_exec_params(&exec_params))) {
  } else if (OB_FALSE_IT(params_.is_resolve_lateral_derived_table_ = true)) {
  } else if (OB_FAIL(select_resolver.add_parent_gen_col_exprs(gen_col_exprs_))) {
  } else if (OB_FAIL(do_resolve_generate_table(peg::table_source(table_node), table_node, select_resolver, table_item))) {
  } else if (OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FALSE_IT(params_.is_resolve_lateral_derived_table_ = false)) {
  } else if (OB_FAIL(table_item->exec_params_.assign(exec_params))) {
  } else {
    set_query_ref_exec_params(NULL);
    table_item->type_ = TableItem::LATERAL_TABLE;
  }
  return ret;
}

int ObDMLResolver::check_contain_lateral_node(const peg::Node *table, bool &is_contain)
{
  int ret = OB_SUCCESS;
  is_contain = peg::table_lateral(*table);
  if (!is_contain && table->kind() == peg::NodeKind::JoinedTable) {
    OZ(SMART_CALL(check_contain_lateral_node(&peg::join_left(*table), is_contain)));
    if (OB_SUCC(ret) && !is_contain) {
      OZ(SMART_CALL(check_contain_lateral_node(&peg::join_right(*table), is_contain)));
    }
  }
  return ret;
}

int ObDMLResolver::do_resolve_generate_table(const peg::Node &query, const peg::Node &table_node,
                                             ObChildStmtResolver &child_resolver,
                                             TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *ref_stmt = NULL;
  ObString alias_name;
  const auto column_aliases = peg::table_columns(table_node);
  /*
   * Generated tables in sel/upd/del statements may contain duplicate columns as
   * long as the outer layer does not reference them. Column checking detects
   * whether referenced columns are duplicated, so duplicate-column detection
   * can be skipped here while still adding the relevant plan cache constraints.
   */
  bool can_skip = false;
  if (OB_FAIL(child_resolver.resolve_child_stmt(query))) {
  } else if (OB_ISNULL(ref_stmt = child_resolver.get_child_stmt()) || OB_ISNULL(get_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (peg::table_has_alias(table_node)) {
    const auto &alias = peg::table_alias(table_node);
    const auto alias_text = alias.text();
    OZ(ob_write_string(*allocator_, syntax_text(alias_text), alias_name));
  }

  bool enable_var_assign_use_das = false;
  if (OB_SUCC(ret)) {
    if (OB_NOT_NULL(session_info_)) {
      enable_var_assign_use_das = session_info_->is_var_assign_use_das_enabled();
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!column_aliases.empty() &&
             OB_FAIL(refine_generate_table_column_name(column_aliases, *ref_stmt))) {
  } else if (OB_FAIL(ObResolverUtils::check_duplicated_column(*ref_stmt, can_skip))) {
  } else if (OB_FAIL(resolve_generate_table_item(ref_stmt, alias_name, table_item))) {
  } else if (enable_var_assign_use_das && OB_FAIL(extract_var_init_exprs(ref_stmt, params_.query_ctx_->var_init_exprs_))) {
    // Extract the var assign expr in generated table, This is to be compatible with some of mysql's uses of variables
    // Such as "select c1,(@rownum:= @rownum+1) as CCBH from t1,(SELECT@rownum:=0) B"
  } else {
  }
  return ret;
}

// Extract the var assign expr in generated table, This is to be compatible with some of mysql's uses of variables
// Such as "select c1,(@rownum:= @rownum+1) as CCBH from t1,(SELECT@rownum:=0) B"
int ObDMLResolver::extract_var_init_exprs(ObSelectStmt *ref_query, ObIArray<ObRawExpr*> &assign_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ref_query)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ref_query->get_from_item_size() <= 0) {
    for (int i = 0; OB_SUCC(ret) && i < ref_query->get_select_item_size(); ++i) {
      const SelectItem &select_item = ref_query->get_select_item(i);
      if (OB_FAIL(ObRawExprUtils::extract_var_assign_exprs(select_item.expr_, assign_exprs))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_generate_table_item(ObSelectStmt *ref_query,
                                               const ObString &alias_name,
                                               TableItem *&tbl_item)
{
  int ret = OB_SUCCESS;
  TableItem *item = NULL;
  ObDMLStmt *dml_stmt = get_stmt();
  ObString used_alias_name = alias_name;
  if (OB_ISNULL(dml_stmt) || OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(NULL == (item = dml_stmt->create_table_item(*allocator_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create table item failed");
  } else if (used_alias_name.empty()
             && OB_FAIL(dml_stmt->generate_anonymous_view_name(*allocator_, used_alias_name))) {
  } else {
    item->ref_query_ = ref_query;
    item->table_id_ = generate_table_id();
    item->table_name_ = used_alias_name;
    item->alias_name_ = used_alias_name;
    item->type_ = TableItem::GENERATED_TABLE;
    item->is_view_table_ = false;
    if (OB_FAIL(dml_stmt->add_table_item(session_info_, item, params_.have_same_table_name_))) {
    } else {
      tbl_item = item;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_hybrid_search_item(const peg::Node &table_node, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  const auto &source = peg::table_source(table_node);
  const ParamStore empty_parameters;
  const ParamStore &parameters = params_.param_list_ == nullptr ? empty_parameters : *params_.param_list_;
  const auto argument_text = [&](const peg::Node &node, ObString &text) {
    int ret = OB_SUCCESS;
    ObRawExpr *expression = nullptr;
    ObObj value;
    if (node.kind() == peg::NodeKind::UserVariable) {
      const auto name = peg::variable_name(node);
      ObString variable_name;
      ObSessionVariable variable;
      OZ(ob_write_string(*allocator_, syntax_text(name), variable_name));
      if (OB_SUCC(ret)) {
        ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, variable_name);
        OZ(session_info_->get_user_variable(variable_name, variable));
        OX(value = variable.value_);
      }
    } else if (node.kind() == peg::NodeKind::Int || node.kind() == peg::NodeKind::Uint
               || node.kind() == peg::NodeKind::Number || node.kind() == peg::NodeKind::Double) {
      const auto literal = peg::expression_literal(node);
      text.assign_ptr(syntax_text(literal.text).ptr(), static_cast<int64_t>(literal.text.size()));
      return ret;
    } else {
      OZ(ObResolverUtils::resolve_const_expr(params_, node, expression, nullptr));
      OZ(ObSQLUtils::calc_const_expr(session_info_, *expression, value, *allocator_, parameters,
                                   session_info_->get_cur_exec_ctx()));
    }
    if (OB_SUCC(ret) && !value.is_null() && !value.is_string_type()) {
      ret = OB_INVALID_ARGUMENT;
    }
    OZ(value.get_string(text));
    return ret;
  };
  ObString table_name;
  ObString search_parameters;
  OZ(argument_text(peg::hybrid_search_table(source), table_name));
  OZ(argument_text(peg::hybrid_search_parameters(source), search_parameters));
  share::ObHybridSearchExecutor executor;
  share::ObHybridSearchArg argument;
  argument.table_name_ = table_name;
  argument.search_params_ = search_parameters;
  argument.search_type_ = share::ObHybridSearchArg::SearchType::GET_SQL;
  ObString query;
  OZ(executor.init(session_info_->get_cur_exec_ctx(), argument));
  OZ(executor.execute_get_sql(query));
  ParsedSql parsed;
  ObParser parser(*allocator_, session_info_->get_sql_mode());
  OZ(parser.parse(query, parsed));
  if (OB_SUCC(ret)) {
    auto &global_hint = const_cast<ObGlobalHint &>(get_stmt()->get_query_ctx()->get_query_hint_for_update().get_global_hint());
    bool enabled = false;
    global_hint.opt_params_.has_enable_opt_param(ObOptParamHint::OptParamType::HIDDEN_COLUMN_VISIBLE, enabled);
    if (!enabled) {
      ObObj value;
      value.set_varchar("true");
      OZ(global_hint.opt_params_.add_opt_param_hint(ObOptParamHint::OptParamType::HIDDEN_COLUMN_VISIBLE, value));
    }
  }
  if (OB_SUCC(ret)) {
    const ParsedSql *previous = params_.syntax_;
    params_.syntax_ = &parsed;
    const int result = resolve_generated_query(*parsed.statement(), table_node, table_item);
    params_.syntax_ = previous;
    ret = result;
  }
  if (OB_SUCC(ret)) {
    params_.query_ctx_->has_hybrid_search_ = true;
  }
  return ret;
}

int ObDMLResolver::resolve_str_const(const peg::Node &node, ObString &path)
{
  int ret = OB_SUCCESS;
  ObObjParam value;
  ObCollationType collation = CS_TYPE_INVALID;
  const auto literal = peg::expression_literal(node);
  const ObString text(syntax_text(literal.text));
  const ObString charset(syntax_text(literal.charset));
  if (OB_FAIL(session_info_->get_collation_connection(collation))) {
  } else if (OB_FAIL(ObResolverUtils::resolve_string_literal(text, ObCharType, collation,
      charset.empty() ? nullptr : &charset, charset.case_compare("binary") == 0,
      *allocator_, value))) {
  } else {
    OZ(ob_write_string(*allocator_, value.get_string(), path));
  }
  return ret;
}


int ObDMLResolver::resolve_unnest_table(const peg::Node &table_node, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  const auto arguments = peg::unnest_arguments(table_node);
  const auto columns = peg::table_columns(table_node);
  ObString table_name("unnest");
  TableItem *item = nullptr;
  if (!columns.empty() && columns.size() != arguments.size()) {
    ret = OB_ERR_PARAM_SIZE;
  } else if (peg::table_has_alias(table_node)) {
    const auto name = peg::table_alias(table_node).text();
    OZ(ob_write_string(*allocator_, syntax_text(name), table_name));
  }
  for (size_t i = 0; OB_SUCC(ret) && i < arguments.size(); ++i) {
    ObRawExpr *expression = nullptr;
    ObString column_name("unnest");
    OZ(resolve_sql_expr(arguments[i], expression));
    CK(OB_NOT_NULL(expression));
    OZ(expression->deduce_type(session_info_));
    if (OB_SUCC(ret) && !columns.empty()) {
      const auto name = columns[i].text();
      OZ(ob_write_string(*allocator_, syntax_text(name), column_name));
    }
    if (OB_SUCC(ret) && OB_ISNULL(item)) {
      OZ(create_unnest_table(item, table_name));
    }
    OZ(item->json_table_def_->doc_exprs_.push_back(expression));
    OZ(add_unnest_column(*item, column_name));
  }
  OX(table_item = item);
  return ret;
}

int ObDMLResolver::create_unnest_table(TableItem *&table_item, ObString table_name)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  ObJsonTableDef *definition = nullptr;
  ObDmlJtColDef *root = nullptr;
  CK(OB_NOT_NULL(stmt));
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(table_item = stmt->create_table_item(*allocator_))
             || OB_ISNULL(definition = static_cast<ObJsonTableDef *>(allocator_->alloc(sizeof(ObJsonTableDef))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    definition = new (definition) ObJsonTableDef();
    definition->table_type_ = MulModeTableType::OB_UNNEST_TABLE_TYPE;
    table_item->table_name_ = table_name;
    table_item->alias_name_ = table_name;
    table_item->table_id_ = generate_table_id();
    table_item->type_ = TableItem::JSON_TABLE;
    table_item->json_table_def_ = definition;
    OZ(stmt->add_table_item(session_info_, table_item));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(root = static_cast<ObDmlJtColDef *>(allocator_->alloc(sizeof(ObDmlJtColDef))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    root = new (root) ObDmlJtColDef();
    root->table_id_ = table_item->table_id_;
    root->col_base_info_.col_type_ = NESTED_COL_TYPE;
    root->col_base_info_.parent_id_ = -1;
    root->col_base_info_.id_ = 0;
    OZ(json_table_infos_.push_back(root));
    OZ(definition->all_cols_.push_back(&root->col_base_info_));
  }
  return ret;
}

int ObDMLResolver::add_unnest_column(TableItem &table_item, ObString column_name)
{
  int ret = OB_SUCCESS;
  ObDmlJtColDef *column = nullptr;
  ObDataType data_type;
  const int64_t column_id = table_item.json_table_def_->all_cols_.count();
  ObRawExpr *expression = table_item.json_table_def_->doc_exprs_.at(column_id - 1);
  if (OB_ISNULL(column = static_cast<ObDmlJtColDef *>(allocator_->alloc(sizeof(ObDmlJtColDef))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    column = new (column) ObDmlJtColDef();
    column->col_base_info_.col_name_ = column_name;
    column->col_base_info_.col_type_ = COL_TYPE_UNNEST;
    column->col_base_info_.parent_id_ = 0;
    column->col_base_info_.id_ = column_id;
    column->col_base_info_.output_column_idx_ = column_id;
  }
  if (OB_SUCC(ret)) {
    ObSubSchemaValue value;
    if (ObCollectionSQLType != expression->get_data_type()) {
      ret = OB_ERR_INVALID_TYPE_FOR_ARGUMENT;
    } else if (OB_FAIL(session_info_->get_cur_exec_ctx()->get_sqludt_meta_by_subschema_id(expression->get_subschema_id(), value))) {
    } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const auto *collection = static_cast<const ObSqlCollectionInfo *>(value.value_);
      const auto *array = static_cast<const ObCollectionArrayType *>(collection->collection_meta_);
      if (array->element_type_->type_id_ == ObNestedType::OB_BASIC_TYPE) {
        data_type = static_cast<const ObCollectionBasicType *>(array->element_type_)->basic_meta_;
      } else if (array->element_type_->type_id_ == ObNestedType::OB_ARRAY_TYPE
                 || array->element_type_->type_id_ == ObNestedType::OB_VECTOR_TYPE) {
        ObString child_definition;
        uint16_t child_id = 0;
        OZ(collection->get_child_def_string(child_definition));
        OZ(session_info_->get_cur_exec_ctx()->get_subschema_id_by_type_string(child_definition, child_id));
        OX(data_type.set_collation_level(CS_LEVEL_IMPLICIT));
        OX(data_type.set_obj_type(ObCollectionSQLType));
        OX(data_type.set_subschema_id(child_id));
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  if (OB_SUCC(ret)) {
    ColumnItem *item = nullptr;
    column->col_base_info_.data_type_ = data_type;
    OZ(generate_json_table_output_column_item(&table_item, data_type, column_name, column_id, item));
    OX(item->col_idx_ = column_id);
    OZ(table_item.json_table_def_->all_cols_.push_back(&column->col_base_info_));
  }
  return ret;
}

int ObDMLResolver::resolve_json_table_item(const peg::Node &parse_tree, TableItem *&tbl_item)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  ObRawExpr *json_doc_expr = NULL;
  TableItem *item = NULL;
  ObJsonTableDef* table_def = NULL;
  int64_t cur_column_id = OB_APP_MIN_COLUMN_ID;
  int32_t id = 0;
  const peg::Node &document = peg::json_document(parse_tree);
  const peg::Node &path_node = peg::json_path(parse_tree);
  // json document node
  if (OB_FAIL(ret)) {
  } else if ((OB_ISNULL(stmt) || OB_ISNULL(allocator_))) {
    ret = OB_NOT_INIT;
  } else {
    OZ (resolve_sql_expr(document, json_doc_expr));
    CK (OB_NOT_NULL(json_doc_expr));
    OZ (json_doc_expr->deduce_type(session_info_));
  }

  if (OB_SUCC(ret)) {
    char* table_buf = static_cast<char*>(allocator_->alloc(sizeof(ObJsonTableDef)));
    if (OB_ISNULL(table_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      table_def = static_cast<ObJsonTableDef*>(new (table_buf) ObJsonTableDef());
      if (OB_FAIL(table_def->doc_exprs_.push_back(json_doc_expr))) {
      } else {
        table_def->table_type_ = MulModeTableType::OB_ORA_JSON_TABLE_TYPE;
      }
    }
  }

  // json table alias node
  ObDmlJtColDef* root_col_def = NULL;
  if (OB_SUCC(ret)) {
    ObString alias_name;
    if (!peg::table_has_alias(parse_tree)) {
      ret = OB_ERR_TABLE_WITHOUT_ALIAS;
    } else if (OB_ISNULL(item = stmt->create_table_item(*allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      const auto &alias = peg::table_alias(parse_tree);
      const auto alias_text = alias.text();
      OZ(ob_write_string(*allocator_, syntax_text(alias_text), alias_name));
    }
    OX (item->table_name_ = alias_name);
    OX (item->alias_name_ = alias_name);
    OX (item->table_id_ = generate_table_id());
    OX (item->type_ = TableItem::JSON_TABLE);
    OX (item->json_table_def_ = table_def);
    OX (tbl_item = item);

    OZ (stmt->add_table_item(session_info_, item));

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(root_col_def = static_cast<ObDmlJtColDef*>(allocator_->alloc(sizeof(ObDmlJtColDef))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      root_col_def = new (root_col_def) ObDmlJtColDef();
      root_col_def->table_id_ = item->table_id_;
      root_col_def->col_base_info_.col_type_ = NESTED_COL_TYPE;
    }
  }

  // path node process
  if (OB_SUCC(ret)) {
    ObString path_str;
    OZ(resolve_str_const(path_node, path_str));
    if (OB_SUCC(ret) && path_str.empty()) {
      ret = OB_ERR_JSON_PATH_EXPRESSION_SYNTAX_ERROR;
      LOG_USER_ERROR(OB_ERR_JSON_PATH_EXPRESSION_SYNTAX_ERROR, 0, "");
    }
    ObIAllocator& alloc_ref = *allocator_;
    if (OB_SUCC(ret) && OB_FAIL(ob_write_string(alloc_ref, path_str, root_col_def->col_base_info_.path_))) {
    }
  }

  if (OB_SUCC(ret)) {
    root_col_def->col_base_info_.on_error_ = JSN_VALUE_IMPLICIT;
    root_col_def->col_base_info_.on_empty_ = JSN_VALUE_IMPLICIT;
  }

  // column node process
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(json_table_infos_.push_back(root_col_def))) {
  } else if (OB_FAIL(resolve_json_table_column_item(peg::json_table_columns(parse_tree), item,
                                                    root_col_def, -1, id, cur_column_id))) {
  }
  return ret;
}

int ObDMLResolver::resolve_function_table_item(const peg::Node &parse_tree,
                                               TableItem *&tbl_item)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();

  ObRawExpr *function_table_expr = NULL;
  TableItem *item = NULL;
  ObString alias_name;
  if (OB_SUCC(ret) && (OB_ISNULL(stmt) || OB_ISNULL(allocator_))) {
    ret = OB_NOT_INIT;
  }
  if (OB_SUCC(ret)) {
    OX (params_.is_resolve_table_function_expr_ = true);
    OZ (resolve_sql_expr(peg::collection_table_expression(parse_tree), function_table_expr));
    OX (params_.is_resolve_table_function_expr_ = false);
    CK (OB_NOT_NULL(function_table_expr));
  }
  CK (OB_NOT_NULL(function_table_expr));
  OZ (function_table_expr->deduce_type(session_info_));
  if (OB_SUCC(ret)) {
    if (function_table_expr->get_result_type().is_ext()) {
      // PL collection used in TABLE(), extract PL info from schema
      CK(OB_NOT_NULL(schema_checker_));
      if (OB_SUCC(ret)) {
        pl::ObPLPackageGuard *package_guard = NULL;
        const ObUserDefinedType *user_type = NULL;
        CK (OB_NOT_NULL(params_.schema_checker_));
        CK (OB_NOT_NULL(session_info_));
        CK (OB_NOT_NULL(session_info_->get_cur_exec_ctx()));
        OZ (session_info_->get_cur_exec_ctx()->get_package_guard(package_guard));
        CK (OB_NOT_NULL(package_guard));
        OZ (ObResolverUtils::get_user_type(
          params_.allocator_, params_.session_info_, params_.plan_cache_, params_.sql_proxy_,
          params_.schema_checker_->get_schema_guard(),
          *package_guard,
          function_table_expr->get_udt_id(),
          user_type));
        if (OB_FAIL(ret)) {
        } else if (OB_UNLIKELY(NULL == user_type)) {
          ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
          LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, 14, "TABLE FUNCTION");
        } else if (!user_type->is_collection_type()) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "access rows from a non-nested table item");
        } else if (OB_FAIL(add_udt_dependency(*user_type))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(item = stmt->create_table_item(*allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (peg::table_has_alias(parse_tree)) {
      const auto &alias = peg::table_alias(parse_tree);
      const auto alias_text = alias.text();
      OZ(ob_write_string(*allocator_, syntax_text(alias_text), alias_name));
    } else {
      if (OB_FAIL(stmt->generate_func_table_name(*allocator_, alias_name))) {
      }
    }
    OX (item->table_name_ = alias_name);
    OX (item->alias_name_ = alias_name);
    OX (item->table_id_ = generate_table_id());
    OX (item->type_ = TableItem::FUNCTION_TABLE);
    OX (item->function_table_expr_ = function_table_expr);

    if (OB_SUCC(ret) && function_table_expr->is_udf_expr()) {
      ObUDFRawExpr *udf = static_cast<ObUDFRawExpr*>(function_table_expr);
      ObSchemaObjVersion table_version;
      share::schema::ObSchemaGetterGuard *schema_guard = NULL;
      uint64_t database_id = OB_INVALID_ID;
      const ObString &database_name = udf->get_database_name().empty() ? session_info_->get_database_name() : udf->get_database_name();
      CK (OB_NOT_NULL(udf));
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(schema_guard = params_.schema_checker_->get_schema_guard())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_guard->get_database_id(database_name, database_id))) {
      } else if (udf->need_add_dependency()) {
        uint64_t dep_obj_id = view_ref_id_;
        uint64_t dep_db_id = database_id;
        ObArray<ObSchemaObjVersion> vers;
        OZ (udf->get_schema_object_version(*schema_guard, vers));
        for (int64_t i = 0; OB_SUCC(ret) && i < vers.count(); ++i) {
          table_version = vers.at(i);
          OZ (stmt->add_global_dependency_table(table_version));
          OZ (stmt->add_ref_obj_version(dep_obj_id, dep_db_id, ObObjectType::VIEW, table_version, *allocator_));
        }
      }
      OX (stmt_->get_query_ctx()->has_pl_udf_ = true);
      OX (stmt_->get_query_ctx()->disable_udf_parallel_ |= !udf->is_parallel_enable());
    } else if (OB_SUCC(ret) && function_table_expr->is_sys_func_expr()) {
      // xxx
    }
  }
  OZ (stmt->add_table_item(session_info_, item));
  if (OB_SUCC(ret)) {
    //
    // ObFunctionTable fills row data when it depends on the columns before row being the output columns of udf, here we force to add udf's output columns to ObFunctionTable
    ObSEArray<ColumnItem, 16> col_items;
    CK (OB_NOT_NULL(item));
    OZ (resolve_function_table_column_item(*item, col_items));
  }
  OX (tbl_item = item);
  return ret;
}

int ObDMLResolver::resolve_base_or_alias_table_item_normal(const uint64_t database_id,
                                                           const ObString &db_name,
                                                           const bool &is_db_explicit,
                                                           const ObString &tbl_name,
                                                           const ObString &alias_name,
                                                           TableItem *&tbl_item,
                                                           bool cte_table_fisrt,
                                                           uint64_t dep_obj_id)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  TableItem *item = NULL;
  const ObTableSchema *tschema = NULL;
  if (OB_ISNULL(stmt) || OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(NULL == (item = stmt->create_table_item(*allocator_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create table item failed");
  } else if (alias_name.length() > 0) {
    item->type_ = TableItem::ALIAS_TABLE;
  } else {
    item->type_ = TableItem::BASE_TABLE;
  }
  if (OB_SUCC(ret)) {
    item->database_name_ = db_name;
    bool select_index_enabled = false;
    const bool is_hidden = session_info_->is_table_name_hidden();
    if (OB_FAIL(session_info_->is_select_index_enabled(select_index_enabled))) {
    } else if (OB_FAIL(schema_checker_->get_table_schema(database_id,
                                                         tbl_name,
                                                         false /*data table first*/,
                                                         cte_table_fisrt,
                                                         is_hidden,
                                                         tschema))) {
      if (OB_TABLE_NOT_EXIST == ret &&
          ((stmt->is_select_stmt() && select_index_enabled) ||
           session_info_->get_ddl_info().is_ddl() ||
           session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility())) {
        if (OB_FAIL(schema_checker_->get_table_schema(
                                                      database_id,
                                                      tbl_name,
                                                      true /* for index table */,
                                                      cte_table_fisrt,
                                                      is_hidden,
                                                      tschema,
                                                      false/*is_built_in_index*/))) {
          if (OB_TABLE_NOT_EXIST == ret ||
               session_info_->get_ddl_info().is_ddl() ||
               session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility()) {
            if (OB_FAIL(schema_checker_->get_table_schema(
                                                          database_id,
                                                          tbl_name,
                                                          true /* for index table */,
                                                          cte_table_fisrt,
                                                          is_hidden,
                                                          tschema,
                                                          true/*is_built_in_index*/))) {
            }
          } else {
          }
        }
      } else {
      }
    }

    if (OB_SUCC(ret)) {
      if (session_info_->get_ddl_info().is_ddl()) {
        if (!tschema->is_storage_local_index_table()) {
          item->ref_id_ = tschema->get_table_id();
          item->table_id_ = generate_table_id();
          item->is_system_table_ = tschema->is_sys_table();
          item->is_view_table_ = tschema->is_view_table();
          item->table_name_ = tschema->get_table_name_str();
          item->alias_name_ = alias_name;
          item->ddl_schema_version_ = tschema->get_schema_version();
          item->ddl_table_id_ = tschema->get_table_id();
          item->table_type_ = tschema->get_table_type();
        } else {
          const ObTableSchema *tab_schema = nullptr;
          if (OB_FAIL(schema_checker_->get_table_schema( tschema->get_data_table_id(), tab_schema))) {
          } else {
            item->ref_id_ = tab_schema->get_table_id();
            item->table_id_ = generate_table_id();
            item->is_system_table_ = tab_schema->is_sys_table();
            item->is_view_table_ = tab_schema->is_view_table();
            item->table_name_ = tab_schema->get_table_name_str();
            item->alias_name_ = alias_name;
            item->ddl_schema_version_ = tschema->get_schema_version();
            item->ddl_table_id_ = tschema->get_table_id();
            item->table_type_ = tschema->get_table_type();
          }
        }
      } else if (tschema->is_index_table()) {
        //feature: select * from index_name where... rewrite to select index_col1, index_col2... from data_table_name where...
        //the feature only use in mysqtest case, not open for user
        const ObTableSchema *tab_schema = NULL;
        item->is_index_table_ = tschema->is_index_table();
        item->table_id_ = generate_table_id();
        item->type_ = TableItem::ALIAS_TABLE;
        // main table schema
        if (OB_FAIL(schema_checker_->get_table_schema( tschema->get_data_table_id(), tab_schema))) {
        } else {
          item->ref_id_ = tschema->get_table_id();
          item->table_name_ = tab_schema->get_table_name_str(); // the name of the main table
          // Use the index name as the alias name of the main table
          item->alias_name_ = alias_name.empty() ? tschema->get_table_name_str() : alias_name;
          // If it is an index table lookup, the dependencies of the main table also need to be added to the plan
          ObSchemaObjVersion table_version;
          table_version.object_id_ = tab_schema->get_table_id();
          table_version.object_type_ = DEPENDENCY_TABLE;
          table_version.version_ = tab_schema->get_schema_version();
          table_version.is_db_explicit_ = params_.is_in_view_ ? true : is_db_explicit;
          uint64_t dep_db_id = tab_schema->get_database_id();
          if (common::is_cte_table(table_version.object_id_)) {
            // do nothing
          } else if (OB_FAIL(stmt->add_global_dependency_table(table_version))) {
          } else if (OB_FAIL(stmt->add_ref_obj_version(dep_obj_id, dep_db_id, ObObjectType::VIEW, table_version, *allocator_))) {
          }
        }
      } else {
        item->ref_id_ = tschema->get_table_id();
        item->table_id_ = generate_table_id();
        item->table_name_ = tbl_name;
        item->alias_name_ = alias_name;
        item->is_system_table_ = tschema->is_sys_table();
        item->is_view_table_ = tschema->is_view_table();
        item->ddl_schema_version_ = tschema->get_schema_version();
        item->table_type_ = tschema->get_table_type();
      }
    }
    if (OB_SUCC(ret)) {
      ObSchemaObjVersion table_version;
      table_version.object_id_ = tschema->get_table_id();
      table_version.object_type_ = tschema->is_view_table() ? DEPENDENCY_VIEW : DEPENDENCY_TABLE;
      table_version.version_ = tschema->get_schema_version();
      table_version.is_db_explicit_ = params_.is_in_view_ ? true : is_db_explicit;
      uint64_t dep_db_id = tschema->get_database_id();
      if (common::is_cte_table(table_version.object_id_)) {
         // do nothing
      } else if (OB_FAIL(stmt->add_global_dependency_table(table_version))) {
      } else if (OB_FAIL(stmt->add_ref_obj_version(dep_obj_id, dep_db_id, ObObjectType::VIEW, table_version, *allocator_))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(stmt->add_table_item(session_info_, item, params_.have_same_table_name_))) {
      } else {
        tbl_item = item;
      }
    }
  }
  return ret;
}

int ObDMLResolver::expand_view(TableItem &view_item)
{
  int ret = OB_SUCCESS;
  int64_t org_session_id = 0;
  share::schema::ObSchemaGetterGuard *schema_guard = NULL;
  uint64_t database_id = OB_INVALID_ID;
  ObString old_database_name;
  uint64_t old_database_id = OB_INVALID_ID;
  if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(session_info_)
      || OB_ISNULL(schema_guard = params_.schema_checker_->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_guard->get_database_id(view_item.database_name_,
                                                   database_id))) {
  } else if (OB_FAIL(ob_write_string(*allocator_, session_info_->get_database_name(), old_database_name))) {
  } else {
    old_database_id = session_info_->get_database_id();
  }
  if (OB_SUCC(ret)) {
    //bug19839990, MySQL view parsing needs to ignore temporary tables, currently does not support views containing temporary tables,
    // Here update sess id to prevent parsing tables in view definition as temporary tables
    org_session_id = params_.schema_checker_->get_schema_guard()->get_session_id();
    params_.schema_checker_->get_schema_guard()->set_session_id(0);
  }

  if (OB_SUCC(ret)) {
    params_.is_expanding_view_ = true;
    ObViewTableResolver view_resolver(params_, get_view_db_name(), get_view_name());
    view_resolver.set_current_level(current_level_);
    view_resolver.set_current_view_level(current_view_level_ + 1);
    view_resolver.set_view_ref_id(view_item.ref_id_);
    view_resolver.set_current_view_item(view_item);
    view_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
    if (OB_FAIL(do_expand_view(view_item, view_resolver))) {
    }
    params_.is_expanding_view_ = false;
    params_.schema_checker_->get_schema_guard()->set_session_id(org_session_id);
  }
  return ret;
}

int ObDMLResolver::do_expand_view(TableItem &view_item, ObChildStmtResolver &view_resolver)
{
  int ret = OB_SUCCESS;
  ObViewStateGuard view_state_guard(static_cast<ObSelectResolver &>(view_resolver).params_);
  ObDMLStmt *stmt = get_stmt();

  if (OB_ISNULL(stmt)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // expand view as subquery which use view name as alias
    ObSelectStmt *view_stmt = NULL;
    const ObTableSchema *view_schema = NULL;
    ObReferenceObjTable *ref_obj_tbl = NULL;

    if (OB_FAIL(schema_checker_->get_table_schema( view_item.ref_id_, view_schema))) {
    } else {
      // parse and resolve view defination
      ParsedSql view_result;
      ObString view_def;

      ObParser parser(*params_.allocator_, session_info_->get_sql_mode());
      if (OB_FAIL(ObSQLUtils::generate_view_definition_for_resolve(
                              *params_.allocator_,
                              session_info_->get_local_collation_connection(),
                              view_schema->get_view_schema(),
                              view_def))) {
      } else if (OB_FAIL(parser.parse(view_def, view_result))) {
      } else if (OB_ISNULL(ref_obj_tbl = stmt->get_ref_obj_table())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ref_obj_tbl->set_obj_schema_version(view_item.ref_id_,
                         view_schema->get_database_id(), ObObjectType::VIEW,
                         view_schema->get_max_dependency_version(),
                         view_schema->get_schema_version(), *allocator_))) {
      } else {
        // use alias to make all columns number continued
        // view is always in from, while the from subquery cannot see all attributes of parents, so parent cannot be passed to from substmt
        // select_resolver.set_upper_scope_stmt(stmt_);
        const ParsedSql *previous_syntax = params_.syntax_;
        params_.syntax_ = &view_result;
        const int resolve_result = view_resolver.resolve_child_stmt(*view_result.statement());
        params_.syntax_ = previous_syntax;
        if (OB_FAIL(resolve_result)) {
          if (OB_TABLE_NOT_EXIST == ret || OB_ERR_BAD_FIELD_ERROR == ret || OB_NON_UNIQ_ERROR == ret) {
            ret = OB_ERR_VIEW_INVALID;
            const ObString &db_name = view_item.database_name_;
            const ObString &table_name = view_item.table_name_;
            LOG_USER_ERROR(OB_ERR_VIEW_INVALID, db_name.length(), db_name.ptr(), table_name.length(), table_name.ptr());
          } else {
          }
        } else {
          view_stmt = view_resolver.get_child_stmt();
          view_stmt->set_is_view_stmt(true, view_item.ref_id_);
          view_stmt->set_check_option(view_schema->get_view_schema().get_view_check_option());
        }
      }
      if (OB_SUCC(ret)) {
        for (int64_t i = 0; i < view_stmt->get_select_item_size(); ++i) {
          SelectItem &item = view_stmt->get_select_item(i);
          item.is_real_alias_ = true;
        }
      }
      // push-down view_stmt as sub-query for view_item
      if (OB_SUCC(ret)) {
        view_item.type_ = TableItem::GENERATED_TABLE;
        view_item.ref_query_ = view_stmt;
        view_item.is_view_table_ = true;
      }

      int tmp_ret = OB_SUCCESS;
      bool reset_column_infos = false;
      if (OB_UNLIKELY(OB_SUCCESS != ret && OB_ERR_VIEW_INVALID != ret)) {
      } else if (OB_UNLIKELY(OB_ERR_VIEW_INVALID == ret)) {
        // do nothing
      } else if (OB_ISNULL(params_.dependency_info_queue_)) {
        tmp_ret = OB_ERR_UNEXPECTED;
      } else if (OB_SUCCESS != (tmp_ret = ObSQLUtils::async_recompile_view(
          *view_schema, view_stmt, reset_column_infos, *allocator_, *session_info_,
          *params_.dependency_info_queue_))) {
        LOG_WARN("failed to add recompile view task", K(tmp_ret));
        if (OB_ERR_TOO_LONG_COLUMN_LENGTH == tmp_ret) {
          tmp_ret = OB_SUCCESS; //ignore
        }
      }
      if (OB_SUCCESS == ret) {
        ret = tmp_ret;
      }
    }
  }

  return ret;
}

int ObDMLResolver::resolve_table_partition_expr(const TableItem &table_item, const ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  const ObString &part_str = table_schema.get_part_option().get_part_func_expr_str();
  ObPartitionFuncType part_type = table_schema.get_part_option().get_part_func_type();
  ObRawExpr *part_expr = NULL;
  ObRawExpr *subpart_expr = NULL;
  ObDMLStmt *dml_stmt = get_stmt();
  if (OB_ISNULL(dml_stmt)) {
    ret = OB_NOT_INIT;
  } else if (table_schema.get_part_level() != PARTITION_LEVEL_ZERO) {
    if (OB_FAIL(resolve_partition_expr(table_item, table_schema, part_type, part_str, part_expr))) {
    } else if (PARTITION_LEVEL_TWO == table_schema.get_part_level()) {
      const ObString &subpart_str = table_schema.get_sub_part_option().get_part_func_expr_str();
      ObPartitionFuncType subpart_type = table_schema.get_sub_part_option().get_part_func_type();
      if (OB_FAIL(resolve_partition_expr(table_item, table_schema, subpart_type, subpart_str, subpart_expr))) {
      }
    }
    if (OB_SUCC(ret)) {
      ObRawExpr *tmp_part_expr = part_expr;
      ObRawExpr *tmp_subpart_expr = subpart_expr;
      if ((session_info_->get_ddl_info().is_ddl()) && ObItemType::T_INSERT == params_.resolver_scope_stmt_type_) {
        // Only insert into select stmt needs to replace the virtual column(aka, part key) with its' definition.
        const ObTableSchema *index_schema = NULL;
        const ObPartitionKeyInfo &partition_keys = table_schema.get_partition_key_info();
        const ObPartitionKeyInfo &subpartition_keys = table_schema.get_subpartition_key_info();
        bool index_table_has_part_key = true;
        bool index_table_has_subpart_key = true;
        if (OB_FAIL(schema_checker_->get_table_schema( table_item.ddl_table_id_, index_schema))) {
        } else if (OB_ISNULL(index_schema)) {
          ret = OB_TABLE_NOT_EXIST;
          LOG_WARN("index table not exists", K(table_item.ddl_table_id_));
        } else if (nullptr != part_expr) {
          if (OB_FAIL(check_index_table_has_partition_keys(index_schema, partition_keys, index_table_has_part_key))) {
          } else if (index_table_has_part_key) {
            // part key is in index table, no need to do replace
          } else if (OB_FAIL(ObRawExprUtils::get_real_expr_without_generated_column(part_expr, tmp_part_expr))) {
          }
        }
        if (OB_SUCC(ret) && nullptr != subpart_expr) {
          if (OB_FAIL(check_index_table_has_partition_keys(index_schema,
                                                           subpartition_keys,
                                                           index_table_has_subpart_key))) {
          } else if (index_table_has_subpart_key) {
            // subpart key is in index table, no need to do replace
          } else if (OB_FAIL(ObRawExprUtils::get_real_expr_without_generated_column(subpart_expr, tmp_subpart_expr))) {
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(dml_stmt->set_part_expr(table_item.table_id_, table_item.ref_id_, tmp_part_expr, tmp_subpart_expr))) {
      } else {
      }
    }
  }
  //resolve global index table partition expr
  if (OB_SUCC(ret)) {
    ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
    if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const ObTableSchema *index_schema = NULL;
      if (OB_FAIL(schema_checker_->get_table_schema( simple_index_infos.at(i).table_id_, index_schema))) {
      } else if (OB_ISNULL(index_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("index table not exists", K(simple_index_infos.at(i).table_id_));
      } else if (index_schema->is_final_invalid_index() || !index_schema->is_global_index_table()) {
        //do nothing
      } else if (index_schema->get_part_level() != PARTITION_LEVEL_ZERO) {
        ObPartitionFuncType index_part_type = index_schema->get_part_option().get_part_func_type();
        const ObString &index_part_str = index_schema->get_part_option().get_part_func_expr_str();
        ObRawExpr *index_part_expr = NULL;
        ObRawExpr *index_subpart_expr = NULL;
        if (OB_FAIL(resolve_partition_expr(table_item, table_schema, index_part_type, index_part_str, index_part_expr))) {
        } else if (OB_FAIL(PARTITION_LEVEL_TWO == index_schema->get_part_level())) {
          ObPartitionFuncType index_subpart_type = index_schema->get_sub_part_option().get_part_func_type();
          const ObString &index_subpart_str = index_schema->get_sub_part_option().get_part_func_expr_str();
          if (OB_FAIL(resolve_partition_expr(table_item,
                                             table_schema,
                                             index_subpart_type,
                                             index_subpart_str,
                                             index_subpart_expr))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(dml_stmt->set_part_expr(table_item.table_id_,
                                              index_schema->get_table_id(),
                                              index_part_expr,
                                              index_subpart_expr))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_foreign_key_constraint(const TableItem *table_item)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!table_item->is_basic_table()) {
    //nothing to do, only resolve foreign key constraint for basic table
  } else if (OB_FAIL(schema_checker_->get_table_schema( table_item->ref_id_, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else if (OB_FAIL(resolve_fk_table_partition_expr(*table_item, *table_schema))) {
  }
  return ret;
}

int ObDMLResolver::resolve_fk_table_partition_expr(const TableItem &table_item, const ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *dml_stmt = get_stmt();
  if (OB_ISNULL(dml_stmt)) {
    ret = OB_NOT_INIT;
  } else if (table_schema.get_foreign_key_infos().count() > 0) {
    const common::ObIArray<ObForeignKeyInfo> & foreign_key_infos = table_schema.get_foreign_key_infos();
    for (int64_t i = 0; i < foreign_key_infos.count(); i++) {
      const ObForeignKeyInfo &foreign_key_info = foreign_key_infos.at(i);
      const uint64_t parent_table_id = foreign_key_info.parent_table_id_;
      const uint64_t child_table_id = foreign_key_info.child_table_id_;
      if (child_table_id == table_schema.get_table_id() && !foreign_key_info.is_parent_table_mock_ && foreign_key_info.is_ref_unique_index()) {
        const ObTableSchema *parent_table_schema = nullptr;
        bool parent_key_is_pkey = false;
        const common::ObSEArray<uint64_t, 4> &parent_column_ids = foreign_key_info.parent_column_ids_;
        const ObTableSchema *resolve_table_schema = nullptr;
        uint64_t fk_scan_tid = OB_INVALID_ID;
        if (OB_FAIL(schema_checker_->get_table_schema( parent_table_id, parent_table_schema))) { //NOTE: Can we use this function to get schema here
          if (OB_TABLE_NOT_EXIST == ret) {
            // Note: Parent table not exist, return OB_ERR_NO_REFERENCED_ROW instead of table not exist to ensure compatibility
            ret = OB_ERR_NO_REFERENCED_ROW;
          }
        } else if (OB_ISNULL(parent_table_schema)) {
          // Note: Parent table not exist, return OB_ERR_NO_REFERENCED_ROW instead of table not exist to ensure compatibility
          ret = OB_ERR_NO_REFERENCED_ROW;
          LOG_WARN("parent table not exists", K(parent_table_id));
        } else if (OB_FAIL(parent_table_schema->get_fk_check_index_tid(*schema_checker_->get_schema_guard(), parent_column_ids, fk_scan_tid))) {
        } else if (OB_INVALID_ID == fk_scan_tid) {
          ret = OB_ERR_CANNOT_ADD_FOREIGN;
        } else if (OB_FAIL(schema_checker_->get_table_schema( fk_scan_tid, resolve_table_schema))) {
        } else if (OB_ISNULL(resolve_table_schema)) {
          ret = OB_TABLE_NOT_EXIST;
        } else {
          ObRawExpr *parent_part_expr = nullptr;
          ObRawExpr *parent_subpart_expr = nullptr;
          const ObString &part_str = resolve_table_schema->get_part_option().get_part_func_expr_str();
          ObPartitionFuncType part_type = resolve_table_schema->get_part_option().get_part_func_type();
          // NOTE: for parent index table, can we still use table_item of child table here
          if (resolve_table_schema->get_part_level() != PARTITION_LEVEL_ZERO) {
            if (OB_FAIL(resolve_partition_expr(table_item, *resolve_table_schema, part_type, part_str, parent_part_expr, true, &foreign_key_info))) {
            } else if (PARTITION_LEVEL_TWO == resolve_table_schema->get_part_level()) {
              const ObString &parent_subpart_str = resolve_table_schema->get_sub_part_option().get_part_func_expr_str();
              ObPartitionFuncType parent_subpart_type = resolve_table_schema->get_sub_part_option().get_part_func_type();
              if (OB_FAIL(resolve_partition_expr(table_item, *parent_table_schema, parent_subpart_type, parent_subpart_str, parent_subpart_expr, true, &foreign_key_info))) {
              }
            }
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(dml_stmt->set_part_expr(foreign_key_info.foreign_key_id_, fk_scan_tid,
                                parent_part_expr, parent_subpart_expr))) {
              /*
                create table t17(a int, b int, c int, d int, primary key (a)) partition by hash(a) partitions 3;
                create table tf17(a int, b int, c int, d int, primary key (a), foreign key (a) references t17 (a)) partition by hash(a) partitions 3;
                update tf17 partition(p0) as C, tf17 as P set C.d = C.d + 100 where C.a = P.a;
                In following cases, the partition key of t17 will be resolved twice, but duplicate part expr setup can be ignored
              */
              if (ret == OB_ERR_TABLE_EXIST) {
                ret = OB_SUCCESS;
                LOG_INFO("Duplicate foreign key", K(lbt()));
              } else {
              }
            } else {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::map_to_fk_column_name(const ObTableSchema &child_table_schema,
                                         const ObTableSchema &parent_table_schema,
                                         const ObForeignKeyInfo &fk_info,
                                         const ObString &pk_col_name,
                                         ObString &fk_col_name)
{
  int ret = OB_SUCCESS;
  bool is_column_exist = false;
  if (OB_ISNULL(parent_table_schema.get_column_schema(pk_col_name))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const uint64_t pk_col_id = parent_table_schema.get_column_schema(pk_col_name)->get_column_id();
    uint64_t fk_col_id = OB_INVALID_ID;
    if OB_FAIL(fk_info.get_child_column_id(pk_col_id, fk_col_id)) {
    } else {
      child_table_schema.get_column_name_by_column_id(fk_col_id, fk_col_name, is_column_exist);
      if (!is_column_exist) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_columns_for_fk_partition_expr(ObRawExpr *&expr,
                                            ObIArray<ObQualifiedName> &columns,
                                            const TableItem &table_item, // table_item of dml table(child_table)
                                            const ObTableSchema &parent_table_schema,
                                            const ObForeignKeyInfo *fk_info)
{
  int ret = OB_SUCCESS;
  const uint64_t child_table_id = fk_info->child_table_id_;
  const ObTableSchema *child_table_schema = nullptr;
  if (OB_ISNULL(fk_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_checker_->get_table_schema(child_table_id, child_table_schema))) {
  } else {
    ObArray<ObRawExpr*> real_exprs;
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); i++) {
      ObQualifiedName &q_name = columns.at(i);
      ObRawExpr *real_ref_expr = NULL;
      ObString child_col_name;
      if (q_name.is_sys_func()) {
        if (OB_FAIL(resolve_qualified_identifier(q_name, columns, real_exprs, real_ref_expr))) {
        } else if (OB_ISNULL(real_ref_expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (!real_ref_expr->is_sys_func_expr()) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ObSysFunRawExpr *sys_func_expr = static_cast<ObSysFunRawExpr*>(real_ref_expr);
          if (OB_FAIL(sys_func_expr->check_param_num())) {
          }
        }
      } else if (OB_FAIL(map_to_fk_column_name(*child_table_schema, parent_table_schema, *fk_info, q_name.col_name_, child_col_name))) {
      } else {
        ColumnItem *column_item = NULL;
        if (OB_ISNULL(child_col_name)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(resolve_basic_column_item(table_item, child_col_name, false, column_item))) {
        } else {
          real_ref_expr = column_item->expr_;
        }
      }

      if (OB_SUCC(ret)) {
        for (int64_t i = 0; OB_SUCC(ret) && i < real_exprs.count(); ++i) {
          if (OB_FAIL(ObRawExprUtils::replace_ref_column(real_ref_expr, columns.at(i).ref_expr_, real_exprs.at(i)))) {
          }
        }
        if (OB_FAIL(ret)) {
          // nothing to do
        } else if (OB_FAIL(real_exprs.push_back(real_ref_expr))) {
        } else if OB_FAIL((ObRawExprUtils::replace_ref_column(expr, q_name.ref_expr_, real_ref_expr))) {
        }
      }
    }
  }
  return ret;
}


//for recursively process columns item in resolve_partition_expr
//just wrap columns process logic in resolve_partition_expr
int ObDMLResolver::resolve_columns_for_partition_expr(ObRawExpr *&expr,
                                                      ObIArray<ObQualifiedName> &columns,
                                                      const TableItem &table_item,
                                                      bool include_hidden)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr*> real_exprs;
  for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); i++) {
    ObQualifiedName &q_name = columns.at(i);
    ObRawExpr *real_ref_expr = NULL;
    if (q_name.is_sys_func()) {
      if (OB_FAIL(resolve_qualified_identifier(q_name, columns, real_exprs, real_ref_expr))) {
      } else if (OB_ISNULL(real_ref_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (!real_ref_expr->is_sys_func_expr()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObSysFunRawExpr *sys_func_expr = static_cast<ObSysFunRawExpr*>(real_ref_expr);
         if (OB_FAIL(sys_func_expr->check_param_num())) {
        }
      }
    } else {
      ColumnItem *column_item = NULL;
      if (OB_FAIL(resolve_basic_column_item(table_item, q_name.col_name_, include_hidden, column_item))) {
      } else {
        real_ref_expr = column_item->expr_;
      }
    }

    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < real_exprs.count(); ++i) {
        OZ (ObRawExprUtils::replace_ref_column(real_ref_expr, columns.at(i).ref_expr_, real_exprs.at(i)));
      }
      OZ (real_exprs.push_back(real_ref_expr));
      OZ (ObRawExprUtils::replace_ref_column(expr, q_name.ref_expr_, real_ref_expr));
    }
  }
  return ret;
}
int ObDMLResolver::resolve_partition_expr(
    const TableItem &table_item,
    const ObTableSchema &table_schema,
    const ObPartitionFuncType part_type,
    const ObString &part_str,
    ObRawExpr *&expr,
    bool for_fk,
    const ObForeignKeyInfo *fk_info)
{
  int ret = OB_SUCCESS;
  ObArray<ObQualifiedName> columns;

  //for special case key()
  if (OB_ISNULL(params_.session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null pointer", K(ret));
  } else if (OB_UNLIKELY(part_type >= PARTITION_FUNC_TYPE_MAX)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (PARTITION_FUNC_TYPE_KEY_IMPLICIT == part_type) {
    if (OB_FAIL(ObResolverUtils::build_partition_key_expr(params_,
                                                          table_schema,
                                                          expr,
                                                          &columns))) {
    }
  } else if (OB_UNLIKELY(part_str.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSqlString expression;
    ParsedSql part_expression;
    ObParser parser(*allocator_, params_.session_info_->get_sql_mode());
    if (PARTITION_FUNC_TYPE_KEY == part_type) {
      ret = expression.append_fmt("%s(%.*s)", N_PART_KEY, part_str.length(), part_str.ptr());
    } else {
      ret = expression.append_fmt("(%.*s)", part_str.length(), part_str.ptr());
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(parser.parse_expression(expression.string(), part_expression))) {
        LOG_WARN("failed to parse partition expression", K(ret), K(expression));
      } else {
        const ParsedSql *previous_syntax = params_.syntax_;
        params_.syntax_ = &part_expression;
        ret = resolve_partition_expr(*part_expression.statement(), expr, columns);
        params_.syntax_ = previous_syntax;
        if (OB_SUCC(ret) && OB_ISNULL(expr)) { ret = OB_ERR_UNEXPECTED; }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (for_fk) {
      if (OB_ISNULL(fk_info)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolve_columns_for_fk_partition_expr(expr, columns, table_item, table_schema, fk_info))) {
      }
    } else if (OB_FAIL(resolve_columns_for_partition_expr(expr, columns, table_item,
            table_schema.is_fts_index() || table_schema.is_vec_index()))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(expr->formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::resolve_partition_expr(const peg::Node &part_expr_node, ObRawExpr *&expr, ObIArray<ObQualifiedName> &columns)
{
  int ret = OB_SUCCESS;
  ObArray<ObSubQueryInfo> sub_query_info;
  ObArray<ObVarInfo> sys_vars;
  ObArray<ObAggFunRawExpr*> aggr_exprs;
  ObArray<ObWinFunRawExpr*> win_exprs;
  ObArray<ObUDFInfo> udf_info;
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
    ctx.stmt_ = static_cast<ObStmt*>(get_stmt());
    ctx.session_info_ = params_.session_info_;
    ctx.query_ctx_ = params_.query_ctx_;
    ObRawExprResolverImpl expr_resolver(ctx);
    if (OB_FAIL(params_.session_info_->get_name_case_mode(ctx.case_mode_))) {
    } else if (OB_FAIL(expr_resolver.resolve(part_expr_node, expr, columns, sys_vars,
                                             sub_query_info, aggr_exprs, win_exprs, udf_info,
                                             op_exprs, user_var_exprs, inlist_infos, match_exprs))) {
    } else if (sub_query_info.count() > 0 || sys_vars.count() > 0 || aggr_exprs.count() > 0 ||
               columns.count() <= 0 || udf_info.count() > 0 || op_exprs.count() > 0 ||
               inlist_infos.count() > 0) {
      ret = OB_ERR_UNEXPECTED; //TODO Molly not allow type cast in part expr?
      LOG_WARN("part expr is invalid", K(sub_query_info.count()), K(sys_vars.count()),
                K(aggr_exprs.count()), K(columns.count()), K(udf_info.count()));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_is_expr(ObRawExpr *&expr, bool &replace_happened)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *is_expr = dynamic_cast<ObOpRawExpr *>(expr);
  ObDMLStmt *stmt = get_stmt();
  int64_t num_expr = expr->get_param_count();
  ColumnItem *column_item = NULL;
  replace_happened = false;
  if (2 != num_expr) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("is_expr must have two sub_expr", K(num_expr));
  } else if (NULL == is_expr->get_param_expr(0) || NULL == is_expr->get_param_expr(1)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_REF_COLUMN == is_expr->get_param_expr(0)->get_expr_type()) {
    ObColumnRefRawExpr *ref_expr = static_cast<ObColumnRefRawExpr *>(is_expr->get_param_expr(0));
    TableItem *table = NULL;
    if (OB_ISNULL(table = stmt->get_table_item_by_id(ref_expr->get_table_id()))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!table->is_basic_table()) {
      // do nothing
    } else if (OB_ISNULL(column_item = stmt->get_column_item_by_id(ref_expr->get_table_id(), ref_expr->get_column_id()))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (T_NULL == is_expr->get_param_expr(1)->get_expr_type()) {
      if ((column_item->get_column_type()->is_date() || column_item->get_column_type()->is_datetime()
           || column_item->get_column_type()->is_mysql_date()
           || column_item->get_column_type()->is_mysql_datetime())
          && column_item->is_not_null_for_read()) {
        // If c1 is a date or datetime column and is declared not null,
        // resolve c1 is null as c1 is null or c1 = zero_date
        // keep the is null expression for such condition:
        //  select * from t1 left join t2 on t1.pk=t2.pk where t2.c1 is null;
        if (OB_FAIL(resolve_not_null_date_column_is_null(expr, column_item->get_column_type()))) {
        } else {
          replace_happened = true;
        }
      } else if (column_item->is_auto_increment()) {
        if (OB_FAIL(resolve_autoincrement_column_is_null(expr))) {
        } else {
          stmt->set_affected_last_insert_id(true);
          replace_happened = true;
        }
      }
    }
  }
  return ret;
}

/* resolve default function */
int ObDMLResolver::resolve_special_expr_static(
    const ObTableSchema *table_schema,
    const ObSQLSessionInfo &session_info,
    ObRawExprFactory &expr_factory,
    ObRawExpr *&expr,
    bool& has_default,
    const ObResolverUtils::PureFunctionCheckStatus check_status)
{
  int ret = OB_SUCCESS;
  bool is_found = false;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret) && (expr->has_flag(CNT_DEFAULT))) {
    has_default = true;
    if (expr->has_flag(IS_DEFAULT)) {
      ObDefaultValueUtils utils(NULL, NULL, NULL);
      if (OB_FAIL(utils.resolve_default_function_static(table_schema,
                                                        session_info,
                                                        expr_factory,
                                                        expr,
                                                        check_status))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); i++) {
      if (OB_FAIL(SMART_CALL(resolve_special_expr_static(table_schema,
                                                         session_info,
                                                         expr_factory,
                                                         expr->get_param_expr(i),
                                                         has_default,
                                                         check_status)))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_special_expr(ObRawExpr *&expr, ObStmtScope scope)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(expr) || OB_ISNULL(stmt) || OB_ISNULL(stmt->get_query_ctx())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (expr->has_flag(CNT_LAST_INSERT_ID) || expr->has_flag(IS_LAST_INSERT_ID)) {
    stmt->set_affected_last_insert_id(true);
  } else {
    // pass `affected_last_insert_id` from child stmt
    ObArray<ObSelectStmt*> child_stmts;
    if (OB_FAIL(stmt->get_child_stmts(child_stmts))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < child_stmts.count(); ++i) {
        if (OB_ISNULL(child_stmts.at(i))) {
          ret = OB_INVALID_ARGUMENT;
        } else if (child_stmts.at(i)->get_affected_last_insert_id()) {
          stmt->set_affected_last_insert_id(true);
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret) && (expr->has_flag(CNT_DEFAULT)
      || (expr->has_flag(CNT_IS_EXPR) && T_WHERE_SCOPE == scope)
      || gen_col_exprs_.count() > 0)) {
    bool replace_happened = false;
    if (expr->has_flag(IS_DEFAULT)) {
      ObDefaultValueUtils utils(stmt, &params_, this);
      if (OB_FAIL(utils.resolve_default_function(expr, scope))) {
      }
    } else if (T_OP_IS == expr->get_expr_type()) {
      if (OB_FAIL(resolve_is_expr(expr, replace_happened))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && !replace_happened && i < expr->get_param_count(); i++) {
      if (OB_FAIL(SMART_CALL(resolve_special_expr(expr->get_param_expr(i), scope)))) {
      }
    }
  }
  if (OB_SUCC(ret) && expr->has_flag(CNT_CUR_TIME)) {
    stmt->get_query_ctx()->fetch_cur_time_ = true;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (stmt->is_select_stmt() && T_FIELD_LIST_SCOPE == scope) {
    // do nothing
  }
  return ret;
}

int ObDMLResolver::build_heap_table_hidden_pk_expr(ObRawExpr *&expr, const ObColumnRefRawExpr *ref_expr)
{
  int ret = OB_SUCCESS;
  ObPseudoColumnRawExpr *func_expr = nullptr;
  if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(ref_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_TABLET_AUTOINC_NEXTVAL, func_expr))) {
  } else {
    func_expr->set_expr_name(ObString::make_string("pk_tablet_seq"));
    func_expr->set_accuracy(ref_expr->get_accuracy());
    func_expr->set_result_flag(ref_expr->get_result_flag());
    func_expr->set_data_type(ref_expr->get_data_type());
    if (OB_FAIL(func_expr->formalize(session_info_))) {
    } else {
      expr = func_expr;
    }
  }
  return ret;
}

// build next_val expr; set expr as its param
int ObDMLResolver::build_autoinc_nextval_expr(ObRawExpr *&expr,
                                              const uint64_t autoinc_table_id,
                                              const uint64_t autoinc_col_id,
                                              const ObString autoinc_table_name,
                                              const ObString autoinc_column_name)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *func_expr = NULL;
  if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_FUN_SYS_AUTOINC_NEXTVAL, func_expr))) {
  } else {
    func_expr->set_func_name(ObString::make_string(N_AUTOINC_NEXTVAL));
    if (NULL != expr && OB_FAIL(func_expr->set_param_expr(expr))) {
    } else if (OB_FAIL(func_expr->formalize(session_info_))) {
    } else if (OB_FAIL(ObAutoincNextvalExtra::init_autoinc_nextval_extra(
            allocator_,
            reinterpret_cast<ObRawExpr *&>(func_expr),
            autoinc_table_id,
            autoinc_col_id,
            autoinc_table_name,
            autoinc_column_name))) {
    } else {
      expr = func_expr;
    }
  }
  return ret;
}

// build partid expr; set expr as its param

int ObDMLResolver::resolve_all_basic_table_columns(const TableItem &table_item, bool include_hidden, ObIArray<ColumnItem> *column_items)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  ColumnItem *col_item = NULL;
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(stmt)) {
    ret = OB_NOT_INIT;
    LOG_WARN("resolver status is invalid", K_(schema_checker), K(stmt));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!table_item.is_basic_table()
                         && !table_item.is_fake_cte_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table isn't basic table", K_(table_item.type));
  } else {
    const ObTableSchema* table_schema = NULL;
    // If select table is index table, then * expansion should be all columns of the index table rather than all columns of the main table
    if (OB_FAIL(schema_checker_->get_table_schema(
                                                  table_item.ref_id_,
                                                  table_schema))) {
    } else if (OB_FAIL(column_items->reserve(table_schema->get_column_count()))) {
    } else {
      ObColumnIterByPrevNextID iter(*table_schema);
      const ObColumnSchemaV2 *column_schema = NULL;
      int i = 0;
      while (OB_SUCC(ret) && OB_SUCC(iter.next(column_schema))) {
        if (OB_ISNULL(column_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (column_schema->is_shadow_column()) {
          // don't show shadow columns for select * from idx
          continue;
        } else if (column_schema->is_invisible_column()) {
          // don't show invisible columns for select * from tbl_name
          continue;
        } else if (!include_hidden
            && column_schema->is_hidden()) {
          // Skip hidden columns unless the caller explicitly requested them.
          continue;
        } else if (OB_FAIL(resolve_basic_column_item(table_item, column_schema->get_column_name_str(),
                                                     include_hidden, col_item))) {
        } else if (column_items != NULL) {
          if (OB_FAIL(column_items->push_back(*col_item))) {
          }
        }
      }
      if (ret != OB_ITER_END) {
      } else {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_all_generated_table_columns(const TableItem &table_item,
    ObIArray<ColumnItem> &column_items)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(stmt)) {
    ret = OB_NOT_INIT;
  } else if (!table_item.is_generated_table() && !table_item.is_temp_table()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(table_item.ref_query_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObString column_name; // not used, keep empty
    for (int64_t i = 0; OB_SUCC(ret) && i < table_item.ref_query_->get_select_item_size(); i++) {
      const uint64_t col_id = OB_APP_MIN_COLUMN_ID + i;
      ColumnItem *col_item = NULL;
      if (OB_FAIL(resolve_generated_table_column_item(table_item, column_name, col_item, stmt, col_id))) {
      } else if (OB_ISNULL(col_item)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(column_items.push_back(*col_item))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_and_split_sql_expr(const peg::Node &node, ObIArray<ObRawExpr*> &and_exprs)
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr = nullptr;
  if (OB_ISNULL(params_.expr_factory_) || OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(resolve_sql_expr(node, expr))) {
  } else if (OB_FAIL(expr->formalize(session_info_))) {
  } else {
    OZ (ObTransformUtils::flatten_expr(expr, and_exprs));
  }
  return ret;
}
// Parse all condition expr, and add bool expr before these condition expr as needed
// Only add bool expr when the new execution engine is enabled
int ObDMLResolver::resolve_and_split_sql_expr_with_bool_expr(const peg::Node &node,
                                                        ObIArray<ObRawExpr*> &and_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params_.expr_factory_) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(resolve_and_split_sql_expr(node, and_exprs))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < and_exprs.count(); ++i) {
      ObRawExpr *new_expr = NULL;
      if (OB_FAIL(ObRawExprUtils::try_create_bool_expr(and_exprs.at(i), new_expr,
                                                          *params_.expr_factory_))) {
      } else if (OB_FAIL(new_expr->formalize(session_info_))) {
      } else {
        and_exprs.at(i) = new_expr;
      }
    }
  }
  return ret;
}



int ObDMLResolver::resolve_where_clause(const peg::Node *expression)
{
  int ret = OB_SUCCESS;
  if (expression != nullptr) {
    current_scope_ = T_WHERE_SCOPE;
    ObDMLStmt *stmt = get_stmt();
    CK(OB_NOT_NULL(stmt));
    OZ(resolve_and_split_sql_expr_with_bool_expr(*expression, stmt->get_condition_exprs()));
  }
  return ret;
}

int ObDMLResolver::resolve_order_clause(rust::Slice<const peg::Node> order_items, bool is_for_set_query  /*default false*/)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!order_items.empty()) {
    current_scope_ = T_ORDER_SCOPE;
    stmt->get_order_items().reset();
    for (size_t i = 0; OB_SUCC(ret) && i < order_items.size(); ++i) {
      OrderItem order_item;
      if (OB_FAIL(resolve_order_item(order_items[i], order_item))) {
      } else if (OB_FAIL(stmt->add_order_item(order_item))) {
      }
      if (OB_ERR_AGGREGATE_ORDER_FOR_UNION == ret) {
        LOG_USER_ERROR(OB_ERR_AGGREGATE_ORDER_FOR_UNION, i + 1);
      }
    }
  } else if (!params_.is_from_create_view_ && stmt->is_select_stmt() &&is_for_set_query)  {
    // is_for_set_query means set query order should be preserved.
    //union/minus/intersect add select exprs as order by expers if stmt has no order items
    //not for union-all/recursive
      ObSEArray<ObRawExpr *, 4> select_exprs;
      ObSEArray<ObRawExpr *, 4> order_exprs;
      ObSEArray<ObOrderDirection, 8> default_directions;
      if (OB_FAIL(static_cast<ObSelectStmt *>(stmt)->get_select_exprs(select_exprs))) {
      }
      for(int64_t i = 0; OB_SUCC(ret) && i < select_exprs.count(); ++i) {
        bool is_comparable = true;
        if (OB_ISNULL(select_exprs.at(i))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ObRawExprUtils::is_expr_comparable(select_exprs.at(i), is_comparable))) {
        } else if (!is_comparable) {
          //do nothing
        } else if (OB_FAIL(order_exprs.push_back(select_exprs.at(i)))) {
        }
      }
      if (OB_FAIL(ret)) {
        //do nothing
      } else if (OB_FAIL(ObOptimizerUtil::get_default_directions(order_exprs.count(), default_directions))) {
      } else if (OB_FAIL(ObOptimizerUtil::make_sort_keys(order_exprs, default_directions, stmt->get_order_items()))) {
      }
  }
  return ret;
}

int ObDMLResolver::resolve_order_item(const peg::Node &sort_node, OrderItem &order_item)
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr = nullptr;
  const peg::Node *expression = &peg::sort_expression(sort_node);
  while (expression->kind() == peg::NodeKind::Parenthesized) {
    expression = &peg::parenthesized_expression(*expression);
  }
  if (OB_FAIL(ObResolverUtils::set_direction_by_mode(sort_node, order_item))) {
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_UNLIKELY(expression->kind() == peg::NodeKind::Int)) {
    ret = OB_ERR_PARSER_SYNTAX;
    SQL_RESV_LOG(WARN, "index order item not support in update");
  } else if (OB_FAIL(resolve_sql_expr(*expression, expr))) {
  } else {
    // check if order by item has var assign expr, which will cause uncertain behavior
    if (OB_NOT_NULL(expr) && expr->has_flag(CNT_ASSIGN_EXPR)) {
      LOG_USER_WARN(OB_ERR_DEPRECATED_SYNTAX, "Setting user variables within expressions",
        "SET variable=expression, ... or SELECT expression(s) INTO variables(s)");
      if (OB_NOT_NULL(session_info_) && OB_NOT_NULL(session_info_->get_cur_exec_ctx()) &&
          OB_NOT_NULL(session_info_->get_cur_exec_ctx()->get_sql_ctx())) {
        const ObSqlCtx *sql_ctx = session_info_->get_cur_exec_ctx()->get_sql_ctx();
        LOG_ERROR("Variable assignment in order by items will cause uncertain behavior",
                  K(ObString(sql_ctx->sql_id_)));
      }
    }
    order_item.expr_ = expr;
  }
  return ret;
}

int ObDMLResolver::add_column_to_stmt(const TableItem &table_item,
                                      const share::schema::ObColumnSchemaV2 &col,
                                      common::ObIArray<ObColumnRefRawExpr*> &column_exprs,
                                      ObDMLStmt *stmt)
{
  int ret = OB_SUCCESS;
  stmt = (NULL == stmt) ? get_stmt() : stmt;
  if (NULL == stmt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get stmt fail", K(stmt));
  } else {
    ColumnItem *column_item = NULL;
    if (table_item.is_generated_table() || table_item.is_temp_table()) {
      column_item = ObResolverUtils::find_col_by_base_col_id(*stmt, table_item, col.get_column_id(),
                                            col.get_table_id());
      if (NULL == column_item) {
        ret = OB_ERR_UNEXPECTED;
      }
    } else if (table_item.is_basic_table()) {
      column_item = stmt->get_column_item_by_id(table_item.table_id_, col.get_column_id());
      if (NULL == column_item) {
        if (OB_FAIL(resolve_basic_column_item(table_item, col.get_column_name_str(), true, column_item, stmt))) {
        } else if (OB_ISNULL(column_item) || OB_ISNULL(column_item->expr_)) {
          ret = OB_ERR_BAD_FIELD_ERROR;
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }


    if (OB_SUCC(ret)) {
      if (OB_FAIL(add_var_to_array_no_dup(column_exprs, column_item->expr_))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::add_all_rowkey_columns_to_stmt(const TableItem &table_item,
                                                  ObIArray<ObColumnRefRawExpr*> &column_exprs,
                                                  ObDMLStmt *stmt /*= NULL*/)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  const ObColumnSchemaV2 *column_schema = NULL;
  uint64_t rowkey_column_id = 0;
  uint64_t base_table_id = table_item.get_base_table_item().ref_id_;
  stmt = (NULL == stmt) ? get_stmt() : stmt;
  if (OB_ISNULL(stmt) || OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_checker_->get_table_schema( base_table_id, table_schema))) {
  } else if (NULL == table_schema) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid table schema", K(table_item));
  } else {
    const ObRowkeyInfo &rowkey_info = table_schema->get_rowkey_info();
    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_info.get_size(); ++i) {
      if (OB_FAIL(rowkey_info.get_column_id(i, rowkey_column_id))) {
      } else if (OB_FAIL(get_column_schema(base_table_id,
                                           rowkey_column_id,
                                           column_schema,
                                           true))) {
      } else if (OB_FAIL(add_column_to_stmt(table_item, *column_schema, column_exprs, stmt))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_approx_clause(bool approximate)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  uint64_t data_version = 0;
  if (OB_ISNULL(stmt) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (approximate && stmt->get_order_item_size() == 1) {
    int order_size = stmt->get_order_item_size();
    bool found = false;
    bool has_const = false;
    bool is_match = false;
    bool is_vec_index_valid = false;
    ObRawExpr *tmp_expr = stmt->get_order_item(0).expr_;
    if (OB_NOT_NULL(tmp_expr) && tmp_expr->is_vector_sort_expr()) {
      // only order by distance with approx, set it true
      int size = tmp_expr->get_param_count();
      for (int i = 0; OB_SUCC(ret) && i < size; ++i) {
        ObRawExpr *param_expr = tmp_expr->get_param_expr(i);
        if (OB_ISNULL(param_expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (param_expr->is_const_expr()) {
          has_const = true;
        } else if (param_expr->is_column_ref_expr()) {
          const ObColumnRefRawExpr &ref_col_expr = static_cast<const ObColumnRefRawExpr&>(*param_expr);
          share::schema::ObSchemaGetterGuard *schema_guard = NULL;
          const ObTableSchema *table_schema = nullptr;
          const ObString column_name = ref_col_expr.get_column_name();
          const int64_t column_id = ref_col_expr.get_column_id();
          int64_t table_id = OB_INVALID_ID;
          // find real table id
          for (int64_t i = 0; OB_SUCC(ret) && table_id == OB_INVALID_ID && i < stmt->get_column_size(); ++i) {
            ColumnItem &column_item = stmt->get_column_items().at(i);
            if (column_item.column_id_ == column_id) {
              table_id = column_item.base_tid_;
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(schema_guard = schema_checker_->get_schema_guard())) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                               table_id,
                                                               table_schema))) {
          } else if (OB_ISNULL(table_schema)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(ObVectorIndexUtil::check_distance_algorithm_match(
              *schema_guard, *table_schema, column_name, tmp_expr->get_expr_type(), is_match))) {
          } else if (!is_match) {
            LOG_WARN("distance expr and index distance algorithm is not match, will not set using index",
              K(tmp_expr->get_expr_type()));
          } else if (OB_FAIL(ObVectorIndexUtil::check_vector_index_by_column_name(
              *schema_guard, *table_schema, column_name, is_vec_index_valid))) {
          }
        } else {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Using vector index without column ref or vector const expr is");
        }
      }
      if (OB_SUCC(ret) && is_match && is_vec_index_valid && tmp_expr->get_expr_type() != T_FUN_SYS_INNER_PRODUCT) {
        stmt->set_has_vec_approx(true);
      }
      if (OB_SUCC(ret) && !has_const) {
        // if has vector expr, but no const param, not support use vector index
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Using vector index without const param is");
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "Using vector index without vector_sort_expr is");
    }
  } else if (approximate) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Multi order by item when using vector index is");
  }
  return ret;
}

int ObDMLResolver::resolve_vector_index_params(rust::Slice<const peg::Node> parameters)
{
  int ret = OB_SUCCESS;
  for (const auto &parameter : parameters) {
    if (OB_FAIL(ret)) { break; }
    const auto &name = peg::vector_parameter_name(parameter);
    const auto &value = peg::vector_parameter_value(parameter);
    OZ(ObVectorIndexUtil::resolve_query_param(syntax_text(name),
        params_.syntax_->source(value), get_stmt()->get_vector_index_query_param()));
  }
  return ret;
}

int ObDMLResolver::resolve_limit_clause(const peg::Node *node, bool disable_offset/*= false*/)
{
  int ret = OB_SUCCESS;
  if (node) {
    current_scope_ = T_LIMIT_SCOPE;
    ObDMLStmt *stmt = get_stmt();
    const peg::Node *limit_node = &peg::limit_count(*node);
    const peg::Node *offset_node = peg::limit_has_offset(*node) ? &peg::limit_offset(*node) : nullptr;
    for (const auto *value : {limit_node, offset_node}) {
      if (value != nullptr && value->kind() != peg::NodeKind::Parameter && value->kind() != peg::NodeKind::Column) {
        uint64_t integer = 0;
        if (OB_FAIL(ObResolverUtils::resolve_unsigned_integer(*value, "LIMIT", integer))) return ret;
      }
    }
    const auto parameter_index = [this](const peg::Node *value) -> int64_t {
      if (value == nullptr) return -1;
      const auto *binding = params_.parameterization_ != nullptr
          ? params_.parameterization_->find(value) : nullptr;
      return binding != nullptr ? binding->parameter_index
          : value->kind() == peg::NodeKind::Parameter ? peg::placeholder_index(*value) : -1;
    };
    const int64_t limit_index = parameter_index(limit_node);
    const int64_t offset_index = parameter_index(offset_node);
    ObRawExpr* limit_count = NULL;
    ObRawExpr* limit_offset = NULL;
    if (disable_offset && OB_NOT_NULL(offset_node)) {
      ret = OB_ERR_PARSE_SQL;
      const ObString source = params_.syntax_->source(*offset_node);
      int32_t str_len = source.length();
      int32_t line_no = 1;
      LOG_WARN("can't set offset for limit clause in delete/update stmt");
      LOG_USER_ERROR(OB_ERR_PARSE_SQL, ob_errpkt_strerror(OB_ERR_PARSER_SYNTAX),
                    str_len, source.ptr(), line_no);
    // resolve the question mark with less value first
    } else if (limit_index >= 0 && offset_index >= 0 && limit_index > offset_index) {
      if (OB_FAIL(ObResolverUtils::resolve_const_expr(params_, *offset_node, limit_offset, NULL))
          || OB_FAIL(ObResolverUtils::resolve_const_expr(params_, *limit_node, limit_count, NULL))) {
      }
    } else {
      if (limit_node != NULL) {
        OZ(ObResolverUtils::resolve_const_expr(params_, *limit_node, limit_count, NULL));
      }
      if (ret == OB_SUCCESS && offset_node != NULL) {
        OZ(ObResolverUtils::resolve_const_expr(params_, *offset_node, limit_offset, NULL));
      }
    }
    CK(session_info_)
    if (OB_SUCC(ret)) {
      // make sure limit expr is int value in static typing engine.
      ObRawExpr **exprs[] = { &limit_count, &limit_offset };
      for (int64_t i = 0; i < ARRAYSIZEOF(exprs) && OB_SUCC(ret); i++) {
        ObRawExprResType dst_type;
        dst_type.set_int();
        ObSysFunRawExpr *cast_expr = NULL;
        if (NULL != (*exprs[i]) && !ob_is_int_tc((*exprs[i])->get_result_type().get_type())) {
          OZ(ObRawExprUtils::create_cast_expr(
                  *params_.expr_factory_, *exprs[i], dst_type, cast_expr, session_info_));
          CK(NULL != cast_expr);
          if (OB_SUCC(ret)) {
            *exprs[i] = cast_expr;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      stmt->set_limit_offset(limit_count, limit_offset);
    }
  }
  return ret;
}

// Forbit select with order by limit exists in subquery in Oralce mode
// eg: select 1 from t1 where c1 in (select d1 from t2 order by c1); --error
// If subquery contains both fetch clause, then order by is allowed:
// eg: select 1 from t1 where c1 in (select d1 from t2 order by c1 fetch next 1 rows only); --right
int ObDMLResolver::check_order_by_for_subquery_stmt(const ObSubQueryInfo &info)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObDMLResolver::check_stmt_order_by(const ObSelectStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (stmt->has_order_by()) {
      ret = OB_ERR_PARSER_SYNTAX;
    } else if (stmt->is_set_stmt()) {
      ObSEArray<ObSelectStmt*, 2> child_stmts;
      if (OB_FAIL(stmt->get_child_stmts(child_stmts))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < child_stmts.count(); ++i) {
        const ObSelectStmt *sub_stmt = child_stmts.at(i);
        if (OB_FAIL(SMART_CALL(check_stmt_order_by(sub_stmt)))) {
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_inlist_info(ObIArray<ObInListInfo> &inlist_infos)
{
  int ret = OB_SUCCESS;
  if (inlist_infos.empty()) {
    /* do nothing */
  } else {
    ObInListResolver inlist_resolver(this);
    for (int64_t i = 0; OB_SUCC(ret) && i < inlist_infos.count(); i++) {
      if (OB_FAIL(inlist_resolver.resolve_inlist(inlist_infos.at(i)))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_subquery_info(const ObIArray<ObSubQueryInfo> &subquery_info)
{
  int ret = OB_SUCCESS;
  if (current_level_ + 1 >= OB_MAX_SUBQUERY_LAYER_NUM && subquery_info.count() > 0) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Too many levels of subquery");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < subquery_info.count(); i++) {
    const ObSubQueryInfo &info = subquery_info.at(i);
    ObSelectResolver subquery_resolver(params_);
    subquery_resolver.set_current_level(current_level_ + 1);
    subquery_resolver.set_current_view_level(current_view_level_);
    subquery_resolver.set_parent_namespace_resolver(this);
    subquery_resolver.set_in_exists_subquery(info.parents_expr_info_.has_member(IS_EXISTS));
    set_query_ref_exec_params(info.ref_expr_ == NULL ? NULL : &info.ref_expr_->get_exec_params());
    if (OB_FAIL(add_cte_table_to_children(subquery_resolver))) {
    } else if (OB_FAIL(subquery_resolver.add_parent_gen_col_exprs(gen_col_exprs_))) {
    } else if (info.parents_expr_info_.has_member(IS_AGG)) {
      subquery_resolver.set_parent_aggr_level(current_level_);
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(do_resolve_subquery_info(info, subquery_resolver))) {
      }
    }
    set_query_ref_exec_params(NULL);
  }
  return ret;
}

int ObDMLResolver::do_resolve_subquery_info(const ObSubQueryInfo &subquery_info, ObChildStmtResolver &child_resolver)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  ObSelectStmt *sub_stmt = NULL;

  if (OB_ISNULL(subquery_info.sub_query_) || OB_ISNULL(subquery_info.ref_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("subquery info is invalid", K_(subquery_info.sub_query), K_(subquery_info.ref_expr));
  } else if (OB_UNLIKELY(peg::NodeKind::Select != subquery_info.sub_query_->kind())) {
    ret = OB_ERR_ILLEGAL_TYPE;
    LOG_WARN("Unknown statement type in subquery", "stmt_type", static_cast<int32_t>(subquery_info.sub_query_->kind()));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(child_resolver.resolve_child_stmt(*(subquery_info.sub_query_)))) {
  } else {
    sub_stmt = child_resolver.get_child_stmt();
    subquery_info.ref_expr_->set_output_column(sub_stmt->get_select_item_size());
    // Save the result type of subquery select item to ObUnaryRef
    for (int64_t i = 0; OB_SUCC(ret) && i < sub_stmt->get_select_item_size(); ++i) {
      ObRawExpr *target_expr = sub_stmt->get_select_item(i).expr_;
      if (OB_ISNULL(target_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const ObRawExprResType &column_type = target_expr->get_result_type();
        if (OB_FAIL(subquery_info.ref_expr_->add_column_type(column_type))) {
        } else if (column_type.is_lob_storage()) {
          ObRawExprResType &last_item = subquery_info.ref_expr_->get_column_types().
                                     at(subquery_info.ref_expr_->get_column_types().count() - 1);
          last_item.set_has_lob_header();
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    subquery_info.ref_expr_->set_ref_stmt(sub_stmt);
    if (OB_FAIL(stmt->add_subquery_ref(const_cast<ObSubQueryInfo&>(subquery_info).ref_expr_))) {
    } else {
      if (OB_FAIL(check_order_by_for_subquery_stmt(subquery_info))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::check_col_param_on_expr(ObRawExpr *expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (expr->is_sys_func_expr()) {
    int param_num = expr->get_param_count();
    ObSysFunRawExpr* f_expr = static_cast<ObSysFunRawExpr *>(expr);
    // check param of json_exists passing clause
    if (0 == f_expr->get_func_name().case_compare("json_exists") && param_num >= 6) {
      for (int i = 2; i < param_num - 2 && OB_SUCC(ret); i += 2) {
        if (expr->get_param_expr(i)->get_expr_type() == T_REF_COLUMN) {
          ret = OB_ERR_INVALID_VARIABLE_IN_JSON_PATH;
          LOG_USER_ERROR(OB_ERR_INVALID_VARIABLE_IN_JSON_PATH);
        }
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}
int ObDMLResolver::check_expr_param(const ObRawExpr &expr)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_REF_QUERY == expr.get_expr_type()) {
    const ObQueryRefRawExpr &ref_query = static_cast<const ObQueryRefRawExpr&>(expr);
    if (1 != ref_query.get_output_column()) {
      ret = OB_ERR_INVALID_COLUMN_NUM;
      LOG_USER_ERROR(OB_ERR_INVALID_COLUMN_NUM, (int64_t)1);
    }
  } else if (T_OP_ROW == expr.get_expr_type()){
    const ObRawExpr *e = &expr;
    // need check row expr child, e.g.: +((c1, c2)) is resolved to: ROW(ROW(c1, c2))
    while (OB_SUCC(ret) && T_OP_ROW == e->get_expr_type() && 1 == e->get_param_count()) {
      e = e->get_param_expr(0);
      CK(NULL != e);
    }
    if (OB_SUCC(ret) && 1 != e->get_param_count()) {
      ret = OB_ERR_INVALID_COLUMN_NUM;
      LOG_USER_ERROR(OB_ERR_INVALID_COLUMN_NUM, (int64_t)1);
    }
  }
  return ret;
}

int ObDMLResolver::resolve_partitions(rust::Slice<const peg::Node> partitions,
                                      const ObTableSchema &schema, TableItem &table)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObObjectID, 4> ids;
  ObSEArray<ObString, 4> names;
  for (const auto &partition : partitions) {
    if (OB_FAIL(ret)) { break; }
    ObString name;
    ObSEArray<ObObjectID, 16> partition_ids;
    const auto partition_text = partition.text();
    OZ(ob_write_string(*allocator_, syntax_text(partition_text), name));
    if (OB_SUCC(ret)) {
      ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
      ObPartGetter getter(schema);
      if (OB_FAIL(getter.get_part_ids(name, partition_ids))) {
        if (ret == OB_UNKNOWN_PARTITION) {
          LOG_USER_ERROR(OB_UNKNOWN_PARTITION, name.length(), name.ptr(),
                         schema.get_table_name_str().length(), schema.get_table_name_str().ptr());
        }
      }
      OZ(append_array_no_dup(ids, partition_ids));
      OZ(names.push_back(name));
    }
  }
  OZ(table.part_ids_.assign(ids));
  OZ(table.part_names_.assign(names));
  return ret;
}


int ObDMLResolver::check_basic_column_generated(const ObColumnRefRawExpr *col_expr,
                                                ObDMLStmt *dml_stmt,
                                                bool &is_generated)
{
  int ret = OB_SUCCESS;
  is_generated = false;
  const TableItem *table_item = NULL;
  const ColumnItem *view_column_item = NULL;
  if (OB_ISNULL(col_expr) || OB_ISNULL(dml_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (col_expr->is_generated_column()) {
    is_generated = true;
  } else if (OB_ISNULL(table_item = dml_stmt->get_table_item_by_id(col_expr->get_table_id()))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (false == ((table_item->is_generated_table() || table_item->is_temp_table())
                        && OB_NOT_NULL(table_item->view_base_item_))) {
    //do thing
  } else if (OB_ISNULL(view_column_item = dml_stmt->get_column_item_by_id(
                                                                      col_expr->get_table_id(),
                                                                      col_expr->get_column_id()))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObSelectStmt *select_stmt = NULL;
    ColumnItem *basic_column_item = NULL;
    while((table_item->is_generated_table() || table_item->is_temp_table()) && OB_NOT_NULL(table_item->view_base_item_)) {
      select_stmt = table_item->ref_query_;
      table_item = table_item->view_base_item_;
    }
    if (OB_ISNULL(select_stmt) || OB_ISNULL(table_item)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(basic_column_item = select_stmt->get_column_item_by_id(
                                                            table_item->table_id_,
                                                            view_column_item->base_cid_))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(basic_column_item->expr_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (basic_column_item->expr_->is_generated_column()) {
      is_generated = true;
    }
  }
  return ret;
}

// Check that a generated column's pad flag is consistent with the session SQL mode.
int ObDMLResolver::check_pad_generated_column(const ObSQLMode sql_mode,
                                              const ObTableSchema &table_schema,
                                              const ObColumnSchemaV2 &column_schema,
                                              bool is_link)
{
  UNUSED(is_link);
  int ret = OB_SUCCESS;
  if (!column_schema.is_generated_column()) {
    // do nothing
  } else if (is_pad_char_to_full_length(sql_mode)
             == column_schema.has_column_flag(PAD_WHEN_CALC_GENERATED_COLUMN_FLAG)) {
    // do nothing
  } else {
    bool has_char_dep_column = false;
    bool is_stored_column = column_schema.is_stored_generated_column();
    ObSEArray<uint64_t, 5> cascaded_columns;
    ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
    if (OB_FAIL(column_schema.get_cascaded_column_ids(cascaded_columns))) {
    } else if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && !has_char_dep_column && i < cascaded_columns.count(); ++i) {
      uint64_t column_id = cascaded_columns.at(i);
      const ObColumnSchemaV2 *cascaded_col_schema = table_schema.get_column_schema(column_id);
      if (OB_ISNULL(cascaded_col_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (ObCharType == cascaded_col_schema->get_data_type()) {
        has_char_dep_column = true;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && !is_stored_column && i < simple_index_infos.count(); ++i) {
      const ObTableSchema *index_table_schema = NULL;
      if (OB_FAIL(schema_checker_->get_table_schema( simple_index_infos.at(i).table_id_, index_table_schema))) {
      } else if (OB_ISNULL(index_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(index_table_schema->has_column(column_schema.get_column_id(), is_stored_column))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (has_char_dep_column && is_stored_column) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "change PAD_CHAR option after created generated column");
    }
  }
  return ret;
}

int ObDMLResolver::build_padding_expr(const ObSQLSessionInfo *session,
                                      const ColumnItem* column,
                                      ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(session));
  CK(OB_NOT_NULL(column));
  CK(OB_NOT_NULL(expr));
  CK(OB_NOT_NULL(get_stmt()));
  const TableItem *table_item = NULL;
  CK(OB_NOT_NULL(table_item = get_stmt()->get_table_item_by_id(column->table_id_)));
  if (OB_SUCC(ret)) {
    const ObColumnSchemaV2 *column_schema = NULL;
    const uint64_t tid = OB_INVALID_ID == column->base_tid_ ? column->table_id_ : column->base_tid_;
    const uint64_t cid = OB_INVALID_ID == column->base_cid_ ? column->column_id_ : column->base_cid_;
    if (OB_FAIL(get_column_schema(tid, cid, column_schema, true))) {
    } else if (NULL == column_schema) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get column schema fail", K(column_schema));
    } else if (OB_FAIL(build_padding_expr(session, column_schema, expr))) {
    }
  }
  return ret;
}

int ObDMLResolver::build_padding_expr(const ObSQLSessionInfo *session,
                                      const ObColumnSchemaV2 *column_schema,
                                      ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(build_padding_expr(session->get_sql_mode(), column_schema, expr))) {
  }
  return ret;
}

int ObDMLResolver::build_padding_expr(const ObSQLMode sql_mode,
                                      const share::schema::ObColumnSchemaV2 *column_schema,
                                      ObRawExpr *&expr,
                                      const ObLocalSessionVar *local_vars,
                                      int64_t local_var_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(column_schema) || OB_ISNULL(expr) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ObObjMeta::is_binary(column_schema->get_data_type(), column_schema->get_collation_type())) {
    if (OB_FAIL(ObRawExprUtils::build_pad_expr(*params_.expr_factory_,
                                               false,
                                               column_schema,
                                               expr,
                                               this->session_info_,
                                               local_vars,
                                               local_var_id))) {
    }
  } else if (ObCharType == column_schema->get_data_type()) {
    if (is_pad_char_to_full_length(sql_mode)) {
      if (OB_FAIL(ObRawExprUtils::build_pad_expr(*params_.expr_factory_,
                                                 true,
                                                 column_schema,
                                                 expr,
                                                 this->session_info_,
                                                 local_vars,
                                                 local_var_id))) {
      }
    } else {
      if (OB_FAIL(ObRawExprUtils::build_trim_expr(column_schema, *params_.expr_factory_, session_info_, expr, local_vars, local_var_id))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::build_nvl_expr(const ColumnItem *column_item, ObRawExpr *&expr1, ObRawExpr *&expr2)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params_.expr_factory_) || OB_ISNULL(session_info_) ||
      OB_ISNULL(column_item) || OB_ISNULL(expr1) || OB_ISNULL(expr2)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObRawExprUtils::build_nvl_expr(*params_.expr_factory_, column_item, expr1, expr2))) {
  } else if (OB_FAIL(expr1->formalize(session_info_))) {
  }
  return ret;
}

int ObDMLResolver::build_nvl_expr(const ColumnItem *column_item, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(column_item) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (column_item->get_column_type()->is_timestamp() && column_item->is_not_null_for_write()) {
    bool explicit_value = false;
    if (NULL == session_info_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(session_info_->get_explicit_defaults_for_timestamp(explicit_value))) {
    } else if (!explicit_value) {
      if (OB_FAIL(ObRawExprUtils::build_nvl_expr(*params_.expr_factory_, column_item, expr))) {
      } else if (OB_FAIL(expr->formalize(session_info_))) {
      }
    }
  }
  return ret;
}
// Special processing for c1 is null (c1 is the auto-increment column issue)
int ObDMLResolver::resolve_autoincrement_column_is_null(ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  bool sql_auto_is_null = false;
  if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_) || OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(expr->get_expr_type() != T_OP_IS) || OB_ISNULL(expr->get_param_expr(0))
  || OB_ISNULL(expr->get_param_expr(1))
  || OB_UNLIKELY(expr->get_param_expr(0)->get_expr_type() != T_REF_COLUMN)
  || OB_UNLIKELY(expr->get_param_expr(1)->get_expr_type() != T_NULL)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(session_info_->get_sql_auto_is_null(sql_auto_is_null))) {
  } else if (!sql_auto_is_null) {
    //nothing to do
  } else if (OB_FAIL(ObRawExprUtils::build_equal_last_insert_id_expr(
              *params_.expr_factory_, expr, session_info_))) {
  }
  return ret;
}

int ObDMLResolver::resolve_not_null_date_column_is_null(ObRawExpr *&expr, const ObRawExprResType* col_type)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *equal_expr = NULL;
  ObConstRawExpr *zero_date = NULL;
  ObRawExpr *or_expr = NULL;
  ObSEArray<ObRawExpr*, 2> param_exprs;
  if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_)
      || OB_ISNULL(expr) || OB_ISNULL(col_type)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_OP_EQ, equal_expr))) {
  } else if (col_type->is_date() &&
             OB_FAIL(ObRawExprUtils::build_const_date_expr(*params_.expr_factory_,
                                                           ObTimeConverter::ZERO_DATE,
                                                           zero_date))) {
  } else if (col_type->is_datetime() &&
             OB_FAIL(ObRawExprUtils::build_const_datetime_expr(*params_.expr_factory_,
                                                               ObTimeConverter::ZERO_DATETIME,
                                                               zero_date))) {
  } else if (col_type->is_mysql_date() &&
             OB_FAIL(ObRawExprUtils::build_const_mysql_date_expr(*params_.expr_factory_,
                                                                 ObTimeConverter::MYSQL_ZERO_DATE,
                                                                 zero_date))) {
  } else if (col_type->is_mysql_datetime() &&
             OB_FAIL(ObRawExprUtils::build_const_mysql_datetime_expr(*params_.expr_factory_,
                                                               ObTimeConverter::MYSQL_ZERO_DATETIME,
                                                               zero_date))) {
  }else if (OB_ISNULL(equal_expr) || OB_ISNULL(zero_date)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(equal_expr->set_param_exprs(expr->get_param_expr(0), zero_date))) {
  } else if (OB_FAIL(param_exprs.push_back(expr))) {
  } else if (OB_FAIL(param_exprs.push_back(equal_expr))) {
  } else if (OB_FAIL(ObRawExprUtils::build_or_exprs(*params_.expr_factory_,
                                                    param_exprs,
                                                    or_expr))) {
  } else if (OB_FAIL(or_expr->formalize(session_info_))) {
  } else {
    expr = or_expr;
  }
  return ret;
}

bool ObDMLResolver::is_need_add_additional_function(const ObRawExpr *expr)
{
  bool bret = false;
  if (OB_ISNULL(expr)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid argument to check whether to add additional function", K(expr));
  } else if (T_FUN_COLUMN_CONV == expr->get_expr_type()) {
    bret = false;
  } else {
    bret = true;
  }
  return bret;
}
// Column conversion relies on the cast expression to perform the conversion.
// eg: column_conv -> cast_expr -> child_expr directly add pad expr after might cover cast expr, become
//     column_conv -> pad_expr -> cast_expr -> child_expr, so need to erase inner expr first,
//     Add pad, then perform formalize, final result is:
//     column_conv -> cast_expr -> pad_expr -> child_expr
int ObDMLResolver::try_add_padding_expr_for_column_conv(const ColumnItem *column,
                                                        ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(session_info_));
  CK(OB_NOT_NULL(column));
  CK(OB_NOT_NULL(expr));
  CK(OB_NOT_NULL(params_.query_ctx_));
  if (OB_SUCC(ret) && T_FUN_COLUMN_CONV == expr->get_expr_type()
      && !params_.query_ctx_->is_prepare_stmt()) {
    CK(ObExprColumnConv::PARAMS_COUNT_WITH_COLUMN_INFO == expr->get_param_count()
      || ObExprColumnConv::PARAMS_COUNT_WITHOUT_COLUMN_INFO == expr->get_param_count());
    CK(OB_NOT_NULL(expr->get_param_expr(4)));
    if (OB_SUCC(ret)) {
      ObRawExpr *&ori_child = expr->get_param_expr(4);
      ObRawExpr *real_child = NULL;
      OZ(ObRawExprUtils::erase_inner_cast_exprs(ori_child, real_child));
      CK(OB_NOT_NULL(real_child));
      if (OB_SUCC(ret) && real_child->get_expr_type() != T_FUN_PAD
          && real_child->get_expr_type() != T_FUN_INNER_TRIM) {
        if (OB_FAIL(build_padding_expr(session_info_, column, real_child))) {
        } else {
          ObRawExpr *&ref_child = expr->get_param_expr(4);
          CK(OB_NOT_NULL(ref_child));
          CK(OB_NOT_NULL(real_child));
          OX(ref_child = real_child);
          OZ(expr->formalize(session_info_));
        }
      }
    } else if (OB_SUCC(ret)) {
      ObRawExpr *&ori_child = expr->get_param_expr(4);
      if (ori_child->get_expr_type() != T_FUN_PAD
          && ori_child->get_expr_type() != T_FUN_INNER_TRIM) {
        if (OB_FAIL(build_padding_expr(session_info_, column, ori_child))) {
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::add_additional_function_according_to_type(const ColumnItem *column,
                                                             ObRawExpr *&expr,
                                                             ObStmtScope scope,
                                                             bool need_padding,
                                                             bool in_insert_value_list)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(column) || OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(column->expr_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(params_.query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!is_need_add_additional_function(expr)) {
    // Used to process values(c1) function where c1 is char/binary;
    if (need_padding && OB_FAIL(try_add_padding_expr_for_column_conv(column, expr))) {
    }
  } else {
    if (OB_SUCC(ret)) {
      if (T_INSERT_SCOPE == scope && column->is_auto_increment()) {
        // Convert the nextval result to the column's declared data type first.
        if (OB_FAIL(ObRawExprUtils::build_column_conv_expr(*params_.expr_factory_,
                                                           *params_.allocator_,
                                                           *column->get_expr(),
                                                           expr,
                                                           session_info_))) {
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(build_autoinc_nextval_expr(
                expr,
                column->base_tid_,column->base_cid_,
                column->get_expr()->get_table_name(),
                column->get_expr()->get_column_name()))) {
        }
      } else if (column->get_column_type()->is_timestamp()) {
        if (OB_FAIL(build_nvl_expr(column, expr))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (need_padding && !params_.query_ctx_->is_prepare_stmt()) {
        OZ(build_padding_expr(session_info_, column, expr));
      }
      if (OB_SUCC(ret)) {
        if (!in_insert_value_list && OB_FAIL(ObRawExprUtils::build_column_conv_expr(*params_.expr_factory_,
                                                                  *params_.allocator_,
                                                                  *column->get_expr(),
                                                                  expr,
                                                                  session_info_))) {
        } else if (column->is_geo_ && T_FUN_COLUMN_CONV == expr->get_expr_type()) {
          // 1. set geo sub type to cast mode to column covert expr when update
          // 2. check geo type while doing column covert.
          const ObColumnRefRawExpr *raw_expr = column->get_expr();
          if (OB_ISNULL(raw_expr)) {
            ret = OB_ERR_NULL_VALUE;
          } else {
            ObGeoType geo_type = raw_expr->get_geo_type();
            ObConstRawExpr *type_expr = static_cast<ObConstRawExpr *>(expr->get_param_expr(0));
            ObObj obj;
            obj.set_int(ObInt32Type, static_cast<uint32_t>(geo_type) << 16 | ObGeometryType);
            type_expr->set_value(obj);
          }
        }
      }
    }
  } //end else
  return ret;
}

int ObDMLResolver::get_ddl_schema_in_insert_into_select_clause(const ObTableSchema *&ddl_table_schema)
{
  int ret = OB_SUCCESS;
  ddl_table_schema = nullptr;

  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (stmt->is_insert_stmt()) {
    const ObInsertStmt *insert_stmt = reinterpret_cast<ObInsertStmt *>(stmt);
    if (OB_NOT_NULL(session_info_) && OB_NOT_NULL(schema_checker_) && session_info_->get_ddl_info().is_ddl()) {
      if (OB_ISNULL(insert_stmt) || OB_UNLIKELY(insert_stmt->get_table_items().count() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                           insert_stmt->get_table_item(0)->ddl_table_id_,
                                                           ddl_table_schema))) {
      } else if (OB_ISNULL(ddl_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_generated_column_expr(const ObString &expr_str,
    const TableItem &table_item, const ObColumnSchemaV2 *column_schema,
    const ObColumnRefRawExpr &column, ObRawExpr *&ref_expr,
    const bool used_for_generated_column, ObDMLStmt *stmt/* = NULL */)
{
  int ret = OB_SUCCESS;
  ObArray<ObQualifiedName> columns;
  ObRawExprFactory *expr_factory = NULL;
  ObSQLSessionInfo *session_info = NULL;
  const ObTableSchema *table_schema = NULL;
  const ObTableSchema *ddl_table_schema = nullptr;
  bool include_hidden = false;
  ObSQLMode sql_mode = 0;
  ObCollationType cs_type = CS_TYPE_INVALID;
  if (OB_ISNULL(expr_factory = params_.expr_factory_)
     || OB_ISNULL(session_info = params_.session_info_)
     || OB_ISNULL(schema_checker_)) {
    ret = OB_NOT_INIT;
  } else if (OB_NOT_NULL(column_schema) &&
             OB_FAIL(schema_checker_->get_table_schema( column_schema->get_table_id(),
                                                       table_schema))) {
  } else {
    sql_mode = session_info->get_sql_mode();
    cs_type = session_info->get_local_collation_connection();
    if (OB_NOT_NULL(column_schema)) {
      //prefer using the solidified vars
      if (OB_FAIL(ObSQLUtils::merge_solidified_var_into_collation(
            column_schema->get_local_session_var(), cs_type))) {
      } else if (OB_FAIL(ObSQLUtils::merge_solidified_var_into_sql_mode(
                  &column_schema->get_local_session_var(), sql_mode))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
    //do nothing
  } else if (OB_FAIL(ObRawExprUtils::build_generated_column_expr(expr_str,
                                                                 *expr_factory, *session_info,
                                                                 sql_mode, cs_type,
                                                                 ref_expr, columns,
                                                                 table_schema,
                                                                 this,
                                                                 schema_checker_))) {
  } else if (OB_NOT_NULL(column_schema) && column_schema->is_doc_id_column()) {
    bool need_fill = true;
    if (OB_FAIL(get_ddl_schema_in_insert_into_select_clause(ddl_table_schema))) {
    } else if (OB_NOT_NULL(ddl_table_schema) && !ddl_table_schema->is_fts_or_multivalue_index()) {
      need_fill = false;
    }
    if (OB_SUCC(ret) && need_fill && OB_FAIL(fill_doc_id_expr_param(table_item.table_id_, table_item.ref_id_, table_schema, ref_expr, stmt))) {
    }
  } else if (OB_NOT_NULL(column_schema) && column_schema->is_vec_hnsw_vid_column()) {
    bool need_fill = true;
    if (OB_FAIL(get_ddl_schema_in_insert_into_select_clause(ddl_table_schema))) {
    } else if (OB_NOT_NULL(ddl_table_schema) && !ddl_table_schema->is_vec_hnsw_index()) {
      need_fill = false;
    }
    if (OB_SUCC(ret) && need_fill && OB_FAIL(fill_vec_id_expr_param(table_item.table_id_, table_item.ref_id_, table_schema, ref_expr, stmt))) {
    }
  }

  bool is_default_udt_constructor = false;
  ObArray<ObRawExpr*> udf_construct_exprs;
  ColumnItem *basic_column_item = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
    ColumnItem *col_item = NULL;
    ObRawExpr *real_ref_expr = NULL;
    ObArray<ObRawExpr*> real_exprs;
    if (!used_for_generated_column && !(columns.at(i).is_sys_func() || columns.at(i).is_pl_udf())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (columns.at(i).is_sys_func()) {
      if (OB_FAIL(resolve_qualified_identifier(columns.at(i), columns, real_exprs, real_ref_expr))) {
      } else if (OB_ISNULL(real_ref_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (!real_ref_expr->is_sys_func_expr()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObSysFunRawExpr *sys_func_expr = static_cast<ObSysFunRawExpr*>(real_ref_expr);
        if (OB_FAIL(sys_func_expr->check_param_num())) {
        }
      }
    } else if (columns.at(i).is_pl_udf()) {
      if (OB_FAIL(resolve_qualified_identifier(columns.at(i), columns, real_exprs, real_ref_expr))) {
      } else if (OB_ISNULL(real_ref_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (used_for_generated_column) {
        if (!real_ref_expr->is_udf_expr()) {
          ret = OB_ERR_UNEXPECTED;
        }
      } else { // !used_for_generated_column
        if ((real_ref_expr->get_expr_type() != T_FUN_PL_OBJECT_CONSTRUCT)
            && (real_ref_expr->get_expr_type() != T_FUN_PL_COLLECTION_CONSTRUCT)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          is_default_udt_constructor = true;
          // column_ref is replaced inside build_generated_column_expr with udf,
          // here replace udf with object/collection constructor
          if (OB_FAIL(udf_construct_exprs.push_back(real_ref_expr))) {
          }

          for (int64_t i = 0; OB_SUCC(ret) && i < udf_construct_exprs.count(); ++i) {
            ObQualifiedName &q_name = columns.at(i);
            if (OB_FAIL(ObRawExprUtils::replace_ref_column(real_ref_expr,
                                                            q_name.ref_expr_,
                                                            udf_construct_exprs.at(i)))) {
            }
          }
          // replace expr, only outside ref_expr_ is equal to expr
          ObQualifiedName &q_name = columns.at(i);
          const ObUDFInfo &udf_info = q_name.access_idents_.at(q_name.access_idents_.count() - 1).udf_info_;
          if (OB_SUCC(ret) && OB_NOT_NULL(udf_info.ref_expr_)) {
            if (OB_FAIL(ObRawExprUtils::replace_ref_column(ref_expr, udf_info.ref_expr_, real_ref_expr))) {
            }
          }
        }
      }
    } else {
      if (OB_FAIL(resolve_basic_column_item(table_item, columns.at(i).col_name_,
                                            include_hidden, col_item, stmt))) {
      } else if (OB_ISNULL(col_item) || OB_ISNULL(col_item->expr_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        real_ref_expr = col_item->expr_;
        basic_column_item = col_item;
      }
    }

    if (OB_SUCC(ret) && !is_default_udt_constructor) {
      if (OB_FAIL(real_exprs.push_back(ref_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::replace_ref_column(ref_expr, columns.at(i).ref_expr_, real_ref_expr))) {
      } else { /*do nothing*/ }
    }
  }

  // fill ivf vector index expr
  if (OB_SUCC(ret) && OB_NOT_NULL(column_schema)) {
    bool need_fill = false;
    bool need_dist_algo_expr = false;
    if (OB_FAIL(check_need_fill_ivf_vec_expr_param(*stmt, *column_schema, ref_expr, need_fill, need_dist_algo_expr))) {
    } else if (need_fill) {
      if (OB_ISNULL(basic_column_item)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(fill_ivf_vec_expr_param(table_item.table_id_, table_item.ref_id_, basic_column_item->column_id_,
          column_schema, table_schema, need_dist_algo_expr, ref_expr, stmt))) {
      }
    }
  }

  // fill embedded_vec expr
  if (OB_SUCC(ret) && OB_NOT_NULL(column_schema) && column_schema->is_hybrid_embedded_vec_column()) {
    bool need_fill = false;
    if (OB_FAIL(check_need_fill_embedded_vec_expr_param(*stmt, *column_schema, need_fill))) {
    } else if (need_fill) {
      if (OB_FAIL(fill_embedded_vec_expr_param(table_item.table_id_, table_item.ref_id_, column_schema->get_column_id(), table_schema, ref_expr, stmt))) {
      }
    }
  }

  int64_t var_array_idx = OB_INVALID_INDEX_INT64;
  ObLocalSessionVar local_vars(allocator_);
  if (OB_SUCC(ret)) {
    //set local session info for generate columns
    if (OB_ISNULL(params_.query_ctx_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (0 == column_schema->get_local_session_var().get_var_count()) {
      //do nothing
    } else if (OB_FAIL(local_vars.assign(column_schema->get_local_session_var()))) {
    } else if (OB_FAIL(ObLocalSessionVarHelper::remove_vars_same_with_session(local_vars, session_info))) {
    } else if (0 == local_vars.get_var_count()) {
      //do nothing if all local vars are same with cur session vars
    } else if (OB_FAIL(params_.query_ctx_->add_local_session_vars(allocator_, local_vars, var_array_idx))) {
    } else if (!session_info->is_inner()) {
      //print user warnings
      ObSEArray<const ObSessionSysVar *, 4> var_array;
      if (OB_FAIL(local_vars.get_local_vars(var_array))) {
      } else {
        SMART_VAR(char[OB_MAX_DEFAULT_VALUE_LENGTH],val_buf) {
          for (int64_t i = 0; OB_SUCC(ret) && i < var_array.count(); ++i) {
            ObString var_name;
            int64_t pos = 0;
            if (OB_ISNULL(var_array.at(i))) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(share::ObSysVarMeta::get_sys_var_name_by_id(var_array.at(i)->type_, var_name))) {
            } else if (OB_FAIL(var_array.at(i)->val_.print_sql_literal(val_buf, 100, pos))) {
            } else {
              LOG_USER_WARN(OB_ERR_SESSION_VAR_CHANGED,
                            var_name.length(), var_name.ptr(),
                            column_schema->get_column_name_str().length(), column_schema->get_column_name_str().ptr(),
                            static_cast<int>(pos), &val_buf[0]);
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(check_pad_generated_column(sql_mode, *table_schema, *column_schema))) {
    } else if (OB_FAIL(ObRawExprUtils::build_pad_expr_recursively(*expr_factory, *session_info,
        *table_schema, *column_schema, ref_expr, &local_vars, var_array_idx))) {
    } else if (OB_FAIL(build_padding_expr(sql_mode, column_schema, ref_expr, &local_vars, var_array_idx))) {
    } else if (OB_FAIL(ref_expr->formalize_with_local_vars(session_info, &local_vars, var_array_idx))) {
    } else if (ObRawExprUtils::need_column_conv(column.get_result_type(), *ref_expr, true)) {
      if (OB_FAIL(ObRawExprUtils::build_column_conv_expr(*expr_factory, *allocator_,
                                                         column, ref_expr, session_info,
                                                         used_for_generated_column,
                                                         &local_vars,
                                                         var_array_idx))) {
      }
    }
  }

  return ret;
}

int ObDMLResolver::resolve_generated_column_expr_temp(TableItem *table_item)
{
  int ret = OB_SUCCESS;
  bool has_index = false;
  const ObColumnSchemaV2 *col_schema = NULL;
  const share::schema::ObTableSchema *table_schema = NULL;
  if (OB_ISNULL(params_.expr_factory_) || OB_ISNULL(schema_checker_) ||
      OB_ISNULL(params_.session_info_) || OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!table_item->is_basic_table()) {
    //do nothing
  } else if (table_item->ref_id_ == OB_INVALID_ID) {
    //do nothing
  } else if (OB_FAIL(schema_checker_->get_table_schema( table_item->ref_id_,
              table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (table_schema->has_generated_column()) {
    ObArray<uint64_t> column_ids;
    ObRawExpr *expr = NULL;
    if (OB_FAIL(table_schema->get_generated_column_ids(column_ids))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
      ObString expr_def;
      ColumnItem *col_item = NULL;
      if (OB_ISNULL(col_schema = table_schema->get_column_schema(column_ids.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get column schema failed", K(column_ids.at(i)));
      } else if (!col_schema->is_generated_column()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column schema is not generated column", K(*col_schema));
      } else if (OB_FAIL(schema_checker_->check_column_has_index(col_schema->get_table_id(),
                                                                 col_schema->get_column_id(),
                                                                 has_index))) {
      } else if (!col_schema->is_stored_generated_column() && !has_index
                 && !col_schema->is_spatial_generated_column()
                 && !col_schema->is_vec_ivf_data_vector_column()
                 && !col_schema->is_vec_ivf_center_vector_column()
                 && !col_schema->is_vec_ivf_pq_center_ids_column()) {
        //do nothing
        // Match generated columns materialized to storage, reduce redundant calculations
        //heap table's generated column as a partition key will also be materialized as a primary key
        // for view, ivf_data_vector need to be resolved in data_table select stmt
      } else if (col_schema->is_fulltext_column() && col_schema->is_virtual_generated_column()) {
        // fulltext columns on main table are hidden virtual generated columns, do not access it by default
        // for view, cid need to be resolved in data_table select stmt
      } else if (NULL == get_stmt()->get_column_item_by_id(table_item->table_id_, col_schema->get_column_id())) {
        if (OB_FAIL(resolve_basic_column_item(*table_item, col_schema->get_column_name_str(), true, col_item, get_stmt()))) {
        }
      }
    }
  }
  return ret;
}


int ObDMLResolver::find_const_params_for_gen_column(const ObRawExpr &expr)
{
  int ret = OB_SUCCESS;

  CK( OB_NOT_NULL(params_.query_ctx_) );

  for (int64_t i = 0; OB_SUCC(ret) && i < gen_col_exprs_.count(); i++) {
    CK( OB_NOT_NULL(gen_col_exprs_.at(i).dependent_expr_) );
    ObExprEqualCheckContext check_context;
    ObPCConstParamInfo const_param_info;

    check_context.err_code_ = OB_SUCCESS;
    check_context.override_const_compare_ = false;
    check_context.ignore_implicit_cast_ = true;

    if (OB_SUCC(ret) &&
        gen_col_exprs_.at(i).dependent_expr_->same_as(expr, &check_context)) {
      if (OB_FAIL(check_context.err_code_)) {
      } else if (check_context.param_expr_.count() > 0) {
        // generate column may not contain const param, so check this
        const_param_info.const_idx_.reset();
        const_param_info.const_params_.reset();
        for (int64_t i = 0; OB_SUCC(ret) && i < check_context.param_expr_.count(); i++) {
          ObExprEqualCheckContext::ParamExprPair &param_expr = check_context.param_expr_.at(i);
          CK( OB_NOT_NULL(param_expr.expr_),
              param_expr.param_idx_ >= 0 );
          if (OB_SUCC(ret)) {
            OC( (const_param_info.const_idx_.push_back)(param_expr.param_idx_) );

            const ObConstRawExpr *c_expr = dynamic_cast<const ObConstRawExpr *>(param_expr.expr_);
            CK( OB_NOT_NULL(c_expr) );
            OC( (const_param_info.const_params_.push_back)(c_expr->get_value()) );
          }
        }
        OC( (params_.query_ctx_->all_possible_const_param_constraints_.push_back)(const_param_info) );
      }
    }
  }
  return ret;
}

int ObDMLResolver::deduce_generated_exprs(ObIArray<ObRawExpr*> &exprs)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr*, 8> generate_exprs;
  for (int64_t i = 0; OB_SUCC(ret) && !params_.is_from_create_view_ && i < exprs.count(); ++i) {
    ObRawExpr *expr = exprs.at(i);
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObColumnRefRawExpr *column_expr = NULL;
      ObRawExpr *value_expr = NULL;
      ObRawExpr *escape_expr = NULL;
      ObRawExpr *param_expr1 = NULL;
      ObRawExpr *param_expr2 = NULL;
      ObItemType type = expr->get_expr_type();
      if (OB_FAIL(ret)) {
      } else if (IS_BASIC_CMP_OP(expr->get_expr_type())
                 || IS_SINGLE_VALUE_OP(expr->get_expr_type())) {
        //only =/</<=/>/>=/IN/like can deduce generated exprs
        if (OB_ISNULL(param_expr1 = expr->get_param_expr(0)) || OB_ISNULL(param_expr2 = expr->get_param_expr(1))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (T_OP_LIKE == expr->get_expr_type()) {
          /*

          err1: should add const expr for expr2
          err2: if expr2 is 'a%d' deduce is error
                c1 like 'a%d' DOESN'T MEAN:
                substr(c1, 1, n) like substr('a%d', 1, n)
                because after %, there is normal char.
          */
          column_expr = NULL;
        } else if (T_OP_IN == expr->get_expr_type()) {
          if (T_OP_ROW == param_expr2->get_expr_type()
              && !param_expr2->has_generalized_column()
              && param_expr1->get_result_type().is_string_type()) {
            bool all_match = true;
            for (int64_t j = 0; OB_SUCC(ret) && all_match && j < param_expr2->get_param_count(); ++j) {
              if (OB_ISNULL(param_expr2->get_param_expr(j))) {
                ret = OB_ERR_UNEXPECTED;
              } else if (!param_expr2->get_param_expr(j)->get_result_type().is_string_type()
                  || param_expr2->get_param_expr(j)->get_collation_type() != param_expr1->get_collation_type()) {
                all_match = false;
              }
            }
            if (OB_SUCC(ret) && all_match) {
              column_expr = static_cast<ObColumnRefRawExpr*>(expr->get_param_expr(0));
              value_expr = expr->get_param_expr(1);
            }
          }
        } else if (param_expr1->is_column_ref_expr() && param_expr2->is_const_raw_expr()) {
          if (param_expr1->get_result_type().is_string_type() //only for string and same collation
              && param_expr2->get_result_type().is_string_type()
              && param_expr1->get_collation_type() == param_expr2->get_collation_type()) {
            column_expr = static_cast<ObColumnRefRawExpr*>(expr->get_param_expr(0));
            value_expr = expr->get_param_expr(1);
          }
        } else if (param_expr1->is_const_raw_expr() && param_expr2->is_column_ref_expr()) {
          if (param_expr1->get_result_type().is_string_type()
              && param_expr2->get_result_type().is_string_type()
              && param_expr1->get_collation_type() == param_expr2->get_collation_type()) {
            type = get_opposite_compare_type(expr->get_expr_type());
            column_expr = static_cast<ObColumnRefRawExpr*>(expr->get_param_expr(1));
            value_expr = expr->get_param_expr(0);
          }
        }

        //only column op const
        if (OB_SUCC(ret) && NULL != column_expr && column_expr->has_generated_column_deps()) {
          for (int64_t j = 0; OB_SUCC(ret) && j < gen_col_exprs_.count(); ++j) {
            const ObRawExpr *dep_expr = gen_col_exprs_.at(j).dependent_expr_;
            const ObRawExpr *substr_expr = NULL;
            if (OB_ISNULL(dep_expr)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (ObRawExprUtils::has_prefix_str_expr(*dep_expr, *column_expr, substr_expr)) {
              ObRawExpr *new_expr = NULL;
              ObColumnRefRawExpr *left_expr = NULL;
              ObDMLStmt *stmt = gen_col_exprs_.at(j).stmt_;
              TableItem *table_item = gen_col_exprs_.at(j).table_item_;
              const ObString &column_name = gen_col_exprs_.at(j).column_name_;
              ColumnItem *col_item = NULL;
              ObItemType gen_type = type;
              if (T_OP_GT == type) {
                gen_type = T_OP_GE;
              } else if (T_OP_LT == type) {
                gen_type = T_OP_LE;
              }
              if (OB_ISNULL(table_item)) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(resolve_basic_column_item(*table_item, column_name, true, col_item, stmt))) {
              } else if (OB_ISNULL(col_item) || OB_ISNULL(left_expr = col_item->expr_)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("column item is invalid", K(col_item), K(left_expr));
              } else if (OB_FAIL(build_prefix_index_compare_expr(*left_expr,
                                                                 const_cast<ObRawExpr*>(substr_expr),
                                                                 gen_type,
                                                                 *value_expr,
                                                                 escape_expr,
                                                                 new_expr))) {
              } else if (OB_FAIL(generate_exprs.push_back(new_expr))) {
              } else { /*do nothing*/ }
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(append(exprs, generate_exprs))) {
    } else if (OB_ISNULL(get_stmt())) {
      ret = OB_ERR_UNEXPECTED;
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObDMLResolver::resolve_geo_mbr_column()
{
  int ret = OB_SUCCESS;
  // try to get mbr generated column
  for (int64_t j = 0; OB_SUCC(ret) && j < gen_col_exprs_.count(); ++j) {
    const ObRawExpr *dep_expr = gen_col_exprs_.at(j).dependent_expr_;
    if (dep_expr->get_expr_type() == T_FUN_SYS_SPATIAL_MBR) {
      ObColumnRefRawExpr *left_expr = NULL;
      ObDMLStmt *stmt = gen_col_exprs_.at(j).stmt_;
      TableItem *table_item = gen_col_exprs_.at(j).table_item_;
      const ObString &column_name = gen_col_exprs_.at(j).column_name_;
      ColumnItem *col_item = NULL;
      if (OB_ISNULL(table_item)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolve_basic_column_item(*table_item, column_name, true, col_item, stmt))) {
      } else if (OB_ISNULL(col_item) || OB_ISNULL(left_expr = col_item->expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column item is invalid", K(col_item), K(left_expr));
      } else {
        left_expr->set_explicited_reference();
        if (OB_FAIL(stmt->add_column_item(*col_item))) {
        }
      }
    }
  }
  return ret;
}



int ObDMLResolver::resolve_table_relation_factor(const peg::Node *node,
                                                 uint64_t &database_id,
                                                 ObString &table_name,
                                                 ObString &db_name,
                                                 bool &is_db_explicit)
{
  int ret = OB_SUCCESS;
  ObQueryCtx *query_ctx = NULL;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(get_stmt()) || OB_ISNULL(query_ctx = get_stmt()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    {
      if (OB_FAIL(resolve_table_relation_factor_normal(node,
                                                       database_id,
                                                       table_name,
                                                       db_name,
                                                       is_db_explicit))) {
      }
    }
  }
  return ret;
}


int ObDMLResolver::add_object_version_to_dependency(share::schema::ObDependencyTableType table_type,
                                                    share::schema::ObSchemaType schema_type,
                                                    uint64_t object_id,
                                                    uint64_t database_id,
                                                    uint64_t dep_obj_id,
                                                    bool is_db_expilicit)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_stmt()) || OB_ISNULL(schema_checker_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t schema_version = OB_INVALID_VERSION;
    ObSchemaObjVersion obj_version;
    bool is_pl_schema_type =
         PACKAGE_SCHEMA == schema_type
         || ROUTINE_SCHEMA == schema_type
         || TRIGGER_SCHEMA == schema_type;
    if (OB_FAIL(schema_checker_->get_schema_version(
        object_id,
        schema_type,
        schema_version))) {
    } else if (OB_UNLIKELY(OB_INVALID_VERSION == schema_version)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      obj_version.object_id_ = object_id;
      obj_version.object_type_ = table_type,
      obj_version.version_ = schema_version;
      obj_version.is_db_explicit_  = params_.is_in_view_ ? true : is_db_expilicit;
      uint64_t dep_db_id = database_id;
      if (OB_FAIL(get_stmt()->add_global_dependency_table(obj_version))) {
      } else if (OB_FAIL(get_stmt()->add_ref_obj_version(dep_obj_id, dep_db_id, ObObjectType::VIEW, obj_version, *allocator_))) {
      }
    }
  }
  return ret;
}
// Add object to schema version dependency collection
// When the object schema version changes, check dependencies to determine if the object needs to be regenerated

int ObDMLResolver::resolve_table_relation_factor_normal(const peg::Node *node,
                                                        uint64_t &database_id,
                                                        ObString &table_name,
                                                        ObString &db_name)
{
  bool is_db_explicit = false;
  UNUSED(is_db_explicit);
  return resolve_table_relation_factor_normal(node,
                                              database_id,
                                              table_name,
                                              db_name,
                                              is_db_explicit);
}

int ObDMLResolver::resolve_table_relation_factor_normal(const peg::Node *node,
                                                        uint64_t &database_id,
                                                        ObString &table_name,
                                                        ObString &db_name,
                                                        bool &is_db_explicit)
{
  int ret = OB_SUCCESS;
  database_id = OB_INVALID_ID;
  is_db_explicit = false;
  ObString out_db_name;
  ObString out_table_name;
  if (OB_FAIL(resolve_table_relation_node_v2(node, table_name, db_name, is_db_explicit))) {
  } else if (params_.is_resolve_fake_cte_table_) {
    database_id = OB_CTE_DATABASE_ID;
  } else if (OB_FAIL(schema_checker_->get_database_id(db_name, database_id))) {
    if (OB_SCHEMA_EAGAIN != ret) {
      ret = OB_ERR_BAD_DATABASE;
    }
  } else if (OB_FAIL(check_table_exist_or_not(database_id, table_name, db_name))) {
  }

  // table_name and db_name memory may come from schema, so deep copy them to SQL memory
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ob_write_string(*allocator_, table_name, out_table_name))) {
  } else if (OB_FAIL(ob_write_string(*allocator_, db_name, out_db_name))) {
  } else {
    table_name = out_table_name;
    db_name = out_db_name;
  }
  return ret;
}

int ObDMLResolver::check_table_exist_or_not(uint64_t &database_id,
                                            ObString &table_name,
                                            ObString &db_name)
{
  int ret = OB_SUCCESS;
  database_id = OB_INVALID_ID;
  if (OB_FAIL(schema_checker_->get_database_id(db_name, database_id))) {
    if (OB_SCHEMA_EAGAIN != ret) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("Invalid database name, database not exist", K(db_name));
    }
  } else {
    bool is_exist = false;
    bool select_index_enabled = false;
    const bool is_hidden = session_info_->is_table_name_hidden();
    if (OB_FAIL(session_info_->is_select_index_enabled(select_index_enabled))) {
    } else if ((select_index_enabled && is_select_resolver()) ||
               session_info_->get_ddl_info().is_ddl() ||
               session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility()) {
      if (OB_FAIL(schema_checker_->check_table_exists(db_name,
                                                      table_name,
                                                      false /*is_index_table*/,
                                                      is_hidden,
                                                      is_exist))) {
      } else if (!is_exist
                 && OB_FAIL(schema_checker_->check_table_exists(db_name,
                                                                table_name,
                                                                true /*is_index_table*/,
                                                                is_hidden,
                                                                is_exist,
                                                                false /*is_built_in_index*/))) {
      } else if (((select_index_enabled && is_select_resolver()) || session_info_->get_ddl_info().is_ddl()
                  || session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility())
                 && !is_exist) {
        if (OB_FAIL(schema_checker_->check_table_exists(db_name,
                                                        table_name,
                                                        true /*is_index_table*/,
                                                        is_hidden,
                                                        is_exist,
                                                        true /*is_built_in_index*/))) {
        }
      }
    } else {
      const bool is_index = false;
      if (OB_FAIL(schema_checker_->check_table_exists(db_name, table_name, is_index, is_hidden, is_exist))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_exist) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_INFO("table not exist",
                 K(database_id),
                 K(table_name),
                 KPHEX(table_name.ptr(), table_name.length()),
                 K(ret));
      }
    }
  }
  return ret;
}

int ObDMLResolver::check_json_table_column_constrain(ObDmlJtColDef *col_def)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(col_def)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObDataType &data_type = col_def->col_base_info_.data_type_;
    ObObjType obj_type = data_type.get_obj_type();
    if (obj_type != ObVarcharType && col_def->col_base_info_.truncate_) {
      ret = OB_ERR_UNSUPPORT_TRUNCATE_TYPE;
      LOG_USER_ERROR(OB_ERR_UNSUPPORT_TRUNCATE_TYPE);
    }
  }
  return ret;
}

int ObDMLResolver::resolve_function_table_column_item(const TableItem &table_item,
                                                      const common::ObObjMeta &meta_type,
                                                      const common::ObAccuracy &accuracy,
                                                      const ObString &column_name,
                                                      uint64_t column_id,
                                                      ColumnItem *&col_item)
{
  int ret = OB_SUCCESS;
  ObColumnRefRawExpr *col_expr = NULL;
  ColumnItem column_item;
  sql::ObRawExprResType result_type;
  ObDMLStmt *stmt = get_stmt();
  CK (OB_NOT_NULL(stmt));
  CK (OB_NOT_NULL(params_.expr_factory_));
  CK (OB_LIKELY(table_item.is_function_table()));
  OZ (params_.expr_factory_->create_raw_expr(T_REF_COLUMN, col_expr));
  CK (OB_NOT_NULL(col_expr));
  OX (col_expr->set_ref_id(table_item.table_id_, column_id));
  OX (result_type.set_meta(meta_type));
  OX (result_type.set_accuracy(accuracy));
  OX (col_expr->set_result_type(result_type));
  if (table_item.get_object_name().empty()) {
    OX (col_expr->set_column_name(column_name));
  } else {
    OX (col_expr->set_column_attr(table_item.get_object_name(), column_name));
  }
  OX (col_expr->set_database_name(table_item.database_name_));
  if (OB_SUCC(ret) && ob_is_enumset_tc(col_expr->get_data_type())) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "enum set in table function");
  }
  OX (column_item.expr_ = col_expr);
  OX (column_item.table_id_ = col_expr->get_table_id());
  OX (column_item.column_id_ = col_expr->get_column_id());
  OX (column_item.column_name_ = col_expr->get_column_name());
  OZ (col_expr->extract_info());
  OZ (stmt->add_column_item(column_item));
  OX (col_item = stmt->get_column_item(stmt->get_column_size() - 1));
  return ret;
}

int ObDMLResolver::resolve_function_table_column_item(const TableItem &table_item,
                                                      const ObString &column_name,
                                                      ColumnItem *&col_item)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  CK (OB_NOT_NULL(stmt));
  CK (OB_LIKELY(table_item.is_function_table()));
  if (OB_FAIL(ret)) {
    // do nothing ...
  } else if (NULL != (col_item = stmt->get_column_item(table_item.table_id_, column_name))) {
    //exist, ignore resolve...
  } else {
    ObSEArray<ColumnItem, 16> col_items;
    OZ (ObResolverUtils::check_function_table_column_exist(table_item,
                                                           params_,
                                                           column_name));
    OZ (resolve_function_table_column_item(table_item, col_items));
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_column_size(); ++i) {
      ColumnItem* column_item = stmt->get_column_item(i);
      CK (OB_NOT_NULL(column_item));
      if (OB_SUCC(ret)
          && column_item->table_id_ == table_item.table_id_
          && ObCharset::case_insensitive_equal(column_item->column_name_, column_name)) {
        col_item = column_item;
        break;
      }
    }
    if (OB_SUCC(ret) && OB_ISNULL(col_item)) {
      ret = OB_ERR_BAD_FIELD_ERROR;
    }
  }
  return ret;
}



int ObDMLResolver::resolve_json_table_check_dup_name(const ObJsonTableDef* table_def,
                                                     const ObString& column_name,
                                                     bool& exists)
{
  int ret = OB_SUCCESS;
  exists = false;
  if (OB_ISNULL(table_def)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (size_t i = 0; i < table_def->all_cols_.count(); ++i) {
      const ObJtColBaseInfo *info = table_def->all_cols_.at(i);
      if (OB_ISNULL(info)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (info->col_type_ == static_cast<int32_t>(NESTED_COL_TYPE)) {
      } else if (ObCharset::case_insensitive_equal(info->col_name_, column_name)) {
        exists = true;
        break;
      }
    }
  }

  return ret;
}
int ObDMLResolver::resolve_json_table_column_type(const peg::Node &column,
                                                  ObDataType &data_type,
                                                  ObDmlJtColDef *definition)
{
  int ret = OB_SUCCESS;
  data_type.set_collation_level(CS_LEVEL_IMPLICIT);
  if (peg::json_column_kind(column) == peg::JsonColumnKind::Ordinality) {
    data_type.set_int();
    data_type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObInt32Type]);
  } else {
    const bool decimal = config::_enable_convert_real_to_decimal();
    bool mysql_dates = false;
    OZ(ObSQLUtils::check_enable_mysql_compatible_dates(session_info_, false, mysql_dates));
    OZ(ObResolverUtils::resolve_data_type(peg::json_column_type(column),
        definition->col_base_info_.col_name_, data_type, false,
        session_info_->get_session_nls_params(), decimal, mysql_dates, decimal));
    if (OB_SUCC(ret)) {
      definition->col_base_info_.geometry_type_ = ObResolverUtils::resolve_geometry_type(
          peg::type_syntax(peg::json_column_type(column)).geometry);
      ObCharsetType charset = data_type.get_charset_type();
      ObCollationType collation = data_type.get_collation_type();
      if (collation == CS_TYPE_INVALID || charset == CHARSET_INVALID) {
        OZ(session_info_->get_collation_connection(collation));
        OZ(session_info_->get_character_set_connection(charset));
        data_type.set_charset_type(charset);
        data_type.set_collation_type(collation);
      }
      if (ob_is_json_tc(data_type.get_obj_type())) { data_type.set_collation_type(CS_TYPE_UTF8MB4_BIN); }
    }
  }
  return ret;
}

int ObDMLResolver::generate_json_table_output_column_item(TableItem *table_item,
                                                          const ObDataType &data_type,
                                                          const ObString &column_name,
                                                          int64_t column_id,
                                                          ColumnItem *&col_item)
{
  int ret = OB_SUCCESS;
  ObColumnRefRawExpr *col_expr = NULL;
  ColumnItem column_item;
  sql::ObRawExprResType result_type;
  ObDMLStmt *stmt = get_stmt();
  CK (OB_NOT_NULL(stmt));
  CK (OB_NOT_NULL(table_item));
  CK (OB_NOT_NULL(params_.expr_factory_));
  CK (OB_LIKELY(table_item->is_json_table()));
  OZ (params_.expr_factory_->create_raw_expr(T_REF_COLUMN, col_expr));
  CK (OB_NOT_NULL(col_expr));
  OX (col_expr->set_ref_id(table_item->table_id_, column_id));
  OX (result_type.set_meta(data_type.get_meta_type()));
  OX (result_type.set_accuracy(data_type.get_accuracy()));
  OX (col_expr->set_result_type(result_type));
  if (OB_FAIL(ret)) {
  } else if (table_item->get_object_name().empty()) {
    OX (col_expr->set_column_name(column_name));
  } else {
    OX (col_expr->set_column_attr(table_item->get_object_name(), column_name));
  }

  OX (col_expr->set_database_name(table_item->database_name_));
  if (OB_FAIL(ret)) {
  } else {
    col_expr->unset_result_flag(NOT_NULL_FLAG);
  }
  OX (column_item.expr_ = col_expr);
  OX (column_item.table_id_ = col_expr->get_table_id());
  OX (column_item.column_id_ = col_expr->get_column_id());
  OX (column_item.column_name_ = col_expr->get_column_name());
  OZ (col_expr->extract_info());
  OZ (stmt->add_column_item(column_item));
  OX (col_item = stmt->get_column_item(stmt->get_column_size() - 1));
  return ret;
}

static JsnValueType json_table_response(peg::JsonResponse response)
{
  switch (response) {
    case peg::JsonResponse::Error: return JSN_VALUE_ERROR;
    case peg::JsonResponse::Null: return JSN_VALUE_NULL;
    case peg::JsonResponse::Default: return JSN_VALUE_DEFAULT;
    default: return JSN_VALUE_IMPLICIT;
  }
}

int ObDMLResolver::resolve_json_table_regular_column(const peg::Node &column,
                                                     TableItem *table,
                                                     ObDmlJtColDef *&definition,
                                                     int32_t parent, int32_t &id,
                                                     int64_t &column_id)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  const auto kind = peg::json_column_kind(column);
  const auto &name = peg::json_column_name(column);
  const auto name_text = name.text();
  void *buffer = allocator_->alloc(sizeof(ObDmlJtColDef));
  ObDataType data_type;
  ColumnItem *item = nullptr;
  if (OB_ISNULL(buffer)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (name_text.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (syntax_text(name_text).ptr()[name_text.size() - 1] == ' ') {
    ret = OB_WRONG_COLUMN_NAME;
    LOG_USER_ERROR(OB_WRONG_COLUMN_NAME, static_cast<int32_t>(name_text.size()), syntax_text(name_text).ptr());
  } else {
    definition = new (buffer) ObDmlJtColDef();
    auto &info = definition->col_base_info_;
    OZ(ob_write_string(*allocator_, syntax_text(name_text), info.col_name_));
    info.is_name_quoted_ = peg::identifier_is_quoted(name);
    info.col_type_ = kind == peg::JsonColumnKind::Ordinality ? COL_TYPE_ORDINALITY
        : kind == peg::JsonColumnKind::Exists ? COL_TYPE_EXISTS : COL_TYPE_VALUE;
    if (kind != peg::JsonColumnKind::Ordinality) {
      OZ(resolve_str_const(peg::json_path(column), info.path_));
      const auto options = peg::json_options(column);
      info.on_error_ = json_table_response(options.on_error);
      info.on_empty_ = json_table_response(options.on_empty);
      info.truncate_ = options.truncate;
    }
    OZ(resolve_json_table_check_dup_name(table->json_table_def_, info.col_name_, exists));
    if (OB_SUCC(ret) && exists) { ret = OB_NON_UNIQ_ERROR; }
    OZ(resolve_json_table_column_type(column, data_type, definition));
    OZ(generate_json_table_output_column_item(table, data_type, info.col_name_, column_id, item));
    if (OB_SUCC(ret)) {
      info.parent_id_ = parent;
      info.id_ = id++;
      item->col_idx_ = table->json_table_def_->all_cols_.count();
      info.data_type_ = data_type;
      info.output_column_idx_ = column_id++;
      OZ(table->json_table_def_->all_cols_.push_back(&info));
      OZ(check_json_table_column_constrain(definition));
      if (OB_SUCC(ret) && kind == peg::JsonColumnKind::Value) {
        const auto options = peg::json_options(column);
        if (options.on_error == peg::JsonResponse::Default) {
          OZ(resolve_sql_expr(peg::json_error_default(column), item->default_value_expr_));
          OZ(item->default_value_expr_->deduce_type(session_info_));
        }
        if (options.on_empty == peg::JsonResponse::Default) {
          OZ(resolve_sql_expr(peg::json_empty_default(column), item->default_empty_expr_));
          OZ(item->default_empty_expr_->deduce_type(session_info_));
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_json_table_nested_column(const peg::Node &column,
                                                    TableItem *table,
                                                    ObDmlJtColDef *&definition,
                                                    int32_t parent, int32_t &id,
                                                    int64_t &column_id)
{
  int ret = OB_SUCCESS;
  void *buffer = allocator_->alloc(sizeof(ObDmlJtColDef));
  if (OB_ISNULL(buffer)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    definition = new (buffer) ObDmlJtColDef();
    definition->col_base_info_.col_type_ = NESTED_COL_TYPE;
    ObString path;
    OZ(resolve_str_const(peg::json_path(column), path));
    if (OB_SUCC(ret) && path.empty()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_SUCC(ret) && path.ptr()[0] != '$') {
      ObSqlString qualified;
      OZ(qualified.append_fmt("$.%.*s", path.length(), path.ptr()));
      OZ(ob_write_string(*allocator_, qualified.string(), definition->col_base_info_.path_));
    } else if (OB_SUCC(ret)) {
      definition->col_base_info_.path_ = path;
    }
    OZ(resolve_json_table_column_item(peg::json_nested_columns(column), table,
                                     definition, parent, id, column_id));
  }
  return ret;
}

int ObDMLResolver::resolve_json_table_column_item(rust::Slice<const peg::Node> columns,
                                                  TableItem *table,
                                                  ObDmlJtColDef *definition,
                                                  int32_t parent, int32_t &id,
                                                  int64_t &column_id)
{
  int ret = OB_SUCCESS;
  const int32_t current_id = id++;
  definition->col_base_info_.parent_id_ = parent;
  definition->col_base_info_.id_ = current_id;
  OZ(table->json_table_def_->all_cols_.push_back(&definition->col_base_info_));
  for (const auto &column : columns) {
    if (OB_FAIL(ret)) { break; }
    ObDmlJtColDef *child = nullptr;
    if (peg::json_column_kind(column) == peg::JsonColumnKind::Nested) {
      OZ(resolve_json_table_nested_column(column, table, child, current_id, id, column_id));
      OZ(definition->nested_cols_.push_back(child));
    } else {
      OZ(resolve_json_table_regular_column(column, table, child, current_id, id, column_id));
      OZ(definition->regular_cols_.push_back(child));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_json_table_column_item(const TableItem &table_item,
                                                  const ObString &column_name,
                                                  ColumnItem *&col_item)
{
  int ret = OB_SUCCESS;
  col_item = NULL;
  ObDMLStmt *stmt = get_stmt();
  common::ObCollationType cs_type = common::CS_TYPE_UTF8MB4_GENERAL_CI;
  CK (OB_NOT_NULL(stmt));
  CK (OB_LIKELY(table_item.is_json_table()));
  CK (OB_NOT_NULL(table_item.json_table_def_));
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_column_size(); ++i) {
    ColumnItem &item = stmt->get_column_items().at(i);
    if (table_item.table_id_ == item.table_id_
        && (0 == ObCharset::strcmp(cs_type, column_name, item.column_name_))) {
      if (OB_ISNULL(col_item)) {
        col_item = &item;
      } else {
        ret = OB_NON_UNIQ_ERROR;
        LOG_USER_ERROR(OB_NON_UNIQ_ERROR, column_name.length(), column_name.ptr(),
                        table_item.get_table_name().length(), table_item.get_table_name().ptr());
      }
    }
  } // end for
  if (OB_SUCC(ret) && OB_ISNULL(col_item)) {
    ret = OB_ERR_BAD_FIELD_ERROR;
  }
  return ret;
}

int ObDMLResolver::resolve_json_table_column_all_items(const TableItem &table_item,
                                                   ObIArray<ColumnItem> &col_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *table_expr = NULL;
  ObJsonTableDef* jt_def = NULL;

  CK (OB_LIKELY(table_item.is_json_table()));
  CK (OB_NOT_NULL(table_item.json_table_def_));
  ObDMLStmt* stmt = get_stmt();
  CK (OB_NOT_NULL(stmt));

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(jt_def = table_item.json_table_def_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (size_t i = 0; OB_SUCC(ret) && i < jt_def->all_cols_.count(); ++i) {
      ObJtColBaseInfo* col_info = jt_def->all_cols_.at(i);
      if (OB_ISNULL(col_info)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (col_info->col_type_ != NESTED_COL_TYPE) {
        ColumnItem* col_item = stmt->get_column_item_by_id(table_item.table_id_, col_info->output_column_idx_);
        CK (OB_NOT_NULL(col_item));
        if (OB_SUCC(ret) && OB_FAIL(col_items.push_back(*col_item))) {
        }
      }
    }
  }

  return ret;
}

int ObDMLResolver::resolve_function_table_column_item(const TableItem &table_item,
                                                      ObIArray<ColumnItem> &col_items)
{
  int ret = OB_SUCCESS;
  CK (OB_LIKELY(table_item.is_function_table()));
  CK (OB_NOT_NULL(table_item.function_table_expr_));
  if (OB_SUCC(ret)) {
    if (table_item.function_table_expr_->get_result_type().is_ext()) {
      ret = resolve_function_table_column_item_udf(table_item, col_items);
    } else {
      ret = resolve_function_table_column_item_sys_func(table_item, col_items);
    }
  }
  return ret;
}

int ObDMLResolver::resolve_function_table_column_item_udf(const TableItem &table_item,
                                                          ObIArray<ColumnItem> &col_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *table_expr = NULL;
  ColumnItem *col_item = NULL;
  ObDMLStmt *stmt = get_stmt();
  const ObUserDefinedType *user_type = NULL;

  CK (OB_NOT_NULL(stmt));

  CK (OB_LIKELY(table_item.is_function_table()));
  CK (OB_NOT_NULL(table_expr = table_item.function_table_expr_));
  OZ (table_expr->deduce_type(session_info_));
  CK (table_expr->get_udt_id() != OB_INVALID_ID);

  CK (OB_NOT_NULL(schema_checker_))
  ObPLPackageGuard package_guard{};
  OZ (ObResolverUtils::get_user_type(
    params_.allocator_, params_.session_info_, params_.plan_cache_, params_.sql_proxy_,
    schema_checker_->get_schema_guard(),
    package_guard,
    table_expr->get_udt_id(), user_type));
  CK (OB_NOT_NULL(user_type));
  if (OB_SUCC(ret) && !user_type->is_collection_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "udf return type is not table type in function table");
  }
  const ObCollectionType *coll_type = NULL;
  CK (OB_NOT_NULL(coll_type = static_cast<const ObCollectionType*>(user_type)));
  if (OB_SUCC(ret)
      && !coll_type->get_element_type().is_obj_type()
      && !coll_type->get_element_type().is_record_type()
      && !coll_type->get_element_type().is_collection_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "current udt type");
  }
  // The element type of the array is a primitive type
  if (OB_SUCC(ret) && coll_type->get_element_type().is_obj_type()) {
    CK (OB_NOT_NULL(coll_type->get_element_type().get_data_type()));
    if (OB_FAIL(ret)) { // do nothing ...
    } else if (NULL != (col_item = stmt->get_column_item(table_item.table_id_, ObString("COLUMN_VALUE")))) {
      //exist, ignore resolve...
    } else {
      OZ (resolve_function_table_column_item(table_item,
                                            coll_type->get_element_type().get_data_type()->get_meta_type(),
                                            coll_type->get_element_type().get_data_type()->get_accuracy(),
                                            ObString("COLUMN_VALUE"),
                                            OB_APP_MIN_COLUMN_ID,
                                            col_item));
    }
    CK (OB_NOT_NULL(col_item));
    OZ (col_items.push_back(*col_item));
  }

  // The element type of the array is Object, at this time multiple columns should be output
  if (OB_SUCC(ret) && coll_type->get_element_type().is_record_type()) {
    ObPLPackageGuard package_guard{};
    const ObRecordType *record_type = NULL;
    const ObUserDefinedType *user_type = NULL;
    CK (OB_NOT_NULL(schema_checker_))
    OZ (ObResolverUtils::get_user_type(
      params_.allocator_, params_.session_info_, params_.plan_cache_, params_.sql_proxy_,
      schema_checker_->get_schema_guard(),
      package_guard,
      coll_type->get_element_type().get_user_type_id(),
      user_type));
    CK (OB_NOT_NULL(user_type));
    if (OB_SUCC(ret) && !user_type->is_record_type()) {
      ret = OB_ERR_UNEXPECTED;
    }
    CK (OB_NOT_NULL(record_type = static_cast<const ObRecordType *>(user_type)));
    for (int64_t i = 0; OB_SUCC(ret) && i < record_type->get_member_count(); ++i) {
      const ObPLDataType *pl_type = record_type->get_record_member_type(i);
      ObString column_name;
      ObAccuracy accuracy = ObAccuracy(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET);
      common::ObObjMeta meta;
      OX (col_item = NULL);
      CK (OB_NOT_NULL(pl_type));
      CK (OB_NOT_NULL(record_type->get_record_member_name(i)));
      OZ (ob_write_string(*(params_.allocator_), *(record_type->get_record_member_name(i)), column_name));
      CK (OB_NOT_NULL(column_name));
      if (OB_SUCC(ret)) {
        if (pl_type->is_obj_type()) {
          CK (OB_NOT_NULL(pl_type->get_data_type()));
          OX (meta = pl_type->get_data_type()->get_meta_type());
          OX (accuracy.set_accuracy(pl_type->get_data_type()->get_accuracy()));
        } else {
          OX (meta.set_ext());
          OX (meta.set_extend_type(pl_type->get_type()));
          OX (accuracy.set_accuracy(pl_type->get_user_type_id()));
        }
      }

      if (OB_FAIL(ret)) { // do nothing ...
      } else if (NULL != (col_item = stmt->get_column_item(table_item.table_id_, column_name))) {
        //exist, ignore resolve...
      } else {
        OZ (resolve_function_table_column_item(table_item,
                                               meta,
                                               accuracy,
                                               column_name,
                                               OB_APP_MIN_COLUMN_ID + i,
                                               col_item));
      }
      CK (OB_NOT_NULL(col_item));
      OZ (col_items.push_back(*col_item));
    }
  }

  // The array element type is collection
  if (OB_SUCC(ret) && coll_type->get_element_type().is_collection_type()) {
    if (NULL != (col_item = stmt->get_column_item(table_item.table_id_, ObString("COLUMN_VALUE")))) {
      //exist, ignore resolve...
    } else {
      ObAccuracy accuracy = ObAccuracy(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET);
      common::ObObjMeta meta;
      accuracy.set_accuracy(coll_type->get_element_type().get_user_type_id());
      meta.set_ext();
      meta.set_extend_type(coll_type->get_element_type().get_type());
      OZ (resolve_function_table_column_item(table_item,
                                             meta,
                                             accuracy,
                                             ObString("COLUMN_VALUE"),
                                             OB_APP_MIN_COLUMN_ID,
                                             col_item));
    }
    CK (OB_NOT_NULL(col_item));
    OZ (col_items.push_back(*col_item));
  }

  return ret;
}

int ObDMLResolver::resolve_function_table_column_item_sys_func(const TableItem &table_item,
                                                               ObIArray<ColumnItem> &col_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *table_expr = NULL;
  ColumnItem *col_item = NULL;
  ObDMLStmt *stmt = get_stmt();
  const ObUserDefinedType *user_type = NULL;

  CK (OB_NOT_NULL(stmt));
  CK (OB_LIKELY(table_item.is_function_table()));
  CK (OB_NOT_NULL(table_expr = table_item.function_table_expr_));
  OZ (table_expr->deduce_type(session_info_));
  if (OB_FAIL(ret)) { // do nothing ...
  } else if (!ObResolverUtils::is_expr_can_be_used_in_table_function(*table_expr)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "access rows from a non-nested table item");
  } else if (NULL != (col_item = stmt->get_column_item(table_item.table_id_, ObString("COLUMN_VALUE")))) {
    //exist, ignore resolve...
  } else {
    OZ (resolve_function_table_column_item(table_item,
                                           table_expr->get_result_meta(),
                                           table_expr->get_accuracy(),
                                           ObString("COLUMN_VALUE"),
                                           OB_APP_MIN_COLUMN_ID,
                                           col_item));
  }
  CK (OB_NOT_NULL(col_item));
  OZ (col_items.push_back(*col_item));
  return ret;
}

//
// columns with is json constraint should set_strict_json_column > 00

int ObDMLResolver::resolve_generated_table_column_item(const TableItem &table_item,
                                                       const common::ObString &column_name,
                                                       ColumnItem *&col_item,
                                                       ObDMLStmt *stmt /* = NULL */,
                                                       const uint64_t column_id /* = OB_INVALID_ID */,
                                                       const int64_t select_item_offset /* = 0 */,
                                                       const bool skip_check /* = false */)
{
  int ret = OB_SUCCESS;
  ObColumnRefRawExpr *col_expr = NULL;
  ColumnItem column_item;
  bool with_is_json_constraint = false;
  if (NULL == stmt) {
    stmt = get_stmt();
  }
  if (OB_ISNULL(stmt) || OB_ISNULL(schema_checker_) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!table_item.is_generated_table() &&
                         !table_item.is_fake_cte_table() &&
                         !table_item.is_temp_table() &&
                         !table_item.is_lateral_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not generated table", K_(table_item.type));
  }
  bool found = false;
  if (OB_SUCC(ret) && !(skip_check)) {
    if (OB_INVALID_ID != column_id) {
      col_item = stmt->get_column_item_by_id(table_item.table_id_, column_id);
    } else {
      col_item = stmt->get_column_item(table_item.table_id_, column_name);
    }
    found = NULL != col_item;
  }
  if (OB_SUCC(ret) && !found) {
    ObSelectStmt *ref_stmt = table_item.ref_query_;
    bool is_break = false;
    if (OB_ISNULL(ref_stmt)) {
      ret = OB_NOT_INIT;
    }

    int64_t i = select_item_offset;
    for (; OB_SUCC(ret) && !is_break && i < ref_stmt->get_select_item_size(); ++i) {
      SelectItem &ref_select_item = ref_stmt->get_select_item(i);
      if (column_id != OB_INVALID_ID
            ? i + OB_APP_MIN_COLUMN_ID == column_id
            : ObCharset::case_insensitive_equal(column_name, ref_select_item.alias_name_)) {
        ObRawExpr *select_expr = ref_select_item.expr_;
        if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_REF_COLUMN, col_expr))) {
        } else if (OB_ISNULL(select_expr) || OB_ISNULL(col_expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(select_expr->deduce_type(session_info_))) {
        } else {
          //because of view table, generated table item may be has database_name and table name,
          //also alias name maybe be empty
          col_expr->set_ref_id(table_item.table_id_, i + OB_APP_MIN_COLUMN_ID);
          col_expr->set_result_type(select_expr->get_result_type());
          if (table_item.is_view_table_ && col_expr->get_result_type().is_string_type() && CS_LEVEL_EXPLICIT != col_expr->get_collation_level()) {
            col_expr->set_collation_level(CS_LEVEL_IMPLICIT);
          }
          col_expr->set_column_attr(table_item.get_table_name(), ref_select_item.alias_name_);
          col_expr->set_database_name(table_item.database_name_);
          //set enum_set_values
          if (ob_is_geometry(select_expr->get_data_type()) && !select_expr->is_column_ref_expr()) {
            col_expr->set_srs_id(OB_DEFAULT_COLUMN_SRS_ID);
          } else if (ObSQLUtils::check_json_expr(*select_expr)) {
            with_is_json_constraint = true;
          }
          is_break = true;

          if (OB_FAIL(ret)) {
            //do nothing
          } else if (OB_FAIL(ObResolverUtils::resolve_default_value_and_expr_from_select_item(ref_select_item,
                                                                                     column_item,
                                                                                     ref_stmt))) {
            if (ret == OB_ERR_BAD_FIELD_ERROR) {
              // ignore the NOT_FOUND error, since it might be rowid.
              ret = OB_SUCCESS;
            }
          } else if (OB_FAIL(erase_redundant_generated_table_column_flag(*ref_stmt, select_expr,
                                                                         *col_expr))) {
          } else {
            if (select_expr->is_column_ref_expr()) {
              ObColumnRefRawExpr *col_ref = static_cast<ObColumnRefRawExpr *>(select_expr);
              {
                col_expr->set_joined_dup_column(col_ref->is_joined_dup_column());
                col_expr->set_lob_column(col_ref->is_lob_column());
                col_expr->set_srs_id(col_ref->get_srs_id());
                col_expr->set_udt_set_id(col_ref->get_udt_set_id());
                if (stmt->get_stmt_type() == stmt::T_INSERT || stmt->get_stmt_type() == stmt::T_UPDATE) {
                  col_expr->set_hidden_column(col_ref->is_hidden_column());
                }
                ColumnItem *item = ref_stmt->get_column_item_by_id(col_ref->get_table_id(), col_ref->get_column_id());
                if (OB_ISNULL(item)) {
                  ret = OB_ERR_UNEXPECTED;
                } else {
                  column_item.base_tid_ = item->base_tid_;
                  column_item.base_cid_ = item->base_cid_;
                  column_item.is_geo_ = item->is_geo_;
                }
              }
            }
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_ISNULL(col_expr)) {
      ret = OB_ERR_BAD_FIELD_ERROR;
    } else {/*do nothing*/}
    //init column item
    if (OB_SUCC(ret)) {
      if (OB_NOT_NULL(col_expr)) {
        col_expr->set_strict_json_column(with_is_json_constraint);
      }
      column_item.expr_ = col_expr;
      column_item.table_id_ = col_expr->get_table_id();
      column_item.column_id_ = col_expr->get_column_id();
      column_item.column_name_ = col_expr->get_column_name();
      if (OB_FAIL(col_expr->extract_info())) {
      } else if (OB_FAIL(stmt->add_column_item(column_item))) {
      } else {
        col_item = stmt->get_column_item(stmt->get_column_size() - 1);
      }
    }
  }
  return ret;
}

int ObDMLResolver::erase_redundant_generated_table_column_flag(const ObSelectStmt &ref_stmt,
                                                                  const ObRawExpr *ref_expr,
                                                                  ObColumnRefRawExpr &col_expr) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ref_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ref_expr->is_column_ref_expr()) {
    bool is_null = false;
    const ObColumnRefRawExpr &ref_col_expr = static_cast<const ObColumnRefRawExpr&>(*ref_expr);
    if (OB_FAIL(ObOptimizerUtil::is_table_on_null_side(&ref_stmt, ref_col_expr.get_table_id(), is_null))) {
    } else if (is_null) {
      col_expr.unset_result_flag(NOT_NULL_FLAG);
      col_expr.unset_result_flag(AUTO_INCREMENT_FLAG);
      col_expr.unset_result_flag(PRI_KEY_FLAG);
      col_expr.unset_result_flag(PART_KEY_FLAG);
      col_expr.unset_result_flag(MULTIPLE_KEY_FLAG);
    }
  }
  return ret;
}

int ObDMLResolver::build_prefix_index_compare_expr(ObRawExpr &column_expr,
                                                   ObRawExpr *prefix_expr,
                                                   ObItemType type,
                                                   ObRawExpr &value_expr,
                                                   ObRawExpr *escape_expr,
                                                   ObRawExpr *&new_op_expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *substr_expr = NULL;
  if (T_OP_LIKE == type) {
    //build value substr expr
    ObOpRawExpr *like_expr = NULL;
    if (OB_FAIL(ObRawExprUtils::create_substr_expr(*params_.expr_factory_,
                                                   params_.session_info_,
                                                   &value_expr,
                                                   prefix_expr->get_param_expr(1),
                                                   prefix_expr->get_param_expr(2),
                                                   substr_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_like_expr(*params_.expr_factory_,
                                                       params_.session_info_,
                                                       &column_expr,
                                                       substr_expr,
                                                       escape_expr,
                                                       like_expr))) {
    } else {
      new_op_expr = like_expr;
    }
  } else {
    ObRawExpr *right_expr = NULL;
    if (T_OP_IN == type) {
      ObOpRawExpr *row_expr = NULL;
      if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_OP_ROW, row_expr))) {
      } else if (OB_ISNULL(row_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(row_expr->init_param_exprs(value_expr.get_param_count()))) {
      } else {
        right_expr = row_expr;
        for (int64_t k = 0; OB_SUCC(ret) && k < value_expr.get_param_count(); ++k) {
          if (OB_FAIL(ObRawExprUtils::create_substr_expr(*params_.expr_factory_,
                                                         params_.session_info_,
                                                         value_expr.get_param_expr(k),
                                                         prefix_expr->get_param_expr(1),
                                                         prefix_expr->get_param_expr(2),
                                                         substr_expr))) {
          } else if (OB_FAIL(row_expr->add_param_expr(substr_expr))) {
          }
        }
      }
    } else {
      if (OB_FAIL(ObRawExprUtils::create_substr_expr(*params_.expr_factory_,
                                                     params_.session_info_,
                                                     &value_expr,
                                                     prefix_expr->get_param_expr(1),
                                                     prefix_expr->get_param_expr(2),
                                                     substr_expr))) {
      } else {
        right_expr = substr_expr;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObRawExprUtils::create_double_op_expr(*params_.expr_factory_,
                                                        params_.session_info_,
                                                        type,
                                                        new_op_expr,
                                                        &column_expr,
                                                        right_expr))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_sample_clause(const peg::Node *sample_node,
                                         TableItem &table_item)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *stmt;
  if (OB_ISNULL(get_stmt()) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!get_stmt()->is_select_stmt())) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "sampling in dml statement");
  } else {
    stmt = static_cast<ObSelectStmt *>(get_stmt());
    void *buf = allocator_->alloc(sizeof(SampleInfo));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      SampleInfo *samp_ptr = new(buf)SampleInfo();
      SampleInfo &sample_info = *samp_ptr;
      sample_info.table_id_ = table_item.table_id_;
      if (peg::sample_method(*sample_node) == peg::SampleMethod::Block) {
        sample_info.method_ = SampleInfo::BLOCK_SAMPLE;
      } else if (peg::sample_method(*sample_node) == peg::SampleMethod::Hybrid) {
        sample_info.method_ = SampleInfo::HYBRID_SAMPLE;

      } else {
        sample_info.method_ = SampleInfo::ROW_SAMPLE;
      }

      sample_info.percent_ = 0;
      const auto &percent = peg::sample_percent(*sample_node);
      const auto percent_text = percent.text();
      const ObString spelling(syntax_text(percent_text));
      if (percent.kind() == peg::NodeKind::SampleInt || percent.kind() == peg::NodeKind::Uint) {
        sample_info.percent_ = static_cast<double>(peg::integer_literal_value(percent));
      } else {
        OZ(ObOptEstObjToScalar::convert_string_to_scalar_for_number(spelling, sample_info.percent_));
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else {
        if (peg::sample_has_seed(*sample_node)) {
          uint64_t seed = 0;
          OZ(ObResolverUtils::resolve_unsigned_integer(peg::sample_seed(*sample_node), "SAMPLE SEED", seed));
          if (OB_SUCC(ret) && seed > INT64_MAX) {
            ret = OB_INVALID_ARGUMENT;
            _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "SAMPLE SEED exceeds the signed integer range");
          }
          OX(sample_info.seed_ = static_cast<int64_t>(seed));
        } else {
          // seed is set to -1 when not provided and we will pick a random seed in this case.
          sample_info.seed_ = -1;
        }
        // resolve sample scope
        if (OB_FAIL(ret)) {
          // do nothing
        } else if (peg::sample_scope(*sample_node) == peg::SampleScope::All) {
          sample_info.scope_ = SampleInfo::SampleScope::SAMPLE_ALL_DATA;
        } else if (peg::sample_scope(*sample_node) == peg::SampleScope::Base) {
          sample_info.scope_ = SampleInfo::SampleScope::SAMPLE_BASE_DATA;
        } else if (peg::sample_scope(*sample_node) == peg::SampleScope::Incremental) {
          sample_info.scope_ = SampleInfo::SampleScope::SAMPLE_INCR_DATA;
        } else {
          ret = OB_ERR_UNEXPECTED;
        }
        if (OB_FAIL(ret)) {
          // do nothing
        } else {
          table_item.sample_info_ = samp_ptr;
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (sample_info.percent_ < 0.000001 || sample_info.percent_ >= 100.0) {
        ret = OB_ERR_INVALID_SAMPLING_RANGE;
      } else if (sample_info.seed_ != -1 && (sample_info.seed_ > (4294967295))) {
        // Official documentation limit [0, 4294967295(2^32 - 1)]
        // Current behavior only requires the seed value to be non-negative.
        // Here log the record once
        LOG_WARN("seed value out of range");
      }
    }
  }

  return ret;
}

int ObDMLResolver::generate_ddl_sample_info_if_needed(TableItem &table_item)
{
  int ret = OB_SUCCESS;
  if (stmt_->is_select_stmt() &&
      session_info_->get_ddl_info().is_ddl() &&
      params_.resolver_scope_stmt_type_ == T_INSERT &&
      !session_info_->get_ddl_info().is_heap_table_ddl()) {
    void *buf = allocator_->alloc(sizeof(SampleInfo));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      SampleInfo *sample_info = new(buf) SampleInfo();
      sample_info->method_ = SampleInfo::SampleMethod::DDL_BLOCK_SAMPLE;
      sample_info->scope_ = SampleInfo::SAMPLE_ALL_DATA;
      int64_t px_object_sample_rate = 0;

      px_object_sample_rate = config::_px_object_sampling();

      sample_info->percent_ = (double)px_object_sample_rate / 1000;
      sample_info->table_id_ = table_item.table_id_;
      // Check if target table is FTS or vector index via upper_insert_resolver_
      if (OB_NOT_NULL(upper_insert_resolver_)) {
        const ObTableSchema *ddl_table_schema = NULL;
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = upper_insert_resolver_->get_ddl_schema_in_insert_into_select_clause(ddl_table_schema))) {
        } else if (OB_NOT_NULL(ddl_table_schema) &&
                   (ddl_table_schema->is_fts_index() || ddl_table_schema->is_vec_index())) {
          sample_info->method_ = SampleInfo::SampleMethod::HYBRID_SAMPLE;
        }
      }
      table_item.sample_info_ = sample_info;
    }
  }
  return ret;
}

int ObDMLResolver::generate_check_constraint_exprs(const TableItem *table_item,
                                                   const share::schema::ObTableSchema *table_schema,
                                                   ObIArray<ObRawExpr*> &check_exprs,
                                                   ObIArray<int64_t> *check_flags/*default null*/)
{
  int ret = OB_SUCCESS;
  ParsedSql expression;
  ObDMLStmt *dml_stmt = static_cast<ObDMLStmt*>(stmt_);
  bool resolve_check_for_optimizer = (check_flags != NULL);
  ObSEArray<ObRawExpr *,4> tmp_check_constraint_exprs;

  if (OB_ISNULL(params_.session_info_) || OB_ISNULL(params_.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (ObTableSchema::const_constraint_iterator iter = table_schema->constraint_begin(); OB_SUCC(ret) &&
         iter != table_schema->constraint_end(); iter ++) {
      ObRawExpr *check_constraint_expr = NULL;
      ObConstraint tmp_constraint;
      ObSEArray<ObQualifiedName, 1> columns;
      ObString constraint_str;
      bool has_default;
      if ((*iter)->get_constraint_type() != CONSTRAINT_TYPE_CHECK
          && (*iter)->get_constraint_type() != CONSTRAINT_TYPE_NOT_NULL) {
        continue;
      } else if (!(*iter)->get_enable_flag() &&
                 (*iter)->is_validated() &&
                 !resolve_check_for_optimizer) {
        const ObSimpleDatabaseSchema *database_schema = NULL;
        if (OB_ISNULL(params_.schema_checker_->get_schema_guard())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(params_.schema_checker_->get_schema_guard()->get_database_schema( table_schema->get_database_id(), database_schema))) {
        } else if (OB_ISNULL(database_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObString &origin_database_name = database_schema->get_database_name_str();
          if (origin_database_name.empty() || (*iter)->get_constraint_name_str().empty()) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            ret = OB_ERR_CONSTRAINT_CONSTRAINT_DISABLE_VALIDATE;
            LOG_USER_ERROR(OB_ERR_CONSTRAINT_CONSTRAINT_DISABLE_VALIDATE,
                           origin_database_name.length(), origin_database_name.ptr(),
                           (*iter)->get_constraint_name_str().length(), (*iter)->get_constraint_name_str().ptr());
          }
        }
      } else if ((*iter)->get_constraint_type() == CONSTRAINT_TYPE_NOT_NULL) {
        continue;
      } else if (!(*iter)->get_enable_flag() &&
                 (*iter)->is_no_validate() &&
                 (!resolve_check_for_optimizer || !(*iter)->get_rely_flag())) {
        continue;
      } else if (OB_FAIL(ob_write_string(*params_.allocator_, (*iter)->get_check_expr_str(), constraint_str))) {
      } else if (OB_FAIL(ObSQLUtils::convert_sql_text_from_schema_for_resolve(
                 *params_.allocator_, params_.session_info_->get_dtc_params(), constraint_str))) {
      } else if (OB_FAIL(ObRawExprUtils::parse_bool_expr_node_from_str(
                 constraint_str, *(params_.allocator_), expression))) {
      } else if (OB_FAIL(ObResolverUtils::resolve_check_constraint_expr(
                 params_, *expression.statement(), expression.source(), *table_schema, tmp_constraint, check_constraint_expr, NULL, &columns))) {
      } else if (OB_FAIL(resolve_special_expr_static(table_schema, *params_.session_info_, *params_.expr_factory_, check_constraint_expr, has_default, ObResolverUtils::PureFunctionCheckStatus::DISABLE_CHECK))) {
      } else if (table_item->is_basic_table() &&
                 OB_FAIL(resolve_columns_for_partition_expr(check_constraint_expr,
                                                            columns,
                                                            *table_item,
                                                            false))) {
      } else if (OB_FAIL(tmp_check_constraint_exprs.push_back(check_constraint_expr))) {
      } else if (resolve_check_for_optimizer) {
        int64_t check_flag = 0;
        if ((*iter)->get_enable_flag()) {
          check_flag |= CheckConstraintFlag::IS_ENABLE_CHECK;
        }
        if ((*iter)->is_validated()) {
          check_flag |= CheckConstraintFlag::IS_VALIDATE_CHECK;
        }
        if ((*iter)->get_rely_flag()) {
          check_flag |= CheckConstraintFlag::IS_RELY_CHECK;
        }
        if (OB_FAIL(check_flags->push_back(check_flag))) {
        }
      }
    }
  }
  if (OB_SUCC(ret) && !tmp_check_constraint_exprs.empty()) {
    if (OB_FAIL(deduce_generated_exprs(tmp_check_constraint_exprs))) {
    } else if (OB_FAIL(append(check_exprs, tmp_check_constraint_exprs))) {
    }
  }
  return ret;
}

int ObDMLResolver::check_disable_parallel_state(ObRawExpr *expr)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  CK (OB_NOT_NULL(expr));
  CK (OB_NOT_NULL(stmt));

  if (OB_FAIL(ret)) {
  } else if (T_FUN_UDF == expr->get_expr_type()) {
    uint64_t database_id = OB_INVALID_ID;
    ObUDFRawExpr *udf_expr = static_cast<ObUDFRawExpr*>(expr);
    ObSEArray<ObSchemaObjVersion, 1> return_value_version;
    CK (OB_NOT_NULL(udf_expr));
    OZ (ObResolverUtils::set_parallel_info(*params_.session_info_,
                                            *params_.schema_checker_->get_schema_guard(),
                                            *expr,
                                            *stmt->get_query_ctx()));
    OX (stmt->get_query_ctx()->disable_udf_parallel_ |= !udf_expr->is_parallel_enable());
    OX (stmt_->get_query_ctx()->has_pl_udf_ = true);
    if (OB_SUCC(ret) &&
        T_FIELD_LIST_SCOPE == current_scope_ &&
        udf_expr->get_result_type().is_ext() &&
        (pl::PL_RECORD_TYPE == udf_expr->get_result_type().get_extend_type() ||
          pl::PL_NESTED_TABLE_TYPE == udf_expr->get_result_type().get_extend_type() ||
          pl::PL_VARRAY_TYPE == udf_expr->get_result_type().get_extend_type() ||
          (pl::PL_OPAQUE_TYPE == udf_expr->get_result_type().get_extend_type() &&
          (udf_expr->get_result_type().get_udt_id() == 300004 || udf_expr->get_result_type().get_udt_id() == 300005)))) {
      OX (stmt->get_query_ctx()->disable_udf_parallel_ |= true);
    }
  } else if (T_FUN_PL_OBJECT_CONSTRUCT == expr->get_expr_type()) {
    if (T_FIELD_LIST_SCOPE == current_scope_) {
      OX (stmt_->get_query_ctx()->disable_udf_parallel_ |= true);
    }
  } else if (T_FUN_PL_COLLECTION_CONSTRUCT == expr->get_expr_type()) {
    if (T_FIELD_LIST_SCOPE == current_scope_) {
      OX (stmt_->get_query_ctx()->disable_udf_parallel_ |= true);
    }
  } else if (T_OBJ_ACCESS_REF == expr->get_expr_type()) {
    OX (stmt->get_query_ctx()->disable_udf_parallel_ |= true);
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
    OZ (SMART_CALL(check_disable_parallel_state(expr->get_param_expr(i))));
  }

  return ret;
}

int ObDMLResolver::resolve_external_name(ObQualifiedName &q_name,
                                         ObIArray<ObQualifiedName> &columns,
                                         ObIArray<ObRawExpr*> &real_exprs,
                                         ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  /*
   * Here we do not check if params_.secondary_namespace_ is empty, if NULL == params_.secondary_namespace_,
   * it means it is called from a pure SQL context, for example select f(1) from dual; where f is a pl function,
   * in this case we only need the schema to handle it
   */
  CK(OB_NOT_NULL(params_.allocator_));
  CK(OB_NOT_NULL(params_.expr_factory_));
  CK(OB_NOT_NULL(params_.session_info_));
  CK(OB_NOT_NULL(params_.schema_checker_));
  CK(OB_NOT_NULL(params_.schema_checker_->get_schema_guard()));
  if (OB_SUCC(ret) && OB_ISNULL(params_.sql_proxy_)) {
    CK (OB_NOT_NULL(params_.sql_proxy_ = GCTX.sql_proxy_));
  }

  if (OB_SUCC(ret)) {
    ObArray<ObSchemaObjVersion> dependency_objects;
    if (OB_FAIL(ObResolverUtils::resolve_external_symbol(*params_.allocator_,
                                                         *params_.expr_factory_,
                                                         *params_.session_info_,
                                                         *params_.schema_checker_->get_schema_guard(),
                                                         params_.sql_proxy_,
                                                         &(params_.external_param_info_),
                                                         params_.secondary_namespace_,
                                                         q_name,
                                                         columns,
                                                         real_exprs,
                                                         expr,
                                                         params_.package_guard_,
                                                         params_.param_list_,
                                                         params_.is_prepare_protocol_,
                                                         false, /*is_check_mode*/
                                                         true /*is_sql_scope*/,
                                                         &dependency_objects))) {
      LOG_WARN_IGNORE_COL_NOTFOUND(ret, "failed to resolve var", K(q_name), K(ret));
    } else if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (expr->is_udf_expr()) {
      //for udf without params, we just set called_in_sql = true,
      //if this expr go through pl :: build_raw_expr later,
      //the flag will change to false;
      OX (expr->set_is_called_in_sql(true));
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < dependency_objects.count(); ++i) {
        OZ (stmt_->add_global_dependency_table(dependency_objects.at(i)));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < dependency_objects.count(); ++i) {
        uint64_t db_id = OB_INVALID_ID;
        
        int64_t schema_version = OB_INVALID_VERSION;
        ObSchemaObjVersion &ver = dependency_objects.at(i);
        OZ (params_.schema_checker_->get_schema_guard()->get_schema_version(ver.get_schema_type(),
                                                                            ver.get_object_id(),
                                                                            schema_version,
                                                                            &db_id));
        CK (db_id != OB_INVALID_ID);
        OZ (stmt_->add_ref_obj_version(view_ref_id_, db_id, ObObjectType::VIEW, dependency_objects.at(i), *allocator_));
      }
    }
    OZ (check_disable_parallel_state(expr));
  }
  if (OB_ERR_SP_UNDECLARED_VAR == ret) {
    ret = OB_ERR_BAD_FIELD_ERROR;
  }
  return ret;
}

int ObDMLResolver::add_cte_table_to_children(ObChildStmtResolver& child_resolver, const ObString *cur_rcte_name/* = NULL*/) {
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < current_cte_tables_.count(); i++) {
    bool exist = false;
    if (OB_ISNULL(current_cte_tables_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (NULL != cur_rcte_name &&
               OB_FAIL(check_table_name_equal(current_cte_tables_[i]->table_name_, *cur_rcte_name, exist))) {
    } else if (exist) {
      // do nothing
    } else if (OB_FAIL(child_resolver.add_parent_cte_table_item(current_cte_tables_.at(i)))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < parent_cte_tables_.count(); i++) {
    bool exist = false;
    if (OB_ISNULL(parent_cte_tables_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (NULL != cur_rcte_name &&
               OB_FAIL(check_table_name_equal(parent_cte_tables_[i]->table_name_, *cur_rcte_name, exist))) {
    } else if (exist) {
      // do nothing
    } else if (OB_FAIL(child_resolver.add_parent_cte_table_item(parent_cte_tables_.at(i)))) {
    }
  }
  return ret;
}

int ObDMLResolver::resolve_pseudo_column(
    const ObQualifiedName &q_name,
    ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  if (0 == q_name.col_name_.case_compare("ORA_ROWSCN")) {
    if (OB_FAIL(resolve_ora_rowscn_pseudo_column(q_name, real_ref_expr))) {
    }
  } else {
    ret = OB_ERR_BAD_FIELD_ERROR;
  }
  return ret;
}

int ObDMLResolver::resolve_ora_rowscn_pseudo_column(
    const ObQualifiedName &q_name,
    ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  ObPseudoColumnRawExpr *pseudo_column_expr = NULL;
  const TableItem *table_item = NULL;
  if (OB_FAIL(column_namespace_checker_.check_table_column_namespace(q_name, table_item))) {
  } else if (OB_ISNULL(table_item)) {
    ret = OB_ERR_BAD_FIELD_ERROR;
  } else if (OB_FAIL(get_stmt()->get_target_pseudo_column(T_ORA_ROWSCN,
                                                          table_item->table_id_,
                                                          pseudo_column_expr))) {
  } else if (pseudo_column_expr != NULL) {
    //this type of pseudo_column_expr has been add
    real_ref_expr = pseudo_column_expr;
  } else if (OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_ORA_ROWSCN, pseudo_column_expr))) {
  } else if (OB_ISNULL(pseudo_column_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    pseudo_column_expr->set_data_type(ObIntType);
    pseudo_column_expr->set_accuracy(ObAccuracy::MAX_ACCURACY[ObIntType]);
    pseudo_column_expr->set_table_id(table_item->table_id_);
    pseudo_column_expr->set_table_name(table_item->get_object_name());
    real_ref_expr = pseudo_column_expr;
    OZ(pseudo_column_expr->add_relation_id(get_stmt()->get_table_bit_index(table_item->table_id_)));
    OZ(get_stmt()->get_pseudo_column_like_exprs().push_back(pseudo_column_expr));
  }
  return ret;
}

// get all column ref expr recursively
int ObDMLResolver::get_all_column_ref(ObRawExpr *expr, ObIArray<ObColumnRefRawExpr*> &arr)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(expr));
  if (OB_SUCC(ret)) {
    const ObItemType type = expr->get_expr_type();
    if (T_REF_COLUMN == type) {
      ObColumnRefRawExpr *col_ref = static_cast<ObColumnRefRawExpr*>(expr);
      if (OB_FAIL(add_var_to_array_no_dup(arr, col_ref))) {
      }
    } else if (IS_EXPR_OP(type) || IS_FUN_SYS_TYPE(type)) {
      ObOpRawExpr *op_expr = static_cast<ObOpRawExpr*>(expr);
      for (int64_t i = 0; OB_SUCC(ret) && i < op_expr->get_param_count(); ++i) {
        if (OB_FAIL(SMART_CALL(get_all_column_ref(op_expr->get_param_expr(i), arr)))) {
        }
      }
    } else {
      // do nothing
    }
  }
  return ret;
}

/*@brief, ObDMLResolver::process_part_str is used to add double quotes to special keywords to remove their keyword attributes, for example:
 * create table t1(SYSTIMESTAMP int) partition by range(SYSTIMESTAMP) (partition "p0" values less than 10000);
 * select SYSTIMESTAMP from dual; ==> select "SYSTIMESTAMP" from dual;
 * Only in this way can the part expr be truly reparsed, otherwise it will be misparsed as a function, essentially here it represents an ordinary column property, currently known keywords are:
 * SYSTIMESTAMP、CURRENT_DATE、LOCALTIMESTAMP、CURRENT_TIMESTAMP、SESSIONTIMEZONE、DBTIMEZONE、
 * bug:
 */

#define ISSPACE(c) ((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t' || (c) == '\f' || (c) == '\v')


int ObDMLResolver::convert_udf_to_agg_expr(ObRawExpr *&expr,
                                           ObRawExpr *parent_expr,
                                           ObExprResolveContext &ctx)
{
  int ret = OB_SUCCESS;
  ObUDFRawExpr *udf_expr = NULL;
  bool parent_is_win_expr = parent_expr != NULL && parent_expr->is_win_func_expr() ? true : false;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    //only pl agg udf allow have distinct/unique/all as common aggr.
  } else if (expr->is_udf_expr() && !static_cast<ObUDFRawExpr *>(expr)->get_is_aggregate_udf() &&
             static_cast<ObUDFRawExpr *>(expr)->get_is_aggr_udf_distinct()) {
    ret = OB_DISTINCT_NOT_ALLOWED;
  } else if (!expr->is_udf_expr() || !static_cast<ObUDFRawExpr *>(expr)->get_is_aggregate_udf()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(SMART_CALL(convert_udf_to_agg_expr(expr->get_param_expr(i),
                                                     expr,
                                                     ctx)))) {
      }
    }
  } else {
    udf_expr = static_cast<ObUDFRawExpr *>(expr);
    ObAggFunRawExpr *agg_expr = NULL;
    if (OB_FAIL(ctx.parents_expr_info_.add_member(IS_AGG))) {
    } else if (OB_ISNULL(ctx.aggr_exprs_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ctx.expr_factory_.create_raw_expr(T_FUN_PL_AGG_UDF, agg_expr))) {
    } else if (!parent_is_win_expr && OB_FAIL(ctx.aggr_exprs_->push_back(agg_expr))) {
    } else {
      agg_expr->set_pl_agg_udf_expr(udf_expr);
      agg_expr->set_param_distinct(udf_expr->get_is_aggr_udf_distinct());
      for (int64_t i = 0; i < udf_expr->get_param_count(); ++i) {
        if (OB_FAIL(SMART_CALL(convert_udf_to_agg_expr(udf_expr->get_param_expr(i), udf_expr, ctx)))) {
        } else if (OB_FAIL(agg_expr->get_real_param_exprs_for_update().push_back(
                                                                    udf_expr->get_param_expr(i)))) {
        } else {
          /*do nothing */
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(agg_expr->extract_info())) {
        } else if (parent_is_win_expr) {
          static_cast<ObWinFunRawExpr *>(parent_expr)->set_agg_expr(agg_expr);
          expr = NULL;//reset pl_agg_udf_expr_ in ObWinFunRawExpr.
        } else {
          expr = agg_expr;
        }
      }
    }

    if (OB_SUCC(ret) && OB_FAIL(ctx.parents_expr_info_.del_member(IS_AGG))) {
    }
  }
  return ret;
}

int ObDMLResolver::add_parent_gen_col_exprs(const ObArray<GenColumnExprInfo> &gen_col_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append(gen_col_exprs_, gen_col_exprs))) {
  }
  return ret;
}

int ObDMLResolver::check_index_table_has_partition_keys(const ObTableSchema *index_schema,
                                                        const ObPartitionKeyInfo &partition_keys,
                                                        bool &has_part_key)
{
  int ret = OB_SUCCESS;
  has_part_key = true;
  if (OB_ISNULL(index_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && has_part_key && i < partition_keys.get_size(); ++i) {
      const ObColumnSchemaV2 *column_schema = nullptr;
      uint64_t col_id = OB_INVALID_ID;
      if (OB_FAIL(partition_keys.get_column_id(i, col_id))) {
      } else if (OB_ISNULL(column_schema = index_schema->get_column_schema(col_id))
                 || column_schema->is_virtual_generated_column()) {
        has_part_key = false;
      }
    }
  }
  return ret;
}

namespace
{
ObItemType semantic_hint_type(peg::NodeKind kind)
{
  switch (kind) {
    case peg::NodeKind::AggrFirstUnnest: return T_AGGR_FIRST_UNNEST;
    case peg::NodeKind::CoalesceSubqueries: return T_COALESCE_SQ;
    case peg::NodeKind::CountToExists: return T_COUNT_TO_EXISTS;
    case peg::NodeKind::NoCountToExists: return T_NO_COUNT_TO_EXISTS;
    case peg::NodeKind::Decorrelate: return T_DECORRELATE;
    case peg::NodeKind::DistinctPushdown: return T_DISTINCT_PUSHDOWN;
    case peg::NodeKind::EliminateJoin: return T_ELIMINATE_JOIN;
    case peg::NodeKind::FastMinmax: return T_FAST_MINMAX;
    case peg::NodeKind::FullScan: return T_FULL_HINT;
    case peg::NodeKind::GroupByPushdown: return T_GBY_PUSHDOWN;
    case peg::NodeKind::HashAggregate: return T_USE_HASH_AGGREGATE;
    case peg::NodeKind::HashDistinct: return T_USE_HASH_DISTINCT;
    case peg::NodeKind::HashSet: return T_USE_HASH_SET;
    case peg::NodeKind::IndexAsc: return T_INDEX_ASC_HINT;
    case peg::NodeKind::IndexDesc: return T_INDEX_DESC_HINT;
    case peg::NodeKind::IndexScan: return T_INDEX_HINT;
    case peg::NodeKind::NoIndex: return T_NO_INDEX_HINT;
    case peg::NodeKind::UnionMerge: return T_UNION_MERGE_HINT;
    case peg::NodeKind::Inline: return T_INLINE;
    case peg::NodeKind::JoinFirstUnnest: return T_JOIN_FIRST_UNNEST;
    case peg::NodeKind::Leading: return T_LEADING;
    case peg::NodeKind::Ordered: return T_ORDERED;
    case peg::NodeKind::LeftToAnti: return T_LEFT_TO_ANTI;
    case peg::NodeKind::Materialize: return T_MATERIALIZE;
    case peg::NodeKind::MergeView: return T_MERGE_HINT;
    case peg::NodeKind::NoAggrFirstUnnest: return T_NO_AGGR_FIRST_UNNEST;
    case peg::NodeKind::NoCoalesceSubqueries: return T_NO_COALESCE_SQ;
    case peg::NodeKind::NoDecorrelate: return T_NO_DECORRELATE;
    case peg::NodeKind::NoDistinctPushdown: return T_NO_DISTINCT_PUSHDOWN;
    case peg::NodeKind::NoEliminateJoin: return T_NO_ELIMINATE_JOIN;
    case peg::NodeKind::NoExpand: return T_NO_EXPAND;
    case peg::NodeKind::NoFastMinmax: return T_NO_FAST_MINMAX;
    case peg::NodeKind::NoGroupByPushdown: return T_NO_GBY_PUSHDOWN;
    case peg::NodeKind::NoHashAggregate: return T_NO_USE_HASH_AGGREGATE;
    case peg::NodeKind::NoHashDistinct: return T_NO_USE_HASH_DISTINCT;
    case peg::NodeKind::NoHashSet: return T_NO_USE_HASH_SET;
    case peg::NodeKind::NoJoinFirstUnnest: return T_NO_JOIN_FIRST_UNNEST;
    case peg::NodeKind::NoLeftToAnti: return T_NO_LEFT_TO_ANTI;
    case peg::NodeKind::NoMergeView: return T_NO_MERGE_HINT;
    case peg::NodeKind::NoOuterToInner: return T_NO_OUTER_TO_INNER;
    case peg::NodeKind::NoPlaceGroupBy: return T_NO_PLACE_GROUP_BY;
    case peg::NodeKind::NoPredicateDeduce: return T_NO_PRED_DEDUCE;
    case peg::NodeKind::NoProjectPrune: return T_NO_PROJECT_PRUNE;
    case peg::NodeKind::NoPullupExpr: return T_NO_PULLUP_EXPR;
    case peg::NodeKind::NoPushLimit: return T_NO_PUSH_LIMIT;
    case peg::NodeKind::NoPushPredCte: return T_NO_PUSH_PRED_CTE;
    case peg::NodeKind::NoPushSubq: return T_NO_PUSH_SUBQ;
    case peg::NodeKind::NoUseDistributedDml: return T_NO_USE_DISTRIBUTED_DML;
    case peg::NodeKind::NoReplaceConst: return T_NO_REPLACE_CONST;
    case peg::NodeKind::NoRewrite: return T_NO_REWRITE;
    case peg::NodeKind::NoSemiToInner: return T_NO_SEMI_TO_INNER;
    case peg::NodeKind::NoSimplifyDistinct: return T_NO_SIMPLIFY_DISTINCT;
    case peg::NodeKind::NoSimplifyExpr: return T_NO_SIMPLIFY_EXPR;
    case peg::NodeKind::NoSimplifyGroupBy: return T_NO_SIMPLIFY_GROUP_BY;
    case peg::NodeKind::NoSimplifyLimit: return T_NO_SIMPLIFY_LIMIT;
    case peg::NodeKind::NoSimplifyOrderBy: return T_NO_SIMPLIFY_ORDER_BY;
    case peg::NodeKind::NoSimplifySet: return T_NO_SIMPLIFY_SET;
    case peg::NodeKind::NoSimplifySubquery: return T_NO_SIMPLIFY_SUBQUERY;
    case peg::NodeKind::NoSimplifyWinfunc: return T_NO_SIMPLIFY_WINFUNC;
    case peg::NodeKind::NoTransformDistinctAgg: return T_NO_TRANSFORM_DISTINCT_AGG;
    case peg::NodeKind::NoUnnest: return T_NO_UNNEST;
    case peg::NodeKind::NoUseDas: return T_NO_USE_DAS_HINT;
    case peg::NodeKind::NoUseHash: return T_NO_USE_HASH;
    case peg::NodeKind::NoUseLateMaterialization: return T_NO_USE_LATE_MATERIALIZATION;
    case peg::NodeKind::NoUseMerge: return T_NO_USE_MERGE;
    case peg::NodeKind::NoUseNl: return T_NO_USE_NL;
    case peg::NodeKind::NoUseNlMaterialization: return T_NO_USE_NL_MATERIALIZATION;
    case peg::NodeKind::NoWindowMagic: return T_NO_WIN_MAGIC;
    case peg::NodeKind::OuterToInner: return T_OUTER_TO_INNER;
    case peg::NodeKind::PlaceGroupBy: return T_PLACE_GROUP_BY;
    case peg::NodeKind::PqDistinct: return T_PQ_DISTINCT_HINT;
    case peg::NodeKind::PqDistribute: return T_PQ_DISTRIBUTE;
    case peg::NodeKind::PqGroupBy: return T_PQ_GBY_HINT;
    case peg::NodeKind::JoinFilter: return T_PX_JOIN_FILTER;
    case peg::NodeKind::NoJoinFilter: return T_NO_PX_JOIN_FILTER;
    case peg::NodeKind::PartJoinFilter: return T_PX_PART_JOIN_FILTER;
    case peg::NodeKind::NoPartJoinFilter: return T_NO_PX_PART_JOIN_FILTER;
    case peg::NodeKind::PqMap: return T_PQ_MAP;
    case peg::NodeKind::PqSet: return T_PQ_SET;
    case peg::NodeKind::PqSubquery: return T_PQ_SUBQUERY;
    case peg::NodeKind::PredicateDeduce: return T_PRED_DEDUCE;
    case peg::NodeKind::ProjectPrune: return T_PROJECT_PRUNE;
    case peg::NodeKind::PullupExpr: return T_PULLUP_EXPR;
    case peg::NodeKind::PushLimit: return T_PUSH_LIMIT;
    case peg::NodeKind::PushPredCte: return T_PUSH_PRED_CTE;
    case peg::NodeKind::PushSubq: return T_PUSH_SUBQ;
    case peg::NodeKind::UseDistributedDml: return T_USE_DISTRIBUTED_DML;
    case peg::NodeKind::ReplaceConst: return T_REPLACE_CONST;
    case peg::NodeKind::SemiToInner: return T_SEMI_TO_INNER;
    case peg::NodeKind::SimplifyDistinct: return T_SIMPLIFY_DISTINCT;
    case peg::NodeKind::SimplifyExpr: return T_SIMPLIFY_EXPR;
    case peg::NodeKind::SimplifyGroupBy: return T_SIMPLIFY_GROUP_BY;
    case peg::NodeKind::SimplifyLimit: return T_SIMPLIFY_LIMIT;
    case peg::NodeKind::SimplifyOrderBy: return T_SIMPLIFY_ORDER_BY;
    case peg::NodeKind::SimplifySet: return T_SIMPLIFY_SET;
    case peg::NodeKind::SimplifySubquery: return T_SIMPLIFY_SUBQUERY;
    case peg::NodeKind::SimplifyWinfunc: return T_SIMPLIFY_WINFUNC;
    case peg::NodeKind::TableDynamicSampling: return T_TABLE_DYNAMIC_SAMPLING;
    case peg::NodeKind::TableParallel: return T_TABLE_PARALLEL;
    case peg::NodeKind::TransformDistinctAgg: return T_TRANSFORM_DISTINCT_AGG;
    case peg::NodeKind::Unnest: return T_UNNEST;
    case peg::NodeKind::UseConcat: return T_USE_CONCAT;
    case peg::NodeKind::UseDas: return T_USE_DAS_HINT;
    case peg::NodeKind::UseHash: return T_USE_HASH;
    case peg::NodeKind::UseLateMaterialization: return T_USE_LATE_MATERIALIZATION;
    case peg::NodeKind::UseMerge: return T_USE_MERGE;
    case peg::NodeKind::UseNl: return T_USE_NL;
    case peg::NodeKind::UseNlMaterialization: return T_USE_NL_MATERIALIZATION;
    case peg::NodeKind::WindowMagic: return T_WIN_MAGIC;
    default: return T_INVALID;
  }
}

ObItemType distribution_method(peg::NodeKind kind)
{
  switch (kind) {
    case peg::NodeKind::DistributeAll: return T_DISTRIBUTE_ALL;
    case peg::NodeKind::DistributeNone: return T_DISTRIBUTE_NONE;
    case peg::NodeKind::DistributePartition: return T_DISTRIBUTE_PARTITION;
    case peg::NodeKind::DistributeRandom: return T_DISTRIBUTE_RANDOM;
    case peg::NodeKind::DistributeHash: return T_DISTRIBUTE_HASH;
    case peg::NodeKind::DistributeBroadcast: return T_DISTRIBUTE_BROADCAST;
    case peg::NodeKind::DistributeLocal: return T_DISTRIBUTE_LOCAL;
    case peg::NodeKind::DistributeBc2Host: return T_DISTRIBUTE_BC2HOST;
    case peg::NodeKind::DistributeRange: return T_DISTRIBUTE_RANGE;
    case peg::NodeKind::DistributeList: return T_DISTRIBUTE_LIST;
    case peg::NodeKind::DistributeBasic: return T_DISTRIBUTE_BASIC;
    case peg::NodeKind::DistributeHashLocal: return T_DISTRIBUTE_HASH_LOCAL;
    default: return T_INVALID;
  }
}

const peg::Node *query_block_in_hint(const peg::Node &hint)
{
  return peg::hint_has_query_block(hint) ? &peg::hint_query_block(hint) : nullptr;
}
}

int ObDMLResolver::resolve_hints(rust::Slice<const peg::Node> nodes)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = NULL;
  ObQueryCtx *query_ctx = NULL;
  ObString qb_name;
  if (OB_ISNULL(stmt = get_stmt()) || OB_ISNULL(query_ctx = stmt->get_query_ctx())) {
    ret = OB_NOT_INIT;
  } else if (nodes.empty()) {
    /* do nothing */
  } else {
    ObQueryHint &query_hint = query_ctx->get_query_hint_for_update();
    ObGlobalHint global_hint;
    ObSEArray<ObHint*, 8> hints;
    bool get_outline_data = false;
    bool filter_embedded_hint = query_hint.has_outline_data() || query_hint.has_user_def_outline();
    if (OB_FAIL(inner_resolve_hints(nodes, filter_embedded_hint,
                                    get_outline_data,
                                    global_hint,
                                    hints,
                                    qb_name))) {
    } else if (filter_embedded_hint) {
      /* do nothing */
    } else if (get_outline_data) {
      if (OB_FAIL(query_hint.set_outline_data_hints(global_hint,
                                                    stmt->get_stmt_id(),
                                                    hints,
                                                    true))) {
      }
    } else if (OB_FAIL(query_hint.get_global_hint().merge_global_hint(global_hint))) {
    } else if (OB_FAIL(query_hint.append_hints(stmt->get_stmt_id(), hints))) {
    }
  }

  // record origin stmt qb name
  if (OB_SUCC(ret) && OB_FAIL(query_ctx->get_query_hint_for_update().set_stmt_id_map_info(*stmt, qb_name))) {
  }
  return ret;
}

int ObDMLResolver::resolve_outline_data_hints()
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = NULL;
  ObQueryCtx *query_ctx = NULL;
  const peg::Node *outline_statement = params_.outline_ == nullptr ? nullptr : params_.outline_->statement();
  if (OB_ISNULL(stmt = get_stmt()) || OB_ISNULL(query_ctx = stmt->get_query_ctx())) {
    ret = OB_NOT_INIT;
  } else if (get_parent_namespace_resolver() != NULL) {
    /* do noting */
  } else if (outline_statement == nullptr || outline_statement->kind() != peg::NodeKind::Select ||
             peg::query_hints(*outline_statement).empty()) {
    /* do noting */
  } else {
    ObQueryHint &query_hint = query_ctx->get_query_hint_for_update();
    ObGlobalHint global_hint;
    ObSEArray<ObHint*, 8> hints;
    bool get_outline_data = false;
    ObString qb_name;
    if (OB_FAIL(inner_resolve_hints(peg::query_hints(*outline_statement), false,
                                    get_outline_data,
                                    global_hint,
                                    hints,
                                    qb_name))) {
    } else if (OB_FAIL(query_hint.set_outline_data_hints(global_hint, stmt->get_stmt_id(),
                                                         hints, false))) {
    }
  }
  return ret;
}

int ObDMLResolver::inner_resolve_hints(rust::Slice<const peg::Node> nodes,
                                       const bool filter_embedded_hint,
                                       bool &get_outline_data,
                                       ObGlobalHint &global_hint,
                                       ObIArray<ObHint*> &hints,
                                       ObString &qb_name)
{
  int ret = OB_SUCCESS;
  get_outline_data = false;
  global_hint.reset();
  hints.reset();
  qb_name.reset();
  {
    ObGlobalHint embeded_global_hint;
    ObSEArray<ObHint*, 8> embedded_hints;
    bool in_outline_data = false;
    bool reset_outline_hints = false;
    bool resolved_hint = false;
    ObSEArray<ObHint*, 2> cur_hints;
    bool qb_name_conflict = false;
    for (size_t i = 0; OB_SUCC(ret) && i < nodes.size(); i++) {
      const peg::Node &hint_node = nodes[i];
      resolved_hint = false;
      cur_hints.reuse();
      if (peg::NodeKind::QbName == hint_node.kind()) {
        ObString tmp_qb_name;
        if (OB_FAIL(resolve_qb_name_node(&peg::hint_query_block(hint_node), tmp_qb_name))) {
        } else if (OB_UNLIKELY(qb_name_conflict || (!qb_name.empty() && !tmp_qb_name.empty()))) {
          qb_name_conflict = true;
          qb_name.reset();
        } else if (!tmp_qb_name.empty()) {
          qb_name = tmp_qb_name;
        }
      } else if (filter_embedded_hint || get_outline_data) {
        /* has valid outline data already, do not resolve hint except qb_name hint */
      } else if (peg::NodeKind::BeginOutline == hint_node.kind()) {
        if (OB_LIKELY(!in_outline_data)) {
          in_outline_data = true;
        } else {
          reset_outline_hints = true;
        }
      } else if (peg::NodeKind::EndOutline == hint_node.kind()) {
        if (OB_UNLIKELY(!in_outline_data)) {
        } else {
          get_outline_data = true;
          in_outline_data = false;
        }
      } else if (OB_FAIL(resolve_global_hint(hint_node,
                                             in_outline_data ? global_hint : embeded_global_hint,
                                             resolved_hint))) {
      } else if (resolved_hint) {
        LOG_DEBUG("resolve global hint node", "type", static_cast<int>(hint_node.kind()), K(in_outline_data));
      } else if (OB_FAIL(resolve_transform_hint(hint_node, resolved_hint, cur_hints))) {
      } else if (!resolved_hint && OB_FAIL(resolve_optimize_hint(hint_node, resolved_hint,
                                                                 cur_hints))) {
      } else if (OB_UNLIKELY(!resolved_hint)) {
        ret = OB_ERR_HINT_UNKNOWN;
        LOG_WARN("Unknown hint", "hint_name", static_cast<int>(hint_node.kind()));
      } else if (OB_FAIL(append(in_outline_data ? hints : embedded_hints, cur_hints))) {
      } else {
        LOG_DEBUG("resolved a tranform/optimize hint.", "type", static_cast<int>(hint_node.kind()),
                                                        K(cur_hints));
      }

      if (OB_SUCC(ret) && reset_outline_hints) {
        if (OB_FAIL(append(embedded_hints, hints))) {
        } else if (OB_FAIL(embeded_global_hint.merge_global_hint(global_hint))) {
        } else {
          reset_outline_hints = false;
          global_hint.reset();
          hints.reset();
        }
      }
    } // end of for

    if (OB_SUCC(ret) && !get_outline_data) {
      if (OB_FAIL(global_hint.merge_global_hint(embeded_global_hint))) {
      } else if (OB_FAIL(append(hints, embedded_hints))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_global_hint(const peg::Node &hint_node,
                                       ObGlobalHint &global_hint,
                                       bool &resolved_hint)
{
  int ret = OB_SUCCESS;
  resolved_hint = true;
  using Kind = peg::NodeKind;
  switch (hint_node.kind()) {
    case Kind::TopK:
      global_hint.merge_topk_hint(peg::integer_literal_value(peg::hint_topk_minimum(hint_node)),
                                 peg::integer_literal_value(peg::hint_topk_precision(hint_node)));
      break;
    case Kind::QueryTimeout:
      global_hint.merge_query_timeout_hint(peg::integer_literal_value(peg::hint_timeout(hint_node)));
      break;
    case Kind::Tracing:
    case Kind::Stat:
    case Kind::Blocking: {
      const auto targets = peg::hint_allocation_targets(hint_node);
      for (const auto &target : targets) {
        if (target.kind() == Kind::Uint || target.kind() == Kind::Number) return OB_SUCCESS;
      }
      ObSEArray<ObAllocOpHint, 8> operators;
      for (const auto &target : targets) {
        if (OB_SUCC(ret)) {
          ObAllocOpHint hint;
          hint.flags_ = hint_node.kind() == Kind::Blocking ? ObAllocOpHint::OB_MATERIAL
                      : hint_node.kind() == Kind::Stat ? ObAllocOpHint::OB_MONITOR_STAT
                                                      : ObAllocOpHint::OB_MONITOR_TRACING;
          if (target.kind() == Kind::Int) {
            hint.id_ = peg::integer_literal_value(target);
            hint.alloc_level_ = ObAllocOpHint::OB_ENUMERATE;
          } else {
            const auto text = target.text();
            const ObString level(syntax_text(text));
            if (0 == level.case_compare("all")) {
              hint.alloc_level_ = ObAllocOpHint::OB_ALL;
            } else if (0 == level.case_compare("dfo")) {
              hint.alloc_level_ = ObAllocOpHint::OB_DFO;
            }
          }
          OZ(operators.push_back(hint));
        }
      }
      OZ(global_hint.merge_alloc_op_hints(operators));
      break;
    }
    case Kind::TraceLog:
      global_hint.force_trace_log_ = true;
      break;
    case Kind::NoQueryTransformation:
      global_hint.disable_transform_ = true;
      break;
    case Kind::NoCostBasedQueryTransformation:
      global_hint.disable_cost_based_transform_ = true;
      break;
    case Kind::CursorSharingExact:
      global_hint.merge_param_option_hint(ObParamOption::EXACT);
      break;
    case Kind::ReadConsistency:
      global_hint.merge_read_consistency_hint(
          peg::hint_read_consistency(hint_node) == peg::ReadConsistency::Frozen ? FROZEN : STRONG, -1);
      break;
    case Kind::LogLevel: {
      const auto text = peg::hint_log_level(hint_node);
      ObString owned;
      OZ(ob_write_string(*allocator_, syntax_text(text), owned));
      OX(global_hint.merge_log_level_hint(owned));
      break;
    }
    case Kind::OptParam: {
      const peg::Node &name = peg::hint_parameter_name(hint_node);
      const peg::Node &value = peg::hint_parameter_value(hint_node);
      ObString param_name;
      ObObj val;
      const auto name_text = name.text();
      OZ(ob_write_string(*allocator_, syntax_text(name_text), param_name));
      if (value.kind() == Kind::String) {
        ObString text;
        const auto value_text = value.text();
        OZ(ob_write_string(*allocator_, syntax_text(value_text), text));
        val.set_varchar(text);
      } else {
        const bool signed_value = value.kind() == Kind::Unary;
        const bool negative = signed_value && peg::unary_operator(value) == peg::UnaryOperator::Minus;
        const auto &number = signed_value ? peg::unary_operand(value) : value;
        if (number.kind() != Kind::Int && number.kind() != Kind::Uint) return OB_SUCCESS;
        const uint64_t magnitude = peg::integer_literal_value(number);
        const uint64_t maximum = negative ? uint64_t(1) << 63 : INT64_MAX;
        if (magnitude > maximum) return OB_SUCCESS;
        val.set_int(negative ? (magnitude == (uint64_t(1) << 63) ? INT64_MIN : -static_cast<int64_t>(magnitude))
                             : static_cast<int64_t>(magnitude));
      }
      OZ(global_hint.opt_params_.add_opt_param_hint(param_name, val));
      break;
    }
    case Kind::UsePlanCache:
      global_hint.merge_plan_cache_hint(peg::hint_plan_cache_policy(hint_node) == peg::PlanCachePolicy::None
                                       ? OB_USE_PLAN_CACHE_NONE : OB_USE_PLAN_CACHE_DEFAULT);
      break;
    case Kind::EnableParallelDml:
      global_hint.merge_parallel_dml_hint(ObPDMLOption::ENABLE);
      break;
    case Kind::DisableParallelDml:
      global_hint.merge_parallel_dml_hint(ObPDMLOption::DISABLE);
      break;
    case Kind::EnableParallelDasDml:
      global_hint.merge_parallel_das_dml_hint(ObParallelDASOption::ENABLE);
      break;
    case Kind::DisableParallelDasDml:
      global_hint.merge_parallel_das_dml_hint(ObParallelDASOption::DISABLE);
      break;
    case Kind::NoParallel:
      global_hint.merge_parallel_hint(ObGlobalHint::DEFAULT_PARALLEL);
      break;
    case Kind::Parallel:
      switch (peg::hint_parallel_policy(hint_node)) {
        case peg::ParallelPolicy::Manual:
          global_hint.merge_parallel_hint(ObGlobalHint::SET_ENABLE_MANUAL_DOP);
          break;
        case peg::ParallelPolicy::Automatic:
          global_hint.merge_parallel_hint(ObGlobalHint::SET_ENABLE_AUTO_DOP);
          break;
        case peg::ParallelPolicy::Explicit:
          global_hint.merge_parallel_hint(peg::integer_literal_value(peg::hint_parallel_degree(hint_node)));
          break;
      }
      break;
    case Kind::Dop: {
      const auto &fragment = peg::hint_dop_fragment(hint_node);
      const auto &degree = peg::hint_dop_degree(hint_node);
      if (fragment.kind() == Kind::Int && degree.kind() == Kind::Int) {
        OZ(global_hint.merge_dop_hint(peg::integer_literal_value(fragment), peg::integer_literal_value(degree)));
      }
      break;
    }
    case Kind::LoadBatchSize:
      resolved_hint = peg::hint_load_batch_size(hint_node).kind() != Kind::Int;
      break;
    case Kind::DmlParallel:
      global_hint.merge_dml_parallel_hint(peg::integer_literal_value(peg::hint_parallel_degree(hint_node)));
      break;
    case Kind::GatherStatistics:
      global_hint.merge_osg_hint(ObOptimizerStatisticsGatheringHint::OB_OPT_STATS_GATHER);
      break;
    case Kind::NoGatherStatistics:
      global_hint.merge_osg_hint(ObOptimizerStatisticsGatheringHint::OB_NO_OPT_STATS_GATHER);
      break;
    case Kind::DbmsStats:
      global_hint.set_dbms_stats();
      break;
    case Kind::DynamicSampling: {
      const int64_t level = peg::integer_literal_value(peg::hint_sampling_level(hint_node));
      if (level >= ObDynamicSamplingLevel::NO_DYNAMIC_SAMPLING &&
          level <= ObDynamicSamplingLevel::BASIC_DYNAMIC_SAMPLING) {
        global_hint.merge_dynamic_sampling_hint(level);
      }
      break;
    }
    default:
      resolved_hint = false;
      break;
  }
  return ret;
}

int ObDMLResolver::resolve_transform_hint(const peg::Node &hint_node,
                                           bool &resolved_hint,
                                           ObIArray<ObHint*> &hints)
{
  int ret = OB_SUCCESS;
  resolved_hint = true;
  ObTransHint *trans_hint = nullptr;
  switch (hint_node.kind()) {
    case peg::NodeKind::MergeView:
    case peg::NodeKind::NoMergeView:
      OZ(resolve_view_merge_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::UseConcat:
    case peg::NodeKind::NoExpand:
      OZ(resolve_or_expand_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::Materialize:
    case peg::NodeKind::Inline:
      OZ(resolve_materialize_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::SemiToInner:
    case peg::NodeKind::NoSemiToInner:
      OZ(resolve_semi_to_inner_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::CoalesceSubqueries:
    case peg::NodeKind::NoCoalesceSubqueries:
      OZ(resolve_coalesce_sq_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::CountToExists:
    case peg::NodeKind::NoCountToExists:
      OZ(resolve_count_to_exists_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::LeftToAnti:
    case peg::NodeKind::NoLeftToAnti:
      OZ(resolve_left_to_anti_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::EliminateJoin:
    case peg::NodeKind::NoEliminateJoin:
      OZ(resolve_eliminate_join_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::WindowMagic:
    case peg::NodeKind::NoWindowMagic:
      OZ(resolve_win_magic_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::PlaceGroupBy:
    case peg::NodeKind::NoPlaceGroupBy:
      OZ(resolve_place_group_by_hint(hint_node, trans_hint));
      break;
    case peg::NodeKind::NoRewrite:
    case peg::NodeKind::OuterToInner:
    case peg::NodeKind::NoOuterToInner:
    case peg::NodeKind::Unnest:
    case peg::NodeKind::NoUnnest:
    case peg::NodeKind::SimplifyGroupBy:
    case peg::NodeKind::NoSimplifyGroupBy:
    case peg::NodeKind::PredicateDeduce:
    case peg::NodeKind::NoPredicateDeduce:
    case peg::NodeKind::ProjectPrune:
    case peg::NodeKind::NoProjectPrune:
    case peg::NodeKind::PushPredCte:
    case peg::NodeKind::NoPushPredCte:
    case peg::NodeKind::ReplaceConst:
    case peg::NodeKind::NoReplaceConst:
    case peg::NodeKind::SimplifyOrderBy:
    case peg::NodeKind::NoSimplifyOrderBy:
    case peg::NodeKind::SimplifyDistinct:
    case peg::NodeKind::NoSimplifyDistinct:
    case peg::NodeKind::SimplifyWinfunc:
    case peg::NodeKind::NoSimplifyWinfunc:
    case peg::NodeKind::SimplifyExpr:
    case peg::NodeKind::NoSimplifyExpr:
    case peg::NodeKind::SimplifyLimit:
    case peg::NodeKind::NoSimplifyLimit:
    case peg::NodeKind::SimplifySubquery:
    case peg::NodeKind::NoSimplifySubquery:
    case peg::NodeKind::FastMinmax:
    case peg::NodeKind::NoFastMinmax:
    case peg::NodeKind::SimplifySet:
    case peg::NodeKind::NoSimplifySet:
    case peg::NodeKind::PushLimit:
    case peg::NodeKind::NoPushLimit:
    case peg::NodeKind::PullupExpr:
    case peg::NodeKind::NoPullupExpr:
    case peg::NodeKind::AggrFirstUnnest:
    case peg::NodeKind::NoAggrFirstUnnest:
    case peg::NodeKind::JoinFirstUnnest:
    case peg::NodeKind::NoJoinFirstUnnest:
    case peg::NodeKind::Decorrelate:
    case peg::NodeKind::NoDecorrelate:
    case peg::NodeKind::UseLateMaterialization:
    case peg::NodeKind::NoUseLateMaterialization:
    case peg::NodeKind::TransformDistinctAgg:
    case peg::NodeKind::NoTransformDistinctAgg:
      OZ(resolve_normal_transform_hint(hint_node, trans_hint));
      break;
    default:
      resolved_hint = false;
      break;
  }
  if (OB_SUCC(ret) && trans_hint != nullptr) {
    OZ(hints.push_back(trans_hint));
  }
  return ret;
}

int ObDMLResolver::resolve_optimize_hint(const peg::Node &hint_node,
                                           bool &resolved_hint,
                                           ObIArray<ObHint*> &hints)
{
  int ret = OB_SUCCESS;
  resolved_hint = true;
  ObOptHint *opt_hint = nullptr;
  switch (hint_node.kind()) {
    case peg::NodeKind::FullScan:
    case peg::NodeKind::UseDas:
    case peg::NodeKind::NoUseDas:
    case peg::NodeKind::IndexScan:
    case peg::NodeKind::NoIndex:
    case peg::NodeKind::IndexAsc:
    case peg::NodeKind::IndexDesc:
      OZ(resolve_index_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::UnionMerge:
      OZ(resolve_union_merge_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::UseNl:
    case peg::NodeKind::NoUseNl:
    case peg::NodeKind::UseNlMaterialization:
    case peg::NodeKind::NoUseNlMaterialization:
    case peg::NodeKind::UseHash:
    case peg::NodeKind::NoUseHash:
    case peg::NodeKind::UseMerge:
    case peg::NodeKind::NoUseMerge:
      OZ(resolve_join_hint(hint_node, hints));
      break;
    case peg::NodeKind::Leading:
    case peg::NodeKind::Ordered:
      OZ(resolve_join_order_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::JoinFilter:
    case peg::NodeKind::NoJoinFilter:
    case peg::NodeKind::PartJoinFilter:
    case peg::NodeKind::NoPartJoinFilter:
      OZ(resolve_join_filter_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::PqDistributeWindow:
      OZ(resolve_window_distribution_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::PqMap:
      OZ(resolve_pq_map_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::PqDistribute:
      OZ(resolve_pq_distribute_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::PqSet:
      OZ(resolve_pq_set_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::PqSubquery:
      OZ(resolve_pq_subquery_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::PqGroupBy:
    case peg::NodeKind::PqDistinct:
      OZ(resolve_normal_pq_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::HashAggregate:
    case peg::NodeKind::NoHashAggregate:
      OZ(resolve_aggregation_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::TableParallel:
      OZ(resolve_table_parallel_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::TableDynamicSampling:
      OZ(resolve_table_dynamic_sampling_hint(hint_node, opt_hint));
      break;
    case peg::NodeKind::GroupByPushdown:
    case peg::NodeKind::NoGroupByPushdown:
    case peg::NodeKind::HashDistinct:
    case peg::NodeKind::NoHashDistinct:
    case peg::NodeKind::DistinctPushdown:
    case peg::NodeKind::NoDistinctPushdown:
    case peg::NodeKind::HashSet:
    case peg::NodeKind::NoHashSet:
    case peg::NodeKind::PushSubq:
    case peg::NodeKind::NoPushSubq:
    case peg::NodeKind::UseDistributedDml:
    case peg::NodeKind::NoUseDistributedDml:
      OZ(resolve_normal_optimize_hint(hint_node, opt_hint));
      break;
    default:
      resolved_hint = false;
      break;
  }
  if (OB_SUCC(ret) && opt_hint != nullptr) {
    OZ(hints.push_back(opt_hint));
  }
  return ret;
}

int ObDMLResolver::resolve_table_parallel_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  const int64_t parallel = peg::integer_literal_value(peg::hint_parallel_degree(hint_node));
  ObTableParallelHint *hint = nullptr;
  ObString qb_name;
  if (parallel >= 1) {
    OZ(ObQueryHint::create_hint(allocator_, T_TABLE_PARALLEL, hint));
    OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
    OZ(resolve_table_relation_in_hint(peg::hint_tables(hint_node)[0], hint->get_table()));
    if (OB_SUCC(ret)) {
      hint->set_parallel(parallel);
      hint->set_qb_name(qb_name);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_index_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObIndexHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_table_relation_in_hint(peg::hint_tables(hint_node)[0], hint->get_table()));
  if (OB_SUCC(ret)) {
    if (hint->get_hint_type() != T_FULL_HINT && hint->get_hint_type() != T_USE_DAS_HINT) {
      const peg::Node &name = peg::hint_index_name(hint_node);
      const auto name_text = name.text();
      OZ(ob_write_string(*allocator_, syntax_text(name_text), hint->get_index_name()));
      hint->get_index_prefix() = peg::hint_has_index_prefix(hint_node)
          ? static_cast<int64_t>(peg::integer_literal_value(peg::hint_index_prefix(hint_node))) : -1;
    }
    if (OB_SUCC(ret)) {
      hint->set_qb_name(qb_name);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_union_merge_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObUnionMergeHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, T_UNION_MERGE_HINT, hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_table_relation_in_hint(peg::hint_tables(hint_node)[0], hint->get_table()));
  for (const peg::Node &name : peg::hint_index_names(hint_node)) {
    const auto text = name.text();
    ObString index_name;
    OZ(ob_write_string(*allocator_, syntax_text(text), index_name));
    OZ(hint->get_index_name_list().push_back(index_name));
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    opt_hint = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_index_hint(const TableItem &table, rust::Slice<const peg::Node> hints)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  ObQueryCtx *query_ctx = nullptr;
  if (OB_ISNULL(stmt) || OB_ISNULL(query_ctx = stmt->get_query_ctx())) {
    ret = OB_NOT_INIT;
  } else if (!query_ctx->get_query_hint().has_outline_data() && !hints.empty()) {
    const peg::Node &first = hints[0];
    const ObItemType type = peg::index_hint_kind(first) == peg::IndexHintKind::Ignore
        ? T_NO_INDEX_HINT : T_INDEX_HINT;
    ObSEArray<ObHint*, 2> index_hints;
    for (const peg::Node &name : peg::index_hint_names(first)) {
      ObIndexHint *hint = nullptr;
      OZ(ObQueryHint::create_hint(allocator_, type, hint));
      OZ(index_hints.push_back(hint));
      if (OB_SUCC(ret)) {
        const auto name_text = name.text();
        OZ(ob_write_string(*allocator_, syntax_text(name_text), hint->get_index_name()));
        hint->get_table().set_table(table);
      }
    }
    OZ(query_ctx->get_query_hint_for_update().append_hints(stmt->get_stmt_id(), index_hints));
  }
  return ret;
}

int ObDMLResolver::resolve_join_order_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObJoinOrderHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  if (hint_node.kind() == peg::NodeKind::Leading) {
    OZ(resolve_tables_in_leading_hint(peg::hint_leading(hint_node), hint->get_table()));
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    opt_hint = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_join_hint(const peg::Node &hint_node, ObIArray<ObHint*> &hints)
{
  int ret = OB_SUCCESS;
  ObString qb_name;
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  for (const peg::Node &group : peg::hint_tables(hint_node)) {
    ObHint::TablesInHint tables;
    OZ(resolve_simple_table_list_in_hint(group, tables));
    if (OB_SUCC(ret) && !tables.empty()) {
      ObJoinHint *hint = nullptr;
      OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
      OZ(hint->get_tables().assign(tables));
      OZ(hints.push_back(hint));
      if (OB_SUCC(ret)) {
        hint->set_qb_name(qb_name);
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_join_filter_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObJoinFilterHint *hint = nullptr;
  ObString qb_name;
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_table_relation_in_hint(peg::hint_filter_table(hint_node), hint->get_filter_table()));
  for (const auto &tables : peg::hint_filter_left_tables(hint_node)) {
    OZ(resolve_simple_table_list_in_hint(tables, hint->get_left_tables()));
  }
  if (OB_SUCC(ret) && peg::hint_filter_has_pushdown_table(hint_node)) {
    OZ(resolve_table_relation_in_hint(peg::hint_filter_pushdown_table(hint_node),
                                    hint->get_pushdown_filter_table()));
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    opt_hint = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_window_distribution_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObSEArray<ObWindowDistHint::WinDistOption, 2> options;
  for (const auto &option : peg::hint_window_distributions(hint_node)) {
    if (OB_SUCC(ret)) {
      ObWindowDistHint::WinDistOption resolved;
      const auto flags = peg::window_distribution_options(option);
      switch (distribution_method(peg::window_distribution_method(option))) {
        case T_DISTRIBUTE_NONE:
          resolved.algo_ = WinDistAlgo::WIN_DIST_NONE;
          resolved.use_hash_sort_ = flags.partition_sort;
          resolved.use_topn_sort_ = flags.topn;
          break;
        case T_DISTRIBUTE_HASH:
        case T_DISTRIBUTE_HASH_LOCAL:
          resolved.algo_ = peg::window_distribution_method(option) == peg::NodeKind::DistributeHash
              ? WinDistAlgo::WIN_DIST_HASH : WinDistAlgo::WIN_DIST_HASH_LOCAL;
          resolved.use_hash_sort_ = flags.partition_sort;
          resolved.is_push_down_ = flags.pushdown;
          resolved.use_topn_sort_ = flags.topn;
          break;
        case T_DISTRIBUTE_RANGE:
          resolved.algo_ = WinDistAlgo::WIN_DIST_RANGE;
          break;
        case T_DISTRIBUTE_LIST:
          resolved.algo_ = WinDistAlgo::WIN_DIST_LIST;
          break;
        default:
          break;
      }
      for (const auto &index : peg::window_distribution_indexes(option)) {
        if (index.kind() != peg::NodeKind::Int) return OB_SUCCESS;
        OZ(resolved.win_func_idxs_.push_back(peg::integer_literal_value(index)));
      }
      if (!resolved.is_valid()) {
        options.reuse();
        break;
      }
      OZ(options.push_back(resolved));
    }
  }
  if (OB_SUCC(ret) && !options.empty()) {
    ObWindowDistHint *hint = nullptr;
    ObString qb_name;
    OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
    OZ(ObQueryHint::create_hint(allocator_, T_PQ_DISTRIBUTE_WINDOW, hint));
    OZ(hint->set_win_dist_options(options));
    if (OB_SUCC(ret)) {
      hint->set_qb_name(qb_name);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_pq_map_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObJoinHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, T_PQ_MAP, hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_simple_table_list_in_hint(peg::hint_tables(hint_node)[0], hint->get_tables()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    opt_hint = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_pq_distribute_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  const auto methods = peg::hint_distribution_methods(hint_node);
  DistAlgo dist_algo = DistAlgo::DIST_INVALID_METHOD;
  if (methods.empty()) {
    dist_algo = DistAlgo::DIST_BASIC_METHOD;
  } else {
    const ObItemType outer = distribution_method(methods[0].kind());
    const ObItemType inner = distribution_method(methods[1].kind());
    if (T_DISTRIBUTE_HASH == outer && T_DISTRIBUTE_HASH == inner) {
      dist_algo = DistAlgo::DIST_HASH_HASH;
    } else if (T_DISTRIBUTE_BROADCAST == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_BROADCAST_NONE;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_BROADCAST == inner) {
      dist_algo = DistAlgo::DIST_NONE_BROADCAST;
    } else if (T_DISTRIBUTE_PARTITION == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_PARTITION_NONE;
    } else if (T_DISTRIBUTE_HASH == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_HASH_NONE;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_PARTITION == inner) {
      dist_algo = DistAlgo::DIST_NONE_PARTITION;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_HASH == inner) {
      dist_algo = DistAlgo::DIST_NONE_HASH;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_PARTITION_WISE;
    } else if (T_DISTRIBUTE_LOCAL == outer && T_DISTRIBUTE_LOCAL == inner) {
      dist_algo = DistAlgo::DIST_PULL_TO_LOCAL;
    } else if (T_DISTRIBUTE_BC2HOST == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_BC2HOST_NONE;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_ALL == inner) {
      dist_algo = DistAlgo::DIST_NONE_ALL;
    } else if (T_DISTRIBUTE_ALL == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_ALL_NONE;
    } else if (T_DISTRIBUTE_RANDOM == outer && T_DISTRIBUTE_ALL == inner) {
      dist_algo = DistAlgo::DIST_RANDOM_ALL;
    } else if (T_DISTRIBUTE_HASH_LOCAL == outer && T_DISTRIBUTE_HASH_LOCAL == inner) {
      dist_algo = DistAlgo::DIST_HASH_HASH_LOCAL;
    } else if (T_DISTRIBUTE_PARTITION == outer && T_DISTRIBUTE_HASH_LOCAL == inner) {
      dist_algo = DistAlgo::DIST_PARTITION_HASH_LOCAL;
    } else if (T_DISTRIBUTE_HASH_LOCAL == outer && T_DISTRIBUTE_PARTITION == inner) {
      dist_algo = DistAlgo::DIST_HASH_LOCAL_PARTITION;
    } else if (T_DISTRIBUTE_BROADCAST == outer && T_DISTRIBUTE_HASH_LOCAL == inner) {
      dist_algo = DistAlgo::DIST_BROADCAST_HASH_LOCAL;
    } else if (T_DISTRIBUTE_HASH_LOCAL == outer && T_DISTRIBUTE_BROADCAST == inner) {
      dist_algo = DistAlgo::DIST_HASH_LOCAL_BROADCAST;
    }
  }
  if (dist_algo != DistAlgo::DIST_INVALID_METHOD) {
    ObJoinHint *hint = nullptr;
    ObString qb_name;
    OZ(ObQueryHint::create_hint(allocator_, T_PQ_DISTRIBUTE, hint));
    OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
    OZ(resolve_simple_table_list_in_hint(peg::hint_tables(hint_node)[0], hint->get_tables()));
    if (OB_SUCC(ret)) {
      hint->set_qb_name(qb_name);
      hint->set_dist_algo(dist_algo);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_pq_subquery_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  const auto methods = peg::hint_distribution_methods(hint_node);
  DistAlgo dist_algo = DistAlgo::DIST_INVALID_METHOD;
  if (methods.empty() || (methods.size() == 1 && methods[0].kind() == peg::NodeKind::DistributeBasic)) {
    dist_algo = DistAlgo::DIST_BASIC_METHOD;
  } else if (methods.size() == 2) {
    const ObItemType outer = distribution_method(methods[0].kind());
    const ObItemType inner = distribution_method(methods[1].kind());
    if (T_DISTRIBUTE_LOCAL == outer && T_DISTRIBUTE_LOCAL == inner) {
      dist_algo = DistAlgo::DIST_PULL_TO_LOCAL;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_PARTITION_WISE;
    } else if (T_DISTRIBUTE_PARTITION == outer && T_DISTRIBUTE_NONE == inner) {
      dist_algo = DistAlgo::DIST_PARTITION_NONE;
    } else if (T_DISTRIBUTE_NONE == outer && T_DISTRIBUTE_ALL == inner) {
      dist_algo = DistAlgo::DIST_NONE_ALL;
    } else if (T_DISTRIBUTE_RANDOM == outer && T_DISTRIBUTE_ALL == inner) {
      dist_algo = DistAlgo::DIST_RANDOM_ALL;
    } else if (T_DISTRIBUTE_HASH == outer && T_DISTRIBUTE_ALL == inner) {
      dist_algo = DistAlgo::DIST_HASH_ALL;
    }
  }
  if (dist_algo != DistAlgo::DIST_INVALID_METHOD) {
    ObPQSubqueryHint *hint = nullptr;
    ObString qb_name;
    OZ(ObQueryHint::create_hint(allocator_, T_PQ_SUBQUERY, hint));
    OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
    OZ(resolve_qb_name_list(peg::hint_subquery_names(hint_node), hint->get_sub_qb_names()));
    if (OB_SUCC(ret)) {
      hint->set_qb_name(qb_name);
      hint->set_dist_algo(dist_algo);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_pq_set_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  ObString qb_name;
  ObString left_branch;
  ObSEArray<ObItemType, 2> methods;
  const auto syntax_methods = peg::hint_distribution_methods(hint_node);
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_qb_name_node(peg::hint_has_target(hint_node) ? &peg::hint_target(hint_node) : nullptr,
                          left_branch));
  for (const peg::Node &method : syntax_methods) {
    OZ(methods.push_back(distribution_method(method.kind())));
  }
  const bool valid = syntax_methods.empty() ||
      (syntax_methods.size() >= 2 && ObPQSetHint::is_valid_dist_methods(methods));
  if (OB_SUCC(ret) && valid) {
    ObPQSetHint *hint = nullptr;
    OZ(ObQueryHint::create_hint(allocator_, T_PQ_SET, hint));
    OZ(hint->get_dist_methods().assign(methods));
    if (OB_SUCC(ret)) {
      hint->set_qb_name(qb_name);
      hint->set_left_branch(left_branch);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_normal_pq_hint(const peg::Node &hint_node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  const ObItemType method = distribution_method(peg::hint_distribution_methods(hint_node)[0].kind());
  if (ObPQHint::get_dist_method_str(method) != nullptr) {
    ObPQHint *hint = nullptr;
    ObString qb_name;
    OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
    OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
    if (OB_SUCC(ret)) {
      hint->set_qb_name(qb_name);
      hint->set_dist_method(method);
      if (peg::hint_has_sort_method(hint_node)) {
        hint->set_parallel(peg::integer_literal_value(peg::hint_sort_method(hint_node)));
      }
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_view_merge_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObViewMergeHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  ObString parent;
  OZ(resolve_qb_name_node(peg::hint_has_target(hint_node) ? &peg::hint_target(hint_node) : nullptr, parent));
  if (OB_SUCC(ret)) {
    hint->set_parent_qb_name(parent);
    hint->set_is_used_query_push_down(peg::hint_merge_direction(hint_node) == peg::MergeDirection::LessThan);
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_or_expand_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObOrExpandHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  if (OB_SUCC(ret) && peg::hint_has_condition(hint_node)) {
    const peg::Node &condition = peg::hint_condition(hint_node);
    const auto condition_text = condition.text();
    const int32_t length = condition_text.size() >= ObHint::MAX_EXPR_STR_LENGTH_IN_HINT
        ? ObHint::MAX_EXPR_STR_LENGTH_IN_HINT - 1 : static_cast<int32_t>(condition_text.size());
    ObString text;
    OZ(ob_write_string(*allocator_, ObString(length, syntax_text(condition_text).ptr()), text));
    if (OB_SUCC(ret)) {
      hint->set_expand_condition(text.ptr(), text.length());
    }
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_materialize_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObMaterializeHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_multi_qb_name_list(peg::hint_query_block_groups(hint_node), hint->get_qb_name_list()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_coalesce_sq_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObCoalesceSqHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_multi_qb_name_list(peg::hint_query_block_groups(hint_node), hint->get_qb_name_list()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_count_to_exists_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObCountToExistsHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_qb_name_list(peg::hint_subquery_names(hint_node), hint->get_qb_names()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_semi_to_inner_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObSemiToInnerHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  for (const peg::Node &group : peg::hint_tables(hint_node)) {
    OZ(resolve_simple_table_list_in_hint(group, hint->get_tables()));
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_left_to_anti_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObLeftToAntiHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_tb_name_list(peg::hint_tables(hint_node), hint->get_tb_name_list()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_eliminate_join_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObEliminateJoinHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_tb_name_list(peg::hint_tables(hint_node), hint->get_tb_name_list()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_place_group_by_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObGroupByPlacementHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  OZ(resolve_tb_name_list(peg::hint_tables(hint_node), hint->get_tb_name_list()));
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_win_magic_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObWinMagicHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));
  ObSEArray<ObHint::TablesInHint, 4> groups;
  OZ(resolve_tb_name_list(peg::hint_tables(hint_node), groups));
  for (int64_t i = 0; OB_SUCC(ret) && i < groups.count(); ++i) {
    for (int64_t j = 0; OB_SUCC(ret) && j < groups.at(i).count(); ++j) {
      OZ(hint->get_tb_name_list().push_back(groups.at(i).at(j)));
    }
  }
  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_normal_transform_hint(const peg::Node &hint_node, ObTransHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObTransHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));

  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_normal_optimize_hint(const peg::Node &hint_node, ObOptHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObOptHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));

  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_aggregation_hint(const peg::Node &hint_node, ObOptHint *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObAggHint *hint = nullptr;
  ObString qb_name;
  OZ(ObQueryHint::create_hint(allocator_, semantic_hint_type(hint_node.kind()), hint));
  OZ(resolve_qb_name_node(query_block_in_hint(hint_node), qb_name));

  if (OB_SUCC(ret)) {
    hint->set_qb_name(qb_name);
    const auto sort = peg::hint_aggregation_sort(hint_node);
    if (sort != peg::AggregationSort::Default) {
      hint->set_use_partition_sort(sort == peg::AggregationSort::Partition);
    }
    result = hint;
  }
  return ret;
}

int ObDMLResolver::resolve_tb_name_list(rust::Slice<const peg::Node> groups,
                                       ObIArray<ObHint::TablesInHint> &tables)
{
  int ret = OB_SUCCESS;
  for (const peg::Node &group : groups) {
    ObHint::TablesInHint entries;
    OZ(resolve_simple_table_list_in_hint(group, entries));
    OZ(tables.push_back(entries));
  }
  return ret;
}

int ObDMLResolver::resolve_tables_in_leading_hint(const peg::Node &node, ObLeadingTable &leading)
{
  int ret = OB_SUCCESS;
  if (node.kind() == peg::NodeKind::HintRelation) {
    OZ(ObQueryHint::create_hint_table(allocator_, leading.table_));
    OZ(resolve_table_relation_in_hint(node, *leading.table_));
  } else {
    const auto group = peg::hint_leading_group(node);
    OZ(ObQueryHint::create_leading_table(allocator_, leading.left_table_));
    OZ(ObQueryHint::create_leading_table(allocator_, leading.right_table_));
    OZ(SMART_CALL(resolve_tables_in_leading_hint(group[0], *leading.left_table_)));
    OZ(SMART_CALL(resolve_tables_in_leading_hint(group[1], *leading.right_table_)));
  }
  if (OB_SUCC(ret) && !leading.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObDMLResolver::resolve_simple_table_list_in_hint(const peg::Node &node,
                                                   ObIArray<ObTableInHint> &tables)
{
  int ret = OB_SUCCESS;
  if (node.kind() == peg::NodeKind::HintRelation) {
    ObTableInHint table;
    OZ(resolve_table_relation_in_hint(node, table));
    OZ(tables.push_back(table));
  } else {
    for (const peg::Node &table : peg::hint_tables(node)) {
      OZ(resolve_simple_table_list_in_hint(table, tables));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_table_relation_in_hint(const peg::Node &node, ObTableInHint &table)
{
  int ret = OB_SUCCESS;
  bool explicit_database = false;
  table.reset();
  OZ(resolve_qb_name_node(peg::hint_table_has_query_block(node)
                         ? &peg::hint_table_query_block(node) : nullptr, table.qb_name_));
  OZ(resolve_table_relation_node_v2(&peg::hint_table_relation(node), table.table_name_, table.db_name_,
                                    explicit_database, true));
  return ret;
}

int ObDMLResolver::resolve_qb_name_node(const peg::Node *name, ObString &qb_name)
{
  int ret = OB_SUCCESS;
  qb_name.reset();
  if (name != nullptr) {
    const auto name_text = name->text();
    if (name_text.size() <= OB_MAX_QB_NAME_LENGTH) {
      OZ(ob_write_string(*allocator_, syntax_text(name_text), qb_name));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_multi_qb_name_list(rust::Slice<const peg::Node> groups,
                                           ObIArray<QbNameList> &names)
{
  int ret = OB_SUCCESS;
  for (const peg::Node &group : groups) {
    QbNameList entry;
    OZ(resolve_qb_name_list(peg::hint_query_block_names(group), entry.qb_names_));
    OZ(names.push_back(entry));
  }
  return ret;
}

int ObDMLResolver::resolve_qb_name_list(rust::Slice<const peg::Node> names, ObIArray<ObString> &result)
{
  int ret = OB_SUCCESS;
  for (const peg::Node &name : names) {
    ObString value;
    OZ(resolve_qb_name_node(&name, value));
    OZ(result.push_back(value));
  }
  return ret;
}

class CopySchemaExpr : public ObRawExprCopier
{
public:
  CopySchemaExpr(ObRawExprFactory &expr_factory) :
    ObRawExprCopier(expr_factory)
  {}

  int check_need_copy(const ObRawExpr *old_expr, ObRawExpr *&new_expr) override
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(old_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (old_expr->is_column_ref_expr()) {
      new_expr = const_cast<ObRawExpr *>(old_expr);
    } else if (OB_FAIL(ObRawExprCopier::check_need_copy(old_expr, new_expr))) {
    }
    return ret;
  }
};

int ObDMLResolver::copy_schema_expr(ObRawExprFactory &factory,
                                    ObRawExpr *expr,
                                    ObRawExpr *&new_expr)
{
  CopySchemaExpr copier(factory);
  return copier.copy(expr, new_expr);
}

int ObDMLResolver::find_table_index_infos(const ObString &dst_index_name,
                                          const TableItem *table_item,
                                          bool &find_it,
                                          int64_t &table_id,
                                          int64_t &ref_id)
{
  int ret = OB_SUCCESS;
  find_it = false;
  const ObTableSchema *data_table_schema = NULL;
  if (OB_ISNULL(table_item) || OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(params_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_checker_->get_table_schema( table_item->ref_id_, data_table_schema))) {
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exists", K(*table_item));
  } else {
    ObSEArray<ObAuxTableMetaInfo, 16> index_infos;
    if (OB_FAIL(data_table_schema->get_simple_index_infos(index_infos))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && !find_it && i < index_infos.count(); ++i) {
        const ObTableSchema *index_schema = NULL;
        ObString src_index_name;
        if (OB_FAIL(schema_checker_->get_table_schema( index_infos.at(i).table_id_, index_schema))) {
        } else if (OB_ISNULL(index_schema)) {
          ret = OB_TABLE_NOT_EXIST;
          LOG_WARN("index table not exists", K(index_infos.at(i).table_id_));
        } else if (OB_FAIL(index_schema->get_index_name(src_index_name))) {
        } else if (0 == src_index_name.case_compare(dst_index_name)) {
          find_it = true;
          table_id = table_item->table_id_;
          ref_id = index_schema->get_table_id();
        } else {
          LOG_TRACE("fine index name", K(src_index_name), K(dst_index_name),
                                       K(index_schema->get_table_name_str()));
        }
      }
    }
  }
  return ret;
}

bool ObDMLResolver::get_joininfo_by_id(int64_t table_id, ResolverJoinInfo *&join_info) {
  bool found = false;
  for (int64_t i = 0; !found && i < join_infos_.count(); i++) {
    if (table_id == join_infos_.at(i).table_id_) {
      found = true;
      join_info = &join_infos_.at(i);
    }
  }
  return found;
}

int ObDMLResolver::get_json_table_column_by_id(uint64_t table_id, ObDmlJtColDef *&col_def)
{
  int ret = OB_ERR_UNEXPECTED;
  col_def = NULL;

  for (size_t i = 0; i < json_table_infos_.count(); ++i) {
    ObDmlJtColDef* tmp_def = json_table_infos_.at(i);
    if (OB_ISNULL(tmp_def)) {
    } else if (tmp_def->table_id_ == table_id) {
      ret = OB_SUCCESS;
      col_def = tmp_def;
      break;
    }
  }

  return ret;
}



int ObDMLResolver::check_CTE_name_exist(const ObString &var_name, bool &exist, TableItem *&table_item){
  int ret = OB_SUCCESS;
  exist = false;
  for (int64_t i = 0; !exist && OB_SUCC(ret) && i < current_cte_tables_.count(); i++) {
    bool is_equal = false;
    if (OB_ISNULL(current_cte_tables_[i])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(check_table_name_equal(current_cte_tables_[i]->table_name_, var_name, is_equal))) {
    } else if (is_equal) {
      exist = true;
      table_item = current_cte_tables_[i];
    }
  }
  for (int64_t i = 0; !exist && OB_SUCC(ret) && i < parent_cte_tables_.count(); i++) {
    bool is_equal = false;
    if (OB_ISNULL(parent_cte_tables_[i])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(check_table_name_equal(parent_cte_tables_[i]->table_name_, var_name, is_equal))) {
    } else if (is_equal) {
      exist = true;
      table_item = parent_cte_tables_[i];
    }
  }
  return ret;
}

int ObDMLResolver::check_current_CTE_name_exist(const ObString &var_name, bool &exist){
  int ret = OB_SUCCESS;
  exist = false;
  for (int64_t i = 0; !exist && OB_SUCC(ret) && i < current_cte_tables_.count(); i++) {
    if (OB_ISNULL(current_cte_tables_[i])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(check_table_name_equal(current_cte_tables_[i]->table_name_, var_name, exist))) {
    }
  }
  return ret;
}




int ObDMLResolver::set_cte_ctx(ObCteResolverCtx& cte_ctx,
                                  bool copy_col_name /*true*/,
                                  bool in_subquery /*false*/)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(cte_ctx_.assign(cte_ctx))) {
  }
  cte_ctx_.is_recursive_cte_ = false;
  cte_ctx_.cte_col_names_.reset();
  cte_ctx_.is_cte_subquery_ = in_subquery;
  if (cte_ctx_.is_with_resolver())
    ++cte_ctx_.cte_resolve_level_;
  if (copy_col_name) {
    for (int64_t i = 0; OB_SUCC(ret) && i < cte_ctx.cte_col_names_.count(); ++i) {
      if (OB_FAIL(cte_ctx_.cte_col_names_.push_back(cte_ctx.cte_col_names_.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pass cte column name to child resolver failed");
      }
    }
  }
  return ret;
}

int ObDMLResolver::add_cte_table_item(TableItem *table_item, bool &dup_name)
{
  int ret = OB_SUCCESS;
  dup_name = false;
  if (OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bool name_already_exist = false;
    for (int64_t i = 0; !name_already_exist && OB_SUCC(ret) && i < current_cte_tables_.count(); i++) {
      if (OB_FAIL(check_table_name_equal(table_item->table_name_, current_cte_tables_[i]->table_name_, name_already_exist))) {
      }
    }
    if (name_already_exist) {
      dup_name = true;
    } else if (OB_FAIL(current_cte_tables_.push_back(table_item))) {
    }
  }
  return ret;
}

int ObDMLResolver::get_opt_alias_colnames_for_recursive_cte(
  ObCteResolverCtx &ctx,
  rust::Slice<const peg::Node> columns)
{
  int ret = OB_SUCCESS;
  // Traverse all nodes, get col name
  ctx.cte_col_names_.reuse();
  if (columns.empty()) {
    ctx.has_cte_param_list_ = false;
  } else {
    ctx.has_cte_param_list_ = true;
    int64_t alias_num = columns.size();
    for (int64_t i = 0; OB_SUCC(ret) && i < alias_num; ++i) {
      const auto column_text = columns[i].text();
      if (column_text.size() <= 0) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObString column_alia_name;
        OZ(ob_write_string(*allocator_, syntax_text(column_text), column_alia_name));
        if (OB_FAIL(ctx.cte_col_names_.push_back(column_alia_name))) {
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::init_cte_resolver(ObSelectResolver &select_resolver,
                                    rust::Slice<const peg::Node> columns,
                                    ObString &table_name,
                                    bool has_recursive_word)
{
  int ret = OB_SUCCESS;
  select_resolver.set_current_level(current_level_);
  select_resolver.set_current_view_level(current_view_level_);
  select_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
  // Mark nested WITH-clause scopes as non-recorded in this resolver context.
  select_resolver.set_non_record(with_clause_without_record_
                                 || T_WITH_CLAUSE_SCOPE == current_scope_);
  if (OB_FAIL(select_resolver.set_cte_ctx(cte_ctx_))) {
  } else if (OB_FAIL(get_opt_alias_colnames_for_recursive_cte(select_resolver.cte_ctx_, columns))) {
  } else {
    // Do clear cte_ctx_
    select_resolver.cte_ctx_.set_current_cte_table_name(table_name);
    select_resolver.cte_ctx_.set_is_with_resolver(true);
    select_resolver.cte_ctx_.reset_subquery_level();
    select_resolver.cte_ctx_.reset_branch_count();
    select_resolver.cte_ctx_.set_has_recursive_word(has_recursive_word);
    ObString *rcte_name = has_recursive_word ? &table_name : NULL;
    /* Pass the current cte definition table name to the sub-resolver for determining if it is a recursive type of cte */
    if (OB_FAIL(add_cte_table_to_children(select_resolver, rcte_name))) {
    }
  }
  return ret;
}

int ObDMLResolver::add_fake_schema(ObSelectStmt *left_stmt)
{
  int ret = OB_SUCCESS;
  ObString tblname = cte_ctx_.current_cte_table_name_;
  ObTableSchema *tbl_schema = static_cast<ObTableSchema *>(allocator_->alloc(sizeof(ObTableSchema)));
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  if (OB_ISNULL(left_stmt)
      || OB_ISNULL(tbl_schema)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(session_info_->get_name_case_mode(case_mode))) {
  } else {
    if (OB_LOWERCASE_AND_INSENSITIVE == case_mode) {
      ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, tblname);
    }
    tbl_schema = new (tbl_schema) ObTableSchema(allocator_);
    tbl_schema->set_table_type(USER_TABLE);
    tbl_schema->set_table_name(tblname);
    //muhang magic number 50000 below is the user table
    int64_t magic_table_id = generate_cte_table_id();
    int64_t magic_db_id = common::OB_CTE_DATABASE_ID;
    int64_t magic_col_id = generate_cte_column_base_id();
    tbl_schema->set_table_id(magic_table_id);
    
    tbl_schema->set_database_id(magic_db_id);

    //cte view
    if (OB_SUCC(ret)) {
      ObSelectStmt *select_stmt = left_stmt;
      if (OB_ISNULL(select_stmt)) {
        ret = OB_INVALID_ARGUMENT;
      } else if (cte_ctx_.cte_col_names_.count() != select_stmt->get_select_item_size()) {
        if (cte_ctx_.cte_col_names_.empty()) {
          ret = OB_ERR_NEED_COLUMN_ALIAS_LIST_IN_RECURSIVE_CTE;
        } else {
          ret = OB_ERR_CTE_COLUMN_NUMBER_NOT_MATCH;
        }
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < select_stmt->get_select_item_size(); ++i) {
          ObRawExpr *&expr = select_stmt->get_select_item(i).expr_;
          ObColumnSchemaV2 *new_col = static_cast<ObColumnSchemaV2 *>(
            allocator_->alloc(sizeof(ObColumnSchemaV2)));
          if (OB_ISNULL(expr)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_ISNULL(new_col)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (expr->get_result_type().is_null()) {
            ObRawExpr *new_expr = NULL;
            ObRawExprResType bin_type;
            bin_type.set_binary();
            bin_type.set_length(0);
            bin_type.set_collation_level(CS_LEVEL_IMPLICIT);
            if (OB_FAIL(ObRawExprUtils::try_add_cast_expr_above(params_.expr_factory_,
                                                                session_info_,
                                                                *expr,
                                                                bin_type,
                                                                new_expr))) {
            } else if (expr == new_expr) {
              /*do nothing*/
            } else if (OB_FAIL(new_expr->add_flag(IS_INNER_ADDED_EXPR))) {
            } else {
              expr = new_expr;
            }
          }
          if (OB_SUCC(ret)) {
            new_col = new (new_col) ObColumnSchemaV2(allocator_);
            new_col->set_column_name(cte_ctx_.cte_col_names_.at(i));
            
            new_col->set_table_id(magic_table_id);
            new_col->set_column_id(magic_col_id + i);
            new_col->set_meta_type(expr->get_result_type());
            new_col->set_accuracy(expr->get_accuracy());
            new_col->set_collation_type(expr->get_collation_type());
            new_col->add_column_flag(CTE_GENERATED_COLUMN_FLAG);
            if (OB_FAIL(ObDDLResolver::fill_column_with_subschema(*expr, *session_info_, *new_col))) {
            } else if (OB_FAIL(tbl_schema->add_column(*new_col))) {
              if (OB_ERR_COLUMN_DUPLICATE == ret) {
                ObString &name = cte_ctx_.cte_col_names_.at(i);
                LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, name.length(), name.ptr());
              }
            }
            // Because table schema internally performs a deep copy once, so this must be released externally
            allocator_->free(new_col);
            //ob_free(new_col);
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_checker_->add_fake_cte_schema(tbl_schema))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_basic_table(const peg::Node &parse_tree, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (stmt->is_select_stmt() ||
             (stmt->is_delete_stmt() || stmt->is_update_stmt())) {
    if (OB_FAIL(resolve_basic_table_with_cte(parse_tree, table_item))) {
    }
  } else if (OB_FAIL(resolve_basic_table_without_cte(parse_tree, table_item))) {
  }


  return ret;
}

int ObDMLResolver::resolve_basic_table_with_cte(const peg::Node &parse_tree, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  const peg::Node &relation = peg::table_source(parse_tree);
  const auto &name = peg::relation_name(relation);
  const bool no_defined_database_name = !peg::relation_has_database(relation);
  bool find_CTE_name = false;
  // Lookup order: first look up normal cte, then look up recursive cte, finally look up normal tables
  // The upper-level CTE with the same name as the current RCTE has been removed
  const auto name_text = name.text();
  ObString tblname(syntax_text(name_text));
  bool is_equal = false;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (no_defined_database_name &&
      OB_FAIL(check_CTE_name_exist(tblname, find_CTE_name, table_item))) {
  } else if (find_CTE_name) {
    TableItem* CTE_table_item = table_item;
    table_item = NULL;
    if (OB_FAIL(resolve_cte_table(parse_tree, CTE_table_item, table_item))) {
    } else if (OB_ISNULL(table_item)) {
      table_item = CTE_table_item;
      ret = OB_ERR_UNEXPECTED;
    }
  } else if (OB_FAIL(check_table_name_equal(cte_ctx_.current_cte_table_name_, tblname, is_equal))) {
  } else if (cte_ctx_.check_has_recursive_word()
      && cte_ctx_.is_with_resolver()
      && is_equal
      && tblname.length()
      && no_defined_database_name) {
    // cte table references itself, at this point the cte is recursive
    TableItem *item = NULL;
    if (OB_FAIL(resolve_recursive_cte_table(parse_tree, item))) {
    } else if (cte_ctx_.more_than_two_branch()) {
      ret = OB_ERR_NEED_ONLY_TWO_BRANCH_IN_RECURSIVE_CTE;
    } else if (cte_ctx_.is_in_subquery()) {
      // Recursive CTE is not allowed in subqueries
      ret = OB_ERR_NEED_REFERENCE_ITSELF_DIRECTLY_IN_RECURSIVE_CTE;
    } else if (OB_ISNULL(item)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      table_item = item;
      cte_ctx_.set_recursive(true);
      //union all right side uses recursive cte table. temporary, cte is parsed into the left subclause, later, these subclauses will be replaced by fake table operator
      table_item->is_recursive_union_fake_table_ = true;
      table_item->cte_type_ = TableItem::FAKE_CTE;
      // CTE_TABLE is only marked in the with clause, the cte pseudo table parsed from the right child of union all
      table_item->type_ = TableItem::CTE_TABLE;
    }
  } else if (OB_FAIL(resolve_basic_table_without_cte(parse_tree, table_item))) {
    int tmp_ret = ret;
    if (OB_TABLE_NOT_EXIST == ret && cte_ctx_.is_with_resolver()) {
      if (peg::table_has_alias(parse_tree)) {
        const auto &alias = peg::table_alias(parse_tree);
        const auto alias_text = alias.text();
        ObString table_name(syntax_text(alias_text));
        if (OB_FAIL(check_table_name_equal(cte_ctx_.current_cte_table_name_, table_name, is_equal))) {
        } else if (is_equal) {
          //change the error number
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Alias table name same as recursive cte name");
        } else {
          ret = tmp_ret;
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_recursive_cte_table(const peg::Node &parse_tree, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  /**
   * Construct fake schema for recursive cte table
   */
  ObSelectStmt *base_stmt = cte_ctx_.left_select_stmt_;
  if (OB_ISNULL(base_stmt) && cte_ctx_.is_set_left_resolver_) {
    ret = OB_ERR_NEED_INIT_BRANCH_IN_RECURSIVE_CTE;
  } else if (OB_ISNULL(base_stmt)) {
    //ret = OB_NOT_SUPPORTED;
    ret = OB_ERR_NEED_UNION_ALL_IN_RECURSIVE_CTE;
  } else if (OB_FAIL(add_fake_schema(base_stmt))) {
  } else if (FALSE_IT(params_.is_resolve_fake_cte_table_ = true)) {
  } else if (OB_FAIL(resolve_basic_table_without_cte(parse_tree, table_item))) {
  }
  params_.is_resolve_fake_cte_table_ = false;
  return ret;
}

int ObDMLResolver::resolve_cte_table(const peg::Node &table,
                                      const TableItem *definition,
                                      TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *statement = get_stmt();
  if (OB_ISNULL(definition) || OB_ISNULL(statement) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(table_item = statement->create_table_item(*allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    if (peg::table_has_alias(table)) {
      const auto &alias = peg::table_alias(table);
      const auto alias_text = alias.text();
      OZ(ob_write_string(*allocator_, syntax_text(alias_text), table_item->alias_name_));
    }
    const auto &name = peg::relation_name(peg::table_source(table));
    const auto name_text = name.text();
    OZ(ob_write_string(*allocator_, syntax_text(name_text), table_item->table_name_));
    table_item->table_id_ = generate_table_id();
    table_item->type_ = TableItem::TEMP_TABLE;
    table_item->ref_query_ = definition->ref_query_;
    table_item->cte_type_ = definition->cte_type_;
    table_item->has_cte_column_aliases_ = definition->has_cte_column_aliases_;
    OZ(statement->add_table_item(session_info_, table_item));
  }
  return ret;
}

int ObDMLResolver::resolve_with_clause_opt_alias_colnames(rust::Slice<const peg::Node> columns,
                                                             TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *sub_select_stmt = nullptr;
  int64_t sub_select_stmt_item_count = 0;
  int64_t without_pseudo_count = 0;
  ObSEArray<common::ObString, 8> column_alias;
  if (OB_ISNULL(table_item)
      || OB_ISNULL(table_item->ref_query_)
      || (table_item->ref_query_->get_select_items()).count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (columns.empty()) {
    if (OB_FAIL(ObResolverUtils::check_duplicated_column(*table_item->ref_query_))) {
    }
  } else {
    /**
     * put table_item sub_qurey(stmt)'s select item to this level.
     * for every column name in T_LINK_NODE put it into array
     * column name's count must be equal to sub select item' count
     * change or add alias name
     */
    sub_select_stmt = table_item->ref_query_;
    ObIArray<SelectItem> &sub_select_items = sub_select_stmt->get_select_items();
    sub_select_stmt_item_count = sub_select_items.count();
    without_pseudo_count = sub_select_stmt_item_count;
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
      const auto column_text = columns[i].text();
      if (OB_UNLIKELY(column_text.size() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(column_text.size()
                             > OB_MAX_EXTENDED_USER_TABLE_NAME_LENGTH)) {
        ret = OB_ERR_TOO_LONG_IDENT;
      } else {
        ObString column_name;
        OZ(ob_write_string(*allocator_, syntax_text(column_text), column_name));
        if (OB_FAIL(column_alias.push_back(column_name))) {
        }
      }
    }
  }

  if (OB_SUCC(ret) && !columns.empty()) {
    //check the select item has "column ambiguously defined"
    common::hash::ObHashSet<ObString> column_name;
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    ObCollationType cs_type = CS_TYPE_INVALID;
    sub_select_stmt_item_count = column_alias.count();
    if (OB_FAIL(column_name.create((8)))) {
    } else if (OB_ISNULL(session_info_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(session_info_->get_name_case_mode(mode))) {
    } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    } else {
      //bool perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
      // Check if aliases are equal, note that pseudo columns are not included
      for (int64_t i = 0; OB_SUCC(ret) && i < sub_select_stmt_item_count; ++i) {
        ObString src = column_alias.at(i);
        if (OB_FAIL(column_name.set_refactored(src, 0))) {
          //change error number
          if (OB_HASH_EXIST == ret) {
            ret = OB_ERR_COLUMN_DUPLICATE;
            LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, src.length(), src.ptr());
          }
        }
      }
      //destory the hash table whether the ret is OB_SUCC or not
      if (column_name.created()) {
        column_name.destroy();
      }
    }
  }

  if (OB_SUCC(ret) && !columns.empty() && without_pseudo_count != columns.size()) {
    ret = OB_ERR_CTE_COLUMN_NUMBER_NOT_MATCH;
  } else if (OB_SUCC(ret)) {
    sub_select_stmt = table_item->ref_query_;
    ObIArray<SelectItem> &sub_select_items = sub_select_stmt->get_select_items();
    for (int64_t i = 0; i < column_alias.count(); ++i) {
      SelectItem &select_item = sub_select_items.at(i);
      select_item.alias_name_ = column_alias.at(i);
      select_item.is_real_alias_ = true;
      // cte set the alias, so parameterization information is no longer needed
      select_item.reset_param_const_infos();
    }
  }

  return ret;
}

/**
 * The special nature of the with clause, tables generated by the with clause may not appear in the from clause.
 * The tables generated here are only placed in the CTE_table array, and will only be added to the corresponding stmt
 * if the table is used when parsing the from clause.
 */
int ObDMLResolver::resolve_with_clause_subquery(const peg::Node &parse_tree, TableItem *&table_item, bool has_recursive_word)
{
  int ret = OB_SUCCESS;
  const peg::Node &alias_node = peg::cte_name(parse_tree);
  const auto columns = peg::cte_columns(parse_tree);
  const peg::Node &table_node = peg::cte_query(parse_tree);

  /*First set opt alias col */
  TableItem *item = NULL;
  ObString search_pseudo_column_name;
  ObString cycle_pseudo_column_name;
  ObString table_name;
  ObDMLStmt *stmt = get_stmt();
  ObSelectStmt *ref_stmt = NULL;
  ObSelectResolver select_resolver(params_);
  ObSelectResolver *cur_select_resolver = NULL;
  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const auto alias_node_text = alias_node.text();
    OZ(ob_write_string(*allocator_, syntax_text(alias_node_text), table_name));
    if (OB_FAIL(init_cte_resolver(select_resolver, columns, table_name, has_recursive_word))) {
    } else if (OB_FAIL(select_resolver.resolve_child_stmt(table_node))) {
    } else if (OB_ISNULL(ref_stmt = select_resolver.get_child_stmt())){
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(item = stmt->create_table_item(*allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create table item failed", K(ret));
    } else {
      item->ref_query_ = ref_stmt;
      item->table_id_ = generate_table_id();
      item->table_name_ = table_name;
      item->alias_name_ = table_name;
      item->type_ = TableItem::GENERATED_TABLE;
      item->cte_type_ = ref_stmt->is_recursive_union() ?
         TableItem::RECURSIVE_CTE : TableItem::NORMAL_CTE;
      item->has_cte_column_aliases_ = !columns.empty();
      table_item = item;
    }
  }
  if (OB_SUCC(ret)) {
    OZ( select_resolver.get_current_recursive_cte_table(ref_stmt) );
    OZ ( schema_checker_->remove_tmp_cte_schemas(select_resolver.cte_ctx_.get_current_cte_table_name()) );
  }
  return ret;
}


/**
 * @muhang.zb
 * Used to support with clause + select syntax.
 * Used to support that the with clause itself will generate subquery tables, and even specify the names of the columns in the subquery tables.
 * Originally, the parsing of tables was handled in the from clause; originally, the parsing of columns (select items) was handled in the select clause.
 * To ensure that the from parsing code remains unchanged, the with clause does not prematurely insert the generated tables into the from parsing result, but stores the result in the CTE_table member variable of stmt.
 */
int ObDMLResolver::resolve_with_clause(const peg::Node &query, bool same_level)
{
  int ret = OB_SUCCESS;
  UNUSED(same_level);
  current_scope_ = T_WITH_CLAUSE_SCOPE;
  ObDMLStmt *dml_stmt = get_stmt();
  TableItem *table_item = NULL;
  bool duplicate_name = false;

  if (OB_ISNULL(dml_stmt)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const bool has_recursive_word = peg::query_recursive(query);
    for (const peg::Node &cte : peg::query_ctes(query)) {
      if (OB_FAIL(ret)) { break; }
      {
        const auto &name = peg::cte_name(cte);
        const auto name_text = name.text();
        ObString table_name(syntax_text(name_text));
        if (OB_FAIL(ObDMLResolver::check_current_CTE_name_exist(table_name, duplicate_name))) {
        } else if (duplicate_name) {
          ret = OB_ERR_NONUNIQ_TABLE;
          LOG_USER_ERROR(OB_ERR_NONUNIQ_TABLE, table_name.length(), table_name.ptr());
        } else if (OB_FAIL(resolve_with_clause_subquery(cte, table_item, has_recursive_word))) {
        } else if (OB_FAIL(add_cte_table_item(table_item, duplicate_name))) {
        } else if (duplicate_name) {
          //syntax error
          //ERROR 1066(42000):Not unique table/alias: 't1'
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Duplicate CTE name");
        } else if (OB_FAIL(dml_stmt->add_cte_definition(table_item))) {
        } else if (OB_FAIL(resolve_with_clause_opt_alias_colnames(peg::cte_columns(cte),
                                                                  table_item))) {
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_table_dynamic_sampling_hint(const peg::Node &node, ObOptHint *&opt_hint)
{
  int ret = OB_SUCCESS;
  opt_hint = nullptr;
  const int64_t level = peg::integer_literal_value(peg::hint_sampling_level(node));
  if (level == ObDynamicSamplingLevel::NO_DYNAMIC_SAMPLING ||
      level == ObDynamicSamplingLevel::BASIC_DYNAMIC_SAMPLING) {
    ObTableDynamicSamplingHint *hint = nullptr;
    ObString qb_name;
    OZ(ObQueryHint::create_hint(allocator_, T_TABLE_DYNAMIC_SAMPLING, hint));
    OZ(resolve_qb_name_node(query_block_in_hint(node), qb_name));
    OZ(resolve_table_relation_in_hint(peg::hint_tables(node)[0], hint->get_table()));
    const int64_t blocks = peg::hint_has_sampling_blocks(node)
        ? static_cast<int64_t>(peg::integer_literal_value(peg::hint_sampling_blocks(node))) : 0;
    if (OB_SUCC(ret) && (!peg::hint_has_sampling_blocks(node) || blocks > 0)) {
      hint->set_dynamic_sampling(level);
      hint->set_qb_name(qb_name);
      hint->set_sample_block_cnt(blocks);
      opt_hint = hint;
    }
  }
  return ret;
}

int ObDMLResolver::resolve_values_table_item(const peg::Node &table_node, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  bool is_with_cte = false;
  ObDMLStmt *dml_stmt = get_stmt();
  TableItem *new_table_item = NULL;
  ObString alias_name;
  const bool row_alias = upper_insert_resolver_ != nullptr && upper_insert_resolver_->has_row_alias();
  if (OB_ISNULL(dml_stmt) ||  OB_ISNULL(allocator_) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(new_table_item = dml_stmt->create_table_item(*allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create table item failed");
  } else if (OB_FAIL(dml_stmt->generate_values_table_name(*allocator_, alias_name))) {
  }
  if (OB_SUCC(ret)) {
    void *buf = NULL;
    if (OB_ISNULL(buf = allocator_->alloc(sizeof(ObValuesTableDef)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(new_table_item->values_table_def_ = new (buf) ObValuesTableDef())) {
    } else if ((upper_insert_resolver_ == NULL || row_alias) &&
               OB_FAIL(resolve_values_table_for_select(table_node, *new_table_item->values_table_def_))) {
    //insert values table: insert into ....values row(...), row(...),...
    } else if (upper_insert_resolver_ != NULL && !row_alias &&
               OB_FAIL(resolve_values_table_for_insert(table_node, *new_table_item->values_table_def_))) {
    } else {
      new_table_item->table_id_ = generate_table_id();
      new_table_item->table_name_ = alias_name;
      new_table_item->alias_name_ = alias_name;
      new_table_item->type_ = TableItem::VALUES_TABLE;
      new_table_item->is_view_table_ = false;
      if (OB_FAIL(dml_stmt->add_table_item(session_info_, new_table_item))) {
      } else if (OB_FAIL(gen_values_table_column_items(
                                                   new_table_item->values_table_def_->column_cnt_,
                                                   new_table_item->values_table_def_->column_types_,
                                                   *new_table_item))) {
      } else {
        table_item = new_table_item;
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_values_table_for_select(const peg::Node &table_node,
                                                   ObValuesTableDef &table_def)
{
  int ret = OB_SUCCESS;
  const auto rows = peg::values_rows(table_node);
  const bool row_alias = upper_insert_resolver_ != nullptr && upper_insert_resolver_->has_row_alias();
  if (OB_ISNULL(params_.expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t column_cnt = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < rows.size(); i++) {
      const auto values = peg::row_elements(rows[i]);
      if (values.empty()) {
        ret = OB_ERR_VALUES_CLAUSE_NEED_HAVE_COLUMN;
      } else if (row_alias && values.size() != upper_insert_resolver_->get_insert_stmt()->get_values_desc().count()) {
        ret = OB_ERR_COULUMN_VALUE_NOT_MATCH;
        LOG_USER_ERROR(OB_ERR_COULUMN_VALUE_NOT_MATCH, i + 1);
      } else if (values.size() > common::OB_USER_ROW_MAX_COLUMNS_COUNT) {
        ret = OB_ERR_TOO_MANY_COLUMNS;
      } else {
        column_cnt = (i == 0 ? values.size() : column_cnt);
        if (OB_UNLIKELY(values.size() != column_cnt)) {
          ret = OB_ERR_COULUMN_VALUE_NOT_MATCH;
          LOG_USER_ERROR(OB_ERR_COULUMN_VALUE_NOT_MATCH, column_cnt);
          LOG_WARN("Column count doesn't match value count",
                   "num_child", values.size(),
                   "values desc count", column_cnt);
        } else {
          ObSEArray<ObRawExprResType, 8> cur_values_types;
          ObSEArray<ObRawExpr*, 8> cur_values_vector;
          for (int32_t j = 0; OB_SUCC(ret) && j < values.size(); j++) {
            ObRawExpr *expr = NULL;
            const peg::Node &value_node = values[j];
            if (value_node.kind() == peg::NodeKind::Missing) {
              ret = OB_ERR_VALUES_CLAUSE_NEED_HAVE_COLUMN;
            } else if (OB_FAIL(resolve_sql_expr(value_node, expr))) {
            } else if (OB_ISNULL(expr)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (row_alias) {
              OZ(resolve_row_alias_value(j, expr));
            } else if (expr->get_expr_type() == T_DEFAULT) {
              ret = OB_ERR_VALUES_CLAUSE_CANNOT_USE_DEFAULT_VALUES;
            }
            if (OB_SUCC(ret)) {
              if (OB_ISNULL(expr)) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(expr->formalize(params_.session_info_))) {
              } else if (OB_FAIL(cur_values_vector.push_back(expr))) {
              } else if (OB_FAIL(cur_values_types.push_back(expr->get_result_type()))) {
              } else {
              }
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(append(table_def.access_exprs_, cur_values_vector))) {
            } else if (i == 0) {
              if (OB_FAIL(append(table_def.column_types_, cur_values_types))) {
              } else {/*do nothing*/}
            } else {
              if (OB_FAIL(get_values_res_types(cur_values_types, table_def.column_types_))) {
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      table_def.column_cnt_ = column_cnt;
      if (OB_FAIL(try_add_cast_to_values(table_def.column_types_, table_def.access_exprs_))) {
      } else if (OB_FAIL(compute_values_table_row_count(table_def))) {
      } else if (OB_FAIL(estimate_values_table_stats(table_def))) {
      } else {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_row_alias_value(int64_t index, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = upper_insert_resolver_->get_insert_stmt();
  auto &table_info = insert_stmt->get_insert_table_info();
  const auto *target = table_info.values_desc_.at(index);
  if (target->is_generated_column()) {
    ret = OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN;
    const ObString &column = target->get_column_name();
    const ObString &table = insert_stmt->get_table_item_by_id(table_info.table_id_)->table_name_;
    LOG_USER_ERROR(OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN, column.length(), column.ptr(), table.length(), table.ptr());
  } else if (expr->get_expr_type() == T_DEFAULT) {
    ColumnItem *column = insert_stmt->get_column_item_by_id(table_info.table_id_, target->get_column_id());
    if (OB_ISNULL(column)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObDefaultValueUtils defaults(insert_stmt, &params_, upper_insert_resolver_);
      OZ(defaults.resolve_default_expr(*column, expr, T_INSERT_SCOPE));
    }
  }
  return ret;
}

int ObDMLResolver::resolve_values_table_for_insert(const peg::Node &table_node,
                                                   ObValuesTableDef &table_def)
{
  int ret = OB_SUCCESS;
  const auto rows = peg::values_rows(table_node);
  ObSEArray<int64_t, 8> value_idxs;
  ObInsertStmt *insert_stmt = NULL;
  bool is_all_default = false;
  if (OB_ISNULL(params_.expr_factory_) ||
      OB_ISNULL(upper_insert_resolver_) ||
      OB_ISNULL(insert_stmt = upper_insert_resolver_->get_insert_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(adjust_values_desc_position(insert_stmt->get_insert_table_info(),
                                                 value_idxs))) {
  } else {
    bool is_all_default = false;
    int64_t column_cnt = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < rows.size(); i++) {
      const auto values = peg::row_elements(rows[i]);
      if (values.size() > common::OB_USER_ROW_MAX_COLUMNS_COUNT) {
        ret = OB_ERR_TOO_MANY_COLUMNS;
      } else {
        //insert into table values row(), row()...
        bool tmp_is_all_default = values.empty();
        ObSEArray<ObRawExpr*, 8> cur_values_vector;
        ObInsertTableInfo &table_info = insert_stmt->get_insert_table_info();
        is_all_default = (i == 0 ? tmp_is_all_default : is_all_default);
        column_cnt = ((i == 0 || is_all_default) ? values.size() : column_cnt);
        if (OB_UNLIKELY(values.size() != column_cnt ||
                        tmp_is_all_default != is_all_default)) {
          ret = OB_ERR_COULUMN_VALUE_NOT_MATCH;
          LOG_USER_ERROR(OB_ERR_COULUMN_VALUE_NOT_MATCH, column_cnt);
          LOG_WARN("Column count doesn't match value count",
                   "num_child", values.size(),
                   "values desc count", column_cnt);
        } else if (is_all_default) {
          if (OB_FAIL(build_row_for_empty_values(cur_values_vector))) {
          } else {
            column_cnt = cur_values_vector.count();//set the real values desc count
          }
        } else if (OB_UNLIKELY(table_info.values_desc_.count() != value_idxs.count() ||
                               value_idxs.count() != values.size())) {
          ret = OB_ERR_COULUMN_VALUE_NOT_MATCH;
          LOG_USER_ERROR(OB_ERR_COULUMN_VALUE_NOT_MATCH, table_info.values_desc_.count());
          LOG_WARN("Column count doesn't match value count",
                   "num_child", values.size(),
                   "values desc count", table_info.values_desc_.count());
        } else {
          for (int32_t j = 0; OB_SUCC(ret) && j < values.size(); j++) {
            ObRawExpr *expr = NULL;
            const peg::Node &value_node = values[value_idxs.at(j)];
            if (OB_FAIL(resolve_sql_expr(value_node, expr))) {
            } else if (OB_ISNULL(expr) || OB_ISNULL(table_info.values_desc_.at(j))) {
              ret = OB_ERR_UNEXPECTED;
            } else if (table_info.values_desc_.at(j)->is_generated_column() &&
                       expr->get_expr_type() != T_DEFAULT) {
              ret = OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN;
              ColumnItem *orig_col_item = NULL;
              uint64_t column_id = table_info.values_desc_.at(j)->get_column_id();
              if (NULL != (orig_col_item = insert_stmt->get_column_item_by_id(table_info.table_id_,
                                                                              column_id))
                  && orig_col_item->expr_ != NULL) {
                const ObString &column_name = orig_col_item->expr_->get_column_name();
                const ObString &table_name = orig_col_item->expr_->get_table_name();
                LOG_USER_ERROR(OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN,
                               column_name.length(), column_name.ptr(),
                               table_name.length(), table_name.ptr());
              }
            } else if (expr->get_expr_type() == T_DEFAULT) {
              ColumnItem *column_item = NULL;
              uint64_t column_id = table_info.values_desc_.at(j)->get_column_id();
              if (OB_ISNULL(column_item = insert_stmt->get_column_item_by_id(table_info.table_id_,
                                                                             column_id))) {
                ret = OB_ERR_UNEXPECTED;
              } else if (table_info.values_desc_.at(j)->is_generated_column()) {
                if (OB_FAIL(copy_schema_expr(*params_.expr_factory_,
                                              column_item->expr_->get_dependant_expr(),
                                              expr))) {
                } else if (expr->has_flag(CNT_COLUMN)) {
                  if (OB_FAIL(replace_column_ref(cur_values_vector, table_info.values_desc_, expr))) {
                  } else {
                  }
                }
              } else {
                ObDefaultValueUtils utils(insert_stmt, &params_, upper_insert_resolver_);
                if (OB_FAIL(utils.resolve_default_expr(*column_item, expr, T_INSERT_SCOPE))) {
                }
              }
            }
            if (OB_SUCC(ret)) {
              if (OB_ISNULL(expr)) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(expr->formalize(params_.session_info_))) {
              } else if (OB_FAIL(cur_values_vector.push_back(expr))) {
              } else {
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(append(table_def.access_exprs_, cur_values_vector))) {
          } else if (i == 0) {
            for (int64_t k = 0; OB_SUCC(ret) && k < cur_values_vector.count(); k++) {
              if (OB_FAIL(table_def.column_types_.push_back(cur_values_vector.at(k)->get_result_type()))) {
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      table_def.column_cnt_ = column_cnt;
      if (OB_FAIL(compute_values_table_row_count(table_def))) {
      } else if (OB_FAIL(estimate_values_table_stats(table_def))) {
      } else {
      }
    }
  }
  return ret;
}

int ObDMLResolver::get_values_res_types(const ObIArray<ObRawExprResType> &cur_values_types,
                                        ObIArray<ObRawExprResType> &res_types)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_) ||
      OB_UNLIKELY(res_types.count() != cur_values_types.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObExprTypeCtx type_ctx;
    ObSQLUtils::init_type_ctx(session_info_, type_ctx);
    for (int64_t i = 0; OB_SUCC(ret) && i < res_types.count(); ++i) {
      ObExprVersion dummy_op(*allocator_);
      ObExprResType new_res_type;
      ObSEArray<ObExprResType, 2> tmp_types;
      ObCollationType coll_type = CS_TYPE_INVALID;
      if (OB_FAIL(tmp_types.push_back(res_types.at(i))) ||
          OB_FAIL(tmp_types.push_back(cur_values_types.at(i)))) {
      } else if (OB_FAIL(session_info_->get_collation_connection(coll_type))) {
      } else if (OB_FAIL(dummy_op.aggregate_result_type_for_merge(new_res_type, &tmp_types.at(0),
                                                                  tmp_types.count(),
                                                                  type_ctx))) {
      } else {
        res_types.at(i) = new_res_type;
      }
    }
  }
  return ret;
}

int ObDMLResolver::try_add_cast_to_values(const ObIArray<ObRawExprResType> &res_types,
                                          ObIArray<ObRawExpr*> &values_vector)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(res_types.empty() || values_vector.count() % res_types.count() != 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t res_cnt = res_types.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < values_vector.count(); ++i) {
      ObRawExpr *new_expr = NULL;
      int64_t j = i % res_cnt;
      if (OB_ISNULL(values_vector.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (values_vector.at(i)->get_result_type() == res_types.at(j)) {
        //do nothing
      } else if (OB_FAIL(ObRawExprUtils::try_add_cast_expr_above(params_.expr_factory_,
                                                                 session_info_,
                                                                 *values_vector.at(i),
                                                                 res_types.at(j),
                                                                 new_expr))) {
      } else if (values_vector.at(i) == new_expr) {
        /*do nothing*/
      } else if (OB_ISNULL(new_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(new_expr->add_flag(IS_INNER_ADDED_EXPR))) {
      } else {
        values_vector.at(i) = new_expr;
      }
    }
  }
  return ret;
}

int ObDMLResolver::gen_values_table_column_items(const int64_t column_cnt,
                                                 const ObIArray<ObRawExprResType> &res_types,
                                                 TableItem &table_item)
{
  int ret = OB_SUCCESS;
  ObValuesTableDef *table_def = table_item.values_table_def_;
  if (OB_ISNULL(params_.expr_factory_) || OB_ISNULL(allocator_) || OB_ISNULL(get_stmt()) ||
      OB_ISNULL(table_def) ||
      OB_UNLIKELY(column_cnt <= 0 || table_def->access_exprs_.empty() ||
                  table_def->access_exprs_.count() % column_cnt != 0 ||
                  res_types.count() != column_cnt)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt; ++i) {
      ObColumnRefRawExpr *column_expr = NULL;
      if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_REF_COLUMN, column_expr))) {
      } else if (OB_ISNULL(column_expr) || OB_ISNULL(table_def->access_exprs_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(("value desc is null"));
      } else {
        column_expr->set_result_type(res_types.at(i));
        column_expr->set_result_flag(table_def->access_exprs_.at(i)->get_result_flag());
        column_expr->set_ref_id(table_item.table_id_, i + OB_APP_MIN_COLUMN_ID);
        // compatible Mysql8.0, column name is column_0, column_1, ...
        ObSqlString tmp_col_name;
        char *buf = NULL;
        if (upper_insert_resolver_ != nullptr && upper_insert_resolver_->has_row_alias()) {
          OZ(tmp_col_name.append(upper_insert_resolver_->get_insert_stmt()->get_values_desc().at(i)->get_column_name()));
        } else {
          OZ(tmp_col_name.append_fmt("column_%ld", i));
        }
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(buf = static_cast<char*>(allocator_->alloc(tmp_col_name.length())))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(buf, tmp_col_name.ptr(), tmp_col_name.length());
          ObString column_name(tmp_col_name.length(), buf);
          column_expr->set_column_attr(table_item.table_name_, column_name);
          if (ob_is_enumset_tc(column_expr->get_result_type().get_type()) ||
              column_expr->get_result_type().is_lob_storage()) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "type of column in values table");
          } else if (OB_FAIL(column_expr->add_flag(IS_COLUMN))) {
          } else {
            ColumnItem column_item;
            column_item.expr_ = column_expr;
            column_item.table_id_ = column_expr->get_table_id();
            column_item.column_id_ = column_expr->get_column_id();
            column_item.column_name_ = column_expr->get_column_name();
            if (OB_FAIL(get_stmt()->add_column_item(column_item))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDMLResolver::compute_values_table_row_count(ObValuesTableDef &table_def)
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr1 = NULL;
  ObRawExpr *expr2 = NULL;
  bool is_dynamic = false;
  int64_t row_cnt = 0;
  int64_t expr_count = table_def.access_exprs_.count();
  if (OB_UNLIKELY(table_def.column_cnt_ == 0 || expr_count == 0) ||
      OB_ISNULL(expr1 = table_def.access_exprs_.at(0)) ||
      OB_ISNULL(expr2 = table_def.access_exprs_.at(expr_count - 1)) ||
      OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_QUESTIONMARK == expr1->get_expr_type() && expr1->is_static_scalar_const_expr() &&
             T_QUESTIONMARK == expr2->get_expr_type() && expr2->is_static_scalar_const_expr()) {
    ObConstRawExpr *param_expr = static_cast<ObConstRawExpr *>(expr1);
    int64_t param_idx = param_expr->get_value().get_unknown();
    bool is_ps_prepare = session_info_->is_varparams_sql_prepare();
    if (OB_ISNULL(params_.param_list_) ||
        OB_UNLIKELY(param_idx < 0 || param_idx >= params_.param_list_->count())) {
      if (is_ps_prepare) {
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else if (params_.param_list_->at(param_idx).is_ext_sql_array()) {
      if (OB_FAIL(params_.param_list_->at(param_idx).get_real_param_count(row_cnt))) {
      } else {
        is_dynamic = true;
        table_def.access_type_ = ObValuesTableDef::FOLD_ACCESS_EXPR;
        table_def.row_cnt_ = row_cnt;
        table_def.start_param_idx_ = param_idx;
        param_expr = static_cast<ObConstRawExpr *>(expr2);
        table_def.end_param_idx_ = param_expr->get_value().get_unknown();
        table_def.is_const_ = true;
      }
    }
  }
  if (OB_SUCC(ret) && !is_dynamic) {
    table_def.row_cnt_ = expr_count / table_def.column_cnt_;
    table_def.access_type_ = ObValuesTableDef::ACCESS_EXPR;
  }
  return ret;
}

int ObDMLResolver::add_obj_to_llc_bitmap(const ObObj &obj, char *llc_bitmap, double &num_null)
{
  int ret = OB_SUCCESS;
  uint64_t hash_value = 0;
  if (OB_ISNULL(llc_bitmap)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (obj.is_null()) {
    num_null += 1.0;
  } else {
    if (obj.is_string_type()) {
      hash_value = obj.varchar_hash(obj.get_collation_type(), hash_value);
    } else if (OB_FAIL(obj.hash(hash_value, hash_value))) {
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObAggregateProcessor::llc_add_value(hash_value, llc_bitmap, ObOptColumnStat::NUM_LLC_BUCKET))) {
      }
    }
  }
  return ret;
}

/*
  row_cnt <= compute_ndv_thredhold(2000), do accurate estimate stats
  row_cnt > compute_ndv_thredhold(2000), do accurate sampling 2000 rows record at head
*/
int ObDMLResolver::estimate_values_table_stats(ObValuesTableDef &table_def)
{
  int ret = OB_SUCCESS;
  const int64_t compute_ndv_thredhold = 2000;
  char *llc_bitmap = NULL;
  const int64_t llc_bitmap_size = ObOptColumnStat::NUM_LLC_BUCKET;
  ObArenaAllocator alloc("ValuesTableStat");
  const ParamStore *param_store = params_.param_list_;
  bool is_ps_prepare = false;
  bool has_ps_param = false;
  table_def.column_ndvs_.reset();
  table_def.column_nnvs_.reset();
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_ps_prepare = session_info_->is_varparams_sql_prepare();
  }
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < table_def.column_cnt_; col_idx++) {
    double ndv = table_def.row_cnt_;
    double num_null = 0.0;
    if (ObValuesTableDef::ACCESS_EXPR == table_def.access_type_) {
      /* ndv = table_def.row_cnt_, num_null = 0.0 */
    } else if (OB_ISNULL(llc_bitmap = static_cast<char*>(alloc.alloc(llc_bitmap_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("allocate memory for uncompressed data failed.", K(ret), K(llc_bitmap_size));
    } else {
      MEMSET(llc_bitmap, 0, llc_bitmap_size);
      if (ObValuesTableDef::FOLD_ACCESS_EXPR == table_def.access_type_) {
        ObRawExpr *access_expr = NULL;
        if (OB_UNLIKELY(col_idx >= table_def.access_exprs_.count()) ||
            OB_ISNULL(access_expr = table_def.access_exprs_.at(col_idx))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (T_QUESTIONMARK == access_expr->get_expr_type()) {
          ObConstRawExpr *param_expr = static_cast<ObConstRawExpr *>(access_expr);
          int64_t param_idx = param_expr->get_value().get_unknown();
          if (OB_ISNULL(param_store) ||
              OB_UNLIKELY(param_idx < 0 || param_idx >= param_store->count())) {
            if (is_ps_prepare) {
              has_ps_param = true;
            } else {
              ret = OB_ERR_UNEXPECTED;
            }
          } else if (param_store->at(param_idx).is_ext_sql_array()) {
            const ObSqlArrayObj *array_obj =
                      reinterpret_cast<const ObSqlArrayObj*>(param_store->at(param_idx).get_ext());
            if (OB_ISNULL(array_obj)) {
              ret = OB_ERR_UNEXPECTED;
            } else {
              for (int64_t i = 0; OB_SUCC(ret) && i < array_obj->count_ && i < compute_ndv_thredhold; i++) {
                const ObObjParam &obj_param = array_obj->data_[i];
                if (OB_FAIL(add_obj_to_llc_bitmap(obj_param, llc_bitmap, num_null))) {
                }
              }
            }
          }
        }
      } else if (ObValuesTableDef::ACCESS_PARAM == table_def.access_type_) {
        for (int64_t i = 0; OB_SUCC(ret) && i < table_def.row_cnt_ && i < compute_ndv_thredhold; i++) {
          int64_t param_idx = table_def.start_param_idx_ + i * table_def.column_cnt_ + col_idx;
          if (OB_UNLIKELY(param_idx < 0 || param_idx >= param_store->count())) {
            if (is_ps_prepare) {
              has_ps_param = true;
            } else {
              ret = OB_ERR_UNEXPECTED;
            }
          } else if (OB_FAIL(add_obj_to_llc_bitmap(param_store->at(param_idx), llc_bitmap, num_null))) {
          }
        }
      } else if (ObValuesTableDef::ACCESS_OBJ == table_def.access_type_) {
        for (int64_t i = 0; OB_SUCC(ret) && i < table_def.row_cnt_ && i < compute_ndv_thredhold; i++) {
          int64_t param_idx = i * table_def.column_cnt_ + col_idx;
          if (OB_FAIL(add_obj_to_llc_bitmap(table_def.access_objs_.at(param_idx), llc_bitmap, num_null))) {
          }
        }
      }
      if (OB_SUCC(ret) && !has_ps_param) {
        ndv = MIN(ObGlobalNdvEval::get_ndv_from_llc(llc_bitmap), ndv);
        ndv = table_def.row_cnt_ <= compute_ndv_thredhold ? ndv :
                  ObOptSelectivity::scale_distinct(table_def.row_cnt_, compute_ndv_thredhold, ndv);
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(table_def.column_ndvs_.push_back(ndv))) {
      } else if (OB_FAIL(table_def.column_nnvs_.push_back(num_null))) {
      } else {
      }
    }
  }
  return ret;
}

int ObDMLResolver::refine_generate_table_column_name(rust::Slice<const peg::Node> columns,
                                                       ObSelectStmt &select_stmt)
{
  int ret = OB_SUCCESS;
  if (columns.size() != select_stmt.get_select_item_size()) {
    ret = OB_ERR_VIEW_WRONG_LIST;
  } else {
    for (size_t i = 0; i < columns.size(); ++i) {
      SelectItem &item = select_stmt.get_select_item(i);
      const auto column_text = columns[i].text();
      OZ(ob_write_string(*allocator_, syntax_text(column_text), item.alias_name_));
      item.is_real_alias_ = true;
    }
  }
  return ret;
}

int ObDMLResolver::replace_column_ref(ObIArray<ObRawExpr*> &values_vector,
                                      ObIArray<ObColumnRefRawExpr*> &values_desc,
                                      ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!expr->has_flag(CNT_COLUMN)) {
    //do nothing
  } else if (expr->get_param_count() > 0) {
    for (int i = 0; OB_SUCC(ret) && i < expr->get_param_count(); i++) {
      if (OB_FAIL(SMART_CALL(replace_column_ref(values_vector, values_desc, expr->get_param_expr(i))))) {
      }
    }
  } else if (expr->is_column_ref_expr()) {
    int64_t value_index = -1;
    if (values_desc.count() < values_vector.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid array size", K(values_vector), K(values_desc));
    } else {
      uint64_t column_id = static_cast<ObColumnRefRawExpr*>(expr)->get_column_id();
      bool found_it = false;
      for(int64_t i = 0; !found_it && i < values_vector.count(); i++) {
        if (values_desc.at(i)->get_column_id() == column_id) {
          expr = values_vector.at(i);
          found_it = true;
        }
      }
      if (!found_it) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObDMLResolver::build_row_for_empty_values(ObIArray<ObRawExpr*> &values_vector)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = NULL;
  if (OB_ISNULL(upper_insert_resolver_) ||
      OB_ISNULL(insert_stmt = upper_insert_resolver_->get_insert_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ColumnItem *item = NULL;
    ObDefaultValueUtils utils(insert_stmt, &params_, static_cast<ObDMLResolver*>(upper_insert_resolver_));
    ObInsertTableInfo &table_info = insert_stmt->get_insert_table_info();
    for (int64_t i = 0; OB_SUCC(ret) && i < table_info.values_desc_.count(); ++i) {
      ObRawExpr *expr = NULL;
      int64_t column_id = table_info.values_desc_.at(i)->get_column_id();
      if (OB_ISNULL(item = insert_stmt->get_column_item_by_id(table_info.table_id_, column_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get column item", K(column_id));
      } else if (OB_UNLIKELY(item->expr_->is_generated_column())) {
        if (OB_FAIL(copy_schema_expr(*params_.expr_factory_,
                                     item->expr_->get_dependant_expr(),
                                     expr))) {
        } else if (expr->has_flag(CNT_COLUMN)) {
          if (OB_FAIL(replace_column_ref(values_vector, table_info.values_desc_, expr))) {
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(values_vector.push_back(expr))) {
        }
      } else if (item->is_auto_increment()) {
        if (OB_FAIL(ObRawExprUtils::build_null_expr(*params_.expr_factory_, expr))) {
        } else if (OB_FAIL(values_vector.push_back(expr))) {
        }
      } else {
        if (OB_FAIL(utils.generate_insert_value(item, expr))) {
        } else if (OB_FAIL(values_vector.push_back(expr))) {
        }
      }
    }
  }
  return ret;
}

//move generated columns behind basic columns before resolve values
int ObDMLResolver::adjust_values_desc_position(ObInsertTableInfo& table_info,
                                               ObIArray<int64_t> &value_idxs)
{
  int ret = OB_SUCCESS;
  ObArray<ObColumnRefRawExpr*> tmp_values_desc;
  if (OB_FAIL(value_idxs.reserve(table_info.values_desc_.count()))) {
  } else if (OB_FAIL(tmp_values_desc.reserve(table_info.values_desc_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < 2; ++i) {
    for (int64_t j = 0; OB_SUCC(ret) && j < table_info.values_desc_.count(); ++j) {
      if (OB_ISNULL(table_info.values_desc_.at(j))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inalid value desc", K(j), K(table_info.values_desc_));
      } else if ((i == 0 && !table_info.values_desc_.at(j)->is_generated_column())
                  || (i == 1 && table_info.values_desc_.at(j)->is_generated_column())) {
        if (OB_FAIL(tmp_values_desc.push_back(table_info.values_desc_.at(j)))) {
        } else if (OB_FAIL(value_idxs.push_back(j))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    table_info.values_desc_.reuse();
    if (OB_FAIL(append(table_info.values_desc_, tmp_values_desc))) {
    }
  }
  return ret;
}

int ObDMLResolver::fill_vec_id_expr_param(
    const uint64_t table_id,
    const uint64_t index_tid,
    const ObTableSchema *table_schema,
    ObRawExpr *&vec_id_expr,
    ObDMLStmt *stmt /* = NULL */)
{
  int ret = OB_SUCCESS;
  if (NULL == stmt) {
    stmt = get_stmt();
  }
  uint64_t rowkey_vid_tid = index_tid;
  if (OB_ISNULL(table_schema) || OB_ISNULL(vec_id_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(index_tid != table_schema->get_table_id())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(T_FUN_SYS_VEC_VID != vec_id_expr->get_expr_type())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_) || OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (table_schema->is_user_table() && OB_FAIL(table_schema->get_rowkey_vid_tid(rowkey_vid_tid))) {
    ObSchemaGetterGuard &schema_guard = *params_.schema_checker_->get_schema_guard();
    int tmp_ret = ret;
    bool is_all_deleted = false;
    /* 1. Here it is impossible to be a scenario where rowkey_vid cannot be obtained because the construction of the rowkey_vid index table has not been completed, because rowkey_vid is the first index table built later. If the outer layer judgment column is the vid column, then it indicates that rowkey_vid has already been created.
       2. Here it can only be a scenario where vector indexes are being deleted. During deletion, rowkey_vid may have already been deleted, but vid_rowkey has not been deleted. The outer function judges that the vid column on the main table still exists and enters this function. Therefore, it is necessary to determine whether tables 345 exist. If they do not exist, it means that tables 1 and 2 are currently being deleted, and the scenario where rowkey_vid cannot be obtained is possible. In this case, success should be returned.
     */
    if (OB_FAIL(ObVectorIndexUtil::check_vec_aux_index_deleted(schema_guard, *table_schema, is_all_deleted))) {
    }
    if (OB_SUCC(ret) && is_all_deleted) {
      ret = OB_SUCCESS;
    } else {
      ret = tmp_ret;
    }
  } else {
    CopySchemaExpr copier(*params_.expr_factory_);
    ObSysFunRawExpr *expr = static_cast<ObSysFunRawExpr *>(vec_id_expr);
    ObRawExpr *part_expr = stmt->get_part_expr(table_id, rowkey_vid_tid);
    ObRawExpr *subpart_expr = stmt->get_subpart_expr(table_id, rowkey_vid_tid);
    schema::ObPartitionLevel part_level = table_schema->get_part_level();
    ObRawExpr *calc_tablet_id_expr = nullptr;
    ObRawExpr *copy_part_expr = nullptr;
    ObRawExpr *copy_subpart_expr = nullptr;
    if (OB_FAIL(copier.copy(part_expr, copy_part_expr))) {
    } else if (OB_FAIL(copier.copy(subpart_expr, copy_subpart_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_calc_partition_tablet_id_expr(*params_.expr_factory_, *session_info_, rowkey_vid_tid,
            part_level, copy_part_expr, copy_subpart_expr, calc_tablet_id_expr))) {
    } else if (OB_ISNULL(calc_tablet_id_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr->set_param_expr(calc_tablet_id_expr))) {
    } else if (OB_FAIL(expr->formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::fill_doc_id_expr_param(
    const uint64_t table_id,
    const uint64_t index_tid,
    const ObTableSchema *table_schema,
    ObRawExpr *&doc_id_expr,
    ObDMLStmt *stmt /* = NULL */)
{
  int ret = OB_SUCCESS;
  if (NULL == stmt) {
    stmt = get_stmt();
  }
  if (OB_ISNULL(table_schema) || OB_ISNULL(doc_id_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(index_tid != table_schema->get_table_id())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(T_FUN_SYS_DOC_ID != doc_id_expr->get_expr_type())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_) || OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    CopySchemaExpr copier(*params_.expr_factory_);
    ObSysFunRawExpr *expr = static_cast<ObSysFunRawExpr *>(doc_id_expr);
    ObRawExpr *part_expr = nullptr;
    ObRawExpr *subpart_expr = nullptr;
    schema::ObPartitionLevel part_level = table_schema->get_part_level();
    ObRawExpr *calc_tablet_id_expr = nullptr;
    if (OB_FAIL(copier.copy(stmt->get_part_expr(table_id, index_tid), part_expr))) {
    } else if (OB_FAIL(copier.copy(stmt->get_subpart_expr(table_id, index_tid), subpart_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_calc_partition_tablet_id_expr(*params_.expr_factory_, *session_info_, index_tid,
                                                                           part_level, part_expr, subpart_expr, calc_tablet_id_expr))) {
    } else if (OB_ISNULL(calc_tablet_id_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr->set_param_expr(calc_tablet_id_expr))) {
    } else if (OB_FAIL(expr->formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::fill_ivf_vec_expr_param(
    const uint64_t table_id,
    const uint64_t index_tid,
    const uint64_t column_id, // index column
    const ObColumnSchemaV2 *column_schema, // generate column schema
    const ObTableSchema *table_schema,
    bool need_dist_algo_expr,
    ObRawExpr *&raw_expr,
    ObDMLStmt *stmt /* = NULL */)
{
  int ret = OB_SUCCESS;
  if (NULL == stmt) {
    stmt = get_stmt();
  }
  ObVectorIndexParam param;
  ObIndexType index_type = INDEX_TYPE_IS_NOT;
  ObSEArray<ObIndexType, 2> index_type_array;
  bool index_readable = true;
  bool param_filled = false;
  if (OB_ISNULL(table_schema) || OB_ISNULL(column_schema) || OB_ISNULL(raw_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(index_tid != table_schema->get_table_id())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(T_FUN_SYS_VEC_IVF_CENTER_ID != raw_expr->get_expr_type())
          && OB_UNLIKELY(T_FUN_SYS_VEC_IVF_SQ8_DATA_VECTOR != raw_expr->get_expr_type())
          && OB_UNLIKELY(T_FUN_SYS_VEC_IVF_PQ_CENTER_IDS != raw_expr->get_expr_type())
          && OB_UNLIKELY(T_FUN_SYS_VEC_IVF_PQ_CENTER_VECTOR != raw_expr->get_expr_type()
          && OB_UNLIKELY(T_FUN_SYS_VEC_IVF_FLAT_DATA_VECTOR != raw_expr->get_expr_type()))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_) || OB_ISNULL(stmt) || OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_param(
      schema_checker_->get_schema_guard(),
      *table_schema,
      column_id,
      param,
      param_filled))) {
  } else if (param_filled) {
    if (OB_FAIL(ObVectorIndexUtil::get_vector_index_type(raw_expr, param,
                                                         index_type_array))) {
    } else if (index_type_array.empty() && T_FUN_SYS_VEC_IVF_FLAT_DATA_VECTOR != raw_expr->get_expr_type()) {
      ret = OB_ERR_UNEXPECTED;
    }
  } else {
    index_readable = false;
  }

  // add calc_table_id_expr & calc_tablet_id_expr
  ObSysFunRawExpr *expr = static_cast<ObSysFunRawExpr *>(raw_expr);
  if (OB_SUCC(ret)) {
    int64_t new_capacity = expr->get_param_count() + 2 * index_type_array.count() + ObIvfConstant::IVF_VEC_EXPR_PARAM_COUNT;
    if (OB_FAIL(expr->extend_param_exprs(new_capacity))) {
    }
  }
  for (int i = 0; OB_SUCC(ret) && i < index_type_array.count() && index_readable; ++i) {
    uint64_t vec_index_tid = OB_INVALID_ID;
    if (OB_FAIL(ObVectorIndexUtil::get_right_index_tid_in_rebuild(
        schema_checker_->get_schema_guard(),
        *table_schema,
        index_type_array.at(i),
        column_id,
        column_schema,
        vec_index_tid))) {
    } else if (OB_INVALID_ID == vec_index_tid) {
      index_readable = false;
      uint64_t rowkey_cid_tid = OB_INVALID_ID;
      if (!column_schema->is_vec_ivf_center_id_column() && !column_schema->is_vec_ivf_pq_center_ids_column()) {
      } else if (OB_FAIL(ObVectorIndexUtil::check_rowkey_cid_table_readable(
          schema_checker_->get_schema_guard(),
          *table_schema,
          column_schema->get_column_id(),
          rowkey_cid_tid,
          true))) {
      } else if (OB_INVALID_ID != rowkey_cid_tid) {
        ObConstRawExpr *calc_table_id_expr = nullptr;
        if (OB_FAIL(ObRawExprUtils::build_const_uint_expr(*params_.expr_factory_, ObUInt64Type, vec_index_tid, calc_table_id_expr))) {
        } else if (OB_FAIL(expr->add_param_expr(calc_table_id_expr))) {
        }
      }
    } else {
      const ObTableSchema *index_table_schema = nullptr;
      CopySchemaExpr copier(*params_.expr_factory_);
      ObRawExpr *part_expr = stmt->get_part_expr(table_id, vec_index_tid);
      ObRawExpr *subpart_expr = stmt->get_subpart_expr(table_id, vec_index_tid);
      schema::ObPartitionLevel part_level = table_schema->get_part_level();
      ObRawExpr *calc_tablet_id_expr = nullptr;
      ObRawExpr *copy_part_expr = nullptr;
      ObRawExpr *copy_subpart_expr = nullptr;
      ObConstRawExpr *calc_table_id_expr = nullptr;
      if (OB_FAIL(schema_checker_->get_schema_guard()->get_table_schema( vec_index_tid, index_table_schema))) {
      } else if (OB_ISNULL(index_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
      } else if (!index_table_schema->can_read_index()) {
        index_readable = false;
        uint64_t rowkey_cid_tid = OB_INVALID_ID;
        if (!column_schema->is_vec_ivf_center_id_column() && !column_schema->is_vec_ivf_pq_center_ids_column()) {
        } else if (OB_FAIL(ObVectorIndexUtil::check_rowkey_cid_table_readable(
            schema_checker_->get_schema_guard(),
            *table_schema,
            column_schema->get_column_id(),
            rowkey_cid_tid,
            true))) {
        } else if (OB_INVALID_ID != rowkey_cid_tid) {
          ObConstRawExpr *calc_table_id_expr = nullptr;
          if (OB_FAIL(ObRawExprUtils::build_const_uint_expr(*params_.expr_factory_, ObUInt64Type, vec_index_tid, calc_table_id_expr))) {
          } else if (OB_FAIL(expr->add_param_expr(calc_table_id_expr))) {
          }
        }
      } else if (OB_FAIL(ObRawExprUtils::build_const_uint_expr(*params_.expr_factory_, ObUInt64Type, vec_index_tid, calc_table_id_expr))) {
      } else if (OB_FAIL(copier.copy(part_expr, copy_part_expr))) {
      } else if (OB_FAIL(copier.copy(subpart_expr, copy_subpart_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_calc_partition_tablet_id_expr(*params_.expr_factory_, *session_info_, vec_index_tid,
              part_level, copy_part_expr, copy_subpart_expr, calc_tablet_id_expr))) {
      } else if (OB_ISNULL(calc_tablet_id_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(expr->add_param_expr(calc_table_id_expr))) {
      } else if (OB_FAIL(expr->add_param_expr(calc_tablet_id_expr))) {
      }
    }
  }

  if (OB_SUCC(ret) && need_dist_algo_expr && index_readable) {
    ObConstRawExpr *calc_dist_algo_expr = nullptr;
    if (OB_FAIL(ObRawExprUtils::build_const_uint_expr(
        *params_.expr_factory_, ObUInt64Type, static_cast<uint64_t>(param.dist_algorithm_), calc_dist_algo_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(calc_dist_algo_expr))) {
    }
  }

  if (OB_SUCC(ret) && T_FUN_SYS_VEC_IVF_PQ_CENTER_IDS == raw_expr->get_expr_type() && index_readable) {
    ObConstRawExpr *calc_pq_m_expr = nullptr;
    ObConstRawExpr *calc_nbits_expr = nullptr;
    if (OB_FAIL(ObRawExprUtils::build_const_uint_expr(
        *params_.expr_factory_, ObUInt64Type, static_cast<uint64_t>(param.m_), calc_pq_m_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(calc_pq_m_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_const_uint_expr(
        *params_.expr_factory_, ObUInt64Type, static_cast<uint64_t>(param.nbits_), calc_nbits_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(calc_nbits_expr))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(expr->formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::fill_embedded_vec_expr_param(
    const uint64_t table_id,
    const uint64_t index_tid,
    const uint64_t column_id,
    const ObTableSchema *table_schema,
    ObRawExpr *&embedded_vec_expr,
    ObDMLStmt *stmt /* = NULL */)
{
  int ret = OB_SUCCESS;
  if (NULL == stmt) {
    stmt = get_stmt();
  }
  uint64_t embedded_vec_tid = index_tid;
  ObVectorIndexParam param;
  bool param_filled = false;
  if (OB_ISNULL(table_schema) || OB_ISNULL(embedded_vec_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(index_tid != table_schema->get_table_id())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(T_FUN_SYS_EMBEDDED_VEC != embedded_vec_expr->get_expr_type())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(params_.expr_factory_) || OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_param(schema_checker_->get_schema_guard(),
                                                               *table_schema,
                                                               column_id,
                                                               param,
                                                               param_filled))) {
  } else if (table_schema->is_user_table() && OB_FAIL(ObVectorIndexUtil::check_hybrid_embedded_vec_cid_table_readable(schema_checker_->get_schema_guard(), *table_schema, column_id, embedded_vec_tid, true))) {
  } else if (OB_INVALID_ID == embedded_vec_tid) {
    // do nothing, skip the embedded vec column
  } else {
    ObSysFunRawExpr *expr = static_cast<ObSysFunRawExpr *>(embedded_vec_expr);
    ObConstRawExpr *data_expr = nullptr;
    ObConstRawExpr *model_expr = nullptr;
    ObConstRawExpr *url_expr = nullptr;
    ObConstRawExpr *user_key_expr = nullptr;
    ObConstRawExpr *sync_mode_expr = nullptr;
    ObConstRawExpr *calc_dim_expr = nullptr;
    ObString data_str = "data";
    ObString model_name = param.endpoint_;
    ObString url_str = "url_str";
    ObString user_key = "user_key";
    ObString sync_model = param.sync_interval_type_ == ObVectorIndexSyncIntervalType::VSIT_IMMEDIATE ? "IMMEDIATE" : "";
    if (OB_FAIL(expr->extend_param_exprs(6))) {
    } else {
      // TODO(shancai): constuct expr params, when ai function is ready
    }
    // add params
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_, ObVarcharType, model_name, ObCharset::get_default_collation(ObCharset::get_default_charset()), model_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(model_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_, ObVarcharType, url_str, ObCharset::get_default_collation(ObCharset::get_default_charset()), url_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(url_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_, ObVarcharType, user_key, ObCharset::get_default_collation(ObCharset::get_default_charset()), user_key_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(user_key_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_, ObVarcharType, sync_model, ObCharset::get_default_collation(ObCharset::get_default_charset()), sync_mode_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(sync_mode_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(*params_.expr_factory_, ObIntType, param.dim_, calc_dim_expr))) {
    } else if (OB_FAIL(expr->add_param_expr(calc_dim_expr))) {
    } else if (OB_FAIL(expr->formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::check_match_against_expr(ObIArray<ObMatchFunRawExpr*> &match_exprs, const ObStmtScope scope, bool &is_es_match)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(match_exprs.count() == 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(match_exprs.at(0))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_es_match = false;
    for (int64_t i = 0; OB_SUCC(ret) && !is_es_match && i < match_exprs.count(); i++) {
      if (OB_ISNULL(match_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (match_exprs.at(i)->is_es_match()) {
        is_es_match = true;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (is_es_match) {
      if (OB_UNLIKELY(match_exprs.count() > 1 || stmt->get_match_exprs().count() > 0)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Multiple MATCHes or mixing MATCH with MATCH AGAINST is");
      } else if (OB_UNLIKELY(scope != T_WHERE_SCOPE)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "MATCH outside of WHERE clause is");
      }
    } else {
      if (OB_UNLIKELY(stmt->get_match_exprs().count() > 0
          && stmt->get_match_exprs().at(0)->is_es_match())) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Multiple MATCHes or mixing MATCH with MATCH AGAINST is");
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_match_against_exprs(ObRawExpr *&expr,
                                               ObIArray<ObMatchFunRawExpr*> &match_exprs,
                                               const ObStmtScope scope)
{
  int ret = OB_SUCCESS;
  ObDMLStmt *stmt = get_stmt();
  ObQuestionmarkEqualCtx check_ctx;
  ObExprEqualCheckContext equal_ctx;
  equal_ctx.override_const_compare_ = true;
  const ParamStore *param_store = params_.param_list_;
  bool is_es_match = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(expr) || OB_ISNULL(params_.query_ctx_) || OB_ISNULL(param_store)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_match_against_expr(match_exprs, scope, is_es_match))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < match_exprs.count(); i++) {
      uint64_t table_id = OB_INVALID_ID;
      ObMatchFunRawExpr *cur_match_expr = NULL;
      ObMatchFunRawExpr *match_expr_on_table = NULL;
      ObSEArray<ObRawExpr *, 4> match_exprs_on_table;
      bool table_on_null_side = false;
      bool is_simple_filter = false;
      ObSEArray<ObPCConstParamInfo, 4> param_constraints;

      if (OB_ISNULL(cur_match_expr = match_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (cur_match_expr->get_match_columns().count() < 1) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < cur_match_expr->get_match_columns().count(); j++) {
          ObColumnRefRawExpr *match_col = NULL;
          if (OB_ISNULL(cur_match_expr->get_match_columns().at(j)) || !cur_match_expr->get_match_columns().at(j)->is_column_ref_expr()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against column");
          } else if (OB_FALSE_IT(match_col = static_cast<ObColumnRefRawExpr*>(cur_match_expr->get_match_columns().at(j)))) {
          } else if (table_id != OB_INVALID_ID && table_id != match_col->get_table_id()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against columns on different tables");
          } else {
            table_id = match_col->get_table_id();
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(stmt->get_match_expr_on_table(table_id, match_exprs_on_table))) {
      } else if (cur_match_expr->is_es_match() && OB_FAIL(resolve_es_match_expr(*cur_match_expr))) {
      } else if (!cur_match_expr->is_es_match() && OB_FAIL(resolve_match_against_expr(*cur_match_expr))) {
      } else if (cur_match_expr->get_mode_flag() == ObMatchAgainstMode::MATCH_PHRASE_MODE &&
                 OB_FAIL(resolve_match_against_expr_with_match_phrase_mode(expr, cur_match_expr, scope))) {
      } else {
        bool shared = false;
        for (int64_t idx = 0; OB_SUCC(ret) && !shared && idx < match_exprs_on_table.count(); ++idx) {
          if (match_exprs_on_table.at(idx)->same_as(*cur_match_expr, &check_ctx)) {
            match_expr_on_table = static_cast<ObMatchFunRawExpr *>(match_exprs_on_table.at(idx));
            shared = true;
          } else if (match_exprs_on_table.at(idx)->same_as(*cur_match_expr, &equal_ctx)) {
            match_expr_on_table = static_cast<ObMatchFunRawExpr *>(match_exprs_on_table.at(idx));
            shared = true;
            // constraints need to be added if the same_as judgement relies on specific const value
            for(int64_t i = 0; OB_SUCC(ret) && i < equal_ctx.param_expr_.count(); i++) {
              ObPCConstParamInfo param_info;
              int64_t param_idx = equal_ctx.param_expr_.at(i).param_idx_;
              if (OB_UNLIKELY(param_idx < 0 || param_idx >= param_store->count())) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(param_info.const_idx_.push_back(param_idx))) {
              } else if (OB_FAIL(param_info.const_params_.push_back(param_store->at(param_idx)))) {
              } else if (OB_FAIL(param_constraints.push_back(param_info))) {
              }
            }
          }
        }
      }
      ObRawExprReplacer replacer;
      if (OB_FAIL(ret)) {
      } else if (nullptr == match_expr_on_table) {
        // same expr not found in stmt
        if (OB_FAIL(ObOptimizerUtil::is_table_on_null_side(stmt, table_id, table_on_null_side))) {
        } else if (table_on_null_side) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "fulltext search on null side of joined table");
        } else if (OB_FAIL(stmt->get_match_exprs().push_back(cur_match_expr))) {
        }
      } else if (OB_FAIL(replacer.add_replace_expr(cur_match_expr, match_expr_on_table))) {
      } else if (OB_FAIL(replacer.replace(expr))) {
      } else if (OB_FAIL(append(params_.query_ctx_->all_equal_param_constraints_, check_ctx.equal_pairs_))) {
      } else if (OB_FAIL(append(params_.query_ctx_->all_plan_const_param_constraints_, param_constraints))) {
      }
    }
  }
  return ret;
}

int ObDMLResolver::resolve_match_against_expr_with_match_phrase_mode(ObRawExpr *&expr, ObMatchFunRawExpr *&cur_match_expr, const ObStmtScope scope)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr) || OB_ISNULL(cur_match_expr)) {
    ret = OB_ERR_UNEXPECTED;
  }
  cur_match_expr->set_mode_flag(ObMatchAgainstMode::NATURAL_LANGUAGE_MODE);

  ObRawExpr *result_op = nullptr;
  ObRawExpr *and_op = nullptr;
  ObRawExpr *new_match_expr = nullptr;
  ObRawExpr *like_expr = nullptr;
  ObRawExpr *need_replace_expr = nullptr;
  ObSEArray<ObRawExpr*, 1> and_param_exprs;
  ObSEArray<ObRawExpr*, 1> or_like_param_exprs;
  bool result_is_bool = true;
  ObRawExpr *search_key_expr = cur_match_expr->get_search_key();
  ObRawExpr *match_parent_expr = nullptr;

  ObOpRawExpr *concat_text_expr = nullptr;
  ObConstRawExpr *const_str_expr = nullptr;
  ObConstRawExpr *like_es_expr = NULL;
  ObSEArray<ObRawExpr*, 1> substr_param_exprs;
  const char *ptr_value = "%";
  ObString const_str_value(1, ptr_value);
  const char *es_ptr_value = "\\";
  ObString like_escape(1, es_ptr_value);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_,
                                                      ObVarcharType,
                                                      const_str_value,
                                                      search_key_expr->get_result_type().get_collation_type(),
                                                      const_str_expr))) {
  } else if (OB_FAIL(substr_param_exprs.push_back(const_str_expr))) {
  } else if (OB_FAIL(substr_param_exprs.push_back(search_key_expr))) {
  } else if (OB_FAIL(substr_param_exprs.push_back(const_str_expr))) {
  } else if (OB_FAIL(ObRawExprUtils::create_concat_expr(*params_.expr_factory_,
                                                        params_.session_info_,
                                                        substr_param_exprs,
                                                        concat_text_expr))) {
  } else if (OB_FAIL(ObRawExprUtils::build_const_string_expr(*params_.expr_factory_,
                                                              ObVarcharType,
                                                              like_escape,
                                                              search_key_expr->get_result_type().get_collation_type(),
                                                              like_es_expr))) {
  } else {
    ObIArray<ObRawExpr*> &column_list = cur_match_expr->get_match_columns();
    for (int64_t j = 0; OB_SUCC(ret) && j < column_list.count(); ++j) {
      ObColumnRefRawExpr *col_ref = nullptr;
      ObOpRawExpr *like_op = nullptr;
      if (OB_UNLIKELY(OB_ISNULL(column_list.at(j)) || !column_list.at(j)->is_column_ref_expr())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against column");
      } else if (FALSE_IT(col_ref = static_cast<ObColumnRefRawExpr*>(column_list.at(j)))) {
      } else if (OB_FAIL(ObRawExprUtils::build_like_expr(*params_.expr_factory_,
                                                         params_.session_info_,
                                                         col_ref,
                                                         concat_text_expr,
                                                         like_es_expr,
                                                         like_op))) {
      } else if (OB_FAIL(or_like_param_exprs.push_back(like_op))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (or_like_param_exprs.count() == 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (or_like_param_exprs.count() == 1) {
    like_expr = or_like_param_exprs.at(0);
  } else if (OB_FAIL(ObRawExprUtils::build_or_exprs(*params_.expr_factory_, or_like_param_exprs, like_expr))){
  } else if (OB_FAIL(like_expr->formalize(params_.session_info_))) {
  }

  if (OB_FAIL(ret)) {
  } else if ((scope == T_WHERE_SCOPE || scope == T_HAVING_SCOPE) && cur_match_expr == expr) {
    // where/having + one match against
    ObOpRawExpr *bool_expr = nullptr;
    if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_OP_BOOL, bool_expr))) {
    } else if (OB_ISNULL(bool_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(bool_expr->init_param_exprs(1))) {
    } else if (OB_FAIL(bool_expr->add_param_expr(cur_match_expr))) {
    } else if (OB_FAIL(bool_expr->add_flag(IS_INNER_ADDED_EXPR))) {
    } else if (OB_FAIL(bool_expr->formalize(params_.session_info_))) {
    } else {
      new_match_expr = bool_expr;
      need_replace_expr = cur_match_expr;
    }
  } else if (OB_FAIL(ObTransformUtils::find_parent_expr(expr, cur_match_expr, match_parent_expr))) {
  } else if (nullptr == match_parent_expr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (match_parent_expr->is_bool_expr() || match_parent_expr->get_expr_type() == T_OP_BOOL) {
    // when case 'not match against()', the expr tree: not expr -> op_bool expr -> match expr
    new_match_expr = match_parent_expr;
    need_replace_expr = match_parent_expr;
  } else {
    // case when
    result_is_bool = false;
    ObConstRawExpr *const_double_expr = nullptr;
    constexpr double out_put = 0;
    if (OB_FAIL(ObRawExprUtils::build_const_double_expr(*params_.expr_factory_, ObDoubleType, out_put, const_double_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_case_when_expr(*params_.expr_factory_, like_expr, cur_match_expr, const_double_expr, new_match_expr))) {
    } else if (OB_FAIL(new_match_expr->formalize(params_.session_info_))) {
    } else {
      need_replace_expr = cur_match_expr;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!result_is_bool) {
    result_op = new_match_expr;
  } else if (OB_FAIL(and_param_exprs.push_back(new_match_expr))) {
  } else if (OB_FAIL(and_param_exprs.push_back(like_expr))) {
  } else if (OB_FAIL(ObRawExprUtils::build_and_expr(*params_.expr_factory_, and_param_exprs, and_op))) {
  } else if (OB_FAIL(and_op->formalize(params_.session_info_))) {
  } else if (FALSE_IT(result_op = and_op)) {
  }
  ObRawExprReplacer replacer;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(replacer.add_replace_expr(need_replace_expr, result_op))) {
  } else if (OB_FAIL(replacer.replace(expr))) {
  }
  return ret;
}

int ObDMLResolver::resolve_match_against_expr(ObMatchFunRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr.get_param_expr(0)) || OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_)
      || OB_ISNULL(get_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("column list is invalid", K(expr.get_param_expr(0)), K(get_stmt()));
  } else {
    const TableItem *table_item = NULL;
    const ObTableSchema *table_schema = nullptr;
    ObIArray<ObRawExpr*> &column_list = expr.get_match_columns();
    uint64_t table_id = OB_INVALID_ID;
    ColumnReferenceSet column_set;
    const ObColumnSchemaV2 *fulltext_col = NULL;

    // get matched fulltext index
    for (int64_t i = 0; OB_SUCC(ret) && i < column_list.count(); ++i) {
      ObColumnRefRawExpr *col_ref = nullptr;
      if (OB_UNLIKELY(OB_ISNULL(column_list.at(i)) || !column_list.at(i)->is_column_ref_expr())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against column");
      } else if (FALSE_IT(col_ref = static_cast<ObColumnRefRawExpr*>(column_list.at(i)))) {
      } else if (OB_FAIL(column_set.add_member(col_ref->get_column_id()))) {
      } else if (0 == i) {
        table_id = col_ref->get_table_id();
      } else if (OB_UNLIKELY(col_ref->get_table_id() != table_id)) {
        //check all table id
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against columns on different tables");
      }
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_ISNULL(table_item = get_stmt()->get_table_item_by_id(table_id))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table item don't exist", K(table_id));
    } else if (!table_item->is_basic_table()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against column on non-base table");
    } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                         table_item->ref_id_,
                                                         table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(resolve_match_index(column_set, *table_schema, expr))) {
    } else if (OB_FAIL(expr.formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::resolve_es_match_expr(ObMatchFunRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr.get_param_expr(0)) || OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_)
      || OB_ISNULL(get_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("column list is invalid", K(expr.get_param_expr(0)), K(get_stmt()));
  } else {
    const TableItem *table_item = NULL;
    const ObTableSchema *table_schema = nullptr;
    ObIArray<ObRawExpr*> &column_list = expr.get_match_columns();
    uint64_t table_id = OB_INVALID_ID;
    ColumnReferenceSet column_set;
    const ObColumnSchemaV2 *column_schema = nullptr;
    ObCollationType collation_type = CS_TYPE_MAX;

    // get matched fulltext index
    for (int64_t i = 0; OB_SUCC(ret) && i < column_list.count(); ++i) {
      ObColumnRefRawExpr *col_ref = nullptr;
      if (OB_UNLIKELY(OB_ISNULL(column_list.at(i)) || !column_list.at(i)->is_column_ref_expr())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against column");
      } else if (FALSE_IT(col_ref = static_cast<ObColumnRefRawExpr*>(column_list.at(i)))) {
      } else if (OB_FAIL(column_set.add_member(col_ref->get_column_id()))) {
      } else if (0 == i && FALSE_IT(table_id = col_ref->get_table_id())) {
      } else if (OB_UNLIKELY(col_ref->get_table_id() != table_id)) {
        //check all table id
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against columns on different tables");
      } else if (OB_ISNULL(table_item = get_stmt()->get_table_item_by_id(table_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table item don't exist", K(table_id));
      } else if (!table_item->is_basic_table()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "match against column on non-base table");
      } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                           table_item->ref_id_,
                                                           table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolve_match_index(column_set, *table_schema, expr))) {
      } else if (FALSE_IT(column_set.reset())) {
      } else if (OB_FAIL(schema_checker_->get_column_schema(
                                                            table_item->ref_id_,
                                                            col_ref->get_column_id(),
                                                            column_schema))) {
      } else if (0 == i && FALSE_IT(collation_type = column_schema->get_collation_type())) {
      } else if (OB_UNLIKELY(column_schema->get_collation_type() != collation_type)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "MATCH: Columns have different collation types");
      }
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(expr.formalize(session_info_))) {
    }
  }
  return ret;
}

int ObDMLResolver::resolve_match_index(
    const ColumnReferenceSet &match_column_set,
    const ObTableSchema &table_schema,
    ObMatchFunRawExpr &match_against)
{
  int ret = OB_SUCCESS;
  int64_t inv_idx_tid = OB_INVALID_ID;
  int64_t fwd_idx_tid = OB_INVALID_ID;
  int64_t doc_rowkey_tid = OB_INVALID_ID;
  uint64_t column_id = OB_INVALID_ID;
  uint64_t database_id = OB_INVALID_ID;
  ObSEArray<ObAuxTableMetaInfo, 4> index_infos;
  const ObTableSchema *doc_rowkey_schema = nullptr;
  const ObTableSchema *inv_idx_schema = nullptr;
  const ObTableSchema *fwd_idx_schema = nullptr;
  uint64_t docid_col_id = OB_INVALID_ID;
  if (OB_FAIL(table_schema.get_docid_col_id(docid_col_id))) {
    if (OB_ERR_INDEX_KEY_NOT_FOUND == ret) {
      ret = OB_SUCCESS;
    } else {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(table_schema.get_simple_index_infos(index_infos))) {
  } else if (OB_INVALID_ID != docid_col_id) {
    database_id = table_schema.get_database_id();
    for (int64_t i = 0; i < index_infos.count(); ++i) {
      if (share::schema::is_doc_rowkey_aux(index_infos.at(i).index_type_)) {
        doc_rowkey_tid = index_infos.at(i).table_id_;
        break;
      }
    }
    if (OB_UNLIKELY(OB_INVALID_ID == doc_rowkey_tid)) {
      ret = OB_ERR_FT_COLUMN_NOT_INDEXED;
    } else if (OB_FAIL(schema_checker_->get_table_schema( doc_rowkey_tid, doc_rowkey_schema))) {
    } else if (OB_ISNULL(doc_rowkey_schema)) {
      ret = OB_ERR_FT_COLUMN_NOT_INDEXED;
    } else if (!doc_rowkey_schema->can_read_index() || !doc_rowkey_schema->is_index_visible()) {
      ret = OB_ERR_FT_COLUMN_NOT_INDEXED;
    }
  }
  bool found_matched_index = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < index_infos.count() && !found_matched_index; ++i) {
    inv_idx_schema = nullptr;
    const ObAuxTableMetaInfo &index_info = index_infos.at(i);
    if (!share::schema::is_fts_index_aux(index_info.index_type_)) {
      // skip
    } else if (OB_FAIL(schema_checker_->get_table_schema( index_info.table_id_, inv_idx_schema))) {
    } else if (OB_ISNULL(inv_idx_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!inv_idx_schema->can_read_index() || !inv_idx_schema->is_index_visible()) {
      // index unavailable
    } else if (OB_FAIL(ObTransformUtils::check_fulltext_index_match_column(match_column_set,
                                                                           &table_schema,
                                                                           inv_idx_schema,
                                                                           found_matched_index))) {
    } else if (found_matched_index) {
        inv_idx_tid = index_info.table_id_;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(!found_matched_index)) {
    ret = OB_ERR_FT_COLUMN_NOT_INDEXED;
  } else {
    // find matched inverted index and forward index
    bool found_fwd_idx = false;
    const ObString &inv_idx_name = inv_idx_schema->get_table_name_str();
    for (int64_t i = 0; OB_SUCC(ret) && i < index_infos.count(); ++i) {
      const ObAuxTableMetaInfo &index_info = index_infos.at(i);
      if (!share::schema::is_fts_doc_word_aux(index_info.index_type_)) {
        // skip
      } else if (OB_FAIL(schema_checker_->get_table_schema( index_info.table_id_, fwd_idx_schema))) {
      } else if (OB_ISNULL(fwd_idx_schema)) {
        ret = OB_ERR_FT_COLUMN_NOT_INDEXED;
      } else if (!fwd_idx_schema->get_table_name_str().prefix_match(inv_idx_name)) {
        // skip
      } else if (!fwd_idx_schema->can_read_index() || !fwd_idx_schema->is_index_visible()) {
        // index unavailable
      } else {
        found_fwd_idx = true;
        fwd_idx_tid = fwd_idx_schema->get_table_id();
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(!found_fwd_idx)) {
        ret = OB_ERR_FT_COLUMN_NOT_INDEXED;
      }
    }
  }
  return ret;
}

int ObDMLResolver::add_udt_dependency(const pl::ObUserDefinedType &udt_type)
{
  int ret = OB_SUCCESS;

  ObStmt *stmt = get_stmt();
  const uint64_t udt_id = udt_type.get_user_type_id();

  if (OB_ISNULL(stmt) || OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
  } else if (udt_id == OB_INVALID_ID) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected invalid udt id", K(udt_type));
  } else if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(params_.schema_checker_->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < udt_type.get_member_count(); ++i) {
      const ObPLDataType *element_type = udt_type.get_member(i);
      const ObUserDefinedType *user_type = NULL;
      if (OB_ISNULL(element_type)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (element_type->is_obj_type()) { // basic type
      } else { // composite type
        ObPLPackageGuard package_guard{};
        if (OB_FAIL(ObResolverUtils::get_user_type(
              params_.allocator_, params_.session_info_, params_.plan_cache_, params_.sql_proxy_,
              params_.schema_checker_->get_schema_guard(),
              package_guard,
              element_type->get_user_type_id(),
              user_type))) {
        } else if (OB_ISNULL(user_type)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(SMART_CALL(add_udt_dependency(*user_type)))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObSchemaGetterGuard &schema_guard = *params_.schema_checker_->get_schema_guard();
    ObSchemaObjVersion version;
    switch (udt_type.get_type_from()) {
    case PL_TYPE_PACKAGE: {
      const ObPackageInfo *pkg_info = nullptr;
      const uint64_t pkg_id = extract_package_id(udt_id);
      
      if (OB_FAIL(schema_guard.get_package_info( pkg_id, pkg_info))) {
      } else if (OB_ISNULL(pkg_info)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (FALSE_IT(version = ObSchemaObjVersion(pkg_id,
                                                       pkg_info->get_schema_version(),
                                                       DEPENDENCY_PACKAGE))) {
        // unreachable
      } else if (OB_FAIL(stmt->add_global_dependency_table(version))) {
      }
      break;
    }
    case PL_TYPE_ATTR_TYPE:
    case PL_TYPE_ATTR_ROWTYPE: {
      const ObTableSchema* table_schema = nullptr;

      if (OB_ISNULL(params_.session_info_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_guard.get_table_schema( udt_id, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (FALSE_IT(version = ObSchemaObjVersion(udt_id,
                                                       table_schema->get_schema_version(),
                                                       DEPENDENCY_TABLE))) {
        // unreachable
      } else if (OB_FAIL(stmt->add_global_dependency_table(version))) {
      }
      break;
    }
    default: {
      // do nothing
    }
    }
  }

  return ret;
}

int ObDMLResolver::check_domain_id_need_column_ref_expr(ObDMLStmt &stmt, ObSchemaGetterGuard *schema_guard, bool &need_column_ref_expr, const ObColumnSchemaV2 *col_schema)
{
  int ret = OB_SUCCESS;
  need_column_ref_expr = false;
  if (stmt.is_delete_stmt() || stmt.is_update_stmt()) {
    if (col_schema->is_doc_id_column()) {
      const share::schema::ObTableSchema *table = nullptr;
      ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
      const ObSimpleTableSchemaV2 *index_schema = nullptr;
      if (OB_FAIL(schema_checker_->get_table_schema( col_schema->get_table_id(), table))) {
      } else if (OB_FAIL(table->get_simple_index_infos(simple_index_infos))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && !need_column_ref_expr && i < simple_index_infos.count(); ++i) {
        const ObIndexType index_type = simple_index_infos.at(i).index_type_;
        const uint64_t index_tid = simple_index_infos.at(i).table_id_;
        if (is_fts_or_multivalue_index(index_type) && !is_rowkey_doc_aux(index_type)) {
          // has doc_id column on table with valid fulltext / multivalue index
          const share::schema::ObTableSchema *fts_index_schema = nullptr;
          const share::schema::ObTableSchema *rowkey_doc_schema = nullptr;
          uint64_t rowkey_doc_tid = OB_INVALID_ID;
          if (OB_FAIL(schema_checker_->get_table_schema( index_tid, fts_index_schema))) {
          } else if (OB_ISNULL(fts_index_schema)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_UNLIKELY(fts_index_schema->is_final_invalid_index())) {
            // skip invalid index
          } else if (OB_FAIL(table->get_rowkey_doc_tid(rowkey_doc_tid))) {
          } else if (OB_FAIL(schema_checker_->get_table_schema( rowkey_doc_tid, rowkey_doc_schema))) {
          } else if (OB_ISNULL(rowkey_doc_schema)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_UNLIKELY(!rowkey_doc_schema->can_read_index() || !rowkey_doc_schema->is_index_visible())) {
            // rowkey doc table is not readable or not visible, skip
          } else {
            need_column_ref_expr = true;
          }
        }
      }
    } else if (col_schema->is_vec_ivf_center_id_column() || col_schema->is_vec_ivf_pq_center_ids_column()) {
      uint64_t rowkey_cid_tid = OB_INVALID_ID;
      const share::schema::ObTableSchema *table = nullptr;
      const ObSimpleTableSchemaV2 *index_schema = nullptr;
      if (OB_FAIL(schema_checker_->get_table_schema( col_schema->get_table_id(), table))) {
      } else if (OB_FAIL(ObVectorIndexUtil::check_rowkey_cid_table_readable(
          schema_guard,
          *table,
          col_schema->get_column_id(),
          rowkey_cid_tid))) {
      } else if (OB_INVALID_ID != rowkey_cid_tid) {
        need_column_ref_expr = true;
      }
    } else if (col_schema->is_hybrid_embedded_vec_column()) {
      uint64_t embedded_vec_tid = OB_INVALID_ID;
      const share::schema::ObTableSchema *table = nullptr;
      const ObSimpleTableSchemaV2 *index_schema = nullptr;
      if (OB_FAIL(schema_checker_->get_table_schema( col_schema->get_table_id(), table))) {
      } else if (OB_FAIL(ObVectorIndexUtil::check_hybrid_embedded_vec_cid_table_readable(
          schema_guard,
          *table,
          col_schema->get_column_id(),
          embedded_vec_tid))) {
      } else if (OB_INVALID_ID != embedded_vec_tid) {
        need_column_ref_expr = true;
      }
    } else {
      need_column_ref_expr = true;
    }
  } else if (stmt.is_select_stmt() && OB_ISNULL(upper_insert_resolver_)) {
    need_column_ref_expr = true;
  } else if (stmt.is_select_stmt() && OB_NOT_NULL(upper_insert_resolver_)) {
    need_column_ref_expr = true;
    if (OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (session_info_->get_ddl_info().is_ddl()) {
      ObDMLStmt *insert_stmt = upper_insert_resolver_->get_stmt();
      const share::schema::ObTableSchema *ddl_table_schema = nullptr;
      if (OB_ISNULL(insert_stmt) || OB_UNLIKELY(insert_stmt->get_table_items().count() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_checker_->get_table_schema(
              insert_stmt->get_table_item(0)->ddl_table_id_, ddl_table_schema))) {
      } else if (OB_ISNULL(ddl_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (ObDomainIdUtils::check_table_need_column_ref_in_ddl(ddl_table_schema, col_schema)) {
        need_column_ref_expr = false;
      }
    }
  }
  return ret;
}

int ObDMLResolver::check_need_fill_ivf_vec_expr_param(const ObDMLStmt &stmt,
                                                      const ObColumnSchemaV2 &column_schema,
                                                      ObRawExpr *ref_expr,
                                                      bool &need_fill,
                                                      bool &need_dist_algo_expr)
{
  int ret = OB_SUCCESS;
  need_fill = false;
  need_dist_algo_expr = false;
  if (column_schema.is_vec_ivf_center_id_column()) {
    need_fill = true;
    need_dist_algo_expr = true;
  } else if (column_schema.is_vec_ivf_pq_center_ids_column()) {
    need_fill = true;
    need_dist_algo_expr = true;
    bool is_ddl = false;
    ObIndexType index_type = ObIndexType::INDEX_TYPE_MAX;
    if (stmt.is_insert_stmt()) {
      if (OB_ISNULL(session_info_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (session_info_->get_ddl_info().is_ddl()) {
        need_fill = false;
      }
    } else if (OB_FAIL(get_ivf_index_type_if_ddl(stmt, is_ddl, index_type))) {
    } else if (is_ddl && !share::schema::is_vec_ivfpq_rowkey_cid_index(index_type)
                      && !share::schema::is_vec_ivfpq_code_index(index_type)) {
      need_fill = false;
    }
  } else if (column_schema.is_vec_ivf_data_vector_column() || column_schema.is_vec_ivf_center_vector_column()) {
    if (OB_ISNULL(ref_expr)) {
      ret = OB_ERR_NULL_VALUE;
    } else if (column_schema.is_vec_ivf_data_vector_column()
            && T_FUN_SYS_VEC_IVF_SQ8_DATA_VECTOR == ref_expr->get_expr_type()) {
      need_fill = true;
    } else if (column_schema.is_vec_ivf_data_vector_column() && T_FUN_SYS_VEC_IVF_FLAT_DATA_VECTOR == ref_expr->get_expr_type()) {
      need_fill = true;
      need_dist_algo_expr = true;
    } else if (column_schema.is_vec_ivf_center_vector_column()
            && T_FUN_SYS_VEC_IVF_PQ_CENTER_VECTOR == ref_expr->get_expr_type()) {
      need_fill = true;
      need_dist_algo_expr = true;
    }
  }
  return ret;
}

int ObDMLResolver::get_ivf_index_type_if_ddl(const ObDMLStmt &stmt, bool &is_ddl, ObIndexType &index_type)
{
  int ret = OB_SUCCESS;
  is_ddl = false;
  if (stmt.is_select_stmt() && OB_NOT_NULL(upper_insert_resolver_)) {
    if (OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (session_info_->get_ddl_info().is_ddl()) {
      ObDMLStmt *insert_stmt = upper_insert_resolver_->get_stmt();
      const share::schema::ObTableSchema *ddl_table_schema = nullptr;
      is_ddl = true;
      if (OB_ISNULL(insert_stmt) || OB_UNLIKELY(insert_stmt->get_table_items().count() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_checker_->get_table_schema(
              insert_stmt->get_table_item(0)->ddl_table_id_, ddl_table_schema))) {
      } else if (OB_ISNULL(ddl_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        index_type = ddl_table_schema->get_index_type();
      }
    }
  }
  return ret;
}

int ObDMLResolver::check_need_fill_embedded_vec_expr_param(const ObDMLStmt &stmt,
                                                           const ObColumnSchemaV2 &column_schema,
                                                           bool &need_fill)
{
  int ret = OB_SUCCESS;
  need_fill = false;
  if (column_schema.is_hybrid_embedded_vec_column()) {
    need_fill = true;
    // is ddl task, need_fill set false
    if (stmt.is_insert_stmt()) {
      if (OB_ISNULL(session_info_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (session_info_->get_ddl_info().is_ddl()) {
        need_fill = false;
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
