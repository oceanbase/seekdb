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

#include "ob_memstore_allocator.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_shared_memory_allocator_mgr.h"

// Undefine macOS system macro to avoid conflict with ObFifoArena::PAGE_SIZE
#ifdef PAGE_SIZE
#undef PAGE_SIZE
#endif

namespace oceanbase
{
using namespace share;
namespace share
{


int ObMemstoreAllocator::AllocHandle::init()
{
  int ret = OB_SUCCESS;

  ObMemstoreAllocator &host = ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->memstore_allocator();
  (void)host.init_handle(*this);
  return ret;
}

int ObMemstoreAllocator::init()
{
  throttle_tool_ = &(::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->share_resource_throttle_tool());
  int ret = arena_.init();
  if (OB_SUCC(ret)) {
    set_nway_per_group(1);
  }
  return ret;
}

void ObMemstoreAllocator::init_handle(AllocHandle& handle)
{
  handle.do_reset();
  handle.set_host(this);
  {
    LockGuard guard(lock_);
    hlist_.init_handle(handle);
  }
  COMMON_LOG(TRACE, "MTALLOC.init", KP(&handle.mt_));
}

void ObMemstoreAllocator::destroy_handle(AllocHandle& handle)
{
  ObTimeGuard time_guard("ObMemstoreAllocator::destroy_handle", 100 * 1000);
  COMMON_LOG(TRACE, "MTALLOC.destroy", KP(&handle.mt_));
  arena_.free(handle.arena_handle_);
  time_guard.click();
  {
    LockGuard guard(lock_);
    time_guard.click();
    hlist_.destroy_handle(handle);
    time_guard.click();
    if (hlist_.is_empty()) {
      arena_.reset();
    }
    time_guard.click();
  }
  handle.do_reset();
}


void ObMemstoreAllocator::set_frozen(AllocHandle& handle)
{
  COMMON_LOG(TRACE, "MTALLOC.set_frozen", KP(&handle.mt_));
  LockGuard guard(lock_);
  hlist_.set_frozen(handle);
}

void ObMemstoreAllocator::set_nway_per_group(const int64_t nway)
{
  LockGuard guard(lock_);
  arena_.update_nway_per_group(nway);
}

int ObMemstoreAllocator::set_memstore_threshold()
{
  LockGuard guard(lock_);
  int ret = set_memstore_threshold_without_lock();
  return ret;
}


int64_t ObMemstoreAllocator::resource_unit_size()
{
  static const int64_t MEMSTORE_RESOURCE_UNIT_SIZE = 2LL * 1024LL * 1024LL; /* 2MB */
  return MEMSTORE_RESOURCE_UNIT_SIZE;
}


void ObMemstoreAllocator::adaptive_update_limit(const int64_t holding_size,
                                                const int64_t config_specify_resource_limit,
                                                int64_t &resource_limit,
                                                int64_t &last_update_limit_ts,
                                                bool &is_updated)
{
  // do nothing
}

};  // namespace share
};  // namespace oceanbase
