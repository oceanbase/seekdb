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
#include "sql/engine/expr/ob_i_expr_extra_info.h"
#include "lib/container/ob_fixed_array.h"

namespace oceanbase { namespace sql {
struct LogicalProgramInfo final : ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  LogicalProgramInfo(common::ObIAllocator &allocator, ObExprOperatorType type)
    : ObIExprExtraInfo(allocator, type), image_(allocator), parents_(allocator), ordinals_(allocator) {}
  int deep_copy(common::ObIAllocator &allocator, ObExprOperatorType type,
                ObIExprExtraInfo *&copy) const override;
  common::ObFixedArray<uint64_t, common::ObIAllocator> image_;
  common::ObFixedArray<int64_t, common::ObIAllocator> parents_;
  common::ObFixedArray<int64_t, common::ObIAllocator> ordinals_;
};

int prepare_logical_program(ObExpr &expr, common::ObIAllocator &allocator);
int eval_logical_program(const ObExpr &expr, ObEvalCtx &ctx, common::ObDatum &result);
int eval_logical_program_batch(const ObExpr &expr, ObEvalCtx &ctx,
                               const ObBitVector &skip, int64_t rows);
} }
