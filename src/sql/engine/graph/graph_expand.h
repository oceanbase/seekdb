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

// The DAS-facing access adapter owns the transaction descriptor, statement
// snapshot and schema guard. It must return an error for scan/RPC failures and
// must never encode a failed read as an empty successful page.
class IGraphExpandAccess
{
public:
  virtual ~IGraphExpandAccess() = default;

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
