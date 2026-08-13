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

#ifndef OCEANBASE_SHARE_VECTOR_OB_BITMAP_NULL_VECTOR_BASE_H_
#define OCEANBASE_SHARE_VECTOR_OB_BITMAP_NULL_VECTOR_BASE_H_

#include "query/engine/vector/ob_vector_base.h"
#include "share/vector/ob_bit_vector.h"

namespace oceanbase
{
namespace common
{

class ObBitmapNullVectorBase: public ObVectorBase
{
public:
  ObBitmapNullVectorBase(sql::ObBitVector *nulls) :
    ObVectorBase(), nulls_(nulls), flag_(0)
  {}

  // Returning true is meaningless, returning false indicates that there is indeed no null.
  OB_INLINE bool has_null() const override final { return has_null_; };
  OB_INLINE void set_has_null() override final { has_null_ = true; };
  inline void set_has_null(bool flag) { has_null_ = flag; };
  OB_INLINE void reset_has_null() override final { has_null_ = false; };

  inline sql::ObBitVector *get_nulls() { return nulls_; }
  OB_INLINE void set_nulls(sql::ObBitVector *nulls) { nulls_ = nulls; }
  inline const sql::ObBitVector *get_nulls() const { return nulls_; }
  inline uint16_t get_flag() const { return flag_; }
  inline void reset_flag()
  {
    flag_ = 0;
  }

  OB_INLINE bool is_null(const int64_t idx) const override final { return nulls_->at(idx); }
  OB_INLINE void set_null(const int64_t idx) override final {
    nulls_->set(idx);
    set_has_null();
  };
  OB_INLINE void unset_null(const int64_t idx) override final {
    nulls_->unset(idx);
  };

  inline void from(sql::ObBitVector *nulls, const uint16_t flag)
  {
    nulls_ = nulls;
    flag_ = flag & 1;
  }

  // Note: if need to add new flag or change the default value of an existing flag,
  // please make sure to synchronize this function accordingly.
  static uint16_t get_default_flag(bool has_null)
  {
    return has_null ? 1 : 0;
  }

protected:
  sql::ObBitVector *nulls_;
  union {
		struct {
			uint16_t has_null_:1;
			uint16_t reserved_:15;
		};
		uint16_t flag_;
	};
};

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_BITMAP_NULL_VECTOR_BASE_H_
