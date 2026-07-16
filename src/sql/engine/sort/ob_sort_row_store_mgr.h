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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_ROW_STORE_MGR_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_ROW_STORE_MGR_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

template <typename StoreRowType, bool HAS_ADDON>
class ObSortRowStoreMgr
{
public:
  ObSortRowStoreMgr() : is_inited_(false), sk_store_(nullptr), addon_store_(nullptr)
  {
  }

  int init(ObIAllocator &allocator, int64_t block_size)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    block_size_ = block_size;
    is_inited_ = true;
    return ret;
  }

  void reset()
  {
    sk_store_ = nullptr;
    addon_store_ = nullptr;
    is_inited_ = false;
  }

  OB_INLINE void *get_sk_store() { return sk_store_; }
  OB_INLINE void *get_addon_store() { return addon_store_; }
  OB_INLINE int64_t get_block_size() const { return block_size_; }

private:
  bool is_inited_;
  ObIAllocator *allocator_;
  void *sk_store_;
  void *addon_store_;
  int64_t block_size_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_ROW_STORE_MGR_H_ */
