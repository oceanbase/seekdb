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

#include <gtest/gtest.h>
#define private public
#include "lib/alloc/ob_ctx_allocator.h"
#undef private
#include "lib/coro/testing.h"
#include "lib/container/ob_array.h"

using namespace std;
using namespace oceanbase::lib;
using namespace oceanbase::common;

struct TestCtxAllocator
{
  TestCtxAllocator(int64_t ctx_id = 0)
    : ctx_allocator_(ctx_id, &allocator_),
      allocator_(ctx_allocator_, ctx_id)
  {
    ctx_allocator_.set_memory_mgr();
    ctx_allocator_.set_limit(INT64_MAX);
  }
  int set_memory_mgr() {return ctx_allocator_.set_memory_mgr(); }
  int set_limit(int64_t size) { return ctx_allocator_.set_limit(size); }

  ObCtxAllocatorState ctx_allocator_;
  ObCtxAllocator allocator_;
};

static void set_allocator_hard_limit(const int64_t bytes)
{
  ObMallocAllocator::get_instance()->set_allocator_hard_limit(bytes);
  CHUNK_MGR.set_hard_limit(bytes);
}

TEST(TestAllocator, CtxAlloc)
{
  CHUNK_MGR.set_max_chunk_cache_size(1<<20);
  TestCtxAllocator test_allocator(1);
  ObCtxAllocator &ta = test_allocator.allocator_;
  ObMemAttr attr("CtxAlloc", 1);
  const int64_t hold = CHUNK_MGR.get_hold();

  cout << "current hold: " << hold << endl;

  EXPECT_TRUE(NULL != ta.alloc(1, attr));

  ta.print_memory_usage();
}

TEST(TestAllocator, SysLimit)
{
  TestCtxAllocator test_allocator{};
  ObCtxAllocator &ta = test_allocator.allocator_;
  ObMemAttr attr("CtxAlloc");
  // Baseline on the allocator sum_hold_ (the counter the limit checks), not the
  // global CHUNK_MGR hold. A fresh alloc maps two chunks (metadata + data).
  int64_t hold = test_allocator.ctx_allocator_.get_total_hold();

  cout << "current hold: " << hold << endl;

  set_allocator_hard_limit(hold);
  EXPECT_EQ(NULL, ta.alloc(1, attr));

  hold = test_allocator.ctx_allocator_.get_total_hold();
  set_allocator_hard_limit(hold + (1<<20));
  EXPECT_EQ(NULL, ta.alloc(1, attr));

  hold = test_allocator.ctx_allocator_.get_total_hold();
  set_allocator_hard_limit(hold + 8L * INTACT_ACHUNK_SIZE);
  EXPECT_TRUE(NULL != ta.alloc(1, attr));

  set_allocator_hard_limit(INT64_MAX);
}

TEST(TestAllocator, TotalLimit)
{
  TestCtxAllocator test_allocator{};
  ObCtxAllocator &ta = test_allocator.allocator_;
  ObMemAttr attr("CtxAlloc");
  const int64_t hold = test_allocator.ctx_allocator_.get_total_hold();

  cout << "current hold: " << hold << endl;

  // One chunk of headroom: it fits the NORMAL-priority metadata chunk but not the
  // data chunk, so NORMAL is rejected while OB_HIGH_ALLOC bypasses the hard limit.
  // Keep CHUNK_MGR open so only the allocator limit is under test.
  set_allocator_hard_limit(hold + INTACT_ACHUNK_SIZE);
  CHUNK_MGR.set_limit(INT64_MAX);
  CHUNK_MGR.set_hard_limit(INT64_MAX);
  EXPECT_FALSE(NULL != ta.alloc(1, attr));

  attr.prio_ = OB_HIGH_ALLOC;
  EXPECT_TRUE(NULL != ta.alloc(1, attr));
  EXPECT_TRUE(NULL != ta.alloc(1, attr));

  attr.prio_ = OB_NORMAL_ALLOC;
  EXPECT_FALSE(NULL != ta.alloc(2 << 20, attr));

  attr.prio_ = OB_HIGH_ALLOC;
  EXPECT_TRUE(NULL != ta.alloc(1, attr));

  set_allocator_hard_limit(INT64_MAX);
}

TEST(TestAllocator, SetMemoryMgrTwice)
{
  TestCtxAllocator test_allocator{};
  ASSERT_EQ(OB_INIT_TWICE, test_allocator.set_memory_mgr());
}

TEST(TestAllocator, ctx_limit)
{
  CHUNK_MGR.set_limit(1L * 1024L * 1024L * 1024L);
  const uint64_t ctx_id = 1;
  TestCtxAllocator test_allocator(ctx_id);
  ObCtxAllocator &ctx_ta = test_allocator.allocator_;

  ObMemAttr attr;
  attr.ctx_id_ = ctx_id;
  const int64_t size = 512 * 1024;
  const int64_t limit = 30 * size;
  int64_t alloced = 0;
  ctx_ta.set_limit(limit);
  ctx_ta.set_hard_limit(limit);
  while (true) {
    if (NULL == ctx_ta.alloc(size, attr)) {
      break;
    } else {
      alloced += size;
    }
  }
  cout << "alloced: " << alloced << endl;
  cout << "limit: " << limit << endl;
  cout << "hold: " << ctx_ta.get_hold() << endl;
  ASSERT_TRUE(alloced > limit / 2 && alloced <= limit);
  ASSERT_TRUE(ctx_ta.get_hold() > limit / 2 && ctx_ta.get_hold() <= limit);
}

TEST(TestAllocator, reserve)
{
  CHUNK_MGR.set_limit(1L * 1024L * 1024L * 1024L);
  const uint64_t ctx_id = 1;
  TestCtxAllocator test_allocator(ctx_id);
  const int64_t limit = 1L * 1024L * 1024L * 1024L;
  test_allocator.set_limit(limit);
  ObCtxAllocator &ta = test_allocator.allocator_;

  ASSERT_EQ(nullptr, ta.head_chunk_.next_);
  const int64_t reserve_size = 2 * INTACT_ACHUNK_SIZE;
  int ret = ta.set_idle(reserve_size, true);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(ta.chunk_cnt_, reserve_size / INTACT_ACHUNK_SIZE);
  int64_t chunk_cnt = 0;
  AChunk *chunk = &ta.head_chunk_;
  while (chunk->next_ != nullptr) {
    chunk_cnt++;
    chunk = chunk->next_;
  }
  ASSERT_EQ(ta.chunk_cnt_, chunk_cnt);
  ObMemAttr attr("CtxAlloc",  ctx_id);
  void *ptr = ta.alloc(1, attr);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(ta.chunk_cnt_, chunk_cnt - 1);
  int64_t total_alloc_size = 0;
  int64_t alloc_size = 512;
  while (true) {
    void *ptr = ta.alloc(alloc_size, attr);
    ASSERT_NE(nullptr, ptr);
    total_alloc_size += alloc_size;
    if (total_alloc_size > reserve_size) {
      ASSERT_EQ(0, ta.chunk_cnt_);
      break;
    }
  }
  chunk_cnt = ta.chunk_cnt_;
  ret = ta.set_idle(reserve_size * 2, true);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_NE(chunk_cnt, ta.chunk_cnt_);

  chunk_cnt = ta.chunk_cnt_;
  ret = ta.set_idle(reserve_size * 4);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(chunk_cnt, ta.chunk_cnt_);

  ret = ta.set_idle(reserve_size);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_GT(chunk_cnt, ta.chunk_cnt_);
}

TEST(TestAllocator, set_idle)
{
  CHUNK_MGR.set_limit(1L * 1024L * 1024L * 1024L);
  const uint64_t ctx_id = 2;
  TestCtxAllocator test_allocator(ctx_id);
  const int64_t limit = 1L * 1024L * 1024L * 1024L;
  test_allocator.set_limit(limit);
  ObCtxAllocator &ta = test_allocator.allocator_;
  ObMemAttr attr("CtxAlloc", ctx_id);
  void *ptr = ta.alloc(INTACT_ACHUNK_SIZE / 2, attr);
  ASSERT_NE(nullptr, ptr);
  int64_t hold = ta.get_hold();
  ta.free(ptr);
  ASSERT_EQ(0, ta.chunk_cnt_);
  ASSERT_LT(ta.get_hold(), hold);

  const int64_t alloc_cnt = 10;
  void *ptrs[alloc_cnt];
  for (int i = 0; i < alloc_cnt; ++i) {
    ptr = ta.alloc(INTACT_ACHUNK_SIZE / 2, attr);
    ASSERT_NE(nullptr, ptr);
    ptrs[i] = ptr;
  }
  hold = ta.get_hold();
  const int64_t idle_size = alloc_cnt * INTACT_ACHUNK_SIZE;
  int ret = ta.set_idle(idle_size);
  ASSERT_EQ(OB_SUCCESS, ret);
  for (int i = 0; i < alloc_cnt; ++i) {
    ta.free(ptrs[i]);
  }
  ASSERT_EQ(10, ta.chunk_cnt_);
  ASSERT_EQ(ta.get_hold(), hold);
  ret = ta.set_idle(0);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, ta.chunk_cnt_);
  ASSERT_LT(ta.get_hold(), hold);
}

TEST(TestAllocator, idle)
{
  CHUNK_MGR.set_limit(1L * 1024L * 1024L * 1024L);
  const uint64_t ctx_id = 3;
  TestCtxAllocator test_allocator(ctx_id);
  const int64_t limit = 1L * 1024L * 1024L * 1024L;
  test_allocator.set_limit(limit);
  ObCtxAllocator &ta = test_allocator.allocator_;

  // > limit
  ASSERT_EQ(OB_INVALID_ARGUMENT, ta.set_idle(limit + INTACT_ACHUNK_SIZE));
  // %2M != 0
  // ASSERT_EQ(OB_INVALID_ARGUMENT, ta.set_idle(INTACT_ACHUNK_SIZE + 1));
  const int64_t init_size = 10 * INTACT_ACHUNK_SIZE;
  const int64_t idle_size = 50 * INTACT_ACHUNK_SIZE;
  int ret = ta.set_idle(init_size, true);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = ta.set_idle(idle_size);
  ASSERT_EQ(OB_SUCCESS, ret);

  int64_t chunk_cnt = init_size/INTACT_ACHUNK_SIZE;
  ASSERT_EQ(ta.chunk_cnt_, chunk_cnt);
  int64_t hold = ta.get_hold();
  const int64_t alloc_size = 1024 * 100;
  ObMemAttr attr("CtxAlloc", ctx_id);
  int k  = 0;
  ObArray<void *> ptrs;
  while (hold == ta.get_hold()) {
    k++;
    void *ptr = ta.alloc(alloc_size, attr);
    ASSERT_NE(nullptr, ptr);
    ptrs.push_back(ptr);
  }
  ASSERT_GT(k, init_size / alloc_size / 2);
  ASSERT_LT(hold, ta.get_hold());
  ASSERT_EQ(0, ta.chunk_cnt_);

  while (ta.get_hold() < idle_size * 2) {
    void * ptr = ta.alloc(alloc_size, attr);
    ASSERT_NE(nullptr, ptr);
    ptrs.push_back(ptr);
  }

  ASSERT_EQ(0, ta.chunk_cnt_);
  int64_t last_chunk_cnt = ta.chunk_cnt_;
  int64_t i = 0;
  for (; i < ptrs.size() && ta.chunk_cnt_ == last_chunk_cnt; i++) {
    last_chunk_cnt = ta.chunk_cnt_;
    ta.free(ptrs[i]);
  }
  ASSERT_TRUE(i != 0 && i != ptrs.size());
  hold = ta.get_hold();
  for (; i < ptrs.size(); i++) {
    ta.free(ptrs[i]);
    ASSERT_EQ(ta.get_hold(), hold);
  }
  chunk_cnt = idle_size/INTACT_ACHUNK_SIZE;
  ASSERT_EQ(ta.chunk_cnt_, chunk_cnt);

  ret = ta.set_idle(0);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, ta.get_hold());
}

TEST(TestAllocator, chunk_free_list_push_pop_concurrency)
{
  CHUNK_MGR.set_limit(1L * 1024L * 1024L * 1024L);
  const uint64_t ctx_id = 1;
  TestCtxAllocator test_allocator(ctx_id);
  const int64_t limit = 1L * 1024L * 1024L * 1024L;
  test_allocator.set_limit(limit);
  ObCtxAllocator &ta = test_allocator.allocator_;

  ASSERT_EQ(nullptr, ta.head_chunk_.next_);

  int chunk_num = 0;
  int th_num = 32;
  cotesting::FlexPool([&ta, &chunk_num]() {
    int64_t start_time = ObTimeUtility::current_time();
    while (ObTimeUtility::current_time() < start_time + 10 * 1000 * 1000)
    {
      AChunk *chunk = nullptr;
      if (0 == ObRandom::rand(0, 1)) {
        chunk = ta.pop_chunk();
      } else {
        chunk = new AChunk();
        ATOMIC_INC(&chunk_num);
      }
      if (chunk != nullptr) {
        usleep(1000);
        ta.push_chunk(chunk);
      }
    }
  }, th_num).start();
  AChunk *chunk = nullptr;
  while ((chunk = ta.pop_chunk()) != nullptr)
  {
    delete chunk;
    chunk_num--;
  }
  ASSERT_EQ(0, chunk_num);
}

int main(int argc, char *argv[])
{
  signal(49, SIG_IGN);
  OB_LOGGER.set_disable_logging(true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
