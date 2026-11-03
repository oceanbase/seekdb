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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/graph/graph_bidirectional_shortest.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"

#include <algorithm>

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

// Keep a physical input batch small enough for indexed DAS ranges. When an
// access falls back to a table scan, GraphExpand instead receives the entire
// frontier so that the edge table is scanned only once for that BFS level.
static constexpr int64_t GRAPH_SHORTEST_INDEXED_BATCH_SIZE =
    GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE;
static constexpr int64_t GRAPH_SHORTEST_EDGE_PAGE_SIZE =
    GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE;
// Bound one adjacency-cost probe to the same physical row count as an edge
// page. This is large enough to distinguish ordinary degree skew without
// allowing cost selection itself to scan an unbounded adjacency list.
static constexpr int64_t GRAPH_SHORTEST_PROBE_MAX_SOURCES =
    GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE;
static constexpr int64_t GRAPH_SHORTEST_PROBE_RAW_ROWS =
    GRAPH_SHORTEST_EDGE_PAGE_SIZE;

int add_nonnegative(int64_t value, int64_t &total)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(value < 0 || total < 0 || total > INT64_MAX - value)) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    total += value;
  }
  return ret;
}

int64_t saturated_add(int64_t left, int64_t right)
{
  return left < 0 || right < 0 || left > INT64_MAX - right
      ? INT64_MAX : left + right;
}

} // namespace

GraphBidirectionalShortest::SearchSide::SearchSide(
    ObIAllocator &allocator,
    IGraphExpandAccess &access,
    int64_t memory_limit)
    : access_(access),
      expand_(allocator, access, GRAPH_SHORTEST_EDGE_PAGE_SIZE, memory_limit),
      visited_(allocator),
      identity_allocator_(allocator),
      frontier_a_(ModulePageAllocator(allocator, "GraphBidirFtr")),
      frontier_b_(ModulePageAllocator(allocator, "GraphBidirFtr")),
      visited_depths_(ModulePageAllocator(allocator, "GraphBidirDpt")),
      input_batch_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                   ModulePageAllocator(allocator, "GraphBidirIn")),
      lookup_request_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                      ModulePageAllocator(allocator, "GraphBidirLkp")),
      lookup_existing_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                       ModulePageAllocator(allocator, "GraphBidirLkp")),
      probe_sources_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                     ModulePageAllocator(allocator, "GraphBidirPrb"))
{
}

int GraphBidirectionalShortest::SearchSide::copy_identity(
    const GraphElementIdentity &source, GraphElementIdentity &stable)
{
  int ret = OB_SUCCESS;
  stable.reset();
  if (OB_UNLIKELY(!source.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    stable.graph_id_ = source.graph_id_;
    stable.element_id_ = source.element_id_;
    if (OB_FAIL(source.rowkey_.deep_copy(stable.rowkey_, identity_allocator_))) {
      LOG_WARN("failed to copy shortest-path vertex identity", K(ret), K(source));
    }
  }
  return ret;
}

void GraphBidirectionalShortest::SearchSide::finish_level()
{
  // The old frontier cannot be needed after its complete level has been
  // consumed. The visited index owns a separate stable rowkey copy.
  current().reset();
  frontier_a_active_ = !frontier_a_active_;
  ++depth_;
}

void GraphBidirectionalShortest::SearchSide::reset()
{
  expand_.release();
  expand_.set_sources_verified(false);
  visited_.reset();
  frontier_a_.reset();
  frontier_b_.reset();
  visited_depths_.reset();
  input_batch_.reset();
  lookup_request_.reset();
  lookup_existing_.reset();
  probe_sources_.reset();
  identity_allocator_.reset();
  direction_ = GraphPathDirection::OUT;
  depth_ = 0;
  frontier_a_active_ = true;
}

int64_t GraphBidirectionalShortest::SearchSide::used_memory() const
{
  int64_t bytes = 0;
  const int64_t parts[] = {
      expand_.used_memory(), visited_.used_memory(),
      identity_allocator_.total(), frontier_a_.mem_used(),
      frontier_b_.mem_used(), visited_depths_.mem_used(),
      input_batch_.get_data_size(), lookup_request_.get_data_size(),
      lookup_existing_.get_data_size(), probe_sources_.get_data_size()};
  for (const int64_t part : parts) {
    bytes = saturated_add(bytes, part);
  }
  return bytes;
}

GraphBidirectionalShortest::GraphBidirectionalShortest(
    ObIAllocator &allocator,
    IGraphExpandAccess &forward_access,
    IGraphExpandAccess &reverse_access,
    int64_t memory_limit)
    : forward_(allocator, forward_access, memory_limit),
      reverse_(allocator, reverse_access, memory_limit),
      memory_limit_(memory_limit)
{
}

GraphBidirectionalShortest::~GraphBidirectionalShortest()
{
  release();
}

int64_t GraphBidirectionalShortest::used_memory() const
{
  return saturated_add(forward_.used_memory(), reverse_.used_memory());
}

int GraphBidirectionalShortest::check_memory_limit()
{
  int ret = OB_SUCCESS;
  const int64_t resident_bytes = used_memory();
  if (OB_UNLIKELY(memory_limit_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(resident_bytes > memory_limit_)) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
    LOG_WARN("bidirectional shortest path exceeded graph work-area limit",
             K(ret), K(resident_bytes), K_(memory_limit));
  } else {
    stats_.peak_path_memory_ =
        std::max(stats_.peak_path_memory_, resident_bytes);
  }
  return ret;
}

int GraphBidirectionalShortest::validate_and_seed(
    SearchSide &side, const GraphElementIdentity &identity, bool &exists)
{
  int ret = OB_SUCCESS;
  GraphElementIdentity stable;
  int64_t index = -1;
  bool inserted = false;
  exists = false;
  side.lookup_request_.reuse();
  side.lookup_existing_.reuse();
  if (OB_FAIL(side.copy_identity(identity, stable))) {
  } else if (OB_FAIL(side.lookup_request_.push_back(stable))) {
  } else if (OB_FAIL(check_memory_limit())) {
  } else if (OB_FAIL(side.access_.check_status())) {
  } else if (OB_FAIL(side.access_.lookup_vertices(
                 side.lookup_request_, side.lookup_existing_,
                 GraphVertexLookupPhase::SOURCE))) {
    LOG_WARN("failed to look up shortest-path root", K(ret), K(identity));
  } else if (OB_FAIL(side.access_.check_status())) {
  } else if (OB_UNLIKELY(side.lookup_existing_.count() > 1
                         || (side.lookup_existing_.count() == 1
                             && !(side.lookup_existing_.at(0) == stable)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex lookup returned unexpected shortest-path root",
             K(ret), K(identity));
  } else if (side.lookup_existing_.empty()) {
    // A bound key is not an existence proof. In particular, a disconnected
    // goal cannot be treated as a valid reverse BFS root without this lookup.
  } else if (OB_FAIL(side.visited_.init(1))) {
  } else if (OB_FAIL(side.visited_.get_or_insert(stable, index, inserted))) {
  } else if (OB_UNLIKELY(!inserted || index != 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(side.visited_depths_.push_back(0))) {
  } else if (OB_FAIL(side.current().push_back(stable))) {
  } else if (OB_FAIL(check_memory_limit())) {
  } else {
    exists = true;
  }
  side.lookup_request_.reuse();
  side.lookup_existing_.reuse();
  return ret;
}

GraphBidirectionalShortest::SearchSide &
GraphBidirectionalShortest::choose_side_by_frontier()
{
  const bool forward_indexed = forward_.expand_.can_partition_sources();
  const bool reverse_indexed = reverse_.expand_.can_partition_sources();
  const int64_t forward_count = forward_.current().count();
  const int64_t reverse_count = reverse_.current().count();
  if (forward_indexed != reverse_indexed) {
    SearchSide &indexed = forward_indexed ? forward_ : reverse_;
    SearchSide &full_scan = forward_indexed ? reverse_ : forward_;
    // A few indexed ranges are normally cheaper than a whole edge-table
    // scan. Once the indexed frontier outgrows one physical source batch,
    // permit the smaller opposite frontier to take the full-scan side.
    return indexed.current().count() <= GRAPH_SHORTEST_INDEXED_BATCH_SIZE
        || indexed.current().count() <= full_scan.current().count()
        ? indexed : full_scan;
  }
  // At equal width, start from the goal. A zero-in-degree bound goal then
  // proves "no path" without expanding the potentially high-degree source.
  return reverse_count <= forward_count ? reverse_ : forward_;
}

int GraphBidirectionalShortest::probe_level(
    SearchSide &side, LevelProbe &probe)
{
  int ret = OB_SUCCESS;
  probe = LevelProbe();
  const int64_t source_count = side.current().count();
  if (source_count > GRAPH_SHORTEST_PROBE_MAX_SOURCES) {
    // More than one physical source batch is already beyond this bounded
    // comparison. The frontier-width fallback will select a side.
  } else {
    side.probe_sources_.reuse();
    for (int64_t i = 0; OB_SUCC(ret) && i < source_count; ++i) {
      if (i % GRAPH_SHORTEST_PROBE_RAW_ROWS == 0
          && OB_FAIL(side.access_.check_status())) {
      } else if (OB_FAIL(side.probe_sources_.push_back(
                     side.current().at(i)))) {
        LOG_WARN("failed to collect shortest-path probe sources", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
    }
    if (OB_SUCC(ret)) {
      // The adapter releases root-lookup scratch before probing. Exclude its
      // current bytes so the measured probe peak is not counted twice.
      const int64_t resident_before = used_memory();
      const int64_t adapter_before = side.access_.used_memory();
      const int64_t base_memory = resident_before >= adapter_before
          ? resident_before - adapter_before : resident_before;
      int64_t probe_memory = 0;
      if (side.access_.supports_adjacency_probe()) {
        ret = side.access_.probe_adjacency_rows(
            side.probe_sources_, side.direction_,
            GRAPH_SHORTEST_PROBE_RAW_ROWS, probe.scanned_rows_,
            probe.complete_, probe_memory);
        probe.measured_ = OB_SUCC(ret);
        // The adapter closes its cursor internally; this also handles a
        // partial implementation that returned an access error.
        side.access_.release();
      }
      if (OB_SUCC(ret)
          && OB_UNLIKELY(probe.scanned_rows_ < 0
                         || probe.scanned_rows_ > GRAPH_SHORTEST_PROBE_RAW_ROWS
                         || probe_memory < 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_SUCC(ret)
                 && OB_FAIL(add_nonnegative(
                        probe.scanned_rows_, stats_.scanned_edges_))) {
      } else if (OB_SUCC(ret)) {
        const int64_t peak_memory = saturated_add(base_memory, probe_memory);
        stats_.peak_path_memory_ = std::max(stats_.peak_path_memory_, peak_memory);
        if (OB_UNLIKELY(peak_memory > memory_limit_)) {
          ret = OB_EXCEED_QUERY_MEM_LIMIT;
          LOG_WARN("shortest-path probe exceeded graph work-area limit",
                   K(ret), K(peak_memory), K_(memory_limit));
        }
      }
    }
    side.probe_sources_.reset();
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
    }
  }
  return ret;
}

int GraphBidirectionalShortest::choose_side(SearchSide *&side)
{
  int ret = OB_SUCCESS;
  side = &choose_side_by_frontier();
  // Cost probes compare physical adjacency work only when both directions
  // have independent indexed ranges. A full-scan side must still receive the
  // complete BFS frontier and is selected by the frontier policy above.
  if (forward_.expand_.can_partition_sources()
      && reverse_.expand_.can_partition_sources()
      && forward_.access_.supports_adjacency_probe()
      && reverse_.access_.supports_adjacency_probe()) {
    LevelProbe forward_probe;
    LevelProbe reverse_probe;
    if (OB_FAIL(probe_level(forward_, forward_probe))) {
      LOG_WARN("failed to probe forward shortest-path level", K(ret));
    } else if (OB_FAIL(probe_level(reverse_, reverse_probe))) {
      LOG_WARN("failed to probe reverse shortest-path level", K(ret));
    } else if (!forward_probe.measured_ || !reverse_probe.measured_) {
      // The bounded frontier policy remains valid when either side has too
      // many source ranges to measure in one physical batch.
    } else if (forward_probe.complete_ != reverse_probe.complete_) {
      // An incomplete probe has at least one full page of physical rows. A
      // complete probe is therefore no more expensive than that lower bound.
      side = forward_probe.complete_ ? &forward_ : &reverse_;
    } else if (forward_probe.scanned_rows_ != reverse_probe.scanned_rows_) {
      side = forward_probe.scanned_rows_ < reverse_probe.scanned_rows_
          ? &forward_ : &reverse_;
    }
    // Equal probes, including two page-sized lower bounds, keep the indexed
    // frontier decision. It remains exact because either side completes one
    // whole BFS level before the next choice.
  }
  return ret;
}

int GraphBidirectionalShortest::accept_extension(
    SearchSide &side, const SearchSide &opposite,
    const GraphElementIdentity &target_identity, int64_t &best_distance)
{
  int ret = OB_SUCCESS;
  int64_t own_index = -1;
  int64_t opposite_index = -1;
  bool own_seen = false;
  bool opposite_seen = false;
  const int64_t next_depth = side.depth_ + 1;
  if (OB_FAIL(side.visited_.find(target_identity, own_index, own_seen))) {
  } else if (OB_FAIL(opposite.visited_.find(
                 target_identity, opposite_index, opposite_seen))) {
  } else if (OB_UNLIKELY((own_seen && own_index >= side.visited_depths_.count())
                         || (opposite_seen
                             && opposite_index >= opposite.visited_depths_.count()))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t own_depth = own_seen
        ? side.visited_depths_.at(own_index) : next_depth;
    if (opposite_seen) {
      const int64_t candidate =
          own_depth + opposite.visited_depths_.at(opposite_index);
      best_distance = best_distance < 0
          ? candidate : std::min(best_distance, candidate);
    }
    if (!own_seen && best_distance < 0) {
      GraphElementIdentity stable;
      int64_t inserted_index = -1;
      bool inserted = false;
      // GraphExpandOutput borrows the current DAS edge page. The visited
      // index and next frontier must point to our independent work-area copy.
      if (OB_FAIL(side.copy_identity(target_identity, stable))) {
      } else if (OB_FAIL(side.visited_.get_or_insert(
                     stable, inserted_index, inserted))) {
      } else if (OB_UNLIKELY(!inserted
                             || inserted_index != side.visited_depths_.count())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(side.visited_depths_.push_back(next_depth))) {
      } else if (OB_FAIL(side.next().push_back(stable))) {
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
  }
  return ret;
}

int GraphBidirectionalShortest::accumulate_stats(const GraphExpandStats &batch)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_nonnegative(batch.input_states_, stats_.input_states_))) {
  } else if (OB_FAIL(add_nonnegative(
                 batch.distinct_sources_, stats_.distinct_sources_))) {
  } else if (OB_FAIL(add_nonnegative(
                 batch.scanned_edges_, stats_.scanned_edges_))) {
  } else if (OB_FAIL(add_nonnegative(
                 batch.looked_up_vertices_, stats_.looked_up_vertices_))) {
  } else if (OB_FAIL(add_nonnegative(
                 batch.orphan_edges_, stats_.orphan_edges_))) {
  } else if (OB_FAIL(add_nonnegative(
                 batch.output_states_, stats_.output_states_))) {
  } else {
    stats_.distinct_sources_exact_ &= batch.distinct_sources_exact_;
    stats_.peak_path_memory_ = std::max(
        stats_.peak_path_memory_, batch.peak_path_memory_);
  }
  return ret;
}

int GraphBidirectionalShortest::expand_level(
    SearchSide &side, SearchSide &opposite, int64_t &best_distance)
{
  int ret = OB_SUCCESS;
  const int64_t frontier_count = side.current().count();
  const int64_t max_batch = side.expand_.can_partition_sources()
      ? GRAPH_SHORTEST_INDEXED_BATCH_SIZE : frontier_count;
  side.next().reuse();
  for (int64_t begin = 0; OB_SUCC(ret) && begin < frontier_count;) {
    side.input_batch_.reuse();
    const int64_t end = begin + std::min(max_batch, frontier_count - begin);
    for (int64_t i = begin; OB_SUCC(ret) && i < end; ++i) {
      GraphExpandInput input;
      input.binding_id_ = 0;
      input.path_state_id_ = i;
      input.source_identity_ = side.current().at(i);
      if (OB_FAIL(side.input_batch_.push_back(input))) {
        LOG_WARN("failed to append shortest-path frontier input", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
    } else if (OB_SUCC(ret) && OB_FAIL(side.expand_.open(
                   side.input_batch_, side.direction_, &side.visited_))) {
      LOG_WARN("failed to open shortest-path adjacency batch", K(ret), K(begin), K(end));
    } else if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
    }
    GraphExpandOutput extension;
    while (OB_SUCC(ret)) {
      const int next_ret = side.expand_.get_next_row(extension);
      if (next_ret == OB_ITER_END) {
        break;
      } else if (OB_SUCCESS != next_ret) {
        ret = next_ret;
        LOG_WARN("failed to read shortest-path adjacency", K(ret), K(begin));
      } else if (OB_FAIL(accept_extension(
                     side, opposite, extension.target_identity_, best_distance))) {
        LOG_WARN("failed to accept shortest-path extension", K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
    } else if (OB_SUCC(ret)
               && OB_FAIL(accumulate_stats(side.expand_.get_stats()))) {
      LOG_WARN("failed to aggregate shortest-path access statistics", K(ret));
    }
    side.expand_.release();
    begin = end;
  }
  if (OB_SUCC(ret)) {
    // Do not stop at the first intersection: inspect every edge of this level
    // and retain the minimum distance. All earlier levels on both sides were
    // completed, so a shorter route would already have intersected those
    // visited sets or would be seen among this level's crossing edges.
    side.finish_level();
    ++expanded_hop_count_;
    ret = check_memory_limit();
  }
  return ret;
}

int GraphBidirectionalShortest::search(
    const GraphElementIdentity &source_identity,
    const GraphElementIdentity &goal_identity,
    GraphPathDirection forward_direction,
    int64_t upper_bound,
    int64_t &distance,
    bool &found)
{
  int ret = OB_SUCCESS;
  bool source_exists = false;
  bool goal_exists = false;
  int64_t best_distance = -1;
  release();
  distance = -1;
  found = false;
  if (OB_UNLIKELY(memory_limit_ <= 0
                  || upper_bound < 1 || upper_bound > GRAPH_PATH_MAX_HOPS
                  || (forward_direction != GraphPathDirection::OUT
                      && forward_direction != GraphPathDirection::IN)
                  || !source_identity.is_valid() || !goal_identity.is_valid()
                  || source_identity.graph_id_ != goal_identity.graph_id_
                  || source_identity.element_id_ != goal_identity.element_id_
                  || source_identity == goal_identity)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid bound shortest-distance search", K(ret),
             K(source_identity), K(goal_identity), K(forward_direction),
             K(upper_bound), K_(memory_limit));
  } else {
    forward_.direction_ = forward_direction;
    reverse_.direction_ = forward_direction == GraphPathDirection::OUT
        ? GraphPathDirection::IN : GraphPathDirection::OUT;
    if (OB_FAIL(validate_and_seed(forward_, source_identity, source_exists))) {
    } else if (source_exists
               && OB_FAIL(validate_and_seed(reverse_, goal_identity, goal_exists))) {
    } else if (source_exists && goal_exists) {
      // Both roots and every later GraphExpand target were checked under the
      // statement snapshot, so subsequent frontier-source checks are redundant.
      forward_.expand_.set_sources_verified(true);
      reverse_.expand_.set_sources_verified(true);
      while (OB_SUCC(ret) && best_distance < 0
             && !forward_.current().empty() && !reverse_.current().empty()
             && forward_.depth_ + reverse_.depth_ < upper_bound) {
        if (OB_FAIL(forward_.expand_.check_status())) {
        } else if (OB_FAIL(reverse_.expand_.check_status())) {
        } else {
          SearchSide *side = nullptr;
          if (OB_FAIL(choose_side(side))) {
            LOG_WARN("failed to choose shortest-path expansion side", K(ret));
          } else if (OB_ISNULL(side)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            SearchSide &opposite = side == &forward_ ? reverse_ : forward_;
            if (OB_FAIL(expand_level(*side, opposite, best_distance))) {
              LOG_WARN("failed to expand shortest-path BFS level", K(ret));
            }
          }
        }
      }
      if (OB_SUCC(ret) && best_distance >= 0) {
        found = true;
        distance = best_distance;
      }
    }
  }
  // Drop live graph state even for a successful search. Aggregate stats and
  // peak memory stay available until the next search or explicit release().
  release_state();
  return ret;
}

void GraphBidirectionalShortest::release_state()
{
  forward_.reset();
  reverse_.reset();
}

void GraphBidirectionalShortest::release()
{
  release_state();
  stats_ = GraphExpandStats();
  expanded_hop_count_ = 0;
}

} // namespace sql
} // namespace oceanbase
