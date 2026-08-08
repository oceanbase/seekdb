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

#ifndef OCEANBASE_SHARE_TABLE_OB_TTL_SCHEDULE_H_
#define OCEANBASE_SHARE_TABLE_OB_TTL_SCHEDULE_H_

#include <stdlib.h>
#include <string.h>
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

struct ObTTLDayTime
{
  ObTTLDayTime() : hour_(0), min_(0), sec_(0) {}
  bool is_valid() const
  {
    return hour_ >= 0 && hour_ <= 24
        && min_ >= 0 && min_ <= 60
        && sec_ >= 0 && sec_ <= 60;
  }
  TO_STRING_KV(K_(hour), K_(min), K_(sec));

  int32_t hour_;
  int32_t min_;
  int32_t sec_;
};

struct ObTTLDutyDuration
{
  ObTTLDutyDuration() : begin_(), end_(), not_set_(true) {}
  bool is_valid() const { return not_set_ || (begin_.is_valid() && end_.is_valid()); }
  TO_STRING_KV(K_(begin), K_(end));

  ObTTLDayTime begin_;
  ObTTLDayTime end_;
  bool not_set_;
};

class ObTTLDutyScheduleParser final
{
public:
  static int parse(const char *str, ObTTLDutyDuration &duration)
  {
    int ret = OB_SUCCESS;
    if (nullptr == str || '\0' == str[0]) {
      duration.not_set_ = true;
    } else {
      ObString input(str);
      const char *begin = input.find('[');
      const char *split = input.find(',');
      const char *end = input.reverse_find(']');
      if (nullptr == begin || nullptr == split || nullptr == end || begin >= split || split >= end) {
        ret = OB_INVALID_CONFIG;
      } else {
        ObString first;
        ObString second;
        first.assign_ptr(begin + 1, static_cast<ObString::obstr_size_t>(split - begin - 1));
        second.assign_ptr(split + 1, static_cast<ObString::obstr_size_t>(end - split - 1));
        if (OB_SUCCESS != (ret = parse_daytime_(first, duration.begin_))
            || OB_SUCCESS != (ret = parse_daytime_(second, duration.end_))) {
        } else {
          duration.not_set_ = false;
        }
      }
    }
    return ret;
  }

private:
  static bool extract_value_(const char *ptr, const uint64_t len, int32_t &value)
  {
    bool found = false;
    char buffer[16] = {0};
    for (uint64_t i = 0; !found && i < len; ++i) {
      if (ptr[i] >= '0' && ptr[i] <= '9') {
        const uint64_t remaining = len - i;
        const uint64_t copy_len = remaining < sizeof(buffer) - 1 ? remaining : sizeof(buffer) - 1;
        MEMCPY(buffer, ptr + i, copy_len);
        value = static_cast<int32_t>(atoi(buffer));
        found = true;
      }
    }
    return found;
  }

  static int parse_daytime_(ObString &input, ObTTLDayTime &daytime)
  {
    int ret = OB_SUCCESS;
    const char *first_split = input.find(':');
    const char *second_split = input.reverse_find(':');
    if (nullptr == first_split || nullptr == second_split || first_split >= second_split
        || !extract_value_(input.ptr(), first_split - input.ptr(), daytime.hour_)
        || !extract_value_(first_split + 1, second_split - first_split - 1, daytime.min_)
        || !extract_value_(second_split + 1, input.length() + input.ptr() - second_split, daytime.sec_)) {
      ret = OB_INVALID_CONFIG;
    }
    return ret;
  }
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_TABLE_OB_TTL_SCHEDULE_H_
