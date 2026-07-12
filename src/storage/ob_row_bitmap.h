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
#ifndef OB_STORAGE_OB_ROW_BITMAP_H_
#define OB_STORAGE_OB_ROW_BITMAP_H_

#include "lib/container/ob_bitmap.h"

namespace oceanbase
{
namespace storage
{

class ObRowBitmap
{
public:
  explicit ObRowBitmap(common::ObIAllocator &allocator)
      : bitmap_(allocator), start_row_id_(-1)
  {}
  ~ObRowBitmap() = default;

  int init(const uint64_t count) { return bitmap_.init(count); }
  int switch_context(const uint64_t count)
  {
    start_row_id_ = -1;
    return bitmap_.reserve(count);
  }
  void reuse(const int64_t start_row_id, const bool is_all_true = false)
  {
    start_row_id_ = start_row_id;
    bitmap_.reuse(is_all_true);
  }
  common::ObBitmap *get_inner_bitmap() { return &bitmap_; }
  int64_t get_start_id() const { return start_row_id_; }
  int set(const int64_t row_idx, const bool value = true)
  {
    OB_ASSERT(row_idx >= start_row_id_);
    return bitmap_.set(row_idx - start_row_id_, value);
  }
  bool test(const int64_t row_idx) const
  {
    OB_ASSERT(row_idx >= start_row_id_);
    return bitmap_.test(row_idx - start_row_id_);
  }
  bool is_all_true() const { return bitmap_.is_all_true(); }
  int get_next_valid_row(const int64_t row_id, int64_t &next_row_id) const;
  int bit_and(const common::ObBitmap &right);

  TO_STRING_KV(K_(start_row_id), K_(bitmap));

private:
  common::ObBitmap bitmap_;
  int64_t start_row_id_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_OB_ROW_BITMAP_H_
