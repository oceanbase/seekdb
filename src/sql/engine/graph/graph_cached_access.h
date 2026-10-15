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

#include "sql/engine/graph/graph_expand.h"
#include "sql/engine/graph/graph_adjacency_cache.h"

namespace oceanbase
{
namespace sql
{

// Statement/operator-local adjacency reuse. Only complete successful reads
// become entries. The owner must include all outer filter parameters in the
// selected context, and clear this cache on close or execution failure.
// Vertex existence is always checked by the underlying adapter. Bindings and
// paths never enter the cache, so parallel edges and seed copies retain bags.
class GraphCachedAccess final : public IGraphExpandAccess
{
public:
  GraphCachedAccess(common::ObIAllocator &allocator, IGraphExpandAccess &child,
                    int64_t memory_limit);
  ~GraphCachedAccess() override { clear(); }
  void set_enabled(bool enabled) { enabled_ = enabled; }
  // Select the complete, typed outer-parameter tuple for this rescan. Contexts
  // belong to this statement only; a hash match still compares all bytes.
  int select_context(const common::ObIArray<common::ObObj> &parameters);
  void clear();
  void release() override;
  int64_t used_memory() const override;
  int check_status() override { return child_.check_status(); }
  bool can_partition_sources() const override { return child_.can_partition_sources(); }
  int lookup_vertices(const common::ObIArray<GraphElementIdentity> &requested,
                      common::ObIArray<GraphElementIdentity> &existing,
                      GraphVertexLookupPhase phase) override
  { return child_.lookup_vertices(requested, existing, phase); }
  int scan_edges(const common::ObIArray<GraphElementIdentity> &sources,
                 GraphPathDirection direction, const GraphElementIdentity *after_edge,
                 int64_t limit, common::ObIArray<GraphExpandEdge> &edges,
                 int64_t &scanned_edges, bool &end) override;
private:
  int begin(const common::ObIArray<GraphElementIdentity> &sources);

  IGraphExpandAccess &child_;
  GraphAdjacencyCache cache_;
  // Request-local arrays are not backed by the optional cache sub-budget.
  common::ObArray<int64_t> hits_;
  common::ObArray<GraphElementIdentity> misses_;
  common::ObArenaAllocator cursor_allocator_;
  GraphElementIdentity child_cursor_{};
  int64_t hit_position_{0};
  int64_t hit_edge_{-1};
  bool enabled_{false};
  bool bypass_{true};
  bool child_has_cursor_{false};
};

} // namespace sql
} // namespace oceanbase
