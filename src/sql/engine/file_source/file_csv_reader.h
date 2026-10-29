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

#pragma once

#include "sql/engine/file_source/file_source_def.h"
#include "sql/engine/file_source/file_type_inference.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Callback invoked for every scanned row. Returning false stops the scan.
typedef std::function<bool(const FileRow &row)> FileRowCallback;

/// Streaming CSV reader.
///
/// The reader supports RFC 4180 style quoting, doubled or escaped quote
/// characters, embedded newlines inside quoted fields, UTF-8 payloads and
/// `LF` / `CRLF` line endings. The file is never materialized in memory, the
/// parser consumes a fixed size buffer.
class FileCsvReader
{
public:
  FileCsvReader();
  ~FileCsvReader();

public:
  /// Open the file and remember the identity used for change detection.
  FileSourceError open(const std::string &path, const FileReadOptions &options);
  /// Scan the whole file and derive the schema of the file.
  FileSourceError infer_schema(FileSchema &schema);
  /// Scan the file and hand every row to the callback.
  FileSourceError scan(const FileRowCallback &callback);
  /// Read the next row of the file.
  ///
  /// The method implements the pull model used by the SQL executor, the file
  /// is read record by record and never materialized in memory. It returns
  /// false when the end of the file is reached or when an error occurs, the
  /// error is reported through `error`.
  bool next_row(FileRow &row, FileSourceError &error);
  /// The identity of the file observed when it was opened.
  const FileSignature &signature() const { return signature_; }
  /// Verify that the file did not change since it was opened.
  FileSourceError check_unchanged() const;

private:
  class Tokenizer;

  FileSourceError read_header();
  FileSourceError scan_rows(const FileRowCallback &callback, bool collect_only);

private:
  std::string path_;
  FileReadOptions options_;
  FileSignature signature_;
  FileSchema schema_;
  std::vector<std::string> header_;
  bool opened_ = false;
  /// Tokenizer of the streaming read path, owned by the reader.
  Tokenizer *tokenizer_ = nullptr;
  /// First record of a file without a header line, it is returned by the
  /// first call of `next_row`.
  std::vector<std::string> pending_fields_;
  std::vector<bool> pending_quoted_;
  int64_t pending_line_ = 0;
  bool has_pending_ = false;
};

/// Build a column name list from the raw header fields, appending the `__N`
/// suffix to duplicated names (`id`, `id__2`, `id__3`, ...).
void build_unique_column_names(const std::vector<std::string> &raw_names, std::vector<std::string> &names);

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
