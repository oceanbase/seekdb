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
#include "sql/engine/graph/graph_expand_access_desc.h"
#include "sql/engine/graph/graph_expand_scan_desc.h"
#include "sql/das/ob_das_ref.h"

namespace oceanbase
{
namespace sql
{
class ObTableLocation;

// Owns one stateful edge scan across pages, including index-back tasks.
// Returned identities/payload borrow the current page arena. The caller must
// stabilize them before the next scan_edges() call or release().
class GraphEdgeScanCursor final
{
public:
  GraphEdgeScanCursor(ObExecContext &exec_ctx, ObEvalCtx &eval_ctx,
                      common::ObIAllocator &work_area_allocator);
  ~GraphEdgeScanCursor();
  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           const GraphExpandScanDesc &edge_scan);
  int release();
  int64_t used_memory() const;
  bool can_partition_sources() const { return access_desc_.uses_adjacency_index(); }
  int scan_edges(const common::ObIArray<GraphElementIdentity> &sources,
                 GraphPathDirection direction,
                 const GraphElementIdentity *after_edge,
                 int64_t limit,
                 common::ObIArray<GraphExpandEdge> &edges,
                 int64_t &scanned_edges, bool &end);
private:
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
    uint64_t source_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    uint64_t edge_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    uint64_t target_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    const GraphExpandScanDesc *scan_desc_{nullptr};
    const ObDASScanCtDef *access_ctdef_{nullptr};
    const ObDASScanCtDef *output_ctdef_{nullptr};
    ObExpr *source_exprs_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    ObExpr *edge_exprs_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    ObExpr *target_exprs_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    TO_STRING_KV(K_(edge_element_id), K_(edge_table_id), K_(access_table_id),
                 K_(source_key_count), K_(edge_key_count),
                 K_(target_key_count),
                 "source_columns", common::ObArrayWrap<uint64_t>(source_columns_, source_key_count_),
                 "edge_columns", common::ObArrayWrap<uint64_t>(edge_columns_, edge_key_count_),
                 "target_columns", common::ObArrayWrap<uint64_t>(target_columns_, target_key_count_),
                 KP_(scan_desc), KP_(access_ctdef), KP_(output_ctdef));
  };


  int check_status();
  int init_edge_binding(const GraphPathDesc &path_desc,
                        const GraphExpandAccessDesc &access_desc,
                        const GraphExpandScanDesc &scan_desc,
                        EdgeScanBinding &binding);
  int materialize_identity(uint64_t element_id, int64_t key_count,
                           ObExpr *const *key_exprs,
                           GraphElementIdentity &identity);
  int save_edge_cursor(const GraphElementIdentity &identity);
  int edge_row_has_null_endpoint(bool &has_null) const;
  int materialize_edge(GraphExpandEdge &edge, bool &matches);
  int materialize_edge_payload(GraphEdgePayload &payload);
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
  int clear_edge_eval_flags(bool vectorized);
  int get_edge_page(
      const common::ObIArray<GraphElementIdentity> &sources,
      int64_t limit,
      common::ObIArray<GraphExpandEdge> &edges,
      int64_t &scanned_edges,
      bool &end);
  bool has_live_edge_scan_resources() const;
  bool edge_scan_resources_released() const;
  int reset_edge_scan(int ret, bool retain_allocator_page);
  int build_edge_source_index(
      const common::ObIArray<GraphElementIdentity> &sources);

  ObExecContext &exec_ctx_;
  ObEvalCtx &eval_ctx_;
  ObDASRef edge_das_ref_;
  // Materialized edge identities are page-local views. The caller stabilizes
  // them before the next scan_edges() call, when this arena can be reused.
  common::ObArenaAllocator identity_page_allocator_;
  // The scan cursor must outlive page identities and the vertex lookup between
  // two edge pages, so it owns a separate deep copy of the last edge rowkey.
  common::ObArenaAllocator edge_cursor_allocator_;
  GraphIdentityIndex edge_source_index_;
  GraphPathDesc path_desc_{};
  GraphExpandAccessDesc access_desc_{};
  EdgeScanBinding edge_binding_{};
  ObDASScanRtDef *edge_scan_rtdef_{nullptr};
  ObDASScanRtDef *edge_lookup_rtdef_{nullptr};
  DASOpResultIter edge_result_iter_{};
  GraphElementIdentity edge_cursor_{};
  const GraphElementIdentity *edge_sources_data_{nullptr};
  int64_t edge_source_count_{0};
  int64_t edge_batch_count_{0};
  int64_t edge_batch_index_{0};
  bool has_edge_cursor_{false};
  bool edge_scan_active_{false};
  bool initialized_{false};
};

} // namespace sql
} // namespace oceanbase
