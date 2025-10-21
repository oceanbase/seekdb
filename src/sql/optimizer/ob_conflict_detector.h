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

#ifndef _OB_CONFLICT_DETECTOR_H
#define _OB_CONFLICT_DETECTOR_H

#include "lib/container/ob_se_array.h"
#include "sql/ob_sql_define.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{
namespace sql
{

struct TableDependInfo;
class ObJoinOrder;

/*
 * Used to indicate the future join condition for inner join
 */
struct JoinInfo
{
  JoinInfo() :
      table_set_(),
      on_conditions_(),
      where_conditions_(),
      equal_join_conditions_(),
      join_type_(UNKNOWN_JOIN)
      {}

  JoinInfo(ObJoinType join_type) :
      table_set_(),
      on_conditions_(),
      where_conditions_(),
      equal_join_conditions_(),
      join_type_(join_type)
      {}

  virtual ~JoinInfo() {};
  TO_STRING_KV(K_(join_type),
                K_(table_set),
                K_(on_conditions),
                K_(where_conditions),
                K_(equal_join_conditions));
  ObRelIds table_set_; // set of tables to join (i.e., all tables included in join_qual_ except itself)
  common::ObSEArray<ObRawExpr*, 4, common::ModulePageAllocator, true> on_conditions_; // from on conditions, if it is outer/semi join
  common::ObSEArray<ObRawExpr*, 4, common::ModulePageAllocator, true> where_conditions_; // conditions from where, if it is outer/semi join, then it is join filter, if it is inner join, then it is join condition
  common::ObSEArray<ObRawExpr*, 4, common::ModulePageAllocator, true> equal_join_conditions_; // is a subset of the join conditions (outer/semi's on condition, inner join's where condition) with only simple equality, used when predicting the order required for future merge join
  ObJoinType join_type_;
};

class ObConflictDetector
{
  friend class ObConflictDetectorGenerator;
public:
  ObConflictDetector() :
    join_info_(),
    CR_(),
    cross_product_rule_(),
    delay_cross_product_rule_(),
    L_TES_(),
    R_TES_(),
    L_DS_(),
    R_DS_(),
    is_degenerate_pred_(false),
    is_commutative_(false),
    is_redundancy_(false)
    {}

  virtual ~ObConflictDetector() {}

  TO_STRING_KV(K_(join_info),
               K_(CR),
               K_(cross_product_rule),
               K_(delay_cross_product_rule),
               K_(L_TES),
               K_(R_TES),
               K_(L_DS),
               K_(R_DS),
               K_(is_degenerate_pred),
               K_(is_commutative),
               K_(is_redundancy));

public:
  inline JoinInfo& get_join_info() {return join_info_;}
  inline const JoinInfo& get_join_info() const {return join_info_;}

  static int build_confict(common::ObIAllocator &allocator,
                           ObConflictDetector* &detector);

  static int satisfy_associativity_rule(const ObConflictDetector &left,
                                        const ObConflictDetector &right,
                                        bool &is_satisfy);

  static int satisfy_left_asscom_rule(const ObConflictDetector &left,
                                      const ObConflictDetector &right,
                                      bool &is_satisfy);

  static int satisfy_right_asscom_rule(const ObConflictDetector &left,
                                       const ObConflictDetector &right,
                                       bool &is_satisfy);

  static int choose_detectors(ObRelIds &left_tables,
                              ObRelIds &right_tables,
                              ObIArray<ObConflictDetector*> &left_used_detectors,
                              ObIArray<ObConflictDetector*> &right_used_detectors,
                              ObIArray<TableDependInfo> &table_depend_infos,
                              ObIArray<ObConflictDetector*> &all_detectors,
                              ObIArray<ObConflictDetector*> &valid_detectors,
                              bool delay_cross_product,
                              bool &is_strict_order);

  static int check_join_info(const ObIArray<ObConflictDetector*> &valid_detectors,
                             ObJoinType &join_type,
                             bool &is_valid);

  static int merge_join_info(const ObIArray<ObConflictDetector*> &valid_detectors,
                             JoinInfo &join_info);

  int check_join_legal(const ObRelIds &left_set,
                       const ObRelIds &right_set,
                       const ObRelIds &combined_set,
                       bool delay_cross_product,
                       ObIArray<TableDependInfo> &table_depend_infos,
                       bool &legal);
  

private:
  // table set contains all tables referenced by the current join condition, that is, SES
  JoinInfo join_info_;
  //conflict rules: R1 -> R2
  common::ObSEArray<std::pair<ObRelIds, ObRelIds> , 4, common::ModulePageAllocator, true> CR_;
  common::ObSEArray<std::pair<ObRelIds, ObRelIds> , 4, common::ModulePageAllocator, true> cross_product_rule_;
  common::ObSEArray<std::pair<ObRelIds, ObRelIds> , 4, common::ModulePageAllocator, true> delay_cross_product_rule_;
  //left total eligibility set
  ObRelIds L_TES_;
  //right total eligibility set
  ObRelIds R_TES_;
  // left degenerate set, used to check the validity of join condition being a degenerate predicate, stores all table sets of the left subtree
  ObRelIds L_DS_;
  // right degenerate set, holds all table sets of the right subtree
  ObRelIds R_DS_;
  bool is_degenerate_pred_;
  // Current join whether left and right tables can be swapped
  bool is_commutative_;
  // Generate redundant Cartesian product for hint
  bool is_redundancy_;
};

class ObConflictDetectorGenerator
{
public:
  ObConflictDetectorGenerator(common::ObIAllocator &allocator,
                              ObRawExprFactory &expr_factory,
                              ObSQLSessionInfo *session_info,
                              ObRawExprCopier *onetime_copier,
                              bool should_deduce_conds,
                              bool should_pushdown_filters,
                              const common::ObIArray<TableDependInfo> &table_depend_infos,
                              const common::ObIArray<ObRawExpr*> &push_subq_exprs,
                              common::ObIArray<ObRelIds> &bushy_tree_infos,
                              common::ObIArray<ObRawExpr*> &new_or_quals,
                              ObQueryCtx *ctx) :
    allocator_(allocator),
    expr_factory_(expr_factory),
    session_info_(session_info),
    onetime_copier_(onetime_copier),
    should_deduce_conds_(should_deduce_conds),
    should_pushdown_const_filters_(should_pushdown_filters),
    table_depend_infos_(table_depend_infos),
    push_subq_exprs_(push_subq_exprs),
    bushy_tree_infos_(bushy_tree_infos),
    new_or_quals_(new_or_quals),
    query_ctx_(ctx)
    {}

  virtual ~ObConflictDetectorGenerator() {}

public:
  int generate_conflict_detectors(const ObDMLStmt *stmt,
                                  const ObIArray<TableItem*> &table_items,
                                  const ObIArray<SemiInfo*> &semi_infos,
                                  const ObIArray<ObRawExpr*> &quals,
                                  ObIArray<ObSEArray<ObRawExpr*,4>> &baserel_filters,
                                  ObIArray<ObConflictDetector*> &conflict_detectors);

private:
  int add_conflict_rule(const ObRelIds &left,
                        const ObRelIds &right,
                        ObIArray<std::pair<ObRelIds, ObRelIds>> &rules);

  int generate_conflict_rule(ObConflictDetector *parent,
                             ObConflictDetector *child,
                             bool is_left_child,
                             ObIArray<std::pair<ObRelIds, ObRelIds>> &rules);

  int generate_semi_join_detectors(const ObDMLStmt *stmt,
                                   const ObIArray<SemiInfo*> &semi_infos,
                                   ObRelIds &left_rel_ids,
                                   const ObIArray<ObConflictDetector*> &inner_join_detectors,
                                   ObIArray<ObConflictDetector*> &semi_join_detectors);

  int generate_inner_join_detectors(const ObDMLStmt *stmt,
                                    const ObIArray<TableItem*> &table_items,
                                    ObIArray<ObRawExpr*> &quals,
                                    ObIArray<ObSEArray<ObRawExpr*,4>> &baserel_filters,
                                    ObIArray<ObConflictDetector*> &inner_join_detectors);

  int generate_outer_join_detectors(const ObDMLStmt *stmt,
                                    TableItem *table_item,
                                    ObIArray<ObRawExpr*> &table_filter,
                                    ObIArray<ObSEArray<ObRawExpr*,4>> &baserel_filters,
                                    ObIArray<ObConflictDetector*> &outer_join_detectors);

  int distribute_quals(const ObDMLStmt *stmt,
                       TableItem *table_item,
                       const ObIArray<ObRawExpr*> &table_filter,
                       ObIArray<ObSEArray<ObRawExpr*,4>> &baserel_filters);

  int flatten_inner_join(TableItem *table_item,
                         ObIArray<ObRawExpr*> &table_filter,
                         ObIArray<TableItem*> &table_items);

  int inner_generate_outer_join_detectors(const ObDMLStmt *stmt,
                                          JoinedTable *joined_table,
                                          ObIArray<ObRawExpr*> &table_filter,
                                          ObIArray<ObSEArray<ObRawExpr*,4>> &baserel_filters,
                                          ObIArray<ObConflictDetector*> &outer_join_detectors);

  int pushdown_where_filters(const ObDMLStmt *stmt,
                             JoinedTable *joined_table,
                             ObIArray<ObRawExpr*> &table_filter,
                             ObIArray<ObRawExpr*> &left_quals,
                             ObIArray<ObRawExpr*> &right_quals);

  int pushdown_on_conditions(const ObDMLStmt *stmt,
                             JoinedTable *joined_table,
                             ObIArray<ObRawExpr*> &left_quals,
                             ObIArray<ObRawExpr*> &right_quals,
                             ObIArray<ObRawExpr*> &join_quals);

  int generate_cross_product_detector(const ObDMLStmt *stmt,
                                      const ObIArray<TableItem*> &table_items,
                                      ObIArray<ObRawExpr*> &quals,
                                      ObIArray<ObConflictDetector*> &inner_join_detectors);

  int generate_cross_product_conflict_rule(const ObDMLStmt *stmt,
                                           ObConflictDetector *cross_product_detector,
                                           const ObIArray<TableItem*> &table_items,
                                           const ObIArray<ObRawExpr*> &join_conditions);

  int check_join_info(const ObRelIds &left,
                      const ObRelIds &right,
                      const ObIArray<ObRelIds> &base_table_ids,
                      bool &is_connected);

  int deduce_redundant_join_conds(const ObDMLStmt *stmt,
                                  const ObIArray<ObRawExpr*> &quals,
                                  const ObIArray<TableItem*> &table_items,
                                  ObIArray<ObRawExpr*> &redundant_quals);

  int deduce_redundant_join_conds_with_equal_set(const ObIArray<ObRawExpr*> &equal_set,
                                                 ObIArray<ObRelIds> &connect_infos,
                                                 ObIArray<ObRelIds> &single_table_ids,
                                                 ObIArray<ObRawExpr*> &redundancy_quals);

  int find_inner_conflict_detector(const ObIArray<ObConflictDetector*> &inner_conflict_detectors,
                                   const ObRelIds &rel_ids,
                                   ObConflictDetector* &detector);

  bool has_depend_table(const ObRelIds& table_ids);

private:
  common::ObIAllocator &allocator_;
  ObRawExprFactory &expr_factory_;
  ObSQLSessionInfo *session_info_;
  ObRawExprCopier *onetime_copier_;
  bool should_deduce_conds_;
  bool should_pushdown_const_filters_;
  const common::ObIArray<TableDependInfo> &table_depend_infos_;
  const common::ObIArray<ObRawExpr*> &push_subq_exprs_;
  common::ObIArray<ObRelIds> &bushy_tree_infos_;
  common::ObIArray<ObRawExpr*> &new_or_quals_;
  ObQueryCtx *query_ctx_;
};


} // namespace sql
} // namespace oceanbase

#endif /* _OB_CONFLICT_DETECTOR_H */
