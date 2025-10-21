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

#ifndef OCEANBASE_SHARE_VECTOR_OB_DISCRETE_BASE_H_
#define OCEANBASE_SHARE_VECTOR_OB_DISCRETE_BASE_H_

#include "share/vector/ob_bitmap_null_vector_base.h"

namespace oceanbase
{
namespace common
{

class ObDiscreteBase : public ObBitmapNullVectorBase
{
public:
  ObDiscreteBase(int32_t *lens, char **ptrs, sql::ObBitVector *nulls)
    : ObBitmapNullVectorBase(nulls), lens_(lens), ptrs_(ptrs)
  {}

  static const VectorFormat FORMAT = VEC_DISCRETE;
  DECLARE_TO_STRING;
  inline ObLength *get_lens() { return lens_; }
  inline const ObLength *get_lens() const { return lens_; }
  OB_INLINE void set_lens(ObLength *lens) { lens_ = lens; }
  inline char **get_ptrs() const { return ptrs_; }
  OB_INLINE void set_ptrs(char **ptrs) { ptrs_ = ptrs; }
  virtual int32_t to_rows(const sql::RowMeta &row_meta,
                       sql::ObCompactRow **stored_rows,
                       const uint16_t selector[],
                       const int64_t size,
                       const int64_t col_idx) const override final;

  virtual int to_rows(const sql::RowMeta &row_meta, sql::ObCompactRow **stored_rows,
                       const int64_t size, const int64_t col_idx) const override final;

protected:
  ObLength *lens_;
  char **ptrs_;
};

}
}
#endif //OCEANBASE_SHARE_VECTOR_OB_DISCRETE_BASE_H_
