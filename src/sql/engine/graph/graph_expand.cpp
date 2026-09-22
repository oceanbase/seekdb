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
#include "share/schema/graph_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

static constexpr int64_t GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE = 4096;
// Conservative fixed-memory reserves for container bookkeeping and identity
// buffers; variable-length ObObj payloads are accounted by identity_allocator_.
static constexpr int64_t GRAPH_EXPAND_INPUT_MEMORY_RESERVE_BYTES = 128;
static constexpr int64_t GRAPH_EXPAND_EDGE_MEMORY_RESERVE_BYTES = 256;

namespace
{

bool valid_graph_key_count(int64_t count)
{
  return count > 0 && count <= GRAPH_IDENTITY_MAX_KEYS;
}

bool valid_graph_columns(const uint64_t *columns, int64_t count)
{
  bool valid = columns != nullptr && valid_graph_key_count(count);
  for (int64_t i = 0; valid && i < count; ++i) {
    valid = columns[i] != OB_INVALID_ID;
  }
  return valid;
}

void copy_graph_columns(const uint64_t *source,
                        int64_t count,
                        uint64_t *destination)
{
  for (int64_t i = 0; i < count; ++i) {
    destination[i] = source[i];
  }
}

bool index_has_adjacency_prefix(const ObTableSchema &index_schema,
                                const uint64_t *columns,
                                int64_t column_count)
{
  const ObIndexInfo &index_columns = index_schema.get_index_info();
  bool matches = valid_graph_columns(columns, column_count)
      && index_columns.get_size() >= column_count;
  for (int64_t i = 0; matches && i < column_count; ++i) {
    const ObIndexColumn *column = index_columns.get_column(i);
    matches = column != nullptr && column->column_id_ == columns[i];
  }
  return matches;
}

} // namespace

bool GraphExpandAccessDesc::is_valid() const
{
  return source_table_id_ != OB_INVALID_ID && source_table_version_ > 0
      && edge_table_id_ != OB_INVALID_ID && edge_table_version_ > 0
      && target_table_id_ != OB_INVALID_ID && target_table_version_ > 0
      && edge_access_table_id_ != OB_INVALID_ID
      && edge_access_table_version_ > 0
      && valid_graph_columns(source_key_columns_, source_key_count_)
      && valid_graph_columns(edge_key_columns_, edge_key_count_)
      && valid_graph_columns(target_key_columns_, target_key_count_)
      && valid_graph_columns(edge_current_columns_, source_key_count_)
      && valid_graph_columns(edge_next_columns_, target_key_count_);
}

int GraphExpandAccessDesc::init(const GraphPathDesc &path_desc,
                                uint64_t edge_access_table_id,
                                ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  GraphExpandAccessDesc desc;
  const GraphSchema *graph = nullptr;
  const GraphElement *source = nullptr;
  const GraphElement *edge = nullptr;
  const GraphElement *target = nullptr;
  const ObTableSchema *source_table = nullptr;
  const ObTableSchema *edge_table = nullptr;
  const ObTableSchema *target_table = nullptr;
  const ObTableSchema *edge_access_table = nullptr;
  const uint64_t *edge_current_columns = nullptr;
  const uint64_t *edge_next_columns = nullptr;
  int64_t edge_current_count = 0;
  int64_t edge_next_count = 0;

  if (OB_UNLIKELY(!path_desc.is_valid()
                  || edge_access_table_id == OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph expand access arguments", K(ret), K(path_desc),
             K(edge_access_table_id));
  } else if (OB_FAIL(schema_guard.get_graph_schema(path_desc.graph_id_, graph))) {
    LOG_WARN("failed to get graph schema", K(ret), K(path_desc));
  } else if (OB_ISNULL(graph)) {
    ret = OB_SCHEMA_ERROR;
    LOG_WARN("graph schema is missing", K(ret), K(path_desc));
  } else if (OB_UNLIKELY(graph->get_schema_version()
                         != path_desc.graph_version_)) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("graph schema version changed", K(ret), K(path_desc),
             "current_version", graph->get_schema_version());
  } else if (OB_ISNULL(source = graph->get_element(path_desc.source_element_id_))
             || OB_ISNULL(edge = graph->get_element(path_desc.edge_element_id_))
             || OB_ISNULL(target = graph->get_element(path_desc.target_element_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph path element mapping is missing", K(ret), K(path_desc),
             KPC(graph));
  } else if (OB_UNLIKELY(!source->is_vertex() || edge->is_vertex()
                         || !target->is_vertex())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph path element kind is inconsistent", K(ret), K(path_desc),
             KPC(source), KPC(edge), KPC(target));
  } else {
    const bool reverse = path_desc.direction_ == GraphPathDirection::IN;
    const uint64_t current_element_id = reverse ? edge->destination_id_
                                                : edge->source_id_;
    const uint64_t next_element_id = reverse ? edge->source_id_
                                             : edge->destination_id_;
    edge_current_columns = reverse ? edge->destination_columns_
                                   : edge->source_columns_;
    edge_next_columns = reverse ? edge->source_columns_
                                : edge->destination_columns_;
    edge_current_count = reverse ? edge->destination_key_count_
                                 : edge->source_key_count_;
    edge_next_count = reverse ? edge->source_key_count_
                              : edge->destination_key_count_;
    if (OB_UNLIKELY(current_element_id != source->id_
                    || next_element_id != target->id_
                    || edge_current_count != source->key_count_
                    || edge_next_count != target->key_count_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge endpoints do not match the path", K(ret),
               K(path_desc), KPC(source), KPC(edge), KPC(target));
    }
  }
  if (OB_SUCC(ret)
      && (OB_FAIL(schema_guard.get_table_schema(source->table_id_, source_table))
          || OB_FAIL(schema_guard.get_table_schema(edge->table_id_, edge_table))
          || OB_FAIL(schema_guard.get_table_schema(target->table_id_, target_table))
          || OB_FAIL(schema_guard.get_table_schema(edge_access_table_id,
                                                   edge_access_table)))) {
    LOG_WARN("failed to get graph expand table schema", K(ret), K(path_desc),
             K(edge_access_table_id));
  } else if (OB_SUCC(ret)
             && (OB_ISNULL(source_table) || OB_ISNULL(edge_table)
                 || OB_ISNULL(target_table) || OB_ISNULL(edge_access_table))) {
    ret = OB_SCHEMA_ERROR;
    LOG_WARN("graph expand table schema is missing", K(ret), K(path_desc),
             K(edge_access_table_id), KPC(source_table), KPC(edge_table),
             KPC(target_table), KPC(edge_access_table));
  } else if (OB_SUCC(ret) && edge_access_table_id != edge->table_id_
             && OB_UNLIKELY((!edge_access_table->is_normal_index()
                             && !edge_access_table->is_unique_index())
                            || edge_access_table->is_global_index_table()
                            || edge_access_table->get_data_table_id()
                                   != edge->table_id_
                            || !edge_access_table->can_read_index()
                            || !edge_access_table->is_index_visible()
                            || edge_access_table->is_final_invalid_index()
                            || !index_has_adjacency_prefix(
                                   *edge_access_table, edge_current_columns,
                                   edge_current_count))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph adjacency access table", K(ret), K(path_desc),
             K(edge_access_table_id), KPC(edge_access_table));
  }
  if (OB_SUCC(ret)) {
    desc.source_table_id_ = source->table_id_;
    desc.source_table_version_ = source_table->get_schema_version();
    desc.edge_table_id_ = edge->table_id_;
    desc.edge_table_version_ = edge_table->get_schema_version();
    desc.target_table_id_ = target->table_id_;
    desc.target_table_version_ = target_table->get_schema_version();
    desc.edge_access_table_id_ = edge_access_table_id;
    desc.edge_access_table_version_ = edge_access_table->get_schema_version();
    desc.source_key_count_ = source->key_count_;
    desc.edge_key_count_ = edge->key_count_;
    desc.target_key_count_ = target->key_count_;
    copy_graph_columns(source->key_columns_, source->key_count_,
                       desc.source_key_columns_);
    copy_graph_columns(edge->key_columns_, edge->key_count_,
                       desc.edge_key_columns_);
    copy_graph_columns(target->key_columns_, target->key_count_,
                       desc.target_key_columns_);
    copy_graph_columns(edge_current_columns, edge_current_count,
                       desc.edge_current_columns_);
    copy_graph_columns(edge_next_columns, edge_next_count,
                       desc.edge_next_columns_);
    if (OB_UNLIKELY(!desc.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("generated graph expand access descriptor is invalid", K(ret),
               K(desc), K(path_desc));
    } else {
      *this = desc;
    }
  }
  return ret;
}

uint64_t GraphExpandAccessDesc::hash(uint64_t seed) const
{
  seed = do_hash(source_table_id_, seed);
  seed = do_hash(source_table_version_, seed);
  seed = do_hash(edge_table_id_, seed);
  seed = do_hash(edge_table_version_, seed);
  seed = do_hash(target_table_id_, seed);
  seed = do_hash(target_table_version_, seed);
  seed = do_hash(edge_access_table_id_, seed);
  seed = do_hash(edge_access_table_version_, seed);
  seed = do_hash(source_key_count_, seed);
  seed = do_hash(edge_key_count_, seed);
  seed = do_hash(target_key_count_, seed);
  for (int64_t i = 0; i < GRAPH_IDENTITY_MAX_KEYS; ++i) {
    seed = do_hash(source_key_columns_[i], seed);
    seed = do_hash(edge_key_columns_[i], seed);
    seed = do_hash(target_key_columns_[i], seed);
    seed = do_hash(edge_current_columns_[i], seed);
    seed = do_hash(edge_next_columns_[i], seed);
  }
  return seed;
}

static_assert(GRAPH_IDENTITY_MAX_KEYS == 2,
              "update GraphExpandAccessDesc serialization for a new key limit");

OB_SERIALIZE_MEMBER(GraphExpandAccessDesc,
                    source_table_id_,
                    source_table_version_,
                    edge_table_id_,
                    edge_table_version_,
                    target_table_id_,
                    target_table_version_,
                    edge_access_table_id_,
                    edge_access_table_version_,
                    source_key_count_,
                    edge_key_count_,
                    target_key_count_,
                    source_key_columns_[0],
                    source_key_columns_[1],
                    edge_key_columns_[0],
                    edge_key_columns_[1],
                    target_key_columns_[0],
                    target_key_columns_[1],
                    edge_current_columns_[0],
                    edge_current_columns_[1],
                    edge_next_columns_[0],
                    edge_next_columns_[1]);

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
  // reuse() keeps arena pages for the following batch, so report resident
  // capacity rather than only bytes populated by the current scanner call.
  const int64_t identity_bytes = identity_allocator_.total();
  return fixed_memory_bytes_ > INT64_MAX - identity_bytes
      ? INT64_MAX
      : fixed_memory_bytes_ + identity_bytes;
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
  } else if (inputs.count() > GRAPH_EXPAND_MAX_INPUT_STATE_COUNT) {
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
    const int64_t per_input = sizeof(GraphExpandInput) + sizeof(GraphElementIdentity)
        + GRAPH_EXPAND_INPUT_MEMORY_RESERVE_BYTES;
    const int64_t per_edge = sizeof(GraphExpandEdge) + 2 * sizeof(GraphElementIdentity)
        + GRAPH_EXPAND_EDGE_MEMORY_RESERVE_BYTES;
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
  access_.release();
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
