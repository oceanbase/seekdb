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

namespace oceanbase
{
namespace sql
{

class ObTableScanSpec;

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
  void set_expand_scan_ops(uint64_t source_scan_op_id,
                           uint64_t edge_scan_op_id,
                           uint64_t target_scan_op_id)
  {
    source_scan_op_id_ = source_scan_op_id;
    edge_scan_op_id_ = edge_scan_op_id;
    target_scan_op_id_ = target_scan_op_id;
  }

  const GraphPathDesc &get_path_desc() const { return path_desc_; }
  const GraphExpandAccessDesc &get_expand_access_desc() const
  { return expand_access_desc_; }
  int64_t get_lower_bound() const { return path_desc_.lower_bound_; }
  int64_t get_upper_bound() const { return path_desc_.upper_bound_; }
  GraphPathDirection get_direction() const { return path_desc_.direction_; }
  GraphPathMode get_path_mode() const { return path_desc_.path_mode_; }
  int resolve_expand_scan_specs(const ObTableScanSpec *&source_scan,
                                const ObTableScanSpec *&edge_scan,
                                const ObTableScanSpec *&target_scan) const;

private:
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc expand_access_desc_{};
  // Operator IDs, rather than table IDs, distinguish the anchor and step
  // aliases when both vertices use the same mapped table. The scan specs keep
  // the generated filters, table-location metadata and DAS ctdefs that the
  // native expand adapter will borrow.
  uint64_t source_scan_op_id_{common::OB_INVALID_ID};
  uint64_t edge_scan_op_id_{common::OB_INVALID_ID};
  uint64_t target_scan_op_id_{common::OB_INVALID_ID};
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

private:
  const GraphFeedbackLoopSpec &get_graph_spec() const
  { return static_cast<const GraphFeedbackLoopSpec &>(spec_); }
};

} // namespace sql
} // namespace oceanbase
