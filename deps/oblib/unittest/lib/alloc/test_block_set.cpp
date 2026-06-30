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

#include "lib/allocator/ob_malloc.h"

using namespace std;
using namespace oceanbase::lib;
using namespace oceanbase::common;

ObMemAttr attr;

static const uint32_t INTACT_BIG_ABLOCK_SIZE = ACHUNK_SIZE;
static const uint32_t BIG_ABLOCK_SIZE = INTACT_BIG_ABLOCK_SIZE - ABLOCK_HEADER_SIZE;
static const uint32_t INTACT_BIG_AOBJECT_SIZE = BIG_ABLOCK_SIZE;
static const uint32_t BIG_AOBJECT_SIZE = INTACT_BIG_AOBJECT_SIZE - AOBJECT_META_SIZE;

class TestBlockSet
    : public ::testing::Test
{
public:
  TestBlockSet()
      : tallocator_()
  {}
  virtual void SetUp()
  {
    tallocator_ = ObMallocAllocator::get_instance()->get_tenant_ctx_allocator(0);
    tallocator_->set_limit(1000L << 20);
    cs_.set_chunk_mgr(&tallocator_->get_chunk_mgr());
    cs_.set_tenant_ctx_allocator(*tallocator_.ref_allocator());
  }

  virtual void TearDown()
  {
  }

  ABlock *Malloc(uint64_t size)
  {
    ABlock *block = cs_.alloc_block(size, attr);
    return block;
  }

  void Free(ABlock *block)
  {
    cs_.free_block(block);
  }

  void check_ptr(void *p)
  {
    UNUSED(p);
    ASSERT_TRUE(p != NULL);
    // ASSERT_EQ(56, (uint64_t)p & 0xFF) << ((uint64_t)p & 0xFF);
  }

protected:
  ObTenantCtxAllocatorGuard tallocator_;
  BlockSet cs_;
};

TEST_F(TestBlockSet, ManyMalloc)
{
  ABlock *p = NULL;
  int64_t cnt = 1L << 10;
  uint64_t sz = 32;

  while (cnt--) {
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    sz = ((sz | reinterpret_cast<size_t>(p)) & ((1<<18) - 1));
  }
}

TEST_F(TestBlockSet, AllocLarge)
{
  uint64_t sz = 1L << 18;
  int64_t cnt = 1L << 10;
  ABlock *p = NULL;

  while (cnt--) {
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
    sz = ((sz | reinterpret_cast<size_t>(p)) & ((1<<25) - 1));
  }
}

TEST_F(TestBlockSet, NormalBlock)
{
  const uint64_t sz = INTACT_BIG_AOBJECT_SIZE;
  int64_t cnt = 1L << 10;
  ABlock *p = NULL;

  while (cnt--) {
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
  }
}

TEST_F(TestBlockSet, BigBlock)
{
  const uint64_t sz = 1L << 20;
  int64_t cnt = 1L << 20;
  ABlock *p = NULL;

  while (cnt--) {
    p = Malloc(sz);
    check_ptr(p);
    Free(p);
  }
}

TEST_F(TestBlockSet, BigBlockOrigin)
{
  const uint64_t sz = 1L << 20;
  int64_t cnt = 1L << 10;
  void *p = NULL;

  while (cnt--) {
    p = ob_malloc(sz, ObNewModIds::TEST);
    check_ptr(p);
    ob_free(p);
  }
}

TEST_F(TestBlockSet, ReusePurgedBlock)
{
  const uint64_t sz = 100L * ABLOCK_SIZE;
  ABlock *p1 = Malloc(sz);
  ABlock *p2 = Malloc(sz);
  check_ptr(p1);
  check_ptr(p2);
  ASSERT_NE(p1, p2);

  Free(p1);
  const uint64_t hold_before_wash = cs_.get_total_hold();
  const int64_t washed_size = cs_.sync_wash(INT64_MAX);
  ASSERT_GT(washed_size, 0);
  ASSERT_LT(cs_.get_total_hold(), hold_before_wash);

  const uint64_t hold_after_wash = cs_.get_total_hold();
  ABlock *p3 = Malloc(sz);
  ASSERT_EQ(p1, p3);
  ASSERT_TRUE(p3->in_use_);
  ASSERT_FALSE(p3->is_washed_);
  ASSERT_GT(cs_.get_total_hold(), hold_after_wash);

  Free(p2);
  Free(p3);
}

TEST_F(TestBlockSet, SplitPurgedBlock)
{
  const uint64_t first_blocks = 100;
  const uint64_t first_size = first_blocks * ABLOCK_SIZE;
  const uint64_t second_size = (BLOCKS_PER_CHUNK - first_blocks) * ABLOCK_SIZE;
  ABlock *p1 = Malloc(first_size);
  ABlock *p2 = Malloc(second_size);
  check_ptr(p1);
  check_ptr(p2);
  AChunk *chunk = p2->chunk();
  ASSERT_EQ(chunk, p1->chunk());

  Free(p1);
  ASSERT_EQ(static_cast<int64_t>(first_size), cs_.sync_wash(INT64_MAX));
  ASSERT_EQ(first_size, chunk->washed_size_);
  ASSERT_EQ(1, chunk->washed_blks_);

  const uint64_t split_blocks = 40;
  ABlock *p3 = Malloc(split_blocks * ABLOCK_SIZE);
  ASSERT_EQ(p1, p3);
  ASSERT_EQ((first_blocks - split_blocks) * ABLOCK_SIZE, chunk->washed_size_);
  ASSERT_EQ(1, chunk->washed_blks_);

  ABlock *p4 = Malloc((first_blocks - split_blocks) * ABLOCK_SIZE);
  ASSERT_EQ(p1 + split_blocks, p4);
  ASSERT_EQ(0, chunk->washed_size_);
  ASSERT_EQ(0, chunk->washed_blks_);

  Free(p2);
  Free(p3);
  Free(p4);
}

TEST_F(TestBlockSet, MergeAdjacentPurgedBlocks)
{
  const uint64_t blocks = 32;
  const uint64_t tail_blocks = BLOCKS_PER_CHUNK - 3 * blocks;
  const uint64_t sz = blocks * ABLOCK_SIZE;
  ABlock *p1 = Malloc(sz);
  ABlock *p2 = Malloc(sz);
  ABlock *p3 = Malloc(sz);
  ABlock *p4 = Malloc(tail_blocks * ABLOCK_SIZE);
  check_ptr(p1);
  check_ptr(p2);
  check_ptr(p3);
  check_ptr(p4);
  AChunk *chunk = p1->chunk();
  ASSERT_EQ(chunk, p2->chunk());
  ASSERT_EQ(chunk, p3->chunk());
  ASSERT_EQ(chunk, p4->chunk());

  Free(p1);
  ASSERT_EQ(static_cast<int64_t>(sz), cs_.sync_wash(INT64_MAX));
  ASSERT_EQ(sz, chunk->washed_size_);
  ASSERT_EQ(1, chunk->washed_blks_);

  Free(p3);
  ASSERT_EQ(static_cast<int64_t>(sz), cs_.sync_wash(INT64_MAX));
  ASSERT_EQ(2 * sz, chunk->washed_size_);
  ASSERT_EQ(2, chunk->washed_blks_);

  Free(p2);
  ASSERT_EQ(static_cast<int64_t>(sz), cs_.sync_wash(INT64_MAX));
  ASSERT_EQ(3 * sz, chunk->washed_size_);
  ASSERT_EQ(1, chunk->washed_blks_);

  ABlock *p5 = Malloc(3 * sz);
  ASSERT_EQ(p1, p5);
  ASSERT_EQ(0, chunk->washed_size_);
  ASSERT_EQ(0, chunk->washed_blks_);

  Free(p4);
  Free(p5);
}

TEST_F(TestBlockSet, Single)
{
  uint64_t sz = INTACT_NORMAL_AOBJECT_SIZE;
  ABlock *pa[1024] = {};
  int cnt = 10;
  while (cnt--) {
    int i = 0;
    for (i = 0; i < 255; ++i) {
      pa[i] = Malloc(sz);
      check_ptr(pa[i]);
    }
    cout << "free" << cnt << endl;
    while (i--) {
      Free(pa[i]);
    }
    cout << cnt << endl;
  }
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
