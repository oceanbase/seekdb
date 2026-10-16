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

// Owns vertex multi-get and its reusable source/target partition routers.
// DAS tasks are call-local; release() drops transient results, not routers.
class GraphVertexLookup final
{
public:
  GraphVertexLookup(ObExecContext &exec_ctx, ObEvalCtx &eval_ctx,
                    common::ObIAllocator &work_area_allocator);
  ~GraphVertexLookup();
  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           const GraphExpandScanDesc &source_scan,
           const GraphExpandScanDesc &target_scan);
  int lookup_vertices(const common::ObIArray<GraphElementIdentity> &requested,
                      common::ObIArray<GraphElementIdentity> &existing,
                      GraphVertexLookupPhase phase);
  void release();
  int64_t used_memory() const;
  // The next physical access ends the previous lookup's budget-check window.
  void clear_lookup_memory() { vertex_lookup_memory_bytes_ = 0; }

private:
  struct VertexLookupBinding
  {
    bool is_valid() const;

    uint64_t element_id_{common::OB_INVALID_ID};
    uint64_t table_id_{common::OB_INVALID_ID};
    int64_t key_count_{0};
    uint64_t key_columns_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    const GraphExpandScanDesc *scan_desc_{nullptr};
    const ObDASScanCtDef *scan_ctdef_{nullptr};
    const ObDASTableLocMeta *loc_meta_{nullptr};
    ObExpr *key_exprs_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    TO_STRING_KV(K_(element_id), K_(table_id), K_(key_count),
                 "key_columns", common::ObArrayWrap<uint64_t>(key_columns_, key_count_),
                 KP_(scan_desc), KP_(scan_ctdef), KP_(loc_meta));
  };

  int init_vertex_binding(uint64_t element_id,
                          uint64_t table_id,
                          int64_t key_count,
                          const uint64_t *key_columns,
                          const GraphExpandScanDesc &scan_desc,
                          VertexLookupBinding &binding);
  const VertexLookupBinding *find_vertex_binding(uint64_t element_id) const;
  int validate_requested(
      const common::ObIArray<GraphElementIdentity> &requested,
      const VertexLookupBinding *&binding) const;
  int lookup_vertices(const VertexLookupBinding &binding,
                      const common::ObIArray<GraphElementIdentity> &requested,
                      common::ObIArray<GraphElementIdentity> &existing,
                      GraphVertexLookupPhase phase);
  int get_vertex_router(const VertexLookupBinding &binding,
                        ObTableLocation *&router);
  void destroy_vertex_routers();

  int check_status();
  int materialize_identity(const VertexLookupBinding &binding,
                           GraphElementIdentity &identity);
  ObExecContext &exec_ctx_;
  ObEvalCtx &eval_ctx_;
  common::ObIAllocator &work_area_allocator_;
  // Destroy both routers before reclaiming their shared arena.
  common::ObArenaAllocator vertex_router_allocator_{work_area_allocator_};
  common::ObArenaAllocator identity_page_allocator_;
  GraphPathDesc path_desc_{};
  VertexLookupBinding source_binding_{};
  VertexLookupBinding target_binding_{};
  ObTableLocation *source_vertex_router_{nullptr};
  ObTableLocation *target_vertex_router_{nullptr};
  // Preserve transient DAS usage until the caller checks query memory.
  int64_t vertex_lookup_memory_bytes_{0};
  bool initialized_{false};
};

} // namespace sql
} // namespace oceanbase
