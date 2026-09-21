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

#define USING_LOG_PREFIX SQL_OPT

#include "sql/optimizer/graph_feedback_loop_log_op.h"
#include "sql/optimizer/ob_join_order.h"
#include "sql/optimizer/ob_log_table_scan.h"
#include "sql/optimizer/ob_opt_est_cost.h"
#include "sql/resolver/dml/ob_select_stmt.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::log_op_def;

namespace
{

const double GRAPH_FEEDBACK_ESTIMATE_LIMIT = 1.0e15;
const char *GRAPH_STEP_EDGE_ALIAS = "__g_step_edge";

double bounded_graph_estimate_add(double left, double right)
{
  return left >= GRAPH_FEEDBACK_ESTIMATE_LIMIT - right
      ? GRAPH_FEEDBACK_ESTIMATE_LIMIT
      : left + right;
}

double bounded_graph_estimate_multiply(double left, double right)
{
  double result = 0.0;
  if (left > 0.0 && right > 0.0) {
    result = left >= GRAPH_FEEDBACK_ESTIMATE_LIMIT / right
        ? GRAPH_FEEDBACK_ESTIMATE_LIMIT
        : left * right;
  }
  return result;
}

// Keep execution work separate from emitted cardinality. Hops below min_hops
// still build the next frontier, but they do not contribute output rows.
void estimate_bounded_graph_feedback(double seed_rows,
                                     double first_step_rows,
                                     int64_t min_hops,
                                     int64_t max_hops,
                                     GraphFeedbackAccessMethod access_method,
                                     double &recursive_work_rows,
                                     double &output_rows,
                                     double &step_cost_factor)
{
  seed_rows = std::max(0.0, seed_rows);
  first_step_rows = std::max(0.0, first_step_rows);
  recursive_work_rows = 0.0;
  output_rows = min_hops == 0 ? seed_rows : 0.0;
  step_cost_factor = 0.0;

  if (seed_rows > 0.0 && max_hops > 0) {
    // The first step is executed even when it produces an empty frontier.
    step_cost_factor = 1.0;
    if (first_step_rows > 0.0) {
      const double fanout = first_step_rows / std::max(1.0, seed_rows);
      double frontier_rows = first_step_rows;
      double index_step_factor = 1.0;
      for (int64_t hop = 1; hop <= max_hops; ++hop) {
        if (hop > 1) {
          frontier_rows = bounded_graph_estimate_multiply(frontier_rows, fanout);
          index_step_factor = bounded_graph_estimate_multiply(index_step_factor, fanout);
          step_cost_factor = bounded_graph_estimate_add(step_cost_factor,
                                                        index_step_factor);
        }
        recursive_work_rows = bounded_graph_estimate_add(recursive_work_rows,
                                                         frontier_rows);
        if (hop >= min_hops) {
          output_rows = bounded_graph_estimate_add(output_rows, frontier_rows);
        }
      }

      // A full-scan step reads the edge table once for every non-empty level;
      // an index step scales with the estimated input frontier instead.
      if (access_method != GraphFeedbackAccessMethod::INDEX_SCAN) {
        step_cost_factor = std::max(step_cost_factor,
                                    static_cast<double>(max_hops));
      }
    }
  }
}

const ObLogTableScan *find_step_edge_scan(const ObLogicalOperator *root)
{
  const ObLogTableScan *result = nullptr;
  if (root != nullptr && root->get_type() == LOG_TABLE_SCAN) {
    const ObLogTableScan *scan = static_cast<const ObLogTableScan *>(root);
    if (scan->get_table_name().case_compare(GRAPH_STEP_EDGE_ALIAS) == 0) {
      result = scan;
    }
  }
  for (int64_t i = 0; result == nullptr && root != nullptr
                      && i < root->get_num_of_child(); ++i) {
    result = find_step_edge_scan(root->get_child(i));
  }
  return result;
}

const char *access_method_name(GraphFeedbackAccessMethod access_method)
{
  const char *name = "unknown";
  if (access_method == GraphFeedbackAccessMethod::INDEX_SCAN) {
    name = "index-scan";
  } else if (access_method == GraphFeedbackAccessMethod::FULL_SCAN) {
    name = "full-scan";
  }
  return name;
}

const char *path_mode_name(GraphPathMode path_mode)
{
  const char *name = "UNKNOWN";
  switch (path_mode) {
    case GraphPathMode::WALK:
      name = "WALK";
      break;
    case GraphPathMode::TRAIL:
      name = "TRAIL";
      break;
    default:
      break;
  }
  return name;
}

} // namespace

int GraphFeedbackLoopLogOp::initialize_step_access_method()
{
  int ret = OB_SUCCESS;
  const ObLogicalOperator *step = get_child(second_child);
  const ObLogTableScan *edge_scan = nullptr;
  if (OB_ISNULL(step)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback step child is null", K(ret));
  } else if (OB_ISNULL(edge_scan = find_step_edge_scan(step))) {
    // Keep UNKNOWN visible in EXPLAIN instead of guessing. The internal alias
    // lookup is transitional until the step child is a logical GraphExpand.
    step_access_method_ = GraphFeedbackAccessMethod::UNKNOWN;
  } else {
    step_access_method_ = edge_scan->is_index_scan()
        ? GraphFeedbackAccessMethod::INDEX_SCAN
        : GraphFeedbackAccessMethod::FULL_SCAN;
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_plan_item_info(PlanText &plan_text,
                                               ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
  } else {
    BEGIN_BUF_PRINT;
    if (OB_FAIL(BUF_PRINTF("direction=%s, hops={%ld,%ld}, access=%s, mode=%s",
                           path_desc_.direction_ == GraphPathDirection::IN
                               ? "IN" : "OUT",
                           path_desc_.lower_bound_,
                           path_desc_.upper_bound_,
                           access_method_name(step_access_method_),
                           path_mode_name(path_desc_.path_mode_)))) {
    }
    END_BUF_PRINT(plan_item.special_predicates_, plan_item.special_predicates_len_);
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_feedback_exprs(ObIArray<ObRawExpr *> &exprs) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_stmt()) || OB_UNLIKELY(!get_stmt()->is_select_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback statement is not a select", K(ret), K(get_stmt()));
  } else if (OB_FAIL(static_cast<const ObSelectStmt *>(get_stmt())->get_select_exprs(exprs))) {
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_op_exprs(ObIArray<ObRawExpr *> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_feedback_exprs(all_exprs))) {
  } else if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
  }
  return ret;
}

int GraphFeedbackLoopLogOp::is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr *> feedback_exprs;
  is_fixed = false;
  if (OB_FAIL(get_feedback_exprs(feedback_exprs))) {
  } else {
    is_fixed = ObOptimizerUtil::find_item(feedback_exprs, expr);
  }
  return ret;
}

int GraphFeedbackLoopLogOp::est_cost()
{
  int ret = OB_SUCCESS;
  double card = 0.0;
  double op_cost = 0.0;
  double cost = 0.0;
  EstimateCostInfo param;
  param.need_parallel_ = get_parallel();
  if (OB_FAIL(do_re_est_cost(param, card, op_cost, cost))) {
  } else {
    set_card(card);
    set_op_cost(op_cost);
    set_cost(cost);
  }
  return ret;
}

int GraphFeedbackLoopLogOp::est_width()
{
  int ret = OB_SUCCESS;
  const ObLogicalOperator *seed = get_child(first_child);
  const ObLogicalOperator *step = get_child(second_child);
  if (OB_ISNULL(seed) || OB_ISNULL(step)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback children are not initialized", K(ret), K(seed), K(step));
  } else {
    set_width(std::max(seed->get_width(), step->get_width()));
  }
  return ret;
}

int GraphFeedbackLoopLogOp::do_re_est_cost(EstimateCostInfo &param,
                                           double &card,
                                           double &op_cost,
                                           double &cost)
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *seed = get_child(first_child);
  ObLogicalOperator *step = get_child(second_child);
  double seed_rows = 0.0;
  double seed_cost = 0.0;
  double first_step_rows = 0.0;
  double first_step_cost = 0.0;
  double recursive_work_rows = 0.0;
  double output_rows = 0.0;
  double step_cost_factor = 0.0;
  EstimateCostInfo child_param;
  card = 0.0;
  op_cost = 0.0;
  cost = 0.0;

  if (OB_ISNULL(get_plan()) || OB_ISNULL(seed) || OB_ISNULL(step)
      || OB_UNLIKELY(get_num_of_child() != 2)
      || OB_UNLIKELY(!path_desc_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph feedback cost inputs", K(ret), K_(path_desc),
             K(get_num_of_child()), K(seed), K(step));
  } else if (OB_FAIL(child_param.assign(param))) {
  } else {
    // A LIMIT above the walk cannot reduce an intermediate frontier without
    // changing which paths are observable, so never push its row count into a
    // seed or step child during costing.
    child_param.need_row_count_ = -1;
    if (OB_FAIL(seed->re_est_cost(child_param, seed_rows, seed_cost))) {
    } else if (OB_FAIL(step->re_est_cost(child_param,
                                        first_step_rows,
                                        first_step_cost))) {
    }
  }

  if (OB_SUCC(ret)) {
    estimate_bounded_graph_feedback(seed_rows,
                                    first_step_rows,
                                    path_desc_.lower_bound_,
                                    path_desc_.upper_bound_,
                                    step_access_method_,
                                    recursive_work_rows,
                                    output_rows,
                                    step_cost_factor);
    const double scaled_step_cost = bounded_graph_estimate_multiply(
        std::max(0.0, first_step_cost), step_cost_factor);
    const double work_rows = bounded_graph_estimate_add(std::max(0.0, seed_rows),
                                                         recursive_work_rows);
    const int64_t parallel = std::max<int64_t>(1, param.need_parallel_);
    op_cost = ObOptEstCost::cost_get_rows(
        work_rows / parallel, get_plan()->get_optimizer_context());
    cost = bounded_graph_estimate_add(std::max(0.0, seed_cost), scaled_step_cost);
    cost = bounded_graph_estimate_add(cost, op_cost);
    card = param.need_row_count_ >= 0 && param.need_row_count_ < output_rows
        ? param.need_row_count_
        : output_rows;
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_card_without_filter(double &card)
{
  int ret = OB_SUCCESS;
  const ObLogicalOperator *seed = get_child(first_child);
  const ObLogicalOperator *step = get_child(second_child);
  double recursive_work_rows = 0.0;
  double step_cost_factor = 0.0;
  if (OB_ISNULL(seed) || OB_ISNULL(step) || OB_UNLIKELY(get_num_of_child() != 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph feedback children", K(ret), K(seed), K(step),
             K(get_num_of_child()));
  } else {
    estimate_bounded_graph_feedback(seed->get_card(),
                                    step->get_card(),
                                    path_desc_.lower_bound_,
                                    path_desc_.upper_bound_,
                                    step_access_method_,
                                    recursive_work_rows,
                                    card,
                                    step_cost_factor);
  }
  return ret;
}

uint64_t GraphFeedbackLoopLogOp::hash(uint64_t seed) const
{
  seed = do_hash(path_desc_.graph_id_, seed);
  seed = do_hash(path_desc_.graph_version_, seed);
  seed = do_hash(path_desc_.source_element_id_, seed);
  seed = do_hash(path_desc_.edge_element_id_, seed);
  seed = do_hash(path_desc_.target_element_id_, seed);
  seed = do_hash(path_desc_.lower_bound_, seed);
  seed = do_hash(path_desc_.upper_bound_, seed);
  seed = do_hash(static_cast<int64_t>(path_desc_.direction_), seed);
  seed = do_hash(static_cast<int64_t>(path_desc_.path_mode_), seed);
  seed = do_hash(static_cast<int64_t>(path_desc_.row_shape_), seed);
  seed = do_hash(path_desc_.need_path_, seed);
  seed = do_hash(pull_to_local_, seed);
  seed = do_hash(static_cast<int64_t>(step_access_method_), seed);
  return ObLogicalOperator::hash(seed);
}

int GraphFeedbackLoopLogOp::compute_const_exprs()
{
  // A path may be produced by either the seed or any recursive iteration, so
  // constants from one child cannot be inherited by the feedback output.
  return OB_SUCCESS;
}

int GraphFeedbackLoopLogOp::compute_equal_set()
{
  int ret = OB_SUCCESS;
  EqualSets *equal_sets = nullptr;
  if (OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback plan is null", K(ret));
  } else if (OB_ISNULL(equal_sets = get_plan()->create_equal_sets())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to create graph feedback equal sets", K(ret));
  } else {
    set_output_equal_sets(equal_sets);
  }
  return ret;
}

int GraphFeedbackLoopLogOp::compute_fd_item_set()
{
  set_fd_item_set(&empty_fd_item_set_);
  return OB_SUCCESS;
}

int GraphFeedbackLoopLogOp::compute_op_ordering()
{
  // Graph path modes promise no row order in the absence of an outer ORDER BY.
  reset_op_ordering();
  return OB_SUCCESS;
}

int GraphFeedbackLoopLogOp::compute_one_row_info()
{
  set_is_at_most_one_row(false);
  return OB_SUCCESS;
}

int GraphFeedbackLoopLogOp::compute_sharding_info()
{
  int ret = OB_SUCCESS;
  is_partition_wise_ = false;
  inherit_sharding_index_ = OB_INVALID_INDEX;
  weak_sharding_.reset();
  if (OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback plan is null", K(ret));
  } else if (pull_to_local_) {
    strong_sharding_ = get_plan()->get_optimizer_context().get_local_sharding();
  } else if (OB_FAIL(ObOptimizerUtil::compute_basic_sharding_info(
                 get_plan()->get_optimizer_context().get_local_server_addr(),
                 get_child_list(),
                 get_plan()->get_allocator(),
                 strong_sharding_,
                 inherit_sharding_index_))) {
  }
  return ret;
}

int GraphFeedbackLoopLogOp::compute_op_parallel_info()
{
  // A feedback frontier has one owner. DAS below the step may batch storage
  // access, but splitting the controller itself would duplicate path states.
  set_parallel(ObGlobalHint::DEFAULT_PARALLEL);
  set_available_parallel(ObGlobalHint::DEFAULT_PARALLEL);
  set_op_parallel_rule(OpParallelRule::OP_DAS_DOP);
  return OB_SUCCESS;
}

int GraphFeedbackLoopLogOp::check_use_child_ordering(
    bool &used,
    int64_t &inherit_child_ordering_index)
{
  used = false;
  inherit_child_ordering_index = OB_INVALID_INDEX;
  return OB_SUCCESS;
}
