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

// The contract between the seekdb file_sql framework and the per format
// modules. The framework owns the SQL entry, the switch, the path check and
// the execution operator; a format module only has to turn an opened file
// into a schema and then into batches of rows.
//
// A format module never opens a path and never decides whether a path is
// allowed. The framework opens the file after the switch and the
// secure_file_priv check have passed and hands over the borrowed handle.

#include "file_sql_types.h"

#include <cstdio>
#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

enum class FileFormat
{
  CSV,
  JSONL,
  PARQUET,
};

// The resource limits of a scan. The framework owns them, a format module
// only reads the limits that apply to it. The defaults are the limits that
// the modules of member C use on their own.
struct FileScanLimits
{
  // The largest number of columns a file may have.
  size_t max_columns = 4096;
  // The largest number of rows a scan may return.
  size_t max_rows = 64 * 1024 * 1024;
  // The largest number of row groups a parquet file may have.
  size_t max_row_groups = 4096;
  // The largest number of bytes a batch of rows may hold.
  size_t max_batch_bytes = 32 * 1024 * 1024;
  // The largest number of bytes a decompressed page may hold.
  size_t max_page_bytes = 64 * 1024 * 1024;
  // The largest number of bytes a single line of a file may hold.
  size_t max_line_bytes = 16 * 1024 * 1024;
};

// Options a table function accepts. Only the options of the requested format
// are read, the others keep their default value.
struct FileScanOptions
{
  bool header = true;
  int64_t sample_size = -1;
  std::string null_string;
  // read_json/read_ndjson: "newline_delimited".
  std::string format;
  char delimiter = ',';
  char quote = '"';
  char escape = '"';
  bool skip_blank_lines = false;
  // The columns to read. An empty list means every column of the file. Only
  // the parquet module uses it, it is the column pruning of the file.
  std::vector<std::string> projection;
  // The bounds of the scan.
  FileScanLimits limits;
};

// One row of a scanned file. The value type is the one of the shared schema
// module, the row type is declared here so that a format module does not
// have to include the module of another format.
using FileTableRow = std::vector<ScalarValue>;

// A format module implements this interface. The instance is owned by the
// framework and lives as long as one scan of one file.
class FileTableScanner
{
public:
  virtual ~FileTableScanner() = default;

  // Infer the schema of the whole file. The handle stays owned by the
  // caller and must stay valid until the scan ends. After prepare() returned
  // OK the columns are published and must not change afterwards.
  virtual FileStatus prepare(FILE *input, const FileScanOptions &options) = 0;

  // Produce at most `limit` rows. END means that the file is exhausted, any
  // other non OK status must be propagated to the caller.
  virtual FileStatus next_batch(size_t limit, std::vector<FileTableRow> &rows) = 0;

  // Restart the scan at the first data row. The schema is not inferred again.
  // A failure is reported, the caller must not go on with a half rewound scan.
  virtual FileStatus rescan() = 0;

  virtual const std::vector<ColumnSchema> &columns() const = 0;
};

// Factory of a format module. The registry lets a format module be added
// without touching the framework files.
using FileScannerFactory = FileTableScanner *(*)();

FileStatus register_file_scanner(FileFormat format, FileScannerFactory factory);
void unregister_file_scanner(FileFormat format);

// Create a scanner for `format`. Returns NULL when no module is registered.
FileTableScanner *create_file_scanner(FileFormat format);

// Map a table function name to a format. Returns false for unknown names.
bool parse_file_format(const std::string &name, FileFormat &format);

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
