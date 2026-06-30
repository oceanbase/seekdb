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


#include "ob_mds_allocator.h"
#include "share/rc/ob_module_provider.h"
#include "share/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::storage::mds;

namespace oceanbase {
namespace share {

int64_t ObTenantMdsAllocator::resource_unit_size()
{
  static const int64_t MDS_RESOURCE_UNIT_SIZE = OB_MALLOC_NORMAL_BLOCK_SIZE; /* 8KB */
  return MDS_RESOURCE_UNIT_SIZE;
}

void ObTenantMdsAllocator::init_throttle_config(int64_t &resource_limit, int64_t &trigger_percentage, int64_t &max_duration)
{
  // define some default value
  const int64_t MDS_LIMIT_PERCENTAGE = 5;
  const int64_t MDS_THROTTLE_TRIGGER_PERCENTAGE = 60;
  const int64_t MDS_THROTTLE_MAX_DURATION = 2LL * 60LL * 60LL * 1000LL * 1000LL;  // 2 hours

  int64_t total_memory = lib::get_tenant_memory_limit();

  // init throttle config from cluster config
  resource_limit = total_memory * GCONF._mds_memory_limit_percentage / 100LL;
  trigger_percentage = GCONF.writing_throttling_trigger_percentage;
  max_duration = GCONF.writing_throttling_maximum_duration;
}
void ObTenantMdsAllocator::adaptive_update_limit(const int64_t holding_size,
                                                 const int64_t config_specify_resource_limit,
                                                 int64_t &resource_limit,
                                                 int64_t &last_update_limit_ts,
                                                 bool &is_updated)
{
  // do nothing
}

int ObTenantMdsAllocator::init()
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  // TODO : @gengli new ctx id?
  
  mem_attr.ctx_id_ = ObCtxIds::MDS_DATA_ID;
  mem_attr.label_ = "MdsTable";
  ObSharedMemAllocMgr *share_mem_alloc_mgr = share::g_mp->shared_mem_alloc_mgr();
  throttle_tool_ = &(share_mem_alloc_mgr->share_resource_throttle_tool());
  MDS_TG(10_ms);
  if (IS_INIT){
    ret = OB_INIT_TWICE;
    SHARE_LOG(WARN, "init tenant mds allocator twice", KR(ret), KPC(this));
  } else if (OB_ISNULL(throttle_tool_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (MDS_FAIL(allocator_.init(OB_MALLOC_NORMAL_BLOCK_SIZE, block_alloc_, mem_attr))) {
    MDS_LOG(WARN, "init vslice allocator failed", K(ret), K(OB_MALLOC_NORMAL_BLOCK_SIZE), KP(this), K(mem_attr));
  } else {
    allocator_.set_nway(MDS_ALLOC_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}

void *ObTenantMdsAllocator::alloc(const int64_t size)
{
  int64_t abs_expire_time = THIS_WORKER.get_timeout_ts();
  return alloc(size, abs_expire_time);
}

void *ObTenantMdsAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  UNUSED(attr);
  void *obj = alloc(size);
  MDS_LOG_RET(WARN, OB_INVALID_ARGUMENT, "VSLICE Allocator not support mark attr", KP(obj), K(size), K(attr));
  return obj;
}

void *ObTenantMdsAllocator::alloc(const int64_t size, const int64_t abs_expire_time)
{
  bool is_throttled = false;

  // record alloc resource in throttle tool, but do not throttle immediately
  // ObMdsThrottleGuard calls the real throttle logic
  (void)throttle_tool_->alloc_resource<ObTenantMdsAllocator>(size, abs_expire_time, is_throttled);

  // if is throttled, do throttle
  if (OB_UNLIKELY(is_throttled)) {
    share::mds_throttled_alloc() += size;
  }

  void *obj = allocator_.alloc(size);
  if (OB_NOT_NULL(obj)) {
    share::g_mp->tenant_mds_service()
        ->record_alloc_backtrace(obj,
                                 __thread_mds_tag__,
                                 __thread_mds_alloc_type__,
                                 __thread_mds_alloc_file__,
                                 __thread_mds_alloc_func__,
                                 __thread_mds_alloc_line__);  // for debug mem leak
  }
  return obj;
}


void ObTenantMdsAllocator::free(void *ptr)
{
  allocator_.free(ptr);
  share::g_mp->tenant_mds_service()->erase_alloc_backtrace(ptr);
}

void ObTenantMdsAllocator::set_attr(const ObMemAttr &attr) { allocator_.set_attr(attr); }

void *ObTenantBufferCtxAllocator::alloc(const int64_t size)
{
  void *obj = share::mtl_malloc(size, ObMemAttr("MDS_CTX_DEFAULT", ObCtxIds::MDS_CTX_ID));
  if (OB_NOT_NULL(obj)) {
    share::g_mp->tenant_mds_service()->record_alloc_backtrace(obj,
                                                     __thread_mds_tag__,
                                                     __thread_mds_alloc_type__,
                                                     __thread_mds_alloc_file__,
                                                     __thread_mds_alloc_func__,
                                                     __thread_mds_alloc_line__);// for debug mem leak
  }
  return obj;
}

void *ObTenantBufferCtxAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  void *obj = share::mtl_malloc(size, attr);
  if (OB_NOT_NULL(obj)) {
    share::g_mp->tenant_mds_service()->record_alloc_backtrace(obj,
                                                     __thread_mds_tag__,
                                                     __thread_mds_alloc_type__,
                                                     __thread_mds_alloc_file__,
                                                     __thread_mds_alloc_func__,
                                                     __thread_mds_alloc_line__);// for debug mem leak
  }
  return obj;
}

void ObTenantBufferCtxAllocator::free(void *ptr)
{
  share::mtl_free(ptr);
  share::g_mp->tenant_mds_service()->erase_alloc_backtrace(ptr);
}

ObMdsThrottleGuard::ObMdsThrottleGuard(const share::ObLSID ls_id, const bool for_replay, const int64_t abs_expire_time)
    : ls_id_(ls_id), for_replay_(for_replay), abs_expire_time_(abs_expire_time)
{
  throttle_tool_ = &(share::g_mp->shared_mem_alloc_mgr()->share_resource_throttle_tool());
  if (0 == abs_expire_time) {
    abs_expire_time_ =
        ObClockGenerator::getClock() + ObThrottleUnit<ObMdsThrottleGuard>::DEFAULT_MAX_THROTTLE_TIME;
  }
  share::mds_throttled_alloc() = 0;
}

ObMdsThrottleGuard::~ObMdsThrottleGuard()
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObThrottleInfoGuard share_ti_guard;
  ObThrottleInfoGuard module_ti_guard;

  if (OB_ISNULL(throttle_tool_)) {
    MDS_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "throttle tool is unexpected nullptr", KP(throttle_tool_));
  } else if (throttle_tool_->is_throttling<ObTenantMdsAllocator>(share_ti_guard, module_ti_guard)) {

    if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
    } else if (OB_ISNULL(ls_handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(ERROR, "get ls handle failed", KR(ret), K(ls_id_));
    } else {
      (void)TxShareMemThrottleUtil::do_throttle<ObTenantMdsAllocator>(for_replay_,
                                                                      abs_expire_time_,
                                                                      share::mds_throttled_alloc(),
                                                                      *(ls_handle.get_ls()),
                                                                      *throttle_tool_,
                                                                      share_ti_guard,
                                                                      module_ti_guard);
    }

    if (throttle_tool_->still_throttling<ObTenantMdsAllocator>(share_ti_guard, module_ti_guard)) {
      (void)throttle_tool_->skip_throttle<ObTenantMdsAllocator>(
          share::mds_throttled_alloc(), share_ti_guard, module_ti_guard);

      if (module_ti_guard.is_valid()) {
        module_ti_guard.throttle_info()->reset();
      }
    } 

    // reset mds throttled alloc size
    share::mds_throttled_alloc() = 0;
  } else {
    // do not need throttle, exit directly
  }
}

}  // namespace share
}  // namespace oceanbase
