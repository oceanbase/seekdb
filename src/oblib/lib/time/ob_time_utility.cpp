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

#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#ifdef __APPLE__
#include <mach/mach_time.h>
#elif defined(_WIN32)
#include <windows.h>
#include "lib/hash/ob_hashutils.h"
#endif

using namespace oceanbase;
using namespace oceanbase::common;

OB_SERIALIZE_MEMBER(ObMonotonicTs, mts_);

static __thread bool systime_error = false;

#ifdef __APPLE__
// Single-layer thread-local design:
// - Each thread calibrates independently from gettimeofday() every 1 second
// - Simple, no cross-thread synchronization needed
// - gettimeofday() is fast on macOS (~25ns, commpage, no real syscall)
static constexpr uint64_t CALIBRATION_INTERVAL_NS = 1ULL * 1000000000ULL;

// Thread-local time base (each thread calibrates independently)
// Uses thread_local for automatic initialization on first access
struct MachTimeBaseLocal {
  int64_t base_wall_time_us_;
  uint64_t base_mach_time_;
  uint64_t next_calibrate_mach_;
  uint64_t calibration_interval_mach_;
  uint32_t numer_;
  uint32_t denom_;

  MachTimeBaseLocal() {
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    numer_ = timebase.numer;
    denom_ = timebase.denom;
    calibration_interval_mach_ = CALIBRATION_INTERVAL_NS * denom_ / numer_;
    // Do NOT call calibrate() here. Instead, set next_calibrate_mach_ = 0
    // to force calibration on first use via calibrate_if_needed().
    // This avoids a uint64_t underflow bug: the constructor is invoked lazily
    // on first thread_local access, AFTER the caller has already captured
    // current_mach = mach_absolute_time(). If calibrate() independently reads
    // mach_absolute_time(), base_mach_time_ becomes newer than the caller's
    // current_mach, causing (current_mach - base_mach_time_) to underflow.
    base_wall_time_us_ = 0;
    base_mach_time_ = 0;
    next_calibrate_mach_ = 0;
  }

  // Calibrate using the caller's mach time to guarantee
  // base_mach_time_ <= current_mach (no underflow possible).
  void calibrate(uint64_t current_mach) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    base_wall_time_us_ = tv.tv_sec * 1000000LL + tv.tv_usec;
    base_mach_time_ = current_mach;
    next_calibrate_mach_ = current_mach + calibration_interval_mach_;
  }

  void calibrate_if_needed(uint64_t current_mach) {
    if (OB_UNLIKELY(current_mach >= next_calibrate_mach_)) {
      calibrate(current_mach);
    }
  }
};

static thread_local MachTimeBaseLocal tl_time_base;
#elif defined(_WIN32)
// Windows: QPC-based high-precision time with periodic calibration
// Same design as macOS mach_absolute_time approach:
// - QPC provides monotonic, high-precision counter (~100ns or better)
// - Calibrate against GetSystemTimePreciseAsFileTime every 1 second
// - Thread-local to avoid cross-thread synchronization

struct QPCTimeBaseLocal {
  int64_t base_wall_time_us_;
  int64_t base_qpc_;
  int64_t next_calibrate_qpc_;
  int64_t calibration_interval_qpc_;
  int64_t qpc_freq_;

  QPCTimeBaseLocal() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpc_freq_ = freq.QuadPart;
    calibration_interval_qpc_ = qpc_freq_;  // ~1 second
    calibrate();
  }

  void calibrate() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t /= 10;
    t -= 11644473600ULL * 1000000ULL;

    base_wall_time_us_ = static_cast<int64_t>(t);
    base_qpc_ = now.QuadPart;
    next_calibrate_qpc_ = now.QuadPart + calibration_interval_qpc_;
  }

  void calibrate_if_needed(int64_t current_qpc) {
    if (OB_UNLIKELY(current_qpc >= next_calibrate_qpc_)) {
      calibrate();
    }
  }
};

static __declspec(thread) bool win_tl_time_inited = false;
static __declspec(thread) QPCTimeBaseLocal *win_tl_time_base = nullptr;

static QPCTimeBaseLocal &get_win_tl_time_base() {
  if (OB_UNLIKELY(!win_tl_time_inited)) {
    static __declspec(thread) char buf[sizeof(QPCTimeBaseLocal)];
    win_tl_time_base = new (buf) QPCTimeBaseLocal();
    win_tl_time_inited = true;
  }
  return *win_tl_time_base;
}
#endif

int64_t ObTimeUtility::current_time()
{
#ifdef __APPLE__
  uint64_t current_mach = mach_absolute_time();

  // Recalibrate from gettimeofday() every ~1 second
  tl_time_base.calibrate_if_needed(current_mach);

  // Hot path: pure local memory access, zero cross-thread contention
  uint64_t elapsed_ns = (current_mach - tl_time_base.base_mach_time_) * tl_time_base.numer_ / tl_time_base.denom_;
  return tl_time_base.base_wall_time_us_ + static_cast<int64_t>(elapsed_ns / 1000);
#elif defined(_WIN32)
  QPCTimeBaseLocal &base = get_win_tl_time_base();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  base.calibrate_if_needed(now.QuadPart);
  int64_t elapsed_us = (now.QuadPart - base.base_qpc_) * 1000000LL / base.qpc_freq_;
  return base.base_wall_time_us_ + elapsed_us;
#else
  int err_ret = 0;
  struct timeval t;
  if (OB_UNLIKELY((err_ret = gettimeofday(&t, nullptr)) < 0)) {
    LIB_LOG_RET(ERROR, err_ret, "gettimeofday error", K(err_ret), K(errno));
    systime_error = true;
    ob_abort();
  }
  return (static_cast<int64_t>(t.tv_sec) * 1000000L +
          static_cast<int64_t>(t.tv_usec));
#endif
}

int64_t ObTimeUtility::current_time_s()
{
  return (ObTimeUtility::current_time() / 1000000L);
}

int64_t ObTimeUtility::current_time_ms()
{
  return (ObTimeUtility::current_time() / 1000L);
}

int64_t ObTimeUtility::current_time_us()
{
  return ObTimeUtility::current_time();
}

int64_t ObTimeUtility::current_time_ns()
{
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  t -= UINT64_C(116444736000000000);
  return static_cast<int64_t>(t) * 100;
#else
  int err_ret = 0;
  struct timespec ts;
  if (OB_UNLIKELY((err_ret = clock_gettime(CLOCK_REALTIME, &ts)) != 0)) {
      LIB_LOG_RET(WARN, err_ret, "current system not support CLOCK_REALTIME", K(err_ret), K(errno));
      systime_error = true;
      ob_abort();
  }
  return static_cast<int64_t>(ts.tv_sec) * 1000000000L +
    static_cast<int64_t>(ts.tv_nsec);
#endif
}

int64_t ObTimeUtility::current_monotonic_raw_time()
{
#ifdef _WIN32
  LARGE_INTEGER freq, counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  int64_t sec = counter.QuadPart / freq.QuadPart;
  int64_t frac = counter.QuadPart % freq.QuadPart;
  return sec * 1000000L + frac * 1000000L / freq.QuadPart;
#else
  int64_t ret_val = 0;
  int err_ret = 0;
  struct timespec ts;

  if (IS_SYSTEM_SUPPORT_MONOTONIC_RAW) {
    if (OB_UNLIKELY((err_ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) != 0)) {
      LIB_LOG_RET(WARN, err_ret, "current system not support CLOCK_MONOTONIC_RAW", K(err_ret), K(errno));
      IS_SYSTEM_SUPPORT_MONOTONIC_RAW = false;
      ret_val = current_time();
    } else {
      // TODO: div 1000 can be replace to bitwise
      ret_val = static_cast<int64_t>(ts.tv_sec) * 1000000L +
        static_cast<int64_t>(ts.tv_nsec / 1000);
    }
  } else {
    // not support monotonic raw, use real time instead
    ret_val = current_time();
  }

  return ret_val;
#endif
}

int64_t ObTimeUtility::current_time_coarse()
{
#if defined(__APPLE__) || defined(_WIN32)
  // On macOS, use the same time source as current_time() to avoid clock skew.
  // This is because current_time() uses mach_absolute_time() with a fixed base,
  // while clock_gettime(CLOCK_REALTIME) returns the actual system clock which
  // may drift due to NTP adjustments. Using different time sources causes
  // check_clock() in timer service to detect false "Hardware clock skew" and
  // generate massive warning logs, leading to CPU exhaustion over time.
  return current_time();
#else
  struct timespec t;
  int err_ret = 0;
  if (OB_UNLIKELY((err_ret = clock_gettime(
#ifdef HAVE_REALTIME_COARSE
                      CLOCK_REALTIME_COARSE,
#else
                      CLOCK_REALTIME,
#endif
                      &t)) != 0)) {
    LIB_LOG_RET(ERROR, err_ret, "clock_gettime error", K(err_ret), K(errno));
    systime_error = true;
    ob_abort();
  }
  return (static_cast<int64_t>(t.tv_sec) * 1000000L +
          static_cast<int64_t>(t.tv_nsec / 1000));
#endif
}

int64_t ObMonotonicTs::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  databuff_printf(buf, buf_len, pos, "[mts=%ld]", mts_);
  return pos;
}
