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

#ifndef OCEANBASE_ALLOCATOR_OB_BLOCK_ALLOC_MGR_H_
#define OCEANBASE_ALLOCATOR_OB_BLOCK_ALLOC_MGR_H_

#include "lib/allocator/ob_malloc.h"
#include "common/ob_clock_generator.h"

namespace oceanbase
{
namespace common
{
class ObBlockAllocMgr;
extern ObBlockAllocMgr default_blk_alloc;

class ObBlockAllocMgr
{
public:
  ObBlockAllocMgr(int64_t limit = INT64_MAX): limit_(limit), hold_(0) {}
  ~ObBlockAllocMgr() {}
  void set_limit(int64_t limit) { ATOMIC_STORE(&limit_, limit); }
  int64_t limit() const { return ATOMIC_LOAD(&limit_); }
  int64_t hold() const { return ATOMIC_LOAD(&hold_); }
  void* alloc_block(int64_t size, ObMemAttr &attr) {
    void *ret = NULL;
    int64_t used_after_alloc = ATOMIC_AAF(&hold_, size);
    if (used_after_alloc > limit_) {
      ATOMIC_AAF(&hold_, -size);
      if (REACH_TIME_INTERVAL(1000 * 1000)) {
        _OB_LOG_RET(WARN, common::OB_ALLOCATE_MEMORY_FAILED, "block alloc over limit, limit=%ld alloc_size=%ld", limit_, size);
      }
    } else if (NULL == (ret = (void*)ob_malloc(size, attr))) {
      ATOMIC_AAF(&hold_, -size);
    } else {}
    return ret;
  }
  void free_block(void *ptr, int64_t size) {
    if (NULL != ptr) {
      ob_free(ptr);
      ATOMIC_FAA(&hold_, -size);
    }
  }
  bool can_alloc_block(int64_t size) const {
    return  ((limit_ - hold_) > size);
  }
private:
  int64_t limit_;
  int64_t hold_;
};
}; // end namespace common
}; // end namespace oceanbase
#endif /* OCEANBASE_ALLOCATOR_OB_BLOCK_ALLOC_MGR_H_ */
