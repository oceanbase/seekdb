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

#ifndef OB_TRANSFORM_JOIN_ELIMINATION_H
#define OB_TRANSFORM_JOIN_ELIMINATION_H

#include "sql/ob_sql_context.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/rewrite/ob_transform_rule.h"

namespace oceanbase
{
namespace share {
namespace schema {
class ObForeignKeyInfo;
}
}
namespace sql
{
struct ObStmtMapInfo;
class ObDelUpdStmt;

class ObTransformJoinElimination : public ObTransformRule
{
  struct EliminationHelper {
    EliminationHelper()
      : count_(0),
        remain_(0)
    {
    }
    int push_back(TableItem *child, TableItem *parent, share::schema::ObForeignKeyInfo *info);
    int get_eliminable_group(TableItem *&child,
                             TableItem *&parent,
                             share::schema::ObForeignKeyInfo *&info,
                             bool &find);
    int is_table_in_child_items(const TableItem *target,
                                bool &find);
    int64_t get_remain() {return remain_;}
    int64_t count_;
    int64_t remain_;
    ObSEArray<TableItem *, 16> child_table_items_;
    ObSEArray<TableItem *, 16> parent_table_items_;
    ObSEArray<share::schema::ObForeignKeyInfo *, 16> foreign_key_infos_;
    ObSEArray<bool, 16> bitmap_;
  };

public:
  explicit ObTransformJoinElimination(ObTransformerCtx *ctx)
      : ObTransformRule(ctx, TransMethod::POST_ORDER, T_ELIMINATE_JOIN) {}

  virtual ~ObTransformJoinElimination() {}
  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;
  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
private:
  int construct_eliminated_tables(const ObDMLStmt *stmt,
                                  const ObIArray<TableItem *> &removed_tables,
                                  ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  int construct_eliminated_table(const ObDMLStmt *stmt,
                                 const TableItem *table,
                                 ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  /**
   * @brief 
   * check if a table can be eliminated
   *  update t1, t1 tt set t1.b = 1, tt.b = 2 where t1.a = tt.a;
   * update a column twice is illegal when the column (t1.b in this case) is a primary key/parition key/index key
   * for simplicity, we forbid the join eliminate when current sql is update
   * @param stmt 
   * @param table_id 
   * @param is_valid 
   * @return int 
   */
  int check_eliminate_delupd_table_valid(const ObDelUpdStmt *stmt, uint64_t table_id, bool &is_valid);
  int check_hint_valid(const ObDMLStmt &stmt,
                       const TableItem &table,
                       bool &is_valid);
  /**
   * @brief eliminate_join_self_key
   * self key join elimination scenarios include:
   * basic_table or generated_table in stmt from items
   * basic_table or generated_table in semi items
   * inner join tables in joined_table of stmt items
   * inner join tables in joined_table of semi items
   */
  int eliminate_join_self_foreign_key(ObDMLStmt *stmt,
                                      bool &trans_happened,
                                      ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  int check_eliminate_join_self_key_valid(ObDMLStmt *stmt,
                                          TableItem *source_table,
                                          TableItem *target_table,
                                          ObStmtMapInfo &stmt_map_info,
                                          bool is_from_base_table,
                                          bool &is_valid,
                                          EqualSets *equal_sets);
  /**
   * @brief eliminate_SKJ_in_from_base_table
   * eliminate basic_table or generated_table in stmt from items
   * existing self key lossless join
   */
  int eliminate_join_in_from_base_table(ObDMLStmt *stmt,
                                        bool &trans_happened,
                                        ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int eliminate_join_in_from_item(ObDMLStmt *stmt,
                                  const ObIArray<FromItem> &from_items,
                                  SemiInfo *semi_info,
                                  bool &trans_happened);

  int extract_candi_table(ObDMLStmt *stmt,
                          const ObIArray<FromItem> &from_items,
                          ObIArray<TableItem *> &candi_tables,
                          ObIArray<TableItem *> &candi_child_tables);

  int extract_candi_table(JoinedTable *table,
                          ObIArray<TableItem *> &candi_child_tables);

  /**
   * @brief eliminate_join_in_joined_table
   * eliminate inner join tables in joined_table
   * existing self key lossless join
   */
  int eliminate_join_in_joined_table(ObDMLStmt *stmt,
                                     bool &trans_happened,
                                     ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  /**
   * @brief eliminate_join_in_joined_table
   * If from_item is a joined table, attempt to eliminate it and update the joined_table structure
   */
  int eliminate_join_in_joined_table(ObDMLStmt *stmt,
                                     FromItem &from_item,
                                     ObIArray<JoinedTable*> &joined_tables,
                                     bool &trans_happened,
                                     ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  
  /**
   * @brief eliminate_join_in_joined_table
   * Try to eliminate joined table, if it is an inner join and a lossless self-join
   */
  int eliminate_join_in_joined_table(ObDMLStmt *stmt,
                                     TableItem *&table_item,
                                     ObIArray<TableItem *> &child_candi_tables,
                                     ObIArray<ObRawExpr *> &trans_conditions,
                                     bool &trans_happened,
                                     ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int eliminate_candi_tables(ObDMLStmt *stmt,
                             ObIArray<ObRawExpr*> &conds,
                             ObIArray<TableItem*> &candi_tables,
                             ObIArray<TableItem*> &child_candi_tables,
                             bool is_from_base_table,
                             bool &trans_happened,
                             ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  int check_on_null_side(ObDMLStmt *stmt,
                         uint64_t source_table_id,
                         uint64_t target_table_id,
                         bool is_from_base_table,
                         bool &is_on_null_side);

  int do_join_elimination_self_key(ObDMLStmt *stmt,
                                   TableItem *source_table,
                                   TableItem *target_table,
                                   bool is_from_base_table,
                                   bool &trans_happened,
                                   ObIArray<ObSEArray<TableItem *, 4>> &trans_tables,
                                   EqualSets *equal_sets = NULL);

  int do_join_elimination_foreign_key(ObDMLStmt *stmt,
                                      const TableItem *child_table,
                                      const TableItem *parent_table,
                                      const share::schema::ObForeignKeyInfo *foreign_key_info);

  int classify_joined_table(JoinedTable *joined_table,
                            ObIArray<JoinedTable *> &inner_join_tables,
                            ObIArray<TableItem *> &outer_join_tables,
                            ObIArray<TableItem *> &other_tables,
                            ObIArray<ObRawExpr *> &inner_join_conds);

  int rebuild_joined_tables(ObDMLStmt *stmt,
                            TableItem *&top_table,
                            ObIArray<JoinedTable*> &inner_join_tables,
                            ObIArray<TableItem*> &tables,
                            ObIArray<ObRawExpr*> &join_conds);

  int extract_lossless_join_columns(JoinedTable *joined_table,
                                    const ObIArray<int64_t> &output_map,
                                    ObIArray<ObRawExpr *> &source_exprs,
                                    ObIArray<ObRawExpr *> &target_exprs);
  int extract_child_conditions(ObDMLStmt *stmt,
                               TableItem *source_table,
                               ObIArray<ObRawExpr *> &join_conditions,
                               ObSqlBitSet<> &right_rel_ids);
  
  int adjust_relation_exprs(const ObSqlBitSet<8, int64_t> &right_rel_ids,
                            const ObIArray<ObRawExpr *> &join_conditions,
                            ObIArray<ObRawExpr *> &relation_exprs,
                            bool &is_valid);
  
  int compute_table_expr_ref_count(const ObSqlBitSet<> &rel_ids,
                                   const ObIArray<ObRawExpr *> &exprs,
                                   int64_t &total_ref_count);
  
  int compute_table_expr_ref_count(const ObSqlBitSet<> &rel_ids,
                                   const ObIArray<ObRawExprPointer> &exprs,
                                   int64_t &total_ref_count);
  
  int compute_table_expr_ref_count(const ObSqlBitSet<> &rel_ids,
                                   const ObRawExpr* exprs,
                                   int64_t &total_ref_count);

  int extract_equal_join_columns(const ObIArray<ObRawExpr *> &join_conds,
                                 const TableItem *source_table,
                                 const TableItem *target_table,
                                 ObIArray<ObRawExpr *> &source_exprs,
                                 ObIArray<ObRawExpr *> &target_exprs,
                                 ObIArray<int64_t> *unused_conds);

  int adjust_table_items(ObDMLStmt *stmt,
                         TableItem *source_table,
                         TableItem *target_table,
                         ObStmtMapInfo &info);

  int reverse_select_items_map(const ObSelectStmt *source_stmt,
                               const ObSelectStmt *target_stmt,
                               const ObIArray<int64_t> &column_map,
                               ObIArray<int64_t> &reverse_column_map);

  int create_missing_select_items(ObSelectStmt *source_stmt,
                                  ObSelectStmt *target_stmt,
                                  ObIArray<int64_t> &column_map,
                                  const ObIArray<int64_t> &table_map);
  /**
   * @brief trans_table_item
   * Rewrite the structure associated with target_table to be associated with source_table
   * Remove target table from from item
   * Remove target table from partition information
   * Reconstruct rel_idx
   * @param stmt
   * @param source_table
   * @param target_table
   * @return
   */
  int trans_table_item(ObDMLStmt *stmt,
                       const TableItem *source_table,
                       const TableItem *target_table);

  int eliminate_outer_join(ObIArray<ObParentDMLStmt> &parent_stmts,
                           ObDMLStmt *stmt,
                           bool &trans_happened,
                           ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int eliminate_outer_join_in_joined_table(ObDMLStmt *stmt,
                                           TableItem *&table_item,
                                           const bool is_non_sens_dul_vals,
                                           const bool is_root_table,
                                           ObIArray<uint64_t> &removed_ids,
                                           ObIArray<ObRawExprPointer> &relation_exprs,
                                           bool &trans_happen,
                                           ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int get_eliminable_tables(const ObDMLStmt *stmt,
                            const ObIArray<ObRawExpr *> &conds,
                            const ObIArray<TableItem *> &candi_tables,
                            const ObIArray<TableItem *> &candi_child_tables,
                            EliminationHelper &helper);

  /**
   * @brief check_transform_validity_foreign_key
   * Check if source_table and target_table can perform foreign key join elimination.
   * For any two tables with a foreign key constraint, the conditions for foreign key join elimination are:
   *    1. Only involves the primary key columns of the parent table
   *    2. The join condition contains a one-to-one corresponding equality condition for the foreign key (i.e., ensuring the join is unique)
   *
   *  Example t1(c1, c2, c3, primary key (c1, c2))
   *       t2(c1, c2, c3, c4,
   *          foreign key (c1, c2) references t1(c1, c2),
   *          foreign key (c3, c4) references t1(c1, c2))
   *  Then 1. select * from t1, t2 where t1.c1 = t2.c1 and t1.c2 = t2.c2;
   *        \-> Cannot be eliminated, references non-primary key columns of the parent table, need to read the parent table to get the value of that column.
   *
   *     2. select t1.c1, t2.* from t1, t2 where t1.c1 = t2.c1;
   *        |   Cannot be eliminated, the join condition does not contain a one-to-one corresponding equality condition for the foreign key,
   *        \-> One column in t2 may connect to multiple rows in t1, need to read t1 for verification.
   *
   *     3. select t1.c1, t2.* from t1, t2 where t1.c1 = t2.c1 and t1.c2 and t2.c2;
   *        |   Can be eliminated, the join condition contains a one-to-one corresponding equality condition for the foreign key, ensuring that
   *        |   each column in t2 can connect to at most one row in t1; and only references the primary key columns of the parent table, can
   *        |   use the corresponding foreign key columns in the child table instead.
   *        |   This query can be rewritten as
   *        \-> select t2.c1, t2.* from t2 where t2.c1 is not null and t2. c2 is not null;
   */
  int check_transform_validity_foreign_key(const ObDMLStmt *stmt,
                                           const ObIArray<ObRawExpr *> &join_conds,
                                           const TableItem *source_table,
                                           const TableItem *target_table,
                                           bool &can_be_eliminated,
                                           bool &is_first_table_parent,
                                           share::schema::ObForeignKeyInfo *&foreign_key_info);

  int check_transform_validity_foreign_key(const ObDMLStmt *target_stmt,
                                           const TableItem *source_table,
                                           const TableItem *target_table,
                                           const ObIArray<ObRawExpr*> &source_exprs,
                                           const ObIArray<ObRawExpr*> &target_exprs,
                                           bool &can_be_eliminated,
                                           share::schema::ObForeignKeyInfo *&foreign_key_info);

  int extract_equal_join_columns(const ObIArray<ObRawExpr *> &join_conds,
                                 const uint64_t source_tid,
                                 const uint64_t upper_target_tid,
                                 const ObIArray<ObRawExpr*> &upper_column_exprs,
                                 const ObIArray<ObRawExpr*> &child_column_exprs,
                                 ObIArray<ObRawExpr*> &source_exprs,
                                 ObIArray<ObRawExpr*> &target_exprs,
                                 ObIArray<int64_t> *unused_conds);

  /**
   * @brief check_transform_validity_outer_join
   * Check if the joined table can perform outer join elimination
   * For outer join elimination rewrite, the conditions are as follows:
   *    1. The join columns of the right table in the join condition must be unique
   *    2. It needs to be of left outer join type
   *    3. Columns of the right table do not exist in select item, groupby, orderby, having.
   * Example: t1(a int, b int, c int);
   *          t2(a int, b int, c int, primary key(a,b))
   *    1. select t1.* from t1 inner join on t2 on t1.a=t2.a and t1.b=t2,b;
   *       \->Cannot eliminate, not a left join type
   *    2. select t1.* from t1 left join on t2 on t1.a=t2.a
   *       \->Cannot eliminate, join condition is not unique
   *    3. select t1.*, t2.a from t1 left join t2 on t1.a=t2.a and t1.b=t2.b
   *       \->Cannot rewrite, because select items contain columns of the right table.
   */
  int check_transform_validity_outer_join(ObDMLStmt *stmt,
                                          JoinedTable *joined_table,
                                          const bool is_non_sens_dul_vals,
                                          ObIArray<ObRawExprPointer> &relation_exprs,
                                          bool &is_valid);

  int check_has_semi_join_conditions(ObDMLStmt *stmt,
                                     const ObSqlBitSet<8, int64_t> &rel_ids,
                                     bool &is_valid);


  /**
   *  @brief check_all_column_primary_key
   *  Check if all columns in stmt belonging to table_id are in the parent table columns of foreign key info
   */
  int check_all_column_primary_key(const ObDMLStmt *stmt,
                                   const uint64_t table_id,
                                   const share::schema::ObForeignKeyInfo *info,
                                   bool &all_primary_key);

  /**
   * @brief trans_column_items_foreign_key
   * If col0 \in child, col1 \in parent, and there exists a foreign key relationship between them,
   * then delete col1 and change all pointers pointing to col1 in expressions to point to col0.
   *
   * In foreign key join elimination, when a column of the parent table appears, there must be a corresponding column in the child table,
   * so there is no need to handle the case where col appears alone in the parent table.
   */
  int trans_column_items_foreign_key(ObDMLStmt *stmt,
                                     const TableItem *child_table,
                                     const TableItem *parent_table,
                                     const share::schema::ObForeignKeyInfo *foreign_key_info);

  /**
   * @brief get_child_column_id_by_parent_column_id
   * Get child table column id corresponding to parent table column id from foreign key info
   */
  int get_child_column_id_by_parent_column_id(const share::schema::ObForeignKeyInfo *info,
                                              const uint64_t parent_column_id,
                                              uint64_t &child_column_id);
  //zhenling.zzg enhance semi anti self join elimination
  /**
   * @brief eliminate_semi_join_self_key
   * Eliminate semi and anti join of self-joins
   * Rules for eliminating semi join:
   * 1、condition must have at least one simple join predicate (equality predicate on the same column);
   * 2、simple join predicates in join_expr must have a uniqueness constraint; (or if join_expr consists only of simple joins connected by AND without other predicates, a uniqueness constraint is not required)
   * 3、if there is a partition hint, the partitions referenced by the inner table must include the partitions referenced by the outer table;
   * Rules for eliminating anti join:
   * 1、the AND connection tree in join_expr must consist only of simple join predicates (equality predicates on the same column), and the filter predicates of the inner table must be a simple AND tree;
   * 2、simple join predicates in join_expr must have a uniqueness constraint; (or if join_expr consists only of simple joins connected by AND and the inner table has no filter predicates, a uniqueness constraint is not required)
   * 3、if there is a partition hint, the partitions referenced by the inner table must include the partitions referenced by the outer table;
   */
  //int eliminate_semi_join_self_key(ObDMLStmt *stmt,
  //                                bool &trans_happened);

  int eliminate_semi_join_self_foreign_key(ObDMLStmt *stmt,
                                           bool &trans_happened,
                                           ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  /**
   * @brief 
   *  eliminate left outer join when the join condition is always false:
   *    select t1.a, t2.a from t1 left join t2 on false;
   *  can be transformed into:
   *    select t1.a, null from t1;
   * @param stmt 
   * @param trans_happened 
   * @return int 
   */
  int eliminate_left_outer_join(ObDMLStmt *stmt, bool &trans_happened,
                                ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  int do_eliminate_left_outer_join_rec(ObDMLStmt *stmt,
                                       TableItem *&table,
                                       bool &trans_happened,
                                       ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  int do_eliminate_left_outer_join(ObDMLStmt *stmt,
                                   TableItem *&table,
                                   ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);
  int get_table_items_and_ids(ObDMLStmt *stmt,
                              TableItem *table,
                              ObIArray<uint64_t> &table_ids,
                              ObIArray<TableItem *> &table_items);

  int left_join_can_be_eliminated(ObDMLStmt *stmt, TableItem *table, bool &can_be_eliminated);

  int eliminate_semi_join_self_key(ObDMLStmt *stmt,
                                   SemiInfo *semi_info,
                                   ObIArray<ObRawExpr*> &conds,
                                   bool &trans_happened,
                                   bool &has_removed_semi_info,
                                   ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int eliminate_semi_join_foreign_key(ObDMLStmt *stmt,
                                      SemiInfo *semi_info,
                                      bool &trans_happened,
                                      bool &has_removed_semi_info,
                                      ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int try_remove_semi_info(ObDMLStmt *stmt, SemiInfo *semi_info);

  int eliminate_semi_right_child_table(ObDMLStmt *stmt,
                                       SemiInfo *semi_info,
                                       ObIArray<TableItem*> &right_tables,
                                       ObIArray<ObRawExpr*> &source_col_exprs,
                                       ObIArray<ObRawExpr*> &target_col_exprs,
                                       ObIArray<ObSEArray<TableItem *, 4>> &trans_tables);

  int adjust_source_table(ObDMLStmt *source_stmt,
                          ObDMLStmt *target_stmt,
                          const TableItem *source_table,
                          const TableItem *target_table,
                          const ObIArray<int64_t> *output_map,
                          ObIArray<ObRawExpr*> &source_col_exprs,
                          ObIArray<ObRawExpr*> &target_col_exprs);

  int convert_target_table_column_exprs(ObDMLStmt *source_stmt,
                                        ObDMLStmt *target_stmt,
                                        const ObIArray<TableItem*> &source_tables,
                                        const ObIArray<TableItem*> &target_tables,
                                        ObIArray<ObStmtMapInfo> &stmt_map_infos,
                                        ObIArray<ObRawExpr*> &source_col_exprs,
                                        ObIArray<ObRawExpr*> &target_col_exprs);

  int convert_target_table_column_exprs(ObDMLStmt *source_stmt,
                              ObDMLStmt *target_stmt,
                              const ObIArray<TableItem*> &child_tables,
                              const ObIArray<TableItem*> &parent_tables,
                              const ObIArray<share::schema::ObForeignKeyInfo*> &foreign_key_infos,
                              ObIArray<ObRawExpr*> &source_col_exprs,
                              ObIArray<ObRawExpr*> &target_col_exprs);

  /**
   * @brief check_transform_validity_semi_self_key
   * Check if the semi join contained in semi_info can be eliminated
   * @param stmt 
   * @param semi_join The semi join to be checked
   * @param stmt_map_infos The mapping relationship of generated_table related to semi_join
   * @param rel_map_info The mapping relationship of the left and right table sets of semi_join
   * @param can_be_eliminated Whether it can be eliminated
   */
  int check_transform_validity_semi_self_key(ObDMLStmt *stmt,
                                             SemiInfo *semi_info,
                                             ObIArray<ObRawExpr*> &candi_conds,
                                             TableItem *&source_table,
                                             TableItem *&right_table,
                                             ObStmtMapInfo &stmt_map_info);

  int check_transform_validity_semi_self_key(ObDMLStmt *stmt,
                                             SemiInfo *semi_info,
                                             ObIArray<ObRawExpr*> &candi_conds,
                                             ObIArray<TableItem*> &left_tables,
                                             ObIArray<TableItem*> &right_tables,
                                             ObIArray<ObStmtMapInfo> &stmt_map_infos,
                                             ObDMLStmt *&target_stmt);

  int is_table_column_used_in_subquery(const ObSelectStmt &stmt,
                                       const uint64_t table_id,
                                       bool &used);

  /**
   * @brief check_semi_join_condition
   * check the semi join condition
   * @param stmt
   * @param semi_info
   * @param source_tables semi_info->left_tables_
   * @param target_tables semi_info->right_tables_
   * @param stmt_map_info generate table mapping relationship
   * @param rel_map_info source tables, target tables mapping relationship
   * @param source_exprs return columns from source tables participating in semi join
   * @param target_exprs return columns from target tables participating in semi join
   * @param is_simple_join_condition whether the semi join condition consists only of equality on the same columns
   * @param target_tables_have_filter whether there are filter predicates on target tables
   * if target tables have multiple tables and internal join predicates, those join predicates are considered as filter predicates after Cartesian product
   * @param is_simple_filter if there is a filter on the target table, return whether all filters are simple predicates connected by AND
   */
  // source_table and target_table come from the same stmt
  int check_semi_join_condition(ObDMLStmt *stmt,
                                ObIArray<ObRawExpr*> &semi_conds,
                                const TableItem *source_table,
                                const TableItem *target_table,
                                const ObStmtMapInfo &stmt_map_info,
                                ObIArray<ObRawExpr*> &source_exprs,
                                ObIArray<ObRawExpr*> &target_exprs,
                                bool &is_simple_join_condition,
                                bool &target_tables_have_filter,
                                bool &is_simple_filter);

  // source_table comes from stmt, target_table comes from child select stmt target_stmt
  int check_semi_join_condition(ObDMLStmt *stmt,
                                ObSelectStmt *target_stmt,
                                ObIArray<ObRawExpr *> &semi_conds,
                                ObIArray<ObSqlBitSet<>> &select_relids,
                                const TableItem *source_table,
                                const TableItem *target_table,
                                const ObStmtMapInfo &stmt_map_info,
                                ObIArray<ObRawExpr *> &source_exprs,
                                ObIArray<ObRawExpr *> &target_exprs,
                                bool &is_simple_join_condition,
                                bool &target_tables_have_filter,
                                bool &is_simple_filter);

  int get_epxrs_rel_ids_in_child_stmt(ObDMLStmt *stmt,
                                      ObSelectStmt *child_stmt,
                                      ObIArray<ObRawExpr *> &cond_exprs,
                                      uint64_t table_id,
                                      ObIArray<ObSqlBitSet<>> &rel_ids);

  /**
   * @is_equal_column
   * does source_col have a mapping relationship with target_col
   * @param is_equal whether there is a mapping relationship
   * @param is_reverse whether it is a reverse match mapping relationship
   */
  int is_equal_column(const ObIArray<TableItem*> &source_tables,
                      const ObIArray<TableItem*> &target_tables,
                      const ObIArray<ObStmtMapInfo> &stmt_map_infos,
                      const ObIArray<int64_t> &rel_map_info,
                      const ObColumnRefRawExpr *source_col,
                      const ObColumnRefRawExpr *target_col,
                      bool &is_equal,
                      bool &is_reverse);

  int is_equal_column(const TableItem *source_table,
                      const TableItem *target_table,
                      const ObIArray<int64_t> &output_map,
                      uint64_t source_col_id,
                      uint64_t target_col_id,
                      bool &is_equal);
  
  /**
   * @brief do_elimination_semi_join_self_key
   * predicate processing after eliminating semi join
   * the handling of anti join is quite special
   * need to rewrite predicates first, then delete inner table and update table hash idx
   */
  int do_elimination_semi_join_self_key(ObDMLStmt *stmt,
                                        SemiInfo *semi_info,
                                        const ObIArray<TableItem*> &source_tables,
                                        const ObIArray<TableItem*> &target_tables,
                                        ObIArray<ObStmtMapInfo> &stmt_map_infos,
                                        const ObIArray<int64_t> &rel_map_info);
  
   /**
   * @brief trans_semi_table_item
   * Rewrite the structure associated with target_table to be associated with source_table
   * Remove target table from table items
   * Remove target table from partition information
   * Reconstruct rel_idx
   * @param stmt
   * @param source_table
   * @param target_table
   * @return
   */
  int trans_semi_table_item(ObDMLStmt *stmt,
                            const TableItem *target_table);

  /**
   * @brief trans_semi_condition_exprs
   * Eliminate anti join predicate rewriting is different from eliminating semi join, it needs to be done before adjust table item
   * because the filter predicate expressions on the target table need to take LNNVL, while the filter predicates on the source table
   * do not need to be processed, so if adjust table item is done afterwards, it will be unable to distinguish between source table
   * and target table
   */
  int trans_semi_condition_exprs(ObDMLStmt *stmt,
                                 SemiInfo *semi_info);

  /**
   * @brief eliminate_semi_join_foreign_key
   * Eliminate semi, anti join of foreign key primary key join
   * Rules for eliminating semi join:
   * 1、Primary foreign key join;
   * 2、Outer table is the table where the foreign key is located;
   * 3、Inner table has no partition hint
   * 4、Inner table has no non-primary key filter predicates;
   * 5、When the primary foreign key index is in NOVALIDATE + norely status, this constraint should not be used for primary foreign key join elimination;
   * Rules for eliminating semi join:
   * 1、Primary foreign key join;
   * 2、Outer table is the table where the foreign key is located;
   * 3、Inner table has no partition hint
   * 4、Inner table has no filter predicates;
   * 5、When the primary foreign key index is in NOVALIDATE + norely status, this constraint should not be used for primary foreign key join elimination;
   */
  //int eliminate_semi_join_foreign_key(ObDMLStmt *stmt,
  //                                    bool &trans_happened);

  /**
   * @brief check_transform_validity_semi_foreign_key
   * Check if the semi join contained in semi_info can be eliminated
   * @param stmt 
   * @param semi_join The semi join to be checked
   * @param can_be_eliminated Whether it can be eliminated
   */
  int check_transform_validity_semi_foreign_key(ObDMLStmt *stmt,
                                                SemiInfo *semi_info,
                                                TableItem *right_table,
                                                TableItem *&left_table,
                                                share::schema::ObForeignKeyInfo *&foreign_key_info);
                                                

  int check_transform_validity_semi_foreign_key(ObDMLStmt *stmt,
                                  SemiInfo *semi_info,
                                  TableItem *right_table,
                                  ObIArray<TableItem*> &left_tables,
                                  ObIArray<TableItem*> &right_tables,
                                  ObIArray<share::schema::ObForeignKeyInfo*> &foreign_key_infos);

  int get_column_exprs(ObDMLStmt &stmt,
                       ObSelectStmt &child_stmt,
                       const uint64_t upper_table_id,
                       const uint64_t child_table_id,
                       ObIArray<ObRawExpr*> &upper_columns,
                       ObIArray<ObRawExpr*> &child_columns);


  // functions below trans self equal conditions to not null
  /**
   * Rewrite equality condition on the same column in stmt
   * If it is not null col, delete directly; if it is nullable col, replace with IS_NOT_NULL
   */
  int trans_self_equal_conds(ObDMLStmt *stmt);
  int trans_self_equal_conds(ObDMLStmt *stmt, ObIArray<ObRawExpr*> &cond_exprs);
  // Check if col is nullable, if so, then add is_not_null expression
  int add_is_not_null_if_needed(ObDMLStmt *stmt,
                                ObIArray<ObColumnRefRawExpr *> &col_exprs,
                                ObIArray<ObRawExpr*> &cond_exprs);
  // For expr if column does not have a NOT NULL constraint, then replace with IS NOT NULL;
  // Otherwise replace with TRUE, for OR, AND expressions, it will recursively rewrite sub-expressions
  int recursive_trans_equal_join_condition(ObDMLStmt *stmt, ObRawExpr *expr);
  int do_trans_equal_join_condition(ObDMLStmt *stmt,
                                    ObRawExpr *expr,
                                    bool &has_trans,
                                    ObRawExpr* &new_expr);

};

} //namespace sql
} //namespace oceanbase

#endif // OB_TRANSFORM_JOIN_ELIMINATION_H
