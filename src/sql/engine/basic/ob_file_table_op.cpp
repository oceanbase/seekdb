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

#include "sql/engine/basic/file_sql/file_sql_path.h"
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
  OB_UNIS_ENCODE(is_schema_);
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
  OB_UNIS_ADD_LEN(is_schema_);
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
  OB_UNIS_DECODE(is_schema_);
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
  if (file_sql::FileFormat::GLOB == format) {
    // The glob function discovers the files of a pattern, there is no file to
    // open. Every file of the pattern is a row of the table.
    ObString secure_file_priv;
    const ObSQLSessionInfo *session_info = get_exec_ctx().get_my_session();
    if (OB_ISNULL(session_info) || OB_FAIL(session_info->get_secure_file_priv(secure_file_priv))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const std::string secure_dir = (NULL == secure_file_priv.ptr())
                                        ? std::string()
                                        : std::string(secure_file_priv.ptr(), secure_file_priv.length());
      const std::string pattern = (NULL == spec.path_.ptr())
                                     ? std::string()
                                     : std::string(spec.path_.ptr(), spec.path_.length());
      std::vector<std::string> files;
      const file_sql::PathCode code = file_sql::expand_glob_pattern(pattern, secure_dir, files);
      if (file_sql::PathCode::OK != code) {
        ret = OB_FILE_NOT_EXIST;
        LOG_USER_ERROR(OB_FILE_NOT_EXIST,
                       "cannot expand the pattern %s: %s",
                       pattern.c_str(),
                       file_sql::path_code_message(code));
      } else {
        for (const std::string &file : files) {
          file_sql::FileTableRow row;
          file_sql::ScalarValue value;
          value.type = file_sql::ScalarType::VARCHAR;
          value.is_null = false;
          value.text = file;
          row.push_back(value);
          rows_.push_back(row);
        }
        row_index_ = 0;
      }
    }
    return ret;
  }
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
  if (OB_ISNULL(scanner_)) {
    // The glob function does not read a file, it has no schema to check.
    return ret;
  }
  if (spec.is_schema_) {
    // The columns of the table are the columns of the schema of the file, the
    // number of the columns of the file is not the number of the columns of
    // the table.
    return ret;
  }
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
  } else if (MY_SPEC.is_schema_ && OB_FAIL(build_schema_rows())) {
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
  if (file_sql::FileFormat::GLOB == static_cast<file_sql::FileFormat>(MY_SPEC.file_format_)) {
    // The rows of a glob are built when the file is opened.
    ret = OB_ITER_END;
    return ret;
  }
  if (MY_SPEC.is_schema_) {
    // The rows of the schema of the file are built when the file is opened,
    // the file has no rows to read.
    ret = OB_ITER_END;
    return ret;
  }
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

int ObFileTableOp::build_schema_rows()
{
  int ret = OB_SUCCESS;
  // The schema of a file has a row for every column of the file, the row has
  // the name of the column, the type of the column, the null of the column,
  // the key of the column, the default of the column and the extra of the
  // column.
  std::vector<file_sql::ColumnSchema> columns;
  if (OB_ISNULL(scanner_)) {
    // The glob function does not read a file, it has one column, the column
    // file.
    file_sql::ColumnSchema column;
    column.name = "file";
    column.type = file_sql::ScalarType::VARCHAR;
    column.nullable = false;
    columns.push_back(column);
  } else {
    columns = scanner_->columns();
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < static_cast<int64_t>(columns.size()); ++i) {
    const file_sql::ColumnSchema &column = columns[i];
    file_sql::FileTableRow row;
    file_sql::ScalarValue value;
    value.type = file_sql::ScalarType::VARCHAR;
    value.is_null = false;
    value.text = column.name;
    row.push_back(value);
    value.text = file_sql::scalar_type_name(column.type);
    row.push_back(value);
    value.text = column.nullable ? "YES" : "NO";
    row.push_back(value);
    // The key, the default and the extra of a column of a file are not known.
    value.type = file_sql::ScalarType::NULL_VALUE;
    value.is_null = true;
    value.text.clear();
    row.push_back(value);
    row.push_back(value);
    row.push_back(value);
    rows_.push_back(row);
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
      // The expressions of the operator are evaluated again for every row.
      // The flag that remembers that an expression was evaluated is not
      // cleared by the framework when the operator is driven row by row, so
      // the filter of the statement would be evaluated only for the first row
      // of the file and its result would be used for every other row. The
      // other operators that produce rows clear the flag themselves, see
      // ObValuesTableAccessOp::inner_get_next_row().
      clear_evaluated_flag();
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
