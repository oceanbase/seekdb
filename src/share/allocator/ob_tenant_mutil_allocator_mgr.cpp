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

#include "share/allocator/ob_shared_memory_allocator_mgr.h"
#include "share/rc/ob_module_provider.h"
#include "share/allocator/ob_tenant_mutil_allocator_mgr.h"

using namespace oceanbase::share;

namespace oceanbase
{
namespace common
{

int ObTenantMutilAllocatorMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    tma_ = NULL;
    is_inited_ = true;
  }
  return ret;
}

// Get the log allocator for specified tenant, create it when tenant not exist
int ObTenantMutilAllocatorMgr::get_tenant_log_allocator(ObILogAllocator *&out_allocator)
{
  int ret = OB_SUCCESS;
  ObTenantMutilAllocator *allocator = NULL;
  if (OB_FAIL(get_tenant_mutil_allocator_(allocator))) {
  } else {
    out_allocator = allocator;
  }
  return ret;
}

int ObTenantMutilAllocatorMgr::delete_tenant_log_allocator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(delete_tenant_mutil_allocator_())) {
    OB_LOG(WARN, "delete_tenant_mutil_allocator_ failed", K(ret));
  } else {
    OB_LOG(INFO, "delete_tenant_mutil_allocator_ success");
  }
  return ret;
}

int ObTenantMutilAllocatorMgr::get_tenant_mutil_allocator_(TMA *&out_allocator)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    // Single process-level account.
    // Need rlock
    do {
      obsys::ObRLockGuard guard(lock_);
      out_allocator = ATOMIC_LOAD(&tma_);
    } while(0);

    if (NULL == out_allocator) {
      // Need create new allocator
      if (OB_FAIL(create_tenant_mutil_allocator_(out_allocator))) {
        OB_LOG(WARN, "fail to create_tenant_mutil_allocator_", K(ret));
      }
    }
  }

  if (OB_SUCC(ret) && OB_ISNULL(out_allocator)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "got allocator is NULL", K(ret));
  }

  return ret;
}

int ObTenantMutilAllocatorMgr::get_tenant_memstore_limit_percent_(int64_t &limit_percent) const
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    MOD_SCOPE {
      limit_percent = share::g_mp->tenant_freezer()->get_memstore_limit_percentage();
    }
  }
  return ret;
}

int ObTenantMutilAllocatorMgr::construct_allocator_(TMA *&out_allocator)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    ObMemAttr attr(ObModIds::OB_TENANT_MUTIL_ALLOCATOR);
    SET_USE_500(attr);
    void *buf = ob_malloc(sizeof(TMA), attr);
    if (NULL == buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "failed to alloc memory", K(ret));
    } else {
      TMA *allocator = new (buf) TMA{};
      out_allocator = allocator;
      OB_LOG(INFO, "ObTenantMutilAllocator init success");
    }
  }
  return ret;
}

int ObTenantMutilAllocatorMgr::create_tenant_mutil_allocator_(TMA *&out_allocator)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    // Single process-level account.
    // wlock
    obsys::ObWLockGuard guard(lock_);
    if (NULL != (out_allocator = ATOMIC_LOAD(&tma_))) {
    } else {
      TMA *tmp_tma = NULL;
      if (OB_FAIL(construct_allocator_(tmp_tma))) {
        OB_LOG(WARN, "fail to construct_allocator_", K(ret));
      } else if (!ATOMIC_BCAS(&tma_, NULL, tmp_tma)) {
        out_allocator = ATOMIC_LOAD(&tma_);
        if (NULL != tmp_tma) {
          tmp_tma->~TMA();
          ob_free(tmp_tma);
        }
      } else {
        out_allocator = ATOMIC_LOAD(&tma_);
      }
    }
  }

  return ret;
}

int ObTenantMutilAllocatorMgr::delete_tenant_mutil_allocator_()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    // Single process-level account.
    // Need wlock
    obsys::ObWLockGuard guard(lock_);
    TMA *tma_allocator = NULL;
    if (NULL != (tma_allocator = ATOMIC_LOAD(&tma_))) {
      tma_ = NULL;
      // destroy tma object
      tma_allocator->~TMA();
      ob_free(tma_allocator);
      tma_allocator = NULL;
    }
  }

  return ret;
}

ObTenantMutilAllocatorMgr &ObTenantMutilAllocatorMgr::get_instance()
{
  static ObTenantMutilAllocatorMgr instance_;
  return instance_;
}

int ObTenantMutilAllocatorMgr::update_tenant_mem_limit(const share::ObUnitInfoGetter::ObTenantConfig &tenant_config)
{
  // Update mem_limit for the (single) tenant, called when unit specifications or
  // memstore_limite_percentage change
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
      int64_t cur_memstore_limit_percent = 0;
      
      const bool has_memstore = tenant_config.has_memstore_;
      int32_t nway = (int32_t)(tenant_config.config_.max_cpu());
      if (nway == 0) {
        nway = 1;
      }
      const int64_t memory_size = tenant_config.config_.memory_size();
      int64_t new_tma_limit = memory_size;
      if (has_memstore) {
        // If the unit type of tenant is not Log, need to subtract
        // the reserved memory of memstore
        if (OB_TMP_FAIL(get_tenant_memstore_limit_percent_(cur_memstore_limit_percent))) {
          OB_LOG(WARN, "memstore_limit_percentage val is unexpected", K(cur_memstore_limit_percent));
        } else if (cur_memstore_limit_percent > 100 || cur_memstore_limit_percent <= 0) {
          OB_LOG(WARN, "memstore_limit_percentage val is unexpected", K(cur_memstore_limit_percent));
        } else {
          new_tma_limit = memory_size / 100 * ( 100 - cur_memstore_limit_percent);
        }
      }
      ObTenantMutilAllocator *tma= NULL;
      if (OB_TMP_FAIL(get_tenant_mutil_allocator_(tma))) {
        OB_LOG(WARN, "get_tenant_mutil_allocator_ failed", K(tmp_ret));
      } else if (NULL == tma) {
        OB_LOG(WARN, "get_tenant_mutil_allocator_ failed");
      } else {
        tma->set_nway(nway);
        int64_t pre_tma_limit = tma->get_limit();
        if (pre_tma_limit != new_tma_limit) {
          tma->set_limit(new_tma_limit);
        }
        OB_LOG(INFO, "ObTenantMutilAllocator update tenant mem_limit finished", K(ret),
             K(nway), K(new_tma_limit), K(pre_tma_limit), K(cur_memstore_limit_percent), K(tenant_config));
      }

      //update memstore threshold of MemstoreAllocator
      MOD_SCOPE {
        ObMemstoreAllocator &memstore_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
        if (OB_FAIL(memstore_allocator.set_memstore_threshold())) {
          OB_LOG(WARN, "failed to set_memstore_threshold of memstore allocator", K(ret));
        } else {
          OB_LOG(INFO, "succ to set_memstore_threshold of memstore allocator", K(ret));
        }
      }
  }
  return ret;
}

}
}
