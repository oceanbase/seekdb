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

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace oceanbase
{
namespace observer
{

// Background consumer of __all_ai_column_task. One thread is enough for the
// single-node deployment: it claims tasks, calls the model through the normal
// AI function expression, and publishes a whole request group atomically.
//
// A task only becomes visible once the transaction that wrote it commits, and
// the row is dropped as soon as the dependency it belongs to changes again.
// Relying on the poll interval alone is therefore not enough: a task that is
// written and invalidated again within one poll tick is never claimed at all,
// and the model never sees the input it was meant to see. The writer notifies
// the worker instead, so a task is claimed as soon as it is committed.
class ObAiColumnWorker
{
public:
  static ObAiColumnWorker &get_instance();
  int start();
  void stop();
  // Wakes the worker up so a task that was just written is claimed without
  // waiting for the next poll tick. May be called before the writing
  // transaction commits: the worker then keeps polling at a short interval for
  // a while, so the row is claimed as soon as it becomes visible.
  static void notify_task_created();
private:
  ObAiColumnWorker();
  void run();
  void process_tasks();

  std::atomic<bool> started_;
  std::atomic<bool> stopped_;
  // Number of fast poll rounds still owed after a notification.
  std::atomic<int32_t> fast_rounds_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
};

} // namespace observer
} // namespace oceanbase
