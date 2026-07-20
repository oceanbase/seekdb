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

#define USING_LOG_PREFIX STORAGE
#include <gtest/gtest.h>
#define protected public
#define private public

#include "src/storage/compaction/ob_compaction_schedule_iterator.h"
#include "src/storage/ob_i_store.h"

namespace oceanbase
{
using namespace share;
using namespace common;

namespace unittest
{

class MockObCompactionScheduleIterator : public compaction::ObCompactionScheduleIterator
{
public:
  MockObCompactionScheduleIterator()
    : ObCompactionScheduleIterator(true/*is_major*/),
      mock_tablet_cnt_(0),
      error_tablet_id_(),
      errno_(OB_SUCCESS),
      touch_counts_()
  {}

  int init(
      const int64_t max_batch_tablet_cnt,
      const int64_t tablet_cnt,
      const int64_t error_tablet_idx = -1,
      const int input_errno = OB_SUCCESS)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(max_batch_tablet_cnt <= 0 || tablet_cnt < 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), K(max_batch_tablet_cnt), K(tablet_cnt));
    } else {
      max_batch_tablet_cnt_ = max_batch_tablet_cnt;
      mock_tablet_cnt_ = tablet_cnt;
      error_tablet_id_ = ObTabletID(error_tablet_idx + 1);
      errno_ = input_errno;
      touch_counts_.reset();
      for (int64_t i = 0; OB_SUCC(ret) && i < mock_tablet_cnt_; ++i) {
        if (OB_FAIL(touch_counts_.push_back(0))) {
          LOG_WARN("failed to init touch count", KR(ret), K(i));
        }
      }
      ObLS *ls = nullptr;
      if (OB_SUCC(ret)
          && OB_FAIL(compaction::ObBasicMergeScheduleIterator::init(max_batch_tablet_cnt, ls))) {
        LOG_WARN("failed to init basic iterator", KR(ret));
      }
    }
    return ret;
  }

  virtual int get_tablet_ids() override
  {
    int ret = OB_SUCCESS;
    for (int64_t i = 0; OB_SUCC(ret) && i < mock_tablet_cnt_; ++i) {
      if (OB_FAIL(tablet_ids_.array_.push_back(ObTabletID(i + 1)))) {
        LOG_WARN("failed to push tablet id", KR(ret), K(i));
      }
    }
    return ret;
  }

  virtual int get_tablet_handle(const ObTabletID &tablet_id, ObTabletHandle &tablet_handle) override
  {
    int ret = OB_SUCCESS;
    UNUSED(tablet_handle);
    const int64_t idx = tablet_id.id() - 1;
    if (tablet_id == error_tablet_id_) {
      ret = errno_;
    } else if (OB_UNLIKELY(idx < 0 || idx >= touch_counts_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet id is out of range", KR(ret), K(tablet_id), K_(mock_tablet_cnt));
    } else {
      touch_counts_.at(idx)++;
    }
    return ret;
  }

  int check_touch_counts(const int64_t error_tablet_idx, const int input_errno)
  {
    int ret = OB_SUCCESS;
    for (int64_t i = 0; OB_SUCC(ret) && i < touch_counts_.count(); ++i) {
      const bool should_touch = OB_SUCCESS == input_errno
          || (OB_TABLET_NOT_EXIST == input_errno && i != error_tablet_idx)
          || (OB_TABLET_NOT_EXIST != input_errno && i < error_tablet_idx);
      const int64_t expected = should_touch ? 1 : 0;
      if (touch_counts_.at(i) != expected) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet touch count is unexpected", KR(ret), K(i),
                 "actual", touch_counts_.at(i), K(expected),
                 K(error_tablet_idx), K(input_errno));
      }
    }
    return ret;
  }

  int64_t mock_tablet_cnt_;
  ObTabletID error_tablet_id_;
  int errno_;
  ObArray<int64_t> touch_counts_;
};

class TestCompactionIter : public ::testing::Test
{
public:
  void test_iter(
      const int64_t max_batch_tablet_cnt,
      const int64_t tablet_cnt,
      const int64_t error_tablet_idx = -1,
      const int input_errno = OB_SUCCESS);
};

void TestCompactionIter::test_iter(
    const int64_t max_batch_tablet_cnt,
    const int64_t tablet_cnt,
    const int64_t error_tablet_idx,
    const int input_errno)
{
  LOG_INFO("test_iter", K(max_batch_tablet_cnt), K(tablet_cnt), K(error_tablet_idx), K(input_errno));
  MockObCompactionScheduleIterator iter;
  ASSERT_EQ(OB_SUCCESS, iter.init(max_batch_tablet_cnt, tablet_cnt, error_tablet_idx, input_errno));

  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  int64_t iter_batch_cnt = 0;
  int64_t iter_cnt = 0;
  while (OB_SUCC(ret)) {
    while (OB_SUCC(ret)) {
      if (OB_SUCC(iter.get_next_tablet(tablet_handle))) {
        iter_cnt++;
      } else {
        if (OB_ITER_END != ret) {
          iter.finish_scan();
        }
        ret = OB_SUCCESS;
        break;
      }
    }
    ++iter_batch_cnt;
    if (iter.is_valid()) {
      ASSERT_GE(iter.schedule_tablet_cnt_, max_batch_tablet_cnt);
      iter.start_cur_batch();
    } else {
      break;
    }
  }

  int64_t expect_iter_cnt = tablet_cnt;
  if (OB_TABLET_NOT_EXIST == input_errno) {
    expect_iter_cnt = tablet_cnt - (tablet_cnt > error_tablet_idx ? 1 : 0);
  } else if (OB_SUCCESS != input_errno) {
    expect_iter_cnt = MIN(tablet_cnt, error_tablet_idx);
  }
  ASSERT_EQ(iter_cnt, expect_iter_cnt);
  ASSERT_EQ(OB_SUCCESS, iter.check_touch_counts(error_tablet_idx, input_errno));
  ASSERT_EQ(iter_batch_cnt, MAX(1, iter_cnt / max_batch_tablet_cnt + (iter_cnt % max_batch_tablet_cnt != 0)));
}

TEST_F(TestCompactionIter, test_tablet_iteration_normal_loop)
{
  test_iter(10000, 0);
  test_iter(10000, 100);
  test_iter(1000, 1000);
  test_iter(1000, 10000);
  test_iter(100, 10000);
}

TEST_F(TestCompactionIter, test_tablet_iteration_skips_missing_tablet)
{
  test_iter(1000, 10000, 50, OB_TABLET_NOT_EXIST);
  test_iter(1000, 1001, 999, OB_TABLET_NOT_EXIST);
}

TEST_F(TestCompactionIter, test_tablet_iteration_stops_on_error)
{
  test_iter(1000, 10000, 50, OB_ERR_UNEXPECTED);
  test_iter(1000, 1000, 999, OB_ERR_UNEXPECTED);
  test_iter(1000, 999, 0, OB_ERR_UNEXPECTED);
  test_iter(1000, 50, 1, OB_ERR_UNEXPECTED);
}

TEST_F(TestCompactionIter, test_tablet_iteration_can_restart)
{
  MockObCompactionScheduleIterator iter;
  ObTabletHandle tablet_handle;
  ASSERT_EQ(OB_SUCCESS, iter.init(2, 3));
  ASSERT_EQ(OB_SUCCESS, iter.get_next_tablet(tablet_handle));
  ASSERT_EQ(OB_SUCCESS, iter.get_next_tablet(tablet_handle));
  iter.start_cur_batch();
  ASSERT_EQ(OB_SUCCESS, iter.get_next_tablet(tablet_handle));
  ASSERT_EQ(OB_ITER_END, iter.get_next_tablet(tablet_handle));
  ASSERT_TRUE(iter.is_scan_finish());

  ASSERT_EQ(OB_SUCCESS, iter.init(2, 3));
  ASSERT_FALSE(iter.is_scan_finish());
  ASSERT_EQ(OB_SUCCESS, iter.get_next_tablet(tablet_handle));
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_compaction_iter.log*");
  OB_LOGGER.set_file_name("test_compaction_iter.log", true);
  oceanbase::common::ObLogger::get_logger().set_log_level("TRACE");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
