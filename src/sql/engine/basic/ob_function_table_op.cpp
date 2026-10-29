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

#include "sql/engine/basic/ob_function_table_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/file_source/file_source_reader.h"
#include "common/timezone/ob_time_convert.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace oceanbase
{
using namespace common;
namespace sql
{

/// Runtime context of the file table function. The class is defined in this
/// file so that the STL types it holds do not leak into the operator header.
class ObFileSourceCtx
{
public:
  ObFileSourceCtx() : reader(NULL), describe_index_(0), describe_rows_() {}
  ~ObFileSourceCtx() { reset(); }
  void reset()
  {
    if (NULL != reader) {
      delete reader;
      reader = NULL;
    }
    describe_rows_.clear();
    describe_index_ = 0;
  }
public:
  file_source::FileSourceReader *reader;
  int64_t describe_index_;
  std::vector<std::vector<std::string> > describe_rows_;
};

static int file_source_error_to_errno(const file_source::FileSourceError &error)
{
  int ret = OB_ERR_UNEXPECTED;
  switch (error.code()) {
    case file_source::FileErrorCode::FILE_NOT_FOUND:
      ret = OB_FILE_NOT_EXIST;
      break;
    case file_source::FileErrorCode::PATH_NOT_ALLOWED:
      ret = OB_ERR_NO_PRIVILEGE;
      break;
    case file_source::FileErrorCode::UNSUPPORTED_FORMAT:
    case file_source::FileErrorCode::NOT_SUPPORTED:
      ret = OB_NOT_SUPPORTED;
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      break;
  }
  return ret;
}

static std::string file_source_upper(const std::string &text)
{
  std::string result(text);
  for (size_t i = 0; i < result.size(); ++i) {
    result[i] = static_cast<char>(::toupper(static_cast<unsigned char>(result[i])));
  }
  return result;
}

/// Parse an integer literal of the file. The boolean values true/false are
/// accepted as well because the csv/json files store them as text.
static int file_source_parse_int(const char *value, int32_t len, int64_t &result)
{
  int ret = OB_SUCCESS;
  std::string text(value, len);
  std::string lower_text;
  lower_text.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    lower_text.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(text[i]))));
  }
  if ("true" == lower_text || "t" == lower_text) {
    result = 1;
  } else if ("false" == lower_text || "f" == lower_text) {
    result = 0;
  } else {
    char *endptr = NULL;
    errno = 0;
    long long int_value = strtoll(text.c_str(), &endptr, 10);
    if (NULL == endptr || endptr == text.c_str() || '\0' != *endptr) {
      ret = OB_ERR_UNEXPECTED;
      LOG_USER_ERROR(OB_ERR_UNEXPECTED, "invalid integer value '%s' in file", text.c_str());
    } else {
      result = static_cast<int64_t>(int_value);
    }
  }
  return ret;
}

static int file_source_parse_double(const char *value, int32_t len, double &result)
{
  int ret = OB_SUCCESS;
  std::string text(value, len);
  char *endptr = NULL;
  errno = 0;
  double double_value = strtod(text.c_str(), &endptr);
  if (NULL == endptr || endptr == text.c_str() || '\0' != *endptr) {
    ret = OB_ERR_UNEXPECTED;
    LOG_USER_ERROR(OB_ERR_UNEXPECTED, "invalid numeric value '%s' in file", text.c_str());
  } else {
    result = double_value;
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObFunctionTableSpec, ObOpSpec), value_expr_, column_exprs_, has_correlated_expr_,
                    file_path_, file_format_, file_has_header_, file_sample_size_, file_delimiter_,
                    file_describe_, file_null_string_);

int ObFunctionTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  already_calc_ = false;
  if (!MY_SPEC.file_path_.empty()) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_file;
    ret = open_file_source();
  } else if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ObExtendType == MY_SPEC.value_expr_->datum_meta_.type_) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_udf;
  } else {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_sys_func;
  }
  return ret;
}

int ObFunctionTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
  } else {
    node_idx_ = 0;
    if (!MY_SPEC.file_path_.empty()) {
      // rewind the file scan
      ret = open_file_source();
    } else if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_close()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  row_count_ = 0;
  col_count_ = 0;
  value_table_ = NULL;
  if (OB_NOT_NULL(file_ctx_)) {
    file_ctx_->reset();
  }
  return ret;
}

void ObFunctionTableOp::destroy()
{
  if (OB_NOT_NULL(file_ctx_)) {
    delete file_ctx_;
    file_ctx_ = NULL;
  }
  ObOperator::destroy();
}


int ObFunctionTableOp::get_current_result(ObObj &result)
{
  int ret = OB_SUCCESS;
  void *data = NULL;
  CK (already_calc_);
  if (node_idx_ < 0 || node_idx_ >= row_count_ * col_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get current result in table function",
              K(node_idx_), K(row_count_), K(col_count_));
  }
  do {
    CK (node_idx_ >= 0);
    if (OB_SUCC(ret) && node_idx_ >= row_count_) {
      ret = OB_ITER_END;
    }
    CK (OB_NOT_NULL(value_table_));
    OX (data = value_table_->get_data());
    CK (OB_NOT_NULL(data));
    OX (result = (static_cast<ObObj*>(data))[node_idx_++]);
  } while (OB_SUCC(ret) && result.get_meta().get_type() == ObMaxType);
  return ret;
}

int ObFunctionTableOp::inner_get_next_row()
{
  return (this->*next_row_func_)();
}

int ObFunctionTableOp::inner_get_next_row_udf()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ObExtendType != MY_SPEC.value_expr_->datum_meta_.type_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(plan_ctx->set_autoinc_id_tmp(0))) {
  } else if (OB_FAIL(ctx_.check_status())) {
  } else {
    ObDatum *value = nullptr;
    if (!already_calc_) {
      if (OB_FAIL(MY_SPEC.value_expr_->eval(eval_ctx_, value))) {
      } else if (value->is_null()) {
        //do nothing
      } else if (OB_ISNULL(value_table_ 
                 = reinterpret_cast<pl::ObPLCollection*>(value->get_ext()))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        row_count_ = value_table_->is_inited() ? value_table_->get_count() : 0;
        col_count_ = value_table_->get_column_count();
      }
      already_calc_ = true;
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(node_idx_ >= row_count_)) {
      ret = OB_ITER_END;
    }
    CK (MY_SPEC.column_exprs_.count() >= col_count_);
    ObObj obj_stack[col_count_];
    if (OB_SUCC(ret)) {
      if (nullptr != value_table_ 
          && ObExtendType == value_table_->get_element_type().get_obj_type()) {
        pl::ObPLComposite *composite = NULL;
        pl::ObPLRecord *record = NULL;
        ObObj record_obj;
        OZ (get_current_result(record_obj));
        if (OB_FAIL(ret)) {
        } else if (ObUserDefinedSQLType == record_obj.get_type()) {
          obj_stack[0] = record_obj;   
        } else if (record_obj.is_pl_extend()) {
          CK (OB_NOT_NULL(composite = reinterpret_cast<pl::ObPLComposite*>(record_obj.get_ext())));
          if (OB_SUCC(ret)) {
            if (composite->is_record()) {
              OX (record = static_cast<pl::ObPLRecord*>(composite));
              CK (record->get_count() == col_count_);
              for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
                OZ (record->get_element(i, obj_stack[i]));
              }
            } else if (composite->is_collection()) {
              OX (obj_stack[0] = record_obj);
            } else {
              ret = OB_ERR_UNEXPECTED;
            }
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("unexpected here", K(ret), K(record_obj), K(record_obj.meta_));
        }
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
          if (OB_FAIL(get_current_result(obj_stack[i]))) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
        if (obj_stack[i].is_null()) {
          MY_SPEC.column_exprs_.at(i)->locate_datum_for_write(eval_ctx_).set_null();
        } else {
          const ObObjDatumMapType &datum_map = MY_SPEC.column_exprs_.at(i)->obj_datum_map_;
          ObExpr * const &expr = MY_SPEC.column_exprs_.at(i);
          ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
          if (OB_FAIL(datum.from_obj(obj_stack[i], datum_map))) {
          } else if (is_lob_storage(obj_stack[i].get_type()) &&
                     OB_FAIL(ob_adjust_lob_datum(get_exec_ctx(), obj_stack[i],
                                                 expr->obj_meta_, datum_map,
                                                 get_exec_ctx().get_allocator(), datum))) {
          }
        }
        if (OB_SUCC(ret)) {
          MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
        }
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_sys_func()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObDatum *value = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ctx_.check_status())) {
  } else if (OB_FAIL(MY_SPEC.value_expr_->eval(eval_ctx_, value))) {
    if (OB_ITER_END != ret) {
    }
  } else {
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_datum(*value);
    MY_SPEC.column_exprs_.at(0)->set_evaluated_projected(eval_ctx_);
  }
  return ret;
}


int ObFunctionTableOp::open_file_source()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(file_ctx_)) {
    file_ctx_ = new (std::nothrow) ObFileSourceCtx();
    if (OB_ISNULL(file_ctx_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc file source context", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_ISNULL(file_ctx_->reader)) {
    file_ctx_->reader = new (std::nothrow) file_source::FileSourceReader();
    if (OB_ISNULL(file_ctx_->reader)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc file source reader", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    file_source::FileReadOptions options;
    options.format = static_cast<file_source::FileFormat>(MY_SPEC.file_format_);
    options.has_header = (MY_SPEC.file_has_header_ < 0) ? true : (0 != MY_SPEC.file_has_header_);
    options.sample_size = MY_SPEC.file_sample_size_;
    if (MY_SPEC.file_delimiter_ > 0) {
      options.delimiter = static_cast<char>(MY_SPEC.file_delimiter_);
    }
    if (!MY_SPEC.file_null_string_.empty()) {
      options.null_string.assign(MY_SPEC.file_null_string_.ptr(), MY_SPEC.file_null_string_.length());
    }
    const std::string path(MY_SPEC.file_path_.ptr(), MY_SPEC.file_path_.length());
    file_source::FileSourceError error;
    if (file_source::FileFormat::GLOB == options.format) {
      error = file_ctx_->reader->open_glob(path);
    } else {
      error = file_ctx_->reader->open(path, options);
    }
    if (!error.is_ok()) {
      ret = file_source_error_to_errno(error);
      LOG_USER_ERROR(ret, "%s", error.to_string().c_str());
    } else if (MY_SPEC.file_describe_) {
      ret = build_describe_rows();
    }
  }
  return ret;
}

int ObFunctionTableOp::build_describe_rows()
{
  int ret = OB_SUCCESS;
  file_ctx_->describe_rows_.clear();
  file_ctx_->describe_index_ = 0;
  file_source::FileSchema schema;
  file_source::FileSourceError error = file_ctx_->reader->infer_schema(schema);
  if (!error.is_ok()) {
    ret = file_source_error_to_errno(error);
    LOG_USER_ERROR(ret, "%s", error.to_string().c_str());
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < schema.column_count(); ++i) {
      const file_source::FileColumn *column = schema.column_at(i);
      if (OB_ISNULL(column)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        std::vector<std::string> row;
        row.push_back(column->name);
        row.push_back(file_source_upper(file_source::column_type_name(column->type)));
        row.push_back(column->nullable ? "YES" : "NO");
        row.push_back("NULL");
        row.push_back("NULL");
        row.push_back("NULL");
        file_ctx_->describe_rows_.push_back(row);
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::produce_describe_row()
{
  int ret = OB_SUCCESS;
  if (file_ctx_->describe_index_ >= static_cast<int64_t>(file_ctx_->describe_rows_.size())) {
    ret = OB_ITER_END;
  } else {
    const std::vector<std::string> &row = file_ctx_->describe_rows_[file_ctx_->describe_index_];
    ++file_ctx_->describe_index_;
    const int64_t col_count = MY_SPEC.column_exprs_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      ObExpr * const expr = MY_SPEC.column_exprs_.at(i);
      ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
      if (i >= static_cast<int64_t>(row.size())) {
        datum.set_null();
      } else {
        ret = set_file_datum(expr, datum, row[i].c_str(), static_cast<int32_t>(row[i].length()));
      }
      if (OB_SUCC(ret)) {
        expr->set_evaluated_projected(eval_ctx_);
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_file()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_ISNULL(file_ctx_) || OB_ISNULL(file_ctx_->reader)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ctx_.check_status())) {
  } else if (MY_SPEC.file_describe_) {
    ret = produce_describe_row();
  } else {
    file_source::FileRow row;
    file_source::FileSourceError error;
    const bool has_row = file_ctx_->reader->next_row(row, error);
    if (!has_row) {
      if (error.is_ok()) {
        ret = OB_ITER_END;
      } else {
        ret = file_source_error_to_errno(error);
        LOG_USER_ERROR(ret, "%s", error.to_string().c_str());
      }
    } else {
      ret = produce_file_row(row);
    }
  }
  return ret;
}

int ObFunctionTableOp::produce_file_row(const file_source::FileRow &row)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = MY_SPEC.column_exprs_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
    ObExpr * const expr = MY_SPEC.column_exprs_.at(i);
    ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
    if (i >= static_cast<int64_t>(row.values.size())
        || (i < static_cast<int64_t>(row.nulls.size()) && row.nulls[i])) {
      datum.set_null();
    } else {
      ret = set_file_datum(expr, datum, row.values[i].c_str(), static_cast<int32_t>(row.values[i].length()));
    }
    if (OB_SUCC(ret)) {
      expr->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObFunctionTableOp::set_file_datum(ObExpr *expr, ObDatum &datum, const char *value, int32_t len)
{
  int ret = OB_SUCCESS;
  const ObObjType type = expr->obj_meta_.get_type();
  switch (type) {
    case ObTinyIntType:
    case ObSmallIntType:
    case ObIntType:
    case ObMediumIntType:
    case ObBigIntType: {
      int64_t int_value = 0;
      if (OB_FAIL(file_source_parse_int(value, len, int_value))) {
      } else {
        datum.set_int(int_value);
      }
      break;
    }
    case ObFloatType:
    case ObDoubleType: {
      double double_value = 0;
      if (OB_FAIL(file_source_parse_double(value, len, double_value))) {
      } else {
        datum.set_double(double_value);
      }
      break;
    }
    case ObDateType: {
      int32_t date_value = 0;
      ObString date_string(static_cast<int64_t>(len), value);
      if (OB_FAIL(ObTimeConverter::str_to_date(date_string, date_value))) {
        std::string text(value, len);
        ret = OB_ERR_UNEXPECTED;
        LOG_USER_ERROR(ret, "invalid date value '%s' in file", text.c_str());
      } else {
        datum.set_date(date_value);
      }
      break;
    }
    case ObDateTimeType:
    case ObTimestampType: {
      std::string normalized(value, len);
      // '2026-08-03T12:13:14' is normalized to '2026-08-03 12:13:14'
      for (size_t i = 0; i < normalized.size(); ++i) {
        if ('T' == normalized[i]) {
          normalized[i] = ' ';
          break;
        }
      }
      int64_t datetime_value = 0;
      ObTimeConvertCtx cvrt_ctx(NULL, false);
      ObString datetime_string(static_cast<int64_t>(normalized.length()), normalized.c_str());
      if (OB_FAIL(ObTimeConverter::str_to_datetime(datetime_string, cvrt_ctx, datetime_value))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_USER_ERROR(ret, "invalid datetime value '%s' in file", normalized.c_str());
      } else {
        datum.set_datetime(datetime_value);
      }
      break;
    }
    default: {
      if (len <= 0) {
        // empty string, no need to allocate any buffer
        datum.set_string("", 0);
      } else {
        char *buf = expr->get_str_res_mem(eval_ctx_, len);
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          memcpy(buf, value, len);
          datum.set_string(buf, len);
        }
      }
      break;
    }
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
