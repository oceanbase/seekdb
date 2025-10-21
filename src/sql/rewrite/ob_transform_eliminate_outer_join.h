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

#ifndef _OB_TRANSFORM_ELIMINATE_OUTER_JOIN_H
#define _OB_TRANSFORM_ELIMINATE_OUTER_JOIN_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{

class ObTransformEliminateOuterJoin : public ObTransformRule
{
public:
  explicit ObTransformEliminateOuterJoin(ObTransformerCtx *ctx)
  : ObTransformRule(ctx, TransMethod::POST_ORDER, T_OUTER_TO_INNER) {}

  virtual ~ObTransformEliminateOuterJoin() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
private:

  /**
   * @brief eliminate_outer_join
   * respectively eliminate outer joins in the from list, semi from list
   * directly eliminate inner joins
   * elimination conditions for left outer joins:
   *  1、the right table has null-rejecting predicates in where conditions;
   *  2、or elimination based on primary-foreign key join
   */
  int eliminate_outer_join(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                           ObDMLStmt *&stmt,
                           bool &trans_happened);

  /**
   * @brief recursive_eliminate_outer_join_in_joined_table
   * If table_item is a joined_table, and it is a left join, full join, inner join
   * First eliminate the top-level joined_table, then recursively eliminate the left and right tables, otherwise do not eliminate
   * At the same time, if elimination is successful, the corresponding join_table will be split into left and right tables
   * into from_item_list
   * @param select_stmt
   * @param cur_table_item The table item to be processed
   * @param from_item_list The last top-level tbale_item is placed into from_item
   * @param joined_table_list Collect the joined_table obtained after processing
   * @param conditions The set of expressions for searching empty rejections
   * @param shou_move_to_from_list If elimination is successful, whether the left and right tables can be split into from_item_list
   * Elimination must be successful from the top level to the current level to be able to split
   * @param trans_happened Whether rewriting has occurred
   */
  int recursive_eliminate_outer_join_in_table_item(ObDMLStmt *stmt,
                                                  TableItem *cur_table_item,
                                                  common::ObIArray<FromItem> &from_item_list,
                                                  common::ObIArray<JoinedTable*> &joined_table_list,
                                                  ObIArray<ObRawExpr*> &conditions,
                                                  bool should_move_to_from_list,
                                                  bool &trans_happened);

  /**
   * @brief is_outer_joined_table_type
   * Determine if it is a joined table, and whether it is left join, full join, inner join
   * If not, determine based on should_move_to_from_list
   * Whether to place it in from_item_list, joined_table_list
   */
  int is_outer_joined_table_type(ObDMLStmt *stmt,
                                  TableItem *cur_table_item,
                                  common::ObIArray<FromItem> &from_item_list,
                                  common::ObIArray<JoinedTable*> &joined_table_list,
                                  bool should_move_to_from_list,
                                  bool &is_my_joined_table_type);

  /**
   * @brief do_eliminate_outer_join
   * Execute outer join elimination
   * inner join is directly eliminated
   * full join first eliminates the left join, then converts the right join to a left join and eliminates it
   * Finally, if the outer join is completely eliminated to inner join, process from_list and conditions
   */
  int do_eliminate_outer_join(ObDMLStmt *stmt,
                              JoinedTable *cur_joined_table,
                              common::ObIArray<FromItem> &from_item_list,
                              common::ObIArray<JoinedTable*> &joined_table_list,
                              ObIArray<ObRawExpr*> &conditions,
                              bool should_move_to_from_list,
                              bool &trans_happened);

  /**
   * @brief can_be_eliminated
   * Split the left outer join elimination judgment logic into two cases
   * 1.Can the outer join be eliminated using the null reject predicate of the where condition
   * 2.Can the outer join be eliminated using the primary-foreign key join
  */
  int can_be_eliminated(ObDMLStmt *stmt,
                        JoinedTable *joined_table,
                        ObIArray<ObRawExpr*> &conditions,
                        bool &can_eliminate);

  /**
   * @brief can_be_eliminated_with_null_reject
   * If the right table has a null-reject predicate in conditions, the left join can be eliminated
   * @param stmt select stmt
   * @param joined_table table items to be processed
   * @param conditions set of expressions to be processed
   * @param has_null_reject whether the right table has a null-reject predicate in conditions
   */
  int can_be_eliminated_with_null_reject(ObDMLStmt *stmt,
                                        JoinedTable *joined_table,
                                        ObIArray<ObRawExpr*> &conditions,
                                        bool &has_null_reject);

  /**
   * @brief can_be_eliminated_with_foreign_primary_join
   * Determine if a left outer join can be eliminated based on foreign key joins, requirements:
   * 1、The join condition is a foreign key join
   * 2、The table containing the foreign key is the left table
   * 3、The foreign key has a NOT NULL constraint or a NOT NULL predicate
   * 4、The foreign key is in CASCADE or CHECK status
   * 5、If either the left table or the right table has a part hint, disable the rewrite
   * 6、The ON condition only contains foreign key equality join conditions (i.e., ensuring the right table's primary key is not lost)
   * eg:
   * t1(c1 int, c2 int, primary key(c1))
   * t2(c1 int, c2 int not null,
   *          foreign key(c1) references t1(c1),
   *          foreign key(c2) references t1(c1))
   * 1.select * from t2 left join t1 on t1.c1=t2.c1;
   *  \->Cannot be eliminated, t2(c1) does not have a NOT NULL constraint
   * 2.select * from t2 left join t1 on t1.c1=t2.c2;
   *  \->Can be rewritten as select * from t1,t2 where t1.c1=t2.c2;
   * @param  joined_table The joined table to determine if the left outer join can be eliminated
   * @param can_be_eliminated Returns whether the left outer join can be eliminated
   */
  int can_be_eliminated_with_foreign_primary_join(ObDMLStmt *stmt,
                                                  JoinedTable *joined_table,
                                                  ObIArray<ObRawExpr*> &conditions,
                                                  bool &can_eliminate);

  /**
   * @brief can_be_eliminated_with_null_side_column_in_aggr
   * when the column on the null side of the outer join appears in the aggregate function of scalar group by, eliminate outer join
   * select count(B.c2) as xx from t1 A, t2 B where A.c1 = B.c2(+);
   *   =>
   * select count(B.c2) as xx from t1 A, t2 B where A.c1 = B.c2;
   */
  int can_be_eliminated_with_null_side_column_in_aggr(ObDMLStmt *stmt,
                                                      JoinedTable *joined_table,
                                                      bool &can_eliminate);

  /**
   * @brief all_columns_is_not_null
   * are all columns in col_exprs not null
   */
  int is_all_columns_not_null(ObDMLStmt *stmt,
                              const ObIArray<const ObRawExpr *> &col_exprs,
                              ObIArray<ObRawExpr*> &conditions,
                              bool &is_not_null);

  /**
   * @brief is_simple_join_condition
   * Determine if the join condition is a simple AND connection of column equalities
   */
  int is_simple_join_condition(const ObIArray<ObRawExpr *> &join_condition,
                              const TableItem* left_table,
                              const TableItem* right_table,
                              bool &is_simple);

  /**
   * @brief extract_columns
   * Find all columns col in expr that meet the following conditions
   *    col's table_id \in rel_ids
   */
  int extract_columns(const ObRawExpr *expr,
                      const ObSqlBitSet<> &rel_ids,
                      common::ObIArray<const ObRawExpr *> &col_exprs);

  /**
   * @brief extract_columns
   * Find all columns col in exprs that meet the following conditions
   *    col's table_id \in rel_ids
   */
  int extract_columns_from_join_conditions(const ObIArray<ObRawExpr *> &exprs,
                                          const ObSqlBitSet<> &rel_ids,
                                          common::ObIArray<const ObRawExpr *> &col_exprs);

  /**
   * @brief check_expr_ref_column_all_in_aggr
   * check column reference only in aggr
   * such as:
   *   count(a + b + 1) => true
   *   count(a + 1) + b => false
   */
  int check_expr_ref_column_all_in_aggr(const ObRawExpr *expr, bool &is_in);
  
};
}//namespace sql
}//namespace oceanbase

#endif
