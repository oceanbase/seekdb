/**
 * (c) 2025 OceanBase
 * This file contains utilities for reading local files as tables via SQL.
 * It implements option parsing, deterministic type inference, and change detection.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_scan_utils.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "lib/allocator/ob_malloc.h"
#include "lib/charset/ob_charset.h"
#include "lib/container/ob_se_array.h"
#include "lib/file/ob_file.h"
#include "lib/string/ob_sql_string.h"
#include "sql/engine/basic/ob_file_csv_reader.h"

namespace oceanbase
{
namespace sql
{

using namespace common;

namespace
{

/// compare a string with a literal, ignoring the case
inline bool is_equal_ignore_case(const ObString &value, const char *literal)
{
  const int64_t len = strlen(literal);
  return value.length() == len && 0 == strncasecmp(value.ptr(), literal, len);
}

/// check whether [ptr, ptr+len) contains only decimal digits
bool is_digits(const char *ptr, const int64_t len)
{
  if (NULL == ptr || len <= 0) {
    return false;
  }
  for (int64_t i = 0; i < len; ++i) {
    if (ptr[i] < '0' || ptr[i] > '9') {
      return false;
    }
  }
  return true;
}

/// parse a signed decimal integer.  Returns false when the value is not an
/// integer or does not fit into int64 (the caller then falls back to DOUBLE).
bool parse_int64(const ObString &value, int64_t &out)
{
  bool valid = false;
  if (value.length() > 0 && value.length() < 32) {
    int64_t i = (value.ptr()[0] == '-' || value.ptr()[0] == '+') ? 1 : 0;
    if (i < value.length() && is_digits(value.ptr() + i, value.length() - i)) {
      char buf[32];
      MEMCPY(buf, value.ptr(), value.length());
      buf[value.length()] = '\0';
      char *end = NULL;
      errno = 0;
      const long long parsed = strtoll(buf, &end, 10);
      if (ERANGE != errno && NULL != end && end == buf + value.length()) {
        out = static_cast<int64_t>(parsed);
        valid = true;
      }
    }
  }
  return valid;
}

/// check whether a value is a valid decimal integer (may overflow int64)
bool is_integer_literal(const ObString &value)
{
  if (value.length() == 0) {
    return false;
  }
  int64_t i = (value.ptr()[0] == '-' || value.ptr()[0] == '+') ? 1 : 0;
  return i < value.length() && is_digits(value.ptr() + i, value.length() - i);
}

/// check whether a value is a valid floating point number
bool is_double_literal(const ObString &value)
{
  if (value.length() == 0) {
    return false;
  }
  int64_t i = (value.ptr()[0] == '-' || value.ptr()[0] == '+') ? 1 : 0;
  bool has_digit = false;
  bool has_dot = false;
  bool has_exp = false;
  for (; i < value.length(); i++) {
    const char c = value.ptr()[i];
    if (c >= '0' && c <= '9') {
      has_digit = true;
    } else if (c == '.' && !has_dot && !has_exp) {
      has_dot = true;
    } else if ((c == 'e' || c == 'E') && has_digit && !has_exp) {
      has_exp = true;
      has_digit = false;
    } else {
      return false;
    }
  }
  return has_digit;
}

/// check whether a value is a boolean literal
bool is_boolean_literal(const ObString &value)
{
  return (value.length() == 4 && 0 == strncasecmp(value.ptr(), "true", 4))
         || (value.length() == 5 && 0 == strncasecmp(value.ptr(), "false", 5));
}

inline int two_digits(const char *ptr)
{
  return (ptr[0] - '0') * 10 + (ptr[1] - '0');
}

inline bool is_valid_date_fields(const char *ptr)
{
  const int year = (ptr[0] - '0') * 1000 + (ptr[1] - '0') * 100
                   + (ptr[2] - '0') * 10 + (ptr[3] - '0');
  const int month = two_digits(ptr + 5);
  const int day = two_digits(ptr + 8);
  return year >= 1 && year <= 9999 && month >= 1 && month <= 12
         && day >= 1 && day <= 31;
}

/// check whether a value looks like a date, i.e. YYYY-MM-DD
bool is_date_literal(const ObString &value)
{
  if (value.length() != 10) {
    return false;
  }
  return value.ptr()[4] == '-' && value.ptr()[7] == '-'
         && is_digits(value.ptr(), 4)
         && is_digits(value.ptr() + 5, 2)
         && is_digits(value.ptr() + 8, 2)
         && is_valid_date_fields(value.ptr());
}

/// check whether a value looks like a datetime, i.e. YYYY-MM-DD HH:MM:SS[.ffffff]
bool is_datetime_literal(const ObString &value)
{
  // the separator between the date and the time part may be a space or a 'T'
  if (value.length() < 19) {
    return false;
  }
  const char sep = value.ptr()[10];
  const int64_t extra = value.length() - 19;
  if ((sep != ' ' && sep != 'T')
      || !is_date_literal(ObString(10, value.ptr()))
      || !is_digits(value.ptr() + 11, 2)
      || value.ptr()[13] != ':'
      || !is_digits(value.ptr() + 14, 2)
      || value.ptr()[16] != ':'
      || !is_digits(value.ptr() + 17, 2)) {
    return false;
  }
  if (extra > 0) {
    if (extra > 7 || value.ptr()[19] != '.'
        || !is_digits(value.ptr() + 20, extra - 1)) {
      return false;
    }
  }
  const int hour = two_digits(value.ptr() + 11);
  const int minute = two_digits(value.ptr() + 14);
  const int second = two_digits(value.ptr() + 17);
  return hour <= 23 && minute <= 59 && second <= 60;
}

/// find a named option in the argument list
int find_named_arg(const ObIArray<ObFileSqlArg> &args, const char *name, const ObFileSqlArg *&arg)
{
  int ret = OB_SUCCESS;
  arg = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < args.count(); i++) {
    if (args.at(i).name_.length() > 0 && is_equal_ignore_case(args.at(i).name_, name)) {
      if (NULL != arg) {
        ret = OB_ERR_PARSE_SQL;
        LOG_USER_ERROR(OB_ERR_PARSE_SQL, "duplicate option");
      } else {
        arg = &args.at(i);
      }
    }
  }
  return ret;
}

/// copy a string option value into the allocator
int dup_string_arg(const ObFileSqlArg &arg, ObIAllocator &allocator, ObString &value)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (arg.str_value_.length() > 0) {
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(arg.str_value_.length())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buf, arg.str_value_.ptr(), arg.str_value_.length());
      value.assign(buf, arg.str_value_.length());
    }
  }
  return ret;
}

/// resolve the compression option
int parse_compression_arg(const ObString &value, ObCSVGeneralFormat::ObCSVCompression &compression)
{
  int ret = OB_SUCCESS;
  if (is_equal_ignore_case(value, "none")) {
    compression = ObCSVGeneralFormat::ObCSVCompression::NONE;
  } else if (is_equal_ignore_case(value, "gzip")) {
    compression = ObCSVGeneralFormat::ObCSVCompression::GZIP;
  } else if (is_equal_ignore_case(value, "deflate")) {
    compression = ObCSVGeneralFormat::ObCSVCompression::DEFLATE;
  } else if (is_equal_ignore_case(value, "zstd")) {
    compression = ObCSVGeneralFormat::ObCSVCompression::ZSTD;
  } else if (is_equal_ignore_case(value, "auto")) {
    compression = ObCSVGeneralFormat::ObCSVCompression::AUTO;
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, value.length(), value.ptr());
  }
  return ret;
}

/// resolve the encoding option
int parse_encoding_arg(const ObString &value, ObCharsetType &cs_type)
{
  int ret = OB_SUCCESS;
  const ObCharsetType type = ObCharset::charset_type(value.ptr());
  if (CHARSET_INVALID == type) {
    ret = OB_ERR_UNKNOWN_CHARSET;
    LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, value.length(), value.ptr());
  } else {
    cs_type = type;
  }
  return ret;
}

} // namespace

int ObFileSqlUtils::get_format_type(const ObString &func_name, ObFileScanFormat &type)
{
  int ret = OB_SUCCESS;
  if (is_equal_ignore_case(func_name, "read_csv")) {
    type = ObFileScanFormat::CSV;
  } else if (is_equal_ignore_case(func_name, "read_json") || is_equal_ignore_case(func_name, "read_ndjson")) {
    type = ObFileScanFormat::JSON;
  } else if (is_equal_ignore_case(func_name, "read_parquet")) {
    type = ObFileScanFormat::PARQUET;
  } else if (is_equal_ignore_case(func_name, "glob")) {
    type = ObFileScanFormat::GLOB;
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, func_name.length(), func_name.ptr());
  }
  return ret;
}

int ObFileSqlUtils::build_csv_format(const ObIArray<ObFileSqlArg> &args,
                                     ObIAllocator &allocator,
                                     ObCSVGeneralFormat &csv_format)
{
  int ret = OB_SUCCESS;
  const ObFileSqlArg *arg = NULL;
  // DuckDB defaults: comma separated, double quoted fields, header row present.
  // The backslash escape is disabled by default, standard CSV uses doubled
  // quotes ("") to escape a quote character.
  csv_format.field_term_str_ = ObString(1, ",");
  csv_format.line_term_str_ = ObString(1, "\n");
  csv_format.field_enclosed_char_ = '"';
  csv_format.field_escaped_char_ = INT64_MAX;
  csv_format.cs_type_ = CHARSET_UTF8MB4;
  csv_format.empty_field_as_null_ = true;
  csv_format.skip_blank_lines_ = true;
  csv_format.parse_header_ = true;
  csv_format.skip_header_lines_ = 1;

  if (OB_FAIL(find_named_arg(args, "header", arg))) {
  } else if (NULL != arg) {
    csv_format.parse_header_ = (arg->kind_ == ObFileSqlArg::BOOL) ? arg->int_value_ != 0 : false;
    csv_format.skip_header_lines_ = csv_format.parse_header_ ? 1 : 0;
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "delimiter", arg))) {
  } else if (NULL != arg) {
    if (OB_FAIL(dup_string_arg(*arg, allocator, csv_format.field_term_str_))) {
    } else if (csv_format.field_term_str_.length() != 1) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "delimiter must be a single character");
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "quote", arg))) {
  } else if (NULL != arg) {
    ObString quote;
    if (OB_FAIL(dup_string_arg(*arg, allocator, quote))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      csv_format.field_enclosed_char_ = quote.length() == 1 ? quote.ptr()[0] : INT64_MAX;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "escape", arg))) {
  } else if (NULL != arg) {
    ObString escape;
    if (OB_FAIL(dup_string_arg(*arg, allocator, escape))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      csv_format.field_escaped_char_ = escape.length() == 1 ? escape.ptr()[0] : INT64_MAX;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "null", arg))) {
  } else if (NULL != arg) {
    ObString null_str;
    if (OB_FAIL(dup_string_arg(*arg, allocator, null_str))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(csv_format.null_if_.allocate_array(allocator, 1))) {
    } else {
      csv_format.null_if_.at(0) = null_str;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "compression", arg))) {
  } else if (NULL != arg) {
    if (OB_FAIL(parse_compression_arg(arg->str_value_, csv_format.compression_algorithm_))) {
    } else if (csv_format.compression_algorithm_ == ObCSVGeneralFormat::ObCSVCompression::AUTO) {
      csv_format.compression_algorithm_ = ObCSVGeneralFormat::ObCSVCompression::NONE;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "encoding", arg))) {
  } else if (NULL != arg) {
    if (OB_FAIL(parse_encoding_arg(arg->str_value_, csv_format.cs_type_))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "skip_blank_lines", arg))) {
  } else if (NULL != arg) {
    csv_format.skip_blank_lines_ = (arg->kind_ == ObFileSqlArg::BOOL) ? arg->int_value_ != 0 : false;
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "trim_space", arg))) {
  } else if (NULL != arg) {
    csv_format.trim_space_ = (arg->kind_ == ObFileSqlArg::BOOL) ? arg->int_value_ != 0 : false;
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "sample_size", arg))) {
  } else if (NULL != arg) {
    // only a full scan is supported, so the sample size must be -1
    if (arg->kind_ != ObFileSqlArg::INT || arg->int_value_ != -1) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "sample_size");
    }
  }
  return ret;
}

int ObFileSqlUtils::build_json_format(const ObIArray<ObFileSqlArg> &args,
                                      ObIAllocator &allocator,
                                      ObCSVGeneralFormat &json_format)
{
  int ret = OB_SUCCESS;
  const ObFileSqlArg *arg = NULL;
  json_format.line_term_str_ = ObString(1, "\n");
  json_format.cs_type_ = CHARSET_UTF8MB4;

  if (OB_FAIL(find_named_arg(args, "sample_size", arg))) {
  } else if (NULL != arg) {
    // only a full scan is supported, so the sample size must be -1
    if (arg->kind_ != ObFileSqlArg::INT || arg->int_value_ != -1) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "sample_size");
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "compression", arg))) {
  } else if (NULL != arg) {
    if (OB_FAIL(parse_compression_arg(arg->str_value_, json_format.compression_algorithm_))) {
    } else if (json_format.compression_algorithm_ == ObCSVGeneralFormat::ObCSVCompression::AUTO) {
      json_format.compression_algorithm_ = ObCSVGeneralFormat::ObCSVCompression::NONE;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "encoding", arg))) {
  } else if (NULL != arg) {
    if (OB_FAIL(parse_encoding_arg(arg->str_value_, json_format.cs_type_))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(find_named_arg(args, "format", arg))) {
  } else if (NULL != arg) {
    if (!is_equal_ignore_case(arg->str_value_, "newline_delimited") && !is_equal_ignore_case(arg->str_value_, "array")) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, arg->str_value_.length(), arg->str_value_.ptr());
    }
  }
  return ret;
}

bool ObFileSqlUtils::ColInfer::feed_field(const ObString &value, const ObCharsetType cs_type)
{
  (void)cs_type;
  const State old_state = state_;
  if (value.length() == 0) {
    // a NULL value does not narrow the type
    return false;
  }
  int64_t int_value = 0;
  const bool fits_int64 = parse_int64(value, int_value);
  if (state_ == ST_NULL) {
    if (is_boolean_literal(value)) {
      state_ = ST_BOOLEAN;
    } else if (fits_int64) {
      state_ = ST_INT;
      bigint_min_ = std::min(bigint_min_, int_value);
      bigint_max_ = std::max(bigint_max_, int_value);
    } else if (is_integer_literal(value)) {
      // an integer literal that overflows int64 can only be represented as DOUBLE
      state_ = ST_DOUBLE;
    } else if (is_double_literal(value)) {
      state_ = ST_DOUBLE;
    } else if (is_date_literal(value)) {
      state_ = ST_DATE;
    } else if (is_datetime_literal(value)) {
      state_ = ST_DATETIME;
    } else {
      state_ = ST_VARCHAR;
      max_varchar_len_ = value.length();
    }
  } else if (state_ == ST_BOOLEAN) {
    // a boolean mixed with anything else becomes a string
    if (!is_boolean_literal(value)) {
      state_ = ST_VARCHAR;
      max_varchar_len_ = value.length();
    }
  } else if (state_ == ST_INT) {
    if (fits_int64) {
      bigint_min_ = std::min(bigint_min_, int_value);
      bigint_max_ = std::max(bigint_max_, int_value);
    } else if (is_integer_literal(value) || is_double_literal(value)) {
      state_ = ST_DOUBLE;
    } else {
      state_ = ST_VARCHAR;
      max_varchar_len_ = value.length();
    }
  } else if (state_ == ST_DOUBLE) {
    if (!is_double_literal(value)) {
      state_ = ST_VARCHAR;
      max_varchar_len_ = value.length();
    }
  } else if (state_ == ST_DATE) {
    if (is_date_literal(value)) {
      // stays a date
    } else if (is_datetime_literal(value)) {
      state_ = ST_DATETIME;
    } else {
      state_ = ST_VARCHAR;
      max_varchar_len_ = value.length();
    }
  } else if (state_ == ST_DATETIME) {
    // a date-only value is still representable as a datetime
    if (!is_datetime_literal(value) && !is_date_literal(value)) {
      state_ = ST_VARCHAR;
      max_varchar_len_ = value.length();
    }
  } else if (state_ == ST_VARCHAR) {
    max_varchar_len_ = std::max(max_varchar_len_, value.length());
  }
  return old_state != state_;
}

void ObFileSqlUtils::ColInfer::finish(ObObjType &data_type,
                                      int32_t &max_varchar_len,
                                      ObCollationType &collation_type)
{
  collation_type = CS_TYPE_UTF8MB4_BIN;
  switch (state_) {
    case ST_NULL:
      data_type = ObNullType;
      max_varchar_len = 0;
      break;
    case ST_BOOLEAN:
      data_type = ObTinyIntType;
      max_varchar_len = 0;
      break;
    case ST_INT:
      // ObIntType is a 64 bit integer in oceanbase, its sql type name is BIGINT
      data_type = ObIntType;
      max_varchar_len = 0;
      break;
    case ST_DOUBLE:
      data_type = ObDoubleType;
      max_varchar_len = 0;
      break;
    case ST_DATE:
      data_type = ObDateType;
      max_varchar_len = 0;
      break;
    case ST_DATETIME:
      // keep the wall clock value as DATETIME instead of TIMESTAMP so that no
      // time zone conversion is applied when the value is read back.
      data_type = ObDateTimeType;
      max_varchar_len = 0;
      break;
    case ST_VARCHAR:
    default:
      data_type = ObVarcharType;
      max_varchar_len = std::max(max_varchar_len_, 1);
      break;
  }
}

namespace
{

/// build a unique column name: empty header fields become columnN and
/// duplicate names get a __N suffix (id, id -> id, id__2).
int build_unique_column_name(const ObString &raw_name,
                             const int64_t column_idx,
                             common::ObIAllocator &allocator,
                             const common::ObIArray<ObFileScanColMeta> &col_metas,
                             ObString &name)
{
  int ret = OB_SUCCESS;
  ObSqlString candidate;
  ObString base = raw_name;
  if (base.empty()) {
    if (OB_FAIL(candidate.append_fmt("column%ld", column_idx))) {
      LOG_WARN("failed to build column name", K(ret), K(column_idx));
    }
  } else if (OB_FAIL(candidate.assign(base))) {
    LOG_WARN("failed to build column name", K(ret));
  }
  int64_t suffix = 2;
  bool duplicated = true;
  while (OB_SUCC(ret) && duplicated) {
    duplicated = false;
    for (int64_t i = 0; !duplicated && i < col_metas.count(); ++i) {
      duplicated = (col_metas.at(i).name_ == ObString(candidate.length(), candidate.ptr()));
    }
    if (OB_SUCC(ret) && duplicated) {
      candidate.reuse();
      if (base.empty()) {
        ret = candidate.append_fmt("column%ld__%ld", column_idx, suffix++);
      } else {
        ret = candidate.append_fmt("%.*s__%ld", base.length(), base.ptr(), suffix++);
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("failed to build unique column name", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ob_write_string(allocator,
                                ObString(candidate.length(), candidate.ptr()),
                                name))) {
      LOG_WARN("failed to copy column name", K(ret));
    }
  }
  return ret;
}

/// stream the CSV file once and infer every column type.
int infer_csv_column_schemas(const ObString &file_url,
                             const ObCSVGeneralFormat &format_opt,
                             common::ObIAllocator &allocator,
                             common::ObIArray<ObFileScanColMeta> &col_metas,
                             int64_t *row_cnt)
{
  int ret = OB_SUCCESS;
  ObFileCsvReader reader;
  ObSEArray<ObFileCsvField, 16> fields;
  ObSEArray<ObFileSqlUtils::ColInfer, 16> infer_states;
  ObSEArray<bool, 16> nullable_flags;
  int64_t column_cnt = 0;
  int64_t record_cnt = 0;
  if (OB_FAIL(reader.open(file_url, format_opt, allocator))) {
    LOG_WARN("failed to open csv file", K(ret), K(file_url));
  } else {
    // the header names, if any, fix the column count up front
    const ObIArray<ObString> &header = reader.get_header();
    if (reader.has_header() && header.count() > 0) {
      column_cnt = header.count();
      for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt; ++i) {
        ObFileScanColMeta meta;
        if (OB_FAIL(build_unique_column_name(header.at(i), i, allocator, col_metas, meta.name_))) {
          LOG_WARN("failed to build column name", K(ret), K(i));
        } else if (OB_FAIL(col_metas.push_back(meta))) {
          LOG_WARN("failed to add column meta", K(ret));
        } else if (OB_FAIL(infer_states.push_back(ObFileSqlUtils::ColInfer()))) {
          LOG_WARN("failed to add inference state", K(ret));
        } else if (OB_FAIL(nullable_flags.push_back(false))) {
          LOG_WARN("failed to add nullable flag", K(ret));
        }
      }
    }
  }

  while (OB_SUCC(ret)) {
    fields.reuse();
    if (OB_FAIL(reader.next_record(fields))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else if (OB_ERR_UNEXPECTED == ret) {
        char msg[128];
        snprintf(msg, sizeof(msg), "malformed CSV record at line %ld",
                 static_cast<long>(reader.get_record_no()));
        LOG_WARN("malformed csv record", K(ret), "record_no", reader.get_record_no());
        LOG_USER_ERROR(OB_ERR_UNEXPECTED, msg);
      } else {
        LOG_WARN("failed to read csv record", K(ret));
      }
    } else if (0 == column_cnt) {
      // no header: derive the column count from the first data record
      column_cnt = fields.count();
      for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt; ++i) {
        ObFileScanColMeta meta;
        if (OB_FAIL(build_unique_column_name(ObString(), i, allocator, col_metas, meta.name_))) {
          LOG_WARN("failed to build column name", K(ret), K(i));
        } else if (OB_FAIL(col_metas.push_back(meta))) {
          LOG_WARN("failed to add column meta", K(ret));
        } else if (OB_FAIL(infer_states.push_back(ObFileSqlUtils::ColInfer()))) {
          LOG_WARN("failed to add inference state", K(ret));
        } else if (OB_FAIL(nullable_flags.push_back(false))) {
          LOG_WARN("failed to add nullable flag", K(ret));
        }
      }
    } else if (fields.count() != column_cnt) {
      ret = OB_ERR_WRONG_VALUE_COUNT_ON_ROW;
      LOG_WARN("csv record has a wrong number of fields", K(ret),
               K(column_cnt), K(fields.count()), "record_no", reader.get_record_no());
      LOG_USER_ERROR(OB_ERR_WRONG_VALUE_COUNT_ON_ROW, static_cast<int32_t>(reader.get_record_no()));
    }

    ++record_cnt;
    for (int64_t i = 0; OB_SUCC(ret) && i < fields.count(); ++i) {
      const ObFileCsvField &field = fields.at(i);
      if (field.is_null_) {
        nullable_flags.at(i) = true;
      } else {
        infer_states.at(i).feed_field(ObString(field.len_, field.ptr_), format_opt.cs_type_);
      }
    }
  }

  if (OB_FAIL(ret)) {
    // nothing to do
  } else if (0 == column_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("csv file has no readable columns", K(ret), K(file_url));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt; ++i) {
      ObObjType data_type = ObNullType;
      int32_t max_varchar_len = 0;
      ObCollationType collation_type = CS_TYPE_UTF8MB4_BIN;
      infer_states.at(i).finish(data_type, max_varchar_len, collation_type);
      if (ObNullType == data_type) {
        // a column with only NULL values is exposed as VARCHAR
        data_type = ObVarcharType;
        max_varchar_len = 1;
      }
      col_metas.at(i).type_ = data_type;
      col_metas.at(i).length_ = max_varchar_len;
      col_metas.at(i).nullable_ = nullable_flags.at(i);
    }
  }
  if (OB_SUCC(ret) && NULL != row_cnt) {
    *row_cnt = record_cnt;
  }
  reader.close();
  return ret;
}

} // namespace

int ObFileSqlUtils::infer_column_schemas(const ObString &file_url,
                                         ObFileScanFormat format,
                                         const ObCSVGeneralFormat &format_opt,
                                         common::ObIAllocator &allocator,
                                         common::ObIArray<ObFileScanColMeta> &col_metas,
                                         int64_t *row_cnt)
{
  int ret = OB_SUCCESS;
  if (ObFileScanFormat::CSV == format) {
    ret = infer_csv_column_schemas(file_url, format_opt, allocator, col_metas, row_cnt);
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "infer schema for this file format");
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
