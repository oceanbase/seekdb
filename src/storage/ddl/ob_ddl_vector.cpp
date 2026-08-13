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

#define USING_LOG_PREFIX STORAGE

#include "storage/ddl/ob_ddl_vector.h"
#include "storage/access/ob_table_param.h"
#include "storage/ddl/ob_ddl_continuous_vector.h"
#include "storage/ddl/ob_ddl_discrete_vector.h"
#include "storage/ddl/ob_ddl_fixed_length_vector.h"
#include "storage/ddl/ob_ddl_nullable_vector.h"
#include "storage/ddl/ob_ddl_vector_utils.h"

namespace oceanbase
{
namespace storage
{
using namespace share::schema;

int ObDDLVector::create_vector(VectorFormat format, VecValueTypeClass value_tc,
                                      bool is_nullable, const int64_t max_batch_size,
                                      ObIAllocator &allocator, ObDDLVector *&dl_vector)
{
  int ret = OB_SUCCESS;
  dl_vector = nullptr;
  ObIVector *vector = nullptr;
  switch (format) {
    case VEC_FIXED:
      if (OB_FAIL(ObDDLVectorUtils::new_vector(VEC_FIXED, value_tc, allocator, vector))) {
      } else if (OB_FAIL(
                   ObDDLVectorUtils::prepare_vector(vector, max_batch_size, allocator))) {
      } else {
        ObFixedLengthBase *fixed_vector = static_cast<ObFixedLengthBase *>(vector);
        switch (value_tc) {
#define FIXED_VECTOR_INIT_SWITCH(value_tc)                                                     \
  case value_tc: {                                                                             \
    using VecValueType = RTCType<value_tc>;                                                    \
    using FixedLengthVecType = ObDDLFixedLengthVector<VecValueType>;                    \
    using NullableVecType = ObDDLNullableVector<FixedLengthVecType, ObFixedLengthBase>; \
    if (is_nullable) {                                                                         \
      dl_vector = OB_NEWx(NullableVecType, &allocator, fixed_vector);                          \
    } else {                                                                                   \
      dl_vector = OB_NEWx(FixedLengthVecType, &allocator, fixed_vector);                       \
    }                                                                                          \
    break;                                                                                     \
  }
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_INTEGER);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_UINTEGER);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_FLOAT);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DOUBLE);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_FIXED_DOUBLE);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DATETIME);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DATE);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_TIME);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_YEAR);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_UNKNOWN);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_BIT);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TZ);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_TIMESTAMP_TINY);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_YM);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_INTERVAL_DS);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT32);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT64);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT128);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT256);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_DEC_INT512);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATETIME);
          FIXED_VECTOR_INIT_SWITCH(VEC_TC_MYSQL_DATE);
#undef FIXED_VECTOR_INIT_SWITCH
          default:
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected fixed vector value type class", KR(ret), K(value_tc));
            break;
        }
      }
      break;
    case VEC_CONTINUOUS:
      if (OB_FAIL(
            ObDDLVectorUtils::new_vector(VEC_CONTINUOUS, value_tc, allocator, vector))) {
      } else if (OB_FAIL(
                   ObDDLVectorUtils::prepare_vector(vector, max_batch_size, allocator))) {
      } else {
        if (is_nullable) {
          using NullableVecType =
            ObDDLNullableVector<ObDDLContinuousVector, ObContinuousBase>;
          dl_vector = OB_NEWx(NullableVecType, &allocator, static_cast<ObContinuousBase *>(vector));
        } else {
          dl_vector = OB_NEWx(ObDDLContinuousVector, &allocator,
                              static_cast<ObContinuousBase *>(vector));
        }
      }
      break;
    case VEC_DISCRETE:
      if (OB_FAIL(ObDDLVectorUtils::new_vector(VEC_DISCRETE, value_tc, allocator, vector))) {
      } else if (OB_FAIL(
                   ObDDLVectorUtils::prepare_vector(vector, max_batch_size, allocator))) {
      } else {
        if (is_nullable) {
          using NullableVecType =
            ObDDLNullableVector<ObDDLDiscreteVector, ObDiscreteBase>;
          dl_vector = OB_NEWx(NullableVecType, &allocator, static_cast<ObDiscreteBase *>(vector));
        } else {
          dl_vector =
            OB_NEWx(ObDDLDiscreteVector, &allocator, static_cast<ObDiscreteBase *>(vector));
        }
      }
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected vector format", KR(ret), K(format));
      break;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(nullptr == dl_vector)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new direct load vector", KR(ret), K(format));
  }
  return ret;
}

int ObDDLVector::create_vector(const ObColDesc &col_desc, bool is_nullable,
                                      const int64_t max_batch_size, ObIAllocator &allocator,
                                      ObDDLVector *&vector)
{
  return create_vector(col_desc.col_type_, is_nullable, max_batch_size, allocator, vector);
}

int ObDDLVector::create_vector(const common::ObObjMeta &col_type, bool is_nullable,
                                      const int64_t max_batch_size, ObIAllocator &allocator,
                                      ObDDLVector *&vector)
{
  int ret = OB_SUCCESS;
  const int16_t precision = col_type.is_decimal_int()
                              ? col_type.get_stored_precision()
                              : PRECISION_UNKNOWN_YET;
  VecValueTypeClass value_tc =
    get_vec_value_tc(col_type.get_type(), col_type.get_scale(), precision);
  const bool is_fixed = is_fixed_length_vec(value_tc);
  VectorFormat format = is_fixed ? VEC_FIXED : VEC_DISCRETE; // VEC_CONTINUOUS;
  if (OB_FAIL(create_vector(format, value_tc, is_nullable, max_batch_size, allocator, vector))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
