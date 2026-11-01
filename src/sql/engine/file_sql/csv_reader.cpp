#include "csv_reader.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace oceanbase::sql::file_sql {

CsvReader::CsvReader(std::istream &input, size_t max_record_bytes)
  : input_(input), max_record_bytes_(max_record_bytes)
{
  if (input_.peek() == 0xef) {
    for (int i = 0; i < 3; ++i) {
      const int ch = input_.get();
      if (ch == std::char_traits<char>::eof()) break;
      prefix_.push_back(static_cast<char>(ch));
    }
    if (prefix_ == "\xef\xbb\xbf") prefix_.clear();
    if (input_.eof()) input_.clear(input_.rdstate() & ~std::ios::eofbit & ~std::ios::failbit);
  }
}

int CsvReader::get()
{
  return prefix_pos_ < prefix_.size() ? static_cast<unsigned char>(prefix_[prefix_pos_++]) : input_.get();
}
int CsvReader::peek()
{
  return prefix_pos_ < prefix_.size() ? static_cast<unsigned char>(prefix_[prefix_pos_]) : input_.peek();
}

ReadResult CsvReader::next(Row &row)
{
  row.clear();
  if (ended_) return ReadResult::end;
  enum State { start, unquoted, quoted, closed } state = start;
  Field field;
  size_t bytes = 0;
  for (;;) {
    const int ch = get();
    if (ch == std::char_traits<char>::eof()) {
      ended_ = true;
      if (input_.bad()) return ReadResult::io_error;
      if (state == quoted) return ReadResult::invalid;
      if (bytes == 0) return ReadResult::end;
      if (row.size() == 4096) return ReadResult::too_large;
      row.push_back(std::move(field));
      return ReadResult::row;
    }
    if (++bytes > max_record_bytes_) { ended_ = true; return ReadResult::too_large; }
    if (state == quoted) {
      if (ch == '"') state = closed;
      else field.text.push_back(static_cast<char>(ch));
    } else if (state == closed && ch == '"') {
      field.text.push_back('"');
      state = quoted;
    } else if (ch == ',' || ch == '\n' || ch == '\r') {
      if (row.size() == 4096) { ended_ = true; return ReadResult::too_large; }
      row.push_back(std::move(field));
      field = Field{};
      state = start;
      if (ch != ',') {
        if (ch == '\r' && peek() == '\n') get();
        return ReadResult::row;
      }
    } else if (state == closed) {
      ended_ = true;
      return ReadResult::invalid;
    } else if (ch == '"') {
      if (state != start) { ended_ = true; return ReadResult::invalid; }
      state = quoted;
      field.quoted = true;
    } else {
      field.text.push_back(static_cast<char>(ch));
      state = unquoted;
    }
  }
}

static int digits(const std::string &value, size_t pos, size_t count)
{
  int result = 0;
  if (pos + count > value.size()) return -1;
  for (size_t i = 0; i < count; ++i) {
    const unsigned char ch = value[pos + i];
    if (ch < '0' || ch > '9') return -1;
    result = result * 10 + ch - '0';
  }
  return result;
}

static bool valid_date(const std::string &value)
{
  if (value.size() < 10 || value[4] != '-' || value[7] != '-') return false;
  const int year = digits(value, 0, 4), month = digits(value, 5, 2), day = digits(value, 8, 2);
  if (year < 1 || month < 1 || month > 12 || day < 1) return false;
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}

Type classify(const std::string &value)
{
  if (value.empty()) return Type::text;
  std::string lower = value;
  for (char &ch : lower) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
  if (lower == "true" || lower == "false") return Type::boolean;
  int64_t integer = 0;
  const auto int_result = std::from_chars(value.data(), value.data() + value.size(), integer);
  if (int_result.ec == std::errc() && int_result.ptr == value.data() + value.size()) return Type::integer;
  double real = 0;
  const auto real_result = std::from_chars(value.data(), value.data() + value.size(), real);
  if (real_result.ec == std::errc() && real_result.ptr == value.data() + value.size() && std::isfinite(real)) return Type::real;
  if (valid_date(value)) {
    if (value.size() == 10) return Type::date;
    if (value.size() == 19 && (value[10] == ' ' || value[10] == 'T') && value[13] == ':' && value[16] == ':') {
      const int hour = digits(value, 11, 2), minute = digits(value, 14, 2), second = digits(value, 17, 2);
      if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60) return Type::datetime;
    }
  }
  return Type::text;
}

Type widen(Type before, Type after)
{
  if (before == Type::null) return after;
  if (after == Type::null || before == after) return before;
  if ((before == Type::integer && after == Type::real) || (before == Type::real && after == Type::integer)) return Type::real;
  if ((before == Type::date && after == Type::datetime) || (before == Type::datetime && after == Type::date)) return Type::datetime;
  return Type::text;
}

ReadResult infer_csv(std::istream &input, bool header, Schema &schema,
                     const std::function<bool()> &keep_running)
{
  schema.clear();
  if (keep_running && !keep_running()) return ReadResult::cancelled;
  CsvReader reader(input);
  Row row;
  ReadResult result = reader.next(row);
  if (result != ReadResult::row) return result == ReadResult::end ? ReadResult::invalid : result;
  if (row.size() > 4096) return ReadResult::too_large;
  std::unordered_set<std::string> names;
  for (size_t i = 0; i < row.size(); ++i) {
    std::string base = header ? row[i].text : "column" + std::to_string(i);
    if (base.empty()) base = "column" + std::to_string(i);
    std::string name = base;
    for (size_t suffix = 2;; ++suffix) {
      if (keep_running && !keep_running()) return ReadResult::cancelled;
      std::string key = name;
      for (char &ch : key) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
      if (names.insert(key).second) break;
      name = base + "__" + std::to_string(suffix);
    }
    schema.push_back({name, Type::null, false});
  }
  if (header) result = reader.next(row);
  size_t rows = 0;
  while (result == ReadResult::row) {
    if ((rows++ % 1024) == 0 && keep_running && !keep_running()) return ReadResult::cancelled;
    if (row.size() != schema.size()) return ReadResult::invalid;
    for (size_t i = 0; i < row.size(); ++i) {
      schema[i].nullable |= row[i].is_null();
      schema[i].max_bytes = std::max(schema[i].max_bytes, row[i].text.size());
      if (!row[i].is_null()) schema[i].type = widen(schema[i].type, classify(row[i].text));
    }
    result = reader.next(row);
  }
  return result;
}

const char *type_name(Type type)
{
  switch (type) {
    case Type::null: return "NULL";
    case Type::integer: return "BIGINT";
    case Type::real: return "DOUBLE";
    case Type::boolean: return "BOOLEAN";
    case Type::date: return "DATE";
    case Type::datetime: return "DATETIME";
    default: return "VARCHAR";
  }
}
} // namespace oceanbase::sql::file_sql
