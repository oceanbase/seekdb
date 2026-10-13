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
#include "sql/optimizer/ob_log_join.h"
#include "sql/optimizer/ob_log_sort.h"
#include "sql/optimizer/ob_log_table_scan.h"
#include "sql/optimizer/ob_opt_est_cost.h"
#include "sql/resolver/ddl/ob_explain_stmt.h"
#include "sql/resolver/dml/ob_select_stmt.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::log_op_def;

namespace
{

const double GRAPH_FEEDBACK_ESTIMATE_LIMIT = 1.0e15;
const int64_t GRAPH_FEEDBACK_DEPTH_COLUMN_COUNT = 1;
const char *GRAPH_SEED_VERTEX_ALIAS = "__g_seed_vertex";
const char *GRAPH_STEP_EDGE_ALIAS = "__g_step_edge";
const char *GRAPH_STEP_VERTEX_ALIAS = "__g_step_vertex";

// Native expansion bypasses the recursive child's joins. Only parameters
// bound outside that statement survive the bypass and stay constant per rescan.
bool is_outer_graph_parameter(const ObRawExpr &expr, const ObLogTableScan &edge)
{
  bool safe = expr.is_exec_param_expr() && edge.get_stmt() != nullptr;
  if (safe) {
    const ObExecParamRawExpr &param = static_cast<const ObExecParamRawExpr &>(expr);
    const ObRawExpr *ref = param.get_ref_expr();
    safe = !param.is_onetime() && ref != nullptr && ref->is_column_ref_expr();
    if (safe) {
      const ObColumnRefRawExpr &column = *static_cast<const ObColumnRefRawExpr *>(ref);
      safe = !column.is_cte_generated_column()
          && edge.get_stmt()->get_table_item_by_id(column.get_table_id()) == nullptr;
    }
    // These scalar values serialize by value, not as LOB/JSON/UDT locators.
    if (safe) {
      switch (expr.get_data_type()) {
        case ObNullType:
        case ObTinyIntType: case ObSmallIntType: case ObMediumIntType:
        case ObInt32Type: case ObIntType:
        case ObUTinyIntType: case ObUSmallIntType: case ObUMediumIntType:
        case ObUInt32Type: case ObUInt64Type:
        case ObFloatType: case ObDoubleType:
        case ObNumberType: case ObUNumberType: case ObDecimalIntType:
        case ObVarcharType: case ObCharType:
        case ObDateTimeType: case ObTimestampType: case ObDateType:
        case ObMySQLDateType: case ObMySQLDateTimeType:
        case ObTimeType: case ObYearType:
          break;
        default:
          safe = false;
      }
    }
  }
  return safe;
}

bool has_only_outer_graph_parameters(const ObRawExpr &root, const ObLogTableScan &edge)
{
  ObArray<const ObRawExpr *> pending;
  bool safe = OB_SUCCESS == pending.push_back(&root);
  while (safe && !pending.empty()) {
    const ObRawExpr *expr = pending.at(pending.count() - 1);
    pending.pop_back();
    safe = expr != nullptr;
    if (safe && expr->is_exec_param_expr()) {
      safe = is_outer_graph_parameter(*expr, edge);
    } else if (safe && expr->has_flag(CNT_DYNAMIC_PARAM)) {
      safe = expr->get_param_count() > 0;
      for (int64_t i = 0; safe && i < expr->get_param_count(); ++i) {
        safe = OB_SUCCESS == pending.push_back(expr->get_param_expr(i));
      }
    }
  }
  return safe;
}

bool are_native_scan_filters_supported(
    const ObIArray<ObRawExpr *> &filters)
{
  bool supported = true;
  for (int64_t i = 0; supported && i < filters.count(); ++i) {
    supported = filters.at(i) != nullptr && filters.at(i)->is_deterministic();
  }
  return supported;
}

// Only these generated equalities are enforced by GraphExpand's source
// association and endpoint lookup. Other range predicates must still be
// evaluated after the native executor replaces the relational scan ranges.
bool is_native_identity_range(const ObRawExpr &expr,
                              const ObLogTableScan &scan,
                              const ObLogTableScan &edge_scan,
                              const GraphExpandAccessDesc &access)
{
  bool identity_range = false;
  if (expr.get_expr_type() == T_OP_EQ && expr.get_param_count() == 2) {
    for (int64_t side = 0; !identity_range && side < 2; ++side) {
      const ObRawExpr *column = expr.get_param_expr(side);
      const ObRawExpr *param = expr.get_param_expr(1 - side);
      if (column != nullptr && column->is_column_ref_expr()
          && param != nullptr && param->is_exec_param_expr()) {
        const ObRawExpr *ref = static_cast<const ObExecParamRawExpr *>(param)->get_ref_expr();
        if (ref != nullptr && ref->is_column_ref_expr()) {
          const ObColumnRefRawExpr &key = *static_cast<const ObColumnRefRawExpr *>(column);
          const ObColumnRefRawExpr &bound = *static_cast<const ObColumnRefRawExpr *>(ref);
          const bool is_edge = &scan == &edge_scan;
          const int64_t key_count = is_edge ? access.source_key_count_ : access.target_key_count_;
          for (int64_t i = 0; !identity_range && i < key_count; ++i) {
            identity_range = key.get_table_id() == scan.get_table_id()
                && key.get_column_id() == (is_edge ? access.edge_current_columns_[i]
                                                  : access.target_key_columns_[i])
                && (is_edge
                    ? bound.is_cte_generated_column()
                        && bound.get_cte_generate_column_projector_offset()
                            == GRAPH_FEEDBACK_DEPTH_COLUMN_COUNT + access.source_key_count_ + i
                    : bound.get_table_id() == edge_scan.get_table_id()
                        && bound.get_column_id() == access.edge_next_columns_[i]);
          }
        }
      }
    }
  }
  return identity_range;
}

int retain_native_edge_range_filters(ObLogTableScan &edge_scan,
                                     const ObLogTableScan &target_scan,
                                     const GraphExpandAccessDesc &access,
                                     bool &supported)
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr *> edge_filters;
  const ObLogTableScan *scans[] = {&edge_scan, &target_scan};
  for (int64_t s = 0; OB_SUCC(ret) && supported && s < ARRAYSIZEOF(scans); ++s) {
    const ObLogTableScan &scan = *scans[s];
    for (int64_t i = 0; OB_SUCC(ret) && supported && i < scan.get_range_conditions().count(); ++i) {
      ObRawExpr *expr = scan.get_range_conditions().at(i);
      if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (is_native_identity_range(*expr, scan, edge_scan, access)) {
        // The native frontier/lookup enforces this generated join equality.
      } else if (&scan != &edge_scan || !expr->is_deterministic()
                 || !has_only_outer_graph_parameters(*expr, edge_scan)) {
        // Target lookup is existence-only, and recursive join parameters are
        // not populated when that child is bypassed. Keep these on fallback.
        supported = false;
      } else if (OB_FAIL(add_var_to_array_no_dup(edge_filters, expr))) {
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && supported && i < edge_filters.count(); ++i) {
    ObArray<ObRawExpr *> columns;
    if (OB_FAIL(ObRawExprUtils::extract_column_exprs(edge_filters.at(i), columns))) {
    } else if (OB_FAIL(add_var_to_array_no_dup(edge_scan.get_filter_exprs(), edge_filters.at(i)))) {
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < columns.count(); ++j) {
      if (OB_FAIL(add_var_to_array_no_dup(edge_scan.get_access_exprs(), columns.at(j)))) {
      } else if (OB_FAIL(edge_scan.add_runtime_access_expr(columns.at(j)))) {
      }
    }
  }
  // The normal table-scan expression allocation pass reclassifies these
  // filters for covering scans and index-back before code generation.
  return ret;
}

bool is_native_graph_step_supported(
    const ObLogicalOperator *root,
    const ObLogTableScan *edge_scan,
    const ObLogTableScan *target_scan,
    const GraphExpandAccessDesc &access)
{
  bool supported = root != nullptr;
  if (supported && root == edge_scan) {
    // GraphExpand evaluates filters copied into the edge scan descriptor. Keep
    // volatile filters on the relational path: their evaluation count and
    // side effects must not change when the recursive child is bypassed.
    supported = are_native_scan_filters_supported(root->get_filter_exprs())
        && are_native_scan_filters_supported(root->get_startup_exprs());
    const ObIArray<ObRawExpr *> *filters[] = {
        &root->get_filter_exprs(), &root->get_startup_exprs()};
    for (int64_t a = 0; supported && a < ARRAYSIZEOF(filters); ++a) {
      for (int64_t i = 0; supported && i < filters[a]->count(); ++i) {
        const ObRawExpr &expr = *filters[a]->at(i);
        if (a == 0 && is_native_identity_range(expr, *edge_scan, *edge_scan, access)) {
          // Frontier identity matching replaces this generated equality.
          // Codegen checks its final placement before removing the native copy;
          // batch-scan selection/filter pushdown have not been finalized here.
        } else {
          supported = has_only_outer_graph_parameters(expr, *edge_scan);
        }
      }
    }
  } else if (supported && root == target_scan) {
    // The current vertex lookup validates identity/existence only. Until it
    // evaluates target scan filters with relational cadence, any such filter
    // must keep the recursive step on the fallback runtime.
    supported = root->get_filter_exprs().empty()
        && root->get_startup_exprs().empty();
  } else if (supported && root->get_type() != LOG_TABLE_SCAN) {
    // Filters owned by joins/sorts/materialization are not represented in a
    // GraphExpandScanDesc, so native execution cannot silently skip them.
    supported = root->get_filter_exprs().empty()
        && root->get_startup_exprs().empty();
    if (supported && root->get_type() == LOG_JOIN) {
      const ObLogJoin *join = static_cast<const ObLogJoin *>(root);
      supported = join->get_other_join_conditions().empty();
    }
  }
  for (int64_t i = 0; supported && i < root->get_num_of_child(); ++i) {
    supported = is_native_graph_step_supported(
        root->get_child(i), edge_scan, target_scan, access);
  }
  return supported;
}

const ObLogTableScan *find_internal_scan(const ObLogicalOperator *root,
                                         const char *table_alias);

ObLogTableScan *find_internal_scan(ObLogicalOperator *root,
                                   const char *table_alias)
{
  return const_cast<ObLogTableScan *>(find_internal_scan(
      static_cast<const ObLogicalOperator *>(root), table_alias));
}

int enable_graph_vertex_lookup(ObLogTableScan &scan)
{
  int ret = OB_SUCCESS;
  scan.set_needs_graph_vertex_lookup(true);
  if (scan.get_is_index_global()
      && scan.get_global_index_back_table_partition_info() == nullptr) {
    const AccessPath *access_path = scan.get_access_path();
    if (OB_ISNULL(access_path) || OB_ISNULL(access_path->parent_)
        || OB_ISNULL(access_path->parent_->get_table_partition_info())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph vertex base table location is missing", K(ret),
               K(scan.get_table_id()), K(scan.get_real_ref_table_id()),
               K(scan.get_real_index_table_id()), KP(access_path));
    } else {
      scan.set_global_index_back_table_partition_info(
          access_path->parent_->get_table_partition_info());
    }
  }
  return ret;
}

int retain_graph_edge_columns(
    ObLogTableScan &scan,
    const GraphExpandAccessDesc &access_desc,
    share::schema::ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  ObLogPlan *plan = scan.get_plan();
  const share::schema::ObTableSchema *edge_table = nullptr;
  const uint64_t *column_groups[] = {
      access_desc.edge_current_columns_,
      access_desc.edge_key_columns_,
      access_desc.edge_next_columns_};
  const int64_t column_counts[] = {
      access_desc.source_key_count_,
      access_desc.edge_key_count_,
      access_desc.target_key_count_};
  if (OB_ISNULL(plan) || OB_UNLIKELY(!access_desc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(schema_guard.get_table_schema(
                 access_desc.edge_table_id_, edge_table))) {
    LOG_WARN("failed to get graph edge table schema", K(ret),
             K(access_desc.edge_table_id_));
  } else if (OB_ISNULL(edge_table)) {
    ret = OB_SCHEMA_ERROR;
    LOG_WARN("graph edge table schema is missing", K(ret),
             K(access_desc.edge_table_id_));
  }
  for (int64_t group = 0;
       OB_SUCC(ret) && group < ARRAYSIZEOF(column_groups);
       ++group) {
    for (int64_t i = 0; OB_SUCC(ret) && i < column_counts[group]; ++i) {
      const uint64_t column_id = column_groups[group][i];
      ObRawExpr *expr = plan->get_column_expr_by_id(scan.get_table_id(),
                                                    column_id);
      const share::schema::ObColumnSchemaV2 *column_schema = nullptr;
      ColumnItem column_item;
      if (OB_ISNULL(expr)) {
        if (OB_ISNULL(column_schema = edge_table->get_column_schema(column_id))) {
          ret = OB_SCHEMA_ERROR;
          LOG_WARN("graph edge column schema is missing", K(ret),
                   K(column_id), K(access_desc.edge_table_id_));
        } else if (OB_FAIL(plan->generate_column_expr(
                       plan->get_optimizer_context().get_expr_factory(),
                       scan.get_table_id(), *column_schema, column_item))) {
          LOG_WARN("failed to generate graph edge column expression", K(ret),
                   K(column_id), K(scan.get_table_id()));
        } else {
          expr = column_item.expr_;
        }
      }
      if (OB_SUCC(ret)
          && OB_FAIL(add_var_to_array_no_dup(scan.get_access_exprs(), expr))) {
        LOG_WARN("failed to add graph edge access expression", K(ret),
                 K(column_id), K(scan.get_table_id()));
      } else if (OB_SUCC(ret) && OB_FAIL(scan.add_runtime_access_expr(expr))) {
        LOG_WARN("failed to retain graph edge access expression", K(ret),
                 K(column_id), K(scan.get_table_id()));
      }
    }
  }
  return ret;
}

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

const ObLogTableScan *find_internal_scan(const ObLogicalOperator *root,
                                         const char *table_alias)
{
  const ObLogTableScan *result = nullptr;
  if (root != nullptr && root->get_type() == LOG_TABLE_SCAN) {
    const ObLogTableScan *scan = static_cast<const ObLogTableScan *>(root);
    if (scan->get_table_name().case_compare(table_alias) == 0) {
      result = scan;
    }
  }
  for (int64_t i = 0; result == nullptr && root != nullptr
                      && i < root->get_num_of_child(); ++i) {
    result = find_internal_scan(
        static_cast<const ObLogicalOperator *>(root->get_child(i)),
        table_alias);
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
    case GraphPathMode::ACYCLIC:
      name = "ACYCLIC";
      break;
    default:
      break;
  }
  return name;
}

} // namespace

bool GraphFeedbackLoopLogOp::can_use_basic_native_runtime() const
{
  const ObLogicalOperator *anchor = get_child(first_child);
  return anchor != nullptr && native_step_supported_
      && supports_basic_native_graph_feedback(
             path_desc_, expand_access_desc_,
             anchor->get_output_exprs().count(), path_payload_exprs_.count());
}

bool GraphFeedbackLoopLogOp::can_reuse_adjacency(ObIArray<ObRawExpr *> *parameters) const
{
  if (parameters != nullptr) { parameters->reuse(); }
  const ObLogTableScan *edge = find_internal_scan(get_child(second_child), GRAPH_STEP_EDGE_ALIAS);
  const ObDMLStmt *root = get_plan()->get_optimizer_context().get_root_stmt();
  // EXPLAIN describes the enclosed statement's eligibility. The wrapper is
  // not a SELECT, but must not make a reusable SELECT appear non-reusable.
  // The runtime still independently rejects non-plain/locking statements.
  if (root != nullptr && root->is_explain_stmt()) {
    root = static_cast<const ObExplainStmt *>(root)->get_explain_query_stmt();
  }
  bool safe = can_use_basic_native_runtime() && edge != nullptr
      && root != nullptr
      && root->is_select_stmt() && !static_cast<const ObSelectStmt *>(root)->has_for_update();
  ObArray<const ObRawExpr *> pending;
  const ObIArray<ObRawExpr *> *filters[] = {edge == nullptr ? nullptr : &edge->get_filter_exprs(),
      edge == nullptr ? nullptr : &edge->get_startup_exprs()};
  for (int64_t a = 0; safe && a < ARRAYSIZEOF(filters); ++a) {
    for (int64_t i = 0; safe && i < filters[a]->count(); ++i) {
      const ObRawExpr *expr = filters[a]->at(i);
      if (a == 0 && expr != nullptr
          && is_native_identity_range(*expr, *edge, *edge, expand_access_desc_)) {
        // Native frontier matching replaces this recursive join parameter.
      } else {
        safe = OB_SUCCESS == pending.push_back(expr);
      }
    }
  }
  for (int64_t i = 0; safe && i < path_payload_exprs_.count(); ++i) {
    ObRawExpr *payload = path_payload_exprs_.at(i);
    safe = payload != nullptr && payload->get_param_count() == 3
        && OB_SUCCESS == pending.push_back(payload->get_param_expr(2));
  }
  // Only pure scalar operations over this edge and statement constants are
  // reusable. Every proven outer parameter enters the cache key; parameters
  // produced by the bypassed recursive child and unproven expressions do not.
  while (safe && !pending.empty()) {
    const ObRawExpr *expr = pending.at(pending.count() - 1);
    pending.pop_back();
    safe = expr != nullptr && expr->is_deterministic();
    if (safe && expr->is_exec_param_expr()) {
      safe = is_outer_graph_parameter(*expr, *edge);
      if (safe && parameters != nullptr) {
        safe = OB_SUCCESS == add_var_to_array_no_dup(*parameters, const_cast<ObRawExpr *>(expr));
      }
    } else if (safe && expr->is_column_ref_expr()) {
      safe = static_cast<const ObColumnRefRawExpr *>(expr)->get_table_id() == edge->get_table_id();
    } else if (safe && !expr->is_const_expr()) {
      safe = (expr->is_op_expr() || expr->get_expr_type() == T_FUN_SYS_CAST)
          && expr->get_param_count() > 0;
      for (int64_t i = 0; safe && i < expr->get_param_count(); ++i) {
        safe = OB_SUCCESS == pending.push_back(expr->get_param_expr(i));
      }
    }
  }
  if (!safe && parameters != nullptr) { parameters->reuse(); }
  return safe;
}

int GraphFeedbackLoopLogOp::initialize_path_payloads(ObLogTableScan &edge_scan)
{
  int ret = OB_SUCCESS;
  path_payload_exprs_.reuse();
  const ObSelectStmt *stmt = static_cast<const ObSelectStmt *>(get_stmt());
  const ObSelectStmt *step = stmt == nullptr ? nullptr : stmt->get_set_query(1);
  const int64_t payload_begin = GRAPH_FEEDBACK_DEPTH_COLUMN_COUNT
      + expand_access_desc_.source_key_count_ + expand_access_desc_.target_key_count_
      + (path_desc_.needs_edge_key_history()
         ? path_desc_.upper_bound_ * expand_access_desc_.edge_key_count_ : 0);
  if (!native_step_supported_ || path_desc_.path_mode_ != GraphPathMode::WALK) {
  } else if (OB_ISNULL(step)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = payload_begin; OB_SUCC(ret) && native_step_supported_
         && i < step->get_select_item_size(); ++i) {
      ObRawExpr *expr = nullptr;
      ObArray<ObRawExpr *> columns;
      if (OB_FAIL(ObRawExprUtils::erase_inner_added_exprs(
              step->get_select_item(i).expr_, expr))) {
      }
      // Recursive CTE select items carry a column conversion to the declared
      // accumulator type. Only bypass the identity JSON-to-JSON conversion;
      // other conversions must retain the relational execution semantics.
      if (OB_SUCC(ret) && expr != nullptr && expr->get_expr_type() == T_FUN_COLUMN_CONV
          && expr->get_param_count() > 4 && expr->get_param_expr(4) != nullptr
          && expr->get_result_type().get_type() == ObJsonType
          && expr->get_param_expr(4)->get_result_type().get_type() == ObJsonType) {
        expr = expr->get_param_expr(4);
      }
      if (OB_FAIL(ret)) {
      } else if (expr == nullptr || expr->get_expr_type() != T_FUN_SYS_JSON_ARRAY_APPEND
                 || expr->get_param_count() != 3) {
        native_step_supported_ = false;
      } else {
        ObRawExpr *value = expr->get_param_expr(2);
        if (value == nullptr || !value->is_deterministic()
            || value->has_flag(CNT_DYNAMIC_PARAM)) {
          native_step_supported_ = false;
        } else if (OB_FAIL(ObRawExprUtils::extract_column_exprs(value, columns))) {
        }
        for (int64_t j = 0; OB_SUCC(ret) && native_step_supported_
             && j < columns.count(); ++j) {
          native_step_supported_ = columns.at(j)->is_column_ref_expr()
              && static_cast<ObColumnRefRawExpr *>(columns.at(j))->get_table_id()
                  == edge_scan.get_table_id();
          if (native_step_supported_
              && OB_FAIL(edge_scan.add_runtime_access_expr(columns.at(j)))) {
          }
        }
        if (OB_SUCC(ret) && native_step_supported_
            && OB_FAIL(path_payload_exprs_.push_back(expr))) {
        }
      }
    }
  }
  if (!native_step_supported_) {
    path_payload_exprs_.reuse();
  }
  return ret;
}

int GraphFeedbackLoopLogOp::initialize_expand_access()
{
  int ret = OB_SUCCESS;
  native_step_supported_ = false;
  ObLogicalOperator *anchor = get_child(first_child);
  ObLogicalOperator *step = get_child(second_child);
  ObLogTableScan *source_scan = nullptr;
  ObLogTableScan *edge_scan = nullptr;
  ObLogTableScan *target_scan = nullptr;
  share::schema::ObSchemaGetterGuard *schema_guard = nullptr;
  if (OB_ISNULL(get_plan())
      || OB_ISNULL(schema_guard = get_plan()->get_optimizer_context().get_schema_guard())
      || OB_ISNULL(anchor) || OB_ISNULL(step)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback access dependencies are missing", K(ret),
             K(get_plan()), K(schema_guard), K(anchor), K(step));
  } else if (OB_ISNULL(source_scan = find_internal_scan(
                           anchor, GRAPH_SEED_VERTEX_ALIAS))
             || OB_ISNULL(edge_scan = find_internal_scan(
                              step, GRAPH_STEP_EDGE_ALIAS))
             || OB_ISNULL(target_scan = find_internal_scan(
                                step, GRAPH_STEP_VERTEX_ALIAS))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback internal scan is missing", K(ret),
             K(source_scan), K(edge_scan), K(target_scan), KPC(anchor),
             KPC(step));
  } else if (OB_FAIL(expand_access_desc_.init(
                 path_desc_, edge_scan->get_index_table_id(), *schema_guard))) {
    LOG_WARN("failed to initialize graph expand access", K(ret), K_(path_desc),
             "edge_access_table_id", edge_scan->get_index_table_id());
  } else if (OB_FAIL(retain_graph_edge_columns(
                 *edge_scan, expand_access_desc_, *schema_guard))) {
    LOG_WARN("failed to retain graph edge access columns", K(ret),
             K_(expand_access_desc));
  } else if (OB_FAIL(enable_graph_vertex_lookup(*source_scan))) {
  } else if (OB_FAIL(enable_graph_vertex_lookup(*target_scan))) {
  } else {
    // These scans may use a covering secondary index for their relational
    // child plan. GraphExpand still needs an unfiltered base-table ctdef to
    // validate each endpoint by its declared graph key.
    step_access_method_ = expand_access_desc_.uses_adjacency_index()
        ? GraphFeedbackAccessMethod::INDEX_SCAN
        : GraphFeedbackAccessMethod::FULL_SCAN;
    // The current DAS descriptor describes one endpoint orientation. Keep the
    // bidirectional relational step intact rather than dropping its OR branch.
    native_step_supported_ = path_desc_.direction_ != GraphPathDirection::BOTH
        && is_native_graph_step_supported(step, edge_scan, target_scan, expand_access_desc_);
    if (OB_FAIL(retain_native_edge_range_filters(
            *edge_scan, *target_scan, expand_access_desc_, native_step_supported_))) {
      LOG_WARN("failed to retain graph edge range filters", K(ret));
    } else if (OB_FAIL(initialize_path_payloads(*edge_scan))) {
      LOG_WARN("failed to bind native graph path payloads", K(ret));
    }
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_native_edge_identity_filters(
    ObIArray<ObRawExpr *> &exprs) const
{
  int ret = OB_SUCCESS;
  const ObLogTableScan *edge = find_internal_scan(get_child(second_child), GRAPH_STEP_EDGE_ALIAS);
  if (OB_ISNULL(edge)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObIArray<ObRawExpr *> &filters = edge->get_filter_exprs();
    for (int64_t i = 0; OB_SUCC(ret) && i < filters.count(); ++i) {
      if (OB_ISNULL(filters.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (is_native_identity_range(*filters.at(i), *edge, *edge, expand_access_desc_)) {
        ret = exprs.push_back(filters.at(i));
      }
    }
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_expand_scan_op_ids(
    uint64_t &source_scan_op_id,
    uint64_t &edge_scan_op_id,
    uint64_t &target_scan_op_id) const
{
  int ret = OB_SUCCESS;
  const ObLogTableScan *source_scan = find_internal_scan(
      get_child(first_child), GRAPH_SEED_VERTEX_ALIAS);
  const ObLogTableScan *edge_scan = find_internal_scan(
      get_child(second_child), GRAPH_STEP_EDGE_ALIAS);
  const ObLogTableScan *target_scan = find_internal_scan(
      get_child(second_child), GRAPH_STEP_VERTEX_ALIAS);
  if (OB_ISNULL(source_scan) || OB_ISNULL(edge_scan) || OB_ISNULL(target_scan)
      || source_scan->get_op_id() == OB_INVALID_ID
      || edge_scan->get_op_id() == OB_INVALID_ID
      || target_scan->get_op_id() == OB_INVALID_ID) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph expand scans are not numbered", K(ret), K(source_scan),
             K(edge_scan), K(target_scan));
  } else {
    source_scan_op_id = source_scan->get_op_id();
    edge_scan_op_id = edge_scan->get_op_id();
    target_scan_op_id = target_scan->get_op_id();
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_seed_key_exprs(
    ObIArray<ObRawExpr *> &exprs) const
{
  int ret = OB_SUCCESS;
  const ObLogicalOperator *anchor = get_child(first_child);
  const int64_t key_count = expand_access_desc_.source_key_count_;
  if (OB_ISNULL(anchor) || OB_UNLIKELY(key_count <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback seed description is invalid", K(ret), K(anchor),
             K(key_count));
  } else if (OB_UNLIKELY(anchor->get_output_exprs().count()
                         < GRAPH_FEEDBACK_DEPTH_COLUMN_COUNT + key_count)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback anchor does not expose every source key", K(ret),
             K(key_count), "output_count",
             anchor->get_output_exprs().count());
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < key_count; ++i) {
      ObRawExpr *expr = anchor->get_output_exprs().at(
          GRAPH_FEEDBACK_DEPTH_COLUMN_COUNT + i);
      if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph feedback source key expression is null", K(ret), K(i));
      } else if (OB_FAIL(exprs.push_back(expr))) {
        LOG_WARN("failed to append graph feedback source key", K(ret), K(i));
      }
    }
  }
  return ret;
}

int GraphFeedbackLoopLogOp::get_native_runtime_exprs(
    ObIArray<ObRawExpr *> &exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append_array_no_dup(exprs, path_payload_exprs_))) {
    LOG_WARN("failed to register graph path payload expressions", K(ret));
  }
  ObLogTableScan *scans[] = {
      find_internal_scan(get_child(first_child), GRAPH_SEED_VERTEX_ALIAS),
      find_internal_scan(get_child(second_child), GRAPH_STEP_EDGE_ALIAS),
      find_internal_scan(get_child(second_child), GRAPH_STEP_VERTEX_ALIAS)};
  ObSEArray<ObRawExpr *, 16> scan_exprs;
  for (int64_t i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(scans); ++i) {
    scan_exprs.reuse();
    if (OB_ISNULL(scans[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph expand scan is missing", K(ret), K(i));
    } else if (OB_FAIL(scans[i]->get_op_exprs(scan_exprs))) {
      LOG_WARN("failed to collect graph expand scan expressions", K(ret),
               K(i), K(scans[i]->get_op_id()));
    } else if (OB_FAIL(append_array_no_dup(exprs, scan_exprs))) {
      LOG_WARN("failed to append graph expand scan expressions", K(ret),
               K(i));
    }
  }
  ObArray<ObRawExpr *> seed_key_exprs;
  if (OB_SUCC(ret) && OB_FAIL(get_seed_key_exprs(seed_key_exprs))) {
    LOG_WARN("failed to collect graph feedback seed expressions", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(append_array_no_dup(exprs, seed_key_exprs))) {
    LOG_WARN("failed to register graph feedback seed expressions", K(ret));
  }
  return ret;
}

int GraphFeedbackLoopLogOp::can_prune_duplicate_states(bool &can_prune)
{
  int ret = OB_SUCCESS;
  can_prune = false;
  const bool shortest = path_desc_.any_shortest_ && path_desc_.shortest_single_segment_;
  bool safe = can_use_basic_native_runtime()
      && path_desc_.path_mode_ == GraphPathMode::WALK
      && (shortest || (path_desc_.row_shape_ == GraphPathRowShape::PER_MATCH
                      && !path_desc_.need_path_ && path_payload_exprs_.empty()));
  ObArray<ObRawExpr *> keys;
  if (safe && OB_FAIL(get_seed_key_exprs(keys))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && safe && i < keys.count(); ++i) {
    // Tolerance equality is not an equivalence relation suitable for pruning
    // vertices whose subsequent neighborhoods may differ at its boundaries.
    safe = keys.at(i) != nullptr
        && keys.at(i)->get_result_type().get_type_class() != ObDoubleTC
        && keys.at(i)->get_result_type().get_type_class() != ObFloatTC;
  }
  ObLogicalOperator *boundary = get_parent();
  const auto boundary_type = shortest ? LOG_WINDOW_FUNCTION : LOG_DISTINCT;
  while (safe && boundary != nullptr && boundary->get_type() != boundary_type) {
    const auto type = boundary->get_type();
    safe = type == LOG_SUBPLAN_SCAN || type == LOG_JOIN
        || type == LOG_SORT || type == LOG_MATERIAL;
    if (safe) {
      boundary = boundary->get_parent();
    }
  }
  // Inspect the whole intervening subtree, not only the spine: a correlated
  // aggregate/window/LIMIT or volatile sibling can observe path multiplicity.
  // Keep this allowlist conservative; unsupported plans retain ordinary WALK.
  ObArray<ObLogicalOperator *> pending;
  if (OB_SUCC(ret) && safe && boundary != nullptr
      && OB_FAIL(pending.push_back(boundary))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && safe && i < pending.count(); ++i) {
    ObLogicalOperator *node = pending.at(i);
    if (OB_ISNULL(node)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (node == this) {
      // Native eligibility already checks the generated single-hop filters.
      // ANY SHORTEST needs one witness per (binding, vertex, hop): subsequent
      // WALK continuations do not depend on the earlier path. Keeping hop in
      // this key also preserves cycles needed to meet a positive lower bound.
    } else {
      switch (node->get_type()) {
        case LOG_DISTINCT:
        case LOG_SUBPLAN_SCAN:
        case LOG_MATERIAL:
          break;
        case LOG_WINDOW_FUNCTION:
          safe = shortest && node == boundary;
          break;
        case LOG_JOIN:
          safe = static_cast<ObLogJoin *>(node)->get_join_type() == INNER_JOIN;
          break;
        case LOG_SORT: {
          ObLogSort *sort = static_cast<ObLogSort *>(node);
          // ROW_NUMBER()=1 may become a partition Top-N directly under the
          // generated shortest selector. Equal-hop witnesses are interchangeable
          // there; this does not authorize pruning below arbitrary user LIMIT.
          safe = (sort->get_topn_expr() == nullptr
                  || (shortest && sort->is_part_sort() && sort->get_parent() == boundary))
              && sort->get_topk_limit_expr() == nullptr
              && sort->get_pushdown_topn_filter_expr() == nullptr;
          break;
        }
        case LOG_TABLE_SCAN: {
          ObLogTableScan *scan = static_cast<ObLogTableScan *>(node);
          // Deterministic scalar expressions do not make sampling, virtual
          // tables or ranked retrieval stable across a correlated rescan.
          safe = scan->get_limit_expr() == nullptr && !scan->is_sample_scan()
              && !scan->is_vec_idx_scan() && !scan->is_text_retrieval_scan()
              && !scan->has_es_match() && !scan->has_merge_fts_index()
              && !scan->has_func_lookup()
              && (scan->get_table_type() == share::schema::USER_TABLE
                  || scan->get_table_type() == share::schema::USER_INDEX);
          break;
        }
        default:
          safe = false;
      }
    }
    if (OB_SUCC(ret) && safe) {
      ObArray<ObRawExpr *> exprs;
      if (node == this) {
        // A filter pushed onto the loop itself also runs once per output path.
        // Native scan eligibility does not cover these post-traversal filters.
        ret = node->ObLogicalOperator::get_op_exprs(exprs);
      } else {
        ret = node->get_op_exprs(exprs);
      }
      if (OB_SUCC(ret) && OB_FAIL(append(exprs, node->get_output_exprs()))) {
      }
      for (int64_t j = 0; OB_SUCC(ret) && safe && j < exprs.count(); ++j) {
        safe = exprs.at(j) != nullptr && exprs.at(j)->is_deterministic()
            && !exprs.at(j)->has_flag(CNT_SUB_QUERY);
      }
      for (int64_t j = 0; OB_SUCC(ret) && safe && node != this
                              && j < node->get_num_of_child(); ++j) {
        ret = pending.push_back(node->get_child(j));
      }
    }
  }
  can_prune = OB_SUCC(ret) && safe && boundary != nullptr;
  return ret;
}

int GraphFeedbackLoopLogOp::get_plan_item_info(PlanText &plan_text,
                                               ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  bool prune = false;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
  } else if (OB_FAIL(can_prune_duplicate_states(prune))) {
  } else {
    BEGIN_BUF_PRINT;
    if (OB_FAIL(BUF_PRINTF("direction=%s, hops={%ld,%ld}, access=%s, mode=%s, runtime=%s, states=%s, search=%s, adjacency_reuse=%s",
                           path_desc_.direction_ == GraphPathDirection::IN
                               ? "IN" : path_desc_.direction_ == GraphPathDirection::BOTH ? "BOTH" : "OUT",
                           path_desc_.lower_bound_,
                           path_desc_.upper_bound_,
                           access_method_name(step_access_method_),
                           path_mode_name(path_desc_.path_mode_),
                           can_use_basic_native_runtime()
                               ? "native" : "fallback",
                           prune ? "binding_vertex_hop" : "paths",
                           path_desc_.any_shortest_ ? "any_shortest" : "all",
                           can_reuse_adjacency() ? "eligible" : "off"))) {
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
  } else if (OB_FAIL(get_native_runtime_exprs(all_exprs))) {
    LOG_WARN("failed to collect native graph runtime expressions", K(ret));
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
  seed = do_hash(path_desc_.any_shortest_, seed);
  seed = do_hash(path_desc_.shortest_single_segment_, seed);
  seed = expand_access_desc_.hash(seed);
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
