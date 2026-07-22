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

#include "object_mgr.h"
#include "lib/alloc/ob_malloc_allocator.h"

using namespace oceanbase;
using namespace lib;

SubObjectMgr::SubObjectMgr(ObCtxAllocator &ctx_allocator,
                           const bool enable_no_log,
                           const uint32_t ablock_size,
                           const bool enable_dirty_list,
                           IBlockMgr *blk_mgr)
  : IBlockMgr(ctx_allocator.get_ctx_id()),
    ctx_allocator_(ctx_allocator),
    mutex_(common::ObLatchIds::ALLOC_OBJECT_LOCK),
    normal_locker_(mutex_), no_log_locker_(mutex_),
    locker_(!enable_no_log ? static_cast<ISetLocker&>(normal_locker_) :
            static_cast<ISetLocker&>(no_log_locker_)),
    bs_(), os_(NULL, ablock_size, enable_dirty_list)
{
  bs_.set_ctx_allocator(ctx_allocator);
  bs_.set_locker(&locker_);
  bs_.set_chunk_mgr(&ctx_allocator.get_chunk_mgr());
  os_.set_locker(&locker_);
  NULL == blk_mgr ? os_.set_block_mgr(this) : os_.set_block_mgr(blk_mgr);
  mutex_.enable_record_stat(false);
}

void SubObjectMgr::free_object(AObject *object)
{
  ABlock *block = object->block();
  abort_unless(block->is_valid());
  abort_unless(block->in_use_);
  abort_unless(block->obj_set_ != NULL);
  ObjectSet *os = (ObjectSet *)block->obj_set_;
  abort_unless(&os_ == os);
  os->free_object(object);
}

void SubObjectMgr::free_block(ABlock *block)
{
  abort_unless(block);
  abort_unless(block->is_valid());
  AChunk *chunk = block->chunk();
  abort_unless(chunk);
  abort_unless(chunk->is_valid());
  abort_unless(&bs_ == chunk->block_set_);
  bs_.free_block(block);
}

ObjectMgr::ObjectMgr(ObCtxAllocator &ctx_allocator,
                     bool enable_no_log,
                     uint32_t ablock_size,
                     int parallel,
                     bool enable_dirty_list,
                     IBlockMgr *blk_mgr)
  : IBlockMgr(ctx_allocator.get_ctx_id()),
    ctx_allocator_(ctx_allocator),
    enable_no_log_(enable_no_log),
    ablock_size_(ablock_size),
    parallel_(parallel),
    enable_dirty_list_(enable_dirty_list),
    blk_mgr_(blk_mgr),
    sub_cnt_(0),
    root_mgr_(ctx_allocator, enable_no_log, ablock_size_,
              enable_dirty_list, blk_mgr_)
{
  MEMSET(sub_mgrs_, 0, sizeof(sub_mgrs_));
}

ObjectMgr::~ObjectMgr()
{
  reset();
}

void ObjectMgr::reset() {
  for (int i = 0; i < ATOMIC_LOAD(&sub_cnt_); i++) {
    if (sub_mgrs_[i] != nullptr) {
      destroy_sub_mgr(sub_mgrs_[i]);
      ATOMIC_STORE(&sub_mgrs_[i], nullptr);
    }
  }
  ATOMIC_STORE(&sub_cnt_, 0);
}

AObject *ObjectMgr::alloc_object(uint64_t size, const ObMemAttr &attr)
{
  AObject *obj = NULL;
  const uint64_t start = common::get_itid();
  SubObjectMgr *sub_mgr = nullptr;
  for (uint64_t i = 0; NULL == obj && i < ATOMIC_LOAD(&sub_cnt_); i++) {
    uint64_t idx = (start + i) % sub_cnt_;
    sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx]);
    if (OB_ISNULL(sub_mgr)) {
      // do nothing
    } else if (sub_mgr->trylock()) {
      obj = sub_mgr->alloc_object(size, attr);
      sub_mgr->unlock();
    }
  }
  bool stop = false;
  if (OB_ISNULL(obj)) {
    auto cnt = ATOMIC_LOAD(&sub_cnt_);
    if (cnt < parallel_) {
      if (OB_NOT_NULL(sub_mgr = create_sub_mgr())) {
        if (ATOMIC_BCAS(&sub_mgrs_[cnt], nullptr, sub_mgr)) {
          ATOMIC_INC(&sub_cnt_);
        } else {
          destroy_sub_mgr(sub_mgr);
        }
      } else {
        stop = true;
      }
    }
    if (OB_ISNULL(obj) && OB_LIKELY(!stop)) {
      uint64_t idx = start;
      while (OB_ISNULL(obj)) {
        sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx++ % parallel_]);
        if (OB_ISNULL(sub_mgr)) {
          continue;
        } else if (OB_SUCCESS == sub_mgr->lock(ALLOC_LOCK_TIMEOUT_US)) {
          obj = sub_mgr->alloc_object(size, attr);
          sub_mgr->unlock();
          break;
        }
      }
    }
  }
  return obj;
}

AObject *ObjectMgr::realloc_object(
    AObject *obj, const uint64_t size, const ObMemAttr &attr)
{
  AObject *new_obj = NULL;

  if (NULL != obj) {
    abort_unless(obj->MAGIC_CODE_ == AOBJECT_MAGIC_CODE
                 || obj->MAGIC_CODE_ == BIG_AOBJECT_MAGIC_CODE);

    ABlock *block = obj->block();

    abort_unless(block->is_valid());
    abort_unless(block->in_use_);
    abort_unless(block->obj_set_ != NULL);

    ObjectSet *os = (ObjectSet *)block->obj_set_;
    abort_unless(os);
    if (os != NULL) {
      os->lock();
      new_obj = os->realloc_object(obj, size, attr);
      os->unlock();
    }
  } else {
    new_obj = alloc_object(size, attr);
  }

  return new_obj;
}

void ObjectMgr::free_object(AObject *obj)
{
  ABlock *block = obj->block();
  abort_unless(block->is_valid());
  abort_unless(block->in_use_);
  abort_unless(block->obj_set_ != NULL);

  ObjectSet *set = (ObjectSet *)block->obj_set_;
  set->free_object(obj);
  // TODO by fengshuo.fs: when object_set is empty, try free the sub_mgr of it.
}

ABlock *ObjectMgr::alloc_block(uint64_t size, const ObMemAttr &attr)
{
  ABlock *block = NULL;
  const uint64_t start = common::get_itid();
  SubObjectMgr *sub_mgr = nullptr;
  for (uint64_t i = 0; NULL == block && i < ATOMIC_LOAD(&sub_cnt_); i++) {
    uint64_t idx = (start + i) % sub_cnt_;
    sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx]);
    if (OB_ISNULL(sub_mgr)) {
      // do nothing
    } else if (sub_mgr->trylock()) {
      block = sub_mgr->alloc_block(size, attr);
      sub_mgr->unlock();
    }
  }
  bool stop = false;
  if (OB_ISNULL(block)) {
    auto cnt = ATOMIC_LOAD(&sub_cnt_);
    if (cnt < parallel_) {
      if (OB_NOT_NULL(sub_mgr = create_sub_mgr())) {
        if (ATOMIC_BCAS(&sub_mgrs_[cnt], nullptr, sub_mgr)) {
          ATOMIC_INC(&sub_cnt_);
        } else {
          destroy_sub_mgr(sub_mgr);
        }
      } else {
        stop = true;
      }
    }
    if (OB_ISNULL(block) && OB_LIKELY(!stop)) {
      uint64_t idx = start;
      while (OB_ISNULL(block)) {
        sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx++ % parallel_]);
        if (OB_ISNULL(sub_mgr)) {
          continue;
        } else if (OB_SUCCESS == sub_mgr->lock(ALLOC_LOCK_TIMEOUT_US)) {
          block = sub_mgr->alloc_block(size, attr);
          sub_mgr->unlock();
          break;
        }
      }
    }
  }
  return block;
}

void ObjectMgr::free_block(ABlock *block)
{
  abort_unless(block);
  abort_unless(block->is_valid());
  AChunk *chunk = block->chunk();
  abort_unless(chunk);
  abort_unless(chunk->is_valid());
  BlockSet *bs = chunk->block_set_;
  bs->lock();
  bs->free_block(block);
  bs->unlock();
  // TODO by fengshuo.fs: when block_set is empty, try free the sub_mgr of it.
}

SubObjectMgr *ObjectMgr::create_sub_mgr()
{
  SubObjectMgr *sub_mgr = nullptr;
  ObMemAttr attr;
  
  attr.label_ = common::ObModIds::OB_CTX_ALLOCATOR;
  attr.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
  auto ctx_allocator = ObMallocAllocator::get_instance()->get_ctx_allocator(attr.ctx_id_);

  class SubObjectMgrWrapper {
  public:
    SubObjectMgrWrapper(SubObjectMgr& sub_mgr)
      : sub_mgr_(sub_mgr)
    {}
    AObject *realloc_object(AObject *obj,  const uint64_t size, const ObMemAttr &attr)
    {
      int lock_ret = sub_mgr_.lock();
      AObject *new_obj = sub_mgr_.realloc_object(obj, size, attr);
      sub_mgr_.unlock();
      return new_obj;
    }
    void free_object(AObject *obj)
    {
      sub_mgr_.free_object(obj);
    }
  private:
    SubObjectMgr& sub_mgr_;
  } root_mgr(static_cast<ObjectMgr&>(ctx_allocator->get_block_mgr()).root_mgr_);
  void *ptr = ObCtxAllocator::common_realloc(NULL, sizeof(SubObjectMgr), attr,
      *(ctx_allocator.ref_allocator()), root_mgr);
  if (OB_NOT_NULL(ptr)) {
    sub_mgr = new (ptr) SubObjectMgr(ctx_allocator_, enable_no_log_,
        ablock_size_, enable_dirty_list_, blk_mgr_);
  }
  return sub_mgr;
}

void ObjectMgr::destroy_sub_mgr(SubObjectMgr *sub_mgr)
{
  if (sub_mgr != nullptr) {
    sub_mgr->~SubObjectMgr();
    ObCtxAllocator::common_free(sub_mgr);
  }
}

ObjectMgr::Stat ObjectMgr::get_stat()
{
  int64_t hold, payload, used;
  hold = payload = used = 0;
  const uint64_t start = common::get_itid();
  for (uint64_t i = 0; i < ATOMIC_LOAD(&sub_cnt_); i++) {
    uint64_t idx = (start + i) % sub_cnt_;
    auto sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx]);
    if (OB_ISNULL(sub_mgr)) {
      // do nothing
    } else {
      hold += sub_mgr->get_hold();
      payload += sub_mgr->get_payload();
      used += sub_mgr->get_used();
    }
  }
  return Stat{
      .hold_ = hold,
      .payload_ = payload,
      .used_ = used
      };
}

bool ObjectMgr::check_has_unfree()
{
  bool has_unfree = false;
  for (uint64_t idx = 0; idx < ATOMIC_LOAD(&sub_cnt_) && !has_unfree; idx++) {
    auto sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx]);
    if (OB_ISNULL(sub_mgr)) {
      // do nothing
    } else {
      sub_mgr->lock();
      DEFER(sub_mgr->unlock());
      has_unfree = sub_mgr->check_has_unfree();
    }
  }
  return has_unfree;
}

bool ObjectMgr::check_has_unfree(char *first_label, char *first_bt)
{
  bool has_unfree = false;
  for (uint64_t idx = 0; idx < ATOMIC_LOAD(&sub_cnt_) && !has_unfree; idx++) {
    auto sub_mgr = ATOMIC_LOAD(&sub_mgrs_[idx]);
    if (OB_ISNULL(sub_mgr)) {
      // do nothing
    } else {
      sub_mgr->lock();
      DEFER(sub_mgr->unlock());
      has_unfree = sub_mgr->check_has_unfree(first_label, first_bt);
    }
  }
  return has_unfree;
}
