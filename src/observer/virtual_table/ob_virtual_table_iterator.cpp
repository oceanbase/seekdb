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
#include "observer/virtual_table/ob_virtual_table_iterator.h"

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
  //Since ObObj's destructor does not do meaningful operations, in order to save performance, ObObj's destructor call is omitted, and the cells_memory is directly released.
  if (OB_LIKELY(NULL != allocator_ && NULL != cur_row_.cells_)) {
    allocator_->free(cur_row_.cells_);
  }
  cur_row_.cells_ = NULL;
  cur_row_.count_ = 0;
  key_ranges_.reset();
  row_calc_buf_.reset();
  convert_alloc_.reset();
  allocator_ = NULL;
  session_ = NULL;
  sql_schema_guard_.reset();
}

// Get key types from the mapped origin virtual table by matching column names.

// Extended virtual table keys are converted using the matching MySQL virtual table
// column types. Every extended virtual table must map to one MySQL virtual table.

void ObVirtualTableIterator::init_cast_ctx()
{
  const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session_);
  cast_ctx_ = ObCastCtx(&convert_alloc_, &dtc_params, CM_NONE, table_schema_->get_collation_type());
}

int ObVirtualTableIterator::open()
{
  int ret = OB_SUCCESS;
  void *tmp_ptr = NULL;
  ObObj *cells = NULL;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(scan_param_)
             || (NULL != scan_param_->output_exprs_ && NULL == scan_param_->op_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(tmp_ptr = allocator_->alloc( (reserved_column_cnt_ > 0 ? reserved_column_cnt_ : 1) * sizeof(ObObj)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(ERROR, "fail to alloc cells", K(ret), K(reserved_column_cnt_));
  } else if (OB_ISNULL(cells = new (tmp_ptr) ObObj[(reserved_column_cnt_ > 0 ? reserved_column_cnt_ : 1)])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(init_sql_schema_guard_())) {
  } else {
    cur_row_.cells_ = cells;
    cur_row_.count_ = reserved_column_cnt_;
    init_cast_ctx();
    ret = inner_open();
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
        ObObj &obj_convert = cur_row->cells_[i];
      if (OB_FAIL(ObTextStringResult::ob_convert_obj_temporay_lob(obj_convert, row_calc_buf_))) {
      }
    }
    if (OB_SUCC(ret) && ob_is_string_tc(col_schema->get_data_type())
        && (col_schema->get_data_length() < cur_row->cells_[i].get_string_len()
            // do charset convert when obj meta is different from expr meta
            || (CS_TYPE_INVALID != cur_row->cells_[i].get_collation_type()
                && col_schema->get_collation_type() != cur_row->cells_[i].get_collation_type()))) {
      //Check the column schema to ensure that it meets the schema definition;
      //But currently, only strings that exceed the length limit are processed to prevent occupying too many performance resources and causing too much interface delay
      ObObj output_obj;
      ObArray<ObString> *type_infos = NULL;
      const bool is_strict = false;
      ObRawExprResType res_type;
      res_type.set_accuracy(col_schema->get_accuracy());
      res_type.set_collation_type(col_schema->get_collation_type());
      res_type.set_type(col_schema->get_data_type());
      ObCastCtx cast_ctx = cast_ctx_;
      cast_ctx.cast_mode_ = cast_ctx_.cast_mode_ | CM_WARN_ON_FAIL;
      if (OB_FAIL(ObExprColumnConv::convert_skip_null_check(output_obj, cur_row->cells_[i],
                                                            res_type, is_strict, cast_ctx,
                                                            type_infos))) {
      } else {
        cur_row->cells_[i] = output_obj;
        if (OB_SUCCESS != cast_ctx.warning_) {
        }
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
      if (OB_FAIL(datum.from_obj(row->cells_[i], expr->obj_datum_map_))) {
      } else if (is_lob_storage(row->cells_[i].get_type()) &&
                 OB_FAIL(ob_adjust_lob_datum(scan_param_->op_->get_eval_ctx().exec_ctx_,
                                             row->cells_[i], expr->obj_meta_,
                                             expr->obj_datum_map_, *allocator_, datum))) {
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
  } else {
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      //Since ObObj's destructor does not do meaningful operations, in order to save performance, ObObj's destructor call is omitted, and the cells_memory is directly released.
      if (cur_row_.cells_ != NULL) {
        allocator_->free(cur_row_.cells_);
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
