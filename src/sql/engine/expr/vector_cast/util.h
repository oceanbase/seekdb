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

#ifndef OCEANBASE_SQL_ENG_EXPR_VECTOR_CAST_UTIL_H_
#define OCEANBASE_SQL_ENG_EXPR_VECTOR_CAST_UTIL_H_

#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "share/ob_errno.h"
#include "share/object/ob_obj_cast.h"

#define CAST_CHECKER_ARG_DECL const ObExpr &expr, ObEvalCtx &ctx, const EvalBound &bound,\
                                     const ObBitVector &skip, int &warning
#define CAST_CHECKER_ARG  expr, ctx, bound, skip, warning

namespace oceanbase
{
using namespace common;
namespace sql
{
template<VecValueTypeClass vec_tc, typename Vector>
struct BatchValueRangeChecker
{
  static const bool defined_ = false;
  static int check(CAST_CHECKER_ARG_DECL);
};

} // end sql
} // end ocenabase

#include "sql/engine/expr/vector_cast/util.ipp"

#undef DEF_BATCH_RANGE_CHECKER_DECL
#undef DEF_BATCH_RANGE_CHECKER_ARG
#endif // OCEANBASE_SQL_ENG_EXPR_VECTOR_CASE_UTIL_H_
