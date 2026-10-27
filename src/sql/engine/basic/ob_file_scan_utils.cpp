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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_scan_utils.h"

#include <cerrno>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#define ob_stat _stat
#define ob_stat_t struct _stat
#else
#include <unistd.h>
#include <limits.h>
#include <glob.h>
#define ob_stat stat
#define ob_stat_t struct stat
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace sql
{
namespace
{

constexpr int64_t FILE_SCAN_READ_BUF_SIZE = 64 * 1024;
// A single CSV record must fit into this limit so that a malformed file cannot
// exhaust the server memory.
constexpr int64_t FILE_SCAN_MAX_ROW_BYTES = 16 * 1024 * 1024;
constexpr int64_t FILE_SCAN_MAX_COLUMNS = 4096;

inline bool is_digit(const char ch) { return ch >= '0' && ch <= '9'; }

inline char lower_char(const char ch)
{
  return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool ob_str_case_equal(const ObString &str, const char *text)
{
  const int64_t len = static_cast<int64_t>(strlen(text));
  bool equal = (str.length() == len);
  for (int64_t i = 0; equal && i < len; ++i) {
    equal = lower_char(str.ptr()[i]) == lower_char(text[i]);
  }
  return equal;
}

// Days since 1970-01-01 in the proleptic Gregorian calendar.
int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
  y -= (m <= 2) ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool is_leap_year(const int64_t year)
{
  return (0 == year % 4 && 0 != year % 100) || 0 == year % 400;
}

int days_in_month(const int64_t year, const int64_t month)
{
  static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = 0;
  if (month >= 1 && month <= 12) {
    days = DAYS[month - 1];
    if (2 == month && is_leap_year(year)) {
      ++days;
    }
  }
  return days;
}

bool valid_date_parts(const int64_t year, const int64_t month, const int64_t day)
{
  return year >= 0 && year <= 9999 && month >= 1 && month <= 12 && day >= 1
      && day <= days_in_month(year, month);
}

bool path_prefix_match(const char *dir, const char *path)
{
  bool match = false;
  if (OB_NOT_NULL(dir) && OB_NOT_NULL(path)) {
    const size_t dir_len = strlen(dir);
    if (dir_len > 0 && 0 == strncmp(dir, path, dir_len)) {
      match = (dir_len == strlen(path)) || ('/' == dir[dir_len - 1]) || ('/' == path[dir_len]);
    }
  }
  return match;
}

} // namespace

// ---------------------------------------------------------------- path helpers

int FileScanUtils::resolve_and_check_path(const ObString &secure_file_priv,
                                          const ObString &path,
                                          ObIAllocator &allocator,
                                          ObString &resolved_path)
{
  int ret = OB_SUCCESS;
  char *path_cstr = nullptr;
  char *priv_cstr = nullptr;
  char buf[PATH_MAX + 1];
  char *priv_real = nullptr;
  if (secure_file_priv.empty()) {
    // the feature must be configured explicitly, an empty value never means
    // "no restriction"
    ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
    LOG_WARN("secure_file_priv is empty, file access denied", K(ret));
  } else if (path.empty() || '/' != path.ptr()[0]) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file path must be an absolute path", K(ret), K(path));
  } else if (OB_ISNULL(path_cstr = static_cast<char *>(allocator.alloc(path.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(priv_cstr = static_cast<char *>(allocator.alloc(secure_file_priv.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(path_cstr, path.ptr(), path.length());
    path_cstr[path.length()] = '\0';
    MEMCPY(priv_cstr, secure_file_priv.ptr(), secure_file_priv.length());
    priv_cstr[secure_file_priv.length()] = '\0';
    if (OB_ISNULL(::realpath(path_cstr, buf))) {
      ret = (ENOENT == errno) ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
      LOG_WARN("failed to resolve file path", K(ret), K(errno), K(path));
    } else if (OB_ISNULL(priv_real = ::realpath(priv_cstr, nullptr))) {
      ret = (ENOENT == errno) ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
      LOG_WARN("failed to resolve secure_file_priv", K(ret), K(errno), K(secure_file_priv));
    } else if (!path_prefix_match(priv_real, buf)) {
      ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
      LOG_WARN("file is out of secure_file_priv", K(ret), K(path), K(secure_file_priv));
    } else {
      char *copy = static_cast<char *>(allocator.alloc(strlen(buf) + 1));
      if (OB_ISNULL(copy)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(copy, buf, strlen(buf) + 1);
        resolved_path.assign_ptr(copy, static_cast<int32_t>(strlen(copy)));
      }
    }
  }
  if (OB_NOT_NULL(priv_real)) {
    ::free(priv_real);
  }
  return ret;
}

int FileScanUtils::resolve_and_check_parent_path(const ObString &secure_file_priv,
                                                 const ObString &path,
                                                 ObIAllocator &allocator,
                                                 ObString &resolved_path)
{
  int ret = OB_SUCCESS;
  char *path_cstr = nullptr;
  char *priv_cstr = nullptr;
  char dir_buf[PATH_MAX + 1];
  char parent_real[PATH_MAX + 1];
  char *priv_real = nullptr;
  const char *slash = nullptr;
  if (secure_file_priv.empty()) {
    ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
    LOG_WARN("secure_file_priv is empty, file access denied", K(ret));
  } else if (path.empty() || '/' != path.ptr()[0]) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file path must be an absolute path", K(ret), K(path));
  } else if (OB_ISNULL(path_cstr = static_cast<char *>(allocator.alloc(path.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(priv_cstr = static_cast<char *>(allocator.alloc(secure_file_priv.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(path_cstr, path.ptr(), path.length());
    path_cstr[path.length()] = '\0';
    MEMCPY(priv_cstr, secure_file_priv.ptr(), secure_file_priv.length());
    priv_cstr[secure_file_priv.length()] = '\0';
    slash = strrchr(path_cstr, '/');
    if (OB_ISNULL(slash) || slash == path_cstr) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid target path", K(ret), K(path));
    } else {
      const size_t dir_len = static_cast<size_t>(slash - path_cstr);
      MEMCPY(dir_buf, path_cstr, dir_len);
      dir_buf[dir_len] = '\0';
      if (OB_ISNULL(::realpath(dir_buf, parent_real))) {
        ret = (ENOENT == errno) ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
        LOG_WARN("failed to resolve target directory", K(ret), K(errno), K(path));
      } else if (OB_ISNULL(priv_real = ::realpath(priv_cstr, nullptr))) {
        ret = (ENOENT == errno) ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
      } else if (!path_prefix_match(priv_real, parent_real)) {
        ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
        LOG_WARN("target is out of secure_file_priv", K(ret), K(path));
      } else {
        const int64_t total = static_cast<int64_t>(strlen(parent_real)) + 1
                              + static_cast<int64_t>(strlen(slash + 1)) + 1;
        char *copy = static_cast<char *>(allocator.alloc(total));
        if (OB_ISNULL(copy)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          const int64_t written = snprintf(copy, total, "%s/%s", parent_real, slash + 1);
          resolved_path.assign_ptr(copy, static_cast<int32_t>(written));
        }
      }
    }
  }
  if (OB_NOT_NULL(priv_real)) {
    ::free(priv_real);
  }
  return ret;
}

int FileScanUtils::glob_files(ObIAllocator &allocator,
                              const ObString &pattern,
                              ObIArray<ObString> &files)
{
  int ret = OB_SUCCESS;
#if defined(_WIN32)
  UNUSED(allocator);
  UNUSED(pattern);
  UNUSED(files);
  ret = OB_NOT_SUPPORTED;
#else
  char *pattern_cstr = nullptr;
  if (OB_ISNULL(pattern_cstr = static_cast<char *>(allocator.alloc(pattern.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(pattern_cstr, pattern.ptr(), pattern.length());
    pattern_cstr[pattern.length()] = '\0';
    glob_t glob_buf;
    MEMSET(&glob_buf, 0, sizeof(glob_buf));
    const int glob_ret = ::glob(pattern_cstr, GLOB_NOSORT, nullptr, &glob_buf);
    if (0 != glob_ret && GLOB_NOMATCH != glob_ret) {
      ret = OB_IO_ERROR;
      LOG_WARN("glob failed", K(ret), K(glob_ret), K(pattern));
    } else if (0 == glob_ret) {
      ObSEArray<ObString, 16> matched;
      for (size_t i = 0; OB_SUCC(ret) && i < glob_buf.gl_pathc; ++i) {
        ob_stat_t st;
        MEMSET(&st, 0, sizeof(st));
        if (0 == ob_stat(glob_buf.gl_pathv[i], &st) && S_ISREG(st.st_mode)) {
          const size_t len = strlen(glob_buf.gl_pathv[i]);
          char *copy = static_cast<char *>(allocator.alloc(len + 1));
          if (OB_ISNULL(copy)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            MEMCPY(copy, glob_buf.gl_pathv[i], len + 1);
            if (OB_FAIL(matched.push_back(ObString(static_cast<int32_t>(len), copy)))) {
            }
          }
        }
      }
      std::sort(matched.begin(), matched.end(),
                [](const ObString &left, const ObString &right) {
                  return left.compare(right) < 0;
                });
      for (int64_t i = 0; OB_SUCC(ret) && i < matched.count(); ++i) {
        if (OB_FAIL(files.push_back(matched.at(i)))) {
        }
      }
    }
    ::globfree(&glob_buf);
  }
#endif
  return ret;
}

int FileScanUtils::stat_file(const char *path, FileStat &file_stat)
{
  int ret = OB_SUCCESS;
  ob_stat_t st;
  MEMSET(&st, 0, sizeof(st));
  if (OB_ISNULL(path)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 != ob_stat(path, &st)) {
    ret = (ENOENT == errno) ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
  } else {
    file_stat.device_ = static_cast<int64_t>(st.st_dev);
    file_stat.inode_ = static_cast<int64_t>(st.st_ino);
    file_stat.size_ = static_cast<int64_t>(st.st_size);
#if defined(_WIN32)
    file_stat.mtime_ns_ = static_cast<int64_t>(st.st_mtime) * 1000000000LL;
#elif defined(__APPLE__)
    file_stat.mtime_ns_ = static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL
                          + static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#else
    file_stat.mtime_ns_ = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL
                          + static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
  }
  return ret;
}

// ------------------------------------------------------------ type inference

const char *FileScanUtils::infer_type_name(const FileInferType type)
{
  const char *name = "VARCHAR";
  switch (type) {
    case FileInferType::FIT_NULL: name = "NULL"; break;
    case FileInferType::FIT_BOOLEAN: name = "BOOLEAN"; break;
    case FileInferType::FIT_BIGINT: name = "BIGINT"; break;
    case FileInferType::FIT_DOUBLE: name = "DOUBLE"; break;
    case FileInferType::FIT_DATE: name = "DATE"; break;
    case FileInferType::FIT_DATETIME: name = "DATETIME"; break;
    case FileInferType::FIT_VARCHAR: name = "VARCHAR"; break;
    default: break;
  }
  return name;
}

ObObjType FileScanUtils::infer_to_obj_type(const FileInferType type)
{
  ObObjType obj_type = ObVarcharType;
  switch (type) {
    case FileInferType::FIT_NULL: obj_type = ObNullType; break;
    case FileInferType::FIT_BOOLEAN: obj_type = ObTinyIntType; break;
    case FileInferType::FIT_BIGINT: obj_type = ObIntType; break;
    case FileInferType::FIT_DOUBLE: obj_type = ObDoubleType; break;
    case FileInferType::FIT_DATE: obj_type = ObDateType; break;
    case FileInferType::FIT_DATETIME: obj_type = ObDateTimeType; break;
    case FileInferType::FIT_VARCHAR: obj_type = ObVarcharType; break;
    default: break;
  }
  return obj_type;
}

FileInferType FileScanUtils::merge_infer_type(const FileInferType left, const FileInferType right)
{
  FileInferType merged = FileInferType::FIT_VARCHAR;
  if (FileInferType::FIT_NULL == left) {
    merged = right;
  } else if (FileInferType::FIT_NULL == right) {
    merged = left;
  } else if (left == right) {
    merged = left;
  } else if ((FileInferType::FIT_BIGINT == left && FileInferType::FIT_DOUBLE == right)
             || (FileInferType::FIT_DOUBLE == left && FileInferType::FIT_BIGINT == right)) {
    merged = FileInferType::FIT_DOUBLE;
  } else if ((FileInferType::FIT_DATE == left && FileInferType::FIT_DATETIME == right)
             || (FileInferType::FIT_DATETIME == left && FileInferType::FIT_DATE == right)) {
    merged = FileInferType::FIT_DATETIME;
  } else {
    merged = FileInferType::FIT_VARCHAR;
  }
  return merged;
}

int FileScanUtils::parse_int64(const ObString &text, int64_t &value, bool &fit)
{
  int ret = OB_SUCCESS;
  int64_t i = 0;
  bool negative = false;
  bool overflow = false;
  int64_t result = 0;
  fit = false;
  if (text.length() > 0 && ('+' == text.ptr()[0] || '-' == text.ptr()[0])) {
    negative = ('-' == text.ptr()[0]);
    i = 1;
  }
  if (i >= text.length()) {
    ret = OB_INVALID_DATA;
  }
  for (; OB_SUCC(ret) && i < text.length(); ++i) {
    const char ch = text.ptr()[i];
    if (!is_digit(ch)) {
      ret = OB_INVALID_DATA;
    } else if (!overflow) {
      const int digit = ch - '0';
      const int64_t limit_div = (negative ? INT64_MIN : INT64_MAX) / 10;
      const int limit_digit = static_cast<int>(negative ? 8 : 7);
      if (result < limit_div || (result == limit_div && digit > limit_digit)) {
        overflow = true;
      } else {
        result = result * 10 + digit;
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (overflow) {
      fit = false;
    } else {
      value = negative ? -result : result;
      fit = true;
    }
  }
  return ret;
}

int FileScanUtils::parse_double(const ObString &text, double &value)
{
  int ret = OB_SUCCESS;
  char buf[512];
  char *end = nullptr;
  if (text.length() <= 0 || text.length() >= static_cast<int64_t>(sizeof(buf))) {
    ret = OB_INVALID_DATA;
  } else {
    MEMCPY(buf, text.ptr(), text.length());
    buf[text.length()] = '\0';
    errno = 0;
    value = ::strtod(buf, &end);
    if (end != buf + text.length() || ERANGE == errno
        || std::isnan(value) || std::isinf(value)) {
      ret = OB_INVALID_DATA;
    }
  }
  return ret;
}

int FileScanUtils::parse_date(const ObString &text, int32_t &days)
{
  int ret = OB_SUCCESS;
  if (text.length() != 10 || '-' != text.ptr()[4] || '-' != text.ptr()[7]) {
    ret = OB_INVALID_DATA;
  } else {
    int64_t parts[3] = {0, 0, 0};
    for (int64_t p = 0; OB_SUCC(ret) && p < 3; ++p) {
      const int64_t begin = (0 == p) ? 0 : (1 == p ? 5 : 8);
      const int64_t len = (0 == p) ? 4 : 2;
      for (int64_t i = 0; OB_SUCC(ret) && i < len; ++i) {
        const char ch = text.ptr()[begin + i];
        if (!is_digit(ch)) {
          ret = OB_INVALID_DATA;
        } else {
          parts[p] = parts[p] * 10 + (ch - '0');
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (!valid_date_parts(parts[0], parts[1], parts[2])) {
      ret = OB_INVALID_DATA;
    } else {
      days = static_cast<int32_t>(days_from_civil(parts[0], parts[1], parts[2]));
    }
  }
  return ret;
}

int FileScanUtils::parse_datetime(const ObString &text, int64_t &usec)
{
  int ret = OB_SUCCESS;
  if (text.length() < 19 || (' ' != text.ptr()[10] && 'T' != text.ptr()[10])
      || ':' != text.ptr()[13] || ':' != text.ptr()[16]) {
    ret = OB_INVALID_DATA;
  } else {
    int32_t days = 0;
    ObString date_part;
    date_part.assign_ptr(text.ptr(), 10);
    if (OB_FAIL(parse_date(date_part, days))) {
    } else {
      int64_t hms[3] = {0, 0, 0};
      for (int64_t p = 0; OB_SUCC(ret) && p < 3; ++p) {
        const int64_t begin = 11 + p * 3;
        for (int64_t i = 0; OB_SUCC(ret) && i < 2; ++i) {
          const char ch = text.ptr()[begin + i];
          if (!is_digit(ch)) {
            ret = OB_INVALID_DATA;
          } else {
            hms[p] = hms[p] * 10 + (ch - '0');
          }
        }
      }
      int64_t fraction_us = 0;
      int64_t fraction_digits = 0;
      if (OB_FAIL(ret)) {
      } else if (text.length() > 19) {
        if ('.' != text.ptr()[19]) {
          ret = OB_INVALID_DATA;
        }
        for (int64_t i = 20; OB_SUCC(ret) && i < text.length(); ++i) {
          const char ch = text.ptr()[i];
          if (!is_digit(ch) || fraction_digits >= 6) {
            ret = OB_INVALID_DATA;
          } else {
            fraction_us = fraction_us * 10 + (ch - '0');
            ++fraction_digits;
          }
        }
        for (int64_t i = fraction_digits; OB_SUCC(ret) && i < 6; ++i) {
          fraction_us *= 10;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (hms[0] > 23 || hms[1] > 59 || hms[2] > 59) {
        ret = OB_INVALID_DATA;
      } else {
        usec = static_cast<int64_t>(days) * 86400LL * 1000000LL
               + (hms[0] * 3600 + hms[1] * 60 + hms[2]) * 1000000LL + fraction_us;
      }
    }
  }
  return ret;
}

int FileScanUtils::sniff_scalar(const ObString &text, FileInferType &type)
{
  int ret = OB_SUCCESS;
  int64_t int_value = 0;
  double double_value = 0;
  int32_t date_value = 0;
  int64_t datetime_value = 0;
  bool int_fit = false;
  type = FileInferType::FIT_NULL;
  if (0 == text.length()) {
    // empty field, keeps FIT_NULL
  } else if (ob_str_case_equal(text, "true") || ob_str_case_equal(text, "false")) {
    type = FileInferType::FIT_BOOLEAN;
  } else if (OB_SUCCESS == parse_int64(text, int_value, int_fit) && int_fit) {
    type = FileInferType::FIT_BIGINT;
  } else if (OB_SUCCESS == parse_double(text, double_value)) {
    // an integer that does not fit into int64 is still a valid double
    type = FileInferType::FIT_DOUBLE;
  } else if (OB_SUCCESS == parse_date(text, date_value)) {
    type = FileInferType::FIT_DATE;
  } else if (OB_SUCCESS == parse_datetime(text, datetime_value)) {
    type = FileInferType::FIT_DATETIME;
  } else {
    type = FileInferType::FIT_VARCHAR;
  }
  return ret;
}

int FileScanUtils::convert_value(const ObString &text, const FileInferType type, ObObj &obj)
{
  int ret = OB_SUCCESS;
  int64_t int_value = 0;
  double double_value = 0;
  int32_t date_value = 0;
  int64_t datetime_value = 0;
  bool int_fit = false;
  switch (type) {
    case FileInferType::FIT_NULL:
      obj.set_null();
      break;
    case FileInferType::FIT_BOOLEAN:
      obj.set_tinyint(static_cast<int8_t>(ob_str_case_equal(text, "true") ? 1 : 0));
      break;
    case FileInferType::FIT_BIGINT:
      if (OB_FAIL(parse_int64(text, int_value, int_fit)) || !int_fit) {
        ret = OB_INVALID_DATA;
      } else {
        obj.set_int(int_value);
      }
      break;
    case FileInferType::FIT_DOUBLE:
      if (OB_FAIL(parse_double(text, double_value))) {
      } else {
        obj.set_double(double_value);
      }
      break;
    case FileInferType::FIT_DATE:
      if (OB_FAIL(parse_date(text, date_value))) {
      } else {
        obj.set_date(date_value);
      }
      break;
    case FileInferType::FIT_DATETIME:
      if (OB_FAIL(parse_datetime(text, datetime_value))) {
      } else {
        obj.set_datetime(datetime_value);
      }
      break;
    case FileInferType::FIT_VARCHAR:
      obj.set_varchar(text);
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      break;
  }
  return ret;
}

bool FileScanUtils::is_null_field(const ObString &text, const FileScanOptions &options)
{
  bool is_null = false;
  if (options.null_str_configured_) {
    is_null = (text.length() == options.null_str_.length())
              && (0 == text.length()
                  || 0 == MEMCMP(text.ptr(), options.null_str_.ptr(), text.length()));
  } else {
    is_null = (0 == text.length());
  }
  return is_null;
}

// ------------------------------------------------------------------ CSV reader

CsvFileReader::CsvFileReader()
{
}

CsvFileReader::~CsvFileReader()
{
  close();
}

void CsvFileReader::close_file()
{
  if (OB_NOT_NULL(fp_)) {
    ::fclose(fp_);
    fp_ = nullptr;
  }
  if (OB_NOT_NULL(read_buf_)) {
    ::free(read_buf_);
    read_buf_ = nullptr;
  }
  if (OB_NOT_NULL(row_buf_)) {
    ::free(row_buf_);
    row_buf_ = nullptr;
  }
  read_buf_cap_ = 0;
  row_buf_cap_ = 0;
  row_buf_pos_ = 0;
  read_buf_pos_ = 0;
  read_buf_len_ = 0;
  opened_ = false;
}

int CsvFileReader::open(const char *path, const FileScanOptions &options)
{
  int ret = OB_SUCCESS;
  if (is_opened()) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(path)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(stat_file(path, stat_))) {
  } else if (OB_ISNULL(fp_ = ::fopen(path, "rb"))) {
    ret = (EACCES == errno) ? OB_FILE_OR_DIRECTORY_PERMISSION_DENIED : OB_IO_ERROR;
    LOG_WARN("failed to open csv file", K(ret), K(errno), K(path));
  } else if (OB_ISNULL(read_buf_ = static_cast<char *>(::malloc(FILE_SCAN_READ_BUF_SIZE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(row_buf_ = static_cast<char *>(::malloc(1024)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    options_ = options;
    path_.assign_ptr(path, static_cast<int32_t>(strlen(path)));
    read_buf_cap_ = FILE_SCAN_READ_BUF_SIZE;
    row_buf_cap_ = 1024;
    line_no_ = 1;
    column_count_ = 0;
    eof_ = false;
    opened_ = true;
  }
  if (OB_FAIL(ret)) {
    close_file();
  }
  return ret;
}

int CsvFileReader::close()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(fp_) && 0 != ::fclose(fp_)) {
    ret = OB_IO_ERROR;
  }
  fp_ = nullptr;
  if (OB_NOT_NULL(read_buf_)) {
    ::free(read_buf_);
    read_buf_ = nullptr;
  }
  if (OB_NOT_NULL(row_buf_)) {
    ::free(row_buf_);
    row_buf_ = nullptr;
  }
  read_buf_cap_ = 0;
  row_buf_cap_ = 0;
  row_buf_pos_ = 0;
  read_buf_pos_ = 0;
  read_buf_len_ = 0;
  opened_ = false;
  return ret;
}

int CsvFileReader::check_unchanged() const
{
  int ret = OB_SUCCESS;
  FileStat current;
  if (path_.empty()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(stat_file(path_.ptr(), current))) {
  } else if (!(current == stat_)) {
    // the file changed under the scan, the result would be inconsistent
    ret = OB_FILE_RECYCLED;
    LOG_WARN("file changed during scan", K(ret), K(stat_), K(current));
  }
  return ret;
}

int CsvFileReader::rescan()
{
  int ret = OB_SUCCESS;
  if (!is_opened()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(check_unchanged())) {
  } else if (0 != ::fseek(fp_, 0, SEEK_SET)) {
    ret = OB_IO_ERROR;
  } else {
    read_buf_pos_ = 0;
    read_buf_len_ = 0;
    row_buf_pos_ = 0;
    line_no_ = 1;
    eof_ = false;
    row_fields_.reuse();
  }
  return ret;
}

int CsvFileReader::refill()
{
  int ret = OB_SUCCESS;
  if (read_buf_pos_ < read_buf_len_) {
    // data is still buffered
  } else if (eof_) {
    ret = OB_ITER_END;
  } else {
    read_buf_pos_ = 0;
    read_buf_len_ = static_cast<int64_t>(::fread(read_buf_, 1, FILE_SCAN_READ_BUF_SIZE, fp_));
    if (0 == read_buf_len_) {
      if (0 != ::ferror(fp_)) {
        ret = OB_IO_ERROR;
      } else {
        eof_ = true;
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}

int CsvFileReader::read_char(char &ch)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(refill())) {
  } else {
    ch = read_buf_[read_buf_pos_++];
  }
  return ret;
}

int CsvFileReader::append_char(const char ch)
{
  int ret = OB_SUCCESS;
  if (row_buf_pos_ >= FILE_SCAN_MAX_ROW_BYTES) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("csv record is too large", K(ret), K(row_buf_pos_));
  } else if (row_buf_pos_ >= row_buf_cap_) {
    const int64_t new_cap = std::min<int64_t>(row_buf_cap_ * 2, FILE_SCAN_MAX_ROW_BYTES);
    char *new_buf = static_cast<char *>(::realloc(row_buf_, new_cap));
    if (OB_ISNULL(new_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      row_buf_ = new_buf;
      row_buf_cap_ = new_cap;
      row_buf_[row_buf_pos_++] = ch;
    }
  } else {
    row_buf_[row_buf_pos_++] = ch;
  }
  return ret;
}

int CsvFileReader::parse_row()
{
  int ret = OB_SUCCESS;
  enum State { FIELD_START = 0, UNQUOTED = 1, QUOTED = 2, QUOTE_IN_QUOTED = 3 };
  State state = FIELD_START;
  row_buf_pos_ = 0;
  row_fields_.reuse();
  int32_t field_begin = 0;
  bool row_done = false;
  bool any_char = false;
  while (OB_SUCC(ret) && !row_done) {
    char ch = '\0';
    const int read_ret = read_char(ch);
    if (OB_ITER_END == read_ret) {
      if (any_char) {
        // last record without a trailing newline
        if (OB_FAIL(row_fields_.push_back(
                ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                         row_buf_ + field_begin)))) {
        }
        row_done = true;
      } else {
        ret = OB_ITER_END;
      }
      break;
    } else if (OB_FAIL(read_ret)) {
      ret = read_ret;
      break;
    }
    any_char = true;
    switch (state) {
      case FIELD_START: {
        if (ch == options_.quote_) {
          field_begin = static_cast<int32_t>(row_buf_pos_);
          state = QUOTED;
        } else if (ch == options_.delimiter_) {
          if (OB_FAIL(row_fields_.push_back(ObString(0, row_buf_ + row_buf_pos_)))) {
          }
        } else if ('\n' == ch) {
          ++line_no_;
          if (row_fields_.empty() && 0 == row_buf_pos_) {
            // blank line, skipped
          } else {
            if (OB_FAIL(row_fields_.push_back(ObString(0, row_buf_ + row_buf_pos_)))) {
            }
            row_done = true;
          }
        } else if ('\r' == ch) {
          char next = '\0';
          const int next_ret = read_char(next);
          if (OB_SUCC(next_ret) && '\n' != next && read_buf_pos_ > 0) {
            --read_buf_pos_;
          }
          if (row_fields_.empty() && 0 == row_buf_pos_) {
          } else {
            if (OB_FAIL(row_fields_.push_back(ObString(0, row_buf_ + row_buf_pos_)))) {
            }
            row_done = true;
          }
          ++line_no_;
        } else {
          field_begin = static_cast<int32_t>(row_buf_pos_);
          if (OB_FAIL(append_char(ch))) {
          }
          state = UNQUOTED;
        }
        break;
      }
      case UNQUOTED: {
        if (ch == options_.delimiter_) {
          if (OB_FAIL(row_fields_.push_back(
                  ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                           row_buf_ + field_begin)))) {
          }
          field_begin = static_cast<int32_t>(row_buf_pos_);
          state = FIELD_START;
        } else if ('\n' == ch) {
          ++line_no_;
          if (OB_FAIL(row_fields_.push_back(
                  ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                           row_buf_ + field_begin)))) {
          }
          row_done = true;
        } else if ('\r' == ch) {
          char next = '\0';
          const int next_ret = read_char(next);
          if (OB_SUCC(next_ret) && '\n' != next && read_buf_pos_ > 0) {
            --read_buf_pos_;
          }
          if (OB_FAIL(row_fields_.push_back(
                  ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                           row_buf_ + field_begin)))) {
          }
          row_done = true;
          ++line_no_;
        } else if (OB_FAIL(append_char(ch))) {
        }
        break;
      }
      case QUOTED: {
        if (ch == options_.escape_ && options_.escape_ != options_.quote_) {
          char next = '\0';
          if (OB_FAIL(read_char(next))) {
          } else if (OB_FAIL(append_char(next))) {
          }
        } else if (ch == options_.quote_) {
          state = QUOTE_IN_QUOTED;
        } else {
          if ('\n' == ch) {
            ++line_no_;
          }
          if (OB_FAIL(append_char(ch))) {
          }
        }
        break;
      }
      case QUOTE_IN_QUOTED: {
        if (ch == options_.quote_) {
          // "" inside a quoted field is a literal quote
          if (OB_FAIL(append_char(ch))) {
          }
          state = QUOTED;
        } else if (ch == options_.delimiter_) {
          if (OB_FAIL(row_fields_.push_back(
                  ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                           row_buf_ + field_begin)))) {
          }
          field_begin = static_cast<int32_t>(row_buf_pos_);
          state = FIELD_START;
        } else if ('\n' == ch) {
          ++line_no_;
          if (OB_FAIL(row_fields_.push_back(
                  ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                           row_buf_ + field_begin)))) {
          }
          row_done = true;
        } else if ('\r' == ch) {
          char next = '\0';
          const int next_ret = read_char(next);
          if (OB_SUCC(next_ret) && '\n' != next && read_buf_pos_ > 0) {
            --read_buf_pos_;
          }
          if (OB_FAIL(row_fields_.push_back(
                  ObString(static_cast<int32_t>(row_buf_pos_ - field_begin),
                           row_buf_ + field_begin)))) {
          }
          row_done = true;
          ++line_no_;
        } else {
          // tolerate characters after the closing quote
          if (OB_FAIL(append_char(ch))) {
          }
          state = UNQUOTED;
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        break;
    }
  }
  if (OB_SUCC(ret)) {
    column_count_ = std::max<int64_t>(column_count_, row_fields_.count());
    if (row_fields_.count() > FILE_SCAN_MAX_COLUMNS) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("too many csv columns", K(ret), K(row_fields_.count()));
    }
  }
  return ret;
}

int CsvFileReader::get_next_row(ObIArray<ObString> &fields)
{
  int ret = OB_SUCCESS;
  if (!is_opened()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(parse_row())) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row_fields_.count(); ++i) {
      if (OB_FAIL(fields.push_back(row_fields_.at(i)))) {
      }
    }
  }
  return ret;
}

int CsvFileReader::infer_schema(ObIAllocator &allocator, ObIArray<FileColumnDesc> &columns)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 32> header_names;
  ObSEArray<ObString, 32> fields;
  ObSEArray<FileInferType, 32> types;
  ObSEArray<bool, 32> nullables;
  int64_t expected_columns = -1;
  if (!is_opened()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(rescan())) {
  } else if (options_.header_) {
    ObSEArray<ObString, 32> raw_names;
    if (OB_FAIL(get_next_row(raw_names))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    } else {
      // the names point into the row buffer, they must be copied before the
      // inference pass reuses it
      for (int64_t i = 0; OB_SUCC(ret) && i < raw_names.count(); ++i) {
        ObString name_copy;
        if (OB_FAIL(ob_write_string(allocator, raw_names.at(i), name_copy))) {
        } else if (OB_FAIL(header_names.push_back(name_copy))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      expected_columns = header_names.count();
    }
  }

  while (OB_SUCC(ret)) {
    fields.reuse();
    if (OB_FAIL(get_next_row(fields))) {
      if (OB_ITER_END != ret) {
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } else if (fields.count() > FILE_SCAN_MAX_COLUMNS) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("too many columns in file", K(ret), K(fields.count()));
    } else if (expected_columns < 0) {
      // no header: the first data row defines the column count
      expected_columns = fields.count();
      for (int64_t i = 0; OB_SUCC(ret) && i < expected_columns; ++i) {
        if (OB_FAIL(types.push_back(FileInferType::FIT_NULL))
            || OB_FAIL(nullables.push_back(false))) {
        }
      }
    } else if (fields.count() != expected_columns) {
      ret = OB_INVALID_DATA;
      LOG_WARN("row has a different column count", K(ret), K(fields.count()),
               K(expected_columns), K(line_no_));
    }
    if (OB_FAIL(ret)) {
    } else {
      for (int64_t i = 0; i < expected_columns; ++i) {
        FileInferType field_type = FileInferType::FIT_NULL;
        if (OB_FAIL(sniff_scalar(fields.at(i), field_type))) {
        } else if (FileInferType::FIT_NULL == field_type
                   || is_null_field(fields.at(i), options_)) {
          nullables.at(i) = true;
        } else {
          types.at(i) = merge_infer_type(types.at(i), field_type);
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (expected_columns < 0) {
      expected_columns = 0;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < expected_columns; ++i) {
      FileColumnDesc desc;
      char name_buf[OB_MAX_COLUMN_NAME_LENGTH];
      ObString column_name;
      if (options_.header_ && i < header_names.count() && !header_names.at(i).empty()) {
        column_name = header_names.at(i);
      } else {
        const int64_t written = snprintf(name_buf, sizeof(name_buf), "column%ld", i);
        column_name.assign_ptr(name_buf, static_cast<int32_t>(written));
      }
      // duplicate headers are renamed name__2, name__3, ... (the tests rely on
      // the `id__2` spelling)
      char dedup_buf[OB_MAX_COLUMN_NAME_LENGTH + 16];
      ObString final_name = column_name;
      int64_t occurrence = 1;
      bool duplicated = true;
      while (duplicated) {
        duplicated = false;
        for (int64_t j = 0; !duplicated && j < columns.count(); ++j) {
          duplicated = (0 == columns.at(j).name_.case_compare(final_name));
        }
        if (duplicated) {
          ++occurrence;
          const int64_t written = snprintf(dedup_buf, sizeof(dedup_buf), "%.*s__%ld",
                                           column_name.length(), column_name.ptr(), occurrence);
          final_name.assign_ptr(dedup_buf, static_cast<int32_t>(written));
        }
      }
      if (OB_FAIL(ob_write_string(allocator, final_name, desc.name_))) {
      } else {
        // a column that never saw a value is reported as VARCHAR, the same way
        // DuckDB describes an all NULL column
        desc.type_ = (FileInferType::FIT_NULL == types.at(i))
                     ? ObVarcharType : infer_to_obj_type(types.at(i));
        desc.nullable_ = nullables.at(i) || FileInferType::FIT_NULL == types.at(i);
        if (OB_FAIL(columns.push_back(desc))) {
        }
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(rescan())) {
  }
  return ret;
}

// ------------------------------------------------------------------ CSV writer

CsvFileWriter::CsvFileWriter()
{
}

CsvFileWriter::~CsvFileWriter()
{
  if (!committed_) {
    abort();
  }
}

int CsvFileWriter::open(const char *path, const FileScanOptions &options)
{
  int ret = OB_SUCCESS;
  if (opened_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(path)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
      // the target must not exist, an existing file is an error
      ret = (EEXIST == errno) ? OB_FILE_ALREADY_EXIST : OB_IO_ERROR;
      LOG_WARN("failed to create copy target", K(ret), K(errno), K(path));
    } else if (OB_ISNULL(fp_ = ::fdopen(fd, "wb"))) {
      ret = OB_IO_ERROR;
      ::close(fd);
      LOG_WARN("failed to open copy target stream", K(ret), K(errno), K(path));
    }
    if (OB_FAIL(ret)) {
    } else {
      options_ = options;
      path_.assign_ptr(path, static_cast<int32_t>(strlen(path)));
      opened_ = true;
      committed_ = false;
      row_count_ = 0;
    }
  }
  return ret;
}

int CsvFileWriter::write_raw(const char *data, const int64_t length)
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (length > 0
             && length != static_cast<int64_t>(::fwrite(data, 1, length, fp_))) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to write copy data", K(ret), K(errno));
  }
  return ret;
}

int CsvFileWriter::write_field(const ObString &value, const bool is_null)
{
  int ret = OB_SUCCESS;
  if (is_null) {
    if (options_.null_str_configured_) {
      ret = write_raw(options_.null_str_.ptr(), options_.null_str_.length());
    }
  } else {
    bool need_quote = false;
    for (int64_t i = 0; !need_quote && i < value.length(); ++i) {
      const char ch = value.ptr()[i];
      need_quote = (ch == options_.delimiter_ || ch == options_.quote_
                    || ch == '\n' || ch == '\r');
    }
    if (!need_quote) {
      ret = write_raw(value.ptr(), value.length());
    } else if (OB_FAIL(write_raw(&options_.quote_, 1))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < value.length(); ++i) {
        const char ch = value.ptr()[i];
        if (ch == options_.quote_ && OB_FAIL(write_raw(&options_.quote_, 1))) {
        } else if (OB_FAIL(write_raw(&ch, 1))) {
        }
      }
      if (OB_SUCC(ret)) {
        ret = write_raw(&options_.quote_, 1);
      }
    }
  }
  return ret;
}

int CsvFileWriter::write_header(const ObIArray<ObString> &column_names)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); ++i) {
    if (i > 0 && OB_FAIL(write_raw(&options_.delimiter_, 1))) {
    } else if (OB_FAIL(write_field(column_names.at(i), false))) {
    }
  }
  if (OB_SUCC(ret)) {
    ret = write_raw("\n", 1);
  }
  return ret;
}

int CsvFileWriter::write_row(const ObIArray<ObString> &values, const ObIArray<bool> &is_null)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < values.count(); ++i) {
    const bool null_value = (i < is_null.count()) ? is_null.at(i) : false;
    if (i > 0 && OB_FAIL(write_raw(&options_.delimiter_, 1))) {
    } else if (OB_FAIL(write_field(values.at(i), null_value))) {
    }
  }
  if (OB_SUCC(ret)) {
    ret = write_raw("\n", 1);
  }
  if (OB_SUCC(ret)) {
    ++row_count_;
  }
  return ret;
}

int CsvFileWriter::commit()
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (0 != ::fflush(fp_)) {
    ret = OB_IO_ERROR;
  } else if (0 != ::fclose(fp_)) {
    ret = OB_IO_ERROR;
    fp_ = nullptr;
  } else {
    fp_ = nullptr;
    opened_ = false;
    committed_ = true;
  }
  return ret;
}

int CsvFileWriter::abort()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(fp_)) {
    ::fclose(fp_);
    fp_ = nullptr;
  }
  if (opened_ && !path_.empty()) {
    char path_buf[PATH_MAX + 1];
    const int64_t len = std::min<int64_t>(path_.length(),
                                          static_cast<int64_t>(sizeof(path_buf) - 1));
    MEMCPY(path_buf, path_.ptr(), len);
    path_buf[len] = '\0';
    if (0 != ::remove(path_buf) && ENOENT != errno) {
      ret = OB_IO_ERROR;
      LOG_WARN("failed to remove partial copy file", K(ret), K(errno), K(path_));
    }
  }
  opened_ = false;
  committed_ = true;
  return ret;
}

} // namespace sql
} // namespace oceanbase
