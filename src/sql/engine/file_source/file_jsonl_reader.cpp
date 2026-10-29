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

#include "sql/engine/file_source/file_jsonl_reader.h"
#include "sql/engine/file_source/file_source_util.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace oceanbase {
namespace sql {
namespace file_source {

namespace {

/// Minimal JSON scanner for flat objects.
///
/// The scanner supports the complete JSON grammar for scalar values and
/// reports nested objects and arrays as unsupported, because the file source
/// layer only maps flat columns.
class JsonScanner
{
public:
  explicit JsonScanner(const std::string &text) : text_(text), pos_(0) {}

  bool parse_object(std::vector<std::string> &keys,
                    std::vector<JsonFieldValue> &values,
                    std::string &error)
  {
    skip_space();
    if (!expect('{')) {
      error = "the value is not a JSON object";
      return false;
    }
    skip_space();
    if (peek() == '}') {
      ++pos_;
      skip_space();
      if (pos_ != text_.size()) {
        error = "unexpected trailing content after the JSON object";
        return false;
      }
      return true;
    }
    while (true) {
      skip_space();
      std::string key;
      if (!parse_string(key, error)) {
        return false;
      }
      skip_space();
      if (!expect(':')) {
        error = "expected ':' after the object key";
        return false;
      }
      skip_space();
      JsonFieldValue value;
      if (!parse_value(value, error)) {
        return false;
      }
      keys.push_back(key);
      values.push_back(value);
      skip_space();
      const int c = peek();
      if (',' == c) {
        ++pos_;
        continue;
      }
      if ('}' == c) {
        ++pos_;
        break;
      }
      error = "expected ',' or '}' in the JSON object";
      return false;
    }
    skip_space();
    if (pos_ != text_.size()) {
      error = "unexpected trailing content after the JSON object";
      return false;
    }
    return true;
  }

private:
  int peek() const { return pos_ < text_.size() ? static_cast<unsigned char>(text_[pos_]) : -1; }

  void skip_space()
  {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (' ' == c || '\t' == c || '\n' == c || '\r' == c) {
        ++pos_;
        continue;
      }
      break;
    }
  }

  bool expect(char c)
  {
    if (peek() == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool parse_string(std::string &out, std::string &error)
  {
    if (!expect('"')) {
      error = "expected a JSON string";
      return false;
    }
    out.clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if ('"' == c) {
        return true;
      }
      if ('\\' != c) {
        out.push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) {
        break;
      }
      const char escaped = text_[pos_++];
      switch (escaped) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            error = "invalid unicode escape in the JSON string";
            return false;
          }
          unsigned int code = 0;
          for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9') {
              code |= static_cast<unsigned int>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
              code |= static_cast<unsigned int>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
              code |= static_cast<unsigned int>(h - 'A' + 10);
            } else {
              error = "invalid unicode escape in the JSON string";
              return false;
            }
          }
          // encode the code point as UTF-8
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          error = "invalid escape sequence in the JSON string";
          return false;
      }
    }
    error = "unterminated JSON string";
    return false;
  }

  bool parse_value(JsonFieldValue &value, std::string &error)
  {
    const int c = peek();
    if (-1 == c) {
      error = "unexpected end of the JSON object";
      return false;
    }
    if ('{' == c) {
      error = "nested JSON objects are not supported";
      return false;
    }
    if ('[' == c) {
      error = "nested JSON arrays are not supported";
      return false;
    }
    if ('"' == c) {
      value.is_null = false;
      value.is_string = true;
      return parse_string(value.text, error);
    }
    value.is_null = false;
    value.is_string = false;
    if (text_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      value.text = "true";
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      value.text = "false";
      return true;
    }
    if (text_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
      value.is_null = true;
      value.text.clear();
      return true;
    }
    // number
    const size_t begin = pos_;
    if ('-' == c) {
      ++pos_;
    }
    while (pos_ < text_.size()) {
      const char n = text_[pos_];
      if ((n >= '0' && n <= '9') || '.' == n || 'e' == n || 'E' == n || '+' == n || '-' == n) {
        ++pos_;
        continue;
      }
      break;
    }
    if (begin == pos_) {
      error = "invalid JSON value";
      return false;
    }
    value.text = text_.substr(begin, pos_ - begin);
    return true;
  }

private:
  const std::string &text_;
  size_t pos_;
};




}  // namespace

/// Read the file line by line, the line content is handed to the callback.
/// The file is read with a fixed size buffer, huge lines are still handled
/// correctly because the buffer is refilled on demand.
class FileJsonlReader::LineReader
{
public:
  explicit LineReader(const std::string &path) : path_(path) {}

  FileSourceError open()
  {
    file_ = std::fopen(path_.c_str(), "rb");
    if (NULL == file_) {
      return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot open file '" + path_ + "'");
    }
    buffer_.resize(64 * 1024);
    return FileSourceError();
  }

  void close()
  {
    if (NULL != file_) {
      std::fclose(file_);
      file_ = NULL;
    }
  }

  /// Read the next line, returns false at the end of the file.
  bool next_line(std::string &line, int64_t &line_number)
  {
    line.clear();
    ++line_number;
    bool any = false;
    while (true) {
      if (pos_ >= len_) {
        if (!fill()) {
          break;
        }
      }
      const char c = buffer_[pos_++];
      any = true;
      if ('\n' == c) {
        if (!line.empty() && '\r' == line[line.size() - 1]) {
          line.erase(line.size() - 1);
        }
        return true;
      }
      line.push_back(c);
    }
    if (!any) {
      return false;
    }
    if (!line.empty() && '\r' == line[line.size() - 1]) {
      line.erase(line.size() - 1);
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
      return false;
    }
    pos_ = 0;
    len_ = read;
    return true;
  }

private:
  std::string path_;
  FILE *file_ = NULL;
  std::vector<char> buffer_;
  size_t pos_ = 0;
  size_t len_ = 0;
};

bool parse_flat_json_object(const std::string &line,
                             std::vector<std::string> &keys,
                             std::vector<JsonFieldValue> &values,
                             std::string &error)
{
  keys.clear();
  values.clear();
  JsonScanner scanner(line);
  return scanner.parse_object(keys, values, error);
}

FileJsonlReader::FileJsonlReader() {}

FileJsonlReader::~FileJsonlReader()
{
  if (NULL != reader_) {
    reader_->close();
    delete reader_;
    reader_ = NULL;
  }
}

FileSourceError FileJsonlReader::open(const std::string &path, const FileReadOptions &options)
{
  path_ = path;
  options_ = options;
  if (!get_file_signature(path_, signature_)) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot stat file '" + path + "'");
  }
  if (NULL != reader_) {
    reader_->close();
    delete reader_;
    reader_ = NULL;
  }
  reader_ = new LineReader(path_);
  FileSourceError error = reader_->open();
  if (!error.is_ok()) {
    delete reader_;
    reader_ = NULL;
    return error;
  }
  opened_ = true;
  return FileSourceError();
}

FileSourceError FileJsonlReader::check_unchanged() const
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

FileSourceError FileJsonlReader::infer_schema(FileSchema &schema)
{
  LineReader reader(path_);
  FileSourceError error = reader.open();
  if (!error.is_ok()) {
    return error;
  }
  // the column order follows the first appearance of a field
  std::vector<std::string> column_order;
  std::map<std::string, size_t> column_index;
  std::vector<ColumnTypeAccumulator> accumulators;
  bool nullable = false;
  std::string line;
  int64_t line_number = 0;
  int64_t row_count = 0;
  while (reader.next_line(line, line_number)) {
    // skip blank lines
    bool blank = true;
    for (size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (' ' != c && '\t' != c) {
        blank = false;
        break;
      }
    }
    if (blank) {
      continue;
    }
    std::vector<std::string> keys;
    std::vector<JsonFieldValue> values;
    std::string parse_error;
    if (!parse_flat_json_object(line, keys, values, parse_error)) {
      reader.close();
      return FileSourceError(FileErrorCode::NOT_SUPPORTED, parse_error, line_number);
    }
    for (size_t i = 0; i < keys.size(); ++i) {
      std::map<std::string, size_t>::iterator it = column_index.find(keys[i]);
      size_t idx = 0;
      if (it == column_index.end()) {
        idx = column_order.size();
        column_index[keys[i]] = idx;
        column_order.push_back(keys[i]);
        accumulators.push_back(ColumnTypeAccumulator());
      } else {
        idx = it->second;
      }
      if (values[i].is_null) {
        accumulators[idx].feed_null();
        nullable = true;
      } else {
        accumulators[idx].feed_value(values[i].text);
      }
    }
    // fields that are missing in this row are NULL
    for (size_t i = 0; i < accumulators.size(); ++i) {
      bool present = false;
      for (size_t k = 0; k < keys.size(); ++k) {
        if (keys[k] == column_order[i]) {
          present = true;
          break;
        }
      }
      if (!present) {
        accumulators[i].feed_null();
        nullable = true;
      }
    }
    ++row_count;
    if (options_.max_row_count > 0 && row_count > options_.max_row_count) {
      break;
    }
  }
  reader.close();
  schema.columns.clear();
  for (size_t i = 0; i < column_order.size(); ++i) {
    FileColumn column;
    column.name = column_order[i];
    column.type = accumulators[i].type();
    column.nullable = accumulators[i].has_null();
    schema.columns.push_back(column);
  }
  column_names_ = column_order;
  schema_ = schema;
  return FileSourceError();
}

FileSourceError FileJsonlReader::scan_rows(const FileRowCallback &callback, bool collect_only)
{
  LineReader reader(path_);
  FileSourceError error = reader.open();
  if (!error.is_ok()) {
    return error;
  }
  std::string line;
  int64_t line_number = 0;
  int64_t row_count = 0;
  FileRow row;
  while (reader.next_line(line, line_number)) {
    bool blank = true;
    for (size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (' ' != c && '\t' != c) {
        blank = false;
        break;
      }
    }
    if (blank) {
      continue;
    }
    std::vector<std::string> keys;
    std::vector<JsonFieldValue> values;
    std::string parse_error;
    if (!parse_flat_json_object(line, keys, values, parse_error)) {
      reader.close();
      return FileSourceError(FileErrorCode::NOT_SUPPORTED, parse_error, line_number);
    }
    row.values.clear();
    row.nulls.clear();
    for (size_t i = 0; i < column_names_.size(); ++i) {
      const std::string &name = column_names_[i];
      bool found = false;
      for (size_t k = 0; k < keys.size(); ++k) {
        if (keys[k] != name) {
          continue;
        }
        found = true;
        row.values.push_back(values[k].text);
        row.nulls.push_back(values[k].is_null);
        break;
      }
      if (!found) {
        row.values.push_back("");
        row.nulls.push_back(true);
      }
    }
    ++row_count;
    if (options_.max_row_count > 0 && row_count > options_.max_row_count) {
      reader.close();
      return FileSourceError(FileErrorCode::LIMIT_EXCEEDED, "the row limit of the file scan was exceeded");
    }
    if (!collect_only && !callback(row)) {
      break;
    }
  }
  reader.close();
  return FileSourceError();
}

bool FileJsonlReader::next_row(FileRow &row, FileSourceError &error)
{
  if (!opened_ || NULL == reader_) {
    error = FileSourceError(FileErrorCode::FILE_NOT_FOUND, "the json file was not opened");
    return false;
  }
  std::string line;
  int64_t line_number = 0;
  while (reader_->next_line(line, line_number)) {
    bool blank = true;
    for (size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (' ' != c && '\t' != c) {
        blank = false;
        break;
      }
    }
    if (blank) {
      continue;
    }
    std::vector<std::string> keys;
    std::vector<JsonFieldValue> values;
    std::string parse_error;
    if (!parse_flat_json_object(line, keys, values, parse_error)) {
      error = FileSourceError(FileErrorCode::NOT_SUPPORTED, parse_error, line_number);
      return false;
    }
    if (column_names_.empty()) {
      column_names_ = keys;
    }
    row.values.clear();
    row.nulls.clear();
    for (size_t i = 0; i < column_names_.size(); ++i) {
      const std::string &name = column_names_[i];
      bool found = false;
      for (size_t k = 0; k < keys.size(); ++k) {
        if (keys[k] != name) {
          continue;
        }
        found = true;
        row.values.push_back(values[k].text);
        row.nulls.push_back(values[k].is_null);
        break;
      }
      if (!found) {
        row.values.push_back("");
        row.nulls.push_back(true);
      }
    }
    error = FileSourceError();
    return true;
  }
  error = FileSourceError();
  return false;
}

FileSourceError FileJsonlReader::scan(const FileRowCallback &callback)
{
  if (!opened_) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "the json file was not opened");
  }
  return scan_rows(callback, false);
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
