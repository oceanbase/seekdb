/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OB_TRANSFORM_PRE_PROCESS_H_
#define OB_TRANSFORM_PRE_PROCESS_H_

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "sql/ob_sql_context.h"

namespace oceanbase
{

namespace sql
{

typedef std::pair<uint64_t, uint64_t> JoinTableIdPair;

struct DistinctObjMeta
{
  ObObjType obj_type_;
  ObCollationType coll_type_;
  ObCollationLevel coll_level_;
  ObScale scale_;

  DistinctObjMeta(ObObjType obj_type, ObCollationType coll_type,
                  ObCollationLevel coll_level, ObScale scale)
    : obj_type_(obj_type), coll_type_(coll_type),
      coll_level_(coll_level), scale_(scale)
  {
    if (!ObDatumFuncs::is_string_type(obj_type_) && !ob_is_enum_or_set_type(obj_type_)) {
      coll_type_ = CS_TYPE_MAX;
      coll_level_ = CS_LEVEL_INVALID;
    }
  }
  DistinctObjMeta()
    : obj_type_(common::ObMaxType), coll_type_(common::CS_TYPE_MAX) ,
      coll_level_(CS_LEVEL_INVALID), scale_(-1) {}

  bool operator==(const DistinctObjMeta &other) const
  {
    bool cs_level_equal = (coll_level_ == other.coll_level_);
    bool res = obj_type_ == other.obj_type_ && coll_type_ == other.coll_type_ && cs_level_equal;
    if (res && ob_is_double_type(obj_type_)) {
      bool is_fixed_double_1 = SCALE_UNKNOWN_YET < scale_ && OB_MAX_DOUBLE_FLOAT_SCALE >= scale_;
      bool is_fixed_double_2 = SCALE_UNKNOWN_YET < other.scale_ && OB_MAX_DOUBLE_FLOAT_SCALE >= other.scale_;
      res = is_fixed_double_1 == is_fixed_double_2;
    }
    return res;
  }
  TO_STRING_KV(K_(obj_type), K_(coll_type), K_(coll_level), K_(scale));
};

class ObTransformPreProcess: public ObTransformRule
{
public:
  explicit ObTransformPreProcess(ObTransformerCtx *ctx)
     : ObTransformRule(ctx, TransMethod::POST_ORDER) { }
  virtual ~ObTransformPreProcess() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;

  static int transform_expr(ObRawExprFactory &expr_factory,
                            const ObSQLSessionInfo &session,
                            ObRawExpr *&expr,
                            bool &trans_happened);
private:
  virtual int need_transform(const common::ObIArray<ObParentDMLStmt> &parent_stmts,
                             const int64_t current_level,
                             const ObDMLStmt &stmt,
                             bool &need_trans) override;
// used for transform in expr to or exprs

  /*
   * following functions are used to add all rowkey columns
   */
  int add_all_rowkey_columns_to_stmt(ObDMLStmt *stmt, bool &trans_happened);
  int add_all_rowkey_columns_to_stmt(const ObTableSchema &table_schema,
                                     const TableItem &table_item,
                                     ObRawExprFactory &expr_factory,
                                     ObDMLStmt &stmt,
                                     ObIArray<ColumnItem> &column_items);


  int is_subquery_correlated(const ObSelectStmt *stmt,
                             hash::ObHashSet<uint64_t> &param_set,
                             bool &is_correlated);
  
  int add_exec_params(const ObSelectStmt &stmt,
                      hash::ObHashSet<uint64_t> &param_set);
  
  int has_new_exec_param(const ObRawExpr *expr,
                         const hash::ObHashSet<uint64_t> &param_set,
                         bool &has_new);

  int create_child_stmts_for_groupby_sets(ObSelectStmt *origin_stmt,
                                          ObIArray<ObSelectStmt*> &child_stmts);
  int get_groupby_and_rollup_exprs_list(ObSelectStmt *origin_stmt,
                                        ObIArray<ObGroupbyExpr> &groupby_exprs_list,
                                        ObIArray<ObGroupbyExpr> &rollup_exprs_list);
  // calc 'grouping', 'grouping id', 'group id'
  int calc_group_type_aggr_func(const ObIArray<ObRawExpr*> &groupby_exprs,
                                const ObIArray<ObRawExpr*> &rollup_exprs,
                                const ObIArray<ObGroupbyExpr> &groupby_exprs_list,
                                const int64_t cur_index,
                                const int64_t origin_groupby_num,
                                ObAggFunRawExpr *expr,
                                ObRawExpr *&new_expr);
  int calc_grouping_in_grouping_sets(const ObIArray<ObRawExpr*> &groupby_exprs,
                                     const ObIArray<ObRawExpr*> &rollup_exprs,
                                     ObAggFunRawExpr *expr,
                                     ObRawExpr *&new_expr);

	/*
	 * follow functions are used for eliminate having
	 */
	int eliminate_having(ObDMLStmt *stmt, bool &trans_happened);

	/*
	 * following functions are used to replace func is serving tenant
	 */
	int replace_func_is_serving_tenant(ObDMLStmt *&stmt, bool &trans_happened);
	int recursive_replace_func_is_serving_tenant(ObDMLStmt &stmt,
                                               ObRawExpr *&cond_expr,
                                               bool &trans_happened);
	int calc_const_raw_expr_and_get_int(const ObStmt &stmt,
                                      ObRawExpr *const_expr,
                                      ObExecContext &exec_ctx,
                                      ObSQLSessionInfo *session,
                                      ObIAllocator &allocator,
                                      int64_t &result);
  int transform_special_expr(ObDMLStmt *&stmt, bool &trans_happened);
	int collect_all_tableitem(ObDMLStmt *stmt,
                            TableItem *table_item,
                            common::ObArray<TableItem*> &table_item_list);

  int transform_exprs(ObDMLStmt *stmt, bool &trans_happened);
  int transform_for_nested_aggregate(ObDMLStmt *&stmt, bool &trans_happened);
  int generate_child_level_aggr_stmt(ObSelectStmt *stmt, ObSelectStmt *&sub_stmt);
  int get_inner_aggr_exprs(ObSelectStmt *sub_stmt, common::ObIArray<ObAggFunRawExpr*>& inner_aggr_exprs);
  int get_first_level_output_exprs(ObSelectStmt *sub_stmt,
                                   common::ObIArray<ObRawExpr*>& inner_aggr_exprs);
  int generate_parent_level_aggr_stmt(ObSelectStmt *&stmt, ObSelectStmt *sub_stmt);
  int remove_nested_aggr_exprs(ObSelectStmt *stmt);
  int construct_column_items_from_exprs(const ObIArray<ObRawExpr*> &column_exprs,
                                        ObIArray<ColumnItem> &column_items);
  /*
   * following functions are used to transform in_expr to or_expr
   */
  static int transform_in_or_notin_expr_with_row(ObRawExprFactory &expr_factory,
                                                 const ObSQLSessionInfo &session,
                                                 const bool is_in_expr,
                                                 ObRawExpr *&in_expr,
                                                 bool &trans_happened);

  static int transform_in_or_notin_expr_without_row(ObRawExprFactory &expr_factory,
                                                 const ObSQLSessionInfo &session,
                                                 const bool is_in_expr,
                                                 ObRawExpr *&in_epxr,
                                                 bool &trans_happened);

  static int create_partial_expr(ObRawExprFactory &expr_factory,
                                 const ObSQLSessionInfo &session,
                                 ObRawExpr *left_expr,
                                 ObIArray<ObRawExpr*> &same_type_exprs,
                                 const bool is_in_expr,
                                 ObIArray<ObRawExpr*> &transed_in_exprs);

  /*
   * following functions are used to transform arg_case_expr to case_expr
   */
  static int transform_arg_case_recursively(ObRawExprFactory &expr_factory,
                                                const ObSQLSessionInfo &session,
                                                ObRawExpr *&expr,
                                                bool &trans_happened);
  static int transform_arg_case_expr(ObRawExprFactory &expr_factory,
                                     const ObSQLSessionInfo &session,
                                     ObRawExpr *&expr,
                                     bool &trans_happened);
  static int create_equal_expr_for_case_expr(ObRawExprFactory &expr_factory,
                                             const ObSQLSessionInfo &session,
                                             ObRawExpr *arg_expr,
                                             ObRawExpr *when_expr,
                                             const ObCollationType cmp_cs_type,
                                             ObOpRawExpr *&equal_expr);
  static int add_row_type_to_array_no_dup(common::ObIArray<ObSEArray<DistinctObjMeta, 4>> &row_type_array,
                                          const ObSEArray<DistinctObjMeta, 4> &row_type);

  static bool is_same_row_type(const common::ObIArray<DistinctObjMeta> &left,
                               const common::ObIArray<DistinctObjMeta> &right);

  static int get_final_transed_or_and_expr(
      ObRawExprFactory &expr_factory,
      const ObSQLSessionInfo &session,
      const bool is_in_expr,
      common::ObIArray<ObRawExpr *> &transed_in_exprs,
      ObRawExpr *&final_or_expr);

  static int check_and_transform_in_or_notin(ObRawExprFactory &expr_factory,
                                             const ObSQLSessionInfo &session,
                                             ObRawExpr *&in_expr,
                                             bool &trans_happened);
  static int replace_in_or_notin_recursively(ObRawExprFactory &expr_factory,
                                             const ObSQLSessionInfo &session,
                                             ObRawExpr *&root_expr,
                                             bool &trans_happened);
  static ObItemType reverse_cmp_type_of_align_date4cmp(const ObItemType &cmp_type);
  static int replace_cast_expr_align_date4cmp(ObRawExprFactory &expr_factory,
                                              const ObSQLSessionInfo &session,
                                              const ObItemType &cmp_type,
                                              ObRawExpr *&expr);
  static int replace_op_row_expr_align_date4cmp(ObRawExprFactory &expr_factory,
                                                const ObSQLSessionInfo &session,
                                                const ObItemType &cmp_type,
                                                ObRawExpr *&left_row_expr,
                                                ObRawExpr *&right_row_expr);
  static int check_and_transform_align_date4cmp(ObRawExprFactory &expr_factory,
                                                const ObSQLSessionInfo &session,
                                                ObRawExpr *&in_expr,
                                                const ObItemType &cmp_type);
  static int replace_align_date4cmp_recursively(ObRawExprFactory &expr_factory,
                                                const ObSQLSessionInfo &session,
                                                ObRawExpr *&root_expr);                                   
  static int replace_inner_row_cmp_val_recursively(ObRawExprFactory &expr_factory,
                                                   const ObSQLSessionInfo &session,
                                                   ObRawExpr *&root_expr,
                                                   bool &trans_happened);
  static int check_and_transform_inner_row_cmp_val(ObRawExprFactory &expr_factory,
                                                   const ObSQLSessionInfo &session,
                                                   ObRawExpr *&row_cmp_expr,
                                                   bool &trans_happened);
  template<bool IS_LEFT>
  static int transform_inner_op_row_cmp_for_decimal_int(ObRawExprFactory &expr_factory,
                                                        const ObSQLSessionInfo &session,
                                                        ObRawExpr *&row_cmp_expr,
                                                        ObRawExpr *&row_expr,
                                                        bool &trans_happened);
  int transformer_aggr_expr(ObDMLStmt *stmt, bool &trans_happened);

  int replace_group_id_in_stmt(ObSelectStmt *stmt);
  int replace_group_id_in_expr_recursive(ObRawExpr *&expr);
  // transform json object with star
  int transform_json_object_expr_with_star(const ObIArray<ObParentDMLStmt> &parent_stmts,
                                           ObDMLStmt *stmt, bool &trans_happened);

  int check_stmt_contain_param_expr(ObDMLStmt *stmt, bool &contain);

  int check_stmt_can_batch(ObDMLStmt *batch_stmt, bool &can_batch);

  int check_contain_param_expr(ObDMLStmt *stmt, TableItem *table_item, bool &contain_param);

  int transform_for_upd_del_batch_stmt(ObDMLStmt *batch_stmt,
                                       ObSelectStmt* inner_view_stmt,
                                       bool &trans_happened);
  int create_inner_view_stmt(ObDMLStmt *batch_stmt, ObSelectStmt*& inner_view_stmt);

  int transform_for_ins_batch_stmt(ObDMLStmt *batch_stmt, bool &trans_happened);

  int transform_for_insertup_batch_stmt(ObDMLStmt *batch_stmt, bool &trans_happened);

  int transform_for_batch_stmt(ObDMLStmt *batch_stmt, bool &trans_happened);

  int check_insert_can_batch(ObInsertStmt *insert_stmt, bool &can_batch);

  bool check_insertup_support_batch_opt(ObInsertStmt *insert_stmt, bool &can_batch);

  int formalize_batch_stmt(ObDMLStmt *batch_stmt,
                          ObSelectStmt* inner_view_stmt,
                          const ObIArray<ObRawExpr *> &other_exprs,
                          bool &trans_happened);

  int mock_select_list_for_upd_del(ObDMLStmt &batch_stmt, ObSelectStmt &inner_view);



  int create_stmt_id_expr(ObPseudoColumnRawExpr *&stmt_id_expr);

  int create_params_expr(ObPseudoColumnRawExpr *&pseudo_param_expr,
                         ObRawExpr *origin_param_expr,
                         int64_t name_id);

  int mock_select_list_for_inner_view(ObDMLStmt &batch_stmt, ObSelectStmt &inner_view);
  
  int transform_outerjoin_exprs(ObDMLStmt *stmt, bool &trans_happened);
  
  int remove_shared_expr(ObDMLStmt *stmt,
                         JoinedTable *joined_table,
                         hash::ObHashSet<uint64_t, hash::NoPthreadDefendMode> &expr_set,
                         bool is_nullside);

  int do_remove_shared_expr(hash::ObHashSet<uint64_t, hash::NoPthreadDefendMode> &expr_set,
                            ObIArray<ObRawExpr *> &padnull_exprs,
                            bool is_nullside,
                            ObRawExpr *&expr,
                            bool &has_nullside_column);
  
  int check_nullside_expr(ObRawExpr *expr, bool &bret);

  int transform_full_outer_join(ObDMLStmt *&stmt, bool &trans_happened);

  int check_join_condition(ObDMLStmt *stmt,
                           JoinedTable *table,
                           bool &has_equal,
                           bool &has_subquery);

  /**
   * @brief recursively_eliminate_full_join
   * 以左-右-后的方式后续遍历from item及semi from item中的joined_table结构
   */
  int recursively_eliminate_full_join(ObDMLStmt &stmt,
                                      TableItem *table_item,
                                      bool &trans_happened);

  /**
   * @brief expand_full_outer_join
   * for select stmt contains a single full outer join, expand to left join union all anti join
   */
  int expand_full_outer_join(ObSelectStmt *&ref_query);

  int create_select_items_for_semi_join(ObDMLStmt *stmt,
                                        TableItem *from_table_item,
                                        const ObIArray<SelectItem> &select_items,
                                        ObIArray<SelectItem> &output_select_items);

  int switch_left_outer_to_semi_join(ObSelectStmt *&sub_stmt,
                                     JoinedTable *joined_table,
                                     const ObIArray<SelectItem> &select_items);

  int extract_idx_from_table_items(ObDMLStmt *sub_stmt,
                                   const TableItem *table_item,
                                   ObSqlBitSet<> &rel_ids);

  int formalize_limit_expr(ObDMLStmt &stmt);
  int transform_rollup_exprs(ObDMLStmt *stmt, bool &trans_happened);
  int get_rollup_const_exprs(ObSelectStmt *stmt,
                             ObIArray<ObRawExpr*> &const_exprs,
                             ObIArray<ObRawExpr*> &const_remove_const_exprs,
                             ObIArray<ObRawExpr*> &exec_params,
                             ObIArray<ObRawExpr*> &exec_params_remove_const_exprs,
                             ObIArray<ObRawExpr*> &column_ref_exprs,
                             ObIArray<ObRawExpr*> &column_ref_remove_const_exprs,
                             ObIArray<ObRawExpr*> &query_ref_exprs,
                             ObIArray<ObRawExpr*> &query_ref_remove_const_exprs,
                             bool &trans_happened);
  int replace_remove_const_exprs(ObSelectStmt *stmt,
                                ObIArray<ObRawExpr*> &const_exprs,
                                ObIArray<ObRawExpr*> &const_remove_const_exprs,
                                ObIArray<ObRawExpr*> &exec_params,
                                ObIArray<ObRawExpr*> &exec_params_remove_const_exprs,
                                ObIArray<ObRawExpr*> &column_ref_exprs,
                                ObIArray<ObRawExpr*> &column_ref_remove_const_exprs,
                                ObIArray<ObRawExpr*> &query_ref_exprs,
                                ObIArray<ObRawExpr*> &query_ref_remove_const_exprs);

  int transform_cast_multiset_for_stmt(ObDMLStmt *&stmt, bool &is_happened);
  int transform_cast_multiset_for_expr(ObDMLStmt &stmt, ObRawExpr *&expr, bool &trans_happened);
  int add_constructor_to_multiset(ObDMLStmt &stmt,
                                  ObQueryRefRawExpr *multiset_expr,
                                  const pl::ObPLDataType &elem_type,
                                  bool& trans_happened);
  int add_column_conv_to_multiset(ObQueryRefRawExpr *multiset_expr,
                                  const pl::ObPLDataType &elem_type,
                                  bool& trans_happened);

  int transform_for_last_insert_id(ObDMLStmt *stmt, bool &trans_happened);
  int expand_for_last_insert_id(ObDMLStmt &stmt, ObIArray<ObRawExpr*> &exprs, bool &is_happended);
  int expand_last_insert_id_for_join(ObDMLStmt &stmt, JoinedTable *join_table, bool &is_happened);
  int remove_last_insert_id(ObRawExpr *&expr);
  int check_last_insert_id_removable(const ObRawExpr *expr, bool &is_removable);
  
  int expand_correlated_cte(ObDMLStmt *stmt, bool& trans_happened);
  int check_exec_param_correlated(const ObRawExpr *expr, bool &is_correlated);
  int check_is_correlated_cte(ObSelectStmt *stmt, ObIArray<ObSelectStmt *> &visited_cte, bool &is_correlated);
  int convert_join_preds_vector_to_scalar(JoinedTable &joined_table, bool &trans_happened);
  int preserve_order_for_fulltext_search(ObDMLStmt *stmt, bool& trans_happened);

  int flatten_conditions(ObDMLStmt *stmt, bool &trans_happened);
  int recursive_flatten_join_conditions(ObDMLStmt *stmt, TableItem *table, bool &trans_happened);
  int do_flatten_conditions(ObDMLStmt *stmt, ObIArray<ObRawExpr*> &conditions, bool &trans_happened);
  int expand_materialized_view(ObDMLStmt *stmt, bool &trans_happened);
  int preserve_order_for_pagination(ObDMLStmt *stmt, 
                                    bool &trans_happened);
  int check_stmt_need_preserve_order(ObDMLStmt *stmt, 
                                     ObIArray<ObSelectStmt*> &preserve_order_stmts, 
                                     bool &is_valid);

  int check_view_need_preserve_order(ObSelectStmt* stmt, 
                                     ObIArray<ObSelectStmt*> &preserve_order_stmts, 
                                     bool &need_preserve);

  int check_set_stmt_need_preserve_order(ObSelectStmt* stmt, 
                                         ObIArray<ObSelectStmt*> &preserve_order_stmts, 
                                         bool &need_preserve);

  int add_order_by_for_stmt(ObSelectStmt* stmt, bool &trans_happened);

  int get_rowkey_for_single_table(ObSelectStmt* stmt, 
                                  ObIArray<ObRawExpr*> &unique_keys, 
                                  bool &is_valid);
                                  
  int preserve_order_for_gby(ObDMLStmt *stmt, 
                             bool &trans_happened);
  int add_order_by_gby_for_stmt(ObSelectStmt* stmt, bool &trans_happened);

  int try_gen_straight_join_leading(ObDMLStmt *stmt, bool &trans_happened);
  int get_flattened_tables_of_pure_straight_join(ObDMLStmt* stmt, 
                                                 ObIArray<TableItem*> &flattened_tables);
  int check_pure_straight_join_table(TableItem* table_item, bool &is_pure_straight_join,
                                     ObIArray<TableItem*> &flattened_tables);
  int add_ordered_hint(ObDMLStmt* stmt, ObStmtHint &stmt_hint);
  int add_leading_hint_by_flattened_tables(ObDMLStmt* stmt, 
                                           ObStmtHint &stmt_hint, 
                                           ObIArray<TableItem*> &flattened_tables);
  int construct_leading_table(ObDMLStmt* stmt, 
                              ObIArray<TableItem*> &flattened_tables, 
                              ObLeadingTable &leading_table);
  int construct_leaf_leading_table(ObDMLStmt *stmt, TableItem *table, ObLeadingTable *&leading_table);
  int reset_view_base(ObDMLStmt *stmt);
  
private:
  DISALLOW_COPY_AND_ASSIGN(ObTransformPreProcess);
};

}
}

#endif /* OB_TRANSFORM_PRE_PROCESS_H_ */
