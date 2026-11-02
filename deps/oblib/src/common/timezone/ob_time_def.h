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
// lightweight time type definitions(extracted from ob_time_convert.h，for self-contained inclusion by core headers such as ob_object/ob_datum，
// avoids pulling in the heavyweight ObTimeConverter)。
#ifndef OCEANBASE_LIB_TIMEZONE_OB_TIME_DEF_H_
#define OCEANBASE_LIB_TIMEZONE_OB_TIME_DEF_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"  // TO_STRING_KV

namespace oceanbase
{
namespace common
{

struct ObMySQLDate
{
  ObMySQLDate() : date_(0) {}
  ObMySQLDate(int32_t date) : date_(date) {}
  inline bool operator==(const ObMySQLDate &other) const { return date_ == other.date_; }
  inline bool operator!=(const ObMySQLDate &other) const { return date_ != other.date_; }
  inline bool operator>(const ObMySQLDate &other) const { return date_ > other.date_; }
  inline bool operator<(const ObMySQLDate &other) const { return date_ < other.date_; }
  inline bool operator>=(const ObMySQLDate &other) const { return date_ >= other.date_; }
  inline bool operator<=(const ObMySQLDate &other) const { return date_ <= other.date_; }
  TO_STRING_KV(K_(date), K_(year), K_(month), K_(day));
  union {
    struct {
      uint32_t day_ : 5;
      uint32_t month_ : 4;
      uint32_t year_ : 14;
      uint32_t reserved_ : 9;
    };
    int32_t date_;
  };
};

struct ObMySQLDateTime
{
private:
  static const int32_t DATETIME_YEAR_OFFSET = 13;
public:
  ObMySQLDateTime() : datetime_(0) {}
  ObMySQLDateTime(int64_t datetime) : datetime_(datetime) {}
  inline bool operator==(const ObMySQLDateTime &other) const { return datetime_ == other.datetime_; }
  inline bool operator!=(const ObMySQLDateTime &other) const { return datetime_ != other.datetime_; }
  inline bool operator>(const ObMySQLDateTime &other) const { return datetime_ > other.datetime_; }
  inline bool operator<(const ObMySQLDateTime &other) const { return datetime_ < other.datetime_; }
  inline bool operator>=(const ObMySQLDateTime &other) const { return datetime_ >= other.datetime_; }
  inline bool operator<=(const ObMySQLDateTime &other) const { return datetime_ <= other.datetime_; }
  inline int32_t year() const { return year_month_ / DATETIME_YEAR_OFFSET; }
  inline int32_t month() const { return year_month_ % DATETIME_YEAR_OFFSET; }
  inline static uint64_t year_month(uint64_t year, uint64_t month)
  { return year * DATETIME_YEAR_OFFSET + month; }
  TO_STRING_KV(K_(datetime), "year", year(), "month", month(), K_(day), K_(hour), K_(minute),
               K_(second), K_(microseconds));
  union {
    struct {
      uint64_t microseconds_ : 24;
      uint64_t second_ : 6;
      uint64_t minute_ : 6;
      uint64_t hour_ : 5;
      uint64_t day_ : 5;
      uint64_t year_month_: 17;
      uint64_t sign_ : 1;
    };
    int64_t datetime_;
  };
};

typedef ObMySQLDate MySQLDateType;
typedef ObMySQLDateTime MySQLDateTimeType;

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_TIMEZONE_OB_TIME_DEF_H_
