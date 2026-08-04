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
#include "mtlenv/mock_server_runtime_env.h"
namespace oceanbase
{

using namespace share;
using namespace common;
using namespace storage;

class ObTestQueryRowIterator : public ObQueryRowIterator
{
public:
  virtual int get_next_row(blocksstable::ObDatumRow *&row)
  {
    return OB_SUCCESS;
  }
  virtual void reset()
  {
  }
  virtual void reclaim()
  {
  }
};

class ObGlobalIteratorPoolTest: public ::testing::Test
{
public:
  ObGlobalIteratorPoolTest();
  virtual ~ObGlobalIteratorPoolTest() = default;
  virtual void SetUp() override;
  virtual void TearDown() override;
  static void SetUpTestCase();
  static void TearDownTestCase();
};

ObGlobalIteratorPoolTest::ObGlobalIteratorPoolTest()
{
}

void ObGlobalIteratorPoolTest::SetUpTestCase()
{
  ASSERT_EQ(MockServerRuntimeEnv::get_instance().init(), OB_SUCCESS);
}

void ObGlobalIteratorPoolTest::TearDownTestCase()
{
  MockServerRuntimeEnv::get_instance().destroy();
}

void ObGlobalIteratorPoolTest::SetUp()
{
  ASSERT_TRUE(MockServerRuntimeEnv::get_instance().is_inited());
  lib::set_memory_budget(ObGlobalIteratorPool::ITER_POOL_MIN_MEM_THRESHOLD * 2);
}

void ObGlobalIteratorPoolTest::TearDown()
{
}

TEST_F(ObGlobalIteratorPoolTest, init)
{
  int ret = 0;
  ObGlobalIteratorPool iter_pool;
  ASSERT_TRUE(iter_pool.check_need_iterator_pool());
  ret = iter_pool.init();
  ASSERT_EQ(ret, OB_SUCCESS);
  const int64_t mem_limit = iter_pool.memory_budget_ * ObGlobalIteratorPool::ITER_POOL_MAX_MEM_PERCENT;
  const int64_t bucket_cnt = mem_limit / (ObGlobalIteratorPool::ITER_POOL_ITER_MEM_LIMIT * (1 + ObGlobalIteratorPool::ITER_POOL_MAX_CACHED_ITER_TYPE));
  ASSERT_EQ(bucket_cnt, iter_pool.bucket_cnt_);
  for (int64_t i = 0; i <= ObGlobalIteratorPool::ITER_POOL_MAX_CACHED_ITER_TYPE; ++i) {
    CachedIteratorNode *nodes = iter_pool.cached_node_array_[i];
    ASSERT_TRUE(nullptr != nodes);
    for (int64_t j = 0; j < iter_pool.bucket_cnt_; ++j) {
      CachedIteratorNode &node = nodes[j];
      ASSERT_FALSE(node.is_occupied_);
      ASSERT_TRUE(nullptr == node.iter_);
      ASSERT_TRUE(nullptr == node.stmt_iter_pool_);
    }
  }
}

TEST_F(ObGlobalIteratorPoolTest, get)
{
  int ret = 0;
  ObGlobalIteratorPool iter_pool;
  ret = iter_pool.init();
  ASSERT_EQ(ret, OB_SUCCESS);
  ObQRIterType type = T_INVALID_ITER_TYPE;
  CachedIteratorNode *cached_node = nullptr;
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_INVALID_ARGUMENT);
  ASSERT_TRUE(nullptr == cached_node);

  type = T_MAX_ITER_TYPE;
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_INVALID_ARGUMENT);
  ASSERT_TRUE(nullptr == cached_node);

  type = T_SINGLE_GET;
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node);
  ASSERT_TRUE(cached_node->is_occupied_);
  ASSERT_TRUE(nullptr == cached_node->iter_);
  ASSERT_TRUE(nullptr != cached_node->stmt_iter_pool_);

  ObArenaAllocator *iter_alloc = cached_node->get_iter_allocator();
  void *buf =iter_alloc->alloc(sizeof(ObTestQueryRowIterator));
  ASSERT_TRUE(nullptr != buf);
  ObTestQueryRowIterator *merge = new (buf) ObTestQueryRowIterator();
  cached_node->set_iter(merge);

  type = T_SINGLE_GET;
  CachedIteratorNode *cached_node1 = nullptr;
  ret = iter_pool.get(type, cached_node1);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr == cached_node1);

  iter_pool.release(cached_node);
  ret = iter_pool.get(type, cached_node1);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node1);
  ASSERT_TRUE(cached_node1->is_occupied_);
  ASSERT_TRUE(nullptr != cached_node1->iter_);
  ASSERT_TRUE(nullptr != cached_node1->stmt_iter_pool_);
  ASSERT_TRUE(merge == cached_node1->iter_);
  iter_pool.release(cached_node);

  type = T_MULTI_GET;
  CachedIteratorNode *mg_cached_node = nullptr;
  ret = iter_pool.get(type, mg_cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != mg_cached_node);
  ASSERT_TRUE(mg_cached_node->is_occupied_);
  ASSERT_TRUE(nullptr == mg_cached_node->iter_);
  ASSERT_TRUE(nullptr != mg_cached_node->stmt_iter_pool_);
  iter_pool.release(mg_cached_node);

  type = T_SINGLE_SCAN;
  CachedIteratorNode *ss_cached_node = nullptr;
  ret = iter_pool.get(type, ss_cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != ss_cached_node);
  ASSERT_TRUE(ss_cached_node->is_occupied_);
  ASSERT_TRUE(nullptr == ss_cached_node->iter_);
  ASSERT_TRUE(nullptr != ss_cached_node->stmt_iter_pool_);
  iter_pool.release(ss_cached_node);

  type = T_MULTI_SCAN;
  CachedIteratorNode *ms_cached_node = nullptr;
  ret = iter_pool.get(type, ms_cached_node);
  ASSERT_EQ(ret, OB_INVALID_ARGUMENT);
  ASSERT_TRUE(nullptr == ms_cached_node);
}

TEST_F(ObGlobalIteratorPoolTest, release)
{
  int ret = 0;
  ObGlobalIteratorPool iter_pool;
  ret = iter_pool.init();
  ASSERT_EQ(ret, OB_SUCCESS);
  ObQRIterType type = T_MULTI_GET;
  CachedIteratorNode *cached_node = nullptr;
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node);
  ASSERT_TRUE(cached_node->is_occupied_);
  ASSERT_TRUE(nullptr == cached_node->iter_);
  ASSERT_TRUE(nullptr != cached_node->stmt_iter_pool_);

  ObArenaAllocator *iter_alloc = cached_node->get_iter_allocator();
  void *buf = iter_alloc->alloc(sizeof(ObTestQueryRowIterator));
  ASSERT_TRUE(nullptr != buf);
  ObTestQueryRowIterator *merge = new (buf) ObTestQueryRowIterator();
  cached_node->set_iter(merge);
  iter_pool.release(cached_node);

  CachedIteratorNode *cached_node1 = nullptr;
  ret = iter_pool.get(type, cached_node1);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node1);
  ASSERT_TRUE(cached_node1->is_occupied_);
  ASSERT_TRUE(nullptr != cached_node1->iter_);
  ASSERT_TRUE(nullptr != cached_node1->stmt_iter_pool_);
  ASSERT_TRUE(merge == cached_node1->iter_);

  ObArenaAllocator *iter_alloc1 = cached_node1->get_iter_allocator();
  ASSERT_TRUE(iter_alloc == iter_alloc1);
  ASSERT_TRUE(iter_alloc1->total() > 0);
  buf = iter_alloc1->alloc(ObGlobalIteratorPool::ITER_POOL_ITER_MEM_LIMIT + 1);
  ASSERT_TRUE(nullptr != buf);
  ASSERT_TRUE(iter_alloc1->total() > ObGlobalIteratorPool::ITER_POOL_ITER_MEM_LIMIT);
  iter_pool.release(cached_node1);

  CachedIteratorNode *cached_node2 = nullptr;
  ret = iter_pool.get(type, cached_node2);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node2);
  ASSERT_TRUE(cached_node2->is_occupied_);
  ASSERT_TRUE(nullptr == cached_node2->iter_);
  ASSERT_TRUE(nullptr != cached_node2->stmt_iter_pool_);
  ObArenaAllocator *iter_alloc2 = cached_node2->get_iter_allocator();
  ASSERT_TRUE(iter_alloc == iter_alloc2);
  ASSERT_TRUE(iter_alloc2->total() < ObGlobalIteratorPool::ITER_POOL_ITER_MEM_LIMIT);

  buf = iter_alloc->alloc(sizeof(ObTestQueryRowIterator));
  ASSERT_TRUE(nullptr != buf);
  merge = new (buf) ObTestQueryRowIterator();
  cached_node2->set_iter(merge);
  cached_node2->set_exception_occur(true);
  iter_pool.release(cached_node2);

  CachedIteratorNode *cached_node3 = nullptr;
  ret = iter_pool.get(type, cached_node3);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node3);
  ASSERT_TRUE(cached_node3->is_occupied_);
  ASSERT_TRUE(nullptr == cached_node3->iter_);
  ASSERT_TRUE(nullptr != cached_node3->stmt_iter_pool_);
  ObArenaAllocator *iter_alloc3 = cached_node3->get_iter_allocator();
  ASSERT_TRUE(iter_alloc == iter_alloc3);
  ASSERT_TRUE(iter_alloc3->total() < ObGlobalIteratorPool::ITER_POOL_ITER_MEM_LIMIT);
}

TEST_F(ObGlobalIteratorPoolTest, destroy)
{
  int ret = 0;
  ObGlobalIteratorPool iter_pool;
  ret = iter_pool.init();
  ASSERT_EQ(ret, OB_SUCCESS);

  ObQRIterType type = T_SINGLE_SCAN;
  CachedIteratorNode *cached_node = nullptr;
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node);
  ASSERT_TRUE(cached_node->is_occupied_);
  ASSERT_TRUE(nullptr == cached_node->iter_);
  ASSERT_TRUE(nullptr != cached_node->stmt_iter_pool_);

  ObArenaAllocator *iter_alloc = cached_node->get_iter_allocator();
  void *buf = iter_alloc->alloc(sizeof(ObTestQueryRowIterator));
  ASSERT_TRUE(nullptr != buf);
  ObTestQueryRowIterator *merge = new (buf) ObTestQueryRowIterator();
  cached_node->set_iter(merge);
  iter_pool.release(cached_node);

  iter_pool.destroy();
  ASSERT_EQ(0, iter_pool.bucket_cnt_);
  for (int64_t i = 0; i <= ObGlobalIteratorPool::ITER_POOL_MAX_CACHED_ITER_TYPE; ++i) {
    CachedIteratorNode *nodes = iter_pool.cached_node_array_[i];
    ASSERT_TRUE(nullptr == nodes);
  }
}

TEST_F(ObGlobalIteratorPoolTest, wash)
{
  int ret = 0;
  const int64_t original_memory_budget = lib::get_memory_budget();
  const int64_t initial_memory_budget = ObGlobalIteratorPool::ITER_POOL_MIN_MEM_THRESHOLD * 2;
  const int64_t shrunk_memory_budget = ObGlobalIteratorPool::ITER_POOL_MIN_MEM_THRESHOLD;
  lib::set_memory_budget(initial_memory_budget);
  ObGlobalIteratorPool iter_pool;
  iter_pool.memory_budget_ = initial_memory_budget;
  ret = iter_pool.init();
  ASSERT_EQ(ret, OB_SUCCESS);
  ObQRIterType type = T_SINGLE_SCAN;
  CachedIteratorNode *cached_node = nullptr;
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr != cached_node);
  ASSERT_TRUE(cached_node->is_occupied_);
  ASSERT_TRUE(nullptr == cached_node->iter_);

  ObArenaAllocator *iter_alloc = cached_node->get_iter_allocator();
  void *buf = iter_alloc->alloc(sizeof(ObTestQueryRowIterator));
  ASSERT_TRUE(nullptr != buf);
  ObTestQueryRowIterator *merge = new (buf) ObTestQueryRowIterator();
  cached_node->set_iter(merge);
  buf = iter_alloc->alloc(256 * 1024);
  ASSERT_TRUE(nullptr != buf);

  ASSERT_EQ(initial_memory_budget, iter_pool.memory_budget_);
  lib::set_memory_budget(shrunk_memory_budget);
  iter_pool.wash();
  STORAGE_LOG(INFO, "after memory budget shrink", K(iter_pool), K(shrunk_memory_budget));
  ASSERT_FALSE(iter_pool.is_washing_);
  ASSERT_TRUE(iter_pool.is_disabled_);
  ASSERT_LT(iter_pool.calc_bucket_cnt(), iter_pool.bucket_cnt_);

  iter_pool.release(cached_node);
  ret = iter_pool.get(type, cached_node);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_TRUE(nullptr == cached_node);
  STORAGE_LOG(INFO, "after release", K(iter_pool));

  lib::set_memory_budget(initial_memory_budget);
  iter_pool.wash();
  STORAGE_LOG(INFO, "after memory budget restore", K(iter_pool), K(initial_memory_budget));
  ASSERT_FALSE(iter_pool.is_washing_);
  ASSERT_FALSE(iter_pool.is_disabled_);

  lib::set_memory_budget(ObGlobalIteratorPool::ITER_POOL_MIN_MEM_THRESHOLD - 1);
  iter_pool.wash();
  STORAGE_LOG(INFO, "after disabling iterator pool", K(iter_pool));
  ASSERT_FALSE(iter_pool.is_washing_);
  ASSERT_TRUE(iter_pool.is_disabled_);

  lib::set_memory_budget(initial_memory_budget);
  iter_pool.wash();
  STORAGE_LOG(INFO, "after re-enabling iterator pool", K(iter_pool));
  ASSERT_FALSE(iter_pool.is_washing_);
  ASSERT_FALSE(iter_pool.is_disabled_);
  ASSERT_EQ(initial_memory_budget, iter_pool.memory_budget_);
  lib::set_memory_budget(original_memory_budget);
}

}

int main(int argc, char **argv)
{
  system("rm -f test_global_iterator_pool.log*");
  OB_LOGGER.set_file_name("test_global_iterator_pool.log", true, true);
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
