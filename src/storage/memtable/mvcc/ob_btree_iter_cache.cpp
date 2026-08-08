/**
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

#include <new>
#include "data_plane/memtable/ob_btree_iter_cache_api.h"
#include "storage/memtable/mvcc/ob_btree_iter_cache.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/worker.h"
#include "query/session/ob_session_access.h"

namespace oceanbase
{
namespace memtable
{

void ObBtreeIterCache::destroy()
{
  FreeNode *cur = freelist_;
  while (cur != nullptr) {
    FreeNode *next = cur->next_;
    ob_free(cur);
    cur = next;
  }
  freelist_ = nullptr;
  free_count_ = 0;
}

void *ObBtreeIterCache::alloc(int64_t size)
{
  void *ptr = nullptr;
  if (OB_NOT_NULL(freelist_)) {
    ptr = freelist_;
    freelist_ = freelist_->next_;
    --free_count_;
  } else {
    ptr = ob_malloc(size, ObMemAttr("BtreeIterCache"));
  }
  return ptr;
}

void ObBtreeIterCache::free(void *ptr)
{
  if (OB_ISNULL(ptr)) {
    return;
  }
  if (free_count_ < MAX_FREE_COUNT) {
    FreeNode *node = static_cast<FreeNode *>(ptr);
    node->next_ = freelist_;
    freelist_ = node;
    ++free_count_;
  } else {
    ob_free(ptr);
  }
}

void *btree_iter_alloc(int64_t size)
{
  void *ptr = nullptr;
  sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (OB_NOT_NULL(session)) {
    ObBtreeIterCache *cache = static_cast<ObBtreeIterCache *>(
        query::ObSessionAccess::get_btree_iter_cache(session));
    if (OB_NOT_NULL(cache)) {
      ptr = cache->alloc(size);
    }
  }
  if (OB_ISNULL(ptr)) {
    ptr = ob_malloc(size, ObMemAttr("BtreeIter"));
  }
  return ptr;
}

void btree_iter_free(void *ptr)
{
  if (OB_ISNULL(ptr)) {
    return;
  }
  sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (OB_NOT_NULL(session)) {
    ObBtreeIterCache *cache = static_cast<ObBtreeIterCache *>(
        query::ObSessionAccess::get_btree_iter_cache(session));
    if (OB_NOT_NULL(cache)) {
      cache->free(ptr);
      return;
    }
  }
  ob_free(ptr);
}

} // namespace memtable

namespace data_plane
{

void *create_btree_iter_cache(common::ObIAllocator &allocator)
{
  void *buffer = allocator.alloc(sizeof(memtable::ObBtreeIterCache));
  return nullptr == buffer ? nullptr : new (buffer) memtable::ObBtreeIterCache();
}

void destroy_btree_iter_cache(common::ObIAllocator &allocator, void *&cache)
{
  if (nullptr != cache) {
    memtable::ObBtreeIterCache *typed_cache =
        static_cast<memtable::ObBtreeIterCache *>(cache);
    typed_cache->destroy();
    typed_cache->~ObBtreeIterCache();
    allocator.free(typed_cache);
    cache = nullptr;
  }
}

} // namespace data_plane
} // namespace oceanbase
