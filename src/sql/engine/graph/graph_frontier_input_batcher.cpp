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
#include "sql/engine/graph/graph_frontier_input_batcher.h"
#include <algorithm>

namespace oceanbase
{
using namespace common;
namespace sql
{

GraphFrontierInputBatcher::GraphFrontierInputBatcher(
    ObIAllocator &allocator, const GraphPathStateStore &states, GraphExpand &expand,
    int64_t batch_size)
    : states_(states), expand_(expand), source_index_(allocator),
      source_groups_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphFrontier")),
      next_source_positions_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphFrontier")),
      input_batch_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphFrontier")),
      batch_size_(batch_size)
{}

int64_t GraphFrontierInputBatcher::used_memory() const
{
  int64_t bytes = source_index_.used_memory();
  const int64_t parts[] = {source_groups_.get_data_size(),
      next_source_positions_.get_data_size(), input_batch_.get_data_size()};
  for (int64_t part : parts) {
    if (part < 0 || bytes < 0 || bytes > INT64_MAX - part) { return INT64_MAX; }
    bytes += part;
  }
  return bytes;
}

int GraphFrontierInputBatcher::check_memory_limit(int64_t memory_budget) const
{
  return memory_budget <= 0 || used_memory() > memory_budget
      ? OB_EXCEED_QUERY_MEM_LIMIT : OB_SUCCESS;
}

int GraphFrontierInputBatcher::begin_level(
    const ObIArray<int64_t> &states, int64_t hop, int64_t memory_budget)
{
  int ret = OB_SUCCESS;
  GraphPathState first;
  source_index_.reset();
  source_groups_.reuse();
  next_source_positions_.reuse();
  input_batch_.reuse();
  state_ids_ = &states;
  hop_ = hop;
  next_position_ = 0;
  if (states.empty() || batch_size_ <= 0 || batch_size_ > GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(states_.get_state(states.at(0), first))) {
  } else {
    // This execution capability must not depend on starting statistics.
    source_index_enabled_ = !identity_has_double_key(first.current_identity_);
    batch_by_source_groups_ = expand_.can_partition_sources() && source_index_enabled_;
    if (source_index_enabled_ && OB_FAIL(source_index_.init(states.count()))) {
    } else if (OB_FAIL(check_memory_limit(memory_budget))) {
    } else if (OB_FAIL(prepare_source_groups(memory_budget))) {
    }
  }
  return ret;
}

void GraphFrontierInputBatcher::finish_level()
{
  source_index_.reset();
  state_ids_ = nullptr;
}

void GraphFrontierInputBatcher::reset()
{
  source_index_.reset();
  source_groups_.reset();
  next_source_positions_.reset();
  input_batch_.reset();
  state_ids_ = nullptr;
  hop_ = 0;
  next_position_ = 0;
  source_index_enabled_ = false;
  batch_by_source_groups_ = false;
}

int GraphFrontierInputBatcher::prepare_source_groups(int64_t memory_budget)
{
  int ret = OB_SUCCESS;
  // DOUBLE tolerance equality is non-transitive. Preserve its bounded linear
  // matching path rather than assigning each input to only one source group.
  for (int64_t i = 0; OB_SUCC(ret) && batch_by_source_groups_
                          && i < state_ids_->count(); ++i) {
    GraphPathState state;
    int64_t group_index = -1;
    bool inserted = false;
    if (i % batch_size_ == 0 && OB_FAIL(expand_.check_status())) {
    } else if (OB_FAIL(states_.get_state(state_ids_->at(i), state))) {
    } else if (OB_UNLIKELY(state.hop_ != hop_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(source_index_.get_or_insert(
                   state.current_identity_, group_index, inserted))) {
    } else if (inserted && OB_FAIL(source_groups_.push_back(SourceGroup()))) {
    } else if (OB_UNLIKELY(group_index < 0 || group_index >= source_groups_.count())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(next_source_positions_.push_back(-1))) {
    } else {
      SourceGroup &group = source_groups_.at(group_index);
      if (group.last_position_ >= 0) {
        next_source_positions_.at(group.last_position_) = i;
      } else {
        group.first_position_ = i;
      }
      group.last_position_ = i;
      ret = check_memory_limit(memory_budget);
    }
  }
  return ret;
}

int GraphFrontierInputBatcher::append_input(int64_t position, int64_t memory_budget)
{
  int ret = OB_SUCCESS;
  GraphPathState state;
  GraphExpandInput input;
  if (OB_UNLIKELY(position < 0 || position >= state_ids_->count())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(states_.get_state(state_ids_->at(position), state))) {
  } else if (OB_UNLIKELY(state.hop_ != hop_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(states_.make_expand_input(state_ids_->at(position), input))) {
  } else if (OB_FAIL(input_batch_.push_back(input))) {
  } else {
    if (!batch_by_source_groups_ && source_index_enabled_) {
      int64_t index = -1;
      bool inserted = false;
      if (identity_has_double_key(input.source_identity_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = source_index_.get_or_insert(input.source_identity_, index, inserted);
      }
    }
    if (OB_SUCC(ret) && input_batch_.count() % batch_size_ == 0) {
      ret = expand_.check_status();
    }
    if (OB_SUCC(ret)) { ret = check_memory_limit(memory_budget); }
  }
  return ret;
}

int GraphFrontierInputBatcher::next_batch(int64_t memory_budget)
{
  int ret = OB_SUCCESS;
  const int64_t start = next_position_;
  input_batch_.reuse();
  const int64_t input_units = state_ids_ == nullptr ? 0 : this->input_units();
  if (OB_UNLIKELY(start < 0 || start >= input_units
                  || batch_size_ <= 0
                  || batch_size_ > GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier input batch", K(ret), K(start),
             K(input_units), K_(batch_size));
  } else {
    // Keep all bindings of a source together, even when they occur far apart
    // in the frontier. Full scans still receive the complete level.
    const int64_t batch_size = expand_.can_partition_sources()
        ? batch_size_ : state_ids_->count();
    const int64_t count = std::min(batch_size,
                                   input_units - start);
    next_position_ = start + count;
    for (int64_t i = start; OB_SUCC(ret) && i < next_position_; ++i) {
      int64_t position = batch_by_source_groups_ ? source_groups_.at(i).first_position_ : i;
      while (OB_SUCC(ret) && position >= 0) {
        if (OB_FAIL(append_input(position, memory_budget))) {
          LOG_WARN("failed to append grouped graph input", K(ret), K(position));
        } else {
          position = batch_by_source_groups_ ? next_source_positions_.at(position) : -1;
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit(memory_budget))) {
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
