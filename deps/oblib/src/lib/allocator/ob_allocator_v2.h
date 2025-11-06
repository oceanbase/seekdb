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

#ifndef  OCEANBASE_COMMON_ALLOCATOR_H_
#define  OCEANBASE_COMMON_ALLOCATOR_H_

#include "lib/ob_define.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/alloc/object_set.h"
#include "lib/alloc/ob_malloc_allocator.h"
#include "lib/alloc/alloc_interface.h"
#include "lib/allocator/ob_page_manager.h"
#ifndef ENABLE_SANITY
#include "lib/lock/ob_latch.h"
#else
#include "lib/alloc/ob_latch_v2.h"
#endif

namespace oceanbase
{
namespace lib
{
class __MemoryContext__;
}
namespace common
{
using lib::__MemoryContext__;
using lib::ObjectSet;
using lib::AObject;
using common::ObPageManager;

class ObAllocator : public ObIAllocator
{
  friend class lib::__MemoryContext__;
  friend class ObParallelAllocator;
public:
  ObAllocator(__MemoryContext__ *mem_context, const ObMemAttr &attr,
              const bool use_pm=false,
              const uint32_t ablock_size=lib::INTACT_NORMAL_AOBJECT_SIZE);
  virtual ~ObAllocator() {}
  virtual void *alloc(const int64_t size) override
  {
    return alloc(size, attr_);
  }
  virtual void *alloc(const int64_t size, const ObMemAttr &attr) override;
  virtual void free(void *ptr) override;
  int64_t hold() const;
  int64_t total() const override { return hold(); }
  int64_t used() const override;
  void *get_pm() { return pm_; }
private:
  int init();
  void lock() { locker_->lock(); }
  void unlock() { locker_->unlock(); }
  bool trylock() { return locker_->trylock(); }
  // for unittest only
  void reset() override { os_.reset(); }
private:
  __MemoryContext__ *mem_context_;
  ObMemAttr attr_;
  const bool use_pm_;
  void *pm_;
  lib::ISetLocker *locker_;
  lib::SetDoNothingLocker do_nothing_locker_;
#ifndef ENABLE_SANITY
  lib::ObMutex mutex_;
#else
  lib::ObMutexV2 mutex_;
#endif
  lib::SetLocker<decltype(mutex_)> do_locker_;
  class : public lib::IBlockMgr
  {
  public:
    virtual ABlock *alloc_block(uint64_t size, const ObMemAttr &attr) override
    {
      ABlock *block = NULL;
      if (OB_NOT_NULL(ta_)) {
        block = ta_->get_block_mgr().alloc_block(size, attr);
      }
      return block;
    }
    virtual void free_block(ABlock *block) override
    {
      if (OB_NOT_NULL(ta_)) {
        ta_->get_block_mgr().free_block(block);
      } else {
        OB_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "tenant ctx allocator is null", K(tenant_id_), K(ctx_id_));
      }
    }
    virtual int64_t sync_wash(int64_t wash_size) override
    {
      int64_t washed_size = 0;
      if (OB_NOT_NULL(ta_)) {
        washed_size = ta_->sync_wash(wash_size);
      }
      return washed_size;
    }
    void set_tenant_ctx(const int64_t tenant_id, const int64_t ctx_id)
    {
      tenant_id_ = tenant_id;
      ctx_id_ = ctx_id;
      ta_ = ObMallocAllocator::get_instance()->get_tenant_ctx_allocator(tenant_id_, ctx_id_);
    }
  private:
    lib::ObTenantCtxAllocatorGuard ta_;
  } blk_mgr_, nblk_mgr_;
  ObjectSet os_;
  bool is_inited_;
public:
};

inline ObAllocator::ObAllocator(__MemoryContext__ *mem_context, const ObMemAttr &attr, const bool use_pm,
                                const uint32_t ablock_size)
  : mem_context_(mem_context),
    attr_(attr),
    use_pm_(use_pm),
    pm_(nullptr),
    locker_(&do_nothing_locker_),
    mutex_(common::ObLatchIds::OB_ALLOCATOR_LOCK),
    do_locker_(mutex_),
    os_(mem_context_, ablock_size),
    is_inited_(false)
{
  attr_.tenant_id_ = OB_SYS_TENANT_ID;
}

inline int ObAllocator::init()
{
  int ret = OB_SUCCESS;
  ObPageManager *pm = nullptr;
  lib::IBlockMgr *blk_mgr = nullptr;
  lib::IBlockMgr *nblk_mgr = nullptr;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    OB_LOG(ERROR, "init twice", K(ret));
  } else if (use_pm_ &&
             (pm = ObPageManager::thread_local_instance()) != nullptr &&
             attr_.ctx_id_ == pm->get_ctx_id()) {
    blk_mgr = pm;
    pm_ = pm;
  } else {
    blk_mgr_.set_tenant_ctx(attr_.tenant_id_, attr_.ctx_id_);
    blk_mgr = &blk_mgr_;
  }

  if (OB_SUCC(ret)) {
    nblk_mgr_.set_tenant_ctx(attr_.tenant_id_, attr_.ctx_id_);
    nblk_mgr = &nblk_mgr_;
    os_.set_block_mgr(blk_mgr);
    os_.set_locker(locker_);
    is_inited_ = true;
  }
  return ret;
}

inline int64_t ObAllocator::hold() const
{
  return os_.get_hold_bytes();
}

OB_INLINE int64_t ObAllocator::used() const
{
  return os_.get_alloc_bytes();
}

class ObParallelAllocator : public ObIAllocator
{
  // Maximum concurrency
  static const int N = 8;
public:
  ObParallelAllocator(ObAllocator &root_allocator,
                      __MemoryContext__ *mem_context,
                      const ObMemAttr &attr,
                      const int parallel=4,
                      const uint32_t ablock_size=lib::INTACT_NORMAL_AOBJECT_SIZE);
  virtual ~ObParallelAllocator();
  void *alloc(const int64_t size) override
  {
    return alloc(size, attr_);
  }
  void *alloc(const int64_t size, const ObMemAttr &attr) override;
  void free(void *ptr) override;
  int64_t hold() const;
  int64_t total() const override { return hold(); }
  int64_t used() const override;
private:
  int init();
private:
  ObAllocator &root_allocator_;
  __MemoryContext__ *mem_context_;
  ObMemAttr attr_;
  uint32_t ablock_size_;
  // buffer of sub_allocators_
  void *buf_;
  // Static allocation takes up too much space, considering that there is less demand for parallel multiple channels, change to dynamic allocation
  ObAllocator *sub_allocators_[N];
  const int sub_cnt_;
  bool is_inited_;
  // for init
#ifndef ENABLE_SANITY
  lib::ObMutex mutex_;
#else
  lib::ObMutexV2 mutex_;
#endif
};

inline ObParallelAllocator::ObParallelAllocator(ObAllocator &root_allocator,
                                                 __MemoryContext__ *mem_context,
                                                 const ObMemAttr &attr,
                                                 const int parallel,
                                                 const uint32_t ablock_size)
  : root_allocator_(root_allocator), mem_context_(mem_context), attr_(attr),
    ablock_size_(ablock_size), buf_(nullptr), sub_cnt_(MIN(parallel, N)),
    is_inited_(false)
{
  for (int i = 0; i < sub_cnt_; i++) {
    sub_allocators_[i] = nullptr;
  }
}

inline ObParallelAllocator::~ObParallelAllocator()
{
  for (int64_t i = 0; i < sub_cnt_; i++) {
    if (sub_allocators_[i] != nullptr) {
      sub_allocators_[i]->~ObAllocator();
      sub_allocators_[i] = nullptr;
    }
  }
  // Release the memory of the multipath itself
  if (buf_ != nullptr) {
    root_allocator_.free(buf_);
    buf_ = nullptr;
  }
}

inline int ObParallelAllocator::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else {
    buf_ = root_allocator_.alloc(sizeof(ObAllocator) * sub_cnt_);
    if (OB_ISNULL(buf_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    for (int i = 0; OB_SUCC(ret) && i < sub_cnt_; i++) {
      sub_allocators_[i] = new ((char*)buf_ + sizeof(ObAllocator) *  i)
        ObAllocator(mem_context_, attr_, false, ablock_size_);
      sub_allocators_[i]->locker_ = &sub_allocators_[i]->do_locker_;
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  }
  return ret;
}

inline int64_t ObParallelAllocator::hold() const
{
  int64_t hold = 0;
  if (is_inited_) {
    hold += root_allocator_.hold();
    for (int64_t i = 0; i < sub_cnt_; i++) {
      hold += sub_allocators_[i]->hold();
    }
  }
  return hold;
}

inline int64_t ObParallelAllocator::used() const
{
  int64_t used = 0;
  if (is_inited_) {
    used += root_allocator_.used();
    for (int64_t i = 0; i < sub_cnt_; i++) {
      used += sub_allocators_[i]->used();
    }
  }
  return used;
}

}
}

#endif //OCEANBASE_COMMON_ALLOCATOR_H_
