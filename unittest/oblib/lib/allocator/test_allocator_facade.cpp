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
#include "lib/allocator/page_arena.h"
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

class ArenaLikeAllocator final : public ObIAllocator
{
public:
  ArenaLikeAllocator()
    : allocator_(), allocation_(nullptr), freed_(nullptr)
  {}

  ~ArenaLikeAllocator() override
  {
    allocator_.free(allocation_);
  }

  void *alloc(const int64_t size) override
  {
    return alloc(size, ObMemAttr());
  }

  void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    allocation_ = allocator_.alloc(size, attr);
    return allocation_;
  }

  void free(void *ptr) override
  {
    // Page arenas accept individual frees but reclaim the backing page only
    // during reset/destruction.  Preserve the pointer for an identity check.
    freed_ = ptr;
  }

  void *allocation() const { return allocation_; }
  void *freed() const { return freed_; }

private:
  ObMalloc allocator_;
  void *allocation_;
  void *freed_;
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

TEST(TestMallocBackend, allocate_reallocate_free)
{
  ObMemAttr attr;
  void *ptr = ob_malloc(100, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(ob_malloc_usable_size(ptr), 100);

  ptr = ob_realloc(ptr, 200, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(ob_malloc_usable_size(ptr), 200);
  EXPECT_EQ(nullptr, ob_realloc(ptr, 0, attr));
  EXPECT_EQ(nullptr, ob_malloc(0, attr));
  EXPECT_EQ(nullptr, ob_malloc(-1, attr));
}

TEST(TestMallocBackend, module_page_allocation_survives_resolver_transition)
{
  ObMemAttr attr("ResolverTrans", ObCtxIds::WORK_AREA);
  ModulePageAllocator allocator(attr);
  MemoryUsageTracker tracker;

  void *before_resolver = allocator.alloc(64);
  ASSERT_NE(nullptr, before_resolver);
  void *while_resolver = nullptr;
  {
    ContextMemoryTrackerGuard tracker_guard(tracker);
    while_resolver = allocator.alloc(128);
    ASSERT_NE(nullptr, while_resolver);
    ASSERT_GT(tracker.used(), 0);
    allocator.free(before_resolver);
    ASSERT_GT(tracker.used(), 0);
  }

  // The allocation remembers its accounting owner. Clearing the resolver
  // must not change how its header is decoded or where its charge is released.
  allocator.free(while_resolver);
  ASSERT_EQ(0, tracker.used());
}

TEST(TestMallocBackend, module_page_allocator_preserves_explicit_owner_layout)
{
  MemoryUsageTracker tracker;
  ContextMemoryTrackerGuard tracker_guard(tracker);
  ArenaLikeAllocator arena;
  ModulePageAllocator allocator(arena, "ArenaDelegate");
  allocator.set_ctx_id(ObCtxIds::WORK_AREA);

  void *ptr = allocator.alloc(128);
  ASSERT_NE(nullptr, ptr);
  EXPECT_EQ(arena.allocation(), ptr);
  EXPECT_EQ(0, tracker.used());

  allocator.free(ptr);
  EXPECT_EQ(ptr, arena.freed());
  EXPECT_EQ(0, tracker.used());
}

TEST(TestMallocBackend, untracked_adapter_preserves_backing_layout)
{
  ArenaLikeAllocator arena;
  ObMemAttr attr("UntrackedAlloc");
  TrackedAllocator allocator(arena, nullptr, nullptr, attr);

  void *ptr = allocator.alloc(128);
  ASSERT_NE(nullptr, ptr);
  EXPECT_EQ(arena.allocation(), ptr);
  EXPECT_EQ(ptr, allocator.raw_pointer(ptr));

  allocator.free(ptr);
  EXPECT_EQ(ptr, arena.freed());
}

TEST(TestMallocBackend, quota_realloc_preserves_original_on_rejection)
{
  CountingAllocator backing_allocator;
  MemoryUsageTracker tracker;
  MemoryQuota quota(4096);
  ObMemAttr attr("QuotaRealloc");
  TrackedAllocator allocator(backing_allocator, &tracker, &quota, attr);

  char *ptr = static_cast<char *>(allocator.alloc(64));
  ASSERT_NE(nullptr, ptr);
  MEMSET(ptr, 0x5a, 64);
  const int64_t old_committed = quota.committed();
  ASSERT_GT(old_committed, 64);
  quota.set_limit(old_committed);

  ASSERT_EQ(nullptr, allocator.realloc(ptr, 128, attr));
  ASSERT_EQ(old_committed, quota.committed());
  ASSERT_EQ(old_committed, tracker.used());
  for (int i = 0; i < 64; ++i) {
    ASSERT_EQ(static_cast<char>(0x5a), ptr[i]);
  }

  quota.set_limit(4096);
  ptr = static_cast<char *>(allocator.realloc(ptr, 128, attr));
  ASSERT_NE(nullptr, ptr);
  for (int i = 0; i < 64; ++i) {
    ASSERT_EQ(static_cast<char>(0x5a), ptr[i]);
  }
  allocator.free(ptr);
  ASSERT_EQ(0, quota.committed());
  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(0, tracker.used());
  ASSERT_EQ(0, backing_allocator.live_allocations());
}

TEST(TestMallocBackend, quota_realloc_admits_temporary_replacement_in_full)
{
  CountingAllocator backing_allocator;
  MemoryUsageTracker tracker;
  MemoryQuota quota(4096);
  ObMemAttr attr("QuotaRealPeak");
  TrackedAllocator allocator(backing_allocator, &tracker, &quota, attr);

  char *ptr = static_cast<char *>(allocator.alloc(64));
  ASSERT_NE(nullptr, ptr);
  MEMSET(ptr, 0x3c, 64);
  const int64_t old_committed = quota.committed();
  ASSERT_GT(old_committed, 64);

  // The replacement needs more than 127 backing bytes while the original is
  // still live.  A delta-only reservation would incorrectly admit it.
  quota.set_limit(old_committed + 127);
  ASSERT_EQ(nullptr, allocator.realloc(ptr, 128, attr));
  ASSERT_EQ(old_committed, quota.committed());
  for (int i = 0; i < 64; ++i) {
    ASSERT_EQ(static_cast<char>(0x3c), ptr[i]);
  }

  allocator.free(ptr);
  ASSERT_EQ(0, quota.committed());
  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(0, tracker.used());
}

TEST(TestMemoryContextJemalloc, destroy_reclaims_owned_allocations)
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

TEST(TestMemoryContextJemalloc, context_lifecycle_releases_tracked_pages)
{
  MemoryUsageTracker tracker;
  ContextMemoryTrackerGuard tracker_guard(tracker);
  ContextParam param;
  param.set_mem_attr("TrackCtxArena", ObCtxIds::WORK_AREA);
  MemoryContext context;
  ASSERT_EQ(OB_SUCCESS,
            MemoryContext::root()->CREATE_CONTEXT(context, param));
  const int64_t context_overhead = tracker.used();

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  ASSERT_GT(context->arena_hold(), 0);
  // The tracker charges the backend usable size, including the allocator's
  // owner header and size-class rounding; arena_hold() reports page payload.
  ASSERT_GE(tracker.used(), context_overhead + context->arena_hold());

  context->reuse();
  ASSERT_EQ(0, context->arena_used());
  ASSERT_GE(tracker.used(), context_overhead + context->arena_hold());

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  context->get_arena_allocator().reset();
  ASSERT_EQ(0, context->arena_hold());
  ASSERT_EQ(context_overhead, tracker.used());

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  const int64_t tracked_arena_bytes = tracker.used();
  ASSERT_NE(nullptr, context->get_malloc_allocator().alloc(257));
  ASSERT_GT(context->malloc_hold(), 0);
  ASSERT_GT(tracker.used(), tracked_arena_bytes);
  DESTROY_CONTEXT(context);
  ASSERT_EQ(0, tracker.used());
}

TEST(TestMemoryContextParentDestroy, recursively_reclaims_children_once)
{
  MemoryUsageTracker tracker;
  ContextMemoryTrackerGuard tracker_guard(tracker);
  ContextParam param;
  param.set_mem_attr("ParentDestroy", ObCtxIds::WORK_AREA);
  MemoryContext parent;
  MemoryContext child;
  MemoryContext grandchild;
  ASSERT_EQ(OB_SUCCESS, MemoryContext::root()->CREATE_CONTEXT(parent, param));
  ASSERT_EQ(OB_SUCCESS, parent->CREATE_CONTEXT(child, param));
  ASSERT_EQ(OB_SUCCESS, child->CREATE_CONTEXT(grandchild, param));

  ASSERT_NE(nullptr, parent->get_arena_allocator().alloc(4097));
  ASSERT_NE(nullptr, child->get_malloc_allocator().alloc(257));
  ASSERT_NE(nullptr, grandchild->get_arena_allocator().alloc(8193));
  ASSERT_GT(tracker.used(), 0);

  DESTROY_CONTEXT(parent);
  parent = nullptr;
  child = nullptr;
  grandchild = nullptr;
  ASSERT_EQ(0, tracker.used());
}

TEST(TestMemoryContextParentDestroy, explicitly_destroyed_child_is_unlinked)
{
  MemoryUsageTracker tracker;
  ContextMemoryTrackerGuard tracker_guard(tracker);
  ContextParam param;
  param.set_mem_attr("PartialDestroy", ObCtxIds::WORK_AREA);
  MemoryContext parent;
  MemoryContext child;
  ASSERT_EQ(OB_SUCCESS, MemoryContext::root()->CREATE_CONTEXT(parent, param));
  ASSERT_EQ(OB_SUCCESS, parent->CREATE_CONTEXT(child, param));
  ASSERT_NE(nullptr, child->get_malloc_allocator().alloc(257));

  DESTROY_CONTEXT(child);
  child = nullptr;
  ASSERT_EQ(0, tracker.used());
  DESTROY_CONTEXT(parent);
  parent = nullptr;
  ASSERT_EQ(0, tracker.used());
}

TEST(TestMemoryContextParentDestroy, repeated_subtree_lifecycle)
{
  MemoryUsageTracker tracker;
  ContextMemoryTrackerGuard tracker_guard(tracker);
  ContextParam param;
  param.set_mem_attr("RepeatedSubtree", ObCtxIds::WORK_AREA);
  for (int i = 0; i < 1000; ++i) {
    MemoryContext parent;
    MemoryContext child;
    MemoryContext grandchild;
    ASSERT_EQ(OB_SUCCESS, MemoryContext::root()->CREATE_CONTEXT(parent, param));
    ASSERT_EQ(OB_SUCCESS, parent->CREATE_CONTEXT(child, param));
    ASSERT_EQ(OB_SUCCESS, child->CREATE_CONTEXT(grandchild, param));
    ASSERT_NE(nullptr, parent->get_malloc_allocator().alloc(17 + i % 31));
    ASSERT_NE(nullptr, child->get_arena_allocator().alloc(257 + i % 63));
    ASSERT_NE(nullptr, grandchild->get_malloc_allocator().alloc(65 + i % 17));
    DESTROY_CONTEXT(parent);
    parent = nullptr;
    child = nullptr;
    grandchild = nullptr;
    ASSERT_EQ(0, tracker.used());
  }
}

#if defined(OB_HAVE_BUNDLED_JEMALLOC)
TEST(TestMallocBackend, aligned_jemalloc)
{
  void *ptr = jemalloc_memalign(64, 100);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % 64);
  ob_free(ptr);
}

TEST(TestMallocBackend, process_stats_are_readable)
{
  AllocatorProcessStats stats;
  ASSERT_TRUE(get_allocator_process_stats(stats));
  ASSERT_TRUE(stats.allocator_stats_available_);
  ASSERT_GT(stats.allocated_, 0U);
  ASSERT_GE(stats.active_, stats.allocated_);
  ASSERT_GE(stats.resident_, stats.active_);
  ASSERT_GE(stats.mapped_, stats.active_);
#if defined(__linux__)
  ASSERT_GT(stats.virtual_memory_, 0);
  ASSERT_GT(stats.process_resident_, 0);
#endif
}

#if defined(__linux__) && !defined(ENABLE_SANITY)
TEST(TestMallocBackend, restore_after_fork)
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
