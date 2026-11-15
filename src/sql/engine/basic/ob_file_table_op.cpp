/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_file_table_op.h"

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

// The number of rows that are read from the file at once.
static const int64_t FILE_TABLE_BATCH_SIZE = 256;

OB_DEF_SERIALIZE(ObFileTableSpec)
{
  INIT_SUCC(ret);
  BASE_SER((ObFileTableSpec, ObOpSpec));
  OB_UNIS_ENCODE(column_exprs_);
  OB_UNIS_ENCODE(path_);
  OB_UNIS_ENCODE(format_);
  OB_UNIS_ENCODE(header_);
  OB_UNIS_ENCODE(sample_size_);
  OB_UNIS_ENCODE(null_string_);
  OB_UNIS_ENCODE(delimiter_);
  OB_UNIS_ENCODE(quote_);
  OB_UNIS_ENCODE(escape_);
  OB_UNIS_ENCODE(skip_blank_lines_);
  OB_UNIS_ENCODE(file_format_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObFileTableSpec)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObFileTableSpec, ObOpSpec));
  OB_UNIS_ADD_LEN(column_exprs_);
  OB_UNIS_ADD_LEN(path_);
  OB_UNIS_ADD_LEN(format_);
  OB_UNIS_ADD_LEN(header_);
  OB_UNIS_ADD_LEN(sample_size_);
  OB_UNIS_ADD_LEN(null_string_);
  OB_UNIS_ADD_LEN(delimiter_);
  OB_UNIS_ADD_LEN(quote_);
  OB_UNIS_ADD_LEN(escape_);
  OB_UNIS_ADD_LEN(skip_blank_lines_);
  OB_UNIS_ADD_LEN(file_format_);
  return len;
}

OB_DEF_DESERIALIZE(ObFileTableSpec)
{
  INIT_SUCC(ret);
  BASE_DESER((ObFileTableSpec, ObOpSpec));
  OB_UNIS_DECODE(column_exprs_);
  OB_UNIS_DECODE(path_);
  OB_UNIS_DECODE(format_);
  OB_UNIS_DECODE(header_);
  OB_UNIS_DECODE(sample_size_);
  OB_UNIS_DECODE(null_string_);
  OB_UNIS_DECODE(delimiter_);
  OB_UNIS_DECODE(quote_);
  OB_UNIS_DECODE(escape_);
  OB_UNIS_DECODE(skip_blank_lines_);
  OB_UNIS_DECODE(file_format_);
  return ret;
}

ObFileTableOp::~ObFileTableOp()
{
  close_file();
}

int ObFileTableOp::open_file()
{
  int ret = OB_SUCCESS;
  const ObFileTableSpec &spec = MY_SPEC;
  file_sql::FileFormat format = static_cast<file_sql::FileFormat>(spec.file_format_);
  if (OB_ISNULL(input_)) {
    if (NULL == (input_ = ::fopen(spec.path_.ptr(), "rb"))) {
      ret = OB_FILE_NOT_EXIST;
      LOG_USER_ERROR(OB_FILE_NOT_EXIST, "cannot open the file %s", spec.path_.ptr());
    }
  }
  if (OB_SUCC(ret) && NULL == scanner_) {
    file_sql::FileScanOptions options;
    options.header = spec.header_;
    options.sample_size = spec.sample_size_;
    options.null_string = (NULL == spec.null_string_.ptr())
                              ? std::string()
                              : std::string(spec.null_string_.ptr(), spec.null_string_.length());
    options.format = (NULL == spec.format_.ptr()) ? std::string()
                                                  : std::string(spec.format_.ptr(), spec.format_.length());
    options.delimiter = spec.delimiter_;
    options.quote = spec.quote_;
    options.escape = spec.escape_;
    options.skip_blank_lines = spec.skip_blank_lines_;
    if (OB_ISNULL(scanner_ = file_sql::create_file_scanner(format))) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "the file format of the table function is not supported");
    } else {
      const file_sql::FileStatus status = scanner_->prepare(input_, options);
      if (!status.ok()) {
        ret = OB_INVALID_DATA;
        LOG_USER_ERROR(OB_INVALID_DATA,
                       "cannot read the file %s: %s",
                       spec.path_.ptr(),
                       status.message.c_str());
      }
    }
  }
  return ret;
}

int ObFileTableOp::close_file()
{
  int ret = OB_SUCCESS;
  if (NULL != scanner_) {
    delete scanner_;
    scanner_ = NULL;
  }
  if (NULL != input_) {
    ::fclose(input_);
    input_ = NULL;
  }
  return ret;
}

int ObFileTableOp::check_schema()
{
  int ret = OB_SUCCESS;
  const ObFileTableSpec &spec = MY_SPEC;
  const std::vector<file_sql::ColumnSchema> &columns = scanner_->columns();
  if (columns.size() != spec.column_exprs_.count()) {
    // The file changed after the schema was inferred, the plan does not match
    // the file any more.
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA,
                   "the file %s changed after the statement was resolved",
                   spec.path_.ptr());
  }
  return ret;
}

int ObFileTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  row_index_ = 0;
  rows_.clear();
  if (OB_FAIL(open_file())) {
  } else if (OB_FAIL(check_schema())) {
  } else {
    // The rows of the first batch are read when the first row is requested.
  }
  return ret;
}

int ObFileTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
  } else if (OB_FAIL(close_file())) {
  } else {
    row_index_ = 0;
    rows_.clear();
    ret = open_file();
  }
  return ret;
}

int ObFileTableOp::inner_close()
{
  int ret = OB_SUCCESS;
  row_index_ = 0;
  rows_.clear();
  if (OB_FAIL(close_file())) {
  }
  return ret;
}

void ObFileTableOp::destroy()
{
  close_file();
  ObOperator::destroy();
}

int ObFileTableOp::fetch_batch()
{
  int ret = OB_SUCCESS;
  rows_.clear();
  row_index_ = 0;
  // The text of the columns of the previous batch is not needed any more.
  row_alloc_.reset();
  const file_sql::FileStatus status = scanner_->next_batch(FILE_TABLE_BATCH_SIZE, rows_);
  if (file_sql::FileCode::END == status.code) {
    ret = OB_ITER_END;
  } else if (!status.ok()) {
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA,
                   "cannot read the file %s: %s",
                   MY_SPEC.path_.ptr(),
                   status.message.c_str());
  }
  return ret;
}

int ObFileTableOp::write_row(const file_sql::FileTableRow &row)
{
  int ret = OB_SUCCESS;
  const ObFileTableSpec &spec = MY_SPEC;
  if (row.size() != spec.column_exprs_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the file table produced a row with an unexpected number of columns",
             K(row.size()),
             K(spec.column_exprs_.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row.size(); ++i) {
      ObExpr *expr = spec.column_exprs_.at(i);
      if (OB_ISNULL(expr)) {
        // The plan does not use this column of the file, the value of the
        // column is not needed.
        continue;
      }
      ObDataType data_type;
      data_type.set_obj_type(expr->datum_meta_.type_);
      data_type.set_collation_type(expr->datum_meta_.cs_type_);
      data_type.set_precision(expr->datum_meta_.precision_);
      data_type.set_scale(expr->datum_meta_.scale_);
      ret = file_sql::set_datum_from_scalar(expr->locate_datum_for_write(eval_ctx_),
                                             data_type,
                                             row.at(i),
                                             row_alloc_);
    }
  }
  return ret;
}

int ObFileTableOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if (row_index_ < rows_.size()) {
      ret = write_row(rows_.at(row_index_++));
      break;
    }
    // The current batch is used up, the next one is read from the file.
    ret = fetch_batch();
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
