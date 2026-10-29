/**
 * Copyright (c) 2026 OceanBase
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "sql/engine/file_source/file_csv_reader.h"
#include "sql/engine/file_source/file_jsonl_reader.h"
#include "sql/engine/file_source/file_parquet_reader.h"
#include "sql/engine/file_source/file_source_def.h"
#include "sql/engine/file_source/file_source_util.h"

namespace oceanbase {
namespace sql {
namespace file_source {

/// Unified entry point of the file sources.
///
/// The reader hides the format specific implementations behind one interface:
/// the SQL executor opens the file, asks for the inferred schema and pulls the
/// rows one by one. The file is never materialized in memory, every reader
/// streams the content of the file.
///
/// The reader also supports the `glob` mode, the mode lists the files that
/// match a pattern and reports every file as one row with a single `file`
/// column.
class FileSourceReader
{
public:
  FileSourceReader();
  ~FileSourceReader();

public:
  /// Open the file and detect the format of the file.
  ///
  /// The format is taken from the options, when the options do not define a
  /// format the format is detected by the extension of the file.
  FileSourceError open(const std::string &path, const FileReadOptions &options);
  /// Open the glob mode, every file that matches the pattern is reported as
  /// one row with a single `file` column.
  FileSourceError open_glob(const std::string &pattern);
  /// Infer the schema of the opened file.
  FileSourceError infer_schema(FileSchema &schema);
  /// Read the next row of the file.
  ///
  /// It returns false when the end of the file is reached or when an error
  /// occurs, the error is reported through `error`.
  bool next_row(FileRow &row, FileSourceError &error);
  /// Signature of the file, used to detect that the file was modified while
  /// the query was running.
  const FileSignature &signature() const;
  /// Check that the file was not modified since it was opened.
  FileSourceError check_unchanged() const;

  /// List the files that match the glob pattern.
  ///
  /// The list is sorted by name, duplicated entries are removed.
  static FileSourceError list_files(const std::string &pattern, std::vector<std::string> &files);

public:
  const std::string &path() const { return path_; }
  FileFormat format() const { return format_; }
  /// True when the reader lists the files of a glob pattern.
  bool is_glob() const { return is_glob_; }

private:
  std::string path_;
  FileReadOptions options_;
  FileFormat format_ = FileFormat::AUTO;
  bool is_glob_ = false;
  bool opened_ = false;
  /// Files reported by the glob mode.
  std::vector<std::string> glob_files_;
  size_t glob_index_ = 0;
  FileCsvReader csv_reader_;
  FileJsonlReader jsonl_reader_;
  FileParquetReader parquet_reader_;
};

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
