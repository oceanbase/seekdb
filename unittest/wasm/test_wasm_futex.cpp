/*
 * Copyright (c) 2026 OceanBase.
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
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <chrono>
#include <thread>
#include "lib/lock/ob_futex.h"

using oceanbase::lib::ObFutex;
using namespace oceanbase::common;

struct State {
  ObFutex futex;
  int64_t timeout_us = 5000000;
  std::atomic<int> ready{0};
  std::atomic<int> done{0};
};

static void *waiter(void *arg)
{
  auto &state = *static_cast<State *>(arg);
  state.ready.fetch_add(1);
  const int ret = state.futex.wait(0, state.timeout_us);
  assert(ret == OB_SUCCESS);
  state.done.fetch_add(1);
  return nullptr;
}

static void check_wait_wake(int64_t timeout_us)
{
  std::printf("futex wait/wake timeout_us=%" PRId64 "\n", timeout_us);
  State state;
  state.timeout_us = timeout_us;
  pthread_t threads[3];
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  for (auto &thread : threads) { assert(pthread_create(&thread, nullptr, waiter, &state) == 0); }
  while (state.ready.load() != 3) {
    assert(std::chrono::steady_clock::now() < deadline);
    sched_yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(state.done.load() == 0);
  // Keep the value unchanged so success requires a real wake rather than
  // the value-mismatch fast path. Wake repeatedly to cover scheduling races.
  int total_woken = 0;
  while (state.done.load() != 3) {
    int count = state.futex.wake(1);
    assert(count == 0 || count == 1);
    total_woken += count;
    assert(std::chrono::steady_clock::now() < deadline);
    sched_yield();
  }
  for (auto &thread : threads) { assert(pthread_join(thread, nullptr) == 0); }
  assert(total_woken == 3);
  assert(state.futex.val() == 0);
}

int main()
{
  alignas(4) int word = 0;
  const timespec zero{0, 0};
  const timespec invalid[]{{0, 1000000000}, {-1, 0}, {0, -1}};
  assert(futex_wait(&word, 1, nullptr) == EAGAIN);
  assert(futex_wait(&word, 0, &zero) == ETIMEDOUT);
  for (const auto &timeout : invalid) { assert(futex_wait(&word, 0, &timeout) == EINVAL); }
  assert(futex_wake(&word, 0) == 0);
  assert(futex_wake(&word, -1) == -1 && errno == EINVAL);
  State state;
  const auto start = std::chrono::steady_clock::now();
  assert(state.futex.wait(0, 20000) == OB_TIMEOUT);
  assert(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(15));
  const int64_t timeouts[]{5000000, INT64_MAX / 1000 - 1, INT64_MAX / 1000,
                           INT64_MAX / 1000 + 1, INT64_MAX};
  for (const int64_t timeout : timeouts) { check_wait_wake(timeout); }
  std::puts("seekdb WASM futex PASS");
}
