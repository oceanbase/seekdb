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

#include "share/rc/ob_server_runtime.h"
#include "ob_log_allocator_mgr.h"
#include "ob_log_allocator.h"

using namespace oceanbase::share;

namespace oceanbase
{
namespace common
{

int ObLogAllocatorMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    allocator_ = NULL;
    is_inited_ = true;
  }
  return ret;
}

int ObLogAllocatorMgr::get_log_allocator(ObILogAllocator *&out_allocator)
{
  int ret = OB_SUCCESS;
  ObLogAllocator *allocator = NULL;
  if (OB_FAIL(get_allocator_(allocator))) {
  } else {
    out_allocator = allocator;
  }
  return ret;
}

int ObLogAllocatorMgr::delete_log_allocator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(delete_allocator_())) {
    OB_LOG(WARN, "delete_allocator_ failed", K(ret));
  } else {
    OB_LOG(INFO, "delete_allocator_ success");
  }
  return ret;
}

int ObLogAllocatorMgr::get_allocator_(Allocator *&out_allocator)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    do {
      obsys::ObRLockGuard guard(lock_);
      out_allocator = ATOMIC_LOAD(&allocator_);
    } while(0);

    if (NULL == out_allocator) {
      if (OB_FAIL(create_allocator_(out_allocator))) {
        OB_LOG(WARN, "fail to create_allocator_", K(ret));
      }
    }
  }

  if (OB_SUCC(ret) && OB_ISNULL(out_allocator)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "got allocator is NULL", K(ret));
  }

  return ret;
}

int ObLogAllocatorMgr::get_memstore_limit_percent_(int64_t &limit_percent) const
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    SERVER_MODULE_SCOPE {
      share::ObIMemstoreRuntime *runtime =
          share::server_service<share::ObIMemstoreRuntime>();
      if (OB_ISNULL(runtime)) {
        ret = OB_NOT_INIT;
      } else {
        ret = runtime->get_memstore_limit_percentage(limit_percent);
      }
    }
  }
  return ret;
}

int ObLogAllocatorMgr::construct_allocator_(Allocator *&out_allocator)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    ObMemAttr attr(ObModIds::OB_LOG_ALLOCATOR);
    void *buf = ob_malloc(sizeof(Allocator), attr);
    if (NULL == buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "failed to alloc memory", K(ret));
    } else {
      Allocator *allocator = new (buf) Allocator{};
      out_allocator = allocator;
      OB_LOG(INFO, "ObLogAllocator init success");
    }
  }
  return ret;
}

int ObLogAllocatorMgr::create_allocator_(Allocator *&out_allocator)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    obsys::ObWLockGuard guard(lock_);
    if (NULL != (out_allocator = ATOMIC_LOAD(&allocator_))) {
    } else {
      Allocator *new_allocator = NULL;
      if (OB_FAIL(construct_allocator_(new_allocator))) {
        OB_LOG(WARN, "fail to construct_allocator_", K(ret));
      } else if (!ATOMIC_BCAS(&allocator_, NULL, new_allocator)) {
        out_allocator = ATOMIC_LOAD(&allocator_);
        if (NULL != new_allocator) {
          new_allocator->~Allocator();
          ob_free(new_allocator);
        }
      } else {
        out_allocator = ATOMIC_LOAD(&allocator_);
      }
    }
  }

  return ret;
}

int ObLogAllocatorMgr::delete_allocator_()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    obsys::ObWLockGuard guard(lock_);
    Allocator *allocator = NULL;
    if (NULL != (allocator = ATOMIC_LOAD(&allocator_))) {
      allocator_ = NULL;
      allocator->~Allocator();
      ob_free(allocator);
    }
  }

  return ret;
}

ObLogAllocatorMgr &ObLogAllocatorMgr::get_instance()
{
  static ObLogAllocatorMgr instance_;
  return instance_;
}

int ObLogAllocatorMgr::update_memory_limit(const share::ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
      int64_t cur_memstore_limit_percent = 0;

      const bool has_memstore = runtime_config.has_memstore_;
      int32_t nway = static_cast<int32_t>(runtime_config.resource_config_.max_cpu());
      if (nway == 0) {
        nway = 1;
      }
      const int64_t memory_size = runtime_config.resource_config_.memory_size();
      int64_t new_limit = memory_size;
      if (has_memstore) {
        if (OB_TMP_FAIL(get_memstore_limit_percent_(cur_memstore_limit_percent))) {
          OB_LOG(WARN, "memstore_limit_percentage val is unexpected", K(cur_memstore_limit_percent));
        } else if (cur_memstore_limit_percent > 100 || cur_memstore_limit_percent <= 0) {
          OB_LOG(WARN, "memstore_limit_percentage val is unexpected", K(cur_memstore_limit_percent));
        } else {
          new_limit = memory_size / 100 * ( 100 - cur_memstore_limit_percent);
        }
      }
      ObLogAllocator *log_allocator = NULL;
      if (OB_TMP_FAIL(get_allocator_(log_allocator))) {
        OB_LOG(WARN, "get_allocator_ failed", K(tmp_ret));
      } else if (NULL == log_allocator) {
        OB_LOG(WARN, "get_allocator_ failed");
      } else {
        log_allocator->set_nway(nway);
        int64_t previous_limit = log_allocator->get_limit();
        if (previous_limit != new_limit) {
          log_allocator->set_limit(new_limit);
        }
        OB_LOG(INFO, "ObLogAllocator memory limit updated", K(ret),
             K(nway), K(new_limit), K(previous_limit), K(cur_memstore_limit_percent), K(runtime_config));
      }

      SERVER_MODULE_SCOPE {
        share::ObIMemstoreRuntime *runtime =
            share::server_service<share::ObIMemstoreRuntime>();
        if (OB_ISNULL(runtime)) {
          ret = OB_NOT_INIT;
          OB_LOG(WARN, "memstore runtime is unavailable", K(ret));
        } else if (OB_FAIL(runtime->set_memstore_threshold())) {
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
