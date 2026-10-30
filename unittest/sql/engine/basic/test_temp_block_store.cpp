/*
 * Copyright (c) 2026 OceanBase.
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
#include "lib/allocator/ob_jemalloc.h"
#include "sql/engine/basic/ob_temp_block_store.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

class TestTempBlockStore : public ::testing::Test
{
protected:
  void expect_balanced_cleanup(const bool submit_read, const bool explicit_reset)
  {
    // Warm up lazy initialization before measuring this thread's allocations.
    for (int64_t i = 0; i < 8; ++i) {
      ASSERT_EQ(OB_SUCCESS, cleanup_one_reader(submit_read, explicit_reset));
    }
    uint64_t *allocated = nullptr;
    uint64_t *deallocated = nullptr;
    size_t size = sizeof(allocated);
    ASSERT_EQ(0, je_mallctl("thread.allocatedp", &allocated, &size, nullptr, 0));
    size = sizeof(deallocated);
    ASSERT_EQ(0, je_mallctl("thread.deallocatedp", &deallocated, &size, nullptr, 0));
    ASSERT_NE(nullptr, allocated);
    ASSERT_NE(nullptr, deallocated);
    const uint64_t allocated_before = *allocated;
    const uint64_t deallocated_before = *deallocated;
    for (int64_t i = 0; i < 128; ++i) {
      ASSERT_EQ(OB_SUCCESS, cleanup_one_reader(submit_read, explicit_reset));
    }
    const uint64_t allocated_bytes = *allocated - allocated_before;
    const uint64_t deallocated_bytes = *deallocated - deallocated_before;
    EXPECT_EQ(allocated_bytes, deallocated_bytes);
  }

  int cleanup_one_reader(const bool submit_read, const bool explicit_reset)
  {
    int ret = OB_SUCCESS;
    ObTempBlockStore store;
    ObTempBlockStore::BlockReader reader;
    data_plane::ObTmpFileIOHandle *handle = nullptr;
    if (OB_FAIL(reader.init(&store))) {
    } else if (OB_FAIL(reader.get_read_io_handler(handle))) {
    } else if (submit_read) {
      // The real read entry point allocates the handle's private implementation
      // before rejecting the missing server service. No database or file is
      // needed to exercise cleanup after an I/O submission failure.
      data_plane::ObTmpFileIOInfo io;
      if (OB_NOT_INIT != data_plane::tmp_file_aio_pread(io, 0, *handle)) {
        ret = OB_ERR_UNEXPECTED;
      }
      reader.reuse();
      data_plane::ObTmpFileIOHandle *reused = nullptr;
      if (OB_SUCC(ret) && OB_FAIL(reader.get_read_io_handler(reused))) {
      } else if (OB_SUCC(ret) && reused != handle) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (explicit_reset) {
      reader.reset();
      reader.reset();
    }
    return ret;
  }
};

TEST_F(TestTempBlockStore, reset_releases_private_read_state)
{
  expect_balanced_cleanup(true, true);
}

TEST_F(TestTempBlockStore, destructor_releases_private_read_state)
{
  expect_balanced_cleanup(true, false);
}

TEST_F(TestTempBlockStore, reset_without_io_is_idempotent)
{
  expect_balanced_cleanup(false, true);
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
