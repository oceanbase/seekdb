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

namespace oceanbase
{
namespace sql
{

// Adjacency access limits one source partition independently of the caller's
// query-memory budget. Full scans receive a complete BFS level so the edge
// table is scanned only once; their size is governed by the work-area limit.
static const int64_t GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE = 4096;
static const int64_t GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE = 4096;

// Query-local hash index over typed graph identities. Compatible NUMBER and
// DECIMAL_INT keys are normalized, while tolerance-compared DOUBLE keys use a
// type bucket, so bucket selection follows graph endpoint equality. Identities
// are shallow views: the caller must keep their rowkey backing alive until
// reset(). An optional binding ID separates correlation domains without
// changing element identity. Both arrays use the owner's graph work area.
class GraphIdentityIndex final
{
public:
  explicit GraphIdentityIndex(common::ObIAllocator &allocator);
  ~GraphIdentityIndex() = default;

  int init(int64_t expected_count);
  int get_or_insert(const GraphElementIdentity &identity,
                    int64_t &entry_index,
                    bool &inserted,
                    int64_t binding_id = 0);
  int find(const GraphElementIdentity &identity,
           int64_t &entry_index,
           bool &found,
           int64_t binding_id = 0) const;
  void reset();
  int64_t count() const { return entries_.count(); }
  int64_t used_memory() const
  {
    return bucket_heads_.get_data_size()
               > INT64_MAX - entries_.get_data_size()
        ? INT64_MAX
        : bucket_heads_.get_data_size() + entries_.get_data_size();
  }

private:
  struct Entry
  {
    GraphElementIdentity identity_{};
    // Optional correlation domain; adjacency-only users keep the default zero.
    int64_t binding_id_{0};
    int64_t next_entry_{-1};
    TO_STRING_KV(K_(identity), K_(next_entry));
  };

  int calc_compatible_hash(const GraphElementIdentity &identity,
                           uint64_t &hash) const;
  int get_bucket(const GraphElementIdentity &identity,
                 int64_t &bucket,
                 int64_t binding_id) const;
  int grow_buckets();

private:
  common::ObArray<int64_t> bucket_heads_;
  common::ObArray<Entry> entries_;

  DISALLOW_COPY_AND_ASSIGN(GraphIdentityIndex);
};

// Distinguishes the anchor-existence lookup from the target-existence lookup
// performed after an edge page. Access implementations may share machinery,
// but failure injection and diagnostics must retain the actual phase.
enum class GraphVertexLookupPhase : uint8_t
{
  SOURCE = 0,
  TARGET
};

// The DAS-facing access adapter owns the transaction descriptor, statement
// snapshot and schema guard. It must return an error for scan/RPC failures and
// must never encode a failed read as an empty successful page.
class IGraphExpandAccess
{
public:
  virtual ~IGraphExpandAccess() = default;

  virtual void release() {}
  virtual int64_t used_memory() const { return 0; }
  virtual int check_status() = 0;
  // True when disjoint source batches can be scanned independently without
  // repeating an unfiltered access. Adjacency ranges have this property;
  // edge-table fallback scans require the complete BFS-level source set.
  virtual bool can_partition_sources() const = 0;

  virtual int lookup_vertices(
      const common::ObIArray<GraphElementIdentity> &requested,
      common::ObIArray<GraphElementIdentity> &existing,
      GraphVertexLookupPhase phase) = 0;

  // scanned_edges is the number of rows delivered by DAS during this call,
  // before graph SQL filters and full-scan frontier matching.
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

private:
  struct SourceInputGroup
  {
    int64_t first_input_{-1};
    int64_t last_input_{-1};
    TO_STRING_KV(K_(first_input), K_(last_input));
  };

  int load_next_page();
  int validate_and_account(const common::ObIArray<GraphExpandInput> &inputs);
  int build_source_input_groups();
  int lookup_source_vertices();
  int build_existing_source_index();
  int start_edge_input_group(const GraphExpandEdge &edge);
  bool contains(const common::ObIArray<GraphElementIdentity> &identities,
                const GraphElementIdentity &identity) const;
  int validate_lookup_result(
      const common::ObIArray<GraphElementIdentity> &requested,
      const common::ObIArray<GraphElementIdentity> &existing) const;
  int deep_copy_identity(const GraphElementIdentity &source,
                         GraphElementIdentity &destination);
  int deep_copy_inputs(const common::ObIArray<GraphExpandInput> &inputs);
  int validate_input_keys(
      const common::ObIArray<GraphExpandInput> &inputs);
  int stabilize_identities(common::ObIArray<GraphElementIdentity> &identities);
  int stabilize_edge_page();
  int check_memory_limit();
  int push_unique(common::ObIArray<GraphElementIdentity> &identities,
                  const GraphElementIdentity &identity);
  int fail(int error);

private:
  IGraphExpandAccess &access_;
  // ObObj owns variable-length values by pointer. Keep a resettable child arena
  // for every identity copied across scanner/lookup calls so page turnover can
  // never invalidate path state.
  common::ObArenaAllocator identity_allocator_;
  // Maps one distinct typed source identity to its input group. Group links
  // retain every binding/path-state occurrence in original input order.
  GraphIdentityIndex source_input_index_;
  // Validates source lookup results and gives each returned edge constant-time
  // source-existence checks. DOUBLE batches retain linear comparison because
  // fixed-scale tolerant equality is not transitive.
  GraphIdentityIndex existing_source_index_;
  int64_t page_size_;
  int64_t memory_limit_;
  int64_t fixed_memory_bytes_;
  GraphPathDirection direction_;
  common::ObArray<GraphExpandInput> inputs_;
  // Transient open-addressed slots replace quadratic duplicate handle checks.
  // validate_input_keys() releases them before adjacency state is allocated.
  common::ObArray<int64_t> input_key_slots_;
  common::ObArray<GraphElementIdentity> source_identities_;
  common::ObArray<SourceInputGroup> source_input_groups_;
  common::ObArray<int64_t> next_input_indices_;
  // A full-scan frontier remains whole for the edge scan, but source vertex
  // existence checks are bounded so DAS never fans an unbounded range set to
  // every selected tablet.
  common::ObArray<GraphElementIdentity> source_lookup_batch_;
  common::ObArray<GraphElementIdentity> source_lookup_results_;
  common::ObArray<GraphElementIdentity> existing_sources_;
  common::ObArray<GraphExpandEdge> edge_page_;
  common::ObArray<GraphElementIdentity> target_identities_;
  common::ObArray<GraphElementIdentity> existing_targets_;
  GraphElementIdentity after_edge_;
  int64_t edge_index_;
  int64_t input_index_;
  bool has_after_edge_;
  bool end_;
  bool is_open_;
  bool source_input_groups_enabled_{true};
  bool input_group_started_{false};
  GraphExpandStats stats_;
};

} // namespace sql
} // namespace oceanbase
