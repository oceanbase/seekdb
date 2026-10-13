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
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
namespace sql
{

// An optional allocation sub-budget, backed by the same query work area as
// paths and bindings. Exhausting this budget disables reuse, never truncates
// graph output; failure of the underlying query allocator remains an error.
class GraphCacheAllocator final : public common::ObIAllocator
{
public:
  GraphCacheAllocator(common::ObIAllocator &parent, int64_t limit)
      : tracked_(parent, &tracker_), limit_(limit) {}
  void *alloc(int64_t size) override;
  void *alloc(int64_t size, const lib::ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override;
  int64_t total() const override;
  int64_t used() const override { return total(); }
  bool budget_exceeded() const { return budget_exceeded_; }
  void clear_budget_error() { budget_exceeded_ = false; }
private:
  common::MemoryUsageTracker tracker_{};
  common::TrackedAllocator tracked_;
  int64_t limit_{0};
  int64_t allocations_{0};
  bool budget_exceeded_{false};
};

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
  struct Context
  {
    common::ObString key_{};
    uint64_t hash_{0};
    int64_t next_{-1};
    TO_STRING_KV(K_(hash), K_(next));
  };
  struct Entry
  {
    int64_t first_edge_{-1};
    int64_t last_edge_{-1};
    bool complete_{false};
    TO_STRING_KV(K_(first_edge), K_(last_edge), K_(complete));
  };
  struct Edge
  {
    GraphExpandEdge value_{};
    int64_t next_{-1};
    TO_STRING_KV(K_(value), K_(next));
  };
  int begin(const common::ObIArray<GraphElementIdentity> &sources);
  int begin_recording();
  int record(const common::ObIArray<GraphExpandEdge> &edges, bool end);
  int recover_cache_budget(int ret);
  int grow_context_buckets();
  void reset_cache();
  int copy_identity(const GraphElementIdentity &source, GraphElementIdentity &dest);

  IGraphExpandAccess &child_;
  GraphCacheAllocator cache_allocator_;
  common::ObArenaAllocator identities_;
  GraphIdentityIndex index_;
  common::ObArray<Entry> entries_;
  common::ObArray<Edge> cached_edges_;
  common::ObArray<Context> contexts_;
  common::ObArray<int64_t> context_buckets_;
  // Request-local arrays are not backed by the optional cache sub-budget.
  common::ObArray<int64_t> hits_;
  common::ObArray<GraphElementIdentity> misses_;
  common::ObArenaAllocator cursor_allocator_;
  GraphElementIdentity child_cursor_{};
  int64_t hit_position_{0};
  int64_t hit_edge_{-1};
  int64_t first_new_entry_{-1};
  // Zero denotes parameter-independent reads; positive IDs are private cache
  // namespaces, NOT seed binding IDs or graph element IDs. -1 bypasses reuse.
  int64_t context_id_{0};
  bool parameterized_{false};
  bool enabled_{false};
  bool bypass_{true};
  bool recording_{false};
  bool child_has_cursor_{false};
};

} // namespace sql
} // namespace oceanbase
