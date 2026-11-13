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
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace oceanbase { namespace sql { namespace file_sql {

enum class FileCode { OK, END, INVALID_ARGUMENT, INVALID_DATA, IO_ERROR, RESOURCE_LIMIT, FILE_CHANGED };

struct FileStatus
{
  FileCode code = FileCode::OK;
  uint64_t record = 0;
  uint64_t line = 0;
  size_t column = 0;
  std::string message;
  bool ok() const { return code == FileCode::OK; }
};

enum class ScalarType { NULL_VALUE, BIGINT, DOUBLE, BOOLEAN, VARCHAR, DATE, DATETIME };
const char *scalar_type_name(ScalarType type);
bool valid_utf8(std::string_view text);
ScalarType infer_scalar_type(std::string_view text);
ScalarType widen_scalar_type(ScalarType left, ScalarType right);

struct TextCell
{
  std::string text;
  bool is_null = false;
};
using TextRow = std::vector<TextCell>;

struct ColumnSchema
{
  std::string name;
  ScalarType type = ScalarType::NULL_VALUE;
  bool nullable = false;
};

// A nullable, tagged scalar for conversion to ObDatum/ObObj by the SQL scan adapter.
// VARCHAR text is owned by this value; DATETIME text is normalized to a space separator.
struct ScalarValue
{
  ScalarType type = ScalarType::NULL_VALUE;
  bool is_null = true;
  int64_t integer = 0;
  double real = 0;
  std::string text;
};
FileStatus convert_scalar(const TextCell &cell, ScalarType type, ScalarValue &value);

// No rows are retained: memory is proportional to the number of columns.
class SchemaInference
{
public:
  FileStatus init(const std::vector<std::string> &names);
  FileStatus observe(const TextRow &row, uint64_t record = 0, uint64_t line = 0);
  const std::vector<ColumnSchema> &columns() const { return columns_; }
  uint64_t row_count() const { return row_count_; }
private:
  std::vector<ColumnSchema> columns_;
  uint64_t row_count_ = 0;
};

}}} // namespace oceanbase::sql::file_sql
