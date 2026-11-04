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

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/graph/graph_feedback_row_desc.h"
#include "sql/engine/graph/graph_path_spec.h"

namespace oceanbase
{
namespace sql
{

class GraphFeedbackLoopSpec;

// Converts one completed graph result into the generated recursive-row
// expressions. It owns only the temporary JSON backing for the current row;
// traversal, binding restoration and output scheduling remain with the runtime.
class GraphFeedbackRowMaterializer final
{
public:
  GraphFeedbackRowMaterializer(ObEvalCtx &eval_ctx,
                               common::ObIAllocator &allocator)
      : eval_ctx_(eval_ctx), payload_allocator_(allocator)
  {}
  ~GraphFeedbackRowMaterializer() = default;

  int materialize_path(const GraphFeedbackLoopSpec &spec,
                       const common::ObIArray<GraphPathState> &path);
  int materialize_shortest_distance(
      const GraphFeedbackLoopSpec &spec,
      const GraphElementIdentity &source,
      const GraphElementIdentity &goal,
      int64_t distance);
  void reset() { payload_allocator_.reset(); }

private:
  int write_value(ObExpr *expr, const common::ObObj &value);
  int write_key(const ExprFixedArray &output_exprs,
                int64_t begin,
                int64_t count,
                const GraphElementIdentity &identity);
  int materialize_path_payloads(
      const GraphFeedbackLoopSpec &spec,
      const common::ObIArray<GraphPathState> &path);

  ObEvalCtx &eval_ctx_;
  common::ObArenaAllocator payload_allocator_;

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackRowMaterializer);
};

// Owns deep copies of rows while a vectorized output batch is assembled.
// Graph DAS and scalar expression evaluation share frames, so rows must be
// staged before the next graph access and published only after access stops.
class GraphFeedbackOutputBatch final
{
public:
  GraphFeedbackOutputBatch(ObEvalCtx &eval_ctx,
                           common::ObIAllocator &allocator)
      : eval_ctx_(eval_ctx), allocator_(allocator),
        rows_(OB_MALLOC_NORMAL_BLOCK_SIZE,
              common::ModulePageAllocator(allocator, "GraphOutBatch"))
  {}
  ~GraphFeedbackOutputBatch() = default;

  void begin();
  int stage(const ExprFixedArray &output_exprs);
  int publish(const ExprFixedArray &output_exprs);
  void reset();

private:
  ObEvalCtx &eval_ctx_;
  common::ObArenaAllocator allocator_;
  common::ObArray<ObChunkDatumStore::StoredRow *> rows_;

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackOutputBatch);
};

} // namespace sql
} // namespace oceanbase
