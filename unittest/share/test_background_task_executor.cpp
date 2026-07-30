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

#define USING_LOG_PREFIX SHARE
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "lib/time/ob_time_utility.h"
#include "lib/thread/ob_async_task_queue.h"
#include "share/ob_background_task_executor.h"

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

class ExecutionRecorder
{
public:
  void record(const int64_t source_id)
  {
    std::lock_guard<std::mutex> guard(lock_);
    order_.push_back(source_id);
  }

  std::vector<int64_t> get_order() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return order_;
  }

private:
  mutable std::mutex lock_;
  std::vector<int64_t> order_;
};

class DemoTaskSource : public ObIBackgroundTaskSource
{
public:
  struct Task
  {
    Task(const int64_t task_id, const int64_t sleep_us)
      : task_id_(task_id), sleep_us_(sleep_us)
    {}

    int64_t task_id_;
    int64_t sleep_us_;
  };

  explicit DemoTaskSource(
      const int64_t source_id,
      ExecutionRecorder *recorder = NULL)
    : source_id_(source_id),
      recorder_(recorder),
      next_task_id_(0),
      completed_count_(0),
      active_count_(0),
      max_active_count_(0),
      quantum_count_(0),
      fail_next_quantum_(false),
      pause_before_first_return_(false),
      first_return_paused_(false),
      release_first_return_(false)
  {}

  void enqueue(
      const ObBackgroundTaskPriority priority,
      const int64_t task_count,
      const int64_t sleep_us)
  {
    std::lock_guard<std::mutex> guard(lock_);
    for (int64_t i = 0; i < task_count; ++i) {
      queues_[priority].push_back(Task(++next_task_id_, sleep_us));
    }
  }

  void pause_before_first_return()
  {
    std::lock_guard<std::mutex> guard(lock_);
    pause_before_first_return_ = true;
  }

  void fail_next_quantum()
  {
    std::lock_guard<std::mutex> guard(lock_);
    fail_next_quantum_ = true;
  }

  bool wait_first_return_paused(const int64_t timeout_ms)
  {
    std::unique_lock<std::mutex> guard(lock_);
    return cond_.wait_for(
        guard,
        std::chrono::milliseconds(timeout_ms),
        [this]() { return first_return_paused_; });
  }

  void release_first_return()
  {
    std::lock_guard<std::mutex> guard(lock_);
    release_first_return_ = true;
    cond_.notify_all();
  }

  bool wait_completed(
      const int64_t expected_count,
      const int64_t timeout_ms)
  {
    std::unique_lock<std::mutex> guard(lock_);
    return cond_.wait_for(
        guard,
        std::chrono::milliseconds(timeout_ms),
        [this, expected_count]() {
          return completed_count_ >= expected_count;
        });
  }

  int64_t get_completed_count() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return completed_count_;
  }

  int64_t get_max_active_count() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return max_active_count_;
  }

  int64_t get_quantum_count() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return quantum_count_;
  }

  std::set<std::thread::id> get_worker_ids() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return worker_ids_;
  }

  virtual int process_one_quantum(
      const ObBackgroundTaskPriority priority,
      ObBackgroundTaskRunResult &result) override
  {
    int ret = OB_SUCCESS;
    Task task(0, 0);
    bool has_task = false;
    bool fail_quantum = false;
    {
      std::lock_guard<std::mutex> guard(lock_);
      ++quantum_count_;
      if (fail_next_quantum_) {
        fail_next_quantum_ = false;
        fail_quantum = true;
        ret = OB_EAGAIN;
      } else if (!queues_[priority].empty()) {
        task = queues_[priority].front();
        queues_[priority].pop_front();
        has_task = true;
        ++active_count_;
        max_active_count_ = std::max(max_active_count_, active_count_);
      }
      cond_.notify_all();
    }

    if (fail_quantum) {
      // The task was not claimed. The executor must retain readiness without
      // spinning until the source publishes a retry notification.
    } else if (has_task) {
      if (task.sleep_us_ > 0) {
        ob_usleep(task.sleep_us_);
      }

      std::unique_lock<std::mutex> guard(lock_);
      --active_count_;
      ++completed_count_;
      worker_ids_.insert(std::this_thread::get_id());
      if (NULL != recorder_) {
        recorder_->record(source_id_);
      }
      result.processed_count_ = 1;
      result.has_more_ready_ = !queues_[priority].empty();
      cond_.notify_all();

      // Deliberately return a stale "no more work" result. A producer can
      // enqueue and notify while this wait releases source lock. Executor's
      // notify epoch must prevent that notification from being cleared.
      if (pause_before_first_return_ && 1 == completed_count_) {
        first_return_paused_ = true;
        cond_.notify_all();
        cond_.wait(guard, [this]() { return release_first_return_; });
      }
    } else {
      std::lock_guard<std::mutex> guard(lock_);
      result.has_more_ready_ = !queues_[priority].empty();
    }
    return ret;
  }

private:
  int64_t source_id_;
  ExecutionRecorder *recorder_;
  mutable std::mutex lock_;
  std::condition_variable cond_;
  std::deque<Task> queues_[BG_TASK_PRIORITY_COUNT];
  int64_t next_task_id_;
  int64_t completed_count_;
  int64_t active_count_;
  int64_t max_active_count_;
  int64_t quantum_count_;
  bool fail_next_quantum_;
  bool pause_before_first_return_;
  bool first_return_paused_;
  bool release_first_return_;
  std::set<std::thread::id> worker_ids_;
};

class DelayedTaskSource : public ObIBackgroundTaskSource
{
public:
  explicit DelayedTaskSource(const int64_t delay_us)
    : delay_us_(delay_us),
      quantum_count_(0),
      first_quantum_ts_(0),
      second_quantum_ts_(0)
  {}

  virtual int process_one_quantum(
      const ObBackgroundTaskPriority priority,
      ObBackgroundTaskRunResult &result) override
  {
    UNUSED(priority);
    std::lock_guard<std::mutex> guard(lock_);
    ++quantum_count_;
    if (1 == quantum_count_) {
      first_quantum_ts_ = ObTimeUtility::current_time();
      result.next_ready_ts_ = first_quantum_ts_ + delay_us_;
    } else if (2 == quantum_count_) {
      second_quantum_ts_ = ObTimeUtility::current_time();
      result.processed_count_ = 1;
    }
    cond_.notify_all();
    return OB_SUCCESS;
  }

  bool wait_quantum_count(
      const int64_t expected_count,
      const int64_t timeout_ms)
  {
    std::unique_lock<std::mutex> guard(lock_);
    return cond_.wait_for(
        guard,
        std::chrono::milliseconds(timeout_ms),
        [this, expected_count]() {
          return quantum_count_ >= expected_count;
        });
  }

  int64_t get_quantum_count() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return quantum_count_;
  }

  int64_t get_elapsed_us() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return second_quantum_ts_ - first_quantum_ts_;
  }

private:
  int64_t delay_us_;
  mutable std::mutex lock_;
  std::condition_variable cond_;
  int64_t quantum_count_;
  int64_t first_quantum_ts_;
  int64_t second_quantum_ts_;
};

class RetryAsyncTask : public ObAsyncTask
{
public:
  RetryAsyncTask(
      std::atomic<int64_t> *attempt_count,
      const int64_t failure_count)
    : attempt_count_(attempt_count),
      failure_count_(failure_count)
  {}

  virtual int process() override
  {
    const int64_t attempt = attempt_count_->fetch_add(1) + 1;
    return attempt <= failure_count_ ? OB_EAGAIN : OB_SUCCESS;
  }

  virtual int64_t get_deep_copy_size() const override
  {
    return sizeof(*this);
  }

  virtual ObAsyncTask *deep_copy(
      char *buf,
      const int64_t buf_size) const override
  {
    return NULL == buf || buf_size < static_cast<int64_t>(sizeof(*this))
        ? NULL
        : new (buf) RetryAsyncTask(attempt_count_, failure_count_);
  }

private:
  std::atomic<int64_t> *attempt_count_;
  int64_t failure_count_;
};

ObBackgroundTaskSourceConfig make_config(
    const char *name,
    const int64_t max_concurrency)
{
  ObBackgroundTaskSourceConfig config;
  config.name_ = name;
  config.max_concurrency_ = max_concurrency;
  for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
    config.max_concurrency_by_priority_[i] = max_concurrency;
  }
  return config;
}

} // end anonymous namespace

TEST(TestBackgroundTaskExecutor, three_sources_share_two_workers_and_shrink_to_warm_floor)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(2));

  DemoTaskSource source_a(1);
  DemoTaskSource source_b(2);
  DemoTaskSource source_c(3);
  ObBackgroundTaskSourceHandle handle_a;
  ObBackgroundTaskSourceHandle handle_b;
  ObBackgroundTaskSourceHandle handle_c;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_a, make_config("source_a", 1), handle_a));
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_b, make_config("source_b", 1), handle_b));
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_c, make_config("source_c", 1), handle_c));
  ASSERT_EQ(3, executor.get_registered_source_count());

  source_a.enqueue(BG_TASK_NORMAL, 6, 20 * 1000);
  source_b.enqueue(BG_TASK_NORMAL, 6, 20 * 1000);
  source_c.enqueue(BG_TASK_NORMAL, 6, 20 * 1000);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_a, BG_TASK_NORMAL));
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_b, BG_TASK_NORMAL));
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_c, BG_TASK_NORMAL));

  ASSERT_TRUE(wait_until(
      [&executor]() { return 2 == executor.get_worker_count(); }, 1000));
  ASSERT_TRUE(source_a.wait_completed(6, 5000));
  ASSERT_TRUE(source_b.wait_completed(6, 5000));
  ASSERT_TRUE(source_c.wait_completed(6, 5000));
  EXPECT_EQ(1, source_a.get_max_active_count());
  EXPECT_EQ(1, source_b.get_max_active_count());
  EXPECT_EQ(1, source_c.get_max_active_count());

  std::set<std::thread::id> worker_ids = source_a.get_worker_ids();
  const std::set<std::thread::id> worker_ids_b = source_b.get_worker_ids();
  const std::set<std::thread::id> worker_ids_c = source_c.get_worker_ids();
  worker_ids.insert(worker_ids_b.begin(), worker_ids_b.end());
  worker_ids.insert(worker_ids_c.begin(), worker_ids_c.end());
  EXPECT_LE(worker_ids.size(), 2);

  ASSERT_TRUE(wait_until(
      [&executor]() { return 1 == executor.get_worker_count(); }, 6000));
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle_a));
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle_b));
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle_c));
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, configured_warm_floor_retains_high_water_workers)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(2, 2));

  DemoTaskSource source_a(1);
  DemoTaskSource source_b(2);
  ObBackgroundTaskSourceHandle handle_a;
  ObBackgroundTaskSourceHandle handle_b;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_a, make_config("source_a", 1), handle_a));
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_b, make_config("source_b", 1), handle_b));

  source_a.enqueue(BG_TASK_NORMAL, 1, 200 * 1000);
  source_b.enqueue(BG_TASK_NORMAL, 1, 200 * 1000);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_a, BG_TASK_NORMAL));
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_b, BG_TASK_NORMAL));
  ASSERT_TRUE(wait_until(
      [&executor]() { return 2 == executor.get_worker_count(); }, 1000));
  ASSERT_TRUE(source_a.wait_completed(1, 2000));
  ASSERT_TRUE(source_b.wait_completed(1, 2000));

  // Two queue-pop timeouts are enough for the default shrink path to run.
  // The configured warm floor must keep both already-created workers.
  ob_usleep(2500 * 1000);
  EXPECT_EQ(2, executor.get_worker_count());

  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle_a));
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle_b));
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, source_capacity)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  std::vector<std::unique_ptr<DemoTaskSource> > sources;
  std::vector<ObBackgroundTaskSourceHandle> handles(
      ObBackgroundTaskExecutor::MAX_SOURCE_COUNT);
  for (int64_t i = 0;
      i < ObBackgroundTaskExecutor::MAX_SOURCE_COUNT;
      ++i) {
    sources.push_back(std::make_unique<DemoTaskSource>(i));
    ASSERT_EQ(OB_SUCCESS,
        executor.register_source(
            *sources.back(),
            make_config("capacity_source", 1),
            handles[i]));
  }
  EXPECT_EQ(ObBackgroundTaskExecutor::MAX_SOURCE_COUNT,
      executor.get_registered_source_count());

  DemoTaskSource overflow_source(
      ObBackgroundTaskExecutor::MAX_SOURCE_COUNT);
  ObBackgroundTaskSourceHandle overflow_handle;
  EXPECT_EQ(OB_SIZE_OVERFLOW,
      executor.register_source(
          overflow_source,
          make_config("overflow_source", 1),
          overflow_handle));

  for (int64_t i = 0;
      i < ObBackgroundTaskExecutor::MAX_SOURCE_COUNT;
      ++i) {
    ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handles[i]));
  }
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, notify_epoch_prevents_lost_wakeup)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  DemoTaskSource source(1);
  source.pause_before_first_return();
  ObBackgroundTaskSourceHandle handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source, make_config("source", 1), handle));

  source.enqueue(BG_TASK_NORMAL, 1, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(source.wait_first_return_paused(2000));

  source.enqueue(BG_TASK_NORMAL, 1, 0);
  for (int64_t i = 0; i < 16; ++i) {
    ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  }
  source.release_first_return();

  ASSERT_TRUE(source.wait_completed(2, 2000));
  EXPECT_EQ(2, source.get_completed_count());
  EXPECT_EQ(2, source.get_quantum_count());
  ASSERT_TRUE(wait_until(
      [&executor]() { return 1 == executor.get_worker_count(); }, 6000));
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle));
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, source_concurrency_is_bounded)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(3));

  DemoTaskSource source(1);
  ObBackgroundTaskSourceHandle handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source, make_config("source", 2), handle));

  source.enqueue(BG_TASK_NORMAL, 6, 50 * 1000);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(source.wait_completed(6, 3000));
  EXPECT_EQ(2, source.get_max_active_count());
  EXPECT_LE(executor.get_worker_count(), 2);
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, one_ready_does_not_expand_to_source_limit)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(4));

  DemoTaskSource source(1);
  ObBackgroundTaskSourceHandle handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source, make_config("source", 4), handle));

  source.enqueue(BG_TASK_NORMAL, 1, 300 * 1000);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(wait_until(
      [&source]() { return 1 == source.get_max_active_count(); }, 1000));
  ob_usleep(50 * 1000);
  EXPECT_EQ(1, executor.get_worker_count());
  ASSERT_TRUE(source.wait_completed(1, 1000));
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, failed_quantum_waits_for_retry_notification)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(2));

  DemoTaskSource source(1);
  ObBackgroundTaskSourceHandle handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source, make_config("source", 2), handle));

  source.enqueue(BG_TASK_NORMAL, 1, 0);
  source.fail_next_quantum();
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(wait_until(
      [&source]() { return 1 == source.get_quantum_count(); }, 1000));
  ob_usleep(100 * 1000);
  EXPECT_EQ(0, source.get_completed_count());
  EXPECT_EQ(1, source.get_quantum_count());

  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(source.wait_completed(1, 1000));
  EXPECT_EQ(2, source.get_quantum_count());
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, executor_can_restart_after_workers_stop)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  DemoTaskSource first_source(1);
  ObBackgroundTaskSourceHandle first_handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(
          first_source, make_config("first_source", 1), first_handle));
  first_source.enqueue(BG_TASK_NORMAL, 1, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(first_handle, BG_TASK_NORMAL));
  ASSERT_TRUE(first_source.wait_completed(1, 1000));
  executor.destroy();
  EXPECT_EQ(0, executor.get_worker_count());

  ASSERT_EQ(OB_SUCCESS, executor.init(1));
  DemoTaskSource second_source(2);
  ObBackgroundTaskSourceHandle second_handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(
          second_source, make_config("second_source", 1), second_handle));
  second_source.enqueue(BG_TASK_NORMAL, 1, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(second_handle, BG_TASK_NORMAL));
  ASSERT_TRUE(second_source.wait_completed(1, 1000));
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, unregister_rejects_new_notify_until_running_finishes)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  DemoTaskSource source(1);
  source.pause_before_first_return();
  ObBackgroundTaskSourceHandle handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source, make_config("source", 1), handle));

  source.enqueue(BG_TASK_NORMAL, 1, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(source.wait_first_return_paused(1000));
  EXPECT_EQ(OB_EAGAIN, executor.unregister_source(handle));
  EXPECT_EQ(OB_IN_STOP_STATE,
      executor.notify(handle, BG_TASK_NORMAL));
  source.release_first_return();

  int unregister_ret = OB_EAGAIN;
  const int64_t deadline = ObTimeUtility::current_time() + 1000 * 1000;
  while (OB_EAGAIN == unregister_ret
      && ObTimeUtility::current_time() < deadline) {
    ob_usleep(10 * 1000);
    unregister_ret = executor.unregister_source(handle);
  }
  EXPECT_EQ(OB_SUCCESS, unregister_ret);
  EXPECT_FALSE(handle.is_valid());
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, same_priority_sources_are_round_robin)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  ExecutionRecorder recorder;
  DemoTaskSource source_a(1, &recorder);
  DemoTaskSource source_b(2, &recorder);
  source_a.pause_before_first_return();
  ObBackgroundTaskSourceHandle handle_a;
  ObBackgroundTaskSourceHandle handle_b;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_a, make_config("source_a", 1), handle_a));
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_b, make_config("source_b", 1), handle_b));

  source_a.enqueue(BG_TASK_NORMAL, 3, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_a, BG_TASK_NORMAL));
  ASSERT_TRUE(source_a.wait_first_return_paused(2000));
  source_b.enqueue(BG_TASK_NORMAL, 3, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle_b, BG_TASK_NORMAL));
  source_a.release_first_return();

  ASSERT_TRUE(source_a.wait_completed(3, 2000));
  ASSERT_TRUE(source_b.wait_completed(3, 2000));
  const std::vector<int64_t> order = recorder.get_order();
  ASSERT_EQ(6, order.size());
  for (int64_t i = 0; i < 6; ++i) {
    EXPECT_EQ(1 + i % 2, order.at(i));
  }
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, weighted_priority_does_not_starve_low)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  ExecutionRecorder recorder;
  DemoTaskSource high_source(1, &recorder);
  DemoTaskSource low_source(2, &recorder);
  high_source.pause_before_first_return();
  ObBackgroundTaskSourceHandle high_handle;
  ObBackgroundTaskSourceHandle low_handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(
          high_source, make_config("high_source", 1), high_handle));
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(
          low_source, make_config("low_source", 1), low_handle));

  high_source.enqueue(BG_TASK_HIGH, 20, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(high_handle, BG_TASK_HIGH));
  ASSERT_TRUE(high_source.wait_first_return_paused(2000));
  low_source.enqueue(BG_TASK_LOW, 1, 0);
  ASSERT_EQ(OB_SUCCESS, executor.notify(low_handle, BG_TASK_LOW));
  high_source.release_first_return();

  ASSERT_TRUE(low_source.wait_completed(1, 2000));
  ASSERT_TRUE(high_source.wait_completed(20, 2000));
  const std::vector<int64_t> order = recorder.get_order();
  const std::vector<int64_t>::const_iterator low_pos =
      std::find(order.begin(), order.end(), 2);
  ASSERT_NE(order.end(), low_pos);
  EXPECT_LE(std::distance(order.begin(), low_pos), 8);
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, stale_handle_cannot_notify_reused_slot)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  DemoTaskSource source_a(1);
  DemoTaskSource source_b(2);
  ObBackgroundTaskSourceHandle handle_a;
  ObBackgroundTaskSourceHandle handle_b;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_a, make_config("source_a", 1), handle_a));
  const ObBackgroundTaskSourceHandle stale_handle = handle_a;
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle_a));
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source_b, make_config("source_b", 1), handle_b));
  EXPECT_EQ(stale_handle.slot_id_, handle_b.slot_id_);
  EXPECT_NE(stale_handle.generation_, handle_b.generation_);
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
      executor.notify(stale_handle, BG_TASK_NORMAL));
  EXPECT_EQ(0, executor.get_worker_count());
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, delayed_notification_does_not_hot_loop)
{
  ObBackgroundTaskExecutor executor;
  ASSERT_EQ(OB_SUCCESS, executor.init(1));

  const int64_t delay_us = 200 * 1000;
  DelayedTaskSource source(delay_us);
  ObBackgroundTaskSourceHandle handle;
  ASSERT_EQ(OB_SUCCESS,
      executor.register_source(source, make_config("delayed_source", 1), handle));
  ASSERT_EQ(OB_SUCCESS, executor.notify(handle, BG_TASK_NORMAL));
  ASSERT_TRUE(source.wait_quantum_count(1, 1000));

  ob_usleep(100 * 1000);
  EXPECT_EQ(1, source.get_quantum_count());
  ASSERT_TRUE(source.wait_quantum_count(2, 1000));
  EXPECT_GE(source.get_elapsed_us(), delay_us);
  ASSERT_EQ(OB_SUCCESS, executor.unregister_source(handle));
  executor.destroy();
}

TEST(TestBackgroundTaskExecutor, external_async_queue_preserves_retry_delay)
{
  ObAsyncTaskQueue queue;
  ASSERT_EQ(OB_SUCCESS, queue.init_without_thread(8));

  std::atomic<int64_t> attempt_count(0);
  RetryAsyncTask task(&attempt_count, 1);
  task.set_retry_times(1);
  task.set_retry_interval(200 * 1000);
  ASSERT_EQ(OB_SUCCESS, queue.push(task));

  int64_t next_ready_ts = 0;
  bool processed = false;
  bool has_more_ready = false;
  ASSERT_EQ(OB_SUCCESS,
      queue.process_one_task(processed, next_ready_ts, has_more_ready));
  EXPECT_TRUE(processed);
  EXPECT_EQ(1, attempt_count.load());
  EXPECT_TRUE(has_more_ready);
  EXPECT_EQ(0, next_ready_ts);

  ASSERT_EQ(OB_SUCCESS,
      queue.process_one_task(processed, next_ready_ts, has_more_ready));
  EXPECT_FALSE(processed);
  EXPECT_EQ(1, attempt_count.load());
  EXPECT_FALSE(has_more_ready);
  ASSERT_GT(next_ready_ts, ObTimeUtility::current_time());

  const int64_t sleep_us =
      MAX(static_cast<int64_t>(0),
          next_ready_ts - ObTimeUtility::current_time());
  ob_usleep(sleep_us + 10 * 1000);
  ASSERT_EQ(OB_SUCCESS,
      queue.process_one_task(processed, next_ready_ts, has_more_ready));
  EXPECT_TRUE(processed);
  EXPECT_EQ(2, attempt_count.load());
  EXPECT_FALSE(has_more_ready);
  EXPECT_EQ(0, next_ready_ts);
  ASSERT_EQ(OB_SUCCESS, queue.destroy());
}

} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_background_task_executor.log*");
  OB_LOGGER.set_file_name("test_background_task_executor.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
