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

#include <cstdint>
#include <string>
#include <vector>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Logical type of a column produced by a file scan.
/// The set is intentionally small and deterministic, see the "确定性类型推断"
/// requirement of the "file as database" competition task.
enum class ColumnType
{
  NULL_TYPE = 0,
  BOOLEAN,
  BIGINT,
  DOUBLE,
  VARCHAR,
  DATE,
  DATETIME,
};

/// Physical file format understood by the file source layer.
enum class FileFormat
{
  AUTO = 0,
  CSV,
  JSON,
  JSONL,
  PARQUET,
  /// The path is a glob pattern which matches a set of files. Only the
  /// `glob()` table function can produce this format.
  GLOB,
};

/// Error codes reported by the file source layer. They are mapped onto
/// user visible errors by the SQL layer.
enum class FileErrorCode
{
  OK = 0,
  /// The requested path is outside of the allowed `secure_file_priv` area.
  PATH_NOT_ALLOWED,
  /// The file does not exist or cannot be opened.
  FILE_NOT_FOUND,
  /// The file extension / format is not supported.
  UNSUPPORTED_FORMAT,
  /// The content of the file cannot be parsed.
  PARSE_ERROR,
  /// The file does not match the inferred schema (e.g. field count mismatch).
  SCHEMA_MISMATCH,
  /// The file changed between the schema inference pass and the scan pass.
  FILE_CHANGED,
  /// Generic IO failure.
  IO_ERROR,
  /// The requested feature is not supported (e.g. nested JSON values).
  NOT_SUPPORTED,
  /// Resource limit exceeded (row/byte budget).
  LIMIT_EXCEEDED,
};

/// Error object carrying a machine readable code plus a human readable message.
class FileSourceError
{
public:
  FileSourceError() : code_(FileErrorCode::OK), line_(-1) {}
  FileSourceError(FileErrorCode code, const std::string &message, int64_t line = -1)
      : code_(code), message_(message), line_(line)
  {}

public:
  bool is_ok() const { return FileErrorCode::OK == code_; }
  FileErrorCode code() const { return code_; }
  const std::string &message() const { return message_; }
  int64_t line() const { return line_; }
  /// Line number qualified message, used to build user visible errors.
  std::string to_string() const;

private:
  FileErrorCode code_;
  std::string message_;
  int64_t line_;
};

/// Description of one column of a scanned file.
struct FileColumn
{
  std::string name;
  ColumnType type = ColumnType::NULL_TYPE;
  /// True when at least one row carries a NULL value for this column.
  bool nullable = false;
};

/// The (dynamically inferred) schema of a scanned file.
struct FileSchema
{
  std::vector<FileColumn> columns;

  int64_t column_count() const { return static_cast<int64_t>(columns.size()); }
  int64_t find_column(const std::string &name) const;
  const FileColumn *column_at(int64_t idx) const;
};

/// Options shared by all file readers.
struct FileReadOptions
{
  FileFormat format = FileFormat::AUTO;
  /// Whether the first CSV line holds the column names.
  bool has_header = true;
  /// Only `-1` (scan the whole file) is supported, see the task statement.
  int64_t sample_size = -1;
  char delimiter = ',';
  char quote = '"';
  /// Escape character inside a quoted field. Doubling the quote character is
  /// always accepted, independently of this option.
  char escape = '\\';
  /// Text that is interpreted as SQL NULL. An empty string means: unquoted
  /// empty fields are NULL, quoted empty fields are empty strings.
  std::string null_string;
  /// Maximum number of bytes a single field may occupy.
  int64_t max_field_size = 16 * 1024 * 1024;
  /// Maximum number of rows a single scan may produce.
  int64_t max_row_count = 0;  // 0 means unlimited
};

/// A single row of a scanned file. Values are kept in their textual form and
/// converted by the SQL layer according to the inferred schema.
struct FileRow
{
  std::vector<std::string> values;
  std::vector<bool> nulls;
};

/// Identity of a file, used to detect that the file changed between the
/// schema inference pass and the scan pass.
struct FileSignature
{
  int64_t size = -1;
  int64_t mtime_sec = -1;
  int64_t mtime_nsec = -1;

  bool operator==(const FileSignature &other) const
  {
    return size == other.size && mtime_sec == other.mtime_sec && mtime_nsec == other.mtime_nsec;
  }
};

const char *column_type_name(ColumnType type);
/// Parse a textual option value ("csv", "json", "newline_delimited", ...).
bool parse_file_format(const std::string &text, FileFormat &format);

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
