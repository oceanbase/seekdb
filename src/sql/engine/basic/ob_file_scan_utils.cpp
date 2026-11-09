/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_scan_utils.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace sql
{

namespace
{

const int64_t FILE_SCAN_BUFFER_SIZE = 64 * 1024;

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

/// Classify a numeric literal. Returns true when the text is an integer.
bool parse_numeric(const char *data, int64_t len, bool &is_int, bool &is_double, int64_t &int_value)
{
  is_int = false;
  is_double = false;
  int_value = 0;
  if (len <= 0) {
    return false;
  }
  int64_t i = 0;
  if (data[i] == '+' || data[i] == '-') {
    ++i;
  }
  if (i >= len) {
    return false;
  }
  int64_t int_part = 0;
  bool has_digits = false;
  bool overflow = false;
  for (; i < len && is_digit(data[i]); ++i) {
    has_digits = true;
    if (int_part > (INT64_MAX - 9) / 10) {
      overflow = true;
    } else {
      int_part = int_part * 10 + (data[i] - '0');
    }
  }
  if (!has_digits) {
    return false;
  }
  bool has_dot = false;
  bool has_exp = false;
  int64_t j = i;
  if (j < len && data[j] == '.') {
    has_dot = true;
    ++j;
    while (j < len && is_digit(data[j])) {
      ++j;
    }
  }
  if (j < len && (data[j] == 'e' || data[j] == 'E')) {
    has_exp = true;
    ++j;
    if (j < len && (data[j] == '+' || data[j] == '-')) {
      ++j;
    }
    while (j < len && is_digit(data[j])) {
      ++j;
    }
  }
  if (j != len) {
    return false;
  }
  if (overflow || has_dot || has_exp) {
    is_double = true;
    return true;
  }
  is_int = true;
  int_value = ('-' == data[0]) ? -int_part : int_part;
  return true;
}

bool parse_bool(const char *data, int64_t len, bool &value)
{
  if (4 == len && 0 == strncasecmp(data, "true", 4)) {
    value = true;
    return true;
  }
  if (5 == len && 0 == strncasecmp(data, "false", 5)) {
    value = false;
    return true;
  }
  return false;
}

/// Copy an ObString path into a NUL terminated buffer, an ObString is not NUL terminated by contract.
int copy_path(const ObString &path, char *buf, int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (path.length() < 0 || path.length() >= buf_len) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    memcpy(buf, path.ptr(), static_cast<size_t>(path.length()));
    buf[path.length()] = '\0';
  }
  return ret;
}

int file_seek(FILE *fp, int64_t offset, int whence)
{
#ifdef _WIN32
  return _fseeki64(fp, offset, whence);
#else
  return fseeko(fp, offset, whence);
#endif
}

int64_t file_tell(FILE *fp)
{
#ifdef _WIN32
  return _ftelli64(fp);
#else
  return ftello(fp);
#endif
}

/// Open a file for reading and report its size.
int open_scan_file(const ObString &path, FILE *&fp, int64_t &size)
{
  int ret = OB_SUCCESS;
  char path_buf[PATH_MAX + 1];
  fp = NULL;
  size = 0;
  if (OB_FAIL(ret = copy_path(path, path_buf, sizeof(path_buf)))) {
    LOG_USER_ERROR(OB_FILE_NOT_EXIST, "file scan cannot access the file");
  } else if (OB_ISNULL(fp = fopen(path_buf, "rb"))) {
    ret = OB_FILE_NOT_EXIST;
    LOG_USER_ERROR(OB_FILE_NOT_EXIST, "file scan cannot open the file");
  } else if (0 != file_seek(fp, 0, SEEK_END)) {
    ret = OB_IO_ERROR;
    LOG_USER_ERROR(OB_IO_ERROR, "file scan seek failed");
  } else if ((size = file_tell(fp)) < 0) {
    ret = OB_IO_ERROR;
    LOG_USER_ERROR(OB_IO_ERROR, "file scan seek failed");
  } else {
    // the handle stays where it is, every read seeks explicitly
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(fp)) {
    fclose(fp);
    fp = NULL;
  }
  return ret;
}

/// Positional read, FILE* based so that the offset is honoured on every platform.
int file_pread(FILE *fp, void *buf, int64_t count, int64_t offset, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  read_size = 0;
  if (OB_ISNULL(fp)) {
    ret = OB_IO_ERROR;
    LOG_USER_ERROR(OB_IO_ERROR, "file scan read failed");
  } else if (0 != file_seek(fp, offset, SEEK_SET)) {
    ret = OB_IO_ERROR;
    LOG_USER_ERROR(OB_IO_ERROR, "file scan seek failed");
  } else {
    read_size = static_cast<int64_t>(fread(buf, 1, static_cast<size_t>(count), fp));
    if (read_size < count && 0 != ferror(fp)) {
      ret = OB_IO_ERROR;
      LOG_USER_ERROR(OB_IO_ERROR, "file scan read failed");
    }
  }
  return ret;
}

/// Buffered sequential reader on top of stdio.
class ScanFile
{
public:
  ScanFile() : fp_(NULL), offset_(0), size_(0), buf_(NULL), buf_len_(0), buf_pos_(0), eof_(false) {}
  ~ScanFile() { close(); }
  int open(const ObString &path, ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    close();
    if (OB_FAIL(ret = open_scan_file(path, fp_, size_))) {
    } else if (NULL == (buf_ = static_cast<char *>(allocator.alloc(FILE_SCAN_BUFFER_SIZE)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    return ret;
  }
  void close()
  {
    if (OB_NOT_NULL(fp_)) {
      fclose(fp_);
      fp_ = NULL;
    }
    buf_ = NULL;
    buf_len_ = 0;
    buf_pos_ = 0;
    eof_ = false;
  }
  /// Read one byte. Returns OB_ITER_END at the end of the file.
  int getc(char &c)
  {
    int ret = OB_SUCCESS;
    if (buf_pos_ >= buf_len_) {
      if (eof_) {
        return OB_ITER_END;
      }
      if (OB_FAIL(ret = fill())) {
        return ret;
      }
      if (0 == buf_len_) {
        eof_ = true;
        return OB_ITER_END;
      }
    }
    c = buf_[buf_pos_++];
    return OB_SUCCESS;
  }
  /// Look at the next byte without consuming it.
  int peek(char &c)
  {
    int ret = OB_SUCCESS;
    if (buf_pos_ >= buf_len_) {
      if (eof_) {
        return OB_ITER_END;
      }
      if (OB_FAIL(ret = fill())) {
        return ret;
      }
      if (0 == buf_len_) {
        eof_ = true;
        return OB_ITER_END;
      }
    }
    c = buf_[buf_pos_];
    return OB_SUCCESS;
  }
  int64_t size() const { return size_; }
private:
  int fill()
  {
    int ret = OB_SUCCESS;
    int64_t read_size = 0;
    buf_pos_ = 0;
    buf_len_ = 0;
    if (offset_ >= size_) {
      return OB_SUCCESS;
    }
    const int64_t to_read = std::min<int64_t>(FILE_SCAN_BUFFER_SIZE, size_ - offset_);
    if (OB_FAIL(ret = file_pread(fp_, buf_, to_read, offset_, read_size))) {
    } else {
      offset_ += read_size;
      buf_len_ = read_size;
    }
    return ret;
  }
private:
  FILE *fp_;
  int64_t offset_;
  int64_t size_;
  char *buf_;
  int64_t buf_len_;
  int64_t buf_pos_;
  bool eof_;
};

// ---------------------------------------------------------------------------
// CSV reader
// ---------------------------------------------------------------------------
class CsvFileScanReader : public FileScanReader
{
public:
  int open(const ObString &path, const FileScanOptions &options, ObIAllocator &allocator) override
  {
    options_ = options;
    row_num_ = 0;
    header_read_ = false;
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret = file_.open(path, allocator))) {
    } else if (options_.has_header_ && OB_FAIL(ret = read_header())) {
    }
    return ret;
  }
  int next_row() override
  {
    int ret = OB_SUCCESS;
    if (options_.has_header_ && !header_read_) {
      if (OB_FAIL(ret = read_header())) {
        return ret;
      }
    }
    if (OB_FAIL(ret = parse_row())) {
      return ret;
    }
    ++row_num_;
    if (!options_.has_header_ && 0 == expected_columns_) {
      expected_columns_ = row_.size();
    }
    if (row_.size() != expected_columns_) {
      LOG_USER_ERROR(OB_INVALID_DATA,
                     "csv row %ld holds %ld fields while the header declares %ld",
                     row_num_,
                     row_.size(),
                     expected_columns_);
      return OB_INVALID_DATA;
    }
    fields_.reuse();
    for (size_t i = 0; i < row_.size(); ++i) {
      const std::string &value = row_[i];
      bool matches_null_str = false;
      if (!options_.null_str_.empty()
          && static_cast<size_t>(options_.null_str_.length()) == value.length()) {
        matches_null_str = 0 == memcmp(value.data(), options_.null_str_.ptr(), value.length());
      }
      const bool is_null = !quoted_[i] && (value.empty() || matches_null_str);
      if (OB_FAIL(ret = fields_.push_back(is_null ? ObString(0, NULL)
                                                  : ObString(static_cast<int32_t>(value.length()),
                                                             value.data())))) {
        return ret;
      }
    }
    return OB_SUCCESS;
  }
  const FileScanFields &fields() const override { return fields_; }
  void close() override { file_.close(); }
  int64_t row_count() const override { return -1; }
  const std::vector<std::string> &header() const { return header_; }
private:
  int read_header()
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret = parse_row())) {
      if (OB_ITER_END == ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_USER_ERROR(OB_ERR_UNEXPECTED, "the csv file has no header row");
      }
      return ret;
    }
    header_.swap(row_);
    row_.clear();
    quoted_.clear();
    header_read_ = true;
    expected_columns_ = header_.size();
    return OB_SUCCESS;
  }
  /// Parse one record into row_ and quoted_.
  int parse_row()
  {
    int ret = OB_SUCCESS;
    row_.clear();
    quoted_.clear();
    std::string cur;
    bool in_quotes = false;
    bool field_quoted = false;
    bool has_data = false;
    while (true) {
      char c = 0;
      int peek_ret = file_.peek(c);
      if (OB_ITER_END == peek_ret) {
        if (!has_data && row_.empty()) {
          return OB_ITER_END;
        }
        row_.push_back(cur);
        quoted_.push_back(field_quoted);
        return OB_SUCCESS;
      } else if (OB_FAIL(peek_ret)) {
        return peek_ret;
      }
      if (in_quotes) {
        file_.getc(c);
        if (c == options_.quote_char_) {
          char next = 0;
          if (OB_SUCC(file_.peek(next)) && next == options_.quote_char_
              && options_.escape_char_ == options_.quote_char_) {
            file_.getc(next);
            cur.push_back(options_.quote_char_);
          } else {
            in_quotes = false;
          }
        } else if (c == options_.escape_char_ && options_.escape_char_ != options_.quote_char_) {
          char escaped = 0;
          if (OB_SUCC(file_.getc(escaped))) {
            cur.push_back(escaped);
          }
        } else {
          cur.push_back(c);
        }
        has_data = true;
        continue;
      }
      if (c == options_.quote_char_ && cur.empty() && !field_quoted) {
        file_.getc(c);
        in_quotes = true;
        field_quoted = true;
        has_data = true;
        continue;
      }
      if (c == options_.field_delim_) {
        file_.getc(c);
        row_.push_back(cur);
        quoted_.push_back(field_quoted);
        cur.clear();
        field_quoted = false;
        has_data = true;
        continue;
      }
      if (c == '\n' || c == '\r') {
        file_.getc(c);
        if (c == '\r') {
          char next = 0;
          if (OB_SUCC(file_.peek(next)) && next == '\n') {
            file_.getc(next);
          }
        }
        row_.push_back(cur);
        quoted_.push_back(field_quoted);
        return OB_SUCCESS;
      }
      file_.getc(c);
      cur.push_back(c);
      has_data = true;
    }
  }
private:
  ScanFile file_;
  FileScanOptions options_;
  FileScanFields fields_;
  std::vector<std::string> row_;
  std::vector<bool> quoted_;
  std::vector<std::string> header_;
  int64_t row_num_ = 0;
  size_t expected_columns_ = 0;
  bool header_read_ = false;
};

} // namespace

namespace
{

// ---------------------------------------------------------------------------
// Minimal JSON object parser used by the jsonl reader
// ---------------------------------------------------------------------------
struct JsonValue
{
  FileScanValueType type_;
  std::string text_;
  JsonValue() : type_(FILE_SCAN_VALUE_NULL) {}
};

class JsonLineParser
{
public:
  int parse(const char *data, int64_t len)
  {
    keys_.clear();
    values_.clear();
    data_ = data;
    pos_ = 0;
    len_ = len;
    skip_space();
    int ret = OB_SUCCESS;
    if (pos_ >= len_ || '{' != data_[pos_]) {
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "a jsonl record must be a json object");
      return ret;
    }
    if (OB_FAIL(ret = parse_object())) {
      return ret;
    }
    skip_space();
    if (pos_ != len_) {
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "trailing characters after the jsonl record");
    }
    return ret;
  }
  const std::vector<std::string> &keys() const { return keys_; }
  const std::vector<JsonValue> &values() const { return values_; }
private:
  void skip_space()
  {
    while (pos_ < len_ && (' ' == data_[pos_] || '\t' == data_[pos_] || '\r' == data_[pos_]
                           || '\n' == data_[pos_])) {
      ++pos_;
    }
  }
  int parse_object()
  {
    int ret = OB_SUCCESS;
    ++pos_; // consume '{'
    skip_space();
    if (pos_ < len_ && '}' == data_[pos_]) {
      ++pos_;
      return OB_SUCCESS;
    }
    while (OB_SUCC(ret)) {
      skip_space();
      std::string key;
      if (OB_FAIL(ret = parse_string(key))) {
        break;
      }
      skip_space();
      if (pos_ >= len_ || ':' != data_[pos_]) {
        ret = OB_INVALID_DATA;
        LOG_USER_ERROR(OB_INVALID_DATA, "a jsonl key must be followed by ':'");
        break;
      }
      ++pos_;
      skip_space();
      JsonValue value;
      if (OB_FAIL(ret = parse_value(value))) {
        break;
      }
      keys_.push_back(key);
      values_.push_back(value);
      skip_space();
      if (pos_ < len_ && ',' == data_[pos_]) {
        ++pos_;
        continue;
      }
      if (pos_ < len_ && '}' == data_[pos_]) {
        ++pos_;
        break;
      }
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "malformed jsonl object");
      break;
    }
    return ret;
  }
  int parse_value(JsonValue &value)
  {
    int ret = OB_SUCCESS;
    if (pos_ >= len_) {
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "unexpected end of the jsonl record");
      return ret;
    }
    const char c = data_[pos_];
    if ('"' == c) {
      value.type_ = FILE_SCAN_VALUE_VARCHAR;
      if (OB_FAIL(ret = parse_string(value.text_))) {
        return ret;
      }
      return OB_SUCCESS;
    }
    if ('{' == c || '[' == c) {
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "nested json objects and arrays are not supported");
      return ret;
    }
    if ('t' == c) {
      if (pos_ + 4 <= len_ && 0 == strncmp(data_ + pos_, "true", 4)) {
        pos_ += 4;
        value.type_ = FILE_SCAN_VALUE_BOOL;
        value.text_ = "true";
        return OB_SUCCESS;
      }
    } else if ('f' == c) {
      if (pos_ + 5 <= len_ && 0 == strncmp(data_ + pos_, "false", 5)) {
        pos_ += 5;
        value.type_ = FILE_SCAN_VALUE_BOOL;
        value.text_ = "false";
        return OB_SUCCESS;
      }
    } else if ('n' == c) {
      if (pos_ + 4 <= len_ && 0 == strncmp(data_ + pos_, "null", 4)) {
        pos_ += 4;
        value.type_ = FILE_SCAN_VALUE_NULL;
        return OB_SUCCESS;
      }
    }
    // number
    const int64_t start = pos_;
    bool is_int = false;
    bool is_double = false;
    while (pos_ < len_
           && (is_digit(data_[pos_]) || '.' == data_[pos_] || 'e' == data_[pos_]
               || 'E' == data_[pos_] || '+' == data_[pos_] || '-' == data_[pos_])) {
      ++pos_;
    }
    const int64_t text_len = pos_ - start;
    int64_t int_value = 0;
    if (text_len > 0 && parse_numeric(data_ + start, text_len, is_int, is_double, int_value)) {
      value.type_ = is_int ? FILE_SCAN_VALUE_INT : FILE_SCAN_VALUE_DOUBLE;
      value.text_.assign(data_ + start, text_len);
      return OB_SUCCESS;
    }
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA, "unsupported json value in a jsonl record");
    return ret;
  }
  int parse_string(std::string &out)
  {
    int ret = OB_SUCCESS;
    if (pos_ >= len_ || '"' != data_[pos_]) {
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "a json string was expected");
      return ret;
    }
    ++pos_;
    out.clear();
    while (pos_ < len_) {
      const char c = data_[pos_++];
      if ('"' == c) {
        return OB_SUCCESS;
      }
      if ('\\' == c) {
        if (pos_ >= len_) {
          break;
        }
        const char esc = data_[pos_++];
        switch (esc) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'u': {
            if (pos_ + 4 > len_) {
              ret = OB_INVALID_DATA;
              LOG_USER_ERROR(OB_INVALID_DATA, "invalid unicode escape in a jsonl record");
              return ret;
            }
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = data_[pos_ + i];
              code <<= 4;
              if (h >= '0' && h <= '9') {
                code |= static_cast<unsigned int>(h - '0');
              } else if (h >= 'a' && h <= 'f') {
                code |= static_cast<unsigned int>(h - 'a' + 10);
              } else if (h >= 'A' && h <= 'F') {
                code |= static_cast<unsigned int>(h - 'A' + 10);
              } else {
                ret = OB_INVALID_DATA;
                LOG_USER_ERROR(OB_INVALID_DATA, "invalid unicode escape in a jsonl record");
                return ret;
              }
            }
            pos_ += 4;
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
          default: out.push_back(esc); break;
        }
        continue;
      }
      out.push_back(c);
    }
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA, "unterminated json string in a jsonl record");
    return ret;
  }
private:
  const char *data_ = NULL;
  int64_t pos_ = 0;
  int64_t len_ = 0;
  std::vector<std::string> keys_;
  std::vector<JsonValue> values_;
};

// ---------------------------------------------------------------------------
// JSONL reader
// ---------------------------------------------------------------------------
class JsonlFileScanReader : public FileScanReader
{
public:
  int open(const ObString &path, const FileScanOptions &options, ObIAllocator &allocator) override
  {
    options_ = options;
    line_num_ = 0;
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret = file_.open(path, allocator))) {
      return ret;
    }
    return ret;
  }
  int next_row() override
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret = read_line())) {
      return ret;
    }
    ++line_num_;
    if (OB_FAIL(ret = parser_.parse(line_.data(), line_.length()))) {
      return ret;
    }
    fields_.reuse();
    for (int64_t i = 0; OB_SUCC(ret) && i < options_.column_names_.count(); ++i) {
      bool found = false;
      const ObString &column_name = options_.column_names_.at(i);
      const std::string key(column_name.ptr(), static_cast<size_t>(column_name.length()));
      const std::vector<std::string> &keys = parser_.keys();
      for (size_t k = 0; k < keys.size(); ++k) {
        if (keys[k] == key) {
          const JsonValue &value = parser_.values()[k];
          if (FILE_SCAN_VALUE_NULL == value.type_) {
            if (OB_FAIL(ret = fields_.push_back(ObString(0, NULL)))) {
              return ret;
            }
          } else if (OB_FAIL(ret = fields_.push_back(ObString(static_cast<int32_t>(value.text_.length()),
                                                             value.text_.data())))) {
            return ret;
          }
          found = true;
          break;
        }
      }
      if (!found && OB_FAIL(ret = fields_.push_back(ObString(0, NULL)))) {
        return ret;
      }
    }
    return OB_SUCCESS;
  }
  const FileScanFields &fields() const override { return fields_; }
  const std::vector<std::string> &last_keys() const { return parser_.keys(); }
  const std::vector<JsonValue> &last_values() const { return parser_.values(); }
  void close() override { file_.close(); }
private:
  int read_line()
  {
    int ret = OB_SUCCESS;
    line_.clear();
    bool has_data = false;
    while (true) {
      char c = 0;
      int peek_ret = file_.peek(c);
      if (OB_ITER_END == peek_ret) {
        if (!has_data && line_.empty()) {
          return OB_ITER_END;
        }
        return OB_SUCCESS;
      } else if (OB_FAIL(peek_ret)) {
        return peek_ret;
      }
      file_.getc(c);
      if ('\n' == c) {
        return OB_SUCCESS;
      }
      if ('\r' == c) {
        char next = 0;
        if (OB_SUCC(file_.peek(next)) && '\n' == next) {
          file_.getc(next);
        }
        return OB_SUCCESS;
      }
      line_.push_back(c);
      has_data = true;
    }
  }
private:
  ScanFile file_;
  FileScanOptions options_;
  FileScanFields fields_;
  JsonLineParser parser_;
  std::string line_;
  int64_t line_num_ = 0;
};

} // namespace

namespace
{

// ---------------------------------------------------------------------------
// Thrift compact protocol reader
// ---------------------------------------------------------------------------
const int8_t T_STOP = 0;
const int8_t T_BOOLEAN_TRUE = 1;
const int8_t T_BOOLEAN_FALSE = 2;
const int8_t T_BYTE = 3;
const int8_t T_I16 = 4;
const int8_t T_I32 = 5;
const int8_t T_I64 = 6;
const int8_t T_DOUBLE = 7;
const int8_t T_BINARY = 8;
const int8_t T_LIST = 9;
const int8_t T_SET = 10;
const int8_t T_MAP = 11;
const int8_t T_STRUCT = 12;

class ThriftCompact
{
public:
  ThriftCompact(const char *data, int64_t len) : data_(data), len_(len), pos_(0), last_field_id_(0) {}
  bool read_field_begin(int16_t &field_id, int8_t &type)
  {
    uint8_t b = 0;
    if (!read_byte(b)) {
      return false;
    }
    if (T_STOP == b) {
      field_id = 0;
      type = T_STOP;
      return true;
    }
    type = static_cast<int8_t>(b & 0x0F);
    const int8_t delta = static_cast<int8_t>((b & 0xF0) >> 4);
    if (0 == delta) {
      int64_t zigzag = 0;
      if (!read_zigzag(zigzag)) {
        return false;
      }
      field_id = static_cast<int16_t>(zigzag);
    } else {
      field_id = static_cast<int16_t>(last_field_id_ + delta);
    }
    last_field_id_ = field_id;
    return true;
  }
  bool read_varint(uint64_t &value)
  {
    value = 0;
    int shift = 0;
    while (pos_ < len_) {
      const uint8_t b = static_cast<uint8_t>(data_[pos_++]);
      value |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (0 == (b & 0x80)) {
        return true;
      }
      shift += 7;
      if (shift > 63) {
        return false;
      }
    }
    return false;
  }
  bool read_zigzag(int64_t &value)
  {
    uint64_t raw = 0;
    if (!read_varint(raw)) {
      return false;
    }
    value = static_cast<int64_t>((raw >> 1) ^ (~(raw & 1) + 1));
    return true;
  }
  bool read_i32(int32_t &value)
  {
    int64_t zigzag = 0;
    if (!read_zigzag(zigzag)) {
      return false;
    }
    value = static_cast<int32_t>(zigzag);
    return true;
  }
  bool read_i64(int64_t &value)
  {
    int64_t zigzag = 0;
    if (!read_zigzag(zigzag)) {
      return false;
    }
    value = zigzag;
    return true;
  }
  bool read_byte(uint8_t &value)
  {
    if (pos_ >= len_) {
      return false;
    }
    value = static_cast<uint8_t>(data_[pos_++]);
    return true;
  }
  bool read_binary(std::string &value)
  {
    uint64_t size = 0;
    if (!read_varint(size)) {
      return false;
    }
    if (pos_ + static_cast<int64_t>(size) > len_) {
      return false;
    }
    value.assign(data_ + pos_, static_cast<size_t>(size));
    pos_ += static_cast<int64_t>(size);
    return true;
  }
  bool read_list_begin(int8_t &elem_type, int32_t &size)
  {
    uint8_t b = 0;
    if (!read_byte(b)) {
      return false;
    }
    elem_type = static_cast<int8_t>(b & 0x0F);
    size = static_cast<int32_t>((b & 0xF0) >> 4);
    if (15 == size) {
      uint64_t big_size = 0;
      if (!read_varint(big_size)) {
        return false;
      }
      size = static_cast<int32_t>(big_size);
    }
    return true;
  }
  bool skip(int8_t type)
  {
    switch (type) {
      case T_BOOLEAN_TRUE:
      case T_BOOLEAN_FALSE:
        return true;
      case T_BYTE: {
        uint8_t b = 0;
        return read_byte(b);
      }
      case T_I16:
      case T_I32:
      case T_I64: {
        int64_t v = 0;
        return read_zigzag(v);
      }
      case T_DOUBLE: return skip_bytes(8);
      case T_BINARY: {
        std::string tmp;
        return read_binary(tmp);
      }
      case T_LIST:
      case T_SET: {
        int8_t elem_type = 0;
        int32_t size = 0;
        if (!read_list_begin(elem_type, size)) {
          return false;
        }
        for (int32_t i = 0; i < size; ++i) {
          if (!skip(elem_type)) {
            return false;
          }
        }
        return true;
      }
      case T_MAP: {
        uint8_t b = 0;
        if (!read_byte(b)) {
          return false;
        }
        int32_t size = static_cast<int32_t>((b & 0xF0) >> 4);
        if (15 == size) {
          uint64_t big_size = 0;
          if (!read_varint(big_size)) {
            return false;
          }
          size = static_cast<int32_t>(big_size);
        }
        if (0 == size) {
          return true;
        }
        const int8_t key_type = static_cast<int8_t>(b & 0x0F);
        int8_t value_type = 0;
        {
          uint8_t vb = 0;
          if (!read_byte(vb)) {
            return false;
          }
          value_type = static_cast<int8_t>(vb & 0x0F);
        }
        for (int32_t i = 0; i < size; ++i) {
          if (!skip(key_type) || !skip(value_type)) {
            return false;
          }
        }
        return true;
      }
      case T_STRUCT: {
        while (true) {
          int16_t field_id = 0;
          int8_t field_type = 0;
          if (!read_field_begin(field_id, field_type)) {
            return false;
          }
          if (T_STOP == field_type) {
            return true;
          }
          if (!skip(field_type)) {
            return false;
          }
        }
      }
      default: return false;
    }
  }
  bool skip_bytes(int64_t count)
  {
    if (pos_ + count > len_) {
      return false;
    }
    pos_ += count;
    return true;
  }
  int64_t pos() const { return pos_; }
  void set_pos(int64_t pos) { pos_ = pos; }
  const char *data() const { return data_; }
  int64_t len() const { return len_; }
private:
  const char *data_;
  int64_t len_;
  int64_t pos_;
  int16_t last_field_id_;
};

// ---------------------------------------------------------------------------
// Parquet metadata
// ---------------------------------------------------------------------------
const int32_t PARQUET_TYPE_BOOLEAN = 0;
const int32_t PARQUET_TYPE_INT32 = 1;
const int32_t PARQUET_TYPE_INT64 = 2;
const int32_t PARQUET_TYPE_INT96 = 3;
const int32_t PARQUET_TYPE_FLOAT = 4;
const int32_t PARQUET_TYPE_DOUBLE = 5;
const int32_t PARQUET_TYPE_BYTE_ARRAY = 6;
const int32_t PARQUET_TYPE_FIXED_LEN_BYTE_ARRAY = 7;

const int32_t PARQUET_CODEC_UNCOMPRESSED = 0;
const int32_t PARQUET_CODEC_SNAPPY = 1;

const int32_t PARQUET_ENCODING_PLAIN = 0;
const int32_t PARQUET_ENCODING_PLAIN_DICTIONARY = 2;
const int32_t PARQUET_ENCODING_RLE = 3;
const int32_t PARQUET_ENCODING_BIT_PACKED = 4;
const int32_t PARQUET_ENCODING_RLE_DICTIONARY = 8;

const int32_t PARQUET_PAGE_DATA = 0;
const int32_t PARQUET_PAGE_DICTIONARY = 2;
const int32_t PARQUET_PAGE_DATA_V2 = 3;

const int32_t PARQUET_REPETITION_REQUIRED = 0;
const int32_t PARQUET_REPETITION_OPTIONAL = 1;

struct ParquetSchemaElement
{
  int32_t type_ = -1;
  int32_t repetition_type_ = PARQUET_REPETITION_REQUIRED;
  int32_t num_children_ = 0;
  std::string name_;
};

struct ParquetColumnMeta
{
  int32_t type_ = -1;
  int32_t codec_ = PARQUET_CODEC_UNCOMPRESSED;
  int32_t encoding_ = PARQUET_ENCODING_PLAIN;
  int64_t num_values_ = 0;
  int64_t data_page_offset_ = 0;
  int64_t dictionary_page_offset_ = 0;
  bool has_dictionary_ = false;
  int32_t type_length_ = 0;
};

struct ParquetColumnChunk
{
  int64_t file_offset_ = 0;
  ParquetColumnMeta meta_;
  bool has_meta_ = false;
};

struct ParquetRowGroup
{
  std::vector<ParquetColumnChunk> columns_;
  int64_t num_rows_ = 0;
};

struct ParquetFileMeta
{
  std::vector<ParquetSchemaElement> schema_;
  int64_t num_rows_ = 0;
  std::vector<ParquetRowGroup> row_groups_;
};

struct ParquetPageHeader
{
  int32_t type_ = 0;
  int32_t uncompressed_page_size_ = 0;
  int32_t compressed_page_size_ = 0;
  int32_t num_values_ = 0;
  int32_t encoding_ = 0;
  int32_t num_nulls_ = 0;
  int32_t definition_levels_byte_length_ = 0;
  int32_t repetition_levels_byte_length_ = 0;
  bool is_compressed_ = true;
};

bool parse_schema_element(ThriftCompact &in, ParquetSchemaElement &element)
{
  bool ok = true;
  int16_t field_id = 0;
  int8_t type = 0;
  while (ok && in.read_field_begin(field_id, type)) {
    if (T_STOP == type) {
      break;
    }
    switch (field_id) {
      case 1: ok = in.read_i32(element.type_); break;
      case 3: ok = in.read_i32(element.repetition_type_); break;
      case 4: ok = in.read_binary(element.name_); break;
      case 5: ok = in.read_i32(element.num_children_); break;
      default: ok = in.skip(type); break;
    }
  }
  return ok;
}

bool parse_column_meta(ThriftCompact &in, ParquetColumnMeta &meta)
{
  bool ok = true;
  int16_t field_id = 0;
  int8_t type = 0;
  while (ok && in.read_field_begin(field_id, type)) {
    if (T_STOP == type) {
      break;
    }
    switch (field_id) {
      case 1: ok = in.read_i32(meta.type_); break;
      case 2: {
        int8_t elem_type = 0;
        int32_t size = 0;
        ok = in.read_list_begin(elem_type, size);
        for (int32_t i = 0; ok && i < size; ++i) {
          int32_t encoding = 0;
          ok = in.read_i32(encoding);
          if (ok) {
            meta.encoding_ = encoding;
          }
        }
        break;
      }
      case 3: ok = in.skip(type); break; // path_in_schema
      case 4: ok = in.read_i32(meta.codec_); break;
      case 5: ok = in.read_i64(meta.num_values_); break;
      case 9: ok = in.read_i64(meta.data_page_offset_); break;
      case 11:
        ok = in.read_i64(meta.dictionary_page_offset_);
        meta.has_dictionary_ = true;
        break;
      default: ok = in.skip(type); break;
    }
  }
  return ok;
}

bool parse_column_chunk(ThriftCompact &in, ParquetColumnChunk &chunk)
{
  bool ok = true;
  int16_t field_id = 0;
  int8_t type = 0;
  while (ok && in.read_field_begin(field_id, type)) {
    if (T_STOP == type) {
      break;
    }
    if (3 == field_id) {
      ThriftCompact meta_in(in.data() + in.pos(), in.len() - in.pos());
      ok = parse_column_meta(meta_in, chunk.meta_);
      chunk.has_meta_ = true;
      if (ok) {
        // the metadata struct is fully consumed, rewind the outer reader
        in.set_pos(in.pos() + meta_in.pos());
      }
    } else if (2 == field_id) {
      ok = in.read_i64(chunk.file_offset_);
    } else {
      ok = in.skip(type);
    }
  }
  return ok;
}

bool parse_row_group(ThriftCompact &in, ParquetRowGroup &group)
{
  bool ok = true;
  int16_t field_id = 0;
  int8_t type = 0;
  while (ok && in.read_field_begin(field_id, type)) {
    if (T_STOP == type) {
      break;
    }
    if (1 == field_id) {
      int8_t elem_type = 0;
      int32_t size = 0;
      ok = in.read_list_begin(elem_type, size);
      for (int32_t i = 0; ok && i < size; ++i) {
        ThriftCompact chunk_in(in.data() + in.pos(), in.len() - in.pos());
        ParquetColumnChunk chunk;
        ok = parse_column_chunk(chunk_in, chunk);
        if (ok) {
          in.set_pos(in.pos() + chunk_in.pos());
          group.columns_.push_back(chunk);
        }
      }
    } else if (3 == field_id) {
      ok = in.read_i64(group.num_rows_);
    } else {
      ok = in.skip(type);
    }
  }
  return ok;
}

bool parse_file_meta(const char *data, int64_t len, ParquetFileMeta &meta)
{
  bool ok = true;
  ThriftCompact in(data, len);
  int16_t field_id = 0;
  int8_t type = 0;
  while (ok && in.read_field_begin(field_id, type)) {
    if (T_STOP == type) {
      break;
    }
    if (2 == field_id) {
      int8_t elem_type = 0;
      int32_t size = 0;
      ok = in.read_list_begin(elem_type, size);
      for (int32_t i = 0; ok && i < size; ++i) {
        ThriftCompact elem_in(in.data() + in.pos(), in.len() - in.pos());
        ParquetSchemaElement element;
        ok = parse_schema_element(elem_in, element);
        if (ok) {
          in.set_pos(in.pos() + elem_in.pos());
          meta.schema_.push_back(element);
        }
      }
    } else if (3 == field_id) {
      ok = in.read_i64(meta.num_rows_);
    } else if (4 == field_id) {
      int8_t elem_type = 0;
      int32_t size = 0;
      ok = in.read_list_begin(elem_type, size);
      for (int32_t i = 0; ok && i < size; ++i) {
        ThriftCompact elem_in(in.data() + in.pos(), in.len() - in.pos());
        ParquetRowGroup group;
        ok = parse_row_group(elem_in, group);
        if (ok) {
          in.set_pos(in.pos() + elem_in.pos());
          meta.row_groups_.push_back(group);
        }
      }
    } else {
      ok = in.skip(type);
    }
  }
  return ok;
}

bool parse_page_header(ThriftCompact &in, ParquetPageHeader &header)
{
  bool ok = true;
  int16_t field_id = 0;
  int8_t type = 0;
  while (ok && in.read_field_begin(field_id, type)) {
    if (T_STOP == type) {
      break;
    }
    switch (field_id) {
      case 1: ok = in.read_i32(header.type_); break;
      case 2: ok = in.read_i32(header.uncompressed_page_size_); break;
      case 3: ok = in.read_i32(header.compressed_page_size_); break;
      case 5:
      case 8: {
        ThriftCompact sub(in.data() + in.pos(), in.len() - in.pos());
        ok = true;
        int16_t sub_id = 0;
        int8_t sub_type = 0;
        while (ok && sub.read_field_begin(sub_id, sub_type)) {
          if (T_STOP == sub_type) {
            break;
          }
          switch (sub_id) {
            case 1: ok = sub.read_i32(header.num_values_); break;
            case 2: ok = sub.read_i32(header.encoding_); break;
            case 4: ok = sub.read_i32(header.encoding_); break;
            case 5: ok = sub.read_i32(header.definition_levels_byte_length_); break;
            case 6: ok = sub.read_i32(header.repetition_levels_byte_length_); break;
            case 7: {
              // a boolean carries its value in the type of the field
              header.is_compressed_ = (T_BOOLEAN_TRUE == sub_type);
              break;
            }
            default: ok = sub.skip(sub_type); break;
          }
        }
        if (ok) {
          in.set_pos(in.pos() + sub.pos());
        }
        break;
      }
      case 7: {
        ThriftCompact sub(in.data() + in.pos(), in.len() - in.pos());
        ok = true;
        int16_t sub_id = 0;
        int8_t sub_type = 0;
        while (ok && sub.read_field_begin(sub_id, sub_type)) {
          if (T_STOP == sub_type) {
            break;
          }
          switch (sub_id) {
            case 1: ok = sub.read_i32(header.num_values_); break;
            case 2: ok = sub.read_i32(header.encoding_); break;
            default: ok = sub.skip(sub_type); break;
          }
        }
        if (ok) {
          in.set_pos(in.pos() + sub.pos());
        }
        break;
      }
      default: ok = in.skip(type); break;
    }
  }
  return ok;
}

} // namespace

namespace
{

// ---------------------------------------------------------------------------
// Snappy raw decompression
// ---------------------------------------------------------------------------
/// The snappy block starts with the length of the uncompressed data.
bool snappy_uncompressed_len(const char *src, int64_t src_len, int64_t &len)
{
  int64_t pos = 0;
  uint64_t value = 0;
  int shift = 0;
  while (pos < src_len) {
    const uint8_t b = static_cast<uint8_t>(src[pos++]);
    value |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (0 == (b & 0x80)) {
      len = static_cast<int64_t>(value);
      return true;
    }
    shift += 7;
    if (shift > 63) {
      break;
    }
  }
  return false;
}

bool snappy_uncompress(const char *src, int64_t src_len, char *dst, int64_t dst_len, int64_t *consumed = NULL)
{
  int64_t pos = 0;
  uint64_t uncompressed_len = 0;
  int shift = 0;
  while (pos < src_len) {
    const uint8_t b = static_cast<uint8_t>(src[pos++]);
    uncompressed_len |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (0 == (b & 0x80)) {
      break;
    }
    shift += 7;
    if (shift > 63) {
      return false;
    }
  }
  if (static_cast<int64_t>(uncompressed_len) != dst_len) {
    return false;
  }
  int64_t dpos = 0;
  while (pos < src_len && dpos < dst_len) {
    const uint8_t tag = static_cast<uint8_t>(src[pos++]);
    const uint8_t kind = tag & 0x03;
    if (0 == kind) {
      int64_t len = (tag >> 2) + 1;
      if (len > 60) {
        const int64_t extra = len - 60;
        if (pos + extra > src_len) {
          return false;
        }
        uint64_t value = 0;
        for (int64_t i = 0; i < extra; ++i) {
          value |= static_cast<uint64_t>(static_cast<uint8_t>(src[pos + i])) << (8 * i);
        }
        pos += extra;
        len = static_cast<int64_t>(value) + 1;
      }
      if (pos + len > src_len || dpos + len > dst_len) {
        return false;
      }
      memcpy(dst + dpos, src + pos, static_cast<size_t>(len));
      pos += len;
      dpos += len;
    } else {
      int64_t len = 0;
      int64_t offset = 0;
      if (1 == kind) {
        len = 4 + ((tag >> 2) & 0x07);
        if (pos >= src_len) {
          return false;
        }
        offset = (static_cast<int64_t>(tag >> 5) << 8) | static_cast<uint8_t>(src[pos++]);
      } else if (2 == kind) {
        len = 1 + (tag >> 2);
        if (pos + 2 > src_len) {
          return false;
        }
        offset = static_cast<uint8_t>(src[pos]) | (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 1])) << 8);
        pos += 2;
      } else {
        len = 1 + (tag >> 2);
        if (pos + 4 > src_len) {
          return false;
        }
        offset = static_cast<uint8_t>(src[pos])
                  | (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 1])) << 8)
                  | (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 2])) << 16)
                  | (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 3])) << 24);
        pos += 4;
      }
      if (offset <= 0 || offset > dpos || dpos + len > dst_len) {
        return false;
      }
      int64_t src_pos = dpos - offset;
      for (int64_t i = 0; i < len; ++i) {
        dst[dpos + i] = dst[src_pos + i];
      }
      dpos += len;
    }
  }
  if (NULL != consumed) {
    *consumed = pos;
  }
  return dpos == dst_len;
}

// ---------------------------------------------------------------------------
// RLE / bit-packing hybrid decoding
// ---------------------------------------------------------------------------
class BitReader
{
public:
  BitReader(const char *data, int64_t len) : data_(data), len_(len), bit_pos_(0) {}
  uint32_t read_bits(int32_t count)
  {
    uint32_t value = 0;
    for (int32_t i = 0; i < count; ++i) {
      const int64_t byte_index = bit_pos_ >> 3;
      if (byte_index >= len_) {
        break;
      }
      const int32_t bit = static_cast<int32_t>(bit_pos_ & 7);
      value |= (static_cast<uint32_t>(static_cast<uint8_t>(data_[byte_index]) >> bit) & 1u) << i;
      ++bit_pos_;
    }
    return value;
  }
  int64_t byte_pos() const { return (bit_pos_ + 7) / 8; }
private:
  const char *data_;
  int64_t len_;
  int64_t bit_pos_;
};

/// Decode a count values encoded with the RLE/bit-packing hybrid.
bool decode_rle_hybrid(const char *data,
                       int64_t len,
                       int32_t bit_width,
                       int32_t count,
                       std::vector<int32_t> &out)
{
  int64_t pos = 0;
  out.clear();
  out.reserve(static_cast<size_t>(count));
  while (static_cast<int32_t>(out.size()) < count && pos < len) {
    uint64_t header = 0;
    int shift = 0;
    while (pos < len) {
      const uint8_t b = static_cast<uint8_t>(data[pos++]);
      header |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (0 == (b & 0x80)) {
        break;
      }
      shift += 7;
      if (shift > 63) {
        return false;
      }
    }
    if (0 == (header & 1)) {
      const int32_t run = static_cast<int32_t>(header >> 1);
      const int64_t byte_len = (bit_width + 7) / 8;
      if (pos + byte_len > len) {
        return false;
      }
      uint32_t value = 0;
      for (int64_t i = 0; i < byte_len; ++i) {
        value |= static_cast<uint32_t>(static_cast<uint8_t>(data[pos + i])) << (8 * i);
      }
      pos += byte_len;
      for (int32_t i = 0; i < run && static_cast<int32_t>(out.size()) < count; ++i) {
        out.push_back(static_cast<int32_t>(value));
      }
    } else {
      const int32_t groups = static_cast<int32_t>(header >> 1);
      const int32_t values = groups * 8;
      BitReader reader(data + pos, len - pos);
      for (int32_t i = 0; i < values && static_cast<int32_t>(out.size()) < count; ++i) {
        out.push_back(static_cast<int32_t>(reader.read_bits(bit_width)));
      }
      pos += reader.byte_pos();
    }
  }
  return static_cast<int32_t>(out.size()) == count;
}

/// Format a double the way the sql engine renders it.
void format_double(double value, std::string &out)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "%.15g", value);
  if (strtod(buf, NULL) != value) {
    snprintf(buf, sizeof(buf), "%.17g", value);
  }
  out = buf;
}

} // namespace

namespace
{

int32_t read_le32(const char *data)
{
  uint32_t value = 0;
  memcpy(&value, data, sizeof(value));
  return static_cast<int32_t>(value);
}

int64_t read_le64(const char *data)
{
  uint64_t value = 0;
  memcpy(&value, data, sizeof(value));
  return static_cast<int64_t>(value);
}

/// Decode plain encoded parquet values.
bool decode_plain(const char *data,
                  int64_t len,
                  int32_t type,
                  int32_t count,
                  int32_t type_length,
                  std::vector<std::string> &out)
{
  int64_t pos = 0;
  for (int32_t i = 0; i < count; ++i) {
    std::string value;
    switch (type) {
      case PARQUET_TYPE_BOOLEAN: {
        const int64_t byte_index = i / 8;
        if (byte_index >= len) {
          return false;
        }
        const bool bit = (0 != ((static_cast<uint8_t>(data[byte_index]) >> (i % 8)) & 1));
        value = bit ? "true" : "false";
        break;
      }
      case PARQUET_TYPE_INT32: {
        if (pos + 4 > len) {
          return false;
        }
        value = std::to_string(read_le32(data + pos));
        pos += 4;
        break;
      }
      case PARQUET_TYPE_INT64: {
        if (pos + 8 > len) {
          return false;
        }
        value = std::to_string(read_le64(data + pos));
        pos += 8;
        break;
      }
      case PARQUET_TYPE_FLOAT: {
        if (pos + 4 > len) {
          return false;
        }
        float number = 0.0f;
        memcpy(&number, data + pos, sizeof(number));
        format_double(static_cast<double>(number), value);
        pos += 4;
        break;
      }
      case PARQUET_TYPE_DOUBLE: {
        if (pos + 8 > len) {
          return false;
        }
        double number = 0.0;
        memcpy(&number, data + pos, sizeof(number));
        format_double(number, value);
        pos += 8;
        break;
      }
      case PARQUET_TYPE_BYTE_ARRAY: {
        if (pos + 4 > len) {
          return false;
        }
        const int32_t size = read_le32(data + pos);
        pos += 4;
        if (size < 0 || pos + size > len) {
          return false;
        }
        value.assign(data + pos, static_cast<size_t>(size));
        pos += size;
        break;
      }
      case PARQUET_TYPE_FIXED_LEN_BYTE_ARRAY: {
        if (type_length <= 0 || pos + type_length > len) {
          return false;
        }
        value.assign(data + pos, static_cast<size_t>(type_length));
        pos += type_length;
        break;
      }
      default: return false;
    }
    out.push_back(value);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Parquet reader
// ---------------------------------------------------------------------------
class ParquetFileScanReader : public FileScanReader
{
public:
  int open(const ObString &path, const FileScanOptions &options, ObIAllocator &allocator) override
  {
    options_ = options;
    row_index_ = 0;
    group_index_ = 0;
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret = open_scan_file(path, fp_, file_size_))) {
      LOG_USER_ERROR(OB_FILE_NOT_EXIST, "file scan cannot open the parquet file");
      return ret;
    }
    if (OB_FAIL(ret = read_footer())) {
      return ret;
    }
    if (OB_FAIL(ret = collect_leaves())) {
      return ret;
    }
    if (!meta_.row_groups_.empty() && OB_FAIL(ret = load_row_group())) {
      return ret;
    }
    return OB_SUCCESS;
  }
  int next_row() override
  {
    int ret = OB_SUCCESS;
    while (true) {
      if (group_index_ >= static_cast<int64_t>(meta_.row_groups_.size())) {
        return OB_ITER_END;
      }
      if (row_index_ >= group_rows_) {
        ++group_index_;
        row_index_ = 0;
        if (group_index_ >= static_cast<int64_t>(meta_.row_groups_.size())) {
          return OB_ITER_END;
        }
        if (OB_FAIL(ret = load_row_group())) {
          return ret;
        }
        continue;
      }
      break;
    }
    fields_.reuse();
    for (int64_t c = 0; OB_SUCC(ret) && c < static_cast<int64_t>(column_values_.size()); ++c) {
      if (column_nulls_[c][row_index_]) {
        if (OB_FAIL(ret = fields_.push_back(ObString(0, NULL)))) {
          return ret;
        }
      } else {
        const std::string &value = column_values_[c][row_index_];
        if (OB_FAIL(ret = fields_.push_back(ObString(static_cast<int32_t>(value.length()),
                                                    value.data())))) {
          return ret;
        }
      }
    }
    ++row_index_;
    return OB_SUCCESS;
  }
  const FileScanFields &fields() const override { return fields_; }
  void close() override
  {
    if (OB_NOT_NULL(fp_)) {
      fclose(fp_);
      fp_ = NULL;
    }
  }
  int64_t row_count() const override { return meta_.num_rows_; }
  const std::vector<ParquetSchemaElement> &leaves() const { return leaves_; }
private:
  int read_footer()
  {
    int ret = OB_SUCCESS;
    const int64_t size = file_size_;
    if (size < 12) {
      LOG_USER_ERROR(OB_INVALID_DATA, "the parquet file is too small");
      return OB_INVALID_DATA;
    }
    char tail[8];
    int64_t read_size = 0;
    if (OB_FAIL(ret = file_pread(fp_, tail, sizeof(tail), size - sizeof(tail), read_size))) {
      return ret;
    }
    if (read_size != static_cast<int64_t>(sizeof(tail))) {
      return OB_INVALID_DATA;
    }
    const int32_t footer_len = read_le32(tail);
    if (footer_len <= 0 || static_cast<int64_t>(footer_len) + 8 > size) {
      LOG_USER_ERROR(OB_INVALID_DATA, "the parquet footer is invalid");
      return OB_INVALID_DATA;
    }
    footer_.resize(static_cast<size_t>(footer_len));
    if (OB_FAIL(ret = file_pread(fp_, &footer_[0], footer_len, size - 8 - footer_len, read_size))) {
      return ret;
    }
    if (read_size != footer_len) {
      return OB_INVALID_DATA;
    }
    if (!parse_file_meta(footer_.data(), footer_len, meta_)) {
      LOG_USER_ERROR(OB_INVALID_DATA, "the parquet footer cannot be parsed");
      return OB_INVALID_DATA;
    }
    return OB_SUCCESS;
  }
  int collect_leaves()
  {
    int ret = OB_SUCCESS;
    leaves_.clear();
    for (size_t i = 1; i < meta_.schema_.size(); ++i) {
      const ParquetSchemaElement &element = meta_.schema_[i];
      if (element.type_ >= 0) {
        leaves_.push_back(element);
      }
    }
    if (leaves_.empty()) {
      LOG_USER_ERROR(OB_INVALID_DATA, "the parquet file has no column");
      ret = OB_INVALID_DATA;
    }
    return ret;
  }
  int read_bytes(int64_t offset, int64_t len, char *buf, int64_t &read_size)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ret = file_pread(fp_, buf, len, offset, read_size))) {
    }
    return ret;
  }
  int load_row_group()
  {
    int ret = OB_SUCCESS;
    const ParquetRowGroup &group = meta_.row_groups_[group_index_];
    group_rows_ = group.num_rows_;
    column_values_.assign(leaves_.size(), std::vector<std::string>());
    column_nulls_.assign(leaves_.size(), std::vector<bool>());
    for (size_t c = 0; OB_SUCC(ret) && c < group.columns_.size() && c < leaves_.size(); ++c) {
      if (OB_FAIL(ret = decode_column(group.columns_[c], leaves_[c], column_values_[c], column_nulls_[c]))) {
        return ret;
      }
    }
    return ret;
  }
  int decode_column(const ParquetColumnChunk &chunk,
                    const ParquetSchemaElement &leaf,
                    std::vector<std::string> &values,
                    std::vector<bool> &nulls)
  {
    int ret = OB_SUCCESS;
    if (!chunk.has_meta_) {
      LOG_USER_ERROR(OB_INVALID_DATA, "the parquet column chunk has no metadata");
      return OB_INVALID_DATA;
    }
    const ParquetColumnMeta &meta = chunk.meta_;
    const int32_t max_def_level = (PARQUET_REPETITION_OPTIONAL == leaf.repetition_type_) ? 1 : 0;
    int64_t offset = meta.has_dictionary_ && meta.dictionary_page_offset_ > 0 ? meta.dictionary_page_offset_
                                                                            : meta.data_page_offset_;
    std::vector<std::string> dictionary;
    int64_t produced = 0;
    while (produced < meta.num_values_) {
      ParquetPageHeader header;
      char header_buf[8192];
      int64_t read_size = 0;
      if (OB_FAIL(ret = read_bytes(offset, sizeof(header_buf), header_buf, read_size))) {
        return ret;
      }
      ThriftCompact header_in(header_buf, read_size);
      if (!parse_page_header(header_in, header)) {
        LOG_USER_ERROR(OB_INVALID_DATA, "the parquet page header cannot be parsed");
        return OB_INVALID_DATA;
      }
      const int64_t header_size = header_in.pos();
      offset += header_size;
      // the levels of a v2 page are stored uncompressed in front of the values
      const int64_t levels_len = (PARQUET_PAGE_DATA_V2 == header.type_)
                                 ? header.repetition_levels_byte_length_ + header.definition_levels_byte_length_
                                 : 0;
      // the compressed size of a page holds the values only, the levels of a v2
      // page are stored uncompressed in front of them
      const int64_t page_len = header.compressed_page_size_ + levels_len;
      std::string page_data;
      page_data.resize(static_cast<size_t>(page_len));
      if (page_len > 0
          && OB_FAIL(ret = read_bytes(offset, page_len, &page_data[0], read_size))) {
        return ret;
      }
      const int64_t page_offset = offset;
      std::string plain;
      if (PARQUET_CODEC_SNAPPY == meta.codec_) {
        int64_t uncompressed_len = 0;
        if (!snappy_uncompressed_len(page_data.data() + levels_len,
                                     page_data.length() - levels_len,
                                     uncompressed_len)) {
          LOG_USER_ERROR(OB_INVALID_DATA, "the parquet page cannot be decompressed");
          return OB_INVALID_DATA;
        }
        plain.resize(static_cast<size_t>(levels_len + uncompressed_len));
        if (levels_len > 0) {
          memcpy(&plain[0], page_data.data(), levels_len);
        }
        int64_t consumed = 0;
        if (!snappy_uncompress(page_data.data() + levels_len, page_data.length() - levels_len,
                               &plain[0] + levels_len, uncompressed_len, &consumed)) {
          LOG_USER_ERROR(OB_INVALID_DATA, "the parquet page cannot be decompressed");
          return OB_INVALID_DATA;
        }
        // the page ends where the compressed values end
        offset = page_offset + levels_len + consumed;
      } else {
        plain.swap(page_data);
        offset = page_offset + page_len;
      }
      if (PARQUET_PAGE_DICTIONARY == header.type_) {
        if (OB_FAIL(ret = decode_dictionary(plain, header.num_values_, meta, dictionary))) {
          return ret;
        }
        continue;
      }
      if (OB_FAIL(ret = decode_data_page(plain, header, meta, leaf, max_def_level, dictionary, values, nulls))) {
        return ret;
      }
      produced += header.num_values_;
    }
    return ret;
  }
  int decode_dictionary(const std::string &page,
                        int32_t count,
                        const ParquetColumnMeta &meta,
                        std::vector<std::string> &dictionary)
  {
    int ret = OB_SUCCESS;
    dictionary.clear();
    if (!decode_plain(page.data(), page.length(), meta.type_, count, meta.type_length_, dictionary)) {
      LOG_USER_ERROR(OB_INVALID_DATA, "the parquet dictionary page cannot be decoded");
      ret = OB_INVALID_DATA;
    }
    return ret;
  }
  int decode_data_page(const std::string &page,
                       const ParquetPageHeader &header,
                       const ParquetColumnMeta &meta,
                       const ParquetSchemaElement &leaf,
                       int32_t max_def_level,
                       const std::vector<std::string> &dictionary,
                       std::vector<std::string> &values,
                       std::vector<bool> &nulls)
  {
    int ret = OB_SUCCESS;
    const char *data = page.data();
    int64_t len = page.length();
    int64_t pos = 0;
    std::vector<int32_t> def_levels;
    if (max_def_level > 0) {
      if (PARQUET_PAGE_DATA_V2 == header.type_) {
        const int64_t level_len = header.definition_levels_byte_length_;
        if (pos + level_len > len) {
          return OB_INVALID_DATA;
        }
        if (!decode_rle_hybrid(data + pos, level_len, 1, header.num_values_, def_levels)) {
          return OB_INVALID_DATA;
        }
        pos += level_len;
      } else {
        if (pos + 4 > len) {
          return OB_INVALID_DATA;
        }
        const int32_t level_len = read_le32(data + pos);
        pos += 4;
        if (level_len < 0 || pos + level_len > len) {
          return OB_INVALID_DATA;
        }
        if (!decode_rle_hybrid(data + pos, level_len, 1, header.num_values_, def_levels)) {
          return OB_INVALID_DATA;
        }
        pos += level_len;
      }
    }
    int32_t non_null = header.num_values_;
    if (max_def_level > 0) {
      non_null = 0;
      for (size_t i = 0; i < def_levels.size(); ++i) {
        if (def_levels[i] == max_def_level) {
          ++non_null;
        }
      }
    }
    std::vector<std::string> decoded;
    const int32_t encoding = (PARQUET_PAGE_DATA_V2 == header.type_) ? header.encoding_ : header.encoding_;
    if (PARQUET_ENCODING_RLE_DICTIONARY == encoding || PARQUET_ENCODING_PLAIN_DICTIONARY == encoding) {
      if (pos >= len) {
        return OB_INVALID_DATA;
      }
      const int32_t bit_width = static_cast<uint8_t>(data[pos++]);
      std::vector<int32_t> indices;
      if (!decode_rle_hybrid(data + pos, len - pos, bit_width, non_null, indices)) {
        return OB_INVALID_DATA;
      }
      for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] < 0 || indices[i] >= static_cast<int32_t>(dictionary.size())) {
          return OB_INVALID_DATA;
        }
        decoded.push_back(dictionary[indices[i]]);
      }
    } else if (PARQUET_ENCODING_RLE == encoding && PARQUET_TYPE_BOOLEAN == meta.type_) {
      std::vector<int32_t> bits;
      if (!decode_rle_hybrid(data + pos, len - pos, 1, non_null, bits)) {
        return OB_INVALID_DATA;
      }
      for (size_t i = 0; i < bits.size(); ++i) {
        decoded.push_back(bits[i] ? "true" : "false");
      }
    } else {
      if (!decode_plain(data + pos, len - pos, meta.type_, non_null, meta.type_length_, decoded)) {
        return OB_INVALID_DATA;
      }
    }
    size_t value_index = 0;
    for (int32_t i = 0; i < header.num_values_; ++i) {
      const bool is_null = (max_def_level > 0) && (def_levels[i] != max_def_level);
      if (is_null) {
        values.push_back(std::string());
        nulls.push_back(true);
      } else {
        if (value_index >= decoded.size()) {
          return OB_INVALID_DATA;
        }
        values.push_back(decoded[value_index++]);
        nulls.push_back(false);
      }
    }
    return ret;
  }
private:
  FILE *fp_ = NULL;
  int64_t file_size_ = 0;
  FileScanOptions options_;
  FileScanFields fields_;
  std::string footer_;
  ParquetFileMeta meta_;
  std::vector<ParquetSchemaElement> leaves_;
  std::vector<std::vector<std::string>> column_values_;
  std::vector<std::vector<bool>> column_nulls_;
  int64_t group_index_ = 0;
  int64_t row_index_ = 0;
  int64_t group_rows_ = 0;
};

} // namespace

FileScanOptions::FileScanOptions()
  : format_(FILE_SCAN_FORMAT_CSV),
    has_header_(true),
    field_delim_(','),
    quote_char_('"'),
    escape_char_('"'),
    sample_size_(-1)
{}

FileScanType FileScanUtils::to_file_scan_type(ObObjType type)
{
  switch (type) {
    case ObIntType: return FILE_SCAN_TYPE_BIGINT;
    case ObDoubleType: return FILE_SCAN_TYPE_DOUBLE;
    case ObTinyIntType: return FILE_SCAN_TYPE_BOOLEAN;
    case ObMySQLDateType: return FILE_SCAN_TYPE_DATE;
    case ObMySQLDateTimeType: return FILE_SCAN_TYPE_DATETIME;
    default: return FILE_SCAN_TYPE_VARCHAR;
  }
}

ObObjType FileScanUtils::to_obj_type(FileScanType type)
{
  switch (type) {
    case FILE_SCAN_TYPE_BIGINT: return ObIntType;
    case FILE_SCAN_TYPE_DOUBLE: return ObDoubleType;
    case FILE_SCAN_TYPE_BOOLEAN: return ObTinyIntType;
    case FILE_SCAN_TYPE_VARCHAR: return ObVarcharType;
    case FILE_SCAN_TYPE_DATE: return ObMySQLDateType;
    case FILE_SCAN_TYPE_DATETIME: return ObMySQLDateTimeType;
    default: return ObNullType;
  }
}

const char *FileScanUtils::type_name(FileScanType type)
{
  switch (type) {
    case FILE_SCAN_TYPE_BIGINT: return "BIGINT";
    case FILE_SCAN_TYPE_DOUBLE: return "DOUBLE";
    case FILE_SCAN_TYPE_BOOLEAN: return "BOOLEAN";
    case FILE_SCAN_TYPE_VARCHAR: return "VARCHAR";
    case FILE_SCAN_TYPE_DATE: return "DATE";
    case FILE_SCAN_TYPE_DATETIME: return "DATETIME";
    default: return "NULL";
  }
}

FileScanType FileScanUtils::merge_types(uint32_t value_mask)
{
  if (0 == value_mask) {
    return FILE_SCAN_TYPE_NULL;
  }
  const uint32_t meaningful = value_mask & ~FILE_SCAN_MASK_NULL;
  if (0 == meaningful) {
    return FILE_SCAN_TYPE_NULL;
  }
  if (FILE_SCAN_MASK_VARCHAR == meaningful) {
    return FILE_SCAN_TYPE_VARCHAR;
  }
  if (FILE_SCAN_MASK_INT == meaningful) {
    return FILE_SCAN_TYPE_BIGINT;
  }
  if (FILE_SCAN_MASK_DOUBLE == meaningful) {
    return FILE_SCAN_TYPE_DOUBLE;
  }
  if ((meaningful | FILE_SCAN_MASK_INT) == (FILE_SCAN_MASK_INT | FILE_SCAN_MASK_DOUBLE)) {
    return FILE_SCAN_TYPE_DOUBLE;
  }
  if (FILE_SCAN_MASK_BOOL == meaningful) {
    return FILE_SCAN_TYPE_BOOLEAN;
  }
  if (FILE_SCAN_MASK_DATE == meaningful) {
    return FILE_SCAN_TYPE_DATE;
  }
  if ((meaningful | FILE_SCAN_MASK_DATE) == (FILE_SCAN_MASK_DATE | FILE_SCAN_MASK_DATETIME)) {
    return FILE_SCAN_TYPE_DATETIME;
  }
  return FILE_SCAN_TYPE_VARCHAR;
}

int FileScanUtils::parse_date(const ObString &value, int32_t &year, int32_t &month, int32_t &day)
{
  if (10 != value.length()) {
    return OB_INVALID_ARGUMENT;
  }
  const char *data = value.ptr();
  if (!is_digit(data[0]) || !is_digit(data[1]) || !is_digit(data[2]) || !is_digit(data[3])
      || data[4] != '-' || !is_digit(data[5]) || !is_digit(data[6]) || data[7] != '-'
      || !is_digit(data[8]) || !is_digit(data[9])) {
    return OB_INVALID_ARGUMENT;
  }
  year = (data[0] - '0') * 1000 + (data[1] - '0') * 100 + (data[2] - '0') * 10 + (data[3] - '0');
  month = (data[5] - '0') * 10 + (data[6] - '0');
  day = (data[8] - '0') * 10 + (data[9] - '0');
  if (year < 1000 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
    return OB_INVALID_ARGUMENT;
  }
  static const int days_of_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int max_day = days_of_month[month - 1];
  if (2 == month && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
    max_day = 29;
  }
  if (day > max_day) {
    return OB_INVALID_ARGUMENT;
  }
  return OB_SUCCESS;
}

int FileScanUtils::parse_datetime(const ObString &value,
                                  int32_t &year,
                                  int32_t &month,
                                  int32_t &day,
                                  int32_t &hour,
                                  int32_t &minute,
                                  int32_t &second,
                                  int32_t &microsecond)
{
  int ret = OB_SUCCESS;
  hour = 0;
  minute = 0;
  second = 0;
  microsecond = 0;
  if (value.length() < 10) {
    return OB_INVALID_ARGUMENT;
  }
  ObString date_part(10, value.ptr());
  if (OB_FAIL(parse_date(date_part, year, month, day))) {
    return OB_INVALID_ARGUMENT;
  }
  if (10 == value.length()) {
    return OB_SUCCESS;
  }
  const char *data = value.ptr();
  const int64_t len = value.length();
  int64_t pos = 10;
  if (data[pos] != 'T' && data[pos] != 't' && data[pos] != ' ') {
    return OB_INVALID_ARGUMENT;
  }
  ++pos;
  if (pos + 8 > len || !is_digit(data[pos]) || !is_digit(data[pos + 1]) || data[pos + 2] != ':'
      || !is_digit(data[pos + 3]) || !is_digit(data[pos + 4]) || data[pos + 5] != ':'
      || !is_digit(data[pos + 6]) || !is_digit(data[pos + 7])) {
    return OB_INVALID_ARGUMENT;
  }
  hour = (data[pos] - '0') * 10 + (data[pos + 1] - '0');
  minute = (data[pos + 3] - '0') * 10 + (data[pos + 4] - '0');
  second = (data[pos + 6] - '0') * 10 + (data[pos + 7] - '0');
  pos += 8;
  if (hour > 23 || minute > 59 || second > 59) {
    return OB_INVALID_ARGUMENT;
  }
  if (pos < len && data[pos] == '.') {
    ++pos;
    int64_t digits = 0;
    int64_t scale = 100000;
    while (pos < len && is_digit(data[pos])) {
      if (scale > 0) {
        microsecond += (data[pos] - '0') * scale;
        scale /= 10;
      }
      ++pos;
      ++digits;
    }
    if (0 == digits) {
      return OB_INVALID_ARGUMENT;
    }
  }
  if (pos != len) {
    return OB_INVALID_ARGUMENT;
  }
  return OB_SUCCESS;
}

FileScanValueType FileScanUtils::classify_csv_value(const ObString &value)
{
  if (value.empty()) {
    return FILE_SCAN_VALUE_NULL;
  }
  bool bool_value = false;
  if (parse_bool(value.ptr(), value.length(), bool_value)) {
    return FILE_SCAN_VALUE_BOOL;
  }
  bool is_int = false;
  bool is_double = false;
  int64_t int_value = 0;
  if (parse_numeric(value.ptr(), value.length(), is_int, is_double, int_value)) {
    return is_int ? FILE_SCAN_VALUE_INT : FILE_SCAN_VALUE_DOUBLE;
  }
  int32_t year = 0;
  int32_t month = 0;
  int32_t day = 0;
  if (OB_SUCCESS == parse_date(value, year, month, day)) {
    return FILE_SCAN_VALUE_DATE;
  }
  int32_t hour = 0;
  int32_t minute = 0;
  int32_t second = 0;
  int32_t microsecond = 0;
  if (OB_SUCCESS == parse_datetime(value, year, month, day, hour, minute, second, microsecond)) {
    return FILE_SCAN_VALUE_DATETIME;
  }
  return FILE_SCAN_VALUE_VARCHAR;
}

int FileScanUtils::set_datum(const ObString &value, FileScanType type, ObDatum &datum)
{
  int ret = OB_SUCCESS;
  if (NULL == value.ptr()) {
    datum.set_null();
    return OB_SUCCESS;
  }
  switch (type) {
    case FILE_SCAN_TYPE_BIGINT: {
      bool is_int = false;
      bool is_double = false;
      int64_t number = 0;
      if (!parse_numeric(value.ptr(), value.length(), is_int, is_double, number) || !is_int) {
        ret = OB_INVALID_DATA;
      } else {
        datum.set_int(number);
      }
      break;
    }
    case FILE_SCAN_TYPE_DOUBLE: {
      char *endptr = NULL;
      errno = 0;
      const double number = strtod(value.ptr(), &endptr);
      if (endptr != value.ptr() + value.length() || ERANGE == errno) {
        ret = OB_INVALID_DATA;
      } else {
        datum.set_double(number);
      }
      break;
    }
    case FILE_SCAN_TYPE_BOOLEAN: {
      bool bool_value = false;
      if (!parse_bool(value.ptr(), value.length(), bool_value)) {
        ret = OB_INVALID_DATA;
      } else {
        datum.set_bool(bool_value);
      }
      break;
    }
    case FILE_SCAN_TYPE_VARCHAR: {
      datum.set_string(value);
      break;
    }
    case FILE_SCAN_TYPE_DATE: {
      int32_t year = 0;
      int32_t month = 0;
      int32_t day = 0;
      if (OB_FAIL(ret = parse_date(value, year, month, day))) {
        ret = OB_INVALID_DATA;
      } else {
        common::ObMySQLDate date;
        date.year_ = year;
        date.month_ = month;
        date.day_ = day;
        datum.set_mysql_date(date);
      }
      break;
    }
    case FILE_SCAN_TYPE_DATETIME: {
      int32_t year = 0;
      int32_t month = 0;
      int32_t day = 0;
      int32_t hour = 0;
      int32_t minute = 0;
      int32_t second = 0;
      int32_t microsecond = 0;
      if (OB_FAIL(ret = parse_datetime(value, year, month, day, hour, minute, second, microsecond))) {
        ret = OB_INVALID_DATA;
      } else {
        common::ObMySQLDateTime datetime;
        datetime.year_month_ = common::ObMySQLDateTime::year_month(year, month);
        datetime.day_ = day;
        datetime.hour_ = hour;
        datetime.minute_ = minute;
        datetime.second_ = second;
        datetime.microseconds_ = microsecond;
        datetime.sign_ = 0;
        datum.set_mysql_datetime(datetime);
      }
      break;
    }
    default:
      ret = OB_INVALID_DATA;
      break;
  }
  return ret;
}

namespace
{

bool glob_match(const char *pattern, int64_t pattern_len, const char *name, int64_t name_len)
{
  int64_t p = 0;
  int64_t n = 0;
  int64_t star_p = -1;
  int64_t star_n = 0;
  while (n < name_len) {
    if (p < pattern_len && ('?' == pattern[p] || pattern[p] == name[n])) {
      ++p;
      ++n;
    } else if (p < pattern_len && '*' == pattern[p]) {
      star_p = p++;
      star_n = n;
    } else if (star_p >= 0) {
      p = star_p + 1;
      n = ++star_n;
    } else {
      return false;
    }
  }
  while (p < pattern_len && '*' == pattern[p]) {
    ++p;
  }
  return p == pattern_len;
}

int glob_walk(const std::string &dir,
              const std::vector<std::string> &segments,
              size_t index,
              std::vector<std::string> &results)
{
  int ret = OB_SUCCESS;
  if (index == segments.size()) {
    results.push_back(dir);
    return OB_SUCCESS;
  }
  const std::string &segment = segments[index];
  if (segment.find('*') == std::string::npos && segment.find('?') == std::string::npos) {
    return glob_walk(dir + "/" + segment, segments, index + 1, results);
  }
  DIR *handle = opendir(dir.c_str());
  if (NULL == handle) {
    return OB_SUCCESS;
  }
  std::vector<std::string> names;
  struct dirent *entry = NULL;
  while (NULL != (entry = readdir(handle))) {
    names.push_back(entry->d_name);
  }
  closedir(handle);
  for (size_t i = 0; OB_SUCC(ret) && i < names.size(); ++i) {
    const std::string &name = names[i];
    if ("." == name || ".." == name) {
      continue;
    }
    if (!glob_match(segment.c_str(), segment.length(), name.c_str(), name.length())) {
      continue;
    }
    if (OB_FAIL(ret = glob_walk(dir + "/" + name, segments, index + 1, results))) {
      return ret;
    }
  }
  return ret;
}

int64_t find_name(const std::vector<std::string> &names, const std::string &name)
{
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i] == name) {
      return static_cast<int64_t>(i);
    }
  }
  return -1;
}

int infer_csv_schema(const ObString &path,
                     const FileScanOptions &options,
                     common::ObIArray<FileScanColumn> &columns,
                     ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  CsvFileScanReader reader;
  if (OB_FAIL(ret = reader.open(path, options, allocator))) {
    return ret;
  }
  std::vector<std::string> names;
  std::vector<uint32_t> masks;
  std::vector<bool> nullable;
  const std::vector<std::string> &header = reader.header();
  for (size_t i = 0; i < header.size(); ++i) {
    std::string name = header[i];
    if (find_name(names, name) >= 0) {
      int64_t counter = 2;
      std::string candidate;
      do {
        candidate = name + "__" + std::to_string(counter++);
      } while (find_name(names, candidate) >= 0);
      name = candidate;
    }
    names.push_back(name);
    masks.push_back(0);
    nullable.push_back(false);
  }
  while (OB_SUCC(ret)) {
    const int next = reader.next_row();
    if (OB_ITER_END == next) {
      break;
    } else if (OB_FAIL(next)) {
      ret = next;
      break;
    }
    const FileScanFields &fields = reader.fields();
    for (size_t i = 0; i < fields.count() && i < masks.size(); ++i) {
      const ObString &value = fields.at(i);
      if (NULL == value.ptr()) {
        masks[i] |= FILE_SCAN_MASK_NULL;
        nullable[i] = true;
      } else if (value.empty()) {
        masks[i] |= FILE_SCAN_MASK_VARCHAR;
      } else {
        masks[i] |= 1u << FileScanUtils::classify_csv_value(value);
      }
    }
  }
  if (OB_FAIL(ret)) {
    return ret;
  }
  for (size_t i = 0; i < names.size(); ++i) {
    FileScanColumn column;
    // the names live in a local vector, copy them into the allocator of the caller
    if (OB_FAIL(ret = ob_write_string(allocator,
                                        ObString(static_cast<int32_t>(names[i].length()), names[i].data()),
                                        column.name_))) {
      return ret;
    }
    column.type_ = FileScanUtils::merge_types(masks[i]);
    column.nullable_ = nullable[i];
    if (OB_FAIL(ret = columns.push_back(column))) {
      return ret;
    }
  }
  return ret;
}

int infer_jsonl_schema(const ObString &path,
                        const FileScanOptions &options,
                        common::ObIArray<FileScanColumn> &columns,
                        ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  FileScanOptions scan_options = options;
  scan_options.column_names_.reuse();
  JsonlFileScanReader reader;
  if (OB_FAIL(ret = reader.open(path, scan_options, allocator))) {
    return ret;
  }
  std::vector<std::string> names;
  std::vector<uint32_t> masks;
  std::vector<bool> nullable;
  while (OB_SUCC(ret)) {
    const int next = reader.next_row();
    if (OB_ITER_END == next) {
      break;
    } else if (OB_FAIL(next)) {
      ret = next;
      break;
    }
    const std::vector<std::string> &keys = reader.last_keys();
    const std::vector<JsonValue> &values = reader.last_values();
    for (size_t i = 0; i < keys.size(); ++i) {
      int64_t index = find_name(names, keys[i]);
      if (index < 0) {
        names.push_back(keys[i]);
        masks.push_back(0);
        nullable.push_back(false);
        index = static_cast<int64_t>(names.size()) - 1;
      }
      if (i >= values.size()) {
        continue;
      }
      if (FILE_SCAN_VALUE_NULL == values[i].type_) {
        masks[index] |= FILE_SCAN_MASK_NULL;
        nullable[index] = true;
      } else {
        masks[index] |= 1u << values[i].type_;
      }
    }
  }
  if (OB_FAIL(ret)) {
    return ret;
  }
  for (size_t i = 0; i < names.size(); ++i) {
    FileScanColumn column;
    // the names live in a local vector, copy them into the allocator of the caller
    if (OB_FAIL(ret = ob_write_string(allocator,
                                        ObString(static_cast<int32_t>(names[i].length()), names[i].data()),
                                        column.name_))) {
      return ret;
    }
    column.type_ = FileScanUtils::merge_types(masks[i]);
    column.nullable_ = nullable[i];
    if (OB_FAIL(ret = columns.push_back(column))) {
      return ret;
    }
  }
  return ret;
}

FileScanType parquet_type(int32_t type)
{
  switch (type) {
    case PARQUET_TYPE_BOOLEAN: return FILE_SCAN_TYPE_BOOLEAN;
    case PARQUET_TYPE_INT32:
    case PARQUET_TYPE_INT64: return FILE_SCAN_TYPE_BIGINT;
    case PARQUET_TYPE_FLOAT:
    case PARQUET_TYPE_DOUBLE: return FILE_SCAN_TYPE_DOUBLE;
    case PARQUET_TYPE_INT96:
    case PARQUET_TYPE_BYTE_ARRAY:
    case PARQUET_TYPE_FIXED_LEN_BYTE_ARRAY: return FILE_SCAN_TYPE_VARCHAR;
    default: return FILE_SCAN_TYPE_VARCHAR;
  }
}

int infer_parquet_schema(const ObString &path,
                          const FileScanOptions &options,
                          common::ObIArray<FileScanColumn> &columns,
                          ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ParquetFileScanReader reader;
  if (OB_FAIL(ret = reader.open(path, options, allocator))) {
    return ret;
  }
  const std::vector<ParquetSchemaElement> &leaves = reader.leaves();
  for (size_t i = 0; i < leaves.size(); ++i) {
    FileScanColumn column;
    // the names live in the footer of the reader, copy them into the allocator of the caller
    if (OB_FAIL(ret = ob_write_string(allocator,
                                        ObString(static_cast<int32_t>(leaves[i].name_.length()), leaves[i].name_.data()),
                                        column.name_))) {
      return ret;
    }
    column.type_ = parquet_type(leaves[i].type_);
    column.nullable_ = (PARQUET_REPETITION_OPTIONAL == leaves[i].repetition_type_);
    if (OB_FAIL(ret = columns.push_back(column))) {
      return ret;
    }
  }
  return ret;
}

} // namespace

int FileScanUtils::create_reader(const ObString &path,
                               const FileScanOptions &options,
                               ObIAllocator &allocator,
                               FileScanReader *&reader)
{
  int ret = OB_SUCCESS;
  reader = NULL;
  void *buf = NULL;
  FileScanOptions scan_options = options;
  // the jsonl reader maps the keys of a record onto the columns of the file
  if (FILE_SCAN_FORMAT_JSONL == scan_options.format_) {
    common::ObArray<FileScanColumn, common::ModulePageAllocator, true> columns;
    if (OB_FAIL(ret = infer_schema(path, scan_options, columns, allocator))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
        if (OB_FAIL(ret = scan_options.column_names_.push_back(columns.at(i).name_))) {
          break;
        }
      }
    }
  }
  // every reader needs its own memory, the parquet reader is the biggest one
  const size_t reader_size = (FILE_SCAN_FORMAT_PARQUET == options.format_) ? sizeof(ParquetFileScanReader)
                                : (FILE_SCAN_FORMAT_JSONL == options.format_) ? sizeof(JsonlFileScanReader)
                                : sizeof(CsvFileScanReader);
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(buf = allocator.alloc(reader_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    FileScanReader *candidate = NULL;
    switch (options.format_) {
      case FILE_SCAN_FORMAT_JSONL:
        candidate = new (buf) JsonlFileScanReader();
        break;
      case FILE_SCAN_FORMAT_PARQUET:
        candidate = new (buf) ParquetFileScanReader();
        break;
      default:
        candidate = new (buf) CsvFileScanReader();
        break;
    }
    if (OB_FAIL(ret = candidate->open(path, scan_options, allocator))) {
      candidate->close();
      allocator.free(buf);
    } else {
      reader = candidate;
    }
  }
  return ret;
}

void FileScanUtils::destroy_reader(FileScanReader *reader)
{
  if (OB_NOT_NULL(reader)) {
    reader->close();
  }
}

int FileScanUtils::infer_schema(const ObString &path,
                                const FileScanOptions &options,
                                common::ObIArray<FileScanColumn> &columns,
                                ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  switch (options.format_) {
    case FILE_SCAN_FORMAT_CSV: ret = infer_csv_schema(path, options, columns, allocator); break;
    case FILE_SCAN_FORMAT_JSONL: ret = infer_jsonl_schema(path, options, columns, allocator); break;
    case FILE_SCAN_FORMAT_PARQUET: ret = infer_parquet_schema(path, options, columns, allocator); break;
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "unsupported file scan format");
      break;
  }
  return ret;
}

int FileScanUtils::glob_files(const ObString &pattern,
                              common::ObIArray<ObString> &files,
                              ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  std::vector<std::string> segments;
  std::string current;
  const std::string text(pattern.ptr(), static_cast<size_t>(pattern.length()));
  std::string root = ".";
  size_t begin = 0;
  if (text.size() > 2 && ':' == text[1]) {
    // a windows path starts with the drive letter, e.g. C:/temp
    root = text.substr(0, 2);
    begin = 2;
  } else if (!text.empty() && '/' == text[0]) {
    root = "/";
    begin = 1;
  }
  for (size_t i = begin; i < text.size(); ++i) {
    if ('/' == text[i]) {
      if (!current.empty()) {
        segments.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(text[i]);
    }
  }
  if (!current.empty()) {
    segments.push_back(current);
  }
  std::vector<std::string> matches;
  if (OB_FAIL(ret = glob_walk(root, segments, 0, matches))) {
    return ret;
  }
  std::vector<std::string> sorted;
  for (size_t i = 0; i < matches.size(); ++i) {
    struct stat st;
    if (0 != stat(matches[i].c_str(), &st)) {
      continue;
    }
    if (!S_ISREG(st.st_mode)) {
      continue;
    }
    sorted.push_back(matches[i]);
  }
  std::sort(sorted.begin(), sorted.end());
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (OB_FAIL(ret = files.push_back(ObString(static_cast<int32_t>(sorted[i].length()),
                                                sorted[i].data())))) {
      return ret;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
