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

#include "data_plane/access/ob_datum_reshape.h"
#include "storage/blocksstable/ob_storage_datum.h"
#include "data_plane/encoding/ob_ascii_util.h"
#include "lib/charset/ob_charset.h"
#include "share/ob_batch_selector.h"
#include "query/engine/vector/ob_continuous_base.h"
#include "query/engine/vector/ob_discrete_format.h"
#include "query/engine/vector/ob_uniform_base.h"
#include "query/engine/vector/type_traits.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace sql;
namespace data_plane
{
int ObDatumReshape::pad_datum_value(const ObObjMeta &col_type,
                                    const ObAccuracy &col_accuracy,
                                    ObIAllocator &allocator,
                                    blocksstable::ObStorageDatum &datum_value)
{
  int ret = OB_SUCCESS;
  if (!datum_value.is_null() && col_type.is_fixed_len_char_type()) {
    const ObCollationType cs_type = col_type.get_collation_type();
    const ObString space_pattern = ObCharsetUtils::get_const_str(cs_type, OB_PADDING_CHAR);
    const int32_t target_char_len = col_accuracy.get_length();
    const int32_t current_char_len = storage::can_do_ascii_optimize(cs_type)
        && storage::is_ascii_str(datum_value.ptr_, datum_value.len_)
        ? datum_value.len_
        : static_cast<int32_t>(ObCharset::strlen_char(
              cs_type, datum_value.ptr_, datum_value.len_));
    if (current_char_len < target_char_len) {
      const int32_t padding_chars = target_char_len - current_char_len;
      const int32_t padded_len = datum_value.len_ + padding_chars * space_pattern.length();
      char *buffer = static_cast<char *>(allocator.alloc(padded_len));
      if (OB_ISNULL(buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate datum padding", K(ret), K(padded_len));
      } else {
        MEMCPY(buffer, datum_value.ptr_, datum_value.len_);
        int32_t pos = datum_value.len_;
        for (int32_t i = 0; i < padding_chars; ++i) {
          MEMCPY(buffer + pos, space_pattern.ptr(), space_pattern.length());
          pos += space_pattern.length();
        }
        datum_value.set_string(ObString(pos, buffer));
      }
    }
  }
  return ret;
}

int ObDatumReshape::reshape_datum_value(const ObObjMeta &col_type,
                                    const ObAccuracy &col_accuracy,
                                    ObIAllocator &allocator,
                                    blocksstable::ObStorageDatum &datum_value)
{
  int ret = OB_SUCCESS;
  if (col_type.is_binary()) {
    int32_t binary_len = col_accuracy.get_length();
    int32_t len = datum_value.len_;
    if (binary_len > len) {
      char *dest_str = NULL;
      const char *str = datum_value.ptr_;
      if (OB_ISNULL(dest_str = (char *)(allocator.alloc(binary_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc mem to binary", K(ret), K(binary_len));
      } else {
        char pad_char = '\0';
        MEMCPY(dest_str, str, len);
        MEMSET(dest_str + len, pad_char, binary_len - len);
        datum_value.set_string(ObString(binary_len, dest_str));
      }
    }
  } else if (col_type.is_fixed_len_char_type()) {
    const char *str = datum_value.ptr_;
    int32_t len = datum_value.len_;
    ObString space_pattern = ObCharsetUtils::get_const_str(col_type.get_collation_type(), ' ');
    for (; len >= space_pattern.length(); len -= space_pattern.length()) {
      if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
        break;
      }
    }
    datum_value.set_string(ObString(len, str));
  }
  return ret;
}

int ObDatumReshape::reshape_datum_vector_value(const ObObjMeta &col_type,
                                           const ObAccuracy &col_accuracy,
                                           ObIAllocator &allocator,
                                           const ObDatumVector &datum_vector,
                                           ObBatchSelector &batch_selector)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!batch_selector.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(batch_selector));
  } else {
    ObBatchSelector single_selector(static_cast<int64_t>(0), 1);
    ObBatchSelector &selector = datum_vector.is_batch() ? batch_selector : single_selector;
    if (col_type.is_binary()) {
      const int32_t binary_len = col_accuracy.get_length();
      const char pad_char = '\0';
      int64_t i = 0;
      while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
        ObDatum &datum = datum_vector.datums_[i];
        if (!datum.is_null() && datum.len_ < binary_len) {
          const char *str = datum.ptr_;
          ObLength len = datum.len_;
          char *dest_str = nullptr;
          if (OB_ISNULL(dest_str = (char *)(allocator.alloc(binary_len)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc mem to binary", K(ret), K(binary_len));
          } else {
            MEMCPY(dest_str, str, len);
            MEMSET(dest_str + len, pad_char, binary_len - len);
            datum.ptr_ = dest_str;
            datum.len_ = binary_len;
          }
        }
      }
      if (OB_LIKELY(OB_ITER_END == ret)) {
        ret = OB_SUCCESS;
      }
    } else if (col_type.is_fixed_len_char_type()) {
      const ObString space_pattern = ObCharsetUtils::get_const_str(col_type.get_collation_type(), ' ');
      int64_t i = 0;
      while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
        ObDatum &datum = datum_vector.datums_[i];
        if (!datum.is_null()) {
          ObLength len = datum.len_;
          const char *str = datum.ptr_;
          for (; len >= space_pattern.length(); len -= space_pattern.length()) {
            if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
              break;
            }
          }
          datum.len_ = len;
        }
      }
      if (OB_LIKELY(OB_ITER_END == ret)) {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

static bool fast_check_vector_is_all_null(ObIVector *vector, const int64_t batch_size)
{
  bool is_all_null = false;
  VectorFormat format = vector->get_format();
  switch (format) {
    case VEC_FIXED:
    case VEC_DISCRETE:
    case VEC_CONTINUOUS: {
      ObBitmapNullVectorBase *base = static_cast<ObBitmapNullVectorBase *>(vector);
      is_all_null = base->has_null() && base->get_nulls()->is_all_true(batch_size);
      break;
    }
    default:
      break;
  }
  return is_all_null;
}

static int new_discrete_vector(VecValueTypeClass value_tc,
                               const int64_t max_batch_size,
                               ObIAllocator &allocator,
                               ObDiscreteBase *&result_vec)
{
  int ret = OB_SUCCESS;
  result_vec = nullptr;
  ObIVector *vector = nullptr;
  switch (value_tc) {
#define DISCRETE_VECTOR_INIT_SWITCH(value_tc)                           \
  case value_tc: {                                                      \
    using VecType = RTVectorType<VEC_DISCRETE, value_tc>;               \
    static_assert(sizeof(VecType) <= ObIVector::MAX_VECTOR_STRUCT_SIZE, \
                  "vector size exceeds MAX_VECTOR_STRUCT_SIZE");        \
    vector = OB_NEWx(VecType, &allocator, nullptr, nullptr, nullptr);   \
    break;                                                              \
  }
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_STRING);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_RAW);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_LOB);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_JSON);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_GEO);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_UDT);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
#undef DISCRETE_VECTOR_INIT_SWITCH
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected discrete vector value type class", KR(ret), K(value_tc));
      break;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(vector)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc vecttor", KR(ret));
  } else {
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
      discrete_vec->set_nulls(nulls);
      discrete_vec->set_lens(lens);
      discrete_vec->set_ptrs(ptrs);
      result_vec = discrete_vec;
    }
  }
  return ret;
}

int ObDatumReshape::reshape_vector_value(const ObObjMeta &col_type,
                                     const ObAccuracy &col_accuracy,
                                     ObIAllocator &allocator,
                                     ObIVector *&vector,
                                     ObBatchSelector &selector)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(vector) || !selector.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(vector), K(selector));
  } else if (fast_check_vector_is_all_null(vector, selector.get_max())) {
    // do nothing
  } else if (col_type.is_binary()) {
    const int32_t binary_len = col_accuracy.get_length();
    const char pad_char = '\0';
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_CONTINUOUS:
      {
        ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
        ObDiscreteBase *discrete_vec = nullptr;
        char *data = continuous_vec->get_data();
        uint32_t *offsets = continuous_vec->get_offsets();
        char **ptrs = nullptr;
        ObLength *lens = nullptr;
        bool has_value_change = false;
        VecValueTypeClass value_tc = get_vec_value_tc(col_type.get_type(),
                                                      col_type.get_scale(),
                                                      col_type.get_stored_precision());
        if (OB_FAIL(new_discrete_vector(value_tc, selector.get_max(), allocator, discrete_vec))) {
        } else {
          ptrs = discrete_vec->get_ptrs();
          lens = discrete_vec->get_lens();
        }
        int64_t i = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
          if (continuous_vec->is_null(i)) {
            discrete_vec->set_null(i);
          } else {
            const ObLength len = offsets[i + 1] - offsets[i];
            char *str = data + offsets[i];
            if (len < binary_len) {
              char *dest_str = nullptr;
              if (OB_ISNULL(dest_str = (char *)(allocator.alloc(binary_len)))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_WARN("fail to alloc mem to binary", K(ret), K(binary_len));
              } else {
                MEMCPY(dest_str, str, len);
                MEMSET(dest_str + len, pad_char, binary_len - len);
                ptrs[i] = dest_str;
                lens[i] = binary_len;
                has_value_change = true;
              }
            } else {
              ptrs[i] = str;
              lens[i] = binary_len;
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        if (OB_SUCC(ret) && has_value_change) {
          vector = discrete_vec;
        }
        break;
      }
      case VEC_DISCRETE:
      {
        ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
        char **ptrs = discrete_vec->get_ptrs();
        ObLength *lens =discrete_vec->get_lens();
        int64_t i = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
          if (!discrete_vec->is_null(i) && lens[i] < binary_len) {
            char *str = ptrs[i];
            ObLength len = lens[i];
            char *dest_str = nullptr;
            if (OB_ISNULL(dest_str = (char *)(allocator.alloc(binary_len)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to alloc mem to binary", K(ret), K(binary_len));
            } else {
              MEMCPY(dest_str, str, len);
              MEMSET(dest_str + len, pad_char, binary_len - len);
              ptrs[i] = dest_str;
              lens[i] = binary_len;
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      case VEC_UNIFORM:
      {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        ObDatum *datums = uniform_vec->get_datums();
        int64_t i = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
          ObDatum &datum = datums[i];
          if (!datum.is_null() && datum.len_ < binary_len) {
            const char *str = datum.ptr_;
            ObLength len = datum.len_;
            char *dest_str = nullptr;
            if (OB_ISNULL(dest_str = (char *)(allocator.alloc(binary_len)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to alloc mem to binary", K(ret), K(binary_len));
            } else {
              MEMCPY(dest_str, str, len);
              MEMSET(dest_str + len, pad_char, binary_len - len);
              datum.ptr_ = dest_str;
              datum.len_ = binary_len;
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      case VEC_UNIFORM_CONST:
      {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        ObDatum &datum = uniform_vec->get_datums()[0];
        if (!datum.is_null() && datum.len_ < binary_len) {
          const char *str = datum.ptr_;
          ObLength len = datum.len_;
          char *dest_str = nullptr;
          if (OB_ISNULL(dest_str = (char *)(allocator.alloc(binary_len)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc mem to binary", K(ret), K(binary_len));
          } else {
            MEMCPY(dest_str, str, len);
            MEMSET(dest_str + len, pad_char, binary_len - len);
            datum.ptr_ = dest_str;
            datum.len_ = binary_len;
          }
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected binary vector format", KR(ret), K(format), K(col_type));
        break;
    }
  } else if (col_type.is_fixed_len_char_type()) {
    const ObString space_pattern = ObCharsetUtils::get_const_str(col_type.get_collation_type(), ' ');
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_CONTINUOUS:
      {
        ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
        ObDiscreteBase *discrete_vec = nullptr;
        char *data = continuous_vec->get_data();
        uint32_t *offsets = continuous_vec->get_offsets();
        char **ptrs = nullptr;
        ObLength *lens = nullptr;
        bool has_value_change = false;
        VecValueTypeClass value_tc = get_vec_value_tc(col_type.get_type(),
                                                      col_type.get_scale(),
                                                      col_type.get_stored_precision());
        if (OB_FAIL(new_discrete_vector(value_tc, selector.get_max(), allocator, discrete_vec))) {
        } else {
          ptrs = discrete_vec->get_ptrs();
          lens = discrete_vec->get_lens();
        }
        int64_t i = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
          if (continuous_vec->is_null(i)) {
            discrete_vec->set_null(i);
          } else {
            const ObLength length = offsets[i + 1] - offsets[i];
            {
              ObLength len = length;
              char *str = data + offsets[i];
              for (; len >= space_pattern.length(); len -= space_pattern.length()) {
                if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
                  break;
                }
              }
              ptrs[i] = str;
              lens[i] = len;
              if (len != length) {
                has_value_change = true;
              }
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        if (OB_SUCC(ret) && has_value_change) {
          vector = discrete_vec;
        }
        break;
      }
      case VEC_DISCRETE:
      {
        ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
        char **ptrs = discrete_vec->get_ptrs();
        ObLength *lens =discrete_vec->get_lens();
        int64_t i = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
          if (!discrete_vec->is_null(i)) {
            ObLength len = lens[i];
            {
              const char *str = ptrs[i];
              for (; len >= space_pattern.length(); len -= space_pattern.length()) {
                if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
                  break;
                }
              }
            }
            lens[i] = len;
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      case VEC_UNIFORM:
      {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        ObDatum *datums = uniform_vec->get_datums();
        int64_t i = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(i))) {
          ObDatum &datum = datums[i];
          if (!datum.is_null()) {
            ObLength len = datum.len_;
            {
              const char *str = datum.ptr_;
              for (; len >= space_pattern.length(); len -= space_pattern.length()) {
                if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
                  break;
                }
              }
              datum.len_ = len;
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      case VEC_UNIFORM_CONST:
      {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        ObDatum &datum = uniform_vec->get_datums()[0];
        if (!datum.is_null()) {
          ObLength len = datum.len_;
          {
            const char *str = datum.ptr_;
            for (; len >= space_pattern.length(); len -= space_pattern.length()) {
              if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
                break;
              }
            }
            datum.len_ = len;
          }
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected fixed len char vector format", KR(ret), K(format), K(col_type));
        break;
    }
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
