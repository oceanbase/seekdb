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
#include "sql/das/ob_das_ref.h"

namespace oceanbase
{
namespace sql
{
namespace graph_das_detail
{
// Shared mechanics only: callers own all runtime definitions and allocations.
int close_das_tasks(ObDASRef &das_ref, int ret);
int init_scan_rtdef(ObExecContext &exec_ctx,
                    ObEvalCtx &eval_ctx,
                    const GraphExpandScanDesc &scan_desc,
                    const ObDASScanCtDef &scan_ctdef,
                    const DASTableAccessMeta *access_meta,
                    bool uses_index_back,
                    common::ObIAllocator &scan_allocator,
                    ObDASScanRtDef &scan_rtdef);
int materialize_identity(ObEvalCtx &eval_ctx,
                         common::ObIAllocator &allocator,
                         uint64_t graph_id,
                         uint64_t element_id,
                         int64_t key_count,
                         ObExpr *const *key_exprs,
                         GraphElementIdentity &identity);
} // namespace graph_das_detail
} // namespace sql
} // namespace oceanbase
