/*
 * Copyright (c) 2025 OceanBase.
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

#include "sql/engine/expr/ob_expr_merging_frozen_time.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprMergingFrozenTime::ObExprMergingFrozenTime(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_MERGING_FROZEN_TIME,
                         N_MERGING_FROZEN_TIME,
                         0,
                         VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION,
                         INTERNAL_IN_MYSQL_MODE)
{
}

ObExprMergingFrozenTime::~ObExprMergingFrozenTime()
{
}

int ObExprMergingFrozenTime::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_timestamp();
  type.set_scale(MAX_SCALE_FOR_TEMPORAL);
  return OB_SUCCESS;
}

}
}
