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
#include "lib/random/ob_random.h"

namespace oceanbase
{
namespace common
{

TEST(ObRandom, normal)
{
  int64_t res = 0;
  //test static methods
  res = ObRandom::rand(10, 10);
  ASSERT_EQ(10, res);
  res = ObRandom::rand(0, 100);
  ASSERT_TRUE(res <= 100 && res >= 0);
  res = ObRandom::rand(-1, -2);
  ASSERT_TRUE(res >= -2 && res <= -1);
  res = ObRandom::rand(10, 1);
  ASSERT_TRUE(res >= 1 && res <= 10);

  //test int64_t random number
  ObRandom rand1;
  res = rand1.get();
  res = rand1.get(10, 10);
  ASSERT_EQ(10, res);
  res = rand1.get(0, 100);
  ASSERT_TRUE(res <= 100 && res >= 0);
  res = rand1.get(-1, -2);
  ASSERT_TRUE(res >= -2 && res <= -1);
  res = rand1.get(10, 1);
  ASSERT_TRUE(res >= 1 && res <= 10);

  //test int32_t random number
  res = rand1.get_int32();
}

}
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}



