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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include <time.h>

#include "lib/resource/ob_memory_quota.h"

using oceanbase::common::MemoryQuota;

// This is a standalone performance runner, not a correctness unit test.

#ifdef SEEKDB_STANDALONE_QUOTA_BENCHMARK
// ob_tsi_utils.cpp only calls this if the process exhausts all 65,536 thread
// ids.  The benchmark links that small runtime directly to keep its allocator
// hot path independent from the product shared library.
void ob_abort(void) noexcept
{
  std::abort();
}
#endif

namespace
{

struct Options
{
  const char *mode_ = nullptr;
  int threads_ = 0;
  int64_t warmup_ms_ = 0;
  int64_t duration_ms_ = 0;
};

bool parse_positive(const char *value, int64_t &result)
{
  char *end = nullptr;
  const long long parsed = nullptr == value ? 0 : std::strtoll(value, &end, 10);
  const bool valid = nullptr != value && value != end && '\0' == *end && parsed > 0;
  if (valid) {
    result = parsed;
  }
  return valid;
}

bool parse_options(const int argc, char **argv, Options &options)
{
  bool valid = 5 == argc;
  int64_t threads = 0;
  valid = valid && (0 == std::strcmp(argv[1], "legacy_hold")
                    || 0 == std::strcmp(argv[1], "quota"));
  valid = valid && parse_positive(argv[2], threads)
      && threads <= std::numeric_limits<int>::max();
  valid = valid && parse_positive(argv[3], options.warmup_ms_);
  valid = valid && parse_positive(argv[4], options.duration_ms_);
  if (valid) {
    options.mode_ = argv[1];
    options.threads_ = static_cast<int>(threads);
  }
  return valid;
}

double process_cpu_seconds()
{
  struct timespec value = {};
  return 0 == clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value)
      ? static_cast<double>(value.tv_sec)
          + static_cast<double>(value.tv_nsec) / 1000000000.0
      : 0.0;
}

uint64_t run_once(const Options &options, const int64_t duration_ms)
{
  static constexpr int64_t LEGACY_LIMIT =
      std::numeric_limits<int64_t>::max() / 2;
  std::atomic<bool> start(false);
  std::atomic<bool> stop(false);
  alignas(64) std::atomic<int64_t> baseline_sum_hold(0);
  alignas(64) std::atomic<int64_t> baseline_ctx_hold(0);
  volatile int64_t baseline_hard_limit = LEGACY_LIMIT;
  volatile int64_t baseline_ctx_limit = LEGACY_LIMIT;
  MemoryQuota quota(std::numeric_limits<int64_t>::max());
  std::vector<uint64_t> operations(static_cast<size_t>(options.threads_), 0);
  std::vector<std::thread> workers;
  workers.reserve(static_cast<size_t>(options.threads_));

  for (int thread_index = 0; thread_index < options.threads_; ++thread_index) {
    workers.emplace_back([&, thread_index]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      uint64_t local_operations = 0;
      if (0 == std::strcmp(options.mode_, "quota")) {
        while (!stop.load(std::memory_order_relaxed)) {
          if (quota.reserve(1) && quota.reconcile(1, 1)) {
            quota.release(1);
            ++local_operations;
          }
        }
      } else {
        // ObMemoryMgr::update_hold() first updated sum_hold_ and then called
        // update_ctx_hold() for ordinary component allocations.  A successful
        // allocate/free pair therefore performed four shared RMW operations.
        // Preserve that complete base-side shape without rebuilding deleted
        // allocator code; using one add/sub pair would undercount the old path
        // by half and make this comparison meaningless.
        while (!stop.load(std::memory_order_relaxed)) {
          const int64_t sum = baseline_sum_hold.fetch_add(
              1, std::memory_order_seq_cst) + 1;
          if (sum <= baseline_hard_limit) {
            // The removed update_ctx_hold() first sampled hold/limit and then
            // used ATOMIC_AAF (__sync, full barrier) for the admitted charge.
            if (baseline_ctx_hold.load(std::memory_order_relaxed) + 1
                <= baseline_ctx_limit) {
              const int64_t ctx = baseline_ctx_hold.fetch_add(
                  1, std::memory_order_seq_cst) + 1;
              if (ctx <= baseline_ctx_limit) {
                baseline_ctx_hold.fetch_sub(1, std::memory_order_seq_cst);
                baseline_sum_hold.fetch_sub(1, std::memory_order_seq_cst);
                ++local_operations;
              } else {
                baseline_ctx_hold.fetch_sub(1, std::memory_order_seq_cst);
                baseline_sum_hold.fetch_sub(1, std::memory_order_seq_cst);
              }
            } else {
              baseline_sum_hold.fetch_sub(1, std::memory_order_seq_cst);
            }
          } else {
            baseline_sum_hold.fetch_sub(1, std::memory_order_seq_cst);
          }
        }
      }
      operations[static_cast<size_t>(thread_index)] = local_operations;
    });
  }

  start.store(true, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
  stop.store(true, std::memory_order_release);
  uint64_t total_operations = 0;
  for (std::thread &worker : workers) {
    worker.join();
  }
  for (const uint64_t count : operations) {
    total_operations += count;
  }

  if (0 != baseline_sum_hold.load(std::memory_order_relaxed)
      || 0 != baseline_ctx_hold.load(std::memory_order_relaxed)
      || 0 != quota.reserved() || 0 != quota.committed()) {
    std::fprintf(stderr, "benchmark accounting did not return to zero\n");
    std::exit(2);
  }
  return total_operations;
}

} // namespace

int main(int argc, char **argv)
{
  Options options;
  if (!parse_options(argc, argv, options)) {
    std::fprintf(stderr,
        "usage: %s legacy_hold|quota THREADS WARMUP_MS DURATION_MS\n", argv[0]);
    return 2;
  }

  static_cast<void>(run_once(options, options.warmup_ms_));
  const double cpu_start = process_cpu_seconds();
  const auto wall_start = std::chrono::steady_clock::now();
  const uint64_t operations = run_once(options, options.duration_ms_);
  const auto wall_end = std::chrono::steady_clock::now();
  const double cpu_seconds = process_cpu_seconds() - cpu_start;
  const double wall_seconds =
      std::chrono::duration<double>(wall_end - wall_start).count();
  const double operations_per_second = wall_seconds > 0
      ? static_cast<double>(operations) / wall_seconds : 0.0;

  std::printf("mode=%s threads=%d operations=%llu wall_seconds=%.6f "
              "cpu_seconds=%.6f operations_per_second=%.3f\n",
      options.mode_, options.threads_,
      static_cast<unsigned long long>(operations), wall_seconds,
      cpu_seconds, operations_per_second);
  return 0;
}
