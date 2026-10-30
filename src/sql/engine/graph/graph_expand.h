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

#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "sql/engine/graph/graph_path_spec.h"
#include "sql/engine/graph/graph_expand_input_bindings.h"
#include "sql/engine/graph/graph_expand_edge_page.h"

namespace oceanbase
{
namespace sql
{

// Adjacency access limits one source partition independently of the caller's
// query-memory budget. Full scans receive a complete BFS level so the edge
// table is scanned only once; their size is governed by the work-area limit.
static const int64_t GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE = 4096;
static const int64_t GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE = 4096;

// Distinguishes the anchor-existence lookup from the target-existence lookup
// performed after an edge page. Access implementations may share machinery,
// but failure injection and diagnostics must retain the actual phase.
enum class GraphVertexLookupPhase : uint8_t
{
  SOURCE = 0,
  TARGET
};

// The DAS-facing adapter uses the statement's transaction, snapshot and schema
// guard. It must return an error for scan/RPC failures and
// must never encode a failed read as an empty successful page.
class IGraphExpandAccess
{
public:
  virtual ~IGraphExpandAccess() = default;

  // End the current access; initialization and complete cached entries may
  // survive. Implementations provide a separate clear() when needed.
  virtual void release() {}
  virtual int64_t used_memory() const { return 0; }
  virtual int check_status() = 0;
  // True when disjoint source batches can be scanned independently without
  // repeating an unfiltered access. Adjacency ranges have this property;
  // edge-table fallback scans require the complete BFS-level source set.
  virtual bool can_partition_sources() const = 0;

  // Returned identities borrow adapter storage. Copy them before another
  // access call or release(), which may overwrite that storage.
  virtual int lookup_vertices(
      const common::ObIArray<GraphElementIdentity> &requested,
      common::ObIArray<GraphElementIdentity> &existing,
      GraphVertexLookupPhase phase) = 0;

  // scanned_edges is the number of rows delivered by DAS during this call,
  // before graph SQL filters and full-scan frontier matching.
  // Returned identities/payloads have the same borrowed lifetime as lookups.
  // after_edge continues the current scan; it is not a random-seek interface.
  virtual int scan_edges(
      const common::ObIArray<GraphElementIdentity> &sources,
      GraphPathDirection direction,
      const GraphElementIdentity *after_edge,
      int64_t limit,
      common::ObIArray<GraphExpandEdge> &edges,
      int64_t &scanned_edges,
      bool &end) = 0;
};

// Stateful single-hop attach controller. It shares adjacency reads for equal
// source identities, always validates target existence, then restores every
// (binding_id,path_state_id) association without deduplicating output rows.
// It neither owns binding payload/path-state storage nor allocates their IDs:
// the frontier owner supplies input handles and creates a child state for each
// returned edge/target extension.
// Buffers are allocated from the caller's query/work-area allocator and never
// spill. Any read, cancellation, timeout or protocol error releases all state.
class GraphExpand final
{
public:
  GraphExpand(common::ObIAllocator &allocator,
              IGraphExpandAccess &access,
              int64_t page_size,
              int64_t memory_limit);
  ~GraphExpand() = default;

  int open(const common::ObIArray<GraphExpandInput> &inputs,
           GraphPathDirection direction);
  // Output borrows the current edge page. Retain a deep copy before the next
  // get_next_row(), rescan() or release() call.
  int get_next_row(GraphExpandOutput &output);
  int rescan(const common::ObIArray<GraphExpandInput> &inputs,
             GraphPathDirection direction);
  void release();

  const GraphExpandStats &get_stats() const { return stats_; }
  int64_t used_memory() const;
  bool is_open() const { return is_open_; }
  bool can_partition_sources() const
  { return access_.can_partition_sources(); }
  int check_status() { return access_.check_status(); }
  // Only the native frontier may assert this: roots came from the anchor
  // table, and every child passed target existence under the same snapshot.
  void set_sources_verified(bool verified) { sources_verified_ = verified; }

private:
  int load_next_page();
  int validate_and_account(const common::ObIArray<GraphExpandInput> &inputs);
  int lookup_source_vertices();
  int build_existing_source_index();
  bool contains(const common::ObIArray<GraphElementIdentity> &identities,
                const GraphElementIdentity &identity) const;
  int validate_lookup_result(
      const common::ObIArray<GraphElementIdentity> &requested,
      const common::ObIArray<GraphElementIdentity> &existing) const;
  int deep_copy_identity(const GraphElementIdentity &source,
                         GraphElementIdentity &destination);
  int check_memory_limit();
  int fail(int error);

private:
  IGraphExpandAccess &access_;
  bool sources_verified_{false};
  // Source-existence identities live for the input batch, independently of edge pages.
  common::ObArenaAllocator identity_allocator_;
  GraphExpandInputBindings input_bindings_;
  // Validates source lookup results and gives each returned edge constant-time
  // source-existence checks. DOUBLE batches retain linear comparison because
  // fixed-scale tolerant equality is not transitive.
  GraphIdentityIndex existing_source_index_;
  int64_t page_size_;
  int64_t memory_limit_;
  int64_t fixed_memory_bytes_{0};
  GraphPathDirection direction_{GraphPathDirection::OUT};
  // A full-scan frontier remains whole for the edge scan, but source vertex
  // existence checks are bounded so DAS never fans an unbounded range set to
  // every selected tablet.
  common::ObArray<GraphElementIdentity> source_lookup_batch_;
  common::ObArray<GraphElementIdentity> source_lookup_results_;
  common::ObArray<GraphElementIdentity> existing_sources_;
  GraphExpandEdgePage edge_page_;
  int64_t edge_index_{0};
  bool end_{false};
  bool is_open_{false};
  bool input_group_started_{false};
  GraphExpandStats stats_{};
};

} // namespace sql
} // namespace oceanbase
