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

#ifndef _OCEANBASE_COMMON_OB_TIME_UTILITY_H_
#define _OCEANBASE_COMMON_OB_TIME_UTILITY_H_
#include <chrono>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#endif
#include <time.h>
#include "lib/coro/co_var.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/ob_define.h"
#include "lib/time/ob_tsc_timestamp.h"

namespace oceanbase
{
namespace common
{
class ObTimeUtility
{
public:
  static int64_t current_time();
  static int64_t current_time_s();
  static int64_t current_time_ms();
  static int64_t current_time_us();
	static int64_t current_time_ns();
  static int64_t fast_current_time();
  static int64_t current_monotonic_time();
  static int64_t current_time_coarse();
  static int64_t current_monotonic_raw_time();
private:
  ObTimeUtility() = delete;
};

static bool IS_SYSTEM_SUPPORT_MONOTONIC_RAW = true;

// high performance current time, number of instructions is 1/6 of vdso_gettimeofday's.
OB_INLINE int64_t ObTimeUtility::fast_current_time()
{
  return ObTimeUtility::current_time();
}

inline int64_t ObTimeUtility::current_monotonic_time()
{
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

typedef ObTimeUtility ObTimeUtil;

typedef struct ObMonotonicTs
{
  OB_UNIS_VERSION(1);
public:
  explicit ObMonotonicTs(int64_t mts) : mts_(mts) {}
  ObMonotonicTs() { reset(); }
  ~ObMonotonicTs() { reset(); }
  void reset() { mts_ = 0; }
  bool is_valid() const { return mts_ > 0; }
  bool operator!=(const struct ObMonotonicTs other) const { return  mts_ != other.mts_; }
  bool operator==(const struct ObMonotonicTs other) const { return  mts_ == other.mts_; }
  bool operator>(const struct ObMonotonicTs other) const { return  mts_ > other.mts_; }
  bool operator>=(const struct ObMonotonicTs other) const { return  mts_ >= other.mts_; }
  bool operator<(const struct ObMonotonicTs other) const { return  mts_ < other.mts_; }
  bool operator<=(const struct ObMonotonicTs other) const { return  mts_ <= other.mts_; }
  struct ObMonotonicTs operator+(const struct ObMonotonicTs other) const { return ObMonotonicTs(mts_ + other.mts_); }
  struct ObMonotonicTs operator-(const struct ObMonotonicTs other) const { return ObMonotonicTs(mts_ - other.mts_); }
  static struct ObMonotonicTs current_time() { return ObMonotonicTs(common::ObTimeUtility::current_time()); }
  int64_t to_string(char *buf, const int64_t buf_len) const;
public:
  int64_t mts_;
} ObMonotonicTs;


} //common
} //oceanbase

#endif //_OCEANBASE_COMMON_OB_TIME_UTILITY_H_
