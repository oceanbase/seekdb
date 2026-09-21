/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include "sql/engine/graph/graph_expand.h"
#include "sql/optimizer/ob_logical_operator.h"

namespace oceanbase
{
namespace sql
{

// Describes how one feedback iteration reaches the edge table. This is kept on
// the graph operator instead of being inferred by EXPLAIN from a generic SET
// subtree. UNKNOWN is retained for defensive diagnostics while the current
// recursive-SQL lowering is being replaced by a logical GraphExpand child.
enum class GraphFeedbackAccessMethod : int8_t
{
  UNKNOWN = 0,
  FULL_SCAN,
  INDEX_SCAN
};

// Logical controller for a bounded graph path. The first child produces seed
// path states and the second child performs one expansion step over the current
// frontier. Unlike ObLogSet, this operator never applies relational set
// distinctness: both WALK and TRAIL retain bag multiplicity between paths.
class GraphFeedbackLoopLogOp final : public ObLogicalOperator
{
public:
  explicit GraphFeedbackLoopLogOp(ObLogPlan &plan)
      : ObLogicalOperator(plan)
  {}
  ~GraphFeedbackLoopLogOp() override = default;

  void configure_path(const GraphPathDesc &path_desc,
                      bool pull_to_local)
  {
    path_desc_ = path_desc;
    pull_to_local_ = pull_to_local;
  }

  int initialize_expand_access();
  const GraphPathDesc &get_path_desc() const { return path_desc_; }
  const GraphExpandAccessDesc &get_expand_access_desc() const
  { return expand_access_desc_; }
  int64_t get_min_hops() const { return path_desc_.lower_bound_; }
  int64_t get_max_hops() const { return path_desc_.upper_bound_; }
  bool is_reverse() const
  { return path_desc_.direction_ == GraphPathDirection::IN; }
  GraphPathMode get_path_mode() const { return path_desc_.path_mode_; }
  GraphFeedbackAccessMethod get_step_access_method() const
  {
    return step_access_method_;
  }

  int get_plan_item_info(PlanText &plan_text, ObSqlPlanItem &plan_item) override;
  int get_op_exprs(ObIArray<ObRawExpr *> &all_exprs) override;
  int is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed) override;
  int est_cost() override;
  int est_width() override;
  int do_re_est_cost(EstimateCostInfo &param,
                     double &card,
                     double &op_cost,
                     double &cost) override;
  int est_ambient_card() override { return OB_SUCCESS; }
  int get_card_without_filter(double &card) override;
  uint64_t hash(uint64_t seed) const override;

  int compute_const_exprs() override;
  int compute_equal_set() override;
  int compute_fd_item_set() override;
  int compute_op_ordering() override;
  int compute_one_row_info() override;
  int compute_sharding_info() override;
  int compute_op_parallel_info() override;
  int allocate_startup_expr_post() override { return OB_SUCCESS; }
  int check_use_child_ordering(bool &used,
                               int64_t &inherit_child_ordering_index) override;
  bool is_consume_child_1by1() const override { return true; }

  VIRTUAL_TO_STRING_KV(K_(path_desc), K_(expand_access_desc),
                       "pull to local", pull_to_local_,
                       "step access", static_cast<int64_t>(step_access_method_));

private:
  int get_feedback_exprs(ObIArray<ObRawExpr *> &exprs) const;

private:
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc expand_access_desc_{};
  bool pull_to_local_{false};
  GraphFeedbackAccessMethod step_access_method_{GraphFeedbackAccessMethod::UNKNOWN};

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackLoopLogOp);
};

} // namespace sql
} // namespace oceanbase
