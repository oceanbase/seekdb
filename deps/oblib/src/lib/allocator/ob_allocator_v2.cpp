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

#include "lib/allocator/ob_allocator_v2.h"
#ifdef _WIN32
#include "lib/alloc/alloc_failed_reason.h"
#endif

using namespace oceanbase::lib;
namespace oceanbase
{
namespace common
{
void *ObAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  UNUSED(attr);
  void *ptr = nullptr;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = init();
  }
  if (OB_SUCC(ret)) {
    ObMemAttr inner_attr = attr_;
    if (attr.label_.is_valid()) {
      inner_attr.label_ = attr.label_;
    }
    auto ctx_allocator = lib::ObMallocAllocator::get_instance()->get_ctx_allocator(inner_attr.ctx_id_);
    if (OB_LIKELY(NULL != ctx_allocator)) {
      ptr = ObCtxAllocator::common_realloc(NULL, size, inner_attr,
          *(ctx_allocator.ref_allocator()), os_);
    }
  }
  return ptr;
}

void ObAllocator::free(void *ptr)
{
  // Free the object directly; the owning object set carries its context.
  ObCtxAllocator::common_free(ptr);
}

void *ObParallelAllocator::alloc(const int64_t size, const ObMemAttr &attr)
{
  void *ptr = NULL;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    lib::ObLockGuard<decltype(mutex_)> guard(mutex_);
    if (!is_inited_) {
      ret = init();
    }
  }
  if (OB_SUCC(ret)) {
    bool found = false;
    const int64_t start = common::get_itid();
    for (int64_t i = 0; nullptr == ptr && i < sub_cnt_ && !found; i++) {
      int64_t idx = (start + i) % sub_cnt_;
      if (sub_allocators_[idx]->trylock()) {
        ptr = sub_allocators_[idx]->alloc(size, attr);
        sub_allocators_[idx]->unlock();
        found = true;
        break;
      }
    }
    if (!found && nullptr == ptr) {
      const int64_t idx = start % sub_cnt_;
      sub_allocators_[idx]->lock();
      ptr = sub_allocators_[idx]->alloc(size, attr);
      sub_allocators_[idx]->unlock();
    }
  }
  return ptr;
}

void ObParallelAllocator::free(void *ptr)
{
  if (OB_LIKELY(nullptr != ptr)) {
    AObject *obj = reinterpret_cast<AObject*>((char*)ptr - lib::AOBJECT_HEADER_SIZE);
    abort_unless(obj);
    abort_unless(obj->MAGIC_CODE_ == lib::AOBJECT_MAGIC_CODE
                 || obj->MAGIC_CODE_ == lib::BIG_AOBJECT_MAGIC_CODE);
    abort_unless(obj->in_use_);
    lib::ABlock *block = obj->block();
    abort_unless(block);
    abort_unless(block->is_valid());
    ObjectSet *os = (ObjectSet*)block->obj_set_;
    // The locking process is driven by obj_set
    os->free_object(obj);
  }
}

} // end of namespace common
} // end of namespace oceanbase
