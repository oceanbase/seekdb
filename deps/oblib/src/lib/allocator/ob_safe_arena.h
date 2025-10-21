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

#ifndef OCEANBASE_COMMON_SAFE_ARENA_H__
#define OCEANBASE_COMMON_SAFE_ARENA_H__

#include "lib/allocator/page_arena.h"  // ModuleArena
#include "lib/lock/ob_spin_lock.h"     // ObSpinLock

namespace oceanbase
{
namespace common
{
class ObSafeArena : public ObIAllocator
{
public:
  ObSafeArena(const lib::ObLabel &label, const int64_t page_size = OB_MALLOC_NORMAL_BLOCK_SIZE,
              int64_t tenant_id = OB_SERVER_TENANT_ID)
      : arena_alloc_(label, page_size, tenant_id),
        lock_(ObLatchIds::OB_AREAN_ALLOCATOR_LOCK)
  {}

  virtual ~ObSafeArena() {}

public:
  virtual void *alloc(const int64_t sz) override
  {
    ObSpinLockGuard guard(lock_);
    return arena_alloc_.alloc(sz);
  }

  virtual void* alloc(const int64_t sz, const ObMemAttr &attr) override
  {
    ObSpinLockGuard guard(lock_);
    return arena_alloc_.alloc(sz, attr);
  }

  virtual void free(void *ptr) override
  {
    ObSpinLockGuard guard(lock_);
    arena_alloc_.free(ptr);
  }

  virtual void clear()
  {
    ObSpinLockGuard guard(lock_);
    arena_alloc_.clear();
  }

  virtual void reuse() override
  {
    ObSpinLockGuard guard(lock_);
    arena_alloc_.reuse();
  }

  virtual void reset() override
  {
    ObSpinLockGuard guard(lock_);
    arena_alloc_.reset();
  }

  int64_t used() const override
  { return arena_alloc_.used(); }

  int64_t total() const override
  { return arena_alloc_.total(); }

  ModuleArena &get_arena()
  {
    return arena_alloc_.get_arena();
  }

private:
  ObArenaAllocator arena_alloc_;
  ObSpinLock lock_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObSafeArena);
};
} // namespace common
} // namespace oceanbase
#endif /* OCEANBASE_COMMON_SAFE_ARENA_H__ */
