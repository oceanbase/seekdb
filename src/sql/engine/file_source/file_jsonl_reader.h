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

#include "sql/engine/file_source/file_csv_reader.h"
#include "sql/engine/file_source/file_source_def.h"
#include "sql/engine/file_source/file_type_inference.h"

#include <string>
#include <vector>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Streaming reader for newline delimited JSON files.
///
/// Only flat objects are supported, nested objects and arrays are rejected
/// with a diagnostic that points to the offending line. The schema of the
/// file is the union of all fields, missing fields are reported as NULL.
class FileJsonlReader
{
public:
  FileJsonlReader();
  ~FileJsonlReader();

public:
  FileSourceError open(const std::string &path, const FileReadOptions &options);
  FileSourceError infer_schema(FileSchema &schema);
  FileSourceError scan(const FileRowCallback &callback);
  /// Read the next row of the file.
  ///
  /// The method implements the pull model used by the SQL executor, the file
  /// is read line by line and never materialized in memory. It returns false
  /// when the end of the file is reached or when an error occurs, the error
  /// is reported through `error`.
  bool next_row(FileRow &row, FileSourceError &error);
  const FileSignature &signature() const { return signature_; }
  FileSourceError check_unchanged() const;

private:
  class LineReader;

  FileSourceError scan_rows(const FileRowCallback &callback, bool collect_only);

private:
  std::string path_;
  FileReadOptions options_;
  FileSignature signature_;
  FileSchema schema_;
  std::vector<std::string> column_names_;
  bool opened_ = false;
  /// Line reader of the streaming read path, owned by the reader.
  LineReader *reader_ = nullptr;
};

/// Value of a single JSON field of a flat object.
struct JsonFieldValue
{
  /// The textual representation of the value, empty for JSON null.
  std::string text;
  /// True when the value is a JSON null.
  bool is_null = true;
  /// True when the value is a JSON string, the text then holds the decoded
  /// string content.
  bool is_string = false;
};

/// Parse one flat JSON object. Returns false and fills `error` when the input
/// is not a valid flat JSON object.
bool parse_flat_json_object(const std::string &line,
                             std::vector<std::string> &keys,
                             std::vector<JsonFieldValue> &values,
                             std::string &error);

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
