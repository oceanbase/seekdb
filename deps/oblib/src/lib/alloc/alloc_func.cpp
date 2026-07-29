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

#include "alloc_func.h"
#include "lib/alloc/ob_malloc_allocator.h"
#include "lib/utility/ob_tracepoint.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;

namespace oceanbase
{
namespace lib
{

void set_hard_memory_limit(int64_t bytes)
{
  
  // Set the allocator hard memory limit.
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_allocator_hard_limit(bytes);
  }

  // set chunk manager hard memory limit
  CHUNK_MGR.set_hard_limit(bytes);
}

int64_t get_hard_memory_limit()
{
  return CHUNK_MGR.get_hard_limit();
}

void set_memory_limit(int64_t bytes)
{
  
  // Set the allocator memory limit.
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_allocator_limit(bytes);
  }

  // set chunk manager memory limit
  CHUNK_MGR.set_limit(bytes);
}

int64_t get_memory_limit()
{
  return CHUNK_MGR.get_limit();
}

int64_t get_memory_hold()
{
  return CHUNK_MGR.get_hold();
}

int64_t get_memory_used()
{
  return CHUNK_MGR.get_used();
}

int64_t get_memory_avail()
{
  return get_memory_limit() - get_memory_used();
}

int64_t get_hard_memory_remain()
{
  return get_hard_memory_limit() - get_memory_used() + get_allocator_cache_hold();
}

void set_allocator_memory_limit(int64_t bytes)
{
  // Set the allocator memory limit.
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_allocator_limit(bytes);
  }

  // set chunk manager memory limit
  CHUNK_MGR.set_limit(bytes);
}

int64_t get_allocator_memory_limit()
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_total_limit();
  }
  return bytes;
}

int64_t get_allocator_memory_hold()
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_total_hold();
  }
  return bytes;
}

int64_t get_allocator_memory_hold(const uint64_t ctx_id)
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_ctx_hold(ctx_id);
  }
  return bytes;
}

int64_t get_allocator_cache_hold()
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_allocator_cache_hold();
  }
  return bytes;
}

int64_t get_allocator_memory_remain()
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_allocator_remain();
  }
  return bytes;
}

void get_label_memory(
  ObLabel &label,
  common::ObLabelItem &item)
{
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->get_label_usage(label, item);
  }
}

void ob_set_reserved_memory(const int64_t bytes)
{
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_reserved(bytes);
  }
}

int64_t ob_get_reserved_memory()
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_reserved();
  }
  return bytes;
}

int set_ctx_limit(uint64_t ctx_id, const int64_t limit)
{
  int ret = OB_SUCCESS;
  ObMallocAllocator *alloc = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(alloc)) {
    auto ctx_allocator = alloc->get_ctx_allocator(ctx_id);
    if (OB_NOT_NULL(ctx_allocator)) {
      if (OB_FAIL(ctx_allocator->set_limit(limit))) {
        LIB_LOG(WARN, "set_limit failed", K(ret), K(limit));
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
  } else {
    ret = OB_NOT_INIT;
  }
  return ret;
}

int set_wa_limit(int64_t wa_pctg)
{
  const int64_t memory_limit = get_allocator_memory_limit();
  // Keep a practical lower bound for the work area on small servers.
  const int64_t lower_limit = 150L << 20;
  const int64_t wa_limit =
    std::min(static_cast<int64_t>(memory_limit * 0.8),
             std::max(lower_limit, (memory_limit / 100) * wa_pctg));
  return set_ctx_limit(common::ObCtxIds::WORK_AREA, wa_limit);
}

int set_meta_obj_limit(int64_t meta_obj_pct_lmt)
{
  const int64_t memory_limit = get_allocator_memory_limit();
  const int64_t ctx_limit = 0 == meta_obj_pct_lmt ? memory_limit : (memory_limit / 100) * meta_obj_pct_lmt;

  return set_ctx_limit(common::ObCtxIds::META_OBJ_CTX_ID, ctx_limit);
}

bool errsim_alloc(const ObMemAttr &attr)
{
  int en4_val = (int)EventTable::EN_4;
  bool bret = OB_SUCCESS != en4_val;
  if (bret) {
    AllocFailedCtx &afc = g_alloc_failed_ctx();
    afc.reason_ = AllocFailedReason::ERRSIM_INJECTION;
  }
  return bret;
}

int set_req_chunkmgr_parallel(uint64_t ctx_id, int32_t parallel)
{
  int ret = OB_SUCCESS;
  ObMallocAllocator *ma = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(ma)) {
    ObCtxAllocatorGuard ctx_allocator = ma->get_ctx_allocator(ctx_id);
    if (OB_NOT_NULL(ctx_allocator)) {
      ctx_allocator->set_req_chunkmgr_parallel(parallel);
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
  } else {
    ret = OB_NOT_INIT;
  }
  return ret;
}

} // end of namespace lib
} // end of namespace oceanbase
