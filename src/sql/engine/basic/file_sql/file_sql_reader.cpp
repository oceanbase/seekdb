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
#include "file_sql_reader.h"

#include "file_sql_csv.h"

#include <cstring>
#include <mutex>

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

namespace
{

// The CSV module of member B already implements the required behaviour, the
// adapter only maps the framework options onto CsvOptions.
class CsvFileScanner : public FileTableScanner
{
public:
  FileStatus prepare(FILE *input, const FileScanOptions &options) override
  {
    CsvOptions csv_options;
    csv_options.header = options.header;
    csv_options.delimiter = options.delimiter;
    csv_options.quote = options.quote;
    csv_options.escape = options.escape;
    csv_options.null_string = options.null_string;
    csv_options.skip_blank_lines = options.skip_blank_lines;
    return scan_.prepare(input, csv_options, options.sample_size);
  }

  FileStatus next_batch(size_t limit, std::vector<FileTableRow> &rows) override
  {
    return scan_.next_batch(limit, rows);
  }

  FileStatus rescan() override { return scan_.rescan(); }

  const std::vector<ColumnSchema> &columns() const override { return scan_.columns(); }

private:
  CsvTableScan scan_;
};

FileTableScanner *create_csv_scanner() { return new (std::nothrow) CsvFileScanner(); }

struct ScannerRegistry
{
  std::mutex mutex_;
  FileScannerFactory factories_[3] = {nullptr, nullptr, nullptr};
};

ScannerRegistry &scanner_registry()
{
  static ScannerRegistry registry;
  return registry;
}

int format_index(FileFormat format) { return static_cast<int>(format); }

} // namespace

FileStatus register_file_scanner(FileFormat format, FileScannerFactory factory)
{
  FileStatus status;
  ScannerRegistry &registry = scanner_registry();
  std::lock_guard<std::mutex> guard(registry.mutex_);
  const int index = format_index(format);
  if (index < 0 || index >= 3) {
    status.code = FileCode::INVALID_ARGUMENT;
    status.message = "unknown file format";
  } else if (nullptr == factory) {
    status.code = FileCode::INVALID_ARGUMENT;
    status.message = "the file format has no scanner";
  } else {
    registry.factories_[index] = factory;
  }
  return status;
}

void unregister_file_scanner(FileFormat format)
{
  ScannerRegistry &registry = scanner_registry();
  std::lock_guard<std::mutex> guard(registry.mutex_);
  const int index = format_index(format);
  if (index >= 0 && index < 3) {
    registry.factories_[index] = nullptr;
  }
}

FileTableScanner *create_file_scanner(FileFormat format)
{
  if (FileFormat::CSV == format) {
    // The csv module of member B is part of the framework, it is always
    // available.
    return create_csv_scanner();
  }
  ScannerRegistry &registry = scanner_registry();
  FileScannerFactory factory = nullptr;
  {
    std::lock_guard<std::mutex> guard(registry.mutex_);
    const int index = format_index(format);
    if (index >= 0 && index < 3) {
      factory = registry.factories_[index];
    }
  }
  if (nullptr == factory) {
    return nullptr;
  }
  return factory();
}

bool parse_file_format(const std::string &name, FileFormat &format)
{
  // The names of the table functions and the short names of the formats are
  // both accepted, read_json and read_ndjson read the same format.
  if (0 == ::strcasecmp(name.c_str(), "read_csv") || 0 == ::strcasecmp(name.c_str(), "csv")) {
    format = FileFormat::CSV;
    return true;
  }
  if (0 == ::strcasecmp(name.c_str(), "read_json") || 0 == ::strcasecmp(name.c_str(), "read_ndjson")
      || 0 == ::strcasecmp(name.c_str(), "json") || 0 == ::strcasecmp(name.c_str(), "ndjson")
      || 0 == ::strcasecmp(name.c_str(), "jsonl") || 0 == ::strcasecmp(name.c_str(), "newline_delimited")) {
    format = FileFormat::JSONL;
    return true;
  }
  if (0 == ::strcasecmp(name.c_str(), "read_parquet") || 0 == ::strcasecmp(name.c_str(), "parquet")) {
    format = FileFormat::PARQUET;
    return true;
  }
  return false;
}

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
