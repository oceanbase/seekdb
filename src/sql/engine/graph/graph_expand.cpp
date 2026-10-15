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
#include "common/wide_integer/ob_wide_integer.h"
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

GraphIdentityIndex::GraphIdentityIndex(ObIAllocator &allocator)
    : bucket_heads_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                    ModulePageAllocator(allocator, "GraphIdIndex")),
      entries_(OB_MALLOC_NORMAL_BLOCK_SIZE,
               ModulePageAllocator(allocator, "GraphIdIndex"))
{
}

int GraphIdentityIndex::init(int64_t expected_count)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(expected_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph identity count", K(ret), K(expected_count));
  } else {
    const int64_t bucket_count = std::max<int64_t>(1, expected_count);
    if (OB_FAIL(bucket_heads_.prepare_allocate(bucket_count))) {
      LOG_WARN("failed to allocate graph identity buckets", K(ret),
               K(bucket_count));
    } else {
      for (int64_t i = 0; i < bucket_heads_.count(); ++i) {
        bucket_heads_.at(i) = -1;
      }
    }
  }
  return ret;
}

int GraphIdentityIndex::calc_compatible_hash(
    const GraphElementIdentity &identity,
    uint64_t &hash) const
{
  int ret = OB_SUCCESS;
  hash = do_hash(identity.graph_id_, 0);
  hash = do_hash(identity.element_id_, hash);
  for (int64_t i = 0;
       OB_SUCC(ret) && i < identity.rowkey_.get_obj_cnt(); ++i) {
    const ObObj &key = identity.rowkey_.get_obj_ptr()[i];
    if (key.is_decimal_int()) {
      // Graph endpoint compatibility follows ordinary key comparison and
      // permits DECIMAL_INT <-> NUMBER as well as different decimal scales.
      // ObObj's raw hash includes the concrete type and decimal scale, so
      // normalize this numeric domain before choosing a hash bucket. The
      // index still uses the full typed identity for collision equality.
      ObNumStackOnceAlloc number_allocator;
      number::ObNumber number;
      ObObj normalized;
      if (OB_FAIL(wide::to_number(key.get_decimal_int(), key.get_int_bytes(),
                                  key.get_scale(), number_allocator, number))) {
        LOG_WARN("failed to normalize graph decimal identity", K(ret), K(key));
      } else {
        normalized.set_number(ObNumberType, number);
        ret = normalized.hash(hash, hash);
      }
    } else if (key.get_type_class() == ObNumberTC) {
      // Signed and unsigned NUMBER values share comparison semantics for
      // non-negative values. Hash both with the same canonical type so a
      // legal NUMBER <-> DECIMAL_INT endpoint mapping reaches one bucket.
      ObObj normalized;
      normalized.set_number(ObNumberType, key.get_number());
      ret = normalized.hash(hash, hash);
    } else if (key.get_type_class() == ObDoubleTC) {
      // Fixed-scale DOUBLE comparison is tolerance based and can compare
      // equal across different declared scales. No value-dependent hash can
      // preserve that non-exact equality for every boundary value, so keep
      // DOUBLE keys in a type bucket and let typed equality resolve collisions.
      hash = do_hash(key.get_type(), hash);
    } else {
      ret = key.hash(hash, hash);
    }
  }
  return ret;
}

int GraphIdentityIndex::get_bucket(
    const GraphElementIdentity &identity,
    int64_t &bucket,
    int64_t binding_id) const
{
  int ret = OB_SUCCESS;
  uint64_t hash = 0;
  bucket = -1;
  if (bucket_heads_.empty()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(calc_compatible_hash(identity, hash))) {
    LOG_WARN("failed to hash graph identity", K(ret), K(identity));
  } else {
    bucket = static_cast<int64_t>(do_hash(binding_id, hash) % bucket_heads_.count());
  }
  return ret;
}

int GraphIdentityIndex::find(const GraphElementIdentity &identity,
                             int64_t &entry_index,
                             bool &found,
                             int64_t binding_id) const
{
  int ret = OB_SUCCESS;
  int64_t visited = 0;
  entry_index = -1;
  found = false;
  int64_t bucket = -1;
  if (OB_UNLIKELY(!identity.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph identity lookup", K(ret), K(identity));
  } else if (OB_FAIL(get_bucket(identity, bucket, binding_id))) {
    LOG_WARN("failed to locate graph identity bucket", K(ret), K(identity));
  } else {
    if (OB_UNLIKELY(bucket < 0 || bucket >= bucket_heads_.count())) {
      ret = OB_NOT_INIT;
      LOG_WARN("graph identity index is not initialized", K(ret), K(bucket));
    } else {
      int64_t current = bucket_heads_.at(bucket);
      while (OB_SUCC(ret) && !found && current >= 0) {
        if (OB_UNLIKELY(current >= entries_.count()
                        || visited++ >= entries_.count())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("corrupted graph identity hash chain", K(ret), K(current),
                   K(visited), "entry_count", entries_.count());
        } else if (entries_.at(current).binding_id_ == binding_id
                   && entries_.at(current).identity_ == identity) {
          entry_index = current;
          found = true;
        } else {
          current = entries_.at(current).next_entry_;
        }
      }
    }
  }
  return ret;
}

int GraphIdentityIndex::get_or_insert(
    const GraphElementIdentity &identity,
    int64_t &entry_index,
    bool &inserted,
    int64_t binding_id)
{
  int ret = OB_SUCCESS;
  bool found = false;
  inserted = false;
  if (OB_FAIL(find(identity, entry_index, found, binding_id))) {
  } else if (found) {
  } else if (OB_FAIL(grow_buckets())) {
  } else {
    int64_t bucket = -1;
    if (OB_FAIL(get_bucket(identity, bucket, binding_id))) {
      LOG_WARN("failed to locate graph identity bucket", K(ret), K(identity));
    } else {
      Entry entry;
      entry.identity_ = identity;
      entry.binding_id_ = binding_id;
      entry.next_entry_ = bucket_heads_.at(bucket);
      if (OB_FAIL(entries_.push_back(entry))) {
        LOG_WARN("failed to append graph identity entry", K(ret), K(identity));
      } else {
        entry_index = entries_.count() - 1;
        bucket_heads_.at(bucket) = entry_index;
        inserted = true;
      }
    }
  }
  return ret;
}

int GraphIdentityIndex::grow_buckets()
{
  int ret = OB_SUCCESS;
  // Reachable frontiers can grow far beyond their input level's size. Keep a
  // bounded load factor instead of degrading to linear chains at a high-degree
  // vertex. Entry positions (used by existing adjacency callers) stay stable.
  if (entries_.count() >= bucket_heads_.count()) {
    if (bucket_heads_.count() > INT64_MAX / 2) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_FAIL(bucket_heads_.prepare_allocate(bucket_heads_.count() * 2))) {
    } else {
      for (int64_t i = 0; i < bucket_heads_.count(); ++i) {
        bucket_heads_.at(i) = -1;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < entries_.count(); ++i) {
        Entry &entry = entries_.at(i);
        int64_t bucket = -1;
        if (OB_FAIL(get_bucket(entry.identity_, bucket, entry.binding_id_))) {
        } else {
          entry.next_entry_ = bucket_heads_.at(bucket);
          bucket_heads_.at(bucket) = i;
        }
      }
    }
  }
  return ret;
}

void GraphIdentityIndex::reset()
{
  bucket_heads_.reset();
  entries_.reset();
}

GraphExpand::GraphExpand(ObIAllocator &allocator,
                         IGraphExpandAccess &access,
                         int64_t page_size,
                         int64_t memory_limit)
  : access_(access),
    identity_allocator_(allocator),
    source_input_index_(allocator),
    existing_source_index_(allocator),
    page_size_(page_size),
    memory_limit_(memory_limit),
    fixed_memory_bytes_(0),
    direction_(GraphPathDirection::OUT),
    inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    input_key_slots_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                     ModulePageAllocator(allocator, "GraphExpand")),
    source_identities_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    source_input_groups_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphExpand")),
    next_input_indices_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                        ModulePageAllocator(allocator, "GraphExpand")),
    source_lookup_batch_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphExpand")),
    source_lookup_results_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                           ModulePageAllocator(allocator, "GraphExpand")),
    existing_sources_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    edge_page_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    target_identities_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    existing_targets_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
    edge_index_(0),
    input_index_(-1),
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
    if (OB_FAIL(source.rowkey_.deep_copy(destination.rowkey_,
                                         identity_allocator_))) {
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
    if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
    } else if (OB_FAIL(deep_copy_identity(inputs.at(i).source_identity_,
                                   stable.source_identity_))) {
    } else if (OB_FAIL(inputs_.push_back(stable))) {
    }
  }
  return ret;
}

int GraphExpand::validate_input_keys(
    const ObIArray<GraphExpandInput> &inputs)
{
  int ret = OB_SUCCESS;
  if (!inputs.empty()) {
    if (OB_UNLIKELY(inputs.count()
                    > (INT64_MAX - (OB_MALLOC_NORMAL_BLOCK_SIZE - 1))
                          / GRAPH_EXPAND_INPUT_KEY_BYTES_PER_INPUT)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      const int64_t slot_count =
          inputs.count() * GRAPH_EXPAND_INPUT_KEY_SLOT_MULTIPLIER;
      if (OB_FAIL(input_key_slots_.prepare_allocate(slot_count))) {
        LOG_WARN("failed to allocate graph input key index", K(ret),
                 K(slot_count));
      } else if (OB_FAIL(check_memory_limit())) {
        LOG_WARN("graph input key index exceeds memory limit", K(ret),
                 K(slot_count));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < slot_count; ++i) {
        if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
        } else {
          input_key_slots_.at(i) = -1;
        }
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < inputs.count(); ++i) {
        if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
          break;
        }
        const GraphExpandInput &input = inputs.at(i);
        uint64_t hash = do_hash(input.binding_id_, 0);
        hash = do_hash(input.path_state_id_, hash);
        int64_t slot = static_cast<int64_t>(hash % slot_count);
        int64_t probed = 0;
        while (OB_SUCC(ret) && input_key_slots_.at(slot) >= 0
               && probed < slot_count) {
          const GraphExpandInput &existing =
              inputs.at(input_key_slots_.at(slot));
          if (input.binding_id_ == existing.binding_id_
              && input.path_state_id_ == existing.path_state_id_) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("duplicate graph expand input key", K(ret), K(i),
                     "existing_index", input_key_slots_.at(slot), K(input));
          } else {
            slot = (slot + 1) % slot_count;
            ++probed;
          }
        }
        if (OB_SUCC(ret) && OB_UNLIKELY(probed >= slot_count)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("graph input key index is full", K(ret), K(i),
                   K(slot_count));
        } else if (OB_SUCC(ret)) {
          input_key_slots_.at(slot) = i;
        }
      }
    }
  }
  input_key_slots_.reset();
  return ret;
}

int GraphExpand::build_source_input_groups()
{
  int ret = OB_SUCCESS;
  source_input_groups_enabled_ = true;
  for (int64_t i = 0;
       OB_SUCC(ret) && source_input_groups_enabled_ && i < inputs_.count(); ++i) {
    if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
      break;
    }
    const ObRowkey &rowkey = inputs_.at(i).source_identity_.rowkey_;
    for (int64_t key = 0;
         source_input_groups_enabled_ && key < rowkey.get_obj_cnt(); ++key) {
      source_input_groups_enabled_ =
          rowkey.get_obj_ptr()[key].get_type_class() != ObDoubleTC;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!source_input_groups_enabled_) {
    // Fixed-scale DOUBLE rowkey equality is tolerance based and therefore not
    // transitive. One input can match more than one apparent source group, so
    // a single-result hash lookup is unsafe; retain the linear bag-semantic
    // matching path for the whole batch.
    for (int64_t i = 0; OB_SUCC(ret) && i < inputs_.count(); ++i) {
      if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
      } else if (OB_FAIL(push_unique(source_identities_,
                              inputs_.at(i).source_identity_))) {
        LOG_WARN("failed to append graph source identity", K(ret), K(i));
      }
    }
  } else {
    if (OB_FAIL(source_input_index_.init(inputs_.count()))) {
      LOG_WARN("failed to initialize graph source input index", K(ret),
               "input_count", inputs_.count());
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < inputs_.count(); ++i) {
      int64_t group_index = -1;
      bool inserted = false;
      if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(access_.check_status())) {
      } else if (OB_FAIL(source_input_index_.get_or_insert(
              inputs_.at(i).source_identity_, group_index, inserted))) {
        LOG_WARN("failed to index graph source input", K(ret), K(i));
      } else if (inserted
                 && OB_UNLIKELY(group_index != source_input_groups_.count()
                                || group_index != source_identities_.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph source input index is inconsistent", K(ret), K(i),
                 K(group_index), "group_count", source_input_groups_.count(),
                 "source_count", source_identities_.count());
      } else if (inserted
                 && OB_FAIL(source_identities_.push_back(
                     inputs_.at(i).source_identity_))) {
        LOG_WARN("failed to append graph source identity", K(ret), K(i));
      } else if (inserted
                 && OB_FAIL(source_input_groups_.push_back(SourceInputGroup()))) {
        LOG_WARN("failed to append graph source input group", K(ret), K(i));
      } else if (OB_UNLIKELY(group_index < 0
                             || group_index >= source_input_groups_.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid graph source input group", K(ret), K(i),
                 K(group_index), "group_count", source_input_groups_.count());
      } else if (OB_FAIL(next_input_indices_.push_back(-1))) {
        LOG_WARN("failed to append graph source input link", K(ret), K(i));
      } else {
        SourceInputGroup &group = source_input_groups_.at(group_index);
        if (group.last_input_ >= 0) {
          next_input_indices_.at(group.last_input_) = i;
        } else {
          group.first_input_ = i;
        }
        group.last_input_ = i;
      }
    }
    if (OB_SUCC(ret)
        && OB_UNLIKELY(source_input_index_.count()
                           != source_input_groups_.count()
                       || source_input_groups_.count()
                           != source_identities_.count()
                       || next_input_indices_.count() != inputs_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph source input groups are incomplete", K(ret),
               "index_count", source_input_index_.count(),
               "group_count", source_input_groups_.count(),
               "source_count", source_identities_.count(),
               "link_count", next_input_indices_.count(),
               "input_count", inputs_.count());
    }
  }
  return ret;
}

int GraphExpand::lookup_source_vertices()
{
  int ret = OB_SUCCESS;
  existing_sources_.reuse();
  for (int64_t start = 0;
       OB_SUCC(ret) && start < source_identities_.count();) {
    source_lookup_batch_.reuse();
    source_lookup_results_.reuse();
    const int64_t batch_count = std::min(
        GRAPH_EXPAND_VERTEX_LOOKUP_BATCH_SIZE,
        source_identities_.count() - start);
    const int64_t end = start + batch_count;
    for (int64_t i = start; OB_SUCC(ret) && i < end; ++i) {
      if (OB_FAIL(source_lookup_batch_.push_back(source_identities_.at(i)))) {
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

int GraphExpand::start_edge_input_group(const GraphExpandEdge &edge)
{
  int ret = OB_SUCCESS;
  int64_t group_index = -1;
  bool found = false;
  if (OB_UNLIKELY(input_group_started_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("graph edge input group is already started", K(ret),
             K_(edge_index), K_(input_index));
  } else if (OB_FAIL(source_input_index_.find(
                 edge.source_identity_, group_index, found))) {
    LOG_WARN("failed to find graph edge input group", K(ret), K(edge));
  } else if (OB_UNLIKELY(!found || group_index < 0
                         || group_index >= source_input_groups_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge has no source input group", K(ret), K(edge),
             K(group_index), K(found));
  } else {
    const SourceInputGroup &group = source_input_groups_.at(group_index);
    if (OB_UNLIKELY(group.first_input_ < 0
                    || group.first_input_ >= inputs_.count()
                    || group.last_input_ < group.first_input_
                    || group.last_input_ >= inputs_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid graph source input group", K(ret), K(group_index),
               K(group), "input_count", inputs_.count());
    } else {
      input_index_ = group.first_input_;
      input_group_started_ = true;
    }
  }
  return ret;
}

int GraphExpand::build_existing_source_index()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!source_input_groups_enabled_)) {
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
    } else if (OB_FAIL(source_input_index_.find(
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
    } else if (OB_FAIL(stable.payload_.deep_copy(edge_page_.at(i).payload_,
                                                identity_allocator_))) {
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
  // ObArray allocates block-rounded capacity and reuse() retains arena pages.
  // Compare the conservative preflight reservation with actual resident
  // capacity so small batches still account every allocated container block.
  const int64_t container_parts[] = {
      inputs_.get_data_size(),
      input_key_slots_.get_data_size(),
      source_identities_.get_data_size(),
      source_input_index_.used_memory(),
      existing_source_index_.used_memory(),
      source_input_groups_.get_data_size(),
      next_input_indices_.get_data_size(),
      source_lookup_batch_.get_data_size(),
      source_lookup_results_.get_data_size(),
      existing_sources_.get_data_size(),
      edge_page_.get_data_size(),
      target_identities_.get_data_size(),
      existing_targets_.get_data_size()};
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
        sizeof(SourceInputGroup) + sizeof(int64_t);
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
  if (OB_SUCC(ret) && OB_FAIL(validate_input_keys(inputs))) {
    LOG_WARN("invalid graph expand input keys", K(ret),
             "input_count", inputs.count());
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
  } else if (OB_FAIL(build_source_input_groups())) {
  } else if (access_.can_partition_sources()
             && source_identities_.count() > GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE) {
    // Bound physical ranges, not the number of associated path/seed bindings.
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(check_memory_limit())) {
  }
  if (OB_SUCC(ret)) {
    stats_.input_states_ = inputs.count();
    stats_.distinct_sources_ = source_identities_.count();
    if (source_identities_.empty()) {
      end_ = true;
    } else if (OB_FAIL(lookup_source_vertices())) {
    } else if (existing_sources_.empty()) {
      end_ = true;
    } else if (source_input_groups_enabled_
               && OB_FAIL(build_existing_source_index())) {
    } else if (!source_input_groups_enabled_
               && OB_FAIL(validate_lookup_result(
                   source_identities_, existing_sources_))) {
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
  target_identities_.reuse();
  existing_targets_.reuse();
  edge_index_ = 0;
  input_index_ = source_input_groups_enabled_ ? -1 : 0;
  input_group_started_ = false;
  if (OB_FAIL(access_.check_status())) {
  } else if (OB_FAIL(access_.scan_edges(existing_sources_, direction_,
                                        has_after_edge_ ? &after_edge_ : nullptr,
                                        page_size_, edge_page_, scanned_edges,
                                        page_end))) {
  } else if (OB_FAIL(access_.check_status())) {
  } else if (edge_page_.count() > page_size_
             || (edge_page_.empty() && !page_end)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(stabilize_edge_page())) {
  } else if (OB_FAIL(check_memory_limit())) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_page_.count(); ++i) {
    const GraphExpandEdge &edge = edge_page_.at(i);
    bool source_exists = false;
    if (!edge.source_identity_.is_valid() || !edge.edge_identity_.is_valid()
        || !edge.target_identity_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (source_input_groups_enabled_) {
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
            || (has_after_edge_ && edge.edge_identity_ == after_edge_))) {
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
  if (OB_SUCC(ret)) {
    stats_.scanned_edges_ = saturated_nonnegative_add(
        stats_.scanned_edges_, scanned_edges);
  }
  if (OB_SUCC(ret) && !edge_page_.empty()) {
    stats_.looked_up_vertices_ += target_identities_.count();
    if (OB_FAIL(access_.lookup_vertices(
            target_identities_, existing_targets_,
            GraphVertexLookupPhase::TARGET))) {
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
        if (source_input_groups_enabled_) {
          if (!input_group_started_
              && OB_FAIL(start_edge_input_group(edge))) {
          }
          while (OB_SUCC(ret) && input_index_ >= 0) {
            if (OB_UNLIKELY(input_index_ >= inputs_.count()
                            || input_index_ >= next_input_indices_.count())) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid graph source input link", K(ret),
                       K_(input_index), "input_count", inputs_.count(),
                       "link_count", next_input_indices_.count());
            } else if (OB_FAIL(access_.check_status())) {
            } else {
              const int64_t current_input = input_index_;
              const GraphExpandInput &input = inputs_.at(current_input);
              if (OB_UNLIKELY(!(input.source_identity_
                                == edge.source_identity_))) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("graph source input group is corrupted", K(ret),
                         K(current_input), K(input), K(edge));
              } else {
                input_index_ = next_input_indices_.at(current_input);
                output.binding_id_ = input.binding_id_;
                output.path_state_id_ = input.path_state_id_;
                output.edge_identity_ = edge.edge_identity_;
                output.target_identity_ = edge.target_identity_;
                output.edge_payload_ = edge.payload_;
                ++stats_.output_states_;
                return OB_SUCCESS;
              }
            }
          }
        } else {
          while (OB_SUCC(ret) && input_index_ < inputs_.count()) {
            if (OB_FAIL(access_.check_status())) {
            } else {
              const GraphExpandInput &input = inputs_.at(input_index_++);
              if (input.source_identity_ == edge.source_identity_) {
                output.binding_id_ = input.binding_id_;
                output.path_state_id_ = input.path_state_id_;
                output.edge_identity_ = edge.edge_identity_;
                output.target_identity_ = edge.target_identity_;
                output.edge_payload_ = edge.payload_;
                ++stats_.output_states_;
                return OB_SUCCESS;
              }
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        ++edge_index_;
        input_index_ = source_input_groups_enabled_ ? -1 : 0;
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
  inputs_.reset();
  input_key_slots_.reset();
  source_input_index_.reset();
  existing_source_index_.reset();
  source_identities_.reset();
  source_input_groups_.reset();
  next_input_indices_.reset();
  source_lookup_batch_.reset();
  source_lookup_results_.reset();
  existing_sources_.reset();
  edge_page_.reset();
  target_identities_.reset();
  existing_targets_.reset();
  after_edge_.reset();
  identity_allocator_.reuse();
  edge_index_ = 0;
  input_index_ = -1;
  has_after_edge_ = false;
  end_ = false;
  is_open_ = false;
  source_input_groups_enabled_ = true;
  input_group_started_ = false;
  fixed_memory_bytes_ = 0;
}

} // namespace sql
} // namespace oceanbase
