/**
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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/dml/ob_file_sql_reader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "share/ob_errno.h"
#include "common/ob_accuracy.h"
#include "common/timezone/ob_time_convert.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
namespace file_sql
{

using namespace common;

namespace
{

const int64_t MAX_FILE_SIZE = 512LL * 1024 * 1024;

struct CsvField
{
  ObString value_;
  bool quoted_;
  bool is_null_;
  CsvField() : value_(), quoted_(false), is_null_(false) {}
};

struct CsvRow
{
  ObSEArray<CsvField, 16> fields_;
  int64_t line_;
  CsvRow() : fields_(), line_(0) {}
};

struct JsonCell
{
  FileSqlType kind_;
  ObString text_;
  bool is_null_;
  JsonCell() : kind_(FILE_SQL_NULL), text_(), is_null_(true) {}
};

struct JsonEntry
{
  ObString name_;
  JsonCell cell_;
  JsonEntry() : name_(), cell_() {}
};

struct JsonRow
{
  ObSEArray<JsonEntry, 16> entries_;
  int64_t line_;
  JsonRow() : entries_(), line_(0) {}
};

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_space_char(char c) { return ' ' == c || '\t' == c || '\r' == c || '\n' == c; }

static bool str_case_equal(const ObString &s, const char *lit)
{
  const int64_t len = static_cast<int64_t>(strlen(lit));
  return s.length() == len && 0 == s.case_compare(lit);
}

static bool parse_int64(const ObString &s, int64_t &value)
{
  bool ok = !s.empty();
  int64_t v = 0;
  int64_t i = 0;
  bool neg = false;
  if (ok && ('+' == s[0] || '-' == s[0])) {
    neg = ('-' == s[0]);
    i = 1;
    ok = i < s.length();
  }
  for (; ok && i < s.length(); ++i) {
    char c = s[i];
    if (!is_digit(c)) {
      ok = false;
    } else {
      int digit = c - '0';
      if (!neg && v > (INT64_MAX - digit) / 10) {
        ok = false;
      } else if (neg && v < (INT64_MIN + digit) / 10) {
        ok = false;
      } else {
        v = v * 10 + (neg ? -digit : digit);
      }
    }
  }
  if (ok) {
    value = v;
  }
  return ok;
}

static bool parse_double_value(const ObString &s, double &value)
{
  bool ok = !s.empty();
  for (int64_t i = 0; ok && i < s.length(); ++i) {
    char c = s[i];
    ok = is_digit(c) || '.' == c || 'e' == c || 'E' == c || '+' == c || '-' == c;
  }
  if (ok) {
    std::string buf(s.ptr(), s.length());
    char *end = nullptr;
    double v = strtod(buf.c_str(), &end);
    ok = (nullptr != end && end == buf.c_str() + buf.size());
    if (ok) {
      value = v;
    }
  }
  return ok;
}

static bool parse_bool_value(const ObString &s, bool &value)
{
  bool ok = true;
  if (str_case_equal(s, "true")) {
    value = true;
  } else if (str_case_equal(s, "false")) {
    value = false;
  } else {
    ok = false;
  }
  return ok;
}

static bool match_date_pattern(const ObString &s)
{
  bool ok = (10 == s.length());
  for (int64_t i = 0; ok && i < s.length(); ++i) {
    if (4 == i || 7 == i) {
      ok = ('-' == s[i]);
    } else {
      ok = is_digit(s[i]);
    }
  }
  return ok;
}

static bool match_datetime_pattern(const ObString &s, int32_t &scale)
{
  bool ok = (s.length() >= 19);
  scale = 0;
  for (int64_t i = 0; ok && i < s.length() && i < 19; ++i) {
    if (4 == i || 7 == i) {
      ok = ('-' == s[i]);
    } else if (10 == i) {
      ok = (' ' == s[i] || 'T' == s[i] || 't' == s[i]);
    } else if (13 == i || 16 == i) {
      ok = (':' == s[i]);
    } else {
      ok = is_digit(s[i]);
    }
  }
  if (ok && s.length() > 19) {
    ok = ('.' == s[19]);
    for (int64_t i = 20; ok && i < s.length(); ++i) {
      ok = is_digit(s[i]) && i - 20 < 6;
      if (ok) {
        ++scale;
      }
    }
  }
  return ok;
}

static bool parse_date_value(const ObString &s, const ObSQLSessionInfo *session, int32_t &value)
{
  bool ok = match_date_pattern(s);
  if (ok) {
    ObDateSqlMode sql_mode;
    if (nullptr != session) {
      sql_mode.init(session->get_sql_mode());
    }
    ok = (OB_SUCCESS == ObTimeConverter::str_to_date(s, value, sql_mode));
  }
  return ok;
}

static bool parse_datetime_value(const ObString &s, const ObSQLSessionInfo *session, int64_t &value)
{
  int32_t scale = 0;
  bool ok = match_datetime_pattern(s, scale);
  if (ok) {
    ObDateSqlMode sql_mode;
    int16_t scale_out = static_cast<int16_t>(scale);
    ObTimeConvertCtx cvrt_ctx(nullptr == session ? nullptr : TZ_INFO(session), false);
    if (nullptr != session) {
      sql_mode.init(session->get_sql_mode());
      sql_mode.allow_invalid_dates_ = false;
    }
    ok = (OB_SUCCESS == ObTimeConverter::str_to_datetime(s, cvrt_ctx, value, &scale_out, sql_mode));
  }
  return ok;
}

static FileSqlType classify_text(const ObString &s, const ObSQLSessionInfo *session)
{
  FileSqlType type = FILE_SQL_VARCHAR;
  bool dummy_bool = false;
  int64_t int_value = 0;
  double double_value = 0;
  int32_t date_value = 0;
  int64_t datetime_value = 0;
  if (str_case_equal(s, "true") || str_case_equal(s, "false")) {
    type = FILE_SQL_BOOLEAN;
  } else if (parse_int64(s, int_value)) {
    type = FILE_SQL_BIGINT;
  } else if (parse_double_value(s, double_value)) {
    type = FILE_SQL_DOUBLE;
  } else if (parse_date_value(s, session, date_value)) {
    type = FILE_SQL_DATE;
  } else if (parse_datetime_value(s, session, datetime_value)) {
    type = FILE_SQL_DATETIME;
  }
  UNUSED(dummy_bool);
  return type;
}

static FileSqlType merge_type(const FileSqlType lhs, const FileSqlType rhs)
{
  FileSqlType merged = FILE_SQL_VARCHAR;
  if (FILE_SQL_NULL == lhs) {
    merged = rhs;
  } else if (FILE_SQL_NULL == rhs || lhs == rhs) {
    merged = lhs;
  } else if ((FILE_SQL_BIGINT == lhs && FILE_SQL_DOUBLE == rhs)
             || (FILE_SQL_DOUBLE == lhs && FILE_SQL_BIGINT == rhs)) {
    merged = FILE_SQL_DOUBLE;
  } else if ((FILE_SQL_DATE == lhs && FILE_SQL_DATETIME == rhs)
             || (FILE_SQL_DATETIME == lhs && FILE_SQL_DATE == rhs)) {
    merged = FILE_SQL_DATETIME;
  } else {
    merged = FILE_SQL_VARCHAR;
  }
  return merged;
}

static int read_whole_file(const ObString &path, ObIAllocator &allocator, ObString &content)
{
  int ret = OB_SUCCESS;
  std::string file_path(path.ptr(), path.length());
  FILE *fp = fopen(file_path.c_str(), "rb");
  if (nullptr == fp) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read file, cannot open the file");
  } else {
    int64_t file_size = 0;
    if (0 != fseek(fp, 0, SEEK_END) || (file_size = ftell(fp)) < 0 || file_size > MAX_FILE_SIZE) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read file, invalid file size");
    } else if (0 != fseek(fp, 0, SEEK_SET)) {
      ret = OB_IO_ERROR;
    } else {
      char *buffer = static_cast<char *>(allocator.alloc(file_size + 1));
      if (nullptr == buffer) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (file_size > 0 && file_size != static_cast<int64_t>(fread(buffer, 1, file_size, fp))) {
        ret = OB_IO_ERROR;
      } else {
        buffer[file_size] = '\0';
        content.assign_ptr(buffer, static_cast<int32_t>(file_size));
      }
    }
    fclose(fp);
  }
  return ret;
}

static int copy_string(const ObString &src, ObIAllocator &allocator, ObString &dst)
{
  int ret = OB_SUCCESS;
  if (src.empty()) {
    dst.reset();
  } else {
    char *buffer = static_cast<char *>(allocator.alloc(src.length()));
    if (nullptr == buffer) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buffer, src.ptr(), src.length());
      dst.assign_ptr(buffer, src.length());
    }
  }
  return ret;
}

static bool is_wildcard(const char *pattern, const int64_t len)
{
  for (int64_t i = 0; i < len; ++i) {
    if ('*' == pattern[i] || '?' == pattern[i]) {
      return true;
    }
  }
  return false;
}

static bool wildcard_match(const char *pattern, const int64_t plen,
                           const char *text, const int64_t tlen)
{
  int64_t pi = 0;
  int64_t ti = 0;
  int64_t star_pi = -1;
  int64_t star_ti = 0;
  while (ti < tlen) {
    if (pi < plen && ('?' == pattern[pi] || pattern[pi] == text[ti])) {
      ++pi;
      ++ti;
    } else if (pi < plen && '*' == pattern[pi]) {
      star_pi = pi++;
      star_ti = ti;
    } else if (-1 != star_pi) {
      pi = star_pi + 1;
      ti = ++star_ti;
    } else {
      return false;
    }
  }
  while (pi < plen && '*' == pattern[pi]) {
    ++pi;
  }
  return pi == plen;
}

static int list_dir(const std::string &dir,
                    std::vector<std::string> &names,
                    std::vector<bool> &is_dir)
{
  int ret = OB_SUCCESS;
  names.clear();
  is_dir.clear();
#ifdef _WIN32
  std::string pattern = dir + "\\*";
  WIN32_FIND_DATAA find_data;
  HANDLE handle = FindFirstFileA(pattern.c_str(), &find_data);
  if (INVALID_HANDLE_VALUE != handle) {
    do {
      std::string name(find_data.cFileName);
      if ("." != name && ".." != name) {
        names.push_back(name);
        is_dir.push_back(0 != (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
      }
    } while (FindNextFileA(handle, &find_data));
    FindClose(handle);
  }
#else
  DIR *dirp = opendir(dir.c_str());
  if (nullptr != dirp) {
    struct dirent *entry = nullptr;
    while (nullptr != (entry = readdir(dirp))) {
      std::string name(entry->d_name);
      if ("." != name && ".." != name) {
        names.push_back(name);
        std::string full = dir + "/" + name;
        bool dir_flag = false;
        struct stat st;
        if (0 == stat(full.c_str(), &st) && S_ISDIR(st.st_mode)) {
          dir_flag = true;
        }
        is_dir.push_back(dir_flag);
      }
    }
    closedir(dirp);
  }
#endif
  return ret;
}

static bool path_exists(const std::string &path)
{
  bool exists = false;
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path.c_str());
  exists = (INVALID_FILE_ATTRIBUTES != attr);
#else
  struct stat st;
  exists = (0 == stat(path.c_str(), &st));
#endif
  return exists;
}

static bool path_is_dir(const std::string &path)
{
  bool is_dir = false;
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path.c_str());
  is_dir = (INVALID_FILE_ATTRIBUTES != attr && 0 != (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
  struct stat st;
  is_dir = (0 == stat(path.c_str(), &st) && S_ISDIR(st.st_mode));
#endif
  return is_dir;
}

static int parse_csv_content(const ObString &content,
                             const FileSqlOptions &options,
                             ObIAllocator &allocator,
                             ObSEArray<CsvRow, 32> &rows)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  const int64_t size = content.length();
  int64_t pos = 0;
  int64_t line_no = 1;
  bool first_row = true;
  int64_t expected_fields = 0;
  while (OB_SUCC(ret) && pos < size) {
    CsvRow row;
    row.line_ = line_no;
    bool end_of_row = false;
    while (OB_SUCC(ret) && !end_of_row) {
      CsvField field;
      if (pos < size && options.quote_ == data[pos]) {
        field.quoted_ = true;
        ++pos;
        std::string value;
        bool closed = false;
        while (OB_SUCC(ret) && pos < size && !closed) {
          char c = data[pos];
          if (options.escape_ != options.quote_ && c == options.escape_) {
            if (pos + 1 < size) {
              value.push_back(data[pos + 1]);
              pos += 2;
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_csv, invalid escape at end of file");
            }
          } else if (c == options.quote_) {
            if (pos + 1 < size && data[pos + 1] == options.quote_) {
              value.push_back(options.quote_);
              pos += 2;
            } else {
              closed = true;
              ++pos;
            }
          } else {
            if ('\n' == c) {
              ++line_no;
            }
            value.push_back(c);
            ++pos;
          }
        }
        if (OB_FAIL(ret)) {
        } else if (!closed) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_csv, unterminated quoted field");
          LOG_WARN("unterminated quoted field", K(ret), K(row.line_));
        } else if (OB_FAIL(copy_string(ObString(static_cast<int32_t>(value.size()), value.data()),
                                       allocator, field.value_))) {
        }
      } else {
        int64_t start = pos;
        while (pos < size) {
          char c = data[pos];
          if (c == options.delimiter_ || '\n' == c || '\r' == c) {
            break;
          }
          ++pos;
        }
        field.value_.assign_ptr(data + start, static_cast<int32_t>(pos - start));
        if (field.value_.empty()) {
          field.is_null_ = true;
        } else if (!options.null_str_.empty() && field.value_.length() == options.null_str_.length()
                   && 0 == MEMCMP(field.value_.ptr(), options.null_str_.ptr(), field.value_.length())) {
          field.is_null_ = true;
        }
        if (OB_FAIL(copy_string(field.value_, allocator, field.value_))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(row.fields_.push_back(field))) {
      } else if (pos >= size) {
        end_of_row = true;
      } else if (options.delimiter_ == data[pos]) {
        ++pos;
        if (pos >= size) {
          CsvField tail;
          tail.is_null_ = true;
          if (OB_FAIL(row.fields_.push_back(tail))) {
          }
          end_of_row = true;
        }
      } else {
        // '\r' or '\n'
        if ('\r' == data[pos]) {
          ++pos;
          if (pos < size && '\n' == data[pos]) {
            ++pos;
          }
        } else {
          ++pos;
        }
        end_of_row = true;
        ++line_no;
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      bool blank_line = 1 == row.fields_.count() && row.fields_.at(0).is_null_
                        && !row.fields_.at(0).quoted_ && first_row;
      if (blank_line) {
        // skip the leading empty line only
      } else if (1 == row.fields_.count() && row.fields_.at(0).is_null_
                 && !row.fields_.at(0).quoted_ && !first_row) {
        // skip empty lines inside the file
      } else {
        if (first_row) {
          expected_fields = row.fields_.count();
          first_row = false;
        } else if (row.fields_.count() != expected_fields) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_csv, column count mismatch");
          LOG_WARN("column count mismatch", K(ret), K(row.line_), K(row.fields_.count()),
                   K(expected_fields));
        }
        if (OB_SUCC(ret) && OB_FAIL(rows.push_back(row))) {
        }
      }
    }
  }
  return ret;
}

static int infer_csv_schema(const ObSEArray<CsvRow, 32> &rows,
                            const bool has_header,
                            const ObSQLSessionInfo *session,
                            ObIAllocator &allocator,
                            ObSEArray<FileSqlColumn, 8> &columns)
{
  int ret = OB_SUCCESS;
  int64_t column_cnt = 0;
  if (!rows.empty()) {
    column_cnt = rows.at(0).fields_.count();
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt; ++i) {
    FileSqlColumn column;
    column.type_ = FILE_SQL_NULL;
    column.length_ = 1;
    if (has_header) {
      const ObString &name = rows.at(0).fields_.at(i).value_;
      if (OB_FAIL(copy_string(name, allocator, column.name_))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      if (column.name_.empty()) {
        char buffer[32];
        int len = snprintf(buffer, sizeof(buffer), "column_%ld", i + 1);
        ObString generated(len, buffer);
        if (OB_FAIL(copy_string(generated, allocator, column.name_))) {
        }
      }
      if (OB_FAIL(columns.push_back(column))) {
      }
    }
  }
  for (int64_t r = has_header ? 1 : 0; OB_SUCC(ret) && r < rows.count(); ++r) {
    const CsvRow &row = rows.at(r);
    for (int64_t c = 0; OB_SUCC(ret) && c < row.fields_.count() && c < columns.count(); ++c) {
      const CsvField &field = row.fields_.at(c);
      FileSqlColumn &column = columns.at(c);
      if (field.is_null_) {
        column.nullable_ = true;
      } else {
        FileSqlType type = classify_text(field.value_, session);
        column.type_ = merge_type(column.type_, type);
        if (field.value_.length() > column.length_) {
          column.length_ = field.value_.length();
        }
      }
    }
  }
  return ret;
}

static int build_value(const FileSqlType column_type,
                       const ObString &text,
                       const bool is_null,
                       const ObSQLSessionInfo *session,
                       common::ObObjParam &value)
{
  int ret = OB_SUCCESS;
  if (is_null) {
    value.set_null();
  } else if (FILE_SQL_BIGINT == column_type) {
    int64_t int_value = 0;
    if (!parse_int64(text, int_value)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid bigint value", K(ret), K(text));
    } else {
      value.set_int(int_value);
      value.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType]);
    }
  } else if (FILE_SQL_DOUBLE == column_type) {
    double double_value = 0;
    if (!parse_double_value(text, double_value)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid double value", K(ret), K(text));
    } else {
      value.set_double(double_value);
      value.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObDoubleType]);
    }
  } else if (FILE_SQL_BOOLEAN == column_type) {
    bool bool_value = false;
    if (!parse_bool_value(text, bool_value)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid boolean value", K(ret), K(text));
    } else {
      value.set_int(ObTinyIntType, bool_value ? 1 : 0);
      value.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObTinyIntType]);
    }
  } else if (FILE_SQL_DATE == column_type) {
    int32_t date_value = 0;
    if (!parse_date_value(text, session, date_value)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid date value", K(ret), K(text));
    } else {
      value.set_date(date_value);
      value.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObDateType]);
    }
  } else if (FILE_SQL_DATETIME == column_type) {
    int64_t datetime_value = 0;
    if (!parse_datetime_value(text, session, datetime_value)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid datetime value", K(ret), K(text));
    } else {
      value.set_datetime(datetime_value);
      value.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObDateTimeType]);
    }
  } else {
    value.set_string(ObVarcharType, text);
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    value.set_accuracy(ObAccuracy(text.length()));
  }
  if (OB_SUCC(ret)) {
    value.set_param_meta();
  }
  return ret;
}

static int fill_csv_values(const ObSEArray<CsvRow, 32> &rows,
                           const bool has_header,
                           const ObSQLSessionInfo *session,
                           FileSqlTable &table)
{
  int ret = OB_SUCCESS;
  for (int64_t r = has_header ? 1 : 0; OB_SUCC(ret) && r < rows.count(); ++r) {
    const CsvRow &row = rows.at(r);
    for (int64_t c = 0; OB_SUCC(ret) && c < table.columns_.count(); ++c) {
      ObString text;
      bool is_null = true;
      if (c < row.fields_.count()) {
        text = row.fields_.at(c).value_;
        is_null = row.fields_.at(c).is_null_;
      }
      ObObjParam value;
      if (OB_FAIL(build_value(table.columns_.at(c).type_, text, is_null, session, value))) {
      } else if (OB_FAIL(table.values_.push_back(value))) {
      }
    }
    if (OB_SUCC(ret)) {
      ++table.row_cnt_;
    }
  }
  table.column_cnt_ = table.columns_.count();
  return ret;
}

static int fill_json_row_values(const ObSEArray<JsonRow, 32> &rows, FileSqlTable &table)
{
  int ret = OB_SUCCESS;
  for (int64_t r = 0; OB_SUCC(ret) && r < rows.count(); ++r) {
    const JsonRow &row = rows.at(r);
    for (int64_t c = 0; OB_SUCC(ret) && c < table.columns_.count(); ++c) {
      const FileSqlColumn &column = table.columns_.at(c);
      const JsonCell *found = nullptr;
      for (int64_t e = 0; e < row.entries_.count(); ++e) {
        const JsonEntry &entry = row.entries_.at(e);
        if (entry.name_.length() == column.name_.length()
            && 0 == MEMCMP(entry.name_.ptr(), column.name_.ptr(), column.name_.length())) {
          found = &entry.cell_;
          break;
        }
      }
      ObString text;
      bool is_null = true;
      if (nullptr != found && !found->is_null_) {
        text = found->text_;
        is_null = false;
      }
      ObObjParam value;
      if (OB_FAIL(build_value(column.type_, text, is_null, nullptr, value))) {
      } else if (OB_FAIL(table.values_.push_back(value))) {
      }
    }
    if (OB_SUCC(ret)) {
      ++table.row_cnt_;
    }
  }
  table.column_cnt_ = table.columns_.count();
  return ret;
}

static int skip_json_space(const char *data, const int64_t size, int64_t &pos, int64_t &line)
{
  while (pos < size && is_space_char(data[pos])) {
    if ('\n' == data[pos]) {
      ++line;
    }
    ++pos;
  }
  return OB_SUCCESS;
}

static int parse_json_string(const char *data, const int64_t size, int64_t &pos,
                             int64_t &line, ObIAllocator &allocator, ObString &out)
{
  int ret = OB_SUCCESS;
  std::string value;
  bool closed = false;
  ++pos; // skip opening quote
  while (OB_SUCC(ret) && pos < size && !closed) {
    char c = data[pos];
    if ('"' == c) {
      closed = true;
      ++pos;
    } else if ('\\' == c) {
      if (pos + 1 >= size) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        char escape = data[pos + 1];
        pos += 2;
        switch (escape) {
          case '"': value.push_back('"'); break;
          case '\\': value.push_back('\\'); break;
          case '/': value.push_back('/'); break;
          case 'b': value.push_back('\b'); break;
          case 'f': value.push_back('\f'); break;
          case 'n': value.push_back('\n'); break;
          case 'r': value.push_back('\r'); break;
          case 't': value.push_back('\t'); break;
          case 'u': {
            if (pos + 4 > size) {
              ret = OB_INVALID_ARGUMENT;
            } else {
              uint32_t code = 0;
              bool ok = true;
              for (int i = 0; i < 4 && ok; ++i) {
                char h = data[pos + i];
                int v = 0;
                if (h >= '0' && h <= '9') {
                  v = h - '0';
                } else if (h >= 'a' && h <= 'f') {
                  v = h - 'a' + 10;
                } else if (h >= 'A' && h <= 'F') {
                  v = h - 'A' + 10;
                } else {
                  ok = false;
                }
                code = (code << 4) | v;
              }
              if (!ok) {
                ret = OB_INVALID_ARGUMENT;
              } else {
                pos += 4;
                if (code >= 0xD800 && code <= 0xDBFF && pos + 6 <= size
                    && '\\' == data[pos] && 'u' == data[pos + 1]) {
                  uint32_t low = 0;
                  bool low_ok = true;
                  for (int i = 0; i < 4 && low_ok; ++i) {
                    char h = data[pos + 2 + i];
                    int v = 0;
                    if (h >= '0' && h <= '9') {
                      v = h - '0';
                    } else if (h >= 'a' && h <= 'f') {
                      v = h - 'a' + 10;
                    } else if (h >= 'A' && h <= 'F') {
                      v = h - 'A' + 10;
                    } else {
                      low_ok = false;
                    }
                    low = (low << 4) | v;
                  }
                  if (low_ok && low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    pos += 6;
                  }
                }
                if (code < 0x80) {
                  value.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                  value.push_back(static_cast<char>(0xC0 | (code >> 6)));
                  value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code < 0x10000) {
                  value.push_back(static_cast<char>(0xE0 | (code >> 12)));
                  value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                  value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                  value.push_back(static_cast<char>(0xF0 | (code >> 18)));
                  value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                  value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                  value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
              }
            }
            break;
          }
          default:
            ret = OB_INVALID_ARGUMENT;
            break;
        }
      }
    } else {
      if ('\n' == c) {
        ++line;
      }
      value.push_back(c);
      ++pos;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!closed) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(copy_string(ObString(static_cast<int32_t>(value.size()), value.data()),
                                 allocator, out))) {
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("invalid json string", K(ret), K(line));
  }
  return ret;
}

static int parse_json_scalar(const char *data, const int64_t size, int64_t &pos, int64_t &line,
                             ObIAllocator &allocator, JsonCell &cell)
{
  int ret = OB_SUCCESS;
  skip_json_space(data, size, pos, line);
  if (pos >= size) {
    ret = OB_INVALID_ARGUMENT;
  } else if ('"' == data[pos]) {
    ObString value;
    if (OB_FAIL(parse_json_string(data, size, pos, line, allocator, value))) {
    } else {
      cell.kind_ = FILE_SQL_VARCHAR;
      cell.text_ = value;
      cell.is_null_ = false;
    }
  } else if ('{' == data[pos] || '[' == data[pos]) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_json, nested object or array");
    LOG_WARN("nested object or array is not supported", K(ret), K(line));
  } else if (pos + 4 <= size && 0 == strncmp(data + pos, "true", 4)) {
    cell.kind_ = FILE_SQL_BOOLEAN;
    cell.text_.assign_ptr("true", 4);
    cell.is_null_ = false;
    pos += 4;
  } else if (pos + 5 <= size && 0 == strncmp(data + pos, "false", 5)) {
    cell.kind_ = FILE_SQL_BOOLEAN;
    cell.text_.assign_ptr("false", 5);
    cell.is_null_ = false;
    pos += 5;
  } else if (pos + 4 <= size && 0 == strncmp(data + pos, "null", 4)) {
    cell.is_null_ = true;
    cell.kind_ = FILE_SQL_NULL;
    pos += 4;
  } else {
    int64_t start = pos;
    while (pos < size && ',' != data[pos] && '}' != data[pos] && ']' != data[pos]
           && !is_space_char(data[pos])) {
      ++pos;
    }
    ObString token(static_cast<int32_t>(pos - start), data + start);
    int64_t int_value = 0;
    double double_value = 0;
    if (token.empty()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (parse_int64(token, int_value)) {
      cell.kind_ = FILE_SQL_BIGINT;
      cell.text_ = token;
      cell.is_null_ = false;
    } else if (parse_double_value(token, double_value)) {
      cell.kind_ = FILE_SQL_DOUBLE;
      cell.text_ = token;
      cell.is_null_ = false;
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("invalid json value", K(ret), K(line));
    }
  }
  return ret;
}

static int parse_json_object(const char *data, const int64_t size, int64_t &pos, int64_t &line,
                             ObIAllocator &allocator, JsonRow &row)
{
  int ret = OB_SUCCESS;
  skip_json_space(data, size, pos, line);
  if (pos >= size || '{' != data[pos]) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expect json object", K(ret), K(line));
  } else {
    ++pos;
    skip_json_space(data, size, pos, line);
    if (pos < size && '}' == data[pos]) {
      ++pos;
    } else {
      bool finished = false;
      while (OB_SUCC(ret) && !finished) {
        skip_json_space(data, size, pos, line);
        if (pos >= size || '"' != data[pos]) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("expect json object key", K(ret), K(line));
        } else {
          JsonEntry entry;
          if (OB_FAIL(parse_json_string(data, size, pos, line, allocator, entry.name_))) {
          } else {
            skip_json_space(data, size, pos, line);
            if (pos >= size || ':' != data[pos]) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("expect json colon", K(ret), K(line));
            } else {
              ++pos;
              if (OB_FAIL(parse_json_scalar(data, size, pos, line, allocator, entry.cell_))) {
              } else if (OB_FAIL(row.entries_.push_back(entry))) {
              }
            }
          }
        }
        if (OB_FAIL(ret)) {
        } else {
          skip_json_space(data, size, pos, line);
          if (pos < size && ',' == data[pos]) {
            ++pos;
          } else if (pos < size && '}' == data[pos]) {
            ++pos;
            finished = true;
          } else {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("unexpected json object separator", K(ret), K(line));
          }
        }
      }
    }
  }
  return ret;
}

static int build_json_columns(const ObSEArray<JsonRow, 32> &rows,
                              ObIAllocator &allocator,
                              FileSqlTable &table)
{
  int ret = OB_SUCCESS;
  for (int64_t r = 0; OB_SUCC(ret) && r < rows.count(); ++r) {
    const JsonRow &row = rows.at(r);
    for (int64_t e = 0; OB_SUCC(ret) && e < row.entries_.count(); ++e) {
      const JsonEntry &entry = row.entries_.at(e);
      int64_t column_index = -1;
      for (int64_t c = 0; c < table.columns_.count(); ++c) {
        if (table.columns_.at(c).name_.length() == entry.name_.length()
            && 0 == MEMCMP(table.columns_.at(c).name_.ptr(), entry.name_.ptr(),
                           entry.name_.length())) {
          column_index = c;
          break;
        }
      }
      if (-1 == column_index) {
        FileSqlColumn column;
        column.type_ = FILE_SQL_NULL;
        column.length_ = 1;
        if (OB_FAIL(copy_string(entry.name_, allocator, column.name_))) {
        } else if (OB_FAIL(table.columns_.push_back(column))) {
        } else {
          column_index = table.columns_.count() - 1;
        }
      }
      if (OB_SUCC(ret)) {
        FileSqlColumn &column = table.columns_.at(column_index);
        if (!entry.cell_.is_null_) {
          column.type_ = merge_type(column.type_, entry.cell_.kind_);
          if (entry.cell_.text_.length() > column.length_) {
            column.length_ = entry.cell_.text_.length();
          }
        }
      }
    }
  }
  return ret;
}

static int parse_ndjson_rows(const ObString &content, ObIAllocator &allocator,
                             ObSEArray<JsonRow, 32> &rows)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  const int64_t size = content.length();
  int64_t pos = 0;
  int64_t line = 1;
  while (OB_SUCC(ret) && pos < size) {
    skip_json_space(data, size, pos, line);
    if (pos >= size) {
      break;
    }
    JsonRow row;
    row.line_ = line;
    if (OB_FAIL(parse_json_object(data, size, pos, line, allocator, row))) {
      LOG_WARN("failed to parse json line", K(ret), K(line));
    } else if (OB_FAIL(rows.push_back(row))) {
    }
  }
  return ret;
}

// read_json accepts a top level array of flat objects.
static int parse_json_array_rows(const ObString &content, ObIAllocator &allocator,
                                 ObSEArray<JsonRow, 32> &rows)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  const int64_t size = content.length();
  int64_t pos = 0;
  int64_t line = 1;
  skip_json_space(data, size, pos, line);
  if (pos >= size || '[' != data[pos]) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("expect json array", K(ret), K(line));
  } else {
    ++pos;
    skip_json_space(data, size, pos, line);
    if (pos < size && ']' == data[pos]) {
      ++pos;
    } else {
      bool finished = false;
      while (OB_SUCC(ret) && !finished) {
        JsonRow row;
        row.line_ = line;
        if (OB_FAIL(parse_json_object(data, size, pos, line, allocator, row))) {
          LOG_WARN("failed to parse json object in array", K(ret), K(line));
        } else if (OB_FAIL(rows.push_back(row))) {
        } else {
          skip_json_space(data, size, pos, line);
          if (pos < size && ',' == data[pos]) {
            ++pos;
          } else if (pos < size && ']' == data[pos]) {
            ++pos;
            finished = true;
          } else {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("unexpected json array separator", K(ret), K(line));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      skip_json_space(data, size, pos, line);
      if (pos < size) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unexpected content after json array", K(ret), K(line));
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Parquet reader helpers
// ---------------------------------------------------------------------------

const int32_t PQ_PHYS_BOOLEAN = 0;
const int32_t PQ_PHYS_INT32 = 1;
const int32_t PQ_PHYS_INT64 = 2;
const int32_t PQ_PHYS_FLOAT = 4;
const int32_t PQ_PHYS_DOUBLE = 5;
const int32_t PQ_PHYS_BYTE_ARRAY = 6;
const int32_t PQ_PHYS_FLBA = 7;

const int32_t PQ_REP_REQUIRED = 0;

const int32_t PQ_CODEC_UNCOMPRESSED = 0;
const int32_t PQ_CODEC_SNAPPY = 1;

const int32_t PQ_PAGE_DATA_V1 = 0;
const int32_t PQ_PAGE_DICTIONARY = 2;
const int32_t PQ_PAGE_DATA_V2 = 3;

const int32_t PQ_ENC_PLAIN = 0;
const int32_t PQ_ENC_PLAIN_DICTIONARY = 2;
const int32_t PQ_ENC_RLE = 3;
const int32_t PQ_ENC_RLE_DICTIONARY = 8;

const int32_t PQ_CONVERTED_DATE = 6;
const int32_t PQ_CONVERTED_TIMESTAMP_MILLIS = 9;
const int32_t PQ_CONVERTED_TIMESTAMP_MICROS = 10;

struct PqThriftReader
{
  const char *data_;
  int64_t size_;
  int64_t pos_;
  int16_t last_field_id_;
  PqThriftReader(const char *data, const int64_t size, const int64_t pos)
    : data_(data), size_(size), pos_(pos), last_field_id_(0) {}
  int read_byte(uint8_t &value)
  {
    int ret = OB_SUCCESS;
    if (pos_ < 0 || pos_ + 1 > size_) {
      ret = OB_IO_ERROR;
    } else {
      value = static_cast<uint8_t>(data_[pos_]);
      ++pos_;
    }
    return ret;
  }
  int read_varint(uint64_t &value)
  {
    int ret = OB_SUCCESS;
    value = 0;
    int64_t shift = 0;
    bool done = false;
    for (int64_t i = 0; OB_SUCC(ret) && !done; ++i) {
      uint8_t byte = 0;
      if (i >= 10 || shift > 63) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(read_byte(byte))) {
      } else {
        value |= static_cast<uint64_t>(byte & 0x7f) << shift;
        done = (0 == (byte & 0x80));
        shift += 7;
      }
    }
    return ret;
  }
  int read_zigzag(int64_t &value)
  {
    int ret = OB_SUCCESS;
    uint64_t raw = 0;
    if (OB_FAIL(read_varint(raw))) {
    } else {
      value = static_cast<int64_t>(raw >> 1) ^ -static_cast<int64_t>(raw & 1);
    }
    return ret;
  }
  int read_binary(const char *&ptr, int64_t &len)
  {
    int ret = OB_SUCCESS;
    uint64_t var_len = 0;
    ptr = nullptr;
    len = 0;
    if (OB_FAIL(read_varint(var_len))) {
    } else if (static_cast<int64_t>(var_len) > size_ - pos_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ptr = data_ + pos_;
      len = static_cast<int64_t>(var_len);
      pos_ += len;
    }
    return ret;
  }
  int read_list_begin(uint8_t &elem_type, int64_t &count)
  {
    int ret = OB_SUCCESS;
    uint8_t byte = 0;
    elem_type = 0;
    count = 0;
    if (OB_FAIL(read_byte(byte))) {
    } else {
      elem_type = byte & 0x0f;
      count = (byte >> 4) & 0x0f;
      if (15 == count) {
        uint64_t var_count = 0;
        if (OB_FAIL(read_varint(var_count))) {
        } else if (var_count > static_cast<uint64_t>(size_)) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          count = static_cast<int64_t>(var_count);
        }
      }
    }
    return ret;
  }
  int read_struct_field(uint8_t &type, int16_t &id, bool &stop)
  {
    int ret = OB_SUCCESS;
    uint8_t byte = 0;
    type = 0;
    stop = false;
    if (OB_FAIL(read_byte(byte))) {
    } else if (0 == byte) {
      stop = true;
    } else {
      type = byte & 0x0f;
      const int16_t delta = static_cast<int16_t>((byte >> 4) & 0x0f);
      if (0 == delta) {
        int64_t zigzag_id = 0;
        if (OB_FAIL(read_zigzag(zigzag_id))) {
        } else {
          id = static_cast<int16_t>(zigzag_id);
        }
      } else {
        id = static_cast<int16_t>(last_field_id_ + delta);
      }
      last_field_id_ = id;
    }
    return ret;
  }
  int skip_value(uint8_t type)
  {
    int ret = OB_SUCCESS;
    switch (type) {
      case 1: // boolean true, value encoded in the field type
      case 2: // boolean false
        break;
      case 3: {
        uint8_t byte = 0;
        ret = read_byte(byte);
        break;
      }
      case 4:
      case 5:
      case 6: {
        int64_t value = 0;
        ret = read_zigzag(value);
        break;
      }
      case 7: {
        if (size_ - pos_ < 8) {
          ret = OB_IO_ERROR;
        } else {
          pos_ += 8;
        }
        break;
      }
      case 8: {
        const char *ptr = nullptr;
        int64_t len = 0;
        ret = read_binary(ptr, len);
        break;
      }
      case 9:
      case 10: {
        uint8_t elem_type = 0;
        int64_t count = 0;
        if (OB_FAIL(read_list_begin(elem_type, count))) {
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
            ret = skip_value(elem_type);
          }
        }
        break;
      }
      case 11: {
        uint8_t byte = 0;
        int64_t count = 0;
        if (OB_FAIL(read_byte(byte))) {
        } else {
          count = (byte >> 4) & 0x0f;
          if (0 == count) {
            uint64_t var_count = 0;
            if (OB_FAIL(read_varint(var_count))) {
            } else {
              count = static_cast<int64_t>(var_count);
            }
          }
          uint8_t types = 0;
          if (OB_SUCC(ret) && OB_FAIL(read_byte(types))) {
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
            if (OB_FAIL(skip_value(types >> 4))) {
            } else if (OB_FAIL(skip_value(types & 0x0f))) {
            }
          }
        }
        break;
      }
      case 12: {
        const int16_t saved_field_id = last_field_id_;
        last_field_id_ = 0;
        bool stop = false;
        while (OB_SUCC(ret) && !stop) {
          uint8_t field_type = 0;
          int16_t field_id = 0;
          if (OB_FAIL(read_struct_field(field_type, field_id, stop))) {
          } else if (!stop) {
            ret = skip_value(field_type);
          }
        }
        last_field_id_ = saved_field_id;
        break;
      }
      default:
        ret = OB_INVALID_ARGUMENT;
        break;
    }
    return ret;
  }
};

struct PqSchemaElement
{
  int32_t physical_type_;
  int32_t type_length_;
  int32_t repetition_;
  int32_t converted_type_;
  int32_t num_children_;
  std::string name_;
  PqSchemaElement()
    : physical_type_(-1), type_length_(0), repetition_(0), converted_type_(-1),
      num_children_(0) {}
};

struct PqColumnMeta
{
  int32_t physical_type_;
  int32_t codec_;
  int64_t num_values_;
  int64_t data_page_offset_;
  int64_t dictionary_page_offset_;
  int64_t total_compressed_size_;
  PqColumnMeta()
    : physical_type_(-1), codec_(0), num_values_(0), data_page_offset_(0),
      dictionary_page_offset_(0), total_compressed_size_(0) {}
};

struct PqRowGroup
{
  std::vector<PqColumnMeta> columns_;
  int64_t num_rows_;
  PqRowGroup() : columns_(), num_rows_(0) {}
};

struct PqPageHeader
{
  int32_t type_;
  int32_t uncompressed_page_size_;
  int32_t compressed_page_size_;
  int32_t num_values_;
  int32_t encoding_;
  int32_t num_nulls_;
  int32_t def_levels_byte_length_;
  int32_t rep_levels_byte_length_;
  bool is_compressed_;
  PqPageHeader()
    : type_(-1), uncompressed_page_size_(0), compressed_page_size_(0), num_values_(0),
      encoding_(PQ_ENC_PLAIN), num_nulls_(0), def_levels_byte_length_(0),
      rep_levels_byte_length_(0), is_compressed_(true) {}
};

static int pq_parse_schema_element(PqThriftReader &reader, PqSchemaElement &element)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        element.physical_type_ = static_cast<int32_t>(value);
      }
    } else if (2 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        element.type_length_ = static_cast<int32_t>(value);
      }
    } else if (3 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        element.repetition_ = static_cast<int32_t>(value);
      }
    } else if (4 == id) {
      const char *ptr = nullptr;
      int64_t len = 0;
      if (OB_FAIL(reader.read_binary(ptr, len))) {
      } else {
        element.name_.assign(ptr, static_cast<size_t>(len));
      }
    } else if (5 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        element.num_children_ = static_cast<int32_t>(value);
      }
    } else if (6 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        element.converted_type_ = static_cast<int32_t>(value);
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_column_meta(PqThriftReader &reader, PqColumnMeta &meta)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        meta.physical_type_ = static_cast<int32_t>(value);
      }
    } else if (4 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        meta.codec_ = static_cast<int32_t>(value);
      }
    } else if (5 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        meta.num_values_ = value;
      }
    } else if (7 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        meta.total_compressed_size_ = value;
      }
    } else if (9 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        meta.data_page_offset_ = value;
      }
    } else if (11 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        meta.dictionary_page_offset_ = value;
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_column_chunk(PqThriftReader &reader, PqColumnMeta &meta)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (3 == id) {
      PqThriftReader meta_reader(reader.data_, reader.size_, reader.pos_);
      meta_reader.last_field_id_ = 0;
      if (OB_FAIL(pq_parse_column_meta(meta_reader, meta))) {
      } else {
        reader.pos_ = meta_reader.pos_;
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_row_group(PqThriftReader &reader, PqRowGroup &row_group)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      uint8_t elem_type = 0;
      int64_t count = 0;
      if (OB_FAIL(reader.read_list_begin(elem_type, count))) {
      } else if (12 != elem_type) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
          PqColumnMeta meta;
          PqThriftReader chunk_reader(reader.data_, reader.size_, reader.pos_);
          chunk_reader.last_field_id_ = 0;
          if (OB_FAIL(pq_parse_column_chunk(chunk_reader, meta))) {
          } else {
            reader.pos_ = chunk_reader.pos_;
            row_group.columns_.push_back(meta);
          }
        }
      }
    } else if (3 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        row_group.num_rows_ = value;
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_data_page_header(PqThriftReader &reader, PqPageHeader &header)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.num_values_ = static_cast<int32_t>(value);
      }
    } else if (2 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.encoding_ = static_cast<int32_t>(value);
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_dictionary_page_header(PqThriftReader &reader, PqPageHeader &header)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.num_values_ = static_cast<int32_t>(value);
      }
    } else if (2 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.encoding_ = static_cast<int32_t>(value);
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_data_page_header_v2(PqThriftReader &reader, PqPageHeader &header)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.num_values_ = static_cast<int32_t>(value);
      }
    } else if (2 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.num_nulls_ = static_cast<int32_t>(value);
      }
    } else if (4 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.encoding_ = static_cast<int32_t>(value);
      }
    } else if (5 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.def_levels_byte_length_ = static_cast<int32_t>(value);
      }
    } else if (6 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.rep_levels_byte_length_ = static_cast<int32_t>(value);
      }
    } else if (7 == id) {
      header.is_compressed_ = (1 == type);
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_page_header(PqThriftReader &reader, PqPageHeader &header)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (1 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.type_ = static_cast<int32_t>(value);
      }
    } else if (2 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.uncompressed_page_size_ = static_cast<int32_t>(value);
      }
    } else if (3 == id) {
      int64_t value = 0;
      if (OB_FAIL(reader.read_zigzag(value))) {
      } else {
        header.compressed_page_size_ = static_cast<int32_t>(value);
      }
    } else if (5 == id) {
      PqThriftReader sub_reader(reader.data_, reader.size_, reader.pos_);
      sub_reader.last_field_id_ = 0;
      if (OB_FAIL(pq_parse_data_page_header(sub_reader, header))) {
      } else {
        reader.pos_ = sub_reader.pos_;
      }
    } else if (7 == id) {
      PqThriftReader sub_reader(reader.data_, reader.size_, reader.pos_);
      sub_reader.last_field_id_ = 0;
      if (OB_FAIL(pq_parse_dictionary_page_header(sub_reader, header))) {
      } else {
        reader.pos_ = sub_reader.pos_;
      }
    } else if (8 == id) {
      PqThriftReader sub_reader(reader.data_, reader.size_, reader.pos_);
      sub_reader.last_field_id_ = 0;
      if (OB_FAIL(pq_parse_data_page_header_v2(sub_reader, header))) {
      } else {
        reader.pos_ = sub_reader.pos_;
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_parse_file_metadata(PqThriftReader &reader,
                                  std::vector<PqSchemaElement> &schema,
                                  std::vector<PqRowGroup> &row_groups)
{
  int ret = OB_SUCCESS;
  const int16_t saved_field_id = reader.last_field_id_;
  reader.last_field_id_ = 0;
  bool stop = false;
  while (OB_SUCC(ret) && !stop) {
    uint8_t type = 0;
    int16_t id = 0;
    if (OB_FAIL(reader.read_struct_field(type, id, stop))) {
    } else if (stop) {
    } else if (2 == id) {
      uint8_t elem_type = 0;
      int64_t count = 0;
      if (OB_FAIL(reader.read_list_begin(elem_type, count))) {
      } else if (12 != elem_type) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
          PqSchemaElement element;
          PqThriftReader element_reader(reader.data_, reader.size_, reader.pos_);
          element_reader.last_field_id_ = 0;
          if (OB_FAIL(pq_parse_schema_element(element_reader, element))) {
          } else {
            reader.pos_ = element_reader.pos_;
            schema.push_back(element);
          }
        }
      }
    } else if (4 == id) {
      uint8_t elem_type = 0;
      int64_t count = 0;
      if (OB_FAIL(reader.read_list_begin(elem_type, count))) {
      } else if (12 != elem_type) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
          PqRowGroup row_group;
          PqThriftReader group_reader(reader.data_, reader.size_, reader.pos_);
          group_reader.last_field_id_ = 0;
          if (OB_FAIL(pq_parse_row_group(group_reader, row_group))) {
          } else {
            reader.pos_ = group_reader.pos_;
            row_groups.push_back(row_group);
          }
        }
      }
    } else if (OB_FAIL(reader.skip_value(type))) {
    }
  }
  reader.last_field_id_ = saved_field_id;
  return ret;
}

static int pq_snappy_uncompress(const char *src, const int64_t src_len, std::string &out)
{
  int ret = OB_SUCCESS;
  out.clear();
  int64_t pos = 0;
  uint64_t expected = 0;
  int64_t shift = 0;
  bool done = false;
  while (OB_SUCC(ret) && !done) {
    if (pos >= src_len || shift > 63) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const uint8_t byte = static_cast<uint8_t>(src[pos]);
      ++pos;
      expected |= static_cast<uint64_t>(byte & 0x7f) << shift;
      shift += 7;
      done = (0 == (byte & 0x80));
    }
  }
  if (OB_SUCC(ret) && expected > static_cast<uint64_t>(MAX_FILE_SIZE)) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret)) {
    out.reserve(static_cast<size_t>(expected));
    while (OB_SUCC(ret) && pos < src_len && out.size() < expected) {
      const uint8_t tag = static_cast<uint8_t>(src[pos]);
      ++pos;
      const int32_t type = tag & 0x03;
      if (0 == type) {
        int64_t len = static_cast<int64_t>(tag >> 2);
        if (len >= 60) {
          const int64_t extra = len - 59;
          if (pos + extra > src_len) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            uint64_t raw_len = 0;
            for (int64_t i = 0; i < extra; ++i) {
              raw_len |= static_cast<uint64_t>(static_cast<uint8_t>(src[pos + i])) << (8 * i);
            }
            pos += extra;
            len = static_cast<int64_t>(raw_len) + 1;
          }
        } else {
          len += 1;
        }
        if (OB_FAIL(ret)) {
        } else if (pos + len > src_len) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          out.append(src + pos, static_cast<size_t>(len));
          pos += len;
        }
      } else {
        int64_t len = 0;
        int64_t offset = 0;
        if (1 == type) {
          len = ((tag >> 2) & 0x07) + 4;
          if (pos + 1 > src_len) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            offset = (static_cast<int64_t>(tag >> 5) << 8) | static_cast<uint8_t>(src[pos]);
            pos += 1;
          }
        } else if (2 == type) {
          len = static_cast<int64_t>(tag >> 2) + 1;
          if (pos + 2 > src_len) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            offset = static_cast<uint8_t>(src[pos]) |
                     (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 1])) << 8);
            pos += 2;
          }
        } else {
          len = static_cast<int64_t>(tag >> 2) + 1;
          if (pos + 4 > src_len) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            offset = static_cast<uint8_t>(src[pos]) |
                     (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 1])) << 8) |
                     (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 2])) << 16) |
                     (static_cast<int64_t>(static_cast<uint8_t>(src[pos + 3])) << 24);
            pos += 4;
          }
        }
        if (OB_FAIL(ret)) {
        } else if (offset <= 0 || static_cast<int64_t>(out.size()) < offset) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          for (int64_t i = 0; i < len; ++i) {
            out.push_back(out[out.size() - static_cast<size_t>(offset)]);
          }
        }
      }
    }
    if (OB_SUCC(ret) && static_cast<int64_t>(out.size()) != static_cast<int64_t>(expected)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

struct PqRleDecoder
{
  const char *data_;
  int64_t size_;
  int64_t pos_;
  int32_t bit_width_;
  const char *packed_ptr_;
  int64_t packed_index_;
  int64_t packed_remaining_;
  int64_t rle_remaining_;
  int32_t rle_value_;
  PqRleDecoder(const char *data, const int64_t size, const int32_t bit_width)
    : data_(data), size_(size), pos_(0), bit_width_(bit_width), packed_ptr_(nullptr),
      packed_index_(0), packed_remaining_(0), rle_remaining_(0), rle_value_(0) {}
  int read_run_header()
  {
    int ret = OB_SUCCESS;
    uint64_t header = 0;
    int64_t shift = 0;
    bool done = false;
    while (OB_SUCC(ret) && !done) {
      if (pos_ >= size_ || shift > 63) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const uint8_t byte = static_cast<uint8_t>(data_[pos_]);
        ++pos_;
        header |= static_cast<uint64_t>(byte & 0x7f) << shift;
        shift += 7;
        done = (0 == (byte & 0x80));
      }
    }
    if (OB_SUCC(ret)) {
      if (header & 1) {
        const int64_t groups = static_cast<int64_t>(header >> 1);
        const int64_t bytes = groups * bit_width_;
        if (groups <= 0 || bytes < 0 || pos_ + bytes > size_) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          packed_ptr_ = data_ + pos_;
          pos_ += bytes;
          packed_index_ = 0;
          packed_remaining_ = groups * 8;
        }
      } else {
        const int64_t run_len = static_cast<int64_t>(header >> 1);
        const int64_t bytes = (bit_width_ + 7) / 8;
        if (run_len <= 0 || pos_ + bytes > size_) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          uint32_t raw = 0;
          for (int64_t i = 0; i < bytes; ++i) {
            raw |= static_cast<uint32_t>(static_cast<uint8_t>(data_[pos_ + i])) << (8 * i);
          }
          pos_ += bytes;
          rle_value_ = static_cast<int32_t>(raw);
          rle_remaining_ = run_len;
        }
      }
    }
    return ret;
  }
  int next_value(int32_t &value)
  {
    int ret = OB_SUCCESS;
    bool ready = false;
    while (OB_SUCC(ret) && !ready) {
      if (rle_remaining_ > 0) {
        value = rle_value_;
        --rle_remaining_;
        ready = true;
      } else if (packed_remaining_ > 0) {
        const int64_t bit_pos = packed_index_ * bit_width_;
        const int64_t byte_pos = bit_pos / 8;
        const int32_t bit_off = static_cast<int32_t>(bit_pos % 8);
        const int64_t need_bytes = (bit_width_ + bit_off + 7) / 8;
        uint64_t raw = 0;
        for (int64_t i = 0; i < need_bytes; ++i) {
          raw |= static_cast<uint64_t>(static_cast<uint8_t>(packed_ptr_[byte_pos + i])) << (8 * i);
        }
        const uint64_t mask = (bit_width_ >= 32) ? 0xffffffffULL : ((1ULL << bit_width_) - 1);
        value = static_cast<int32_t>((raw >> bit_off) & mask);
        ++packed_index_;
        --packed_remaining_;
        ready = true;
      } else if (OB_FAIL(read_run_header())) {
      }
    }
    return ret;
  }
};

static int64_t pq_floor_div(const int64_t value, const int64_t divisor)
{
  int64_t quotient = value / divisor;
  if (0 != value % divisor && ((value < 0) != (divisor < 0))) {
    --quotient;
  }
  return quotient;
}

static void pq_format_date_days(const int64_t days, std::string &out)
{
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t year = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t day = doy - (153 * mp + 2) / 5 + 1;
  int64_t month = mp + (mp < 10 ? 3 : -9);
  if (month <= 2) {
    ++year;
  }
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lld",
           static_cast<long long>(year),
           static_cast<long long>(month),
           static_cast<long long>(day));
  out.assign(buffer);
}

static void pq_format_timestamp_value(const int64_t raw,
                                      const int32_t converted_type,
                                      std::string &out)
{
  const bool is_millis = (PQ_CONVERTED_TIMESTAMP_MILLIS == converted_type);
  const int64_t units_per_second = is_millis ? 1000 : 1000000;
  const int32_t digits = is_millis ? 3 : 6;
  const int64_t seconds = pq_floor_div(raw, units_per_second);
  const int32_t fraction = static_cast<int32_t>(raw - seconds * units_per_second);
  const int64_t days = pq_floor_div(seconds, 86400);
  const int64_t second_of_day = seconds - days * 86400;
  std::string date;
  pq_format_date_days(days, date);
  char buffer[48];
  const int hour = static_cast<int>(second_of_day / 3600);
  const int minute = static_cast<int>((second_of_day % 3600) / 60);
  const int second = static_cast<int>(second_of_day % 60);
  if (fraction > 0) {
    snprintf(buffer, sizeof(buffer), " %02d:%02d:%02d.%0*d",
             hour, minute, second, static_cast<int>(digits), static_cast<int>(fraction));
  } else {
    snprintf(buffer, sizeof(buffer), " %02d:%02d:%02d", hour, minute, second);
  }
  out = date;
  out.append(buffer);
}

static int32_t pq_read_int32(const char *ptr)
{
  const uint32_t raw = static_cast<uint32_t>(static_cast<uint8_t>(ptr[0])) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(ptr[1])) << 8) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(ptr[2])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(ptr[3])) << 24);
  return static_cast<int32_t>(raw);
}

static int64_t pq_read_int64(const char *ptr)
{
  const uint64_t raw = static_cast<uint64_t>(static_cast<uint8_t>(ptr[0])) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[1])) << 8) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[2])) << 16) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[3])) << 24) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[4])) << 32) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[5])) << 40) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[6])) << 48) |
                       (static_cast<uint64_t>(static_cast<uint8_t>(ptr[7])) << 56);
  return static_cast<int64_t>(raw);
}

static int pq_decode_plain(const PqSchemaElement &element,
                           const char *data,
                           const int64_t size,
                           int64_t &pos,
                           const int64_t count,
                           std::vector<std::string> &out)
{
  int ret = OB_SUCCESS;
  char buffer[64];
  if (count < 0 || pos < 0 || pos > size) {
    ret = OB_INVALID_ARGUMENT;
  } else if (PQ_PHYS_BOOLEAN == element.physical_type_) {
    const int64_t bytes = (count + 7) / 8;
    if (pos + bytes > size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        const uint8_t byte = static_cast<uint8_t>(data[pos + i / 8]);
        const bool bit = (0 != ((byte >> (i % 8)) & 0x01));
        out.push_back(bit ? "true" : "false");
      }
      pos += bytes;
    }
  } else if (PQ_PHYS_INT32 == element.physical_type_) {
    if (pos + count * 4 > size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        const int32_t value = pq_read_int32(data + pos);
        pos += 4;
        if (PQ_CONVERTED_DATE == element.converted_type_) {
          std::string text;
          pq_format_date_days(value, text);
          out.push_back(text);
        } else {
          snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
          out.push_back(buffer);
        }
      }
    }
  } else if (PQ_PHYS_INT64 == element.physical_type_) {
    if (pos + count * 8 > size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        const int64_t value = pq_read_int64(data + pos);
        pos += 8;
        if (PQ_CONVERTED_TIMESTAMP_MILLIS == element.converted_type_ ||
            PQ_CONVERTED_TIMESTAMP_MICROS == element.converted_type_) {
          std::string text;
          pq_format_timestamp_value(value, element.converted_type_, text);
          out.push_back(text);
        } else {
          snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
          out.push_back(buffer);
        }
      }
    }
  } else if (PQ_PHYS_FLOAT == element.physical_type_) {
    if (pos + count * 4 > size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        float value = 0.0f;
        MEMCPY(&value, data + pos, 4);
        pos += 4;
        snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
        out.push_back(buffer);
      }
    }
  } else if (PQ_PHYS_DOUBLE == element.physical_type_) {
    if (pos + count * 8 > size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        double value = 0.0;
        MEMCPY(&value, data + pos, 8);
        pos += 8;
        snprintf(buffer, sizeof(buffer), "%.17g", value);
        out.push_back(buffer);
      }
    }
  } else if (PQ_PHYS_BYTE_ARRAY == element.physical_type_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      if (pos + 4 > size) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const int32_t len = pq_read_int32(data + pos);
        pos += 4;
        if (len < 0 || pos + len > size) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          out.push_back(std::string(data + pos, static_cast<size_t>(len)));
          pos += len;
        }
      }
    }
  } else if (PQ_PHYS_FLBA == element.physical_type_) {
    if (element.type_length_ <= 0 || pos + count * element.type_length_ > size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < count; ++i) {
        out.push_back(std::string(data + pos, static_cast<size_t>(element.type_length_)));
        pos += element.type_length_;
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_parquet, unsupported physical type");
  }
  return ret;
}

static int pq_decode_values(const PqSchemaElement &element,
                            const std::vector<std::string> &dictionary,
                            const bool has_dictionary,
                            const int32_t encoding,
                            const char *data,
                            const int64_t size,
                            const int64_t count,
                            std::vector<std::string> &out)
{
  int ret = OB_SUCCESS;
  if (0 == count) {
  } else if (count < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (PQ_ENC_PLAIN == encoding) {
    int64_t pos = 0;
    ret = pq_decode_plain(element, data, size, pos, count, out);
  } else if (PQ_ENC_PLAIN_DICTIONARY == encoding || PQ_ENC_RLE_DICTIONARY == encoding) {
    if (!has_dictionary || size < 1) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const int32_t bit_width = static_cast<uint8_t>(data[0]);
      if (bit_width > 32) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        PqRleDecoder decoder(data + 1, size - 1, bit_width);
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
          int32_t index = 0;
          if (OB_FAIL(decoder.next_value(index))) {
          } else if (index < 0 || static_cast<size_t>(index) >= dictionary.size()) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            out.push_back(dictionary[static_cast<size_t>(index)]);
          }
        }
      }
    }
  } else if (PQ_ENC_RLE == encoding && PQ_PHYS_BOOLEAN == element.physical_type_) {
    PqRleDecoder decoder(data, size, 1);
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      int32_t value = 0;
      if (OB_FAIL(decoder.next_value(value))) {
      } else {
        out.push_back(0 != value ? "true" : "false");
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_parquet, unsupported value encoding");
  }
  return ret;
}

static int pq_decompress(const int32_t codec,
                         const char *src,
                         const int64_t src_len,
                         std::string &storage,
                         const char *&out,
                         int64_t &out_len)
{
  int ret = OB_SUCCESS;
  storage.clear();
  out = src;
  out_len = src_len;
  if (PQ_CODEC_UNCOMPRESSED == codec) {
  } else if (PQ_CODEC_SNAPPY == codec) {
    if (OB_FAIL(pq_snappy_uncompress(src, src_len, storage))) {
    } else {
      out = storage.data();
      out_len = static_cast<int64_t>(storage.size());
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_parquet, unsupported compression codec");
  }
  return ret;
}

static int pq_decode_column_chunk(const char *file,
                                  const int64_t file_size,
                                  const PqSchemaElement &element,
                                  const PqColumnMeta &meta,
                                  const int64_t num_rows,
                                  std::vector<std::string> &texts,
                                  std::vector<bool> &nulls)
{
  int ret = OB_SUCCESS;
  texts.assign(static_cast<size_t>(num_rows), std::string());
  nulls.assign(static_cast<size_t>(num_rows), true);
  int64_t chunk_start = meta.data_page_offset_;
  if (meta.dictionary_page_offset_ > 0 &&
      (chunk_start <= 0 || meta.dictionary_page_offset_ < chunk_start)) {
    chunk_start = meta.dictionary_page_offset_;
  }
  const int64_t chunk_end = chunk_start + meta.total_compressed_size_;
  if (chunk_start <= 0 || meta.total_compressed_size_ <= 0 || chunk_end > file_size) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int32_t max_definition = (PQ_REP_REQUIRED == element.repetition_) ? 0 : 1;
    int64_t filled = 0;
    std::vector<std::string> dictionary;
    bool has_dictionary = false;
    PqThriftReader reader(file, chunk_end, chunk_start);
    while (OB_SUCC(ret) && filled < num_rows && reader.pos_ < chunk_end) {
      PqPageHeader header;
      if (OB_FAIL(pq_parse_page_header(reader, header))) {
      } else if (header.compressed_page_size_ < 0 ||
                 reader.pos_ + header.compressed_page_size_ > chunk_end) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const int64_t body_pos = reader.pos_;
        const char *body = file + body_pos;
        const int64_t body_size = header.compressed_page_size_;
        if (PQ_PAGE_DICTIONARY == header.type_) {
          std::string storage;
          const char *raw = nullptr;
          int64_t raw_size = 0;
          if (OB_FAIL(pq_decompress(meta.codec_, body, body_size, storage, raw, raw_size))) {
          } else {
            dictionary.clear();
            int64_t page_pos = 0;
            if (OB_FAIL(pq_decode_plain(element, raw, raw_size, page_pos,
                                        header.num_values_, dictionary))) {
            } else {
              has_dictionary = true;
            }
          }
        } else if (PQ_PAGE_DATA_V1 == header.type_) {
          std::string storage;
          const char *raw = nullptr;
          int64_t raw_size = 0;
          if (OB_FAIL(pq_decompress(meta.codec_, body, body_size, storage, raw, raw_size))) {
          } else {
            int64_t page_pos = 0;
            std::vector<int32_t> def_levels;
            def_levels.reserve(static_cast<size_t>(header.num_values_));
            if (max_definition > 0) {
              if (page_pos + 4 > raw_size) {
                ret = OB_INVALID_ARGUMENT;
              } else {
                const int32_t level_bytes = pq_read_int32(raw + page_pos);
                page_pos += 4;
                if (level_bytes < 0 || page_pos + level_bytes > raw_size) {
                  ret = OB_INVALID_ARGUMENT;
                } else {
                  PqRleDecoder decoder(raw + page_pos, level_bytes, 1);
                  for (int64_t i = 0; OB_SUCC(ret) && i < header.num_values_; ++i) {
                    int32_t level = 0;
                    if (OB_FAIL(decoder.next_value(level))) {
                    } else {
                      def_levels.push_back(level);
                    }
                  }
                  page_pos += level_bytes;
                }
              }
            }
            if (OB_SUCC(ret)) {
              int64_t non_null = 0;
              for (int64_t i = 0; i < header.num_values_; ++i) {
                if (0 == max_definition ||
                    (static_cast<size_t>(i) < def_levels.size() &&
                     max_definition == def_levels[static_cast<size_t>(i)])) {
                  ++non_null;
                }
              }
              std::vector<std::string> values;
              values.reserve(static_cast<size_t>(non_null));
              if (OB_FAIL(pq_decode_values(element, dictionary, has_dictionary,
                                           header.encoding_,
                                           raw + page_pos, raw_size - page_pos,
                                           non_null, values))) {
              } else if (static_cast<int64_t>(values.size()) != non_null) {
                ret = OB_INVALID_ARGUMENT;
              } else {
                int64_t value_index = 0;
                for (int64_t i = 0; OB_SUCC(ret) && i < header.num_values_ && filled < num_rows; ++i) {
                  const bool is_null = (max_definition > 0 &&
                      (static_cast<size_t>(i) >= def_levels.size() ||
                       max_definition != def_levels[static_cast<size_t>(i)]));
                  if (is_null) {
                    texts[static_cast<size_t>(filled)].clear();
                    nulls[static_cast<size_t>(filled)] = true;
                  } else {
                    texts[static_cast<size_t>(filled)].swap(values[static_cast<size_t>(value_index)]);
                    nulls[static_cast<size_t>(filled)] = false;
                    ++value_index;
                  }
                  ++filled;
                }
              }
            }
          }
        } else if (PQ_PAGE_DATA_V2 == header.type_) {
          const int64_t rep_len = header.rep_levels_byte_length_;
          const int64_t def_len = header.def_levels_byte_length_;
          if (rep_len < 0 || def_len < 0 || rep_len + def_len > body_size) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            std::vector<int32_t> def_levels;
            def_levels.reserve(static_cast<size_t>(header.num_values_));
            if (max_definition > 0) {
              PqRleDecoder decoder(body + rep_len, def_len, 1);
              for (int64_t i = 0; OB_SUCC(ret) && i < header.num_values_; ++i) {
                int32_t level = 0;
                if (OB_FAIL(decoder.next_value(level))) {
                } else {
                  def_levels.push_back(level);
                }
              }
            }
            if (OB_SUCC(ret)) {
              const char *values_ptr = body + rep_len + def_len;
              int64_t values_size = body_size - rep_len - def_len;
              std::string storage;
              if (header.is_compressed_ && PQ_CODEC_UNCOMPRESSED != meta.codec_) {
                const char *raw = nullptr;
                int64_t raw_size = 0;
                if (OB_FAIL(pq_decompress(meta.codec_, values_ptr, values_size,
                                          storage, raw, raw_size))) {
                } else {
                  values_ptr = raw;
                  values_size = raw_size;
                }
              }
              if (OB_SUCC(ret)) {
                const int64_t non_null = static_cast<int64_t>(header.num_values_) - header.num_nulls_;
                std::vector<std::string> values;
                if (non_null < 0) {
                  ret = OB_INVALID_ARGUMENT;
                } else if (OB_FAIL(pq_decode_values(element, dictionary, has_dictionary,
                                                    header.encoding_, values_ptr, values_size,
                                                    non_null, values))) {
                } else if (static_cast<int64_t>(values.size()) != non_null) {
                  ret = OB_INVALID_ARGUMENT;
                } else {
                  int64_t value_index = 0;
                  for (int64_t i = 0; OB_SUCC(ret) && i < header.num_values_ && filled < num_rows; ++i) {
                    const bool is_null = (max_definition > 0 &&
                        (static_cast<size_t>(i) >= def_levels.size() ||
                         max_definition != def_levels[static_cast<size_t>(i)]));
                    if (is_null) {
                      texts[static_cast<size_t>(filled)].clear();
                      nulls[static_cast<size_t>(filled)] = true;
                    } else {
                      texts[static_cast<size_t>(filled)].swap(values[static_cast<size_t>(value_index)]);
                      nulls[static_cast<size_t>(filled)] = false;
                      ++value_index;
                    }
                    ++filled;
                  }
                }
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          reader.pos_ = body_pos + body_size;
        }
      }
    }
    if (OB_SUCC(ret) && filled != num_rows) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

} // namespace

static bool pq_map_file_type(const PqSchemaElement &element, FileSqlType &type)
{
  bool supported = true;
  switch (element.physical_type_) {
    case PQ_PHYS_BOOLEAN:
      type = FILE_SQL_BOOLEAN;
      break;
    case PQ_PHYS_INT32:
      type = (PQ_CONVERTED_DATE == element.converted_type_) ? FILE_SQL_DATE : FILE_SQL_BIGINT;
      break;
    case PQ_PHYS_INT64:
      type = ((PQ_CONVERTED_TIMESTAMP_MILLIS == element.converted_type_ ||
               PQ_CONVERTED_TIMESTAMP_MICROS == element.converted_type_))
             ? FILE_SQL_DATETIME : FILE_SQL_BIGINT;
      break;
    case PQ_PHYS_FLOAT:
    case PQ_PHYS_DOUBLE:
      type = FILE_SQL_DOUBLE;
      break;
    case PQ_PHYS_BYTE_ARRAY:
    case PQ_PHYS_FLBA:
      if (PQ_CONVERTED_DATE == element.converted_type_) {
        type = FILE_SQL_DATE;
      } else if (PQ_CONVERTED_TIMESTAMP_MILLIS == element.converted_type_ ||
                 PQ_CONVERTED_TIMESTAMP_MICROS == element.converted_type_) {
        type = FILE_SQL_DATETIME;
      } else {
        type = FILE_SQL_VARCHAR;
      }
      break;
    default:
      supported = false;
      break;
  }
  return supported;
}

static int read_parquet_table(const ObString &path,
                              const ObSQLSessionInfo *session_info,
                              ObIAllocator &allocator,
                              FileSqlTable &table)
{
  int ret = OB_SUCCESS;
  ObString content;
  if (OB_FAIL(read_whole_file(path, allocator, content))) {
    LOG_WARN("failed to read parquet file", K(ret), K(path));
  } else if (content.length() < 12 ||
             0 != MEMCMP(content.ptr(), "PAR1", 4) ||
             0 != MEMCMP(content.ptr() + content.length() - 4, "PAR1", 4)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_parquet, invalid parquet file");
  } else {
    const char *file = content.ptr();
    const int64_t file_size = content.length();
    const int64_t footer_size =
        static_cast<int64_t>(static_cast<uint32_t>(pq_read_int32(file + file_size - 8)));
    const int64_t footer_pos = file_size - 8 - footer_size;
    if (footer_size <= 0 || footer_pos < 4) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_parquet, invalid footer");
    } else {
      PqThriftReader reader(file, file_size - 8, footer_pos);
      std::vector<PqSchemaElement> schema;
      std::vector<PqRowGroup> row_groups;
      if (OB_FAIL(pq_parse_file_metadata(reader, schema, row_groups))) {
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_parquet, corrupt metadata");
      } else if (schema.size() < 2) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const int64_t column_cnt = static_cast<int64_t>(schema.size()) - 1;
        bool flat = true;
        for (int64_t i = 1; flat && i < static_cast<int64_t>(schema.size()); ++i) {
          if (schema[i].num_children_ > 0 || schema[i].physical_type_ < 0) {
            flat = false;
          }
        }
        if (!flat) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_parquet, nested schema");
        }
        for (int64_t i = 1; OB_SUCC(ret) && i < static_cast<int64_t>(schema.size()); ++i) {
          FileSqlType column_type = FILE_SQL_VARCHAR;
          if (!pq_map_file_type(schema[i], column_type)) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_parquet, unsupported column type");
          } else {
            FileSqlColumn column;
            ObString column_name;
            if (schema[i].name_.empty()) {
              char name_buffer[64];
              snprintf(name_buffer, sizeof(name_buffer), "column_%lld",
                       static_cast<long long>(i));
              column_name.assign_ptr(name_buffer, static_cast<int32_t>(strlen(name_buffer)));
            } else {
              column_name.assign_ptr(schema[i].name_.data(),
                                     static_cast<int32_t>(schema[i].name_.size()));
            }
            column.type_ = column_type;
            column.length_ = 1;
            column.nullable_ = (PQ_REP_REQUIRED != schema[i].repetition_);
            if (OB_FAIL(copy_string(column_name, allocator, column.name_))) {
            } else if (OB_FAIL(table.columns_.push_back(column))) {
            }
          }
        }
        if (OB_SUCC(ret)) {
          table.column_cnt_ = table.columns_.count();
          for (int64_t g = 0; OB_SUCC(ret) && g < static_cast<int64_t>(row_groups.size()); ++g) {
            const PqRowGroup &group = row_groups[static_cast<size_t>(g)];
            if (group.num_rows_ <= 0) {
              continue;
            }
            if (static_cast<int64_t>(group.columns_.size()) != column_cnt) {
              ret = OB_INVALID_ARGUMENT;
              break;
            }
            std::vector<std::vector<std::string> > column_texts(static_cast<size_t>(column_cnt));
            std::vector<std::vector<bool> > column_nulls(static_cast<size_t>(column_cnt));
            for (int64_t c = 0; OB_SUCC(ret) && c < column_cnt; ++c) {
              if (OB_FAIL(pq_decode_column_chunk(file, file_size, schema[static_cast<size_t>(c + 1)],
                                                 group.columns_[static_cast<size_t>(c)],
                                                 group.num_rows_,
                                                 column_texts[static_cast<size_t>(c)],
                                                 column_nulls[static_cast<size_t>(c)]))) {
              }
            }
            for (int64_t r = 0; OB_SUCC(ret) && r < group.num_rows_; ++r) {
              for (int64_t c = 0; OB_SUCC(ret) && c < column_cnt; ++c) {
                const std::string &text = column_texts[static_cast<size_t>(c)][static_cast<size_t>(r)];
                ObObjParam value;
                if (OB_FAIL(build_value(table.columns_.at(c).type_,
                                        ObString(static_cast<int32_t>(text.size()), text.data()),
                                        column_nulls[static_cast<size_t>(c)][static_cast<size_t>(r)],
                                        session_info, value))) {
                } else if (OB_FAIL(table.values_.push_back(value))) {
                }
              }
              if (OB_SUCC(ret)) {
                ++table.row_cnt_;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int read_file_table(const FileSqlFormat format,
                    const ObString &path,
                    const FileSqlOptions &options,
                    const ObSQLSessionInfo *session_info,
                    ObIAllocator &allocator,
                    FileSqlTable &table)
{
  int ret = OB_SUCCESS;
  if (path.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "file table, empty file path");
  } else if (FILE_SQL_GLOB == format) {
    ret = glob_files(path, allocator, table);
  } else if (FILE_SQL_PARQUET == format) {
    ret = read_parquet_table(path, session_info, allocator, table);
  } else {
    ObString content;
    if (OB_FAIL(read_whole_file(path, allocator, content))) {
      LOG_WARN("failed to read file", K(ret), K(path));
    } else if (FILE_SQL_CSV == format) {
      ObSEArray<CsvRow, 32> rows;
      if (OB_FAIL(parse_csv_content(content, options, allocator, rows))) {
      } else if (OB_FAIL(infer_csv_schema(rows, options.header_, session_info,
                                          allocator, table.columns_))) {
      } else if (OB_FAIL(fill_csv_values(rows, options.header_, session_info, table))) {
      }
    } else {
      ObSEArray<JsonRow, 32> rows;
      int64_t pos = 0;
      int64_t line = 1;
      bool is_array = false;
      if (1 == options.json_format_) {
        is_array = true;
      } else if (2 == options.json_format_) {
        is_array = false;
      } else {
        skip_json_space(content.ptr(), content.length(), pos, line);
        is_array = (pos < content.length() && '[' == content.ptr()[pos]);
      }
      if (FILE_SQL_JSON == format && is_array) {
        if (OB_FAIL(parse_json_array_rows(content, allocator, rows))) {
        }
      } else if (OB_FAIL(parse_ndjson_rows(content, allocator, rows))) {
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(build_json_columns(rows, allocator, table))) {
      } else if (OB_FAIL(fill_json_row_values(rows, table))) {
      }
    }
  }
  return ret;
}

int glob_files(const ObString &pattern_ob, ObIAllocator &allocator, FileSqlTable &table)
{
  int ret = OB_SUCCESS;
  std::string pattern(pattern_ob.ptr(), pattern_ob.length());
  std::vector<std::string> files;
  size_t slash_pos = pattern.find_last_of('/');
  std::string dir = (std::string::npos == slash_pos) ? "." : pattern.substr(0, slash_pos);
  std::string name_pattern = (std::string::npos == slash_pos)
                             ? pattern : pattern.substr(slash_pos + 1);
  if (dir.empty()) {
    dir = "/";
  }
  if (name_pattern.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "glob, invalid pattern");
  } else if (!is_wildcard(name_pattern.data(), static_cast<int64_t>(name_pattern.size()))) {
    if (path_exists(pattern) && !path_is_dir(pattern)) {
      files.push_back(pattern);
    }
  } else {
    std::vector<std::string> names;
    std::vector<bool> is_dir;
    if (OB_FAIL(list_dir(dir, names, is_dir))) {
    } else {
      for (size_t i = 0; i < names.size(); ++i) {
        if (!is_dir[i]
            && wildcard_match(name_pattern.data(), static_cast<int64_t>(name_pattern.size()),
                              names[i].data(), static_cast<int64_t>(names[i].size()))) {
          if ("." == dir) {
            files.push_back(names[i]);
          } else if ("/" == dir) {
            files.push_back("/" + names[i]);
          } else {
            files.push_back(dir + "/" + names[i]);
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    std::sort(files.begin(), files.end());
    FileSqlColumn column;
    column.type_ = FILE_SQL_VARCHAR;
    column.length_ = 1;
    column.nullable_ = false;
    ObString column_name;
    column_name.assign_ptr("file", 4);
    int64_t max_len = 4;
    for (size_t i = 0; i < files.size(); ++i) {
      if (static_cast<int64_t>(files[i].size()) > max_len) {
        max_len = static_cast<int64_t>(files[i].size());
      }
    }
    column.length_ = max_len;
    if (OB_FAIL(copy_string(column_name, allocator, column.name_))) {
    } else if (OB_FAIL(table.columns_.push_back(column))) {
    } else {
      for (size_t i = 0; OB_SUCC(ret) && i < files.size(); ++i) {
        ObString file_name;
        ObString file_path(static_cast<int32_t>(files[i].size()), files[i].data());
        ObObjParam value;
        if (OB_FAIL(copy_string(file_path, allocator, file_name))) {
        } else if (OB_FAIL(build_value(FILE_SQL_VARCHAR, file_name, false, nullptr, value))) {
        } else if (OB_FAIL(table.values_.push_back(value))) {
        } else {
          ++table.row_cnt_;
        }
      }
      table.column_cnt_ = table.columns_.count();
    }
  }
  return ret;
}

const char *get_type_name(const FileSqlType type)
{
  const char *name = "VARCHAR";
  switch (type) {
    case FILE_SQL_BIGINT: name = "BIGINT"; break;
    case FILE_SQL_DOUBLE: name = "DOUBLE"; break;
    case FILE_SQL_BOOLEAN: name = "BOOLEAN"; break;
    case FILE_SQL_VARCHAR: name = "VARCHAR"; break;
    case FILE_SQL_DATE: name = "DATE"; break;
    case FILE_SQL_DATETIME: name = "DATETIME"; break;
    case FILE_SQL_NULL: name = "NULL"; break;
    default: break;
  }
  return name;
}

} // namespace file_sql
} // namespace sql
} // namespace oceanbase




