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
#include <cstdint>

namespace oceanbase
{
namespace omt
{
class RequestAdmission
{
public:
  class Guard
  {
  public:
    explicit Guard(RequestAdmission &admission)
        : admission_(admission.enter() ? &admission : nullptr)
    {}
    ~Guard()
    {
      if (nullptr != admission_) {
        admission_->state_.fetch_sub(1, std::memory_order_release);
      }
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    bool is_admitted() const { return nullptr != admission_; }

  private:
    RequestAdmission *admission_;
  };

  RequestAdmission() : state_(0) {}

  void close() { state_.fetch_or(CLOSED, std::memory_order_acq_rel); }
  // Lifecycle owners may reopen only before GC begins. Initialization can
  // already have admitted requests, so opening must preserve their count.
  void open() { state_.fetch_and(~CLOSED, std::memory_order_release); }
  bool is_closed() const
  {
    return 0 != (state_.load(std::memory_order_acquire) & CLOSED);
  }
  bool is_drained() const
  {
    return CLOSED == state_.load(std::memory_order_acquire);
  }

private:
  bool enter()
  {
    uint64_t state = state_.load(std::memory_order_relaxed);
    while (state < CLOSED - 1) {
      if (state_.compare_exchange_weak(state, state + 1,
              std::memory_order_acquire, std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

  static constexpr uint64_t CLOSED = uint64_t{1} << 63;
  std::atomic<uint64_t> state_;
};
}
}
