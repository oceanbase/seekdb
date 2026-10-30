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
#include "sql/engine/graph/graph_expand_access_desc.h"
#include "sql/engine/graph/graph_feedback_row_desc.h"
#include "sql/engine/recursive_cte/ob_recursive_union_all_op.h"
#include "sql/engine/graph/graph_expand_scan_desc.h"

namespace oceanbase
{
namespace sql
{

class GraphFeedbackRuntime;
class GraphShortestRowAdmission;

// The graph operator has its own semantic spec while temporarily sharing the
// recursive row-pump mechanics used by recursive CTE.  A later increment can
// replace RecursivePumpOp with GraphExpand/frontier execution without changing
// the recursive CTE operator hierarchy.
class GraphFeedbackLoopSpec final : public RecursivePumpSpec
{
  OB_UNIS_VERSION_V(3);
public:
  explicit GraphFeedbackLoopSpec(common::ObIAllocator &allocator,
                                 const ObPhyOperatorType type);
  ~GraphFeedbackLoopSpec() = default;

  void set_graph_path(const GraphPathDesc &path_desc)
  {
    path_desc_ = path_desc;
  }
  void set_expand_access(const GraphExpandAccessDesc &expand_access_desc)
  {
    expand_access_desc_ = expand_access_desc;
  }
  void set_native_step_supported(bool supported)
  {
    native_step_supported_ = supported;
  }
  void set_prune_duplicate_states(bool prune) { prune_duplicate_states_ = prune; }
  bool prune_duplicate_states() const { return prune_duplicate_states_; }
  ExprFixedArray &get_shortest_goal_value_exprs() { return shortest_goal_value_exprs_; }
  const ExprFixedArray &get_shortest_goal_value_exprs() const
  { return shortest_goal_value_exprs_; }
  void set_reuse_adjacency(bool reuse) { reuse_adjacency_ = reuse; }
  bool reuse_adjacency() const { return reuse_adjacency_; }
  ExprFixedArray &get_adjacency_parameters() { return adjacency_parameters_; }
  const ExprFixedArray &get_adjacency_parameters() const { return adjacency_parameters_; }
  int init_output_row_desc();
  int bind_expand_scans(uint64_t source_scan_op_id,
                        uint64_t edge_scan_op_id,
                        uint64_t target_scan_op_id);
  int remove_native_edge_identity_filters(const common::ObIArray<ObExpr *> &filters);

  const GraphPathDesc &get_path_desc() const { return path_desc_; }
  const GraphExpandAccessDesc &get_expand_access_desc() const
  { return expand_access_desc_; }
  const GraphFeedbackRowDesc &get_output_row_desc() const
  { return output_row_desc_; }
  int64_t get_lower_bound() const { return path_desc_.lower_bound_; }
  int64_t get_upper_bound() const { return path_desc_.upper_bound_; }
  GraphPathDirection get_direction() const { return path_desc_.direction_; }
  GraphPathMode get_path_mode() const { return path_desc_.path_mode_; }
  const GraphExpandScanDesc &get_source_scan_desc() const
  { return source_scan_desc_; }
  const GraphExpandScanDesc &get_edge_scan_desc() const
  { return edge_scan_desc_; }
  const GraphExpandScanDesc &get_target_scan_desc() const
  { return target_scan_desc_; }
  ExprFixedArray &get_seed_key_exprs() { return seed_key_exprs_; }
  ExprFixedArray &get_path_payload_exprs() { return edge_scan_desc_.payload_exprs_; }
  const ExprFixedArray &get_seed_key_exprs() const { return seed_key_exprs_; }
  bool has_valid_seed_key_exprs() const
  {
    bool valid = seed_key_exprs_.count()
        == expand_access_desc_.source_key_count_;
    for (int64_t i = 0; valid && i < seed_key_exprs_.count(); ++i) {
      valid = seed_key_exprs_.at(i) != nullptr;
    }
    return valid;
  }
  bool can_use_basic_native_runtime() const
  {
    return native_step_supported_
        && output_row_desc_.is_valid(path_desc_, expand_access_desc_)
        && supports_basic_native_graph_feedback(
            path_desc_, expand_access_desc_, output_row_desc_.output_expr_count_,
            edge_scan_desc_.payload_exprs_.count());
  }

private:
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc expand_access_desc_{};
  GraphFeedbackRowDesc output_row_desc_{};
  bool native_step_supported_{false};
  bool prune_duplicate_states_{false};
  bool reuse_adjacency_{false};
  // Evaluated after the anchor child produces a row. The expression order is
  // the source element's declared key order, so it can be copied directly into
  // a typed GraphElementIdentity without inspecting generated column names.
  ExprFixedArray seed_key_exprs_;
  // Present only when both resolver and final-plan proof admit a one-key goal.
  ExprFixedArray shortest_goal_value_exprs_;
  // Complete outer bindings used by edge filters, captured once per rescan.
  ExprFixedArray adjacency_parameters_;
  // These snapshots cross DFO boundaries with the feedback-loop spec. The
  // source, edge and target aliases can map to the same physical table, so
  // binding happens by operator ID once during code generation.
  GraphExpandScanDesc source_scan_desc_;
  GraphExpandScanDesc edge_scan_desc_;
  GraphExpandScanDesc target_scan_desc_;
};

class GraphFeedbackLoopOp final : public RecursivePumpOp
{
public:
  explicit GraphFeedbackLoopOp(ObExecContext &exec_ctx,
                               const ObOpSpec &spec,
                               ObOpInput *input)
      : RecursivePumpOp(exec_ctx, spec, input)
  {}
  ~GraphFeedbackLoopOp() = default;
  int inner_open() override;
  int inner_close() override;
  int inner_get_next_row() override;
  int inner_get_next_batch(int64_t max_row_cnt) override;
  int inner_rescan() override;
  void destroy() override;

private:
  int init_native_runtime();
  int get_next_native_row();
  void init_native_monitor_info();
  void update_native_monitor_info();
  void reset_native_runtime(bool keep_adjacency = false);
  void destroy_native_runtime();
  int init_fallback_admission();
  void destroy_fallback_admission();
  const GraphFeedbackLoopSpec &get_graph_spec() const
  { return static_cast<const GraphFeedbackLoopSpec &>(spec_); }

private:
  // Query-local owner of the native one-hop access and BFS state. Plans outside
  // the basic native capability continue to use RecursivePumpOp unchanged.
  GraphFeedbackRuntime *native_runtime_{nullptr};
  GraphShortestRowAdmission *fallback_admission_{nullptr};
  bool use_native_runtime_{false};
};

} // namespace sql
} // namespace oceanbase
