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
#include <thread>

#include "storage/compaction/ob_snapshot_gc_scn_renewal_state.h"

namespace oceanbase
{
namespace storage
{
namespace unittest
{

TEST(TestSnapshotGcScnRenewalState, renew_target_scn_coalesces_by_max)
{
  ObSnapshotGcScnRenewalState renewal_state;
  static const int64_t THREAD_COUNT = 8;
  std::thread threads[THREAD_COUNT];

  EXPECT_EQ(0, renewal_state.get_target_scn());
  renewal_state.update_target_scn(100);
  renewal_state.update_target_scn(50);
  EXPECT_EQ(100, renewal_state.get_target_scn());

  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    threads[i] = std::thread([&renewal_state, i]() {
      renewal_state.update_target_scn(200 + i);
    });
  }
  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    threads[i].join();
  }
  EXPECT_EQ(200 + THREAD_COUNT - 1,
      renewal_state.get_target_scn());
}

TEST(TestSnapshotGcScnRenewalState, renew_target_scn_never_moves_backward)
{
  ObSnapshotGcScnRenewalState renewal_state;
  renewal_state.update_target_scn(200);
  renewal_state.update_target_scn(100);
  EXPECT_EQ(200, renewal_state.get_target_scn());
}

} // namespace unittest
} // namespace storage
} // namespace oceanbase
