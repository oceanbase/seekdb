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
#include "share/rc/ob_server_runtime.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"

using namespace oceanbase::storage::mds;

namespace oceanbase {
namespace share {

int64_t ObMdsAllocator::resource_unit_size()
{
  static const int64_t MDS_RESOURCE_UNIT_SIZE = OB_MALLOC_NORMAL_BLOCK_SIZE; /* 8KB */
  return MDS_RESOURCE_UNIT_SIZE;
}

void ObMdsAllocator::init_throttle_config(int64_t &resource_limit, int64_t &trigger_percentage, int64_t &max_duration)
{
  // define some default value
  const int64_t MDS_THROTTLE_TRIGGER_PERCENTAGE = 60;
  const int64_t MDS_THROTTLE_MAX_DURATION = 2LL * 60LL * 60LL * 1000LL * 1000LL;  // 2 hours

  resource_limit = lib::get_mds_memory_limit();
  trigger_percentage = GCONF.writing_throttling_trigger_percentage;
  max_duration = GCONF.writing_throttling_maximum_duration;
  if (trigger_percentage <= 0 || max_duration <= 0) {
    SHARE_LOG_RET(WARN, OB_INVALID_CONFIG, "init throttle config with default value");
    trigger_percentage = MDS_THROTTLE_TRIGGER_PERCENTAGE;
    max_duration = MDS_THROTTLE_MAX_DURATION;
  }
}
void ObMdsAllocator::adaptive_update_limit(const int64_t holding_size,
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

ObMdsThrottleGuard::ObMdsThrottleGuard(const bool for_replay, const int64_t abs_expire_time)
    : for_replay_(for_replay), abs_expire_time_(abs_expire_time)
{
  throttle_tool_ = &(::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->share_resource_throttle_tool());
  if (0 == abs_expire_time) {
    abs_expire_time_ =
        ObClockGenerator::getClock() + ObThrottleUnit<ObMdsThrottleGuard>::DEFAULT_MAX_THROTTLE_TIME;
  }
  share::mds_throttled_alloc() = 0;
}

void *ObMdsAllocator::alloc(const int64_t size)
{
  int64_t abs_expire_time = THIS_WORKER.get_timeout_ts();
  return alloc(size, abs_expire_time);
}

void *ObMdsAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  UNUSED(attr);
  void *obj = alloc(size);
  MDS_LOG_RET(WARN, OB_INVALID_ARGUMENT, "VSLICE Allocator not support mark attr", KP(obj), K(size), K(attr));
  return obj;
}

void *ObMdsAllocator::alloc(const int64_t size, const int64_t abs_expire_time)
{
  bool is_throttled = false;
  // record alloc resource in throttle tool, but do not throttle immediately
  // ObMdsThrottleGuard calls the real throttle logic
  (void)throttle_tool_->alloc_resource<ObMdsAllocator>(size, abs_expire_time, is_throttled);
  if (OB_UNLIKELY(is_throttled)) {
    share::mds_throttled_alloc() += size;
  }
  void *obj = allocator_.alloc(size);
  MDS_LOG(DEBUG, "mds alloc ", K(size), KP(obj), K(abs_expire_time));
  return obj;
}

void ObMdsAllocator::free(void *ptr)
{
  allocator_.free(ptr);
}

void ObMdsAllocator::set_attr(const ObMemAttr &attr) { allocator_.set_attr(attr); }

void *ObBufferCtxAllocator::alloc(const int64_t size)
{
  return share::server_malloc(size, ObMemAttr("MDS_CTX_DEFAULT", ObCtxIds::MDS_CTX_ID));
}

void *ObBufferCtxAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  return share::server_malloc(size, attr);
}

void ObBufferCtxAllocator::free(void *ptr)
{
  share::server_free(ptr);
}

}  // namespace share
}  // namespace oceanbase
