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

#include "sql/engine/graph/graph_binding_store.h"
#include "sql/engine/graph/graph_identity_index.h"
#include "sql/engine/graph/graph_feedback_row_desc.h"
#include "sql/engine/recursive_cte/ob_recursive_inner_data_op.h"

namespace oceanbase
{
namespace sql
{
// Retains one existing witness per (source key, current key, hop). The pump
// still owns full witness rows; this policy only owns typed membership keys.
// Enabled only after proving that later WALK continuations are interchangeable.
class GraphShortestRowAdmission final : public IRecursiveRowAdmission
{
public:
  int init(int64_t memory_limit, const GraphPathDesc &path,
           const GraphFeedbackRowDesc &row);
  int admit(const common::ObIArray<ObExpr *> &row,
            ObEvalCtx &ctx, bool &accepted) override;
  void reset();

private:
  int materialize_key(const common::ObIArray<ObExpr *> &row, ObEvalCtx &ctx,
                      int64_t begin, int64_t count, common::ObObj *cells,
                      GraphElementIdentity &identity);
  int intern(GraphIdentityIndex &index, const GraphElementIdentity &identity,
             int64_t binding, int64_t &position, bool &inserted);

  GraphWorkAreaAllocator allocator_{};
  common::ObArenaAllocator keys_{allocator_};
  GraphIdentityIndex sources_{allocator_};
  GraphIdentityIndex states_{allocator_};
  GraphPathDesc path_{};
  GraphFeedbackRowDesc row_{};
};
} // namespace sql
} // namespace oceanbase
