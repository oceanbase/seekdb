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
#include "sql/engine/expr/ob_expr_util.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int GraphEdgePayload::deep_copy(const GraphEdgePayload &source,
                                ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  values_.reset();
  if (source.values_.count_ == 0) {
  } else if (OB_UNLIKELY(!source.values_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t size = source.values_.get_deep_copy_size();
    char *buffer = static_cast<char *>(allocator.alloc(size));
    int64_t pos = 0;
    if (OB_ISNULL(buffer)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(values_.deep_copy(source.values_, buffer, size, pos))) {
      LOG_WARN("failed to retain graph edge payload", K(ret), K(size));
    }
  }
  return ret;
}

// Conservative fixed-memory reserves for container bookkeeping and identity
// buffers; variable-length ObObj payloads are accounted by identity_allocator_.
static constexpr int64_t GRAPH_EXPAND_INPUT_MEMORY_RESERVE_BYTES = 128;
static constexpr int64_t GRAPH_EXPAND_EDGE_MEMORY_RESERVE_BYTES = 256;
static constexpr int64_t GRAPH_EXPAND_INPUT_KEY_SLOT_MULTIPLIER = 2;
static constexpr int64_t GRAPH_EXPAND_INPUT_KEY_BYTES_PER_INPUT =
    GRAPH_EXPAND_INPUT_KEY_SLOT_MULTIPLIER * sizeof(int64_t);
static constexpr int64_t GRAPH_EXPAND_VERTEX_LOOKUP_BATCH_SIZE =
    GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE;

namespace
{

int64_t saturated_nonnegative_add(int64_t left, int64_t right)
{
  return left < 0 || right < 0 || left > INT64_MAX - right
      ? INT64_MAX : left + right;
}

} // namespace

GraphExpand::GraphExpand(ObIAllocator &allocator,
                         IGraphExpandAccess &access,
                         int64_t page_size,
                         int64_t memory_limit)
  : access_(access),
    identity_allocator_(allocator),
    input_bindings_(allocator, access, memory_limit),
    existing_source_index_(allocator),
    page_size_(page_size),
    memory_limit_(memory_limit),
    source_lookup_batch_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphExpand")),
    source_lookup_results_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                           ModulePageAllocator(allocator, "GraphExpand")),
    existing_sources_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    edge_page_(allocator)
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
    if (OB_FAIL(source.rowkey_.deep_copy(destination.rowkey_,
                                         identity_allocator_))) {
    }
  }
  return ret;
}

int GraphExpand::lookup_source_vertices()
{
  int ret = OB_SUCCESS;
  existing_sources_.reuse();
  for (int64_t start = 0;
       OB_SUCC(ret) && start < input_bindings_.sources().count();) {
    source_lookup_batch_.reuse();
    source_lookup_results_.reuse();
    const int64_t batch_count = std::min(
        GRAPH_EXPAND_VERTEX_LOOKUP_BATCH_SIZE,
        input_bindings_.sources().count() - start);
    const int64_t end = start + batch_count;
    for (int64_t i = start; OB_SUCC(ret) && i < end; ++i) {
      if (OB_FAIL(source_lookup_batch_.push_back(input_bindings_.sources().at(i)))) {
        LOG_WARN("failed to append graph source lookup request", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
      LOG_WARN("graph source lookup batch exceeds memory limit", K(ret),
               K(start), K(end));
    } else if (OB_SUCC(ret)
               && OB_FAIL(access_.lookup_vertices(
                   source_lookup_batch_, source_lookup_results_,
                   GraphVertexLookupPhase::SOURCE))) {
      LOG_WARN("failed to look up graph source vertices", K(ret),
               K(start), K(end));
    } else if (OB_SUCC(ret) && OB_FAIL(access_.check_status())) {
    }
    for (int64_t i = 0;
         OB_SUCC(ret) && i < source_lookup_results_.count(); ++i) {
      GraphElementIdentity stable;
      if (OB_FAIL(deep_copy_identity(source_lookup_results_.at(i), stable))) {
        LOG_WARN("failed to stabilize graph source lookup result", K(ret),
                 K(i), K(start), K(end));
      } else if (OB_FAIL(existing_sources_.push_back(stable))) {
        LOG_WARN("failed to append existing graph source", K(ret), K(i),
                 K(start), K(end));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
      LOG_WARN("graph source lookup result exceeds memory limit", K(ret),
               K(start), K(end));
    }
    start = end;
  }
  return ret;
}

int GraphExpand::build_existing_source_index()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!input_bindings_.grouped())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(existing_source_index_.init(existing_sources_.count()))) {
    LOG_WARN("failed to initialize existing graph source index", K(ret),
             "source_count", existing_sources_.count());
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < existing_sources_.count(); ++i) {
    int64_t request_index = -1;
    int64_t existing_index = -1;
    bool requested = false;
    bool inserted = false;
    const GraphElementIdentity &identity = existing_sources_.at(i);
    if (OB_UNLIKELY(!identity.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(input_bindings_.find_source(
                   identity, request_index, requested))) {
      LOG_WARN("failed to find requested graph source", K(ret), K(identity));
    } else if (OB_UNLIKELY(!requested)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph source lookup returned an unexpected row", K(ret),
               K(identity));
    } else if (OB_FAIL(existing_source_index_.get_or_insert(
                   identity, existing_index, inserted))) {
      LOG_WARN("failed to index existing graph source", K(ret), K(identity));
    } else if (OB_UNLIKELY(!inserted)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph source lookup returned a duplicate row", K(ret),
               K(identity), K(existing_index));
    }
  }
  return ret;
}

int GraphExpand::check_memory_limit()
{
  int ret = OB_SUCCESS;
  const int64_t resident_bytes = used_memory();
  if (resident_bytes > memory_limit_) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
  } else {
    stats_.peak_path_memory_ = std::max(stats_.peak_path_memory_,
                                       resident_bytes);
  }
  return ret;
}

int64_t GraphExpand::used_memory() const
{
  // ObArray allocates block-rounded capacity and reuse() retains arena pages.
  // Compare the conservative preflight reservation with actual resident
  // capacity so small batches still account every allocated container block.
  const int64_t container_parts[] = {
      input_bindings_.used_memory(),
      existing_source_index_.used_memory(),
      source_lookup_batch_.get_data_size(),
      source_lookup_results_.get_data_size(),
      existing_sources_.get_data_size(),
      edge_page_.used_memory()};
  int64_t container_bytes = 0;
  for (int64_t i = 0; container_bytes < INT64_MAX
                          && i < ARRAYSIZEOF(container_parts); ++i) {
    if (container_parts[i] < 0
        || container_bytes > INT64_MAX - container_parts[i]) {
      container_bytes = INT64_MAX;
    } else {
      container_bytes += container_parts[i];
    }
  }
  int64_t used_bytes = std::max(fixed_memory_bytes_, container_bytes);
  const int64_t variable_parts[] = {
      identity_allocator_.total(), access_.used_memory()};
  for (int64_t i = 0; used_bytes < INT64_MAX
                          && i < ARRAYSIZEOF(variable_parts); ++i) {
    if (variable_parts[i] < 0
        || used_bytes > INT64_MAX - variable_parts[i]) {
      used_bytes = INT64_MAX;
    } else {
      used_bytes += variable_parts[i];
    }
  }
  return used_bytes;
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
  if (page_size_ <= 0 || page_size_ > GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE
      || memory_limit_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < inputs.count(); ++i) {
    if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
    } else if (inputs.at(i).binding_id_ < 0 || inputs.at(i).path_state_id_ < 0
        || !inputs.at(i).source_identity_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_SUCC(ret)) {
    // Reserve the worst case of one distinct source per input: one identity
    // index entry/bucket, one group descriptor and one next-input link.
    const int64_t source_index_entry_bytes =
        sizeof(GraphElementIdentity) + 2 * sizeof(int64_t);
    const int64_t source_index_bucket_bytes = sizeof(int64_t);
    const int64_t existing_source_index_bytes =
        source_index_entry_bytes + source_index_bucket_bytes;
    const int64_t source_input_group_bytes =
        3 * sizeof(int64_t);
    const int64_t per_input = sizeof(GraphExpandInput)
        + 2 * sizeof(GraphElementIdentity)
        + source_index_entry_bytes + source_index_bucket_bytes
        + existing_source_index_bytes
        + source_input_group_bytes
        + GRAPH_EXPAND_INPUT_MEMORY_RESERVE_BYTES;
    const int64_t per_edge = sizeof(GraphExpandEdge) + 2 * sizeof(GraphElementIdentity)
        + GRAPH_EXPAND_EDGE_MEMORY_RESERVE_BYTES;
    const int64_t lookup_batch_count = std::min(
        inputs.count(), GRAPH_EXPAND_VERTEX_LOOKUP_BATCH_SIZE);
    const int64_t lookup_batch_bytes = lookup_batch_count
        * 2 * sizeof(GraphElementIdentity);
    if (inputs.count() > (INT64_MAX - lookup_batch_bytes) / per_input
        || inputs.count()
               > (INT64_MAX - (OB_MALLOC_NORMAL_BLOCK_SIZE - 1))
                     / GRAPH_EXPAND_INPUT_KEY_BYTES_PER_INPUT
        || page_size_ > (INT64_MAX - lookup_batch_bytes
                         - inputs.count() * per_input) / per_edge) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      const int64_t steady_bytes =
          inputs.count() * per_input + lookup_batch_bytes
          + page_size_ * per_edge;
      const int64_t slot_payload_bytes =
          inputs.count() * GRAPH_EXPAND_INPUT_KEY_BYTES_PER_INPUT;
      const int64_t slot_remainder =
          slot_payload_bytes % OB_MALLOC_NORMAL_BLOCK_SIZE;
      const int64_t transient_bytes = slot_payload_bytes == 0
          ? 0
          : slot_payload_bytes
                + (slot_remainder == 0
                       ? 0 : OB_MALLOC_NORMAL_BLOCK_SIZE - slot_remainder);
      bytes = std::max(steady_bytes, transient_bytes);
      fixed_memory_bytes_ = steady_bytes;
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
  } else if (OB_FAIL(input_bindings_.init(inputs))) {
  } else if (access_.can_partition_sources()
             && input_bindings_.sources().count() > GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE) {
    // Bound physical ranges, not the number of associated path/seed bindings.
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(check_memory_limit())) {
  }
  if (OB_SUCC(ret)) {
    stats_.input_states_ = inputs.count();
    stats_.distinct_sources_ = input_bindings_.sources().count();
    if (input_bindings_.sources().empty()) {
      end_ = true;
    } else if (OB_FAIL(lookup_source_vertices())) {
    } else if (existing_sources_.empty()) {
      end_ = true;
    } else if (input_bindings_.grouped()
               && OB_FAIL(build_existing_source_index())) {
    } else if (!input_bindings_.grouped()
               && OB_FAIL(validate_lookup_result(
                   input_bindings_.sources(), existing_sources_))) {
    } else if (OB_FAIL(check_memory_limit())) {
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
  int64_t scanned_edges = 0;
  edge_page_.reuse();
  edge_index_ = 0;
  input_group_started_ = false;
  if (OB_FAIL(access_.check_status())) {
  } else if (OB_FAIL(access_.scan_edges(existing_sources_, direction_,
                                        edge_page_.continuation(),
                                        page_size_, edge_page_.edges(), scanned_edges,
                                        page_end))) {
  } else if (OB_FAIL(access_.check_status())) {
  } else if (edge_page_.edges().count() > page_size_
             || (edge_page_.edges().empty() && !page_end)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(edge_page_.stabilize_edge_page())) {
  } else if (OB_FAIL(check_memory_limit())) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_page_.edges().count(); ++i) {
    const GraphExpandEdge &edge = edge_page_.edges().at(i);
    bool source_exists = false;
    if (!edge.source_identity_.is_valid() || !edge.edge_identity_.is_valid()
        || !edge.target_identity_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (input_bindings_.grouped()) {
      int64_t source_index = -1;
      if (OB_FAIL(existing_source_index_.find(
              edge.source_identity_, source_index, source_exists))) {
        LOG_WARN("failed to find existing graph edge source", K(ret), K(edge));
      }
    } else {
      source_exists = contains(existing_sources_, edge.source_identity_);
    }
    if (OB_SUCC(ret)
        && (!source_exists
            || (edge_page_.continuation() != nullptr
                && edge.edge_identity_ == *edge_page_.continuation()))) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (edge.edge_identity_ == edge_page_.edges().at(j).edge_identity_) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(push_unique(edge_page_.targets(), edge.target_identity_))) {
    }
  }
  if (OB_SUCC(ret)) {
    stats_.scanned_edges_ = saturated_nonnegative_add(
        stats_.scanned_edges_, scanned_edges);
  }
  if (OB_SUCC(ret) && !edge_page_.edges().empty()) {
    stats_.looked_up_vertices_ += edge_page_.targets().count();
    if (OB_FAIL(access_.lookup_vertices(
            edge_page_.targets(), edge_page_.existing_targets(),
            GraphVertexLookupPhase::TARGET))) {
    } else if (OB_FAIL(access_.check_status())) {
    } else if (OB_FAIL(edge_page_.stabilize_targets())) {
    } else if (OB_FAIL(check_memory_limit())) {
    } else if (OB_FAIL(validate_lookup_result(edge_page_.targets(), edge_page_.existing_targets()))) {
    } else {
      for (int64_t i = 0; i < edge_page_.edges().count(); ++i) {
        if (!contains(edge_page_.existing_targets(), edge_page_.edges().at(i).target_identity_)) {
          ++stats_.orphan_edges_;
        }
      }
      if (OB_FAIL(edge_page_.save_continuation())) {
      } else if (OB_FAIL(check_memory_limit())) {
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
    if (edge_index_ >= edge_page_.edges().count()) {
      if (end_) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(load_next_page())) {
      } else if (edge_page_.edges().empty() && end_) {
        ret = OB_ITER_END;
      }
    }
    if (OB_SUCC(ret)) {
      const GraphExpandEdge &edge = edge_page_.edges().at(edge_index_);
      if (contains(edge_page_.existing_targets(), edge.target_identity_)) {
        const GraphExpandInput *input = nullptr;
        if (!input_group_started_) {
          if (OB_FAIL(input_bindings_.begin_edge(edge.source_identity_))) {
          } else { input_group_started_ = true; }
        }
        if (OB_SUCC(ret) && OB_FAIL(input_bindings_.next_input(edge.source_identity_, input))) {
        } else if (OB_SUCC(ret) && input != nullptr) {
          output.binding_id_ = input->binding_id_;
          output.path_state_id_ = input->path_state_id_;
          output.edge_identity_ = edge.edge_identity_;
          output.target_identity_ = edge.target_identity_;
          output.edge_payload_ = edge.payload_;
          ++stats_.output_states_;
          return OB_SUCCESS;
        }
      }
      if (OB_SUCC(ret)) {
        ++edge_index_;
        input_group_started_ = false;
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
  access_.release();
  input_bindings_.reset();
  existing_source_index_.reset();
  source_lookup_batch_.reset();
  source_lookup_results_.reset();
  existing_sources_.reset();
  edge_page_.reset();
  identity_allocator_.reuse();
  edge_index_ = 0;
  end_ = false;
  is_open_ = false;
  input_group_started_ = false;
  fixed_memory_bytes_ = 0;
}

} // namespace sql
} // namespace oceanbase
