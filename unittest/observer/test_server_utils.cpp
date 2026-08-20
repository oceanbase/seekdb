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

#define private public
#include "observer/ob_server_utils.h"
#undef private

#include <gtest/gtest.h>

namespace oceanbase
{
namespace observer
{
using namespace common;

TEST(TestServerUtils, automatic_disk_size_uses_available_space)
{
  struct statvfs svfs = {};
  svfs.f_bsize = 4096;
  svfs.f_blocks = 100;
  svfs.f_bfree = 20;
  svfs.f_bavail = 15;
  int64_t disk_size = 0;
  int64_t disk_percentage = 0;

  ASSERT_EQ(OB_SUCCESS, ObServerUtils::decide_disk_size(
      svfs, 0, 0, 30, "/log", disk_size, disk_percentage));
  ASSERT_EQ(15 * 4096 * 30 / 100, disk_size);
  ASSERT_EQ(30, disk_percentage);

  ASSERT_EQ(OB_SUCCESS, ObServerUtils::decide_disk_size(
      svfs, 0, 50, 30, "/log", disk_size, disk_percentage));
  ASSERT_EQ(15 * 4096 * 50 / 100, disk_size);
  ASSERT_EQ(50, disk_percentage);
}

TEST(TestServerUtils, explicit_disk_size_keeps_total_space_semantics)
{
  struct statvfs svfs = {};
  svfs.f_bsize = 4096;
  svfs.f_blocks = 100;
  svfs.f_bfree = 20;
  svfs.f_bavail = 15;
  const int64_t configured_size = 50 * 4096;
  int64_t disk_size = 0;
  int64_t disk_percentage = 0;

  ASSERT_EQ(OB_SUCCESS, ObServerUtils::decide_disk_size(
      svfs, configured_size, 0, 30, "/log", disk_size, disk_percentage));
  ASSERT_EQ(configured_size, disk_size);
  ASSERT_EQ(0, disk_percentage);
}

} // namespace observer
} // namespace oceanbase
