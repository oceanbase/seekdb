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

#ifndef OB_FTS_SEGMENT_SORT_H_
#define OB_FTS_SEGMENT_SORT_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "storage/ddl/ob_ddl_encode_sortkey_utils.h"
#include "storage/fts/ob_fts_struct.h"

namespace oceanbase
{
namespace storage
{

template <int64_t SORTKEY_LEN>
class ObFtsSegmentSort
{
public:
  struct SortItem
  {
    char key_[SORTKEY_LEN];
    int64_t token_idx_;
  };

  ObFtsSegmentSort()
    : is_inited_(false), item_count_(0)
  {
  }

  int init(ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    is_inited_ = true;
    return ret;
  }

  int add_token(const ObFTToken &token, int64_t token_idx)
  {
    int ret = OB_SUCCESS;
    SortItem item;
    MEMSET(&item, 0, sizeof(SortItem));
    item.token_idx_ = token_idx;
    if (OB_FAIL(items_.push_back(item))) {
    } else {
      item_count_++;
    }
    return ret;
  }

  int sort()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int get_sorted_token(ObFTToken &token)
  {
    int ret = OB_SUCCESS;
    if (cursor_ >= item_count_) {
      ret = OB_ITER_END;
    }
    return ret;
  }

  void reset()
  {
    items_.reset();
    item_count_ = 0;
    cursor_ = 0;
    is_inited_ = false;
  }

  OB_INLINE int64_t get_item_count() const { return item_count_; }

private:
  bool is_inited_;
  int64_t item_count_;
  int64_t cursor_;
  ObIAllocator *allocator_;
  common::ObSEArray<SortItem, 128> items_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_FTS_SEGMENT_SORT_H_ */
