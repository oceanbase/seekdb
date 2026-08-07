/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/basic/ob_parquet_reader.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <list>
#include <mutex>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <stdlib.h>
#else
#include <limits.h>
#include <stdlib.h>
#endif

#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_parse.h"
#include "common/timezone/ob_time_convert.h"
#include "lib/allocator/page_arena.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_errno.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

class SafeFileStreamBuf : public std::streambuf
{
public:
  explicit SafeFileStreamBuf(const int fd) : fd_(fd), buffer_() { setg(buffer_, buffer_, buffer_); }
  virtual ~SafeFileStreamBuf()
  {
    if (fd_ >= 0) {
      close(fd_);
    }
  }
protected:
  virtual int_type underflow() override
  {
    int_type result = traits_type::eof();
    if (gptr() < egptr()) {
      result = traits_type::to_int_type(*gptr());
    } else {
      const ssize_t read_size = read(fd_, buffer_, sizeof(buffer_));
      if (read_size > 0) {
        setg(buffer_, buffer_, buffer_ + read_size);
        result = traits_type::to_int_type(*gptr());
      }
    }
    return result;
  }
private:
  int fd_;
  char buffer_[64 * 1024];
};

int open_safe_input_stream(const std::string &path,
                           std::unique_ptr<std::streambuf> &buffer,
                           std::unique_ptr<std::istream> &stream,
                           uint64_t *opened_device = nullptr,
                           uint64_t *opened_inode = nullptr,
                           int64_t *opened_size = nullptr,
                           int64_t *opened_mtime_ns = nullptr)
{
  int ret = OB_SUCCESS;
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  struct stat file_stat;
  if (fd < 0) {
    ret = OB_FILE_NOT_OPENED;
  } else if (0 != fstat(fd, &file_stat) || !S_ISREG(file_stat.st_mode)) {
    close(fd);
    ret = OB_INVALID_DATA;
  } else {
    if (nullptr != opened_device) {
      *opened_device = static_cast<uint64_t>(file_stat.st_dev);
    }
    if (nullptr != opened_inode) {
      *opened_inode = static_cast<uint64_t>(file_stat.st_ino);
    }
    if (nullptr != opened_size) {
      *opened_size = static_cast<int64_t>(file_stat.st_size);
    }
    if (nullptr != opened_mtime_ns) {
#if defined(__APPLE__)
      *opened_mtime_ns = static_cast<int64_t>(file_stat.st_mtimespec.tv_sec) * 1000000000L
                       + static_cast<int64_t>(file_stat.st_mtimespec.tv_nsec);
#else
      *opened_mtime_ns = static_cast<int64_t>(file_stat.st_mtim.tv_sec) * 1000000000L
                       + static_cast<int64_t>(file_stat.st_mtim.tv_nsec);
#endif
    }
    buffer.reset(new (std::nothrow) SafeFileStreamBuf(fd));
    if (!buffer) {
      close(fd);
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      stream.reset(new (std::nothrow) std::istream(buffer.get()));
      if (!stream) {
        buffer.reset();
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
  }
  return ret;
}

struct ParsedValue
{
  ParsedValue() : type_(ObFileColumnType::NULL_TYPE), text_(), int_value_(0), double_value_(0), bool_value_(false) {}
  ObFileColumnType type_;
  std::string text_;
  int64_t int_value_;
  double double_value_;
  bool bool_value_;
};

struct ParsedJsonField
{
  std::string name_;
  ParsedValue value_;
};

struct SchemaCacheEntry
{
  std::string path_;
  ObFileFormat format_;
  uint64_t device_;
  uint64_t inode_;
  int64_t size_;
  int64_t mtime_ns_;
  std::vector<ObFileColumnSchema> columns_;
  int64_t row_count_;
};

class SchemaCache
{
public:
  bool get(const std::string &path, const ObFileFormat format,
           const uint64_t device, const uint64_t inode,
           const int64_t size, const int64_t mtime_ns,
           std::vector<ObFileColumnSchema> &columns, int64_t &row_count)
  {
    bool found = false;
    std::lock_guard<std::mutex> guard(lock_);
    for (auto it = entries_.begin(); !found && it != entries_.end(); ++it) {
      if (it->path_ == path && it->format_ == format
          && it->device_ == device && it->inode_ == inode
          && it->size_ == size && it->mtime_ns_ == mtime_ns) {
        columns = it->columns_;
        row_count = it->row_count_;
        entries_.splice(entries_.begin(), entries_, it);
        found = true;
      }
    }
    return found;
  }

  void put(const std::string &path, const ObFileFormat format,
           const uint64_t device, const uint64_t inode,
           const int64_t size, const int64_t mtime_ns,
           const std::vector<ObFileColumnSchema> &columns, const int64_t row_count)
  {
    SchemaCacheEntry entry;
    entry.path_ = path;
    entry.format_ = format;
    entry.device_ = device;
    entry.inode_ = inode;
    entry.size_ = size;
    entry.mtime_ns_ = mtime_ns;
    entry.columns_ = columns;
    entry.row_count_ = row_count;
    std::lock_guard<std::mutex> guard(lock_);
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (it->path_ == path && it->format_ == format
          && it->device_ == device && it->inode_ == inode
          && it->size_ == size && it->mtime_ns_ == mtime_ns) {
        it = entries_.erase(it);
      } else {
        ++it;
      }
    }
    entries_.push_front(entry);
    while (entries_.size() > MAX_ENTRIES) {
      entries_.pop_back();
    }
  }

private:
  static constexpr size_t MAX_ENTRIES = 64;
  std::mutex lock_;
  std::list<SchemaCacheEntry> entries_;
};

SchemaCache &schema_cache()
{
  static SchemaCache cache;
  return cache;
}

static constexpr int64_t MAX_FILE_SIZE = 1024L * 1024L * 1024L;
static constexpr int64_t MAX_COLUMN_COUNT = 1024;
static constexpr int64_t MAX_FIELD_SIZE = 16L * 1024L * 1024L;

bool ascii_iequal(const std::string &left, const char *right)
{
  bool equal = nullptr != right && left.length() == std::strlen(right);
  for (size_t i = 0; equal && i < left.length(); ++i) {
    const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(left[i])));
    const char r = static_cast<char>(std::tolower(static_cast<unsigned char>(right[i])));
    equal = l == r;
  }
  return equal;
}

bool ascii_iequal(const std::string &left, const std::string &right)
{
  bool equal = left.length() == right.length();
  for (size_t i = 0; equal && i < left.length(); ++i) {
    const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(left[i])));
    const char r = static_cast<char>(std::tolower(static_cast<unsigned char>(right[i])));
    equal = l == r;
  }
  return equal;
}

bool ends_with_ci(const std::string &value, const char *suffix)
{
  const size_t suffix_len = std::strlen(suffix);
  bool matched = value.length() >= suffix_len;
  for (size_t i = 0; matched && i < suffix_len; ++i) {
    const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(value[value.length() - suffix_len + i])));
    const char r = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
    matched = l == r;
  }
  return matched;
}

bool is_blank_line(const std::string &line)
{
  bool blank = true;
  for (size_t i = 0; blank && i < line.length(); ++i) {
    blank = 0 != std::isspace(static_cast<unsigned char>(line[i]));
  }
  return blank;
}

int read_csv_record(std::istream &stream, std::vector<std::string> &fields, bool &has_record)
{
  int ret = OB_SUCCESS;
  fields.clear();
  has_record = false;
  std::string field;
  bool in_quotes = false;
  bool quoted_field = false;
  bool after_quote = false;
  bool first_char = true;
  while (OB_SUCC(ret)) {
    const int next = stream.get();
    if (std::char_traits<char>::eof() == next) {
      if (in_quotes) {
        ret = OB_INVALID_DATA;
      } else if (!has_record && field.empty() && fields.empty()) {
        ret = OB_ITER_END;
      } else {
        fields.push_back(field);
        has_record = true;
      }
      break;
    }
    has_record = true;
    const char ch = static_cast<char>(next);
    if (in_quotes) {
      if ('"' == ch) {
        if ('"' == stream.peek()) {
          stream.get();
          field.push_back('"');
        } else {
          in_quotes = false;
          after_quote = true;
        }
      } else {
        field.push_back(ch);
        if (field.length() > static_cast<size_t>(MAX_FIELD_SIZE)) {
          ret = OB_SIZE_OVERFLOW;
        }
      }
    } else if (after_quote) {
      if (',' == ch) {
        fields.push_back(field);
        field.clear();
        quoted_field = false;
        after_quote = false;
        first_char = true;
      } else if ('\n' == ch) {
        fields.push_back(field);
        break;
      } else if ('\r' == ch) {
        if ('\n' == stream.peek()) {
          stream.get();
        }
        fields.push_back(field);
        break;
      } else {
        ret = OB_INVALID_DATA;
      }
    } else if (first_char && '"' == ch) {
      in_quotes = true;
      quoted_field = true;
      first_char = false;
    } else if (',' == ch) {
      fields.push_back(field);
      field.clear();
      quoted_field = false;
      first_char = true;
    } else if ('\n' == ch) {
      fields.push_back(field);
      break;
    } else if ('\r' == ch) {
      if ('\n' == stream.peek()) {
        stream.get();
      }
      fields.push_back(field);
      break;
    } else if ('"' == ch && !quoted_field) {
      ret = OB_INVALID_DATA;
    } else {
      field.push_back(ch);
      if (field.length() > static_cast<size_t>(MAX_FIELD_SIZE)) {
        ret = OB_SIZE_OVERFLOW;
      }
      first_char = false;
    }
  }
  return ret;
}

bool parse_strict_date(const std::string &text, int32_t &date_value)
{
  bool valid = 10 == text.length()
      && '-' == text[4] && '-' == text[7];
  for (size_t i = 0; valid && i < text.length(); ++i) {
    if (4 != i && 7 != i) {
      valid = 0 != std::isdigit(static_cast<unsigned char>(text[i]));
    }
  }
  if (valid) {
    const ObString value(static_cast<int32_t>(text.length()), text.data());
    valid = OB_SUCCESS == ObTimeConverter::str_to_date(value, date_value, 0);
  }
  return valid;
}

bool parse_strict_datetime(const std::string &text, int64_t &datetime_value)
{
  bool valid = text.length() >= 19 && text.length() <= 26
      && '-' == text[4] && '-' == text[7]
      && (' ' == text[10] || 'T' == text[10])
      && ':' == text[13] && ':' == text[16];
  for (size_t i = 0; valid && i < text.length(); ++i) {
    if (4 != i && 7 != i && 10 != i && 13 != i && 16 != i && 19 != i) {
      valid = 0 != std::isdigit(static_cast<unsigned char>(text[i]));
    } else if (19 == i) {
      valid = '.' == text[i];
    }
  }
  if (valid) {
    std::string normalized(text);
    normalized[10] = ' ';
    const ObString value(static_cast<int32_t>(normalized.length()), normalized.data());
    const ObTimeConvertCtx cvrt_ctx(nullptr, false);
    valid = OB_SUCCESS == ObTimeConverter::str_to_datetime(value, cvrt_ctx, datetime_value, nullptr, 0);
  }
  return valid;
}

ParsedValue parse_csv_value(const std::string &text)
{
  ParsedValue value;
  value.text_ = text;
  if (text.empty()) {
    value.type_ = ObFileColumnType::NULL_TYPE;
  } else if (ascii_iequal(text, "true") || ascii_iequal(text, "false")) {
    value.type_ = ObFileColumnType::BOOLEAN;
    value.bool_value_ = ascii_iequal(text, "true");
  } else {
    char *end = nullptr;
    errno = 0;
    const long long int_value = std::strtoll(text.c_str(), &end, 10);
    if (0 == errno && end == text.c_str() + text.length()) {
      value.type_ = ObFileColumnType::BIGINT;
      value.int_value_ = static_cast<int64_t>(int_value);
    } else {
      errno = 0;
      end = nullptr;
      const double double_value = std::strtod(text.c_str(), &end);
      if (0 == errno && end == text.c_str() + text.length() && std::isfinite(double_value)) {
        value.type_ = ObFileColumnType::DOUBLE;
        value.double_value_ = double_value;
      } else {
        int32_t date_value = 0;
        int64_t datetime_value = 0;
        if (parse_strict_date(text, date_value)) {
          value.type_ = ObFileColumnType::DATE;
        } else if (parse_strict_datetime(text, datetime_value)) {
          value.type_ = ObFileColumnType::DATETIME;
        } else {
          value.type_ = ObFileColumnType::VARCHAR;
        }
      }
    }
  }
  return value;
}

ObFileColumnType merge_type(const ObFileColumnType old_type, const ObFileColumnType new_type)
{
  ObFileColumnType result = old_type;
  if (ObFileColumnType::NULL_TYPE == old_type) {
    result = new_type;
  } else if (ObFileColumnType::NULL_TYPE == new_type || old_type == new_type) {
    // keep old type
  } else if ((ObFileColumnType::BIGINT == old_type && ObFileColumnType::DOUBLE == new_type)
          || (ObFileColumnType::DOUBLE == old_type && ObFileColumnType::BIGINT == new_type)) {
    result = ObFileColumnType::DOUBLE;
  } else if ((ObFileColumnType::DATE == old_type && ObFileColumnType::DATETIME == new_type)
          || (ObFileColumnType::DATETIME == old_type && ObFileColumnType::DATE == new_type)) {
    result = ObFileColumnType::DATETIME;
  } else {
    result = ObFileColumnType::VARCHAR;
  }
  return result;
}

std::string make_unique_column_name(const std::string &source_name,
                                    const int64_t ordinal,
                                    const std::vector<ObFileColumnSchema> &columns)
{
  std::string base_name = source_name.empty() ? "_c" + std::to_string(ordinal + 1) : source_name;
  std::string candidate = base_name;
  int64_t suffix = 2;
  bool unique = false;
  while (!unique) {
    unique = true;
    for (size_t i = 0; unique && i < columns.size(); ++i) {
      if (ascii_iequal(candidate, columns[i].column_name_)) {
        unique = false;
        candidate = base_name + "__" + std::to_string(suffix++);
      }
    }
  }
  return candidate;
}

int json_node_to_value(const ObIJsonBase &node, ParsedValue &value)
{
  int ret = OB_SUCCESS;
  value = ParsedValue();
  switch (node.json_type()) {
    case ObJsonNodeType::J_NULL:
      value.type_ = ObFileColumnType::NULL_TYPE;
      break;
    case ObJsonNodeType::J_BOOLEAN:
      value.type_ = ObFileColumnType::BOOLEAN;
      value.bool_value_ = node.get_boolean();
      value.text_ = value.bool_value_ ? "true" : "false";
      break;
    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT:
      value.type_ = ObFileColumnType::BIGINT;
      value.int_value_ = node.get_int();
      value.text_ = std::to_string(value.int_value_);
      break;
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG: {
      const uint64_t uint_value = node.get_uint();
      if (uint_value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        value.type_ = ObFileColumnType::BIGINT;
        value.int_value_ = static_cast<int64_t>(uint_value);
      } else {
        value.type_ = ObFileColumnType::DOUBLE;
        value.double_value_ = static_cast<double>(uint_value);
      }
      value.text_ = std::to_string(uint_value);
      break;
    }
    case ObJsonNodeType::J_DOUBLE:
    case ObJsonNodeType::J_ODOUBLE:
    case ObJsonNodeType::J_OFLOAT:
    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      double double_value = 0;
      if (OB_FAIL(node.to_double(double_value)) || !std::isfinite(double_value)) {
        ret = OB_INVALID_DATA;
      } else {
        char buf[64];
        const int length = std::snprintf(buf, sizeof(buf), "%.17g", double_value);
        value.type_ = ObFileColumnType::DOUBLE;
        value.double_value_ = double_value;
        value.text_.assign(buf, length > 0 ? static_cast<size_t>(length) : 0);
      }
      break;
    }
    case ObJsonNodeType::J_STRING: {
      value.text_.assign(node.get_data(), node.get_data_length());
      int32_t date_value = 0;
      int64_t datetime_value = 0;
      if (parse_strict_date(value.text_, date_value)) {
        value.type_ = ObFileColumnType::DATE;
      } else if (parse_strict_datetime(value.text_, datetime_value)) {
        value.type_ = ObFileColumnType::DATETIME;
      } else {
        value.type_ = ObFileColumnType::VARCHAR;
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT:
      ret = OB_NOT_SUPPORTED;
      break;
    default:
      ret = OB_NOT_SUPPORTED;
      break;
  }
  return ret;
}

int parse_json_object(const std::string &line, std::vector<ParsedJsonField> &fields)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("FileJsonLine");
  ObIJsonBase *root = nullptr;
  fields.clear();
  const ObString json_text(static_cast<int32_t>(line.length()), line.data());
  const uint32_t parse_flags = ObJsonParser::JSN_STRICT_FLAG | ObJsonParser::JSN_UNIQUE_FLAG;
  if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator,
                                               json_text,
                                               ObJsonInType::JSON_TREE,
                                               ObJsonInType::JSON_TREE,
                                               root,
                                               parse_flags,
                                               100))) {
    LOG_WARN("failed to parse jsonl record", K(ret));
  } else if (OB_ISNULL(root) || ObJsonNodeType::J_OBJECT != root->json_type()) {
    ret = OB_INVALID_DATA;
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < root->element_count(); ++i) {
      ObString key;
      ObIJsonBase *child = nullptr;
      ParsedJsonField field;
      if (OB_FAIL(root->get_object_value(i, key, child))) {
        LOG_WARN("failed to get json object field", K(ret), K(i));
      } else if (OB_ISNULL(child)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        field.name_.assign(key.ptr(), key.length());
        if (OB_FAIL(json_node_to_value(*child, field.value_))) {
          LOG_WARN("unsupported jsonl field value", K(ret), K(key));
        } else {
          fields.push_back(field);
        }
      }
    }
  }
  return ret;
}

int convert_value(const ParsedValue &value, const ObFileColumnType target_type, ObFileCell &cell)
{
  int ret = OB_SUCCESS;
  cell.reset();
  if (ObFileColumnType::NULL_TYPE == value.type_) {
    // keep null
  } else {
    cell.is_null_ = false;
    switch (target_type) {
      case ObFileColumnType::BOOLEAN:
        cell.bool_value_ = value.bool_value_;
        break;
      case ObFileColumnType::BIGINT:
        cell.int_value_ = value.int_value_;
        break;
      case ObFileColumnType::DOUBLE:
        cell.double_value_ = ObFileColumnType::BIGINT == value.type_
            ? static_cast<double>(value.int_value_) : value.double_value_;
        break;
      case ObFileColumnType::DATE:
        if (!parse_strict_date(value.text_, cell.date_value_)) {
          ret = OB_INVALID_DATA;
        }
        break;
      case ObFileColumnType::DATETIME: {
        if (ObFileColumnType::DATE == value.type_) {
          std::string datetime_text = value.text_ + " 00:00:00";
          if (!parse_strict_datetime(datetime_text, cell.datetime_value_)) {
            ret = OB_INVALID_DATA;
          }
        } else if (!parse_strict_datetime(value.text_, cell.datetime_value_)) {
          ret = OB_INVALID_DATA;
        }
        break;
      }
      case ObFileColumnType::VARCHAR:
        cell.string_value_ = value.text_;
        break;
      case ObFileColumnType::NULL_TYPE:
        cell.is_null_ = true;
        break;
      default:
        ret = OB_NOT_SUPPORTED;
        break;
    }
  }
  return ret;
}

int infer_csv_schema(const std::string &path,
                     std::vector<ObFileColumnSchema> &columns,
                     int64_t &row_count)
{
  int ret = OB_SUCCESS;
  std::unique_ptr<std::streambuf> stream_buffer;
  std::unique_ptr<std::istream> stream;
  std::vector<std::string> fields;
  bool has_record = false;
  columns.clear();
  row_count = 0;
  if (OB_FAIL(open_safe_input_stream(path, stream_buffer, stream))) {
    LOG_WARN("failed to safely open csv file", K(ret));
  } else if (OB_FAIL(read_csv_record(*stream, fields, has_record))) {
    if (OB_ITER_END == ret) {
      ret = OB_INVALID_DATA;
    }
  } else {
    for (size_t i = 0; i < fields.size(); ++i) {
      ObFileColumnSchema column;
      if (0 == i && fields[i].length() >= 3
          && static_cast<unsigned char>(fields[i][0]) == 0xEF
          && static_cast<unsigned char>(fields[i][1]) == 0xBB
          && static_cast<unsigned char>(fields[i][2]) == 0xBF) {
        fields[i].erase(0, 3);
      }
      column.source_name_ = fields[i];
      column.column_name_ = make_unique_column_name(fields[i], i, columns);
      columns.push_back(column);
      if (columns.size() > static_cast<size_t>(MAX_COLUMN_COUNT)) {
        ret = OB_SIZE_OVERFLOW;
      }
    }
  }
  while (OB_SUCC(ret)) {
    fields.clear();
    has_record = false;
    ret = read_csv_record(*stream, fields, has_record);
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      break;
    } else if (OB_FAIL(ret)) {
      break;
    } else if (fields.size() > columns.size()) {
      ret = OB_INVALID_DATA;
    } else {
      ++row_count;
      for (size_t i = 0; i < columns.size(); ++i) {
        const ParsedValue value = i < fields.size() ? parse_csv_value(fields[i]) : ParsedValue();
        columns[i].nullable_ = columns[i].nullable_ || ObFileColumnType::NULL_TYPE == value.type_;
        columns[i].type_ = merge_type(columns[i].type_, value.type_);
        columns[i].max_length_ = std::max(columns[i].max_length_, static_cast<int64_t>(value.text_.length()));
      }
    }
  }
  for (size_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
    columns[i].source_type_name_ = std::string("CSV/")
                                 + ObFileScanUtils::column_type_name(columns[i].type_);
  }
  return ret;
}

int infer_jsonl_schema(const std::string &path,
                       std::vector<ObFileColumnSchema> &columns,
                       int64_t &row_count)
{
  int ret = OB_SUCCESS;
  std::unique_ptr<std::streambuf> stream_buffer;
  std::unique_ptr<std::istream> stream;
  std::string line;
  columns.clear();
  row_count = 0;
  if (OB_FAIL(open_safe_input_stream(path, stream_buffer, stream))) {
    LOG_WARN("failed to safely open jsonl file", K(ret));
  }
  while (OB_SUCC(ret) && std::getline(*stream, line)) {
    if (line.length() > static_cast<size_t>(MAX_FIELD_SIZE)) {
      ret = OB_SIZE_OVERFLOW;
      break;
    }
    if (!line.empty() && '\r' == line.back()) {
      line.pop_back();
    }
    if (is_blank_line(line)) {
      continue;
    }
    std::vector<ParsedJsonField> fields;
    if (OB_FAIL(parse_json_object(line, fields))) {
      LOG_WARN("failed to infer jsonl record", K(ret), K(row_count));
    } else {
      ++row_count;
      std::vector<bool> seen(columns.size(), false);
      for (size_t field_idx = 0; OB_SUCC(ret) && field_idx < fields.size(); ++field_idx) {
        int64_t column_idx = -1;
        for (size_t i = 0; i < columns.size(); ++i) {
          if (columns[i].source_name_ == fields[field_idx].name_) {
            column_idx = static_cast<int64_t>(i);
            break;
          }
        }
        if (column_idx < 0) {
          ObFileColumnSchema column;
          column.source_name_ = fields[field_idx].name_;
          column.column_name_ = make_unique_column_name(column.source_name_, columns.size(), columns);
          column.nullable_ = row_count > 1;
          column.type_ = fields[field_idx].value_.type_;
          column.max_length_ = fields[field_idx].value_.text_.length();
          columns.push_back(column);
          if (columns.size() > static_cast<size_t>(MAX_COLUMN_COUNT)) {
            ret = OB_SIZE_OVERFLOW;
          }
          seen.push_back(true);
        } else {
          ObFileColumnSchema &column = columns[column_idx];
          seen[column_idx] = true;
          column.nullable_ = column.nullable_
              || ObFileColumnType::NULL_TYPE == fields[field_idx].value_.type_;
          column.type_ = merge_type(column.type_, fields[field_idx].value_.type_);
          column.max_length_ = std::max(column.max_length_,
                                        static_cast<int64_t>(fields[field_idx].value_.text_.length()));
        }
      }
      for (size_t i = 0; i < seen.size(); ++i) {
        if (!seen[i]) {
          columns[i].nullable_ = true;
        }
      }
    }
  }
  if (OB_SUCC(ret) && !stream->eof() && stream->fail()) {
    ret = OB_IO_ERROR;
  }
  for (size_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
    columns[i].source_type_name_ = std::string("JSON/")
                                 + ObFileScanUtils::column_type_name(columns[i].type_);
  }
  return ret;
}

} // namespace

int ObFileScanUtils::parse_format(const std::string &format_name, ObFileFormat &format)
{
  int ret = OB_SUCCESS;
  if (format_name.empty() || ascii_iequal(format_name, "auto")) {
    format = ObFileFormat::AUTO;
  } else if (ascii_iequal(format_name, "csv")) {
    format = ObFileFormat::CSV;
  } else if (ascii_iequal(format_name, "jsonl")) {
    format = ObFileFormat::JSONL;
  } else if (ascii_iequal(format_name, "parquet")) {
    format = ObFileFormat::PARQUET;
  } else {
    format = ObFileFormat::INVALID;
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

int ObFileScanUtils::detect_format(const std::string &path, ObFileFormat &format)
{
  int ret = OB_SUCCESS;
  if (ends_with_ci(path, ".csv")) {
    format = ObFileFormat::CSV;
  } else if (ends_with_ci(path, ".jsonl")) {
    format = ObFileFormat::JSONL;
  } else if (ends_with_ci(path, ".parquet")) {
    format = ObFileFormat::PARQUET;
  } else {
    format = ObFileFormat::INVALID;
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

int ObFileScanUtils::canonicalize_path(const std::string &path, std::string &canonical_path)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  char resolved_path[_MAX_PATH];
  if (nullptr == _fullpath(resolved_path, path.c_str(), sizeof(resolved_path))) {
    ret = OB_FILE_NOT_OPENED;
  } else {
    canonical_path.assign(resolved_path);
  }
#else
  char resolved_path[PATH_MAX];
  if (nullptr == realpath(path.c_str(), resolved_path)) {
    ret = OB_FILE_NOT_OPENED;
  } else {
    canonical_path.assign(resolved_path);
  }
#endif
  return ret;
}

int ObFileScanUtils::get_file_fingerprint(const std::string &path,
                                          std::string &canonical_path,
                                          int64_t &file_size,
                                          int64_t &modified_time_ns)
{
  uint64_t device = 0;
  uint64_t inode = 0;
  return get_file_fingerprint(path, canonical_path, device, inode,
                              file_size, modified_time_ns);
}

namespace
{
int get_path_fingerprint(const std::string &path,
                         const bool require_directory,
                         std::string &canonical_path,
                         uint64_t &device,
                         uint64_t &inode,
                         int64_t &file_size,
                         int64_t &modified_time_ns)
{
  int ret = OB_SUCCESS;
  struct stat path_stat;
  if (OB_FAIL(ObFileScanUtils::canonicalize_path(path, canonical_path))) {
    LOG_WARN("failed to canonicalize file path", K(ret));
  } else if (0 != stat(canonical_path.c_str(), &path_stat)) {
    ret = OB_FILE_NOT_OPENED;
  } else if ((require_directory && !S_ISDIR(path_stat.st_mode))
             || (!require_directory && !S_ISREG(path_stat.st_mode))) {
    ret = OB_INVALID_DATA;
  } else {
    device = static_cast<uint64_t>(path_stat.st_dev);
    inode = static_cast<uint64_t>(path_stat.st_ino);
    file_size = static_cast<int64_t>(path_stat.st_size);
#if defined(_WIN32)
    modified_time_ns = static_cast<int64_t>(path_stat.st_mtime) * 1000000000L;
#elif defined(__APPLE__)
    modified_time_ns = static_cast<int64_t>(path_stat.st_mtimespec.tv_sec) * 1000000000L
                     + static_cast<int64_t>(path_stat.st_mtimespec.tv_nsec);
#else
    modified_time_ns = static_cast<int64_t>(path_stat.st_mtim.tv_sec) * 1000000000L
                     + static_cast<int64_t>(path_stat.st_mtim.tv_nsec);
#endif
  }
  return ret;
}
} // namespace

int ObFileScanUtils::get_file_fingerprint(const std::string &path,
                                          std::string &canonical_path,
                                          uint64_t &device,
                                          uint64_t &inode,
                                          int64_t &file_size,
                                          int64_t &modified_time_ns)
{
  return get_path_fingerprint(path, false, canonical_path, device, inode,
                              file_size, modified_time_ns);
}

int ObFileScanUtils::get_directory_fingerprint(const std::string &path,
                                               std::string &canonical_path,
                                               uint64_t &device,
                                               uint64_t &inode,
                                               int64_t &modified_time_ns)
{
  int64_t ignored_size = 0;
  return get_path_fingerprint(path, true, canonical_path, device, inode,
                              ignored_size, modified_time_ns);
}

int ObFileScanUtils::infer_schema(const std::string &path,
                                  ObFileFormat requested_format,
                                  std::vector<ObFileColumnSchema> &columns,
                                  int64_t &row_count,
                                  std::string &canonical_path,
                                  ObFileFormat &actual_format,
                                  int64_t &file_size,
                                  int64_t &modified_time_ns)
{
  uint64_t device = 0;
  uint64_t inode = 0;
  return infer_schema(path, requested_format, columns, row_count, canonical_path,
                      actual_format, device, inode, file_size, modified_time_ns);
}

int ObFileScanUtils::infer_schema(const std::string &path,
                                  ObFileFormat requested_format,
                                  std::vector<ObFileColumnSchema> &columns,
                                  int64_t &row_count,
                                  std::string &canonical_path,
                                  ObFileFormat &actual_format,
                                  uint64_t &device,
                                  uint64_t &inode,
                                  int64_t &file_size,
                                  int64_t &modified_time_ns)
{
  int ret = OB_SUCCESS;
  uint64_t initial_device = 0;
  uint64_t initial_inode = 0;
  int64_t initial_file_size = 0;
  int64_t initial_modified_time_ns = 0;
  uint64_t verified_device = 0;
  uint64_t verified_inode = 0;
  bool cache_hit = false;
  std::string verified_path;
  if (OB_FAIL(get_file_fingerprint(path, canonical_path, initial_device, initial_inode,
                                   initial_file_size, initial_modified_time_ns))) {
    LOG_WARN("failed to fingerprint file scan path", K(ret));
  } else if (initial_file_size > MAX_FILE_SIZE) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("file exceeds file sql size limit", K(ret), K(initial_file_size), K(MAX_FILE_SIZE));
  } else if (ObFileFormat::AUTO == requested_format
             && OB_FAIL(detect_format(canonical_path, actual_format))) {
    LOG_WARN("failed to detect file scan format", K(ret));
  } else {
    if (ObFileFormat::AUTO != requested_format) {
      actual_format = requested_format;
    }
    if ((cache_hit = schema_cache().get(
           canonical_path, actual_format, initial_device, initial_inode,
           initial_file_size, initial_modified_time_ns, columns, row_count))) {
      // bounded process-local cache hit
    } else switch (actual_format) {
      case ObFileFormat::CSV:
        ret = infer_csv_schema(canonical_path, columns, row_count);
        break;
      case ObFileFormat::JSONL:
        ret = infer_jsonl_schema(canonical_path, columns, row_count);
        break;
      case ObFileFormat::PARQUET:
        ret = ObParquetReader::infer_schema(canonical_path, columns, row_count);
        break;
      default:
        ret = OB_NOT_SUPPORTED;
        break;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(get_file_fingerprint(path, verified_path, verified_device, verified_inode,
                                     file_size, modified_time_ns))) {
      LOG_WARN("failed to verify file after schema inference", K(ret));
    } else if (verified_path != canonical_path
               || verified_device != initial_device
               || verified_inode != initial_inode
               || file_size != initial_file_size
               || modified_time_ns != initial_modified_time_ns) {
      ret = OB_EAGAIN;
      LOG_WARN("file changed while inferring schema", K(ret));
    } else {
      device = verified_device;
      inode = verified_inode;
      if (!cache_hit) {
        schema_cache().put(canonical_path, actual_format, device, inode,
                           file_size, modified_time_ns, columns, row_count);
      }
    }
  }
  return ret;
}

const char *ObFileScanUtils::format_name(const ObFileFormat format)
{
  const char *name = "invalid";
  switch (format) {
    case ObFileFormat::AUTO: name = "auto"; break;
    case ObFileFormat::CSV: name = "csv"; break;
    case ObFileFormat::JSONL: name = "jsonl"; break;
    case ObFileFormat::PARQUET: name = "parquet"; break;
    default: break;
  }
  return name;
}

const char *ObFileScanUtils::column_type_name(const ObFileColumnType type)
{
  const char *name = "INVALID";
  switch (type) {
    case ObFileColumnType::NULL_TYPE: name = "NULL"; break;
    case ObFileColumnType::BOOLEAN: name = "BOOLEAN"; break;
    case ObFileColumnType::BIGINT: name = "BIGINT"; break;
    case ObFileColumnType::DOUBLE: name = "DOUBLE"; break;
    case ObFileColumnType::VARCHAR: name = "VARCHAR"; break;
    case ObFileColumnType::DATE: name = "DATE"; break;
    case ObFileColumnType::DATETIME: name = "DATETIME"; break;
    default: break;
  }
  return name;
}

ObFileScanReader::ObFileScanReader()
  : path_(), format_(ObFileFormat::INVALID), columns_(), projected_columns_(),
    stream_buffer_(), stream_(), current_row_number_(0),
    csv_header_read_(false), expected_device_(0), expected_inode_(0),
    expected_file_size_(0), expected_modified_time_ns_(0), end_verified_(false), parquet_reader_()
{}

ObFileScanReader::~ObFileScanReader()
{
  close();
}

int ObFileScanReader::open(const std::string &path,
                           const ObFileFormat format,
                           const std::vector<ObFileColumnSchema> &columns,
                           const int64_t expected_file_size,
                           const int64_t expected_modified_time_ns)
{
  return open(path, format, columns, 0, 0,
              expected_file_size, expected_modified_time_ns);
}

int ObFileScanReader::open(const std::string &path,
                           const ObFileFormat format,
                           const std::vector<ObFileColumnSchema> &columns,
                           const uint64_t expected_device,
                           const uint64_t expected_inode,
                           const int64_t expected_file_size,
                           const int64_t expected_modified_time_ns)
{
  std::vector<int64_t> projected_column_idxs;
  projected_column_idxs.reserve(columns.size());
  for (size_t i = 0; i < columns.size(); ++i) {
    projected_column_idxs.push_back(static_cast<int64_t>(i));
  }
  return open(path, format, columns, expected_device, expected_inode,
              expected_file_size, expected_modified_time_ns, projected_column_idxs);
}

int ObFileScanReader::open(const std::string &path,
                           const ObFileFormat format,
                           const std::vector<ObFileColumnSchema> &columns,
                           const uint64_t expected_device,
                           const uint64_t expected_inode,
                           const int64_t expected_file_size,
                           const int64_t expected_modified_time_ns,
                           const std::vector<int64_t> &projected_column_idxs)
{
  int ret = OB_SUCCESS;
  path_ = path;
  format_ = format;
  columns_ = columns;
  projected_columns_.assign(columns.size(), false);
  for (size_t i = 0; OB_SUCC(ret) && i < projected_column_idxs.size(); ++i) {
    if (projected_column_idxs[i] < 0
        || projected_column_idxs[i] >= static_cast<int64_t>(columns.size())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      projected_columns_[projected_column_idxs[i]] = true;
    }
  }
  expected_device_ = expected_device;
  expected_inode_ = expected_inode;
  expected_file_size_ = expected_file_size;
  expected_modified_time_ns_ = expected_modified_time_ns;
  if (OB_SUCC(ret)) ret = open_inner();
  return ret;
}

int ObFileScanReader::open_inner()
{
  int ret = OB_SUCCESS;
  close();
  std::string actual_path;
  uint64_t actual_device = 0;
  uint64_t actual_inode = 0;
  int64_t actual_file_size = 0;
  int64_t actual_modified_time_ns = 0;
  if (OB_FAIL(ObFileScanUtils::get_file_fingerprint(path_, actual_path,
                                                    actual_device, actual_inode,
                                                    actual_file_size, actual_modified_time_ns))) {
    LOG_WARN("failed to fingerprint file before scan", K(ret));
  } else if (actual_path != path_
             || (0 != expected_device_ && actual_device != expected_device_)
             || (0 != expected_inode_ && actual_inode != expected_inode_)
             || actual_file_size != expected_file_size_
             || actual_modified_time_ns != expected_modified_time_ns_) {
    ret = OB_SCHEMA_EAGAIN;
    LOG_WARN("file changed after schema inference", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (ObFileFormat::PARQUET == format_) {
      parquet_reader_.reset(new (std::nothrow) ObParquetReader());
      if (!parquet_reader_) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(parquet_reader_->open(path_, columns_, expected_device_, expected_inode_,
                                              expected_file_size_, expected_modified_time_ns_,
                                              projected_columns_))) {
        LOG_WARN("failed to open parquet scan", K(ret));
      }
    } else {
      uint64_t opened_device = 0;
      uint64_t opened_inode = 0;
      int64_t opened_size = 0;
      int64_t opened_mtime_ns = 0;
      if (OB_FAIL(open_safe_input_stream(path_, stream_buffer_, stream_,
                                         &opened_device, &opened_inode,
                                         &opened_size, &opened_mtime_ns))) {
        LOG_WARN("failed to safely open file scan", K(ret));
      } else if ((0 != expected_device_ && opened_device != expected_device_)
                 || (0 != expected_inode_ && opened_inode != expected_inode_)
                 || opened_size != expected_file_size_
                 || opened_mtime_ns != expected_modified_time_ns_) {
        ret = OB_SCHEMA_EAGAIN;
        LOG_WARN("opened file does not match inferred fingerprint", K(ret));
        stream_.reset();
        stream_buffer_.reset();
      }
    }
  }
  current_row_number_ = 0;
  csv_header_read_ = false;
  if (OB_FAIL(ret)) {
  } else if (ObFileFormat::CSV == format_) {
    std::vector<std::string> header;
    bool has_record = false;
    if (OB_FAIL(read_csv_record(*stream_, header, has_record))) {
      LOG_WARN("failed to read csv header", K(ret));
    } else if (header.size() != columns_.size()) {
      ret = OB_INVALID_DATA;
    } else {
      csv_header_read_ = true;
    }
  }
  return ret;
}

int ObFileScanReader::get_next_row(std::vector<ObFileCell> &cells)
{
  int ret = OB_SUCCESS;
  switch (format_) {
    case ObFileFormat::CSV:
      ret = get_next_csv_row(cells);
      break;
    case ObFileFormat::JSONL:
      ret = get_next_jsonl_row(cells);
      break;
    case ObFileFormat::PARQUET:
      ret = get_next_parquet_row(cells);
      break;
    default:
      ret = OB_NOT_SUPPORTED;
      break;
  }
  if (OB_ITER_END == ret && !end_verified_) {
    std::string actual_path;
    uint64_t actual_device = 0;
    uint64_t actual_inode = 0;
    int64_t actual_size = 0;
    int64_t actual_mtime = 0;
    const int verify_ret = ObFileScanUtils::get_file_fingerprint(
      path_, actual_path, actual_device, actual_inode, actual_size, actual_mtime);
    if (OB_SUCCESS != verify_ret || actual_path != path_
        || (0 != expected_device_ && actual_device != expected_device_)
        || (0 != expected_inode_ && actual_inode != expected_inode_)
        || actual_size != expected_file_size_
        || actual_mtime != expected_modified_time_ns_) {
      ret = OB_SCHEMA_EAGAIN;
      LOG_WARN("file changed while scanning", K(ret));
    } else {
      end_verified_ = true;
    }
  }
  return ret;
}

int ObFileScanReader::get_next_csv_row(std::vector<ObFileCell> &cells)
{
  int ret = OB_SUCCESS;
  std::vector<std::string> fields;
  bool has_record = false;
  cells.assign(columns_.size(), ObFileCell());
  if (!csv_header_read_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(read_csv_record(*stream_, fields, has_record))) {
    // return iterator end or parse error
  } else if (fields.size() > columns_.size()) {
    ret = OB_INVALID_DATA;
  } else {
    ++current_row_number_;
    for (size_t i = 0; OB_SUCC(ret) && i < fields.size(); ++i) {
      if (projected_columns_[i]) {
        const ParsedValue value = parse_csv_value(fields[i]);
        if (OB_FAIL(convert_value(value, columns_[i].type_, cells[i]))) {
          LOG_WARN("failed to convert csv field", K(ret), K(i), K(current_row_number_));
        }
      }
    }
  }
  return ret;
}

int ObFileScanReader::get_next_jsonl_row(std::vector<ObFileCell> &cells)
{
  int ret = OB_SUCCESS;
  std::string line;
  bool found = false;
  while (!found && std::getline(*stream_, line)) {
    ++current_row_number_;
    if (line.length() > static_cast<size_t>(MAX_FIELD_SIZE)) {
      ret = OB_SIZE_OVERFLOW;
      break;
    }
    if (!line.empty() && '\r' == line.back()) {
      line.pop_back();
    }
    found = !is_blank_line(line);
  }
  if (OB_FAIL(ret)) {
  } else if (!found) {
    ret = OB_ITER_END;
  } else {
    std::vector<ParsedJsonField> fields;
    cells.assign(columns_.size(), ObFileCell());
    if (OB_FAIL(parse_json_object(line, fields))) {
      LOG_WARN("failed to parse jsonl row", K(ret), K(current_row_number_));
    } else {
      for (size_t field_idx = 0; OB_SUCC(ret) && field_idx < fields.size(); ++field_idx) {
        for (size_t column_idx = 0; column_idx < columns_.size(); ++column_idx) {
          if (fields[field_idx].name_ == columns_[column_idx].source_name_) {
            if (projected_columns_[column_idx]
                && OB_FAIL(convert_value(fields[field_idx].value_, columns_[column_idx].type_, cells[column_idx]))) {
              LOG_WARN("failed to convert jsonl field", K(ret), K(field_idx), K(current_row_number_));
            }
            break;
          }
        }
      }
    }
  }
  return ret;
}

int ObFileScanReader::get_next_parquet_row(std::vector<ObFileCell> &cells)
{
  int ret = OB_SUCCESS;
  if (!parquet_reader_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(parquet_reader_->get_next_row(cells))) {
  } else {
    ++current_row_number_;
  }
  return ret;
}

int ObFileScanReader::rescan()
{
  return open_inner();
}

void ObFileScanReader::close()
{
  stream_.reset();
  stream_buffer_.reset();
  parquet_reader_.reset();
  current_row_number_ = 0;
  csv_header_read_ = false;
  end_verified_ = false;
}

} // namespace sql
} // namespace oceanbase
