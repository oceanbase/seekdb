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
#ifndef OB_STORAGE_OB_ROW_ID_RANGE_H_
#define OB_STORAGE_OB_ROW_ID_RANGE_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{

struct ObMicroBlockRowIdRange
{
  ObMicroBlockRowIdRange() { reset(); }
  ObMicroBlockRowIdRange(const int64_t start, const uint64_t count)
      : start_row_id_(start), end_row_id_(start + count - 1)
  {}
  void set(const int64_t start, const int64_t end)
  {
    start_row_id_ = start;
    end_row_id_ = end;
  }
  int64_t begin() const { return start_row_id_; }
  int64_t end() const { return end_row_id_; }
  void reset()
  {
    start_row_id_ = -1;
    end_row_id_ = -1;
  }
  bool is_valid() const { return start_row_id_ >= 0 && end_row_id_ >= start_row_id_; }
  int64_t get_row_count() const { return end_row_id_ - start_row_id_ + 1; }

  TO_STRING_KV(K_(start_row_id), K_(end_row_id));

  int64_t start_row_id_;
  int64_t end_row_id_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_OB_ROW_ID_RANGE_H_
