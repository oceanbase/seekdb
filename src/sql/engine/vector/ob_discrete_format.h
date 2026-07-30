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

#ifndef OCEANBASE_SHARE_VECTOR_OB_DISCRETE_FORMAT_H_
#define OCEANBASE_SHARE_VECTOR_OB_DISCRETE_FORMAT_H_

#include "sql/engine/vector/ob_discrete_base.h"

namespace oceanbase
{
namespace common
{

class ObDiscreteFormat : public ObDiscreteBase
{
public:
  ObDiscreteFormat(int32_t *lens, char **ptrs, sql::ObBitVector *nulls)
    : ObDiscreteBase(lens, ptrs, nulls)
  {}

  OB_INLINE VectorFormat get_format() const override { return VEC_DISCRETE; }
  OB_INLINE void get_payload(const int64_t idx, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE void get_payload(const int64_t idx, bool &is_null, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE const char *get_payload(const int64_t idx) const override final;
  OB_INLINE ObLength get_length(const int64_t idx) const override final { return lens_[idx]; }
  OB_INLINE void set_length(const int64_t idx, const ObLength length) override
  {
    lens_[idx] = length;
  }
  OB_INLINE void set_payload(const int64_t idx, const void *payload,
                             const ObLength length) override final;
  OB_INLINE void set_payload_shallow(const int64_t idx, const void *payload,
                                     const ObLength length) override final
  {
    if (OB_UNLIKELY(nulls_->at(idx))) {
      unset_null(idx);
    }
    ptrs_[idx] = const_cast<char *>(static_cast<const char *>(payload));
    lens_[idx] = length;
  }
  void set_datum(const int64_t idx, const ObDatum &datum)
  {
    if (datum.is_null()) {
      set_null(idx);
    } else {
      set_payload_shallow(idx, datum.ptr_, datum.len_);
    }
  }
  DEF_VEC_READ_INTERFACES(ObDiscreteFormat);
  DEF_VEC_WRITE_INTERFACES(ObDiscreteFormat);
};

OB_INLINE void ObDiscreteFormat::get_payload(const int64_t idx, const char *&payload,
                                              ObLength &length) const
{
  payload = ptrs_[idx];
  length = lens_[idx];
}

OB_INLINE void ObDiscreteFormat::get_payload(const int64_t idx, bool &is_null,
                                              const char *&payload, ObLength &length) const
{
  is_null = nulls_->at(idx);
  if (!is_null) {
    payload = ptrs_[idx];
    length = lens_[idx];
  }
}

OB_INLINE const char *ObDiscreteFormat::get_payload(const int64_t idx) const
{
  return ptrs_[idx];
}

OB_INLINE void ObDiscreteFormat::set_payload(const int64_t idx, const void *payload,
                                              const ObLength length)
{
  if (OB_UNLIKELY(nulls_->at(idx))) {
    unset_null(idx);
  }
  MEMCPY(ptrs_[idx], payload, length);
  lens_[idx] = length;
}

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_DISCRETE_FORMAT_H_
