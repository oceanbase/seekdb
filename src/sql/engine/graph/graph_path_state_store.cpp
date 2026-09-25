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
#include "sql/engine/graph/graph_path_state_store.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"

#include <utility>

namespace oceanbase
{
using namespace common;
namespace sql
{

GraphPathStateStore::GraphPathStateStore(ObIAllocator &allocator,
                                         int64_t memory_limit)
    : identity_allocator_(allocator),
      memory_limit_(memory_limit),
      states_(OB_MALLOC_NORMAL_BLOCK_SIZE,
              ModulePageAllocator(allocator, "GraphPathState"))
{
}

int GraphPathStateStore::deep_copy_identity(
    const GraphElementIdentity &source,
    GraphElementIdentity &destination)
{
  int ret = OB_SUCCESS;
  destination.reset();
  if (OB_UNLIKELY(!source.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph element identity", K(ret), K(source));
  } else {
    destination.graph_id_ = source.graph_id_;
    destination.element_id_ = source.element_id_;
    if (OB_FAIL(source.rowkey_.deep_copy(destination.rowkey_,
                                         identity_allocator_))) {
      LOG_WARN("failed to copy graph identity rowkey", K(ret), K(source));
    }
  }
  return ret;
}

bool GraphPathStateStore::is_valid_state_id(int64_t path_state_id) const
{
  return path_state_id >= 0 && path_state_id < states_.count();
}

int64_t GraphPathStateStore::used_memory() const
{
  // Count allocator capacity rather than only populated bytes: those pages are
  // already charged to the query allocator and cannot be used elsewhere.
  return states_.get_data_size() + identity_allocator_.total();
}

int GraphPathStateStore::check_memory_limit()
{
  int ret = OB_SUCCESS;
  const int64_t used = used_memory();
  if (OB_UNLIKELY(memory_limit_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph path-state memory limit", K(ret), K_(memory_limit));
  } else if (OB_UNLIKELY(used > memory_limit_)) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
    LOG_WARN("graph path-state store exceeded query memory limit",
             K(ret), K(used), K_(memory_limit));
  } else {
    peak_memory_bytes_ = std::max(peak_memory_bytes_, used);
  }
  return ret;
}

int GraphPathStateStore::append_state(GraphPathState &state,
                                      int64_t &path_state_id)
{
  int ret = OB_SUCCESS;
  path_state_id = GRAPH_INVALID_PATH_STATE_ID;
  if (OB_UNLIKELY(states_.count() == INT64_MAX)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too many graph path states", K(ret), "count", states_.count());
  } else {
    state.path_state_id_ = states_.count();
    if (OB_UNLIKELY(!state.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid graph path state", K(ret), K(state));
    } else if (OB_FAIL(states_.push_back(state))) {
      LOG_WARN("failed to save graph path state", K(ret), K(state));
    } else if (OB_FAIL(check_memory_limit())) {
    } else {
      path_state_id = state.path_state_id_;
    }
  }
  return ret;
}

int GraphPathStateStore::add_root(int64_t binding_id,
                                  const GraphElementIdentity &source_identity,
                                  int64_t &path_state_id)
{
  int ret = OB_SUCCESS;
  GraphPathState state;
  path_state_id = GRAPH_INVALID_PATH_STATE_ID;
  if (OB_UNLIKELY(binding_id < 0 || memory_limit_ <= 0
                  || !source_identity.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph path root", K(ret), K(binding_id),
             K_(memory_limit), K(source_identity));
  } else {
    state.binding_id_ = binding_id;
    state.parent_path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
    state.hop_ = 0;
    if (OB_FAIL(deep_copy_identity(source_identity, state.current_identity_))) {
    } else if (OB_FAIL(append_state(state, path_state_id))) {
    }
  }
  if (OB_SUCCESS != ret) {
    ret = fail(ret);
  }
  return ret;
}

int GraphPathStateStore::add_child(const GraphExpandOutput &extension,
                                   int64_t &path_state_id)
{
  int ret = OB_SUCCESS;
  GraphPathState parent;
  GraphPathState child;
  path_state_id = GRAPH_INVALID_PATH_STATE_ID;
  if (OB_UNLIKELY(extension.binding_id_ < 0
                  || !extension.edge_identity_.is_valid()
                  || !extension.target_identity_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph path extension", K(ret), K(extension));
  } else if (OB_FAIL(get_state(extension.path_state_id_, parent))) {
    LOG_WARN("failed to find graph path parent", K(ret), K(extension));
  } else if (OB_UNLIKELY(extension.binding_id_ != parent.binding_id_
                         || parent.hop_ >= GRAPH_PATH_MAX_HOPS
                         || parent.current_identity_.graph_id_
                                != extension.edge_identity_.graph_id_
                         || parent.current_identity_.graph_id_
                                != extension.target_identity_.graph_id_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("graph path extension does not match its parent",
             K(ret), K(parent), K(extension));
  } else {
    child.binding_id_ = parent.binding_id_;
    child.parent_path_state_id_ = parent.path_state_id_;
    child.hop_ = parent.hop_ + 1;
    if (OB_FAIL(deep_copy_identity(extension.target_identity_,
                                   child.current_identity_))) {
    } else if (OB_FAIL(deep_copy_identity(extension.edge_identity_,
                                          child.incoming_edge_identity_))) {
    } else if (OB_FAIL(append_state(child, path_state_id))) {
    }
  }
  if (OB_SUCCESS != ret) {
    ret = fail(ret);
  }
  return ret;
}

int GraphPathStateStore::get_state(int64_t path_state_id,
                                   GraphPathState &state) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_state_id(path_state_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph path state id", K(ret), K(path_state_id),
             "state_count", states_.count());
  } else if (OB_UNLIKELY(!states_.at(path_state_id).is_valid()
                         || states_.at(path_state_id).path_state_id_
                                != path_state_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("corrupted graph path state", K(ret), K(path_state_id),
             "state", states_.at(path_state_id));
  } else {
    state = states_.at(path_state_id);
  }
  return ret;
}

int GraphPathStateStore::make_expand_input(int64_t path_state_id,
                                           GraphExpandInput &input) const
{
  int ret = OB_SUCCESS;
  GraphPathState state;
  input = GraphExpandInput();
  if (OB_FAIL(get_state(path_state_id, state))) {
  } else {
    input.binding_id_ = state.binding_id_;
    input.path_state_id_ = state.path_state_id_;
    input.source_identity_ = state.current_identity_;
  }
  return ret;
}

int GraphPathStateStore::build_path(
    int64_t leaf_path_state_id,
    ObIArray<GraphPathState> &path) const
{
  int ret = OB_SUCCESS;
  GraphPathState state;
  int64_t leaf_hop = -1;
  int64_t binding_id = -1;
  path.reuse();
  if (OB_FAIL(get_state(leaf_path_state_id, state))) {
    LOG_WARN("failed to read graph path leaf", K(ret),
             K(leaf_path_state_id));
  } else {
    leaf_hop = state.hop_;
    binding_id = state.binding_id_;
  }
  for (int64_t visited = 0; OB_SUCC(ret); ++visited) {
    if (OB_UNLIKELY(visited > GRAPH_PATH_MAX_HOPS
                    || state.hop_ != leaf_hop - visited
                    || state.binding_id_ != binding_id)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inconsistent graph path ancestry", K(ret), K(state),
               K(leaf_hop), K(binding_id), K(visited));
    } else if (OB_FAIL(path.push_back(state))) {
      LOG_WARN("failed to append graph path state", K(ret), K(state));
    } else if (state.hop_ == 0) {
      if (OB_UNLIKELY(state.parent_path_state_id_
                      != GRAPH_INVALID_PATH_STATE_ID)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph path root has a parent", K(ret), K(state));
      }
      break;
    } else {
      GraphPathState parent;
      if (OB_UNLIKELY(!is_valid_state_id(state.parent_path_state_id_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph path parent id is invalid", K(ret), K(state));
      } else if (OB_FAIL(get_state(state.parent_path_state_id_, parent))) {
        LOG_WARN("failed to read graph path parent", K(ret), K(state));
      } else if (OB_UNLIKELY(parent.binding_id_ != state.binding_id_
                             || parent.hop_ + 1 != state.hop_
                             || parent.path_state_id_
                                    != state.parent_path_state_id_
                             || parent.path_state_id_
                                    >= state.path_state_id_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inconsistent graph path parent", K(ret), K(state),
                 K(parent));
      } else {
        state = parent;
      }
    }
  }
  if (OB_SUCC(ret)
      && OB_UNLIKELY(path.count() != leaf_hop + 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph path length does not match leaf hop", K(ret),
             K(leaf_hop), "path_count", path.count());
  }
  for (int64_t left = 0, right = path.count() - 1;
       OB_SUCC(ret) && left < right;
       ++left, --right) {
    std::swap(path.at(left), path.at(right));
  }
  if (OB_SUCCESS != ret) {
    path.reuse();
  }
  return ret;
}

int GraphPathStateStore::contains_edge(
    int64_t path_state_id,
    const GraphElementIdentity &edge_identity,
    bool &contains) const
{
  int ret = OB_SUCCESS;
  GraphPathState state;
  contains = false;
  if (OB_UNLIKELY(!edge_identity.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph edge identity", K(ret), K(edge_identity));
  } else if (OB_FAIL(get_state(path_state_id, state))) {
  } else if (OB_UNLIKELY(edge_identity.graph_id_
                         != state.current_identity_.graph_id_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("graph edge identity belongs to a different graph",
             K(ret), K(state), K(edge_identity));
  }
  for (int64_t visited = 0;
       OB_SUCC(ret) && !contains && state.hop_ > 0;
       ++visited) {
    if (OB_UNLIKELY(visited >= GRAPH_PATH_MAX_HOPS
                    || !state.incoming_edge_identity_.is_valid()
                    || !is_valid_state_id(state.parent_path_state_id_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid graph path ancestry", K(ret), K(state), K(visited));
    } else if (state.incoming_edge_identity_ == edge_identity) {
      contains = true;
    } else {
      GraphPathState parent = states_.at(state.parent_path_state_id_);
      if (OB_UNLIKELY(parent.binding_id_ != state.binding_id_
                      || parent.hop_ + 1 != state.hop_
                      || parent.path_state_id_ >= state.path_state_id_
                      || parent.path_state_id_ != state.parent_path_state_id_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inconsistent graph path parent", K(ret), K(state), K(parent));
      } else {
        state = parent;
      }
    }
  }
  if (OB_SUCC(ret) && !contains
      && OB_UNLIKELY(state.hop_ != 0
                     || state.parent_path_state_id_
                            != GRAPH_INVALID_PATH_STATE_ID)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph path ancestry did not terminate at a root", K(ret), K(state));
  }
  return ret;
}

int GraphPathStateStore::fail(int error)
{
  reset();
  return error;
}

void GraphPathStateStore::reset()
{
  states_.reset();
  identity_allocator_.reset();
  peak_memory_bytes_ = 0;
}

} // namespace sql
} // namespace oceanbase
