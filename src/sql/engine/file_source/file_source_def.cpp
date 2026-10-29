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

#include "sql/engine/file_source/file_source_def.h"

#include <cstring>

namespace oceanbase {
namespace sql {
namespace file_source {

namespace {

bool equals_ignore_case(const std::string &lhs, const char *rhs)
{
  const size_t len = ::strlen(rhs);
  if (lhs.size() != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    char a = lhs[i];
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    char b = rhs[i];
    if (b >= 'A' && b <= 'Z') {
      b = static_cast<char>(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string FileSourceError::to_string() const
{
  if (is_ok()) {
    return "";
  }
  std::string result = message_;
  if (line_ >= 0) {
    result += " (line " + std::to_string(line_) + ")";
  }
  return result;
}

int64_t FileSchema::find_column(const std::string &name) const
{
  for (size_t i = 0; i < columns.size(); ++i) {
    if (columns[i].name == name) {
      return static_cast<int64_t>(i);
    }
  }
  return -1;
}

const FileColumn *FileSchema::column_at(int64_t idx) const
{
  if (idx < 0 || static_cast<size_t>(idx) >= columns.size()) {
    return NULL;
  }
  return &columns[static_cast<size_t>(idx)];
}

const char *column_type_name(ColumnType type)
{
  switch (type) {
    case ColumnType::NULL_TYPE: return "null";
    case ColumnType::BOOLEAN: return "boolean";
    case ColumnType::BIGINT: return "bigint";
    case ColumnType::DOUBLE: return "double";
    case ColumnType::VARCHAR: return "varchar";
    case ColumnType::DATE: return "date";
    case ColumnType::DATETIME: return "datetime";
  }
  return "varchar";
}

bool parse_file_format(const std::string &text, FileFormat &format)
{
  if (equals_ignore_case(text, "csv") || equals_ignore_case(text, "text") || equals_ignore_case(text, "tsv")) {
    format = FileFormat::CSV;
    return true;
  }
  if (equals_ignore_case(text, "json")) {
    format = FileFormat::JSON;
    return true;
  }
  if (equals_ignore_case(text, "jsonl") || equals_ignore_case(text, "newline_delimited")
      || equals_ignore_case(text, "ndjson")) {
    format = FileFormat::JSONL;
    return true;
  }
  if (equals_ignore_case(text, "parquet")) {
    format = FileFormat::PARQUET;
    return true;
  }
  if (equals_ignore_case(text, "auto")) {
    format = FileFormat::AUTO;
    return true;
  }
  return false;
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
