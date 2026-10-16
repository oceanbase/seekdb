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

// Owns adjacency data and parameter namespaces, not an access cursor. Only
// complete successful reads are published. The owner binds this cache to one
// operator's mapping, direction and statement snapshot; it must never share it
// across executions or change those invariants while retaining entries.
class GraphAdjacencyCache final
{
public:
  GraphAdjacencyCache(common::ObIAllocator &allocator, int64_t memory_limit);
  ~GraphAdjacencyCache() { clear(); }
  int select_context(const common::ObIArray<common::ObObj> &parameters);
  bool can_reuse() const { return context_id_ >= 0; }
  int64_t context_id() const { return context_id_; }
  int find(const GraphElementIdentity &source, int64_t &entry, bool &found) const;
  int64_t first_edge(int64_t entry) const { return entries_.at(entry).first_edge_; }
  // Returned identities/payload borrow cache memory until clear(), abort, or
  // budget recovery. The caller must consume cached pages before recording.
  int read_edge(int64_t &position, GraphExpandEdge &edge) const;
  int record(const common::ObIArray<GraphElementIdentity> &sources,
             const common::ObIArray<GraphExpandEdge> &edges, bool end);
  // Preserve completed entries, but discard an abandoned recording. This
  // deliberately retains the existing all-cache invalidation on partial reads.
  void release_recording();
  void clear();
  int64_t used_memory() const { return cache_allocator_.total(); }

private:
  enum class RecordingState { NOT_STARTED, RECORDING, COMPLETE, DISABLED };
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
  int begin_recording(const common::ObIArray<GraphElementIdentity> &sources);
  int recover_cache_budget(int ret);
  int grow_context_buckets();
  void reset_cache();
  int copy_identity(const GraphElementIdentity &source, GraphElementIdentity &dest);

  GraphCacheAllocator cache_allocator_;
  common::ObArenaAllocator identities_;
  GraphIdentityIndex index_;
  common::ObArray<Entry> entries_;
  common::ObArray<Edge> cached_edges_;
  common::ObArray<Context> contexts_;
  common::ObArray<int64_t> context_buckets_;
  int64_t first_new_entry_{-1};
  // 0: parameter-independent; positive: private parameter namespace; -1: bypass.
  // These IDs are neither seed bindings nor graph element IDs.
  int64_t context_id_{0};
  bool parameterized_{false};
  RecordingState recording_state_{RecordingState::NOT_STARTED};
  DISALLOW_COPY_AND_ASSIGN(GraphAdjacencyCache);
};

} // namespace sql
} // namespace oceanbase
