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

#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>

#include "storage/tx/async_index_log_signal.h"
#include "storage/tx/ob_trans_submit_log_cb.h"

namespace oceanbase {
namespace transaction {
namespace {

constexpr int64_t LONG_WAIT_US = 5 * 1000 * 1000;

TEST(TestAsyncIndexLogSignal, disabled_by_default) {
  AsyncIndexLogSignal signal;
  const uint32_t key = signal.get_key();
  signal.notify_log_ready(true);
  EXPECT_EQ(key, signal.get_key());
}

TEST(TestAsyncIndexLogSignal, ignores_unrelated_logs_when_enabled) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  const uint32_t key = signal.get_key();
  signal.notify_log_ready(false);
  EXPECT_EQ(key, signal.get_key());
  signal.notify_log_ready(true);
  EXPECT_NE(key, signal.get_key());
}

TEST(TestAsyncIndexLogSignal, disabling_suppresses_further_log_notifications) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  signal.notify_log_ready(true);
  const uint32_t key = signal.get_key();
  signal.set_enabled(false);
  signal.notify_log_ready(true);
  signal.notify_log_ready(false);
  EXPECT_EQ(key, signal.get_key());
}

TEST(TestAsyncIndexLogSignal, notification_before_wait_is_not_lost) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  const uint32_t key = signal.get_key();
  signal.notify_log_ready(true);
  EXPECT_EQ(common::OB_SUCCESS, signal.wait(key, 1000));
}

TEST(TestAsyncIndexLogSignal, no_notification_times_out) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  EXPECT_EQ(common::OB_TIMEOUT, signal.wait(signal.get_key(), 1000));
}

TEST(TestAsyncIndexLogSignal, log_notification_releases_waiting_thread) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  const uint32_t key = signal.get_key();
  std::promise<void> started;
  auto started_future = started.get_future();
  auto result = std::async(std::launch::async, [&]() {
    started.set_value();
    return signal.wait(key, LONG_WAIT_US);
  });
  started_future.wait();
  // The waiter must remain pending until a notification, independently of
  // whether the scheduler has reached the kernel wait by this point.
  EXPECT_EQ(std::future_status::timeout,
            result.wait_for(std::chrono::milliseconds(20)));
  signal.notify_log_ready(true);
  const auto status = result.wait_for(std::chrono::seconds(2));
  EXPECT_EQ(std::future_status::ready, status);
  if (status == std::future_status::ready) {
    EXPECT_EQ(common::OB_SUCCESS, result.get());
  } else {
    signal.wake_waiter();
  }
}

TEST(TestAsyncIndexLogSignal, stop_before_wait_works_while_disabled) {
  AsyncIndexLogSignal signal;
  const uint32_t key = signal.get_key();
  signal.wake_waiter();
  EXPECT_NE(key, signal.get_key());
  EXPECT_EQ(common::OB_SUCCESS, signal.wait(key, 1000));
}

TEST(TestAsyncIndexLogSignal, stop_releases_waiting_thread_while_disabled) {
  AsyncIndexLogSignal signal;
  const uint32_t key = signal.get_key();
  std::promise<void> started;
  auto started_future = started.get_future();
  auto result = std::async(std::launch::async, [&]() {
    started.set_value();
    return signal.wait(key, LONG_WAIT_US);
  });
  started_future.wait();
  EXPECT_EQ(std::future_status::timeout,
            result.wait_for(std::chrono::milliseconds(20)));
  signal.wake_waiter();
  const auto status = result.wait_for(std::chrono::seconds(2));
  EXPECT_EQ(std::future_status::ready, status);
  if (status == std::future_status::ready) {
    EXPECT_EQ(common::OB_SUCCESS, result.get());
  } else {
    signal.wake_waiter();
  }
}

TEST(TestAsyncIndexLogSignal,
     log_arriving_after_state_check_does_not_wait_for_timeout) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  std::atomic<bool> log_ready(false);
  std::promise<void> checked_empty;
  auto checked_future = checked_empty.get_future();
  std::promise<void> continue_wait;
  auto continue_future = continue_wait.get_future();

  auto result = std::async(std::launch::async, [&]() {
    // This order is the Fetcher protocol: sample the key before checking
    // whether the iterator has reached its current end.
    const uint32_t key = signal.get_key();
    const bool empty = !log_ready.load(std::memory_order_acquire);
    checked_empty.set_value();
    continue_future.wait();
    if (!empty) {
      return common::OB_ERR_UNEXPECTED;
    }
    return signal.wait(key, 1000);
  });

  checked_future.wait();
  // Hold the consumer exactly between its state check and its wait. The
  // notification must remain effective even though no thread is asleep yet.
  log_ready.store(true, std::memory_order_release);
  signal.notify_log_ready(true);
  continue_wait.set_value();
  EXPECT_EQ(common::OB_SUCCESS, result.get());
}

TEST(TestAsyncIndexLogCallback, copied_metadata_survives_source_reuse) {
  ObTxLogCb source;
  ObTxLogCb copy;
  EXPECT_FALSE(source.has_async_index());
  EXPECT_FALSE(copy.has_async_index());
  source.set_has_async_index(true);
  ASSERT_EQ(common::OB_SUCCESS, copy.copy(source));
  EXPECT_TRUE(copy.has_async_index());

  source.reuse();
  EXPECT_FALSE(source.has_async_index());
  EXPECT_TRUE(copy.has_async_index());
  copy.reset();
  EXPECT_FALSE(copy.has_async_index());
}

TEST(TestAsyncIndexLogCallback, copy_of_unrelated_log_clears_old_metadata) {
  ObTxLogCb source;
  ObTxLogCb copy;
  copy.set_has_async_index(true);
  ASSERT_EQ(common::OB_SUCCESS, copy.copy(source));
  EXPECT_FALSE(copy.has_async_index());
}

TEST(TestAsyncIndexLogCallback,
     reused_callback_does_not_notify_for_previous_log) {
  AsyncIndexLogSignal signal;
  signal.set_enabled(true);
  ObTxLogCb callback;
  callback.set_has_async_index(true);
  const uint32_t original_key = signal.get_key();
  signal.notify_log_ready(callback.has_async_index());
  EXPECT_NE(original_key, signal.get_key());

  callback.reuse();
  const uint32_t key = signal.get_key();
  signal.notify_log_ready(callback.has_async_index());
  EXPECT_EQ(key, signal.get_key());

  callback.set_has_async_index(true);
  callback.reset();
  signal.notify_log_ready(callback.has_async_index());
  EXPECT_EQ(key, signal.get_key());
}

} // namespace
} // namespace transaction
} // namespace oceanbase

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
