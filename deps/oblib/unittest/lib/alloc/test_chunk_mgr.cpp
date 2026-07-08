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
#define protected public
#include "lib/resource/achunk_mgr.h"
#undef protected
#undef private
#include "lib/resource/ob_affinity_ctrl.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;

int main(int argc, char *argv[])
{
  AFFINITY_CTRL.init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

class TestChunkMgr
    : public ::testing::Test, public AChunkMgr
{
public:
  TestChunkMgr()
  {}
  virtual void SetUp()
  {
  }

  virtual void TearDown()
  {
  }
  virtual int madvise(void *addr, size_t length, int advice) override
  {
    if (need_fail_) return -1;
    madvise_len_ = length;
    return AChunkMgr::madvise(addr, length, advice);
  }
  AChunk *alloc_chunk(const uint64_t size) { return AChunkMgr::alloc_chunk(size); }
  AChunk *alloc_co_chunk(const uint64_t size) { return AChunkMgr::alloc_co_chunk(size); }
  Slot &normal_slot(int idx = 0) { return slots_[idx]; }
  Slot &large_slot(int idx = 0) { return slots_[MIN_LARGE_ACHUNK_INDEX + idx]; }
  bool need_fail_ = false;
  int madvise_len_ = 0;
};

TEST_F(TestChunkMgr, NormalChunk)
{
  int NORMAL_SIZE = OB_MALLOC_BIG_BLOCK_SIZE;
  int normal_hold = AChunkMgr::hold(NORMAL_SIZE);
  AChunk *chunks[8] = {};
  for (int i = 0; i < 8; ++i) {
    chunks[i] = alloc_chunk(NORMAL_SIZE);
    EXPECT_TRUE(NULL != chunks[i]);
  }
  EXPECT_EQ(8 * normal_hold, hold_);
  for (int i = 0; i < 8; ++i) {
    free_chunk(chunks[i]);
  }
  EXPECT_EQ(0, normal_slot()->get_pushes());
  EXPECT_EQ(0, normal_slot()->get_pops());
  EXPECT_EQ(0, large_slot()->get_pushes());
  EXPECT_EQ(0, large_slot()->get_pops());
  EXPECT_EQ(0, hold_);
}

TEST_F(TestChunkMgr, LargeChunk)
{
  int LARGE_SIZE = INTACT_ACHUNK_SIZE + 100;
  int large_hold = AChunkMgr::hold(LARGE_SIZE);
  AChunk *chunks[8] = {};
  for (int i = 0; i < 8; ++i) {
    chunks[i] = alloc_chunk(LARGE_SIZE);
    EXPECT_TRUE(NULL != chunks[i]);
  }
  EXPECT_EQ(8 * large_hold, hold_);
  for (int i = 0; i < 8; ++i) {
    free_chunk(chunks[i]);
  }
  EXPECT_EQ(0, large_slot()->get_pushes());
  EXPECT_EQ(0, large_slot()->get_pops());
  EXPECT_EQ(0, hold_);
}

TEST_F(TestChunkMgr, HugeChunk)
{
  int HUGE_SIZE = INTACT_ACHUNK_SIZE * 10 + 100;
  AChunk *chunk = alloc_chunk(HUGE_SIZE);
  EXPECT_TRUE(NULL != chunk);
  int64_t hold = chunk->hold();
  EXPECT_EQ(hold, hold_);
  free_chunk(chunk);
  EXPECT_EQ(0, normal_slot()->hold());
  EXPECT_EQ(0, large_slot()->hold());
  EXPECT_EQ(0, hold_);
}

TEST_F(TestChunkMgr, BorderCase_advise_shrink)
{
  int ps = get_page_size();
  int LARGE_SIZE = INTACT_ACHUNK_SIZE + 100 + ps * 3;
  auto *chunk = alloc_chunk(LARGE_SIZE);
  memset(chunk->data_, 0xaa, chunk->hold());
  EXPECT_EQ(0, large_slot()->get_pushes());
  free_chunk(chunk);
  EXPECT_EQ(0, large_slot()->get_pushes());
  EXPECT_EQ(0, large_slot()->get_pops());
  chunk = alloc_chunk(LARGE_SIZE - ps * 3);
  EXPECT_EQ(0, large_slot()->get_pops());
  EXPECT_EQ(0, madvise_len_);
  free_chunk(chunk);
}

TEST_F(TestChunkMgr, BorderCase_advise_expand)
{
  int ps = get_page_size();
  int LARGE_SIZE = INTACT_ACHUNK_SIZE + 100;
  auto *chunk = alloc_chunk(LARGE_SIZE);
  memset(chunk->data_, 0xaa, chunk->hold());
  EXPECT_EQ(0, large_slot()->get_pushes());
  free_chunk(chunk);
  EXPECT_EQ(0, large_slot()->get_pushes());
  EXPECT_EQ(0, large_slot()->get_pops());
  chunk = alloc_chunk(LARGE_SIZE + ps * 3);
  EXPECT_EQ(0, large_slot()->get_pops());
  free_chunk(chunk);
}

TEST_F(TestChunkMgr, BorderCase_advise_fail)
{
  int ps = get_page_size();
  int LARGE_SIZE = INTACT_ACHUNK_SIZE + 100 + ps * 3;
  auto *chunk = alloc_chunk(LARGE_SIZE);
  memset(chunk->data_, 0xaa, chunk->hold());
  EXPECT_EQ(0, large_slot()->get_pushes());
  free_chunk(chunk);
  EXPECT_EQ(0, large_slot()->get_pushes());
  EXPECT_EQ(0, large_slot()->get_pops());
  need_fail_ = true;
  chunk = alloc_chunk(LARGE_SIZE - ps * 3);
  EXPECT_EQ(0, large_slot()->get_pushes());
  EXPECT_EQ(0, large_slot()->get_pops());
  free_chunk(chunk);
}

TEST_F(TestChunkMgr, alloc_co_chunk)
{
  int NORMAL_SIZE = OB_MALLOC_BIG_BLOCK_SIZE;
  set_limit(0);
  auto *chunk = alloc_co_chunk(NORMAL_SIZE);  // high_prio alloc always succeed
  EXPECT_TRUE(chunk != NULL);
  free_chunk(chunk);
  EXPECT_EQ(0, large_slot()->get_pushes());  // direct free
}

TEST_F(TestChunkMgr, FreeListBasic)
{
  {
    AChunk *chunk = alloc_chunk(0);
    free_chunk(chunk);
    EXPECT_EQ(0, normal_slot()->get_pushes());
    EXPECT_EQ(0, normal_slot()->get_pops());
    EXPECT_EQ(0, hold_);
  }
  {
    AChunk *chunk = alloc_chunk(0);
    free_chunk(chunk);
    EXPECT_EQ(0, normal_slot()->get_pushes());
    EXPECT_EQ(0, normal_slot()->get_pops());
    EXPECT_EQ(0, hold_);
  }
  {
    AChunk *chunk = alloc_chunk(OB_MALLOC_BIG_BLOCK_SIZE);
    free_chunk(chunk);
    EXPECT_EQ(0, normal_slot()->get_pushes());
    EXPECT_EQ(0, normal_slot()->get_pops());
    EXPECT_EQ(0, hold_);
  }
}

TEST_F(TestChunkMgr, sync_wash)
{
  set_limit(1LL<<30);
  int NORMAL_SIZE = OB_MALLOC_BIG_BLOCK_SIZE;
  int LARGE_SIZE = INTACT_ACHUNK_SIZE + 100;
  normal_slot()->set_max_chunk_cache_size(1LL<<30);
  normal_slot(1)->set_max_chunk_cache_size(1LL<<30);
  AChunk *chunks[16][2] = {};
  for (int i = 0; i < 16; ++i) {
    chunks[i][0] = alloc_chunk(NORMAL_SIZE);
    chunks[i][1] = alloc_chunk(LARGE_SIZE);
  }
  for (int i = 0; i < 16; ++i) {
    for (int j = 0; j < 2; ++j) {
      free_chunk(chunks[i][j]);
      chunks[i][j] = NULL;
    }
  }
  int64_t hold = get_freelist_hold();
  EXPECT_EQ(0, hold);
  EXPECT_EQ(0, hold_);
  EXPECT_EQ(0, normal_slot()->count());
  EXPECT_EQ(0, large_slot()->count());
  int64_t washed_size = sync_wash();
  EXPECT_EQ(0, washed_size);
  EXPECT_EQ(0, hold_);
  EXPECT_EQ(0, normal_slot()->count());
  EXPECT_EQ(0, large_slot()->count());
}
