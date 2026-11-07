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

#include "sql/engine/basic/ob_file_table_op.h"

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief The texts of a deserialized specification point to the buffer of the
 *        deserialization, they are copied into the memory of the allocator of
 *        the specification.
 */
int ObFileTableSpec::copy_string(common::ObString &str)
{
  int ret = OB_SUCCESS;
  if (str.empty()) {
    str = common::ObString();
  } else if (OB_ISNULL(alloc_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the allocator of the file table spec is null", K(ret));
  } else {
    char *buf = static_cast<char *>(alloc_->alloc(str.length() + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("cannot allocate the memory of the text", K(ret), K(str.length()));
    } else {
      MEMCPY(buf, str.ptr(), str.length());
      buf[str.length()] = '\0';
      str.assign_ptr(buf, str.length());
    }
  }
  return ret;
}

int ObFileTableSpec::repoint_strings()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(copy_string(path_))) {
  } else if (OB_FAIL(copy_string(delimiter_))) {
  } else if (OB_FAIL(copy_string(quote_))) {
  } else if (OB_FAIL(copy_string(escape_))) {
  } else if (OB_FAIL(copy_string(null_str_))) {
  } else if (OB_FAIL(copy_string(json_format_))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < column_names_.count(); ++i) {
      if (OB_FAIL(copy_string(column_names_.at(i)))) {
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObFileTableSpec)
{
  INIT_SUCC(ret);
  BASE_SER((ObFileTableSpec, ObOpSpec));
  OB_UNIS_ENCODE(column_exprs_);
  OB_UNIS_ENCODE(path_);
  OB_UNIS_ENCODE(format_);
  OB_UNIS_ENCODE(header_);
  OB_UNIS_ENCODE(delimiter_);
  OB_UNIS_ENCODE(quote_);
  OB_UNIS_ENCODE(escape_);
  OB_UNIS_ENCODE(null_str_);
  OB_UNIS_ENCODE(json_format_);
  OB_UNIS_ENCODE(union_by_name_);
  OB_UNIS_ENCODE(file_size_);
  OB_UNIS_ENCODE(file_mtime_);
  OB_UNIS_ENCODE(column_names_);
  OB_UNIS_ENCODE(column_types_);
  OB_UNIS_ENCODE(describe_);
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
  OB_UNIS_ADD_LEN(delimiter_);
  OB_UNIS_ADD_LEN(quote_);
  OB_UNIS_ADD_LEN(escape_);
  OB_UNIS_ADD_LEN(null_str_);
  OB_UNIS_ADD_LEN(json_format_);
  OB_UNIS_ADD_LEN(union_by_name_);
  OB_UNIS_ADD_LEN(file_size_);
  OB_UNIS_ADD_LEN(file_mtime_);
  OB_UNIS_ADD_LEN(column_names_);
  OB_UNIS_ADD_LEN(column_types_);
  OB_UNIS_ADD_LEN(describe_);
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
  OB_UNIS_DECODE(delimiter_);
  OB_UNIS_DECODE(quote_);
  OB_UNIS_DECODE(escape_);
  OB_UNIS_DECODE(null_str_);
  OB_UNIS_DECODE(json_format_);
  OB_UNIS_DECODE(union_by_name_);
  OB_UNIS_DECODE(file_size_);
  OB_UNIS_DECODE(file_mtime_);
  OB_UNIS_DECODE(column_names_);
  OB_UNIS_DECODE(column_types_);
  OB_UNIS_DECODE(describe_);
  if (OB_SUCC(ret)) {
    ret = repoint_strings();
  }
  return ret;
}

ObFileTableOp::ObFileTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
  : ObOperator(exec_ctx, spec, input),
    reader_(NULL),
    describe_values_(),
    describe_row_cnt_(0),
    describe_row_idx_(0)
{
}

int ObFileTableOp::build_options_(ObFileScanOptions &options)
{
  options.path_ = MY_SPEC.path_;
  options.format_ = MY_SPEC.format_;
  options.header_ = MY_SPEC.header_;
  options.sample_size_ = -1;
  options.delimiter_ = MY_SPEC.delimiter_;
  options.quote_ = MY_SPEC.quote_;
  options.escape_ = MY_SPEC.escape_;
  options.null_str_ = MY_SPEC.null_str_;
  options.json_format_ = MY_SPEC.json_format_;
  options.union_by_name_ = MY_SPEC.union_by_name_;
  options.file_size_ = MY_SPEC.file_size_;
  options.file_mtime_ = MY_SPEC.file_mtime_;
  return OB_SUCCESS;
}

int ObFileTableOp::build_columns_(common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  const int64_t name_cnt = MY_SPEC.column_names_.count();
  const int64_t type_cnt = MY_SPEC.column_types_.count();
  if (name_cnt != type_cnt || name_cnt != MY_SPEC.column_exprs_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the description of the file table is inconsistent",
             K(ret), K(name_cnt), K(type_cnt), K(MY_SPEC.column_exprs_.count()));
  } else if (OB_FAIL(columns.prepare_allocate(name_cnt))) {
    LOG_WARN("cannot allocate the columns of the file table", K(ret), K(name_cnt));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < name_cnt; ++i) {
      ObFileColumnDef &column = columns.at(i);
      column.name_ = MY_SPEC.column_names_.at(i);
      column.type_ = MY_SPEC.column_types_.at(i);
      column.nullable_ = true;
      column.precision_ = 0;
      column.scale_ = 0;
    }
  }
  return ret;
}

/**
 * @brief Check that the file did not change since its schema was inferred.
 *
 * The schema of a file is inferred when the statement is resolved. The file may
 * change between the resolution and the execution, the schema of the plan may
 * not match the content of the file any more. When the file changed, the
 * schema is inferred again and the types of the columns are compared, the
 * statement fails when they do not match.
 */
int ObFileTableOp::check_file_changed_()
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.file_size_ < 0 || MY_SPEC.file_mtime_ < 0) {
    // the size and the modification time of the file were not recorded
  } else if (!ObFileScanUtils::is_file_changed(MY_SPEC.path_, MY_SPEC.file_size_, MY_SPEC.file_mtime_)) {
    // the file did not change
  } else {
    ObFileScanOptions options;
    common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> new_columns;
    if (OB_FAIL(build_options_(options))) {
    } else if (OB_FAIL(ObFileScanUtils::infer_schema(options, ctx_.get_allocator(), new_columns))) {
      LOG_WARN("cannot infer the schema of the file that has changed", K(ret), K(options.path_));
      ret = OB_ERR_UNEXPECTED;
    } else if (new_columns.count() != MY_SPEC.column_types_.count()) {
      LOG_WARN("the number of the columns of the file that has changed is not the expected one",
               K(ret), K(new_columns.count()), K(MY_SPEC.column_types_.count()));
      ret = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < new_columns.count(); ++i) {
        if (new_columns.at(i).type_ != MY_SPEC.column_types_.at(i)) {
          LOG_WARN("the type of the column %ld of the file that has changed is not the expected one",
                   K(ret), K(i), K(new_columns.at(i).type_), K(MY_SPEC.column_types_.at(i)));
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
    if (OB_FAIL(ret)) {
      LOG_USER_ERROR(OB_ERR_UNEXPECTED, "the file has changed, please run the query again");
    }
  }
  return ret;
}

int ObFileTableOp::open_reader_()
{
  int ret = OB_SUCCESS;
  ObFileScanOptions options;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> columns;
  if (OB_FAIL(build_options_(options))) {
  } else if (!MY_SPEC.describe_ && OB_FAIL(build_columns_(columns))) {
  } else if (OB_FAIL(create_file_scan_reader(options, columns, ctx_.get_allocator(), reader_))) {
    LOG_WARN("cannot create the reader of the file", K(ret), K(options.path_));
  } else if (MY_SPEC.describe_) {
    // the columns of the file are inferred when the statement runs, the reader
    // is not opened because the inference opens the file itself
    if (OB_FAIL(build_describe_values_())) {
      LOG_WARN("cannot describe the columns of the file", K(ret), K(options.path_));
    }
  } else if (OB_FAIL(reader_->open())) {
    LOG_WARN("cannot open the file", K(ret), K(options.path_));
    destroy_file_scan_reader(reader_);
    reader_ = NULL;
  } else { /*do nothing*/ }
  return ret;
}

/**
 * @brief Infer the columns of the file and build the description of them.
 *
 * The description holds one row per column of the file, the row holds the name
 * of the column, the name of its type, whether it is nullable and three
 * values that are always NULL, they are the key, the default value and the
 * extra information of the column.
 */
int ObFileTableOp::build_describe_values_()
{
  int ret = OB_SUCCESS;
  common::ObArray<ObFileColumnDef, common::ModulePageAllocator, true> columns;
  if (OB_ISNULL(reader_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the reader of the file is null", K(ret));
  } else if (OB_FAIL(reader_->infer_schema(ctx_.get_allocator(), columns))) {
    LOG_WARN("cannot infer the columns of the file", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
      const ObFileColumnDef &column = columns.at(i);
      const char *type_name = ObFileScanUtils::type_name(column.type_);
      common::ObObj name_obj;
      common::ObObj type_obj;
      common::ObObj null_obj;
      common::ObObj key_obj;
      common::ObObj default_obj;
      common::ObObj extra_obj;
      name_obj.set_varchar(column.name_);
      type_obj.set_varchar(common::ObString(strlen(type_name), type_name));
      if (column.nullable_) {
        null_obj.set_varchar(common::ObString(3, "YES"));
      } else {
        null_obj.set_varchar(common::ObString(2, "NO"));
      }
      key_obj.set_null();
      default_obj.set_null();
      extra_obj.set_null();
      if (OB_FAIL(describe_values_.push_back(name_obj))) {
      } else if (OB_FAIL(describe_values_.push_back(type_obj))) {
      } else if (OB_FAIL(describe_values_.push_back(null_obj))) {
      } else if (OB_FAIL(describe_values_.push_back(key_obj))) {
      } else if (OB_FAIL(describe_values_.push_back(default_obj))) {
      } else if (OB_FAIL(describe_values_.push_back(extra_obj))) {
      } else {
        ++describe_row_cnt_;
      }
    }
  }
  return ret;
}

int ObFileTableOp::close_reader_()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(reader_)) {
    if (OB_FAIL(reader_->close())) {
      LOG_WARN("cannot close the file", K(ret), K(MY_SPEC.path_));
    }
    destroy_file_scan_reader(reader_);
    reader_ = NULL;
  }
  return ret;
}

int ObFileTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(MY_SPEC.column_exprs_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the file table has no column", K(ret));
  } else if (MY_SPEC.describe_) {
    // the columns of the file are inferred when the statement runs, the
    // schema of the file is not known when the statement is resolved
    if (OB_FAIL(open_reader_())) {
      LOG_WARN("cannot describe the file", K(ret), K(MY_SPEC.path_));
    }
  } else if (OB_FAIL(check_file_changed_())) {
    LOG_WARN("cannot check the state of the file", K(ret), K(MY_SPEC.path_));
  } else if (OB_FAIL(open_reader_())) {
    LOG_WARN("cannot open the file", K(ret), K(MY_SPEC.path_));
  } else { /*do nothing*/ }
  return ret;
}

int ObFileTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
  } else if (MY_SPEC.describe_) {
    describe_row_idx_ = 0;
  } else if (OB_ISNULL(reader_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the reader of the file is null", K(ret));
  } else if (OB_FAIL(reader_->close())) {
    LOG_WARN("cannot close the file", K(ret), K(MY_SPEC.path_));
  } else if (OB_FAIL(reader_->open())) {
    LOG_WARN("cannot open the file again", K(ret), K(MY_SPEC.path_));
  } else { /*do nothing*/ }
  return ret;
}

int ObFileTableOp::switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_switch_iterator())) {
  } else if (MY_SPEC.describe_) {
    describe_row_idx_ = 0;
  } else if (OB_ISNULL(reader_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the reader of the file is null", K(ret));
  } else if (OB_FAIL(reader_->close())) {
    LOG_WARN("cannot close the file", K(ret), K(MY_SPEC.path_));
  } else if (OB_FAIL(reader_->open())) {
    LOG_WARN("cannot open the file again", K(ret), K(MY_SPEC.path_));
  } else { /*do nothing*/ }
  return ret;
}

int ObFileTableOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(close_reader_())) {
    LOG_WARN("cannot close the reader of the file", K(ret), K(MY_SPEC.path_));
  }
  return ret;
}

/**
 * @brief Write the value of a column into the datum of its expression.
 *
 * The value of the reader is valid until the next row is produced, the text is
 * copied into the buffer of the expression.
 */
int ObFileTableOp::write_column_(const int64_t col_idx, const common::ObObj &obj)
{
  int ret = OB_SUCCESS;
  ObExpr *dst_expr = MY_SPEC.column_exprs_.at(col_idx);
  if (OB_ISNULL(dst_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the expression of the column is null", K(ret), K(col_idx));
  } else {
    ObDatum &dst_datum = dst_expr->locate_datum_for_write(eval_ctx_);
    if (obj.is_null()) {
      dst_datum.set_null();
    } else if (common::OBJ_DATUM_STRING == dst_expr->obj_datum_map_) {
      const uint32_t len = obj.val_len_;
      ObExprStrResAlloc res_alloc(*dst_expr, eval_ctx_);
      char *buf = static_cast<char *>(res_alloc.alloc(len > 0 ? len : 1));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("cannot allocate the memory of the value of the file", K(ret), K(len));
      } else {
        if (len > 0) {
          MEMCPY(buf, obj.v_.string_, len);
        }
        dst_datum.set_string(buf, len);
      }
    } else if (OB_FAIL(dst_datum.from_obj(obj))) {
      LOG_WARN("cannot store the value of the file", K(ret), K(col_idx), K(obj));
    } else { /*do nothing*/ }
    if (OB_SUCC(ret)) {
      dst_expr->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObFileTableOp::calc_next_row()
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.describe_) {
    // the operator produces the description of the columns of the file
    if (describe_row_idx_ >= describe_row_cnt_) {
      ret = OB_ITER_END;
    } else {
      const int64_t base = describe_row_idx_ * 6;
      for (int64_t i = 0; OB_SUCC(ret) && i < 6; ++i) {
        if (OB_FAIL(write_column_(i, describe_values_.at(base + i)))) {
        }
      }
      ++describe_row_idx_;
    }
  } else if (OB_ISNULL(reader_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the reader of the file is null", K(ret));
  } else if (OB_FAIL(reader_->next_row())) {
    // OB_ITER_END is returned when the end of the file is reached
  } else {
    const common::ObIArray<common::ObObj> &values = reader_->get_row_values();
    const int64_t col_cnt = MY_SPEC.column_exprs_.count();
    if (values.count() != col_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the number of the values of the row is not the expected one",
               K(ret), K(values.count()), K(col_cnt));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt; ++i) {
        if (OB_FAIL(write_column_(i, values.at(i)))) {
        }
      }
    }
  }
  return ret;
}

int ObFileTableOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_check_status())) {
  } else {
    clear_evaluated_flag();
    ret = calc_next_row();
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
