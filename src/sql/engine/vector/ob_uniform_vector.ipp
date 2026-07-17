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

#include "sql/engine/vector/ob_uniform_vector.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
namespace common
{
#define UNIFORM_VEC_TC_LIST(M)                                                                     \
  M(VEC_TC_NULL)                                                                                   \
  M(VEC_TC_INTEGER)                                                                                \
  M(VEC_TC_UINTEGER)                                                                               \
  M(VEC_TC_FLOAT)                                                                                  \
  M(VEC_TC_DOUBLE)                                                                                 \
  M(VEC_TC_FIXED_DOUBLE)                                                                           \
  M(VEC_TC_NUMBER)                                                                                 \
  M(VEC_TC_DATETIME)                                                                               \
  M(VEC_TC_DATE)                                                                                   \
  M(VEC_TC_TIME)                                                                                   \
  M(VEC_TC_YEAR)                                                                                   \
  M(VEC_TC_EXTEND)                                                                                 \
  M(VEC_TC_UNKNOWN)                                                                                \
  M(VEC_TC_STRING)                                                                                 \
  M(VEC_TC_BIT)                                                                                    \
  M(VEC_TC_ENUM_SET)                                                                               \
  M(VEC_TC_ENUM_SET_INNER)                                                                         \
  M(VEC_TC_TIMESTAMP_TZ)                                                                           \
  M(VEC_TC_TIMESTAMP_TINY)                                                                         \
  M(VEC_TC_RAW)                                                                                    \
  M(VEC_TC_INTERVAL_YM)                                                                            \
  M(VEC_TC_INTERVAL_DS)                                                                            \
  M(VEC_TC_ROWID)                                                                                  \
  M(VEC_TC_LOB)                                                                                    \
  M(VEC_TC_JSON)                                                                                   \
  M(VEC_TC_GEO)                                                                                    \
  M(VEC_TC_UDT)                                                                                    \
  M(VEC_TC_DEC_INT32)                                                                              \
  M(VEC_TC_DEC_INT64)                                                                              \
  M(VEC_TC_DEC_INT128)                                                                             \
  M(VEC_TC_DEC_INT256)                                                                             \
  M(VEC_TC_DEC_INT512)                                                                             \
  M(VEC_TC_COLLECTION)                                                                             \
  M(VEC_TC_ROARINGBITMAP)                                                                          \
  M(VEC_TC_MYSQL_DATE)                                                                             \
  M(VEC_TC_MYSQL_DATETIME)

template<typename BasicOp>
struct UniformBasicOpTraits;

template<VecValueTypeClass vec_tc>
struct UniformBasicOpTraits<VectorBasicOp<vec_tc>>
{
  static constexpr VecValueTypeClass value_tc = vec_tc;
};

template<typename HashMethod, bool hash_v2>
static HashFuncTypeForTc get_uniform_hash_func_by_tc(const VecValueTypeClass tc)
{
  HashFuncTypeForTc res_func = nullptr;
  switch (tc) {
#define GET_HASH_FUNC(vec_tc)                                                                      \
  case (vec_tc): {                                                                                 \
    res_func = VecTCHashCalc<vec_tc, HashMethod, hash_v2>::hash;                                   \
    break;                                                                                         \
  }
    UNIFORM_VEC_TC_LIST(GET_HASH_FUNC)
    case (MAX_VEC_TC):
    default: {
      res_func = VecTCHashCalc<MAX_VEC_TC, HashMethod, hash_v2>::hash;
      break;
    }
#undef GET_HASH_FUNC
  }
  return res_func;
}

template<typename HashMethod, bool hash_v2>
static NullHashFuncTypeForTc get_uniform_null_hash_func_by_tc(const VecValueTypeClass tc)
{
  NullHashFuncTypeForTc res_func = nullptr;
  switch (tc) {
#define GET_NULL_HASH_FUNC(vec_tc)                                                                 \
  case (vec_tc): {                                                                                 \
    res_func = VectorBasicOp<vec_tc>::template null_hash<HashMethod, hash_v2>;                     \
    break;                                                                                         \
  }
    UNIFORM_VEC_TC_LIST(GET_NULL_HASH_FUNC)
    case (MAX_VEC_TC):
    default: {
      res_func = VectorBasicOp<MAX_VEC_TC>::template null_hash<HashMethod, hash_v2>;
      break;
    }
#undef GET_NULL_HASH_FUNC
  }
  return res_func;
}

template<bool IS_CONST, typename HashMethod, bool hash_v2, typename HashResIter>
static int uniform_hash_dispatch(const ObUniformFormat<IS_CONST> &vec,
                                 const VecValueTypeClass tc,
                                 HashResIter &hash_values,
                                 const ObObjMeta &meta,
                                 const sql::ObBitVector &skip,
                                 const sql::EvalBound &bound,
                                 const uint64_t *seeds,
                                 const bool is_batch_seed)
{
  int ret = OB_SUCCESS;
  HashFuncTypeForTc hash_func = get_uniform_hash_func_by_tc<HashMethod, hash_v2>(tc);
  NullHashFuncTypeForTc null_hash_func = get_uniform_null_hash_func_by_tc<HashMethod, hash_v2>(tc);
  if (OB_ISNULL(hash_func) || OB_ISNULL(null_hash_func)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "unexpected null hash func", K(ret), K(tc));
  } else if (!vec.has_null() && bound.get_all_rows_active()) {
    for (int64_t i = bound.start(); OB_SUCC(ret) && i < bound.end(); i++) {
      const uint64_t seed = is_batch_seed ? seeds[i] : seeds[0];
      ret = hash_func(meta, vec.get_payload(i), vec.get_length(i), seed, hash_values[i]);
    }
  } else if (vec.has_null() && bound.get_all_rows_active()) {
    for (int64_t i = bound.start(); OB_SUCC(ret) && i < bound.end(); i++) {
      const uint64_t seed = is_batch_seed ? seeds[i] : seeds[0];
      if (vec.is_null(i)) {
        ret = null_hash_func(seed, hash_values[i]);
      } else {
        ret = hash_func(meta, vec.get_payload(i), vec.get_length(i), seed, hash_values[i]);
      }
    }
  } else if (!vec.has_null() && !bound.get_all_rows_active()) {
    auto op = [&](const int64_t i) __attribute__((always_inline)) {
      const uint64_t seed = is_batch_seed ? seeds[i] : seeds[0];
      return hash_func(meta, vec.get_payload(i), vec.get_length(i), seed, hash_values[i]);
    };
    ret = sql::ObBitVector::flip_foreach(skip, bound, op);
  } else {
    auto op = [&](const int64_t i) __attribute__((always_inline)) {
      int ret = OB_SUCCESS;
      const uint64_t seed = is_batch_seed ? seeds[i] : seeds[0];
      if (vec.is_null(i)) {
        ret = null_hash_func(seed, hash_values[i]);
      } else {
        ret = hash_func(meta, vec.get_payload(i), vec.get_length(i), seed, hash_values[i]);
      }
      return ret;
    };
    ret = sql::ObBitVector::flip_foreach(skip, bound, op);
  }
  return ret;
}

template<bool IS_CONST>
static int uniform_null_first_cmp(const ObUniformFormat<IS_CONST> &vec,
                                  VECTOR_ONE_COMPARE_ARGS)
{
  return expr.basic_funcs_->null_first_cmp_(vec.get_datum(row_idx), ObDatum(r_v, r_len, r_null),
                                            cmp_ret);
}

template<bool IS_CONST>
static int uniform_null_last_cmp(const ObUniformFormat<IS_CONST> &vec,
                                 VECTOR_ONE_COMPARE_ARGS)
{
  return expr.basic_funcs_->null_last_cmp_(vec.get_datum(row_idx), ObDatum(r_v, r_len, r_null),
                                           cmp_ret);
}

template<bool IS_CONST>
static int uniform_no_null_cmp(const ObUniformFormat<IS_CONST> &vec,
                               VECTOR_NOT_NULL_COMPARE_ARGS)
{
  return expr.basic_funcs_->null_last_cmp_(vec.get_datum(row_idx1), vec.get_datum(row_idx2),
                                           cmp_ret);
}

template<bool IS_CONST, bool NULL_FIRST>
static int uniform_mul_cmp(const ObUniformFormat<IS_CONST> &vec,
                           VECTOR_MUL_COMPARE_ARGS)
{
  int ret = OB_SUCCESS;
  cmp_ret = 0;
  uint16_t start_idx = bound.start();
  uint16_t end_idx = bound.end();
  for (int64_t row_idx = start_idx; OB_SUCC(ret) && 0 == cmp_ret && row_idx < end_idx; row_idx++) {
    if (skip.at(row_idx)) {
      continue;
    } else if (NULL_FIRST && OB_FAIL(uniform_null_first_cmp(vec, expr, row_idx, r_null, r_v,
                                                            r_len, cmp_ret))) {
      COMMON_LOG(WARN, "failed to compare", K(ret));
    } else if (!NULL_FIRST && OB_FAIL(uniform_null_last_cmp(vec, expr, row_idx, r_null, r_v,
                                                            r_len, cmp_ret))) {
      COMMON_LOG(WARN, "failed to compare", K(ret));
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

template<bool IS_CONST, bool NO_NULL>
static int uniform_cmp_batch_rows(const ObUniformFormat<IS_CONST> &vec,
                                  VECTOR_COMPARE_BATCH_ROWS_ARGS)
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
      const bool r_null = NO_NULL ? false : rows[i]->is_null(row_col_idx);
      if (OB_FAIL(uniform_null_first_cmp(vec, expr, batch_idx, r_null, r_v, r_len, cmp_ret[i]))) {
        COMMON_LOG(WARN, "failed to compare", K(ret));
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sel_cnt; i++) {
      uint16_t batch_idx = sel[i];
      rows[i]->get_cell_payload(row_meta, row_col_idx, r_v, r_len);
      const bool r_null = NO_NULL ? false : rows[i]->is_null(row_col_idx);
      if (OB_FAIL(uniform_null_first_cmp(vec, expr, batch_idx, r_null, r_v, r_len, cmp_ret[i]))) {
        COMMON_LOG(WARN, "failed to compare", K(ret));
      }
    }
  }
  return ret;
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::default_hash(BATCH_EVAL_HASH_ARGS) const
{
  BatchHashResIter hash_iter(hash_values);
  return uniform_hash_dispatch<IS_CONST, ObDefaultHash, false, BatchHashResIter>(
    *this, UniformBasicOpTraits<BasicOp>::value_tc, hash_iter, expr.obj_meta_, skip, bound, seeds,
    is_batch_seed);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::murmur_hash(BATCH_EVAL_HASH_ARGS) const
{
  BatchHashResIter hash_iter(hash_values);
  return uniform_hash_dispatch<IS_CONST, ObMurmurHash, false, BatchHashResIter>(
    *this, UniformBasicOpTraits<BasicOp>::value_tc, hash_iter, expr.obj_meta_, skip, bound, seeds,
    is_batch_seed);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::murmur_hash_v3(BATCH_EVAL_HASH_ARGS) const
{
  BatchHashResIter hash_iter(hash_values);
  return uniform_hash_dispatch<IS_CONST, ObMurmurHash, true, BatchHashResIter>(
    *this, UniformBasicOpTraits<BasicOp>::value_tc, hash_iter, expr.obj_meta_, skip, bound, seeds,
    is_batch_seed);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::murmur_hash_v3_for_one_row(EVAL_HASH_ARGS_FOR_ROW) const
{

  RowHashResIter hash_iter(&hash_value);
  sql::EvalBound bound(batch_size, batch_idx, batch_idx + 1, true);
  char mock_skip_data[1] = {0};
  sql::ObBitVector &skip = *sql::to_bit_vector(mock_skip_data);
  return uniform_hash_dispatch<IS_CONST, ObMurmurHash, true, RowHashResIter>(
    *this, UniformBasicOpTraits<BasicOp>::value_tc, hash_iter, expr.obj_meta_, skip, bound, &seed,
    false);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::null_first_cmp(VECTOR_ONE_COMPARE_ARGS) const
{
  return uniform_null_first_cmp(*this, expr, row_idx, r_null, r_v, r_len, cmp_ret);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::null_last_cmp(VECTOR_ONE_COMPARE_ARGS) const
{
  return uniform_null_last_cmp(*this, expr, row_idx, r_null, r_v, r_len, cmp_ret);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::no_null_cmp(VECTOR_NOT_NULL_COMPARE_ARGS) const
{
  return uniform_no_null_cmp(*this, expr, row_idx1, row_idx2, cmp_ret);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::null_first_mul_cmp(VECTOR_MUL_COMPARE_ARGS) const
{
  return uniform_mul_cmp<IS_CONST, true>(*this, expr, skip, bound, r_null, r_v, r_len,
                                         diff_row_idx, cmp_ret);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::null_last_mul_cmp(VECTOR_MUL_COMPARE_ARGS) const
{
  return uniform_mul_cmp<IS_CONST, false>(*this, expr, skip, bound, r_null, r_v, r_len,
                                          diff_row_idx, cmp_ret);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::null_first_cmp_batch_rows(VECTOR_COMPARE_BATCH_ROWS_ARGS) const
{
  return uniform_cmp_batch_rows<IS_CONST, false>(*this, expr, sel, sel_cnt, rows, row_col_idx,
                                                 row_meta, cmp_ret);
}

template<bool IS_CONST, typename BasicOp>
int ObUniformVector<IS_CONST, BasicOp>::no_null_cmp_batch_rows(VECTOR_COMPARE_BATCH_ROWS_ARGS) const
{
  return uniform_cmp_batch_rows<IS_CONST, true>(*this, expr, sel, sel_cnt, rows, row_col_idx,
                                                row_meta, cmp_ret);
}
#undef UNIFORM_VEC_TC_LIST
} // end namespace common
} // end namespace oceanbase
