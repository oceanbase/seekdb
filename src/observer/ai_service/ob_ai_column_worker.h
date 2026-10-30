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
#include <thread>

namespace oceanbase
{
namespace observer
{

// Background consumer of __all_ai_column_task. One thread is enough for the
// single-node deployment: it claims tasks, calls the model through the normal
// AI function expression, and publishes a whole request group atomically.
class ObAiColumnWorker
{
public:
  static ObAiColumnWorker &get_instance();
  int start();
  void stop();
private:
  ObAiColumnWorker();
  void run();
  void process_tasks();

  std::atomic<bool> started_;
  std::atomic<bool> stopped_;
  std::thread thread_;
};

} // namespace observer
} // namespace oceanbase
