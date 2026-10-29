/**
 * Copyright (c) 2026 OceanBase
 *
 * OceanBase is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * OceanBase is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OceanBase. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sql/engine/file_source/file_type_inference.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace oceanbase {
namespace sql {
namespace file_source {

namespace {

bool is_space_char(char c) { return ' ' == c || '\t' == c; }

bool is_digit_char(char c) { return c >= '0' && c <= '9'; }

/// Case insensitive comparison of two ASCII strings.
bool equals_ignore_case(const std::string &lhs, const std::string &rhs)
{
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    char a = lhs[i];
    char b = rhs[i];
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = static_cast<char>(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

/// Strictly parse a non negative decimal number, returns false on overflow.
bool parse_uint64(const char *begin, const char *end, uint64_t &value)
{
  if (begin >= end) {
    return false;
  }
  uint64_t result = 0;
  for (const char *p = begin; p < end; ++p) {
    if (!is_digit_char(*p)) {
      return false;
    }
    uint64_t digit = static_cast<uint64_t>(*p - '0');
    if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
      return false;
    }
    result = result * 10 + digit;
  }
  value = result;
  return true;
}

/// Parse a signed decimal integer and report whether it fits into int64.
bool parse_int64(const std::string &text, int64_t &value)
{
  if (text.empty()) {
    return false;
  }
  const char *begin = text.c_str();
  const char *end = begin + text.size();
  bool negative = false;
  if ('-' == *begin || '+' == *begin) {
    negative = ('-' == *begin);
    ++begin;
  }
  uint64_t magnitude = 0;
  if (!parse_uint64(begin, end, magnitude)) {
    return false;
  }
  const uint64_t limit = negative ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1
                                  : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  if (magnitude > limit) {
    return false;
  }
  if (negative) {
    value = (0 == magnitude) ? 0 : -static_cast<int64_t>(magnitude);
  } else {
    value = static_cast<int64_t>(magnitude);
  }
  return true;
}

/// Parse a decimal number, returns false when the text is not a number.
bool parse_double(const std::string &text, double &value)
{
  if (text.empty()) {
    return false;
  }
  errno = 0;
  char *endptr = NULL;
  const double parsed = std::strtod(text.c_str(), &endptr);
  if (NULL == endptr || endptr != text.c_str() + text.size()) {
    return false;
  }
  value = parsed;
  return true;
}

bool is_leap_year(int year)
{
  return (0 == year % 4 && 0 != year % 100) || 0 == year % 400;
}

int days_in_month(int year, int month)
{
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }
  if (2 == month && is_leap_year(year)) {
    return 29;
  }
  return days[month - 1];
}

/// Parse the `YYYY-MM-DD` prefix of the text, returns false when the prefix is
/// not a valid calendar date.
bool parse_date_prefix(const std::string &text, int &year, int &month, int &day, size_t &consumed)
{
  if (text.size() < 10) {
    return false;
  }
  if (!is_digit_char(text[0]) || !is_digit_char(text[1]) || !is_digit_char(text[2]) || !is_digit_char(text[3])) {
    return false;
  }
  if ('-' != text[4] || !is_digit_char(text[5]) || !is_digit_char(text[6])) {
    return false;
  }
  if ('-' != text[7] || !is_digit_char(text[8]) || !is_digit_char(text[9])) {
    return false;
  }
  year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
  month = (text[5] - '0') * 10 + (text[6] - '0');
  day = (text[8] - '0') * 10 + (text[9] - '0');
  if (month < 1 || month > 12) {
    return false;
  }
  if (day < 1 || day > days_in_month(year, month)) {
    return false;
  }
  consumed = 10;
  return true;
}

}  // namespace

bool ColumnTypeAccumulator::is_date_literal(const std::string &text)
{
  int year = 0;
  int month = 0;
  int day = 0;
  size_t consumed = 0;
  if (!parse_date_prefix(text, year, month, day, consumed)) {
    return false;
  }
  return consumed == text.size();
}

bool ColumnTypeAccumulator::is_datetime_literal(const std::string &text)
{
  int year = 0;
  int month = 0;
  int day = 0;
  size_t consumed = 0;
  if (!parse_date_prefix(text, year, month, day, consumed)) {
    return false;
  }
  if (text.size() == consumed) {
    return false;
  }
  const char sep = text[consumed];
  if (' ' != sep && 'T' != sep && 't' != sep) {
    return false;
  }
  size_t pos = consumed + 1;
  if (pos + 8 > text.size()) {
    return false;
  }
  for (int i = 0; i < 8; ++i) {
    const char c = text[pos + i];
    if (i == 2 || i == 5) {
      if (':' != c) {
        return false;
      }
    } else if (!is_digit_char(c)) {
      return false;
    }
  }
  const int hour = (text[pos] - '0') * 10 + (text[pos + 1] - '0');
  const int minute = (text[pos + 3] - '0') * 10 + (text[pos + 4] - '0');
  const int second = (text[pos + 6] - '0') * 10 + (text[pos + 7] - '0');
  if (hour > 23 || minute > 59 || second > 60) {
    return false;
  }
  pos += 8;
  if (pos == text.size()) {
    return true;
  }
  if ('.' != text[pos]) {
    return false;
  }
  ++pos;
  size_t fraction_len = 0;
  while (pos < text.size() && is_digit_char(text[pos])) {
    ++pos;
    ++fraction_len;
  }
  return fraction_len > 0 && pos == text.size();
}

ColumnType ColumnTypeAccumulator::infer_literal_type(const std::string &text)
{
  if (text.empty()) {
    return ColumnType::NULL_TYPE;
  }
  // boolean literals, case insensitive
  if (equals_ignore_case(text, "true") || equals_ignore_case(text, "false")) {
    return ColumnType::BOOLEAN;
  }
  // integer literal, may overflow int64 and then widen to double
  bool numeric = true;
  size_t start = 0;
  if ('-' == text[0] || '+' == text[0]) {
    start = 1;
  }
  if (start >= text.size()) {
    numeric = false;
  }
  for (size_t i = start; numeric && i < text.size(); ++i) {
    if (!is_digit_char(text[i])) {
      numeric = false;
    }
  }
  if (numeric) {
    int64_t parsed = 0;
    if (parse_int64(text, parsed)) {
      return ColumnType::BIGINT;
    }
    double dbl = 0;
    if (parse_double(text, dbl)) {
      return ColumnType::DOUBLE;
    }
    return ColumnType::VARCHAR;
  }
  // floating point literal, the text may contain a fraction and an exponent
  {
    bool float_like = true;
    bool has_digit = false;
    bool has_dot = false;
    bool has_exp = false;
    for (size_t i = start; i < text.size() && float_like; ++i) {
      const char c = text[i];
      if (is_digit_char(c)) {
        has_digit = true;
      } else if ('.' == c && !has_dot && !has_exp) {
        has_dot = true;
      } else if (('e' == c || 'E' == c) && has_digit && !has_exp) {
        has_exp = true;
        if (i + 1 < text.size() && ('-' == text[i + 1] || '+' == text[i + 1])) {
          ++i;
        }
      } else {
        float_like = false;
      }
    }
    if (float_like && has_digit && has_dot) {
      double dbl = 0;
      if (parse_double(text, dbl)) {
        return ColumnType::DOUBLE;
      }
    }
  }
  if (is_datetime_literal(text)) {
    return ColumnType::DATETIME;
  }
  if (is_date_literal(text)) {
    return ColumnType::DATE;
  }
  return ColumnType::VARCHAR;
}

ColumnType ColumnTypeAccumulator::merge_types(ColumnType lhs, ColumnType rhs)
{
  if (lhs == rhs) {
    return lhs;
  }
  if (ColumnType::NULL_TYPE == lhs) {
    return rhs;
  }
  if (ColumnType::NULL_TYPE == rhs) {
    return lhs;
  }
  if (ColumnType::VARCHAR == lhs || ColumnType::VARCHAR == rhs) {
    return ColumnType::VARCHAR;
  }
  const bool lhs_numeric = (ColumnType::BOOLEAN == lhs || ColumnType::BIGINT == lhs || ColumnType::DOUBLE == lhs);
  const bool rhs_numeric = (ColumnType::BOOLEAN == rhs || ColumnType::BIGINT == rhs || ColumnType::DOUBLE == rhs);
  const bool lhs_temporal = (ColumnType::DATE == lhs || ColumnType::DATETIME == lhs);
  const bool rhs_temporal = (ColumnType::DATE == rhs || ColumnType::DATETIME == rhs);
  // numeric and temporal types never mix, they widen to VARCHAR
  if ((lhs_numeric && rhs_temporal) || (lhs_temporal && rhs_numeric)) {
    return ColumnType::VARCHAR;
  }
  if (ColumnType::DOUBLE == lhs || ColumnType::DOUBLE == rhs) {
    return ColumnType::DOUBLE;
  }
  if (ColumnType::BIGINT == lhs || ColumnType::BIGINT == rhs) {
    // BOOLEAN and BIGINT widen to BIGINT
    return ColumnType::BIGINT;
  }
  if (ColumnType::DATETIME == lhs || ColumnType::DATETIME == rhs) {
    // DATE and DATETIME widen to DATETIME
    return ColumnType::DATETIME;
  }
  if (ColumnType::DATE == lhs || ColumnType::DATE == rhs) {
    // DATE and BOOLEAN widen to VARCHAR
    return ColumnType::VARCHAR;
  }
  if (ColumnType::BOOLEAN == lhs || ColumnType::BOOLEAN == rhs) {
    return ColumnType::VARCHAR;
  }
  return ColumnType::VARCHAR;
}

void ColumnTypeAccumulator::feed_null()
{
  has_null_ = true;
}

void ColumnTypeAccumulator::feed_value(const std::string &text)
{
  type_ = merge_types(type_, infer_literal_type(text));
}

void ColumnTypeAccumulator::merge(const ColumnTypeAccumulator &other)
{
  type_ = merge_types(type_, other.type_);
  has_null_ = has_null_ || other.has_null_;
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
