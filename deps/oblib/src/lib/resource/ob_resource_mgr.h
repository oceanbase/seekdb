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

#ifndef OCEANBASE_RESOURCE_OB_RESOURCE_MGR_H_
#define OCEANBASE_RESOURCE_OB_RESOURCE_MGR_H_

#include "lib/ob_define.h"
#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/resource/ob_cache_washer.h"
#include "lib/resource/achunk_mgr.h"

namespace oceanbase
{
namespace lib
{
class ObMemoryMgr
{
public:
  static const int64_t LARGE_REQUEST_EXTRA_MB_COUNT = 2;
  static const int64_t ALIGN_SIZE = static_cast<int64_t>(INTACT_ACHUNK_SIZE);
  ObMemoryMgr();

  virtual ~ObMemoryMgr() {}

  void set_cache_washer(ObICacheWasher &cache_washer);
  AChunk *alloc_chunk(const int64_t size, const ObMemAttr &attr);
  void free_chunk(AChunk *chunk, const ObMemAttr &attr);

  // used by cache module
  void *alloc_cache_mb(const int64_t size);
  void free_cache_mb(void *ptr);

  
  void set_hard_limit(const int64_t hard_limit) { hard_limit_ = hard_limit; }
  int64_t get_hard_limit() const { return hard_limit_; }
  void set_limit(const int64_t limit) { limit_ = limit; }
  int64_t get_limit() const { return limit_; }
  int64_t get_sum_hold() const { return sum_hold_; }
  int64_t get_cache_hold() const { return cache_hold_; }
  int64_t get_cache_item_count() const { return cache_item_count_; }
  const volatile int64_t *get_ctx_hold_bytes() const { return hold_bytes_; }
  inline static int64_t align(const int64_t size)
  {
    return static_cast<int64_t>(CHUNK_MGR.aligned(static_cast<uint64_t>(size)));
  }
  int set_ctx_hard_limit(const uint64_t ctx_id, const int64_t hard_limit);
  int set_ctx_limit(const uint64_t ctx_id, const int64_t limit);
  int get_ctx_limit(const uint64_t ctx_id, int64_t &limit) const;
  int get_ctx_hold(const uint64_t ctx_id, int64_t &hold) const;
  bool update_hold(const int64_t size, const uint64_t ctx_id, const lib::ObLabel &label,
      bool &reach_ctx_limit, bool high_prio = false);
private:
  void update_cache_hold(const int64_t size);
  bool update_ctx_hold(const uint64_t ctx_id, const int64_t size, bool high_prio);
  AChunk *ptr2chunk(void *ptr);
  AChunk *alloc_chunk_(const int64_t size, const ObMemAttr &attr);
  void free_chunk_(AChunk *chunk, const ObMemAttr &attr);
  ObICacheWasher *cache_washer_;
  
  int64_t limit_;
  int64_t hard_limit_;
  int64_t sum_hold_;
  int64_t cache_hold_;
  int64_t cache_item_count_;
  volatile int64_t hold_bytes_[common::ObCtxIds::MAX_CTX_ID];
  volatile int64_t limit_bytes_[common::ObCtxIds::MAX_CTX_ID];
  volatile int64_t hard_limit_bytes_[common::ObCtxIds::MAX_CTX_ID];
};

struct ObResourceState
{
  ObResourceState();
  virtual ~ObResourceState();

  
  ObMemoryMgr memory_mgr_;
  // add other mgr here
  int64_t ref_cnt_;
};

class ObResourceMgr;
class ObResourceMgrHandle
{
public:
  ObResourceMgrHandle();
  virtual ~ObResourceMgrHandle();

  int init(ObResourceMgr *owner, ObResourceState *state);
  bool is_valid() const;
  void reset();
  ObMemoryMgr *get_memory_mgr();
  const ObMemoryMgr *get_memory_mgr() const;
private:
  ObResourceMgr *owner_;
  ObResourceState *state_;
};

class ObResourceMgr
{
  friend class ObResourceMgrHandle;
public:
  ObResourceMgr();
  virtual ~ObResourceMgr();

  int init();
  void destroy();
  static ObResourceMgr &get_instance();
  int set_cache_washer(ObICacheWasher &cache_washer);

  // Creates the resource state on first use.
  int get_handle(ObResourceMgrHandle &handle);
private:
  void inc_ref(ObResourceState *state);
  void dec_ref(ObResourceState *state);
  int get_state_unsafe(ObResourceState *&state);
  int remove_state_unsafe();
  int create_state_unsafe(ObResourceState *&state);

  bool inited_;
  ObICacheWasher *cache_washer_;
  // single server resource state
  common::SpinRWLock lock_;
  ObResourceState *state_;
};

}//end namespace lib
}//end namespace oceanbase

#endif //OCEANBASE_CACHE_OB_CACHE_MEMORY_MGR_H_
