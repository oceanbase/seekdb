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
#include "sql/engine/sort/ob_sort_resource_manager.h"

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
    return ObSortResourceManager::calc_merge_ways(sql_mem_processor,
                                                  mem_context,
                                                  max_ways,
                                                  ObSortResourceManager::get_ddl_merge_buffer_size(),
                                                  merge_ways);
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
