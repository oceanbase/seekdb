/**
 * (c) 2025 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_table_scan_op.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common/timezone/ob_time_convert.h"
#include "lib/utility/ob_utility.h"
#include "sql/engine/basic/ob_file_csv_reader.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

int64_t to_int64(const ObString &str, bool &ok)
{
  int64_t value = 0;
  ok = false;
  if (OB_LIKELY(!str.empty())) {
    const char *begin = str.ptr();
    const char *end = str.ptr() + str.length();
    bool negative = false;
    if ('-' == *begin || '+' == *begin) {
      negative = ('-' == *begin);
      ++begin;
    }
    if (begin < end) {
      bool valid = true;
      for (const char *p = begin; valid && p < end; ++p) {
        if (*p < '0' || *p > '9') {
          valid = false;
        } else if (value > (INT64_MAX - (*p - '0')) / 10) {
          valid = false;
        } else {
          value = value * 10 + (*p - '0');
        }
      }
      if (valid) {
        ok = true;
        value = negative ? -value : value;
      }
    }
  }
  return value;
}

/// convert a boolean literal, true/false/t/f (case insensitive) and 0/1 are
/// accepted, which is exactly what the schema inference accepts as BOOLEAN.
bool to_bool(const ObString &str, bool &value)
{
  bool converted = true;
  if (str.case_compare_equal("true") || str.case_compare_equal("t")) {
    value = true;
  } else if (str.case_compare_equal("false") || str.case_compare_equal("f")) {
    value = false;
  } else {
    bool ok = false;
    const int64_t int_value = to_int64(str, ok);
    if (ok && (0 == int_value || 1 == int_value)) {
      value = (0 != int_value);
    } else {
      converted = false;
    }
  }
  return converted;
}

/// strtod requires a NUL terminated string while a csv field is a slice
int to_double(const ObString &str, ObIAllocator &allocator, double &value)
{
  int ret = OB_SUCCESS;
  const int64_t len = str.length();
  char stack_buf[128];
  char *buf = stack_buf;
  if (len + 1 > static_cast<int64_t>(sizeof(stack_buf))) {
    if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(len + 1)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate double buffer", K(ret), K(len));
    }
  }
  if (OB_SUCC(ret)) {
    MEMCPY(buf, str.ptr(), len);
    buf[len] = '\0';
    char *end = NULL;
    errno = 0;
    value = strtod(buf, &end);
    if (OB_UNLIKELY(end != buf + len || 0 != errno)) {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid double field", K(ret), K(str));
    }
  }
  return ret;
}

} // namespace

ObFileTableScanSpec::ObFileTableScanSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
  : ObOpSpec(alloc, type),
    column_exprs_(alloc),
    column_indexs_(alloc),
    file_path_(),
    format_(0),
    column_count_(0),
    row_count_(0),
    file_size_(-1),
    file_mtime_(-1),
    field_term_(),
    field_enclosed_char_(INT64_MAX),
    field_escaped_char_(INT64_MAX),
    null_str_(),
    cs_type_(CHARSET_UTF8MB4),
    skip_header_lines_(0),
    parse_header_(false),
    skip_blank_lines_(true),
    trim_space_(false),
    empty_field_as_null_(true)
{
}

ObFileTableScanOp::ObFileTableScanOp(ObExecContext &exec_ctx,
                                     const ObOpSpec &spec,
                                     ObOpInput *input)
  : ObOperator(exec_ctx, spec, input),
    csv_format_(),
    fields_(),
    str_alloc_()
{
}

ObFileTableScanOp::~ObFileTableScanOp()
{
  destroy();
}

void ObFileTableScanOp::destroy()
{
  reader_.close();

  fields_.reset();
  str_alloc_.reset();
  ObOperator::destroy();
}

int ObFileTableScanOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_open())) {
    LOG_WARN("failed to inner open operator", K(ret));
  } else if (OB_UNLIKELY(ObFileScanFormat::CSV != static_cast<ObFileScanFormat>(MY_SPEC.format_))) {
    // OB-P:a4L9E_IDIyEM2Ol_ZvDXqw
    // the other file formats are executed by a later stage
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "this file format");
    LOG_WARN("unsupported file format", K(ret), K(MY_SPEC.format_));
  } else if (OB_FAIL(build_csv_format(get_exec_ctx().get_allocator()))) {
    LOG_WARN("failed to build csv format", K(ret));
  } else if (OB_FAIL(check_file_unchanged(MY_SPEC.file_size_, MY_SPEC.file_mtime_))) {
    LOG_WARN("file has been changed after resolve", K(ret));
  } else if (OB_FAIL(reader_.open(MY_SPEC.file_path_, csv_format_,
                                  get_exec_ctx().get_allocator()))) {
    LOG_WARN("failed to open csv file", K(ret), K(MY_SPEC.file_path_));
  }
  return ret;
}

int ObFileTableScanOp::build_csv_format(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 == MY_SPEC.file_path_.length())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file path is empty", K(ret));
  } else {
    csv_format_.field_term_str_ = MY_SPEC.field_term_;
    csv_format_.field_enclosed_char_ = MY_SPEC.field_enclosed_char_;
    csv_format_.field_escaped_char_ = MY_SPEC.field_escaped_char_;
    csv_format_.cs_type_ = static_cast<ObCharsetType>(MY_SPEC.cs_type_);
    csv_format_.skip_header_lines_ = MY_SPEC.skip_header_lines_;
    csv_format_.parse_header_ = MY_SPEC.parse_header_;
    csv_format_.skip_blank_lines_ = MY_SPEC.skip_blank_lines_;
    csv_format_.trim_space_ = MY_SPEC.trim_space_;
    csv_format_.empty_field_as_null_ = MY_SPEC.empty_field_as_null_;
    if (!MY_SPEC.null_str_.empty()) {
      // null_if_ is a non-owning view; the reader copies the first entry
      if (OB_FAIL(csv_format_.null_if_.allocate_array(allocator, 1))) {
        LOG_WARN("failed to allocate null_if", K(ret));
      } else {
        csv_format_.null_if_.at(0) = MY_SPEC.null_str_;
      }
    }
  }
  return ret;
}

int ObFileTableScanOp::check_file_unchanged(const int64_t file_size,
                                            const int64_t file_mtime)
{
  int ret = OB_SUCCESS;
  struct stat file_stat;
  MEMSET(&file_stat, 0, sizeof(file_stat));
  const int64_t path_len = MY_SPEC.file_path_.length();
  if (OB_UNLIKELY(0 == path_len || path_len > PATH_MAX)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid file path", K(ret), K(MY_SPEC.file_path_));
  } else {
    // the deep copied path is not null terminated, build a c string for stat
    char path_buf[PATH_MAX + 1];
    MEMCPY(path_buf, MY_SPEC.file_path_.ptr(), path_len);
    path_buf[path_len] = '\0';
    if (0 != ::stat(path_buf, &file_stat)) {
      ret = OB_FILE_NOT_EXIST;
      LOG_WARN("failed to stat file", K(ret), K(MY_SPEC.file_path_));
    } else if (file_stat.st_size != file_size ||
               static_cast<int64_t>(file_stat.st_mtime) != file_mtime) {
      ret = OB_TABLE_DEFINITION_CHANGED;
      LOG_WARN("file has been modified after the schema was inferred", K(ret),
               K(MY_SPEC.file_path_), K(file_size), "current_size", file_stat.st_size,
               K(file_mtime), "current_mtime", file_stat.st_mtime);
    }
  }
  return ret;
}

int ObFileTableScanOp::convert_field(const ObFileCsvField &field,
                                     const ObObjType type,
                                     ObDatum &datum)
{
  int ret = OB_SUCCESS;
  if (field.is_null_) {
    datum.set_null();
  } else if (OB_UNLIKELY(field.len_ == 0)) {
    if (MY_SPEC.empty_field_as_null_) {
      datum.set_null();
    } else {
      ret = OB_INVALID_DATA;
      LOG_WARN("empty field can not be converted", K(ret));
    }
  } else {
    const ObString value(field.len_, field.ptr_);
    switch (type) {
      case ObTinyIntType: {
        bool bool_value = false;
        if (to_bool(value, bool_value)) {
          datum.set_bool(bool_value);
        } else {
          bool ok = false;
          const int64_t int_value = to_int64(value, ok);
          if (OB_UNLIKELY(!ok)) {
            ret = OB_INVALID_DATA;
            LOG_WARN("invalid boolean field", K(ret), K(value));
          } else {
            datum.set_int(int_value);
          }
        }
        break;
      }
      case ObSmallIntType:
      case ObMediumIntType:
      case ObIntType:
      case ObUTinyIntType:
      case ObUSmallIntType:
      case ObUMediumIntType:
      case ObUInt32Type:
      case ObUInt64Type: {
        bool ok = false;
        const int64_t int_value = to_int64(value, ok);
        if (OB_UNLIKELY(!ok)) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid integer field", K(ret), K(value));
        } else {
          datum.set_int(int_value);
        }
        break;
      }
      case ObDoubleType: {
        double double_value = 0.0;
        if (OB_FAIL(to_double(value, str_alloc_, double_value))) {
          LOG_WARN("invalid double field", K(ret), K(value));
        } else {
          datum.set_double(double_value);
        }
        break;
      }
      case ObDateType: {
        int32_t date_value = 0;
        if (OB_FAIL(ObTimeConverter::str_to_date(value, date_value))) {
          LOG_WARN("invalid date field", K(ret), K(value));
        } else {
          datum.set_date(date_value);
        }
        break;
      }
      case ObDateTimeType: {
        int64_t datetime_value = 0;
        const ObTimeConvertCtx cvrt_ctx(NULL, false, false);
        const ObDateSqlMode date_sql_mode(0);
        if (OB_FAIL(ObTimeConverter::str_to_datetime(value, cvrt_ctx, datetime_value,
                                                     NULL /* scale */, date_sql_mode))) {
          LOG_WARN("invalid datetime field", K(ret), K(value));
        } else {
          datum.set_datetime(datetime_value);
        }
        break;
      }
      case ObVarcharType:
      case ObCharType:
      case ObLongTextType:
      case ObMediumTextType:
      case ObTextType:
      case ObTinyTextType: {
        char *buf = NULL;
        if (OB_ISNULL(buf = static_cast<char *>(str_alloc_.alloc(value.length())))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate string buffer", K(ret), K(value.length()));
        } else {
          MEMCPY(buf, value.ptr(), value.length());
          datum.set_string(buf, value.length());
        }
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("unsupported column type for the file table", K(ret),
                 "type", ob_obj_type_str(type));
        break;
      }
    }
  }
  return ret;
}

int ObFileTableScanOp::fill_row()
{
  int ret = OB_SUCCESS;
  const ObFileTableScanSpec &spec = static_cast<const ObFileTableScanSpec &>(spec_);
  for (int64_t i = 0; OB_SUCC(ret) && i < spec.column_exprs_.count(); ++i) {
    ObExpr *expr = spec.column_exprs_.at(i);
    const int64_t field_idx = spec.column_indexs_.at(i);
    if (OB_ISNULL(expr) || OB_UNLIKELY(field_idx < 0 || field_idx >= fields_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid file table scan column", K(ret), K(i), K(field_idx));
    } else {
      // The ObDatum value setters (set_int/set_double/set_date/...) store the
      // value *through* %ptr_, so the pointer has to be pointed at the reserved
      // buffer of the expression first.  locate_datum_for_write() does that,
      // locate_batch_datums() does not.
      ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
      if (OB_FAIL(convert_field(fields_.at(field_idx), expr->datum_meta_.type_, datum))) {
        LOG_WARN("failed to convert csv field", K(ret), K(i), K(field_idx));
      }
    }
  }
  return ret;
}

int ObFileTableScanOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  const ObFileTableScanSpec &spec = static_cast<const ObFileTableScanSpec &>(spec_);
  // the string values of the previous row are not referenced any more, release
  // them so that the whole file is never materialized in memory
  str_alloc_.reset_remain_one_page();
  clear_evaluated_flag();
  fields_.reuse();
  if (OB_UNLIKELY(0 == spec.column_count_)) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(reader_.next_record(fields_))) {
    if (OB_ITER_END == ret) {
      // OB_ITER_END is the expected way to signal the end of a row iterator
    } else {
      LOG_WARN("failed to read csv record", K(ret), K(reader_.get_record_no()));
    }
  } else if (OB_UNLIKELY(fields_.count() != spec.column_count_)) {
    ret = OB_INVALID_DATA;
    LOG_WARN("csv record has an unexpected number of fields", K(ret),
             "record_no", reader_.get_record_no(),
             "expected", spec.column_count_, "actual", fields_.count());
  } else if (OB_FAIL(fill_row())) {
    LOG_WARN("failed to fill csv record", K(ret));
  }
  return ret;
}

int ObFileTableScanOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("failed to inner rescan", K(ret));
  } else if (OB_FAIL(check_file_unchanged(MY_SPEC.file_size_, MY_SPEC.file_mtime_))) {
    LOG_WARN("file has been changed after resolve", K(ret));
  } else {
    str_alloc_.reset();
    fields_.reuse();
    if (OB_FAIL(reader_.reset())) {
      LOG_WARN("failed to reset csv reader", K(ret));
    }
  }
  return ret;
}

int ObFileTableScanOp::inner_close()
{
  int ret = OB_SUCCESS;
  reader_.close();

  fields_.reset();
  str_alloc_.reset();
  if (OB_FAIL(ObOperator::inner_close())) {
    LOG_WARN("failed to inner close operator", K(ret));
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObFileTableScanSpec, ObOpSpec),
                    column_exprs_,
                    column_indexs_,
                    file_path_,
                    format_,
                    column_count_,
                    row_count_,
                    file_size_,
                    file_mtime_,
                    field_term_,
                    field_enclosed_char_,
                    field_escaped_char_,
                    null_str_,
                    cs_type_,
                    skip_header_lines_,
                    parse_header_,
                    skip_blank_lines_,
                    trim_space_,
                    empty_field_as_null_);

} // namespace sql
} // namespace oceanbase
