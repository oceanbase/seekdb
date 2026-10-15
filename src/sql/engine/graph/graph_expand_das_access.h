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

#include "sql/engine/graph/graph_vertex_lookup.h"
#include "sql/engine/graph/graph_edge_scan_cursor.h"

namespace oceanbase
{
namespace sql
{

// Composes call-local vertex lookup and a cross-page edge cursor. It implements
// the one-hop access contract without owning either component's DAS resources.
class GraphExpandDasAccess final : public IGraphExpandAccess
{
public:
  GraphExpandDasAccess(ObExecContext &exec_ctx, ObEvalCtx &eval_ctx,
                       common::ObIAllocator &work_area_allocator);
  ~GraphExpandDasAccess() override;
  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           const GraphExpandScanDesc &source_scan,
           const GraphExpandScanDesc &edge_scan,
           const GraphExpandScanDesc &target_scan);
  void release() override;
  int64_t used_memory() const override;
  int check_status() override;
  bool can_partition_sources() const override
  { return edge_cursor_.can_partition_sources(); }
  int lookup_vertices(const common::ObIArray<GraphElementIdentity> &requested,
                      common::ObIArray<GraphElementIdentity> &existing,
                      GraphVertexLookupPhase phase) override;
  int scan_edges(const common::ObIArray<GraphElementIdentity> &sources,
                 GraphPathDirection direction,
                 const GraphElementIdentity *after_edge, int64_t limit,
                 common::ObIArray<GraphExpandEdge> &edges,
                 int64_t &scanned_edges, bool &end) override;
  bool is_initialized() const { return initialized_; }
private:
  ObExecContext &exec_ctx_;
  GraphVertexLookup vertex_lookup_;
  GraphEdgeScanCursor edge_cursor_;
  bool initialized_{false};
};

} // namespace sql
} // namespace oceanbase
