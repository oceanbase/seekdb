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

#ifndef OCEABASE_LIB_OB_RB_MEMORY_MGR_
#define OCEABASE_LIB_OB_RB_MEMORY_MGR_

#include "ob_roaringbitmap.h"
#include "lib/allocator/ob_vslice_alloc.h"
#include "lib/allocator/ob_block_alloc_mgr.h"

namespace oceanbase
{
namespace common
{
static roaring_memory_t roaring_memory_mgr;

class ObRbMemMgr
{
public:
  static int init_memory_hook();

private:
  static const int64_t RB_ALLOC_CONCURRENCY = 32;

public:
  ObRbMemMgr() : is_inited_(false), vec_idx_used_(0), block_alloc_(), allocator_() {}
  ~ObRbMemMgr() {}
  static int mtl_init(ObRbMemMgr *&rb_allocator) { return rb_allocator->init(); };

  int init();
  int start() { return OB_SUCCESS; }
  void stop() {}
  void wait() {}
  void destroy();

  void *alloc(size_t size);
  void free(void *ptr);
  void incr_vec_idx_used(size_t size);
  void decr_vec_idx_used(size_t size);
  int64_t get_vec_idx_used() { return ATOMIC_LOAD(&vec_idx_used_); }

private:
  bool is_inited_;
  int64_t vec_idx_used_;
  common::ObBlockAllocMgr block_alloc_;
  common::ObVSliceAlloc allocator_;
};

} // common
} // oceanbase

#endif // OCEABASE_LIB_OB_RB_MEMORY_MGR_
