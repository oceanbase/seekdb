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
#include "sql/engine/graph/graph_expand_input_bindings.h"
#include "sql/engine/graph/graph_expand.h"
#include "share/ob_errno.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

static constexpr int64_t GRAPH_BINDING_KEY_SLOT_MULTIPLIER = 2;
static constexpr int64_t GRAPH_BINDING_KEY_BYTES_PER_INPUT =
    GRAPH_BINDING_KEY_SLOT_MULTIPLIER * sizeof(int64_t);

GraphExpandInputBindings::GraphExpandInputBindings(
    ObIAllocator &allocator, IGraphExpandAccess &access, int64_t memory_limit)
    : access_(access), memory_limit_(memory_limit), identity_allocator_(allocator),
      source_input_index_(allocator),
      inputs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
      input_key_slots_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
      source_identities_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
      source_input_groups_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
      next_input_indices_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand"))
{}

int GraphExpandInputBindings::init(const ObIArray<GraphExpandInput> &inputs)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(validate_input_keys(inputs))) {
  } else if (OB_FAIL(deep_copy_inputs(inputs))) {
  } else if (OB_FAIL(build_source_input_groups())) {
  } else if (OB_FAIL(check_memory_limit())) {
  }
  if (OB_FAIL(ret)) { reset(); }
  return ret;
}

int GraphExpandInputBindings::begin_edge(const GraphElementIdentity &source)
{
  input_group_started_ = false;
  input_index_ = source_input_groups_enabled_ ? -1 : 0;
  return source_input_groups_enabled_ ? start_edge_input_group(source) : OB_SUCCESS;
}

int GraphExpandInputBindings::next_input(
    const GraphElementIdentity &source, const GraphExpandInput *&input)
{
  int ret = OB_SUCCESS;
  input = nullptr;
  while (OB_SUCC(ret) && input == nullptr && input_index_ >= 0) {
    if (input_index_ >= inputs_.count()) {
      if (source_input_groups_enabled_) { ret = OB_ERR_UNEXPECTED; }
      else { input_index_ = -1; }
    } else if (OB_FAIL(access_.check_status())) {
    } else {
      const int64_t current = input_index_;
      if (source_input_groups_enabled_) {
        if (OB_UNLIKELY(current >= next_input_indices_.count()
                        || !(inputs_.at(current).source_identity_ == source))) {
          ret = OB_ERR_UNEXPECTED;
        } else { input_index_ = next_input_indices_.at(current); }
      } else { ++input_index_; }
      if (OB_SUCC(ret) && inputs_.at(current).source_identity_ == source) {
        input = &inputs_.at(current);
      }
    }
  }
  return ret;
}

int64_t GraphExpandInputBindings::used_memory() const
{
  const int64_t parts[] = {identity_allocator_.total(), source_input_index_.used_memory(),
      inputs_.get_data_size(), input_key_slots_.get_data_size(), source_identities_.get_data_size(),
      source_input_groups_.get_data_size(), next_input_indices_.get_data_size()};
  int64_t total = 0;
  for (int64_t part : parts) {
    if (part < 0 || total > INT64_MAX - part) { return INT64_MAX; }
    total += part;
  }
  return total;
}

int GraphExpandInputBindings::check_memory_limit() const
{
  const int64_t used = used_memory();
  int ret = used > memory_limit_ ? OB_EXCEED_QUERY_MEM_LIMIT : OB_SUCCESS;
  if (OB_FAIL(ret)) {
    LOG_WARN("graph expand input bindings exceeded work-area limit",
             K(ret), K(used), K_(memory_limit));
  }
  return ret;
}

void GraphExpandInputBindings::reset()
{
  inputs_.reset();
  input_key_slots_.reset();
  source_input_index_.reset();
  source_identities_.reset();
  source_input_groups_.reset();
  next_input_indices_.reset();
  identity_allocator_.reuse();
  input_index_ = -1;
  source_input_groups_enabled_ = true;
  input_group_started_ = false;
}

int GraphExpandInputBindings::deep_copy_identity(const GraphElementIdentity &source,
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

int GraphExpandInputBindings::deep_copy_inputs(const ObIArray<GraphExpandInput> &inputs)
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

int GraphExpandInputBindings::validate_input_keys(
    const ObIArray<GraphExpandInput> &inputs)
{
  int ret = OB_SUCCESS;
  if (!inputs.empty()) {
    if (OB_UNLIKELY(inputs.count()
                    > (INT64_MAX - (OB_MALLOC_NORMAL_BLOCK_SIZE - 1))
                          / GRAPH_BINDING_KEY_BYTES_PER_INPUT)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      const int64_t slot_count =
          inputs.count() * GRAPH_BINDING_KEY_SLOT_MULTIPLIER;
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

int GraphExpandInputBindings::build_source_input_groups()
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

int GraphExpandInputBindings::start_edge_input_group(const GraphElementIdentity &source)
{
  int ret = OB_SUCCESS;
  int64_t group_index = -1;
  bool found = false;
  if (OB_UNLIKELY(input_group_started_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("graph edge input group is already started", K(ret),
             K_(input_index));
  } else if (OB_FAIL(source_input_index_.find(
                 source, group_index, found))) {
    LOG_WARN("failed to find graph edge input group", K(ret), K(source));
  } else if (OB_UNLIKELY(!found || group_index < 0
                         || group_index >= source_input_groups_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge has no source input group", K(ret), K(source),
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

bool GraphExpandInputBindings::contains(const ObIArray<GraphElementIdentity> &identities,
                           const GraphElementIdentity &identity) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < identities.count(); ++i) {
    found = identities.at(i) == identity;
  }
  return found;
}

int GraphExpandInputBindings::push_unique(ObIArray<GraphElementIdentity> &identities,
                             const GraphElementIdentity &identity)
{
  int ret = OB_SUCCESS;
  if (!contains(identities, identity)) {
    ret = identities.push_back(identity);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
