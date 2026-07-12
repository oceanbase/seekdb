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
#include "storage/ob_row_bitmap.h"

namespace oceanbase
{
namespace storage
{

int ObRowBitmap::get_next_valid_row(const int64_t row_id, int64_t &next_row_id) const
{
  int ret = OB_SUCCESS;
  next_row_id = -1;
  if (OB_UNLIKELY(row_id < start_row_id_ || row_id - start_row_id_ >= bitmap_.size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid row id", K(ret), K(row_id), K_(start_row_id), K_(bitmap));
  } else {
    const int64_t start_offset = row_id - start_row_id_;
    if (OB_FAIL(bitmap_.next_valid_idx(start_offset, bitmap_.size() - start_offset, false, next_row_id))) {
      LOG_WARN("failed to get next valid row", K(ret), K(row_id), K_(bitmap));
    } else if (-1 == next_row_id) {
      ret = OB_ITER_END;
    } else {
      next_row_id += start_row_id_;
    }
  }
  return ret;
}

int ObRowBitmap::bit_and(const common::ObBitmap &right)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(bitmap_.size() != right.size())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected bitmap size", K(ret), K(bitmap_.size()), K(right.size()));
  } else if (OB_FAIL(bitmap_.bit_and(right))) {
    LOG_WARN("failed to combine bitmap", K(ret), K_(bitmap), K(right));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
