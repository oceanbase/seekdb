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
#include "deps/oblib/src/lib/stat/ob_di_cache.h"

namespace oceanbase
{
namespace common
{
TEST(ObStatArray, normal)
{
  int ret = OB_SUCCESS;
  ObWaitEventStatArray stats1;
  ObWaitEventStatArray stats2;

  stats1.get(0)->total_waits_ = 1;
  stats1.get(3)->total_waits_ = 2;
  stats2.get(4)->total_waits_ = 3;
  stats1.add(stats2);

  ASSERT_EQ(1, stats1.get(0)->total_waits_);
  ASSERT_EQ(2, stats1.get(3)->total_waits_);
  ASSERT_EQ(3, stats1.get(4)->total_waits_);
}

}
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}





