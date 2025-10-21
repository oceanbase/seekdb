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

#ifndef OB_PREDICATE_DEDUCER_H
#define OB_PREDICATE_DEDUCER_H

#include "sql/resolver/dml/ob_dml_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObPredicateDeduce {

  enum Type {
    GT = 1 << 0,
    GE = 1 << 1,
    EQ = 1 << 2
  };

public:
  ObPredicateDeduce(ObDMLStmt &stmt) : stmt_(stmt) {
    graph_.set_attr(ObMemAttr(MTL_ID(), "Graph"));
    type_safety_.set_attr(ObMemAttr(MTL_ID(), "TypeSafety"));
    topo_order_.set_attr(ObMemAttr(MTL_ID(), "TopoOrder"));
  }

  int add_predicate(ObRawExpr *pred, bool &is_added);

  int deduce_simple_predicates(ObTransformerCtx &ctx,
                               ObIArray<ObRawExpr *> &result);

  int deduce_general_predicates(ObTransformerCtx &ctx,
                                ObIArray<ObRawExpr *> &target_exprs,
                                ObIArray<ObRawExpr *> &other_preds,
                                ObIArray<std::pair<ObRawExpr *, ObRawExpr *>> &lossless_preds,
                                ObIArray<ObRawExpr *> &result);

  int deduce_aggr_bound_predicates(ObTransformerCtx &ctx,
                                   ObIArray<ObRawExpr *> &target_exprs,
                                   ObIArray<ObRawExpr *> &aggr_bound_preds);
  
  static bool find_equal_expr(const ObIArray<ObRawExpr *> &exprs,
                              const ObRawExpr *target,
                              int64_t *idx = NULL,
                              ObExprParamCheckContext *context = NULL);

  static int check_deduce_validity(ObRawExpr *expr, bool &is_valid);

  static inline bool is_simple_condition(const ObItemType type)
  {
    return T_OP_EQ == type ||
           T_OP_LE == type ||
           T_OP_LT == type ||
           T_OP_GT == type ||
           T_OP_GE == type;
  }

  static inline bool is_general_condition(const ObItemType type)
  {
    return type == T_OP_BTW ||
           type == T_OP_LIKE ||
           type == T_OP_NE ||
           type == T_OP_IN;
  }

  static inline bool contain_special_expr(ObRawExpr &expr)
  {
    return !expr.is_deterministic() ||
           expr.has_flag(CNT_SUB_QUERY);
  }

  static int check_lossless_cast_table_filter(ObRawExpr *expr, 
                                              ObRawExpr *&cast_expr,
                                              bool &is_valid);
private:

  int init();

  int deduce(ObIArray<uint8_t> &graph);

  int create_simple_preds(ObTransformerCtx &ctx,
                          ObIArray<uint8_t> &chosen,
                          ObIArray<ObRawExpr *> &output_exprs);

  int choose_equal_preds(ObIArray<uint8_t> &chosen,
                         ObSqlBitSet<> &expr_equal_with_const);

  int choose_unequal_preds(ObTransformerCtx &ctx,
                           ObIArray<uint8_t> &chosen,
                           ObSqlBitSet<> &ignore_list);

  bool check_deduciable(const ObIArray<uint8_t> &graph,
                        const int64_t mid,
                        const int64_t left,
                        const int64_t right,
                        const Type type)
  {
    uint8_t left_right = 0;
    uint8_t left_mid = graph.at(left * N + mid);
    uint8_t mid_right = graph.at(mid * N + right);
    connect(left_right, left_mid, mid_right);
    return has(left_right, type);
  }

  int topo_sort(ObIArray<int64_t> &order);

  int topo_sort(int64_t id,
                ObIArray<bool> &visited,
                ObIArray<int64_t> &order);

  int choose_input_preds(ObIArray<uint8_t> &chosen,
                         ObIArray<ObRawExpr *> &output_exprs);

  void connect(uint8_t &left_right, uint8_t left_hub, uint8_t hub_right);

  bool is_table_filter(int64_t left, int64_t right) const
  {
    bool bret = false;
    bool left_is_const = is_const(left);
    bool right_is_const = is_const(right);
    if (OB_LIKELY(left >= 0 && left < input_exprs_.count() &&
                  right >= 0 && right < input_exprs_.count() &&
                  NULL != input_exprs_.at(left) &&
                  NULL != input_exprs_.at(right))) {
      if (left_is_const && right_is_const) {
        // do nothing
      } else if (left_is_const) {
        // left does not contain any column of the stmt
        bret = input_exprs_.at(right)->get_relation_ids().num_members() == 1;
      } else if (right_is_const) {
        // right does not contain any column of the stmt
        bret = input_exprs_.at(left)->get_relation_ids().num_members() == 1;
      } else {
        // both left and right contain some columns of the stmt
        bret =
            input_exprs_.at(left)->get_relation_ids().equal(
              input_exprs_.at(right)->get_relation_ids()) &&
            input_exprs_.at(left)->get_relation_ids().num_members() == 1;
      }
    }
    return bret;
  }

  bool is_raw_const(int64_t id) const
  {
    bool bret = false;
    if (OB_LIKELY(id >= 0 && id < input_exprs_.count() &&
                  NULL != input_exprs_.at(id))) {
      bret = input_exprs_.at(id)->is_static_const_expr();
    }
    return bret;
  }

  bool is_const(int64_t id) const
  {
    bool bret = false;
    if (OB_LIKELY(id >= 0 && id < input_exprs_.count() &&
                  NULL != input_exprs_.at(id))) {
      bret = input_exprs_.at(id)->is_const_expr();
    }
    return bret;
  }

  void set(ObIArray<uint8_t> &graph,
           const int64_t left,
           const int64_t right,
           const Type type)
  {
    set(graph.at(left * N + right), type);
    if (type == EQ) {
      set(graph.at(left + right * N), type);
    }
  }

  bool has(const ObIArray<uint8_t> &graph,
           const int64_t left,
           const int64_t right,
           const Type type)
  {
    bool bret = has(graph.at(left * N + right), type);
    if (type == EQ && !bret) {
      bret = has(graph.at(left + right * N), type);
    }
    return bret;
  }

  void clear(ObIArray<uint8_t> &graph,
             const int64_t left,
             const int64_t right,
             const Type type)
  {
    clear(graph.at(left * N + right), type);
    if (type == EQ) {
      clear(graph.at(left + right * N), type);
    }
  }

  bool is_type_safe(int64_t left_id, int64_t right_id)
  {
    return type_safety_.at(left_id * N + right_id) ||
           type_safety_.at(left_id + right_id * N);
  }

  static bool has(const uint8_t &v, const Type type) { return (v & type) == type; }
  static void set(uint8_t &v, const Type type) { v = (v | type); }
  static void clear(uint8_t &v, const Type type) { v = (v & ~type); }

  int check_type_safe(int64_t first, int64_t second, bool &type_safe);

  int convert_pred(const ObRawExpr *pred, int64_t &left_id, int64_t &right_id, Type &type);

  void expand_graph(ObIArray<uint8_t> &graph, int64_t hub1, int64_t hub2);

  int check_general_expr_validity(ObRawExpr *general_expr, bool &is_valid);

  int get_equal_exprs(ObRawExpr *preds,
                      ObIArray<ObRawExpr *> &general_preds,
                      ObIArray<ObRawExpr *> &target_exprs,
                      ObIArray<ObRawExpr *> &equal_exprs);

  int find_similar_expr(ObRawExpr *pred,
                        ObIArray<ObRawExpr *> &general_preds,
                        ObIArray<ObRawExpr *> &first_params);

  int check_aggr_validity(ObRawExpr *expr,
                          ObRawExpr *&param_expr,
                          bool &is_valid);

  int get_expr_bound(ObRawExpr *target,
                     ObRawExpr *&lower,
                     Type &lower_type,
                     ObRawExpr *&upper,
                     Type &upper_type);

  int check_index_part_cond(ObTransformerCtx &ctx,
                            ObRawExpr *left_expr,
                            ObRawExpr *right_expr,
                            bool &is_valid);

  int check_cmp_metas_for_general_preds(ObRawExpr *left_pexr, ObRawExpr *pred,  bool &type_safe);
  
  bool has_raw_const_equal_condition(int64_t param_idx);
private:
  ObObjMeta cmp_type_; // the compare meta used by all exprs in the graph
  /// The expression corresponding to each node in the graph
  ObSEArray<ObRawExpr *, 4> input_exprs_;
  /// Construct the input predicate expression of the connected graph
  ObSEArray<ObRawExpr *, 4> input_preds_;
  // Fully connected graph
  ObArray<uint8_t> graph_;
  /// The comparison type between two expressions is whether it is the same as cmp_type_
  ObArray<bool> type_safety_;
  /// After topological sorting based on size relationship, the order of nodes in the graph
  ObSEArray<int64_t, 4> topo_order_;

  ObDMLStmt &stmt_;

  int64_t N;
};

}
}

#endif // OB_PREDICATE_DEDUCER_H
