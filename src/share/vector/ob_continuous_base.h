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

#ifndef OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_BASE_H_
#define OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_BASE_H_

#include "share/vector/ob_bitmap_null_vector_base.h"

namespace oceanbase
{
namespace common
{

class ObContinuousBase: public ObBitmapNullVectorBase
{
public:
  ObContinuousBase(uint32_t *offsets, char *data, sql::ObBitVector *nulls)
    : ObBitmapNullVectorBase(nulls), offsets_(offsets), data_(data)
  {}
  inline uint32_t *get_offsets() { return offsets_; };
  inline const uint32_t *get_offsets() const { return offsets_; };
  OB_INLINE void set_offsets(uint32_t *offsets) { offsets_ = offsets; }
  inline char *get_data() { return data_; }
  inline const char *get_data() const { return data_; }
  inline void set_data(char *ptr) { data_ = ptr; }
  inline void from_continuous_vector(const bool has_null, const sql::ObBitVector &nulls,
                                     uint32_t *offsets, const int64_t start_idx, 
                                     const int64_t read_rows, char *data)
  {
    UNUSED(has_null);
    has_null_ = false;
    nulls_->reset(read_rows);
    for (int64_t i = 0; i < read_rows; ++i) {
      if (nulls.at(start_idx + i)) {
        nulls_->set(i);
        has_null_ = true;
      }
    }
    offsets_ = offsets + start_idx;
    data_ = data;
  }
  virtual int to_rows(const sql::RowMeta &row_meta,
                       sql::ObCompactRow **stored_rows,
                       const uint16_t selector[],
                       const int64_t size,
                       const int64_t col_idx) const override final;

  virtual int to_rows(const sql::RowMeta &row_meta, sql::ObCompactRow **stored_rows,
                       const int64_t size, const int64_t col_idx) const override final;
  inline void from(uint32_t *offsets, char *data)
  {
    offsets_ = offsets;
    data_ = data;
  }

protected:
  uint32_t *offsets_;
  char *data_;
};

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_CONTINUOUS_BASE_H_
