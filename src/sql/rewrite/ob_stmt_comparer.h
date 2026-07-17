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

#ifndef OB_STMT_COMPARER_H
#define OB_STMT_COMPARER_H

#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{

// NOTE (link.zt) remember to de-construct the struct
/**
 * @brief The ObStmtMapInfo struct
 * Records the mapping relationship of semantically equal items between two stmts
 */
enum QueryRelation
{
  QUERY_LEFT_SUBSET,
  QUERY_RIGHT_SUBSET,
  QUERY_EQUAL,
  QUERY_UNCOMPARABLE
};

struct ObStmtMapInfo {
  common::ObSEArray<common::ObSEArray<int64_t, 4>, 4> view_select_item_map_;
  common::ObSEArray<ObExprConstraint, 4> expr_cons_map_;
  common::ObSEArray<ObPCConstParamInfo, 4> const_param_map_;
  common::ObSEArray<ObPCParamEqualInfo, 4> equal_param_map_;
  common::ObSEArray<int64_t, 4> table_map_;
  common::ObSEArray<int64_t, 4> from_map_;
  common::ObSEArray<int64_t, 4> semi_info_map_;
  common::ObSEArray<int64_t, 4> cond_map_;
  common::ObSEArray<int64_t, 4> group_map_;
  common::ObSEArray<int64_t, 4> having_map_;
  common::ObSEArray<int64_t, 4> select_item_map_;
  bool is_table_equal_;
  bool is_from_equal_;
  bool is_semi_info_equal_;
  bool is_cond_equal_;
  bool is_group_equal_;
  bool is_having_equal_;
  bool is_order_equal_;
  bool is_select_item_equal_;
  bool is_distinct_equal_;
  bool is_qualify_filter_equal_;
  bool left_can_be_replaced_; // used for mv rewrite
  // If from item is generated table, need to record ref query's select item map relationship
  // If it is a set stmt, each set query corresponding mapping relationship is also recorded in view_select_item_map_
  ObStmtMapInfo()
    :is_table_equal_(false),
    is_from_equal_(false),
    is_semi_info_equal_(false),
    is_cond_equal_(false),
    is_group_equal_(false),
    is_having_equal_(false),
    is_order_equal_(false),
    is_select_item_equal_(false),
    is_distinct_equal_(false),
    is_qualify_filter_equal_(false),
    left_can_be_replaced_(true)
    {}

  void reset();
  int assign(const ObStmtMapInfo& other);

  TO_STRING_KV(K_(table_map),
               K_(from_map),
               K_(semi_info_map),
               K_(cond_map),
               K_(group_map),
               K_(having_map),
               K_(select_item_map),
               K_(equal_param_map),
               K_(view_select_item_map),
               K_(is_order_equal),
               K_(is_distinct_equal),
               K_(left_can_be_replaced));
};

struct StmtCompareHelper {
  StmtCompareHelper()
  :stmt_map_infos_(),
  similar_stmts_(),
  hint_force_stmt_set_(),
  stmt_(NULL)
  {}

  virtual ~StmtCompareHelper(){}
  static int alloc_compare_helper(ObIAllocator &allocator, StmtCompareHelper* &helper);

  TO_STRING_KV(
    K_(stmt_map_infos),
    K_(similar_stmts),
    K_(hint_force_stmt_set),
    K_(stmt)
  );

  ObSEArray<ObStmtMapInfo, 8> stmt_map_infos_;
  ObSEArray<ObSelectStmt*, 8> similar_stmts_;
  QbNameList hint_force_stmt_set_;
  ObSelectStmt *stmt_;

private:
  DISABLE_COPY_ASSIGN(StmtCompareHelper);
};

// NOTE (link.zt) remember to de-construct the struct
struct ObStmtCompareContext : ObExprEqualCheckContext
{
  ObStmtCompareContext() :
    ObExprEqualCheckContext(),
    calculable_items_(NULL),
    inner_(NULL),
    outer_(NULL),
    map_info_(),
    equal_param_info_(),
    is_in_same_stmt_(true),
    ora_numeric_cmp_for_grouping_items_(false)
  {
    init_override_params();
  }
  ObStmtCompareContext(bool need_check_deterministic) :
    ObExprEqualCheckContext(need_check_deterministic),
    calculable_items_(NULL),
    inner_(NULL),
    outer_(NULL),
    map_info_(),
    equal_param_info_(),
    is_in_same_stmt_(true),
    ora_numeric_cmp_for_grouping_items_(false)
  {
    init_override_params();
  }
  // for common expression extraction
  ObStmtCompareContext(const ObIArray<ObHiddenColumnItem> *calculable_items,
                       bool need_check_deterministic = false,
                       bool is_in_same_stmt = true) :
    ObExprEqualCheckContext(need_check_deterministic),
    calculable_items_(calculable_items),
    inner_(NULL),
    outer_(NULL),
    map_info_(),
    equal_param_info_(),
    is_in_same_stmt_(is_in_same_stmt),
    ora_numeric_cmp_for_grouping_items_(false)
  {
    init_override_params();
  }
  ObStmtCompareContext(const ObDMLStmt *inner,
                       const ObDMLStmt *outer,
                       const ObStmtMapInfo &map_info,
                       const ObIArray<ObHiddenColumnItem> *calculable_items,
                       bool need_check_deterministic = false,
                       bool is_in_same_stmt = true) :
    ObExprEqualCheckContext(need_check_deterministic),
    calculable_items_(calculable_items),
    inner_(inner),
    outer_(outer),
    map_info_(map_info),
    equal_param_info_(),
    is_in_same_stmt_(is_in_same_stmt),
    ora_numeric_cmp_for_grouping_items_(false)
  {
    init_override_params();
  }
  inline void init_override_params()
  {
    override_column_compare_ = true;
    override_const_compare_ = true;
    override_query_compare_ = true;
    override_set_op_compare_ = true;
  }
  virtual ~ObStmtCompareContext() {}
  
  // since the init() func only initialize the class members, 
  // it is better to use constructor
  // for common expression extraction
  void init(const ObIArray<ObHiddenColumnItem> *calculable_items);

  // for win_magic rewrite
  int init(const ObDMLStmt *inner,
           const ObDMLStmt *outer,
           const ObStmtMapInfo &map_info,
           const ObIArray<ObHiddenColumnItem> *calculable_items);
  
  int get_table_map_idx(uint64_t l_table_id, uint64_t r_table_id);
  // Used to compare whether two expr are structurally symmetric
  // The difference lies only in the table id of some columns
  bool compare_column(const ObColumnRefRawExpr &inner, const ObColumnRefRawExpr &outer) override;

  bool compare_const(const ObConstRawExpr &inner, const ObConstRawExpr &outer) override;

  bool compare_query(const ObQueryRefRawExpr &first, const ObQueryRefRawExpr &second) override;

  int get_calc_expr(const int64_t param_idx, const ObRawExpr *&expr);

  int is_pre_calc_item(const ObConstRawExpr &const_expr, bool &is_calc);

  bool compare_set_op_expr(const ObSetOpRawExpr& left, const ObSetOpRawExpr& right) override;

  const ObIArray<ObHiddenColumnItem> *calculable_items_; // from query context
  // first is the table id from the inner stmt
  // second is the table id from the outer stmt
  const ObDMLStmt *inner_;
  const ObDMLStmt *outer_;
  ObStmtMapInfo map_info_;
  common::ObSEArray<ObPCParamEqualInfo, 4> equal_param_info_;
  common::ObSEArray<ObExprConstraint, 4> expr_cons_info_;
  common::ObSEArray<ObPCConstParamInfo, 4> const_param_info_;
  bool is_in_same_stmt_; // only if the two stmts are in the same parent stmt, can we compare table id and column id directly
  bool ora_numeric_cmp_for_grouping_items_;

private:
  DISABLE_COPY_ASSIGN(ObStmtCompareContext);
};

class ObStmtComparer
{
public:

   /**
   * @brief compute_overlap_between_stmts
   * only consider the overlap in the from, where parts
   * from_map[i]: the i-th from item of the first stmt corresponds to the from_map[i]-th from item of the second stmt
   *              if there is no correspondence, then from_map[i] = OB_INVALID_ID
   * cond_map[i]: the i-th get condition of the first stmt corresponds to the cond_map[i]-th condition of the second stmt
   *              if there is no correspondence, then cond_map[i] = OB_INVALID_ID
   * @return
   */
  static int compute_stmt_overlap(const ObDMLStmt *first,
                                  const ObDMLStmt *second,
                                  ObStmtMapInfo &map_info);

  /* is_strict_select_list = true, it requerys same order select list between two stmts. */
  static int check_stmt_containment(const ObDMLStmt *first,
                                    const ObDMLStmt *second,
                                    ObStmtMapInfo &map_info,
                                    QueryRelation &relation,
                                    bool is_strict_select_list = false,
                                    bool need_check_select_items = true,
                                    bool is_in_same_stmt = true);

  static int compute_conditions_map(const ObDMLStmt *first,
                                    const ObDMLStmt *second,
                                    const ObIArray<ObRawExpr*> &first_exprs,
                                    const ObIArray<ObRawExpr*> &second_exprs,
                                    ObStmtMapInfo &map_info,
                                    ObIArray<int64_t> &condition_map,
                                    QueryRelation &relation,
                                    bool is_in_same_cond = true,
                                    bool is_same_by_order = false,
                                    bool need_check_second_range = false);

  static int compute_orderby_map(const ObDMLStmt *first,
                                 const ObDMLStmt *second,
                                 const ObIArray<OrderItem> &first_orders,
                                 const ObIArray<OrderItem> &second_orders,
                                 ObStmtMapInfo &map_info,
                                 int64_t &match_count);

  static int compute_from_items_map(const ObDMLStmt *first,
                                    const ObDMLStmt *second,
                                    bool is_in_same_stmt,
                                    ObStmtMapInfo &map_info,
                                    QueryRelation &relation);

  static int is_same_from(const ObDMLStmt *first,
                          const FromItem &first_from,
                          const ObDMLStmt *second,
                          const FromItem &second_from,
                          bool is_in_same_stmt,
                          ObStmtMapInfo &map_info,
                          bool &is_same);

  static int is_same_condition(const ObRawExpr *left,
                               const ObRawExpr *right,
                               ObStmtCompareContext &context,
                               bool &is_same);

  static int compute_semi_infos_map(const ObDMLStmt *first,
                                    const ObDMLStmt *second,
                                    bool is_in_same_stmt,
                                    ObStmtMapInfo &map_info,
                                    int64_t &match_count);

  static int is_same_semi_info(const ObDMLStmt *first,
                              const SemiInfo *first_semi_info,
                              const ObDMLStmt *second,
                              const SemiInfo *second_semi_info,
                              bool is_in_same_stmt,
                              ObStmtMapInfo &map_info,
                              bool &is_same);

  static int compute_tables_map(const ObDMLStmt *first,
                                const ObDMLStmt *second,
                                const ObIArray<uint64_t> &first_table_ids,
                                const ObIArray<uint64_t> &second_table_ids,
                                ObStmtMapInfo &map_info,
                                ObIArray<int64_t> &table_map,
                                int64_t &match_count);

  static int compute_unmatched_item(const ObIArray<int64_t> &item_map,
                                    int first_size,
                                    int second_size,
                                    ObIArray<int64_t> &first_unmatched_items,
                                    ObIArray<int64_t> &second_unmatched_items);

  
  static int inner_compute_expr(const ObRawExpr *target_expr,
                                const ObIArray<ObRawExpr*> &source_exprs,
                                ObStmtCompareContext &context,
                                ObRawExprCopier &expr_copier,
                                bool &is_match);

  /**
   * @brief compare_basic_table_item
   * If the partition hint relationships included in the two tables,
   * if the two tables are different, they cannot be compared
   * If the two tables are the same and there is no partition hint, they are equal
   * If both tables are generated_table, only compare whether the referenced subqueries are the same
   */
  static int compare_basic_table_item (const TableItem *first_table,
                                       const TableItem *second_table,
                                       QueryRelation &relation);

  /**
   * @brief compare_joined_table_item
   * Compare whether two joined tables are isomorphic
   * Require that the left and right table items at each level are the same, and the on condition is the same
   */
  static int compare_joined_table_item (const ObDMLStmt *first,
                                        const TableItem *first_table,
                                        const ObDMLStmt *second,
                                        const TableItem *second_table,
                                        bool is_in_same_stmt,
                                        ObStmtMapInfo &map_info,
                                        QueryRelation &relation);

  /**
   * @brief compare_table_item
   * Compare two table items whether they are isomorphic
   */
  static int compare_table_item (const ObDMLStmt *first,
                                const TableItem *first_table,
                                const ObDMLStmt *second,
                                const TableItem *second_table,
                                bool is_in_same_stmt,
                                ObStmtMapInfo &map_info,
                                QueryRelation &relation);

  static int compare_set_stmt(const ObSelectStmt *first,
                              const ObSelectStmt *second,
                              ObStmtMapInfo &map_info,
                              QueryRelation &relation,
                              bool is_in_same_stmt = true);

  static int compare_values_table_item(const ObDMLStmt *first,
                                       const TableItem *first_table,
                                       const ObDMLStmt *second,
                                       const TableItem *second_table,
                                       ObStmtMapInfo &map_info,
                                       QueryRelation &relation);
  static int get_map_table(const ObStmtMapInfo& map_info,
                           const ObSelectStmt *outer_stmt,
                           const ObSelectStmt *inner_stmt,
                           const uint64_t &outer_table_id,
                           uint64_t &inner_table_id);
  static int get_map_column(const ObStmtMapInfo& map_info,
                            const ObSelectStmt *outer_stmt,
                            const ObSelectStmt *inner_stmt,
                            const uint64_t &outer_table_id,
                            const uint64_t &outer_column_id,
                            const bool in_same_stmt,
                            uint64_t &inner_table_id,
                            uint64_t &inner_column_id);

};

}
}


#endif // OB_STMT_COMPARER_H
