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

#define USING_LOG_PREFIX STORAGE

#include "ob_rb_memory_mgr.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/utility/ob_mod_define.h"
#include "share/rc/ob_server_runtime.h"

#include <cstddef>
#include <limits>

namespace oceanbase
{
namespace common
{
ObRbMemMgr *get_rb_mem_mgr()
{
  return ::oceanbase::share::server_service<::oceanbase::common::ObRbMemMgr>();
}

namespace
{

enum class RoaringAllocationDomain : uint8_t
{
  POOLED = 0,
  VECTOR_DIRECT = 1,
};

struct alignas(std::max_align_t) RoaringAllocationHeader
{
  static constexpr uint64_t MAGIC = 0x524f4152494e4731ULL;

  RoaringAllocationHeader(ObRbMemMgr *mem_mgr,
                          MemoryQuota *quota,
                          const size_t requested_size,
                          const int64_t charged_size,
                          const RoaringAllocationDomain domain)
      : magic_(MAGIC), mem_mgr_(mem_mgr), quota_(quota),
        requested_size_(requested_size), charged_size_(charged_size),
        domain_(domain)
  {}

  bool is_valid() const
  {
    return MAGIC == magic_ && nullptr != mem_mgr_
        && requested_size_ > 0 && charged_size_ > 0;
  }

  uint64_t magic_;
  ObRbMemMgr *mem_mgr_;
  MemoryQuota *quota_;
  size_t requested_size_;
  int64_t charged_size_;
  RoaringAllocationDomain domain_;
};

static_assert(0 == sizeof(RoaringAllocationHeader) % alignof(std::max_align_t),
              "CRoaring allocations must preserve malloc alignment");

bool is_vector_bitmap_allocation(const lib::ObMemAttr &attr)
{
  return attr.label_.is_valid()
      && attr.label_[0] == 'V'
      && attr.label_[1] == 'I'
      && attr.label_[2] == 'B';
}

} // namespace

static void *roaring_malloc(size_t size) {
  void *res_ptr = nullptr;
  void *alloc_ptr = nullptr;
  if (size > 0
      && size <= static_cast<size_t>(std::numeric_limits<int64_t>::max())
          - sizeof(RoaringAllocationHeader)) {
    ObRbMemMgr *mem_mgr  = nullptr;
    MemoryQuota *quota = nullptr;
    int64_t charged_size = 0;
    RoaringAllocationDomain domain = RoaringAllocationDomain::POOLED;
    const size_t alloc_size = size + sizeof(RoaringAllocationHeader);
    lib::ObMemAttr last_mem_attr = lib::ObMallocHookAttrGuard::get_tl_mem_attr();
    if (OB_ISNULL(mem_mgr = get_rb_mem_mgr())) {
      int ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("mem_mgr is null");
      ob_abort();
    } else if (is_vector_bitmap_allocation(last_mem_attr)) {
      quota = resolve_memory_quota(ObCtxIds::VECTOR_CTX_ID);
      MemoryReservation reservation(quota, static_cast<int64_t>(alloc_size));
      if (nullptr != quota && reservation.valid()) {
        alloc_ptr = ob_malloc(
            static_cast<int64_t>(alloc_size),
            lib::ObMemAttr(last_mem_attr.label_, ObCtxIds::VECTOR_CTX_ID));
        if (nullptr != alloc_ptr) {
          charged_size = ob_malloc_usable_size(alloc_ptr);
          if (charged_size <= 0) {
            charged_size = static_cast<int64_t>(alloc_size);
          }
        }
        if (nullptr == alloc_ptr || !reservation.reconcile(charged_size)) {
          if (nullptr != alloc_ptr) {
            ob_free(alloc_ptr);
            alloc_ptr = nullptr;
          }
        } else {
          domain = RoaringAllocationDomain::VECTOR_DIRECT;
          mem_mgr->incr_vec_idx_used(static_cast<size_t>(charged_size));
        }
      }
    } else {
      alloc_ptr = mem_mgr->alloc(alloc_size);
      charged_size = static_cast<int64_t>(alloc_size);
    }
    if (alloc_ptr == nullptr) {
      int ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc memory failed", K(size));
      throw std::bad_alloc();
    } else {
      RoaringAllocationHeader *header = new (alloc_ptr) RoaringAllocationHeader(
          mem_mgr, quota, size, charged_size, domain);
      res_ptr = header + 1;
    }
  } else if (size > 0) {
    throw std::bad_alloc();
  }
  return res_ptr;
}

static void roaring_free(void *ptr) {
  if (ptr != nullptr) {
    RoaringAllocationHeader *header =
        static_cast<RoaringAllocationHeader *>(ptr) - 1;
    abort_unless(header->is_valid());
    ObRbMemMgr *mem_mgr = header->mem_mgr_;
    MemoryQuota *quota = header->quota_;
    const int64_t charged_size = header->charged_size_;
    const RoaringAllocationDomain domain = header->domain_;
    header->magic_ = 0;
    if (RoaringAllocationDomain::VECTOR_DIRECT == domain) {
      ob_free(header);
      mem_mgr->decr_vec_idx_used(static_cast<size_t>(charged_size));
      abort_unless(nullptr != quota);
      quota->release(charged_size);
    } else {
      mem_mgr->free(header);
    }
  }
  return;
}

static void *roaring_realloc(void *ptr, size_t size) {
  void *res = nullptr;
  if (0 == size) {
    roaring_free(ptr);
  } else if (NULL == ptr) {
    res = roaring_malloc(size);
  } else {
    RoaringAllocationHeader *header =
        static_cast<RoaringAllocationHeader *>(ptr) - 1;
    abort_unless(header->is_valid());
    const size_t orig_size = header->requested_size_;
    if (orig_size > size) {
      res = ptr;
    } else {
      res = roaring_malloc(size);
      MEMCPY(res, ptr, orig_size);
      roaring_free(ptr);
    }
  }
  return res;
}

static void *roaring_calloc(size_t item_cnt, size_t size) {
  if (0 != size && item_cnt > std::numeric_limits<size_t>::max() / size) {
    throw std::bad_alloc();
  }
  const size_t total_size = item_cnt * size;
  void *res = roaring_malloc(total_size);
  if (res != nullptr) {
    MEMSET(res, 0, total_size);
  }
  return res;
}

static void *roaring_aligned_malloc(size_t alignment, size_t size) {
  void *res = nullptr;
  if (size > 0) {
    if (0 == alignment || 0 != (alignment & (alignment - 1))
        || alignment > std::numeric_limits<size_t>::max()
            - sizeof(void *) + 1
        || size > std::numeric_limits<size_t>::max()
            - (alignment - 1 + sizeof(void *))) {
      throw std::bad_alloc();
    }
    size_t offset = alignment - 1 + sizeof(void *);
    void *orig_ptr = roaring_malloc(size + offset);
    if (orig_ptr == nullptr) {
      res = orig_ptr;
    } else {
      size_t orig_location = reinterpret_cast<size_t>(orig_ptr);
      size_t real_location = (orig_location + offset) & ~(alignment - 1);
      res = reinterpret_cast<void *>(real_location);
      size_t orig_ptr_stroage = real_location - sizeof(void *);
      *reinterpret_cast<void **>(orig_ptr_stroage) = orig_ptr;
    }
  }
  return res;
}

static void roaring_aligned_free(void *ptr) {
  if (ptr != nullptr) {
    size_t orig_ptr_stroage = reinterpret_cast<size_t>(ptr) - sizeof(void *);
    roaring_free(*reinterpret_cast<void **>(orig_ptr_stroage));
  }
  return;
}

int ObRbMemMgr::init_memory_hook()
{
  int ret = OB_SUCCESS;
  roaring_memory_mgr.malloc = roaring_malloc;
  roaring_memory_mgr.realloc = roaring_realloc;
  roaring_memory_mgr.calloc = roaring_calloc;
  roaring_memory_mgr.free = roaring_free;
  roaring_memory_mgr.aligned_malloc = roaring_aligned_malloc;
  roaring_memory_mgr.aligned_free = roaring_aligned_free;
  // initialize global memory hook
  roaring_init_memory_hook(roaring_memory_mgr);
  return ret;
}

int ObRbMemMgr::init()
{
  int ret = OB_SUCCESS;
  
  lib::ObMemAttr mem_attr("RoaringBitmap");
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(allocator_.init(OB_MALLOC_BIG_BLOCK_SIZE, block_alloc_, mem_attr))) {
  } else {
    allocator_.set_nway(RB_ALLOC_CONCURRENCY);
    vec_idx_used_ = 0;
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

void ObRbMemMgr::destroy()
{
  LOG_INFO("destroy CRoaring memory manager");
  allocator_.destroy();
  is_inited_ = false;
}

void *ObRbMemMgr::alloc(size_t size)
{
  return allocator_.alloc(size);
}

void ObRbMemMgr::free(void *ptr)
{
  return allocator_.free(ptr);
}

void ObRbMemMgr::incr_vec_idx_used(size_t size)
{
  ATOMIC_AAF(&vec_idx_used_, size);
}

void ObRbMemMgr::decr_vec_idx_used(size_t size)
{
  ATOMIC_SAF(&vec_idx_used_, size);
}

} // common
} // oceanbase
