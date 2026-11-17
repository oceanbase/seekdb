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
#include "file_sql_adapters.h"

#include "file_sql_jsonl.h"
#include "file_sql_parquet.h"

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

namespace
{

// The CSV module reports the end of a file with the END code, the JSONL and
// the parquet modules report it with an empty batch. The framework treats
// only the END code as the end of a scan, so an empty batch is translated
// here. Without it the scan operator would ask for a batch forever.
FileStatus end_of_file_status()
{
  FileStatus status;
  status.code = FileCode::END;
  return status;
}

// The JSONL module of member C implements the required behaviour, the adapter
// only maps the framework options onto JsonlOptions.
class JsonlFileScanner : public FileTableScanner
{
public:
  FileStatus prepare(FILE *input, const FileScanOptions &options) override
  {
    JsonlOptions jsonl_options;
    jsonl_options.max_line_bytes = options.limits.max_line_bytes;
    jsonl_options.max_columns = options.limits.max_columns;
    jsonl_options.max_batch_bytes = options.limits.max_batch_bytes;
    return scan_.prepare(input, jsonl_options, options.sample_size);
  }

  FileStatus next_batch(size_t limit, std::vector<FileTableRow> &rows) override
  {
    const FileStatus status = scan_.next_batch(limit, rows);
    if (status.ok() && rows.empty()) { return end_of_file_status(); }
    return status;
  }

  FileStatus rescan() override { return scan_.rescan(); }

  const std::vector<ColumnSchema> &columns() const override { return scan_.columns(); }

private:
  JsonlTableScan scan_;
};

// The Parquet module of member C implements the required behaviour, the
// adapter only maps the framework options onto ParquetOptions.
class ParquetFileScanner : public FileTableScanner
{
public:
  FileStatus prepare(FILE *input, const FileScanOptions &options) override
  {
    ParquetOptions parquet_options;
    parquet_options.projection = options.projection;
    parquet_options.max_row_groups = options.limits.max_row_groups;
    parquet_options.max_rows = options.limits.max_rows;
    parquet_options.max_batch_bytes = options.limits.max_batch_bytes;
    parquet_options.max_page_bytes = options.limits.max_page_bytes;
    return reader_.init(input, parquet_options);
  }

  FileStatus next_batch(size_t limit, std::vector<FileTableRow> &rows) override
  {
    const FileStatus status = reader_.next_batch(limit, rows);
    if (status.ok() && rows.empty()) { return end_of_file_status(); }
    return status;
  }

  FileStatus rescan() override { return reader_.rescan(); }

  const std::vector<ColumnSchema> &columns() const override { return reader_.columns(); }

private:
  ParquetReader reader_;
};

FileTableScanner *create_jsonl_scanner() { return new (std::nothrow) JsonlFileScanner(); }

FileTableScanner *create_parquet_scanner() { return new (std::nothrow) ParquetFileScanner(); }

// The scanners of the built-in formats register themselves when the shared
// object is loaded. The registry of the framework owns the entries, the
// registration is idempotent.
struct BuiltinScannerRegistrar
{
  BuiltinScannerRegistrar()
  {
    register_file_scanner(FileFormat::JSONL, create_jsonl_scanner);
    register_file_scanner(FileFormat::PARQUET, create_parquet_scanner);
  }
};

const BuiltinScannerRegistrar g_builtin_scanner_registrar;

} // namespace

FileStatus register_builtin_file_scanners()
{
  // The static registration above already did the work. The function exists
  // so that a caller may register the scanners explicitly.
  FileStatus status;
  status = register_file_scanner(FileFormat::JSONL, create_jsonl_scanner);
  if (!status.ok()) { return status; }
  return register_file_scanner(FileFormat::PARQUET, create_parquet_scanner);
}

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
