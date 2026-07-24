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
#ifndef OCEANBASE_SHARE_VECTOR_OB_FIXED_LENGTH_FORMAT_H_
#define OCEANBASE_SHARE_VECTOR_OB_FIXED_LENGTH_FORMAT_H_

#include "sql/engine/vector/ob_fixed_length_base.h"

namespace oceanbase
{
namespace common
{

template<typename ValueType>
class ObFixedLengthFormat : public ObFixedLengthBase
{
public:
  ObFixedLengthFormat(char *data, sql::ObBitVector *nulls)
    : ObFixedLengthBase(nulls, sizeof(ValueType), data)
  {}

  OB_INLINE VectorFormat get_format() const override final { return VEC_FIXED; }
  OB_INLINE void get_payload(const int64_t idx, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE void get_payload(const int64_t idx, bool &is_null, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE const char *get_payload(const int64_t idx) const override final;
  OB_INLINE ObLength get_length(const int64_t idx) const override final
  {
    UNUSED(idx);
    return sizeof(ValueType);
  }
  OB_INLINE void set_length(const int64_t idx, const ObLength length) override
  {
    UNUSED(idx);
    UNUSED(length);
  }
  OB_INLINE void set_payload(const int64_t idx, const void *payload,
                             const ObLength length) override final
  {
    OB_ASSERT(length == sizeof(ValueType));
    if (!std::is_same<ValueType, char[0]>::value) {
      if (OB_UNLIKELY(nulls_->at(idx))) {
        unset_null(idx);
      }
      (reinterpret_cast<ValueType *>(data_))[idx] = *(static_cast<const ValueType *>(payload));
    }
  }
  OB_INLINE void set_payload_shallow(const int64_t idx, const void *payload,
                                     const ObLength length) override final
  {
    set_payload(idx, payload, length);
  }
  OB_INLINE int32_t type_size() const { return sizeof(ValueType); }
  DEF_VEC_READ_INTERFACES(ObFixedLengthFormat<ValueType>);
  DEF_VEC_WRITE_INTERFACES(ObFixedLengthFormat<ValueType>);
};

template<typename ValueType>
OB_INLINE void ObFixedLengthFormat<ValueType>::get_payload(const int64_t idx, bool &is_null,
                                                            const char *&payload,
                                                            ObLength &length) const
{
  is_null = nulls_->at(idx);
  if (!is_null) {
    payload = reinterpret_cast<const char *>(data_ + sizeof(ValueType) * idx);
    length = type_size();
  }
}

template<typename ValueType>
OB_INLINE void ObFixedLengthFormat<ValueType>::get_payload(const int64_t idx,
                                                            const char *&payload,
                                                            ObLength &length) const
{
  payload = reinterpret_cast<const char *>(data_ + sizeof(ValueType) * idx);
  length = type_size();
}

template<typename ValueType>
OB_INLINE const char *ObFixedLengthFormat<ValueType>::get_payload(const int64_t idx) const
{
  return reinterpret_cast<const char *>(data_ + sizeof(ValueType) * idx);
}

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_FIXED_LENGTH_FORMAT_H_
