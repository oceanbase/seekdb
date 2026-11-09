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

#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{

OB_SERIALIZE_MEMBER((ObFileTableSpec, ObOpSpec),
                    column_exprs_,
                    file_path_,
                    format_,
                    has_header_,
                    field_delim_,
                    quote_char_,
                    escape_char_,
                    is_glob_,
                    is_describe_);

int ObFileTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  allocator_ = &ctx_.get_allocator();
  glob_index_ = 0;
  describe_index_ = 0;
  if (MY_SPEC.is_describe_) {
    if (OB_FAIL(ret = describe_schema())) {
    }
  } else if (OB_FAIL(ret = open_reader())) {
  }
  return ret;
}

int ObFileTableOp::open_reader()
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.is_glob_) {
    common::ObArray<ObString, common::ModulePageAllocator, true> files;
    if (OB_FAIL(ret = FileScanUtils::glob_files(file_path(), files, *allocator_))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < files.count(); ++i) {
        if (OB_FAIL(ret = glob_files_.push_back(files.at(i)))) {
          break;
        }
      }
    }
  } else if (OB_FAIL(ret = FileScanUtils::create_reader(file_path(),
                                                       options(),
                                                       *allocator_,
                                                       reader_))) {
  }
  return ret;
}

ObString ObFileTableOp::file_path() const
{
  ObString path;
  if (MY_SPEC.file_path_.count() > 0) {
    path.assign_ptr(&MY_SPEC.file_path_.at(0), MY_SPEC.file_path_.count() - 1);
  }
  return path;
}

FileScanOptions ObFileTableOp::options() const
{
  FileScanOptions options;
  options.format_ = static_cast<FileScanFormat>(MY_SPEC.format_);
  options.has_header_ = MY_SPEC.has_header_;
  options.field_delim_ = MY_SPEC.field_delim_;
  options.quote_char_ = MY_SPEC.quote_char_;
  options.escape_char_ = MY_SPEC.escape_char_;
  options.sample_size_ = -1;
  return options;
}

int ObFileTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  glob_index_ = 0;
  describe_index_ = 0;
  glob_files_.reuse();
  if (OB_NOT_NULL(reader_)) {
    // the file has to be read again from the very beginning
    FileScanUtils::destroy_reader(reader_);
    reader_ = NULL;
  }
  if (OB_SUCC(ret) && !MY_SPEC.is_describe_ && OB_FAIL(ret = open_reader())) {
  }
  return ret;
}

int ObFileTableOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.is_describe_) {
    ret = write_describe_row();
  } else if (MY_SPEC.is_glob_) {
    ret = write_glob_row();
  } else if (OB_ISNULL(reader_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int next = reader_->next_row();
    if (OB_ITER_END == next) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(next)) {
      ret = next;
    } else {
      ret = write_row(reader_->fields());
    }
  }
  return ret;
}

int ObFileTableOp::write_row(const FileScanFields &fields)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.column_exprs_.count(); ++i) {
    ObExpr *const expr = MY_SPEC.column_exprs_.at(i);
    ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
    if (i >= fields.count() || NULL == fields.at(i).ptr()) {
      datum.set_null();
    } else if (OB_FAIL(ret = FileScanUtils::set_datum(fields.at(i),
                                                      FileScanUtils::to_file_scan_type(expr->datum_meta_.type_),
                                                      datum))) {
    }
    if (OB_SUCC(ret)) {
      expr->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObFileTableOp::write_glob_row()
{
  int ret = OB_SUCCESS;
  if (glob_index_ >= glob_files_.count()) {
    ret = OB_ITER_END;
  } else {
    ObExpr *const expr = MY_SPEC.column_exprs_.at(0);
    ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
    datum.set_string(glob_files_.at(glob_index_));
    expr->set_evaluated_projected(eval_ctx_);
    ++glob_index_;
  }
  return ret;
}

int ObFileTableOp::describe_schema()
{
  int ret = OB_SUCCESS;
  describe_columns_.reset();
  describe_index_ = 0;
  if (MY_SPEC.is_glob_) {
    FileScanColumn column;
    column.name_ = ObString(4, "file");
    column.type_ = FILE_SCAN_TYPE_VARCHAR;
    column.nullable_ = false;
    if (OB_FAIL(ret = describe_columns_.push_back(column))) {
    }
  } else if (OB_FAIL(ret = FileScanUtils::infer_schema(file_path(),
                                                       options(),
                                                       describe_columns_,
                                                       *allocator_))) {
  }
  return ret;
}

int ObFileTableOp::write_describe_row()
{
  int ret = OB_SUCCESS;
  if (describe_index_ >= describe_columns_.count()) {
    ret = OB_ITER_END;
  } else {
    const FileScanColumn &column = describe_columns_.at(describe_index_);
    const char *type_name = FileScanUtils::type_name(column.type_);
    ObString values[6];
    values[0] = column.name_;
    values[1] = ObString(static_cast<int32_t>(strlen(type_name)), type_name);
    values[2] = ObString(column.nullable_ ? 3 : 2, column.nullable_ ? "YES" : "NO");
    values[3] = ObString();
    values[4] = ObString();
    values[5] = ObString();
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.column_exprs_.count() && i < 6; ++i) {
      ObExpr *const expr = MY_SPEC.column_exprs_.at(i);
      ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
      if (OB_ISNULL(values[i].ptr())) {
        datum.set_null();
      } else {
        datum.set_string(values[i]);
      }
      expr->set_evaluated_projected(eval_ctx_);
    }
    ++describe_index_;
  }
  return ret;
}

int ObFileTableOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(reader_)) {
    FileScanUtils::destroy_reader(reader_);
    reader_ = NULL;
  }
  glob_files_.reuse();
  glob_index_ = 0;
  describe_columns_.reset();
  describe_index_ = 0;
  return ret;
}

void ObFileTableOp::destroy()
{
  if (OB_NOT_NULL(reader_)) {
    FileScanUtils::destroy_reader(reader_);
    reader_ = NULL;
  }
  ObOperator::destroy();
}

} // namespace sql
} // namespace oceanbase
