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

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_2d_array.h"
#include "sql/engine/graph/graph_expand.h"
#include "sql/engine/graph/graph_identity_index.h"

namespace oceanbase
{
namespace sql
{

// Query-local shortest-distance search for a caller-proven, distinct pair of
// vertices in a directed WALK. The two accesses must represent the same edge
// mapping under one statement snapshot, with opposite source/destination
// directions. This component does not produce a path witness or apply SQL
// endpoint predicates; the caller selects it only when the requested result
// needs the distance and has already checked those predicates.
//
// Every discovered vertex is indexed using its complete typed primary key.
// The indexes, frontiers, DAS pages and key backing all use one supplied graph
// work-area allocator. No result is returned on an incomplete BFS level or an
// access/memory/cancellation failure.
class GraphBidirectionalShortest final
{
public:
  GraphBidirectionalShortest(common::ObIAllocator &allocator,
                             IGraphExpandAccess &forward_access,
                             IGraphExpandAccess &reverse_access,
                             int64_t memory_limit);
  ~GraphBidirectionalShortest();

  // On success, found=false means no path within upper_bound; distance=-1.
  // The caller proves that both root identities are distinct and that a
  // shortest-distance-only WALK is semantically sufficient.
  int search(const GraphElementIdentity &source_identity,
             const GraphElementIdentity &goal_identity,
             GraphPathDirection forward_direction,
             int64_t upper_bound,
             int64_t &distance,
             bool &found);
  void release();

  // Cumulative physical access stats survive a completed search. They reset
  // when the next search begins or release() is called.
  const GraphExpandStats &get_stats() const { return stats_; }
  int64_t get_expanded_hop_count() const { return expanded_hop_count_; }
  int64_t used_memory() const;
  // Retained arena capacity is not a live DAS cursor. The caller separately
  // checks the two access adapters' edge-scan resource state.
  bool resources_released() const
  { return !forward_.expand_.is_open() && !reverse_.expand_.is_open(); }

private:
  using IdentityFrontier = common::Ob2DArray<GraphElementIdentity,
                                              OB_MALLOC_NORMAL_BLOCK_SIZE>;
  using DepthArray = common::Ob2DArray<int64_t,
                                       OB_MALLOC_NORMAL_BLOCK_SIZE>;

  struct SearchSide
  {
    SearchSide(common::ObIAllocator &allocator,
               IGraphExpandAccess &access,
               int64_t memory_limit);

    IdentityFrontier &current()
    { return frontier_a_active_ ? frontier_a_ : frontier_b_; }
    IdentityFrontier &next()
    { return frontier_a_active_ ? frontier_b_ : frontier_a_; }
    const IdentityFrontier &current() const
    { return frontier_a_active_ ? frontier_a_ : frontier_b_; }
    void finish_level();
    void reset();
    int64_t used_memory() const;
    int copy_identity(const GraphElementIdentity &source,
                      GraphElementIdentity &stable);

    IGraphExpandAccess &access_;
    GraphExpand expand_;
    GraphIdentityIndex visited_;
    common::ObArenaAllocator identity_allocator_;
    IdentityFrontier frontier_a_;
    IdentityFrontier frontier_b_;
    DepthArray visited_depths_;
    common::ObArray<GraphExpandInput> input_batch_;
    common::ObArray<GraphElementIdentity> lookup_request_;
    common::ObArray<GraphElementIdentity> lookup_existing_;
    common::ObArray<GraphElementIdentity> probe_sources_;
    GraphPathDirection direction_{GraphPathDirection::OUT};
    int64_t depth_{0};
    bool frontier_a_active_{true};
  };

  struct LevelProbe
  {
    int64_t scanned_rows_{0};
    bool measured_{false};
    bool complete_{false};
  };

  int validate_and_seed(SearchSide &side,
                        const GraphElementIdentity &identity,
                        bool &exists);
  int expand_level(SearchSide &side, SearchSide &opposite,
                   int64_t &best_distance);
  int accept_extension(SearchSide &side, const SearchSide &opposite,
                       const GraphElementIdentity &target_identity,
                       int64_t &best_distance);
  int probe_level(SearchSide &side, LevelProbe &probe);
  int choose_side(SearchSide *&side);
  SearchSide &choose_side_by_frontier();
  int accumulate_stats(const GraphExpandStats &batch);
  int check_memory_limit();
  void release_state();

  SearchSide forward_;
  SearchSide reverse_;
  int64_t memory_limit_;
  GraphExpandStats stats_{};
  int64_t expanded_hop_count_{0};

  DISALLOW_COPY_AND_ASSIGN(GraphBidirectionalShortest);
};

} // namespace sql
} // namespace oceanbase
