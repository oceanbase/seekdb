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

#ifndef OCEANBASE_STORAGE_OB_HANDLE_MGR_H
#define OCEANBASE_STORAGE_OB_HANDLE_MGR_H

#include "storage/ob_handle_cache.h"

namespace oceanbase
{
namespace storage
{

template<typename Handle, typename Key, int64_t N>
class ObHandleMgr
{
public:
  ObHandleMgr()
    : is_inited_(false),
      is_multi_(false),
      is_ordered_(false),
      last_handle_(NULL),
      handle_cache_(NULL)
  {}
  virtual ~ObHandleMgr() { reset(); }
  void reset()
  {
    is_multi_ = false;
    is_ordered_ = false;
    if (NULL != last_handle_) {
      last_handle_->~Handle();
      last_handle_ = NULL;
    }
    if (NULL != handle_cache_) {
      handle_cache_->~ObHandleCache();
      handle_cache_ = NULL;
    }
    is_inited_ = false;
  }
  int init(const bool is_multi, const bool is_ordered, common::ObIAllocator &allocator)
  {
    int ret = common::OB_SUCCESS;
    void *buf = NULL;
    if (OB_UNLIKELY(is_inited_)) {
      ret = common::OB_INIT_TWICE;
      STORAGE_LOG(WARN, "handle mgr is inited twice", K(ret));
    } else if (is_multi) {
      if (is_ordered) {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(Handle)))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
          STORAGE_LOG(WARN, "failed to allocate last handle");
        } else {
          last_handle_ = new (buf) Handle();
        }
      } else {
        if (OB_ISNULL(buf = allocator.alloc(sizeof(HandleCache)))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
          STORAGE_LOG(WARN, "failed to allocate last handle");
        } else {
          handle_cache_ = new (buf) HandleCache();
        }
      }
    }
    if (OB_SUCC(ret)) {
      is_multi_ = is_multi;
      is_ordered_ = is_ordered;
      is_inited_ = true;
    }
    return ret;
  }
  inline bool is_inited() { return is_inited_; }
  TO_STRING_KV(KP_(last_handle), KP_(handle_cache));
protected:
  typedef ObHandleCache<Key, Handle, N> HandleCache;
  bool is_inited_;
  bool is_multi_;
  bool is_ordered_;
  Handle *last_handle_;
  HandleCache *handle_cache_;
};

}
}
#endif /* OCEANBASE_STORAGE_OB_HANDLE_MGR_H */
