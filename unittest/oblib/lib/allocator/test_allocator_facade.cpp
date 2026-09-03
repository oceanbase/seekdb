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

#include <cstdint>
#include <gtest/gtest.h>
#include "lib/allocator/ob_malloc.h"
#include "lib/rc/context.h"
#if defined(OB_HAVE_BUNDLED_JEMALLOC) && defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;

namespace
{

class CountingAllocator final : public ObIAllocator
{
public:
  CountingAllocator()
    : allocator_(), live_allocations_(0), live_bytes_(0), fail_next_realloc_(false)
  {}

  void *alloc(const int64_t size) override
  {
    return alloc(size, ObMemAttr());
  }

  void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    void *ptr = allocator_.alloc(size, attr);
    if (nullptr != ptr) {
      ++live_allocations_;
      live_bytes_ += ob_malloc_usable_size(ptr);
    }
    return ptr;
  }

  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr) override
  {
    if (fail_next_realloc_) {
      fail_next_realloc_ = false;
      return nullptr;
    }
    const int64_t old_size = nullptr != ptr
        ? ob_malloc_usable_size(const_cast<void *>(ptr))
        : 0;
    void *new_ptr = allocator_.realloc(ptr, size, attr);
    if (nullptr == ptr && nullptr != new_ptr) {
      ++live_allocations_;
    }
    if (nullptr != new_ptr) {
      live_bytes_ += ob_malloc_usable_size(new_ptr) - old_size;
    }
    return new_ptr;
  }

  void free(void *ptr) override
  {
    if (nullptr != ptr) {
      live_bytes_ -= ob_malloc_usable_size(ptr);
      allocator_.free(ptr);
      --live_allocations_;
    }
  }

  int64_t live_allocations() const { return live_allocations_; }
  int64_t live_bytes() const { return live_bytes_; }
  void fail_next_realloc() { fail_next_realloc_ = true; }

private:
  ObMalloc allocator_;
  int64_t live_allocations_;
  int64_t live_bytes_;
  bool fail_next_realloc_;
};

MemoryUsageTracker *context_memory_tracker = nullptr;

MemoryUsageTracker *resolve_context_memory_tracker(const int64_t ctx_id)
{
  return ObCtxIds::WORK_AREA == ctx_id ? context_memory_tracker : nullptr;
}

class ContextMemoryTrackerGuard final
{
public:
  explicit ContextMemoryTrackerGuard(MemoryUsageTracker &tracker)
  {
    context_memory_tracker = &tracker;
    set_memory_usage_tracker_resolver(
        ObCtxIds::WORK_AREA, resolve_context_memory_tracker);
  }

  ~ContextMemoryTrackerGuard()
  {
    set_memory_usage_tracker_resolver(ObCtxIds::WORK_AREA, nullptr);
    context_memory_tracker = nullptr;
  }
};

} // namespace

TEST(TestAllocatorFacade, allocate_reallocate_free)
{
  ObMemAttr attr;
  void *ptr = ob_malloc(100, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(ob_malloc_usable_size(ptr), 100);

  ptr = ob_realloc(ptr, 200, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(ob_malloc_usable_size(ptr), 200);
  ob_free(ptr);
}

TEST(TestMemoryContextMalloc, destroy_reclaims_owned_allocations)
{
  CountingAllocator backing_allocator;
  ObMemAttr attr("MemCtxTest");
  {
    MemoryContextMalloc owner(backing_allocator, attr, true);
    MemoryContextMalloc dispatcher(backing_allocator, attr, false);
    void *first = owner.alloc(64);
    void *second = owner.alloc(128);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    ASSERT_EQ(2, backing_allocator.live_allocations());
    ASSERT_EQ(backing_allocator.live_bytes(), owner.total());

    dispatcher.free(first);
    ASSERT_EQ(1, backing_allocator.live_allocations());
    ASSERT_EQ(backing_allocator.live_bytes(), owner.total());
    ASSERT_EQ(0, dispatcher.total());

    const int64_t before_failed_realloc = owner.total();
    backing_allocator.fail_next_realloc();
    ASSERT_EQ(nullptr, dispatcher.realloc(second, 256, attr));
    ASSERT_EQ(before_failed_realloc, owner.total());
    ASSERT_EQ(1, backing_allocator.live_allocations());
    ASSERT_EQ(backing_allocator.live_bytes(), owner.total());

    second = dispatcher.realloc(second, 256, attr);
    ASSERT_NE(nullptr, second);
    ASSERT_EQ(1, backing_allocator.live_allocations());
    ASSERT_GT(owner.total(), 0);
    ASSERT_EQ(backing_allocator.live_bytes(), owner.total());
  }
  ASSERT_EQ(0, backing_allocator.live_allocations());
}

TEST(TestMemoryContextMalloc, context_lifecycle_releases_tracked_pages)
{
  MemoryUsageTracker tracker;
  ContextMemoryTrackerGuard tracker_guard(tracker);
  ContextParam param;
  param.set_mem_attr("TrackCtxArena", ObCtxIds::WORK_AREA);
  MemoryContext context;
  ASSERT_EQ(OB_SUCCESS,
            MemoryContext::root()->CREATE_CONTEXT(context, param));

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  ASSERT_GT(context->arena_hold(), 0);
  ASSERT_EQ(context->arena_hold(), tracker.used());

  context->reuse();
  ASSERT_EQ(0, context->arena_used());
  ASSERT_EQ(context->arena_hold(), tracker.used());

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  context->get_arena_allocator().reset();
  ASSERT_EQ(0, context->arena_hold());
  ASSERT_EQ(0, tracker.used());

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  ASSERT_NE(nullptr, context->get_malloc_allocator().alloc(257));
  ASSERT_GT(context->malloc_hold(), 0);
  ASSERT_GT(tracker.used(), 0);
  DESTROY_CONTEXT(context);
  ASSERT_EQ(0, tracker.used());
}

#if defined(OB_HAVE_BUNDLED_JEMALLOC)
TEST(TestAllocatorFacade, aligned_jemalloc)
{
  void *ptr = jemalloc_memalign(64, 100);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 64);
  ob_free(ptr);
}

#if defined(__linux__) && !defined(ENABLE_SANITY)
TEST(TestAllocatorFacade, restore_after_fork)
{
  bool enabled = false;
  size_t enabled_size = sizeof(enabled);
  ASSERT_EQ(0, je_mallctl("background_thread", &enabled, &enabled_size,
                          nullptr, 0));
  ASSERT_TRUE(enabled);

  const pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (0 == pid) {
    enabled = true;
    enabled_size = sizeof(enabled);
    if (0 != je_mallctl("background_thread", &enabled, &enabled_size,
                        nullptr, 0) || enabled) {
      _exit(1);
    } else if (!restore_allocator_after_fork()) {
      _exit(2);
    }
    enabled = false;
    enabled_size = sizeof(enabled);
    if (0 != je_mallctl("background_thread", &enabled, &enabled_size,
                        nullptr, 0) || !enabled) {
      _exit(3);
    }
    _exit(0);
  }

  int status = 0;
  ASSERT_EQ(pid, waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}
#endif
#endif
