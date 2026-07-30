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

#define private public
#define protected public
#include "lib/ob_running_mode.h"
#include "lib/time/ob_time_utility.h"
#include "share/ob_background_task_executor.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/tx/ob_tx_timestamp_waiter.h"
#undef protected
#undef private

namespace oceanbase
{
namespace unittest
{

using namespace common;
using namespace share;
using namespace storage;
using namespace transaction;

namespace
{

template <typename Predicate>
bool wait_until(Predicate predicate, const int64_t timeout_ms)
{
  const int64_t deadline =
      ObTimeUtility::current_time() + timeout_ms * 1000;
  while (!predicate() && ObTimeUtility::current_time() < deadline) {
    ob_usleep(10 * 1000);
  }
  return predicate();
}

class MiniModeGuard
{
public:
  MiniModeGuard()
    : old_mini_mode_(ObRunningModeConfig::instance().mini_mode_)
  {
    ObRunningModeConfig::instance().mini_mode_ = true;
  }

  ~MiniModeGuard()
  {
    ObRunningModeConfig::instance().mini_mode_ = old_mini_mode_;
  }

private:
  bool old_mini_mode_;
};

class FakeTsMgr : public ObTsMgr
{
public:
  explicit FakeTsMgr(const int64_t gts)
    : gts_(gts),
      call_count_(0)
  {}

  virtual int get_gts(SCN &scn) override
  {
    ++call_count_;
    return scn.convert_for_gts(gts_.load());
  }

  void set_gts(const int64_t gts)
  {
    gts_.store(gts);
  }

  int64_t get_call_count() const
  {
    return call_count_.load();
  }

private:
  std::atomic<int64_t> gts_;
  std::atomic<int64_t> call_count_;
};

} // end anonymous namespace

TEST(TestTxTimestampWaiter, shared_executor_wait_and_dispatch)
{
  MiniModeGuard mini_mode_guard;
  ObBackgroundTaskExecutor executor;
  FakeTsMgr ts_mgr(100);
  ObTxTimestampWaiter waiter;

  ASSERT_EQ(OB_SUCCESS, executor.init(1));
  ASSERT_EQ(OB_SUCCESS, waiter.init(&ts_mgr, &executor));
  ASSERT_EQ(OB_SUCCESS, waiter.start());
  ASSERT_TRUE(waiter.share::ObThreadPool::has_set_stop());
  ASSERT_EQ(1, executor.get_registered_source_count());

  SCN target_scn;
  ASSERT_EQ(OB_SUCCESS, target_scn.convert_for_gts(200));
  ObTxData tx_data;
  ObTxCtx ctx;
  ctx.ctx_tx_data_.test_init(tx_data, NULL);
  ASSERT_EQ(OB_SUCCESS, ctx.ctx_tx_data_.set_commit_version(target_scn));

  bool need_wait = false;
  ASSERT_EQ(OB_SUCCESS,
      waiter.wait_gts_elapse(target_scn, &ctx, need_wait));
  ASSERT_TRUE(need_wait);
  ASSERT_TRUE(wait_until(
      [&ts_mgr]() { return ts_mgr.get_call_count() >= 2; },
      2000));
  EXPECT_EQ(1, waiter.wait_queue_.size());

  ts_mgr.set_gts(300);
  ASSERT_TRUE(wait_until(
      [&waiter]() { return 0 == waiter.wait_queue_.size(); },
      2000));

  waiter.stop();
  EXPECT_EQ(0, executor.get_registered_source_count());
  // The stack-backed test object has no production slice allocator.
  ctx.ctx_tx_data_.tx_data_guard_.tx_data_ = NULL;
  waiter.destroy();
  executor.destroy();
}

} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
