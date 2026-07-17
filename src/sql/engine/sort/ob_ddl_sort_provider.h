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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_DDL_SORT_PROVIDER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_DDL_SORT_PROVIDER_H_

#include "lib/container/ob_array.h"
#include "lib/lock/ob_mutex.h"
#include "sql/engine/basic/ob_temp_block_store.h"

namespace oceanbase
{
namespace sql
{

class ObDDLSortProvider final
{
public:
  ObDDLSortProvider() : reuse_queue_(), mutex_() {}
  ~ObDDLSortProvider() = default;

  static int calc_merge_ways(ObSqlMemMgrProcessor &sql_mem_processor,
                             lib::MemoryContext mem_context,
                             const int64_t max_ways,
                             int64_t &merge_ways)
  {
    int ret = OB_SUCCESS;
    merge_ways = 0;
    if (OB_UNLIKELY(max_ways < 2)) {
      ret = OB_INVALID_ARGUMENT;
      SQL_ENG_LOG(WARN, "invalid ddl merge ways", K(ret), K(max_ways));
    } else if (OB_FAIL(sql_mem_processor.get_max_available_mem_size(
                   &mem_context->get_malloc_allocator()))) {
      SQL_ENG_LOG(WARN, "failed to get max available memory size", K(ret));
    } else {
      const int64_t block_size = ObTempBlockStore::BLOCK_SIZE;
      const int64_t min_ways = std::max(static_cast<int64_t>(2),
                                        (16L * 1024L * 1024L) / block_size);
      merge_ways = std::max(min_ways, sql_mem_processor.get_mem_bound() / block_size);
      if (merge_ways < max_ways) {
        bool dumped = false;
        const int64_t need_size = max_ways * block_size;
        if (OB_FAIL(sql_mem_processor.extend_max_memory_size(
                &mem_context->get_malloc_allocator(),
                [&](int64_t max_memory_size) { return max_memory_size < need_size; },
                dumped, mem_context->used()))) {
          SQL_ENG_LOG(WARN, "failed to extend ddl merge memory", K(ret));
        } else {
          merge_ways = std::max(merge_ways, sql_mem_processor.get_mem_bound() / block_size);
        }
      }
      merge_ways = std::min(merge_ways, max_ways);
    }
    return ret;
  }

  int push_reuse_handle(void *handle)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(handle)) {
      ret = OB_INVALID_ARGUMENT;
      SQL_ENG_LOG(WARN, "invalid ddl sort reuse handle", K(ret), KP(handle));
    } else {
      lib::ObMutexGuard guard(mutex_);
      if (OB_FAIL(reuse_queue_.push_back(handle))) {
        SQL_ENG_LOG(WARN, "push ddl sort reuse handle failed", K(ret));
      }
    }
    return ret;
  }

  int pop_reuse_handle(void *&handle)
  {
    int ret = OB_SUCCESS;
    handle = nullptr;
    lib::ObMutexGuard guard(mutex_);
    if (!reuse_queue_.empty()) {
      handle = reuse_queue_.at(reuse_queue_.count() - 1);
      reuse_queue_.pop_back();
    }
    return ret;
  }

private:
  common::ObArray<void *> reuse_queue_;
  lib::ObMutex mutex_;

  DISALLOW_COPY_AND_ASSIGN(ObDDLSortProvider);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_DDL_SORT_PROVIDER_H_ */
