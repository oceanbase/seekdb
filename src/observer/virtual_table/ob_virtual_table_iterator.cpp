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

#define USING_LOG_PREFIX COMMON
#include "share/datum/datum_value_cast.h"
#include "observer/virtual_table/ob_virtual_table_iterator.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/engine/ob_exec_context.h"
#include "query/engine/basic/ob_pushdown_filter.h"

#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/das/ob_das_define.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace common
{

void ObVirtualTableIterator::reset()
{
  output_column_ids_.reset();
  reserved_column_cnt_ = 0;
  schema_guard_ = NULL;
  table_schema_ = NULL;
  index_schema_ = NULL;
  // Native descriptors and their fixed payload slots share this one owned block.
  if (OB_LIKELY(NULL != allocator_ && NULL != row_cells_)) {
    allocator_->free(row_cells_);
    row_cells_ = nullptr;
  }
  cur_row_.cells_ = NULL;
  cur_row_.count_ = 0;
  key_ranges_.reset();
  row_calc_buf_.reset();
  reset_convert_ctx();
  allocator_ = NULL;
  session_ = NULL;
  sql_schema_guard_.reset();
}

void ObVirtualTableIterator::reset_convert_ctx()
{
  if (OB_LIKELY(NULL != allocator_ && NULL != convert_cells_)) {
    allocator_->free(convert_cells_);
    convert_cells_ = nullptr;
  }
  saved_key_ranges_.reset();
  cols_schema_.reset();
  convert_row_.cells_ = NULL;
  convert_row_.count_ = 0;
  need_convert_ = false;
  convert_alloc_.reset();
}

int ObVirtualTableIterator::free_convert_ctx()
{
  int ret = OB_SUCCESS;
  if (!need_convert_) {
  } else if (OB_UNLIKELY(convert_row_.count_ < 0 || NULL == convert_row_.cells_)) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      key_ranges_.reset();
      if (OB_FAIL(key_ranges_.assign(saved_key_ranges_))) {
      }
      saved_key_ranges_.reset();
      // Native descriptors and their fixed payload slots share this one owned block.
      allocator_->free(convert_cells_);
      convert_cells_ = nullptr;
      convert_row_.cells_ = NULL;
      convert_row_.count_ = 0;
      convert_alloc_.reset();
      cols_schema_.reset();
    }
  }
  return ret;
}

int ObVirtualTableIterator::convert_key(const ObRowkey &src, ObRowkey &dst, common::ObIArray<const ObColumnSchemaV2*> &key_cols)
{
  int ret = OB_SUCCESS;
  if (src.get_value_count() > 0) {
    const DatumValue *src_key_objs = src.get_values();
    void *tmp_ptr = NULL;
    DatumValue *new_key_obj = NULL;
    tmp_ptr = allocator_->alloc(src.get_value_count() * sizeof(DatumValue));
    if (OB_ISNULL(tmp_ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_ISNULL(new_key_obj = new (tmp_ptr) DatumValue[src.get_value_count()])) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (src.get_value_count() > key_cols.count()) {
      ret = OB_ERR_UNEXPECTED;
    }
    const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session_);
    ObCastCtx cast_ctx(allocator_, &dtc_params, CM_NONE, ObCharset::get_system_collation());
    for (uint64_t nth_obj = 0; OB_SUCC(ret) && nth_obj < src.get_value_count(); ++nth_obj) {
      const DatumValue &src_obj = src_key_objs[nth_obj];
      if (src_obj.is_min_value()) {
        new_key_obj[nth_obj].set_min_value();
      } else if (src_obj.is_max_value()) {
        new_key_obj[nth_obj].set_max_value();
      } else if (src_obj.is_null()) {
        new_key_obj[nth_obj].set_null();
      } else {
        if (OB_FAIL(DatumValueCaster::to_type(key_cols.at(nth_obj)->get_data_type(),
                                        cast_ctx,
                                        src_key_objs[nth_obj],
                                        *allocator_, new_key_obj[nth_obj]))) {
        }
      }
    }//end for
    if (OB_SUCC(ret)) {
      dst.assign(new_key_obj, src.get_value_count());
    }
  }
  return ret;
}

// Get key types from the mapped origin virtual table by matching column names.
int ObVirtualTableIterator::get_key_cols(common::ObIArray<const ObColumnSchemaV2*> &key_cols)
{
  int ret = OB_SUCCESS;
  common::ObArray<uint64_t> column_ids;
  key_cols.reset();
  if (need_convert_ && !key_ranges_.empty()) {
    if (index_schema_->get_rowkey_info().is_valid()
        && index_schema_->get_rowkey_info().get_column_ids(column_ids)) {
    }
    if (OB_SUCC(ret)) {
      common::ObArray<const ObString*> column_names;
      for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
        const ObColumnSchemaV2 * col_schema = table_schema_->get_column_schema(column_ids.at(i));
        if (OB_ISNULL(col_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(column_names.push_back(&col_schema->get_column_name_str()))) {
        }
      }
      if (OB_SUCC(ret) && column_ids.count() != column_names.count()) {
        ret = OB_ERR_UNEXPECTED;
      }
      // get origin key type by column name
      if (OB_SUCC(ret)) {
        const ObTableSchema *org_table_schema = table_schema_;
        if (OB_ISNULL(org_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); ++i) {
            const ObString *column_name = column_names.at(i);
            const ObColumnSchemaV2 *col_schema = org_table_schema->get_column_schema(*column_name);
            if (OB_ISNULL(col_schema)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(key_cols.push_back(col_schema))) {
            }
          }
          if (OB_SUCC(ret) && key_cols.count() != column_names.count()) {
          }
        }
      }
    }
  }
  return ret;
}

// Extended virtual table keys are converted using the matching MySQL virtual table
// column types. Every extended virtual table must map to one MySQL virtual table.
int ObVirtualTableIterator::convert_key_ranges()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!key_ranges_.empty()) {
    common::ObSEArray<common::ObNewRange, 16> tmp_range;
    common::ObArray<const ObColumnSchemaV2*> key_cols;
    if (OB_FAIL(get_key_cols(key_cols))) {
    } else if (key_cols.empty() && 1 == key_ranges_.count() && key_ranges_.at(0).is_whole_range()) {
      ObNewRange new_range;
      new_range.table_id_ = key_ranges_.at(0).table_id_;
      new_range.set_whole_range();
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < key_ranges_.count(); ++i) {
        ObNewRange new_range;
        new_range.table_id_ = key_ranges_.at(i).table_id_;
        new_range.border_flag_ = key_ranges_.at(i).border_flag_;
        if (OB_FAIL(convert_key(key_ranges_.at(i).start_key_, new_range.start_key_, key_cols))) {
        } else if (OB_FAIL(convert_key(key_ranges_.at(i).end_key_, new_range.end_key_, key_cols))) {
        } else if (OB_FAIL(tmp_range.push_back(new_range))) {
        }
      }//end for
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(saved_key_ranges_.assign(key_ranges_))) {
      } else {
        key_ranges_.reset();
        if (OB_FAIL(key_ranges_.assign(tmp_range))) {
        }
      }
    }
  }
  return ret;
}

int ObVirtualTableIterator::init_convert_ctx()
{
  int ret = OB_SUCCESS;
  
  
  const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session_);
  ObCastCtx cast_ctx(&convert_alloc_, &dtc_params, CM_NONE, table_schema_->get_collation_type());
  cast_ctx_ = cast_ctx;

  if (need_convert_) {
    DatumValue *cells = nullptr;
    if (OB_UNLIKELY(NULL == allocator_ || NULL == table_schema_ || NULL == session_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(DatumValueBuffer::allocate(*allocator_, row_cell_capacity(), cells))) {
      COMMON_LOG(ERROR, "fail to alloc cells", K(ret), K(reserved_column_cnt_));
    } else {
      convert_cells_ = cells;
      convert_row_.cells_ = cells;
      convert_row_.count_ = reserved_column_cnt_;
      if (OB_FAIL(convert_key_ranges())) {
      }
    }
  }
  return ret;
}

int ObVirtualTableIterator::open()
{
  int ret = OB_SUCCESS;
  DatumValue *cells = nullptr;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(scan_param_)
             || (NULL != scan_param_->output_exprs_ && NULL == scan_param_->op_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(DatumValueBuffer::allocate(*allocator_, row_cell_capacity(), cells))) {
    COMMON_LOG(ERROR, "fail to alloc cells", K(ret), K(reserved_column_cnt_));
  } else if (OB_FAIL(init_sql_schema_guard_())) {
    allocator_->free(cells);
  } else {
    row_cells_ = cells;
    cur_row_.cells_ = cells;
    cur_row_.count_ = reserved_column_cnt_;
    if (OB_FAIL(init_convert_ctx())) {
    } else if (OB_FAIL(inner_open())) {
    }
  }
  return ret;
}

int ObVirtualTableIterator::get_all_columns_schema()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
    const uint64_t column_id = output_column_ids_.at(i);
    const ObColumnSchemaV2 *col_schema = table_schema_->get_column_schema(column_id);
    if (OB_ISNULL(col_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("col_schema is NULL", K(ret), K(column_id));
    } else if (OB_FAIL(cols_schema_.push_back(col_schema))) {
    }
  }
  return ret;
}

int ObVirtualTableIterator::convert_output_row(ObNewRow *&cur_row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cur_row)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!need_convert_) {
    // don't convert
  } else {
    convert_alloc_.reuse();
    if (cols_schema_.empty() && OB_FAIL(get_all_columns_schema())) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      const ObColumnSchemaV2 *col_schema = cols_schema_.at(i);
      if (cur_row->get_cell(i).is_null()
          || (cur_row->get_cell(i).is_string_type() && 0 == cur_row->get_cell(i).get_data_length())
          || ob_is_empty_lob(cur_row->get_cell(i).get_type(), cur_row->get_cell(i).datum_,
                             cur_row->get_cell(i).has_lob_header())
          || (cur_row->get_cell(i).is_timestamp() && cur_row->get_cell(i).get_timestamp() <= 0)) {
        convert_row_.cells_[i].set_null();
      } else {
        ObCastCtx context = cast_ctx_;
        context.dest_collation_ = col_schema->get_collation_type();
        context.expect_obj_collation_ = col_schema->get_collation_type();
        const DatumValue &source = cur_row->get_cell(i);
        if (source.get_type() == col_schema->get_data_type()
            && !source.is_decimal_int() && !source.is_string_type()) {
          // A same-type scalar reuses the fixed slot; no per-row scalar allocation.
          ret = DatumValueBuffer::assign_view(source, convert_cells_, row_cell_capacity(), i);
        } else {
          ret = DatumValueCaster::to_type(col_schema->get_data_type(), context,
              source, convert_alloc_, convert_row_.cells_[i]);
        }
      }
    }
    cur_row = &convert_row_;
  }
  return ret;
}

int ObVirtualTableIterator::convert_temporary_lob(DatumValue &value, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (!value.is_null() && value.is_lob_storage() && !value.has_lob_header()) {
    SqlTypeMeta result_meta = value.meta_;
    result_meta.set_has_lob_header();
    if (OB_FAIL(ObTextStringResult::ob_convert_datum_temporay_lob(
            value.datum_, value.meta_, result_meta, owner))) {
    } else { value.meta_ = result_meta; }
  }
  return ret;
}

// Preserve the old virtual-table column conversion order: pure native cast,
// then non-strict character validation and the existing native accuracy check.
int ObVirtualTableIterator::check_output_value(const DatumValue &source,
    const ObColumnSchemaV2 &column, ObCastCtx &context, DatumValue &output)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = scan_param_ != nullptr && scan_param_->op_ != nullptr
      ? &scan_param_->op_->get_eval_ctx().exec_ctx_
      : session_ != nullptr ? session_->get_cur_exec_ctx() : nullptr;
  if (exec_ctx == nullptr) {
    ret = OB_NOT_INIT;
  } else {
    ObDatumCaster *caster = nullptr;
    ObDatumCaster nested;
    DatumParameter input, casted, checked;
    input.assign_value(source);
    ObExprResType target;
    target.set_type(column.get_data_type());
    target.set_collation_type(column.get_collation_type());
    target.set_accuracy(column.get_accuracy());
    if (OB_FAIL(exec_ctx->get_native_datum_caster(caster))) {
    } else if (caster->native_cast_active()) {
      if (OB_FAIL(nested.init(*exec_ctx, false))) {}
      else { caster = &nested; }
    }
    if (OB_SUCC(ret) && OB_FAIL(caster->to_value(input, target, context.cast_mode_, row_calc_buf_, casted))) {
    } else if (OB_SUCC(ret) && OB_FAIL(caster->check_accuracy_and_collation(casted,
          column.get_accuracy(), column.get_collation_type(), context.cast_mode_, false,
          row_calc_buf_, checked, &context.warning_))) {
    } else if (OB_SUCC(ret)) {
      output = checked.value_view();
    }
  }
  return ret;
}

int ObVirtualTableIterator::get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObNewRow *cur_row = NULL;
  row_calc_buf_.reuse();
  const int64_t abs_timeout_ts = get_scan_param()->timeout_;
  if (ObClockGenerator::getClock() > abs_timeout_ts) {
    ret = OB_TIMEOUT;
    LOG_WARN("iterate virtual table row timeout", KR(ret), KTIME(abs_timeout_ts));
  } else if (OB_FAIL(THIS_WORKER.check_status())) {
  } else if (OB_FAIL(inner_get_next_row(cur_row))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
    }
  } else if (OB_ISNULL(cur_row)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(cur_row->count_ < output_column_ids_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("row count is less than output column count", K(ret),
              K(cur_row->count_), K(output_column_ids_.count()));
  } else if (cur_row->count_ > 0 &&
             OB_ISNULL(cur_row->cells_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("cur_row->cells_ is NULL", K(ret));
  } else if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("table schema is NULL", K(ret));
  } else if (OB_FAIL(convert_output_row(cur_row))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
    const uint64_t column_id = output_column_ids_.at(i);
    const ObColumnSchemaV2 *col_schema = table_schema_->get_column_schema(column_id);
    if (OB_ISNULL(col_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("col_schema is NULL", K(ret), K(column_id));
    } else if (OB_UNLIKELY(col_schema->get_data_type() != cur_row->cells_[i].get_type()
                           && ObNullType != cur_row->cells_[i].get_type())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("column type in this row is not expected type", K(ret), K(i),
                "table_name", table_schema_->get_table_name_str(),
                "column_name", col_schema->get_column_name_str(),
                K(column_id), K(cur_row->cells_[i]), K(cur_row->cells_[i].get_type()),
                K(col_schema->get_data_type()), K(output_column_ids_));
    }
    if (OB_SUCC(ret)
        && is_lob_storage(col_schema->get_data_type())
        && !cur_row->cells_[i].has_lob_header()) { // cannot be json type;
      DatumValue &value = cur_row->cells_[i];
      SqlTypeMeta result_meta = value.meta_;
      result_meta.set_has_lob_header();
      if (OB_FAIL(ObTextStringResult::ob_convert_datum_temporay_lob(
              value.datum_, value.meta_, result_meta, row_calc_buf_))) {
      } else if (!value.is_null()) {
        value.meta_ = result_meta;
      }
    }
    if (OB_SUCC(ret) && ob_is_string_tc(col_schema->get_data_type())
        && (col_schema->get_data_length() < cur_row->cells_[i].get_string_len()
            // do charset convert when obj meta is different from expr meta
            || (CS_TYPE_INVALID != cur_row->cells_[i].get_collation_type()
                && col_schema->get_collation_type() != cur_row->cells_[i].get_collation_type()))) {
      //Check the column schema to ensure that it meets the schema definition;
      //But currently, only strings that exceed the length limit are processed to prevent occupying too many performance resources and causing too much interface delay
      DatumValue output;
      ObCastCtx context = cast_ctx_;
      context.cast_mode_ |= CM_WARN_ON_FAIL;
      if (OB_FAIL(check_output_value(cur_row->cells_[i], *col_schema, context, output))) {
      } else {
        cur_row->cells_[i] = output;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = cur_row;
  }
  return ret;
}

int ObVirtualTableIterator::get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(capacity < 1)) {
  } else if (OB_ISNULL(scan_param_) || OB_ISNULL(scan_param_->op_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObEvalCtx::BatchInfoScopeGuard guard(scan_param_->op_->get_eval_ctx());
    guard.set_batch_size(1);
    guard.set_batch_idx(0);
    if (OB_FAIL(get_next_row())) {
      if (OB_ITER_END != ret) { LOG_WARN("get next row failed", K(ret)); }
    } else {
      count = 1;
    }
  }
  return ret;
}
int ObVirtualTableIterator::get_next_row()
{
  int ret = OB_SUCCESS;
  ObNewRow *row = NULL;
  if (OB_ISNULL(scan_param_)
      || OB_ISNULL(scan_param_->output_exprs_)
      || OB_ISNULL(scan_param_->op_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (EMPTY_VIRTUAL_TABLE_TABLET_ID == scan_param_->tablet_id_.id()) {
    row = NULL;
    ret = OB_ITER_END;
  } else if (OB_FAIL(get_next_row(row))) {
    if (OB_ITER_END != ret) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(row)) {
  } else if (scan_param_->output_exprs_->count() > row->count_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < scan_param_->output_exprs_->count(); i++) {
      ObExpr *expr = scan_param_->output_exprs_->at(i);
      ObDatum &datum = expr->locate_datum_for_write(scan_param_->op_->get_eval_ctx());
      const DatumValue &value = row->cells_[i];
      ObDatum projected;
      char extended_payload[DATUM_EXTEND_PAYLOAD_SIZE];
      if (OB_FAIL(value.to_row_datum(projected, extended_payload))) {
      } else if (value.is_ext() && OB_FAIL(datum.deep_copy(projected, row_calc_buf_))) {
      } else {
        if (!value.is_ext()) { datum = projected; }
        if (is_lob_storage(value.get_type())) {
          ret = ob_adjust_lob_datum(scan_param_->op_->get_eval_ctx().exec_ctx_,
              static_cast<const ObDatum &>(datum), value.meta_, expr->obj_meta_, *allocator_, datum);
        }
      }
    }
  }
  return ret;
}

int ObVirtualTableIterator::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(cur_row_.count_ > 0 && NULL == cur_row_.cells_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(inner_close())) {
  } else if (OB_FAIL(free_convert_ctx())) {
  } else {
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // Native descriptors and their fixed payload slots share this one owned block.
      if (row_cells_ != nullptr) {
        allocator_->free(row_cells_);
        row_cells_ = nullptr;
      }
      cur_row_.cells_ = NULL;
      cur_row_.count_ = 0;
    }
  }
  row_calc_buf_.reset();
  return ret;
}

// level_str support: db_acc, table_acc
// reference: ob_expr_sys_privilege_check.cpp:calc_resultN
int ObVirtualTableIterator::check_priv(const ObString &level_str,
                                       const ObString &db_name,
                                       const ObString &table_name,
                                       bool &passed)
{
  int ret = OB_SUCCESS;
  share::schema::ObSessionPrivInfo session_priv;
  const common::ObIArray<uint64_t> &enable_role_id_array = session_->get_enable_role_array();
  CK (OB_NOT_NULL(session_) && OB_NOT_NULL(schema_guard_));
  OZ (session_->get_session_priv_info(session_priv));
  // bool allow_show = true;
  if (OB_SUCC(ret)) {
    if (0 == level_str.case_compare("db_acc")) {
      if (OB_FAIL(schema_guard_->check_db_show(session_priv, enable_role_id_array, db_name, passed))) {
      }
    } else if (0 == level_str.case_compare("table_acc")) {
      //if (OB_FAIL(priv_mgr.check_table_show(session_priv,
      if (OB_FAIL(schema_guard_->check_table_show(session_priv, enable_role_id_array, db_name, table_name, passed))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}


int ObVirtualTableIterator::init_sql_schema_guard_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_guard_)) {
    // don't do anything
    // ignore ret
  } else {
    sql_schema_guard_.set_schema_guard(schema_guard_);
  }
  return ret;
}

}// common
}// oceanbase
