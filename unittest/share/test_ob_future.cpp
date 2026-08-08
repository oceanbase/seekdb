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

#define UNITTEST_DEBUG

#include "lib/future/ob_future.h"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace std;

class TestObFuture: public ::testing::Test
{
public:
  TestObFuture() {};
  virtual ~TestObFuture() {};
  virtual void SetUp() { };
  virtual void TearDown() {
    ASSERT_EQ(function::DefaultFunctionAllocator::get_default_allocator().total_alive_num, 0);
    ASSERT_EQ(future::DefaultFutureAllocator::get_default_allocator().total_alive_num, 0);
    ASSERT_EQ(guard::DefaultSharedGuardAllocator::get_default_allocator().total_alive_num, 0);
  };
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestObFuture);
};

TEST_F(TestObFuture, normal) {
  cout << "size:" << sizeof(ObFuture<int>) << "," << sizeof(ObPromise<int>) << endl;
  ObPromise<int> promise;
  ASSERT_EQ(promise.is_valid(), false);
  {
    ObFuture<int> future = promise.get_future();
    ASSERT_EQ(future.is_ready(), false);
    ASSERT_EQ(future.is_valid(), false);
  }
  ASSERT_EQ(promise.set(5), OB_NOT_INIT);
  ASSERT_EQ(promise.init(), OB_SUCCESS);
  ASSERT_EQ(promise.is_valid(), true);
  ASSERT_EQ(promise.init(), OB_INIT_TWICE);
  ObFuture<int> future = promise.get_future();
  ASSERT_EQ(future.is_ready(), false);
  ASSERT_EQ(future.is_valid(), true);
  int first_wait_ret = OB_SUCCESS;
  int second_wait_ret = OB_SUCCESS;
  std::atomic<bool> waits_finished(false);
  thread t([future, &first_wait_ret, &second_wait_ret, &waits_finished]() {
    int *temp = nullptr;
    first_wait_ret = future.wait_for(30);
    second_wait_ret = future.wait_for(30);
    waits_finished.store(true, std::memory_order_release);
    ASSERT_EQ(future.get(temp), OB_SUCCESS);
    ASSERT_EQ(*temp, 5);
    ASSERT_EQ(future.is_ready(), true);
    ASSERT_EQ(future.is_valid(), true);
    ASSERT_EQ(future.get(temp), OB_SUCCESS);
  });
  while (!waits_finished.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  EXPECT_EQ(OB_TIMEOUT, first_wait_ret);
  EXPECT_EQ(OB_TIMEOUT, second_wait_ret);
  ASSERT_EQ(promise.set(5), OB_SUCCESS);
  ASSERT_EQ(promise.set(6), OB_OP_NOT_ALLOW);
  t.join();
}

TEST_F(TestObFuture, return_void) {
  cout << "size:" << sizeof(ObFuture<int>) << "," << sizeof(ObPromise<int>) << endl;
  ObPromise<void> promise;
  ASSERT_EQ(promise.is_valid(), false);
  {
    ObFuture<void> future = promise.get_future();
    ASSERT_EQ(future.is_ready(), false);
    ASSERT_EQ(future.is_valid(), false);
  }
  ASSERT_EQ(promise.init(), OB_SUCCESS);
  ASSERT_EQ(promise.is_valid(), true);
  ASSERT_EQ(promise.init(), OB_INIT_TWICE);
  ObFuture<void> future = promise.get_future();
  ASSERT_EQ(future.is_ready(), false);
  ASSERT_EQ(future.is_valid(), true);
  int first_wait_ret = OB_SUCCESS;
  int second_wait_ret = OB_SUCCESS;
  std::atomic<bool> waits_finished(false);
  thread t([future, &first_wait_ret, &second_wait_ret, &waits_finished]() {
    first_wait_ret = future.wait_for(30);
    second_wait_ret = future.wait_for(30);
    waits_finished.store(true, std::memory_order_release);
    ASSERT_EQ(future.wait(), OB_SUCCESS);
    ASSERT_EQ(future.is_ready(), true);
    ASSERT_EQ(future.is_valid(), true);
    ASSERT_EQ(future.wait(), OB_SUCCESS);
  });
  while (!waits_finished.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  EXPECT_EQ(OB_TIMEOUT, first_wait_ret);
  EXPECT_EQ(OB_TIMEOUT, second_wait_ret);
  ASSERT_EQ(promise.set(), OB_SUCCESS);
  t.join();
}

TEST_F(TestObFuture, promise_destroy_first) {
  {
    ObFuture<void> future;
    {
      ObPromise<void> promise;
      ASSERT_EQ(promise.init(), OB_SUCCESS);
      future = promise.get_future();
      promise.stop_and_notify_all();
    }
    ASSERT_EQ(future.wait(), OB_NOT_RUNNING);
  }

  {
    ObFuture<void> future;
    std::thread *th;
    {
      ObPromise<void> promise;
      ASSERT_EQ(promise.init(), OB_SUCCESS);
      future = promise.get_future();
      th = new thread([promise]() mutable {
        this_thread::sleep_for(chrono::milliseconds(10));
        promise.stop_and_notify_all();
      });
    }
    ASSERT_EQ(future.wait(), OB_NOT_RUNNING);
    th->join();
  }

   {
    ObFuture<void> future;
    std::thread *th;
    {
      ObPromise<void> promise;
      ASSERT_EQ(promise.init(), OB_SUCCESS);
      future = promise.get_future();
      th = new thread([promise]() mutable {
        this_thread::sleep_for(chrono::milliseconds(10));
        promise.stop_and_notify_all();
      });
    }
    ASSERT_EQ(future.wait_for(100), OB_NOT_RUNNING);
    th->join();
  }
}

}
}
