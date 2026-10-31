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

#include "lib/allocator/ob_jemalloc.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <thread>

using namespace oceanbase::common;

namespace {
void require(bool condition, const char *message)
{
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

template <typename T>
T read_control(const char *name)
{
  T value{};
  size_t size = sizeof(value);
  require(0 == je_mallctl(name, &value, &size, nullptr, 0), name);
  require(size == sizeof(value), "unexpected mallctl value size");
  return value;
}

void refresh_stats()
{
  uint64_t epoch = 1;
  require(0 == je_mallctl("epoch", nullptr, nullptr, &epoch, sizeof(epoch)),
          "refresh allocator statistics");
}

void test_allocation()
{
  require(jemalloc_usable_size(nullptr) == 0, "null usable size");
  jemalloc_free(nullptr);
  void *ptr = jemalloc_malloc(128);
  require(ptr != nullptr, "allocate");
  std::memset(ptr, 0x5a, 128);
  require(jemalloc_usable_size(ptr) >= 128, "usable size");
  void *grown = jemalloc_realloc(ptr, 8192);
  require(grown != nullptr, "grow allocation");
  for (size_t i = 0; i < 128; ++i) {
    require(static_cast<unsigned char *>(grown)[i] == 0x5a,
            "realloc preserves contents");
  }
  std::thread releaser([grown] { jemalloc_free(grown); });
  releaser.join();

  for (size_t alignment : {size_t(16), size_t(4096), size_t(65536)}) {
    ptr = jemalloc_memalign(alignment, 12345);
    require(ptr != nullptr, "aligned allocation");
    require(reinterpret_cast<uintptr_t>(ptr) % alignment == 0, "alignment");
    std::memset(ptr, 0xa5, 12345);
    jemalloc_free(ptr);
  }

  // These pointers belong to the host CRT, not the explicit jemalloc domain.
  ptr = std::malloc(128);
  require(ptr != nullptr, "CRT allocation remains usable");
  std::memset(ptr, 0x3c, 128);
  std::free(ptr);
}

void test_background_threads()
{
  require(jemalloc_enable_background_threads(), "enable background threads");
  refresh_stats();
  require(read_control<bool>("background_thread"), "background thread state");
  require(read_control<size_t>("stats.background_thread.num_threads") > 0,
          "background worker was created");
  const uint64_t runs = read_control<uint64_t>("stats.background_thread.num_runs");

  constexpr size_t COUNT = 64;
  constexpr size_t SIZE = 1 << 20;
  void *allocations[COUNT];
  for (size_t i = 0; i < COUNT; ++i) {
    allocations[i] = jemalloc_malloc(SIZE);
    require(allocations[i] != nullptr, "allocate background workload");
    std::memset(allocations[i], 0x6b, SIZE);
  }
  refresh_stats();
  const size_t peak_resident = read_control<size_t>("stats.resident");
  std::thread releaser([&] {
    for (void *ptr : allocations) {
      jemalloc_free(ptr);
    }
  });
  releaser.join();

  // No application allocation triggers decay while the worker returns pages.
  bool reclaimed = false;
  for (int attempt = 0; attempt < 100 && !reclaimed; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    refresh_stats();
    reclaimed = read_control<uint64_t>("stats.background_thread.num_runs") > runs
        && read_control<size_t>("stats.resident") + SIZE * COUNT / 2 < peak_resident;
  }
  require(reclaimed, "background worker reclaimed idle pages");

  for (int i = 0; i < 3; ++i) {
    bool enabled = false;
    require(0 == je_mallctl("background_thread", nullptr, nullptr,
                           &enabled, sizeof(enabled)), "stop background threads");
    require(!read_control<bool>("background_thread"), "background threads stopped");
    require(jemalloc_enable_background_threads(), "restart background threads");
  }
}
} // namespace

extern "C" {
const char *je_malloc_conf =
    "background_thread:false,dirty_decay_ms:100,muzzy_decay_ms:0";
}

int main()
{
  std::printf("jemalloc %s\n", read_control<const char *>("version"));
  require(read_control<std::ptrdiff_t>("opt.dirty_decay_ms") == 100,
          "application malloc_conf overrides the library default");
  test_allocation();
  test_background_threads();
  std::puts("jemalloc backend: PASS");
  return 0;
}
