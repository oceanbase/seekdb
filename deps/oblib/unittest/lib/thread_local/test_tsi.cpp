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
#include "lib/thread_local/ob_tsi_factory.h"

using namespace oceanbase::common;
using namespace std;

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


TEST(TestTsi, TestName)
{
  static int des_count = 0;
  struct S {
    ~S() {
      cout << "destructor S" << endl;
      des_count++;
    }
  };
  struct SS {
    ~SS() {
      cout << des_count << endl;
    }
  };
  GET_TSI(SS);
  S *sp = GET_TSI(S[2])[0];
  (void)(sp);
  ASSERT_TRUE(sp);
}
