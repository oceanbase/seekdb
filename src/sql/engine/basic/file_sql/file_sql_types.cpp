/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "file_sql_types.h"

#include <charconv>
#include <cmath>
#include <system_error>

namespace oceanbase { namespace sql { namespace file_sql {
namespace {
bool digit(char c) { return c >= '0' && c <= '9'; }
bool integer_syntax(std::string_view s)
{
  size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
  if (i == s.size()) { return false; }
  for (; i < s.size(); ++i) { if (!digit(s[i])) { return false; } }
  return true;
}
bool parse_integer(std::string_view s, int64_t &v)
{
  if (!integer_syntax(s)) { return false; }
  if (s[0] == '+') { s.remove_prefix(1); }
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  return r.ec == std::errc() && r.ptr == s.data() + s.size();
}
bool parse_real(std::string_view s, double &v)
{
  size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
  size_t digits = 0;
  while (i < s.size() && digit(s[i])) { ++i; ++digits; }
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && digit(s[i])) { ++i; ++digits; }
  }
  if (digits == 0) { return false; }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { ++i; }
    const size_t start = i;
    while (i < s.size() && digit(s[i])) { ++i; }
    if (i == start) { return false; }
  }
  if (i != s.size()) { return false; }
  if (s[0] == '+') { s.remove_prefix(1); }
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v, std::chars_format::general);
  return r.ec == std::errc() && r.ptr == s.data() + s.size() && std::isfinite(v);
}
bool date_valid(std::string_view s)
{
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') { return false; }
  for (size_t i = 0; i < s.size(); ++i) {
    if (i != 4 && i != 7 && !digit(s[i])) { return false; }
  }
  const int year = (s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
  const int month = (s[5]-'0')*10+s[6]-'0';
  const int day = (s[8]-'0')*10+s[9]-'0';
  if (year < 1 || month < 1 || month > 12) { return false; }
  static constexpr int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year%4 == 0 && (year%100 != 0 || year%400 == 0);
  return day >= 1 && day <= days[month-1] + (month == 2 && leap ? 1 : 0);
}
bool datetime_valid(std::string_view s)
{
  if (s.size() != 19 || !date_valid(s.substr(0,10)) || (s[10] != ' ' && s[10] != 'T')
      || s[13] != ':' || s[16] != ':') { return false; }
  for (size_t i : {size_t(11),size_t(12),size_t(14),size_t(15),size_t(17),size_t(18)}) {
    if (!digit(s[i])) { return false; }
  }
  return (s[11]-'0')*10+s[12]-'0' <= 23
      && (s[14]-'0')*10+s[15]-'0' <= 59 && (s[17]-'0')*10+s[18]-'0' <= 59;
}
} // namespace

const char *scalar_type_name(ScalarType t)
{
  switch (t) {
    case ScalarType::NULL_VALUE: return "NULL";
    case ScalarType::BIGINT: return "BIGINT";
    case ScalarType::DOUBLE: return "DOUBLE";
    case ScalarType::BOOLEAN: return "BOOLEAN";
    case ScalarType::VARCHAR: return "VARCHAR";
    case ScalarType::DATE: return "DATE";
    case ScalarType::DATETIME: return "DATETIME";
  }
  return "NULL";
}

bool valid_utf8(std::string_view s)
{
  size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) { if (c == 0) { return false; } continue; }
    size_t n = 0; uint32_t cp = 0; uint32_t minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 0x1f; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 0x0f; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; minimum = 0x10000; }
    else { return false; }
    if (n > s.size() - i) { return false; }
    for (size_t j = 0; j < n; ++j) {
      const auto d = static_cast<unsigned char>(s[i++]);
      if ((d & 0xc0) != 0x80) { return false; }
      cp = (cp << 6) | (d & 0x3f);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) { return false; }
  }
  return true;
}

ScalarType infer_scalar_type(std::string_view s)
{
  // NULL is carried separately. In particular, a quoted empty string is VARCHAR.
  if (s == "true" || s == "false" || s == "TRUE" || s == "FALSE") { return ScalarType::BOOLEAN; }
  int64_t integer = 0;
  if (integer_syntax(s)) { return parse_integer(s, integer) ? ScalarType::BIGINT : ScalarType::VARCHAR; }
  double real = 0;
  if (parse_real(s, real)) { return ScalarType::DOUBLE; }
  if (date_valid(s)) { return ScalarType::DATE; }
  if (datetime_valid(s)) { return ScalarType::DATETIME; }
  return ScalarType::VARCHAR;
}

ScalarType widen_scalar_type(ScalarType a, ScalarType b)
{
  if (a == ScalarType::NULL_VALUE) { return b; }
  if (b == ScalarType::NULL_VALUE || a == b) { return a; }
  if ((a == ScalarType::BIGINT && b == ScalarType::DOUBLE)
      || (b == ScalarType::BIGINT && a == ScalarType::DOUBLE)) { return ScalarType::DOUBLE; }
  if ((a == ScalarType::DATE && b == ScalarType::DATETIME)
      || (b == ScalarType::DATE && a == ScalarType::DATETIME)) { return ScalarType::DATETIME; }
  return ScalarType::VARCHAR;
}

FileStatus convert_scalar(const TextCell &cell, ScalarType type, ScalarValue &value)
{
  ScalarValue v;
  v.type = type;
  if (!cell.is_null) {
    v.is_null = false;
    bool valid = true;
    switch (type) {
      case ScalarType::BIGINT: valid = parse_integer(cell.text, v.integer); break;
      case ScalarType::DOUBLE: valid = parse_real(cell.text, v.real); break;
      case ScalarType::BOOLEAN:
        valid = infer_scalar_type(cell.text) == ScalarType::BOOLEAN;
        v.integer = (cell.text == "true" || cell.text == "TRUE") ? 1 : 0;
        break;
      case ScalarType::DATE: valid = date_valid(cell.text); v.text = cell.text; break;
      case ScalarType::DATETIME:
        valid = date_valid(cell.text) || datetime_valid(cell.text);
        v.text = cell.text;
        if (v.text.size() == 10) { v.text += " 00:00:00"; }
        else if (v.text.size() == 19) { v.text[10] = ' '; }
        break;
      case ScalarType::VARCHAR: valid = valid_utf8(cell.text); v.text = cell.text; break;
      case ScalarType::NULL_VALUE: valid = false; break;
    }
    if (!valid) { return {FileCode::INVALID_DATA,0,0,0,"value does not match inferred column type"}; }
  }
  value = std::move(v);
  return {};
}

FileStatus SchemaInference::init(const std::vector<std::string> &names)
{
  if (names.empty()) { return {FileCode::INVALID_ARGUMENT,0,0,0,"schema requires at least one column"}; }
  columns_.clear(); row_count_ = 0;
  for (const auto &name : names) { columns_.push_back({name,ScalarType::NULL_VALUE,false}); }
  return {};
}
FileStatus SchemaInference::observe(const TextRow &row, uint64_t record, uint64_t line)
{
  if (columns_.empty() || row.size() != columns_.size()) {
    const size_t column = (row.size() < columns_.size() ? row.size() : columns_.size()) + 1;
    return {FileCode::INVALID_DATA,record,line,column,"CSV record width differs from schema"};
  }
  for (size_t i = 0; i < row.size(); ++i) {
    if (!valid_utf8(row[i].text)) { return {FileCode::INVALID_DATA,record,line,i+1,"invalid UTF-8 or NUL byte"}; }
  }
  for (size_t i = 0; i < row.size(); ++i) {
    if (row[i].is_null) { columns_[i].nullable = true; }
    else { columns_[i].type = widen_scalar_type(columns_[i].type,infer_scalar_type(row[i].text)); }
  }
  ++row_count_;
  return {};
}

}}} // namespace oceanbase::sql::file_sql
