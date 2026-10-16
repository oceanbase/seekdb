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
#include "lib/container/ob_array.h"
#include "sql/engine/graph/graph_path_spec.h"

namespace oceanbase
{
namespace sql
{

// Query-local owner of parent-linked graph path prefixes. State IDs are array
// positions allocated monotonically by this store; they are not persistent
// graph IDs and remain valid only until reset(). Variable-length identity keys
// are copied into the store arena so neither frontier turnover nor DAS page
// reuse can invalidate a saved path.
class GraphPathStateStore final
{
public:
  GraphPathStateStore(common::ObIAllocator &allocator, int64_t memory_limit);
  ~GraphPathStateStore() = default;

  // Creates the zero-hop state for one seed/outer binding. Duplicate seeds are
  // deliberately assigned different states to preserve bag multiplicity.
  int add_root(int64_t binding_id,
               const GraphElementIdentity &source_identity,
               int64_t &path_state_id);

  // GraphExpandOutput::path_state_id_ names the parent prefix. This method
  // validates that association, inherits its binding and creates the child.
  int add_child(const GraphExpandOutput &extension, int64_t &path_state_id);

  // Returned identities are shallow copies backed by this store's arena and
  // therefore remain valid until reset(). The copy itself is stable across a
  // later ObArray growth, unlike a pointer to an element inside states_.
  int get_state(int64_t path_state_id, GraphPathState &state) const;
  int make_expand_input(int64_t path_state_id, GraphExpandInput &input) const;

  // Reconstructs one root-to-leaf path. Returned identities are shallow views
  // backed by this store and remain valid until reset().
  int build_path(int64_t leaf_path_state_id,
                 common::ObIArray<GraphPathState> &path) const;

  // Checks only the ancestry of one path. Equal edges used by different paths
  // do not conflict, which preserves TRAIL path multiplicity.
  int contains_edge(int64_t path_state_id,
                    const GraphElementIdentity &edge_identity,
                    bool &contains) const;

  void reset();
  int64_t count() const { return states_.count(); }
  bool empty() const { return states_.empty(); }
  int64_t used_memory() const;
  int64_t peak_memory() const { return peak_memory_bytes_; }

private:
  int deep_copy_identity(const GraphElementIdentity &source,
                         GraphElementIdentity &destination);
  int append_state(GraphPathState &state, int64_t &path_state_id);
  int check_memory_limit();
  bool is_valid_state_id(int64_t path_state_id) const;
  int fail(int error);

private:
  common::ObArenaAllocator identity_allocator_;
  int64_t memory_limit_;
  common::ObArray<GraphPathState> states_;
  int64_t peak_memory_bytes_{0};

  DISALLOW_COPY_AND_ASSIGN(GraphPathStateStore);
};

} // namespace sql
} // namespace oceanbase
