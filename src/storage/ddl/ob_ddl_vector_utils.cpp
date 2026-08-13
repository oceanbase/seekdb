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

#include "storage/ddl/ob_ddl_vector_utils.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "storage/access/ob_table_param.h"
#include "query/engine/vector/ob_continuous_base.h"
#include "query/engine/vector/ob_discrete_base.h"
#include "query/engine/vector/ob_fixed_length_base.h"
#include "query/engine/vector/ob_uniform_base.h"
#include "storage/blocksstable/ob_storage_datum.h"
#include "storage/ddl/ob_ddl_batch_rows.h"
#include "lib/charset/ob_charset.h"

namespace oceanbase
{
namespace storage
{
using namespace blocksstable;
using namespace common;
using namespace share;
using namespace share::schema;
using namespace sql;

int ObDDLVectorUtils::new_vector(VectorFormat format, VecValueTypeClass value_tc,
                                        ObIAllocator &allocator, ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  switch (format) {
    case VEC_FIXED: {
      ret = new_vector_fixed(value_tc, allocator, vector);
      break;
    }
    case VEC_CONTINUOUS: {
      ret = new_vector_continuous(value_tc, allocator, vector);
      break;
    }
    case VEC_DISCRETE: {
      ret = new_vector_discrete(value_tc, allocator, vector);
      break;
    }
    case VEC_UNIFORM: {
      ret = new_vector_uniform(value_tc, allocator, vector);
      break;
    }
    case VEC_UNIFORM_CONST: {
      ret = new_vector_uniform_const(value_tc, allocator, vector);
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected vector format", KR(ret), K(format));
      break;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(vector)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc vector", KR(ret));
  }
  return ret;
}

int ObDDLVectorUtils::prepare_vector(ObIVector *vector, const int64_t max_batch_size,
                                            ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == vector || max_batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(vector), K(max_batch_size));
  } else {
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_FIXED: {
        ObFixedLengthBase *fixed_vec = static_cast<ObFixedLengthBase *>(vector);
        const ObLength length = fixed_vec->get_length();
        const int64_t nulls_size = ObBitVector::memory_size(max_batch_size);
        const int64_t data_size = length * max_batch_size;
        ObBitVector *nulls = nullptr;
        char *data = nullptr;
        if (OB_ISNULL(nulls = to_bit_vector(allocator.alloc(nulls_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(nulls_size));
        } else if (OB_ISNULL(data = static_cast<char *>(allocator.alloc(data_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(data_size));
        } else {
          nulls->reset(max_batch_size);
          MEMSET(data, 0, data_size);
          fixed_vec->set_nulls(nulls);
          fixed_vec->set_data(data);
        }
        break;
      }
      case VEC_CONTINUOUS: {
        ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
        const int64_t nulls_size = ObBitVector::memory_size(max_batch_size);
        const int64_t offsets_size = sizeof(uint32_t) * (max_batch_size + 1);
        ObBitVector *nulls = nullptr;
        uint32_t *offsets = nullptr;
        if (OB_ISNULL(nulls = to_bit_vector(allocator.alloc(nulls_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(nulls_size));
        } else if (OB_ISNULL(offsets = static_cast<uint32_t *>(allocator.alloc(offsets_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(offsets_size));
        } else {
          nulls->reset(max_batch_size);
          MEMSET(offsets, 0, offsets_size);
          continuous_vec->set_nulls(nulls);
          continuous_vec->set_offsets(offsets);
        }
        break;
      }
      case VEC_DISCRETE: {
        ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
        const int64_t nulls_size = ObBitVector::memory_size(max_batch_size);
        const int64_t lens_size = sizeof(int32_t) * max_batch_size;
        const int64_t ptrs_size = sizeof(char *) * max_batch_size;
        ObBitVector *nulls = nullptr;
        int32_t *lens = nullptr;
        char **ptrs = nullptr;
        if (OB_ISNULL(nulls = to_bit_vector(allocator.alloc(nulls_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(nulls_size));
        } else if (OB_ISNULL(lens = static_cast<int32_t *>(allocator.alloc(lens_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(lens_size));
        } else if (OB_ISNULL(ptrs = static_cast<char **>(allocator.alloc(ptrs_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(ptrs_size));
        } else {
          nulls->reset(max_batch_size);
          MEMSET(lens, 0, lens_size);
          MEMSET(ptrs, 0, ptrs_size);
          discrete_vec->set_nulls(nulls);
          discrete_vec->set_lens(lens);
          discrete_vec->set_ptrs(ptrs);
        }
        break;
      }
      case VEC_UNIFORM: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        const int64_t datums_size = sizeof(ObDatum) * max_batch_size;
        ObDatum *datums = nullptr;
        if (OB_ISNULL(datums = static_cast<ObDatum *>(allocator.alloc(datums_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(datums_size));
        } else {
          uniform_vec->set_datums(datums);
        }
        break;
      }
      case VEC_UNIFORM_CONST: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        const int64_t datums_size = sizeof(ObDatum);
        ObDatum *datums = nullptr;
        if (OB_ISNULL(datums = static_cast<ObDatum *>(allocator.alloc(datums_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc mem", KR(ret), K(datums_size));
        } else {
          uniform_vec->set_datums(datums);
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected vector format", KR(ret), K(format));
        break;
    }
  }
  return ret;
}

int ObDDLVectorUtils::to_datum(ObIVector *vector, const int64_t idx, ObDatum &datum)
{
  int ret = OB_SUCCESS;
  datum.reset();
  if (OB_UNLIKELY(nullptr == vector || idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(vector), K(idx));
  } else {
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_FIXED: {
        ObFixedLengthBase *fixed_vec = static_cast<ObFixedLengthBase *>(vector);
        if (fixed_vec->is_null(idx)) {
          datum.set_null();
        } else {
          datum.len_ = fixed_vec->get_length();
          datum.ptr_ = fixed_vec->get_data() + datum.len_ * idx;
        }
        break;
      }
      case VEC_CONTINUOUS: {
        ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
        if (continuous_vec->is_null(idx)) {
          datum.set_null();
        } else {
          const uint32_t offset1 = continuous_vec->get_offsets()[idx];
          const uint32_t offset2 = continuous_vec->get_offsets()[idx + 1];
          datum.ptr_ = continuous_vec->get_data() + offset1;
          datum.len_ = (offset2 - offset1);
        }
        break;
      }
      case VEC_DISCRETE: {
        ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
        if (discrete_vec->is_null(idx)) {
          datum.set_null();
        } else {
          datum.len_ = discrete_vec->get_lens()[idx];
          datum.ptr_ = discrete_vec->get_ptrs()[idx];
        }
        break;
      }
      case VEC_UNIFORM: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        datum = uniform_vec->get_datums()[idx];
        break;
      }
      case VEC_UNIFORM_CONST: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        datum = uniform_vec->get_datums()[0];
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected vector format", KR(ret), K(format));
        break;
    }
  }
  return ret;
}

int ObDDLVectorUtils::reshape_storage_vector(const ObObjMeta &col_type,
                                             const ObAccuracy &col_accuracy,
                                             ObIAllocator &allocator,
                                             ObIVector *&vector,
                                             ObBatchSelector &selector)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(vector) || !selector.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid storage vector reshape arguments", KR(ret), KP(vector), K(selector));
  } else if (!col_type.is_binary() && !col_type.is_fixed_len_char_type()) {
    // No storage normalization is needed for other column types.
  } else {
    const VectorFormat format = vector->get_format();
    ObIVector *result_vector = vector;
    if (VEC_CONTINUOUS == format) {
      const VecValueTypeClass value_tc = get_vec_value_tc(
          col_type.get_type(), col_type.get_scale(), col_type.get_stored_precision());
      if (OB_FAIL(new_vector(VEC_DISCRETE, value_tc, allocator, result_vector))) {
      } else if (OB_FAIL(prepare_vector(result_vector, selector.get_max(), allocator))) {
      }
    } else if (VEC_DISCRETE != format
               && VEC_UNIFORM != format
               && VEC_UNIFORM_CONST != format) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected storage vector format", KR(ret), K(format), K(col_type));
    }

    int64_t idx = 0;
    while (OB_SUCC(ret) && OB_SUCC(selector.get_next(idx))) {
      if (vector->is_null(idx)) {
        if (result_vector != vector) {
          result_vector->set_null(idx);
        }
      } else {
        const char *payload = nullptr;
        ObLength length = 0;
        vector->get_payload(idx, payload, length);
        const char *normalized_payload = payload;
        ObLength normalized_length = length;
        if (col_type.is_binary()) {
          const int32_t binary_len = col_accuracy.get_length();
          if (length < binary_len) {
            char *padded_payload = static_cast<char *>(allocator.alloc(binary_len));
            if (OB_ISNULL(padded_payload)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("failed to allocate binary padding", KR(ret), K(binary_len));
            } else {
              MEMCPY(padded_payload, payload, length);
              MEMSET(padded_payload + length, '\0', binary_len - length);
              normalized_payload = padded_payload;
              normalized_length = binary_len;
            }
          }
        } else {
          const ObString space_pattern =
              ObCharsetUtils::get_const_str(col_type.get_collation_type(), ' ');
          while (normalized_length >= space_pattern.length()
                 && 0 == MEMCMP(payload + normalized_length - space_pattern.length(),
                                space_pattern.ptr(), space_pattern.length())) {
            normalized_length -= space_pattern.length();
          }
        }
        if (OB_SUCC(ret)
            && (result_vector != vector || normalized_payload != payload
                || normalized_length != length)) {
          result_vector->set_payload_shallow(idx, normalized_payload, normalized_length);
        }
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret) && result_vector != vector) {
      vector = result_vector;
    }
  }
  return ret;
}

int ObDDLVectorUtils::check_rowkey_length(const ObDDLBatchRows &batch_rows,
                                                 const int64_t rowkey_column_count,
                                                 const common::ObIArray<share::schema::ObColDesc> &col_descs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(batch_rows.empty() || batch_rows.get_column_count() < rowkey_column_count ||
                  rowkey_column_count <= 0 || col_descs.count() < rowkey_column_count)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(batch_rows), K(rowkey_column_count), K(col_descs.count()));
  } else {
    int64_t *rowkey_len = nullptr;
    const int64_t row_count = batch_rows.size();
    if (OB_ISNULL(rowkey_len = static_cast<int64_t *>(
                    ob_malloc(sizeof(int64_t) * row_count, "DDL_CheckRK")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", KR(ret), K(rowkey_len));
    } else {
      memset(rowkey_len, 0, sizeof(int64_t) * row_count);
      for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < rowkey_column_count; col_idx++) {
        ObDDLVector *vector = batch_rows.get_vectors().at(col_idx);
        const share::schema::ObColDesc &col_desc = col_descs.at(col_idx);

        if (col_desc.col_type_.is_lob_storage()) {
          // For LOB columns, use the new sum_lob_length method
          if (OB_FAIL(vector->sum_lob_length(rowkey_len, row_count))) {
          }
        } else {
          // For non-LOB columns, use the existing sum_bytes_usage method
          vector->sum_bytes_usage(rowkey_len, row_count);
        }
      }
    }
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_count; row_idx++) {
      if (rowkey_len[row_idx] > OB_MAX_VARCHAR_LENGTH_KEY) {
        ret = OB_ERR_TOO_LONG_KEY_LENGTH;
        LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
        LOG_WARN("rowkey is too long", K(ret), K(row_idx), K(rowkey_len[row_idx]));
      }
    }
    if (OB_NOT_NULL(rowkey_len)) {
      ob_free(rowkey_len);
    }
  }
  return ret;
}

/**
 * tablet id vector
 */

int ObDDLVectorUtils::make_const_tablet_id_vector(const ObTabletID &tablet_id,
                                                         ObIAllocator &allocator,
                                                         ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(tablet_id));
  } else {
    if (OB_FAIL(new_vector(VEC_UNIFORM_CONST, tablet_id_value_tc, allocator, vector))) {
    } else {
      ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
      ObStorageDatum *storage_datum = nullptr;
      if (OB_ISNULL(storage_datum = OB_NEWx(ObStorageDatum, &allocator))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to new ObStorageDatum", KR(ret));
      } else {
        storage_datum->set_uint(tablet_id.id());
        uniform_vec->set_datums(storage_datum);
      }
    }
  }
  return ret;
}

ObTabletID ObDDLVectorUtils::get_tablet_id(ObIVector *vec, const int64_t batch_idx)
{
  ObTabletID tablet_id;
  if (OB_NOT_NULL(vec)) {
    const VectorFormat format = vec->get_format();
    switch (format) {
      case VEC_FIXED:
        tablet_id = reinterpret_cast<const uint64_t *>(
          static_cast<ObFixedLengthBase *>(vec)->get_data())[batch_idx];
        break;
      case VEC_UNIFORM:
        tablet_id = static_cast<ObUniformBase *>(vec)->get_datums()[batch_idx].get_uint();
        break;
      case VEC_UNIFORM_CONST:
        tablet_id = static_cast<ObUniformBase *>(vec)->get_datums()[0].get_uint();
        break;
      default:
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected vector format", K(format));
        break;
    }
  }
  return tablet_id;
}

bool ObDDLVectorUtils::check_all_tablet_id_is_same(const uint64_t *tablet_ids,
                                                          const int64_t size)
{
  bool is_same = true;
  for (int64_t i = 1; i < size; ++i) {
    if (tablet_ids[i] != tablet_ids[0]) {
      is_same = false;
      break;
    }
  }
  return is_same;
}

bool ObDDLVectorUtils::check_is_same_tablet_id(const ObTabletID &tablet_id,
                                                      ObIVector *vector, const int64_t size)
{
  bool is_same = true;
  if (nullptr != vector) {
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_FIXED: {
        ObFixedLengthBase *fixed_vec = static_cast<ObFixedLengthBase *>(vector);
        const uint64_t *tablet_ids = reinterpret_cast<const uint64_t *>(fixed_vec->get_data());
        for (int64_t i = 0; i < size; ++i) {
          if (tablet_ids[i] != tablet_id.id()) {
            is_same = false;
            break;
          }
        }
        break;
      }
      case VEC_UNIFORM: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        const ObDatum *datums = uniform_vec->get_datums();
        for (int64_t i = 0; i < size; ++i) {
          if (datums[i].get_uint() != tablet_id.id()) {
            is_same = false;
            break;
          }
        }
        break;
      }
      case VEC_UNIFORM_CONST: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        const ObDatum &datum = uniform_vec->get_datums()[0];
        is_same = (datum.get_uint() == tablet_id.id());
        break;
      }
      default:
        is_same = false;
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected vector format", K(format));
        break;
    }
  }
  return is_same;
}

bool ObDDLVectorUtils::check_is_same_tablet_id(const ObTabletID &tablet_id,
                                                      ObIVector *vector, const uint16_t *selector,
                                                      const int64_t size)
{
  bool is_same = true;
  if (nullptr != vector) {
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_FIXED: {
        ObFixedLengthBase *fixed_vec = static_cast<ObFixedLengthBase *>(vector);
        const uint64_t *tablet_ids = reinterpret_cast<const uint64_t *>(fixed_vec->get_data());
        for (int64_t i = 0; i < size; ++i) {
          const uint16_t idx = selector[i];
          if (tablet_ids[idx] != tablet_id.id()) {
            is_same = false;
            break;
          }
        }
        break;
      }
      case VEC_UNIFORM: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        const ObDatum *datums = uniform_vec->get_datums();
        for (int64_t i = 0; i < size; ++i) {
          const uint16_t idx = selector[i];
          if (datums[idx].get_uint() != tablet_id.id()) {
            is_same = false;
            break;
          }
        }
        break;
      }
      case VEC_UNIFORM_CONST: {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        const ObDatum &datum = uniform_vec->get_datums()[0];
        is_same = (datum.get_uint() == tablet_id.id());
        break;
      }
      default:
        is_same = false;
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected vector format", K(format));
        break;
    }
  }
  return is_same;
}

bool ObDDLVectorUtils::check_is_same_tablet_id(const ObTabletID &tablet_id,
                                                      const ObDatumVector &datum_vec,
                                                      const int64_t size)
{
  bool is_same = true;
  if (nullptr != datum_vec.datums_) {
    if (datum_vec.is_batch()) {
      for (int64_t i = 0; i < size; ++i) {
        if (datum_vec.datums_[i].get_uint() != tablet_id.id()) {
          is_same = false;
          break;
        }
      }
    } else {
      const ObDatum &datum = datum_vec.datums_[0];
      is_same = (datum.get_uint() == tablet_id.id());
    }
  }
  return is_same;
}

bool ObDDLVectorUtils::check_is_same_tablet_id(const ObTabletID &tablet_id,
                                                      const ObDatumVector &datum_vec,
                                                      const uint16_t *selector, const int64_t size)
{
  bool is_same = true;
  if (nullptr != datum_vec.datums_) {
    if (datum_vec.is_batch()) {
      for (int64_t i = 0; i < size; ++i) {
        const uint16_t idx = selector[i];
        if (datum_vec.datums_[idx].get_uint() != tablet_id.id()) {
          is_same = false;
          break;
        }
      }
    } else {
      const ObDatum &datum = datum_vec.datums_[0];
      is_same = (datum.get_uint() == tablet_id.id());
    }
  }
  return is_same;
}

/**
 * hidden pk vector
 */

int ObDDLVectorUtils::batch_fill_hidden_pk(ObIVector *vector, const int64_t start,
                                                  const int64_t size,
                                                  ObTabletCacheInterval &pk_interval)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == vector || start < 0 || size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(vector), K(start), K(size));
  } else if (size > 0) {
    uint64_t start_value = OB_INVALID_ID;
    if (1 == size) {
      if (OB_FAIL(pk_interval.next_value(start_value))) {
        LOG_WARN("fail to get next value", KR(ret), K(pk_interval));
        ret = OB_ERR_UNEXPECTED; // rewrite error code
      }
    } else {
      ObTabletCacheInterval batch_pk;
      if (OB_FAIL(pk_interval.fetch(size, batch_pk))) {
        LOG_WARN("fail to fetch pk interval", KR(ret), K(pk_interval), K(size));
        ret = OB_ERR_UNEXPECTED; // rewrite error code
      } else if (OB_FAIL(batch_pk.get_value(start_value))) {
        LOG_WARN("fail to get value", KR(ret), K(batch_pk));
        ret = OB_ERR_UNEXPECTED; // rewrite error code
      }
    }
    if (OB_SUCC(ret)) {
      const VectorFormat format = vector->get_format();
      switch (format) {
        case VEC_FIXED: {
          ObFixedLengthBase *fixed_vec = static_cast<ObFixedLengthBase *>(vector);
          if (OB_UNLIKELY(fixed_vec->get_length() != sizeof(uint64_t))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected hidden pk vector value length", KR(ret),
                     K(fixed_vec->get_length()));
          } else {
            uint64_t *pks = reinterpret_cast<uint64_t *>(fixed_vec->get_data());
            for (int64_t i = 0; i < size; ++i) {
              pks[start + i] = (start_value + i);
            }
          }
          break;
        }
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected vector format", KR(ret), K(format));
          break;
      }
    }
  }
  return ret;
}

int ObDDLVectorUtils::batch_fill_value(common::ObIVector *vector, const int64_t start,
                                              const int64_t size, const int64_t value)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == vector || vector->get_format() != VEC_FIXED || start < 0 || size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(start), K(size), KPC(vector));
  } else if (size > 0) {
    ObFixedLengthBase *fixed_vec = static_cast<ObFixedLengthBase *>(vector);
    if (OB_UNLIKELY(fixed_vec->get_length() != sizeof(int64_t))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected vector value length", KR(ret), K(fixed_vec->get_length()));
    } else {
      int64_t *pks = reinterpret_cast<int64_t *>(fixed_vec->get_data());
      for (int64_t i = 0; i < size; ++i) {
        pks[start + i] = value;
      }
    }

  }
  return ret;
}

/**
 * multi version vector
 */

int ObDDLVectorUtils::make_const_multi_version_vector(const int64_t value,
                                                             ObIAllocator &allocator,
                                                             ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  vector = nullptr;
  if (OB_FAIL(new_vector(VEC_UNIFORM_CONST, multi_version_value_tc, allocator, vector))) {
  } else {
    ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
    ObStorageDatum *storage_datum = nullptr;
    if (OB_ISNULL(storage_datum = OB_NEWx(ObStorageDatum, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObStorageDatum", KR(ret));
    } else {
      storage_datum->set_int(value);
      uniform_vec->set_datums(storage_datum);
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
