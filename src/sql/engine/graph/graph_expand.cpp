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
#include "sql/engine/graph/graph_expand.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

GraphExpand::GraphExpand(ObIAllocator &allocator,
                         IGraphExpandAccess &access,
                         int64_t page_size,
                         int64_t memory_limit)
  : access_(access),
    identity_allocator_(allocator),
    page_size_(page_size),
    memory_limit_(memory_limit),
    fixed_memory_bytes_(0),
    direction_(GraphPathDirection::OUT),
    inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    source_identities_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    existing_sources_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    edge_page_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    target_identities_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    existing_targets_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    edge_index_(0),
    input_index_(0),
    has_after_edge_(false),
    end_(false),
    is_open_(false)
{
}

int GraphExpand::deep_copy_identity(const GraphElementIdentity &source,
                                    GraphElementIdentity &destination)
{
  int ret = OB_SUCCESS;
  destination.reset();
  if (!source.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    destination.graph_id_ = source.graph_id_;
    destination.element_id_ = source.element_id_;
    destination.key_count_ = source.key_count_;
    for (int64_t i = 0; OB_SUCC(ret) && i < source.key_count_; ++i) {
      if (OB_FAIL(deep_copy_obj(identity_allocator_, source.keys_[i],
                                destination.keys_[i]))) {
      }
    }
  }
  return ret;
}

int GraphExpand::deep_copy_inputs(const ObIArray<GraphExpandInput> &inputs)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < inputs.count(); ++i) {
    GraphExpandInput stable;
    stable.binding_id_ = inputs.at(i).binding_id_;
    stable.path_state_id_ = inputs.at(i).path_state_id_;
    if (OB_FAIL(deep_copy_identity(inputs.at(i).source_identity_,
                                   stable.source_identity_))) {
    } else if (OB_FAIL(inputs_.push_back(stable))) {
    }
  }
  return ret;
}

int GraphExpand::stabilize_identities(ObIArray<GraphElementIdentity> &identities)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < identities.count(); ++i) {
    GraphElementIdentity stable;
    if (OB_FAIL(deep_copy_identity(identities.at(i), stable))) {
    } else {
      identities.at(i) = stable;
    }
  }
  return ret;
}

int GraphExpand::stabilize_edge_page()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_page_.count(); ++i) {
    GraphExpandEdge stable;
    if (OB_FAIL(deep_copy_identity(edge_page_.at(i).source_identity_,
                                   stable.source_identity_))) {
    } else if (OB_FAIL(deep_copy_identity(edge_page_.at(i).edge_identity_,
                                          stable.edge_identity_))) {
    } else if (OB_FAIL(deep_copy_identity(edge_page_.at(i).target_identity_,
                                          stable.target_identity_))) {
    } else {
      edge_page_.at(i) = stable;
    }
  }
  return ret;
}

int GraphExpand::check_memory_limit()
{
  int ret = OB_SUCCESS;
  const int64_t identity_bytes = identity_allocator_.used();
  if (identity_bytes > memory_limit_
      || fixed_memory_bytes_ > memory_limit_ - identity_bytes) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
  } else {
    stats_.peak_path_memory_ = std::max(stats_.peak_path_memory_,
                                       fixed_memory_bytes_ + identity_bytes);
  }
  return ret;
}

bool GraphExpand::contains(const ObIArray<GraphElementIdentity> &identities,
                           const GraphElementIdentity &identity) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < identities.count(); ++i) {
    found = identities.at(i) == identity;
  }
  return found;
}

int GraphExpand::push_unique(ObIArray<GraphElementIdentity> &identities,
                             const GraphElementIdentity &identity)
{
  int ret = OB_SUCCESS;
  if (!contains(identities, identity)) {
    ret = identities.push_back(identity);
  }
  return ret;
}

int GraphExpand::validate_lookup_result(
    const ObIArray<GraphElementIdentity> &requested,
    const ObIArray<GraphElementIdentity> &existing) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < existing.count(); ++i) {
    if (!existing.at(i).is_valid() || !contains(requested, existing.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (existing.at(i) == existing.at(j)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int GraphExpand::validate_and_account(const ObIArray<GraphExpandInput> &inputs)
{
  int ret = OB_SUCCESS;
  int64_t bytes = 0;
  if (page_size_ <= 0 || page_size_ > 4096 || memory_limit_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (inputs.count() > 4096) {
    ret = OB_SIZE_OVERFLOW;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < inputs.count(); ++i) {
    if (inputs.at(i).binding_id_ < 0 || inputs.at(i).path_state_id_ < 0
        || !inputs.at(i).source_identity_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (inputs.at(i).binding_id_ == inputs.at(j).binding_id_
          && inputs.at(i).path_state_id_ == inputs.at(j).path_state_id_) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t per_input = sizeof(GraphExpandInput) + sizeof(GraphElementIdentity) + 128;
    const int64_t per_edge = sizeof(GraphExpandEdge) + 2 * sizeof(GraphElementIdentity) + 256;
    if (inputs.count() > INT64_MAX / per_input
        || page_size_ > (INT64_MAX - inputs.count() * per_input) / per_edge) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      bytes = inputs.count() * per_input + page_size_ * per_edge;
      fixed_memory_bytes_ = bytes;
      stats_.peak_path_memory_ = bytes;
      if (bytes > memory_limit_) {
        ret = OB_EXCEED_QUERY_MEM_LIMIT;
      }
    }
  }
  return ret;
}

int GraphExpand::open(const ObIArray<GraphExpandInput> &inputs,
                      GraphPathDirection direction)
{
  int ret = OB_SUCCESS;
  release();
  stats_ = GraphExpandStats();
  direction_ = direction;
  if (direction != GraphPathDirection::OUT && direction != GraphPathDirection::IN) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(validate_and_account(inputs))) {
  } else if (OB_FAIL(access_.check_status())) {
  } else if (OB_FAIL(deep_copy_inputs(inputs))) {
  } else if (OB_FAIL(check_memory_limit())) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < inputs_.count(); ++i) {
    ret = push_unique(source_identities_, inputs_.at(i).source_identity_);
  }
  if (OB_SUCC(ret)) {
    stats_.input_states_ = inputs.count();
    stats_.distinct_sources_ = source_identities_.count();
    if (source_identities_.empty()) {
      end_ = true;
    } else if (OB_FAIL(access_.lookup_vertices(source_identities_, existing_sources_))) {
    } else if (OB_FAIL(access_.check_status())) {
    } else if (OB_FAIL(stabilize_identities(existing_sources_))) {
    } else if (OB_FAIL(check_memory_limit())) {
    } else if (OB_FAIL(validate_lookup_result(source_identities_, existing_sources_))) {
    } else if (existing_sources_.empty()) {
      end_ = true;
    }
  }
  if (OB_SUCC(ret)) {
    is_open_ = true;
  } else {
    ret = fail(ret);
  }
  return ret;
}

int GraphExpand::load_next_page()
{
  int ret = OB_SUCCESS;
  bool page_end = false;
  edge_page_.reuse();
  target_identities_.reuse();
  existing_targets_.reuse();
  edge_index_ = 0;
  input_index_ = 0;
  if (OB_FAIL(access_.check_status())) {
  } else if (OB_FAIL(access_.scan_edges(existing_sources_, direction_,
                                        has_after_edge_ ? &after_edge_ : nullptr,
                                        page_size_, edge_page_, page_end))) {
  } else if (OB_FAIL(access_.check_status())) {
  } else if (edge_page_.count() > page_size_
             || (edge_page_.empty() && !page_end)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(stabilize_edge_page())) {
  } else if (OB_FAIL(check_memory_limit())) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_page_.count(); ++i) {
    const GraphExpandEdge &edge = edge_page_.at(i);
    if (!edge.source_identity_.is_valid() || !edge.edge_identity_.is_valid()
        || !edge.target_identity_.is_valid()
        || !contains(existing_sources_, edge.source_identity_)
        || (has_after_edge_ && edge.edge_identity_ == after_edge_)) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (edge.edge_identity_ == edge_page_.at(j).edge_identity_) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(push_unique(target_identities_, edge.target_identity_))) {
    }
  }
  if (OB_SUCC(ret) && !edge_page_.empty()) {
    stats_.scanned_edges_ += edge_page_.count();
    stats_.looked_up_vertices_ += target_identities_.count();
    if (OB_FAIL(access_.lookup_vertices(target_identities_, existing_targets_))) {
    } else if (OB_FAIL(access_.check_status())) {
    } else if (OB_FAIL(stabilize_identities(existing_targets_))) {
    } else if (OB_FAIL(check_memory_limit())) {
    } else if (OB_FAIL(validate_lookup_result(target_identities_, existing_targets_))) {
    } else {
      for (int64_t i = 0; i < edge_page_.count(); ++i) {
        if (!contains(existing_targets_, edge_page_.at(i).target_identity_)) {
          ++stats_.orphan_edges_;
        }
      }
      if (OB_FAIL(deep_copy_identity(edge_page_.at(edge_page_.count() - 1).edge_identity_,
                                     after_edge_))) {
      } else if (OB_FAIL(check_memory_limit())) {
      } else {
        has_after_edge_ = true;
      }
    }
  }
  if (OB_SUCC(ret)) {
    end_ = page_end;
  }
  return ret;
}

int GraphExpand::get_next_row(GraphExpandOutput &output)
{
  int ret = OB_SUCCESS;
  if (!is_open_) {
    ret = OB_NOT_INIT;
  }
  while (OB_SUCC(ret)) {
    if (edge_index_ >= edge_page_.count()) {
      if (end_) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(load_next_page())) {
      } else if (edge_page_.empty() && end_) {
        ret = OB_ITER_END;
      }
    }
    if (OB_SUCC(ret)) {
      const GraphExpandEdge &edge = edge_page_.at(edge_index_);
      if (contains(existing_targets_, edge.target_identity_)) {
        while (OB_SUCC(ret) && input_index_ < inputs_.count()) {
          if (OB_FAIL(access_.check_status())) {
          } else {
            const GraphExpandInput &input = inputs_.at(input_index_++);
            if (input.source_identity_ == edge.source_identity_) {
              output.binding_id_ = input.binding_id_;
              output.path_state_id_ = input.path_state_id_;
              output.edge_identity_ = edge.edge_identity_;
              output.target_identity_ = edge.target_identity_;
              ++stats_.output_states_;
              return OB_SUCCESS;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        ++edge_index_;
        input_index_ = 0;
      }
    }
  }
  if (ret != OB_ITER_END && ret != OB_NOT_INIT) {
    ret = fail(ret);
  }
  return ret;
}

int GraphExpand::rescan(const ObIArray<GraphExpandInput> &inputs,
                        GraphPathDirection direction)
{
  return open(inputs, direction);
}

int GraphExpand::fail(int error)
{
  release();
  return error;
}

void GraphExpand::release()
{
  inputs_.reset();
  source_identities_.reset();
  existing_sources_.reset();
  edge_page_.reset();
  target_identities_.reset();
  existing_targets_.reset();
  after_edge_.reset();
  identity_allocator_.reuse();
  edge_index_ = 0;
  input_index_ = 0;
  has_after_edge_ = false;
  end_ = false;
  is_open_ = false;
  fixed_memory_bytes_ = 0;
}

} // namespace sql
} // namespace oceanbase
