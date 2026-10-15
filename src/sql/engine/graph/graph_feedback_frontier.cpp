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
#include "sql/engine/graph/graph_feedback_frontier.h"
#include "share/ob_errno.h"
#include <algorithm>
#include <utility>

namespace oceanbase
{
using namespace common;
namespace sql
{
GraphFeedbackFrontier::GraphFeedbackFrontier(ObIAllocator &allocator,
                                             GraphExpand &expand,
                                             int64_t input_batch_size,
                                             int64_t memory_limit)
    : expand_(expand),
      state_store_(allocator, memory_limit),
      input_batcher_(allocator, state_store_, expand, input_batch_size),
      next_state_index_(allocator),
      input_batch_size_(input_batch_size),
      memory_limit_(memory_limit),
      frontier_buffer_a_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphFrontier")),
      frontier_buffer_b_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphFrontier")),
      path_buffer_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                   ModulePageAllocator(allocator, "GraphPathResult"))
{
}

int GraphFeedbackFrontier::get_memory_usage(bool include_input_batch, int64_t &used) const
{
  int ret = OB_SUCCESS;
  used = 0;
  const int64_t parts[] = {
      state_store_.used_memory(), frontier_buffer_a_.get_data_size(),
      frontier_buffer_b_.get_data_size(), path_buffer_.get_data_size(),
      next_state_index_.used_memory(), expand_.used_memory(),
      include_input_batch ? input_batcher_.used_memory() : 0};
  for (int64_t part : parts) {
    if (part < 0 || used > INT64_MAX - part) {
      ret = OB_SIZE_OVERFLOW;
      break;
    }
    used += part;
  }
  return ret;
}

int GraphFeedbackFrontier::check_memory_limit()
{
  int ret = OB_SUCCESS;
  int64_t used = 0;
  if (OB_UNLIKELY(memory_limit_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier memory limit", K(ret), K_(memory_limit));
  } else if (OB_FAIL(get_memory_usage(true, used))) {
    LOG_WARN("graph frontier memory accounting overflow", K(ret), K(used));
  } else if (OB_UNLIKELY(used > memory_limit_)) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
    LOG_WARN("graph frontier exceeded query memory limit", K(ret), K(used), K_(memory_limit));
  } else {
    peak_memory_bytes_ = std::max(peak_memory_bytes_, used);
    hop_stats_.record_memory(used);
  }
  return ret;
}

GraphExpandStats GraphFeedbackFrontier::get_pending_hop_stats() const
{
  return hop_stats_.snapshot(input_batcher_.source_count(),
      input_batcher_.has_source_index(), next_state_ids_->count(),
      batch_open_ ? &expand_.get_stats() : nullptr);
}

int64_t GraphFeedbackFrontier::batch_memory_budget() const
{
  int64_t used = 0;
  // InputBatcher checks its own total against the remaining budget.
  return get_memory_usage(false, used) != OB_SUCCESS || used > memory_limit_
      ? 0 : memory_limit_ - used;
}

int GraphFeedbackFrontier::add_seed(
    int64_t binding_id,
    const GraphElementIdentity &source_identity)
{
  int ret = OB_SUCCESS;
  int64_t path_state_id = GRAPH_INVALID_PATH_STATE_ID;
  if (OB_UNLIKELY(hop_ != 0 || expanding_ || input_batch_size_ <= 0
                  || input_batch_size_ > GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE
                  || memory_limit_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier seed state", K(ret), K_(hop),
             K_(input_batch_size), K_(memory_limit));
  } else if (OB_FAIL(state_store_.add_root(binding_id, source_identity,
                                            path_state_id))) {
    LOG_WARN("failed to add graph frontier seed", K(ret), K(binding_id));
  } else if (OB_FAIL(current_state_ids_->push_back(path_state_id))) {
    LOG_WARN("failed to append graph frontier state id", K(ret),
             K(path_state_id));
  } else if (OB_FAIL(check_memory_limit())) {
  }
  if (OB_SUCCESS != ret) {
    ret = fail(ret);
  }
  return ret;
}

int GraphFeedbackFrontier::begin_hop(
    GraphPathDirection direction, GraphPathMode path_mode, bool prune_duplicate_states)
{
  int ret = OB_SUCCESS;
  next_state_ids_->reuse();
  next_state_index_.reset();
  if (OB_FAIL(input_batcher_.begin_level(*current_state_ids_, hop_, batch_memory_budget()))) {
    LOG_WARN("failed to group graph frontier sources", K(ret), K_(hop));
  } else {
    hop_stats_.begin(current_state_ids_->count(), input_batcher_.has_source_index()
        || !expand_.can_partition_sources() || current_state_ids_->count() <= input_batch_size_);
  }
  if (OB_SUCC(ret) && prune_duplicate_states
             && OB_FAIL(next_state_index_.init(current_state_ids_->count()))) {
  }
  if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
  } else if (OB_SUCC(ret)) {
    active_direction_ = direction;
    active_path_mode_ = path_mode;
    active_prune_duplicate_states_ = prune_duplicate_states;
    expanding_ = true;
  }
  return ret;
}

int GraphFeedbackFrontier::finish_hop()
{
  int ret = OB_SUCCESS;
  // Only a fully consumed level can become the input to the following hop.
  std::swap(current_state_ids_, next_state_ids_);
  next_state_ids_->reuse();
  ++hop_;
  expanding_ = false;
  if (OB_FAIL(check_memory_limit())) {
  } else {
    hop_stats_.finish(input_batcher_.source_count(),
                      input_batcher_.has_source_index(), current_state_ids_->count());
    LOG_TRACE("completed graph frontier hop", K_(hop), "stats", hop_stats_.last());
    input_batcher_.finish_level();
    next_state_index_.reset();
  }
  return ret;
}

int GraphFeedbackFrontier::accept_extension(
    const GraphExpandOutput &extension, GraphPathMode path_mode,
    bool prune_duplicate_states, bool &accepted) const
{
  int ret = OB_SUCCESS;
  bool repeated_edge = false;
  bool repeated_state = false;
  int64_t entry_index = -1;
  accepted = false;
  // TRAIL checks one path's ancestry; optional pruning checks only equal
  // binding/vertex states in this next level, never a global visited set.
  if (path_mode == GraphPathMode::TRAIL
      && OB_FAIL(state_store_.contains_edge(
          extension.path_state_id_, extension.edge_identity_, repeated_edge))) {
    LOG_WARN("failed to inspect graph path edge history", K(ret), K(extension));
  } else if (prune_duplicate_states
             && OB_FAIL(next_state_index_.find(extension.target_identity_,
                 entry_index, repeated_state, extension.binding_id_))) {
    LOG_WARN("failed to probe reachable graph state", K(ret));
  } else {
    accepted = !repeated_edge && !repeated_state;
  }
  return ret;
}

int GraphFeedbackFrontier::save_extension(
    const GraphExpandOutput &extension, bool prune_duplicate_states)
{
  int ret = OB_SUCCESS;
  int64_t entry_index = -1;
  int64_t child_state_id = GRAPH_INVALID_PATH_STATE_ID;
  GraphPathState child;
  bool inserted = false;
  if (OB_FAIL(state_store_.add_child(extension, child_state_id))) {
    LOG_WARN("failed to add graph frontier child", K(ret), K(extension));
  } else if (OB_FAIL(next_state_ids_->push_back(child_state_id))) {
    LOG_WARN("failed to append next graph frontier state", K(ret),
             K(child_state_id));
  } else if (prune_duplicate_states
             && OB_FAIL(state_store_.get_state(child_state_id, child))) {
  } else if (prune_duplicate_states
             && OB_FAIL(next_state_index_.get_or_insert(
                 child.current_identity_, entry_index, inserted, child.binding_id_))) {
    // Index the stable state-store copy, never the reusable DAS page.
    LOG_WARN("failed to index reachable graph state", K(ret));
  } else if (OB_FAIL(check_memory_limit())) {
  }
  return ret;
}

int GraphFeedbackFrontier::consume_input_batch(
    GraphPathDirection direction,
    GraphPathMode path_mode,
    bool prune_duplicate_states,
    int64_t output_end)
{
  int ret = OB_SUCCESS;
  bool batch_end = false;
  if (!batch_open_) {
    if (OB_FAIL(expand_.rescan(input_batcher_.batch(), direction))) {
      LOG_WARN("failed to open graph expand for frontier batch", K(ret),
               "input_count", input_batcher_.batch().count());
    } else {
      batch_open_ = true;
    }
  }
  while (OB_SUCC(ret) && next_state_ids_->count() < output_end) {
    GraphExpandOutput extension;
    int next_ret = expand_.get_next_row(extension);
    if (next_ret == OB_ITER_END) {
      batch_end = true;
      break;
    } else if (OB_SUCCESS != next_ret) {
      ret = next_ret;
      LOG_WARN("failed to read graph frontier extension", K(ret));
    } else {
      bool accepted = false;
      if (OB_FAIL(accept_extension(extension, path_mode, prune_duplicate_states, accepted))) {
      } else if (accepted && OB_FAIL(save_extension(extension, prune_duplicate_states))) {
      }
    }
  }
  // Include the access component's resident buffers before releasing its
  // current scanner state. Its reusable identity arena remains accounted by
  // GraphExpand::used_memory() after release().
  if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
  }
  if (OB_SUCC(ret) && batch_end) {
    hop_stats_.add_batch(expand_.get_stats());
  }
  // Keep the current DAS page/cursor alive while the parent consumes this
  // output batch. close/rescan/error releases it even if LIMIT stops early.
  if (OB_SUCCESS != ret || batch_end) {
    expand_.release();
    batch_open_ = false;
  }
  return ret;
}

int GraphFeedbackFrontier::expand(GraphPathDirection direction,
                                  GraphPathMode path_mode,
                                  bool prune_duplicate_states)
{
  int ret = OB_SUCCESS;
  // A scheduling quantum, never a result/fanout limit. In particular a high
  // degree source can yield before its complete adjacency is materialized.
  static constexpr int64_t OUTPUT_BATCH_SIZE = 128;
  if (expanding_
      && (direction != active_direction_ || path_mode != active_path_mode_
          || prune_duplicate_states != active_prune_duplicate_states_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("graph frontier hop parameters changed during continuation", K(ret),
             K_(hop), K(direction), K(path_mode), K(prune_duplicate_states),
             K_(active_direction), K_(active_path_mode), K_(active_prune_duplicate_states));
  } else if (current_state_ids_->empty()) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(hop_ >= GRAPH_PATH_MAX_HOPS
                         || (direction != GraphPathDirection::OUT
                             && direction != GraphPathDirection::IN)
                         || (path_mode != GraphPathMode::WALK
                             && path_mode != GraphPathMode::TRAIL)
                         || (prune_duplicate_states && path_mode != GraphPathMode::WALK))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier expansion", K(ret), K_(hop),
             "direction", static_cast<int64_t>(direction),
             "path_mode", static_cast<int64_t>(path_mode));
  } else {
    if (!expanding_) {
      ret = begin_hop(direction, path_mode, prune_duplicate_states);
    }
    const int64_t output_end = next_state_ids_->count() > INT64_MAX - OUTPUT_BATCH_SIZE
        ? INT64_MAX : next_state_ids_->count() + OUTPUT_BATCH_SIZE;
    while (OB_SUCC(ret) && (batch_open_ || !input_batcher_.exhausted())
           && next_state_ids_->count() < output_end) {
      if (!batch_open_) {
        if (OB_FAIL(input_batcher_.next_batch(batch_memory_budget()))) {
        } else if (OB_FAIL(check_memory_limit())) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(consume_input_batch(
              direction, path_mode, prune_duplicate_states, output_end))) {
      }
    }
    if (OB_SUCC(ret) && !batch_open_ && input_batcher_.exhausted()) {
      ret = finish_hop();
    } else if (OB_SUCC(ret)) {
      LOG_TRACE("yielded graph frontier batch", "hop", get_output_hop(),
                "stats", get_pending_hop_stats());
    }
  }
  if (OB_SUCCESS != ret && OB_ITER_END != ret) {
    ret = fail(ret);
  }
  return ret;
}

int GraphFeedbackFrontier::get_frontier_state(
    int64_t index,
    GraphPathState &state) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(index < 0 || index >= output_state_ids().count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier index", K(ret), K(index),
             "frontier_count", output_state_ids().count());
  } else if (OB_FAIL(state_store_.get_state(output_state_ids().at(index),
                                             state))) {
  } else if (OB_UNLIKELY(state.hop_ != get_output_hop())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph frontier state hop is inconsistent", K(ret), K_(hop),
             K(state));
  }
  return ret;
}

int GraphFeedbackFrontier::get_frontier_path(
    int64_t index,
    const ObIArray<GraphPathState> *&path)
{
  int ret = OB_SUCCESS;
  GraphPathState leaf;
  path = nullptr;
  if (OB_FAIL(get_frontier_state(index, leaf))) {
  } else if (OB_FAIL(state_store_.build_path(leaf.path_state_id_,
                                             path_buffer_))) {
    LOG_WARN("failed to reconstruct graph frontier path", K(ret), K(index),
             K(leaf));
  } else if (OB_FAIL(check_memory_limit())) {
    LOG_WARN("failed to account graph frontier path", K(ret), K(index),
             K(leaf));
  } else {
    path = &path_buffer_;
  }
  if (OB_SUCCESS != ret) {
    path_buffer_.reuse();
  }
  return ret;
}

int GraphFeedbackFrontier::fail(int error)
{
  reset();
  return error;
}

void GraphFeedbackFrontier::reset()
{
  expand_.release();
  // Release borrowed identities and access batches before their state store.
  input_batcher_.reset();
  next_state_index_.reset();
  state_store_.reset();
  frontier_buffer_a_.reset();
  frontier_buffer_b_.reset();
  // ModulePageAllocator may wrap the query arena, whose individual free() is
  // a no-op. Keep this reusable result block across rescans so reset does not
  // lose the only pointer to memory that remains charged to the query.
  path_buffer_.reuse();
  current_state_ids_ = &frontier_buffer_a_;
  next_state_ids_ = &frontier_buffer_b_;
  hop_ = 0;
  peak_memory_bytes_ = 0;
  hop_stats_.reset();
  active_direction_ = GraphPathDirection::OUT;
  active_path_mode_ = GraphPathMode::WALK;
  active_prune_duplicate_states_ = false;
  expanding_ = false;
  batch_open_ = false;
}


} // namespace sql
} // namespace oceanbase
