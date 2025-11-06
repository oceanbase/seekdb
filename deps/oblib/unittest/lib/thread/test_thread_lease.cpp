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

#include "lib/thread/ob_thread_lease.h"
#include <gtest/gtest.h>

using namespace oceanbase::common;
class TestObThreadLease: public ::testing::Test
{
public:
  TestObThreadLease() {}
  virtual ~TestObThreadLease(){}
  virtual void SetUp()
  {
  }
  virtual void TearDown()
  {
  }

  static void SetUpTestCase()
  {
  }

  static void TearDownTestCase()
  {
  }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestObThreadLease);
protected:
  // function members
protected:
};

TEST_F(TestObThreadLease, smoke_test)
{
  ObThreadLease lease;

  ASSERT_EQ(ObThreadLease::IDLE, lease.value());
  ASSERT_EQ(true, lease.acquire());
  ASSERT_EQ(ObThreadLease::HANDLING, lease.value());
  ASSERT_EQ(true, lease.revoke());
  ASSERT_EQ(ObThreadLease::IDLE, lease.value());
  // Directly revoke, expect to return success
  ASSERT_EQ(true, lease.revoke());
  ASSERT_EQ(ObThreadLease::IDLE, lease.value());
}

TEST_F(TestObThreadLease, simulate_multi_thread)
{
  ObThreadLease lease;
  ASSERT_EQ(ObThreadLease::IDLE, lease.value());

  for (int64_t i = 0; i < 100; i++) {
    ASSERT_EQ(true, lease.acquire());
    ASSERT_EQ(ObThreadLease::HANDLING, lease.value());
    // acquire again, expect failure, status change to READY
    ASSERT_EQ(false, lease.acquire());
    ASSERT_EQ(ObThreadLease::READY, lease.value());
    // acquire again, expect failure, status change to READY
    ASSERT_EQ(false, lease.acquire());
    ASSERT_EQ(ObThreadLease::READY, lease.value());
    // acquire again, expect failure, status change to READY
    ASSERT_EQ(false, lease.acquire());
    ASSERT_EQ(ObThreadLease::READY, lease.value());
    // revoke once, expect failure, status change to HANDLING
    ASSERT_EQ(false, lease.revoke());
    ASSERT_EQ(ObThreadLease::HANDLING, lease.value());
    // acquire again, expect failure, status change to READY
    ASSERT_EQ(false, lease.acquire());
    ASSERT_EQ(ObThreadLease::READY, lease.value());
    // acquire again, expect failure, status change to READY
    ASSERT_EQ(false, lease.acquire());
    ASSERT_EQ(ObThreadLease::READY, lease.value());
    // revoke once, expect failure, status changes to HANDLING
    ASSERT_EQ(false, lease.revoke());
    ASSERT_EQ(ObThreadLease::HANDLING, lease.value());
    // Again revoke, expect success, status change to IDLE
    ASSERT_EQ(true, lease.revoke());
    ASSERT_EQ(ObThreadLease::IDLE, lease.value());
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}

