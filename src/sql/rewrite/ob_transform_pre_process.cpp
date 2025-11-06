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

#define USING_LOG_PREFIX SQL_REWRITE
#include "sql/rewrite/ob_transform_pre_process.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "sql/engine/expr/ob_expr_arg_case.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/rewrite/ob_expand_aggregate_utils.h"
#include "pl/ob_pl_resolver.h"
#include "sql/engine/expr/ob_expr_align_date4cmp.h"
#include "share/vector_index/ob_vector_index_util.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
using namespace common;
namespace sql
{
int ObTransformPreProcess::transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                              ObDMLStmt *&stmt,
                                              bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  bool is_happened = false;
  ObDMLStmt *limit_stmt = NULL;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected NULL", K(ret), K(stmt), K(ctx_));
  } else if (OB_FAIL(ObTransformUtils::right_join_to_left(stmt))) {
    LOG_WARN("failed to transform right join as left", K(ret));
  } else if (OB_FAIL(stmt->adjust_duplicated_table_names(*ctx_->allocator_, is_happened))) {
    LOG_WARN("failed to adjust duplicated table names", K(ret));
  } else if (OB_FAIL(THIS_WORKER.check_status())) {
    LOG_WARN("check status failed", K(ret));
  } else {
    trans_happened |= is_happened;
    OPT_TRACE("adjust duplicated table name", is_happened);
    LOG_TRACE("succeed to adjust duplicated table name", K(is_happened), K(ret));

    if (OB_SUCC(ret)) {
      if (OB_FAIL(expand_materialized_view(stmt, is_happened))) {
        LOG_WARN("failed to expand materialized view", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("expand materialized view:", is_happened);
        LOG_TRACE("succeed to expand materialized view",K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(flatten_conditions(stmt, is_happened))) {
        LOG_WARN("failed to flatten_condition", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("flatten condition:", is_happened);
        LOG_TRACE("succeed to flatten_condition", K(is_happened));
      }
    }  
    if (OB_SUCC(ret) && is_mysql_mode()) {
      if (OB_FAIL(try_gen_straight_join_leading(stmt, is_happened))) {
        LOG_WARN("failed to generate straight join leading", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("generate straight join leading", is_happened);
        LOG_TRACE("succeed to generate straight join leading", K(is_happened), K(ret));
      }
    }  
    if (OB_SUCC(ret) && parent_stmts.empty()) {
      if (OB_FAIL(expand_correlated_cte(stmt, is_happened))) {
        LOG_WARN("failed to expand correlated cte", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("expand correlated cte", is_happened);
        LOG_TRACE("succeed to expand correlated cte", K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_json_object_expr_with_star(parent_stmts, stmt, is_happened))) {
        LOG_WARN("failed to transform for json object star", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform for udt columns", is_happened);
        LOG_TRACE("succeed to transform for udt columns", K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (!stmt->has_vec_approx() && OB_FAIL(transform_semantic_vector_dis_expr(stmt, is_happened))) {
        LOG_WARN("failed to transform hybrid semantic vector distance expr", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform hybrid semantic vector distance expr:", is_happened);
        LOG_TRACE("succeed to transform hybrid semantic vector distance expr", K(is_happened));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_cast_multiset_for_stmt(stmt, is_happened))) {
        LOG_WARN("failed to transform for transform for cast multiset", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform for cast multiset", is_happened);
        LOG_TRACE("succeed to transform for cast multiset", K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(add_all_rowkey_columns_to_stmt(stmt, is_happened))) {
        LOG_WARN("faield to add all rowkey columns", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("add all rowkey columns:", is_happened);
        LOG_TRACE("succeed to add all rowkey columns", K(is_happened));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(eliminate_having(stmt, is_happened))) {
        LOG_WARN("failed to elinimate having", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("eliminating having statement:", is_happened);
        LOG_TRACE("succeed to eliminating having statement", K(is_happened));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(replace_func_is_serving_tenant(stmt, is_happened))) {
        LOG_WARN("failed to replace function is_serving_tenant", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("replace is_serving_tenant function:", is_happened);
        LOG_TRACE("succeed to replace function", K(is_happened));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_special_expr(stmt, is_happened))) {
        LOG_WARN("failed to transform_special_expr", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform_special_expr:", is_happened);
        LOG_TRACE("succeed to transform_special_expr", K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_outerjoin_exprs(stmt, is_happened))) {
        LOG_WARN("failed to transform outer join exprs", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform outer join exprs:", is_happened);
        LOG_TRACE("succeed to transform outer join exprs", K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_for_batch_stmt(stmt, is_happened))) {
        LOG_WARN("failed to transform for batch update", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform for batch stmt:", is_happened);
        LOG_TRACE("succeed to transform for batch stmt", K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_exprs(stmt, is_happened))) {
        LOG_WARN("transform exprs failed", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform exprs:", is_happened);
        LOG_TRACE("success to transform exprs", K(is_happened));
      }
    }
    /*transform_for_nested_aggregate, transformer_aggr_expr two functions are strongly dependent, must ensure the order of rewriting both*/
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_for_nested_aggregate(stmt, is_happened))) {
        LOG_WARN("failed to transform for nested aggregate.", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform for nested aggregate:", is_happened);
        LOG_TRACE("succeed to transform for nested aggregate", K(is_happened), K(ret));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(transformer_aggr_expr(stmt, is_happened))) {
          LOG_WARN("failed to transform aggr expr", K(ret));
        } else {
          trans_happened |= is_happened;
          OPT_TRACE("transform aggr expr:", is_happened);
          LOG_TRACE("succeed to transform aggr expr", K(is_happened), K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_full_outer_join(stmt, is_happened))) {
        LOG_WARN("failed to transform full outer join", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("transform full outer join:", is_happened);
        LOG_TRACE("succeed to transform full outer join", K(is_happened));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(preserve_order_for_pagination(NULL == limit_stmt ? stmt : limit_stmt, is_happened))) {
        LOG_WARN("failed to preserve order for pagination", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("preserve order for pagination:", is_happened);
        LOG_TRACE("succeed to preserve order for pagination", K(is_happened));
      }
    }

     if (OB_SUCC(ret)) {
      if (OB_FAIL(preserve_order_for_gby(stmt, is_happened))) {
        LOG_WARN("failed to preserve order for groupby", K(ret));
      } else {
        trans_happened |= is_happened;
        OPT_TRACE("preserve order for groupby:", is_happened);
        LOG_TRACE("succeed to preserve order for groupby", K(is_happened));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_rollup_exprs(stmt, is_happened))) {
        LOG_WARN("failed to transform rollup exprs", K(ret));
      } else {
        trans_happened |= is_happened;
        LOG_TRACE("succeed to transform rollup exprs",  K(is_happened));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(transform_for_last_insert_id(stmt, is_happened))) {
        LOG_WARN("failed to transform for last_insert_id.", K(ret));
      } else {
        trans_happened |= is_happened;
        LOG_TRACE("succeed to transform for last_insert_id.",K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (lib::is_mysql_mode() && stmt->get_match_exprs().count() > 0 &&
          OB_FAIL(preserve_order_for_fulltext_search(stmt, is_happened))) {
        LOG_WARN("failed to preserve order for fulltext search", K(ret));
      } else {
        trans_happened |= is_happened;
        LOG_TRACE("succeed to transform for preserve order for fulltext search",K(is_happened), K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(reset_view_base(stmt))) {
      LOG_WARN("failed to reset view base item", K(ret));
    }
    if (OB_SUCC(ret)) {
      LOG_DEBUG("transform pre process succ", K(*stmt));
     if (OB_FAIL(stmt->formalize_stmt(ctx_->session_info_))) {
        LOG_WARN("failed to formalize stmt", K(ret));
      //} else if (OB_FAIL(stmt->formalize_stmt_expr_reference())) {
      //  LOG_WARN("failed to formalize stmt reference", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::need_transform(const common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                          const int64_t current_level,
                                          const ObDMLStmt &stmt,
                                          bool &need_trans)
{
  UNUSED(parent_stmts);
  UNUSED(current_level);
  UNUSED(stmt);
  need_trans = true;
  return OB_SUCCESS;
}

// for realtime view, replace basic table as generated view
int ObTransformPreProcess::expand_materialized_view(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  const ObHint *hint = NULL;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->session_info_)
      || OB_ISNULL(stmt->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null", K(ret), K(stmt), K(ctx_));
  } else if (ctx_->session_info_->get_ddl_info().is_refreshing_mview()
             || ctx_->session_info_->get_ddl_info().is_major_refreshing_mview()
             || stmt->get_query_ctx()->get_global_hint().has_dbms_stats_hint()) {
    // 1. when refresh mview, do not expand rt-mv
    // 2. when gather stat, do not expand rt-mv
  } else if (NULL != (hint = stmt->get_stmt_hint().get_normal_hint(T_MV_REWRITE))
             && hint->is_disable_hint()) {
    // use no_mv_rewrite to disable expand rt mview
  } else {
    ObIArray<TableItem*> &tables = stmt->get_table_items();
    TableItem *table_item = NULL;
    bool is_modified = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
      if (OB_ISNULL(table_item = tables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null", K(ret), K(table_item));
      } else if (MATERIALIZED_VIEW != table_item->table_type_
                 || !table_item->need_expand_rt_mv_) {
        /* do nothing */
      } else if (OB_FAIL(stmt->check_table_be_modified(table_item->ref_id_, is_modified))) {
        LOG_WARN("fail to check table be modified", K(ret));
      } else if (is_modified) {
        /* do nothing */
      } else if (OB_UNLIKELY(!table_item->part_ids_.empty())) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "access some partitions of real-time materialized view now");
        LOG_WARN("access some partitions of real-time materialized view is not supported now", K(ret));
      } else if (OB_FAIL(ObTransformUtils::expand_mview_table(ctx_, stmt, table_item))) {
        LOG_WARN("fail to expand mview table", K(ret));
      } else {
        trans_happened = true;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::add_all_rowkey_columns_to_stmt(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->schema_checker_)
      || OB_ISNULL(ctx_->expr_factory_) || OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null", K(ret), K(stmt), K(ctx_));
  } else {
    ObIArray<TableItem*> &tables = stmt->get_table_items();
    TableItem *table_item = NULL;
    const ObTableSchema *table_schema = NULL;
    ObSEArray<ColumnItem, 16> column_items;
    for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
      if (OB_ISNULL(table_item = tables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null", K(ret), K(table_item));
      } else if (!table_item->is_basic_table()) {
        /* do nothing */
      } else if (OB_FAIL(ctx_->schema_checker_->get_table_schema(ctx_->session_info_->get_effective_tenant_id(),
                                                                 table_item->ref_id_,
                                                                 table_schema,
                                                                 table_item->is_link_table()))) {
        LOG_WARN("table schema not found", K(table_schema));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get invalid table schema", K(table_schema));
      } else if (OB_FAIL(add_all_rowkey_columns_to_stmt(*table_schema, *table_item,
                                                        *ctx_->expr_factory_,
                                                        *stmt,
                                                        column_items))) {
        LOG_WARN("add all rowkey exprs failed", K(ret));
      }
    }

    if (OB_SUCC(ret) && !column_items.empty()) {
      ObIArray<ColumnItem> &orign_column_items = stmt->get_column_items();
      for (int i = 0; OB_SUCC(ret) && i < orign_column_items.count(); ++i) {
        if (OB_ISNULL(orign_column_items.at(i).expr_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", K(ret), K(orign_column_items.at(i).expr_));
        } else if (!orign_column_items.at(i).expr_->is_rowkey_column() &&
                   OB_FAIL(column_items.push_back(orign_column_items.at(i)))) {
          LOG_WARN("failed to push back column item", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(stmt->get_column_items().assign(column_items))) {
          LOG_WARN("failed to assign column items", K(ret));
        } else {
          trans_happened = true;
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::add_all_rowkey_columns_to_stmt(const ObTableSchema &table_schema,
                                                          const TableItem &table_item,
                                                          ObRawExprFactory &expr_factory,
                                                          ObDMLStmt &stmt,
                                                          ObIArray<ColumnItem> &column_items)
{
  int ret = OB_SUCCESS;
  const ObRowkeyInfo &rowkey_info = table_schema.get_rowkey_info();
  const ObColumnSchemaV2 *column_schema = NULL;
  uint64_t column_id = OB_INVALID_ID;
  ColumnItem *exists_col_item = NULL;
  ObColumnRefRawExpr *rowkey = NULL;
  for (int col_idx = 0; OB_SUCC(ret) && col_idx < rowkey_info.get_size(); ++col_idx) {
    if (OB_FAIL(rowkey_info.get_column_id(col_idx, column_id))) {
      LOG_WARN("Failed to get column id", K(ret));
    } else if (NULL != (exists_col_item = stmt.get_column_item_by_id(table_item.table_id_,
                                                                     column_id))) {
      if (OB_FAIL(column_items.push_back(*exists_col_item))) {
        LOG_WARN("failed to push back column item", K(ret));
      }
    } else if (OB_MOCK_LINK_TABLE_PK_COLUMN_ID == column_id && table_item.is_link_table()) {
      continue;
    } else if (OB_ISNULL(column_schema = (table_schema.get_column_schema(column_id)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get column schema", K(column_id), K(ret));
    } else if (OB_FAIL(ObRawExprUtils::build_column_expr(expr_factory, *column_schema,
                                                         ctx_->session_info_, rowkey))) {
      LOG_WARN("build column expr failed", K(ret));
    } else if (OB_ISNULL(rowkey)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to create raw expr for dummy output", K(ret));
    } else {
      ColumnItem column_item;
      rowkey->set_ref_id(table_item.table_id_, column_schema->get_column_id());
      rowkey->set_column_attr(table_item.get_table_name(), column_schema->get_column_name_str());
      rowkey->set_database_name(table_item.database_name_);
      if (!table_item.alias_name_.empty()) {
        rowkey->set_table_alias_name();
      }
      column_item.expr_ = rowkey;
      column_item.table_id_ = rowkey->get_table_id();
      column_item.column_id_ = rowkey->get_column_id();
      column_item.base_tid_ = table_item.ref_id_;
      column_item.base_cid_ = rowkey->get_column_id();
      column_item.column_name_ = rowkey->get_column_name();
      column_item.set_default_value(column_schema->get_cur_default_value());
      if (OB_FAIL(stmt.add_column_item(column_item))) {
        LOG_WARN("add column item to stmt failed", K(ret));
      } else if (OB_FAIL(column_items.push_back(column_item))) {
        LOG_WARN("failed to push back column item", K(ret));
      } else if (FALSE_IT(rowkey->clear_explicited_referece())) {
      } else if (OB_ISNULL(ctx_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("trans ctx is null", K(ret));
      } else if (OB_FAIL(rowkey->formalize(ctx_->session_info_))) {
        LOG_WARN("formalize rowkey failed", K(ret));
      } else if (OB_FAIL(rowkey->pull_relation_id())) {
        LOG_WARN("failed to pullup relation ids", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::formalize_limit_expr(ObDMLStmt &stmt)
{
  int ret = OB_SUCCESS;

  ObRawExpr *limit_expr = stmt.get_limit_expr();
  ObRawExpr *offset_expr = stmt.get_offset_expr();
  ObRawExpr *percent_expr = stmt.get_limit_percent_expr();
  ObSEArray<ObRawExpr *, 4> params;
  bool transed_zero = false;
  ObConstRawExpr *one_expr = NULL;
  ObConstRawExpr *zero_expr = NULL;
  if (OB_ISNULL(ctx_) || OB_ISNULL(ctx_->exec_ctx_) || OB_ISNULL(ctx_->exec_ctx_->get_physical_plan_ctx())
      || OB_ISNULL(ctx_->allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctx is null", K(ret));
  } else if (limit_expr != NULL) {
    bool is_null_value = false;
    int64_t limit_value = 0;
    ObRawExpr *cmp_expr = NULL;
    if (OB_FAIL(ObTransformUtils::get_expr_int_value(limit_expr,
                                                  &ctx_->exec_ctx_->get_physical_plan_ctx()->get_param_store(),
                                                  ctx_->exec_ctx_,
                                                  ctx_->allocator_,
                                                  limit_value,
                                                  is_null_value))) {
      LOG_WARN("failed to get_expr_int_value", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(*ctx_->expr_factory_,
                                                            ObIntType,
                                                            1,
                                                            one_expr))) {
      LOG_WARN("build expr failed", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(*ctx_->expr_factory_,
                                                            ObIntType,
                                                            0,
                                                            zero_expr))) {
      LOG_WARN("build expr failed", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::create_double_op_expr(*ctx_->expr_factory_, ctx_->session_info_, T_OP_GE, 
                                                            cmp_expr, limit_expr, one_expr))) {
      LOG_WARN("create_double_op_expr_failed", K(ret));

    } else if (!is_null_value && limit_value >= 1) {
      if (OB_FAIL(ObTransformUtils::add_param_bool_constraint(ctx_, cmp_expr, true))) {
        LOG_WARN("add param failed", K(ret));
      }
    } else {
      stmt.set_limit_offset(zero_expr, NULL);
      transed_zero = true;
      if (OB_FAIL(params.push_back(cmp_expr))) {
        LOG_WARN("push back failed", K(ret));
      }
    }
  } else if (percent_expr != NULL) {
    bool is_null_value = false;
    double limit_value = 0;
    ObRawExpr *cmp_expr = NULL;
    ObConstRawExpr *double_zero_expr = NULL;
    if (OB_FAIL(ObTransformUtils::get_percentage_value(percent_expr,
                                                       &stmt,
                                                       &ctx_->exec_ctx_->get_physical_plan_ctx()->get_param_store(),
                                                       ctx_->exec_ctx_,
                                                       ctx_->allocator_,
                                                       limit_value,
                                                       is_null_value))) {
      LOG_WARN("failed to get_percentage_value", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::build_const_double_expr(*ctx_->expr_factory_,
                                                               ObDoubleType,
                                                               0,
                                                               double_zero_expr))) {
      LOG_WARN("build expr failed", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(*ctx_->expr_factory_,
                                                            ObIntType,
                                                            0,
                                                            zero_expr))) {
      LOG_WARN("build expr failed", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::create_double_op_expr(*ctx_->expr_factory_, ctx_->session_info_, T_OP_GT, 
                                                             cmp_expr, percent_expr, double_zero_expr))) {
      LOG_WARN("create_double_op_expr_failed", K(ret));
    } else if (!is_null_value && limit_value > 0) {
      if (OB_FAIL(ObTransformUtils::add_param_bool_constraint(ctx_, cmp_expr, true))) {
        LOG_WARN("add param failed", K(ret));
      }
    } else {
      stmt.set_limit_percent_expr(NULL);
      stmt.set_limit_offset(zero_expr, NULL);
      transed_zero = true;
      if (OB_FAIL(params.push_back(cmp_expr))) {
        LOG_WARN("push back failed", K(ret));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (offset_expr != NULL) {
    bool is_null_value = false;
    int64_t offset_value = 0;
    ObRawExpr *cmp_expr = NULL;
    if (OB_FAIL(ObTransformUtils::get_expr_int_value(offset_expr,
                                                  &ctx_->exec_ctx_->get_physical_plan_ctx()->get_param_store(),
                                                  ctx_->exec_ctx_,
                                                  ctx_->allocator_,
                                                  offset_value,
                                                  is_null_value))) {
      LOG_WARN("failed to get_expr_int_value", K(ret));
    } else if (zero_expr == NULL && OB_FAIL(ObRawExprUtils::build_const_int_expr(*ctx_->expr_factory_,
                                                            ObIntType,
                                                            0,
                                                            zero_expr))) {
      LOG_WARN("build expr failed", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::create_double_op_expr(*ctx_->expr_factory_, ctx_->session_info_, T_OP_GE, 
                                                              cmp_expr, offset_expr, zero_expr))) {
      LOG_WARN("create_double_op_expr_failed", K(ret));
    } else if (offset_value >= 0) {
      if (OB_FAIL(ObTransformUtils::add_param_bool_constraint(ctx_, cmp_expr, true))) {
        LOG_WARN("add param failed", K(ret));
      }
    } else if (is_null_value) {
      stmt.set_limit_offset(zero_expr, NULL);
      transed_zero = true;
      ObRawExpr *new_cond = NULL; 
      if (OB_FAIL(ObRawExprUtils::build_is_not_null_expr(*ctx_->expr_factory_, offset_expr,
                                                     true, new_cond))) {
        LOG_WARN("build expr failed", K(ret));
      } else if (OB_FAIL(params.push_back(new_cond))) {
        LOG_WARN("push back failed", K(ret));
      }
    } else {
      stmt.set_limit_offset(stmt.get_limit_expr(), zero_expr);
      transed_zero = true;
      if (OB_FAIL(ObTransformUtils::add_param_bool_constraint(ctx_, cmp_expr, false))) {
        LOG_WARN("add param failed", K(ret));
      }
    }
  } 


  if (OB_SUCC(ret) && params.count() > 0 && transed_zero) {
    ObRawExpr* and_expr = NULL;
    if (OB_FAIL(ObRawExprUtils::build_and_expr(*ctx_->expr_factory_, params, and_expr))) {
      LOG_WARN("build and expr failed", K(ret));
    } else  if (OB_FAIL(and_expr->formalize(ctx_->session_info_))) {
      LOG_WARN("formalize failed", K(ret));
    } else if (OB_FAIL(ObTransformUtils::add_param_bool_constraint(ctx_, and_expr, false))) {
      LOG_WARN("add param failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObSEArray<ObSelectStmt *, 2> child_stmts;
    if (OB_FAIL(stmt.get_child_stmts(child_stmts))) {
      LOG_WARN("get child_stmts failed", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < child_stmts.count(); i++) {
      if (OB_ISNULL(child_stmts.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt is null", K(ret));
      } else if (OB_FAIL(SMART_CALL(formalize_limit_expr(*child_stmts.at(i))))) {
        LOG_WARN("formalize limit expr failed", K(ret));
      }
    }
  }
  return ret;
}


int ObTransformPreProcess::is_subquery_correlated(const ObSelectStmt *stmt,
                                                  hash::ObHashSet<uint64_t> &param_set,
                                                  bool &is_correlated)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr *> relation_exprs;
  ObArray<ObSelectStmt *> child_stmts;
  if (OB_FAIL(stmt->get_relation_exprs(relation_exprs))) {
    LOG_WARN("failed to get relation exprs", K(ret));
  } else if (OB_FAIL(stmt->get_child_stmts(child_stmts))) {
    LOG_WARN("failed to get child stmts", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && !is_correlated && i < relation_exprs.count(); ++i) {
    if (OB_FAIL(has_new_exec_param(relation_exprs.at(i), param_set, is_correlated))) {
      LOG_WARN("failed to check has new exec param", K(ret));
    }
  }
  if (OB_SUCC(ret) && !is_correlated && OB_FAIL(add_exec_params(*stmt, param_set))) {
    LOG_WARN("failed to add exec params", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && !is_correlated && i < child_stmts.count(); ++i) {
    if (OB_FAIL(SMART_CALL(is_subquery_correlated(child_stmts.at(i), 
                                                  param_set, 
                                                  is_correlated)))) {
      LOG_WARN("failed to check is subquery correlated", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::add_exec_params(const ObSelectStmt &stmt,
                                           hash::ObHashSet<uint64_t> &param_set)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.get_subquery_expr_size(); ++i) {
    ObQueryRefRawExpr *query_ref = stmt.get_subquery_exprs().at(i);
    if (OB_ISNULL(query_ref)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("query_ref is null", K(ret));
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < query_ref->get_exec_params().count(); ++j) {
      ObExecParamRawExpr *exec_param = query_ref->get_exec_param(j);
      uint64_t key = reinterpret_cast<uint64_t>(exec_param);
      if (OB_FAIL(param_set.set_refactored(key))) {
        if (OB_HASH_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to add expr into set", K(ret));
        }
      }
    }
  }
  
  return ret;
}

int ObTransformPreProcess::has_new_exec_param(const ObRawExpr *expr,
                                              const hash::ObHashSet<uint64_t> &param_set,
                                              bool &has_new)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is null", K(ret), K(expr));
  } else if (expr->is_exec_param_expr()) {
    uint64_t key = reinterpret_cast<uint64_t>(expr);
    int tmp_ret = param_set.exist_refactored(key);
    if (OB_HASH_NOT_EXIST == tmp_ret) {
      has_new = true;
    } else if (OB_UNLIKELY(OB_HASH_EXIST != tmp_ret)) {
      ret = tmp_ret;
      LOG_WARN("failed to check hash set", K(ret), K(ret));
    }
  } else if (expr->has_flag(CNT_DYNAMIC_PARAM)) {
    for (int64_t i = 0; OB_SUCC(ret) && !has_new && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(SMART_CALL(has_new_exec_param(expr->get_param_expr(i),
                                                param_set,
                                                has_new)))) {
        LOG_WARN("failed to check has new exec param", K(ret));
      }
    }
  }
  return ret;
}


int ObTransformPreProcess::calc_grouping_in_grouping_sets(const ObIArray<ObRawExpr*> &groupby_exprs,
                                                          const ObIArray<ObRawExpr*> &rollup_exprs,
                                                          ObAggFunRawExpr *expr,
                                                          ObRawExpr *&new_expr)
{
  int ret = OB_SUCCESS;
  new_expr = NULL;
  ObConstRawExpr *one_expr = NULL;
  ObSysFunRawExpr *cast_expr = NULL;
  if (OB_ISNULL(expr) ||
      OB_ISNULL(ctx_) ||
      OB_ISNULL(ctx_->expr_factory_) ||
      OB_ISNULL(ctx_->session_info_) ||
      expr->get_expr_type() != T_FUN_GROUPING) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null expr or wrong type", K(ret), K(expr), K(ctx_));
  } else if (OB_UNLIKELY(expr->get_param_count() != 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(*expr));
  } else if (ObOptimizerUtil::find_item(groupby_exprs, expr->get_param_expr(0)) ||
             ObOptimizerUtil::find_item(rollup_exprs, expr->get_param_expr(0))) {
    /*do nothing*/
  } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(*ctx_->expr_factory_,
                                                          ObIntType,
                                                          1,
                                                          one_expr))) {
    LOG_WARN("failed to build const int expr", K(ret));
  } else if (OB_FAIL(ObRawExprUtils::create_cast_expr(*ctx_->expr_factory_,
                                                      one_expr,
                                                      expr->get_result_type(),
                                                      cast_expr,
                                                      ctx_->session_info_))) {
    LOG_WARN("create cast expr failed", K(ret));
  } else if (OB_FAIL(cast_expr->add_flag(IS_INNER_ADDED_EXPR))) {
    LOG_WARN("failed to add flag", K(ret));
  } else {
    new_expr = cast_expr;
  }
  return ret;
}

int ObTransformPreProcess::calc_group_type_aggr_func(const ObIArray<ObRawExpr*> &groupby_exprs,
                                                     const ObIArray<ObRawExpr*> &rollup_exprs,
                                                     const ObIArray<ObGroupbyExpr> &groupby_exprs_list,
                                                     const int64_t cur_index,
                                                     const int64_t origin_groupby_num,
                                                     ObAggFunRawExpr *expr,
                                                     ObRawExpr *&new_expr)
{
  int ret = OB_SUCCESS;
  new_expr = NULL;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed. unexpected NULL", K(ret));
  } else if (T_FUN_GROUPING == expr->get_expr_type() &&
             OB_FAIL(calc_grouping_in_grouping_sets(groupby_exprs, rollup_exprs, expr, new_expr))) {
    LOG_WARN("failed to calculate grouping in grouping sets", K(ret), K(*expr));
  } else if (T_FUN_GROUPING_ID == expr->get_expr_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "grouping_id is not supported");
  } else if (T_FUN_GROUP_ID == expr->get_expr_type()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "group_id is not supported");
  }
  return ret;
}

/*@brief, ObTransformPreProcess::convert_select_having_in_groupby_stmt, according to oracle action:
 * if the expr not in group by expr except in aggr, the expr will replaced with NULL; eg:
 *  select c1, c2, max(c1), max(c2) from t1 group by grouping sets(c1,c2) having c1 > 1 or c2 > 1 or sum(c1) > 2 or sum(c2) > 2;
 * <==>
 *  select c1, NULL, max(c1), max(c1) from t1 group by c1 having c1 > 1 or NULL > 1 or sum(c1) > 2 or sum(c2) > 2
 * union all
 *  select NULL, c2, max(c1), max(c1) from t1 group by c2 having NULL > 1 or c2 > 1 or sum(c1) > 2 or sum(c2) > 2;
 *
 *  select nvl(c1,1),c3 from t1 group by grouping sets(nvl(c1,1),c3);
 * <==>
 *  select nvl(c1,1), NULL from t1 group by nvl(c1,1)
 * union all
 *  select NULL, c3 from t1 group by c3;
 *
 * select nvl(c1,1) + c3 from t1 group by grouping sets(nvl(c1,1),c3);
 *
 */


int ObTransformPreProcess::eliminate_having(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected NULL pointer", K(stmt), K(ret));
  } else if (!stmt->is_select_stmt()) {
    // do nothing
  } else {
    ObSelectStmt *select_stmt = static_cast<ObSelectStmt *>(stmt);
    if (!select_stmt->has_group_by() && !select_stmt->get_having_exprs().empty()) {
      if (OB_FAIL(append(select_stmt->get_condition_exprs(), select_stmt->get_having_exprs()))) {
        LOG_WARN("failed to append condition exprs", K(ret));
      } else {
        select_stmt->get_having_exprs().reset();
        trans_happened = true;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_func_is_serving_tenant(ObDMLStmt *&stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parameter or data member", K(ret), K(stmt), K(ctx_));
  } else if (OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is NULL", K(ret));
  } else {
    common::ObIArray<ObRawExpr*> &cond_exprs = stmt->get_condition_exprs();
    for (int64_t i = 0; OB_SUCC(ret) && i < cond_exprs.count(); ++i) {
      bool is_happended = false;
      if (OB_ISNULL(cond_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("cond expr is NULL", K(ret), K(i), K(cond_exprs));
      } else if (OB_FAIL(recursive_replace_func_is_serving_tenant(*stmt, cond_exprs.at(i), is_happended))) { // Here must directly pass cond_exprs.at(i), because its value may need to be modified
        LOG_WARN("fail to recursive replace functino is_serving_tenant",
                        K(ret), K(i), K(*cond_exprs.at(i)));
      } else if (!is_happended) {
        //do nothing
      } else if (OB_ISNULL(cond_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null pointer", K(ret));
      } else if (OB_FAIL(cond_exprs.at(i)->formalize(ctx_->session_info_))) {
        LOG_WARN("failed to formalize expr", K(ret), K(i), K(*cond_exprs.at(i)));
      } else if (OB_FAIL(cond_exprs.at(i)->pull_relation_id())) {
        LOG_WARN("failed to pull relation id", K(ret), K(i), K(*cond_exprs.at(i)));
      } else {
        trans_happened = true;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::recursive_replace_func_is_serving_tenant(ObDMLStmt &stmt,
                                                                    ObRawExpr *&cond_expr,
                                                                    bool &trans_happened)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  if (OB_ISNULL(cond_expr) || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->expr_factory_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("cond_expr is NULL", K(cond_expr), K_(ctx));
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
    LOG_WARN("failed to check stack overflow", K(ret));
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too deep recursive", K(ret), K(is_stack_overflow));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < cond_expr->get_param_count(); ++i) {
      // Here you must directly pass cond_expr->get_param_expr(i), because its value may need to be modified
      if (OB_FAIL(SMART_CALL(recursive_replace_func_is_serving_tenant(stmt,
                                                                      cond_expr->get_param_expr(i),
                                                                      trans_happened)))) {
        LOG_WARN("fail to recursive replace_func_is_serving_tenant", K(ret));
      }
    }
    // If is_serving_tenant and tenant_id is a constant expression, then rewrite as (svr_ip, svr_port) in ((ip1,
    // port1), (ip2, port2), ...)  format
    // If the current tenant is the system tenant, directly return true
    if (OB_SUCC(ret) && T_FUN_IS_SERVING_TENANT == cond_expr->get_expr_type()) {
      int64_t tenant_id_int64 = -1;
      if (OB_UNLIKELY(3 != cond_expr->get_param_count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("T_FUN_IS_SERVING_TENANT must has 3 params",
                        K(ret), K(cond_expr->get_param_count()), K(*cond_expr));
      } else if (OB_ISNULL(cond_expr->get_param_expr(0))
                 || OB_ISNULL(cond_expr->get_param_expr(1))
                 || OB_ISNULL(cond_expr->get_param_expr(2))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("T_FUN_IS_SERVING_TENANT has a null param",
                        K(ret), K(cond_expr->get_param_expr(0)),
                        K(cond_expr->get_param_expr(1)),
                        K(cond_expr->get_param_expr(2)), K(*cond_expr));
      } else if (!cond_expr->get_param_expr(2)->is_static_scalar_const_expr()) {
      } else if (OB_ISNULL(ctx_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ctx_ is NULL", K(ret));
      } else if (OB_ISNULL(ctx_->exec_ctx_) || OB_ISNULL(ctx_->session_info_) || OB_ISNULL(ctx_->allocator_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid argument",
                        K(ret), K(ctx_->exec_ctx_), K(ctx_->session_info_), K(ctx_->allocator_));
      } else if (OB_FAIL(calc_const_raw_expr_and_get_int(stmt,
                                                         cond_expr->get_param_expr(2),
                                                         *ctx_->exec_ctx_,
                                                         ctx_->session_info_,
                                                         *ctx_->allocator_,
                                                         tenant_id_int64))) {
        LOG_WARN("fail to calc tenant id", K(ret), K(*cond_expr->get_param_expr(2)));
      } else if (OB_UNLIKELY(tenant_id_int64 <= 0)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("tenant id is <= 0", K(ret), K(tenant_id_int64));
      } else {
        uint64_t tenant_id = static_cast<uint64_t>(tenant_id_int64);
        // if current tenant is sys, return true directly
        if (OB_SYS_TENANT_ID == tenant_id) {
          ObConstRawExpr *true_expr = NULL;
          if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_VARCHAR, true_expr))) {
            LOG_WARN("create const expr failed", K(ret));
          } else if (OB_ISNULL(true_expr)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("true expr is NULL", K(ret));
          } else {
            ObObj true_obj;
            true_obj.set_bool(true);
            true_expr->set_value(true_obj);
            cond_expr = true_expr;
            trans_happened = true;
          }
        } else {
          ObUnitInfoGetter ui_getter;
          ObArray<ObAddr> servers;
          if (OB_ISNULL(ctx_->exec_ctx_->get_sql_proxy())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("sql proxy from exec_ctx_ is NULL", K(ret));
          } else if (OB_FAIL(ui_getter.init(*ctx_->exec_ctx_->get_sql_proxy(), &GCONF))) {
            LOG_WARN("fail to init ObUnitInfoGetter", K(ret));
          } else if (OB_FAIL(ui_getter.get_tenant_servers(tenant_id, servers))) {
            LOG_WARN("fail to get servers of a tenant", K(ret));
          } else if (0 == servers.count()) {
            // Did not find the observer corresponding to this tenant_id, it may be that the tenant_id is illegal, in order to pass the query
            // range, will this be changed to where false form, so although the optimizer will return all partitions, but
            // ObPhyOperator will handle the false condition properly, and will not perform unnecessary queries
            ObConstRawExpr *false_expr = NULL;
            if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_VARCHAR, false_expr))) {
              LOG_WARN("create varchar expr failed", K(ret));
            } else if (OB_ISNULL(false_expr)){
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("false expr is NULL", K(ret));
            } else {
              ObObj false_obj;
              false_obj.set_bool(false);
              false_expr->set_value(false_obj);
              cond_expr = false_expr;
              trans_happened = true;
            }
          } else {
            ObOpRawExpr *in_op = NULL;
            ObOpRawExpr *left_row_op = NULL;
            ObOpRawExpr *right_row_op = NULL;
            if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_OP_IN, in_op))) {
              LOG_WARN("create in operator expr", K(ret));
            } else if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_OP_ROW, left_row_op))) {
              LOG_WARN("create left row operator failed", K(ret));
            } else if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_OP_ROW, right_row_op))) {
              LOG_WARN("create right row op failed", K(ret));
            } else if (OB_ISNULL(in_op) || OB_ISNULL(left_row_op) || OB_ISNULL(right_row_op)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("operator is null", K(in_op), K(left_row_op), K(right_row_op));
            } else if (OB_FAIL(right_row_op->init_param_exprs(servers.count()))) {
              LOG_WARN("failed to init param exprs", K(ret));
            } else {/*do nothing*/}

            for (int64_t i = 0; OB_SUCC(ret) && i < servers.count(); ++i) {
              ObAddr server = servers.at(i);
              ObOpRawExpr *row_op = NULL;
              ObConstRawExpr *ip_expr = NULL;
              ObConstRawExpr *port_expr = NULL;
              char *ip_buf = NULL;
              ObObj ip_obj;
              ObObj port_obj;
              if (OB_UNLIKELY(NULL == (ip_buf = static_cast<char*>(ctx_->allocator_->alloc(OB_MAX_SERVER_ADDR_SIZE))))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_ERROR("fail to alloc ip str buffer", K(ret), LITERAL_K(OB_MAX_SERVER_ADDR_SIZE));
              } else if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_OP_ROW, row_op))) {
                LOG_WARN("create row operator expr failed", K(ret));
              } else if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_VARCHAR, ip_expr))) {
                LOG_WARN("create ip operator expr failed", K(ret));
              } else if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_INT, port_expr))) {
                LOG_WARN("create port expr failed", K(ret));
              } else if (OB_UNLIKELY(!server.ip_to_string(ip_buf, OB_MAX_SERVER_ADDR_SIZE))) {
                ret = OB_INVALID_ARGUMENT;
                LOG_WARN("convert server addr to ip failed", K(ret), K(i), K(server));
              } else if (OB_ISNULL(row_op) || OB_ISNULL(ip_expr) || OB_ISNULL(port_expr)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("expr is null", K(row_op), K(ip_expr), K(port_expr));
              } else {
                ip_obj.set_varchar(ObString(ip_buf));
                ip_obj.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
                ip_obj.set_collation_level(CS_LEVEL_SYSCONST);
                port_obj.set_int(server.get_port());
                ip_expr->set_value(ip_obj);
                port_expr->set_value(port_obj);
                if (OB_FAIL(row_op->set_param_exprs(ip_expr, port_expr))) {
                  LOG_WARN("fail to set param expr", K(ret));
                } else if (OB_FAIL(right_row_op->add_param_expr(row_op))) {
                  LOG_WARN("fail to add param expr", K(ret));
                } else {/*do nothing*/}
              }
            }
            if (OB_SUCC(ret)) {
              if (OB_FAIL(left_row_op->set_param_exprs(cond_expr->get_param_expr(0), cond_expr->get_param_expr(1)))) {
                LOG_WARN("fail to set param expr", K(ret));
              } else if (OB_FAIL(in_op->set_param_exprs(left_row_op, right_row_op))) {
                LOG_WARN("fail to set param expr", K(ret));
              } else if (OB_FAIL(in_op->formalize(ctx_->session_info_))) {
                LOG_WARN("fail to formalize expr", K(ret));
              } else {
                cond_expr = in_op;
                trans_happened = true;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::calc_const_raw_expr_and_get_int(const ObStmt &stmt,
                                                           ObRawExpr *const_expr,
                                                           ObExecContext &exec_ctx,
                                                           ObSQLSessionInfo *session,
                                                           ObIAllocator &allocator,
                                                           int64_t &result)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = NULL;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  ObObj result_int_obj;
  bool got_result = false;
  if (OB_ISNULL(const_expr) || OB_ISNULL(session)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expr or session is NULL", KP(session), KP(const_expr), K(ret));
  } else if (OB_UNLIKELY(!const_expr->is_static_scalar_const_expr())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expr is not const expr", K(ret), K(*const_expr));
  } else if (OB_ISNULL(sql_proxy = exec_ctx.get_sql_proxy())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is NULL", K(ret));
  } else if (OB_ISNULL(plan_ctx = exec_ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("physical plan ctx is NULL", K(ret));
  } else if (OB_FAIL(ObSQLUtils::calc_const_or_calculable_expr(ctx_->exec_ctx_,
                                                               const_expr,
                                                               result_int_obj,
                                                               got_result,
                                                               exec_ctx.get_allocator(),
                                                               false))) {
    LOG_WARN("failed to calc const or calculable expr", K(ret));
  } else if (OB_UNLIKELY(false == ob_is_integer_type(result_int_obj.get_type()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant id result must integer", K(ret), K(result_int_obj));
  } else {
    result = result_int_obj.get_int();
  }
  return ret;
}

int ObTransformPreProcess::transform_special_expr(ObDMLStmt *&stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null stmt or ctx", K(ret), K(stmt), K(ctx_));
  } else if (OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is NULL", K(ret));
  } else if (ObSQLSessionInfo::USER_SESSION == ctx_->session_info_->get_session_type()) {
    ObInsertStmt *ins_stmt = dynamic_cast<ObInsertStmt*>(stmt);
    if (OB_SUCC(ret) && NULL != ins_stmt) {
      // value exprs for insert stmt is not included in relation expr array.
      ObIArray<ObRawExpr*> &value_vectors = ins_stmt->get_values_vector();
      for (int64_t i = 0; OB_SUCC(ret) && i < value_vectors.count(); ++i) {
        ObRawExpr *expr = value_vectors.at(i);
        bool is_happened = false;
        if (OB_FAIL(transform_expr(*ctx_->expr_factory_,
                                   *ctx_->session_info_,
                                   expr,
                                   is_happened))) {
          LOG_WARN("transform expr failed", K(ret));
        } else if(OB_ISNULL(expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("expr is NULL", K(ret));
        } else {
          value_vectors.at(i) = expr;
          trans_happened |= is_happened;
        }
      }
    }
  }
  return ret;
}
// Recursively collect all TableItem from from_item, used for subsequent query rewriting
int ObTransformPreProcess::collect_all_tableitem(ObDMLStmt *stmt,
                                                 TableItem *table_item,
                                                 common::ObArray<TableItem*> &table_item_list)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  if (OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table item is null", K(ret));
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
    LOG_WARN("failed to check stack overflow", K(ret));
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too deep recursive", K(ret), K(is_stack_overflow));
  } else {
    if (table_item->is_joined_table()) {
      JoinedTable *joined_table_item = static_cast<JoinedTable *>(table_item);
      if (OB_FAIL(SMART_CALL(collect_all_tableitem(stmt, joined_table_item->left_table_,
                                                   table_item_list)))) {
        LOG_WARN("failed to collect temp table item", K(ret));
      } else if (OB_FAIL(SMART_CALL(collect_all_tableitem(stmt, joined_table_item->right_table_,
                                                          table_item_list)))) {
        LOG_WARN("failed to collect temp table item", K(ret));
      }
    } else if (table_item->is_basic_table() &&
               OB_FAIL(add_var_to_array_no_dup(table_item_list, table_item))) {
      LOG_WARN("failed to push table item", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::get_inner_aggr_exprs(
    ObSelectStmt *sub_stmt,
    common::ObIArray<ObAggFunRawExpr*>& inner_aggr_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sub_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("select stmt is null", K(ret));
  } else {
    common::ObIArray<ObAggFunRawExpr*> &aggr_exprs = sub_stmt->get_aggr_items();
    for (int64_t i = 0; OB_SUCC(ret) && i < aggr_exprs.count(); ++i) {
      if (OB_ISNULL(aggr_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr of aggr expr is null", K(ret));
      } else if (aggr_exprs.at(i)->in_inner_stmt()) {
        /*do nothing.*/
        if (OB_FAIL(add_var_to_array_no_dup(inner_aggr_exprs,
                                            aggr_exprs.at(i)))) {
          LOG_WARN("failed to to add var to array no dup.", K(ret));
        } 
      } 
    }
  }
  return ret;
}

int ObTransformPreProcess::get_first_level_output_exprs(
    ObSelectStmt *sub_stmt,
    common::ObIArray<ObRawExpr*>& inner_aggr_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sub_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("select stmt is null", K(ret));
  } else {
    common::ObIArray<ObAggFunRawExpr*> &aggr_exprs = sub_stmt->get_aggr_items();
    for (int64_t i = 0; OB_SUCC(ret) && i < aggr_exprs.count(); ++i) {
      if (OB_ISNULL(aggr_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr of aggr expr is null", K(ret));
      } else if (aggr_exprs.at(i)->in_inner_stmt()) {
        /*do nothing.*/
      } else {
        // outer aggr
        int64_t N = aggr_exprs.at(i)->get_param_count();
        for (int64_t j = 0; OB_SUCC(ret) && j < N; ++j) {
          if (OB_ISNULL(aggr_exprs.at(i)->get_param_expr(j))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected null", K(ret));
          } else if (aggr_exprs.at(i)->get_param_expr(j)->is_const_expr()) {
            //do nothing
          } else if (OB_FAIL(add_var_to_array_no_dup(inner_aggr_exprs,
                                                     aggr_exprs.at(i)->get_param_expr(j)))) {
            LOG_WARN("failed to to add var to array no dup.", K(ret));
          } else { /*do nothing.*/ }
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::generate_child_level_aggr_stmt(ObSelectStmt *select_stmt,
                                                          ObSelectStmt *&sub_stmt)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr*, 4> complex_aggr_exprs;
  ObSEArray<ObAggFunRawExpr*, 4> inner_aggr_exprs;

  if (OB_ISNULL(ctx_) || OB_ISNULL(ctx_->stmt_factory_) || OB_ISNULL(ctx_->expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("select stmt is null", K(ret));
  } else if (OB_FAIL(ctx_->stmt_factory_->create_stmt<ObSelectStmt>(sub_stmt))) {
    LOG_WARN("failed to create stmt.", K(ret));
  } else if (FALSE_IT(sub_stmt->set_query_ctx(select_stmt->get_query_ctx()))) {
  } else if (OB_FAIL(sub_stmt->deep_copy(*ctx_->stmt_factory_,
                                         *ctx_->expr_factory_,
                                         *select_stmt))) {
    LOG_WARN("failed to deep copy from nested stmt.", K(ret));
  } else if (OB_FAIL(sub_stmt->adjust_statement_id(ctx_->allocator_,
                                                   ctx_->src_qb_name_,
                                                   ctx_->src_hash_val_))) {
    LOG_WARN("failed to recursive adjust statement id", K(ret));
  } else if (OB_FAIL(get_first_level_output_exprs(sub_stmt, complex_aggr_exprs))) {
    LOG_WARN("failed to low levels aggr.", K(ret));
  } else if (OB_FAIL(get_inner_aggr_exprs(sub_stmt, inner_aggr_exprs)) ) {
  } else {
    sub_stmt->get_aggr_items().reset();
    sub_stmt->get_select_items().reset();
    sub_stmt->get_order_items().reset();
  }
  
  for (int64_t j = 0; OB_SUCC(ret) && j < inner_aggr_exprs.count(); ++j) {
    if (OB_FAIL(add_var_to_array_no_dup(sub_stmt->get_aggr_items(), inner_aggr_exprs.at(j)))) {
      LOG_WARN("failed to add aggr exprs to aggr item.", K(ret));
    }
  }

  for (int64_t j = 0; OB_SUCC(ret) && j < complex_aggr_exprs.count(); ++j) {
    if (OB_FAIL(ObTransformUtils::create_select_item(*ctx_->allocator_,
                                                            complex_aggr_exprs.at(j),
                                                            sub_stmt))) {
      LOG_WARN("failed to push back into select item array.", K(ret));
    } else { /*do nothing.*/ }
  }

  return ret;
}

int ObTransformPreProcess::remove_nested_aggr_exprs(ObSelectStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret));
  } else {
    ObSEArray<ObAggFunRawExpr *, 4> aggr_exprs;
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_aggr_items().count(); i++) {
      if (OB_ISNULL(stmt->get_aggr_items().at(i))) {
       ret = OB_ERR_UNEXPECTED;
       LOG_WARN("expr of aggr expr is null", K(ret));
      } else if (stmt->get_aggr_items().at(i)->in_inner_stmt()) {
       /*do nothing.*/
      } else if (OB_FAIL(aggr_exprs.push_back(stmt->get_aggr_items().at(i)))) {
       LOG_WARN("failed to assign to inner aggr exprs.", K(ret));
      } else { /*do nothing.*/ }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(stmt->get_aggr_items().assign(aggr_exprs))) {
        LOG_WARN("failed to extract second aggr items.", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::construct_column_items_from_exprs(
    const ObIArray<ObRawExpr*> &column_exprs,
    ObIArray<ColumnItem> &column_items)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < column_exprs.count(); ++i) {
    ColumnItem column_item;
    ObColumnRefRawExpr* expr = static_cast<ObColumnRefRawExpr*>(column_exprs.at(i));
    column_item.expr_ = expr;
    column_item.table_id_ = expr->get_table_id();
    column_item.column_id_ = expr->get_column_id();
    column_item.column_name_ = expr->get_expr_name();
    if (OB_FAIL(column_items.push_back(column_item))) {
      LOG_WARN("failed to push back into temp column items.", K(ret));
    } else { /*do nothing.*/ }
  }
  return ret;
}

int ObTransformPreProcess::generate_parent_level_aggr_stmt(ObSelectStmt *&select_stmt,
                                                           ObSelectStmt *sub_stmt)
{
  int ret = OB_SUCCESS;
  TableItem *view_table_item = NULL;
  ObSEArray<ObRawExpr *, 4> old_exprs;
  ObSEArray<ObRawExpr *, 4> new_exprs;
  ObSEArray<ColumnItem, 4> column_items;
  if (OB_ISNULL(select_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("select stmt is null", K(ret));
  } else {
    select_stmt->get_table_items().reset();
    select_stmt->get_joined_tables().reset();
    select_stmt->get_from_items().reset();
    select_stmt->get_column_items().reset();
    select_stmt->get_condition_exprs().reset();
    select_stmt->get_part_exprs().reset();
    select_stmt->get_check_constraint_items().reset();
    select_stmt->get_group_exprs().reset();
    select_stmt->get_rollup_exprs().reset();
    select_stmt->get_having_exprs().reset();
    select_stmt->get_order_items().reset();

    if (OB_FAIL(get_first_level_output_exprs(select_stmt,
                                             old_exprs))) {
      LOG_WARN("failed to get column exprs from stmt from.", K(ret));
    } else if (OB_FAIL(remove_nested_aggr_exprs(select_stmt))) {
      LOG_WARN("failed to extract second aggr items.", K(ret));
    } else if (OB_FAIL(ObTransformUtils::add_new_table_item(ctx_,
                                                            select_stmt,
                                                            sub_stmt,
                                                            view_table_item))) {
      LOG_WARN("failed to add new table item.", K(ret));
    } else if (OB_FAIL(select_stmt->add_from_item(view_table_item->table_id_))) {
      LOG_WARN("failed to add from item", K(ret));
    } else if (OB_FAIL(ObTransformUtils::create_columns_for_view(ctx_,
                                                                 *view_table_item,
                                                                 select_stmt,
                                                                 new_exprs))) {
      LOG_WARN("failed to get select exprs from grouping sets view.", K(ret));
    } else if (OB_FAIL(construct_column_items_from_exprs(new_exprs, column_items))) {
      LOG_WARN("failed to construct column items from exprs.", K(ret));
    } else if (OB_FAIL(replace_group_id_in_stmt(select_stmt))) {
      LOG_WARN("fail to replace group_id in nested aggr");
    } else if (OB_FAIL(select_stmt->replace_relation_exprs(old_exprs, new_exprs))) {
      LOG_WARN("failed to replace inner stmt exprs.", K(ret));
    } else if (OB_FAIL(select_stmt->get_column_items().assign(column_items))) {
      LOG_WARN("failed to assign column items.", K(ret));
    } else if (OB_FAIL(select_stmt->rebuild_tables_hash())) {
      LOG_WARN("failed to rebuild tables hash.", K(ret));
    } else if (OB_FAIL(select_stmt->update_column_item_rel_id())) {
      LOG_WARN("failed to update column items rel id.", K(ret));
    } else if (OB_FAIL(select_stmt->formalize_stmt(ctx_->session_info_))) {
      LOG_WARN("failed to formalized stmt.", K(ret));
    } else { /*do nothing.*/ }
  }
  return ret;
}

int ObTransformPreProcess::transform_for_nested_aggregate(ObDMLStmt *&stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret), K(stmt), K(ctx_));
  } else if (!stmt->is_select_stmt()) {
    /*do nothing.*/
  } else {
    ObSelectStmt *sub_stmt = NULL;
    ObSelectStmt *select_stmt = static_cast<ObSelectStmt *>(stmt);
    /**
     * This function rewrites stmt with nested aggregation into a two-layer stmt
     * select sum(b), max(sum(b)) from t1 group by b;
     * The above SQL can be rewritten as
     * select sum(v.b), max(v.sum_b)
     * from (
     *      select b, sum(b) as sum_b
     *      from t1
     *      group by b
     *      ) v
     * where generate_child_level_aggr_stmt function generates view v
     * generate_parent_level_aggr_stmt generates the outer stmt
     */
    if (!select_stmt->contain_nested_aggr()) {
      /*do nothing.*/
    } else if (OB_FAIL(generate_child_level_aggr_stmt(select_stmt,
                                                      sub_stmt))) {
      LOG_WARN("failed to generate first level aggr stmt.", K(ret));
    } else if (OB_FAIL(generate_parent_level_aggr_stmt(select_stmt,
                                                       sub_stmt))) {
      LOG_WARN("failed to generate nested aggr stmt.", K(ret));
    } else if (OB_FAIL(select_stmt->formalize_stmt(ctx_->session_info_))) {
      LOG_WARN("failed to formalize stmt.", K(ret));
    } else {
      trans_happened = true;
      stmt = select_stmt;
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_group_id_in_stmt(ObSelectStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret));
  } else {
    // replace group_id in select list;
    ObIArray<SelectItem> &sel_items = stmt->get_select_items();
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_items.count(); i++) {
      if (OB_ISNULL(sel_items.at(i).expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr of select item shouldn't be null", K(ret));
      } else if (OB_FAIL(replace_group_id_in_expr_recursive(sel_items.at(i).expr_))) {
        LOG_WARN("fail to replace expr in selet items", K(ret));
      }
    }
    // replace group_id in having exprs;
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_having_expr_size(); i++) {
      if (OB_ISNULL(stmt->get_having_exprs().at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("having expr shouldn't be null", K(ret));
      } else if (OB_FAIL(replace_group_id_in_expr_recursive(stmt->get_having_exprs().at(i)))) {
        LOG_WARN("fail to replace expr in having exprs", K(ret));
      }
    }
    // replace group_id in order by exprs;
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_order_item_size(); i++) {
      if (OB_ISNULL(stmt->get_order_item(i).expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("having expr shouldn't be null", K(ret));
      } else if (OB_FAIL(replace_group_id_in_expr_recursive(stmt->get_order_item(i).expr_))) {
        LOG_WARN("fail to replace expr in having exprs", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_group_id_in_expr_recursive(ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr) || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null pointer", K(ret));
  } else {
    if (expr->is_aggr_expr()) {
      if (expr->get_expr_type() == T_FUN_GROUP_ID) {
        // replace group id with 0;
        ObConstRawExpr *c_expr = NULL;
        if (OB_FAIL(ObRawExprUtils::build_const_int_expr(*ctx_->expr_factory_,
                                                         ObIntType,
                                                         0,
                                                         c_expr))) {
          LOG_WARN("fail to build const expr", K(ret));
        } else if (OB_ISNULL(c_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("c_expr is null", K(ret));
        } else {
          expr = c_expr;
        }
      }
    } else {
      ObRawExpr *tmp_expr = NULL;
      for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); i++) {
        if (OB_ISNULL(tmp_expr = expr->get_param_expr(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get param expr is null", K(ret));
        } else if (OB_FAIL(replace_group_id_in_expr_recursive(tmp_expr))) {
          LOG_WARN("fail to replace group id in expr", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_exprs(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)
      || OB_ISNULL(ctx_->expr_factory_) || OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt or context is null", K(ret));
  } else {
    ObSEArray<ObRawExpr*, 8> relation_exprs;
    ObStmtExprGetter getter;
    getter.set_relation_scope();
    getter.add_scope(SCOPE_BASIC_TABLE);
    ObStmtExprReplacer replacer;
    replacer.set_relation_scope();
    replacer.add_scope(SCOPE_BASIC_TABLE);
    replacer.set_recursive(false);
    if (OB_FAIL(stmt->get_relation_exprs(relation_exprs, getter))) {
      LOG_WARN("failed to get all relation exprs", K(ret));
    } else {
      for (int i = 0; OB_SUCC(ret) && i < relation_exprs.count(); i++) {
        bool is_happened = false;
        ObRawExpr *old_expr = relation_exprs.at(i);
        ObRawExpr *new_expr = relation_exprs.at(i);
        if (OB_ISNULL(old_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("expr is NULL", K(ret));
        } else if (OB_FAIL(transform_expr(*ctx_->expr_factory_,
                                          *ctx_->session_info_,
                                          new_expr,
                                          is_happened))) {
          LOG_WARN("transform expr failed", K(ret));
        } else if (!is_happened) {
          // do nothing
        } else if (OB_FAIL(replacer.add_replace_expr(old_expr, new_expr))) {
          LOG_WARN("failed to add replace expr", K(ret));
        } else {
          trans_happened = true;
        }
      }
      if (OB_SUCC(ret) && trans_happened) {
        if (OB_FAIL(stmt->iterate_stmt_expr(replacer))) {
          LOG_WARN("failed to iterate stmt expr", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_expr(ObRawExprFactory &expr_factory,
                                          const ObSQLSessionInfo &session,
                                          ObRawExpr *&expr,
                                          bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expr is NULL", K(ret));
  }
  if (OB_SUCC(ret)) {
    // rewrite `c1 in (c2, c3)` to `c1 = c2 or c1 = c3`
    if (OB_FAIL(replace_in_or_notin_recursively(
                expr_factory, session, expr, trans_happened))) {
      LOG_WARN("replace in or not in failed", K(ret), K(expr));
    }
  }
  if (OB_SUCC(ret)) {
    // rewrite
    //   `cast c1 when c2 then xxx when c3 then xxx else xxx end`
    // to:
    //   `cast when c1 = c2 then xxx when c1 = c3 then xxx else xxx end`
    if (OB_FAIL(transform_arg_case_recursively(
                expr_factory, session, expr, trans_happened))) {
      LOG_WARN("transform arg case failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    // The rewriting is done for the purpose of MySQL compatibility.
    if (lib::is_mysql_mode()) {
      if (OB_FAIL(replace_align_date4cmp_recursively(expr_factory, session, expr))) {
        LOG_WARN("replace align_date4cmp failed", K(ret), K(expr));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(replace_inner_row_cmp_val_recursively(expr_factory, session,
                                                      expr, trans_happened))) {
      LOG_WARN("replace inner row cmp value failed", K(ret), K(expr));
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_in_or_notin_expr_without_row(ObRawExprFactory &expr_factory,
                                                                  const ObSQLSessionInfo &session,
                                                                  const bool is_in_expr,
                                                                  ObRawExpr *&in_expr,
                                                                  bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObRawExpr *left_expr = in_expr->get_param_expr(0);
  ObRawExpr *right_expr = in_expr->get_param_expr(1);
  ObSEArray<DistinctObjMeta, 4> distinct_types;
  for (int i = 0; OB_SUCC(ret) && i < right_expr->get_param_count(); i++) {
    ObRawExpr *param_expr = right_expr->get_param_expr(i);
    if (OB_ISNULL(param_expr)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null param expr", K(ret), K(param_expr));
    } else {
      ObObjType obj_type = param_expr->get_result_type().get_type();
      ObCollationType coll_type = param_expr->get_result_type().get_collation_type();
      ObCollationLevel coll_level = param_expr->get_result_type().get_collation_level();
      ObScale scale = param_expr->get_result_type().get_scale();
      if (param_expr->is_enum_set_with_subschema()) {
        ObObjMeta obj_meta;
        if (OB_FAIL(ObRawExprUtils::extract_enum_set_collation(param_expr->get_result_type(),
                                                               &session,
                                                               obj_meta))) {
          LOG_WARN("fail to extract enum set cs type", K(ret));
        } else {
          coll_type = obj_meta.get_collation_type();
          coll_level = obj_meta.get_collation_level();
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_UNLIKELY(obj_type == ObMaxType)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected obj type", K(ret), K(obj_type), K(*in_expr));
      } else if (OB_FAIL(add_var_to_array_no_dup(distinct_types,
                                DistinctObjMeta(obj_type, coll_type, coll_level, scale)))) {
        LOG_WARN("failed to push back", K(ret));
      } else {
        LOG_DEBUG("add param expr type", K(i), K(obj_type));
      }
    }
  } // for end

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (1 == distinct_types.count()) {
    // only one type contained in right row expr, do not need rewrite
    // set should_deduce_type = true
    ObOpRawExpr *op_raw_expr = dynamic_cast<ObOpRawExpr *>(in_expr);
    if (OB_ISNULL(op_raw_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid null op_raw_expr", K(ret));
    } else {
      op_raw_expr->set_add_implicit_cast_for_in_param(true);
    }
  } else {
    LOG_DEBUG("distinct types", K(distinct_types));
    ObSEArray<ObRawExpr *, 4> transed_in_exprs;
    ObSEArray<ObRawExpr *, 4> same_type_exprs;
    for (int i = 0; OB_SUCC(ret) && i < distinct_types.count(); i++) {
      same_type_exprs.reuse();
      DistinctObjMeta obj_meta = distinct_types.at(i);
      for (int j = 0; OB_SUCC(ret) && j < right_expr->get_param_count(); j++) {
        ObRawExpr *param_expr = right_expr->get_param_expr(j);
        ObObjType obj_type = param_expr->get_result_type().get_type();
        ObCollationType coll_type = param_expr->get_result_type().get_collation_type();
        ObCollationLevel coll_level = param_expr->get_result_type().get_collation_level();
        ObScale scale = param_expr->get_result_type().get_scale();
        if (param_expr->is_enum_set_with_subschema()) {
          ObObjMeta enum_set_obj_meta;
          if (OB_FAIL(ObRawExprUtils::extract_enum_set_collation(param_expr->get_result_type(),
                                                                &session,
                                                                enum_set_obj_meta))) {
            LOG_WARN("fail to extract enum set cs type", K(ret));
          } else {
            coll_type = enum_set_obj_meta.get_collation_type();
            coll_level = enum_set_obj_meta.get_collation_level();
          }
        }
         DistinctObjMeta tmp_meta(obj_type, coll_type, coll_level, scale);
        if (OB_FAIL(ret)) {
        } else if (obj_meta == tmp_meta
            && OB_FAIL(same_type_exprs.push_back(right_expr->get_param_expr(j)))) {
          LOG_WARN("failed to add param expr", K(ret));
        } else { /* do nothing */ }
      }  // for end
      if (OB_SUCC(ret) && OB_FAIL(create_partial_expr(expr_factory, session, left_expr, same_type_exprs,
                                                      is_in_expr, transed_in_exprs))) {
        LOG_WARN("failed to create partial expr", K(ret));
      }
    } // for end
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_final_transed_or_and_expr(expr_factory,
                                                     session,
                                                     is_in_expr,
                                                     transed_in_exprs,
                                                     in_expr))) {
      LOG_WARN("failed to get final transed or expr", K(ret));
    } else {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_in_or_notin_expr_with_row(ObRawExprFactory &expr_factory,
                                                               const ObSQLSessionInfo &session,
                                                               const bool is_in_expr,
                                                               ObRawExpr *&in_expr,
                                                               bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObRawExpr *left_expr = in_expr->get_param_expr(0);
  ObRawExpr *right_expr = in_expr->get_param_expr(1);
  int row_dim = T_REF_QUERY != left_expr->get_expr_type() ? left_expr->get_param_count() :
                                    static_cast<ObQueryRefRawExpr*>(left_expr)->get_output_column();
  ObSEArray<ObSEArray<DistinctObjMeta, 4>, 4> distinct_row_types;
  ObSEArray<ObSEArray<DistinctObjMeta, 4>, 4> all_row_types;

  for (int i = 0; OB_SUCC(ret) && i < right_expr->get_param_count(); i++) {
    if (OB_ISNULL(right_expr->get_param_expr(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null param expr", K(ret));
    } else if (OB_UNLIKELY(right_expr->get_param_expr(i)->get_param_count() != row_dim)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid param cnt", K(row_dim),
               K(right_expr->get_param_expr(i)->get_param_count()));
    } else {
      ObSEArray<DistinctObjMeta, 4> tmp_row_type;
      for (int j = 0; OB_SUCC(ret) && j < right_expr->get_param_expr(i)->get_param_count();
           j++) {
        if (OB_ISNULL(right_expr->get_param_expr(i)->get_param_expr(j))) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid null param expr", K(ret));
        } else {
          const ObRawExpr *param_expr = right_expr->get_param_expr(i)->get_param_expr(j);
          const ObObjType obj_type = param_expr->get_result_type().get_type();
          const ObCollationType coll_type = param_expr->get_result_type().get_collation_type();
          const ObCollationLevel coll_level = param_expr->get_result_type().get_collation_level();
          const ObScale scale = param_expr->get_result_type().get_scale();
          if (OB_UNLIKELY(obj_type == ObMaxType)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected obj type", K(ret), K(obj_type), K(*in_expr));
          } else if (OB_FAIL(tmp_row_type.push_back(
                               DistinctObjMeta(obj_type, coll_type, coll_level, scale)))) {
            LOG_WARN("failed to push back element", K(ret));
          } else { /* do nothing */ }
        }
      } // for end
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_FAIL(add_row_type_to_array_no_dup(distinct_row_types, tmp_row_type))) {
        LOG_WARN("failed to add_row_type_to_array_no_dup", K(ret));
      } else if (OB_FAIL(all_row_types.push_back(tmp_row_type))) {
        LOG_WARN("failed to push back element", K(ret));
      } else { /* do nothing */ }
    }
  } // for end
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (1 == distinct_row_types.count()) {
    // all rows are same, do nothing
    ObOpRawExpr *op_raw_expr = dynamic_cast<ObOpRawExpr *>(in_expr);
    if (OB_ISNULL(op_raw_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid null op_raw_expr", K(ret));
    } else {
      op_raw_expr->set_add_implicit_cast_for_in_param(true);
    }
  } else {
    ObSEArray<ObRawExpr *, 4> transed_in_exprs;
    ObSEArray<ObRawExpr *, 4> same_type_exprs;
    for (int i = 0; OB_SUCC(ret) && i < distinct_row_types.count(); i++) {
      same_type_exprs.reuse();
      for (int j = 0; OB_SUCC(ret) && j < all_row_types.count(); j++) {
        if (is_same_row_type(distinct_row_types.at(i), all_row_types.at(j)) &&
            OB_FAIL(same_type_exprs.push_back(right_expr->get_param_expr(j)))) {
          LOG_WARN("failed to add param expr", K(ret));
        }
      } // for end
      if (OB_SUCC(ret) && OB_FAIL(create_partial_expr(expr_factory, session, left_expr, same_type_exprs,
                                                      is_in_expr, transed_in_exprs))) {
        LOG_WARN("failed to create partial expr", K(ret));
      }
    } // for end
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_final_transed_or_and_expr(
                expr_factory, session, is_in_expr, transed_in_exprs, in_expr))) {
      LOG_WARN("failed to get final transed in expr", K(ret));
    } else {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::create_partial_expr(ObRawExprFactory &expr_factory,
                                               const ObSQLSessionInfo &session,
                                               ObRawExpr *left_expr,
                                               ObIArray<ObRawExpr*> &same_type_exprs,
                                               const bool is_in_expr,
                                               ObIArray<ObRawExpr*> &transed_in_exprs)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *tmp_expr = NULL;
  ObOpRawExpr *tmp_row_expr = NULL;
  ObRawExpr *tmp_left_expr = left_expr;
  ObRawExpr *tmp_right_expr = NULL;
  if (OB_UNLIKELY(same_type_exprs.empty()) || OB_ISNULL(left_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected empty same_type_exprs", K(ret), K(same_type_exprs), K(left_expr));
  } else if (left_expr->get_expr_type() == T_OP_ROW &&
             OB_FAIL(ObRawExprCopier::copy_expr_node(expr_factory,
                                                     left_expr,
                                                     tmp_left_expr))) {
    // Comment for T_OP_ROW
    // for T_OP_ROW expr, the cast is not added above the expr
    // it is added on the expr's param.
    // when the expr is used by different predicates
    // we may need add different cast for its param
    // hence, we need to copy the expr node.
    // we do not need to copy its param, the param can be shared used.
    LOG_WARN("failed to copy expr node", K(ret));
  } else if (1 == same_type_exprs.count()) { // = / <>
    if (OB_FAIL(expr_factory.create_raw_expr(is_in_expr ? T_OP_EQ : T_OP_NE, tmp_expr))) {
      LOG_WARN("failed to create or create raw expr", K(ret));
    } else {
      tmp_right_expr = same_type_exprs.at(0);
    }
  } else if (OB_FAIL(expr_factory.create_raw_expr(is_in_expr ? T_OP_IN : T_OP_NOT_IN, tmp_expr)) // in / not in
             || OB_FAIL(expr_factory.create_raw_expr(T_OP_ROW, tmp_row_expr))) {
    LOG_WARN("failed to create or create raw expr", K(ret));
  } else if (OB_ISNULL(tmp_row_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null expr", K(ret), K(tmp_row_expr));
  } else if (OB_FAIL(tmp_row_expr->set_param_exprs(same_type_exprs))) {
    LOG_WARN("failed to append exprs", K(ret));
  } else {
    tmp_expr->set_add_implicit_cast_for_in_param(true);
    tmp_right_expr = tmp_row_expr;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(tmp_expr) || OB_ISNULL(tmp_left_expr) || OB_ISNULL(tmp_right_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null expr", K(ret), K(tmp_expr),
                                    K(tmp_left_expr), K(tmp_right_expr));
  } else if (OB_FAIL(tmp_expr->set_param_exprs(tmp_left_expr, tmp_right_expr))) {
    LOG_WARN("failed to set param exprs", K(ret));
  } else if (OB_FAIL(tmp_expr->formalize(&session))) {
    LOG_WARN("failed to formalize expr", K(ret));
  } else if (OB_FAIL(transed_in_exprs.push_back(tmp_expr))) {
    LOG_WARN("failed to push back element", K(ret));
  } else {
    LOG_DEBUG("partial in expr", K(*tmp_expr), K(*tmp_right_expr), K(*left_expr));
  }
  return ret;
}

int ObTransformPreProcess::transform_arg_case_recursively(
    ObRawExprFactory &expr_factory,
    const ObSQLSessionInfo &session,
    ObRawExpr *&expr,
    bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (T_OP_ARG_CASE == expr->get_expr_type()) {
    if (OB_FAIL(transform_arg_case_expr(expr_factory, session, expr, trans_happened))) {
      LOG_WARN("failed to transform_arg_case_expr", K(ret));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
    if (OB_FAIL(SMART_CALL(transform_arg_case_recursively(expr_factory,
                           session,
                           expr->get_param_expr(i),
                           trans_happened)))) {
      LOG_WARN("failed to transform arg case expr", K(ret), K(i));
    }
  }
  return ret;
}

// in engine 3.0 transform arg_case_when_expr to simple_case_when_expr
// eg:
// case arg when when1 then then1       case when arg = when1 then then1
//          when when1 then then2  ->        when arg = when2 then then2
//          else else1                       else else1
int ObTransformPreProcess::transform_arg_case_expr(ObRawExprFactory &expr_factory,
                                                   const ObSQLSessionInfo &session,
                                                   ObRawExpr *&expr,
                                                   bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObCaseOpRawExpr *case_expr = static_cast<ObCaseOpRawExpr*>(expr);
  ObRawExpr *arg_expr = case_expr->get_arg_param_expr();
  ObCaseOpRawExpr *new_case_expr = NULL;
  if (OB_FAIL(expr_factory.create_raw_expr(T_OP_CASE, new_case_expr))) {
    LOG_WARN("failed to create case expr", K(ret));
  } else if (OB_ISNULL(new_case_expr) || OB_ISNULL(arg_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is NULL", K(ret), KP(new_case_expr), KP(arg_expr));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < case_expr->get_when_expr_size(); ++i) {
    ObRawExpr *when_expr = case_expr->get_when_param_expr(i);
    ObRawExpr *then_expr = case_expr->get_then_param_expr(i);
    ObRawExpr *tmp_arg_expr = arg_expr;
    ObOpRawExpr *equal_expr = NULL;
    if (OB_ISNULL(when_expr) || OB_ISNULL(then_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("when_expr is NULL", K(ret), KP(when_expr), KP(then_expr));
    } else if (T_OP_ROW == arg_expr->get_expr_type() &&
               OB_FAIL(ObRawExprCopier::copy_expr_node(expr_factory,
                                                       arg_expr,
                                                       tmp_arg_expr))) {
      // See the Comment for T_OP_ROW above
      LOG_WARN("failed to copy expr", K(ret));
    } else if (OB_FAIL(create_equal_expr_for_case_expr(expr_factory,
                                                       session,
                                                       tmp_arg_expr,
                                                       when_expr,
                                                       case_expr->get_extra_calc_meta().get_collation_type(),
                                                       equal_expr))) {
      LOG_WARN("failed to create equal expr", K(ret));
    } else if (OB_FAIL(new_case_expr->add_when_param_expr(equal_expr)) ||
              OB_FAIL(new_case_expr->add_then_param_expr(then_expr))) {
      LOG_WARN("failed to add param expr", K(ret));
    }
  } // for end
  ObRawExpr* tmp_case_expr = static_cast<ObRawExpr*>(new_case_expr);
  if (OB_SUCC(ret) &&
      !case_expr->is_called_in_sql() &&
      OB_FAIL(ObRawExprUtils::set_call_in_pl(tmp_case_expr))) {
    LOG_WARN("failed to set call_in_pl flag", K(ret));
  }
  if (OB_SUCC(ret)) {
    new_case_expr->set_default_param_expr(case_expr->get_default_param_expr());
    if (OB_FAIL(new_case_expr->formalize(&session))) {
      LOG_WARN("failed to formalize", K(ret));
    } else {
      expr = static_cast<ObRawExpr*>(new_case_expr);
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::create_equal_expr_for_case_expr(ObRawExprFactory &expr_factory,
                                                           const ObSQLSessionInfo &session,
                                                           ObRawExpr *arg_expr,
                                                           ObRawExpr *when_expr,
                                                           const ObCollationType cmp_cs_type,
                                                           ObOpRawExpr *&equal_expr)
{
  int ret = OB_SUCCESS;
  ObObjType obj_type = ObMaxType;
  const ObRawExprResType &arg_type = arg_expr->get_result_type();
  const ObRawExprResType &when_type = when_expr->get_result_type();
  ObRawExpr *new_when_expr = NULL; // cast expr may added
  ObRawExpr *new_arg_expr = NULL;
  if (OB_ISNULL(arg_expr) || OB_ISNULL(when_expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(arg_expr), KP(when_expr));
  } else if (OB_FAIL(expr_factory.create_raw_expr(T_OP_EQ, equal_expr))) {
    LOG_WARN("failed to create equal expr", K(ret));
  } else {
    if (OB_FAIL(ObExprArgCase::get_cmp_type(obj_type, arg_type.get_type(),
                                            when_type.get_type(),
                                            ObMaxType))) { // last argument is unused
      LOG_WARN("failed to get_cmp_type", K(ret));
    } else if (lib::is_mysql_mode() && ob_is_string_type(obj_type)) {
      // when cmp_type is string, need to use case_res_type.calc_type_.cs_type_ as
      // collation type. it is aggregated by all when_exprs.
      // eg: select case col_utf8_general_ci when col_utf8_general_ci then 'a'
      //                                     when col_utf8_bin then 'b' end from tbl;
      //     use col_utf8_bin to compare(see in ObExprArgCase::calc_result_typeN())
      ObRawExprResType cmp_type;
      cmp_type.set_type(obj_type);
      cmp_type.set_collation_type(cmp_cs_type);
      if (OB_FAIL(ObRawExprUtils::try_add_cast_expr_above(&expr_factory, &session,
                                                          *arg_expr, cmp_type, new_arg_expr)) ||
          OB_FAIL(ObRawExprUtils::try_add_cast_expr_above(&expr_factory, &session,
                                                          *when_expr, cmp_type, new_when_expr))) {
        LOG_WARN("failed to add_cast", K(ret), KP(new_arg_expr), KP(new_when_expr));
      }
    } else {
      new_arg_expr = arg_expr;
      new_when_expr = when_expr;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(new_when_expr) || OB_ISNULL(new_arg_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("expr is NULL", K(ret), KP(new_when_expr), KP(new_arg_expr));
    } else if (OB_FAIL(equal_expr->set_param_exprs(new_arg_expr, new_when_expr))) {
      LOG_WARN("failed to set_param_exprs", K(ret));
    }
  }
  return ret;
}

bool ObTransformPreProcess::is_same_row_type(const ObIArray<DistinctObjMeta> &left,
                                             const ObIArray<DistinctObjMeta> &right)
{
  bool ret_bool = true;
  if (left.count() != right.count()) {
    ret_bool = false;
  }
  for (int i = 0; ret_bool && i < left.count(); i++) {
    ret_bool = (left.at(i) == right.at(i));
  } // for end
  return ret_bool;
}

int ObTransformPreProcess::get_final_transed_or_and_expr(
    ObRawExprFactory &expr_factory,
    const ObSQLSessionInfo &session,
    const bool is_in_expr,
    ObIArray<ObRawExpr *> &transed_in_exprs,
    ObRawExpr *&final_or_expr)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *transed_or_expr = NULL;
  ObOpRawExpr *tmp_or_expr = NULL;
  ObOpRawExpr *last_or_expr = NULL;
  ObItemType op_type = is_in_expr ? T_OP_OR : T_OP_AND;
  if (OB_FAIL(expr_factory.create_raw_expr(op_type, transed_or_expr))) {
    LOG_WARN("failed to create or expr", K(ret));
  } else if (OB_ISNULL(transed_or_expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null expr", K(ret), K(transed_or_expr));
  } else if (OB_FAIL(transed_or_expr->init_param_exprs(2))) {
    LOG_WARN("failed to init param exprs", K(ret));
  } else if (OB_FAIL(transed_or_expr->add_param_expr(transed_in_exprs.at(0)))) {
    LOG_WARN("failed to add param expr", K(ret));
  } else {
    last_or_expr = transed_or_expr;
    int cur_idx = 1;
    for (; OB_SUCC(ret) && cur_idx < transed_in_exprs.count() - 1; cur_idx++) {
      if (OB_FAIL(expr_factory.create_raw_expr(op_type, tmp_or_expr))) {
        LOG_WARN("failed to create raw expr", K(ret));
      } else if (OB_ISNULL(tmp_or_expr)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid null expr", K(ret));
      } else if (OB_FAIL(tmp_or_expr->init_param_exprs(2))) {
        LOG_WARN("failed to init param exprs", K(ret));
      } else if (OB_FAIL(last_or_expr->add_param_expr(tmp_or_expr))) {
        LOG_WARN("failed to add param expr", K(ret));
      } else if (OB_FAIL(tmp_or_expr->add_param_expr(transed_in_exprs.at(cur_idx)))) {
        LOG_WARN("failed to add param expr", K(ret));
      } else {
        last_or_expr = tmp_or_expr;
      }
    }  // for end
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(last_or_expr->add_param_expr(transed_in_exprs.at(cur_idx)))) {
      LOG_WARN("failed to add param expr", K(ret));
    } else if (OB_FAIL(transed_or_expr->formalize(&session))) {
      LOG_WARN("expr formalize failed", K(ret));
    } else {
      final_or_expr = transed_or_expr;
    }
  }
  return ret;
}

int ObTransformPreProcess::add_row_type_to_array_no_dup(
                             ObIArray<ObSEArray<DistinctObjMeta, 4>> &row_type_array,
                             const ObSEArray<DistinctObjMeta, 4> &row_type)
{
  int ret = OB_SUCCESS;
  bool founded = false;
  for (int i = 0; OB_SUCC(ret) && !founded && i < row_type_array.count(); i++) {
    if (is_same_row_type(row_type_array.at(i), row_type)) {
      founded = true;
    }
  }
  if (!founded && OB_FAIL(row_type_array.push_back(row_type))) {
    LOG_WARN("failed to push back element", K(ret));
  }
  return ret;
}

int ObTransformPreProcess::check_and_transform_in_or_notin(ObRawExprFactory &expr_factory,
                                                           const ObSQLSessionInfo &session,
                                                           ObRawExpr *&in_expr,
                                                           bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(2 != in_expr->get_param_count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param cnt", K(ret), K(in_expr->get_param_count()));
  } else if (OB_ISNULL(in_expr->get_param_expr(0)) ||
             OB_ISNULL(in_expr->get_param_expr(1))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null params", K(ret), K(in_expr->get_param_expr(0)),
             K(in_expr->get_param_expr(1)));
  } else if (T_OP_ROW == in_expr->get_param_expr(0)->get_expr_type() ||
             (T_REF_QUERY == in_expr->get_param_expr(0)->get_expr_type() &&
              static_cast<ObQueryRefRawExpr*>(in_expr->get_param_expr(0))->get_output_column() > 1)) {
    // (x, y) in ((x0, y0), (x1, y1), ...)
    // (select x, y from ...) in ((x0, y0), (x1, y1), ...))
    LOG_DEBUG("Before Transform", K(*in_expr));
    ret = transform_in_or_notin_expr_with_row(
        expr_factory, session, T_OP_IN == in_expr->get_expr_type(), in_expr, trans_happened);
  } else {
    // x in (x0, x1, ...)
    LOG_DEBUG("Before Transform", K(*in_expr));
    ret = transform_in_or_notin_expr_without_row(
        expr_factory, session, T_OP_IN == in_expr->get_expr_type(), in_expr, trans_happened);
  }
  if (OB_SUCC(ret)) {
    ObOpRawExpr *op_raw_expr = dynamic_cast<ObOpRawExpr *>(in_expr);
    if (OB_ISNULL(op_raw_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid null op_raw_expr", K(ret));
    } else if (OB_FAIL(op_raw_expr->formalize(&session))) {
      LOG_WARN("formalize expr failed", K(ret));
    } else {
      LOG_DEBUG("After Transform", K(*op_raw_expr));
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_in_or_notin_recursively(ObRawExprFactory &expr_factory,
                                                           const ObSQLSessionInfo &session,
                                                           ObRawExpr *&root_expr,
                                                           bool &trans_happened)
{
  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < root_expr->get_param_count(); i++) {
    if (OB_ISNULL(root_expr->get_param_expr(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null param expr", K(ret));
    } else if (OB_FAIL(SMART_CALL(replace_in_or_notin_recursively(expr_factory,
                                                        session,
                                                        root_expr->get_param_expr(i),
                                                        trans_happened)))) {
      LOG_WARN("failed to replace in or notin recursively", K(ret));
    }
  }
  if (OB_SUCC(ret) &&
    (T_OP_IN == root_expr->get_expr_type() || T_OP_NOT_IN == root_expr->get_expr_type())) {
    if (OB_FAIL(check_and_transform_in_or_notin(
                expr_factory, session, root_expr, trans_happened))) {
      LOG_WARN("failed to check and transform", K(ret));
    }
  }
  return ret;
}

ObItemType ObTransformPreProcess::reverse_cmp_type_of_align_date4cmp(const ObItemType &cmp_type)
{
  ObItemType new_cmp_type = cmp_type;
  switch (cmp_type) {
    case T_OP_LE: {
      new_cmp_type = T_OP_GE;
      break;
    }
    case T_OP_LT: {
      new_cmp_type = T_OP_GT;
      break;
    }
    case T_OP_GE: {
      new_cmp_type = T_OP_LE;
      break;
    }
    case T_OP_GT: {
      new_cmp_type = T_OP_LT;
      break;
    }
    default: {
      break;
    }
  }
  return new_cmp_type;
}

int ObTransformPreProcess::replace_cast_expr_align_date4cmp(ObRawExprFactory &expr_factory,
                                                            const ObSQLSessionInfo &session,
                                                            const ObItemType &cmp_type,
                                                            ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is null.", K(ret));
  } else if (expr->get_expr_type() == T_FUN_SYS_CAST) {
    const bool is_implicit_cast = !CM_IS_EXPLICIT_CAST(expr->get_cast_mode());
    ObRawExprResType cast_res_type = expr->get_result_type();
    if (is_implicit_cast && (cast_res_type.is_date() || cast_res_type.is_datetime())) {
      ObRawExpr *cast_child = expr->get_param_expr(0);
      if (OB_ISNULL(cast_child)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("cast child expr is null.", K(ret));
      } else if (ObExprAlignDate4Cmp::is_align_date4cmp_support_obj_type(cast_child->get_data_type())) {
        // create a new align_date4cmp_expr to replace const_expr
        ObSysFunRawExpr *align_date4cmp_expr = NULL;
        if (OB_FAIL(expr_factory.create_raw_expr(T_FUN_SYS_ALIGN_DATE4CMP, align_date4cmp_expr))) {
          LOG_WARN("create align_date4cmp_expr fail.", K(ret), K(align_date4cmp_expr));
        } else if (OB_ISNULL(align_date4cmp_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("align_date4cmp_expr is null.", K(ret));
        } else if (OB_FAIL(align_date4cmp_expr->init_param_exprs(3))) {
          LOG_WARN("failed to init param exprs", K(ret));
        } else {
          align_date4cmp_expr->set_data_type(expr->get_data_type());
          align_date4cmp_expr->set_accuracy(expr->get_accuracy());
          align_date4cmp_expr->set_func_name("INTERNAL_FUNCTION");
          // Copy cast_mode to facilitate determining the method of handling invalid_time.
          align_date4cmp_expr->set_cast_mode(expr->get_cast_mode());
          // add first param_expr(just the cast_child) to align_date4cmp_expr
          if (OB_FAIL(align_date4cmp_expr->add_param_expr(cast_child))) {
            LOG_WARN("align_date4cmp_expr add param cast_expr fail.", K(ret), KPC(cast_child));
          } else {
            // create second param_expr (a ObConstRawExpr used to represent cmp_type)
            // and add it to align_date4cmp_expr.
            ObConstRawExpr *cmp_type_expr = NULL;
            if (OB_FAIL(ObRawExprUtils::build_const_int_expr(expr_factory,
                                              ObIntType, cmp_type, cmp_type_expr))) {
            LOG_WARN("failed to build const int cmp_type_expr", K(ret));
            } else if (OB_ISNULL(cmp_type_expr)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("cmp_type_expr is null.", K(ret));
            } else if (OB_FAIL(align_date4cmp_expr->add_param_expr(cmp_type_expr))){
              LOG_WARN("align_date4cmp_expr add param cmp_type_expr fail.", K(ret), KPC(cmp_type_expr));
            } else {
              // create third param_expr (a ObConstRawExpr used to represent res_type)
              // and add it to align_date4cmp_expr.
              ObConstRawExpr *res_type_expr = NULL;
              ObObjType cast_res_type = expr->get_result_type().get_type();
              int64_t cast_res_type_int = cast_res_type;
              if (OB_FAIL(ObRawExprUtils::build_const_int_expr(expr_factory,
                                                  ObIntType, cast_res_type_int, res_type_expr))) {
                LOG_WARN("failed to build const int res_type_expr", K(ret));
              } else if (OB_ISNULL(res_type_expr)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("res_type_expr is null.", K(ret));
              } else if (OB_FAIL(align_date4cmp_expr->add_param_expr(res_type_expr))){
                LOG_WARN("align_date4cmp_expr add param res_type_expr fail.", K(ret), KPC(res_type_expr));
              } else if (OB_FAIL(align_date4cmp_expr->formalize(&session))) {
                LOG_WARN("align_date4cmp_expr formalize fail.", K(ret));
              } else {
                // Change the expr pointer pointing to cast_expr
                // to point to the newly created align_date4cmp_expr.
                expr = align_date4cmp_expr;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_op_row_expr_align_date4cmp(ObRawExprFactory &expr_factory,
                                                            const ObSQLSessionInfo &session,
                                                            const ObItemType &cmp_type,
                                                            ObRawExpr *&left_row_expr,
                                                            ObRawExpr *&right_row_expr)
{
  int ret = OB_SUCCESS;
  if (left_row_expr->get_expr_type() == T_OP_ROW &&
      right_row_expr->get_expr_type() == T_OP_ROW) {
    int64_t expr_cnt = left_row_expr->get_param_count();
    if (right_row_expr->get_param_count() != expr_cnt) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid param cnt", K(ret), KPC(left_row_expr),
                                            KPC(right_row_expr));
    } else {
      int64_t last_expr_idx = expr_cnt - 1;
      ObRawExpr *expr0 = left_row_expr->get_param_expr(last_expr_idx);
      ObRawExpr *expr1 = right_row_expr->get_param_expr(last_expr_idx);
      if (OB_ISNULL(expr0) || OB_ISNULL(expr1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid null params", K(ret), KPC(left_row_expr),
                                                KPC(right_row_expr));
      } else {
        const ObItemType reverse_cmp_type = reverse_cmp_type_of_align_date4cmp(cmp_type);
        if (OB_FAIL(replace_cast_expr_align_date4cmp(expr_factory, session, reverse_cmp_type, expr0))) {
          LOG_WARN("replace left cast_expr fail.", K(ret), KPC(expr0));
        } else {
          left_row_expr->get_param_expr(last_expr_idx) = expr0;
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(replace_cast_expr_align_date4cmp(expr_factory, session, cmp_type, expr1))) {
            LOG_WARN("replace right cast_expr fail.", K(ret), KPC(expr1));
          } else {
            right_row_expr->get_param_expr(last_expr_idx) = expr1;
          }
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_and_transform_align_date4cmp(ObRawExprFactory &expr_factory,
                                                              const ObSQLSessionInfo &session,
                                                              ObRawExpr *&cmp_expr,
                                                              const ObItemType &cmp_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(2 != cmp_expr->get_param_count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param cnt", K(ret), K(cmp_expr->get_param_count()));
  } else {
    ObRawExpr *expr0 = cmp_expr->get_param_expr(0);
    ObRawExpr *expr1 = cmp_expr->get_param_expr(1);
    if (OB_ISNULL(expr0) || OB_ISNULL(expr1)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null params", K(ret), K(cmp_expr->get_param_expr(0)),
              K(cmp_expr->get_param_expr(1)));
    } else if (OB_FAIL(replace_op_row_expr_align_date4cmp(expr_factory, session,
                                        cmp_type, expr0, expr1))){
      LOG_WARN("fail to replace op_row_expr", K(ret));
    } else {
      // By default, align_date4cmp_expr expects the first parameter to be
      // the value on the right side of the comparison operator.
      // Therefore, if you pass the value on the left side,
      // you need to reverse the comparison operator that is passed in.
      const ObItemType reverse_cmp_type = reverse_cmp_type_of_align_date4cmp(cmp_type);
      if (OB_FAIL(replace_cast_expr_align_date4cmp(expr_factory, session, reverse_cmp_type, expr0))) {
        LOG_WARN("replace left cast_expr fail.", K(ret), KPC(expr0));
      } else {
        cmp_expr->get_param_expr(0) = expr0;
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(replace_cast_expr_align_date4cmp(expr_factory, session, cmp_type, expr1))) {
          LOG_WARN("replace right cast_expr fail.", K(ret), KPC(expr1));
        } else {
          cmp_expr->get_param_expr(1) = expr1;
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_align_date4cmp_recursively(ObRawExprFactory &expr_factory,
                                                              const ObSQLSessionInfo &session,
                                                              ObRawExpr *&root_expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(root_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("root_expr is null.", K(ret));
  }
  // Traverse all exprs from the root.
  for (int i = 0; OB_SUCC(ret) && i < root_expr->get_param_count(); i++) {
    if (OB_ISNULL(root_expr->get_param_expr(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null param expr", K(ret));
    } else if (OB_FAIL(SMART_CALL(replace_align_date4cmp_recursively(expr_factory, session,
                                                        root_expr->get_param_expr(i))))) {
      LOG_WARN("failed to replace in or notin recursively", K(ret));
    }
  }
  // If the current expression is cmp_expr, check if align_date4cmp transformation is necessary.
  if (OB_SUCC(ret) && IS_COMMON_COMPARISON_OP(root_expr->get_expr_type())) {
    if (OB_FAIL(check_and_transform_align_date4cmp(
                expr_factory, session, root_expr, root_expr->get_expr_type()))) {
      LOG_WARN("failed to check and transform", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_inner_row_cmp_val_recursively(ObRawExprFactory &expr_factory,
                                                                 const ObSQLSessionInfo &session,
                                                                 ObRawExpr *&root_expr,
                                                                 bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(root_expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null param expr.", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < root_expr->get_param_count(); i++) {
    if (OB_ISNULL(root_expr->get_param_expr(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null param expr", K(ret));
    } else if (OB_FAIL(SMART_CALL(replace_inner_row_cmp_val_recursively(expr_factory,
                                                                       session,
                                                                       root_expr->get_param_expr(i),
                                                                       trans_happened)))) {
      LOG_WARN("failed to replace row cmp val recursively", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (T_OP_LE <= root_expr->get_expr_type() && T_OP_GT >= root_expr->get_expr_type()) {
    // For row comparisons that are T_OP_LT/T_OP_LE or T_OP_GT/T_OP_GE expressions, need to check
    // whether the range may be expanded. If so, change it to the corresponding inner expression.
    int row_dim = -1;
    if (OB_FAIL(ObRelationalExprOperator::is_row_cmp(*root_expr, row_dim))) {
      LOG_WARN("failed to get row dimension", K(ret));
    } else if (row_dim > 0) {
      if (OB_FAIL(check_and_transform_inner_row_cmp_val(
                expr_factory, session, root_expr, trans_happened))) {
        LOG_WARN("failed to check and transform", K(ret));
      }
    }
  } else if (T_OP_SQ_LE <= root_expr->get_expr_type() && T_OP_SQ_GT >= root_expr->get_expr_type()) {
    // We also need to handle the comparison of subqueries, only focus on the op_row part of
    // the subquery comparison.
    if (OB_UNLIKELY(2 != root_expr->get_param_count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid param count", K(ret), K(root_expr->get_param_count()));
    } else if ((T_OP_ROW == root_expr->get_param_expr(0)->get_expr_type() &&
                root_expr->get_param_expr(0)->get_expr_type() > 1) ||
               (T_OP_ROW == root_expr->get_param_expr(1)->get_expr_type() &&
                root_expr->get_param_expr(1)->get_expr_type() > 1)) {
      if (OB_FAIL(check_and_transform_inner_row_cmp_val(
                expr_factory, session, root_expr, trans_happened))) {
        LOG_WARN("failed to check and transform", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_and_transform_inner_row_cmp_val(ObRawExprFactory &expr_factory,
                                                                 const ObSQLSessionInfo &session,
                                                                 ObRawExpr *&row_cmp_expr,
                                                                 bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObRawExpr *left = NULL;
  ObRawExpr *right = NULL;
  bool row_cmp_trans_happened = false;
  const ObItemType op_type = row_cmp_expr->get_expr_type();
  if (OB_UNLIKELY(2 != row_cmp_expr->get_param_count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param cnt", K(ret), K(row_cmp_expr->get_param_count()));
  } else if (OB_ISNULL(left = row_cmp_expr->get_param_expr(0)) ||
             OB_ISNULL(right = row_cmp_expr->get_param_expr(1))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null params", K(ret), KP(left), KP(right));
  } else if (!((T_OP_LE <= op_type && T_OP_GT >= op_type) ||
               (T_OP_SQ_LE <= op_type && T_OP_SQ_GT >= op_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid op type", K(ret), K(op_type));
  } else if (T_OP_ROW != left->get_expr_type() && T_OP_ROW != right->get_expr_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid types params", K(ret), K(left->get_expr_type()), K(right->get_expr_type()));
  } else if (T_OP_ROW == left->get_expr_type() &&
             OB_FAIL(transform_inner_op_row_cmp_for_decimal_int<true>(expr_factory,
                                                                      session,
                                                                      row_cmp_expr,
                                                                      left,
                                                                      row_cmp_trans_happened))) {
    LOG_WARN("fail to transfrom left op row cmp", K(ret), K(op_type));
  } else if (T_OP_ROW == right->get_expr_type() &&
            OB_FAIL(transform_inner_op_row_cmp_for_decimal_int<false>(expr_factory,
                                                                       session,
                                                                       row_cmp_expr,
                                                                       right,
                                                                       row_cmp_trans_happened))) {
    LOG_WARN("fail to transfrom left op row cmp", K(ret), K(op_type));
  } else if (row_cmp_trans_happened) {
    ObOpRawExpr *op_raw_expr = dynamic_cast<ObOpRawExpr *>(row_cmp_expr);
    if (OB_ISNULL(op_raw_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid null op_raw_expr", K(ret));
    } else if (OB_FALSE_IT(op_raw_expr->get_param_expr(0) = left)) {
    } else if (OB_FALSE_IT(op_raw_expr->get_param_expr(1) = right)) {
    } else if (OB_FAIL(op_raw_expr->formalize(&session))) {
      LOG_WARN("formalize expr failed", K(ret));
    } else {
      trans_happened = row_cmp_trans_happened;
      LOG_DEBUG("After Transform", K(*op_raw_expr));
    }
  }
  return ret;
}

template<bool IS_LEFT>
static int get_error_ret(const ObItemType op_type)
{
  int error_ret = OB_SUCCESS;
  switch (op_type) {
    case T_OP_LT:
    case T_OP_SQ_LT: {
      // (c1, c2) < (cast_expr, cast_expr) => (c1, c2) < (cast_up(expr), cast_up(expr))
      error_ret = IS_LEFT ? OB_ERR_MAX_VALUE : OB_ERR_MIN_VALUE;
      break;
    }
    case T_OP_GT:
    case T_OP_SQ_GT: {
      // (c1, c2) > (cast_expr, cast_expr) => (c1, c2) > (cast_down(expr), cast_down(expr))
      error_ret = IS_LEFT ? OB_ERR_MIN_VALUE : OB_ERR_MAX_VALUE;
      break;
    }
    case T_OP_LE:
    case T_OP_SQ_LE: {
      // (c1, c2) <= (case_expr, cast_expr) => (c1, c2) <= (cast_down(expr), cast_down(expr))
      error_ret = IS_LEFT ? OB_ERR_MIN_VALUE : OB_ERR_MAX_VALUE;
      break;
    }
    case T_OP_GE:
    case T_OP_SQ_GE: {
      // (c1, c2) >= (cast_expr, cast_expr) => (c1, c2) < (cast_up(expr), cast_up(expr))
      error_ret = IS_LEFT ? OB_ERR_MAX_VALUE : OB_ERR_MIN_VALUE;
      break;
    }
    default:
      break;
  }
  return error_ret;
}

template<bool IS_LEFT>
int ObTransformPreProcess::transform_inner_op_row_cmp_for_decimal_int(
    ObRawExprFactory &expr_factory,
    const ObSQLSessionInfo &session,
    ObRawExpr *&row_cmp_expr,
    ObRawExpr *&row_expr,
    bool &trans_happened)
{
  int ret = OB_SUCCESS;
  // The inner expression will return the result based on whether param 1 and param 2 are equal.
  // If they are not equal, it means that the original range has been enlarged. At this time,
  // an error code needs to be returned. The rules are as follows:
  // if the input expr is on the left and it is greater than the value of the column,
  // OB_ERR_MIN_VALUE should be returned at this time, otherwise it is the opposite.
  const ObItemType op_type = row_cmp_expr->get_expr_type();
  const int error_ret = -get_error_ret<IS_LEFT>(op_type);
  // save the error code in the extra field of the expression to pass information
  const uint64_t extra = static_cast<uint64_t>(error_ret);
  const int64_t row_count = row_expr->get_param_count();
  ObSEArray<ObRawExpr *, 4> new_params;
  bool trans_happened_in_row_op = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_count - 1; ++i) {
    ObRawExpr *param_expr = row_expr->get_param_expr(i);
    ObRawExpr *next_expr = row_expr->get_param_expr(i + 1);
    ObRawExpr *new_expr = next_expr;
    const ObRawExprResType &res_type = param_expr->get_result_type();
    if (OB_ISNULL(param_expr) || OB_ISNULL(next_expr)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null params", K(ret), KP(param_expr), KP(next_expr));
    } else if (param_expr->get_expr_type() == T_FUN_SYS_CAST &&
        ob_is_decimal_int_tc(res_type.get_type()) &&
        CM_IS_CONST_TO_DECIMAL_INT(param_expr->get_cast_mode())) {
      // try to transform cast expr
      ObRawExpr *input_expr = param_expr->get_param_expr(0);
      ObSysFunRawExpr *inner_row_cmp_expr = NULL;
      if (OB_ISNULL(input_expr)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid null params", K(ret), KP(input_expr));
      } else if (ob_is_int_uint_tc(input_expr->get_result_type().get_type()) ||
                 ob_is_temporal_type(input_expr->get_result_type().get_type()) ||
                 ob_is_bit_tc(input_expr->get_result_type().get_type())) {
        // ignore this type as input, because it has no decimals there is no loss of precision
      } else if (ob_is_number_or_decimal_int_tc(input_expr->get_result_type().get_type()) &&
                 input_expr->get_result_type().get_scale() != SCALE_UNKNOWN_YET &&
                 input_expr->get_result_type().get_scale() <= res_type.get_scale()) {
        // there are more decimal places, no precision loss, no conversion required
      } else if (OB_FAIL(ObRawExprUtils::build_inner_row_cmp_expr(expr_factory,
                                                                  &session,
                                                                  param_expr,
                                                                  input_expr,
                                                                  next_expr,
                                                                  error_ret,
                                                                  inner_row_cmp_expr))) {
        LOG_WARN("fail to build inner row cmp expr", K(ret));
      } else {
        new_expr = inner_row_cmp_expr;
        trans_happened_in_row_op = true;
      }
    }
    if (OB_SUCC(ret)) {
      // The first parameter does not need to be converted
      if (i == 0 && OB_FAIL(new_params.push_back(param_expr))) {
        LOG_WARN("fail to push back first param expr", K(ret));
      } else if (OB_FAIL(new_params.push_back(new_expr))) {
        LOG_WARN("fail to push back new expr", K(ret));
      }
    }
  }
  // replace all params expr to new param exprs if tansform happened
  if (OB_FAIL(ret) || !trans_happened_in_row_op) {
  } else if (new_params.count() != row_count) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected row count", K(ret), K(new_params.count()), K(row_count));
  } else {
    // This vector may have multiple values, such as (C1, C2, C3, ..., C). After conversion,
    // it is (C1, inner(c2), inner(c3), ..., inner(c))
    trans_happened = true;
    for (int64_t i = 0; OB_SUCC(ret) && trans_happened && i < row_count; ++i) {
      row_expr->get_param_expr(i) = new_params.at(i);
    }
  }
  return ret;
}

/*@brief ObTransformPreProcess::transformer_aggr_expr is used to expand some complex aggregate functions into ordinary aggregate operations;
* eg:var_pop(expr) ==> SUM(expr*expr) - SUM(expr)* SUM(expr)/ COUNT(expr)) / COUNT(expr)
* where the ObExpandAggregateUtils class mainly involves related functions for expanding complex aggregate functions:
*   1.ObExpandAggregateUtils::expand_aggr_expr ==> used to handle the ordinary aggr function interface
*   2.ObExpandAggregateUtils::expand_window_aggr_expr  ==> used to handle the aggr function interface in window functions
*
 */
int ObTransformPreProcess::transformer_aggr_expr(ObDMLStmt *stmt,
                                                 bool &trans_happened)
{
  int ret = OB_SUCCESS;
  bool is_expand_aggr = false;
  bool is_expand_window_aggr = false;
  bool is_happened = false;
  // The previous logic ensured the order of nested aggregation and ordinary function rewriting, trans_happened includes information on whether a nested aggregation function rewrite occurred
  bool is_trans_nested_aggr_happened = trans_happened;
  trans_happened = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->expr_factory_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null allocator", K(ret));
  } else {
    ObExpandAggregateUtils expand_aggr_utils(*ctx_->expr_factory_,  ctx_->session_info_);
    if (OB_FAIL(expand_aggr_utils.expand_aggr_expr(stmt, is_expand_aggr))) {
      LOG_WARN("failed to expand aggr expr", K(ret));
    } else if (OB_FAIL(expand_aggr_utils.expand_window_aggr_expr(stmt, is_expand_window_aggr))) {
      LOG_WARN("failed to expand window aggr expr", K(ret));
    // If nested aggregate function rewriting occurred:
    // select max(avg(c1)) from t1 group by c2;
    // ==>
    // select max(a) from (select avg(c1) as a from t1 group by c2);
    // Need to rewrite the aggregate functions inside the view, and the comment is that nested aggregate functions only have two layers, will not generate a structure exceeding 2 layers
    } else if (is_trans_nested_aggr_happened) {
      TableItem *table_item = NULL;
      if (OB_UNLIKELY(stmt->get_table_items().count() != 1) ||
          OB_ISNULL(table_item = stmt->get_table_item(0)) ||
          OB_UNLIKELY(!table_item->is_generated_table())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(table_item), K(stmt->get_table_items().count()), K(ret));
      } else if (OB_FAIL(transformer_aggr_expr(table_item->ref_query_, is_happened))) {
        LOG_WARN("failed to transformer aggr expr", K(ret));
      } else {/*do nothing*/}
    }
  }
  if (OB_SUCC(ret)) {
    trans_happened = is_expand_aggr | is_expand_window_aggr | is_happened;
  }
  return ret;
}

int ObTransformPreProcess::transform_json_object_expr_with_star(const ObIArray<ObParentDMLStmt> &parent_stmts,
                                                                ObDMLStmt *stmt, bool &trans_happened)
{
  INIT_SUCC(ret);
  ObSEArray<ObRawExpr*, 4> replace_exprs;

  JsonObjectStarChecker expr_checker(replace_exprs);
  ObStmtExprGetter visitor;
  visitor.checker_ = &expr_checker;
  if (OB_SUCC(ret) && OB_FAIL(stmt->iterate_stmt_expr(visitor))) {
    LOG_WARN("get relation exprs failed", K(ret));
  }

  //collect query udt exprs which need to be replaced
  for (int64_t i = 0; OB_SUCC(ret) && i < replace_exprs.count(); i++) {
    if (OB_ISNULL(replace_exprs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("replace expr is null", K(ret));
    } else {
      ObSysFunRawExpr *func_expr = static_cast<ObSysFunRawExpr*>(replace_exprs.at(i));
      if (OB_FAIL(ObTransformUtils::expand_wild_star_to_columns(ctx_, stmt, func_expr))) {
        LOG_WARN("fail to transform star to column node", K(ret));
      } else {
        trans_happened = true;
      }
    }
  }

  return ret;
}

int ObTransformPreProcess::add_semantic_vector_dis_params_to_new_expr(ObDMLStmt *stmt, ObRawExpr *semantic_expr, ObRawExpr *&new_semantic_expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr_0 = semantic_expr->get_param_expr(0);
  ObRawExpr *expr_1 = semantic_expr->get_param_expr(1);

  ObRawExpr *chunk_col_ref = nullptr;
  ObRawExpr *query_vector = nullptr;

  ObColumnRefRawExpr *vector_col_ref = nullptr;
  ObRawExpr *cast_query_vector = nullptr;
  ObRawExpr *dis_type = nullptr;

  ObSchemaGetterGuard *schema_guard = nullptr;

  if (OB_ISNULL(expr_0)
      || OB_ISNULL(expr_1)
      || OB_ISNULL(ctx_)
      || OB_ISNULL(ctx_->session_info_)
      || OB_ISNULL(ctx_->schema_checker_)
      || OB_ISNULL(schema_guard = ctx_->schema_checker_->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param of semantic_distance is null", KP(expr_0), KP(expr_1));
  } else if (expr_0->get_expr_type() != T_REF_COLUMN && expr_1->get_expr_type() != T_REF_COLUMN) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("none param of semantic_distance is col ref", K(expr_0->get_expr_type()), K(expr_1->get_expr_type()));
  } else {
    chunk_col_ref = expr_0->get_expr_type() == T_REF_COLUMN ? expr_0 : expr_1;
    query_vector = expr_0->get_expr_type() == T_REF_COLUMN ? expr_1 : expr_0;
    ObColumnRefRawExpr *raw_chunk_col_ref = static_cast<ObColumnRefRawExpr*>(chunk_col_ref);

    TableItem *table_item = NULL;
    const share::schema::ObTableSchema *data_table_schema = nullptr;
    uint64_t tenant_id = ctx_->session_info_->get_effective_tenant_id();
    if (OB_ISNULL(table_item = stmt->get_table_item_by_id(raw_chunk_col_ref->get_table_id()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table_item is NULL", K(ret), K(raw_chunk_col_ref->get_table_id()));
    } else if (OB_FAIL(schema_guard->get_table_schema(tenant_id, table_item->ref_id_, data_table_schema))) {
      LOG_WARN("failed to get table schema", K(ret), K(table_item->ref_id_));
    } else if (OB_FAIL(create_embedded_table_vector_col_ref(stmt, table_item, data_table_schema, raw_chunk_col_ref, vector_col_ref))) {
      LOG_WARN("failed to create embedded table vector col ref", K(ret));
    } else if (OB_FAIL(create_cast_query_vector_expr(query_vector, vector_col_ref, cast_query_vector))) {
      LOG_WARN("failed to create cast query vector expr", K(ret));
    } else if (OB_FAIL(create_distance_type_const_expr(stmt, data_table_schema, raw_chunk_col_ref, dis_type))) {
      LOG_WARN("failed to create distance type const expr", K(ret));
    } else {
      ObSysFunRawExpr *temp_semantic_expr = nullptr;
      if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_FUN_SYS_SEMANTIC_VECTOR_DISTANCE, temp_semantic_expr))) {
        LOG_WARN("failed to create new semantic expr", K(ret));
      } else if (OB_ISNULL(temp_semantic_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("new semantic expr is null", K(ret));
      } else if (OB_FAIL(temp_semantic_expr->init_param_exprs(3))) {
        LOG_WARN("failed to init semantic param", K(ret));
      } else if (OB_FAIL(temp_semantic_expr->add_param_expr(vector_col_ref))) {
        LOG_WARN("failed to add param to semantic expr", K(ret));
      } else if (OB_FAIL(temp_semantic_expr->add_param_expr(cast_query_vector))) {
        LOG_WARN("failed to add param to semantic expr", K(ret));
      } else if (OB_FAIL(temp_semantic_expr->add_param_expr(dis_type))) {
        LOG_WARN("failed to add param to semantic expr", K(ret));
      } else if (OB_FAIL(temp_semantic_expr->formalize(ctx_->session_info_))) {
        LOG_WARN("formalize failed", K(ret));
      } else {
        new_semantic_expr = temp_semantic_expr;
        LOG_TRACE("successfully created new semantic vector distance expr with 3 params");
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::create_embedded_table_vector_col_ref(
    ObDMLStmt *stmt,
    TableItem *table_item,
    const share::schema::ObTableSchema *data_table_schema,
    ObColumnRefRawExpr *chunk_col_ref,
    ObColumnRefRawExpr *&vector_col_ref)
{
  int ret = OB_SUCCESS;
  vector_col_ref = nullptr;
  ColumnItem *exist_column_item = nullptr;

  for (int64_t i = 0; OB_SUCC(ret) && i < data_table_schema->get_column_count() && OB_ISNULL(vector_col_ref); ++i) {
    const ObColumnSchemaV2 *col_schema = data_table_schema->get_column_schema_by_idx(i);
    if (OB_ISNULL(col_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null column schema ptr", K(ret));
    } else if (col_schema->is_hybrid_embedded_vec_column()) {
      // check match to chunk col
      bool is_same_index = false;
      ObSEArray<uint64_t, 4> embedded_cascaded_ids;
      if (OB_FAIL(col_schema->get_cascaded_column_ids(embedded_cascaded_ids))) {
        LOG_WARN("failed to get cascaded ids for embedded vector column", K(ret), KPC(col_schema));
      } else {
         for (int64_t j = 0; !is_same_index && j < embedded_cascaded_ids.count(); ++j) {
            if (embedded_cascaded_ids.at(j) == chunk_col_ref->get_column_id()) {
              is_same_index = true;
            }
         }
      }

      if (is_same_index) {
        if (OB_NOT_NULL(exist_column_item = stmt->get_column_item(table_item->table_id_, col_schema->get_column_id()))) {
          vector_col_ref = exist_column_item->expr_;
        } else if (OB_FAIL(ObRawExprUtils::build_column_expr(*ctx_->expr_factory_, *col_schema,
                                                      ctx_->session_info_, vector_col_ref))) {
          LOG_WARN("failed to build target vector column expr", K(ret));
        } else if (OB_ISNULL(vector_col_ref)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to build target vector column expr", K(ret));
        } else {
          vector_col_ref->set_ref_id(table_item->table_id_, col_schema->get_column_id());
          vector_col_ref->set_column_attr(data_table_schema->get_table_name(), col_schema->get_column_name_str());
          vector_col_ref->set_database_name(chunk_col_ref->get_database_name());
          vector_col_ref->del_column_flag(VIRTUAL_GENERATED_COLUMN_FLAG);
          ColumnItem column_item;
          column_item.expr_ = vector_col_ref;
          column_item.table_id_ = vector_col_ref->get_table_id();
          column_item.column_id_ = vector_col_ref->get_column_id();
          column_item.column_name_ = vector_col_ref->get_column_name();
          if (OB_FAIL(stmt->add_column_item(column_item))) {
            LOG_WARN("add column item to stmt failed", K(ret));
          } else if (OB_FAIL(vector_col_ref->formalize(ctx_->session_info_))) {
            LOG_WARN("formalize failed", K(ret));
          }
        }
      }
    }
  }

  if (OB_SUCC(ret) && OB_ISNULL(vector_col_ref)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "use semantic_vector_distance without hybrid vector index");
    LOG_WARN("not find hybrid vector index", K(ret));
  }

  return ret;
}

int ObTransformPreProcess::create_cast_query_vector_expr(
    ObRawExpr *query_vector,
    ObRawExpr *vector_col_ref,
    ObRawExpr *&cast_query_vector)
{
  int ret = OB_SUCCESS;
  cast_query_vector = nullptr;

  if (OB_ISNULL(vector_col_ref)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid arguments", K(ret));
  } else {
    ObExprResType dst_type = vector_col_ref->get_result_type();
    ObSysFunRawExpr *cast_expr = nullptr;
    if (OB_FAIL(ObRawExprUtils::create_cast_expr(
        *ctx_->expr_factory_, query_vector, dst_type, cast_expr, ctx_->session_info_))) {
      LOG_WARN("failed to create cast expr", K(ret));
    } else if (OB_ISNULL(cast_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cast expr is null", K(ret));
    } else {
      cast_query_vector = cast_expr;
      LOG_TRACE("created cast query vector expr", K(dst_type));
    }
  }
  return ret;
}

int ObTransformPreProcess::create_distance_type_const_expr(
    ObDMLStmt *stmt,
    const share::schema::ObTableSchema *data_table_schema,
    ObColumnRefRawExpr *chunk_col_ref,
    ObRawExpr *&dis_type)
{
  int ret = OB_SUCCESS;
  dis_type = nullptr;

  uint64_t column_id = chunk_col_ref->get_column_id();
  ObSchemaGetterGuard *schema_guard = ctx_->schema_checker_->get_schema_guard();
  int64_t vec_dis_type = ObVectorIndexDistAlgorithm::VIDA_MAX;

  share::ObVectorIndexParam vector_index_param;
  bool param_filled = false;
  if (OB_FAIL(share::ObVectorIndexUtil::get_vector_index_param(
      schema_guard, *data_table_schema, column_id, vector_index_param, param_filled))) {
    LOG_WARN("failed to get vector index param", K(ret), KPC(data_table_schema), K(column_id));
  } else if (!param_filled) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("vector index param not found", K(ret), KPC(data_table_schema), K(column_id));
  } else if (OB_FAIL(share::ObVectorIndexUtil::get_vec_dis_type_from_dis_algorithm(vector_index_param.dist_algorithm_, vec_dis_type))) {
    LOG_WARN("failed to get vec dis type", K(ret));
  } else {
    ObConstRawExpr *const_expr = nullptr;
    if (OB_FAIL(ctx_->expr_factory_->create_raw_expr(T_INT, const_expr))) {
      LOG_WARN("failed to create const expr", K(ret));
    } else if (OB_ISNULL(const_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("const expr is null", K(ret));
    } else {
      ObObj obj;
      obj.set_int(static_cast<int64_t>(vec_dis_type));
      const_expr->set_value(obj);

      dis_type = const_expr;
      LOG_TRACE("created distance type const expr", K(vector_index_param.dist_algorithm_));
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_semantic_vector_dis_expr(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  ObSEArray<ObRawExpr*, 4> semantic_vec_dis_exprs;

  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret));
  } else {

    SemanticVectorDistExprChecker expr_checker(semantic_vec_dis_exprs);
    ObStmtExprGetter visitor;
    visitor.checker_ = &expr_checker;
    if (OB_FAIL(stmt->iterate_stmt_expr(visitor))) {
      LOG_WARN("failed to iterate stmt expr", K(ret));
    } else {
      ObSEArray<ObRawExpr*, 4> old_exprs;
      ObSEArray<ObRawExpr*, 4> new_exprs;

      for (int64_t i = 0; OB_SUCC(ret) && i < semantic_vec_dis_exprs.count(); ++i) {
        ObRawExpr *semantic_expr = semantic_vec_dis_exprs.at(i);
        if (OB_ISNULL(semantic_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("semantic_distance expr is null", K(ret));
        } else if (semantic_expr->get_param_count() != 2) {
          ret = OB_ERR_PARAM_SIZE;
          LOG_WARN("semantic_distance expr should have 2 params", K(ret), K(semantic_expr->get_param_count()));
        } else {
          ObRawExpr *new_semantic_expr = nullptr;
          if (OB_FAIL(add_semantic_vector_dis_params_to_new_expr(stmt, semantic_expr, new_semantic_expr))) {
            LOG_WARN("failed to add hybrid vector params to expr", K(ret));
          } else if (OB_ISNULL(new_semantic_expr)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("new semantic_distance expr is null", K(ret));
          } else {
            if (OB_FAIL(old_exprs.push_back(semantic_expr))) {
              LOG_WARN("failed to push back old expr", K(ret));
            } else if (OB_FAIL(new_exprs.push_back(new_semantic_expr))) {
              LOG_WARN("failed to push back new expr", K(ret));
            } else {
              trans_happened = true;
            }
          }
        }
      }

      if (OB_SUCC(ret) && trans_happened && old_exprs.count() > 0) {
        if (OB_FAIL(stmt->replace_relation_exprs(old_exprs, new_exprs))) {
          LOG_WARN("failed to replace semantic_distance exprs in stmt", K(ret));
        }
      }
    }

  }
  return ret;
}

//transform batch update stmt to multiple update with nlj:
// update t1 set b=1 where a=1; update t1 set b=2 where a=2; update t1 set b=3 where a=3;...
//-> update t1, (select 1, 1 from dual ...) v set t1.b=v.b where t1.a=v.a;
// batch delete is as batch update
int ObTransformPreProcess::transform_for_upd_del_batch_stmt(ObDMLStmt *batch_stmt,
                                                            ObSelectStmt *inner_view_stmt,
                                                            bool &trans_happened)
{
  int ret = OB_SUCCESS;
  // create select list
  if (OB_FAIL(mock_select_list_for_upd_del(*batch_stmt, *inner_view_stmt))) {
    LOG_WARN("mock select list for batch stmt failed", K(ret));
  } else if (OB_FAIL(formalize_batch_stmt(batch_stmt,
                                          inner_view_stmt,
                                          batch_stmt->get_query_ctx()->ab_param_exprs_,
                                          trans_happened))) {
    LOG_WARN("fail to formalize batch stmt", K(ret), KPC(batch_stmt));
  }
  return ret;
}

int ObTransformPreProcess::transform_for_insertup_batch_stmt(ObDMLStmt *batch_stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = NULL;
  ObExecContext *exec_ctx = nullptr;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObSQLSessionInfo *session_info = NULL;
  ObSEArray<ObRawExpr*, 4> assignment_params;
  ObSEArray<ObRawExpr*, 4> params;
  ObSEArray<ObRawExpr*, 4> assignments_exprs;
  ObSEArray<ObRawExpr*, 4> group_param_exprs;
  ObPseudoColumnRawExpr *stmt_id_expr = NULL;
  const ParamStore *param_store = nullptr;

  if (OB_ISNULL(ctx_)
      || OB_ISNULL(exec_ctx = ctx_->exec_ctx_)
      || OB_ISNULL(plan_ctx = exec_ctx->get_physical_plan_ctx())
      || OB_ISNULL(session_info = ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ctx_), K(exec_ctx), K(plan_ctx), K(ret));
  } else if (FALSE_IT(param_store = &plan_ctx->get_param_store())) {
    // do nothing
  } else if (OB_ISNULL(batch_stmt) ||
             OB_UNLIKELY(!batch_stmt->is_insert_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("batch_stmt or inner_view_stmt is null", K(ret), K(batch_stmt));
  } else if (FALSE_IT(insert_stmt = static_cast<ObInsertStmt*>(batch_stmt))) {
  } else if (FALSE_IT(batch_stmt->get_query_ctx()->ins_values_batch_opt_ = true)) {
  } else if (OB_FAIL(create_stmt_id_expr(stmt_id_expr))) {
    LOG_WARN("fail to create stmt id expr", K(ret));
  } else if (FALSE_IT(static_cast<ObDelUpdStmt*>(batch_stmt)->set_ab_stmt_id_expr(stmt_id_expr))) {
  } else if (OB_FAIL(insert_stmt->get_all_assignment_exprs(assignments_exprs))) {
    LOG_WARN("fail to get all assignments exprs", K(ret));
  } else if (OB_FAIL(ObRawExprUtils::extract_params(assignments_exprs, assignment_params))) {
    LOG_WARN("extract param expr from related exprs failed", K(ret));
  }

  // 1. record assignment params
  for (int64_t i = 0; OB_SUCC(ret) && i < assignment_params.count(); ++i) {
    ObConstRawExpr *param_expr = static_cast<ObConstRawExpr *>(assignment_params.at(i));
    int64_t param_idx = param_expr->get_value().get_unknown();
    if (param_idx < 0 || param_idx >= param_store->count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("param_idx is invalid", K(ret), K(param_idx), KPC(param_store));
    } else if (param_store->at(param_idx).is_batch_parameters()
        && !has_exist_in_array(batch_stmt->get_query_ctx()->ab_param_exprs_,
                               static_cast<ObRawExpr*>(param_expr))) {
      ObPseudoColumnRawExpr *group_param_expr = nullptr;
      //create pseudo column for the sql array param
      if (OB_FAIL(create_params_expr(group_param_expr, param_expr, i))) {
        LOG_WARN("fail to create group param expr", K(ret), K(param_expr));
      } else if (OB_FAIL(batch_stmt->get_query_ctx()->ab_param_exprs_.push_back(param_expr))) {
        LOG_WARN("add param expr to select list exprs failed", K(ret));
      } else if (OB_FAIL(group_param_exprs.push_back(group_param_expr))) {
        LOG_WARN("fail to push back group params", K(ret), K(group_param_expr));
      }
    }
  }

  // 2. deduce value_vector params
  if (OB_FAIL(ret)) {

  } else if (OB_FAIL(ObRawExprUtils::extract_params(insert_stmt->get_values_vector(), params))) {
    LOG_WARN("extract param expr from related exprs failed", K(ret));
  } else {
    common::ObIArray<ObRawExpr*> &value_vector = insert_stmt->get_values_vector();
    const ParamStore &param_store = plan_ctx->get_param_store();
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
      ObConstRawExpr *param_expr = static_cast<ObConstRawExpr *>(params.at(i));
      ObPseudoColumnRawExpr *group_id = NULL;
      int64_t param_idx = param_expr->get_value().get_unknown();
      if (param_idx < 0 || param_idx >= param_store.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param_idx is invalid", K(ret), K(param_idx), K(param_store));
      } else if (!param_store.at(param_idx).is_batch_parameters()) {
        // is not batch params
      } else {
        param_expr->set_is_batch_stmt_parameter();
      }
    }
    // Mark the expression in value_vector with a batch parameter flag.
    // The expression will be cleared eval_flag and re-operated
    // only when the subsequent values ​​operator iterates the parameters.
    for (int64_t i = 0; OB_SUCC(ret) && i < value_vector.count(); ++i) {
      if (OB_FAIL(value_vector.at(i)->formalize(session_info))) {
        LOG_WARN("formalize expr failed", K(ret), K(i), KPC(value_vector.at(i)));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!group_param_exprs.empty() &&
      OB_FAIL(batch_stmt->replace_relation_exprs(batch_stmt->get_query_ctx()->ab_param_exprs_, group_param_exprs))) {
    LOG_WARN("fail to replace relation exprs", K(ret), K(batch_stmt->get_query_ctx()->ab_param_exprs_), K(group_param_exprs));
  } else if (OB_FAIL(insert_stmt->get_group_param_exprs().assign(group_param_exprs))) {
    LOG_WARN("fail to assign group expr", K(ret));
  }

  return ret;
}

int ObTransformPreProcess::transform_for_ins_batch_stmt(ObDMLStmt *batch_stmt,
                                                        bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = NULL;
  ObExecContext *exec_ctx = nullptr;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObSQLSessionInfo *session_info = NULL;
  ObSEArray<ObRawExpr*, 4> params;
  ObPseudoColumnRawExpr *stmt_id_expr = NULL;

  if (OB_ISNULL(ctx_)
      || OB_ISNULL(exec_ctx = ctx_->exec_ctx_)
      || OB_ISNULL(plan_ctx = exec_ctx->get_physical_plan_ctx())
      || OB_ISNULL(session_info = ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ctx_), K(exec_ctx), K(plan_ctx), K(ret));
  } else if (OB_ISNULL(batch_stmt) ||
             OB_UNLIKELY(!batch_stmt->is_insert_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("batch_stmt or inner_view_stmt is null", K(ret), K(batch_stmt));
  } else if (FALSE_IT(insert_stmt = static_cast<ObInsertStmt*>(batch_stmt))) {
  } else if (OB_FAIL(create_stmt_id_expr(stmt_id_expr))) {
    LOG_WARN("fail to create stmt id expr", K(ret));
  } else if (FALSE_IT(batch_stmt->get_query_ctx()->ins_values_batch_opt_ = true)) {
  } else if (FALSE_IT(static_cast<ObDelUpdStmt*>(batch_stmt)->set_ab_stmt_id_expr(stmt_id_expr))) {
  } else if (OB_FAIL(ObRawExprUtils::extract_params(insert_stmt->get_values_vector(), params))) {
    LOG_WARN("extract param expr from related exprs failed", K(ret));
  } else {
    common::ObIArray<ObRawExpr*> &value_vector = insert_stmt->get_values_vector();
    const ParamStore &param_store = plan_ctx->get_param_store();
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
      ObConstRawExpr *param_expr = static_cast<ObConstRawExpr *>(params.at(i));
      ObPseudoColumnRawExpr *group_id = NULL;
      int64_t param_idx = param_expr->get_value().get_unknown();
      if (param_idx < 0 || param_idx >= param_store.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param_idx is invalid", K(ret), K(param_idx), K(param_store));
      } else if (!param_store.at(param_idx).is_batch_parameters()) {
        // Not batch parameter, no need to mark
      } else {
        param_expr->set_is_batch_stmt_parameter();
      }
    }
    // Marked all batch parameter expressions, need to re-derive the expressions
    // Here because of the special nature of insert values, so we only need to derive the expressions in value_vector
    for (int64_t i = 0; OB_SUCC(ret) && i < value_vector.count(); ++i) {
      if (OB_FAIL(value_vector.at(i)->formalize(session_info))) {
        LOG_WARN("formalize expr failed", K(ret), K(i), KPC(value_vector.at(i)));
      }
    }
  }

  return ret;
}

int ObTransformPreProcess::check_contain_param_expr(ObDMLStmt *stmt,
                                                    TableItem *table_item,
                                                    bool &contain_param)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  if (OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table item is null", K(ret));
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
    LOG_WARN("failed to check stack overflow", K(ret));
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too deep recursive", K(ret), K(is_stack_overflow));
  } else if (contain_param) {
    // break;
  } else {
    if (table_item->is_joined_table()) {
      JoinedTable *joined_table_item = static_cast<JoinedTable *>(table_item);
      if (OB_FAIL(ObRawExprUtils::is_contain_params(joined_table_item->get_join_conditions(), contain_param))) {
        LOG_WARN("fail to get releation exprs", K(ret));
      } else if (OB_FAIL(SMART_CALL(check_contain_param_expr(stmt, joined_table_item->left_table_, contain_param)))) {
        LOG_WARN("failed to collect temp table item", K(ret));
      } else if (OB_FAIL(SMART_CALL(check_contain_param_expr(stmt, joined_table_item->right_table_, contain_param)))) {
        LOG_WARN("failed to collect temp table item", K(ret));
      }
    } else if (table_item->is_generated_table()) {
      ObDMLStmt *ref_query_stmt = NULL;
      if (OB_ISNULL(ref_query_stmt = table_item->ref_query_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ref_query is null", K(ret), KPC(table_item));
      } else if (OB_FAIL(check_stmt_contain_param_expr(ref_query_stmt, contain_param))) {
        LOG_WARN("fail to check contain param expr", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_stmt_contain_param_expr(ObDMLStmt *stmt, bool &contain)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr*, 16> related_exprs;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret));
  } else if (OB_FAIL(stmt->get_relation_exprs(related_exprs))) {
    LOG_WARN("fail to get releation exprs", K(ret), KPC(stmt));
  } else if (OB_FAIL(ObRawExprUtils::is_contain_params(related_exprs, contain))) {
    LOG_WARN("fail to get releation exprs", K(ret));
  }
  return ret;
}

int ObTransformPreProcess::check_stmt_can_batch(ObDMLStmt *batch_stmt, bool &contain_param)
{
  int ret = OB_SUCCESS;
  contain_param = false;
  if (OB_ISNULL(batch_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("batch_stmt is null", K(ret));
  } else {
    const common::ObIArray<FromItem> &from_items = batch_stmt->get_from_items();
    for (int64_t i = 0; OB_SUCC(ret) && !contain_param && i < from_items.count(); ++i) {
      TableItem *table_item = NULL;
      const FromItem &from_item = from_items.at(i);
      if (OB_ISNULL(table_item = batch_stmt->get_table_item(from_item))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table_item is null", K(ret), K(from_item));
      } else if (OB_FAIL(check_contain_param_expr(batch_stmt, table_item, contain_param))) {
        LOG_WARN("fail to check contain param expr", K(ret), KPC(table_item));
      }
    }
  }

  return ret;
}

int ObTransformPreProcess::create_inner_view_stmt(ObDMLStmt *batch_stmt, ObSelectStmt*& inner_view_stmt)
{
  int ret = OB_SUCCESS;
  ObStmtFactory *stmt_factory = NULL;
  if (OB_ISNULL(batch_stmt) || OB_ISNULL(stmt_factory = ctx_->stmt_factory_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(batch_stmt), K(stmt_factory), K(ret));
  } else if (OB_FAIL(stmt_factory->create_stmt<ObSelectStmt>(inner_view_stmt))) {
    LOG_WARN("failed to create stmt", K(ret));
  } else {
    inner_view_stmt->set_stmt_type(stmt::T_SELECT);
    inner_view_stmt->set_query_ctx(batch_stmt->get_query_ctx());
    stmt::StmtType stmt_type = batch_stmt->get_stmt_type();
    if (OB_FAIL(inner_view_stmt->adjust_statement_id(ctx_->allocator_,
                                                     ctx_->src_qb_name_,
                                                     ctx_->src_hash_val_))) {
      LOG_WARN("failed to recursive adjust statement id", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_for_batch_stmt(ObDMLStmt *batch_stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObSelectStmt* inner_view_stmt = NULL;
  ObExecContext *exec_ctx = nullptr;
  stmt::StmtType stmt_type = batch_stmt->get_stmt_type();
  if (OB_ISNULL(ctx_) ||
      OB_ISNULL(ctx_->allocator_) ||
      OB_ISNULL(exec_ctx = ctx_->exec_ctx_) ||
      OB_ISNULL(batch_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ctx_), K(exec_ctx), K(ret));
  } else if (!exec_ctx->get_sql_ctx()->is_batch_params_execute()) {
    //rewrite only when stmt is batch multi statement
  } else if (!batch_stmt->is_dml_write_stmt()) {
    // Non-dml_stmt temporarily not processed
  } else if (stmt_type == stmt::T_UPDATE || stmt_type == stmt::T_DELETE) {
    int64_t child_size = 0;
    if (OB_FAIL(batch_stmt->get_child_stmt_size(child_size))) {
      LOG_WARN("fail to get child_stmt size", K(ret), KPC(batch_stmt));
    } else if (child_size != 0) {
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      LOG_TRACE("only DML Stmt without subquery supports batch execution, need to rollback",
                K(ret), K(child_size), KPC(batch_stmt));
    } else if (OB_FAIL(create_inner_view_stmt(batch_stmt, inner_view_stmt))) {
      LOG_WARN("fail to create inner_view_stmt", K(ret));
    } else if (OB_FAIL(transform_for_upd_del_batch_stmt(batch_stmt, inner_view_stmt, trans_happened))) {
      LOG_WARN("fail to transform upd or del batch stmt", K(ret));
    }
  } else if (stmt_type == stmt::T_INSERT || stmt_type == stmt::T_REPLACE) {
    // rewrite of insert
    int64_t child_size = 0;
    bool can_batch = false;
    ObInsertStmt *insert_stmt = static_cast<ObInsertStmt *>(batch_stmt);
    if (OB_FAIL(check_insert_can_batch(insert_stmt, can_batch))) {
      LOG_WARN("fail to check insert can batch", K(ret), KPC(insert_stmt));
    } else if (!can_batch) {
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      LOG_TRACE("can't support insert batch optimization", K(ret), KPC(batch_stmt));
    } else if (!insert_stmt->is_insert_up() &&
        OB_FAIL(transform_for_ins_batch_stmt(batch_stmt, trans_happened))) {
      LOG_WARN("fail to transform ins batch stmt", K(ret));
    } else if (insert_stmt->is_insert_up() &&
        OB_FAIL(transform_for_insertup_batch_stmt(batch_stmt, trans_happened))) {
      LOG_WARN("fail to transform insertup batch opt", K(ret));
    }
  }
  return ret;
}

bool ObTransformPreProcess::check_insertup_support_batch_opt(ObInsertStmt *insert_stmt, bool &can_batch)
{
  int ret = OB_SUCCESS;
  int64_t child_size = 0;
  ObSQLSessionInfo *session_info = NULL;
  if (OB_ISNULL(session_info = ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_ISNULL(insert_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (insert_stmt->is_insert_up() || insert_stmt->is_replace()) {
    if (OB_FAIL(insert_stmt->get_child_stmt_size(child_size))) {
      LOG_WARN("fail to get child_stmt size", K(ret), KPC(insert_stmt));
    } else if (child_size != 0) {
      // with subquery for insertup/replace/insert can't support batch_optimization
      can_batch = false;
      LOG_TRACE("insert with subquery supported batch exec opt", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::check_insert_can_batch(ObInsertStmt *insert_stmt, bool &can_batch)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = nullptr;
  bool is_multi_query = false; 
  can_batch = true;
  if (OB_ISNULL(ctx_) ||
      OB_ISNULL(exec_ctx = ctx_->exec_ctx_) ||
      OB_ISNULL(exec_ctx->get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ctx_), K(exec_ctx), K(ret));
  } else if (FALSE_IT(is_multi_query = exec_ctx->get_sql_ctx()->multi_stmt_item_.is_batched_multi_stmt())) {
    // do nothing
  } else if (insert_stmt->value_from_select()) {
    can_batch = false;
    LOG_TRACE("insert select stmt not supported batch exec opt", K(ret));
  } else if (!insert_stmt->is_insert_single_value()) {
    can_batch = false;
    LOG_TRACE("multi row insert not supported batch exec opt", K(ret));
  } else if (OB_FAIL(check_insertup_support_batch_opt(insert_stmt, can_batch))) {
    LOG_WARN("fail to get child_stmt size", K(ret), KPC(insert_stmt));
  } else if (!can_batch) {
    // with subquery for insertup/replace/insert can't support batch_optimization
    can_batch = false;
    LOG_TRACE("insert with subquery supported batch exec opt", K(ret));
  } else {
    common::ObIArray<ObRawExpr*> &value_vector = insert_stmt->get_values_vector();
    for (int64_t i = 0; OB_SUCC(ret) && i < value_vector.count(); i++) {
      if (value_vector.at(i)->has_flag(CNT_SUB_QUERY)) {
        can_batch = false;
      }
    }
  }
  LOG_TRACE("after check can support batch_optimization", K(ret), K(can_batch));
  return ret;
}

int ObTransformPreProcess::formalize_batch_stmt(ObDMLStmt *batch_stmt,
                                                ObSelectStmt* inner_view_stmt,
                                                const ObIArray<ObRawExpr *> &other_exprs,
                                                bool &trans_happened)
{
  int ret = OB_SUCCESS;
  TableItem *view_table_item = NULL;
  ObSEArray<ObRawExpr *, 4> view_columns;
  ObSQLSessionInfo *session_info = NULL;
  ObRawExpr *stmt_id_expr = NULL;
  if (OB_ISNULL(session_info = ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_FAIL(ObTransformUtils::add_new_table_item(ctx_,
                                                          batch_stmt,
                                                          inner_view_stmt,
                                                          view_table_item))) {
    //create generated table item for inner view
    LOG_WARN("failed to add new table item", K(ret));
  } else if (OB_ISNULL(view_table_item)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table item is null", K(ret));
  } else if (OB_FAIL(batch_stmt->add_from_item(view_table_item->table_id_, false))) {
    LOG_WARN("add from item to batch stmt failed", K(ret), KPC(view_table_item));
  } else if (OB_FAIL(ObTransformUtils::create_columns_for_view(ctx_,
                                                               *view_table_item,
                                                               batch_stmt,
                                                               view_columns))) {
    LOG_WARN("failed to create columns for view", K(ret));
  } else if (OB_ISNULL(stmt_id_expr = view_columns.at(view_columns.count() - 1))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt id expr is null", K(ret), K(stmt_id_expr));
  } else if (FALSE_IT(view_columns.pop_back())) {
    // do nothing
  } else if (OB_FAIL(batch_stmt->replace_relation_exprs(other_exprs, view_columns))) {
    LOG_WARN("replace inner stmt expr failed", K(ret));
  } else if (OB_FAIL(ObTransformUtils::adjust_pseudo_column_like_exprs(*batch_stmt))) {
    LOG_WARN("failed to adjust pseudo column like exprs", K(ret));
  } else if (OB_FAIL(batch_stmt->formalize_stmt(session_info))) {
    LOG_WARN("failed to formalize stmt", K(ret));
  } else {
    trans_happened = true;
    static_cast<ObDelUpdStmt*>(batch_stmt)->set_ab_stmt_id_expr(stmt_id_expr);
    LOG_DEBUG("debug transform_for_batch_stmt",
             K(batch_stmt->get_query_ctx()->ab_param_exprs_), K(view_columns));
  }
  return ret;
}

int ObTransformPreProcess::create_stmt_id_expr(ObPseudoColumnRawExpr *&stmt_id_expr)
{
  int ret = OB_SUCCESS;
  ObRawExprFactory *expr_factory = NULL;
  if (OB_ISNULL(expr_factory = ctx_->expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("assign value_vector failed", K(ret));
  } else if (OB_FAIL(expr_factory->create_raw_expr(T_PSEUDO_STMT_ID, stmt_id_expr))) {
    LOG_WARN("create pseudo column raw expr failed", K(ret));
  } else if (OB_ISNULL(stmt_id_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt id expr is null", K(ret));
  } else {
    stmt_id_expr->set_data_type(ObIntType);
    stmt_id_expr->set_accuracy(ObAccuracy::MAX_ACCURACY[ObIntType]);
    stmt_id_expr->set_table_id(OB_INVALID_ID);
    stmt_id_expr->set_explicited_reference();
    stmt_id_expr->set_expr_name(ObString::make_string("stmt_id"));
  }
  return ret;
}

int ObTransformPreProcess::create_params_expr(ObPseudoColumnRawExpr *&pseudo_param_expr,
                                              ObRawExpr *origin_param_expr,
                                              int64_t name_id)
{
  int ret = OB_SUCCESS;
  char *pseudo_name = nullptr;
  ObRawExprFactory *expr_factory = NULL;
  const int64_t buf_len = 64;
  int64_t pos = 0;
  if (OB_ISNULL(expr_factory = ctx_->expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("assign value_vector failed", K(ret));
  } else if (OB_ISNULL(pseudo_name = static_cast<char*>(ctx_->allocator_->alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate name buffer failed", K(ret));
  } else if (OB_FAIL(databuff_printf(pseudo_name, buf_len, pos,
                                     "group_param_%ld", name_id))) {
    LOG_WARN("databuff print column name failed", K(ret));
  } else if (OB_FAIL(expr_factory->create_raw_expr(T_PSEUDO_GROUP_PARAM, pseudo_param_expr))) {
    LOG_WARN("create pseudo colmn raw expr failed", K(ret));
  } else {
    pseudo_param_expr->set_expr_name(ObString(pos, pseudo_name));
    pseudo_param_expr->set_result_type(origin_param_expr->get_result_type());
    pseudo_param_expr->set_table_id(OB_INVALID_ID);
    pseudo_param_expr->set_explicited_reference();
  }
  return ret;
}


// 1、make value_vector as select_list,then extract_calculable_expr
// 2、after pre_calc, generate the mock select_list for inner_view


int ObTransformPreProcess::mock_select_list_for_upd_del(ObDMLStmt &batch_stmt,
                                                        ObSelectStmt &inner_view)
{
  int ret = OB_SUCCESS;
  const ParamStore *param_store = nullptr;
  ObExecContext *exec_ctx = nullptr;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObSEArray<ObRawExpr*, 4> params;
  ObSEArray<ObRawExpr*, 8> related_exprs;
  ObSEArray<ObRawExpr *, 4> select_list;

  if (OB_ISNULL(ctx_)
      || OB_ISNULL(exec_ctx = ctx_->exec_ctx_)
      || OB_ISNULL(plan_ctx = exec_ctx->get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ctx_), K(exec_ctx), K(plan_ctx), K(ret));
  } else if (FALSE_IT(param_store = &plan_ctx->get_param_store())) {
    // do nothing
  } else if (OB_FAIL(batch_stmt.get_relation_exprs(related_exprs))) {
    LOG_WARN("get relation exprs failed", K(ret));
  } else if (OB_FAIL(ObRawExprUtils::extract_params(related_exprs, params))) {
    LOG_WARN("extract param expr from related exprs failed", K(ret));
  } else { }

  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    ObConstRawExpr *param_expr = static_cast<ObConstRawExpr *>(params.at(i));
    int64_t param_idx = param_expr->get_value().get_unknown();
    if (param_idx < 0 || param_idx >= param_store->count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("param_idx is invalid", K(ret), K(param_idx), KPC(param_store));
    } else if (param_store->at(param_idx).is_batch_parameters()
        && !has_exist_in_array(batch_stmt.get_query_ctx()->ab_param_exprs_,
                               static_cast<ObRawExpr*>(param_expr))) {
      ObPseudoColumnRawExpr *group_param_expr = nullptr;
      //create pseudo column for the sql array param
      if (OB_FAIL(create_params_expr(group_param_expr, param_expr, select_list.count()))) {
        LOG_WARN("fail to create group param expr", K(ret), K(param_expr));
      } else if (OB_FAIL(batch_stmt.get_query_ctx()->ab_param_exprs_.push_back(param_expr))) {
        LOG_WARN("add param expr to select list exprs failed", K(ret));
      } else if (OB_FAIL(select_list.push_back(group_param_expr))) {
        LOG_WARN("store group id expr failed", K(ret));
      }
    }
  }
  //create stmt id expr with inner view
  if (OB_SUCC(ret)) {
    ObPseudoColumnRawExpr *stmt_id_expr = NULL;
    if (OB_FAIL(create_stmt_id_expr(stmt_id_expr))) {
      LOG_WARN("fail to create stmt id expr", K(ret));
    } else if (OB_FAIL(select_list.push_back(stmt_id_expr))) {
      LOG_WARN("store select id expr failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObTransformUtils::create_select_item(*ctx_->allocator_, select_list, &inner_view))) {
      LOG_WARN("failed to create select items", K(ret));
    } else if (!batch_stmt.get_query_ctx()->ab_param_exprs_.empty()) {
      inner_view.set_ab_param_flag(true);
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_full_outer_join(ObDMLStmt *&stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  ObSEArray<JoinedTable*, 4> joined_tables;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(stmt));
  } else if (OB_FAIL(joined_tables.assign(stmt->get_joined_tables()))) {
    LOG_WARN("failed to assign joined table", K(ret));
  } else {
    bool is_happened = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < joined_tables.count(); ++i) {
      if (OB_ISNULL(joined_tables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), K(i), K(joined_tables));
      } else if (OB_FAIL(recursively_eliminate_full_join(*stmt, joined_tables.at(i),
                                                         is_happened))) {
        LOG_WARN("failed to recursively eliminate full join", K(ret));
      } else {
        trans_happened |= is_happened;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::convert_join_preds_vector_to_scalar(JoinedTable &joined_table,
                                                               bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr*, 16> new_join_cond;
  bool is_happened = false;
  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get NULL ptr", K(ret) , KP(ctx_));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < joined_table.get_join_conditions().count(); i++) {
      if (OB_FAIL(ObTransformUtils::convert_preds_vector_to_scalar(*ctx_,
                                                           joined_table.get_join_conditions().at(i),
                                                           new_join_cond,
                                                           is_happened))) {
        LOG_WARN("failed to convert predicate vector to scalar", K(ret));
      }
    }
    if (OB_SUCC(ret) && is_happened) {
      trans_happened = true;
      joined_table.get_join_conditions().reset();
      if (OB_FAIL(append(joined_table.get_join_conditions(), new_join_cond))) {
        LOG_WARN("failed to append join conditions", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::recursively_eliminate_full_join(ObDMLStmt &stmt,
                                                           TableItem *table_item,
                                                           bool &trans_happened)
{
  int ret = OB_SUCCESS;
  bool has_euqal = false;
  bool has_subquery = false;
  JoinedTable *joined_table = static_cast<JoinedTable *>(table_item);
  TableItem *view_table = NULL;
  TableItem *from_table = table_item;
  if (OB_ISNULL(table_item) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table item is null", K(ret), K(table_item), K(ctx_));
  } else if (!table_item->is_joined_table()) {
    /* do nothing */
  } else if (OB_FAIL(SMART_CALL(recursively_eliminate_full_join(stmt,
                                                                joined_table->left_table_,
                                                                trans_happened)))) {
    LOG_WARN("failed to transform full nl join.", K(ret));
  } else if (OB_FAIL(SMART_CALL(recursively_eliminate_full_join(stmt,
                                                                joined_table->right_table_,
                                                                trans_happened)))) {
    LOG_WARN("failed to transform full nl join.", K(ret));
  } else if (!joined_table->is_full_join()) {
    /* do nothing */
  } else if (OB_FAIL(convert_join_preds_vector_to_scalar(*joined_table, trans_happened))) {
    LOG_WARN("failed to convert join preds vector to scalar", K(ret));
  } else if (OB_FAIL(check_join_condition(&stmt, joined_table, has_euqal, has_subquery))) {
    LOG_WARN("failed to check join condition", K(ret));
  } else if (has_euqal || has_subquery) {
    /* do nothing */
  } else if (OB_FAIL(ObTransformUtils::replace_with_empty_view(ctx_,
                                                               &stmt,
                                                               view_table,
                                                               from_table))) {
    LOG_WARN("failed to create empty view table", K(ret));
  } else if (OB_FAIL(ObTransformUtils::create_inline_view(ctx_,
                                                          &stmt,
                                                          view_table,
                                                          from_table))) {
    LOG_WARN("failed to create inline view", K(ret));
  } else if (OB_ISNULL(view_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("view table is null", K(ret), K(view_table));
  } else if (OB_FAIL(expand_full_outer_join(view_table->ref_query_))) {
    LOG_WARN("failed to create view for full nl join.", K(ret));
  } else {
    trans_happened = true;
    view_table->for_update_ = false;
  }
  return ret;
}

int ObTransformPreProcess::check_join_condition(ObDMLStmt *stmt,
                                                JoinedTable *table,
                                                bool &has_equal,
                                                bool &has_subquery)
{
  int ret = OB_SUCCESS;
  has_equal = false;
  has_subquery = false;
  ObSqlBitSet<> left_tables;
  ObSqlBitSet<> right_tables;
  if (OB_ISNULL(stmt) || OB_ISNULL(table) || OB_ISNULL(table->left_table_)
      || OB_ISNULL(table->right_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null param", K(ret));
  } else if (OB_FAIL(stmt->get_table_rel_ids(*table->left_table_, left_tables))) {
    LOG_WARN("failed to get table rel ids", K(ret));
  } else if (OB_FAIL(stmt->get_table_rel_ids(*table->right_table_, right_tables))) {
    LOG_WARN("failed to get table rel ids", K(ret));
  } else {
    ObRawExpr *cond = NULL;
    ObRawExpr *left_param = NULL;
    ObRawExpr *right_param = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < table->join_conditions_.count(); i++) {
      if (OB_ISNULL(cond = table->join_conditions_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null condition", K(ret));
      } else if (OB_FALSE_IT(has_subquery |= cond->has_flag(CNT_SUB_QUERY))) {
      } else if (cond->get_relation_ids().is_empty() || !cond->has_flag(IS_JOIN_COND)) {
        /* do nothing */
      } else if (OB_UNLIKELY(cond->get_param_count() != 2) ||
                OB_ISNULL(left_param = cond->get_param_expr(0)) ||
                OB_ISNULL(right_param = cond->get_param_expr(1))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null param", K(ret));
      } else if ((left_param->get_relation_ids().is_subset(left_tables) &&
                  right_param->get_relation_ids().is_subset(right_tables)) ||
                (right_param->get_relation_ids().is_subset(left_tables) &&
                  left_param->get_relation_ids().is_subset(right_tables))) {
        has_equal = true;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::expand_full_outer_join(ObSelectStmt *&ref_query)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *left_stmt = NULL;
  ObSelectStmt *right_stmt = NULL;
  ObSelectStmt *union_stmt = NULL;
  JoinedTable *joined_table = NULL;
  const int64_t sub_num = 1;
  if (OB_ISNULL(left_stmt = ref_query)
      || OB_UNLIKELY(left_stmt->get_joined_tables().count() != 1)
      || OB_ISNULL(joined_table = left_stmt->get_joined_tables().at(0))
      || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->stmt_factory_) || OB_ISNULL(ctx_->expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected joined table count.", K(ret), K(ctx_), K(left_stmt), K(joined_table));
  } else if (OB_FALSE_IT(joined_table->joined_type_ = LEFT_OUTER_JOIN)) {
  } else if (OB_FAIL(ctx_->stmt_factory_->create_stmt(right_stmt))) {
      LOG_WARN("failed to create select stmt", K(ret));
  } else if (OB_FAIL(SMART_CALL(right_stmt->deep_copy(*ctx_->stmt_factory_,
                                                      *ctx_->expr_factory_,
                                                      *left_stmt)))) {
      LOG_WARN("failed to deep copy select stmt", K(ret));
  } else if (OB_FAIL(right_stmt->recursive_adjust_statement_id(ctx_->allocator_,
                                                               ctx_->src_hash_val_,
                                                               sub_num))) {
    LOG_WARN("failed to recursive adjust statement id", K(ret));
  } else if (OB_FAIL(right_stmt->update_stmt_table_id(ctx_->allocator_, *left_stmt))) {
    LOG_WARN("failed to updatew table id in stmt.", K(ret));
  } else if (right_stmt->get_joined_tables().count() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected joined table count.", K(ret), K(right_stmt->get_joined_tables()));
  } else if (OB_FAIL(switch_left_outer_to_semi_join(right_stmt,
                                                    right_stmt->get_joined_tables().at(0),
                                                    right_stmt->get_select_items()))) {
    LOG_WARN("failed to switch join table to semi.", K(ret));
  } else if (OB_FAIL(ObTransformUtils::create_set_stmt(ctx_, ObSelectStmt::UNION, false, left_stmt,
                                                       right_stmt, union_stmt))) {
    LOG_WARN("failed to create union stmt.", K(ret));
  } else if (OB_ISNULL(union_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(union_stmt));
  } else {
    ref_query = union_stmt;
  }
  return ret;
}

int ObTransformPreProcess::switch_left_outer_to_semi_join(ObSelectStmt *&sub_stmt,
                                                          JoinedTable *joined_table,
                                                          const ObIArray<SelectItem> &select_items)
{
  int ret = OB_SUCCESS;
  SemiInfo *semi_info = NULL;
  ObSEArray<SelectItem, 4> output_select_items;
  ObSEArray<FromItem, 4> from_items;
  ObSEArray<SemiInfo *, 4> semi_infos;
  ObSEArray<JoinedTable *, 4> joined_tables;
  if (OB_ISNULL(joined_table) || OB_ISNULL(sub_stmt)
      || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null pointer.", K(ret));
  } else if (OB_ISNULL(joined_table->left_table_) || OB_ISNULL(joined_table->right_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null pointer.", K(ret));
  } else if (joined_table->left_table_->is_joined_table()) {
    TableItem *view_table = NULL;
    TableItem *push_table = joined_table->left_table_;
    if (OB_FAIL(ObTransformUtils::replace_with_empty_view(ctx_,
                                                          sub_stmt,
                                                          view_table,
                                                          push_table))) {
      LOG_WARN("failed to create empty view table", K(ret));
    } else if (OB_FAIL(ObTransformUtils::create_inline_view(ctx_,
                                                            sub_stmt,
                                                            view_table,
                                                            push_table))) {
      LOG_WARN("failed to create inline view with table", K(ret));
    } else {
      joined_table->left_table_ = view_table;
    }
  } 
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(semi_info = static_cast<SemiInfo*>(ctx_->allocator_->alloc(
                                                                    sizeof(SemiInfo))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to alloc semi info", K(ret));
  } else if (FALSE_IT(semi_info = new(semi_info)SemiInfo())) {
  } else if (OB_FAIL(semi_infos.push_back(semi_info))) {
    LOG_WARN("failed to push back semi info", K(ret));
  } else if (joined_table->right_table_->type_ == TableItem::JOINED_TABLE &&
      OB_FAIL(joined_tables.push_back(static_cast<JoinedTable*>(joined_table->right_table_)))) {
    LOG_WARN("failed push back joined table.", K(ret));
  } else if (OB_FAIL(create_select_items_for_semi_join(sub_stmt,
                                                       joined_table->left_table_,
                                                       select_items,
                                                       output_select_items))) {
    LOG_WARN("failed to assign to column itmes.", K(ret));
  } else if (OB_FAIL(sub_stmt->get_select_items().assign(output_select_items))) {
    LOG_WARN("failed to assign select items.", K(ret));
  } else if (OB_FAIL(semi_info->semi_conditions_.assign(joined_table->join_conditions_))) {
    LOG_WARN("failed to assign to condition exprs.", K(ret));
  } else {
    FromItem from_item;
    TableItem* right_table_item = NULL;
    from_item.is_joined_ = joined_table->right_table_->type_ == TableItem::JOINED_TABLE;
    from_item.table_id_ = joined_table->right_table_->table_id_;
    semi_info->join_type_ = LEFT_ANTI_JOIN;
    semi_info->right_table_id_ = joined_table->left_table_->table_id_;
    semi_info->semi_id_ = sub_stmt->get_query_ctx()->available_tb_id_--;
    if (OB_FAIL(from_items.push_back(from_item))) {
      LOG_WARN("failed to push back from item", K(ret));
    } else if (OB_ISNULL(right_table_item = sub_stmt->get_table_item_by_id(semi_info->right_table_id_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null pointer.", K(ret));
    } else if (OB_FALSE_IT(right_table_item->for_update_ = false)) {
    } else if (from_item.is_joined_) {
      JoinedTable *table = static_cast<JoinedTable*>(joined_table->right_table_);
      ret = semi_info->left_table_ids_.assign(table->single_table_ids_);
    } else {
      ret = semi_info->left_table_ids_.push_back(from_item.table_id_);
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(sub_stmt->get_joined_tables().assign(joined_tables))) {
    LOG_WARN("failed to assign join table.", K(ret));
  } else if (OB_FAIL(sub_stmt->get_semi_infos().assign(semi_infos))) {
    LOG_WARN("failed to assign semi infos.", K(ret));
  } else if (OB_FAIL(sub_stmt->get_from_items().assign(from_items))) {
    LOG_WARN("failed to assign from items.", K(ret));
  } else if (OB_FAIL(sub_stmt->formalize_stmt(ctx_->session_info_))) {
    LOG_WARN("failed to formalize stmt", K(ret));
  }
  return ret;
}

int ObTransformPreProcess::create_select_items_for_semi_join(ObDMLStmt *stmt,
                                                            TableItem *table_item,
                                                            const ObIArray<SelectItem> &select_items,
                                                            ObIArray<SelectItem> &output_select_items)
{
  int ret = OB_SUCCESS;
  ObSqlBitSet<> index_left;
  if (OB_ISNULL(stmt) || OB_ISNULL(table_item) ||
      OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null error.", K(ret));
  } else if (OB_FAIL(extract_idx_from_table_items(stmt,
                                                  table_item,
                                                  index_left))) {
    LOG_WARN("failed to extract idx from join table.", K(ret));
  } else if (OB_FAIL(output_select_items.assign(select_items))) {
    LOG_WARN("failed to assign select items", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < output_select_items.count(); i++) {
    if (output_select_items.at(i).expr_->get_relation_ids().overlap(index_left)) {
      ObRawExpr *null_expr = NULL;
      ObRawExpr *cast_expr = NULL;
      if (OB_FAIL(ObRawExprUtils::build_null_expr(*ctx_->expr_factory_,
                                                  null_expr))) {
        LOG_WARN("failed build null exprs.", K(ret));
      } else if (OB_ISNULL(null_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null expr", K(ret));
      } else if (OB_FALSE_IT(cast_expr = null_expr)) {
      } else if (OB_FAIL(ObTransformUtils::add_cast_for_replace_if_need(*ctx_->expr_factory_, 
                                                                        output_select_items.at(i).expr_, 
                                                                        cast_expr, 
                                                                        ctx_->session_info_))) {
        LOG_WARN("failed to add cast", K(ret));
      } else {
        output_select_items.at(i).expr_ = cast_expr;
      }
    } else { /*do nothing.*/ }
  }
  return ret;
}

int ObTransformPreProcess::extract_idx_from_table_items(ObDMLStmt *sub_stmt,
                                                        const TableItem *table_item,
                                                        ObSqlBitSet<> &rel_ids)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_item) || OB_ISNULL(sub_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null error.", K(ret));
  } else if (table_item->is_joined_table()) {
    const JoinedTable *joined_table = static_cast<const JoinedTable*>(table_item);
    if (OB_FAIL(SMART_CALL(extract_idx_from_table_items(sub_stmt,
                                             joined_table->left_table_,
                                             rel_ids)))) {
      LOG_WARN("failed to extract idx from join table.", K(ret));
    } else if (OB_FAIL(SMART_CALL(extract_idx_from_table_items(sub_stmt,
                                                    joined_table->right_table_,
                                                    rel_ids)))) {
      LOG_WARN("failed to extract idx from join table.", K(ret));
    } else {}
  } else {
    if (OB_FAIL(rel_ids.add_member(sub_stmt->get_table_bit_index(table_item->table_id_)))) {
      LOG_WARN("failed to add member to rel ids.", K(ret));
    } else {}
  }
  return ret;
}

int ObTransformPreProcess::transform_rollup_exprs(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  ObSelectStmt *sel_stmt = NULL;
  ObSEArray<ObRawExpr*, 4> static_const_exprs;
  ObSEArray<ObRawExpr*, 4> static_remove_const_exprs;
  ObSEArray<ObRawExpr*, 4> exec_param_exprs;
  ObSEArray<ObRawExpr*, 4> exec_param_remove_const_exprs;
  ObSEArray<ObRawExpr*, 4> column_ref_exprs;
  ObSEArray<ObRawExpr*, 4> column_ref_remove_const_exprs;
  ObSEArray<ObRawExpr*, 4> query_ref_exprs;
  ObSEArray<ObRawExpr*, 4> query_ref_remove_const_exprs;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null stmt", K(ret));
  } else if (!stmt->is_select_stmt()) {
    //do nothing
  } else if (OB_FALSE_IT(sel_stmt = static_cast<ObSelectStmt*>(stmt))) {
  } else if (!sel_stmt->has_rollup()) {
    //do nothing
  } else if (OB_FAIL(get_rollup_const_exprs(sel_stmt,
                                            static_const_exprs,
                                            static_remove_const_exprs,
                                            exec_param_exprs,
                                            exec_param_remove_const_exprs,
                                            column_ref_exprs,
                                            column_ref_remove_const_exprs,
                                            query_ref_exprs,
                                            query_ref_remove_const_exprs,
                                            trans_happened))) {
    LOG_WARN("failed to get rollup const exprs", K(ret));
  } else if (static_const_exprs.empty() && exec_param_exprs.empty() && query_ref_exprs.empty()) {
    //do nothing
  } else if (OB_FAIL(replace_remove_const_exprs(
                                        sel_stmt,
                                        static_const_exprs,
                                        static_remove_const_exprs,
                                        exec_param_exprs,
                                        exec_param_remove_const_exprs,
                                        column_ref_exprs,
                                        column_ref_remove_const_exprs,
                                        query_ref_exprs,
                                        query_ref_remove_const_exprs))) {
    LOG_WARN("failed to replace remove const exprs", K(ret));
  }
  return ret;
}
 int ObTransformPreProcess::get_rollup_const_exprs(ObSelectStmt *stmt,
                                                  ObIArray<ObRawExpr*> &static_const_exprs,
                                                  ObIArray<ObRawExpr*> &static_remove_const_exprs,
                                                  ObIArray<ObRawExpr*> &exec_param_exprs,
                                                  ObIArray<ObRawExpr*> &exec_params_remove_const_exprs,
                                                  ObIArray<ObRawExpr*> &column_ref_exprs,
                                                  ObIArray<ObRawExpr*> &column_ref_remove_const_exprs,
                                                  ObIArray<ObRawExpr*> &query_ref_exprs,
                                                  ObIArray<ObRawExpr*> &query_ref_remove_const_exprs,
                                                  bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObSEArray<std::pair<int64_t, ObRawExpr *>, 2> dummy_onetime_exprs;
  ObRawExpr *remove_const_expr = NULL;
  bool is_const = false;
  trans_happened = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) ||
      OB_ISNULL(ctx_->expr_factory_) ||
      OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null", K(ret));
  }
  for (int i = 0; OB_SUCC(ret) && i < stmt->get_rollup_expr_size(); ++i) {
    ObRawExpr *expr = stmt->get_rollup_exprs().at(i);
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpect null expr", K(ret));
    } else if (expr->has_flag(CNT_VOLATILE_CONST) || (!expr->is_const_expr() && !expr->is_query_ref_expr())) {
      //do nothing
    } else if (expr->is_query_ref_expr()) {
      // In mysql mode, rollup expr can reference subquery which may be transformed to a static const or onetime expr
      // e.g. select (select 1) as field1, sum(id) from t1 GROUP BY field1 with rollup;
      //      select (select count(1) from t1) as field1, sum(id) from t1 GROUP BY field1 with rollup;
      if (ObOptimizerUtil::find_item(query_ref_exprs, expr)) {
        //do nothing, skip dup exprs
      } else if (OB_FAIL(ObRawExprUtils::build_remove_const_expr(
                                        *ctx_->expr_factory_,
                                        *ctx_->session_info_,
                                        expr,
                                        remove_const_expr))) {
        LOG_WARN("failed to build remove const expr", K(ret));
      } else if (OB_ISNULL(remove_const_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null expr", K(ret));
      } else if (OB_FAIL(query_ref_exprs.push_back(expr))) {
        LOG_WARN("failed to push back expr", K(ret));
      } else if (OB_FAIL(query_ref_remove_const_exprs.push_back(remove_const_expr))) {
        LOG_WARN("failed to push back expr", K(ret));
      } else {
        stmt->get_rollup_exprs().at(i) = remove_const_expr;
        trans_happened = true;
      }
    } else if (expr->is_static_const_expr()) { //static const expr
      if (ObOptimizerUtil::find_item(static_const_exprs, expr)) {
        //do nothing, skip dup exprs
      } else if (OB_FAIL(ObRawExprUtils::build_remove_const_expr(
                                        *ctx_->expr_factory_,
                                        *ctx_->session_info_,
                                        expr,
                                        remove_const_expr))) {
        LOG_WARN("failed to build remove const expr", K(ret));
      } else if (OB_ISNULL(remove_const_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null expr", K(ret));
      } else if (OB_FAIL(static_const_exprs.push_back(expr))) {
        LOG_WARN("failed to push back expr", K(ret));
      } else if (OB_FAIL(static_remove_const_exprs.push_back(remove_const_expr))) {
        LOG_WARN("failed to push back expr", K(ret));
      } else {
        stmt->get_rollup_exprs().at(i) = remove_const_expr;
        trans_happened = true;
      }
    }  else if (ObOptimizerUtil::find_item(exec_param_exprs, expr)) {
      //do nothing, skip dup exprs
    } else if (OB_FAIL(ObRawExprUtils::build_remove_const_expr(
                                      *ctx_->expr_factory_,
                                      *ctx_->session_info_,
                                      expr,
                                      remove_const_expr))) {
      LOG_WARN("failed to build remove const expr", K(ret));
    } else if (OB_ISNULL(remove_const_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpect null expr", K(ret));
    } else {
      stmt->get_rollup_exprs().at(i) = remove_const_expr;
      trans_happened = true;
      if (lib::is_mysql_mode() && expr->is_exec_param_expr()) {
        ObExecParamRawExpr *exec_expr = static_cast<ObExecParamRawExpr *>(expr);
        const ObRawExpr *ref_expr = exec_expr->get_ref_expr();
        if (OB_ISNULL(ref_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        } else if (ref_expr->is_column_ref_expr()) {
          if (OB_FAIL(column_ref_exprs.push_back(expr))) {
            LOG_WARN("failed to push back expr", K(ret));
          } else if (OB_FAIL(column_ref_remove_const_exprs.push_back(remove_const_expr))) {
            LOG_WARN("failed to push back expr", K(ret));
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(exec_param_exprs.push_back(expr))) {
          LOG_WARN("failed to push back expr", K(ret));
        } else if (OB_FAIL(exec_params_remove_const_exprs.push_back(remove_const_expr))) {
          LOG_WARN("failed to push back expr", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::replace_remove_const_exprs(ObSelectStmt *stmt,
                                                      ObIArray<ObRawExpr*> &static_const_exprs,
                                                      ObIArray<ObRawExpr*> &static_remove_const_exprs,
                                                      ObIArray<ObRawExpr*> &exec_params,
                                                      ObIArray<ObRawExpr*> &exec_params_remove_const_exprs,
                                                      ObIArray<ObRawExpr*> &column_ref_exprs,
                                                      ObIArray<ObRawExpr*> &column_ref_remove_const_exprs,
                                                      ObIArray<ObRawExpr*> &query_ref_exprs,
                                                      ObIArray<ObRawExpr*> &query_ref_remove_const_exprs)
{
  int ret = OB_SUCCESS;
  ObQueryCtx *query_ctx = NULL;
  ObStmtCompareContext compare_ctx;
  if (OB_ISNULL(stmt) || OB_ISNULL(query_ctx = stmt->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null", K(ret), K(stmt), K(query_ctx));
  } else if (static_const_exprs.count() != static_remove_const_exprs.count() ||
             exec_params.count() != exec_params_remove_const_exprs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect expr size", K(ret));
  } else if (OB_FALSE_IT(compare_ctx.init(&query_ctx->calculable_items_))) {
    LOG_WARN("failed to init compare context", K(ret));
  } else {
    ObStmtExprReplacer replacer;
    replacer.remove_all();
    replacer.add_scope(SCOPE_HAVING);
    replacer.add_scope(SCOPE_SELECT);
    replacer.add_scope(SCOPE_ORDERBY);
    replacer.set_recursive(false);
    replacer.set_skip_bool_param_mysql(true);
    if (OB_FAIL(replacer.add_replace_exprs(static_const_exprs, static_remove_const_exprs))) {
      LOG_WARN("failed to add replace exprs", K(ret));
    } else if (OB_FAIL(replacer.add_replace_exprs(column_ref_exprs, column_ref_remove_const_exprs))) {
      LOG_WARN("failed to add replace exprs", K(ret));
    } else if (OB_FAIL(replacer.add_replace_exprs(query_ref_exprs, query_ref_remove_const_exprs))) {
      LOG_WARN("failed to add replace exprs", K(ret));
    } else if (OB_FAIL(stmt->iterate_stmt_expr(replacer))) {
      LOG_WARN("failed to iterate stmt expr", K(ret));
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_aggr_item_size(); ++i) {
    ObAggFunRawExpr *agg_expr = NULL;
    if (OB_ISNULL(agg_expr =stmt->get_aggr_item(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret));
    } else if (T_FUN_GROUPING == agg_expr->get_expr_type() || T_FUN_GROUPING_ID == agg_expr->get_expr_type()) {
      for (int64_t j = 0; j < agg_expr->get_param_count(); ++j) {
        bool replaced = false;
        int64_t pos = OB_INVALID_ID;
        if (OB_ISNULL(agg_expr->get_param_expr(j))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        } else if (!agg_expr->get_param_expr(j)->is_const_expr()) {
        } else if (ObOptimizerUtil::find_item(static_const_exprs, agg_expr->get_param_expr(j), &pos)) {
          if (OB_UNLIKELY(pos < 0 || pos >= static_remove_const_exprs.count())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected array pos", K(ret), K(pos), K(static_remove_const_exprs.count()));
          } else {
            agg_expr->get_param_expr(j) = static_remove_const_exprs.at(pos);
          }
        } else if (ObOptimizerUtil::find_item(exec_params, agg_expr->get_param_expr(j), &pos)) {
          if (OB_UNLIKELY(pos < 0 || pos >= exec_params_remove_const_exprs.count())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected array pos", K(ret), K(pos), K(exec_params_remove_const_exprs.count()));
          } else {
            agg_expr->get_param_expr(j) = exec_params_remove_const_exprs.at(pos);
          }
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_cast_multiset_for_stmt(ObDMLStmt *&stmt,
                                                            bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)
      || OB_ISNULL(ctx_->expr_factory_) || OB_ISNULL(ctx_->session_info_)
      || OB_ISNULL(ctx_->allocator_) || OB_ISNULL(ctx_->schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt or context is null", K(ret));
  } else if (stmt->get_subquery_exprs().empty()) {
    // do nothing
  } else {
    ObArray<ObRawExprPointer> relation_exprs;
    ObStmtExprGetter getter;
    if (OB_FAIL(stmt->get_relation_exprs(relation_exprs, getter))) {
      LOG_WARN("failed to get all relation exprs", K(ret));
    } else {
      for (int i = 0; OB_SUCC(ret) && i < relation_exprs.count(); i++) {
        bool is_happened = false;
        ObRawExpr *expr = NULL;
        if (OB_FAIL(relation_exprs.at(i).get(expr))) {
          LOG_WARN("failed to get expr", K(ret));
        } else if (OB_ISNULL(expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("expr is NULL", K(ret));
        } else if (OB_FAIL(transform_cast_multiset_for_expr(*stmt,
                                                            expr,
                                                            is_happened))) {
          LOG_WARN("transform expr failed", K(ret));
        } else if (OB_FAIL(relation_exprs.at(i).set(expr))) {
          LOG_WARN("failed to set expr", K(ret));
        } else {
          trans_happened |= is_happened;
        }
      }
    }
    if (OB_SUCC(ret) && trans_happened &&
        OB_FAIL(stmt->formalize_stmt(ctx_->session_info_))) {
      LOG_WARN("failed to formalize", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::transform_cast_multiset_for_expr(ObDMLStmt &stmt,
                                                            ObRawExpr *&expr,
                                                            bool &trans_happened)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr) || 
      OB_UNLIKELY(T_FUN_SYS_CAST == expr->get_expr_type() &&
                  (expr->get_param_count() != 2 ||
                   expr->get_param_expr(0) == NULL))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected param", K(ret), KPC(expr));
  } else if (T_FUN_SYS_CAST == expr->get_expr_type() &&
             expr->get_param_expr(0)->is_multiset_expr()) {
    ObQueryRefRawExpr *subquery_expr = static_cast<ObQueryRefRawExpr *>(expr->get_param_expr(0));
    ObConstRawExpr *const_expr = static_cast<ObConstRawExpr *>(expr->get_param_expr(1));
    uint64_t udt_id = OB_INVALID_ID;
    if (OB_ISNULL(const_expr) || OB_ISNULL(subquery_expr->get_ref_stmt()) || 
       OB_UNLIKELY(!const_expr->is_const_raw_expr()) ||
       OB_UNLIKELY(OB_INVALID_ID == (udt_id = const_expr->get_udt_id()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected params", K(ret), KPC(expr));
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      OZ (SMART_CALL(transform_cast_multiset_for_expr(stmt,
                                                      expr->get_param_expr(i),
                                                      trans_happened)));
    }
  }
  return ret;
}

int ObTransformPreProcess::add_constructor_to_multiset(ObDMLStmt &stmt,
                                                       ObQueryRefRawExpr *multiset_expr,
                                                       const pl::ObPLDataType &elem_type,
                                                       bool& trans_happened)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObTransformPreProcess::add_column_conv_to_multiset(ObQueryRefRawExpr *multiset_expr,
                                                       const pl::ObPLDataType &elem_type,
                                                       bool& trans_happened)
{
  int ret = OB_SUCCESS;
  const ObDataType *data_type = NULL;
  ObSelectStmt *multiset_stmt = multiset_expr->get_ref_stmt();
  ObSelectStmt *new_stmt = NULL;
  if (OB_ISNULL(data_type = elem_type.get_data_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null element data type", K(ret), K(elem_type));
  } else if (OB_UNLIKELY(multiset_stmt->get_select_item_size() != 1) ||
            OB_UNLIKELY(multiset_expr->get_column_types().count() != 1)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("unexpected column count", K(ret), KPC(multiset_stmt), KPC(multiset_expr));
  } else {
    if (!multiset_stmt->is_set_stmt() && !multiset_stmt->is_distinct()) {
      // do nothing
      // if multiset stmt is set stmt, create a view for it.
    } else if (OB_FAIL(ObTransformUtils::create_stmt_with_generated_table(ctx_, multiset_stmt, new_stmt))) {
      LOG_WARN("failed to create dummy view", K(ret));
    } else if (OB_ISNULL(new_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else {
      multiset_stmt = new_stmt;
      multiset_expr->set_ref_stmt(multiset_stmt);
    }
    if (OB_SUCC(ret)) {
      SelectItem &select_item = multiset_stmt->get_select_item(0);
      ObRawExpr *child = select_item.expr_;
      if (OB_FAIL(ObRawExprUtils::build_column_conv_expr(ctx_->session_info_,
                                                         *ctx_->expr_factory_,
                                                         data_type->get_obj_type(),
                                                         data_type->get_collation_type(),
                                                         data_type->get_accuracy_value(),
                                                         true,
                                                         NULL,
                                                         NULL,
                                                         child,
                                                         true))) {
        LOG_WARN("failed to build column conv expr", K(ret));
      } else {
        select_item.expr_ = child;
        multiset_expr->get_column_types().at(0) = child->get_result_type();
        trans_happened = true;
      }
    }
  } 
  return ret;
}

int ObTransformPreProcess::transform_outerjoin_exprs(ObDMLStmt *stmt, bool &is_happened)
{
  int ret = OB_SUCCESS;
  int64_t set_size = 32;
  ObStmtExprGetter visitor;
  visitor.set_relation_scope();
  visitor.remove_scope(DmlStmtScope::SCOPE_JOINED_TABLE);
  ObArray<ObRawExpr *> relation_exprs;
  hash::ObHashSet<uint64_t, hash::NoPthreadDefendMode> expr_set;
  bool bypass = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret), K(stmt));
  } else if (stmt->get_joined_tables().empty()) {
    bypass = true;
    OPT_TRACE("[BYPASS] transform outerjoin exprs");
  } else if (OB_FAIL(stmt->get_relation_exprs(relation_exprs, visitor))) {
    LOG_WARN("failed to get relation exprs", K(ret));
  } else if (set_size < relation_exprs.count()) {
    set_size = relation_exprs.count();
  }
  if (OB_SUCC(ret) && !bypass) {
    if (OB_FAIL(expr_set.create(set_size, "RewriteExpr", "RewriteExpr"))) {
      LOG_WARN("failed to create expr set", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < relation_exprs.count(); ++i) {
      if (OB_FAIL(ObTransformUtils::append_hashset(relation_exprs.at(i), expr_set))) {
        LOG_WARN("failed to append hashset", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_from_item_size(); ++i) {
      JoinedTable *joined_table = NULL;
      if (!stmt->get_from_item(i).is_joined_) {
        // do nothing
      } else if (OB_ISNULL(joined_table = stmt->get_joined_table(stmt->get_from_item(i).table_id_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("joined table is null", K(ret), K(joined_table));
      } else if (OB_FAIL(remove_shared_expr(stmt, joined_table, expr_set, false))) {
        LOG_WARN("failed to remove shared expr", K(ret));
      }
    }
    if (expr_set.created()) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = expr_set.destroy())) {
        LOG_WARN("failed to destroy expr set", K(ret));
        ret = COVER_SUCC(tmp_ret);
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::remove_shared_expr(ObDMLStmt *stmt,
                                              JoinedTable *joined_table,
                                              hash::ObHashSet<uint64_t, hash::NoPthreadDefendMode> &expr_set,
                                              bool is_nullside)
{
  int ret = OB_SUCCESS;
  TableItem *left = NULL;
  TableItem *right = NULL;
  TableItem *nullside_table = NULL;
  ObArray<ObRawExpr *> padnull_exprs;
  if (OB_ISNULL(joined_table) ||
      OB_ISNULL(left = joined_table->left_table_) ||
      OB_ISNULL(right = joined_table->right_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("joined table is null", K(ret), K(joined_table));
  } else if (joined_table->is_full_join()) {
    nullside_table = joined_table;
  } else if (joined_table->is_left_join()) {
    nullside_table = right;
  } else if (joined_table->is_right_join()) {
    nullside_table = left;
  }
  if (OB_SUCC(ret) && NULL != nullside_table) {
    ObArray<uint64_t> table_ids;
    if (nullside_table->is_joined_table()) {
      if (OB_FAIL(append(table_ids, static_cast<JoinedTable *>(nullside_table)->single_table_ids_))) {
        LOG_WARN("failed to append single table ids", K(ret));
      }
    } else if (OB_FAIL(table_ids.push_back(nullside_table->table_id_))) {
      LOG_WARN("failed to push back table id", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < table_ids.count(); ++i) {
      if (OB_FAIL(stmt->get_column_exprs(table_ids.at(i), padnull_exprs))) {
        LOG_WARN("failed to get column exprs", K(ret));
      }
    }
  }
  if (!padnull_exprs.empty() || is_nullside) {
    for (int64_t i = 0; OB_SUCC(ret) && i < joined_table->join_conditions_.count(); ++i) {
      bool has = false;
      if (OB_FAIL(do_remove_shared_expr(expr_set,
                                        padnull_exprs,
                                        is_nullside,
                                        joined_table->join_conditions_.at(i),
                                        has))) {
        LOG_WARN("failed to remove shared expr", K(ret));
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < joined_table->join_conditions_.count(); ++i) {
    if (OB_FAIL(ObTransformUtils::append_hashset(joined_table->join_conditions_.at(i),
                                                          expr_set))) {
      LOG_WARN("failed to append expr into hashset", K(ret));
    }
  }
  if (OB_SUCC(ret) && left->is_joined_table()) {
    bool left_is_nullside = is_nullside || (joined_table->is_right_join() ||
                                            joined_table->is_full_join());
    if (OB_FAIL(SMART_CALL(remove_shared_expr(stmt,
                                              static_cast<JoinedTable *>(left),
                                              expr_set,
                                              left_is_nullside)))) {
      LOG_WARN("failed to remove shared expr", K(ret));
    }
  }
  if (OB_SUCC(ret) && right->is_joined_table()) {
    bool right_is_nullside = is_nullside || (joined_table->is_left_join() ||
                                             joined_table->is_full_join());
    if (OB_FAIL(SMART_CALL(remove_shared_expr(stmt,
                                              static_cast<JoinedTable *>(right),
                                              expr_set,
                                              right_is_nullside)))) {
      LOG_WARN("failed to remove shared expr", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::do_remove_shared_expr(hash::ObHashSet<uint64_t, hash::NoPthreadDefendMode> &expr_set,
                                                 ObIArray<ObRawExpr *> &padnull_exprs,
                                                 bool is_nullside,
                                                 ObRawExpr *&expr,
                                                 bool &has_padnull_column) 
{
  int ret = OB_SUCCESS;
  bool need_copy = false;
  uint64_t key = reinterpret_cast<uint64_t>(expr);
  has_padnull_column = false;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is null", K(ret));
  } else if (expr->is_column_ref_expr()) {
    has_padnull_column = ObOptimizerUtil::find_item(padnull_exprs, expr);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
    bool has = false;
    if (OB_FAIL(SMART_CALL(do_remove_shared_expr(expr_set, 
                                                 padnull_exprs, 
                                                 is_nullside,
                                                 expr->get_param_expr(i),
                                                 has)))) {
      LOG_WARN("failed to remove shared expr", K(ret));
    } else if (has) {
      has_padnull_column = true;
    }
  }
  if (OB_SUCC(ret) &&
      OB_HASH_EXIST == expr_set.exist_refactored(key) && 
      !expr->is_column_ref_expr() && 
      !expr->is_query_ref_expr() &&
      !expr->is_const_raw_expr() && 
      !expr->is_exec_param_expr() &&
      !expr->is_pseudo_column_expr()) {
    bool bret = false;
    ObRawExpr *new_expr = NULL;
    if (OB_ISNULL(ctx_) || OB_ISNULL(ctx_->expr_factory_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("params have null", K(ret));
    } else if (padnull_exprs.empty() || !has_padnull_column) {
      // do nothing
    } else if (OB_FAIL(ObTransformUtils::is_null_propagate_expr(expr, padnull_exprs, bret))) {
      LOG_WARN("failed to check is null propogate expr", K(ret));
    } else if (!bret) {
      need_copy = true;
    }
    if (OB_SUCC(ret) && !need_copy && is_nullside) {
      if (OB_FAIL(check_nullside_expr(expr, bret))) {
        LOG_WARN("failed to check nullside expr", K(ret));
      } else if (!bret) {
        need_copy = true;
      }
    }
    if (OB_SUCC(ret) && need_copy) {
      if (OB_FAIL(ObRawExprCopier::copy_expr_node(*ctx_->expr_factory_,
                                                  expr,
                                                  new_expr))) {
        LOG_WARN("failed to copy expr node", K(ret));
      } else {
        expr = new_expr;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_nullside_expr(ObRawExpr *expr, bool &bret)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr *> column_exprs;
  bret = false;
  if (OB_FAIL(ObRawExprUtils::extract_column_exprs(expr, column_exprs))) {
    LOG_WARN("failed to extract column exprs", K(ret));
  } else if (OB_FAIL(ObTransformUtils::is_null_propagate_expr(expr, column_exprs, bret))) {
    LOG_WARN("failed to check is null propagate expr", K(ret));
  }
  return ret;
}


int ObTransformPreProcess::transform_for_last_insert_id(ObDMLStmt *stmt, bool &trans_happened) {
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(stmt));
  } else if (stmt->is_select_stmt()) {
    ObSelectStmt *sel_stmt = static_cast<ObSelectStmt*>(stmt);
    bool is_happened = false;
    if (OB_FAIL(expand_for_last_insert_id(*stmt, sel_stmt->get_having_exprs(), is_happened))) {
      LOG_WARN("fail to expand having exprs",K(ret));
    } else {
      trans_happened |= is_happened;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(expand_for_last_insert_id(*stmt, sel_stmt->get_condition_exprs(), is_happened))) {
      LOG_WARN("fail to expand condition exprs",K(ret));
    } else {
      trans_happened |= is_happened;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_stmt->get_joined_tables().count(); ++i) {
      if (OB_FAIL(expand_last_insert_id_for_join(*stmt, sel_stmt->get_joined_tables().at(i), is_happened))) {
        LOG_WARN("failed to expand join conditions", K(ret));
      } else {
        trans_happened |= is_happened;
      }
    }
  } else if (stmt->is_delete_stmt() || stmt->is_update_stmt()) {
    bool is_happened = false;
    if (OB_FAIL(expand_for_last_insert_id(*stmt, stmt->get_condition_exprs(), is_happened))) {
      LOG_WARN("fail to expand having exprs",K(ret));
    } else {
      trans_happened |= is_happened;
    }
  }
  return ret;
}

int ObTransformPreProcess::expand_last_insert_id_for_join(ObDMLStmt &stmt, JoinedTable *join_table, bool &has_happened) {
  int ret = OB_SUCCESS;
  bool is_happened = false;
  has_happened = false;
  if (OB_ISNULL(join_table) || OB_ISNULL(join_table->left_table_) || OB_ISNULL(join_table->right_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(join_table));
  } else if (OB_FAIL(expand_for_last_insert_id(stmt, join_table->join_conditions_, has_happened))) {
    LOG_WARN("failed to expand join conditions", K(ret));
  } else if (join_table->left_table_->is_joined_table() &&
            OB_FAIL(SMART_CALL(expand_last_insert_id_for_join(
                                   stmt, static_cast<JoinedTable*>(join_table->left_table_), is_happened)))) {
    LOG_WARN("fail to expand last_insert_id in left join table", K(ret));
  } else if (FALSE_IT(has_happened |= is_happened)) {
  } else if (join_table->right_table_->is_joined_table() &&
            OB_FAIL(SMART_CALL(expand_last_insert_id_for_join(
                                   stmt, static_cast<JoinedTable*>(join_table->right_table_), is_happened)))) {
    LOG_WARN("fail to expand last_insert_id in right join table", K(ret));
  } else {
    has_happened |= is_happened;
  }
  return ret;
}

int ObTransformPreProcess::expand_for_last_insert_id(ObDMLStmt &stmt, ObIArray<ObRawExpr*> &exprs, bool &is_happended) {
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr *, 4> new_exprs;
  is_happended = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); ++i) {
    ObRawExpr *expr = exprs.at(i);
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret), K(expr), K(i));
    } else if (expr->has_flag(CNT_LAST_INSERT_ID) &&
          (IS_RANGE_CMP_OP(expr->get_expr_type()) || T_OP_EQ == expr->get_expr_type()) &&
          !expr->has_flag(CNT_RAND_FUNC) &&
          !expr->has_flag(CNT_SUB_QUERY) &&
          !expr->has_flag(CNT_SEQ_EXPR) &&
          !expr->has_flag(CNT_DYNAMIC_USER_VARIABLE)) {
      bool removable = false;
      ObRawExpr *left = expr->get_param_expr(0);
      ObRawExpr *right = expr->get_param_expr(1);
      ObRawExpr *check_expr = NULL;
      if (OB_ISNULL(left) || OB_ISNULL(right)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), K(left), K(right));
      } else if (left->has_flag(CNT_LAST_INSERT_ID) && !left->has_flag(CNT_COLUMN) && right->has_flag(CNT_COLUMN)) {
        check_expr = right;
      } else if (right->has_flag(CNT_LAST_INSERT_ID) && !right->has_flag(CNT_COLUMN) && left->has_flag(CNT_COLUMN)) {
        check_expr = left;
      }
      if (OB_FAIL(ret) || NULL == check_expr) {
        //do nothing
      } else if (OB_FAIL(ObTransformUtils::check_is_index_part_key(*ctx_, stmt, check_expr, removable))) {
        LOG_WARN("fail to check if it's a index/part condition", K(ret));
      } else if (!removable) {
        //do nothing if the param which does not contain last_insert_id is not a index key with lossless cast or a index key.
      } else if (OB_FAIL(check_last_insert_id_removable(expr, removable))) {
        LOG_WARN("fail to check whether last_insert_id can be removed", K(ret));
      } else if (removable) {
        ObRawExpr *param_expr = check_expr == left ? right : left;
        ObRawExpr *new_expr = NULL;
        if (OB_FAIL(ObRawExprCopier::copy_expr(
                            *ctx_->expr_factory_,
                            param_expr,
                            param_expr))) {
          LOG_WARN("failed to copy expr", K(ret));
        } else if (OB_ISNULL(param_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("new param is invalid", K(ret));
        } else if (OB_FAIL(remove_last_insert_id(param_expr))) {
          LOG_WARN("failed to remove last insert id exprs", K(ret));
        } else if (OB_FAIL(param_expr->formalize(ctx_->session_info_))) {
          LOG_WARN("failed to formalize expr", K(ret));
        } else if (!param_expr->is_const_expr()) {
          //do nothing
        } else if (OB_FAIL(ObRawExprCopier::copy_expr_node(*ctx_->expr_factory_,
                                                  expr,
                                                  new_expr))) {
          LOG_WARN("failed to copy expr", K(ret));
        } else if (OB_ISNULL(new_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to build new expr", K(ret));
        } else if (new_expr->get_param_expr(0) == check_expr) {
          new_expr->get_param_expr(1) = param_expr;
        } else {
          new_expr->get_param_expr(0) = param_expr;
        }
        if (OB_SUCC(ret) && NULL != new_expr) {
          if (OB_FAIL(new_expr->formalize(ctx_->session_info_))) {
            LOG_WARN("failed to formalize expr", K(ret));
          } else if (OB_FAIL(exprs.push_back(new_expr))) {
            LOG_WARN("failed to push back new pred", K(ret));
          } else {
            is_happended = true;
          }
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_last_insert_id_removable(const ObRawExpr *expr, bool &is_removable) {
  int ret = OB_SUCCESS;
  is_removable = true;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(expr));
  } else if (expr->has_flag(IS_LAST_INSERT_ID)) {
    if (1 != expr->get_param_count()) {
      is_removable = false;
    } else {
      const ObRawExpr *param = expr->get_param_expr(0);
      if (OB_ISNULL(param)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), K(param));
      } else if (!param->has_flag(IS_CONST) && !param->has_flag(IS_CONST_EXPR)) {
        is_removable = false;
      }
    }
  } else if (expr->has_flag(CNT_LAST_INSERT_ID)) {
    bool flag = true;
    for (int64_t i = 0; OB_SUCC(ret) && is_removable && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(check_last_insert_id_removable(expr->get_param_expr(i), flag))) {
        LOG_WARN("fail to check whether last insert id expr is removable", K(ret));
      } else {
        is_removable = is_removable & flag;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::remove_last_insert_id(ObRawExpr *&expr) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (expr->has_flag(IS_LAST_INSERT_ID)) {
    if (1 == expr->get_param_count()) {
      ObRawExpr *new_expr = expr->get_param_expr(0);
      if (OB_ISNULL(new_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), K(new_expr));
      } else if (new_expr->has_flag(IS_CONST) || new_expr->has_flag(IS_CONST_EXPR)) {
        expr = new_expr;
      }
    }
  } else if (expr->has_flag(CNT_LAST_INSERT_ID)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(remove_last_insert_id(expr->get_param_expr(i)))) {
        LOG_WARN("fail to check whether last insert id expr is removable", K(ret));
      }
    }
  } else {}
  return ret;
}

int ObTransformPreProcess::expand_correlated_cte(ObDMLStmt *stmt, bool& trans_happened)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObDMLStmt::TempTableInfo, 8> temp_table_infos;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is NULL", K(ret));
  } else if (OB_FAIL(stmt->collect_temp_table_infos(temp_table_infos))) {
    LOG_WARN("failed to collect temp table infos", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < temp_table_infos.count(); i ++) {
    bool is_correlated = false;
    bool can_expand = true;
    ObSEArray<ObSelectStmt *, 4> dummy;
    ObSelectStmt *temp_query = temp_table_infos.at(i).temp_table_query_;
    if (OB_ISNULL(temp_query)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("got unexpected null ptr", K(ret));
    } else if (OB_FAIL(check_is_correlated_cte(temp_query, dummy, is_correlated))) {
      LOG_WARN("failed to check is correlated cte", K(ret));
    } else if (!is_correlated) {
      //do nothing
    } else if (OB_FAIL(ObTransformUtils::check_inline_temp_table_valid(temp_table_infos.at(i).temp_table_query_, can_expand))) {
      LOG_WARN("failed to check expand temp table valid", K(ret));
    } else if (OB_FAIL(temp_query->is_query_deterministic(can_expand))) {
      LOG_WARN("failed to check stmt is deterministic", K(ret));
    } else if (!can_expand) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("Correlated CTE Not Supported", K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "Correlated CTE");
    } else if (OB_FAIL(ObTransformUtils::inline_temp_table(ctx_, temp_table_infos.at(i)))) {
      LOG_WARN("failed to extend temp table", K(ret));
    } else {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::check_exec_param_correlated(const ObRawExpr *expr, bool &is_correlated)
{
  int ret = OB_SUCCESS;
  is_correlated = false;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is null", K(ret), K(expr));
  } else if (expr->is_exec_param_expr()) {
    if (!expr->has_flag(BE_USED)) {
      is_correlated = true;
    }
  } else if (expr->has_flag(CNT_DYNAMIC_PARAM)) {
    for (int64_t i = 0; OB_SUCC(ret) && !is_correlated && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(SMART_CALL(check_exec_param_correlated(expr->get_param_expr(i), 
                                                        is_correlated)))) {
        LOG_WARN("failed to check exec param correlated", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_is_correlated_cte(ObSelectStmt *stmt, ObIArray<ObSelectStmt *> &visited_cte, bool &is_correlated)
{
  int ret = OB_SUCCESS;
  ObArray<ObSelectStmt *> child_stmts;
  ObArray<ObRawExpr *> relation_exprs;
  is_correlated = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is NULL", K(ret));
  } else if (OB_FAIL(stmt->get_relation_exprs(relation_exprs))) {
    LOG_WARN("failed to get relation exprs", K(ret));
  } else if (OB_FAIL(stmt->get_child_stmts(child_stmts))) {
    LOG_WARN("failed to get child stmts", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && !is_correlated && i < relation_exprs.count(); ++i) {
    ObRawExpr *expr = relation_exprs.at(i);
    if (OB_FAIL(check_exec_param_correlated(expr, is_correlated))) {
      LOG_WARN("failed to check exec param level", K(ret));
    }
  }
  if (!is_correlated) {
    // add flag to mark the exec param refer the same table
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_table_items().count(); ++i) {
      TableItem *table_item = stmt->get_table_items().at(i);
      if (OB_ISNULL(table_item)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (table_item->is_lateral_table()) {
        for (int64_t j = 0; OB_SUCC(ret) && j < table_item->exec_params_.count(); ++j) {
          ObRawExpr *exec_param = table_item->exec_params_.at(j);
          if (OB_ISNULL(exec_param)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("exec param is null", K(ret));
          } else if (OB_FAIL(exec_param->add_flag(BE_USED))) {
            LOG_WARN("failed to add flag", K(ret));
          }
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_subquery_expr_size(); ++i) {
      ObQueryRefRawExpr *query_ref = stmt->get_subquery_exprs().at(i);
      if (OB_ISNULL(query_ref)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("query ref is null", K(ret), K(query_ref));
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < query_ref->get_exec_params().count(); ++j) {
        ObRawExpr *exec_param = query_ref->get_exec_params().at(j);
        if (OB_ISNULL(exec_param)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("exec param is null", K(ret));
        } else if (OB_FAIL(exec_param->add_flag(BE_USED))) {
          LOG_WARN("failed to add flag", K(ret));
        }
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && !is_correlated && i < child_stmts.count(); ++i) {
      if (OB_FAIL(SMART_CALL(check_is_correlated_cte(child_stmts.at(i), visited_cte, is_correlated)))) {
        LOG_WARN("failed to get non correlated subquery", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_table_size(); ++i) {
      TableItem *table = stmt->get_table_item(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpect null table item", K(ret));
      } else if (!table->is_temp_table()) {
        //do nothing
      } else if (ObOptimizerUtil::find_item(visited_cte, table->ref_query_)) {
        //do nothing
      } else if (OB_FAIL(visited_cte.push_back(table->ref_query_))) {
        LOG_WARN("failed to push back stmt", K(ret));
      } else if (OB_FAIL(SMART_CALL(check_is_correlated_cte(table->ref_query_, visited_cte, is_correlated)))) {
        LOG_WARN("failed to get non correlated subquery", K(ret));
      }
    }

    // clear flag
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_table_items().count(); ++i) {
      TableItem *table_item = stmt->get_table_items().at(i);
      if (OB_ISNULL(table_item)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (table_item->is_lateral_table()) {
        for (int64_t j = 0; OB_SUCC(ret) && j < table_item->exec_params_.count(); ++j) {
          ObRawExpr *exec_param = table_item->exec_params_.at(j);
          if (OB_ISNULL(exec_param)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("exec param is null", K(ret));
          } else if (OB_FAIL(exec_param->clear_flag(BE_USED))) {
            LOG_WARN("failed to add flag", K(ret));
          }
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_subquery_expr_size(); ++i) {
      ObQueryRefRawExpr *query_ref = stmt->get_subquery_exprs().at(i);
      if (OB_ISNULL(query_ref)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("query ref is null", K(ret), K(query_ref));
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < query_ref->get_exec_params().count(); ++j) {
        ObRawExpr *exec_param = query_ref->get_exec_params().at(j);
        if (OB_ISNULL(exec_param)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("exec param is null", K(ret));
        } else if (OB_FAIL(exec_param->clear_flag(BE_USED))) {
          LOG_WARN("failed to clear flag", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::flatten_conditions(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  bool flatten_where = false;
  bool flatten_having = false;
  bool flatten_semi_info = false;
  bool flatten_join  = false;
  bool flatten_start_with = false;
  bool flatten_match_condition = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null pointer error", K(stmt), K(ret));
  } else {
    if (OB_FAIL(do_flatten_conditions(stmt, stmt->get_condition_exprs(), flatten_where))) {
      LOG_WARN("flatten_where_condition_expr failed", K(ret));
    } else {
      //simplify having expr
      if (!stmt->is_select_stmt()) {
        //do nothing
      } else if (OB_FAIL(do_flatten_conditions(stmt, static_cast<ObSelectStmt*>(stmt)->get_having_exprs(), flatten_having))) {
        LOG_WARN("flatten_having_condition_expr failed", K(ret));
      }
      if (OB_FAIL(ret)) {
        //do nothing
      } else if (stmt->is_insert_stmt()) {
        //do nothing
      } else {
        //simplify semi info
        for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_semi_infos().count(); ++i) {
          if (OB_ISNULL(stmt->get_semi_infos().at(i))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpect null semi info", K(stmt->get_semi_infos().at(i)), K(ret));
          } else if (OB_FAIL(do_flatten_conditions(stmt, stmt->get_semi_infos().at(i)->semi_conditions_, flatten_semi_info))) {
            LOG_WARN("flatten_semi_info failed", K(ret));
          }
        }
        //simplify join condition expr
        for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_joined_tables().count(); i++) {
          if (OB_ISNULL(stmt->get_joined_tables().at(i))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpect null joined table item", K(stmt->get_joined_tables().at(i)), K(ret));
          } else if (OB_FAIL(recursive_flatten_join_conditions(stmt, stmt->get_joined_tables().at(i), flatten_join))) {
            LOG_WARN("flatten_join_condition_expr failed", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      trans_happened = flatten_where | flatten_having | flatten_start_with |
                       flatten_match_condition | flatten_semi_info| flatten_join;
    }
  }
  return ret;
}

int ObTransformPreProcess::recursive_flatten_join_conditions(ObDMLStmt *stmt, TableItem *table, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  JoinedTable *join_table = NULL;
  trans_happened = false;
  bool cur_happened = false;
  bool left_happened = false;
  bool right_happened = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null pointer", K(stmt), K(table), K(ret));
  } else if (!table->is_joined_table()) {
    /*do nothing*/
  } else if (OB_ISNULL(join_table = static_cast<JoinedTable*>(table)) ||
             OB_ISNULL(join_table->left_table_) || OB_ISNULL(join_table->right_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(join_table), K(join_table->left_table_), K(join_table));
  } else if (OB_FAIL(do_flatten_conditions(stmt, join_table->join_conditions_, cur_happened))) {
    LOG_WARN("failed to flatten join conditions", K(ret));
  } else if (OB_FAIL(SMART_CALL(recursive_flatten_join_conditions(stmt,
                                                                      join_table->left_table_,
                                                                      left_happened)))) {
    LOG_WARN("failed to flatten left child join condition exprs", K(ret));
  } else if (OB_FAIL(SMART_CALL(recursive_flatten_join_conditions(stmt,
                                                                      join_table->right_table_,
                                                                      right_happened)))) {
    LOG_WARN("failed to flatten right child join condition exprs", K(ret));
  } else {
    trans_happened = cur_happened | left_happened | right_happened;
  }
  return ret;
}

int ObTransformPreProcess::do_flatten_conditions(ObDMLStmt *stmt, ObIArray<ObRawExpr*> &conditions, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  bool flatten_happend = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null pointer error", K(stmt), K(ret));
  } else if (conditions.count() == 0) {
    //do nothing
  } else if (OB_FAIL(ObTransformUtils::flatten_and_or_xor(ctx_, conditions, &flatten_happend))) {
    LOG_WARN("flatten_and_or_xor failed", K(ret));
  } else if (trans_happened) {
    trans_happened =  flatten_happend ;
    OPT_TRACE("   flatten_happend:", flatten_happend);
  }
  return ret;
}

// full-text index queries on a single base table are processed with order preservation. 
// (Order is not preserved in multi-table join scenarios.)
int ObTransformPreProcess::preserve_order_for_fulltext_search(ObDMLStmt *stmt, bool& trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  TableItem *table_item = NULL;
  ObRawExpr *match_expr = nullptr;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (stmt->get_table_items().count() != 1 || stmt->get_order_item_size() != 0) {
    // do nothing
  } else if (stmt->is_select_stmt() && 
             (static_cast<ObSelectStmt*>(stmt)->has_order_by() ||
              static_cast<ObSelectStmt*>(stmt)->has_group_by() ||
              static_cast<ObSelectStmt*>(stmt)->has_distinct() ||
              static_cast<ObSelectStmt*>(stmt)->get_aggr_item_size() != 0 ||
              static_cast<ObSelectStmt*>(stmt)->has_window_function() ||
              static_cast<ObSelectStmt*>(stmt)->get_table_items().count() != 1)) {
    // do nothing
  } else if (OB_ISNULL(table_item = stmt->get_table_item(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (!table_item->is_basic_table()) {
    // do nothing
  } else if (0 == stmt->get_match_exprs().count()) {
    // do nothing
  } else if (stmt->get_match_exprs().at(0) != nullptr && stmt->get_match_exprs().at(0)->is_es_match()) {
    ObSEArray<ObRawExpr*, 2> relation_exprs;
    if (OB_FAIL(stmt->get_relation_exprs(relation_exprs))) {
      LOG_WARN("failed to get relation exprs", K(ret));
    } else {
      ObRawExpr *es_match_expr = nullptr;
      ObRawExpr *es_score_expr = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < relation_exprs.count(); ++i) {
        ObRawExpr *relation_expr = relation_exprs.at(i);
        if (OB_ISNULL(relation_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null relation expr", K(ret));
        } else if (relation_expr->has_flag(CNT_MATCH_EXPR) && relation_expr->get_expr_type() == T_OP_ADD) {
          if (relation_expr->get_param_count() != 2) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected relation expr param count", K(ret), K(relation_expr));
          } else {
            es_score_expr = relation_expr;
            ObRawExpr *param_expr0 = relation_expr->get_param_expr(0);
            ObRawExpr *param_expr1 = relation_expr->get_param_expr(1);
            if (OB_ISNULL(param_expr0) || OB_ISNULL(param_expr1)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null param expr", K(ret), KP(param_expr0), KP(param_expr1));
            } else if (param_expr0->has_flag(IS_MATCH_EXPR)) {
              es_match_expr = param_expr0;
            } else if (param_expr1->has_flag(IS_MATCH_EXPR)) {
              es_match_expr = param_expr1;
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected relation expr param expr", K(ret), KP(param_expr0), KP(param_expr1));
            }
          }
        } else if (relation_expr->has_flag(IS_MATCH_EXPR)) {
          es_match_expr = relation_expr;
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(es_match_expr)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("no score expr is not supported", K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "no score expr is");
      } else {
        es_score_expr = nullptr == es_score_expr ? es_match_expr : es_score_expr;
        OrderItem item(es_score_expr, default_desc_direction());
        if (OB_FAIL(stmt->add_order_item(item))) {
          LOG_WARN("failed to add order item", K(ret), K(item));
        } else {
          trans_happened = true;
        }
      }
    }
  } else {
    const common::ObIArray<ObRawExpr *> &condition_exprs = stmt->get_condition_exprs();
    bool found = false;
    for (int64_t i = 0; OB_SUCC(ret) && !found && i < condition_exprs.count(); ++i) {
      ObRawExpr *filter = nullptr;
      if (OB_ISNULL(filter = condition_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected nullptr to where condition filter", K(ret), K(i), KP(filter));
      } else if (filter->has_flag(IS_MATCH_EXPR)) {
        match_expr = filter;
        found = true;
      } else if (!filter->has_flag(CNT_MATCH_EXPR)
          || filter->has_flag(CNT_OR)) {
        // skip
      } else if (IS_RANGE_CMP_OP(filter->get_expr_type())) {
        ObRawExpr *param_expr0 = filter->get_param_expr(0);
        ObRawExpr *param_expr1 = filter->get_param_expr(1);
        if (OB_ISNULL(param_expr0) || OB_ISNULL(param_expr1)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpecter null param expr for range cmp op", K(ret), KP(param_expr0), KP(param_expr1));
        } else if (param_expr0->is_const_expr() && param_expr1->has_flag(IS_MATCH_EXPR)) {
          match_expr = param_expr1;
          found = true;
        } else if (param_expr1->is_const_expr() && param_expr0->has_flag(IS_MATCH_EXPR)) {
          match_expr = param_expr0;
          found = true;
        }
      } else if (filter->get_expr_type() == T_OP_BOOL) {
        ObRawExpr *param_expr = filter->get_param_expr(0);
        if (OB_ISNULL(param_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null param expr for bool op", K(ret));
        } else if (param_expr->has_flag(IS_MATCH_EXPR)) {
          found = true;
          match_expr = param_expr;
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (0 == stmt->get_match_exprs().count()) {
    // do nothing
  } else if (stmt->get_match_exprs().at(0) != nullptr && stmt->get_match_exprs().at(0)->is_es_match()) {
    ObSEArray<ObRawExpr*, 2> relation_exprs;
    if (OB_FAIL(stmt->get_relation_exprs(relation_exprs))) {
      LOG_WARN("failed to get relation exprs", K(ret));
    } else {
      ObRawExpr *es_match_expr = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < relation_exprs.count(); ++i) {
        ObRawExpr *relation_expr = relation_exprs.at(i);
        if (OB_ISNULL(relation_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null relation expr", K(ret));
        } else if (relation_expr->has_flag(CNT_MATCH_EXPR) && relation_expr->get_expr_type() == T_OP_ADD) {
          if (relation_expr->get_param_count() != 2) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected relation expr param count", K(ret), K(relation_expr));
          } else {
            ObRawExpr *param_expr0 = relation_expr->get_param_expr(0);
            ObRawExpr *param_expr1 = relation_expr->get_param_expr(1);
            if (OB_ISNULL(param_expr0) || OB_ISNULL(param_expr1)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null param expr", K(ret), KP(param_expr0), KP(param_expr1));
            } else if (param_expr0->has_flag(IS_MATCH_EXPR)) {
              es_match_expr = param_expr0;
            } else if (param_expr1->has_flag(IS_MATCH_EXPR)) {
              es_match_expr = param_expr1;
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected relation expr param expr", K(ret), KP(param_expr0), KP(param_expr1));
            }
          }
        } else if (relation_expr->has_flag(IS_MATCH_EXPR)) {
          es_match_expr = relation_expr;
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(es_match_expr)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("no score expr is not supported", K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "no score expr is");
      }
    }
  }
  if (OB_SUCC(ret) && nullptr != match_expr && !static_cast<ObMatchFunRawExpr*>(match_expr)->is_es_match()) {
    OrderItem item(match_expr, default_desc_direction());
    if (OB_FAIL(stmt->add_order_item(item))) {
      LOG_WARN("failed to add order item", K(ret), K(item));
    } else {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::preserve_order_for_pagination(ObDMLStmt *stmt, 
                                                         bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  bool is_valid = false;
  ObSEArray<ObSelectStmt*, 2> preserve_order_stmts;
  if (OB_FAIL(check_stmt_need_preserve_order(stmt, 
                                             preserve_order_stmts, 
                                             is_valid))) {
    LOG_WARN("failed to check stmt need add order by", K(ret));
  } else if (!is_valid) {
    //do nothing
  } else {
    bool happened = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < preserve_order_stmts.count(); ++i) {
      if (OB_FAIL(add_order_by_for_stmt(preserve_order_stmts.at(i), happened))) {
        LOG_WARN("failed to add order by for stmt", K(ret));
      } else {
        trans_happened |= happened;
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::check_stmt_need_preserve_order(ObDMLStmt *stmt, 
                                                          ObIArray<ObSelectStmt*> &preserve_order_stmts, 
                                                          bool &is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = false;
  bool is_hint_enabled = false;
  bool has_hint = false;
  ObSelectStmt *sel_stmt = NULL;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) ||
      OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null", K(ret), K(stmt), K(ctx_));
  } else if (OB_FAIL(stmt->get_query_ctx()->get_global_hint().opt_params_.get_bool_opt_param(
              ObOptParamHint::PRESERVE_ORDER_FOR_PAGINATION, is_hint_enabled, has_hint))) {
    LOG_WARN("failed to check has opt param", K(ret));
  } else if (has_hint && !is_hint_enabled) {
    OPT_TRACE("query hint disable preserve order for pagination");
  } else if (OB_FAIL(ctx_->session_info_->is_preserve_order_for_pagination_enabled(is_valid))) {
    LOG_WARN("failed to check preserve order for pagination enabled", K(ret));
  } else if (!is_valid && !has_hint) {
    OPT_TRACE("system config disable preserve order for pagination");
  } else if (!stmt->is_select_stmt()) {
    OPT_TRACE("dml query can not preserve order for pagination");
  } else if (OB_FALSE_IT(sel_stmt=static_cast<ObSelectStmt*>(stmt))) {
  } else if (!sel_stmt->has_limit() || NULL != sel_stmt->get_limit_percent_expr()) {
    OPT_TRACE("query do not have normal limit offset");
  } else if (OB_FALSE_IT(is_valid = true)) {
  } else if (sel_stmt->has_order_by() ||
             sel_stmt->has_group_by() || 
             sel_stmt->has_distinct() ||
             sel_stmt->get_aggr_item_size() != 0 || 
             sel_stmt->has_window_function() || 
             1 != sel_stmt->get_table_items().count()) {
    if (OB_FAIL(preserve_order_stmts.push_back(sel_stmt))) {
      LOG_WARN("failed to push back stmt", K(ret));
    }
  } else {
    TableItem *table = sel_stmt->get_table_items().at(0);
    ObSEArray<ObSelectStmt*, 2> view_preserve_order_stmts;
    bool need_preserve = false;
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null table", K(ret));
    } else if (!table->is_generated_table()) {
      if (OB_FAIL(preserve_order_stmts.push_back(sel_stmt))) {
        LOG_WARN("failed to push back stmt", K(ret));
      }
    } else if (OB_FAIL(check_view_need_preserve_order(table->ref_query_, 
                                                      view_preserve_order_stmts, 
                                                      need_preserve))) {
      LOG_WARN("failed to check view need preserve_order", K(ret));
    } else if (need_preserve && 
               OB_FAIL(append(preserve_order_stmts, view_preserve_order_stmts))) {
      LOG_WARN("failed to append stmt", K(ret));
    } else if (!need_preserve && 
               OB_FAIL(preserve_order_stmts.push_back(sel_stmt))) {
      LOG_WARN("failed to push back stmt", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::check_view_need_preserve_order(ObSelectStmt* stmt, 
                                                          ObIArray<ObSelectStmt*> &preserve_order_stmts,
                                                          bool &need_preserve)
{
  int ret = OB_SUCCESS;
  need_preserve = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null stmt", K(ret));
  } else if (stmt->has_order_by() && !stmt->has_limit()) {
    need_preserve = true;
    if (OB_FAIL(preserve_order_stmts.push_back(stmt))) {
      LOG_WARN("failed to push back stmt", K(ret));
    }
  } else if (!stmt->has_order_by() && 
             !stmt->has_limit() && 
             stmt->is_set_stmt() && 
             OB_FAIL(check_set_stmt_need_preserve_order(stmt, 
                                                        preserve_order_stmts, 
                                                        need_preserve))) {
    LOG_WARN("failed to check set stmt preserve order", K(ret));
  } else if (!stmt->is_spj()) {
    //do nothing
  } else if (1 != stmt->get_table_items().count()) {
    //do nothing
  } else {
    TableItem *table = stmt->get_table_items().at(0);
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null table", K(ret));
    } else if (!table->is_generated_table()) {
      //do nothing
    } else if (OB_FAIL(SMART_CALL(check_view_need_preserve_order(table->ref_query_, 
                                                                 preserve_order_stmts, 
                                                                 need_preserve)))) {
      LOG_WARN("failed to check view need preserve order", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::check_set_stmt_need_preserve_order(ObSelectStmt* stmt, 
                                                              ObIArray<ObSelectStmt*> &preserve_order_stmts, 
                                                              bool &need_preserve)
{
  int ret = OB_SUCCESS;
  need_preserve = false;
  bool force_serial_set_order = false;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) ||
      OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null stmt", K(ret));
  } else if (OB_FAIL(ctx_->session_info_->is_serial_set_order_forced(force_serial_set_order, 
                                                                     false))) {
    LOG_WARN("fail to get force_serial_set_order value", K(ret));
  } else if (!force_serial_set_order) {
    //do nothing
  } else if (!stmt->is_set_stmt() || 
             stmt->is_set_distinct() || 
             stmt->is_recursive_union()) {
    //do nothing
  } else {
    need_preserve = true;
    int64_t N = stmt->get_set_query().count();
    for (int64_t i = 0; OB_SUCC(ret) && need_preserve && i < N; ++i) {
      ObSelectStmt *set_query = stmt->get_set_query().at(i);
      if (OB_ISNULL(set_query)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null stmt", K(ret));
      } else if (OB_FAIL(SMART_CALL(check_view_need_preserve_order(set_query, 
                                                                   preserve_order_stmts, 
                                                                   need_preserve)))) {
        LOG_WARN("failed to check view need preserve order", K(ret));
      }
    }
  }
  return ret;
}

int ObTransformPreProcess::add_order_by_for_stmt(ObSelectStmt* stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  ObSEArray<ObRawExpr*, 2> select_exprs;
  ObSEArray<ObRawExpr*, 2> order_by_exprs;
  ObSEArray<ObRawExpr*, 2> new_order_by_exprs;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null stmt", K(ret));
  } else if (OB_FAIL(stmt->get_order_exprs(order_by_exprs))) {
    LOG_WARN("failed to get order exprs", K(ret));
  } else if (OB_FAIL(ObTransformUtils::check_stmt_unique(stmt, 
                                                         ctx_->session_info_, 
                                                         ctx_->schema_checker_, 
                                                         order_by_exprs,
                                                         true, 
                                                         is_valid))) {
    LOG_WARN("failed to check stmt unique on exprs", K(ret));
  } else if (is_valid) {
    OPT_TRACE("current order by exprs is unique, do not need add extra order by exprs");
  } else if (OB_FAIL(get_rowkey_for_single_table(stmt, select_exprs, is_valid))) {
    LOG_WARN("failed to get rowkey for single table", K(ret));
  } else if (!is_valid &&
             OB_FAIL(stmt->get_select_exprs_without_lob(select_exprs))) {
    LOG_WARN("failed to get select exprs", K(ret));
  } else if (OB_FAIL(ObOptimizerUtil::except_exprs(select_exprs, 
                                                   order_by_exprs, 
                                                   new_order_by_exprs))) {
    LOG_WARN("failed to except exprs", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < new_order_by_exprs.count(); ++i) {
      OrderItem item(new_order_by_exprs.at(i));
      if (OB_FAIL(stmt->add_order_item(item))) {
        LOG_WARN("failed to add order item", K(ret));
      }
    }
    if (OB_SUCC(ret) && !new_order_by_exprs.empty()) {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::get_rowkey_for_single_table(ObSelectStmt* stmt, 
                                                       ObIArray<ObRawExpr*> &unique_keys, 
                                                       bool &is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = false;
  TableItem *table = NULL;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null stmt", K(ret));
  } else if (1 != stmt->get_table_items().count()) {
    //do nothing
  } else if (OB_ISNULL(table=stmt->get_table_items().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null table item", K(ret));
  } else if (!table->is_basic_table()) {
    //do nothing
  } else if (OB_FAIL(ObTransformUtils::generate_unique_key_for_basic_table(ctx_, 
                                                           stmt, 
                                                           table, 
                                                           unique_keys))) {
    LOG_WARN("failed to generate unique key", K(ret));
  } else {
    is_valid = true;
  }
  return ret;
}

int ObTransformPreProcess::preserve_order_for_gby(ObDMLStmt *stmt, 
                                                  bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *sel_stmt = NULL;
  bool is_valid = false;
  bool is_hint_enabled = false;
  bool has_hint = false;
  ObSEArray<ObRawExpr*, 4> group_exprs;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_) ||
      OB_ISNULL(ctx_->session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null", K(ret), K(stmt), K(ctx_));
  } else if (OB_FAIL(stmt->get_query_ctx()->get_global_hint().opt_params_.get_bool_opt_param(
              ObOptParamHint::PRESERVE_ORDER_FOR_GROUPBY, is_hint_enabled, has_hint))) {
    LOG_WARN("failed to check has opt param", K(ret));
  } else if (has_hint && !is_hint_enabled) {
    OPT_TRACE("query hint disable preserve order for group by");
  } else if (OB_FAIL(ctx_->session_info_->is_preserve_order_for_groupby_enabled(is_valid))) {
    LOG_WARN("failed to check preserve order for group by enabled", K(ret));
  } else if (!is_valid && !has_hint) {
    OPT_TRACE("system config disable preserve order for group by");
  } else if (!stmt->is_select_stmt()) {
    OPT_TRACE("dml query can not preserve order for group by");
  } else if (OB_FALSE_IT(sel_stmt=static_cast<ObSelectStmt*>(stmt))) {
  } else if (sel_stmt->has_order_by()) {
    OPT_TRACE("order by query can not preserve order for group by");
  } else if (!sel_stmt->has_group_by() || sel_stmt->is_scala_group_by()) {
    OPT_TRACE("non normal group by query can not preserve order for group by");
  } else if (OB_FAIL(stmt->get_relation_exprs(group_exprs, SCOPE_GROUPBY))) {
    LOG_WARN("failed to get group exprs", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < group_exprs.count(); ++i) {
      OrderItem item(group_exprs.at(i));
      if (OB_FAIL(sel_stmt->add_order_item(item))) {
        LOG_WARN("failed to add order item", K(ret));
      }
    }
    if (OB_SUCC(ret) && !group_exprs.empty()) {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::try_gen_straight_join_leading(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  const ObQueryHint *query_hint = NULL;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_ISNULL(query_hint = stmt->get_stmt_hint().query_hint_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(query_hint));
  } else {
    ObHint *leading_hint = stmt->get_stmt_hint().get_normal_hint(T_LEADING);
    bool exist_leading_hint = query_hint->has_outline_data() || 
                              query_hint->has_user_def_outline() ||
                              OB_NOT_NULL(leading_hint);
    ObSEArray<TableItem*, 4> flattened_tables;

    //if the current qb already has leading hint or outline data, ignore straight_join info.
    if (exist_leading_hint) {
      //do nothing
    } else if (stmt->is_select_stmt() && 
               static_cast<ObSelectStmt*>(stmt)->is_select_straight_join()) {
      if (OB_FAIL(add_ordered_hint(stmt, stmt->get_stmt_hint()))) {
        LOG_WARN("failed to add ordered hint", K(ret));
      } else {
        //for straight_join written in the select clause, an ordered hint is added by default.
        trans_happened = true;
      }
    } else if (OB_FAIL(get_flattened_tables_of_pure_straight_join(stmt, flattened_tables))) {
      LOG_WARN("failed to get flattend tables of first pure straight join table", K(ret));
    } else if (flattened_tables.count() < 2) {
      //do nothing
    } else if (OB_FAIL(add_leading_hint_by_flattened_tables(stmt, stmt->get_stmt_hint(), 
                                                            flattened_tables))) {
      LOG_WARN("failed to add leading hint by straight join table", K(ret));
    } else {
      //for straight_join written in the from clause, generate partial leading info based on 
      //the first pure straight_join table found from left to right.
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformPreProcess::get_flattened_tables_of_pure_straight_join(ObDMLStmt* stmt, 
                                                ObIArray<TableItem*> &flattened_tables)
{
  int ret = OB_SUCCESS;
  ObSEArray<TableItem*, 4> from_tables;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FAIL(stmt->get_from_tables(from_tables))) {
    LOG_WARN("failed to get from tables", K(ret));
  } else {
    bool found = false;
    for (int64_t i = 0; OB_SUCC(ret) && !found && i < from_tables.count(); i++) {
      TableItem *table_item = NULL;
      bool is_pure_straight_join = false;
      if (OB_ISNULL(table_item = from_tables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret));
      } else if (!table_item->is_joined_table()) {
        //do nothing
      } else if (OB_FAIL(check_pure_straight_join_table(table_item, is_pure_straight_join, 
                                                        flattened_tables))) {
        LOG_WARN("failed to check pure straight join table", K(ret));
      } else if (!is_pure_straight_join) {
        flattened_tables.reuse();
      } else {
        found = true;
      }
    }
  }
  return ret;
}

// If all leaf nodes in the joined table are base tables and the join types within it are straight_join, 
// then it is considered a pure_straight_join_table whose join order is considered to be determined
// (left associative after flattening).
int ObTransformPreProcess::check_pure_straight_join_table(TableItem* table_item, 
                                                          bool &is_pure_straight_join,
                                                          ObIArray<TableItem*> &flattened_tables)
{
  int ret = OB_SUCCESS;
  is_pure_straight_join = false;
  if (OB_ISNULL(table_item)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (table_item->is_basic_table()) {
    is_pure_straight_join = true;
    if (OB_FAIL(flattened_tables.push_back(table_item))) {
      LOG_WARN("failed to push back table item", K(ret));
    }
  } else if (!table_item->is_joined_table()) {
    is_pure_straight_join = false;
  } else {
    JoinedTable *joined_table = static_cast<JoinedTable*>(table_item);
    bool is_left_pure_sj = false;
    bool is_right_pure_sj = false;
    if (OB_FAIL(SMART_CALL(check_pure_straight_join_table(joined_table->left_table_,
                                                          is_left_pure_sj,
                                                          flattened_tables)))) {
      LOG_WARN("failed to check pure straight join table", K(ret));
    } else if (OB_FAIL(SMART_CALL(check_pure_straight_join_table(joined_table->right_table_,
                                                                is_right_pure_sj,
                                                                flattened_tables)))) {
      LOG_WARN("failed to check pure straight join table", K(ret));
    } else if (joined_table->is_straight_join()) {
      is_pure_straight_join = is_left_pure_sj && is_right_pure_sj;
    } else {
      is_pure_straight_join = false;
    }
  }
  return ret;
}

int ObTransformPreProcess::add_ordered_hint(ObDMLStmt* stmt, ObStmtHint &stmt_hint)
{
  int ret = OB_SUCCESS;
  ObJoinOrderHint *join_order_hint = NULL;
  ObString qb_name;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FAIL(stmt->get_qb_name(qb_name))) {
    LOG_WARN("failed to get qb name", K(ret));
  } else if (OB_FAIL(ObQueryHint::create_hint(ctx_->allocator_, T_ORDERED, join_order_hint))) {
    LOG_WARN("failed to create hint", K(ret));
  } else if (OB_FALSE_IT(join_order_hint->set_qb_name(qb_name))) {
  } else if (OB_FAIL(stmt_hint.normal_hints_.push_back(join_order_hint))) {
    LOG_WARN("failed to push back hint", K(ret));
  }
  return ret;
}

int ObTransformPreProcess::add_leading_hint_by_flattened_tables(ObDMLStmt* stmt, 
                                                                ObStmtHint &stmt_hint, 
                                                                ObIArray<TableItem*> &flattened_tables)
{
  int ret = OB_SUCCESS;
  ObJoinOrderHint *join_order_hint = NULL;
  ObString qb_name;
  if (OB_ISNULL(stmt) || OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FAIL(stmt->get_qb_name(qb_name))) {
    LOG_WARN("failed to get qb name", K(ret));
  } else if (OB_FAIL(ObQueryHint::create_hint(ctx_->allocator_, T_LEADING, join_order_hint))) {
    LOG_WARN("failed to create hint", K(ret));
  } else if (OB_ISNULL(join_order_hint)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FALSE_IT(join_order_hint->set_qb_name(qb_name))) {
  } else if (OB_FAIL(construct_leading_table(stmt, flattened_tables, join_order_hint->get_table()))) {
    LOG_WARN("failed to construct leading table", K(ret));
  } else if (OB_FAIL(stmt_hint.normal_hints_.push_back(join_order_hint))) {
    LOG_WARN("failed to push back hint", K(ret));
  }
  return ret;
}

int ObTransformPreProcess::construct_leading_table(ObDMLStmt* stmt,
                                                   ObIArray<TableItem*> &flattened_tables,
                                                   ObLeadingTable &leading_table)
{
  int ret = OB_SUCCESS;
  ObLeadingTable *cur_leading_table = NULL;
  if (OB_ISNULL(ctx_) || OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (flattened_tables.count() < 2) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table count", K(ret), K(flattened_tables.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < flattened_tables.count(); i++) {
      ObLeadingTable *left_leading_table = cur_leading_table;
      ObLeadingTable *right_leading_table = NULL;
      if (OB_ISNULL(cur_leading_table)) {
        if (OB_FAIL(construct_leaf_leading_table(stmt, flattened_tables.at(i), cur_leading_table))) {
          LOG_WARN("failed to construct leaf leading table", K(ret));
        } else if (OB_ISNULL(cur_leading_table)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        }
      } else if (OB_FAIL(construct_leaf_leading_table(stmt, flattened_tables.at(i), right_leading_table))) {
        LOG_WARN("failed to construct leaf leading table", K(ret));
      } else if (OB_FAIL(ObQueryHint::create_leading_table(ctx_->allocator_, cur_leading_table))) {
        LOG_WARN("failed to create leading table", K(ret));
      } else if (OB_ISNULL(cur_leading_table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret));
      } else {
        cur_leading_table->left_table_ = left_leading_table;
        cur_leading_table->right_table_ = right_leading_table;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(leading_table.assign(*cur_leading_table))) {
      LOG_WARN("failed to assign leading table", K(ret));
    }
  }
  return ret;
}

int ObTransformPreProcess::construct_leaf_leading_table(ObDMLStmt *stmt,
                                                        TableItem *table,
                                                        ObLeadingTable *&leading_table)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_) || OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FAIL(ObQueryHint::create_leading_table(ctx_->allocator_, leading_table))) {
    LOG_WARN("failed to create leading table", K(ret));
  } else if (OB_ISNULL(leading_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FAIL(ObQueryHint::create_hint_table(ctx_->allocator_, leading_table->table_))) {
    LOG_WARN("fail to create hint table", K(ret));
  } else if (OB_ISNULL(leading_table->table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else if (OB_FAIL(stmt->get_qb_name(leading_table->table_->qb_name_))) {
    LOG_WARN("failed to get qb name", K(ret));
  } else {
    leading_table->table_->db_name_ = table->database_name_;
    leading_table->table_->table_name_ = table->table_name_;
  }
  return ret;
}

int ObTransformPreProcess::reset_view_base(ObDMLStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else {
    ObIArray<TableItem*> &tables = stmt->get_table_items();
    TableItem *table_item = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
      if (OB_ISNULL(table_item = tables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret), KP(table_item));
      } else {
        table_item->view_base_item_ = NULL;
      }
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
