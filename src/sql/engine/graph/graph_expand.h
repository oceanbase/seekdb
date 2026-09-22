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

// GraphExpand validates this protocol limit independently of the caller's
// query-memory budget. Frontier controllers split larger levels into batches.
static const int64_t GRAPH_EXPAND_MAX_INPUT_STATE_COUNT = 4096;

// Physical table/column binding for one GraphExpand hop. Endpoint columns are
// normalized to the traversal direction: edge_current_columns_ join the input
// frontier, while edge_next_columns_ produce target vertex identities. The
// selected edge access table is either edge_table_id_ itself or a readable
// local index whose leading columns are edge_current_columns_.
struct GraphExpandAccessDesc
{
  OB_UNIS_VERSION(1);
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
  uint64_t source_key_columns_[GRAPH_IDENTITY_MAX_KEYS]{
      common::OB_INVALID_ID, common::OB_INVALID_ID};
  uint64_t edge_key_columns_[GRAPH_IDENTITY_MAX_KEYS]{
      common::OB_INVALID_ID, common::OB_INVALID_ID};
  uint64_t target_key_columns_[GRAPH_IDENTITY_MAX_KEYS]{
      common::OB_INVALID_ID, common::OB_INVALID_ID};
  uint64_t edge_current_columns_[GRAPH_IDENTITY_MAX_KEYS]{
      common::OB_INVALID_ID, common::OB_INVALID_ID};
  uint64_t edge_next_columns_[GRAPH_IDENTITY_MAX_KEYS]{
      common::OB_INVALID_ID, common::OB_INVALID_ID};
  TO_STRING_KV(K_(source_table_id), K_(source_table_version),
               K_(edge_table_id), K_(edge_table_version),
               K_(target_table_id), K_(target_table_version),
               K_(edge_access_table_id), K_(edge_access_table_version),
               K_(source_key_count), K_(edge_key_count), K_(target_key_count),
               "source_key_0", source_key_columns_[0],
               "source_key_1", source_key_columns_[1],
               "edge_key_0", edge_key_columns_[0],
               "edge_key_1", edge_key_columns_[1],
               "target_key_0", target_key_columns_[0],
               "target_key_1", target_key_columns_[1],
               "edge_current_0", edge_current_columns_[0],
               "edge_current_1", edge_current_columns_[1],
               "edge_next_0", edge_next_columns_[0],
               "edge_next_1", edge_next_columns_[1]);
};

// The DAS-facing access adapter owns the transaction descriptor, statement
// snapshot and schema guard. It must return an error for scan/RPC failures and
// must never encode a failed read as an empty successful page.
class IGraphExpandAccess
{
public:
  virtual ~IGraphExpandAccess() = default;

  virtual void release() {}
  virtual int check_status() = 0;

  virtual int lookup_vertices(
      const common::ObIArray<GraphElementIdentity> &requested,
      common::ObIArray<GraphElementIdentity> &existing) = 0;

  virtual int scan_edges(
      const common::ObIArray<GraphElementIdentity> &sources,
      GraphPathDirection direction,
      const GraphElementIdentity *after_edge,
      int64_t limit,
      common::ObIArray<GraphExpandEdge> &edges,
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

private:
  int load_next_page();
  int validate_and_account(const common::ObIArray<GraphExpandInput> &inputs);
  bool contains(const common::ObIArray<GraphElementIdentity> &identities,
                const GraphElementIdentity &identity) const;
  int validate_lookup_result(
      const common::ObIArray<GraphElementIdentity> &requested,
      const common::ObIArray<GraphElementIdentity> &existing) const;
  int deep_copy_identity(const GraphElementIdentity &source,
                         GraphElementIdentity &destination);
  int deep_copy_inputs(const common::ObIArray<GraphExpandInput> &inputs);
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
  int64_t page_size_;
  int64_t memory_limit_;
  int64_t fixed_memory_bytes_;
  GraphPathDirection direction_;
  common::ObArray<GraphExpandInput> inputs_;
  common::ObArray<GraphElementIdentity> source_identities_;
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
  GraphExpandStats stats_;
};

} // namespace sql
} // namespace oceanbase
