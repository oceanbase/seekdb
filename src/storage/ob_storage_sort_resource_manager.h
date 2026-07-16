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

#ifndef OB_STORAGE_SORT_RESOURCE_MANAGER_H_
#define OB_STORAGE_SORT_RESOURCE_MANAGER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace storage
{

class ObStorageSortResourceManager
{
public:
  static const int64_t MIN_MEMORY_LIMIT = 8LL * 1024 * 1024;
  static const int64_t DEFAULT_FILE_BUF_SIZE = 2LL * 1024 * 1024;
  static const int64_t MIN_MERGE_WAYS = 2;
  static const int64_t MAX_MERGE_WAYS = 256;

  ObStorageSortResourceManager()
    : mem_limit_(MIN_MEMORY_LIMIT), file_buf_size_(DEFAULT_FILE_BUF_SIZE),
      merge_ways_(MIN_MERGE_WAYS) {}

  int init(int64_t mem_limit, int64_t file_buf_size = DEFAULT_FILE_BUF_SIZE)
  {
    int ret = OB_SUCCESS;
    mem_limit_ = MAX(mem_limit, MIN_MEMORY_LIMIT);
    file_buf_size_ = file_buf_size;
    merge_ways_ = (mem_limit_ - MIN_MEMORY_LIMIT) / file_buf_size_ / 2;
    if (merge_ways_ < MIN_MERGE_WAYS) {
      merge_ways_ = MIN_MERGE_WAYS;
    }
    if (merge_ways_ > MAX_MERGE_WAYS) {
      merge_ways_ = MAX_MERGE_WAYS;
    }
    return ret;
  }

  OB_INLINE int64_t get_mem_limit() const { return mem_limit_; }
  OB_INLINE int64_t get_file_buf_size() const { return file_buf_size_; }
  OB_INLINE int64_t get_merge_ways() const { return merge_ways_; }

private:
  int64_t mem_limit_;
  int64_t file_buf_size_;
  int64_t merge_ways_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_STORAGE_SORT_RESOURCE_MANAGER_H_ */
