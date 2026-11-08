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
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "read_parquet");
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
      skip_json_space(content.ptr(), content.length(), pos, line);
      is_array = (pos < content.length() && '[' == content.ptr()[pos]);
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




