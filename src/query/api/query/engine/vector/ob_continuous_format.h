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

#ifndef OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_FORMAT_H_
#define OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_FORMAT_H_

#include "query/engine/vector/ob_continuous_base.h"

namespace oceanbase
{
namespace common
{
class ObContinuousFormat : public ObContinuousBase
{
public:
  ObContinuousFormat(uint32_t *offsets, char *data, sql::ObBitVector *nulls)
    : ObContinuousBase(offsets, data, nulls)
  {}

  OB_INLINE VectorFormat get_format() const override { return VEC_CONTINUOUS; }
  OB_INLINE void get_payload(const int64_t idx, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE void get_payload(const int64_t idx, bool &is_null, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE const char *get_payload(const int64_t idx) const override final;
  OB_INLINE ObLength get_length(const int64_t idx) const override final;
  OB_INLINE void set_length(const int64_t idx, const ObLength length) override
  {
    UNUSED(idx);
    UNUSED(length);
  }
  OB_INLINE void set_payload(const int64_t idx, const void *payload,
                             const ObLength length) override final;
  OB_INLINE void set_payload_shallow(const int64_t idx, const void *payload,
                                     const ObLength length) override final
  {
    set_payload(idx, payload, length);
  }
  DEF_VEC_READ_INTERFACES(ObContinuousFormat);
  DEF_VEC_WRITE_INTERFACES(ObContinuousFormat);
};

OB_INLINE void ObContinuousFormat::get_payload(const int64_t idx, const char *&payload,
                                                ObLength &length) const
{
  payload = data_ + offsets_[idx];
  length = get_length(idx);
}

OB_INLINE void ObContinuousFormat::get_payload(const int64_t idx, bool &is_null,
                                                const char *&payload, ObLength &length) const
{
  is_null = nulls_->at(idx);
  if (!is_null) {
    payload = data_ + offsets_[idx];
    length = get_length(idx);
  }
}

OB_INLINE const char *ObContinuousFormat::get_payload(const int64_t idx) const
{
  return data_ + offsets_[idx];
}

OB_INLINE ObLength ObContinuousFormat::get_length(const int64_t idx) const
{
  return offsets_[idx + 1] - offsets_[idx];
}

OB_INLINE void ObContinuousFormat::set_payload(const int64_t idx, const void *payload,
                                                const ObLength length)
{
  if (OB_UNLIKELY(nulls_->at(idx))) {
    unset_null(idx);
  }
  MEMCPY(data_ + offsets_[idx], payload, length);
  offsets_[idx + 1] = offsets_[idx] + length;
}

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_FORMAT_H_
