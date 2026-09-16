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

#define USING_LOG_PREFIX SQL_OPT
#include "sql/optimizer/ob_log_set.h"
#include "sql/optimizer/ob_log_table_scan.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "sql/optimizer/ob_join_order.h"
#include "sql/optimizer/ob_log_distinct.h"

using namespace oceanbase;
using namespace sql;
using namespace oceanbase::common;
using namespace oceanbase::sql::log_op_def;

namespace
{
// Keep graph-walk estimates finite and representable in plan-table cardinality
// fields while still allowing substantially more growth than the generic
// recursive-CTE heuristic (which stops at seven rounds or 100x the seed).
const double GRAPH_FEEDBACK_ESTIMATE_LIMIT = 1.0e15;

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

void estimate_bounded_graph_feedback(double seed_rows,
                                     double first_step_rows,
                                     int64_t upper_bound,
                                     double &recursive_rows,
                                     double &total_rows,
                                     double &relative_work)
{
  seed_rows = std::max(0.0, seed_rows);
  first_step_rows = std::max(0.0, first_step_rows);
  recursive_rows = 0.0;
  relative_work = 0.0;
  if (seed_rows > 0.0 && first_step_rows > 0.0 && upper_bound > 0) {
    const double fanout = first_step_rows / std::max(1.0, seed_rows);
    double frontier_rows = first_step_rows;
    for (int64_t hop = 1; hop <= upper_bound; ++hop) {
      if (hop > 1) {
        frontier_rows = bounded_graph_estimate_multiply(frontier_rows, fanout);
      }
      recursive_rows = bounded_graph_estimate_add(recursive_rows, frontier_rows);
      relative_work = bounded_graph_estimate_add(
          relative_work, frontier_rows / first_step_rows);
    }
    // A full-scan recursive branch still runs once per non-empty level even
    // when estimated fan-out shrinks, so do not cost fewer than the bounded
    // number of hops. An empty first level terminates eagerly.
    relative_work = std::max(relative_work, static_cast<double>(upper_bound));
  }
  total_rows = bounded_graph_estimate_add(seed_rows, recursive_rows);
}

const ObLogTableScan *find_graph_table_scan(const ObLogicalOperator *root,
                                            const ObString &table_name)
{
  const ObLogTableScan *result = nullptr;
  if (root != nullptr && root->get_type() == LOG_TABLE_SCAN) {
    const ObLogTableScan *scan = static_cast<const ObLogTableScan *>(root);
    if (scan->get_table_name().case_compare(table_name) == 0) {
      result = scan;
    }
  }
  for (int64_t i = 0; result == nullptr && root != nullptr
                      && i < root->get_num_of_child(); ++i) {
    result = find_graph_table_scan(root->get_child(i), table_name);
  }
  return result;
}
} // namespace

const char *ObLogSet::get_name() const
{
  static const char *set_op_all[ObSelectStmt::SET_OP_NUM] =
  {
    "NONE",
    "UNION ALL",
    "INTERSECT ALL",
    "EXCEPT ALL",
    "RECURSIVE UNION ALL",
  };
  static const char *merge_set_op_distinct[ObSelectStmt::SET_OP_NUM] =
  {
    "NONE",
    "MERGE UNION DISTINCT",
    "MERGE INTERSECT DISTINCT",
    "MERGE EXCEPT DISTINCT",
  };
  static const char *hash_set_op_distinct[ObSelectStmt::SET_OP_NUM] =
  {
    "NONE",
    "HASH UNION DISTINCT",
    "HASH INTERSECT DISTINCT",
    "HASH EXCEPT DISTINCT",
  };
  const char *ret_char = "Unknown type";
  if (is_graph_feedback_loop_) {
    ret_char = "GRAPH FEEDBACK LOOP";
  } else if (set_op_ >= 0 && set_op_ < ObSelectStmt::SET_OP_NUM
      && set_algo_ != INVALID_SET_ALGO) {
    ret_char = !is_distinct_ ? set_op_all[set_op_] :
               (HASH_SET == set_algo_ ? hash_set_op_distinct[set_op_] : merge_set_op_distinct[set_op_]);
    if (is_recursive_union_) {
      ret_char = set_op_all[ObSelectStmt::SetOperator::RECURSIVE];
    }
  } else { /* Do nothing */ }
  return ret_char;
}

int ObLogSet::get_plan_item_info(PlanText &plan_text,
                                 ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
  } else if (is_graph_feedback_loop_) {
    const ObLogTableScan *edge_scan = ::find_graph_table_scan(
        get_child(second_child), ObString::make_string("__g_step_edge"));
    const char *access = edge_scan == nullptr
        ? "unknown"
        : (edge_scan->is_index_scan() ? "index-scan" : "full-scan");
    BEGIN_BUF_PRINT;
    if (OB_FAIL(BUF_PRINTF("direction=%s, hops={%ld,%ld}, access=%s",
                           graph_path_reverse_ ? "IN" : "OUT",
                           graph_path_lower_bound_,
                           graph_path_upper_bound_,
                           access))) {
    }
    END_BUF_PRINT(plan_item.special_predicates_, plan_item.special_predicates_len_);
  }
  return ret;
}

const ObSelectStmt *ObLogSet::get_left_stmt() const
{
  const ObSelectStmt *ret = NULL;
  ObLogicalOperator *left_child = get_child(ObLogicalOperator::first_child);
  if (!OB_ISNULL(left_child) && !OB_ISNULL(left_child->get_plan())
      && !OB_ISNULL(left_child->get_plan()->get_stmt())
      && left_child->get_plan()->get_stmt()->is_select_stmt()) {
    ret = static_cast<const ObSelectStmt*>(left_child->get_plan()->get_stmt());
  }
  return ret;
}


int ObLogSet::compute_const_exprs()
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *left_child = NULL;
  ObLogicalOperator *right_child = NULL;
  if (OB_ISNULL(my_plan_) || OB_ISNULL(left_child = get_child(first_child)) ||
      OB_ISNULL(right_child = get_child(second_child))) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((ObSelectStmt::EXCEPT == get_set_op() || ObSelectStmt::INTERSECT == get_set_op())
             && OB_FAIL(append(output_const_exprs_, left_child->get_output_const_exprs()))) {
  } else if (ObSelectStmt::INTERSECT == get_set_op() &&
             OB_FAIL(append(output_const_exprs_, right_child->get_output_const_exprs()))) {
  } else if (ObSelectStmt::UNION == get_set_op()) {
    //union, left/right are both const and have equal values add const, temporarily not maintained
    //union may have more than two branches
  } else {
    /*do nothing*/
  }
  return ret;
}

int ObLogSet::compute_equal_set()
{
  int ret = OB_SUCCESS;
  EqualSets *ordering_esets = NULL;
  EqualSets temp_ordering_esets;
  ObSEArray<ObRawExpr*, 8> ordering_eset_conditions;
  if (OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!is_set_distinct()) {
    // do nothing
  } else if (OB_FAIL(get_equal_set_conditions(ordering_eset_conditions))) {
  } else if (ObSelectStmt::UNION == get_set_op()) {
    // do nothing
  } else if (ObSelectStmt::EXCEPT == get_set_op()) {
    if (OB_FAIL(temp_ordering_esets.assign(
                  get_child(first_child)->get_output_equal_sets()))) {
    }
  } else if (OB_FAIL(get_input_equal_sets(temp_ordering_esets))) {
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(ordering_esets = get_plan()->create_equal_sets())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(ObEqualAnalysis::compute_equal_set(&get_plan()->get_allocator(),
                                                          ordering_eset_conditions,
                                                          temp_ordering_esets,
                                                          *ordering_esets))) {
    } else {
      set_output_equal_sets(ordering_esets);
    }
  }
  return ret;
}

int ObLogSet::get_equal_set_conditions(ObIArray<ObRawExpr*> &equal_conds)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = NULL;
  ObSEArray<ObRawExpr*, 8> set_exprs;
  const ObSelectStmt *stmt = NULL;
  ObRawExprFactory *expr_factory = NULL;
  if (OB_ISNULL(get_plan()) || OB_ISNULL(get_stmt()) || OB_UNLIKELY(!get_stmt()->is_select_stmt())
      || OB_ISNULL(session_info = get_plan()->get_optimizer_context().get_session_info())
      || OB_ISNULL(expr_factory = &get_plan()->get_optimizer_context().get_expr_factory())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FALSE_IT(stmt = static_cast<const ObSelectStmt*>(get_stmt()))) {
  } else if (OB_FAIL(stmt->get_pure_set_exprs(set_exprs))) {
  } else if (OB_FAIL(ObTransformUtils::get_equal_set_conditions(*expr_factory, session_info,
                                                                stmt, set_exprs, equal_conds))) {
  } else {
  }
  return ret;
}

int ObLogSet::deduce_const_exprs_and_ft_item_set(ObFdItemSet &fd_item_set)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr *, 8> select_exprs;
  if (OB_ISNULL(my_plan_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_set_exprs(select_exprs))) {
  } else if (OB_FAIL(my_plan_->get_fd_item_factory().deduce_fd_item_set(
                                                          get_output_equal_sets(),
                                                          select_exprs,
                                                          get_output_const_exprs(),
                                                          fd_item_set))) {
  }
  return ret;
}

int ObLogSet::compute_fd_item_set()
{
  int ret = OB_SUCCESS;
  ObFdItemSet *fd_item_set = NULL;
  ObExprFdItem *fd_item = NULL;
  ObLogicalOperator *left_child = NULL;
  ObLogicalOperator *right_child = NULL;
  ObSEArray<ObRawExpr *, 8> select_exprs;
  if (OB_ISNULL(my_plan_) || OB_ISNULL(left_child = get_child(first_child)) ||
      OB_ISNULL(right_child = get_child(second_child))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!is_set_distinct()) {
    // do nothing
  } else if (OB_FAIL(my_plan_->get_fd_item_factory().create_fd_item_set(fd_item_set))) {
  } else if (OB_FAIL(get_set_exprs(select_exprs))) {
  } else if (!ObTransformUtils::need_compute_fd_item_set(select_exprs)) {
    //do nothing
  } else if (OB_FAIL(my_plan_->get_fd_item_factory().create_expr_fd_item(
                                                        fd_item,
                                                        true,
                                                        select_exprs,
                                                        select_exprs))) {
  } else if (OB_FAIL(fd_item_set->push_back(fd_item))) {
  } else if ((ObSelectStmt::INTERSECT == set_op_ || ObSelectStmt::EXCEPT == set_op_) &&
            OB_FAIL(append_child_fd_item_set(*fd_item_set, left_child->get_fd_item_set()))) {
  } else if (ObSelectStmt::INTERSECT == set_op_ &&
            OB_FAIL(append_child_fd_item_set(*fd_item_set, right_child->get_fd_item_set()))) {
  } else if (OB_FAIL(deduce_const_exprs_and_ft_item_set(*fd_item_set))) {
  } else {
    set_fd_item_set(fd_item_set);
  }
  return ret;
}

// just ignore table fd item now
// todo: adjust log set fd, convert child fd set to currnet level stmt 
int ObLogSet::append_child_fd_item_set(ObFdItemSet &all_fd_item_set, const ObFdItemSet &child_fd_item_set)
{
  int ret = OB_SUCCESS;
  ObFdItem *fd_item = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < child_fd_item_set.count(); ++i) {
    if (OB_ISNULL(fd_item = child_fd_item_set.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (fd_item->is_table_fd_item()) {
      /* do nothing */
    } else if (OB_FAIL(all_fd_item_set.push_back(fd_item))) {
    }
  }
  return ret;
}

int ObLogSet::compute_op_ordering()
{
  int ret = OB_SUCCESS;
  reset_op_ordering();
  if (MERGE_SET == set_algo_ && is_set_distinct()) {
    ObSEArray<OrderItem, 8> ordering;;
    // Query items and index keys may not be one-to-one, this time you need to put the mapped exprs into ordering
    //only merge union generate sorted output
    ObArray<ObRawExpr*> mapped_exprs; // adjusted according to the order of the map array
    ObSEArray<ObRawExpr *, 8> select_exprs;
    ObLogicalOperator *left_child = NULL;
    if (OB_ISNULL(left_child = get_child(ObLogicalOperator::first_child))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(get_stmt()) || OB_UNLIKELY(!get_stmt()->is_select_stmt())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(static_cast<const ObSelectStmt*>(get_stmt())->get_select_exprs(select_exprs))) {
    } else if (map_array_.count() != select_exprs.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid mapping array", K(map_array_), K(select_exprs));
    } else if (OB_FAIL(ObOptimizerUtil::adjust_exprs_by_mapping(
                         select_exprs, map_array_, mapped_exprs))) {
    } else if (OB_FAIL(ObOptimizerUtil::make_sort_keys(mapped_exprs, set_directions_, ordering))) {
    } else if (OB_FAIL(set_op_ordering(ordering))) {
    } else if (!exchange_allocated_ && (DistAlgo::DIST_PARTITION_WISE == set_dist_algo_)) {
      is_range_order_ = left_child->get_is_range_order();
      is_local_order_ = !is_range_order_;
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObLogSet::compute_one_row_info()
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *left_child = NULL;
  ObLogicalOperator *right_child = NULL;
  if (OB_ISNULL(left_child = get_child(ObLogicalOperator::first_child)) ||
      OB_ISNULL(right_child = get_child(ObLogicalOperator::second_child))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (set_op_ == ObSelectStmt::UNION || is_recursive_union()) {
    set_is_at_most_one_row(false);
  } else if (set_op_ == ObSelectStmt::INTERSECT) {
    set_is_at_most_one_row(
        (left_child->get_is_at_most_one_row() || right_child->get_is_at_most_one_row()));
  } else if (set_op_ == ObSelectStmt::EXCEPT) {
    set_is_at_most_one_row(left_child->get_is_at_most_one_row());
  } else { /*do nothing*/ }
  return ret;
}

int ObLogSet::compute_sharding_info()
{
  int ret = OB_SUCCESS;
  ObLogicalOperator *first_child = NULL;
  ObLogicalOperator *second_child = NULL;
  is_partition_wise_ = (set_dist_algo_ == DistAlgo::DIST_PARTITION_WISE ||
                        set_dist_algo_ == DistAlgo::DIST_EXT_PARTITION_WISE);
  if (OB_ISNULL(get_plan()) ||
      OB_ISNULL(first_child = get_child(ObLogicalOperator::first_child)) ||
      OB_ISNULL(second_child = get_child(ObLogicalOperator::second_child))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (DistAlgo::DIST_BASIC_METHOD == set_dist_algo_) {
    if (OB_FAIL(ObOptimizerUtil::compute_basic_sharding_info(
                                    get_plan()->get_optimizer_context().get_local_server_addr(),
                                    get_child_list(),
                                    get_plan()->get_allocator(),
                                    strong_sharding_,
                                    inherit_sharding_index_))) {
    }
  } else if (DistAlgo::DIST_PULL_TO_LOCAL == set_dist_algo_) {
    strong_sharding_ = get_plan()->get_optimizer_context().get_local_sharding();
  } else if (DistAlgo::DIST_SET_RANDOM == set_dist_algo_) {
    strong_sharding_ = get_plan()->get_optimizer_context().get_distributed_sharding();
  } else if ((DistAlgo::DIST_PARTITION_WISE == set_dist_algo_ ||
              DistAlgo::DIST_EXT_PARTITION_WISE == set_dist_algo_) &&
            (ObSelectStmt::UNION == set_op_) && !is_set_distinct()) {
    is_partition_wise_ = true;
    strong_sharding_ = get_plan()->get_optimizer_context().get_distributed_sharding();
  } else if (DistAlgo::DIST_SET_PARTITION_WISE == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = get_plan()->get_optimizer_context().get_distributed_sharding();
  } else if (DistAlgo::DIST_NONE_HASH == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = first_child->get_strong_sharding();    
    inherit_sharding_index_ = ObLogicalOperator::first_child;
  } else if (DistAlgo::DIST_HASH_NONE == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = second_child->get_strong_sharding();
    inherit_sharding_index_ = ObLogicalOperator::second_child;
  } else if (DistAlgo::DIST_NONE_ALL == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = first_child->get_strong_sharding();
    inherit_sharding_index_ = ObLogicalOperator::first_child;
  } else if (DistAlgo::DIST_ALL_NONE == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = second_child->get_strong_sharding();
    inherit_sharding_index_ = ObLogicalOperator::second_child;
  } else if (DistAlgo::DIST_PARTITION_NONE == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = second_child->get_strong_sharding();
    inherit_sharding_index_ = ObLogicalOperator::second_child;
  } else if (DistAlgo::DIST_NONE_PARTITION == set_dist_algo_) {
    is_partition_wise_ = false;
    strong_sharding_ = first_child->get_strong_sharding();
    inherit_sharding_index_ = ObLogicalOperator::first_child;
  } else if (DistAlgo::DIST_HASH_LOCAL_PARTITION == set_dist_algo_ || 
             DistAlgo::DIST_PARTITION_HASH_LOCAL == set_dist_algo_) {
    strong_sharding_ = get_plan()->get_optimizer_context().get_distributed_sharding();
  } else if (OB_FAIL(ObLogicalOperator::compute_sharding_info())) {
  } else { /*do nothing*/ }
  return ret;
}



int ObLogSet::est_cost()
{
  int ret = OB_SUCCESS;
  double card = 0;
  double op_cost = 0;
  double cost = 0;
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

int ObLogSet::est_width()
{
  int ret = OB_SUCCESS;
  double width = 0.0;
  ObLogicalOperator *left_child = get_child(ObLogicalOperator::first_child);
  ObLogicalOperator *right_child = get_child(ObLogicalOperator::second_child);
  if (OB_ISNULL(left_child) || OB_ISNULL(right_child)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    width = std::max(left_child->get_width(), right_child->get_width());
    set_width(width);
  }
  return ret;
}

int ObLogSet::get_re_est_cost_infos(const EstimateCostInfo &param,
                                    ObIArray<ObBasicCostInfo> &cost_infos,
                                    double &child_cost,
                                    double &card)
{
  int ret = OB_SUCCESS;
  child_cost = 0.0;
  card = 0.0;
  EstimateCostInfo cur_param;
  const double need_row_count = (is_recursive_union() || !is_set_distinct())
                                && param.need_row_count_ >= 0 && param.need_row_count_ < card_
                                ? param.need_row_count_ : -1;
  bool need_scale_ndv = (need_row_count == -1);
  double cur_child_card = 0.0;
  double cur_child_cost = 0.0;
  if (OB_UNLIKELY(is_set_distinct() && get_num_of_child() != child_ndv_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected child ndv count", K(child_ndv_));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < get_num_of_child(); ++i) {
    const ObLogicalOperator *child = get_child(i);
    cur_param.reset();
    double origin_child_card = 0;
    if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(cur_param.assign(param))) {
    } else {
      cur_param.need_row_count_ = need_row_count;
      origin_child_card = child->get_card();
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_child(i)->re_est_cost(cur_param, cur_child_card, cur_child_cost))) {
    } else if (OB_FAIL(cost_infos.push_back(ObBasicCostInfo(cur_child_card, cur_child_cost,
                                                            child->get_width())))) {
    } else if (ObSelectStmt::UNION == get_set_op() && !is_set_distinct()) {
      ObSelectStmt::SetOperator set_type = is_recursive_union() ? ObSelectStmt::RECURSIVE : ObSelectStmt::UNION;
      if (0 == i) {
        card = cur_child_card;
      } else {
        card = ObOptSelectivity::get_set_stmt_output_count(card, cur_child_card, set_type);
      }
      child_cost += cur_child_cost;
    } else {
      double cur_child_ndv = child_ndv_.at(i);
      if (need_scale_ndv) {
        cur_child_ndv = std::min(
            cur_child_ndv,
            ObOptSelectivity::scale_distinct(cur_child_card, origin_child_card, cur_child_ndv));
      }
      if (0 == i) {
        card = cur_child_ndv;
      } else {
        card = ObOptSelectivity::get_set_stmt_output_count(card, cur_child_ndv, get_set_op());
      }
      child_cost += cur_child_cost;
    }
  }
  if (OB_SUCC(ret) && is_graph_feedback_loop_) {
    if (OB_UNLIKELY(2 != cost_infos.count()) ||
        OB_UNLIKELY(graph_path_upper_bound_ < 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected graph feedback cost inputs",
               K(ret), K(cost_infos.count()), K(graph_path_upper_bound_));
    } else {
      ObBasicCostInfo &seed_info = cost_infos.at(first_child);
      ObBasicCostInfo &step_info = cost_infos.at(second_child);
      const double seed_rows = std::max(0.0, seed_info.rows_);
      const double first_step_rows = std::max(0.0, step_info.rows_);
      const double first_step_cost = std::max(0.0, step_info.cost_);
      double recursive_rows = 0.0;
      double total_rows = 0.0;
      double relative_work = 0.0;
      estimate_bounded_graph_feedback(seed_rows, first_step_rows,
                                      graph_path_upper_bound_, recursive_rows,
                                      total_rows, relative_work);

      step_info.rows_ = recursive_rows;
      step_info.cost_ = bounded_graph_estimate_multiply(first_step_cost, relative_work);
      card = total_rows;
      child_cost = bounded_graph_estimate_add(std::max(0.0, seed_info.cost_),
                                               step_info.cost_);
    }
  }
  return ret;
}

int ObLogSet::est_ambient_card()
{
  // do nothing
  return OB_SUCCESS;
}

int ObLogSet::do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost)
{
  int ret = OB_SUCCESS;
  op_cost = 0.0;
  cost = 0.0;
  card = 0.0;
  double tmp_card = 0.0;
  double child_cost = 0.0;
  ObSEArray<ObBasicCostInfo, 4> cost_infos;
  const ObSelectStmt *stmt = dynamic_cast<const ObSelectStmt*>(get_stmt());
  if (OB_ISNULL(stmt) || OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_re_est_cost_infos(param, cost_infos, child_cost, tmp_card))) {
  } else if (is_recursive_union() || !is_set_distinct()) {
    ObCostMergeSetInfo cost_info(cost_infos, get_set_op(), stmt->get_select_item_size());
    if (OB_FAIL(ObOptEstCost::cost_union_all(cost_info, 
                                             op_cost, 
                                             get_plan()->get_optimizer_context()))) {
    }
  } else if (MERGE_SET == set_algo_) {
    ObCostMergeSetInfo cost_info(cost_infos, get_set_op(), stmt->get_select_item_size());
    if (OB_FAIL(ObOptEstCost::cost_merge_set(cost_info, 
                                             op_cost, 
                                             get_plan()->get_optimizer_context()))) {
    }
  } else if (HASH_SET == set_algo_) {
    ObSEArray<ObRawExpr*, 8> select_exprs;
    if (OB_UNLIKELY(2 != cost_infos.count())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(stmt->get_select_exprs(select_exprs))) {
    } else {
      ObCostHashSetInfo hash_cost_info(cost_infos.at(0).rows_, cost_infos.at(0).width_,
                                       cost_infos.at(1).rows_, cost_infos.at(1).width_,
                                       get_set_op(), select_exprs,
                                       NULL, NULL /* no need for hash set*/ );
      if (OB_FAIL(ObOptEstCost::cost_hash_set(hash_cost_info, 
                                              op_cost, 
                                              get_plan()->get_optimizer_context()))) {
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }

  if (OB_SUCC(ret)) {
    cost = child_cost + op_cost;
    card = param.need_row_count_ >= 0 && param.need_row_count_ < tmp_card
           ? param.need_row_count_ : tmp_card;
  }
  return ret;
}

int ObLogSet::get_op_exprs(ObIArray<ObRawExpr*> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_set_exprs(all_exprs))) {
  } else if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
  } else { /*do nothing*/ }

  return ret;
}

int ObLogSet::allocate_granule_pre(AllocGIContext &ctx)
{
  int ret = OB_SUCCESS;
  if (!ctx.exchange_above()) {
  } else if (!ctx.is_in_partition_wise_state()
             && !ctx.is_in_pw_affinity_state()
             && DistAlgo::DIST_PARTITION_WISE == set_dist_algo_) {
    /**
     *        (partition wise join below)
     *                   |
     *                 JOIN(1)
     *                   |
     *             --------------
     *             |            |
     *           JOIN(2)       ...
     *             |
     *            ...
     *   JOIN(1) come into this code block.
     *   he will set 'this' ptr to gi allocate ctx as a reset token,
     *   and in the allocate-granule post stage JOIN(1) will
     *   reset the state of gi-allocate ctx.
     */
    ctx.set_in_partition_wise_state(this);
  } else if (ctx.is_in_partition_wise_state()) {
    /**
     *       (partition wise join below)
     *                   |
     *                 JOIN(1)
     *                   |
     *             --------------
     *             |            |
     *           JOIN(2)       ...
     *             |
     *        ------------
     *        |          |
     *       ...        EX(pkey)
     *                   |
     *                  ...
     *   JOIN(2) come into this code block.
     *   If there is a repartition(by key) below this partition wise plan,
     *   for some complex reason, we can not do partition wise join/union.
     *   We allocate gi above the table scan as usual, and set a affinitize property to these GI.
     */
    if (DIST_PARTITION_NONE == set_dist_algo_ ||
        DIST_NONE_PARTITION == set_dist_algo_ ||
        DIST_SET_RANDOM == set_dist_algo_) {
      if (OB_FAIL(ctx.set_pw_affinity_state())) {
      } else { /*do nothing*/ }
    }
  } else { /*do nothing*/ }
  return ret;
}

int ObLogSet::allocate_granule_post(AllocGIContext &ctx)
{
  int ret = OB_SUCCESS;
  /**
   *       (partition wise join below)
   *                   |
   *                 JOIN(1)
   *                   |
   *             --------------
   *             |            |
   *           JOIN(2)       ...
   *             |
   *            ...
   *   JOIN(1) will reset the state of gi allocate ctx.
   *   As the ctx has record the state was changed by JOIN(2),
   *   so JOIN(2) can not reset this state.
   */
  if (!ctx.exchange_above()) {
  } else if (ctx.is_in_partition_wise_state()) {
    if (ctx.is_op_set_pw(this)) {
      ctx.alloc_gi_ = true;
      if (OB_FAIL(allocate_granule_nodes_above(ctx))) {
      }
      IGNORE_RETURN ctx.reset_info();
    }
  } else if (ctx.is_in_pw_affinity_state()) {
    if (ctx.is_op_set_pw(this)) {
      ctx.alloc_gi_ = true;
      if (OB_FAIL(allocate_gi_recursively(ctx))) {
      }
      IGNORE_RETURN ctx.reset_info();
    }
  } else if (DIST_NONE_PARTITION == set_dist_algo_) {
    if (OB_FAIL(set_granule_nodes_affinity(ctx, 0))) {
    }
  } else if (DIST_PARTITION_NONE == set_dist_algo_) {
    if (OB_FAIL(set_granule_nodes_affinity(ctx, 1))) {
    }
  } else { /*do nothing*/ }

  return ret;
}

uint64_t ObLogSet::hash(uint64_t seed) const
{
  seed = do_hash(is_distinct_, seed);
  seed = do_hash(set_op_, seed);
  seed = do_hash(is_graph_feedback_loop_, seed);
  seed = do_hash(graph_path_lower_bound_, seed);
  seed = do_hash(graph_path_upper_bound_, seed);
  seed = do_hash(graph_path_reverse_, seed);
  seed = ObLogicalOperator::hash(seed);

  return seed;
}

int ObLogSet::get_set_exprs(ObIArray<ObRawExpr *> &set_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_stmt()) || OB_UNLIKELY(!get_stmt()->is_select_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObSelectStmt *sel_stmt = static_cast<const ObSelectStmt *>(get_stmt());
    if (OB_FAIL(sel_stmt->get_select_exprs(set_exprs))) {
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObLogSet::get_pure_set_exprs(ObIArray<ObRawExpr *> &set_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_stmt()) || OB_UNLIKELY(!get_stmt()->is_select_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObSelectStmt *sel_stmt = static_cast<const ObSelectStmt *>(get_stmt());
    if (OB_FAIL(sel_stmt->get_pure_set_exprs(set_exprs))) {
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObLogSet::allocate_startup_expr_post()
{
  int ret = OB_SUCCESS;
  if (ObSelectStmt::UNION == set_op_) {
    //do nothing
  } else if (ObSelectStmt::INTERSECT == set_op_) {
    if (OB_FAIL(ObLogicalOperator::allocate_startup_expr_post())) {
    }
  } else if (ObSelectStmt::EXCEPT == set_op_) {
    if (OB_FAIL(ObLogicalOperator::allocate_startup_expr_post(first_child))) {
    }
  }
  return ret;
}

int ObLogSet::print_outline_data(PlanText &plan_text)
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  const ObDMLStmt *stmt = NULL;
  ObString qb_name;
  ObPQSetHint hint;
  bool has_push_down = false;
  if (OB_ISNULL(get_plan()) || OB_ISNULL(stmt = get_plan()->get_stmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(stmt->get_qb_name(qb_name))) {
  } else if (OB_FAIL(check_has_push_down(has_push_down))) {
  } else if (has_push_down &&
             OB_FAIL(BUF_PRINTF("%s%s(@\"%.*s\")",
                                ObQueryHint::get_outline_indent(plan_text.is_oneline_),
                                ObHint::get_hint_name(T_DISTINCT_PUSHDOWN),
                                qb_name.length(),
                                qb_name.ptr()))) {
  } else if (HASH_SET == set_algo_ &&
             OB_FAIL(BUF_PRINTF("%s%s(@\"%.*s\")",
                                ObQueryHint::get_outline_indent(plan_text.is_oneline_),
                                ObHint::get_hint_name(T_USE_HASH_SET),
                                qb_name.length(), qb_name.ptr()))) {
  } else if (OB_FAIL(construct_pq_set_hint(hint))) {
  } else if (hint.get_dist_methods().empty() && hint.get_left_branch().empty()) {
    /*do nothing*/
  } else if (OB_FALSE_IT(hint.set_qb_name(qb_name))) {
  } else if (OB_FAIL(hint.print_hint(plan_text))) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogSet::print_used_hint(PlanText &plan_text)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObHint *use_hash = get_plan()->get_log_plan_hint().get_normal_hint(T_USE_HASH_SET);
    const ObHint *pushdown = get_plan()->get_log_plan_hint().get_normal_hint(T_DISTINCT_PUSHDOWN);
    const bool algo_match = NULL != use_hash &&
                            ((HASH_SET == set_algo_ && use_hash->is_enable_hint())
                             || (MERGE_SET == set_algo_ && use_hash->is_disable_hint()));
    const ObPQSetHint *used_pq_hint = NULL;
    if (algo_match && OB_FAIL(use_hash->print_hint(plan_text))) {
    } else if (OB_FAIL(get_used_pq_set_hint(used_pq_hint))) {
    } else if (NULL != used_pq_hint && OB_FAIL(used_pq_hint->print_hint(plan_text))) {
    } else if (NULL != pushdown) {
      bool has_push_down = false;
      if (OB_FAIL(check_has_push_down(has_push_down))) {
      } else {
        bool pushdown_match = has_push_down ? pushdown->is_enable_hint()
                                            : pushdown->is_disable_hint();
        if (pushdown_match && OB_FAIL(pushdown->print_hint(plan_text))) {
        }
      }
    }
  }
  return ret;
}

int ObLogSet::get_used_pq_set_hint(const ObPQSetHint *&used_hint)
{
  int ret = OB_SUCCESS;
  used_hint = NULL;
  const ObHint *stmt_pq_set = NULL;
  if (OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (NULL != (stmt_pq_set = get_plan()->get_log_plan_hint().get_normal_hint(T_PQ_SET))) {
    ObPQSetHint hint;
    used_hint = static_cast<const ObPQSetHint*>(stmt_pq_set);
    const ObIArray<ObItemType> &dist_methods = used_hint->get_dist_methods();
    if (OB_FAIL(construct_pq_set_hint(hint))) {
    } else if (dist_methods.count() != hint.get_dist_methods().count()
               || 0 != hint.get_left_branch().case_compare(used_hint->get_left_branch())) {
      used_hint = NULL;
    } else {
      for (int64_t i = 0; NULL != used_hint && i < dist_methods.count(); ++i) {
        if (dist_methods.at(i) != hint.get_dist_methods().at(i)) {
          used_hint = NULL;
        }
      }
    }
  }
  return ret;
}

int ObLogSet::construct_pq_set_hint(ObPQSetHint &hint)
{
  int ret = OB_SUCCESS;
  const ObSelectStmt *stmt = static_cast<const ObSelectStmt *>(get_stmt());
  const ObSelectStmt *left_stmt = get_left_stmt();
  ObString left_branch;
  if (OB_ISNULL(stmt) || OB_ISNULL(left_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!stmt->is_set_distinct()
             || 2 < stmt->get_set_query().count()
             || stmt->get_set_query(0) == left_stmt) {
    /* do nothing */
  } else if (OB_FAIL(left_stmt->get_qb_name(left_branch))) {
  } else {
    hint.set_left_branch(left_branch);
  }

  if (DistAlgo::DIST_BASIC_METHOD != set_dist_algo_) {
    int64_t random_none_idx = OB_INVALID_INDEX;
    if (DistAlgo::DIST_SET_RANDOM == set_dist_algo_) {
      const ObLogicalOperator *child = NULL;
      for (int64_t i = 0; OB_SUCC(ret) && i < get_num_of_child(); ++i) {
        if (OB_ISNULL(child = get_child(i))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (DistAlgo::DIST_SET_RANDOM == set_dist_algo_
                   && LOG_EXCHANGE != child->get_type()) {
          random_none_idx = i;
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(hint.set_pq_set_hint(set_dist_algo_, 
                                                     get_num_of_child(), 
                                                     random_none_idx))) {
    }
  }
  return ret;
}

int ObLogSet::check_has_push_down(bool &has_push_down)
{
  int ret = OB_SUCCESS;
  has_push_down = false;
  for (int64_t i = 0; OB_SUCC(ret) && !has_push_down && i < get_num_of_child(); ++i) {
    const ObLogicalOperator *child = NULL;
    const ObLogicalOperator *pushdown_op = NULL;
    if (OB_ISNULL(child = get_child(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(child->get_pushdown_op(log_op_def::LOG_DISTINCT, pushdown_op))) {
    } else if (NULL == pushdown_op) {
      // do nothing
    } else if (OB_UNLIKELY(log_op_def::LOG_DISTINCT != pushdown_op->get_type())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (static_cast<const ObLogDistinct*>(pushdown_op)->is_push_down()) {
      has_push_down = true;
    }
  }
  return ret;
}

int ObLogSet::compute_op_parallel_info()
{
  int ret = OB_SUCCESS;
  if (is_graph_feedback_loop_) {
    // A feedback frontier has a single owner. DAS below the recursive branch
    // may still batch remote/local reads, but the loop itself must not be split
    // into PX workers because that would duplicate or lose frontier states.
    set_parallel(ObGlobalHint::DEFAULT_PARALLEL);
    set_available_parallel(ObGlobalHint::DEFAULT_PARALLEL);
    set_op_parallel_rule(OpParallelRule::OP_DAS_DOP);
  } else if (OB_FAIL(compute_normal_multi_child_parallel_info())) {
  } else if (DistAlgo::DIST_PARTITION_WISE == get_distributed_algo()) {
    ObLogicalOperator *child = get_child(first_child);
    if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (child->get_part_cnt() > 0 &&
               get_parallel() > child->get_part_cnt()) {
      int64_t reduce_parallel = child->get_part_cnt();
      reduce_parallel = reduce_parallel < 2 ? 2 : reduce_parallel;
      set_parallel(reduce_parallel);
      need_re_est_child_cost_ = true;
    }
  } else if (DistAlgo::DIST_PARTITION_NONE == get_distributed_algo()) {
    ObLogicalOperator *child = get_child(second_child);
    if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (child->get_part_cnt() > 0 &&
               get_parallel() > child->get_part_cnt()) {
      int64_t reduce_parallel = child->get_part_cnt();
      reduce_parallel = reduce_parallel < 2 ? 2 : reduce_parallel;
      set_parallel(reduce_parallel);
      need_re_est_child_cost_ = true;
    }
  } else if (DistAlgo::DIST_NONE_PARTITION == get_distributed_algo()) {
    ObLogicalOperator *child = get_child(first_child);
    if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (child->get_part_cnt() > 0 &&
               get_parallel() > child->get_part_cnt()) {
      int64_t reduce_parallel = child->get_part_cnt();
      reduce_parallel = reduce_parallel < 2 ? 2 : reduce_parallel;
      set_parallel(reduce_parallel);
      need_re_est_child_cost_ = true;
    }
  }
  return ret;
}

int ObLogSet::is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed)
{
  int ret = OB_SUCCESS;
  is_fixed = false;
  ObSEArray<ObRawExpr*, 8> set_exprs;
  if (OB_FAIL(get_set_exprs(set_exprs))) {
  } else {
    is_fixed = ObOptimizerUtil::find_item(set_exprs, expr);
  }
  return ret;
}

int ObLogSet::get_card_without_filter(double &card)
{
  int ret = OB_SUCCESS;
  card = 0.0;
  if (is_graph_feedback_loop_) {
    if (OB_UNLIKELY(2 != get_num_of_child()) ||
        OB_ISNULL(get_child(first_child)) ||
        OB_ISNULL(get_child(second_child))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected graph feedback children", K(ret), K(get_num_of_child()));
    } else {
      double recursive_rows = 0.0;
      double relative_work = 0.0;
      estimate_bounded_graph_feedback(get_child(first_child)->get_card(),
                                      get_child(second_child)->get_card(),
                                      graph_path_upper_bound_, recursive_rows,
                                      card, relative_work);
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && !is_graph_feedback_loop_
       && i < get_num_of_child(); ++i) {
    const ObLogicalOperator *child = get_child(i);
    if (ObSelectStmt::UNION == get_set_op() && !is_set_distinct()) {
      ObSelectStmt::SetOperator set_type = is_recursive_union() ? ObSelectStmt::RECURSIVE : ObSelectStmt::UNION;
      if (0 == i) {
        card = child->get_card();
      } else {
        card = ObOptSelectivity::get_set_stmt_output_count(card, child->get_card(), set_type);
      }
    } else if (0 == i) {
      card = child_ndv_.at(i);
    } else {
      card = ObOptSelectivity::get_set_stmt_output_count(card, child_ndv_.at(i), get_set_op());
    }
  }
  return ret;
}

int ObLogSet::check_use_child_ordering(bool &used, int64_t &inherit_child_ordering_index)
{
  int ret = OB_SUCCESS;
  used = true;
  inherit_child_ordering_index = first_child;
  if (HASH_SET == get_algo()) {
    inherit_child_ordering_index = -1;
    used = false;
  } else if (!is_set_distinct()) {
    used = false;
  }
  if (is_recursive_union()) {
    inherit_child_ordering_index = -1;
  }
  return ret;
}
