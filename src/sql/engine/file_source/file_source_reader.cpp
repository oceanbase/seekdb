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

#include "sql/engine/file_source/file_source_reader.h"

#include <algorithm>

namespace oceanbase {
namespace sql {
namespace file_source {

FileSourceReader::FileSourceReader() {}

FileSourceReader::~FileSourceReader() {}

FileSourceError FileSourceReader::open(const std::string &path, const FileReadOptions &options)
{
  path_ = path;
  options_ = options;
  format_ = options.format;
  is_glob_ = false;
  glob_files_.clear();
  glob_index_ = 0;
  if (FileFormat::AUTO == format_) {
    format_ = detect_format_by_extension(path);
    if (FileFormat::AUTO == format_) {
      return FileSourceError(FileErrorCode::UNSUPPORTED_FORMAT,
                             "the format of the file '" + path + "' is not supported");
    }
  }
  switch (format_) {
    case FileFormat::CSV: {
      FileSourceError error = csv_reader_.open(path, options);
      if (!error.is_ok()) {
        return error;
      }
      break;
    }
    case FileFormat::JSON:
    case FileFormat::JSONL: {
      FileSourceError error = jsonl_reader_.open(path, options);
      if (!error.is_ok()) {
        return error;
      }
      break;
    }
    case FileFormat::PARQUET: {
      FileSourceError error = parquet_reader_.open(path, options);
      if (!error.is_ok()) {
        return error;
      }
      break;
    }
    default:
      return FileSourceError(FileErrorCode::UNSUPPORTED_FORMAT,
                             "the format of the file '" + path + "' is not supported");
  }
  opened_ = true;
  return FileSourceError();
}

FileSourceError FileSourceReader::infer_schema(FileSchema &schema)
{
  if (is_glob_) {
    schema.columns.clear();
    FileColumn column;
    column.name = "file";
    column.type = ColumnType::VARCHAR;
    column.nullable = false;
    schema.columns.push_back(column);
    return FileSourceError();
  }
  switch (format_) {
    case FileFormat::CSV:
      return csv_reader_.infer_schema(schema);
    case FileFormat::JSON:
    case FileFormat::JSONL:
      return jsonl_reader_.infer_schema(schema);
    case FileFormat::PARQUET:
      return parquet_reader_.infer_schema(schema);
    default:
      return FileSourceError(FileErrorCode::UNSUPPORTED_FORMAT, "the format of the file is not supported");
  }
}

bool FileSourceReader::next_row(FileRow &row, FileSourceError &error)
{
  if (is_glob_) {
    if (glob_index_ >= glob_files_.size()) {
      error = FileSourceError();
      return false;
    }
    row.values.clear();
    row.nulls.clear();
    row.values.push_back(glob_files_[glob_index_]);
    row.nulls.push_back(false);
    ++glob_index_;
    error = FileSourceError();
    return true;
  }
  switch (format_) {
    case FileFormat::CSV:
      return csv_reader_.next_row(row, error);
    case FileFormat::JSON:
    case FileFormat::JSONL:
      return jsonl_reader_.next_row(row, error);
    case FileFormat::PARQUET:
      return parquet_reader_.next_row(row, error);
    default:
      error = FileSourceError(FileErrorCode::UNSUPPORTED_FORMAT, "the format of the file is not supported");
      return false;
  }
}

const FileSignature &FileSourceReader::signature() const
{
  switch (format_) {
    case FileFormat::CSV:
      return csv_reader_.signature();
    case FileFormat::JSON:
    case FileFormat::JSONL:
      return jsonl_reader_.signature();
    case FileFormat::PARQUET:
      return parquet_reader_.signature();
    default:
      break;
  }
  static const FileSignature empty_signature;
  return empty_signature;
}

FileSourceError FileSourceReader::check_unchanged() const
{
  switch (format_) {
    case FileFormat::CSV:
      return csv_reader_.check_unchanged();
    case FileFormat::JSON:
    case FileFormat::JSONL:
      return jsonl_reader_.check_unchanged();
    case FileFormat::PARQUET:
      return parquet_reader_.check_unchanged();
    default:
      break;
  }
  return FileSourceError();
}

FileSourceError FileSourceReader::list_files(const std::string &pattern, std::vector<std::string> &files)
{
  files.clear();
  if (!glob_files(pattern, files)) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND,
                           "the pattern '" + pattern + "' does not match any file");
  }
  if (files.empty()) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND,
                           "the pattern '" + pattern + "' does not match any file");
  }
  return FileSourceError();
}

FileSourceError FileSourceReader::open_glob(const std::string &pattern)
{
  path_ = pattern;
  format_ = FileFormat::GLOB;
  is_glob_ = true;
  glob_index_ = 0;
  FileSourceError error = list_files(pattern, glob_files_);
  if (!error.is_ok()) {
    return error;
  }
  opened_ = true;
  return FileSourceError();
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
