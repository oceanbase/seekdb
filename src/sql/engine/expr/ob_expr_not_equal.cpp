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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_not_equal.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprNotEqual::ObExprNotEqual(ObIAllocator &alloc)
    : ObRelationalExprOperator(alloc, T_OP_NE, N_NOT_EQUAL, 2)
{
}

int ObExprNotEqual::calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ret = ObRelationalExprOperator::calc_result_type2(type, type1, type2, type_ctx);
  return ret;
}

}
}
