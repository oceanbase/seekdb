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
#include "common/timezone/ob_time_convert.h"
#include "lib/string/ob_sql_string.h"
#include "sql/engine/basic/ob_file_scan_reader.h"
#include "sql/engine/basic/ob_glob_reader.h"
#include "sql/engine/basic/ob_parquet_reader.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

/**
 * @brief The rank of the integer types, the wider the type the greater the rank.
 */
int ob_file_int_rank(const ObObjType type)
{
  switch (type) {
    case ObTinyIntType: return 1;
    case ObSmallIntType: return 2;
    case ObMediumIntType: return 3;
    case ObInt32Type: return 4;
    case ObIntType: return 5;
    case ObUTinyIntType: return 6;
    case ObUSmallIntType: return 7;
    case ObUMediumIntType: return 8;
    case ObUInt32Type: return 9;
    case ObUInt64Type: return 10;
    default: return 0;
  }
}

bool ob_file_is_unsigned_int(const ObObjType type)
{
  return ob_file_int_rank(type) >= 6;
}

bool ob_file_is_int_tc(const ObObjType type)
{
  return ob_file_int_rank(type) > 0;
}

bool ob_file_is_float_tc(const ObObjType type)
{
  return ObFloatType == type || ObDoubleType == type
         || ObUFloatType == type || ObUDoubleType == type;
}

/**
 * @brief Whether the text is a boolean literal, i.e. "true" or "false".
 * The case of the letters is ignored, "TRUE" and "True" are booleans too.
 */
bool ob_file_is_bool_text(const char *data, const int64_t len)
{
  if (4 != len && 5 != len) {
    return false;
  }
  const char *expected = (4 == len) ? "true" : "false";
  for (int64_t i = 0; i < len; ++i) {
    char c = data[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != expected[i]) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Whether the text is an integer literal that fits into an int64.
 */
bool ob_file_parse_int64(const char *data, const int64_t len, int64_t &value)
{
  bool ret = false;
  int64_t pos = 0;
  bool is_negative = false;
  uint64_t magnitude = 0;
  if (len > 0) {
    if ('-' == data[0] || '+' == data[0]) {
      is_negative = ('-' == data[0]);
      pos = 1;
    }
    if (pos < len) {
      ret = true;
      for (; pos < len; ++pos) {
        const char c = data[pos];
        if (c < '0' || c > '9') {
          ret = false;
          break;
        }
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (magnitude > (UINT64_MAX - digit) / 10) {
          ret = false;
          break;
        }
        magnitude = magnitude * 10 + digit;
      }
      if (ret) {
        const uint64_t limit = is_negative ? 9223372036854775808ULL : 9223372036854775807ULL;
        if (magnitude > limit) {
          ret = false;
        } else if (is_negative) {
          value = -static_cast<int64_t>(magnitude);
        } else {
          value = static_cast<int64_t>(magnitude);
        }
      }
    }
  }
  return ret;
}

/**
 * @brief Whether the text is a floating point literal.
 */
bool ob_file_parse_double(const char *data, const int64_t len, double &value)
{
  bool ret = false;
  // the buffer is long enough for any literal that strtod() accepts
  char buf[64];
  if (len > 0 && len < static_cast<int64_t>(sizeof(buf))) {
    MEMCPY(buf, data, len);
    buf[len] = '\0';
    char *endptr = NULL;
    errno = 0;
    const double dbl = strtod(buf, &endptr);
    if (NULL != endptr && endptr == buf + len) {
      value = dbl;
      ret = true;
    }
  }
  return ret;
}

bool ob_file_is_leap_year(const int32_t year)
{
  return (0 == year % 4 && 0 != year % 100) || (0 == year % 400);
}

/**
 * @brief Whether a text holds another text, the case is ignored.
 */
bool ob_file_text_equals_ci(const char *text, const int64_t len, const char *other)
{
  bool ret = false;
  const int64_t other_len = static_cast<int64_t>(strlen(other));
  if (len == other_len) {
    ret = true;
    for (int64_t i = 0; i < len && ret; ++i) {
      const char c = (text[i] >= 'A' && text[i] <= 'Z') ? static_cast<char>(text[i] - 'A' + 'a') : text[i];
      const char o = (other[i] >= 'A' && other[i] <= 'Z') ? static_cast<char>(other[i] - 'A' + 'a') : other[i];
      ret = (c == o);
    }
  }
  return ret;
}

int32_t ob_file_days_of_month(const int32_t year, const int32_t month)
{
  static const int32_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }
  if (2 == month && ob_file_is_leap_year(year)) {
    return 29;
  }
  return days[month - 1];
}

/**
 * @brief Whether the text is a valid "YYYY-MM-DD" date.
 */
bool ob_file_is_date_text(const char *data, const int64_t len)
{
  bool ret = false;
  if (10 == len) {
    if ('-' == data[4] && '-' == data[7]
        && data[0] >= '0' && data[0] <= '9' && data[1] >= '0' && data[1] <= '9'
        && data[2] >= '0' && data[2] <= '9' && data[3] >= '0' && data[3] <= '9'
        && data[5] >= '0' && data[5] <= '9' && data[6] >= '0' && data[6] <= '9'
        && data[8] >= '0' && data[8] <= '9' && data[9] >= '0' && data[9] <= '9') {
      const int32_t year = (data[0] - '0') * 1000 + (data[1] - '0') * 100 + (data[2] - '0') * 10 + (data[3] - '0');
      const int32_t month = (data[5] - '0') * 10 + (data[6] - '0');
      const int32_t day = (data[8] - '0') * 10 + (data[9] - '0');
      if (year >= 0 && year <= 9999 && month >= 1 && month <= 12
          && day >= 1 && day <= ob_file_days_of_month(year, month)) {
        ret = true;
      }
    }
  }
  return ret;
}

/**
 * @brief Whether the text is a valid "YYYY-MM-DD[ T]HH:MM[:SS[.f...]]" datetime.
 */
bool ob_file_is_datetime_text(const char *data, const int64_t len)
{
  bool ret = false;
  // the shortest datetime is "YYYY-MM-DD HH:MM"
  if (len >= 16 && ob_file_is_date_text(data, 10)
      && (' ' == data[10] || 'T' == data[10])
      && data[11] >= '0' && data[11] <= '9' && data[12] >= '0' && data[12] <= '9'
      && ':' == data[13]
      && data[14] >= '0' && data[14] <= '9' && data[15] >= '0' && data[15] <= '9') {
    const int64_t hour = (data[11] - '0') * 10 + (data[12] - '0');
    const int64_t minute = (data[14] - '0') * 10 + (data[15] - '0');
    if (hour <= 23 && minute <= 59) {
      if (16 == len) {
        ret = true;
      } else if (':' == data[16]) {
        if (len < 19 || data[17] < '0' || data[17] > '9' || data[18] < '0' || data[18] > '9') {
          return false;
        }
        const int64_t second = (data[17] - '0') * 10 + (data[18] - '0');
        if (second > 59) {
          return false;
        }
        if (19 == len) {
          ret = true;
        } else if ('.' == data[19]) {
          int64_t pos = 20;
          while (pos < len && data[pos] >= '0' && data[pos] <= '9') {
            ++pos;
          }
          ret = (pos > 20 && pos <= 26 && pos == len);
        }
      }
    }
  }
  return ret;
}

} // end anonymous namespace

common::ObObjType ObFileScanUtils::infer_scalar_type(const char *data, const int64_t len, bool &is_null)
{
  is_null = false;
  if (OB_ISNULL(data)) {
    is_null = true;
    return common::ObNullType;
  } else if (len <= 0) {
    // an empty text is the empty string, the callers decide whether a missing
    // value is a NULL value
    return common::ObVarcharType;
  } else if (ob_file_is_bool_text(data, len)) {
    return common::ObTinyIntType;
  } else if (ob_file_is_datetime_text(data, len)) {
    return common::ObDateTimeType;
  } else if (ob_file_is_date_text(data, len)) {
    return common::ObDateType;
  } else {
    int64_t int_value = 0;
    double dbl_value = 0.0;
    if (ob_file_parse_int64(data, len, int_value)) {
      return common::ObIntType;
    } else if (ob_file_parse_double(data, len, dbl_value)) {
      return common::ObDoubleType;
    } else {
      return common::ObVarcharType;
    }
  }
}

common::ObObjType ObFileScanUtils::merge_type(const common::ObObjType left, const common::ObObjType right)
{
  common::ObObjType type = common::ObVarcharType;
  if (left == right) {
    type = left;
  } else if (common::ObNullType == left) {
    type = right;
  } else if (common::ObNullType == right) {
    type = left;
  } else if (ob_is_string_type(left) || ob_is_string_type(right)) {
    type = common::ObVarcharType;
  } else if (common::ObDateType == left && common::ObDateTimeType == right) {
    type = common::ObDateTimeType;
  } else if (common::ObDateTimeType == left && common::ObDateType == right) {
    type = common::ObDateTimeType;
  } else if (ob_file_is_int_tc(left) && ob_file_is_int_tc(right)) {
    const int left_rank = ob_file_int_rank(left);
    const int right_rank = ob_file_int_rank(right);
    if (ob_file_is_unsigned_int(left) != ob_file_is_unsigned_int(right)) {
      // the unsigned type is the wider one, but it has to fit into a bigint
      type = (left_rank > right_rank) ? left : right;
      if (ob_file_int_rank(type) <= 9) {
        type = common::ObIntType;
      }
    } else {
      type = (left_rank >= right_rank) ? left : right;
    }
  } else if (ob_file_is_float_tc(left) && ob_file_is_float_tc(right)) {
    type = common::ObDoubleType;
  } else if ((ob_file_is_int_tc(left) && ob_file_is_float_tc(right))
             || (ob_file_is_float_tc(left) && ob_file_is_int_tc(right))) {
    type = common::ObDoubleType;
  } else {
    // the values of different families cannot be merged, keep them as text
    type = common::ObVarcharType;
  }
  return type;
}

bool ObFileScanUtils::is_valid_date(const char *data, const int64_t len)
{
  return ob_file_is_date_text(data, len);
}

bool ObFileScanUtils::is_valid_datetime(const char *data, const int64_t len)
{
  return ob_file_is_datetime_text(data, len);
}

int64_t ObFileScanUtils::datetime_scale(const char *data, const int64_t len)
{
  int64_t scale = 0;
  if (ob_file_is_datetime_text(data, len) && len > 20 && '.' == data[19]) {
    scale = len - 20;
  }
  return scale;
}

int ObFileScanUtils::stat_file(const common::ObString &path, int64_t &file_size, int64_t &file_mtime)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(path.ptr()) || path.length() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the file path is empty", K(ret));
  } else {
    struct stat file_stat;
    MEMSET(&file_stat, 0, sizeof(file_stat));
    if (0 != ::stat(path.ptr(), &file_stat)) {
      ret = OB_FILE_NOT_EXIST;
      LOG_WARN("cannot stat the file", K(ret), K(path));
    } else {
      file_size = file_stat.st_size;
      file_mtime = static_cast<int64_t>(file_stat.st_mtime);
    }
  }
  return ret;
}

bool ObFileScanUtils::is_file_changed(const common::ObString &path,
                                      const int64_t file_size,
                                      const int64_t file_mtime)
{
  int64_t size = 0;
  int64_t mtime = 0;
  bool changed = true;
  if (OB_SUCCESS == stat_file(path, size, mtime)) {
    changed = (size != file_size || mtime != file_mtime);
  }
  return changed;
}

int ObFileScanUtils::set_obj_from_text(common::ObObj &obj,
                                       const common::ObObjType type,
                                       const char *data,
                                       const int64_t len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(data) && len > 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (common::ObNullType == type) {
    obj.set_null();
  } else if (common::ObTinyIntType == type) {
    bool value = false;
    if (ob_file_is_bool_text(data, len)) {
      value = (4 == len);
    } else {
      int64_t int_value = 0;
      if (!ob_file_parse_int64(data, len, int_value) || (0 != int_value && 1 != int_value)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("cannot convert the text to a boolean", K(ret), K(len));
      } else {
        value = (1 == int_value);
      }
    }
    if (OB_SUCC(ret)) {
      obj.set_bool(value);
    }
  } else if (common::ObIntType == type) {
    int64_t int_value = 0;
    if (!ob_file_parse_int64(data, len, int_value)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot convert the text to an integer", K(ret), K(len));
    } else {
      obj.set_int(int_value);
    }
  } else if (common::ObDoubleType == type) {
    double dbl_value = 0.0;
    if (!ob_file_parse_double(data, len, dbl_value)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot convert the text to a double", K(ret), K(len));
    } else {
      obj.set_double(dbl_value);
    }
  } else if (common::ObVarcharType == type) {
    obj.set_string(common::ObVarcharType, data, len);
    obj.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  } else if (common::ObDateType == type) {
    int32_t date_value = 0;
    const common::ObString text(len, data);
    if (OB_FAIL(ObTimeConverter::str_to_date(text, date_value))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot convert the text to a date", K(ret), K(text));
    } else {
      obj.set_type(common::ObDateType);
      obj.set_date_value(date_value);
    }
  } else if (common::ObDateTimeType == type) {
    int64_t datetime_value = 0;
    int16_t scale = 0;
    const common::ObString text(len, data);
    ObTimeConvertCtx cvrt_ctx(nullptr, false);
    if (OB_FAIL(ObTimeConverter::str_to_datetime(text, cvrt_ctx, datetime_value, &scale))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot convert the text to a datetime", K(ret), K(text));
    } else {
      obj.set_type(common::ObDateTimeType);
      obj.set_datetime_value(datetime_value);
      obj.set_scale(scale);
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported file column type", K(ret), K(type));
  }
  return ret;
}

const char *ObFileScanUtils::type_name(const common::ObObjType type)
{
  const char *name = "VARCHAR";
  switch (type) {
    case common::ObNullType: name = "NULL"; break;
    case common::ObTinyIntType: name = "BOOLEAN"; break;
    case common::ObSmallIntType: name = "SMALLINT"; break;
    case common::ObMediumIntType: name = "INT"; break;
    case common::ObInt32Type: name = "INT"; break;
    case common::ObIntType: name = "BIGINT"; break;
    case common::ObUTinyIntType: name = "TINYINT UNSIGNED"; break;
    case common::ObUSmallIntType: name = "SMALLINT UNSIGNED"; break;
    case common::ObUMediumIntType: name = "INT UNSIGNED"; break;
    case common::ObUInt32Type: name = "INT UNSIGNED"; break;
    case common::ObUInt64Type: name = "BIGINT UNSIGNED"; break;
    case common::ObFloatType: name = "FLOAT"; break;
    case common::ObDoubleType: name = "DOUBLE"; break;
    case common::ObNumberType: name = "DECIMAL"; break;
    case common::ObDateType: name = "DATE"; break;
    case common::ObDateTimeType: name = "DATETIME"; break;
    case common::ObTimestampType: name = "TIMESTAMP"; break;
    case common::ObTimeType: name = "TIME"; break;
    case common::ObYearType: name = "YEAR"; break;
    case common::ObVarcharType: name = "VARCHAR"; break;
    case common::ObCharType: name = "CHAR"; break;
    default: break;
  }
  return name;
}

bool ObFileScanUtils::is_supported_extension(const ObFileScanFormat format,
                                             const common::ObString &path)
{
  // the name of the file is the part of the path after the last slash
  const char *name = path.ptr();
  int64_t name_len = path.length();
  for (int64_t i = path.length() - 1; i >= 0; --i) {
    if ('/' == path.ptr()[i]) {
      name = path.ptr() + i + 1;
      name_len = path.length() - i - 1;
      break;
    }
  }
  // the extension is the part of the name after the last dot, a name that
  // begins with a dot has no extension, e.g. ".csv" is a name and not an
  // extension
  const char *ext = NULL;
  int64_t ext_len = 0;
  for (int64_t i = name_len - 1; i > 0; --i) {
    if ('.' == name[i]) {
      ext = name + i + 1;
      ext_len = name_len - i - 1;
      break;
    }
  }
  bool ret = true;
  if (ObFileScanFormat::GLOB == format) {
    // the glob function does not read the files, it discovers them
  } else if (NULL == ext || ext_len <= 0) {
    // the file has no extension, the format of its content is unknown
    ret = false;
  } else {
    switch (format) {
      case ObFileScanFormat::CSV: {
        ret = ob_file_text_equals_ci(ext, ext_len, "csv");
        break;
      }
      case ObFileScanFormat::JSON: {
        ret = ob_file_text_equals_ci(ext, ext_len, "json")
              || ob_file_text_equals_ci(ext, ext_len, "jsonl")
              || ob_file_text_equals_ci(ext, ext_len, "ndjson");
        break;
      }
      case ObFileScanFormat::PARQUET: {
        ret = ob_file_text_equals_ci(ext, ext_len, "parquet");
        break;
      }
      default: {
        ret = false;
        break;
      }
    }
  }
  return ret;
}

int ObFileScanUtils::dedup_column_names(common::ObIAllocator &allocator,
                                        common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
    ObFileColumnDef &column = columns.at(i);
    const common::ObString base_name = column.name_;
    common::ObString name = base_name;
    int64_t suffix = 2;
    bool duplicated = true;
    while (duplicated && OB_SUCC(ret)) {
      duplicated = false;
      for (int64_t j = 0; j < i; ++j) {
        if (0 == columns.at(j).name_.case_compare(name)) {
          duplicated = true;
          break;
        }
      }
      if (duplicated) {
        ObSqlString new_name;
        char *buf = NULL;
        if (OB_FAIL(new_name.append_fmt("%s__%ld", base_name.ptr(), suffix))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(new_name.length())))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(buf, new_name.ptr(), new_name.length());
          name.assign_ptr(buf, new_name.length());
          ++suffix;
        }
      }
    }
    if (OB_SUCC(ret)) {
      column.name_ = name;
    }
  }
  return ret;
}

int ObFileScanUtils::infer_schema(const ObFileScanOptions &options,
                                  common::ObIAllocator &allocator,
                                  common::ObIArray<ObFileColumnDef> &columns,
                                  int64_t *row_cnt)
{
  int ret = OB_SUCCESS;
  ObFileScanReader *reader = NULL;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> empty_columns;
  if (OB_FAIL(create_file_scan_reader(options, empty_columns, allocator, reader))) {
    LOG_WARN("cannot create the file reader", K(ret), K(options.path_));
  } else if (OB_FAIL(reader->infer_schema(allocator, columns))) {
    LOG_WARN("cannot infer the schema of the file", K(ret), K(options.path_));
  } else {
    if (OB_NOT_NULL(row_cnt)) {
      *row_cnt = reader->get_row_cnt();
    }
  }
  destroy_file_scan_reader(reader);
  return ret;
}

int ObFileScanReader::infer_schema(common::ObIAllocator &allocator,
                                   common::ObIArray<ObFileColumnDef> &columns)
{
  UNUSED(allocator);
  UNUSED(columns);
  return OB_NOT_SUPPORTED;
}

int create_file_scan_reader(const ObFileScanOptions &options,
                            const common::ObIArray<ObFileColumnDef> &columns,
                            common::ObIAllocator &allocator,
                            ObFileScanReader *&reader)
{
  int ret = OB_SUCCESS;
  reader = NULL;
  switch (options.format_) {
    case ObFileScanFormat::CSV: {
      void *buf = allocator.alloc(sizeof(ObCsvFileScanReader));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        reader = new (buf) ObCsvFileScanReader(options, columns);
      }
      break;
    }
    case ObFileScanFormat::JSON: {
      void *buf = allocator.alloc(sizeof(ObJsonlFileScanReader));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        reader = new (buf) ObJsonlFileScanReader(options, columns);
      }
      break;
    }
    case ObFileScanFormat::PARQUET: {
      void *buf = allocator.alloc(sizeof(ObParquetFileScanReader));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        reader = new (buf) ObParquetFileScanReader(options, columns);
      }
      break;
    }
    case ObFileScanFormat::GLOB: {
      void *buf = allocator.alloc(sizeof(ObGlobFileScanReader));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        reader = new (buf) ObGlobFileScanReader(options, columns);
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("unsupported file format", K(ret), K(options.format_));
      break;
    }
  }
  if (OB_SUCC(ret) && OB_ISNULL(reader)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("cannot allocate the file reader", K(ret));
  }
  return ret;
}

void destroy_file_scan_reader(ObFileScanReader *reader)
{
  if (OB_NOT_NULL(reader)) {
    reader->~ObFileScanReader();
  }
}

} // end namespace sql
} // end namespace oceanbase
