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
#include "sql/engine/expr/ob_expr_subquery_less_equal.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprSubQueryLessEqual::ObExprSubQueryLessEqual(ObIAllocator &alloc)
  : ObSubQueryRelationalExpr(alloc, T_OP_SQ_LE, N_SQ_LESS_EQUAL, 2, NOT_ROW_DIMENSION,
                             INTERNAL_IN_MYSQL_MODE, INTERNAL_IN_ORACLE_MODE)
{
}

ObExprSubQueryLessEqual::~ObExprSubQueryLessEqual()
{
}


}  // namespace sql
}  // namespace oceanbase
