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

#include "lib/lock/ob_scond.h"

namespace oceanbase
{
namespace transaction
{

// Owned by TransService, which outlives both log callbacks and the Fetcher.
// The single consumer snapshots the key before checking its log iterator.
// Notifications are retry hints; the iterator remains the source of truth.
class AsyncIndexLogSignal final
{
public:
  AsyncIndexLogSignal() : enabled_(false) {}

  void set_enabled(bool enabled) { ATOMIC_STORE(&enabled_, enabled); }

  uint32_t get_key() { return cond_.get_key(); }

  void notify_log_ready(bool has_async_index)
  {
    if (has_async_index && ATOMIC_LOAD(&enabled_)) {
      (void)cond_.signal(1);
    }
  }

  int wait(uint32_t key, int64_t timeout_us)
  {
    return cond_.wait(key, timeout_us);
  }

  // Stop must also wake a consumer that raced with disabling notifications.
  void wake_waiter() { (void)cond_.signal(1); }

private:
  common::SimpleCond cond_;
  bool enabled_;

  DISALLOW_COPY_AND_ASSIGN(AsyncIndexLogSignal);
};

} // namespace transaction
} // namespace oceanbase
