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

#include "sql/das/ob_das_ref.h"
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

// DAS implementation of the physical one-hop access contract. It supports
// batched vertex lookup, adjacency-index range scans and a stateful
// single-tablet edge-table fallback scan. Partitioned fallback scans are added
// separately.
class GraphExpandDasAccess final : public IGraphExpandAccess
{
public:
  GraphExpandDasAccess(ObExecContext &exec_ctx, ObEvalCtx &eval_ctx);
  ~GraphExpandDasAccess() override;

  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           const ObTableScanSpec &source_scan,
           const ObTableScanSpec &edge_scan,
           const ObTableScanSpec &target_scan);

  void release() override;
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
  int init_scan_rtdef(const ObTableScanSpec &scan_spec,
                      const ObDASScanCtDef &scan_ctdef,
                      bool uses_index_back,
                      common::ObIAllocator &scan_allocator,
                      ObDASScanRtDef &scan_rtdef) const;
  int materialize_identity(uint64_t element_id,
                           int64_t key_count,
                           ObExpr *const *key_exprs,
                           GraphElementIdentity &identity) const;
  int materialize_identity(const VertexLookupBinding &binding,
                           GraphElementIdentity &identity) const;
  int edge_row_has_null_endpoint(bool &has_null) const;
  int materialize_edge(GraphExpandEdge &edge, bool &matches) const;
  int validate_edge_scan_request(
      const common::ObIArray<GraphElementIdentity> &sources,
      GraphPathDirection direction,
      const GraphElementIdentity *after_edge,
      int64_t limit) const;
  int init_edge_scan_rtdefs(bool uses_index_back);
  int build_adjacency_ranges(
      const common::ObIArray<GraphElementIdentity> &sources,
      common::ObIArray<common::ObNewRange> &ranges);
  int attach_edge_lookup(ObDASScanOp &scan_op,
                         const ObDASTabletLoc &index_tablet_loc);
  int start_full_edge_scan(bool &empty);
  int start_adjacency_edge_scan(
      const common::ObIArray<GraphElementIdentity> &sources,
      bool &empty);
  int clear_edge_eval_flags();
  int get_edge_page(
      const common::ObIArray<GraphElementIdentity> &sources,
      int64_t limit,
      common::ObIArray<GraphExpandEdge> &edges,
      bool &end);
  int reset_edge_scan(int ret);
  int64_t find_requested(
      const common::ObIArray<GraphElementIdentity> &requested,
      const GraphElementIdentity &identity) const;
  bool contains(const common::ObIArray<GraphElementIdentity> &identities,
                const GraphElementIdentity &identity) const;

private:
  ObExecContext &exec_ctx_;
  ObEvalCtx &eval_ctx_;
  ObDASRef edge_das_ref_;
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc access_desc_{};
  VertexLookupBinding source_binding_{};
  VertexLookupBinding target_binding_{};
  EdgeScanBinding edge_binding_{};
  ObDASScanRtDef *edge_scan_rtdef_{nullptr};
  ObDASScanRtDef *edge_lookup_rtdef_{nullptr};
  DASOpResultIter edge_result_iter_{};
  GraphElementIdentity edge_cursor_{};
  uint64_t edge_sources_hash_{0};
  bool has_edge_cursor_{false};
  bool edge_scan_active_{false};
  bool initialized_{false};
};

} // namespace sql
} // namespace oceanbase
