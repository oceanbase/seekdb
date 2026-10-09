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


#include "ob_vector_allocator.h"
#include "share/config/ob_server_config.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/literals/ob_literals.h"  // _ms literal(free within lib)
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"


namespace oceanbase {
namespace share {

common::MemoryQuota *ObVectorAllocator::resolve_memory_quota(const int64_t ctx_id)
{
  common::MemoryQuota *quota = nullptr;
  if (ObCtxIds::VECTOR_CTX_ID == ctx_id) {
    ObSharedMemAllocMgr *manager = server_service<ObSharedMemAllocMgr>();
    if (nullptr != manager) {
      quota = &manager->vector_allocator().memory_quota();
    }
  }
  return quota;
}

int64_t ObVectorAllocator::hold()
{
  return (memory_context_ != nullptr ? memory_context_->hold() : 0) + get_rb_mem_used();
}

int64_t ObVectorAllocator::get_rb_mem_used() const
{
  ObRbMemMgr *rb_mgr = ::oceanbase::share::server_service<::oceanbase::common::ObRbMemMgr>();
  return rb_mgr != nullptr ? rb_mgr->get_vec_idx_used() : 0;
}

common::MemoryQuotaSample ObVectorAllocator::get_memory_quota_sample() const
{
  return quota_.sample();
}

void ObVectorAllocator::refresh_memory_quota_limit()
{
  quota_.set_limit(MAX(GMEMCONF.get_vector_memory_limit(), 0));
}

int64_t ObVectorAllocator::used()
{
  return ATOMIC_LOAD(&all_used_mem_) + get_rb_mem_used();
}

void *ObVectorAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  UNUSED(attr);
  return alloc(size);
}


void *ObVsagMemContext::Allocate(uint64_t size)
{
  void *ret_ptr = nullptr;
  int ret = OB_SUCCESS;
  if (size != 0) {
    int64_t actual_size = MEM_PTR_HEAD_SIZE + size;
    void *ptr = ObVectorMemContext::alloc(actual_size);
    if (OB_NOT_NULL(ptr)) {
      ATOMIC_AAF(all_vsag_use_mem_, actual_size);

      *(int64_t*)ptr = actual_size;
      ret_ptr = (char*)ptr + MEM_PTR_HEAD_SIZE;
    }
  }

  return ret_ptr;
}

void ObVsagMemContext::Deallocate(void* p)
{
  if (OB_NOT_NULL(p)) {
    void *size_ptr = (char*)p - MEM_PTR_HEAD_SIZE;
    int64_t size = *(int64_t *)size_ptr;

    ATOMIC_SAF(all_vsag_use_mem_, size);
    ObVectorMemContext::free((char*)p - MEM_PTR_HEAD_SIZE);
  }
}

void *ObVsagMemContext::Reallocate(void* p, uint64_t size)
{
  void *new_ptr = nullptr;
  if (size == 0) {
    if (OB_NOT_NULL(p)) {
      Deallocate(p);
      p = nullptr;
    }
  } else if (OB_ISNULL(p)) {
    new_ptr = Allocate(size);
  } else {
    void *size_ptr = (char*)p - MEM_PTR_HEAD_SIZE;
    int64_t old_size = *(int64_t *)size_ptr - MEM_PTR_HEAD_SIZE;
    if (old_size >= size) {
      new_ptr = p;
    } else {
      new_ptr = Allocate(size);
      if (OB_ISNULL(new_ptr) || OB_ISNULL(p)) {
      } else {
        MEMCPY(new_ptr, p, old_size);
        Deallocate(p);
        p = nullptr;
      }
    }
  }
  return new_ptr;
}

int ObVsagMemContext::init(lib::MemoryContext &parent_mem_context,
                           uint64_t *all_vsag_use_mem)
{
  INIT_SUCC(ret);
  lib::ContextParam param;
  ObSharedMemAllocMgr *share_mem_alloc_mgr = ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>();
  ObMemAttr attr("VIndexVsagADP", ObCtxIds::VECTOR_CTX_ID);
  param.set_mem_attr(attr)
    .set_page_size(OB_MALLOC_MIDDLE_BLOCK_SIZE)
    .set_parallel(8)
    .set_properties(lib::ALLOC_THREAD_SAFE | lib::RETURN_MALLOC_DEFAULT);
  if (OB_FAIL(parent_mem_context->CREATE_CONTEXT(mem_context_, param))) {
  } else if (OB_FAIL(ObVectorMemContext::init(mem_context_, &(share_mem_alloc_mgr->vector_allocator())))) {
  } else {
    all_vsag_use_mem_ = all_vsag_use_mem;
  }

  return ret;
}

void* ObVectorMemContext::alloc(int64_t size)
{
  void *ret_ptr = nullptr;
  ret_ptr = memory_context_->get_malloc_allocator().alloc(size);
  if (OB_ISNULL(ret_ptr) && size > 0) {
    const int ret = OB_ERR_VSAG_MEM_LIMIT_EXCEEDED;
    const common::MemoryQuotaSample sample = vector_allocator_->memory_quota().sample();
    OB_LOG(WARN, "vector memory admission failed", K(ret), K(size),
        "vector_limit", GMEMCONF.get_vector_memory_limit(),
        "bitmap_used", vector_allocator_->get_rb_mem_used(),
        "committed", sample.committed_bytes_, "reserved", sample.reserved_bytes_,
        "reject_count", sample.reject_count_);
  }
  return ret_ptr;
}

void ObVectorMemContext::free(void *ptr)
{
  if (OB_NOT_NULL(ptr)) {
    memory_context_->get_malloc_allocator().free(ptr);
  }
}

int ObVectorMemContext::init(lib::MemoryContext &mem_context, ObVectorAllocator *vector_allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mem_context) || OB_ISNULL(vector_allocator)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "mem_context or vector_allocator is null.", K(ret), KPC(mem_context), KP(vector_allocator));
  } else {
    memory_context_ = mem_context;
    vector_allocator_ = vector_allocator;
  }
  return ret;
}

const char* ObIvfMemContext::IVF_CACHE_LABEL = "IvfCacheCtx";
const char* ObIvfMemContext::IVF_BUILD_LABEL = "IvfBuildCtx";

int ObIvfMemContext::init(lib::MemoryContext &parent_mem_context, uint64_t *all_vsag_use_mem,
                          const char *label /*IVF_CACHE_LABEL*/)
{
  INIT_SUCC(ret);
  lib::ContextParam param;
  ObSharedMemAllocMgr *share_mem_alloc_mgr = ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>();
  ObMemAttr attr(label, ObCtxIds::VECTOR_CTX_ID);
  param.set_mem_attr(attr)
    .set_page_size(OB_MALLOC_MIDDLE_BLOCK_SIZE)
    .set_parallel(8)
    .set_properties(lib::ALLOC_THREAD_SAFE | lib::RETURN_MALLOC_DEFAULT);
  if (OB_FAIL(parent_mem_context->CREATE_CONTEXT(mem_context_, param))) {
  } else if (OB_FAIL(ObVectorMemContext::init(mem_context_, &(share_mem_alloc_mgr->vector_allocator())))) {
  } else {
    all_vsag_use_mem_ = all_vsag_use_mem;
  }

  return ret;
}

void *ObIvfMemContext::Allocate(size_t size)
{
  void *ret_ptr = nullptr;
  int ret = OB_SUCCESS;
  if (size != 0) {
    int64_t actual_size = MEM_PTR_HEAD_SIZE + size;
    void *ptr = ObVectorMemContext::alloc(actual_size);
    if (OB_NOT_NULL(ptr)) {
      ATOMIC_AAF(all_vsag_use_mem_, actual_size);

      *(int64_t*)ptr = actual_size;
      ret_ptr = (char*)ptr + MEM_PTR_HEAD_SIZE;
    }
  }

  return ret_ptr;
}

void ObIvfMemContext::Deallocate(void* p)
{
  if (OB_NOT_NULL(p)) {
    void *size_ptr = (char*)p - MEM_PTR_HEAD_SIZE;
    int64_t size = *(int64_t *)size_ptr;

    ATOMIC_SAF(all_vsag_use_mem_, size);
    ObVectorMemContext::free((char*)p - MEM_PTR_HEAD_SIZE);
  }
}

}  // namespace share
}  // namespace oceanbase
