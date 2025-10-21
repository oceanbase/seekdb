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

#ifndef OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_VECTOR_H_
#define OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_VECTOR_H_

#include "share/vector/ob_continuous_format.h"
#include "share/vector/vector_op_util.h"

namespace oceanbase
{
namespace common
{

template<typename BasicOp>
class ObContinuousVector final: public ObContinuousFormat
{
public:
  using VecTCBasicOp = BasicOp;
  using VectorType = ObContinuousVector<VecTCBasicOp>;
  using VecOpUtil = VectorOpUtil<VectorType>;

  ObContinuousVector(uint32_t *offsets, char *data, sql::ObBitVector *nulls)
    : ObContinuousFormat(offsets, data, nulls)
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

extern template class ObContinuousVector<VectorBasicOp<VEC_TC_NUMBER>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_EXTEND>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_STRING>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_ENUM_SET_INNER>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_RAW>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_ROWID>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_LOB>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_JSON>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_GEO>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_UDT>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_COLLECTION>>;
extern template class ObContinuousVector<VectorBasicOp<VEC_TC_ROARINGBITMAP>>;

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_VECTOR_H_
