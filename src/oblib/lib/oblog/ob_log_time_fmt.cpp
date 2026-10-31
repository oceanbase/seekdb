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

#include "ob_log_time_fmt.h"
#include <inttypes.h>
#include <time.h>
#include <stdio.h>
#include "lib/utility/utility.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace common
{

const int TIME_RANGE_INDEX[] = {0,4,
                                5,7,
                                8,10,
                                11,13,
                                14,16,
                                17,19,
                                20,23,
                                23,26};

constexpr int TIME_BUFFER_SIZE = 27;
constexpr int MAX_TIMESTAMP_BUFFER = 1 << 4;// 16
constexpr int IDX_MASK = MAX_TIMESTAMP_BUFFER - 1;
using TimestampStrBuffer_ = char[TIME_BUFFER_SIZE];
using TimestampStrBuffer = TimestampStrBuffer_[MAX_TIMESTAMP_BUFFER];
TLOCAL(TimestampStrBuffer, timestamp_str_buffer);
TLOCAL(uint32_t, buffer_idx) = 0;
TLOCAL(time_t, last_timestamp_unix_sec);
TLOCAL(struct tm, last_timestamp_localtime);

const char *ObTime2Str::ob_timestamp_str(const int64_t ts)
{
  TimestampStrBuffer_ &buffer = timestamp_str_buffer[buffer_idx++ & IDX_MASK];
  // Timeout sentinels such as INT64_MAX are not calendar timestamps. Besides
  // exceeding the fixed four-digit-year buffer, they can trap in Emscripten's
  // JavaScript Date conversion. Preserve their exact value in diagnostic output.
  constexpr int64_t FIRST_SECOND_AFTER_YEAR_9999 = INT64_C(253402300800);
  if (ts < 0 || ts / 1000000 >= FIRST_SECOND_AFTER_YEAR_9999) {
    snprintf(buffer, sizeof(buffer), "%" PRId64, ts);
    return buffer;
  }
  struct tm t = {};
  const time_t ts_s = ts / 1000000;
  bool valid = true;
  if (ts_s <= INT32_MAX) {
    ob_fast_localtime(last_timestamp_unix_sec, last_timestamp_localtime, ts_s, &t);
  } else {
    // The native fast calendar implementation uses 32-bit intermediates.
#ifdef _WIN32
    valid = localtime_s(&t, &ts_s) == 0;
#else
    valid = localtime_r(&ts_s, &t) != nullptr;
#endif
  }
  if (!valid || t.tm_year < -1900 || t.tm_year > 8099) {
    snprintf(buffer, sizeof(buffer), "%" PRId64, ts);
  } else {
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06" PRId64,
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
             ts % 1000000);
  }
  return buffer;
}

const char *ObTime2Str::ob_timestamp_str_range_(const int64_t ts, TimeRange begin, TimeRange to)
{
  const char *str = ObTime2Str::ob_timestamp_str(ts);
  // Raw numeric sentinel output has no calendar components to slice.
  if (str[4] != '-' || str[7] != '-') return str;
  const_cast<char*>(str)[TIME_RANGE_INDEX[2 * static_cast<int>(to) + 1]] = '\0';
  str = &str[TIME_RANGE_INDEX[2 * static_cast<int>(begin)]];
  return str;
}

}// namespace common
}// namespace oceanbase
