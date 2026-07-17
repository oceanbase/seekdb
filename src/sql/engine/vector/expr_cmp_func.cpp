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

#define USING_LOG_PREFIX SHARE

#include "expr_cmp_func.h"

#define NULL_FIRST_IDX 0
#define NULL_LAST_IDX  1

namespace oceanbase
{
namespace common
{
using namespace sql;

NullSafeRowCmpFunc NULLSAFE_ROW_CMP_FUNCS[MAX_VEC_TC][MAX_VEC_TC][2];
RowCmpFunc ROW_CMP_FUNCS[MAX_VEC_TC][MAX_VEC_TC];
sql::ObExpr::EvalVectorFunc EVAL_VECTOR_EXPR_CMP_FUNCS[MAX_VEC_TC][MAX_VEC_TC][CO_MAX];

void VectorCmpExprFuncsHelper::get_cmp_set(const sql::ObDatumMeta &l_meta,
                                           const sql::ObDatumMeta &r_meta,
                                           sql::NullSafeRowCmpFunc &null_first_cmp,
                                           sql::NullSafeRowCmpFunc &null_last_cmp)
{
  VecValueTypeClass l_tc = get_vec_value_tc(l_meta.type_, l_meta.scale_, l_meta.precision_);
  VecValueTypeClass r_tc = get_vec_value_tc(r_meta.type_, r_meta.scale_, r_meta.precision_);
  null_first_cmp = NULLSAFE_ROW_CMP_FUNCS[l_tc][r_tc][NULL_FIRST_IDX];
  null_last_cmp = NULLSAFE_ROW_CMP_FUNCS[l_tc][r_tc][NULL_LAST_IDX];
}

RowCmpFunc VectorCmpExprFuncsHelper::get_row_cmp_func(const sql::ObDatumMeta &l_meta,
                                                      const sql::ObDatumMeta &r_meta)
{
  VecValueTypeClass l_tc = get_vec_value_tc(l_meta.type_, l_meta.scale_, l_meta.precision_);
  VecValueTypeClass r_tc = get_vec_value_tc(r_meta.type_, r_meta.scale_, r_meta.precision_);
  return ROW_CMP_FUNCS[l_tc][r_tc];
}

extern void __expr_cmp_func_compilation0();
extern void __expr_cmp_func_compilation1();
extern void __expr_cmp_func_compilation2();
extern void __expr_cmp_func_compilation3();
extern void __expr_cmp_func_compilation4();
extern void __expr_cmp_func_compilation5();
extern void __expr_cmp_func_compilation6();
extern void __expr_cmp_func_compilation7();

static bool init_all_expr_cmp_funcs()
{
  __expr_cmp_func_compilation0();
  __expr_cmp_func_compilation1();
  __expr_cmp_func_compilation2();
  __expr_cmp_func_compilation3();
  __expr_cmp_func_compilation4();
  __expr_cmp_func_compilation5();
  __expr_cmp_func_compilation6();
  __expr_cmp_func_compilation7();
  return true;
}

static bool g_init_all_expr_cmp_funcs = init_all_expr_cmp_funcs();

} // end namespace common
} // end namespace oceanbase

namespace oceanbase
{
namespace common
{

sql::ObExpr::EvalVectorFunc VectorCmpExprFuncsHelper::get_eval_vector_expr_cmp_func(
  const sql::ObDatumMeta &l_meta, const sql::ObDatumMeta &r_meta, const common::ObCmpOp cmp_op)
{
  LOG_DEBUG("eval vector expr_cmp_func", K(l_meta), K(r_meta), K(cmp_op));
  VecValueTypeClass l_tc = get_vec_value_tc(l_meta.type_, l_meta.scale_, l_meta.precision_);
  VecValueTypeClass r_tc = get_vec_value_tc(r_meta.type_, r_meta.scale_, r_meta.precision_);
  return EVAL_VECTOR_EXPR_CMP_FUNCS[l_tc][r_tc][cmp_op];
}

} // end namespace common

} // end namespace oceanabse


namespace oceanbase
{
namespace common
{
// pure forwarding(declaration lives in ob_vector_cmp_func_basic.h):share base uses the cmp registry through this and does not touch sql headers
void ob_vector_cmp_get_cmp_set(const ObObjType type, const ObCollationType cs_type,
                               const int8_t scale, const int16_t precision,
                               NullSafeRowCmpFunc &null_first_cmp,
                               NullSafeRowCmpFunc &null_last_cmp)
{
  sql::ObDatumMeta meta(type, cs_type, scale, precision);
  VectorCmpExprFuncsHelper::get_cmp_set(meta, meta, null_first_cmp, null_last_cmp);
}
} // namespace common
} // namespace oceanbase
