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

#ifndef OCEANBASE_SHARE_VECTOR_EXPR_CMP_FUNC_H_
#define OCEANBASE_SHARE_VECTOR_EXPR_CMP_FUNC_H_

#include "common/object/ob_obj_type.h"
#include "sql/engine/expr/ob_expr.h"
#include "common/object/ob_obj_compare.h"

namespace oceanbase
{
namespace sql
{
  struct ObDatumMeta;
} // end namespace sql

namespace common
{

struct VectorCmpExprFuncsHelper
{
  static void get_cmp_set(const sql::ObDatumMeta &l_meta, const sql::ObDatumMeta &r_meta,
                          sql::NullSafeRowCmpFunc &null_first_cmp, sql::NullSafeRowCmpFunc &null_last_cmp);
  
  static sql::RowCmpFunc get_row_cmp_func(const sql::ObDatumMeta &l_meta,
                                          const sql::ObDatumMeta &r_meta);

  static sql::ObExpr::EvalVectorFunc get_eval_vector_expr_cmp_func(const sql::ObDatumMeta &l_meta,
                                                                   const sql::ObDatumMeta &r_meta,
                                                                   const common::ObCmpOp cmp_op);
};

} // end namespace common
} // end namespace oceanbase
 #endif // OCEANBASE_SHARE_VECTOR_EXPR_CMP_FUNC_H_
