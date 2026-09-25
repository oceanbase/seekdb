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
#include "sql/engine/graph/graph_path_state_store.h"
#include "sql/engine/recursive_cte/ob_recursive_union_all_op.h"
#include "sql/engine/graph/graph_expand_scan_desc.h"

namespace oceanbase
{
namespace sql
{

class GraphFeedbackRuntime;

// Positional layout of the generated recursive row consumed by the native
// feedback runtime. It deliberately uses expression indexes rather than the
// temporary __g_* aliases created by the SQL lowering.
struct GraphFeedbackRowDesc
{
  OB_UNIS_VERSION(1);
public:
  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           int64_t output_count);
  bool is_valid(const GraphPathDesc &path_desc,
                const GraphExpandAccessDesc &access_desc) const;

  int64_t depth_expr_index_{0};
  int64_t source_key_expr_begin_{0};
  int64_t source_key_expr_count_{0};
  int64_t current_key_expr_begin_{0};
  int64_t current_key_expr_count_{0};
  int64_t trail_key_expr_begin_{0};
  int64_t trail_key_expr_count_{0};
  // Optional JSON edge-identity/property arrays follow the fixed fields.
  int64_t payload_expr_begin_{0};
  int64_t payload_expr_count_{0};
  int64_t output_expr_count_{0};
  TO_STRING_KV(K_(depth_expr_index), K_(source_key_expr_begin),
               K_(source_key_expr_count), K_(current_key_expr_begin),
               K_(current_key_expr_count), K_(trail_key_expr_begin),
               K_(trail_key_expr_count), K_(payload_expr_begin),
               K_(payload_expr_count), K_(output_expr_count));
};

// Query-local owner of one breadth-first path frontier. Each call to expand()
// converts the current path-state IDs into GraphExpandInput batches, consumes
// their single-hop extensions, and installs the accepted child states as the
// next frontier. GraphFeedbackLoopOp will use this controller once its current
// recursive-SQL child is replaced by a native GraphExpand access adapter.
class GraphFeedbackFrontier final
{
public:
  GraphFeedbackFrontier(common::ObIAllocator &allocator,
                        GraphExpand &expand,
                        int64_t input_batch_size,
                        int64_t memory_limit);
  ~GraphFeedbackFrontier() = default;

  // Seeds may only be added to the zero-hop frontier. Duplicate seed rows are
  // intentionally assigned independent path states to preserve bag semantics.
  int add_seed(int64_t binding_id,
               const GraphElementIdentity &source_identity);

  // Advances exactly one hop. WALK accepts every extension; TRAIL rejects an
  // edge already present in that extension's own parent chain. An empty current
  // frontier returns OB_ITER_END without modifying the controller.
  int expand(GraphPathDirection direction, GraphPathMode path_mode);

  int get_frontier_state(int64_t index, GraphPathState &state) const;
  // Reconstructs one current frontier entry from its zero-hop root through
  // the current vertex. The returned query-local view is reused by the next
  // call and remains valid only until then or reset().
  int get_frontier_path(
      int64_t index,
      const common::ObIArray<GraphPathState> *&path);
  int64_t get_hop() const { return hop_; }
  int64_t get_frontier_count() const { return current_state_ids_->count(); }
  int64_t get_path_state_count() const { return state_store_.count(); }
  int64_t get_peak_memory() const { return peak_memory_bytes_; }
  bool empty() const { return current_state_ids_->empty(); }

  // Clears path history and both frontier buffers for close, rescan, or any
  // terminal error. The referenced GraphExpand object remains reusable.
  void reset();

private:
  int build_input_batch(int64_t start, int64_t &next_start);
  int consume_input_batch(GraphPathDirection direction,
                          GraphPathMode path_mode);
  int check_memory_limit();
  int fail(int error);

private:
  GraphExpand &expand_;
  GraphPathStateStore state_store_;
  int64_t input_batch_size_;
  int64_t memory_limit_;
  common::ObArray<int64_t> frontier_buffer_a_;
  common::ObArray<int64_t> frontier_buffer_b_;
  common::ObArray<int64_t> *current_state_ids_{nullptr};
  common::ObArray<int64_t> *next_state_ids_{nullptr};
  common::ObArray<GraphExpandInput> input_batch_;
  common::ObArray<GraphPathState> path_buffer_;
  int64_t hop_{0};
  int64_t peak_memory_bytes_{0};

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackFrontier);
};

// The graph operator has its own semantic spec while temporarily sharing the
// recursive row-pump mechanics used by recursive CTE.  A later increment can
// replace RecursivePumpOp with GraphExpand/frontier execution without changing
// the recursive CTE operator hierarchy.
class GraphFeedbackLoopSpec final : public RecursivePumpSpec
{
  OB_UNIS_VERSION_V(1);
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
  int init_output_row_desc();
  int bind_expand_scans(uint64_t source_scan_op_id,
                        uint64_t edge_scan_op_id,
                        uint64_t target_scan_op_id);

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

private:
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc expand_access_desc_{};
  GraphFeedbackRowDesc output_row_desc_{};
  // Evaluated after the anchor child produces a row. The expression order is
  // the source element's declared key order, so it can be copied directly into
  // a typed GraphElementIdentity without inspecting generated column names.
  ExprFixedArray seed_key_exprs_;
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
  int inner_rescan() override;
  void destroy() override;

private:
  int init_native_runtime();
  void reset_native_runtime();
  void destroy_native_runtime();
  const GraphFeedbackLoopSpec &get_graph_spec() const
  { return static_cast<const GraphFeedbackLoopSpec &>(spec_); }

private:
  // Query-local owner of the native one-hop access and BFS state. The public
  // row-production path still uses RecursivePumpOp until seed/result binding
  // is switched in a later increment.
  GraphFeedbackRuntime *native_runtime_{nullptr};
};

} // namespace sql
} // namespace oceanbase
