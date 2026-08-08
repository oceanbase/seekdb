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

#ifndef _OCEABASE_LIB_ALLOC_OBJECT_SET_H_
#define _OCEABASE_LIB_ALLOC_OBJECT_SET_H_

#include "alloc_struct.h"
#include "lib/utility/alloc_assist.h"
#include "abit_set.h"
#include "block_set.h"
#include "lib/lock/ob_mutex.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace common
{
class ObAllocator;
}
namespace lib
{
class __MemoryContext__;
class ObCtxAllocator;
class IBlockMgr;
class ISetLocker;
class ObjectSet
{
  friend class common::ObAllocator;
  static const uint32_t META_CELLS = (AOBJECT_META_SIZE - 1) / AOBJECT_CELL_BYTES + 1;
  static const uint32_t MIN_FREE_CELLS = META_CELLS + 1+ (15 / AOBJECT_CELL_BYTES);  // next, prev pointer
  static const double FREE_LISTS_BUILD_RATIO;
  static const double BLOCK_CACHE_RATIO;

  typedef AObject *FreeList;
  typedef ABitSet BitMap;

public:
  ObjectSet(__MemoryContext__ *mem_context=nullptr,
            const uint32_t ablock_size=INTACT_NORMAL_AOBJECT_SIZE,
            const bool enable_dirty_list=false);
  ~ObjectSet();
  static bool check_has_unfree(ABlock* block, char *first_label, char *first_bt);

  // main interfaces
  AObject *alloc_object(const uint64_t size, const ObMemAttr &attr);
  void free_object(AObject *obj);
  AObject *realloc_object(AObject *obj, const uint64_t size, const ObMemAttr &attr);
  void reset();

  // statistics
  uint64_t get_alloc_bytes() const;
  uint64_t get_hold_bytes() const;
  uint64_t get_allocs() const;

  void lock();
  void unlock();

  // statistics
  void set_block_mgr(IBlockMgr *blk_mgr) { blk_mgr_ = blk_mgr; }
  IBlockMgr *get_block_mgr() { return blk_mgr_; }
  void set_locker(ISetLocker *locker) { locker_ = locker; }
  inline int64_t get_normal_hold() const;
  inline int64_t get_normal_used() const;
  inline int64_t get_normal_alloc() const;
  bool check_has_unfree(char *first_label, char *first_bt);
private:
  AObject *alloc_normal_object(const uint32_t cls, const ObMemAttr &attr);
  AObject *alloc_big_object(const uint64_t size, const ObMemAttr &attr);

  ABlock *alloc_block(const uint64_t size, const ObMemAttr &attr);
  void free_block(ABlock *block);

  AObject *get_free_object(const uint32_t cls);
  void add_free_object(AObject *obj);

  void free_big_object(AObject *obj);
  void take_off_free_object(AObject *obj);
  void free_normal_object(AObject *obj);

  bool build_free_lists();

  inline AObject *split_obj(AObject *obj, const uint32_t cls, AObject *&remainder);
  inline AObject *merge_obj(AObject *obj);

  void do_free_object(AObject *obj);
  void do_free_dirty_list();

private:
  __MemoryContext__ *mem_context_;
  ISetLocker *locker_;
  IBlockMgr *blk_mgr_;

  ABlock *blist_;

  AObject *last_remainder_;

  BitMap *bm_;
  FreeList *free_lists_;

  lib::ObMutex dirty_list_mutex_;
  AObject *dirty_list_;
  int64_t dirty_objs_;

  uint64_t alloc_bytes_;
  uint64_t used_bytes_;
  uint64_t hold_bytes_;
  uint64_t allocs_;

  uint64_t normal_alloc_bytes_;
  uint64_t normal_used_bytes_;
  uint64_t normal_hold_bytes_;

  uint32_t ablock_size_;
  bool enable_dirty_list_;
  uint32_t cells_per_block_;

  DISALLOW_COPY_AND_ASSIGN(ObjectSet);
} CACHE_ALIGNED; // end of class ObjectSet

inline void ObjectSet::lock()
{
  ObDisableDiagnoseGuard diagnose_disable_guard;
  locker_->lock();
}

inline void ObjectSet::unlock()
{
  ObDisableDiagnoseGuard diagnose_disable_guard;
  locker_->unlock();
}

inline uint64_t ObjectSet::get_alloc_bytes() const
{
  return alloc_bytes_;
}

inline uint64_t ObjectSet::get_hold_bytes() const
{
  return hold_bytes_;
}

inline uint64_t ObjectSet::get_allocs() const
{
  return allocs_;
}

inline int64_t ObjectSet::get_normal_hold() const
{
  return normal_hold_bytes_;
}

inline int64_t ObjectSet::get_normal_used() const
{
  return normal_used_bytes_;
}

inline int64_t ObjectSet::get_normal_alloc() const
{
  return normal_alloc_bytes_;
}

} // end of namespace lib
} // end of namespace oceanbase
#endif /* _OCEABASE_LIB_ALLOC_OBJECT_SET_H_ */
