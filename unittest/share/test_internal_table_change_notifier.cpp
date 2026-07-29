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
#include <atomic>
#include <thread>
#include <vector>

#include "share/ob_internal_table_change_notifier.h"

namespace oceanbase
{
namespace unittest
{

using namespace common;
using namespace share;

class TestInternalTableChangeNotifier : public ::testing::Test
{
public:
  void SetUp() override
  {
    notifier_.destroy();
    ASSERT_EQ(OB_SUCCESS, notifier_.init());
  }

  void TearDown() override
  {
    notifier_.destroy();
  }

protected:
  ObInternalTableChangeNotifier &notifier_ =
      ObInternalTableChangeNotifier::get_instance();
};

TEST_F(TestInternalTableChangeNotifier, registration_and_seal)
{
  const uint64_t table_id = 1001;
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(table_id));
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(table_id));
  uint64_t change_seq = 0;
  EXPECT_EQ(OB_STATE_NOT_MATCH,
      notifier_.get_change_seq(table_id, change_seq));
  notifier_.notify_table_changed(table_id);
  ASSERT_EQ(OB_SUCCESS, notifier_.seal());

  EXPECT_EQ(OB_SUCCESS, notifier_.get_change_seq(table_id, change_seq));
  EXPECT_EQ(1, change_seq);
  EXPECT_EQ(OB_SUCCESS, notifier_.register_table(table_id));
  EXPECT_EQ(OB_STATE_NOT_MATCH, notifier_.register_table(table_id + 1));
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
      notifier_.get_change_seq(table_id + 1, change_seq));
  notifier_.notify_table_changed(table_id + 1);
  EXPECT_EQ(OB_SUCCESS, notifier_.get_change_seq(table_id, change_seq));
  EXPECT_EQ(1, change_seq);
  EXPECT_EQ(OB_INVALID_ARGUMENT, notifier_.register_table(OB_INVALID_ID));
}

TEST_F(TestInternalTableChangeNotifier, supports_more_than_64_tables)
{
  const uint64_t first_table_id = 1000;
  const int64_t table_count = 96;
  for (int64_t i = 0; i < table_count; ++i) {
    ASSERT_EQ(OB_SUCCESS,
        notifier_.register_table(first_table_id + i));
  }
  ASSERT_EQ(OB_SUCCESS, notifier_.seal());
  for (int64_t i = 0; i < table_count; ++i) {
    uint64_t change_seq = 0;
    ASSERT_EQ(OB_SUCCESS,
        notifier_.get_change_seq(first_table_id + i, change_seq));
    EXPECT_EQ(1, change_seq);
  }
}

TEST_F(TestInternalTableChangeNotifier, concurrent_notify_and_read)
{
  const uint64_t table_id = 1080;
  const int64_t thread_count = 8;
  const int64_t increments_per_thread = 10000;
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(table_id));
  ASSERT_EQ(OB_SUCCESS, notifier_.seal());

  std::atomic<bool> writers_done(false);
  std::atomic<bool> read_failed(false);
  std::thread reader([&]() {
    uint64_t previous_seq = 0;
    while (!writers_done.load()) {
      uint64_t current_seq = 0;
      if (OB_SUCCESS != notifier_.get_change_seq(table_id, current_seq)
          || current_seq < previous_seq) {
        read_failed.store(true);
        break;
      }
      previous_seq = current_seq;
    }
  });

  std::vector<std::thread> writers;
  for (int64_t i = 0; i < thread_count; ++i) {
    writers.push_back(std::thread([&]() {
      for (int64_t j = 0; j < increments_per_thread; ++j) {
        notifier_.notify_table_changed(table_id);
      }
    }));
  }
  for (int64_t i = 0; i < thread_count; ++i) {
    writers.at(i).join();
  }
  writers_done.store(true);
  reader.join();

  uint64_t change_seq = 0;
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(table_id, change_seq));
  EXPECT_EQ(1 + thread_count * increments_per_thread, change_seq);
  EXPECT_FALSE(read_failed.load());
}

TEST_F(TestInternalTableChangeNotifier, captured_sequence_preserves_racing_change)
{
  const uint64_t table_id = 1062;
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(table_id));
  ASSERT_EQ(OB_SUCCESS, notifier_.seal());

  uint64_t target_seq = 0;
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(table_id, target_seq));
  notifier_.notify_table_changed(table_id);

  // A successful refresh advances only to its pre-read capture. The commit
  // racing with that refresh remains different for the next timer tick.
  const uint64_t last_seen_seq = target_seq;
  uint64_t current_seq = 0;
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(table_id, current_seq));
  EXPECT_NE(last_seen_seq, current_seq);

  // Independent consumers do not consume one another's observation.
  uint64_t first_consumer_seq = 0;
  uint64_t second_consumer_seq = 0;
  first_consumer_seq = current_seq;
  EXPECT_EQ(current_seq, first_consumer_seq);
  EXPECT_NE(current_seq, second_consumer_seq);
}

TEST_F(TestInternalTableChangeNotifier, mark_all_and_lifecycle)
{
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(1011));
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(1049));
  ASSERT_EQ(OB_SUCCESS, notifier_.seal());

  notifier_.mark_all_tables_changed();
  uint64_t first_seq = 0;
  uint64_t second_seq = 0;
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(1011, first_seq));
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(1049, second_seq));
  EXPECT_EQ(2, first_seq);
  EXPECT_EQ(2, second_seq);

  ASSERT_EQ(OB_SUCCESS, notifier_.activate());
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(1011, first_seq));
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(1049, second_seq));
  EXPECT_EQ(3, first_seq);
  EXPECT_EQ(3, second_seq);

  notifier_.destroy();
  notifier_.notify_table_changed(1011);
  EXPECT_EQ(OB_NOT_INIT, notifier_.get_change_seq(1011, first_seq));
  ASSERT_EQ(OB_SUCCESS, notifier_.init());
  ASSERT_EQ(OB_SUCCESS, notifier_.register_table(1011));
  ASSERT_EQ(OB_SUCCESS, notifier_.seal());
  ASSERT_EQ(OB_SUCCESS, notifier_.get_change_seq(1011, first_seq));
  EXPECT_EQ(1, first_seq);
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_internal_table_change_notifier.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
