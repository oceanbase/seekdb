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
#include "data_plane/ob_iter_cache_api.h"
#include "storage/ob_iter_cache.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/worker.h"
#include "query/session/ob_session_access.h"

namespace oceanbase
{
namespace storage
{

void ObIterCache::destroy()
{
  for (int64_t i = 0; i < ITER_TYPE_COUNT; ++i) {
    FreeNode *cur = freelists_[i];
    while (cur != nullptr) {
      FreeNode *next = cur->next_;
      ob_free(cur);
      cur = next;
    }
    freelists_[i] = nullptr;
    free_counts_[i] = 0;
  }
}

bool ObIterCache::is_valid_type(ObIterCacheType type)
{
  const int64_t index = static_cast<int64_t>(type);
  return index >= 0 && index < ITER_TYPE_COUNT;
}

int64_t ObIterCache::get_max_free_count(ObIterCacheType type)
{
  return ObIterCacheType::BTREE_ITER == type
      ? BTREE_ITER_MAX_FREE_COUNT
      : TABLE_SCAN_ITER_MAX_FREE_COUNT;
}

void *ObIterCache::alloc(ObIterCacheType type, int64_t size)
{
  void *ptr = nullptr;
  if (is_valid_type(type)) {
    const int64_t index = static_cast<int64_t>(type);
    if (OB_NOT_NULL(freelists_[index])) {
      ptr = freelists_[index];
      freelists_[index] = freelists_[index]->next_;
      --free_counts_[index];
    }
  }
  if (OB_ISNULL(ptr)) {
    ptr = ob_malloc(size, ObMemAttr("IterCache"));
  }
  return ptr;
}

void ObIterCache::free(ObIterCacheType type, void *ptr)
{
  if (OB_ISNULL(ptr)) {
    return;
  } else if (is_valid_type(type)) {
    const int64_t index = static_cast<int64_t>(type);
    if (free_counts_[index] < get_max_free_count(type)) {
      FreeNode *node = static_cast<FreeNode *>(ptr);
      node->next_ = freelists_[index];
      freelists_[index] = node;
      ++free_counts_[index];
      return;
    }
  }
  ob_free(ptr);
}

void *iter_alloc(ObIterCacheType type, int64_t size)
{
  void *ptr = nullptr;
  sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (OB_NOT_NULL(session)) {
    ObIterCache *cache = static_cast<ObIterCache *>(
        query::ObSessionAccess::get_iter_cache(session));
    if (OB_NOT_NULL(cache)) {
      ptr = cache->alloc(type, size);
    }
  }
  if (OB_ISNULL(ptr)) {
    ptr = ob_malloc(size, ObMemAttr("Iter"));
  }
  return ptr;
}

void iter_free(ObIterCacheType type, void *ptr)
{
  if (OB_ISNULL(ptr)) {
    return;
  }
  sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (OB_NOT_NULL(session)) {
    ObIterCache *cache = static_cast<ObIterCache *>(
        query::ObSessionAccess::get_iter_cache(session));
    if (OB_NOT_NULL(cache)) {
      cache->free(type, ptr);
      return;
    }
  }
  ob_free(ptr);
}

} // namespace storage

namespace data_plane
{

void *create_iter_cache(common::ObIAllocator &allocator)
{
  void *buffer = allocator.alloc(sizeof(storage::ObIterCache));
  return nullptr == buffer ? nullptr : new (buffer) storage::ObIterCache();
}

void destroy_iter_cache(common::ObIAllocator &allocator, void *&cache)
{
  if (nullptr != cache) {
    storage::ObIterCache *typed_cache =
        static_cast<storage::ObIterCache *>(cache);
    typed_cache->~ObIterCache();
    allocator.free(typed_cache);
    cache = nullptr;
  }
}

} // namespace data_plane
} // namespace oceanbase
