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
#include "lib/utility/ob_defer.h"

using namespace std;

using namespace oceanbase;
using namespace oceanbase::common;

TEST(ScopedLambda, Basic) {
  bool mybool = false;
  {
    auto exec = MakeScopedLambda([&]() { mybool = true; });  // NOLINT
    EXPECT_FALSE(mybool);
  }
  EXPECT_TRUE(mybool);

  mybool = false;
  {
    auto exec = MakeScopedLambda([&]() { mybool = true; });  // NOLINT
    EXPECT_FALSE(mybool);
    exec.deactivate();
  }
  EXPECT_FALSE(mybool);

  mybool = false;
  {
    auto exec = MakeScopedLambda([&]() { mybool = true; });  // NOLINT
    EXPECT_FALSE(mybool);
    exec.deactivate();
    exec.activate();
  }
  EXPECT_TRUE(mybool);

  int counter = 0;
  {
    auto exec = MakeScopedLambda([&]() { ++counter; });  // NOLINT
    EXPECT_EQ(0, counter);
    exec.run_and_expire();
    EXPECT_EQ(1, counter);
  }
  EXPECT_EQ(1, counter);  // should not have executed upon scope exit.
}

TEST(ScopedLambda, Defer) {
  bool mybool = false;
  {
    DEFER(mybool = true);
    EXPECT_FALSE(mybool);
  }
  EXPECT_TRUE(mybool);

  mybool = false;
  {
    NAMED_DEFER(exec, mybool = true);
    EXPECT_FALSE(mybool);
    exec.deactivate();
  }
  EXPECT_FALSE(mybool);

  mybool = false;
  {
    NAMED_DEFER(exec, mybool = true);
    EXPECT_FALSE(mybool);
    exec.deactivate();
    exec.activate();
  }
  EXPECT_TRUE(mybool);

  int counter = 0;
  {
    NAMED_DEFER(exec, ++counter);
    EXPECT_EQ(0, counter);
    exec.run_and_expire();
    EXPECT_EQ(1, counter);
  }
  EXPECT_EQ(1, counter);  // should not have executed upon scope exit.
}

TEST(Defer, InitializerLists) {
  struct S {
    int a;
    int b;
  };
  int x = 10;
  {
    DEFER({
      S s{10, 20};
      x = s.b;
    });
  }
  EXPECT_EQ(20, x);
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
