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

#ifndef OB_SERVER_OBJECT_POOL_H_
#define OB_SERVER_OBJECT_POOL_H_


/*****************************************************************************
 * Global object cache pool, each type has its independent cache pool.
 * Cache queues are twice the number of cores, threads map to cache queues based on thread ID.
 *
 *   sop_borrow(type) is used to obtain an object
 *   sop_return(type, ptr) returns an object
 *
 *   sop_borrow(type) is equivalent to ObServerObjectPool<type>::get_instance().borrow_object()
 *   sop_return(type, ptr) is equivalent to ObServerObjectPool<type>::get_instance().return_object(ptr)
 *
 * The virtual table __all_virtual_server_object_pool can be used to view information about each cache queue of all object cache pools,
 * the meaning of each column in the virtual table refers to the comments of each attribute in ObPoolArenaHead
 *
 *****************************************************************************/

#include <lib/utility/ob_macro_utils.h>
#include <lib/thread_local/ob_tsi_utils.h>
#include <lib/atomic/ob_atomic.h>
#include <lib/utility/utility.h>
#include <lib/cpu/ob_cpu_topology.h>
#include "lib/ob_running_mode.h"
#include "lib/lock/ob_small_spin_lock.h"

#define PTR_META2OBJ(x) reinterpret_cast<T*>(reinterpret_cast<char*>(x) + sizeof(Meta));
#define PTR_OBJ2META(y) reinterpret_cast<Meta*>(reinterpret_cast<char*>(y) - sizeof(Meta));

namespace oceanbase
{
namespace common
{

struct ObPoolArenaHead
{
  common::ObByteLock lock;  // Lock, 4 Bytes
  int32_t borrow_cnt; // Number of calls
  int32_t return_cnt; // Number of calls
  int32_t miss_cnt; // Number of direct allocations
  int32_t miss_return_cnt; // Number of returns directly allocated
  int16_t free_num;  // Number of caches
  int64_t all_using_cnt;
  int16_t reserved1;
  int64_t last_borrow_ts; // Time of last visit
  int64_t last_return_ts; // Time of last visit
  int64_t last_miss_ts; // The time of the last direct allocation
  int64_t last_miss_return_ts; // The return time of the last direct allocation
  void *next; // Point to the first free object
  TO_STRING_KV(
      KP(this),
      K(borrow_cnt),
      K(return_cnt),
      K(miss_cnt),
      K(miss_return_cnt),
      K(last_borrow_ts),
      K(last_return_ts),
      K(last_miss_ts),
      K(last_miss_return_ts),
      KP(next));
  void reset() {
    new (&lock) common::ObByteLock();
    borrow_cnt = 0;
    return_cnt = 0;
    miss_cnt = 0;
    miss_return_cnt = 0;
    free_num = 0;
    all_using_cnt = 0;
    reserved1 = 0;
    last_borrow_ts = 0;
    last_return_ts = 0;
    last_miss_ts = 0;
    last_miss_return_ts = 0;
    next = NULL;
  }
};

/**
 * ObServerObjectPool is a global singleton used to cache Objects, mainly used for caching large objects with high construction and destruction costs.
 * This class caches objects, using a number of cache linked lists equal to the number of cores multiplied by 2.
 *
 * Interfaces:
 *   borrow_object: acquire object
 *   return_object: return object
 */
template <class T>
class ObServerObjectPool
{
  const char *LABEL = ObModIds::OB_SERVER_OBJECT_POOL;
public:
  T* borrow_object() {
    T *ctx = NULL;
    if (OB_LIKELY(is_inited_)) {
      Meta *cmeta = NULL;
      int64_t itid = get_itid();
      int64_t aid = itid % arena_num_;
      ObPoolArenaHead &arena = arena_[aid];
      { // Enter the critical area of the arena, the timestamp is obtained outside the lock, and minimize the length of the critical area
        ObSmallSpinLockGuard<common::ObByteLock> lock_guard(arena.lock);
        cmeta = static_cast<Meta*>(arena.next);
        if (NULL != cmeta) {
          arena.next = static_cast<void*>(cmeta->next);
        }
      }
      if (NULL != cmeta) {
        ctx = PTR_META2OBJ(cmeta);
      }
      if (NULL == ctx) {
        ObMemAttr attr(tenant_id_, LABEL);
        SET_USE_500(attr);
        char *p = static_cast<char*>(ob_malloc(item_size_, attr));
        if (NULL == p) {
          COMMON_LOG_RET(ERROR, common::OB_ALLOCATE_MEMORY_FAILED, "allocate memory failed", K(typeid(T).name()), K(item_size_));
        } else {
          Meta *cmeta = reinterpret_cast<Meta*>(p);
          cmeta->next = NULL;
          cmeta->arena_id = -(aid + 1);
          cmeta->magic = 0xFEDCFEDC01230123;
          ctx = PTR_META2OBJ(p);
          new (ctx) T();
        }
      }
      if (NULL != ctx) {
        ATOMIC_INC(&arena.all_using_cnt);
      }
    }
    return ctx;
  }

  void return_object(T* x) {
    if (NULL == x) {
      COMMON_LOG_RET(ERROR, common::OB_ALLOCATE_MEMORY_FAILED, "allocate memory failed", K(typeid(T).name()), K(item_size_), K(get_itid()));
    } else if (OB_UNLIKELY(!is_inited_)){
      COMMON_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "unexpected return", K(typeid(T).name()), K(item_size_), K(get_itid()));
    } else {
      Meta *cmeta = PTR_OBJ2META(x);
      int64_t aid = cmeta->arena_id;
      if (aid >= 0) {
        x->reset();
        ObPoolArenaHead &arena = arena_[aid];
        { // Enter the critical area of the arena, the timestamp is obtained outside the lock, and minimize the length of the critical area
          ObSmallSpinLockGuard<common::ObByteLock> lock_guard(arena.lock);
          cmeta->next = static_cast<Meta*>(arena.next);
          arena.next = static_cast<void*>(cmeta);
          ATOMIC_DEC(&arena.all_using_cnt);
        }
      } else {
        x->~T();
        ob_free(cmeta);
        ObPoolArenaHead &arena = arena_[-(aid + 1)];
        ATOMIC_DEC(&arena.all_using_cnt);
      }
    }
  }

  /**
   * Brutally allocate 16 available objects for each entry at Pool construction time
   * Memory is directly allocated in one go using ob_malloc based on the total size
   * All objects are placed into their respective entries
   * Since it is a global singleton, this work is completed at program startup
   * TODO: Change to on-demand allocation
   */
  ObServerObjectPool(const int64_t tenant_id, const bool regist, const bool is_mini_mode,
                     const int64_t cpu_count)
    : tenant_id_(tenant_id), regist_(regist), is_mini_mode_(is_mini_mode),
      cpu_count_(cpu_count), arena_num_(0),
      arena_(NULL), cnt_per_arena_(0), item_size_(0), buf_(nullptr), is_inited_(false)
  {}

  int init()
  {
    int ret = OB_SUCCESS;
    const bool is_mini = is_mini_mode_;
    arena_num_ = min(64/*upper_bound*/, max(4/*lower_bound*/, static_cast<int32_t>(cpu_count_) * 2));
    //If the assignment logic of buf_ below is not reached, buf_ will not be initialized
    buf_ = NULL;
    cnt_per_arena_ = is_mini ? 8 : 64;
    int64_t s = (sizeof(T) + sizeof(Meta)); // Each cached object header has a Meta field to store necessary information and linked list pointers
    item_size_ = upper_align(s, CACHE_ALIGN_SIZE); // Align according to the cache line to ensure that there will be no false sharing between objects
    ObMemAttr attr(tenant_id_, LABEL);
    SET_USE_500(attr);
    void *ptr = NULL;
    if (OB_ISNULL(ptr = ob_malloc(sizeof(ObPoolArenaHead) * arena_num_,
                                  SET_USE_500(ObMemAttr(tenant_id_, LABEL))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(ERROR, "allocate memory failed", K(ret), K(typeid(T).name()));
    } else if ((buf_ = ob_malloc(arena_num_ * cnt_per_arena_ * item_size_, attr)) == NULL) {
      ob_free(ptr);
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(ERROR, "allocate memory failed", K(ret), K(typeid(T).name()), K(item_size_),
                 K(arena_num_), K(+cnt_per_arena_));
    } else {
      arena_ = (ObPoolArenaHead*)ptr;
      char *p = reinterpret_cast<char*>(buf_);
      for (int64_t i = 0; i < arena_num_; ++i) {
        Meta *pmeta = NULL;
        for (int64_t j = 0; j < cnt_per_arena_; ++j) {
          Meta *cmeta = reinterpret_cast<Meta*>(p);
          cmeta->next = pmeta;
          cmeta->arena_id = i;
          cmeta->magic = 0xFEDCFEDC01240124;
          pmeta = cmeta;
          new (p + sizeof(Meta)) T();
          p += item_size_;
        }
        ObPoolArenaHead &arena = arena_[i];
        arena.reset();
        arena.next = static_cast<void*>(pmeta);
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
    return ret;
  }

  void destroy() {
    if (is_inited_) {
      bool has_unfree = false;
      for (int64_t i = 0; !has_unfree && i < arena_num_; ++i) {
        ObPoolArenaHead &arena = arena_[i];
        has_unfree = ATOMIC_LOAD(&arena.all_using_cnt) > 0;
      }
      if (!has_unfree) {
        for (int64_t i = 0; i < arena_num_; ++i) {
          Meta *meta = NULL;
          {
            ObPoolArenaHead &arena = arena_[i];
            ObSmallSpinLockGuard<common::ObByteLock> lock_guard(arena.lock);
            meta = static_cast<Meta*>(arena.next);
            arena.next = NULL;
          }
          while (meta != NULL) {
            T *x = PTR_META2OBJ(meta);
            x->reset();
            x->~T();
            meta = meta->next;
          }
        }
        if (buf_ != NULL) {
          ob_free(buf_);
          buf_ = NULL;
        }
        if (arena_ != NULL) {
          ob_free(arena_);
          arena_ = NULL;
        }
        is_inited_ = false;
      }
      if (has_unfree) {
        COMMON_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "server object leak", K(tenant_id_), K(typeid(T).name()));
      }
    }
  }

  ~ObServerObjectPool() {
    destroy();
  }
private:
  struct Meta
  {
    Meta * next;
    int64_t arena_id;
    int64_t magic;
    char padding__[8];
  };
  const int64_t tenant_id_;
  const bool regist_;
  const bool is_mini_mode_;
  const int64_t cpu_count_;
  int32_t arena_num_;
  ObPoolArenaHead *arena_;
  int64_t cnt_per_arena_;
  int64_t item_size_;
  void *buf_;
  bool is_inited_;
};

template<typename T>
inline ObServerObjectPool<T>& get_server_object_pool() {
  class Wrapper {
  public:
    Wrapper()
      : instance_(OB_SERVER_TENANT_ID, true/*regist*/, lib::is_mini_mode(),
                  get_cpu_count())
    {
      instance_.init(); // is_inited_ will be checked all invokes
    }
    ObServerObjectPool<T> instance_;
  };
  static Wrapper w;
  return w.instance_;
}

#define sop_borrow(type)                                                                                        \
  ({                                                                                                            \
    type *iter = common::get_server_object_pool<type>().borrow_object();                                        \
    if (OB_NOT_NULL(iter)) {                                                                                    \
      storage::ObStorageLeakChecker::get_instance().handle_hold(iter); \
    }                                                                                                           \
    (iter);                                                                                                     \
  })

#define sop_return(type, ptr)                                                                                   \
  do {                                                                                                          \
    if (OB_NOT_NULL(ptr)) {                                                                                     \
      storage::ObStorageLeakChecker::get_instance().handle_reset(ptr); \
    }                                                                                                           \
    common::get_server_object_pool<type>().return_object(ptr);                                                  \
  } while (false)

}
}
#endif // OB_SERVER_OBJECT_POOL_H_
