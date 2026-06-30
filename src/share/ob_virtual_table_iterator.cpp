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
#include "share/ob_virtual_table_iterator.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

#include "share/catalog/ob_external_object_ctx.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
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
  reset_convert_ctx();
  allocator_ = NULL;
  session_ = NULL;
  sql_schema_guard_.reset();
}

void ObVirtualTableIterator::reset_convert_ctx()
{
  if (OB_LIKELY(NULL != allocator_ && NULL != convert_row_.cells_)) {
    allocator_->free(convert_row_.cells_);
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
    LOG_WARN("convert row is not init", K(ret), K(convert_row_));
  } else {
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("allocator is NULL", K(ret));
    } else {
      key_ranges_.reset();
      if (OB_FAIL(key_ranges_.assign(saved_key_ranges_))) {
      }
      saved_key_ranges_.reset();
      //Since ObObj's destructor does not do meaningful operations, in order to save performance, ObObj's destructor call is omitted, and the cells_memory is directly released.
      allocator_->free(convert_row_.cells_);
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
  if (src.get_obj_cnt() > 0) {
    const ObObj *src_key_objs = src.get_obj_ptr();
    void *tmp_ptr = NULL;
    ObObj *new_key_obj = NULL;
    tmp_ptr = allocator_->alloc(src.get_obj_cnt() * sizeof(ObObj));
    if (OB_ISNULL(tmp_ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc new obj", K(ret));
    } else if (OB_ISNULL(new_key_obj = new (tmp_ptr) ObObj[src.get_obj_cnt()])) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc new obj", K(ret));
    } else if (src.get_obj_cnt() > key_cols.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("keys are not match with columns", K(ret));
    }
    const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session_);
    ObCastCtx cast_ctx(allocator_, &dtc_params, CM_NONE, ObCharset::get_system_collation());
    for (uint64_t nth_obj = 0; OB_SUCC(ret) && nth_obj < src.get_obj_cnt(); ++nth_obj) {
      const ObObj &src_obj = src_key_objs[nth_obj];
      if (src_obj.is_min_value()) {
        new_key_obj[nth_obj].set_min_value();
      } else if (src_obj.is_max_value()) {
        new_key_obj[nth_obj].set_max_value();
      } else if (src_obj.is_null()) {
        new_key_obj[nth_obj].set_null();
      } else {
        if (OB_FAIL(ObObjCaster::to_type(key_cols.at(nth_obj)->get_data_type(),
                                        cast_ctx,
                                        src_key_objs[nth_obj],
                                        new_key_obj[nth_obj]))) {
        }
      }
    }//end for
    if (OB_SUCC(ret)) {
      dst.assign(new_key_obj, src.get_obj_cnt());
    }
  }
  return ret;
}

// get origin type of keys in mysql mode
// first find the column name that is same as origin virtual table in mysql mode
// then find column type by column name
int ObVirtualTableIterator::get_key_cols(common::ObIArray<const ObColumnSchemaV2*> &key_cols)
{
  int ret = OB_SUCCESS;
  common::ObArray<uint64_t> column_ids;
  key_cols.reset();
  if (need_convert_ && !key_ranges_.empty()) {
    if (index_schema_->get_rowkey_info().is_valid()
        && index_schema_->get_rowkey_info().get_column_ids(column_ids)) {
      LOG_WARN("get key column ids failed", K(ret));
    }
    if (OB_SUCC(ret)) {
      common::ObArray<const ObString*> column_names;
      for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
        const ObColumnSchemaV2 * col_schema = table_schema_->get_column_schema(column_ids.at(i));
        if (OB_ISNULL(col_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column schema is null", K(ret));
        } else if (OB_FAIL(column_names.push_back(&col_schema->get_column_name_str()))) {
        }
      }
      if (OB_SUCC(ret) && column_ids.count() != column_names.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column infos are not match ", K(ret));
      }
      // get origin key type by column name
      if (OB_SUCC(ret)) {
        const ObTableSchema *org_table_schema = NULL;
        uint64_t org_table_id = get_origin_tid_by_oracle_mapping_tid(table_schema_->get_table_id());
        if (OB_INVALID_ID == org_table_id) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get origin table id", K(ret), K(table_schema_->get_table_id()));
        } else if (OB_FAIL(schema_guard_->get_table_schema( org_table_id, org_table_schema))) {
        } else if (NULL == org_table_schema) {
          ret = OB_TABLE_NOT_EXIST;
          LOG_WARN("get table schema failed", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); ++i) {
            const ObString *column_name = column_names.at(i);
            const ObColumnSchemaV2 *col_schema = org_table_schema->get_column_schema(*column_name);
            if (OB_ISNULL(col_schema)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("column schema is null", K(ret), K(*column_name));
            } else if (OB_FAIL(key_cols.push_back(col_schema))) {
            }
          }
          if (OB_SUCC(ret) && key_cols.count() != column_names.count()) {
            LOG_WARN("column infos are not match ", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

// If key objects are in oracle mode, then need to convert to obj in mysql mode
// and it's find the origin type in mysql mode
// every virtual table in oracle mode must be match with one virtual table in mysql mode
int ObVirtualTableIterator::convert_key_ranges()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema is NULL", K(ret));
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
    ObObj *cells = NULL;
    void *tmp_ptr = NULL;
    if (OB_UNLIKELY(NULL == allocator_ || NULL == table_schema_ || NULL == session_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("data member is not init", K(ret), K(allocator_));
    } else if (OB_ISNULL(tmp_ptr = allocator_->alloc(reserved_column_cnt_ <= 0 ? 1 * sizeof(ObObj): reserved_column_cnt_ * sizeof(ObObj)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(ERROR, "fail to alloc cells", K(ret), K(reserved_column_cnt_));
    } else if (OB_ISNULL(cells = new (tmp_ptr) ObObj[reserved_column_cnt_ <= 0 ? 1 : reserved_column_cnt_])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to new cell array", K(ret), K(reserved_column_cnt_));
    } else {
      convert_row_.cells_ = cells;
      convert_row_.count_ = reserved_column_cnt_;
      if (OB_FAIL(convert_key_ranges())) {
      }
    }
  }
  LOG_DEBUG("key ranges", K(ret), K(key_ranges_));
  return ret;
}

int ObVirtualTableIterator::open()
{
  int ret = OB_SUCCESS;
  void *tmp_ptr = NULL;
  ObObj *cells = NULL;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("data member is not init", K(ret), K(allocator_));
  } else if (OB_ISNULL(scan_param_)
             || (NULL != scan_param_->output_exprs_ && NULL == scan_param_->op_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_ISNULL(tmp_ptr = allocator_->alloc( (reserved_column_cnt_ > 0 ? reserved_column_cnt_ : 1) * sizeof(ObObj)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(ERROR, "fail to alloc cells", K(ret), K(reserved_column_cnt_));
  } else if (OB_ISNULL(cells = new (tmp_ptr) ObObj[(reserved_column_cnt_ > 0 ? reserved_column_cnt_ : 1)])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to new cell array", K(ret), K(reserved_column_cnt_));
  } else if (OB_FAIL(init_sql_schema_guard_())) {
  } else {
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
    LOG_WARN("current row is NULL", K(ret));
  } else if (!need_convert_) {
    // don't convert
  } else {
    convert_alloc_.reuse();
    if (cols_schema_.empty() && OB_FAIL(get_all_columns_schema())) {
      LOG_WARN("failed to get columns schema", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t column_id = output_column_ids_.at(i);
      const ObColumnSchemaV2 *col_schema = cols_schema_.at(i);
      if (cur_row->get_cell(i).is_null()
          || (cur_row->get_cell(i).is_string_type() && 0 == cur_row->get_cell(i).get_data_length())
          || ob_is_empty_lob(cur_row->get_cell(i))
          || (cur_row->get_cell(i).is_timestamp() && cur_row->get_cell(i).get_timestamp() <= 0)) {
        convert_row_.cells_[i].set_null();
      } else if (OB_FAIL(ObObjCaster::to_type(col_schema->get_data_type(),
                                              col_schema->get_collation_type(),
                                              cast_ctx_,
                                              cur_row->get_cell(i),
                                              convert_row_.cells_[i]))) {
      }
    }
    cur_row = &convert_row_;
  }
  return ret;
}

int ObVirtualTableIterator::get_next_row(ObNewRow *&row)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_storage_read);
  common::ObASHTabletIdSetterGuard ash_tablet_id_guard(scan_param_ != nullptr? scan_param_->index_id_ : 0);
  ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(tablet_id_, scan_param_ != nullptr? scan_param_->index_id_ : 0);
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
      LOG_WARN("fail to inner get next row", K(ret), KPC(scan_param_));
    }
  } else if (OB_ISNULL(cur_row)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("succ to inner get next row, but row is NULL", K(ret));
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
      if (GCONF.in_upgrade_mode()) {
        LOG_WARN("column type in this row is not expected type", K(ret), K(i),
                 "table_name", table_schema_->get_table_name_str(),
                 "column_name", col_schema->get_column_name_str(),
                 K(column_id), K(cur_row->cells_[i]),
                 K(col_schema->get_data_type()), K(output_column_ids_));
      } else {
        LOG_ERROR("column type in this row is not expected type", K(ret), K(i),
                  "table_name", table_schema_->get_table_name_str(),
                  "column_name", col_schema->get_column_name_str(),
                  K(column_id), K(cur_row->cells_[i]), K(cur_row->cells_[i].get_type()),
                  K(col_schema->get_data_type()), K(output_column_ids_));
      }
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
  LOG_DEBUG("check result row", K(ret), KPC(row));
  return ret;
}

int ObVirtualTableIterator::get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(capacity < 1)) {
  } else if (OB_ISNULL(scan_param_) || OB_ISNULL(scan_param_->op_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null arguments", K(ret));
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
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_storage_read);
  common::ObASHTabletIdSetterGuard ash_tablet_id_guard(scan_param_ != nullptr? scan_param_->index_id_ : 0);
  ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(tablet_id_, scan_param_ != nullptr? scan_param_->index_id_ : 0);
  int ret = OB_SUCCESS;
  ObNewRow *row = NULL;
  if (OB_ISNULL(scan_param_)
      || OB_ISNULL(scan_param_->output_exprs_)
      || OB_ISNULL(scan_param_->op_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (VirtualSvrPair::EMPTY_VIRTUAL_TABLE_TABLET_ID == scan_param_->tablet_id_.id()) {
    row = NULL;
    ret = OB_ITER_END;
  } else if (OB_FAIL(get_next_row(row))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get next row failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(row)) {
    LOG_WARN("NULL row returned", K(ret));
  } else if (scan_param_->output_exprs_->count() > row->count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("row count less than output exprs", K(ret), K(*row),
             "output_exprs_cnt", scan_param_->output_exprs_->count());
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < scan_param_->output_exprs_->count(); i++) {
      ObExpr *expr = scan_param_->output_exprs_->at(i);
      ObDatum &datum = expr->locate_datum_for_write(scan_param_->op_->get_eval_ctx());
      if (OB_FAIL(datum.from_obj(row->cells_[i], expr->obj_datum_map_))) {
      } else if (is_lob_storage(row->cells_[i].get_type()) &&
                 OB_FAIL(ob_adjust_lob_datum(row->cells_[i], expr->obj_meta_,
                                             expr->obj_datum_map_, *allocator_, datum))) {
        LOG_WARN("adjust lob datum failed", K(ret), K(i), K(row->cells_[i].get_meta()), K(expr->obj_meta_));
      } else {
        SANITY_CHECK_RANGE(datum.ptr_, datum.len_);
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
    LOG_WARN("cur_row is not init", K(ret), K(cur_row_));
  } else if (OB_FAIL(inner_close())) {
  } else if (OB_FAIL(free_convert_ctx())) {
  } else {
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("allocator is NULL", K(ret));
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
    //tenant in table is static casted to int64_t,
    //and use statis_cast<uint64_t> for retrieving(same with schema_service)
    // After schema split, the tenant of the normal tenant schema table is 0, at this time, authentication takes session_priv.tenant_
    if (false
        && true) {
      //not current tenant's row
    } else if (0 == level_str.case_compare("db_acc")) {
      if (OB_FAIL(schema_guard_->check_db_show(session_priv, enable_role_id_array, db_name, passed))) {
      }
    } else if (0 == level_str.case_compare("table_acc")) {
      //if (OB_FAIL(priv_mgr.check_table_show(session_priv,
      if (OB_FAIL(schema_guard_->check_table_show(session_priv, enable_role_id_array, db_name, table_name, passed))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Check priv level error", K(ret));
    }
  }
  return ret;
}


int ObVirtualTableIterator::init_sql_schema_guard_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_guard_) || OB_ISNULL(scan_param_) || OB_ISNULL(scan_param_->external_object_ctx_)) {
    // don't do anything
    // ignore ret
  } else if (OB_FALSE_IT(sql_schema_guard_.set_schema_guard(schema_guard_))) {
  } else if (OB_FAIL(sql_schema_guard_.recover_schema_from_external_objects(scan_param_->external_object_ctx_->get_external_objects()))) {
  }
  return ret;
}

}// common
}// oceanbase
