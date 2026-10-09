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
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace sql
{

// Adjacency access limits one source partition independently of the caller's
// query-memory budget. Full scans receive a complete BFS level so the edge
// table is scanned only once; their size is governed by the work-area limit.
static const int64_t GRAPH_EXPAND_MAX_INPUT_STATE_COUNT = 4096;
static const int64_t GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE = 4096;

// Query-local hash index over typed graph identities. Compatible NUMBER and
// DECIMAL_INT keys are normalized, while tolerance-compared DOUBLE keys use a
// type bucket, so bucket selection follows graph endpoint equality. Identities
// are shallow views: the caller must keep their rowkey backing alive until
// reset(). Both buckets and entries use the owner's graph work-area allocator.
class GraphIdentityIndex final
{
public:
  explicit GraphIdentityIndex(common::ObIAllocator &allocator);
  ~GraphIdentityIndex() = default;

  int init(int64_t expected_count);
  int get_or_insert(const GraphElementIdentity &identity,
                    int64_t &entry_index,
                    bool &inserted);
  int find(const GraphElementIdentity &identity,
           int64_t &entry_index,
           bool &found) const;
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
    int64_t next_entry_{-1};
    TO_STRING_KV(K_(identity), K_(next_entry));
  };

  int calc_compatible_hash(const GraphElementIdentity &identity,
                           uint64_t &hash) const;
  int get_bucket(const GraphElementIdentity &identity,
                 int64_t &bucket) const;

private:
  common::ObArray<int64_t> bucket_heads_;
  common::ObArray<Entry> entries_;

  DISALLOW_COPY_AND_ASSIGN(GraphIdentityIndex);
};

// Physical table/column binding for one GraphExpand hop. Endpoint columns are
// normalized to the traversal direction: edge_current_columns_ join the input
// frontier, while edge_next_columns_ produce target vertex identities. The
// selected edge access table is either edge_table_id_ itself or a readable
// local index whose leading columns are edge_current_columns_.
struct GraphExpandAccessDesc
{
  OB_UNIS_VERSION(2);
public:
  int init(const GraphPathDesc &path_desc,
           uint64_t edge_access_table_id,
           share::schema::ObSchemaGetterGuard &schema_guard);
  bool is_valid() const;
  bool uses_adjacency_index() const
  { return edge_access_table_id_ != edge_table_id_; }
  uint64_t hash(uint64_t seed = 0) const;

  uint64_t source_table_id_{common::OB_INVALID_ID};
  int64_t source_table_version_{0};
  uint64_t edge_table_id_{common::OB_INVALID_ID};
  int64_t edge_table_version_{0};
  uint64_t target_table_id_{common::OB_INVALID_ID};
  int64_t target_table_version_{0};
  uint64_t edge_access_table_id_{common::OB_INVALID_ID};
  int64_t edge_access_table_version_{0};
  int64_t source_key_count_{0};
  int64_t edge_key_count_{0};
  int64_t target_key_count_{0};
  uint64_t source_key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t edge_key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t target_key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t edge_current_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  uint64_t edge_next_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  TO_STRING_KV(K_(source_table_id), K_(source_table_version),
               K_(edge_table_id), K_(edge_table_version),
               K_(target_table_id), K_(target_table_version),
               K_(edge_access_table_id), K_(edge_access_table_version),
               K_(source_key_count), K_(edge_key_count), K_(target_key_count),
               "source_keys", common::ObArrayWrap<uint64_t>(source_key_columns_, source_key_count_),
               "edge_keys", common::ObArrayWrap<uint64_t>(edge_key_columns_, edge_key_count_),
               "target_keys", common::ObArrayWrap<uint64_t>(target_key_columns_, target_key_count_),
               "edge_current", common::ObArrayWrap<uint64_t>(edge_current_columns_, source_key_count_),
               "edge_next", common::ObArrayWrap<uint64_t>(edge_next_columns_, target_key_count_));
};

// The first native feedback runtime materializes only the generated recursive
// row's fixed fields: hop count, source key and current key. Keep this predicate
// shared by EXPLAIN and execution so a plan never advertises a runtime that the
// physical operator will reject.
bool supports_basic_native_graph_feedback(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc,
    int64_t output_expr_count);

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
