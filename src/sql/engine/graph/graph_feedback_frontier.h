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

#include "sql/engine/graph/graph_frontier_input_batcher.h"
#include "sql/engine/graph/graph_hop_stats_collector.h"

namespace oceanbase
{
namespace sql
{
// Query-local owner of a breadth-first path frontier. expand() converts the
// input path-state IDs into GraphExpandInput batches and yields bounded groups
// of accepted children. Only a complete level becomes the next input frontier.
// GraphFeedbackLoopOp uses this controller for its conservative
// native subset and retains the recursive-SQL child as a semantic fallback.
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

  // Produces a bounded batch from the next hop, or resumes that same hop.
  // A partially produced hop is visible to the consumer but never becomes
  // input to the following hop until all its extensions have been consumed.
  // This is scheduling, not truncation: further calls return every path.
  // Continuations of one hop must use the same direction, path mode and
  // pruning flag. A mismatch is a terminal error and clears the frontier.
  // WALK normally accepts every extension; a proven
  // duplicate-insensitive consumer can merge equal binding/vertex states at
  // this hop. TRAIL rejects edges in the extension's own history. An empty current
  // frontier returns OB_ITER_END without modifying the controller.
  int expand(GraphPathDirection direction, GraphPathMode path_mode,
             bool prune_duplicate_states = false);

  int get_frontier_state(int64_t index, GraphPathState &state) const;
  // Reconstructs one current frontier entry from its zero-hop root through
  // the current vertex. The returned query-local view is reused by the next
  // call and remains valid only until then or reset().
  int get_frontier_path(
      int64_t index,
      const common::ObIArray<GraphPathState> *&path);
  int64_t get_hop() const { return hop_ + (expanding_ ? 1 : 0); }
  int64_t get_frontier_count() const { return output_state_ids().count(); }
  bool is_expanding() const { return expanding_; }
  int64_t get_path_state_count() const { return state_store_.count(); }
  int64_t get_peak_memory() const { return peak_memory_bytes_; }
  // Statistics for the most recently completed source-hop -> target-hop
  // expansion. They aggregate every physical input batch in that BFS level.
  const GraphExpandStats &get_last_hop_stats() const
  { return hop_stats_.last(); }
  GraphExpandStats get_pending_hop_stats() const;
  bool empty() const { return output_state_ids().empty(); }

  // Clears path history and both frontier buffers for close, rescan, or any
  // terminal error. The referenced GraphExpand object remains reusable.
  void reset();

private:
  int consume_input_batch(GraphPathDirection direction,
                          GraphPathMode path_mode,
                          bool prune_duplicate_states,
                          int64_t output_end);
  const common::ObIArray<int64_t> &output_state_ids() const
  { return expanding_ ? *next_state_ids_ : *current_state_ids_; }
  // Both budget calculation and post-access checks use the same allocation list.
  int get_memory_usage(bool include_input_batch, int64_t &used) const;
  int64_t batch_memory_budget() const;
  int check_memory_limit();
  int fail(int error);

private:
  GraphExpand &expand_;
  GraphPathStateStore state_store_;
  GraphFrontierInputBatcher input_batcher_;
  GraphHopStatsCollector hop_stats_;
  // One entry per (binding, typed vertex) in the NEXT BFS level only. Never a
  // global visited set: a vertex reached at different hops is not equivalent.
  GraphIdentityIndex next_state_index_;
  int64_t input_batch_size_;
  int64_t memory_limit_;
  common::ObArray<int64_t> frontier_buffer_a_;
  common::ObArray<int64_t> frontier_buffer_b_;
  common::ObArray<int64_t> *current_state_ids_{&frontier_buffer_a_};
  common::ObArray<int64_t> *next_state_ids_{&frontier_buffer_b_};
  common::ObArray<GraphPathState> path_buffer_;
  int64_t hop_{0};
  int64_t peak_memory_bytes_{0};
  // Frozen for the whole hop, including all yielded output/input batches.
  GraphPathDirection active_direction_{GraphPathDirection::OUT};
  GraphPathMode active_path_mode_{GraphPathMode::WALK};
  bool active_prune_duplicate_states_{false};
  bool expanding_{false};
  bool batch_open_{false};

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackFrontier);
};


} // namespace sql
} // namespace oceanbase
