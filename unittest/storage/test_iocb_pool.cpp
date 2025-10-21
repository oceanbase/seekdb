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

#define USING_LOG_PREFIX STORAGE

#define protected public
#define private public

#include "share/ob_local_device.h"
#include "share/ob_thread_pool.h"
#include "lib/allocator/ob_fifo_allocator.h"

namespace oceanbase
{
namespace unittest
{

class TestIOCBPool : public ::testing::Test
{
public:
  TestIOCBPool() = default;
  virtual ~TestIOCBPool() = default;
  virtual void SetUp();
  virtual void TearDown();
private:
  ObIOCBPool<share::ObLocalIOCB> iocb_pool_;
  common::ObFIFOAllocator allocator_;
};

void TestIOCBPool::SetUp()
{
  const ObMemAttr mem_attr(OB_SYS_TENANT_ID, "test_iocb_pool");
  ASSERT_EQ(OB_SUCCESS, allocator_.init(lib::ObMallocAllocator::get_instance(), OB_MALLOC_MIDDLE_BLOCK_SIZE, mem_attr));
  ASSERT_EQ(OB_SUCCESS, iocb_pool_.init(allocator_));
}

void TestIOCBPool::TearDown()
{
  iocb_pool_.reset();
  allocator_.reset();
}

class TestIOCBPoolStress : public share::ObThreadPool
{
public:
  explicit TestIOCBPoolStress(ObIOCBPool<share::ObLocalIOCB> &iocb_pool)
    : thread_cnt_(0), iocb_pool_(iocb_pool), iocb_ptrs_(), is_inited_(false) {}
  virtual ~TestIOCBPoolStress() = default;
  int init(const int64_t thread_cnt);
  virtual void run1();
private:
  static const int ALLOC_IOCB_COUNT_PER_THREAD = 1024;

  int64_t thread_cnt_;
  ObIOCBPool<share::ObLocalIOCB> &iocb_pool_;
  share::ObLocalIOCB* iocb_ptrs_[ALLOC_IOCB_COUNT_PER_THREAD];
  bool is_inited_ = false;
};

int TestIOCBPoolStress::init(const int64_t thread_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (thread_cnt < 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(thread_cnt));
  } else {
    thread_cnt_ = thread_cnt;
    is_inited_ = true;
  }
  return ret;
}

void TestIOCBPoolStress::run1()
{
  for (int count = 0; count < ALLOC_IOCB_COUNT_PER_THREAD; ++count) {
    share::ObLocalIOCB *iocb = iocb_pool_.alloc();
    ASSERT_TRUE(nullptr != iocb);
    iocb_ptrs_[count] = iocb;
  }
  for (int64_t i = 0; i < ALLOC_IOCB_COUNT_PER_THREAD; ++i) {
    iocb_pool_.free(iocb_ptrs_[i]);
  }
}

TEST_F(TestIOCBPool, test_multi_thread)
{
  int ret = OB_SUCCESS;
  TestIOCBPoolStress stress(iocb_pool_);
  const int thread_cnt = 64;

  ret = stress.init(thread_cnt);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = stress.start();
  ASSERT_EQ(OB_SUCCESS, ret);

  stress.wait();
}

} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_iocb_pool.log*");
  OB_LOGGER.set_file_name("test_iocb_pool.log", true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
