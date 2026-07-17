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

#ifndef OB_DDL_SORT_PROVIDER_H_
#define OB_DDL_SORT_PROVIDER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/thread/ob_simple_mutex.h"
#include "lib/queue/ob_fixed_queue.h"

namespace oceanbase
{
namespace storage
{

static const int64_t DDL_SORT_MEM_BUDGET = 16LL * 1024 * 1024;
static const int64_t DDL_SORT_MAX_REUSE = 8;

class ObDDLSortHandle
{
public:
  ObDDLSortHandle() : is_inited_(false) {}
  ~ObDDLSortHandle() {}

  int init(ObIAllocator &allocator, int64_t mem_budget)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    mem_budget_ = mem_budget;
    is_inited_ = true;
    return ret;
  }

  void reuse()
  {
    is_inited_ = false;
  }

  OB_INLINE int64_t get_mem_budget() const { return mem_budget_; }

private:
  bool is_inited_;
  ObIAllocator *allocator_;
  int64_t mem_budget_;
};

class ObDDLSortProvider
{
public:
  ObDDLSortProvider()
    : is_inited_(false), max_handles_(DDL_SORT_MAX_REUSE)
  {
  }

  int init(int64_t max_handles = DDL_SORT_MAX_REUSE)
  {
    int ret = OB_SUCCESS;
    max_handles_ = max_handles;
    is_inited_ = true;
    return ret;
  }

  int acquire_handle(ObDDLSortHandle *&handle)
  {
    int ret = OB_SUCCESS;
    handle = nullptr;
    return ret;
  }

  int release_handle(ObDDLSortHandle *handle)
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int64_t calc_merge_ways(int64_t block_size)
  {
    if (block_size > 0) {
      return MAX(2LL, DDL_SORT_MEM_BUDGET / block_size);
    }
    return 2;
  }

  void destroy()
  {
    is_inited_ = false;
  }

private:
  bool is_inited_;
  int64_t max_handles_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_DDL_SORT_PROVIDER_H_ */
