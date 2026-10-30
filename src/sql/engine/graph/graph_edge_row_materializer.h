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
#include "sql/engine/graph/graph_expand_scan_desc.h"

namespace oceanbase
{
namespace sql
{

// Borrowed expressions in declared key order. The owning scan definition
// outlives this binding; column IDs are needed only while establishing it.
struct GraphIdentityExprBinding
{
  int init(const ObDASScanCtDef &ctdef, const uint64_t *columns, int64_t count);
  bool is_valid() const;
  int64_t key_count_{0};
  ObExpr *key_exprs_[common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
  TO_STRING_KV(K_(key_count));
};

// Converts the current DAS row to typed identities and optional edge payload.
// Owns neither DAS state nor memory. Returned values borrow the caller's page
// allocator; payload is requested separately, after frontier matching.
class GraphEdgeRowMaterializer final
{
public:
  GraphEdgeRowMaterializer(ObEvalCtx &eval_ctx, common::ObArenaAllocator &allocator,
                           common::ObArenaAllocator &scratch_allocator,
                           const GraphPathDesc &path_desc)
      : eval_ctx_(eval_ctx), allocator_(allocator), scratch_allocator_(scratch_allocator),
        path_desc_(path_desc)
  {}
  int materialize_edge(const GraphIdentityExprBinding &source,
                       const GraphIdentityExprBinding &edge_binding,
                       const GraphIdentityExprBinding &target,
                       GraphExpandEdge &edge, bool &matches);
  int materialize_payload(const GraphExpandScanDesc &scan_desc, GraphEdgePayload &payload);

private:
  int materialize_identity(uint64_t element_id, const GraphIdentityExprBinding &binding,
                           GraphElementIdentity &identity);
  int row_has_null_endpoint(const GraphIdentityExprBinding &source,
                            const GraphIdentityExprBinding &target, bool &has_null) const;
  ObEvalCtx &eval_ctx_;
  common::ObArenaAllocator &allocator_;
  common::ObArenaAllocator &scratch_allocator_;
  const GraphPathDesc &path_desc_;
};

} // namespace sql
} // namespace oceanbase
