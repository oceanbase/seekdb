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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/graph/graph_expand_das_access.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

GraphExpandDasAccess::GraphExpandDasAccess(
    ObExecContext &exec_ctx, ObEvalCtx &eval_ctx, ObIAllocator &work_area_allocator)
    : exec_ctx_(exec_ctx),
      vertex_lookup_(exec_ctx, eval_ctx, work_area_allocator),
      edge_cursor_(exec_ctx, eval_ctx, work_area_allocator)
{
}

GraphExpandDasAccess::~GraphExpandDasAccess()
{
  release();
}

int GraphExpandDasAccess::init(
    const GraphPathDesc &path_desc, const GraphExpandAccessDesc &access_desc,
    const GraphExpandScanDesc &source_scan, const GraphExpandScanDesc &edge_scan,
    const GraphExpandScanDesc &target_scan)
{
  int ret = edge_cursor_.release();
  vertex_lookup_.release();
  initialized_ = false;
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to release previous graph edge scan", K(ret));
  } else if (OB_FAIL(vertex_lookup_.init(path_desc, access_desc, source_scan, target_scan))) {
    LOG_WARN("failed to initialize graph vertex lookup", K(ret));
  } else if (OB_FAIL(edge_cursor_.init(path_desc, access_desc, edge_scan))) {
    LOG_WARN("failed to initialize graph edge cursor", K(ret));
  } else {
    initialized_ = true;
  }
  return ret;
}

void GraphExpandDasAccess::release()
{
  const int ret = edge_cursor_.release();
  vertex_lookup_.release();
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to release graph edge scan", K(ret));
  }
}

int64_t GraphExpandDasAccess::used_memory() const
{
  const int64_t vertex_bytes = vertex_lookup_.used_memory();
  const int64_t edge_bytes = edge_cursor_.used_memory();
  return vertex_bytes > INT64_MAX - edge_bytes ? INT64_MAX : vertex_bytes + edge_bytes;
}

int GraphExpandDasAccess::check_status()
{
  return exec_ctx_.check_status();
}

int GraphExpandDasAccess::lookup_vertices(
    const ObIArray<GraphElementIdentity> &requested,
    ObIArray<GraphElementIdentity> &existing, GraphVertexLookupPhase phase)
{
  int ret = OB_SUCCESS;
  if (!initialized_) {
    existing.reset();
    ret = OB_NOT_INIT;
  } else {
    ret = vertex_lookup_.lookup_vertices(requested, existing, phase);
  }
  return ret;
}

int GraphExpandDasAccess::scan_edges(
    const ObIArray<GraphElementIdentity> &sources, GraphPathDirection direction,
    const GraphElementIdentity *after_edge, int64_t limit,
    ObIArray<GraphExpandEdge> &edges, int64_t &scanned_edges, bool &end)
{
  int ret = OB_SUCCESS;
  vertex_lookup_.clear_lookup_memory();
  if (!initialized_) {
    edges.reset();
    scanned_edges = 0;
    end = false;
    ret = OB_NOT_INIT;
  } else {
    ret = edge_cursor_.scan_edges(sources, direction, after_edge, limit,
                                  edges, scanned_edges, end);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
