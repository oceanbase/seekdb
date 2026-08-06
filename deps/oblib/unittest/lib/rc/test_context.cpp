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

#define USING_LOG_PREFIX SHARE
#include <gtest/gtest.h>
#define private public
#define protected public
#include "lib/rc/context.h"
#undef private
#undef protected
#include "lib/alloc/memory_dump.h"
#include <csignal>
#include <thread>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;

static bool has_unfree = false;
void has_unfree_callback(char *)
{
  has_unfree = true;
}

static MemoryUsageTracker *test_memory_usage_tracker = nullptr;
MemoryUsageTracker *resolve_test_memory_usage_tracker(const int64_t ctx_id)
{
  return ObCtxIds::WORK_AREA == ctx_id ? test_memory_usage_tracker : nullptr;
}

class TestMemoryUsageTrackerResolverGuard
{
public:
  explicit TestMemoryUsageTrackerResolverGuard(MemoryUsageTracker &tracker)
  {
    test_memory_usage_tracker = &tracker;
    set_memory_usage_tracker_resolver(
        ObCtxIds::WORK_AREA, resolve_test_memory_usage_tracker);
  }
  ~TestMemoryUsageTrackerResolverGuard()
  {
    set_memory_usage_tracker_resolver(ObCtxIds::WORK_AREA, nullptr);
    test_memory_usage_tracker = nullptr;
  }
};

class TestContext: public ::testing::Test
{
public:
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestContext, Basic)
{
  const bool obmalloc_backend = is_ob_malloc_backend();
  // There must be a Flow pointing to root on each thread
  auto &context = Flow::current_ctx();
  auto &flow = Flow::current_flow();
  ASSERT_EQ(MemoryContext::root(), context);
  ASSERT_TRUE(flow.prev_ == flow.next_ && flow.prev_ == nullptr);
  ASSERT_TRUE(context->tree_node_.parent_ == context->tree_node_.child_ &&
              context->tree_node_.parent_ == context->tree_node_.next_ &&
              context->tree_node_.parent_ == nullptr);
  uint64_t ctx_id = ObCtxIds::WORK_AREA;
  ObPageManager g_pm;
  ObPageManager::set_thread_local_instance(g_pm);
  g_pm.set_ctx(ctx_id);
  MemoryContext &root = MemoryContext::root();
  ContextParam param;
  param.set_mem_attr("Context", ctx_id);
  ContextTLOptGuard guard(true);
  param.properties_ = USE_TL_PAGE_OPTIONAL;
  MemoryContext mem_context;
  int ret = root->CREATE_CONTEXT(mem_context, param);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(&mem_context->get_allocator(), &mem_context->get_arena_allocator());
  int64_t used = g_pm.used_;
  ASSERT_EQ(0, used);
  void *ptr = nullptr;
  ObMemAttr attr(ObNewModIds::TEST);
  WITH_CONTEXT(mem_context) {
    ptr = ctxalp(100);
    ASSERT_NE(ptr, nullptr);
    MEMSET(ptr, 0, 100);
    if (obmalloc_backend) {
      ASSERT_GT(g_pm.used_, used);
    } else {
      ASSERT_GT(mem_context->arena_hold(), 0);
    }

    auto &P_MCTX = CURRENT_CONTEXT;
    auto &P_MFLOW = Flow::current_flow();
    CREATE_WITH_TEMP_CONTEXT_P(false, param) {
      ASSERT_EQ(&CURRENT_CONTEXT, &P_MCTX);
    } else {
      ASSERT_TRUE(false);
    }
    CREATE_WITH_TEMP_CONTEXT_P(true, param) {
      ASSERT_NE(&CURRENT_CONTEXT, &P_MCTX);
      ASSERT_EQ(Flow::current_flow().prev_, &P_MFLOW);
      ASSERT_EQ(Flow::current_flow().next_, nullptr);
      int64_t p_hold = P_MCTX->hold();
      int64_t hold = CURRENT_CONTEXT->hold();
      for (int i = 0; i < 64; ++i) {
        ptr = ctxalp(100);
        ASSERT_NE(ptr, nullptr);
      }
      if (obmalloc_backend) {
        ASSERT_GT(g_pm.used_, used);
      }
      ASSERT_EQ(p_hold, P_MCTX->hold());
      ASSERT_LT(hold, CURRENT_CONTEXT->hold());
      int64_t orig_pm_used = g_pm.used_;
      int64_t parent_hold = CURRENT_CONTEXT->hold();
      has_unfree = false;
      CREATE_WITH_TEMP_CONTEXT(param) {
        for (int i = 0; i < 64; ++i) {
          ptr = ctxalp(1024);
          ASSERT_NE(ptr, nullptr);
          ptr = ctxalf(100, attr);
          ASSERT_NE(ptr, nullptr);
          ObArenaAllocator &arena_alloc = CURRENT_CONTEXT->get_arena_allocator();
          ptr = arena_alloc.alloc(1024);
          ASSERT_NE(ptr, nullptr);
          ObIAllocator &alloc = CURRENT_CONTEXT->get_malloc_allocator();
          int64_t ori_used = CURRENT_CONTEXT->used();
          ptr = alloc.alloc(1024);
          ASSERT_NE(ptr, nullptr);
          ASSERT_GT(CURRENT_CONTEXT->used(), ori_used);
          alloc.free(ptr);
          ASSERT_EQ(CURRENT_CONTEXT->used(), ori_used);
        }
      } else {
        ASSERT_TRUE(false);
      }
      ASSERT_EQ(obmalloc_backend, has_unfree);
      if (obmalloc_backend) {
        ASSERT_EQ(orig_pm_used, g_pm.used_);
      } else {
        ASSERT_EQ(parent_hold, CURRENT_CONTEXT->hold());
      }
      CREATE_WITH_TEMP_CONTEXT(param) {
        {
          // In order to allow the object_set inside current_ctx to allocate free_list in advance
          // Don't let the memory occupied by free_list affect subsequent verification
          ctxalf(8192 - 1000, attr);
          ptr = ctxalf(2000, attr);
          ASSERT_NE(ptr, nullptr);
          ctxfree(ptr);
        }
        int64_t subtree_hold_before_subs = CURRENT_CONTEXT->tree_mem_hold();
        int sub_cnt = 8;
        MemoryContext subs[sub_cnt];
        for (int i = 0; i < sub_cnt; ++i) {
          param.properties_ = USE_TL_PAGE_OPTIONAL |
            RETURN_MALLOC_DEFAULT;
          ret = CURRENT_CONTEXT->CREATE_CONTEXT(subs[i], param);
          ASSERT_EQ(OB_SUCCESS, ret);
          ASSERT_EQ(&subs[i]->get_allocator(), &subs[i]->get_malloc_allocator());
          ptr = subs[i]->allocp(100);
          ASSERT_NE(ptr, nullptr);
          ptr = subs[i]->get_arena_allocator().alloc(100);
          ASSERT_NE(ptr, nullptr);
          WITH_CONTEXT(subs[i]) {
            ASSERT_EQ(subs[i], CURRENT_CONTEXT);
            ptr = ctxalp(100);
            ASSERT_NE(ptr, nullptr);
            ptr = CURRENT_CONTEXT->get_arena_allocator().alloc(100);
            ASSERT_NE(ptr, nullptr);
          } else {
            ASSERT_TRUE(false);
          }
        }
        const int64_t subtree_hold_with_all_subs = CURRENT_CONTEXT->tree_mem_hold();
        if (obmalloc_backend) {
          ASSERT_GT(g_pm.used_, orig_pm_used);
        } else {
          ASSERT_GT(subtree_hold_with_all_subs, subtree_hold_before_subs);
        }
        for (int i = 0; i < sub_cnt/2; ++i) {
          DESTROY_CONTEXT(subs[i]);
        }
        if (obmalloc_backend) {
          ASSERT_GT(g_pm.used_, orig_pm_used);
        } else {
          const int64_t subtree_hold_with_half_subs = CURRENT_CONTEXT->tree_mem_hold();
          ASSERT_GT(subtree_hold_with_half_subs, subtree_hold_before_subs);
          ASSERT_LT(subtree_hold_with_half_subs, subtree_hold_with_all_subs);
        }
        // check child num
        int child_cnt = 0;
        for (auto cur = CURRENT_CONTEXT->tree_node_.child_;cur;cur=cur->next_,child_cnt++);
        ASSERT_EQ(child_cnt, sub_cnt/2);
      } else {
        ASSERT_TRUE(false);
      }
      if (obmalloc_backend) {
        ASSERT_EQ(g_pm.used_, orig_pm_used);
      } else {
        ASSERT_EQ(parent_hold, CURRENT_CONTEXT->hold());
      }
    } else {
      ASSERT_TRUE(false);
    }
  } else {
    ASSERT_TRUE(false);
  }
  if (obmalloc_backend) {
    ASSERT_GT(g_pm.used_, used);
  } else {
    ASSERT_GT(mem_context->arena_hold(), 0);
  }
  DESTROY_CONTEXT(mem_context);
  ASSERT_EQ(g_pm.used_, used);

  // Based on testing needs, this code is temporarily retained
  ob_malloc(10000000, ObNewModIds::OB_COMMON_ARRAY);
  ObMemoryDump::get_instance().init();
  ObMemoryDumpTask task;
  task.type_ = DUMP_CHUNK;
  task.dump_all_ = true;
  ObMemoryDump::get_instance().request_dump(task);

  task.type_ = STAT_LABEL;
  ObMemoryDump::get_instance().request_dump(task);
  usleep(1000000);
  ObMallocAllocator::get_instance()->get_ctx_allocator(ObCtxIds::DEFAULT_CTX_ID)->print_memory_usage();
}

TEST_F(TestContext, FreeableAccounting)
{
  ContextParam param;
  ObMemAttr attr("FreeableAcct", ObCtxIds::DEFAULT_CTX_ID);
  param.set_mem_attr(attr).set_properties(RETURN_MALLOC_DEFAULT);
  MemoryContext context;
  ASSERT_EQ(OB_SUCCESS, MemoryContext::root()->CREATE_CONTEXT(context, param));
  ASSERT_GT(MemoryContext::metadata_size(), 0);

  ObIAllocator &allocator = context->get_malloc_allocator();
  const int64_t initial_used = context->used();
  const int64_t initial_hold = context->hold();
  void *ptr = allocator.alloc(17, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GT(context->used(), initial_used);
  if (!is_ob_malloc_backend()) {
    ASSERT_EQ(context->hold(), context->used());
  }

  const int64_t small_used = context->used();
  void *new_ptr = allocator.realloc(ptr, 4097, attr);
  if (is_ob_malloc_backend()) {
    ASSERT_EQ(nullptr, new_ptr);
    ASSERT_EQ(small_used, context->used());
  } else {
    ASSERT_NE(nullptr, new_ptr);
    ptr = new_ptr;
    ASSERT_GT(context->used(), small_used);

    const int64_t used_before_failure = context->used();
    ASSERT_EQ(nullptr, allocator.realloc(ptr, INT64_MAX, attr));
    ASSERT_EQ(used_before_failure, context->used());
    ASSERT_EQ(context->hold(), context->used());
  }

  const int64_t freeable_used = context->used();
  void *arena_ptr = context->get_arena_allocator().alloc(4097);
  ASSERT_NE(nullptr, arena_ptr);
  ASSERT_GT(context->used(), freeable_used);
  context->reuse();
  ASSERT_EQ(freeable_used, context->used());
  const int64_t reused_arena_hold = context->arena_hold();
  ASSERT_GT(reused_arena_hold, 0);

  allocator.free(ptr);
  ASSERT_EQ(initial_used, context->used());
  if (!is_ob_malloc_backend()) {
    ASSERT_EQ(initial_hold + reused_arena_hold, context->hold());
  }

  DESTROY_CONTEXT(context);

  param.set_properties(ALLOC_THREAD_SAFE | RETURN_MALLOC_DEFAULT);
  MemoryContext thread_context;
  ASSERT_EQ(OB_SUCCESS,
            MemoryContext::root()->CREATE_CONTEXT(thread_context, param));
  ObIAllocator &thread_allocator = thread_context->get_malloc_allocator();
  const int64_t thread_initial_used = thread_context->used();
  void *thread_ptr = thread_allocator.alloc(1024, attr);
  ASSERT_NE(nullptr, thread_ptr);
  ASSERT_GT(thread_context->used(), thread_initial_used);
  std::thread free_thread(
      [&thread_allocator, thread_ptr]() { thread_allocator.free(thread_ptr); });
  free_thread.join();
  if (!is_ob_malloc_backend()) {
    ASSERT_EQ(thread_initial_used, thread_context->used());
  }
  DESTROY_CONTEXT(thread_context);
}

TEST_F(TestContext, TrackedAllocatorAccounting)
{
  ObMemAttr attr("TrackedAlloc", ObCtxIds::DEFAULT_CTX_ID);
  ObMalloc base_allocator(attr);
  MemoryUsageTracker tracker;
  TrackedAllocator allocator(base_allocator, &tracker, attr);

  void *ptr = allocator.alloc(17);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(17, tracker.used());

  void *new_ptr = allocator.realloc(ptr, 4097, attr);
  ASSERT_NE(nullptr, new_ptr);
  ptr = new_ptr;
  ASSERT_EQ(4097, tracker.used());

  ASSERT_EQ(nullptr, allocator.realloc(ptr, INT64_MAX, attr));
  ASSERT_EQ(4097, tracker.used());

  std::thread free_thread([&allocator, ptr]() { allocator.free(ptr); });
  free_thread.join();
  ASSERT_EQ(0, tracker.used());
}

TEST_F(TestContext, MemoryContextArenaPageIsTrackedOnce)
{
  MemoryUsageTracker tracker;
  TestMemoryUsageTrackerResolverGuard resolver_guard(tracker);
  ContextParam param;
  param.set_mem_attr("TrackCtxArena", ObCtxIds::WORK_AREA)
      .set_properties(USE_TL_PAGE_OPTIONAL);
  MemoryContext context;
  ASSERT_EQ(OB_SUCCESS, MemoryContext::root()->CREATE_CONTEXT(context, param));

  ASSERT_NE(nullptr, context->get_arena_allocator().alloc(4097));
  ASSERT_GT(context->arena_hold(), 0);
  ASSERT_EQ(context->arena_hold(), tracker.used());

  context->get_arena_allocator().reset();
  ASSERT_EQ(0, tracker.used());
  DESTROY_CONTEXT(context);
}

TEST_F(TestContext, ModulePageAllocatorCopyTracksResolvedContext)
{
  MemoryUsageTracker tracker;
  TestMemoryUsageTrackerResolverGuard resolver_guard(tracker);
  ObMemAttr attr("TrackedPage", ObCtxIds::WORK_AREA);
  ModulePageAllocator *copied = nullptr;
  ModulePageAllocator assigned(attr);
  {
    ModulePageAllocator source(attr);
    copied = new ModulePageAllocator(source);
    assigned = source;
  }
  ASSERT_NE(nullptr, copied);

  void *copied_ptr = copied->alloc(17);
  ASSERT_NE(nullptr, copied_ptr);
  ASSERT_EQ(17, tracker.used());
  copied->free(copied_ptr);
  ASSERT_EQ(0, tracker.used());

  void *assigned_ptr = assigned.alloc(31);
  ASSERT_NE(nullptr, assigned_ptr);
  ASSERT_EQ(31, tracker.used());
  assigned.free(assigned_ptr);
  ASSERT_EQ(0, tracker.used());
  delete copied;
}

TEST_F(TestContext, ArenaAllocatorExplicitTracker)
{
  MemoryUsageTracker tracker;
  ObArenaAllocator allocator("TrackedArena");
  allocator.set_memory_tracker(&tracker);

  ASSERT_NE(nullptr, allocator.alloc(4097));
  ASSERT_EQ(allocator.total(), tracker.used());

  allocator.reset_remain_one_page();
  ASSERT_EQ(allocator.total(), tracker.used());
  allocator.reset();
  ASSERT_EQ(0, tracker.used());
}

bool req_cache_empty(ObCtxAllocator *ta)
{
  for (int i = 0; i < ta->req_chunk_mgr_.parallel_; i++) {
    if (ta->req_chunk_mgr_.chunks_[i]) {
      return false;
    }
  }
  return true;
}

TEST_F(TestContext, PM_Wash)
{
  if (!is_ob_malloc_backend()) {
    return;
  }
  uint64_t ctx_id = ObCtxIds::DEFAULT_CTX_ID;
  auto ta = ObMallocAllocator::get_instance()->get_ctx_allocator(ctx_id);
  ObMemAttr attr("test", ctx_id);
  ObPageManager g_pm;
  ObPageManager::set_thread_local_instance(g_pm);
  g_pm.set_ctx(ctx_id);
  ContextTLOptGuard guard(true);
  ContextParam param;
  param.set_mem_attr(attr);
  param.properties_ = USE_TL_PAGE_OPTIONAL;
  ASSERT_TRUE(req_cache_empty(ta.ref_allocator()));
  int ret = OB_SUCCESS;
  CREATE_WITH_TEMP_CONTEXT(param) {
    void *ptr = ctxalf(100, attr);
    ASSERT_NE(nullptr, ptr);
    ctxfree(ptr);
    ASSERT_FALSE(req_cache_empty(ta.ref_allocator()));
    ta->set_limit(ta->get_hold());
    ASSERT_NE(ob_malloc(OB_MALLOC_BIG_BLOCK_SIZE, attr), nullptr);
  }
}

void emptySignalHandler(int) {}
int main(int argc, char **argv)
{
  std::signal(49, emptySignalHandler);
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
