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

#define USING_LOG_PREFIX SHARE


#include "ob_tx_data_allocator.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase {

namespace share {

thread_local int64_t ObTxDataOpAllocator::local_alloc_size_ = 0;

int64_t ObTxDataAllocator::hold() const
{
  ObSharedMemAllocMgr *manager = nullptr != share::g_mp
      ? share::g_mp->shared_mem_alloc_mgr()
      : nullptr;
  return nullptr != manager ? manager->tx_data_quota_used() : slice_backing();
}

int64_t ObTxDataAllocator::resource_unit_size()
{
  static const int64_t TX_DATA_RESOURCE_UNIT_SIZE = OB_MALLOC_NORMAL_BLOCK_SIZE; /* 8KB */
  return TX_DATA_RESOURCE_UNIT_SIZE;
}

void ObTxDataAllocator::init_throttle_config(int64_t &resource_limit,
                                                   int64_t &trigger_percentage,
                                                   int64_t &max_duration)
{
  resource_limit = lib::get_tx_data_memory_limit();
  trigger_percentage = GCONF.writing_throttling_trigger_percentage;
  max_duration = GCONF.writing_throttling_maximum_duration;
}
void ObTxDataAllocator::adaptive_update_limit(const int64_t holding_size,
                                              const int64_t config_specify_resource_limit,
                                              int64_t &resource_limit,
                                              int64_t &last_update_limit_ts,
                                              bool &is_updated)
{
  (void)holding_size;
  (void)last_update_limit_ts;
  is_updated = resource_limit != config_specify_resource_limit;
  resource_limit = config_specify_resource_limit;
}

void ObTxDataAllocator::reset()
{
  is_inited_ = false;
  slice_allocator_.purge_extra_cached_block(0);
}

ObTxDataThrottleGuard::ObTxDataThrottleGuard(const bool for_replay,
                                             const int64_t abs_expire_time)
    : for_replay_(for_replay), abs_expire_time_(abs_expire_time)
{
  throttle_tool_ = &(::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->share_resource_throttle_tool());
  if (0 == abs_expire_time) {
    abs_expire_time_ =
        ObClockGenerator::getClock() + ObThrottleUnit<ObTxDataAllocator>::DEFAULT_MAX_THROTTLE_TIME;
  }
  share::tx_data_throttled_alloc() = 0;
}

int ObTxDataOpAllocator::init()
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  mem_attr.ctx_id_ = ObCtxIds::TX_DATA_TABLE;
  mem_attr.label_ = "TX_OP";
  ObSharedMemAllocMgr *share_mem_alloc_mgr = ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>();
  throttle_tool_ = &(share_mem_alloc_mgr->share_resource_throttle_tool());
  if (IS_INIT){
    ret = OB_INIT_TWICE;
    SHARE_LOG(WARN, "init MDS allocator twice", KR(ret), KPC(this));
  } else if (OB_ISNULL(throttle_tool_)) {
    ret = OB_ERR_UNEXPECTED;
    SHARE_LOG(WARN, "throttle tool is unexpected null", KP(throttle_tool_), KP(share_mem_alloc_mgr));
  } else if (OB_FAIL(allocator_.init(OB_MALLOC_NORMAL_BLOCK_SIZE, block_alloc_, mem_attr))) {
    MDS_LOG(WARN, "init vslice allocator failed", K(ret), K(OB_MALLOC_NORMAL_BLOCK_SIZE), KP(this), K(mem_attr));
  } else {
    allocator_.set_nway(MDS_ALLOC_CONCURRENCY);
    is_inited_ = true;
  }
  return ret;
}

void *ObTxDataOpAllocator::alloc(const int64_t size)
{
  int64_t abs_expire_time = THIS_WORKER.get_timeout_ts();
  void * buf = alloc(size, abs_expire_time);
  if (OB_NOT_NULL(buf)) {
    local_alloc_size_ += size;
  }
  return buf;
}

void *ObTxDataOpAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  UNUSED(attr);
  void *obj = alloc(size);
  return obj;
}

void *ObTxDataOpAllocator::alloc(const int64_t size, const int64_t abs_expire_time)
{
  bool is_throttled = false;
  (void)throttle_tool_->alloc_resource<ObTxDataAllocator>(
      size, abs_expire_time, is_throttled);
  if (OB_UNLIKELY(is_throttled)) {
    share::tx_data_throttled_alloc() += size;
  }
  void *obj = allocator_.alloc(size);
  return obj;
}

void ObTxDataOpAllocator::free(void *ptr)
{
  allocator_.free(ptr);
}

void ObTxDataOpAllocator::set_attr(const ObMemAttr &attr) { allocator_.set_attr(attr); }

}  // namespace share
}  // namespace oceanbase
