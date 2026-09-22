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

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>
#include <thread>

static thread_local unsigned checkpoints = 0;
static thread_local int *publish_at_checkpoint = nullptr;
static thread_local int gap_wake_count = -1;
static thread_local std::atomic<unsigned> *checkpoint_notification = nullptr;

extern "C" void __real_seekdb_wasm_wait_checkpoint();

extern "C" void __wrap_seekdb_wasm_wait_checkpoint()
{
  ++checkpoints;
  if (checkpoint_notification != nullptr) {
    checkpoint_notification->fetch_add(1);
    checkpoint_notification = nullptr;
  }
  if (publish_at_checkpoint != nullptr) {
    __atomic_store_n(publish_at_checkpoint, 1, __ATOMIC_SEQ_CST);
    gap_wake_count = emscripten_futex_wake(publish_at_checkpoint, 1);
    publish_at_checkpoint = nullptr;
  }
  __real_seekdb_wasm_wait_checkpoint();
}

static void check_errors_and_deadline()
{
  alignas(4) int word = 0;
  checkpoints = 0;
  assert(emscripten_futex_wait(reinterpret_cast<char *>(&word) + 1, 0, 20) == -EINVAL);
  assert(emscripten_futex_wait(&word, 1, INFINITY) == -EWOULDBLOCK);
  assert(checkpoints == 0);
  assert(emscripten_futex_wait(&word, 0, 0) == -ETIMEDOUT);
  assert(checkpoints == 1);
  checkpoints = 0;
  const double started = emscripten_get_now();
  assert(emscripten_futex_wait(&word, 0, 1250) == -ETIMEDOUT);
  const double elapsed = emscripten_get_now() - started;
  assert(elapsed >= 1200 && elapsed < 3000);
  assert(checkpoints >= 1 && checkpoints <= 2);
}

static void check_state_change_between_slices()
{
  alignas(4) int word = 0;
  checkpoints = 0;
  publish_at_checkpoint = &word;
  gap_wake_count = -1;
  assert(emscripten_futex_wait(&word, 0, INFINITY) == -EWOULDBLOCK);
  assert(checkpoints == 1);
  assert(gap_wake_count == 0);
  assert(word == 1);
}

struct WakeState {
  alignas(4) int word = 0;
  std::atomic<bool> waiting{false};
  int result = -1;
};

static void *wait_for_wake(void *argument)
{
  auto &state = *static_cast<WakeState *>(argument);
  state.waiting.store(true);
  state.result = emscripten_futex_wait(&state.word, 0, INFINITY);
  return nullptr;
}

static void check_wake()
{
  WakeState state;
  pthread_t thread;
  assert(pthread_create(&thread, nullptr, wait_for_wake, &state) == 0);
  const double deadline = emscripten_get_now() + 3000;
  while (!state.waiting.load()) {
    assert(emscripten_get_now() < deadline);
    std::this_thread::yield();
  }
  while (emscripten_futex_wake(&state.word, 1) == 0) {
    assert(emscripten_get_now() < deadline);
    std::this_thread::yield();
  }
  assert(pthread_join(thread, nullptr) == 0);
  assert(state.result == 0);
  assert(state.word == 0);
}

struct MutexState {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  std::atomic<unsigned> waiting{0};
  unsigned waits = 0;
};

static void *lock_mutex(void *argument)
{
  auto &state = *static_cast<MutexState *>(argument);
  checkpoints = 0;
  checkpoint_notification = &state.waiting;
  assert(pthread_mutex_lock(&state.mutex) == 0);
  state.waits = checkpoints;
  assert(pthread_mutex_unlock(&state.mutex) == 0);
  return nullptr;
}

static void check_mutex()
{
  MutexState state;
  pthread_t thread;
  assert(pthread_mutex_lock(&state.mutex) == 0);
  assert(pthread_create(&thread, nullptr, lock_mutex, &state) == 0);
  const double deadline = emscripten_get_now() + 5000;
  while (state.waiting.load() == 0) {
    assert(emscripten_get_now() < deadline);
    std::this_thread::yield();
  }
  assert(pthread_mutex_unlock(&state.mutex) == 0);
  assert(pthread_join(thread, nullptr) == 0);
  assert(state.waits >= 1);
  assert(pthread_mutex_destroy(&state.mutex) == 0);
}

struct CondState {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
  std::atomic<unsigned> waiting{0};
  std::atomic<unsigned> timed_out_waiters{0};
  bool ready = false;
  unsigned completed = 0;
  unsigned waits = 0;
};

static void *wait_for_condition(void *argument)
{
  auto &state = *static_cast<CondState *>(argument);
  assert(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr) == 0);
  assert(pthread_mutex_lock(&state.mutex) == 0);
  checkpoints = 0;
  checkpoint_notification = &state.timed_out_waiters;
  state.waiting.fetch_add(1);
  while (!state.ready) {
    assert(pthread_cond_wait(&state.condition, &state.mutex) == 0);
  }
  ++state.completed;
  state.waits += checkpoints;
  assert(pthread_mutex_unlock(&state.mutex) == 0);
  return nullptr;
}

static void check_condition_and_join()
{
  CondState state;
  pthread_t threads[3];
  for (auto &thread : threads) {
    assert(pthread_create(&thread, nullptr, wait_for_condition, &state) == 0);
  }
  const double deadline = emscripten_get_now() + 5000;
  while (state.timed_out_waiters.load() != 3) {
    assert(emscripten_get_now() < deadline);
    std::this_thread::yield();
  }
  assert(state.waiting.load() == 3);
  assert(pthread_mutex_lock(&state.mutex) == 0);
  state.ready = true;
  assert(pthread_cond_broadcast(&state.condition) == 0);
  assert(pthread_mutex_unlock(&state.mutex) == 0);
  for (auto &thread : threads) {
    assert(pthread_join(thread, nullptr) == 0);
  }
  assert(state.completed == 3);
  assert(state.waits >= 3);
  assert(pthread_cond_destroy(&state.condition) == 0);
  assert(pthread_mutex_destroy(&state.mutex) == 0);
}

static void *wait_before_exit(void *argument)
{
  auto &state = *static_cast<WakeState *>(argument);
  state.waiting.store(true);
  while (__atomic_load_n(&state.word, __ATOMIC_SEQ_CST) == 0) {
    const int result = emscripten_futex_wait(&state.word, 0, INFINITY);
    assert(result == 0 || result == -EWOULDBLOCK);
  }
  return &state;
}

static void check_join()
{
  WakeState state;
  pthread_t thread;
  assert(pthread_create(&thread, nullptr, wait_before_exit, &state) == 0);
  const double deadline = emscripten_get_now() + 3000;
  while (!state.waiting.load()) {
    assert(emscripten_get_now() < deadline);
    std::this_thread::yield();
  }
  checkpoints = 0;
  publish_at_checkpoint = &state.word;
  void *result = nullptr;
  assert(pthread_join(thread, &result) == 0);
  assert(checkpoints >= 1);
  assert(publish_at_checkpoint == nullptr);
  assert(result == &state);
}

int main()
{
  check_errors_and_deadline();
  check_state_change_between_slices();
  check_wake();
  check_mutex();
  check_condition_and_join();
  check_join();
  std::puts("seekdb WASM futex wait adapter PASS");
}
