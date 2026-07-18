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

#ifndef OCEANBASE_COMMON_OB_LOG_TIME_FMT_H
#define OCEANBASE_COMMON_OB_LOG_TIME_FMT_H
#include <stdint.h>

enum TimeRange {
  YEAR = 0,
  MONTH = 1,
  DAY = 2,
  HOUR = 3,
  MINUTE = 4,
  SECOND = 5,
  MSECOND = 6,
  USECOND = 7
};

namespace oceanbase
{
namespace common
{

struct ObTime2Str {
  static const char *ob_timestamp_str(const int64_t ts);
  template <TimeRange BEGIN, TimeRange TO>
  static const char *ob_timestamp_str_range(const int64_t ts)
  {
    static_assert(int(BEGIN) <= int(TO), "send range must not less than first range.");
    return ob_timestamp_str_range_(ts, BEGIN, TO);
  }
  static const char *ob_timestamp_str_range_(const int64_t ts, TimeRange begin, TimeRange to);
};

} //end common
} //end oceanbase
#endif
