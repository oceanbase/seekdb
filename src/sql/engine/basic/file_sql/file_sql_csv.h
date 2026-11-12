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
#include <sys/stat.h>

namespace oceanbase { namespace sql { namespace file_sql {

struct CsvOptions
{
  bool header = true;
  char delimiter = ',';
  char quote = '"';
  char escape = '"';
  std::string null_string;
  // Preserve a single-column NULL row represented by an empty physical record.
  bool skip_blank_lines = false;
  size_t max_columns = 4096;
  size_t max_record_bytes = 16 * 1024 * 1024;
  size_t max_batch_bytes = 32 * 1024 * 1024;
};

// Non-owning FILE*. The caller must authorize the path (enable_file_sql,
// secure_file_priv, symlinks) BEFORE opening it. This module never opens a path.
// A seekable regular file is required for full inference followed by streaming.
class CsvReader
{
public:
  CsvReader() = default;
  CsvReader(const CsvReader &) = delete;
  CsvReader &operator=(const CsvReader &) = delete;
  FileStatus init(FILE *input, const CsvOptions &options = {});
  FileStatus next_row(TextRow &row);
  FileStatus next_batch(size_t limit, std::vector<TextRow> &rows);
  FileStatus rescan();
  FileStatus check_unchanged() const;
  uint64_t record_number() const { return record_; }
  uint64_t record_line() const { return record_line_; }
private:
  FileStatus fail(FileCode code, size_t column, const char *message);
  FILE *input_ = nullptr;
  CsvOptions options_;
  struct stat snapshot_ {};
  off_t start_ = 0;
  uint64_t record_ = 0;
  uint64_t line_ = 1;
  uint64_t record_line_ = 1;
  bool eof_ = false;
  FileStatus failure_;
};

using ScalarRow = std::vector<ScalarValue>;

// First pass retains only column state, second pass produces bounded batches.
// Do not execute filters/aggregations here: the native seekdb operator owns them.
class CsvTableScan
{
public:
  FileStatus prepare(FILE *input, const CsvOptions &options = {}, int64_t sample_size = -1);
  FileStatus next_batch(size_t limit, std::vector<ScalarRow> &rows);
  FileStatus rescan();
  const std::vector<ColumnSchema> &columns() const { return schema_.columns(); }
  uint64_t row_count() const { return schema_.row_count(); }
private:
  CsvReader reader_;
  SchemaInference schema_;
  CsvOptions options_;
  bool prepared_ = false;
};

// Streaming serialization for A's COPY sink. The caller owns atomic publication,
// exclusive destination creation, secure_file_priv and removal on query failure.
class CsvWriter
{
public:
  CsvWriter() = default;
  CsvWriter(const CsvWriter &) = delete;
  CsvWriter &operator=(const CsvWriter &) = delete;
  FileStatus init(FILE *output, const std::vector<std::string> &names, const CsvOptions &options = {});
  FileStatus write_row(const TextRow &row);
  FileStatus finish();
private:
  FileStatus write_record(const TextRow &row);
  FILE *output_ = nullptr;
  CsvOptions options_;
  size_t width_ = 0;
  uint64_t record_ = 0;
  FileStatus failure_;
};

}}} // namespace oceanbase::sql::file_sql
