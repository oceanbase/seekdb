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
#include "common/errsim_module/ob_errsim_module_interface.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;

namespace oceanbase
{
namespace lib
{

void set_hard_memory_limit(int64_t bytes)
{
  const uint64_t tenant_id = OB_SYS_TENANT_ID;
  // set resource manager hard memory limit
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_tenant_hard_limit(tenant_id, bytes);
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
  const uint64_t tenant_id = OB_SYS_TENANT_ID;
  // set resource manager memory limit
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_tenant_limit(tenant_id, bytes);
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
  return get_hard_memory_limit() - get_memory_used() + get_tenant_cache_hold(OB_SYS_TENANT_ID);
}

void set_tenant_memory_limit(uint64_t tenant_id, int64_t bytes)
{
  // set resource manager memory limit
  if (OB_SYS_TENANT_ID != tenant_id) return;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_tenant_limit(tenant_id, bytes);
  }

  // set chunk manager memory limit
  CHUNK_MGR.set_limit(bytes);
}

int64_t get_tenant_memory_limit(uint64_t tenant_id)
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_tenant_limit(tenant_id);
  }
  return bytes;
}

int64_t get_tenant_memory_hold(uint64_t tenant_id)
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_tenant_hold(tenant_id);
  }
  return bytes;
}

int64_t get_tenant_memory_hold(const uint64_t tenant_id, const uint64_t ctx_id)
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_tenant_ctx_hold(tenant_id, ctx_id);
  }
  return bytes;
}

int64_t get_tenant_cache_hold(uint64_t tenant_id)
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_tenant_cache_hold(tenant_id);
  }
  return bytes;
}

int64_t get_tenant_memory_remain(uint64_t tenant_id)
{
  int64_t bytes = 0;
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    bytes = allocator->get_tenant_remain(tenant_id);
  }
  return bytes;
}

void get_tenant_label_memory(
  uint64_t tenant_id, ObLabel &label,
  common::ObLabelItem &item)
{
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->get_tenant_label_usage(tenant_id, label, item);
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

int set_ctx_limit(uint64_t tenant_id, uint64_t ctx_id, const int64_t limit)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  int ret = OB_SUCCESS;
  ObMallocAllocator *alloc = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(alloc)) {
    auto ta = alloc->get_tenant_ctx_allocator(tenant_id, ctx_id);
    if (OB_NOT_NULL(ta)) {
      if (OB_FAIL(ta->set_limit(limit))) {
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

int set_wa_limit(uint64_t tenant_id, int64_t wa_pctg)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  const int64_t tenant_limit = get_tenant_memory_limit(tenant_id);
  // For small tenants, work_area may only have dozens of M, which is unavailable. Give work_area a lower limit
  const int64_t lower_limit = 150L << 20;
  const int64_t wa_limit =
    std::min(static_cast<int64_t>(tenant_limit*0.8),
             std::max(lower_limit, (tenant_limit/100) * wa_pctg));
  return set_ctx_limit(tenant_id, common::ObCtxIds::WORK_AREA, wa_limit);
}

int set_meta_obj_limit(uint64_t tenant_id, int64_t meta_obj_pct_lmt)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  const int64_t tenant_limit = get_tenant_memory_limit(tenant_id);
  const int64_t ctx_limit = 0 == meta_obj_pct_lmt ? tenant_limit : (tenant_limit / 100) * meta_obj_pct_lmt;

  return set_ctx_limit(tenant_id, common::ObCtxIds::META_OBJ_CTX_ID, ctx_limit);
}

int set_rpc_limit(uint64_t tenant_id, int64_t rpc_pct_lmt)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  const int64_t tenant_limit = get_tenant_memory_limit(tenant_id);
  const int64_t rpc_lmt = (tenant_limit / 100) * rpc_pct_lmt;
  return set_ctx_limit(tenant_id, common::ObCtxIds::RPC_CTX_ID, rpc_lmt);
}

bool errsim_alloc(const ObMemAttr &attr)
{
  bool bret = OB_SUCCESS != EventTable::EN_4;
#ifdef ERRSIM
  const ObErrsimModuleType type = THIS_WORKER.get_module_type();
  if (is_errsim_module(attr.tenant_id_, type.type_)) {
    //errsim alloc memory failed.
    bret = true;
  }
#endif
  if (bret) {
    AllocFailedCtx &afc = g_alloc_failed_ctx();
    afc.reason_ = AllocFailedReason::ERRSIM_INJECTION;
  }
  return bret;
}

int set_req_chunkmgr_parallel(uint64_t tenant_id, uint64_t ctx_id, int32_t parallel)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  int ret = OB_SUCCESS;
  ObMallocAllocator *ma = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(ma)) {
    ObTenantCtxAllocatorGuard ta = ma->get_tenant_ctx_allocator(tenant_id, ctx_id);
    if (OB_NOT_NULL(ta)) {
      ta->set_req_chunkmgr_parallel(parallel);
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
