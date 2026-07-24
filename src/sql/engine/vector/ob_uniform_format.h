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

#ifndef OCEANBASE_SHARE_VECTOR_OB_UNIFORM_FORMAT_H_
#define OCEANBASE_SHARE_VECTOR_OB_UNIFORM_FORMAT_H_

#include "sql/engine/vector/ob_uniform_base.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
namespace common
{

template<bool IS_CONST>
class ObUniformFormat : public ObUniformBase
{
public:
  ObUniformFormat(ObDatum *datums, sql::ObEvalInfo *eval_info)
    : ObUniformBase(datums, eval_info)
  {}

  OB_INLINE VectorFormat get_format() const override final
  {
    return IS_CONST ? VEC_UNIFORM_CONST : VEC_UNIFORM;
  }
  static const VectorFormat FORMAT = IS_CONST ? VEC_UNIFORM_CONST : VEC_UNIFORM;

  OB_INLINE bool has_null() const override final { return IS_CONST ? get_datum(0).is_null() : true; }
  OB_INLINE void set_has_null() override final {}
  OB_INLINE void reset_has_null() override final {}
  OB_INLINE bool is_null(const int64_t idx) const override final { return get_datum(idx).is_null(); }
  OB_INLINE void set_null(const int64_t idx) override final
  {
    get_datum(idx).set_null();
    eval_info_->notnull_ = false;
  }
  OB_INLINE void unset_null(const int64_t idx) override final { get_datum(idx).set_none(); }
  inline void set_all_null(const int64_t size)
  {
    for (int64_t idx = 0; idx < size; ++idx) {
      get_datum(idx).set_null();
    }
    eval_info_->notnull_ = false;
  }

  OB_INLINE void get_payload(const int64_t idx, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE void get_payload(const int64_t idx, bool &is_null, const char *&payload,
                             ObLength &length) const override final;
  OB_INLINE const char *get_payload(const int64_t idx) const override final;
  OB_INLINE ObLength get_length(const int64_t idx) const override final;
  OB_INLINE void set_length(const int64_t idx, const ObLength length) override;
  OB_INLINE void set_payload(const int64_t idx, const void *payload,
                             const ObLength length) override final
  {
    MEMCPY(const_cast<char *>(get_payload(idx)), payload, length);
    get_datum(idx).pack_ = length;
  }
  OB_INLINE void set_payload_shallow(const int64_t idx, const void *payload,
                                     const ObLength length) override final
  {
    get_datum(idx).ptr_ = static_cast<const char *>(payload);
    get_datum(idx).pack_ = length;
  }

  inline const ObDatum &get_datum(const int64_t idx) const { return datums_[IS_CONST ? 0 : idx]; }
  inline ObDatum &get_datum(const int64_t idx) { return datums_[IS_CONST ? 0 : idx]; }
  DEF_VEC_READ_INTERFACES(ObUniformFormat<IS_CONST>);
  DEF_VEC_WRITE_INTERFACES(ObUniformFormat<IS_CONST>);
};

template<bool IS_CONST>
OB_INLINE void ObUniformFormat<IS_CONST>::get_payload(const int64_t idx, const char *&payload,
                                                       ObLength &length) const
{
  payload = get_datum(idx).ptr_;
  length = get_datum(idx).len_;
}

template<bool IS_CONST>
OB_INLINE void ObUniformFormat<IS_CONST>::get_payload(const int64_t idx, bool &is_null,
                                                       const char *&payload, ObLength &length) const
{
  is_null = get_datum(idx).null_;
  if (!is_null) {
    payload = get_datum(idx).ptr_;
    length = get_datum(idx).len_;
  }
}

template<bool IS_CONST>
OB_INLINE const char *ObUniformFormat<IS_CONST>::get_payload(const int64_t idx) const
{
  return get_datum(idx).ptr_;
}

template<bool IS_CONST>
OB_INLINE ObLength ObUniformFormat<IS_CONST>::get_length(const int64_t idx) const
{
  return get_datum(idx).len_;
}

template<bool IS_CONST>
OB_INLINE void ObUniformFormat<IS_CONST>::set_length(const int64_t idx, const ObLength length)
{
  get_datum(idx).pack_ = length;
}

}
}

#endif // OCEANBASE_SHARE_VECTOR_OB_UNIFORM_FORMAT_H_
