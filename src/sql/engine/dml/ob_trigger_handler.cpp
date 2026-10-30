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

#include "common/datum/datum_parameter_array.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/engine/dml/ob_trigger_handler.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/dml/ob_table_update_op.h"
#include "sql/ob_spi.h"
#include "sql/pl/ob_pl_user_type.h"
#include "sql/pl/ob_pl_stmt.h"
#include "lib/oblog/ob_warning_buffer.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace storage;
using namespace share;
using namespace share::schema;
using namespace observer;
namespace sql
{
namespace {
// The descriptor borrows its stack payload only until destruct_obj returns;
// the record and its allocator retain their existing ownership.
void destruct_trigger_record(pl::ObPLRecord *record, bool keep_composite_attr)
{
  char payload[DATUM_EXTEND_PAYLOAD_SIZE];
  DatumValue value;
  value.meta_.set_ext();
  value.meta_.set_extend_type(record->get_type());
  value.datum_.ptr_ = payload;
  value.datum_.set_extend(reinterpret_cast<int64_t>(record), record->get_type(), record->get_init_size());
  pl::ObUserDefinedType::destruct_obj(value, nullptr, keep_composite_attr);
}

int copy_trigger_value(const DatumValue &source, DatumValue &destination, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  DatumParameter input;
  DatumParameter owned;
  input.datum_ = source.datum_;
  input.set_type_meta(source.get_meta());
  if (input.is_pl_extend()) {
    ret = pl::ObUserDefinedType::clone_parameter(owner, input, owned);
  } else {
    ret = DatumParameterArray::clone_parameter(input, owned, owner);
  }
  if (OB_SUCC(ret)) {
    ret = pl::ObUserDefinedType::destruct_objparam(owner, destination, nullptr);
    if (OB_SUCCESS == ret) { destination = owned.value_view(); }
    else { DatumParameterArray::release_owned(owned, owner); }
  }
  return ret;
}

bool is_delta_temp_lob(const DatumValue &value)
{
  bool is_delta = false;
  if (value.is_lob_storage() && !value.is_null() && value.has_lob_header()
      && value.get_data_length() > 0) {
    ObLobLocatorV2 locator(value.get_string(), true);
    is_delta = locator.is_valid(false) && locator.is_delta_temp_lob();
  }
  return is_delta;
}
}

int TriggerHandle::init_trigger_row(
  ObIAllocator &alloc, int64_t rowtype_col_count, pl::ObPLRecord *&record)
{
  int ret = OB_SUCCESS;
  int64_t init_size = pl::ObRecordType::get_init_size(rowtype_col_count);
  init_size += sizeof(DatumValue) * rowtype_col_count; //append hidden columns
  if (OB_ISNULL(record = reinterpret_cast<pl::ObPLRecord*>(alloc.alloc(init_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    new (record)pl::ObPLRecord(OB_INVALID_ID, rowtype_col_count);
    OZ (record->init_data(alloc, true));
    if (OB_SUCC(ret)) {
      DatumValue *cells = record->get_element();
      for (int64_t i = 0; OB_SUCC(ret) && i < rowtype_col_count; i++) {
        new (&cells[i]) DatumValue();
        cells[i].set_null();
      }
    }
  }
  return ret;
}

/*
  drop table t3;
  create table t3 (
    pk int primary key,
    aa_int int,
    bb_int int
  );
  insert into t3 values (400, 500, 500);
  commit;

  CREATE OR REPLACE TRIGGER seq_trigger_1
    BEFORE UPDATE ON t3
    FOR EACH ROW
  BEGIN
    DBMS_OUTPUT.PUT_LINE('Updating T3, original aa_int is ' || :NEW.aa_int);
    :NEW.aa_int := :NEW.aa_int * 10;
    DBMS_OUTPUT.PUT_LINE('Updating T3, modify aa_int to ' || :NEW.aa_int);
  END;
  /

  CREATE OR REPLACE TRIGGER seq_trigger_2
    BEFORE UPDATE ON t3
    FOR EACH ROW
  BEGIN
    DBMS_OUTPUT.PUT_LINE('Updating T3, original aa_int is ' || :NEW.aa_int);
    :NEW.aa_int := :NEW.aa_int * 10;
    DBMS_OUTPUT.PUT_LINE('Updating T3, modify aa_int to ' || :NEW.aa_int);
  END;
  /

  SQL> update t3 set aa_int = 700 where pk = 400;
  Updating T3, original aa_int is 700
  Updating T3, modify aa_int to 7000
  Updating T3, original aa_int is 7000
  Updating T3, modify aa_int to 70000

  1 row updated.

  * the following trigger which fired after previous trigger can see the modification
  * happened in previous trigger, so we use ObArrayHelper here which can help all triggers
  * to share the same old row and new row.
  */
int TriggerHandle::init_trigger_params(
  ObDMLRtCtx &das_ctx,
  uint64_t trigger_event,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef)
{
  UNUSED(trigger_event);
  int ret = OB_SUCCESS;
  void *when_point_params_buf = NULL;
  void *row_point_params_buf = NULL;
  int64_t param_store_size = sizeof(ParamStore);
  // TODO: This interface can be further refined, for example, when there is no when condition, the related logic of tg_when_point_params_ does not need to be executed,
  //       or when performing insert/delete operations, tg_init_point_params_ also does not need to be executed.
  if (OB_ISNULL(when_point_params_buf = das_ctx.get_exec_ctx().get_allocator().alloc(param_store_size)) ||
      OB_ISNULL(row_point_params_buf = das_ctx.get_exec_ctx().get_allocator().alloc(param_store_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ObIAllocator &allocator = das_ctx.get_exec_ctx().get_allocator();
    trig_rtdef.tg_when_point_params_ = new(when_point_params_buf)ParamStore(ObWrapperAllocator(allocator));
    trig_rtdef.tg_row_point_params_ = new(row_point_params_buf)ParamStore(ObWrapperAllocator(allocator));
    OZ (trig_rtdef.tg_when_point_params_->prepare_allocate(WHEN_POINT_PARAM_COUNT));
    OZ (trig_rtdef.tg_row_point_params_->prepare_allocate(ROW_POINT_PARAM_COUNT));

    pl::ObPLRecord *old_record = NULL;
    pl::ObPLRecord *new_record = NULL;
    // FIXME: for delete it has no new_rowid_expr, trig_col_info hasn't add rowid
    int64_t rowtype_col_count = trig_ctdef.trig_col_info_.get_rowtype_count();
    int64_t init_size = pl::ObRecordType::get_init_size(rowtype_col_count);
    if (trig_ctdef.all_tm_points_.has_when_condition() || trig_ctdef.all_tm_points_.has_row_point()) {
      OZ (init_trigger_row(das_ctx.get_exec_ctx().get_allocator(), rowtype_col_count, old_record));
      OZ (init_trigger_row(das_ctx.get_exec_ctx().get_allocator(), rowtype_col_count, new_record));
    }

    if (OB_SUCC(ret) && trig_ctdef.all_tm_points_.has_when_condition()) {
      OZ (trig_rtdef.tg_when_point_params_->at(0).set_extend(reinterpret_cast<int64_t>(old_record),
                                                        pl::PL_RECORD_TYPE, das_ctx.get_exec_ctx().get_allocator(), init_size));
      trig_rtdef.tg_when_point_params_->at(0).set_param_meta();
      OZ (trig_rtdef.tg_when_point_params_->at(1).set_extend(reinterpret_cast<int64_t>(new_record),
                                                        pl::PL_RECORD_TYPE, das_ctx.get_exec_ctx().get_allocator(), init_size));
      trig_rtdef.tg_when_point_params_->at(1).set_param_meta();
    }
    if (OB_SUCC(ret) && trig_ctdef.all_tm_points_.has_row_point()) {
      OZ (trig_rtdef.tg_row_point_params_->at(0).set_extend(reinterpret_cast<int64_t>(old_record),
                                            pl::PL_RECORD_TYPE, das_ctx.get_exec_ctx().get_allocator(), init_size));
      trig_rtdef.tg_row_point_params_->at(0).set_param_meta();
      OZ (trig_rtdef.tg_row_point_params_->at(1).set_extend(reinterpret_cast<int64_t>(new_record),
                                            pl::PL_RECORD_TYPE, das_ctx.get_exec_ctx().get_allocator(), init_size));
      trig_rtdef.tg_row_point_params_->at(1).set_param_meta();
    }
    trig_rtdef.old_record_ = old_record;
    trig_rtdef.new_record_ = new_record;
  }
  return ret;
}

int TriggerHandle::free_trigger_param_memory(ObTrigDMLRtDef &trig_rtdef, bool keep_composite_attr)
{
  int ret = OB_SUCCESS;
  pl::ObPLRecord *old_record = trig_rtdef.old_record_;
  pl::ObPLRecord *new_record = trig_rtdef.new_record_;
  if (OB_NOT_NULL(old_record)) {
    destruct_trigger_record(old_record, keep_composite_attr);
  }
  if (OB_NOT_NULL(new_record)) {
    destruct_trigger_record(new_record, keep_composite_attr);
  }

  return ret;
}

int TriggerHandle::init_param_rows(
  ObEvalCtx &eval_ctx,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef)
{
  int ret = OB_SUCCESS;
  OZ (init_param_old_row(eval_ctx, trig_ctdef, trig_rtdef));
  OZ (init_param_new_row(eval_ctx, trig_ctdef, trig_rtdef));
  return ret;
}

int TriggerHandle::init_param_old_row(
  ObEvalCtx &eval_ctx,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef)
{
  int ret = OB_SUCCESS;
  if (trig_ctdef.all_tm_points_.has_when_condition() ||
      trig_ctdef.all_tm_points_.has_row_point()) {
    DatumValue *cells = nullptr;
    if (OB_ISNULL(trig_rtdef.old_record_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(cells = trig_rtdef.old_record_->get_element())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      destruct_trigger_record(trig_rtdef.old_record_, true);
    }
    for (int64_t i = 0; i < trig_ctdef.old_row_exprs_.count() && OB_SUCC(ret); ++i) {
      ObDatum *datum;
      ObExpr *expr = trig_ctdef.old_row_exprs_.at(i);
      const bool is_udt = ob_is_geometry(expr->obj_meta_.get_type());
      if (OB_FAIL(trig_ctdef.old_row_exprs_.at(i)->eval(eval_ctx, datum))) {
      } else if (OB_ISNULL(datum))  {
        ret = OB_ERR_UNEXPECTED;
      } else if (is_udt) {
        DatumValue source(*datum, expr->obj_meta_);
        DatumValue converted;
        if (OB_FAIL(OB_ISNULL(eval_ctx.exec_ctx_.get_sql_ctx()))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(convert_sql_type_to_pl_type(
                       OB_NOT_NULL(eval_ctx.exec_ctx_.get_my_session()),
                       OB_NOT_NULL(eval_ctx.exec_ctx_.get_sql_ctx()->schema_guard_),
                       source, converted))) {
        } else if (OB_FAIL(copy_trigger_value(
                       converted, cells[i], *trig_rtdef.old_record_->get_allocator()))) {
        }
      } else if (OB_FAIL(copy_trigger_value(
                     DatumValue(*datum, expr->obj_meta_), cells[i],
                     *trig_rtdef.old_record_->get_allocator()))) {
      }
      LOG_DEBUG("debug init param old expr", K(ret), K(i),
        K(ObToStringExpr(eval_ctx, *expr)));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(trig_ctdef.rowid_old_expr_)) {
      ObDatum *datum;
      if (OB_FAIL(trig_ctdef.rowid_old_expr_->eval(eval_ctx, datum))) {
      }
      LOG_DEBUG("debug init param rowid old expr", K(ret),
        K(ObToStringExpr(eval_ctx, *trig_ctdef.rowid_old_expr_)));
    }
  }
  return ret;
}

int TriggerHandle::init_param_new_row(
  ObEvalCtx &eval_ctx,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef)
{
  int ret = OB_SUCCESS;
  if (trig_ctdef.all_tm_points_.has_when_condition() ||
      trig_ctdef.all_tm_points_.has_row_point()) {
    DatumValue *cells = nullptr;
    if (OB_ISNULL(trig_rtdef.new_record_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(cells = trig_rtdef.new_record_->get_element())){
      ret = OB_ERR_UNEXPECTED;
    } else {
      destruct_trigger_record(trig_rtdef.new_record_, true);
    }
    for (int64_t i = 0; i < trig_ctdef.new_row_exprs_.count() && OB_SUCC(ret); ++i) {
      ObDatum *datum;
      ObExpr *expr = trig_ctdef.new_row_exprs_.at(i);
      const bool is_udt = ob_is_geometry(expr->obj_meta_.get_type());
      if (OB_FAIL(trig_ctdef.new_row_exprs_.at(i)->eval(eval_ctx, datum))) {
      } else if (OB_ISNULL(datum))  {
        ret = OB_ERR_UNEXPECTED;
      } else if (is_udt) {
        DatumValue source(*datum, expr->obj_meta_);
        DatumValue converted;
        if (OB_FAIL(OB_ISNULL(eval_ctx.exec_ctx_.get_sql_ctx()))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(convert_sql_type_to_pl_type(
                       OB_NOT_NULL(eval_ctx.exec_ctx_.get_my_session()),
                       OB_NOT_NULL(eval_ctx.exec_ctx_.get_sql_ctx()->schema_guard_),
                       source, converted))) {
        }
        if (OB_SUCC(ret)) {
          OZ (copy_trigger_value(converted, cells[i], *trig_rtdef.new_record_->get_allocator()));
        }
      } else if (!trig_ctdef.trig_col_info_.get_flags()[i].is_rowid_
          && expr->obj_meta_.get_type() == ObCharType) {
        DatumParameter padded;
        padded.datum_ = *datum;
        padded.set_type_meta(expr->obj_meta_);
        // Pad fixed CHAR values according to session SQL mode and the
        // expression's character/byte length semantics.
        const common::SqlValueType &col_type = expr->obj_meta_.get_type();
        common::ObAccuracy accuracy;
        accuracy.length_ = expr->max_length_;
        accuracy.length_semantics_ = expr->datum_meta_.length_semantics_;
        if (OB_FAIL(ObSPIService::spi_pad_char_or_varchar(eval_ctx.exec_ctx_.get_my_session(),
                                                          col_type,
                                                          accuracy,
                                                          &eval_ctx.exec_ctx_.get_allocator(),
                                                          &padded))) {
        } else {
          OZ (copy_trigger_value(padded.value_view(), cells[i],
                                 *trig_rtdef.new_record_->get_allocator()));
        }
      } else {
        OZ (copy_trigger_value(DatumValue(*datum, expr->obj_meta_), cells[i],
                               *trig_rtdef.new_record_->get_allocator()));
      }
      LOG_DEBUG("debug init param new expr", K(ret), K(i),
        K(ObToStringExpr(eval_ctx, *expr)));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(trig_ctdef.rowid_new_expr_)) {
      ObDatum *datum;
      if (OB_FAIL(trig_ctdef.rowid_new_expr_->eval(eval_ctx, datum))) {
      }
      LOG_DEBUG("debug init param rowid new expr", K(ret),
        K(ObToStringExpr(eval_ctx, *trig_ctdef.rowid_new_expr_)));
    }
  }
  return ret;
}


int TriggerHandle::set_rowid_into_row(
  const ObTriggerColumnsInfo &cols,
  ObEvalCtx &eval_ctx,
  ObExpr *src_expr,
  pl::ObPLRecord *record)
{
  int ret = OB_SUCCESS;
  DatumValue* cells = nullptr;
  CK (OB_NOT_NULL(cols.get_flags()));
  CK (OB_NOT_NULL(record));
  CK (OB_NOT_NULL(record->get_element()));
  CK (OB_NOT_NULL(record->get_allocator()));
  OX (cells = record->get_element());
  // We can't distinguish urowid column and trigger rowid column in ObTriggerColumns,
  // but we can ensure that the trigger rowid column must behind the urowid column if exist,
  // because the trigger rowid column mock in optimizer
  for (int64_t i = cols.get_count() - 1; OB_SUCC(ret) && i >= 0; ++i) {
    if (cols.get_flags()[i].is_rowid_) {
      ObDatum *datum;
      if (OB_FAIL(src_expr->eval(eval_ctx, datum))) {
      } else if (OB_ISNULL(datum)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(copy_trigger_value(DatumValue(*datum, src_expr->obj_meta_),
                                            cells[i], *record->get_allocator()))) {
      } else {
        LOG_DEBUG("set_rowid_into_row done", K(ret), K(ObToStringExpr(eval_ctx, *src_expr)));
      }
      break;
    }
  }
  return ret;
}

// calculate rowid and set into new_record that is for pl
int TriggerHandle::do_handle_rowid_before_row(
  ObTableModifyOp &dml_op,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef,
  uint64_t tg_event)
{
  // ObURowIDType
  int ret = OB_SUCCESS;
  if (NULL != trig_ctdef.rowid_new_expr_ && ObTriggerEvents::is_insert_event(tg_event)) {
    // we set a invalid rowid obj into old_row/new_row
    ret = OB_NOT_IMPLEMENT;
  } else if (NULL != trig_ctdef.rowid_old_expr_ && ObTriggerEvents::is_delete_event(tg_event)) {
    // new.rowid should be same with old.rowid
    OZ (TriggerHandle::set_rowid_into_row(trig_ctdef.trig_col_info_,
                                          dml_op.get_eval_ctx(),
                                          trig_ctdef.rowid_old_expr_,
                                          trig_rtdef.new_record_));
  }
  return ret;
}

int TriggerHandle::calc_when_condition(
  ObTableModifyOp &dml_op,
  ObTrigDMLRtDef &trig_rtdef,
  uint64_t trigger_id,
  bool &need_fire)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(trig_rtdef.tg_when_point_params_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(calc_trigger_routine(dml_op.get_exec_ctx(),
                           trigger_id, ROUTINE_IDX_CALC_WHEN,
                           *trig_rtdef.tg_when_point_params_, &need_fire))) {
  }
  return ret;
}

// PL copy-out owns its new descriptors in TriggerExec. The DML owns the
// initial descriptors and OLD/NEW records across all row timing points.
int TriggerHandle::stabilize_trigger_params(
  const DatumParameter *initial, int64_t count, ParamStore &params,
  ObIAllocator &temporary_owner, ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  if (count != params.count() || count < 0 || count > ROW_POINT_PARAM_COUNT
      || (count != 0 && initial == nullptr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; i < count; ++i) {
      int copy_ret = OB_SUCCESS;
      const DatumParameter &saved = initial[i];
      DatumParameter published = params.at(i);
      DatumParameter temporary_output = published;
      pl::ObPLRecord *original = nullptr;
      pl::ObPLRecord *returned = nullptr;
      // is_pl_extend() itself reads the Extend payload. Check descriptor
      // shape and non-null pointers before invoking payload-reading helpers.
      // Both owners still live here: DML for initial, TriggerExec for OUT.
      if (!saved.datum_.is_native_extend() || saved.datum_.ptr_ == nullptr
          || !published.datum_.is_native_extend() || published.datum_.ptr_ == nullptr
          || saved.get_extend_type() != pl::PL_RECORD_TYPE
          || published.get_extend_type() != pl::PL_RECORD_TYPE
          || !saved.is_pl_extend() || !published.is_pl_extend()
          || (original = reinterpret_cast<pl::ObPLRecord *>(saved.get_ext())) == nullptr
          || (returned = reinterpret_cast<pl::ObPLRecord *>(published.get_ext())) == nullptr) {
        copy_ret = OB_ERR_UNEXPECTED;
      } else if (returned != original) {
        // Preserve the record identity shared with trig_rtdef.old/new_record_.
        // assign copies fields into the original record's allocator; merely
        // copying the replacement pointer would retain a temporary pointee.
        copy_ret = original->assign(returned, original->get_allocator());
      }
      if (copy_ret == OB_SUCCESS) {
        // Preserve all OUT declaration/accuracy/flags but reuse the initial
        // DML-owned payload. No allocation is needed for an unchanged record.
        const int32_t size = published.get_val_len();
        const uint8_t subtype = published.datum_.get_extend_type();
        const uint8_t collation_level = published.datum_.get_extend_collation_level();
        const uint8_t collation = published.datum_.get_extend_collation();
        published.datum_.ptr_ = saved.datum_.ptr_;
        published.datum_.set_extend(reinterpret_cast<int64_t>(original),
            subtype, size, collation_level, collation);
        params.at(i) = published;
      } else {
        // A failed copy cannot leave a dangling temporary descriptor behind.
        params.at(i) = saved;
        if (ret == OB_SUCCESS) { ret = copy_ret; }
      }
      if (original != nullptr && returned != nullptr && returned != original) {
        // A replacement was genuinely cloned by PL copy-out. Retire its
        // internal record allocator before TriggerExec releases its storage.
        const int cleanup_ret = pl::ObUserDefinedType::destruct_objparam(
            temporary_owner, temporary_output, session);
        if (ret == OB_SUCCESS) { ret = cleanup_ret; }
      }
    }
  }
  return ret;
}

int TriggerHandle::calc_trigger_routine(
  ObExecContext &exec_ctx,
  uint64_t trigger_id,
  uint64_t routine_id,
  ParamStore &params)
{
  int ret = OB_SUCCESS;
  OZ (calc_trigger_routine(exec_ctx, trigger_id, routine_id, params, nullptr));
  return ret;
}

int TriggerHandle::calc_trigger_routine(
  ObExecContext &exec_ctx,
  uint64_t trigger_id,
  uint64_t routine_id,
  ParamStore &params,
  bool *condition)
{
  int ret = OB_SUCCESS;
  DatumParameter result;
  DatumParameter initial[ROW_POINT_PARAM_COUNT];
  if (params.count() > ROW_POINT_PARAM_COUNT) { return OB_ERR_UNEXPECTED; }
  const int64_t count = params.count();
  for (int64_t i = 0; i < count; ++i) { initial[i] = params.at(i); }
  ObArray<int64_t> path;
  trigger_id = ObTriggerInfo::get_trigger_spec_package_id(trigger_id);
  bool old_flag = false;
  common::ObArenaAllocator tmp_allocator(common::ObMemAttr("TriggerExec"));
  CK (OB_NOT_NULL(exec_ctx.get_my_session()));
  OX (old_flag = exec_ctx.get_my_session()->is_for_trigger_package());
  OX (exec_ctx.get_my_session()->set_for_trigger_package(true));
  OV (OB_NOT_NULL(exec_ctx.get_pl_engine()));
  if (OB_SUCC(ret)) {
    // TODO: Replace this manual TSI buffer switch with a common nested
    // diagnostic scope for PL, triggers, and inner SQL.  The scope must make
    // discard-on-success/propagate-on-error, merge, and discard-all policies
    // explicit, and no nested execution should reset the outer diagnostic area.
    // Trigger warnings are private to the enclosing DML, but detailed errors
    // still belong to the caller.  Execute with a private diagnostic area so
    // local SQLWARNING handlers work, then copy back only an unhandled error.
    ObWarningBuffer trigger_diagnostics;
    ObWarningBuffer *outer_diagnostics = ob_get_tsi_warning_buffer();
    ob_setup_tsi_warning_buffer(&trigger_diagnostics);
    try {
      OZ (exec_ctx.get_pl_engine()->execute(
        exec_ctx, tmp_allocator, trigger_id, routine_id, path, params, result),
          trigger_id, routine_id, params);
    } catch (...) {
      // The caller retains these parameters even when PL throws. Restore
      // their descriptors before the temporary result arena is destroyed.
      stabilize_trigger_params(initial, count, params, tmp_allocator, exec_ctx.get_my_session());
      ob_setup_tsi_warning_buffer(outer_diagnostics);
      exec_ctx.get_my_session()->set_for_trigger_package(old_flag);
      throw;
    }
    const int copy_ret = stabilize_trigger_params(initial, count, params, tmp_allocator, exec_ctx.get_my_session());
    if (OB_SUCC(ret)) { ret = copy_ret; }
    // WHEN is the only routine result consumed by this interface. Project it
    // while the temporary native scalar payload is still alive.
    if (OB_SUCC(ret) && condition != nullptr) { *condition = result.is_true(); }
    ob_setup_tsi_warning_buffer(outer_diagnostics);
    if (OB_FAIL(ret) && OB_ERR_FUNCTION_UNKNOWN != ret
        && OB_NOT_NULL(outer_diagnostics)
        && (OB_MAX_ERROR_CODE != trigger_diagnostics.get_err_code()
            || '\0' != trigger_diagnostics.get_err_msg()[0])) {
      outer_diagnostics->set_error(trigger_diagnostics.get_err_msg(),
                                   trigger_diagnostics.get_err_code());
      outer_diagnostics->set_error_line_column(
          trigger_diagnostics.get_error_line(),
          trigger_diagnostics.get_error_column());
      outer_diagnostics->set_sql_state(trigger_diagnostics.get_sql_state());
    }
  }
  CK (OB_NOT_NULL(exec_ctx.get_my_session()));
  OZ (exec_ctx.get_my_session()->reset_all_package_state_by_dbms_session());
  if (exec_ctx.get_my_session()->is_for_trigger_package()) {
    // whether `ret == OB_SUCCESS`, need to restore flag
    exec_ctx.get_my_session()->set_for_trigger_package(old_flag);
  }
  return ret;
}

int TriggerHandle::check_and_update_new_row(
  ObTableModifyOp *self_op,
  const ObTriggerColumnsInfo &columns,
  ObEvalCtx &eval_ctx,
  const ObIArray<ObExpr *> &new_row_exprs,
  pl::ObPLRecord *new_record,
  bool check)
{
  int ret = OB_SUCCESS;
  // plus 1 is for rowid
  if (OB_ISNULL(new_record)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    bool updated = false;
    int64_t op_row_idx = 0;
    DatumValue *new_cells = new_record->get_element();
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.get_count(); i++) {
      if (columns.get_flags()[i].is_rowid_) {
      } else if (check) {
        ObDatum *datum;
        DatumValue new_value;
        DatumValue current_value = new_cells[i];
        if (OB_FAIL(new_row_exprs.at(i)->eval(eval_ctx, datum))) {
        } else if (OB_ISNULL(datum))  {
          ret = OB_ERR_UNEXPECTED;
        } else {
          new_value = DatumValue(*datum, new_row_exprs.at(i)->obj_meta_);
          bool is_strict_equal = false;
          if (new_value.get_type() == current_value.get_type()
              && new_value.is_lob_storage()) {
            common::ObArenaAllocator lob_allocator(ObModIds::OB_LOB_READER, OB_MALLOC_NORMAL_BLOCK_SIZE);
            const common::ObLobReadOptions *lob_read_options = nullptr;
            DatumValue comparable_new = new_value;
            DatumValue comparable_current = current_value;
            if (is_delta_temp_lob(new_value) || is_delta_temp_lob(current_value)) {
              is_strict_equal = false;
            } else if (OB_FAIL(eval_ctx.exec_ctx_.get_lob_read_options(lob_read_options))) {
            } else if (OB_FAIL(ObTextStringIter::convert_outrow_lob_to_inrow_templob(
                           new_value, comparable_new, lob_read_options, lob_allocator))) {
            } else if (OB_FAIL(ObTextStringIter::convert_outrow_lob_to_inrow_templob(
                           current_value, comparable_current, lob_read_options, lob_allocator))) {
            } else {
              is_strict_equal = strict_equal_datum_values(comparable_new, comparable_current);
            }
          } else if (new_value.get_type() == current_value.get_type()) {
            is_strict_equal = strict_equal_datum_values(new_value, current_value);
          }
          if (OB_FAIL(ret)) {
          } else if(!is_strict_equal) {
            updated = true;
          }
        }
      }
    }
    LOG_DEBUG("debug update row", K(updated), K(lbt()));
    // updated
    if (OB_SUCC(ret) && (updated || !check)) {
      self_op->clear_dml_evaluated_flag();
    }
    // case: create table t11( c2 generated always as (c1 + 1), c1 int);
    // The generated column c2 is before normal column c1
    // so we need calculate normal column value firstly, then generate column value
    // Firstly calculate the basic columns
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.get_count(); i++) {
      if (!columns.get_flags()[i].is_gen_col_) {
        ObExpr *expr = new_row_exprs.at(i);
        if (OB_ISNULL(expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          DatumValue current_cell;
          ObDatum &write_datum = expr->locate_datum_for_write(eval_ctx);
          if (ob_is_geometry(expr->obj_meta_.get_type())) {
            DatumValue converted;
            if (OB_FAIL(convert_pl_type_to_sql_type(new_cells[i], converted,
                                                    expr->obj_meta_.get_type()))) {
            } else if (OB_FAIL(copy_trigger_value(converted, current_cell,
                                                  eval_ctx.exec_ctx_.get_allocator()))) {
            }
          } else if (OB_FAIL(copy_trigger_value(new_cells[i], current_cell,
                                                eval_ctx.exec_ctx_.get_allocator()))) {
          }
          if (OB_FAIL(ret)) {
          } else {
            write_datum = current_cell.datum_;
          }
          if (OB_SUCC(ret) && current_cell.is_lob_storage()
              && OB_FAIL(ob_adjust_lob_datum(eval_ctx.exec_ctx_, write_datum,
                      current_cell.get_meta(), expr->obj_meta_,
                      eval_ctx.exec_ctx_.get_allocator(), write_datum))) {
          } else if (OB_SUCC(ret)) {
            expr->set_evaluated_flag(eval_ctx);
            LOG_DEBUG("trigger write new datum", K(i),
              K(ObToStringExpr(eval_ctx, *new_row_exprs.at(i))));
          }
        }
      }
    }
    // Secondly calculate the generated column
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.get_count(); i++) {
      if (columns.get_flags()[i].is_gen_col_) {
        // for generated column, the dependent column is already evaluated
        // so the generated column can evalate by eval_function
        ObDatum *datum;
        if (OB_FAIL(new_row_exprs.at(i)->eval(eval_ctx, datum))) {
        } else {
          LOG_DEBUG("trigger write new datum", K(new_cells[i]), K(i),
            K(ObToStringExpr(eval_ctx, *new_row_exprs.at(i))));
        }
      }
    }
  }
  return ret;
}

int TriggerHandle::do_handle_before_row(
  ObTableModifyOp &dml_op,
  ObDASDMLBaseCtDef &das_base_ctdef,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef)
{
  UNUSED(das_base_ctdef);
  int ret = OB_SUCCESS;
  if (trig_ctdef.all_tm_points_.has_before_row()) {
    uint64_t tg_event = trig_ctdef.tg_event_;
    if (OB_FAIL(do_handle_rowid_before_row(dml_op, trig_ctdef, trig_rtdef, tg_event))) {
    } else {
      ObSQLSessionInfo *my_session = dml_op.get_exec_ctx().get_my_session();
      stmt::StmtType saved_stmt_type = stmt::T_NONE;
      bool need_fire = true;
      LOG_DEBUG("TRIGGER", K(trig_ctdef.tg_args_),
        K(trig_ctdef.all_tm_points_.has_before_row()),
        K(trig_ctdef.all_tm_points_.has_after_row()),
        K(trig_ctdef.all_tm_points_.has_before_point()),
        K(trig_ctdef.all_tm_points_.has_after_point()));
      OV (OB_NOT_NULL(my_session));
      for (int64_t i = 0; OB_SUCC(ret) && i < trig_ctdef.tg_args_.count(); i++) {
        const ObTriggerArg &tg_arg = trig_ctdef.tg_args_.at(i);
        if (!tg_arg.has_before_row_point() || !tg_arg.has_trigger_events(tg_event)) {
          need_fire = false;
        } else if (tg_arg.has_when_condition()) {
          OZ (calc_when_condition(dml_op, trig_rtdef, tg_arg.get_trigger_id(), need_fire));
        } else {
          need_fire = true;
        }
        LOG_DEBUG("TRIGGER handle before row", K(need_fire), K(i), K(lbt()));
        if (OB_SUCC(ret) && need_fire) {
          if (OB_ISNULL(trig_rtdef.tg_row_point_params_)) {
            ret = OB_NOT_INIT;
          } else {
            const ObTableModifySpec &modify_spec = static_cast<const ObTableModifySpec&>(dml_op.get_spec());
            if (OB_FAIL(calc_before_row(dml_op, trig_rtdef, tg_arg.get_trigger_id()))) {
            } else if ((ObTriggerEvents::is_update_event(tg_event) ||
                  ObTriggerEvents::is_insert_event(tg_event))) {
                if (OB_FAIL(check_and_update_new_row(&dml_op,
                                              trig_ctdef.trig_col_info_,
                                              dml_op.get_eval_ctx(),
                                              trig_ctdef.new_row_exprs_,
                                              trig_rtdef.new_record_,
                                              ObTriggerEvents::is_update_event(tg_event)))) {
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int TriggerHandle::calc_after_row(
  ObTableModifyOp &dml_op, ObTrigDMLRtDef &trig_rtdef, uint64_t trigger_id)
{
  int ret = OB_SUCCESS;
  uint64_t idx = ROUTINE_IDX_AFTER_ROW;
  if (OB_ISNULL(trig_rtdef.tg_row_point_params_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(calc_trigger_routine(dml_op.get_exec_ctx(),
                                          trigger_id, idx,
                                          *trig_rtdef.tg_row_point_params_))) {
  }
  return ret;
}

int TriggerHandle::calc_before_row(
  ObTableModifyOp &dml_op, ObTrigDMLRtDef &trig_rtdef, uint64_t trigger_id)
{
  int ret = OB_SUCCESS;
  uint64_t idx = ROUTINE_IDX_BEFORE_ROW;
  if (OB_ISNULL(trig_rtdef.tg_row_point_params_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(calc_trigger_routine(dml_op.get_exec_ctx(),
                                          trigger_id, idx,
                                          *trig_rtdef.tg_row_point_params_))) {
  }
  return ret;
}

// only the first trigger need run statement trigger
// to compatible with oralce
int TriggerHandle::do_handle_rowid_after_row(
  ObTableModifyOp &dml_op,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef,
  uint64_t tg_event)
{
  int ret = OB_SUCCESS;
  if (NULL != trig_ctdef.rowid_new_expr_ && ObTriggerEvents::is_insert_event(tg_event)) {
    // old.rowid should be same with new.rowid for insert
    trig_ctdef.rowid_new_expr_->get_eval_info(dml_op.get_eval_ctx()).clear_evaluated_flag();
    OZ (TriggerHandle::set_rowid_into_row(trig_ctdef.trig_col_info_,
                                          dml_op.get_eval_ctx(),
                                          trig_ctdef.rowid_new_expr_,
                                          trig_rtdef.old_record_));
    OZ (TriggerHandle::set_rowid_into_row(trig_ctdef.trig_col_info_,
                                          dml_op.get_eval_ctx(),
                                          trig_ctdef.rowid_new_expr_,
                                          trig_rtdef.new_record_));
  } else if (NULL != trig_ctdef.rowid_old_expr_ && ObTriggerEvents::is_delete_event(tg_event)) {
      // new.rowid should be same with old.rowid
    OZ (TriggerHandle::set_rowid_into_row(trig_ctdef.trig_col_info_,
                                          dml_op.get_eval_ctx(),
                                          trig_ctdef.rowid_old_expr_,
                                          trig_rtdef.new_record_));
  }
  return ret;
}

int TriggerHandle::do_handle_after_row(
  ObTableModifyOp &dml_op,
  const ObTrigDMLCtDef &trig_ctdef,
  ObTrigDMLRtDef &trig_rtdef,
  uint64_t tg_event)
{
  int ret = OB_SUCCESS;
  if (trig_ctdef.all_tm_points_.has_after_row()) {
    if (OB_FAIL(do_handle_rowid_after_row(dml_op, trig_ctdef, trig_rtdef, tg_event))) {
    } else {
      bool need_fire = false;
      for (int64_t i = 0; OB_SUCC(ret) && i < trig_ctdef.tg_args_.count(); i++) {
        const ObTriggerArg &tg_arg = trig_ctdef.tg_args_.at(i);
        if (!tg_arg.has_after_row_point() || !tg_arg.has_trigger_events(tg_event)) {
          need_fire = false;
        } else if (tg_arg.has_when_condition()) {
          OZ (calc_when_condition(dml_op, trig_rtdef, tg_arg.get_trigger_id(), need_fire));
        } else {
          need_fire = true;
        }
        if (need_fire) {
          OZ (calc_after_row(dml_op, trig_rtdef, tg_arg.get_trigger_id()));
        }
      }
    }
  }
  return ret;
}

int64_t TriggerHandle::get_routine_param_count(const uint64_t routine_id)
{
  int64_t count = OB_INVALID_COUNT;
  count = ROW_POINT_PARAM_COUNT;
  return count;
}

int TriggerHandle::convert_sql_type_to_pl_type(bool has_session,
                                               bool has_schema_guard,
                                               const DatumValue &src,
                                               DatumValue &dst)
{
  int ret = OB_SUCCESS;
  if (src.is_null()) {
    if (!has_schema_guard) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // The old cast path leaves its default NULL destination unchanged.
    }
  } else if (!has_session) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // The current common-value cast table has no geometry -> PL Extend cast;
    // preserve its OB_NOT_SUPPORTED result without materializing a legacy value.
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

int TriggerHandle::convert_pl_type_to_sql_type(const DatumValue &src,
                                               DatumValue &dst,
                                               SqlValueType obj_type)
{
  int ret = OB_SUCCESS;
  if (ob_is_geometry(obj_type)) {
    if (src.is_null()) {
      dst = src;
    } else {
      // The current common-value cast table has no PL Extend -> geometry cast.
      ret = OB_NOT_SUPPORTED;
    }
  } else {
    dst = src;
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
