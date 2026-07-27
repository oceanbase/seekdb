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

#include "lib/alloc/ob_iallocator.h"
#include "lib/alloc/ob_ctx_allocator.h"
#include "lib/alloc/alloc_func.h"
#include "lib/lock/ob_rwlock.h"

namespace oceanbase
{
namespace lib
{
using std::nullptr_t;
class ObCtxAllocatorGuard
{
public:
  [[nodiscard]] ObCtxAllocatorGuard()
    : ObCtxAllocatorGuard(nullptr) {}
  [[nodiscard]] ObCtxAllocatorGuard(ObCtxAllocator *allocator)
    : allocator_(allocator) {}
  ~ObCtxAllocatorGuard() = default;
  ObCtxAllocatorGuard(ObCtxAllocatorGuard &&other)
    : ObCtxAllocatorGuard(nullptr)
  {
    *this = std::move(other);
  }
  ObCtxAllocatorGuard &operator=(ObCtxAllocatorGuard &&other)
  {
    allocator_ = other.allocator_;
    other.allocator_ = nullptr;
    return *this;
  }
  ObCtxAllocator* operator->() const
  {
    return allocator_;
  }
  ObCtxAllocator* ref_allocator() const
  {
    return allocator_;
  }
  void revert()
  {
    allocator_ = nullptr;
  }
private:
  ObCtxAllocator *allocator_;
};

inline bool operator==(const ObCtxAllocatorGuard &__a, nullptr_t)
{ return __a.ref_allocator() == nullptr; }

inline bool operator==(nullptr_t, const ObCtxAllocatorGuard &__b)
{ return nullptr == __b.ref_allocator(); }

inline bool operator!=(const ObCtxAllocatorGuard &__a, nullptr_t)
{ return __a.ref_allocator() != nullptr; }

inline bool operator!=(nullptr_t, const ObCtxAllocatorGuard &__b)
{ return nullptr != __b.ref_allocator(); }

inline bool operator==(const ObCtxAllocatorGuard &__a, const ObCtxAllocatorGuard &__b)
{ return __a.ref_allocator() == __b.ref_allocator(); }

inline bool operator!=(const ObCtxAllocatorGuard &__a, const ObCtxAllocatorGuard &__b)
{ return __a.ref_allocator() != __b.ref_allocator(); }

// Implements the ob_malloc/ob_free/ob_realloc interface and dispatches
// allocations to context-specific allocators.
class ObMallocAllocator
    : public common::ObIAllocator
{
public:
  ObMallocAllocator();
  virtual ~ObMallocAllocator();

  void *alloc(const int64_t size);
  void *alloc(const int64_t size, const ObMemAttr &attr);
  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr);
  void free(void *ptr);

  void set_root_allocator();
  static ObMallocAllocator *get_instance();
  ObCtxAllocatorGuard get_ctx_allocator(uint64_t ctx_id) const;
  // statistic relating
  void set_reserved(int64_t bytes);
  int64_t get_reserved() const;
  int set_allocator_hard_limit(int64_t bytes);
  int64_t get_allocator_hard_limit();
  int set_allocator_limit(int64_t bytes);
  int64_t get_total_limit();
  int64_t get_total_hold();
  int64_t get_allocator_cache_hold();
  int64_t get_allocator_remain();
  int64_t get_ctx_hold(const uint64_t ctx_id) const;
  void get_label_usage(ObLabel &label, common::ObLabelItem &item) const;

  void print_ctx_memory_usage() const;
  void print_memory_usage() const;
  int set_ctx_idle(
      const uint64_t ctx_id, const int64_t size, const bool reserve = false);
  static bool is_inited_;
private:
  using InvokeFunc = std::function<int (ObMemoryMgr*)>;
  static int with_resource_handle_invoke(InvokeFunc func);
  int create_allocator(void *buf, ObCtxAllocatorState *&allocator);
public:
  int pl_leaked_times_ = 0;
  int di_leaked_times_ = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMallocAllocator);
private:
  ObCtxAllocatorState *allocator_;
  int64_t reserved_;
}; // end of class ObMallocAllocator


} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OB_MALLOC_ALLOCATOR_H_ */
