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

#ifndef OCEANBASE_SHARE_VECTOR_OB_DISCRETE_VECTOR_H_
#define OCEANBASE_SHARE_VECTOR_OB_DISCRETE_VECTOR_H_

#include "share/vector/ob_discrete_format.h"
#include "share/vector/vector_op_util.h"

namespace oceanbase
{
namespace common
{

template<typename BasicOp>
class ObDiscreteVector final: public ObDiscreteFormat
{
public:
  using VecTCBasicOp = BasicOp;
  using VectorType = ObDiscreteVector<VecTCBasicOp>;
  using VecOpUtil = VectorOpUtil<VectorType>;

  ObDiscreteVector(int32_t *lens, char **ptrs, sql::ObBitVector *nulls)
    : ObDiscreteFormat(lens, ptrs, nulls)
  {}

  int default_hash(BATCH_EVAL_HASH_ARGS) const override;
  int murmur_hash(BATCH_EVAL_HASH_ARGS) const override;
  int murmur_hash_v3(BATCH_EVAL_HASH_ARGS) const override;

  int murmur_hash_v3_for_one_row(EVAL_HASH_ARGS_FOR_ROW) const override;
  int null_first_cmp(VECTOR_ONE_COMPARE_ARGS) const override;
  int null_last_cmp(VECTOR_ONE_COMPARE_ARGS) const override;
  int no_null_cmp(VECTOR_NOT_NULL_COMPARE_ARGS) const override final;
  int null_first_mul_cmp(VECTOR_MUL_COMPARE_ARGS) const override final;
  int null_last_mul_cmp(VECTOR_MUL_COMPARE_ARGS) const override final;
  int null_first_cmp_batch_rows(VECTOR_COMPARE_BATCH_ROWS_ARGS) const override;
  int no_null_cmp_batch_rows(VECTOR_COMPARE_BATCH_ROWS_ARGS) const override;
};

#ifdef _WIN32
// On Windows, provide template definitions in the header so every
// translation unit can instantiate the required specializations.
template<typename BasicOp>
int ObDiscreteVector<BasicOp>::default_hash(BATCH_EVAL_HASH_ARGS) const
{
  BatchHashResIter hash_iter(hash_values);
  return VecOpUtil::template hash_dispatch<ObDefaultHash, false, BatchHashResIter>(
    hash_iter, expr.obj_meta_, *this, skip, bound, seeds, is_batch_seed);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::murmur_hash(BATCH_EVAL_HASH_ARGS) const
{
  BatchHashResIter hash_iter(hash_values);
  return VecOpUtil::template hash_dispatch<ObMurmurHash, false, BatchHashResIter>(
    hash_iter, expr.obj_meta_, *this, skip, bound, seeds, is_batch_seed);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::murmur_hash_v3(BATCH_EVAL_HASH_ARGS) const
{
  BatchHashResIter hash_iter(hash_values);
  return VecOpUtil::template hash_dispatch<ObMurmurHash, true, BatchHashResIter>(
    hash_iter, expr.obj_meta_, *this, skip, bound, seeds, is_batch_seed);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::murmur_hash_v3_for_one_row(EVAL_HASH_ARGS_FOR_ROW) const
{
  RowHashResIter hash_iter(&hash_value);
  sql::EvalBound bound(batch_size, batch_idx, batch_idx + 1, true);
  char mock_skip_data[1] = {0};
  sql::ObBitVector &skip = *sql::to_bit_vector(mock_skip_data);
  return VecOpUtil::template hash_dispatch<ObMurmurHash, true, RowHashResIter>(
    hash_iter, expr.obj_meta_, *this, skip, bound, &seed, false);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::null_first_cmp(VECTOR_ONE_COMPARE_ARGS) const
{
  return VecOpUtil::template ns_cmp<true>(expr.obj_meta_, *this, row_idx, r_null, r_v, r_len, cmp_ret);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::null_last_cmp(VECTOR_ONE_COMPARE_ARGS) const
{
  return VecOpUtil::template ns_cmp<false>(expr.obj_meta_, *this, row_idx, r_null, r_v, r_len, cmp_ret);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::no_null_cmp(VECTOR_NOT_NULL_COMPARE_ARGS) const
{
  return VecOpUtil::Op::cmp(
    expr.obj_meta_,
    get_payload(row_idx1),
    get_length(row_idx1),
    get_payload(row_idx2),
    get_length(row_idx2),
    cmp_ret);
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::null_first_mul_cmp(VECTOR_MUL_COMPARE_ARGS) const
{
  int ret = OB_SUCCESS;
  cmp_ret = 0;
  uint16_t start_idx = bound.start();
  uint16_t end_idx = bound.end();
  for (int64_t row_idx = start_idx; OB_SUCC(ret) && 0 == cmp_ret && row_idx < end_idx; row_idx++) {
    if (skip.at(row_idx)) {
      continue;
    } else if (OB_FAIL(null_first_cmp(expr, row_idx, r_null, r_v, r_len, cmp_ret))) {
      // logging is omitted in header-only Windows specialization
    } else if (0 != cmp_ret) {
      diff_row_idx = row_idx;
      break;
    }
  }
  if (0 == cmp_ret) {
    diff_row_idx = end_idx;
  }
  return ret;
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::null_last_mul_cmp(VECTOR_MUL_COMPARE_ARGS) const
{
  int ret = OB_SUCCESS;
  cmp_ret = 0;
  uint16_t start_idx = bound.start();
  uint16_t end_idx = bound.end();
  for (int64_t row_idx = start_idx; OB_SUCC(ret) && 0 == cmp_ret && row_idx < end_idx; row_idx++) {
    if (skip.at(row_idx)) {
      continue;
    } else if (OB_FAIL(null_last_cmp(expr, row_idx, r_null, r_v, r_len, cmp_ret))) {
      // logging is omitted in header-only Windows specialization
    } else if (0 != cmp_ret) {
      diff_row_idx = row_idx;
      break;
    }
  }
  if (0 == cmp_ret) {
    diff_row_idx = end_idx;
  }
  return ret;
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::null_first_cmp_batch_rows(VECTOR_COMPARE_BATCH_ROWS_ARGS) const
{
  int ret = OB_SUCCESS;
  ObLength r_len = 0;
  const char *r_v = NULL;
  int32_t fixed_offset = 0;
  const bool is_fixed_length = row_meta.is_reordered_fixed_expr(row_col_idx);
  if (is_fixed_length) {
    fixed_offset = row_meta.get_fixed_cell_offset(row_col_idx);
    r_len = row_meta.fixed_length(row_col_idx);
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_cnt; i++) {
      uint16_t batch_idx = sel[i];
      r_v = rows[i]->payload() + fixed_offset;
      if (OB_FAIL(null_first_cmp(
              expr, batch_idx, rows[i]->is_null(row_col_idx), r_v, r_len, cmp_ret[i]))) {
        // logging is omitted in header-only Windows specialization
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_cnt; i++) {
      uint16_t batch_idx = sel[i];
      rows[i]->get_cell_payload(row_meta, row_col_idx, r_v, r_len);
      if (OB_FAIL(null_first_cmp(
              expr, batch_idx, rows[i]->is_null(row_col_idx), r_v, r_len, cmp_ret[i]))) {
        // logging is omitted in header-only Windows specialization
      }
    }
  }
  return ret;
}

template<typename BasicOp>
int ObDiscreteVector<BasicOp>::no_null_cmp_batch_rows(VECTOR_COMPARE_BATCH_ROWS_ARGS) const
{
  int ret = OB_SUCCESS;
  ObLength r_len = 0;
  const char *r_v = NULL;
  int32_t fixed_offset = 0;
  const bool is_fixed_length = row_meta.is_reordered_fixed_expr(row_col_idx);
  if (is_fixed_length) {
    fixed_offset = row_meta.get_fixed_cell_offset(row_col_idx);
    r_len = row_meta.fixed_length(row_col_idx);
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_cnt; i++) {
      uint16_t batch_idx = sel[i];
      r_v = rows[i]->payload() + fixed_offset;
      if (OB_FAIL(VecOpUtil::Op::cmp(expr.obj_meta_,
              get_payload(batch_idx),
              get_length(batch_idx),
              r_v,
              r_len,
              cmp_ret[i]))) {
        // logging is omitted in header-only Windows specialization
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_cnt; i++) {
      uint16_t batch_idx = sel[i];
      rows[i]->get_cell_payload(row_meta, row_col_idx, r_v, r_len);
      if (OB_FAIL(VecOpUtil::Op::cmp(expr.obj_meta_,
              get_payload(batch_idx),
              get_length(batch_idx),
              r_v,
              r_len,
              cmp_ret[i]))) {
        // logging is omitted in header-only Windows specialization
      }
    }
  }
  return ret;
}
#endif

#ifndef _WIN32
// On non-Windows platforms we use explicit instantiations in
// `ob_discrete_vector.cpp` to reduce code size.
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_NUMBER>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_EXTEND>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_STRING>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_ENUM_SET_INNER>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_RAW>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_ROWID>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_LOB>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_JSON>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_GEO>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_UDT>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_COLLECTION>>;
extern template class ObDiscreteVector<VectorBasicOp<VEC_TC_ROARINGBITMAP>>;
#endif

} // end namespace common
} // end namespace oceanbase
#endif // OCEANBASE_SHARE_VECTOR_OB_DISCRETE_VECTOR_H_
