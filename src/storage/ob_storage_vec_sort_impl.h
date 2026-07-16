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

#ifndef OB_STORAGE_VEC_SORT_IMPL_H_
#define OB_STORAGE_VEC_SORT_IMPL_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "storage/ob_storage_sort_resource_manager.h"

namespace oceanbase
{
namespace storage
{

template <typename T, typename Compare>
class ObStorageVecSortImpl
{
public:
  ObStorageVecSortImpl() : is_inited_(false), item_count_(0)
  {
  }

  int init(ObIAllocator &allocator, Compare &comp, int64_t mem_limit)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    comp_ = &comp;
    if (OB_FAIL(res_mgr_.init(mem_limit))) {
    } else {
      is_inited_ = true;
    }
    return ret;
  }

  int add_item(const T &item)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(items_.push_back(item))) {
    } else {
      item_count_++;
    }
    return ret;
  }

  int add_item(T &&item)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(items_.push_back(item))) {
    } else {
      item_count_++;
    }
    return ret;
  }

  int do_sort()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int get_next_item(T &item)
  {
    int ret = OB_SUCCESS;
    if (cursor_ >= item_count_) {
      ret = OB_ITER_END;
    } else {
      item = items_.at(cursor_++);
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
  OB_INLINE const ObStorageSortResourceManager &get_res_mgr() const { return res_mgr_; }

private:
  bool is_inited_;
  int64_t item_count_;
  int64_t cursor_;
  ObIAllocator *allocator_;
  Compare *comp_;
  ObStorageSortResourceManager res_mgr_;
  common::ObSEArray<T, 256> items_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_STORAGE_VEC_SORT_IMPL_H_ */
