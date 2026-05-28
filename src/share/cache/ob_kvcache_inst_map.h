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

#ifndef OCEANBASE_CACHE_OB_KVCACHE_INST_MAP_H_
#define OCEANBASE_CACHE_OB_KVCACHE_INST_MAP_H_

#include "lib/atomic/ob_atomic.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/ob_lf_fifo_allocator.h"
#include "lib/resource/ob_resource_mgr.h"
#include "lib/container/ob_array.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_hashset.h"
#include "lib/queue/ob_fixed_queue.h"
#include "lib/lock/ob_drw_lock.h"
#include "share/cache/ob_cache_utils.h"
#include "share/cache/ob_kvcache_struct.h"
#include "share/ob_i_tenant_mem_limit_getter.h"

namespace oceanbase
{
namespace common
{
class ObKVCacheInstMap;
class HazardDomain;
struct ObTenantMBList
{
  ObTenantMBList() { reset(); }
  ~ObTenantMBList() { reset(); }

  int init(const uint64_t tenant_id);
  void reset() {
    head_.reset();
    head_.prev_ = &head_;
    head_.next_ = &head_;
    resource_mgr_.reset();
    tenant_id_ = common::OB_INVALID_ID;
    ref_cnt_ = 0;
    inited_ = false;
  }
  inline bool is_valid() const { return inited_; }
  inline void inc_ref() { ATOMIC_AAF(&ref_cnt_, 1); }
  int64_t dec_ref() { return ATOMIC_SAF(&ref_cnt_, 1); }
  int64_t get_ref() const { return ATOMIC_LOAD(&ref_cnt_); }

  ObKVMemBlockHandle head_;
  lib::ObTenantResourceMgrHandle resource_mgr_;
  uint64_t tenant_id_;
  int64_t ref_cnt_;
  bool inited_;
};

struct ObKVCacheInst
{
  int64_t cache_id_;
  ObKVCacheStatus status_;
  ObLfFIFOAllocator *node_allocator_;
  bool is_delete_;
  bool is_block_cache_;
  int64_t ref_cnt_;
  ObKVCacheInst()
    : cache_id_(0),
      status_(),
      node_allocator_(nullptr),
      is_delete_(false),
      is_block_cache_(false),
      ref_cnt_(0) {}
  bool can_destroy() const ;
  void reset() {
    cache_id_ = 0;
    status_.reset();
    node_allocator_ = nullptr;
    is_delete_ = false;
    is_block_cache_ = false;
    ref_cnt_ = 0;
  }
  bool is_valid() const { return ref_cnt_ > 0; }
  bool is_mark_delete() const { return ATOMIC_LOAD(&is_delete_); }
  void try_mark_delete();

  TO_STRING_KV(K_(cache_id), K_(is_delete), K_(status), K_(is_block_cache), K_(ref_cnt));
};

class ObKVCacheInstHandle
{
public:
  ObKVCacheInstHandle();
  virtual ~ObKVCacheInstHandle();
  void reset();
  bool is_valid() const;
  inline ObKVCacheInst *get_inst() { return inst_; }
  ObKVCacheInstHandle(const ObKVCacheInstHandle &other);
  ObKVCacheInstHandle& operator = (const ObKVCacheInstHandle& other);
  VIRTUAL_TO_STRING_KV(K_(inst));
private:
  friend class ObKVCacheInstMap;
  ObKVCacheInstMap *map_;
  ObKVCacheInst *inst_;
};

class ObKVCacheInstMap
{
public:
  ObKVCacheInstMap();
  virtual ~ObKVCacheInstMap();
  int init(const int64_t max_entry_cnt, const ObKVCacheConfig *configs,
           const ObITenantMemLimitGetter &mem_limit_getter,
           ObLfFIFOAllocator *node_allocator);
  void destroy();
  int get_cache_inst(
      const ObKVCacheInstKey &inst_key,
      ObKVCacheInstHandle &inst_handle);
  int mark_tenant_delete(const uint64_t tenant_id);
  int erase_tenant(const uint64_t tenant_id);
  int refresh_score();
  int get_cache_info(ObIArray<ObKVCacheInstHandle> &inst_handles);
  void print_all_cache_info();

private:
  friend class ObKVCacheInstHandle;
  typedef hash::ObHashMap<ObKVCacheInstKey, ObKVCacheInst*, hash::NoPthreadDefendMode> KVCacheInstMap;
  void add_inst_ref(ObKVCacheInst *inst);
  void de_inst_ref(ObKVCacheInst *inst);
  int inner_push_inst_handle(const KVCacheInstMap::iterator &iter, ObIArray<ObKVCacheInstHandle> &inst_handles);
private:
  DRWLock lock_;
  KVCacheInstMap  inst_map_;
  const ObKVCacheConfig *configs_;

  const ObITenantMemLimitGetter *mem_limit_getter_;
  ObLfFIFOAllocator *node_allocator_;

  // used by erase tenant cache inst
  bool is_inited_;
};


}//end namespace common
}//end namespace oceanbase

#endif //OCEANBASE_CACHE_OB_KVCACHE_INST_MAP_H_
