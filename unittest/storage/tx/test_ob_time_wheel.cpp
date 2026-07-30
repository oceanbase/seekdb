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

#include "lib/ob_running_mode.h"
#include "lib/time/ob_time_utility.h"
#include "share/ob_background_task_executor.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_time_wheel.h"

namespace oceanbase
{
namespace unittest
{

using namespace common;
using namespace share;

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
    : old_mini_mode_(lib::ObRunningModeConfig::instance().mini_mode_)
  {
    lib::ObRunningModeConfig::instance().mini_mode_ = true;
  }

  ~MiniModeGuard()
  {
    lib::ObRunningModeConfig::instance().mini_mode_ = old_mini_mode_;
  }

private:
  bool old_mini_mode_;
};

class BackgroundExecutorProvider : public ObIModuleProvider
{
public:
  explicit BackgroundExecutorProvider(ObBackgroundTaskExecutor *executor)
    : executor_(executor)
  {}

  virtual ObBackgroundTaskExecutor *background_task_executor() override
  {
    return executor_;
  }

private:
  ObBackgroundTaskExecutor *executor_;
};

class ModuleProviderGuard
{
public:
  explicit ModuleProviderGuard(ObIModuleProvider *provider)
    : old_provider_(g_mp)
  {
    g_mp = provider;
  }

  ~ModuleProviderGuard()
  {
    g_mp = old_provider_;
  }

private:
  ObIModuleProvider *old_provider_;
};

class CountTimeWheelTask : public ObTimeWheelTask
{
public:
  CountTimeWheelTask() : run_count_(0) {}

  virtual void runTimerTask() override
  {
    ++run_count_;
  }

  virtual uint64_t hash() const override
  {
    return 0;
  }

  int64_t get_run_count() const
  {
    return run_count_.load();
  }

private:
  std::atomic<int64_t> run_count_;
};

} // end anonymous namespace

TEST(TestObTimeWheel, mini_mode_uses_shared_executor)
{
  MiniModeGuard mini_mode_guard;
  ObBackgroundTaskExecutor executor;
  BackgroundExecutorProvider provider(&executor);
  ModuleProviderGuard provider_guard(&provider);
  ObTimeWheel time_wheel;
  CountTimeWheelTask fired_task;
  CountTimeWheelTask canceled_task;

  ASSERT_EQ(OB_SUCCESS, executor.init(2));
  ASSERT_EQ(OB_SUCCESS, time_wheel.init(10 * 1000, 1, "TestTimeWheel"));
  ASSERT_EQ(OB_SUCCESS, time_wheel.start());
  ASSERT_EQ(1, executor.get_registered_source_count());

  ASSERT_EQ(OB_SUCCESS, time_wheel.schedule(&fired_task, 50 * 1000));
  ASSERT_TRUE(wait_until(
      [&fired_task]() { return 1 == fired_task.get_run_count(); },
      2000));

  ASSERT_EQ(OB_SUCCESS, time_wheel.schedule(&canceled_task, 200 * 1000));
  ASSERT_EQ(OB_SUCCESS, time_wheel.cancel(&canceled_task));
  ob_usleep(300 * 1000);
  EXPECT_EQ(0, canceled_task.get_run_count());

  ASSERT_EQ(OB_SUCCESS, time_wheel.stop());
  ASSERT_EQ(OB_SUCCESS, time_wheel.wait());
  EXPECT_EQ(0, executor.get_registered_source_count());
  time_wheel.destroy();
  executor.destroy();
}

} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
