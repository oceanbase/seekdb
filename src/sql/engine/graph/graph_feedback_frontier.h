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
  void set_retain_path_history(bool retain) { retain_path_history_ = retain; }

  // Seeds may only be added to the zero-hop frontier. Duplicate seed rows are
  // intentionally assigned independent path states to preserve bag semantics.
  int add_seed(int64_t binding_id,
               const GraphElementIdentity &source_identity);

  // Produces a bounded batch from the next hop, or resumes that same hop.
  // A partially produced hop is visible to the consumer but never becomes
  // input to the following hop until all its extensions have been consumed.
  // This is scheduling, not truncation: further calls return every path.
  // Continuations of one hop must use the same parameters; shortest pruning
  // parameters remain fixed across the whole traversal. A mismatch is a
  // terminal error and clears the frontier. WALK normally accepts every
  // extension. A proven duplicate-insensitive consumer may merge equal
  // binding/vertex states in one hop. For a proven single-segment ANY SHORTEST
  // query, shortest_lower_bound >= 0 also rejects states first seen at an
  // earlier eligible hop. Earlier hops must still be explored: a cycle can be
  // the shortest path satisfying a positive lower bound. -1 retains the
  // original per-hop behavior. TRAIL checks each path's own edge history.
  // An empty current frontier returns OB_ITER_END without changing state.
  int expand(GraphPathDirection direction, GraphPathMode path_mode,
             bool prune_duplicate_states = false,
             int64_t shortest_lower_bound = -1);

  int get_frontier_state(int64_t index, GraphPathState &state) const;
  // Returns the root-to-leaf history when retained, otherwise only the root
  // and leaf (one state for zero hops). Endpoint-only consumers use leaf.hop_,
  // not the array length, as the path length. The view expires on the next call
  // or reset().
  int get_frontier_output_states(
      int64_t index,
      const common::ObIArray<GraphPathState> *&path);
  // Visible output belongs to the next hop while expansion is in progress.
  int64_t get_output_hop() const { return hop_ + (expanding_ ? 1 : 0); }
  // Cumulative states of the visible level, not the latest call's batch size.
  int64_t get_output_state_count() const { return output_state_ids().count(); }
  bool is_expanding() const { return expanding_; }
  int64_t get_path_state_count() const { return state_store_.count(); }
  int64_t get_peak_memory() const { return peak_memory_bytes_; }
  // Statistics for the most recently completed source-hop -> target-hop
  // expansion. They aggregate every physical input batch in that BFS level.
  const GraphExpandStats &get_last_hop_stats() const
  { return hop_stats_.last(); }
  GraphExpandStats get_pending_hop_stats() const;
  // Empty output does not imply traversal completion while is_expanding().
  bool output_empty() const { return output_state_ids().empty(); }

  // Clears path history and both frontier buffers for close, rescan, or any
  // terminal error. The referenced GraphExpand object remains reusable.
  void reset();

private:
  // These helpers share the controller's state; failures are reset by expand().
  int begin_hop(GraphPathDirection direction, GraphPathMode path_mode,
                bool prune_duplicate_states);
  int finish_hop();
  int accept_extension(const GraphExpandOutput &extension, GraphPathMode path_mode,
                       bool prune_duplicate_states, bool &accepted) const;
  int save_extension(const GraphExpandOutput &extension, bool prune_duplicate_states);
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
  // Per-hop (binding, typed vertex) index by default. Only proven ANY SHORTEST
  // retains entries across hops starting at the first eligible output level.
  // Identity backing belongs to state_store_ and outlives this index.
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
  int64_t shortest_lower_bound_{-1};
  bool shortest_mode_configured_{false};
  bool expanding_{false};
  bool batch_open_{false};
  bool retain_path_history_{true};

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackFrontier);
};


} // namespace sql
} // namespace oceanbase
