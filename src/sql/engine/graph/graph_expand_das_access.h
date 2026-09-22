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

namespace oceanbase
{
namespace sql
{

class ObEvalCtx;
class ObExecContext;
class ObExpr;
class ObTableScanSpec;
struct ObDASScanCtDef;
struct ObDASScanRtDef;

// DAS implementation of the physical one-hop access contract. Vertex lookup
// is executable; edge access is introduced in reviewable increments, starting
// with binding the scan output needed to materialize one adjacency row.
class GraphExpandDasAccess final : public IGraphExpandAccess
{
public:
  GraphExpandDasAccess(ObExecContext &exec_ctx, ObEvalCtx &eval_ctx);
  ~GraphExpandDasAccess() override = default;

  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           const ObTableScanSpec &source_scan,
           const ObTableScanSpec &edge_scan,
           const ObTableScanSpec &target_scan);

  int check_status() override;
  int lookup_vertices(
      const common::ObIArray<GraphElementIdentity> &requested,
      common::ObIArray<GraphElementIdentity> &existing) override;
  int scan_edges(
      const common::ObIArray<GraphElementIdentity> &sources,
      GraphPathDirection direction,
      const GraphElementIdentity *after_edge,
      int64_t limit,
      common::ObIArray<GraphExpandEdge> &edges,
      bool &end) override;

  bool is_initialized() const { return initialized_; }

private:
  struct VertexLookupBinding
  {
    bool is_valid() const;

    uint64_t element_id_{common::OB_INVALID_ID};
    uint64_t table_id_{common::OB_INVALID_ID};
    int64_t key_count_{0};
    uint64_t key_columns_[GRAPH_IDENTITY_MAX_KEYS]{
        common::OB_INVALID_ID, common::OB_INVALID_ID};
    const ObTableScanSpec *scan_spec_{nullptr};
    const ObDASScanCtDef *scan_ctdef_{nullptr};
    ObExpr *key_exprs_[GRAPH_IDENTITY_MAX_KEYS]{nullptr, nullptr};
    TO_STRING_KV(K_(element_id), K_(table_id), K_(key_count),
                 "key_column_0", key_columns_[0],
                 "key_column_1", key_columns_[1],
                 KP_(scan_spec), KP_(scan_ctdef));
  };

  // Binds the three typed identities carried by one physical edge row. The
  // output ctdef is the base-table lookup when an adjacency index needs index
  // back, otherwise it is the access scan itself (including covering indexes).
  struct EdgeScanBinding
  {
    bool is_valid() const;

    uint64_t edge_element_id_{common::OB_INVALID_ID};
    uint64_t edge_table_id_{common::OB_INVALID_ID};
    uint64_t access_table_id_{common::OB_INVALID_ID};
    int64_t source_key_count_{0};
    int64_t edge_key_count_{0};
    int64_t target_key_count_{0};
    uint64_t source_columns_[GRAPH_IDENTITY_MAX_KEYS]{
        common::OB_INVALID_ID, common::OB_INVALID_ID};
    uint64_t edge_columns_[GRAPH_IDENTITY_MAX_KEYS]{
        common::OB_INVALID_ID, common::OB_INVALID_ID};
    uint64_t target_columns_[GRAPH_IDENTITY_MAX_KEYS]{
        common::OB_INVALID_ID, common::OB_INVALID_ID};
    const ObTableScanSpec *scan_spec_{nullptr};
    const ObDASScanCtDef *access_ctdef_{nullptr};
    const ObDASScanCtDef *output_ctdef_{nullptr};
    ObExpr *source_exprs_[GRAPH_IDENTITY_MAX_KEYS]{nullptr, nullptr};
    ObExpr *edge_exprs_[GRAPH_IDENTITY_MAX_KEYS]{nullptr, nullptr};
    ObExpr *target_exprs_[GRAPH_IDENTITY_MAX_KEYS]{nullptr, nullptr};
    TO_STRING_KV(K_(edge_element_id), K_(edge_table_id), K_(access_table_id),
                 K_(source_key_count), K_(edge_key_count),
                 K_(target_key_count),
                 "source_column_0", source_columns_[0],
                 "source_column_1", source_columns_[1],
                 "edge_column_0", edge_columns_[0],
                 "edge_column_1", edge_columns_[1],
                 "target_column_0", target_columns_[0],
                 "target_column_1", target_columns_[1],
                 KP_(scan_spec), KP_(access_ctdef), KP_(output_ctdef));
  };

  int init_vertex_binding(uint64_t element_id,
                          uint64_t table_id,
                          int64_t key_count,
                          const uint64_t *key_columns,
                          const ObTableScanSpec &scan_spec,
                          VertexLookupBinding &binding);
  int init_edge_binding(const GraphPathDesc &path_desc,
                        const GraphExpandAccessDesc &access_desc,
                        const ObTableScanSpec &scan_spec,
                        EdgeScanBinding &binding);
  const VertexLookupBinding *find_vertex_binding(uint64_t element_id) const;
  int validate_requested(
      const common::ObIArray<GraphElementIdentity> &requested,
      const VertexLookupBinding *&binding) const;
  int lookup_vertices(const VertexLookupBinding &binding,
                      const common::ObIArray<GraphElementIdentity> &requested,
                      common::ObIArray<GraphElementIdentity> &existing);
  int init_scan_rtdef(const VertexLookupBinding &binding,
                      common::ObIAllocator &scan_allocator,
                      ObDASScanRtDef &scan_rtdef) const;
  int materialize_identity(const VertexLookupBinding &binding,
                           GraphElementIdentity &identity) const;
  int64_t find_requested(
      const common::ObIArray<GraphElementIdentity> &requested,
      const GraphElementIdentity &identity) const;
  bool contains(const common::ObIArray<GraphElementIdentity> &identities,
                const GraphElementIdentity &identity) const;

private:
  ObExecContext &exec_ctx_;
  ObEvalCtx &eval_ctx_;
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc access_desc_{};
  VertexLookupBinding source_binding_{};
  VertexLookupBinding target_binding_{};
  EdgeScanBinding edge_binding_{};
  bool initialized_{false};
};

} // namespace sql
} // namespace oceanbase
