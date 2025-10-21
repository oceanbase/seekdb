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

#include <sys/types.h>
#include "lib/ob_date_unit_type.h"
#include "lib/utility/ob_macro_utils.h"

const char *ob_date_unit_type_str(enum ObDateUnitType type)
{
  static const char *date_unit_type_name[DATE_UNIT_MAX + 1] =
  {
    "microsecond",
    "second",
    "minute",
    "hour",
    "day",
    "week",
    "month",
    "quarter",
    "year",
    "second_microsecond",
    "minute_microsecond",
    "minute_second",
    "hour_microsecond",
    "hour_second",
    "hour_minute",
    "day_microsecond",
    "day_second",
    "day_minute",
    "day_hour",
    "year_month",
    "timezone_hour",
    "timezone_minute",
    "timezone_region",
    "timezone_abbr",
    "unknown",
  };
  static_assert(DATE_UNIT_MAX + 1 == ARRAYSIZEOF(date_unit_type_name),
              "size of array not match enum size");
  return date_unit_type_name[type];
}

const char *ob_date_unit_type_str_upper(enum ObDateUnitType type)
{
  static const char *date_unit_type_name[DATE_UNIT_MAX + 1] =
  {
    "MICROSECOND",
    "SECOND",
    "MINUTE",
    "HOUR",
    "DAY",
    "WEEK",
    "MONTH",
    "QUARTER",
    "YEAR",
    "SECOND_MICROSECOND",
    "MINUTE_MICROSECOND",
    "MINUTE_SECOND",
    "HOUR_MICROSECOND",
    "HOUR_SECOND",
    "HOUR_MINUTE",
    "DAY_MICROSECOND",
    "DAY_SECOND",
    "DAY_MINUTE",
    "DAY_HOUR",
    "YEAR_MONTH",
    "TIMEZONE_HOUR",
    "TIMEZONE_MINUTE",
    "TIMEZONE_REGION",
    "TIMEZONE_ABBR",
    "UNKNOWN",
  };
  static_assert(DATE_UNIT_MAX + 1 == ARRAYSIZEOF(date_unit_type_name),
              "size of array not match enum size");
  return date_unit_type_name[type];
}

const char *ob_get_format_unit_type_str(enum ObGetFormatUnitType type)
{
  static const char *get_format_unit_type_name[GET_FORMAT_MAX + 1] =
  {
    "date",
    "time",
    "datetime",
    "unknown",
  };
  static_assert(GET_FORMAT_MAX + 1 == ARRAYSIZEOF(get_format_unit_type_name),
              "size of array not match enum size");
  return get_format_unit_type_name[type];
}
