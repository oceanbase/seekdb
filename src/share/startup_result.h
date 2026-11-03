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
#include <cassert>

namespace oceanbase::share
{
class StartupResult
{
public:
  // A negative database error and successful startup compete for the same state.
  // Return zero only when the server is already running and may retry the I/O.
  int report_failure(const int error)
  {
    assert(error < 0);
    int expected = STARTING;
    return result_.compare_exchange_strong(expected, error) ? error
        : (expected == RUNNING ? 0 : expected);
  }

  int error() const
  {
    const int result = result_.load();
    return result < 0 ? result : 0;
  }

  int finish()
  {
    int expected = STARTING;
    return result_.compare_exchange_strong(expected, RUNNING) || expected == RUNNING
        ? 0 : expected;
  }

private:
  static constexpr int STARTING = 0;
  static constexpr int RUNNING = 1;
  std::atomic<int> result_{STARTING};
};
}
