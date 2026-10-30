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
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include "observer/change_stream/ob_change_stream_fetcher.h"
#include "logservice/palf/palf_callback_wrapper.h"
#include "logservice/palf/palf_env.h"

namespace oceanbase
{
namespace share
{
using namespace common;

class TestCSFetcherWake : public ::testing::Test
{
protected:
  TestCSFetcherWake() : callback_(&fetcher_), registered_(false) {}

  void SetUp() override
  {
    fetcher_.has_set_stop() = false;
    ASSERT_EQ(OB_SUCCESS, storage_.init(palf::LSN(0)));
    ASSERT_EQ(OB_SUCCESS, fetcher_.iter_.init(palf::LSN(0), [this]() {
      const palf::LSN end_lsn = storage_.get_log_tail();
      if (on_read_end_) {
        on_read_end_();
      }
      return end_lsn;
    }, &storage_));
    ASSERT_EQ(OB_SUCCESS, callbacks_.add_cb_impl(&callback_));
    registered_ = true;
  }

  void TearDown() override
  {
    detach_callback();
    fetcher_.unbind_log_();
  }

  int next() { return fetcher_.next_log_(); }
  int notify() { return callbacks_.update_end_lsn(palf::LSN(0), SCN::min_scn()); }
  uint32_t wake_count() { return fetcher_.log_ready_.get_key(); }
  void stop() { fetcher_.stop(); }

  void detach_callback()
  {
    if (registered_) {
      callbacks_.del_cb_impl(&callback_);
      registered_ = false;
    }
  }

  int64_t next_elapsed_us(const int expected)
  {
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_EQ(expected, next());
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();
  }

  void append_entry()
  {
    const char payload[] = "async row";
    palf::LogEntryHeader header;
    ASSERT_EQ(OB_SUCCESS, header.generate_header(payload, sizeof(payload), SCN::base_scn()));
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, header.serialize(entry_buf_, sizeof(entry_buf_), pos));
    MEMCPY(entry_buf_ + pos, payload, sizeof(payload));
    ASSERT_EQ(OB_SUCCESS, storage_.append(entry_buf_, pos + sizeof(payload)));
  }

  void expect_entry()
  {
    palf::LogEntry entry;
    palf::LSN lsn;
    ASSERT_EQ(OB_SUCCESS, fetcher_.iter_.get_entry(entry, lsn));
    EXPECT_EQ(palf::LSN(0), lsn);
    EXPECT_STREQ("async row", entry.get_data_buf());
  }

  void expect_failed_binding_cleanup()
  {
    palf::PalfEnv unavailable_env;
    fetcher_.palf_env_ = &unavailable_env;
    EXPECT_EQ(OB_ENTRY_NOT_EXIST, fetcher_.bind_log_());
    fetcher_.unbind_log_();
    fetcher_.unbind_log_();
    EXPECT_FALSE(fetcher_.read_handle_.is_valid());
    EXPECT_FALSE(fetcher_.iter_.is_inited());
    fetcher_.palf_env_ = nullptr;
  }

  palf::MemoryStorage storage_;
  ObCSFetcher fetcher_;
  palf::PalfFSCbWrapper callbacks_;
  palf::PalfFSCbNode callback_;
  bool registered_;
  std::function<void()> on_read_end_;
  char entry_buf_[256];
};

TEST_F(TestCSFetcherWake, notification_between_read_and_wait_is_not_lost)
{
  on_read_end_ = [this]() { EXPECT_EQ(OB_SUCCESS, notify()); };
  EXPECT_LT(next_elapsed_us(OB_ITER_END), 150000);
}

TEST_F(TestCSFetcherWake, stale_notification_before_next_attempt_does_not_hide_fresh_data)
{
  on_read_end_ = [this]() { EXPECT_EQ(OB_SUCCESS, notify()); };
  EXPECT_LT(next_elapsed_us(OB_ITER_END), 150000);
  EXPECT_EQ(OB_SUCCESS, notify());
  bool published = false;
  on_read_end_ = [this, &published]() {
    if (!published) {
      published = true;
      append_entry();
      EXPECT_EQ(OB_SUCCESS, notify());
    }
  };
  EXPECT_LT(next_elapsed_us(OB_ITER_END), 150000);
  ASSERT_EQ(OB_SUCCESS, next());
  expect_entry();
}

TEST_F(TestCSFetcherWake, concurrent_notifications_do_not_lose_a_read_attempt_wake)
{
  const int read_attempts = 1000000;
  std::atomic<bool> done(false);
  std::promise<void> started;
  auto notifier_started = started.get_future();
  auto notifier = std::async(std::launch::async, [this, &done, &started]() {
    started.set_value();
    int ret = OB_SUCCESS;
    while (!done.load(std::memory_order_relaxed) && OB_SUCCESS == ret) {
      ret = notify();
    }
    return ret;
  });
  const auto ready = notifier_started.wait_for(std::chrono::seconds(1));
  int attempts = 0;
  int ret = OB_SUCCESS;
  int64_t longest_wait_us = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::future_status::ready == ready && attempts < read_attempts &&
         longest_wait_us < 150000 && std::chrono::steady_clock::now() < deadline) {
    const auto begin = std::chrono::steady_clock::now();
    ret = next();
    const int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();
    longest_wait_us = std::max(longest_wait_us, elapsed_us);
    ++attempts;
    if (OB_ITER_END != ret) {
      break;
    }
  }
  done.store(true, std::memory_order_relaxed);
  EXPECT_EQ(OB_SUCCESS, notifier.get());
  EXPECT_EQ(std::future_status::ready, ready);
  EXPECT_EQ(OB_ITER_END, ret);
  EXPECT_LT(longest_wait_us, 150000) << "read attempt " << attempts;
  EXPECT_EQ(read_attempts, attempts);
}

TEST_F(TestCSFetcherWake, burst_notifications_produce_one_wake_per_read_attempt)
{
  on_read_end_ = [this]() {
    for (int i = 0; i < 1000; ++i) {
      EXPECT_EQ(OB_SUCCESS, notify());
    }
  };
  const uint32_t before = wake_count();
  EXPECT_LT(next_elapsed_us(OB_ITER_END), 150000);
  EXPECT_EQ(before + 1, wake_count());
  EXPECT_LT(next_elapsed_us(OB_ITER_END), 150000);
  EXPECT_EQ(before + 2, wake_count());
}

TEST_F(TestCSFetcherWake, readable_data_does_not_need_a_notification)
{
  append_entry();
  ASSERT_EQ(OB_SUCCESS, next());
  expect_entry();
  const uint32_t before = wake_count();
  EXPECT_EQ(OB_SUCCESS, notify());
  EXPECT_EQ(before, wake_count());
}

TEST_F(TestCSFetcherWake, empty_log_keeps_the_maintenance_timeout)
{
  EXPECT_GE(next_elapsed_us(OB_ITER_END), 180000);
}

TEST_F(TestCSFetcherWake, notification_wakes_a_waiting_reader)
{
  std::promise<void> read;
  auto read_started = read.get_future();
  on_read_end_ = [&read]() { read.set_value(); };
  auto result = std::async(std::launch::async, [this]() { return next(); });
  ASSERT_EQ(std::future_status::ready, read_started.wait_for(std::chrono::seconds(1)));
  EXPECT_EQ(OB_SUCCESS, notify());
  EXPECT_EQ(std::future_status::ready, result.wait_for(std::chrono::milliseconds(150)));
  EXPECT_EQ(OB_ITER_END, result.get());
}

TEST_F(TestCSFetcherWake, stop_before_read_does_not_enter_the_wait)
{
  on_read_end_ = []() { ADD_FAILURE() << "iterator read after stop"; };
  stop();
  EXPECT_LT(next_elapsed_us(OB_CANCELED), 150000);
}

TEST_F(TestCSFetcherWake, stop_during_read_does_not_enter_the_wait)
{
  on_read_end_ = [this]() { stop(); };
  EXPECT_LT(next_elapsed_us(OB_ITER_END), 150000);
}

TEST_F(TestCSFetcherWake, stop_wakes_a_waiting_reader)
{
  std::promise<void> read;
  auto read_started = read.get_future();
  on_read_end_ = [&read]() { read.set_value(); };
  auto result = std::async(std::launch::async, [this]() { return next(); });
  ASSERT_EQ(std::future_status::ready, read_started.wait_for(std::chrono::seconds(1)));
  stop();
  EXPECT_EQ(std::future_status::ready, result.wait_for(std::chrono::milliseconds(150)));
  EXPECT_EQ(OB_ITER_END, result.get());
}

TEST_F(TestCSFetcherWake, detached_callback_preserves_timed_polling)
{
  detach_callback();
  on_read_end_ = [this]() { EXPECT_EQ(OB_SUCCESS, notify()); };
  const uint32_t before = wake_count();
  EXPECT_GE(next_elapsed_us(OB_ITER_END), 180000);
  EXPECT_EQ(before, wake_count());
}

TEST_F(TestCSFetcherWake, failed_open_leaves_no_binding_and_cleanup_is_repeatable)
{
  expect_failed_binding_cleanup();
}

}
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
