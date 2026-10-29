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

#include "sql/engine/file_source/file_csv_reader.h"
#include "sql/engine/file_source/file_source_util.h"

#include <cstring>
#include <map>

namespace oceanbase {
namespace sql {
namespace file_source {

const int64_t kReadBufferSize = 64 * 1024;

/// Buffered CSV tokenizer.
///
/// The tokenizer works on a fixed size buffer, it never keeps the whole file
/// in memory. Every record is returned as a list of raw fields together with
/// the information whether the field was quoted, which is required to tell an
/// empty string apart from a NULL value.
class FileCsvReader::Tokenizer
{
public:
  explicit Tokenizer(const FileReadOptions &options) : options_(options) {}

  FileSourceError open(const std::string &path)
  {
    file_ = std::fopen(path.c_str(), "rb");
    if (NULL == file_) {
      return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot open file '" + path + "'");
    }
    buffer_.resize(kReadBufferSize);
    return FileSourceError();
  }

  void close()
  {
    if (NULL != file_) {
      std::fclose(file_);
      file_ = NULL;
    }
  }

  bool is_open() const { return NULL != file_; }

  /// Read the next record. Returns false when the end of the file is reached.
  bool next_record(std::vector<std::string> &fields, std::vector<bool> &quoted, int64_t &record_line)
  {
    fields.clear();
    quoted.clear();
    if (eof_) {
      return false;
    }
    record_line = line_;
    bool has_content = false;
    while (true) {
      bool field_quoted = false;
      std::string field;
      const bool has_field = read_field(field, field_quoted);
      if (has_field) {
        has_content = true;
      }
      fields.push_back(field);
      quoted.push_back(field_quoted);
      if (delimiter_seen_) {
        continue;
      }
      break;
    }
    if (!has_content && fields.size() == 1 && fields[0].empty() && !quoted[0]) {
      // a trailing newline at the end of the file does not produce a record
      return false;
    }
    return true;
  }

private:
  bool fill()
  {
    if (NULL == file_) {
      return false;
    }
    const size_t read = std::fread(&buffer_[0], 1, buffer_.size(), file_);
    if (0 == read) {
      eof_ = true;
      return false;
    }
    pos_ = 0;
    len_ = read;
    return true;
  }

  int peek()
  {
    if (pos_ >= len_) {
      if (!fill()) {
        return -1;
      }
    }
    return static_cast<unsigned char>(buffer_[pos_]);
  }

  int next_char()
  {
    const int c = peek();
    if (c >= 0) {
      ++pos_;
      if ('\n' == c) {
        ++line_;
      }
    }
    return c;
  }

  /// Read one field, returns false when the end of the file was reached
  /// before any character of the field was consumed.
  bool read_field(std::string &field, bool &is_quoted)
  {
    delimiter_seen_ = false;
    is_quoted = false;
    int c = peek();
    if (c < 0) {
      return false;
    }
    if ('"' == c && options_.quote == c) {
      is_quoted = true;
      ++pos_;
      read_quoted_field(field);
      // the closing quote may be followed by the delimiter or the end of the record
      const int after = peek();
      if (after >= 0 && options_.delimiter == after) {
        ++pos_;
        delimiter_seen_ = true;
      } else if ('\n' == after) {
        ++pos_;
        ++line_;
      } else if ('\r' == after) {
        ++pos_;
        if ('\n' == peek()) {
          ++pos_;
        }
        ++line_;
      }
    } else {
      read_plain_field(field);
    }
    return true;
  }

  void read_quoted_field(std::string &field)
  {
    while (true) {
      const int c = next_char();
      if (c < 0) {
        // unterminated quoted field, the remaining content is used as is
        return;
      }
      if ('"' == c) {
        const int next = peek();
        if (options_.quote == next) {
          field.push_back('"');
          ++pos_;
          continue;
        }
        if (options_.escape != options_.quote && options_.escape == next) {
          ++pos_;
          const int escaped = next_char();
          if (escaped >= 0) {
            field.push_back(static_cast<char>(escaped));
          }
          continue;
        }
        return;
      }
      if ('\r' == c) {
        // normalize CRLF inside quoted fields
        const int next = peek();
        if ('\n' == next) {
          ++pos_;
          ++line_;
        }
        field.push_back('\n');
        continue;
      }
      field.push_back(static_cast<char>(c));
      if (static_cast<int64_t>(field.size()) > options_.max_field_size) {
        break;
      }
    }
  }

  void read_plain_field(std::string &field)
  {
    while (true) {
      const int c = peek();
      if (c < 0) {
        break;
      }
      if (options_.delimiter == c) {
        ++pos_;
        delimiter_seen_ = true;
        break;
      }
      if ('\n' == c) {
        ++pos_;
        ++line_;
        break;
      }
      if ('\r' == c) {
        ++pos_;
        const int next = peek();
        if ('\n' == next) {
          ++pos_;
          ++line_;
        }
        break;
      }
      ++pos_;
      field.push_back(static_cast<char>(c));
      if (static_cast<int64_t>(field.size()) > options_.max_field_size) {
        break;
      }
    }
  }

private:
  FileReadOptions options_;
  FILE *file_ = NULL;
  std::vector<char> buffer_;
  size_t pos_ = 0;
  size_t len_ = 0;
  bool eof_ = false;
  bool delimiter_seen_ = false;
  int64_t line_ = 1;
};


void build_unique_column_names(const std::vector<std::string> &raw_names, std::vector<std::string> &names)
{
  names.clear();
  names.reserve(raw_names.size());
  std::map<std::string, int64_t> counters;
  for (size_t i = 0; i < raw_names.size(); ++i) {
    std::string name = raw_names[i];
    // trim the UTF-8 BOM of the first column
    if (0 == i && name.size() >= 3 && static_cast<unsigned char>(name[0]) == 0xEF
        && static_cast<unsigned char>(name[1]) == 0xBB && static_cast<unsigned char>(name[2]) == 0xBF) {
      name = name.substr(3);
    }
    const int64_t seen = counters[name]++;
    if (0 == seen) {
      names.push_back(name);
    } else {
      names.push_back(name + "__" + std::to_string(seen + 1));
    }
  }
}

FileCsvReader::FileCsvReader() {}

FileCsvReader::~FileCsvReader()
{
  if (NULL != tokenizer_) {
    tokenizer_->close();
    delete tokenizer_;
    tokenizer_ = NULL;
  }
}

FileSourceError FileCsvReader::open(const std::string &path, const FileReadOptions &options)
{
  path_ = path;
  options_ = options;
  if (!get_file_signature(path_, signature_)) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot stat file '" + path + "'");
  }
  if (NULL != tokenizer_) {
    tokenizer_->close();
    delete tokenizer_;
    tokenizer_ = NULL;
  }
  tokenizer_ = new Tokenizer(options_);
  FileSourceError error = tokenizer_->open(path_);
  if (!error.is_ok()) {
    delete tokenizer_;
    tokenizer_ = NULL;
    return error;
  }
  header_.clear();
  has_pending_ = false;
  std::vector<std::string> fields;
  std::vector<bool> quoted;
  int64_t record_line = 0;
  if (!tokenizer_->next_record(fields, quoted, record_line)) {
    tokenizer_->close();
    return FileSourceError(FileErrorCode::PARSE_ERROR, "file '" + path + "' is empty");
  }
  if (options_.has_header) {
    build_unique_column_names(fields, header_);
  } else {
    for (size_t i = 0; i < fields.size(); ++i) {
      header_.push_back("column" + std::to_string(i + 1));
    }
    pending_fields_ = fields;
    pending_quoted_ = quoted;
    pending_line_ = record_line;
    has_pending_ = true;
  }
  opened_ = true;
  return FileSourceError();
}

FileSourceError FileCsvReader::check_unchanged() const
{
  FileSignature current;
  if (!get_file_signature(path_, current)) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot stat file '" + path_ + "'");
  }
  if (!(current == signature_)) {
    return FileSourceError(FileErrorCode::FILE_CHANGED, "file '" + path_ + "' changed during the scan");
  }
  return FileSourceError();
}

FileSourceError FileCsvReader::infer_schema(FileSchema &schema)
{
  Tokenizer tokenizer(options_);
  FileSourceError error = tokenizer.open(path_);
  if (!error.is_ok()) {
    return error;
  }
  std::vector<std::string> fields;
  std::vector<bool> quoted;
  int64_t record_line = 0;
  if (!tokenizer.next_record(fields, quoted, record_line)) {
    tokenizer.close();
    return FileSourceError(FileErrorCode::PARSE_ERROR, "file '" + path_ + "' is empty");
  }
  if (options_.has_header) {
    build_unique_column_names(fields, header_);
  } else {
    for (size_t i = 0; i < fields.size(); ++i) {
      header_.push_back("column" + std::to_string(i + 1));
    }
  }
  const int64_t column_count = static_cast<int64_t>(header_.size());
  std::vector<ColumnTypeAccumulator> accumulators(static_cast<size_t>(column_count));
  int64_t row_count = 0;
  bool first_record = true;
  while (true) {
    if (!first_record || options_.has_header) {
      if (!tokenizer.next_record(fields, quoted, record_line)) {
        break;
      }
    }
    first_record = false;
    if (static_cast<int64_t>(fields.size()) != column_count) {
      tokenizer.close();
      return FileSourceError(FileErrorCode::SCHEMA_MISMATCH,
                             "the number of fields does not match the header of file '" + path_ + "'",
                             record_line);
    }
    for (int64_t i = 0; i < column_count; ++i) {
      const std::string &value = fields[static_cast<size_t>(i)];
      const bool is_quoted = quoted[static_cast<size_t>(i)];
      const bool is_null = (!options_.null_string.empty() && value == options_.null_string)
                           || (!is_quoted && value.empty());
      if (is_null) {
        accumulators[static_cast<size_t>(i)].feed_null();
      } else {
        accumulators[static_cast<size_t>(i)].feed_value(value);
      }
    }
    ++row_count;
    if (options_.max_row_count > 0 && row_count > options_.max_row_count) {
      break;
    }
  }
  tokenizer.close();
  schema.columns.clear();
  for (int64_t i = 0; i < column_count; ++i) {
    FileColumn column;
    column.name = header_[static_cast<size_t>(i)];
    column.type = accumulators[static_cast<size_t>(i)].type();
    column.nullable = accumulators[static_cast<size_t>(i)].has_null();
    schema.columns.push_back(column);
  }
  schema_ = schema;
  return FileSourceError();
}

FileSourceError FileCsvReader::scan_rows(const FileRowCallback &callback, bool collect_only)
{
  Tokenizer tokenizer(options_);
  FileSourceError error = tokenizer.open(path_);
  if (!error.is_ok()) {
    return error;
  }
  std::vector<std::string> fields;
  std::vector<bool> quoted;
  int64_t record_line = 0;
  if (options_.has_header) {
    if (!tokenizer.next_record(fields, quoted, record_line)) {
      tokenizer.close();
      return FileSourceError(FileErrorCode::PARSE_ERROR, "file '" + path_ + "' is empty");
    }
  }
  const int64_t column_count = static_cast<int64_t>(header_.size());
  int64_t row_count = 0;
  FileRow row;
  while (tokenizer.next_record(fields, quoted, record_line)) {
    if (static_cast<int64_t>(fields.size()) != column_count) {
      tokenizer.close();
      return FileSourceError(FileErrorCode::SCHEMA_MISMATCH,
                             "the number of fields does not match the header of file '" + path_ + "'",
                             record_line);
    }
    row.values.clear();
    row.nulls.clear();
    for (int64_t i = 0; i < column_count; ++i) {
      const std::string &value = fields[static_cast<size_t>(i)];
      const bool is_quoted = quoted[static_cast<size_t>(i)];
      const bool is_null = (!options_.null_string.empty() && value == options_.null_string)
                           || (!is_quoted && value.empty());
      row.values.push_back(value);
      row.nulls.push_back(is_null);
    }
    ++row_count;
    if (options_.max_row_count > 0 && row_count > options_.max_row_count) {
      tokenizer.close();
      return FileSourceError(FileErrorCode::LIMIT_EXCEEDED, "the row limit of the file scan was exceeded");
    }
    if (!collect_only && !callback(row)) {
      break;
    }
  }
  tokenizer.close();
  return FileSourceError();
}

bool FileCsvReader::next_row(FileRow &row, FileSourceError &error)
{
  if (!opened_ || NULL == tokenizer_) {
    error = FileSourceError(FileErrorCode::FILE_NOT_FOUND, "the csv file was not opened");
    return false;
  }
  std::vector<std::string> fields;
  std::vector<bool> quoted;
  int64_t record_line = 0;
  if (has_pending_) {
    fields = pending_fields_;
    quoted = pending_quoted_;
    record_line = pending_line_;
    has_pending_ = false;
  } else if (!tokenizer_->next_record(fields, quoted, record_line)) {
    error = FileSourceError();
    return false;
  }
  const int64_t column_count = static_cast<int64_t>(header_.size());
  if (static_cast<int64_t>(fields.size()) != column_count) {
    error = FileSourceError(FileErrorCode::SCHEMA_MISMATCH,
                            "the number of fields does not match the header of file '" + path_ + "'",
                            record_line);
    return false;
  }
  row.values.clear();
  row.nulls.clear();
  for (int64_t i = 0; i < column_count; ++i) {
    const std::string &value = fields[static_cast<size_t>(i)];
    const bool is_quoted = quoted[static_cast<size_t>(i)];
    const bool is_null = (!options_.null_string.empty() && value == options_.null_string)
                         || (!is_quoted && value.empty());
    row.values.push_back(value);
    row.nulls.push_back(is_null);
  }
  error = FileSourceError();
  return true;
}

FileSourceError FileCsvReader::scan(const FileRowCallback &callback)
{
  if (!opened_) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "the csv file was not opened");
  }
  return scan_rows(callback, false);
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
