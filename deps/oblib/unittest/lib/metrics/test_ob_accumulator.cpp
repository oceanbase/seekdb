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

#include "lib/metrics/ob_accumulator.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;

TEST(ObAccumulator, full_test)
{
  ObAccumulator acc;
  ASSERT_EQ(0, acc.get_value());
  acc.add(1);
  ASSERT_EQ(0, acc.get_value());
  acc.freeze();
  ASSERT_EQ(1, acc.get_value());

  // After freeze, get_value always returns 1 until the next freeze
  acc.add(100);
  ASSERT_EQ(1, acc.get_value());
  acc.freeze();
  ASSERT_EQ(100, acc.get_value());

  // After freeze, call add again, the internal temporary value is calculated from 0
  acc.add(200);
  acc.add(300);
  acc.freeze();
  ASSERT_EQ(500, acc.get_value());
}


int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}

