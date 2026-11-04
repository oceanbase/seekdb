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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_csv_parser.h"
#include "lib/ob_name_def.h"
#include "common/object/ob_obj_cast_helper.h"
#include <cctype>

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

int ObCSVParser::skip_whitespace(const ObString& str, int64_t& pos)
{
  while (pos < str.length() && std::isspace(static_cast<unsigned char>(str[pos]))) {
    pos++;
  }
  return OB_SUCCESS;
}

int ObCSVParser::parse_field(const ObString& str,
                             const CSVOptions& opts,
                             int64_t& pos,
                             ObString& field)
{
  int ret = OB_SUCCESS;
  int64_t start_pos = pos;
  int64_t end_pos = pos;
  bool quoted = false;

  if (pos >= str.length()) {
    field.reset();
    return ret;
  }

  // Check if field is quoted
  if (str[pos] == opts.quote_char) {
    quoted = true;
    pos++; // skip opening quote
    start_pos = pos;

    // Find closing quote
    while (pos < str.length()) {
      if (str[pos] == opts.quote_char) {
        // Check for escaped quote (double quote)
        if (pos + 1 < str.length() && str[pos + 1] == opts.quote_char) {
          pos += 2; // skip both quotes
        } else {
          end_pos = pos;
          pos++; // skip closing quote
          break;
        }
      } else {
        pos++;
      }
    }

    if (!quoted || pos > str.length()) {
      // Unterminated quoted field
      ret = OB_INVALID_ARGUMENT;
    }
  } else {
    // Unquoted field - read until delimiter or end of line
    start_pos = pos;
    while (pos < str.length() && str[pos] != opts.delimiter && str[pos] != '\n' && str[pos] != '\r') {
      pos++;
    }
    end_pos = pos;
  }

  if (OB_SUCC(ret)) {
    field.assign_ptr(str.ptr() + start_pos, static_cast<int32_t>(end_pos - start_pos));
  }

  return ret;
}

ObObjType ObCSVParser::infer_type_from_value(const ObString& value)
{
  if (value.empty()) {
    return ObVarcharType;
  }

  // Try integer
  bool is_int = true;
  int64_t start = 0;
  if (value[0] == '+' || value[0] == '-') {
    start = 1;
  }
  for (int64_t i = start; i < value.length(); i++) {
    if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
      is_int = false;
      break;
    }
  }
  if (is_int && value.length() > start) {
    return ObIntType;
  }

  // Try double
  bool has_dot = false;
  bool has_digit = false;
  bool is_double = true;
  start = 0;
  if (value[0] == '+' || value[0] == '-') {
    start = 1;
  }
  for (int64_t i = start; i < value.length(); i++) {
    if (value[i] == '.') {
      if (has_dot) {
        is_double = false;
        break;
      }
      has_dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(value[i]))) {
      has_digit = true;
    } else if (value[i] == 'e' || value[i] == 'E') {
      // Scientific notation
      break;
    } else {
      is_double = false;
      break;
    }
  }
  if (is_double && has_dot && has_digit) {
    return ObDoubleType;
  }

  // Try boolean
  if (value.length() == 4 &&
      (value.case_compare("true") == 0 || value.case_compare("false") == 0)) {
    return ObTinyIntType;
  }

  // Default to varchar
  return ObVarcharType;
}

int ObCSVParser::parse_row(const ObString& row,
                           const CSVOptions& opts,
                           ObIAllocator& allocator,
                           ObSEArray<ObString, 16>& fields)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;

  fields.reset();

  while (pos <= row.length() && OB_SUCC(ret)) {
    ObString field;
    if (OB_FAIL(parse_field(row, opts, pos, field))) {
      LOG_WARN("Failed to parse field", K(ret), K(row), K(pos));
    } else {
      if (OB_FAIL(fields.push_back(field))) {
        LOG_WARN("Failed to add field to array", K(ret));
      }
    }

    // Skip delimiter
    if (pos < row.length() && row[pos] == opts.delimiter) {
      pos++;
    }
  }

  return ret;
}

int ObCSVParser::parse(const ObString& content,
                       const CSVOptions& opts,
                       ObIAllocator& allocator,
                       ObSEArray<ObString, 16>& rows,
                       ObSEArray<ObString, 16>& headers)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t line_start = 0;
  bool first_line = true;

  rows.reset();
  headers.reset();

  while (pos < content.length() && OB_SUCC(ret)) {
    // Find end of line
    int64_t line_end = pos;
    while (line_end < content.length() &&
           content[line_end] != '\n' &&
           content[line_end] != '\r') {
      line_end++;
    }

    // Extract line
    ObString line;
    line.assign_ptr(content.ptr() + pos, static_cast<int32_t>(line_end - pos));

    // Skip empty lines
    if (line.length() > 0) {
      if (first_line && opts.has_header) {
        // Parse header row
        if (OB_FAIL(parse_row(line, opts, allocator, headers))) {
          LOG_WARN("Failed to parse header row", K(ret));
        }
        first_line = false;
      } else {
        // Parse data row
        ObString row_str;
        if (OB_FAIL(rows.push_back(line))) {
          LOG_WARN("Failed to add row", K(ret));
        }
        first_line = false;
      }
    }

    // Move to next line
    pos = line_end;
    if (pos < content.length()) {
      if (content[pos] == '\r' && pos + 1 < content.length() && content[pos + 1] == '\n') {
        pos += 2; // CRLF
      } else {
        pos++; // LF or CR
      }
    }
  }

  return ret;
}

int ObCSVParser::infer_schema(const ObSEArray<ObString, 16>& rows,
                              const CSVOptions& opts,
                              ObIAllocator& allocator,
                              ObSEArray<ObObjType, 16>& types)
{
  int ret = OB_SUCCESS;
  int64_t sample_count = 0;
  int64_t max_samples = opts.sample_size > 0 ? opts.sample_size : 100;

  types.reset();

  // Parse first row to determine number of columns
  if (rows.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("No rows to infer schema from", K(ret));
  } else {
    ObSEArray<ObString, 16> first_fields;
    if (OB_FAIL(parse_row(rows[0], opts, allocator, first_fields))) {
      LOG_WARN("Failed to parse first row", K(ret));
    } else {
      // Initialize types array
      for (int64_t i = 0; i < first_fields.count() && OB_SUCC(ret); i++) {
        ObObjType col_type = infer_type_from_value(first_fields[i]);
        if (OB_FAIL(types.push_back(col_type))) {
          LOG_WARN("Failed to add column type", K(ret));
        }
      }
      sample_count++;
    }
  }

  // Sample more rows to refine types
  for (int64_t row_idx = 1;
       row_idx < rows.count() && sample_count < max_samples && OB_SUCC(ret);
       row_idx++) {
    ObSEArray<ObString, 16> fields;
    if (OB_FAIL(parse_row(rows[row_idx], opts, allocator, fields))) {
      LOG_WARN("Failed to parse row", K(ret), K(row_idx));
      continue;
    }

    // Update types based on this row
    for (int64_t i = 0; i < fields.count() && i < types.count(); i++) {
      ObObjType field_type = infer_type_from_value(fields[i]);

      // Type promotion logic
      if (types[i] == ObIntType && field_type == ObDoubleType) {
        types[i] = ObDoubleType; // int -> double
      } else if (types[i] != ObVarcharType && field_type == ObVarcharType) {
        types[i] = ObVarcharType; // any -> varchar if non-numeric found
      }
      // tinyint stays tinyint (already most general for booleans)
    }

    sample_count++;
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
