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
#include "file_sql_types.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace oceanbase { namespace sql { namespace file_sql {

struct JsonlOptions
{
  size_t max_line_bytes = 16 * 1024 * 1024;
  size_t max_columns = 4096;
  size_t max_batch_bytes = 32 * 1024 * 1024;
};

// One field of a flat JSON object. A field is either a scalar with text, or an
// explicit JSON null.
struct JsonlField
{
  std::string name;
  std::string text;
  bool is_null = false;
};

// Non-owning FILE*. The caller must authorize the path (enable_file_sql,
// secure_file_priv, symlinks) BEFORE opening it. This module never opens a path.
// A seekable regular file is required for full inference followed by streaming.
class JsonlReader
{
public:
  JsonlReader() = default;
  JsonlReader(const JsonlReader &) = delete;
  JsonlReader &operator=(const JsonlReader &) = delete;
  FileStatus init(FILE *input, const JsonlOptions &options = {});
  // Reads one flat JSON object and publishes its fields.
  FileStatus next_record(std::vector<JsonlField> &fields);
  FileStatus rescan();
  FileStatus check_unchanged() const;
  uint64_t record_number() const { return record_; }
  uint64_t record_line() const { return record_line_; }
private:
  FileStatus fail(FileCode code, size_t column, const char *message);
  FILE *input_ = nullptr;
  JsonlOptions options_;
  struct stat snapshot_ {};
  off_t start_ = 0;
  uint64_t record_ = 0;
  uint64_t line_ = 1;
  uint64_t record_line_ = 1;
  bool eof_ = false;
  FileStatus failure_;
};

// The field union is discovered while scanning, so the schema is only known
// after every record has been observed. The first pass keeps only the column
// state, the second pass streams the records. No row is retained in memory.
class JsonlTableScan
{
public:
  FileStatus prepare(FILE *input, const JsonlOptions &options = {}, int64_t sample_size = -1);
  FileStatus next_batch(size_t limit, std::vector<ScalarRow> &rows);
  FileStatus rescan();
  const std::vector<ColumnSchema> &columns() const { return schema_.columns(); }
  uint64_t row_count() const { return schema_.row_count(); }
private:
  // Column order is the order in which the field was first observed.
  FileStatus ensure_column(const std::string &name, size_t &index);
  // First pass: discover the field union and infer the column types.
  FileStatus infer_schema();
  FileStatus build_row(const std::vector<JsonlField> &fields, TextRow &row);
  JsonlReader reader_;
  SchemaInference schema_;
  JsonlOptions options_;
  std::vector<std::string> names_;
  std::unordered_map<std::string, size_t> index_of_name_;
  bool prepared_ = false;
  bool inferred_ = false;
  size_t emitted_ = 0;
};

}}} // namespace oceanbase::sql::file_sql
