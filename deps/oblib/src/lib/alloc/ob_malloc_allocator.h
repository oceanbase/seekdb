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

#ifndef _OB_MALLOC_ALLOCATOR_H_
#define _OB_MALLOC_ALLOCATOR_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/alloc/ob_tenant_ctx_allocator.h"
#include "lib/alloc/alloc_func.h"
#include "lib/lock/ob_rwlock.h"

namespace oceanbase
{
namespace lib
{
using std::nullptr_t;
class ObTenantCtxAllocatorGuard
{
public:
  [[nodiscard]] ObTenantCtxAllocatorGuard()
    : ObTenantCtxAllocatorGuard(nullptr) {}
  [[nodiscard]] ObTenantCtxAllocatorGuard(ObTenantCtxAllocator *allocator, const bool lock=true)
    : allocator_(allocator), lock_(lock)
  {
    if (OB_LIKELY(allocator_ != nullptr && lock)) {
      allocator_->inc_ref_cnt(1);
    }
  }
  ~ObTenantCtxAllocatorGuard()
  {
    revert();
  }
  ObTenantCtxAllocatorGuard(ObTenantCtxAllocatorGuard &&other)
    : ObTenantCtxAllocatorGuard(nullptr)
  {
    *this = std::move(other);
  }
  ObTenantCtxAllocatorGuard &operator=(ObTenantCtxAllocatorGuard &&other)
  {
    revert();
    allocator_ = other.allocator_;
    lock_ = other.lock_;
    other.allocator_ = nullptr;
    return *this;
  }
  ObTenantCtxAllocator* operator->() const
  {
    return allocator_;
  }
  ObTenantCtxAllocator* ref_allocator() const
  {
    return allocator_;
  }
  void revert()
  {
    if (OB_LIKELY(allocator_ != nullptr && lock_)) {
      allocator_->inc_ref_cnt(-1);
    }
    allocator_ = nullptr;
  }
private:
  ObTenantCtxAllocator *allocator_;
  bool lock_;
};

inline bool operator==(const ObTenantCtxAllocatorGuard &__a, nullptr_t)
{ return __a.ref_allocator() == nullptr; }

inline bool operator==(nullptr_t, const ObTenantCtxAllocatorGuard &__b)
{ return nullptr == __b.ref_allocator(); }

inline bool operator!=(const ObTenantCtxAllocatorGuard &__a, nullptr_t)
{ return __a.ref_allocator() != nullptr; }

inline bool operator!=(nullptr_t, const ObTenantCtxAllocatorGuard &__b)
{ return nullptr != __b.ref_allocator(); }

inline bool operator==(const ObTenantCtxAllocatorGuard &__a, const ObTenantCtxAllocatorGuard &__b)
{ return __a.ref_allocator() == __b.ref_allocator(); }

inline bool operator!=(const ObTenantCtxAllocatorGuard &__a, const ObTenantCtxAllocatorGuard &__b)
{ return __a.ref_allocator() != __b.ref_allocator(); }

// It's the implement of ob_malloc/ob_free/ob_realloc interface.  The
// class separates allocator for each tenant thus tenant vaolates each
// other.
class ObMallocAllocator
    : public common::ObIAllocator
{
private:
  static const uint64_t PRESERVED_TENANT_COUNT = 10000;
public:
  ObMallocAllocator();
  virtual ~ObMallocAllocator();

  void *alloc(const int64_t size);
  void *alloc(const int64_t size, const ObMemAttr &attr);
  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr);
  void free(void *ptr);

  void set_root_allocator();
  static ObMallocAllocator *get_instance();
  ObTenantCtxAllocatorGuard get_tenant_ctx_allocator(uint64_t tenant_id, uint64_t ctx_id) const;
  int create_and_add_tenant_allocator(uint64_t tenant_id);
  ObTenantCtxAllocatorGuard get_tenant_ctx_allocator_unrecycled(uint64_t tenant_id,
                                                                uint64_t ctx_id) const;
  // statistic relating
  void set_reserved(int64_t bytes);
  int64_t get_reserved() const;
  int set_tenant_limit(uint64_t tenant_id, int64_t bytes);
  int64_t get_tenant_limit(uint64_t tenant_id);
  int64_t get_tenant_hold(uint64_t tenant_id);
  int64_t get_tenant_cache_hold(uint64_t tenant_id);
  int64_t get_tenant_remain(uint64_t tenant_id);
  int64_t get_tenant_ctx_hold(const uint64_t tenant_id, const uint64_t ctx_id) const;
  void get_tenant_label_usage(uint64_t tenant_id, ObLabel &label, common::ObLabelItem &item) const;

  void print_tenant_ctx_memory_usage(uint64_t tenant_id) const;
  void print_tenant_memory_usage(uint64_t tenant_id) const;
  int set_tenant_ctx_idle(
      const uint64_t tenant_id, const uint64_t ctx_id, const int64_t size, const bool reserve = false);
  int64_t sync_wash(uint64_t tenant_id, uint64_t from_set_tenantctx_id, int64_t wash_size);
  int64_t sync_wash();
  int recycle_tenant_allocator(uint64_t tenant_id);
  int64_t get_max_used_tenant_id() { return max_used_tenant_id_; }
  void make_allocator_create_on_demand() { create_on_demand_ = true; }
  static bool is_inited_;
private:
  using InvokeFunc = std::function<int (ObTenantMemoryMgr*)>;
  static int with_resource_handle_invoke(uint64_t tenant_id, InvokeFunc func);
  int create_tenant_allocator(uint64_t tenant_id, void *buf, ObTenantCtxAllocatorV2 *&allocator);
#ifdef ENABLE_SANITY
  int get_chunks(ObTenantCtxAllocatorV2 *ta, AChunk **chunks, int cap, int &cnt);
  void modify_tenant_memory_access_permission(ObTenantCtxAllocatorV2 *ta, bool accessible);
public:
  bool enable_tenant_leak_memory_protection_ = true;
#endif
public:
  bool force_explict_500_malloc_ = false;
  int pl_leaked_times_ = 0;
  int di_leaked_times_ = 0;
  bool force_malloc_for_absent_tenant_ = false;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMallocAllocator);
private:
  ObTenantCtxAllocatorV2 *allocator_;
  int64_t reserved_;
  uint64_t max_used_tenant_id_;
  bool create_on_demand_;
}; // end of class ObMallocAllocator

extern int64_t mtl_id();

class ObMallocHook
{
public:
  static ObMallocHook &get_instance();
  void *alloc(const int64_t size);
  void free(void *ptr);
private:
  ObMallocHook();
private:
  char label_[AOBJECT_LABEL_SIZE + 1];
  ObMemAttr attr_;
  ObTenantCtxAllocatorGuard ta_;
  ObjectMgrV2 &mgr_;
};

} // end of namespace lib
} // end of namespace oceanbase

extern void enable_malloc_v2(bool enable);
extern bool is_malloc_v2_enabled();
#endif /* _OB_MALLOC_ALLOCATOR_H_ */
