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
#include <cstdio>
#include <pthread.h>
#include <emscripten/stack.h>
#include "lib/utility/ob_common_utility.h"
#include "lib/utility/ob_smart_call.h"

using namespace oceanbase::common;

__attribute__((noinline)) static int64_t consume_stack(int depth, int64_t previous)
{
  volatile unsigned char frame[1024];
  for (size_t i = 0; i < sizeof(frame); ++i) { frame[i] = static_cast<unsigned char>(depth); }
  bool overflow = false;
  int64_t used = 0;
  assert(check_stack_overflow(overflow, 1024, &used) == OB_SUCCESS);
  assert(!overflow && used > previous);
  int64_t maximum = used;
  if (depth > 0) { maximum = consume_stack(depth - 1, used); }
  // Reading after the recursive call prevents tail recursion or frame reuse.
  assert(frame[depth % sizeof(frame)] == static_cast<unsigned char>(depth));
  return maximum;
}

static void check_current_stack(size_t minimum)
{
  void *base = nullptr;
  size_t size = 0;
  assert(get_stackattr(base, size) == OB_SUCCESS);
  assert(reinterpret_cast<uintptr_t>(base) == emscripten_stack_get_end());
  assert(size >= minimum);
  assert(reinterpret_cast<uintptr_t>(base) + size == emscripten_stack_get_base());
  bool overflow = true;
  int64_t used = -1;
  assert(check_stack_overflow(overflow, 0, &used) == OB_SUCCESS && !overflow);
  assert(used >= 0 && static_cast<uint64_t>(used) <= size);
  assert(consume_stack(24, used) > used + 24 * 1024);
  // Reserve the entire stack: active frames must be recognized as overflow.
  assert(check_stack_overflow(overflow, size, &used) == OB_SUCCESS && overflow);
  assert(check_stack_overflow(overflow, size + 1) == OB_ERR_UNEXPECTED && overflow);
  assert(check_stack_overflow(overflow, -1) == OB_INVALID_ARGUMENT && overflow);
  assert(check_stack_overflow() == OB_SUCCESS);
  bool called = false;
  auto callback = [](void *arg) { *static_cast<bool *>(arg) = true; return OB_SUCCESS; };
  assert(call_with_new_stack(&called, callback, nullptr, 0) == OB_SIZE_OVERFLOW);
  assert(!called);
  void *new_stack = reinterpret_cast<void *>(1);
  assert(alloc_stack(1024 * 1024, new_stack) == OB_SIZE_OVERFLOW && new_stack == nullptr);
  void *trace[32] = {};
  int count = light_backtrace(trace, 32);
  assert(count > 0 && count <= 32 && trace[0] != nullptr);
  struct { void *trace[1]; uint32_t guard; } limited = {{nullptr}, UINT32_C(0xabcddcba)};
  assert(_ob_backtrace(limited.trace, 1) == 1);
  assert(limited.guard == UINT32_C(0xabcddcba));
  assert(_ob_backtrace(nullptr, 8) == 0 && _ob_backtrace(trace, 0) == 0);
  char formatted[128];
  int64_t addresses[] = {INT64_C(0x123456789abcdef0), INT64_C(0x12345678)};
  assert(strcmp(parray(formatted, sizeof(formatted), addresses, 2),
                "0x123456789abcdef0 0x12345678") == 0);
  assert(strncmp(lbt(formatted, sizeof(formatted)), "0x", 2) == 0);
}

int main()
{
  check_current_stack(1024 * 1024);
  const uintptr_t main_low = emscripten_stack_get_end();
  const uintptr_t main_high = emscripten_stack_get_base();
  pthread_attr_t attr;
  assert(pthread_attr_init(&attr) == 0);
  assert(pthread_attr_setstacksize(&attr, 512 * 1024) == 0);
  uintptr_t worker_bounds[2] = {};
  pthread_t thread;
  assert(pthread_create(&thread, &attr, [](void *arg) -> void * {
    auto *bounds = static_cast<uintptr_t *>(arg);
    bounds[0] = emscripten_stack_get_end();
    bounds[1] = emscripten_stack_get_base();
    check_current_stack(512 * 1024);
    return nullptr;
  }, worker_bounds) == 0);
  assert(pthread_attr_destroy(&attr) == 0);
  assert(pthread_join(thread, nullptr) == 0);
  assert(worker_bounds[1] <= main_low || worker_bounds[0] >= main_high);
  puts("PASS: real seekdb stack bounds, overflow checks and backtraces on main and pthread stacks");
}
