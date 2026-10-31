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
#include "ob_physical_plan_ctx.h"
#include "share/ob_autoincrement_service.h"
#include "src/sql/engine/ob_des_exec_context.h"
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/ob_sql_context.h"
#include "share/sql_udt_support.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace transaction;
namespace sql
{
ObPhysicalPlanCtx::ObPhysicalPlanCtx(common::ObIAllocator &allocator)
    : allocator_(allocator),
      tsc_snapshot_timestamp_(0),
      ts_timeout_us_(0),
      consistency_level_(INVALID_CONSISTENCY),
      param_store_( (ObWrapperAllocator(&allocator_)) ),
      native_parameters_ready_(false),
      original_param_cnt_(0),
      param_frame_capacity_(0),
      sql_mode_(SMO_DEFAULT),
      autoinc_params_(allocator),
      last_insert_id_session_(0),
      expr_op_size_(0),
      is_ignore_stmt_(false),
      runtime_schema_version_(OB_INVALID_VERSION),
      orig_question_mark_cnt_(0),
      srs_version_(OB_INVALID_VERSION),
      array_param_groups_(),
      affected_rows_(0),
      is_affect_found_row_(false),
      found_rows_(0),
      phy_plan_(NULL),
      curr_col_index_(-1),
      autoinc_cache_handle_(NULL),
      last_insert_id_to_client_(0),
      last_insert_id_cur_stmt_(0),
      autoinc_id_tmp_(0),
      autoinc_col_value_(0),
      last_insert_id_with_expr_(false),
      last_insert_id_changed_(false),
      row_matched_count_(0),
      row_duplicated_count_(0),
      row_deleted_count_(0),
      warning_count_(0),
      is_error_ignored_(false),
      is_select_into_(false),
      is_result_accurate_(true),
      foreign_key_checks_(true),
      exec_ctx_(NULL),
      table_row_count_list_(allocator),
      batched_stmt_param_idxs_(allocator),
      implicit_cursor_infos_(allocator),
      cur_stmt_id_(-1),
      is_or_expand_transformed_(false),
      is_multi_dml_(false),
      field_array_(nullptr),
      is_ps_protocol_(false),
      plan_start_time_(0),
      ps_fixed_array_index_(nullptr),
      subschema_ctx_(allocator_),
      all_local_session_vars_(allocator),
      total_memstore_read_row_count_(0),
      total_ssstore_read_row_count_(0),
      check_pdml_affected_rows_(false)
{
}

ObPhysicalPlanCtx::~ObPhysicalPlanCtx()
{
  destroy();
}

int ObPhysicalPlanCtx::reserve_param_space(int64_t param_count)
{
  int ret = OB_SUCCESS;
  const int64_t old_count = param_store_.count();
  if (param_count < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(param_store_.reserve(param_count))) {
  }
  for (int64_t i = old_count; OB_SUCC(ret) && i < param_count; ++i) {
    ret = param_store_.push_back(DatumParameter());
  }
  if (OB_SUCC(ret) && native_parameters_ready_ && param_store_.count() > old_count) {
    ret = extend_param_frame(old_count);
  }
  if (OB_FAIL(ret)) {
    while (param_store_.count() > old_count) { param_store_.pop_back(); }
    while (native_parameter_types_.count() > old_count) { native_parameter_types_.pop_back(); }
  }
  return ret;
}

int ObPhysicalPlanCtx::append_parameter_input(const DatumParameter &parameter)
{
  return native_parameters_ready_ ? OB_STATE_NOT_MATCH : param_store_.push_back(parameter);
}

int ObPhysicalPlanCtx::assign_parameter_inputs(const ParamStore &parameters)
{
  int ret = OB_SUCCESS;
  if (&parameters == &param_store_) { return ret; }
  // Reserve before invalidating the current binding. Descriptor assignment
  // after reserve is trivial and cannot allocate; input bytes stay owned by
  // their producer until init_datum_param_store imports them into final pages.
  if (OB_FAIL(param_store_.reserve(parameters.count()))) {
  } else {
    native_parameters_ready_ = false;
    native_parameter_types_.reuse();
    param_frame_ptrs_.reuse();
    param_frame_capacity_ = 0;
    ret = param_store_.assign(parameters);
  }
  return ret;
}

int ObPhysicalPlanCtx::truncate_runtime_parameters(int64_t count)
{
  int ret = OB_SUCCESS;
  if (count < original_param_cnt_ || count < 0 || count > param_store_.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    while (param_store_.count() > count) { param_store_.pop_back(); }
    while (native_parameter_types_.count() > count) { native_parameter_types_.pop_back(); }
    // Retain allocated pages/capacity so compiled frame pointers do not move.
    // Logical count gates every native value/type accessor and PX serialization.
  }
  return ret;
}

void ObPhysicalPlanCtx::mark_batch_parameters()
{
  for (int64_t i = 0; i < param_store_.count(); ++i) {
    param_store_.at(i).get_param_flag().is_batch_parameter_ = true;
  }
}

int ObPhysicalPlanCtx::init_datum_param_store()
{
  native_parameters_ready_ = false;
  native_parameter_types_.reuse();
  param_frame_ptrs_.reuse();
  param_frame_capacity_ = 0;
  // Array inputs retain all groups in the execution arena. Ordinary inputs
  // are imported once below, then their descriptors borrow the native slot.
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < param_store_.count(); ++i) {
    if (param_store_.at(i).is_ext_sql_array()) {
      DatumParameter owned;
      if (OB_FAIL(DatumParameterArray::clone_parameter(param_store_.at(i), owned, allocator_))) {
      } else { param_store_.at(i) = owned; }
    }
  }
  if (OB_SUCC(ret)) { ret = extend_param_frame(0); }
  if (OB_SUCC(ret)) { native_parameters_ready_ = true; }
  else {
    native_parameter_types_.reuse();
    param_frame_ptrs_.reuse();
    param_frame_capacity_ = 0;
  }
  return ret;
}

int ObPhysicalPlanCtx::prepare_runtime_parameter(const DatumParameter &source,
    Datum &value, const SqlType *&type, int64_t index)
{
  int ret = OB_SUCCESS;
  const SqlType *candidate_type = nullptr;
  const SqlTypeMeta &meta = source.get_type_meta();
  const bool scalar = !meta.is_enum_or_set() && !meta.is_enumset_inner_type()
      && !meta.is_collection_sql_type() && meta.get_type() != ObExtendType
      && meta.get_type() != ObUserDefinedSQLType;
  if (scalar) {
    SqlType resolved;
    if (OB_FAIL(SqlType::from_legacy(meta, source.get_accuracy(), resolved))) {
    } else {
      // Share an already prepared declaration within this execution context.
      // Ordinary scalar preparation needs neither a temporary arena nor one
      // heap allocation per repeated BIGINT key in the same parameter list.
      const SqlType *previous = get_native_parameter_type(index);
      const SqlType *adjacent = get_native_parameter_type(index - 1);
      if (previous != nullptr && previous->equals(resolved)) { candidate_type = previous; }
      else if (adjacent != nullptr && adjacent->equals(resolved)) { candidate_type = adjacent; }
      if (candidate_type == nullptr) { ret = resolved.clone_to(allocator_, candidate_type); }
    }
  } else {
    ObArenaAllocator temporary("ParamTypePrep");
    const SqlType *resolved = nullptr;
    if (exec_ctx_ == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // Runtime type resolution may populate this execution context's lazy type cache.
      ret = ObExprUtil::resolve_sql_type(*const_cast<ObExecContext *>(exec_ctx_), meta, source.get_accuracy(),
          source.get_subschema_id(), temporary, resolved);
    }
    if (OB_SUCC(ret)) {
      const SqlType *previous = get_native_parameter_type(index);
      if (previous != nullptr && previous->equals(*resolved)) { candidate_type = previous; }
      else { ret = resolved->clone_to(allocator_, candidate_type); }
    }
  }
  if (OB_SUCC(ret)) {
    const DatumCopyPlan plan(*candidate_type);
    Datum imported;
    Datum owned;
    if (OB_FAIL(plan.import_legacy(source.datum_, imported))) {
    } else if (candidate_type->requires_resource_clone()) {
      // PL addresses retain their lvalue identity and existing PL-domain
      // owner. Own only the execution wrapper; cloning the pointee here
      // would redirect OUT/INOUT and object-field writes to a different object.
      int64_t bytes = 0;
      if (OB_FAIL(plan.carrier_span_size(imported, bytes))) {
      } else {
        char *buffer = bytes == 0 ? nullptr : static_cast<char *>(allocator_.alloc(bytes));
        if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          int64_t position = 0;
          ret = plan.copy_carrier_span_borrowing_resources(imported, owned, buffer, bytes, position);
        }
      }
    } else { ret = plan.deep_copy(imported, owned, allocator_); }
    if (OB_SUCC(ret)) { value = owned; type = candidate_type; }
  }
  return ret;
}

int ObPhysicalPlanCtx::publish_runtime_parameter(int64_t index, const DatumParameter &source,
    const Datum &value, const SqlType &type, bool preserve_array_input)
{
  int ret = OB_SUCCESS;
  const DatumCopyPlan plan(type);
  DatumParameter compatibility = source;
  ObDatum legacy;
  bool direct_borrow = true;
  if (index < 0 || index >= param_store_.count() || index >= native_parameter_types_.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!preserve_array_input) {
    ret = plan.borrow_legacy(value, legacy);
    if (ret == OB_NOT_SUPPORTED) {
      direct_borrow = false;
      int64_t bytes = 0;
      ret = plan.legacy_payload_size(value, bytes);
      if (OB_SUCC(ret)) {
        char *buffer = bytes == 0 ? nullptr : static_cast<char *>(allocator_.alloc(bytes));
        if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          int64_t position = 0;
          ret = plan.export_legacy(value, legacy, buffer, bytes, position);
        }
      }
    }
    if (OB_SUCC(ret)) { compatibility.set_datum(legacy); }
  }
  if (OB_SUCC(ret)) {
    Datum *slot = nullptr;
    ObEvalInfo *info = nullptr;
    get_param_frame_info(index, slot, info);
    *slot = value;
    native_parameter_types_.at(index) = &type;
    if (!preserve_array_input) {
      if (direct_borrow) {
        // Already validated above; use the final stable slot rather than the
        // staging value's stack address for an inline compatibility view.
        const int borrow_ret = plan.borrow_legacy(*slot, compatibility.datum_);
        OB_ASSERT(borrow_ret == OB_SUCCESS);
        UNUSED(borrow_ret);
      }
      param_store_.at(index) = compatibility;
    }
    info->evaluated_ = false;
    info->projected_ = false;
  }
  return ret;
}

int ObPhysicalPlanCtx::refresh_parameter_view(int64_t index)
{
  int ret = OB_SUCCESS;
  if (index < 0 || index >= native_parameter_types_.count()
      || native_parameter_types_.at(index) == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!param_store_.at(index).is_ext_sql_array()) {
    const Datum *slot = get_native_parameter(index);
    if (slot == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else {
      const DatumCopyPlan plan(*native_parameter_types_.at(index));
      ObDatum legacy;
      ret = plan.borrow_legacy(*slot, legacy);
      if (ret == OB_NOT_SUPPORTED) {
        // Wrapper materializations already belong to this context. Frame
        // relocation changes only direct scalar/native-reference borrows.
        ret = OB_SUCCESS;
      } else if (OB_SUCC(ret)) { param_store_.at(index).set_datum(legacy); }
    }
  }
  return ret;
}

const Datum *ObPhysicalPlanCtx::get_native_parameter(int64_t index) const
{
  const Datum *value = nullptr;
  if (native_parameters_ready_ && index >= 0 && index < param_store_.count()
      && index < param_frame_capacity_ && index < native_parameter_types_.count() && native_parameter_types_.at(index) != nullptr) {
    int64_t frame = 0;
    int64_t slot = 0;
    ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
    if (frame >= 0 && frame < param_frame_ptrs_.count()) {
      value = reinterpret_cast<const Datum *>(param_frame_ptrs_.at(frame)
          + ScalarFrameLayout::datum_offset(slot));
    }
  }
  return value;
}

const SqlType *ObPhysicalPlanCtx::get_native_parameter_type(int64_t index) const
{
  return index >= 0 && index < native_parameter_types_.count()
      ? native_parameter_types_.at(index) : nullptr;
}

const Datum *ObPhysicalPlanCtx::get_native_bigint_parameter(int64_t index) const
{
  const SqlType *type = get_native_parameter_type(index);
  return type != nullptr && type->kind() == ObIntType ? get_native_parameter(index) : nullptr;
}

int ObPhysicalPlanCtx::set_runtime_parameter(int64_t index, const DatumParameter &parameter)
{
  int ret = OB_SUCCESS;
  if (index < 0 || index >= param_store_.count() || parameter.get_type_meta().is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!native_parameters_ready_) {
    // Before initialization this is a construction input, not an execution
    // value. init_datum_param_store establishes its durable owner later.
    param_store_.at(index) = parameter;
  } else {
    Datum value;
    const SqlType *type = nullptr;
    DatumParameter input = parameter;
    const DatumParameter *effective = &input;
    if (input.is_ext_sql_array()) {
      ret = DatumParameterArray::clone_parameter(parameter, input, allocator_);
      if (OB_SUCC(ret)) {
        const DatumParameterArray *array = input.get_array<DatumParameterArray>();
        if (array == nullptr || array->count_ <= 0) { ret = OB_INVALID_ARGUMENT; }
        else { effective = &array->data_[0]; }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(prepare_runtime_parameter(*effective, value, type, index))) {
    } else if (OB_SUCC(ret)) {
      ret = publish_runtime_parameter(index, input, value, *type, input.is_ext_sql_array());
      if (OB_SUCC(ret) && input.is_ext_sql_array()) { param_store_.at(index) = input; }
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::restore_native_parameter_frames(const ObIArray<char *> &frames,
    const ObIArray<const SqlType *> &types, int64_t original_count)
{
  int ret = OB_SUCCESS;
  const int64_t count = types.count();
  if (count > INT64_MAX - ScalarFrameLayout::slots_per_frame()
      || original_count < 0 || original_count > count || count != param_store_.count()
      || frames.count() != ScalarFrameLayout::frame_count(original_count)
          + ScalarFrameLayout::frame_count(count - original_count)) {
    ret = OB_INVALID_ARGUMENT;
  }
  // Descriptor staging contains only compatibility views. It never carries a
  // second authoritative native scalar and borrows the final incoming slots.
  ObSEArray<DatumParameter, 16> views;
  if (OB_SUCC(ret) && OB_FAIL(views.reserve(count))) {
  } else if (OB_SUCC(ret) && OB_FAIL(native_parameter_types_.reserve(count))) {
  } else if (OB_SUCC(ret) && OB_FAIL(param_frame_ptrs_.reserve(frames.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < frames.count(); ++i) {
    if (frames.at(i) == nullptr || reinterpret_cast<uintptr_t>(frames.at(i)) % alignof(Datum) != 0) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    int64_t page = 0;
    int64_t slot = 0;
    ScalarFrameLayout::locate_parameter(original_count, i, page, slot);
    const Datum &native = *reinterpret_cast<const Datum *>(frames.at(page)
        + ScalarFrameLayout::datum_offset(slot));
    DatumParameter view = param_store_.at(i);
    if (types.at(i) == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const DatumCopyPlan plan(*types.at(i));
      int64_t checked_span = 0;
      if (OB_FAIL(plan.carrier_span_size(native, checked_span))) {
      } else if (!view.is_ext_sql_array()) {
        ObDatum legacy;
        ret = plan.borrow_legacy(native, legacy);
        if (ret == OB_NOT_SUPPORTED) {
          int64_t bytes = 0;
          ret = plan.legacy_payload_size(native, bytes);
          if (OB_SUCC(ret)) {
            char *buffer = bytes == 0 ? nullptr : static_cast<char *>(allocator_.alloc(bytes));
            if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
            else {
              int64_t position = 0;
              ret = plan.export_legacy(native, legacy, buffer, bytes, position);
            }
          }
        }
        if (OB_SUCC(ret)) {
          view.set_datum(legacy);
          view.set_type_meta(types.at(i)->legacy_meta());
          view.set_accuracy(types.at(i)->accuracy());
        }
      }
      if (OB_SUCC(ret)) { ret = views.push_back(view); }
    }
  }
  if (OB_SUCC(ret)) {
    // All allocation/validation has finished. Counts/pointer capacities were
    // reserved above, so these trivial pointer publications cannot allocate.
    const int types_ret = native_parameter_types_.assign(types);
    const int frames_ret = param_frame_ptrs_.assign(frames);
    OB_ASSERT(types_ret == OB_SUCCESS && frames_ret == OB_SUCCESS);
    UNUSED(types_ret); UNUSED(frames_ret);
    for (int64_t i = 0; i < count; ++i) { param_store_.at(i) = views.at(i); }
    original_param_cnt_ = original_count;
    param_frame_capacity_ = count;
    native_parameters_ready_ = true;
  }
  return ret;
}

int ObPhysicalPlanCtx::sync_last_value_local()
{
  int ret = OB_SUCCESS;
  ObAutoincrementService &auto_service = ObAutoincrementService::get_instance();
  ObIArray<AutoincParam> &autoinc_params = get_autoinc_params();
  for (int64_t i = 0; OB_SUCC(ret) && i < autoinc_params.count(); ++i) {
    AutoincParam &autoinc_param = autoinc_params.at(i);
    if (OB_FAIL(auto_service.sync_insert_value_local(autoinc_param))) {
    }
  }
	return ret;
}

int ObPhysicalPlanCtx::sync_last_value_to_store()
{
  int ret = OB_SUCCESS;
  ObAutoincrementService &auto_service = ObAutoincrementService::get_instance();
  ObIArray<AutoincParam> &autoinc_params = get_autoinc_params();
  for (int64_t i = 0; OB_SUCC(ret) && i < autoinc_params.count(); ++i) {
    AutoincParam &autoinc_param = autoinc_params.at(i);
    if (OB_FAIL(auto_service.sync_insert_value(autoinc_param))) {
    }
  }
  return ret;
}

void ObPhysicalPlanCtx::set_cur_time(const int64_t &session_val, const ObSQLSessionInfo &session)
{
  // If the system variable sets timestamp, then the request internally uses the set timestamp
  // If not set, then use the time when the current request is executed
  int64_t ts = session.get_local_timestamp();
  if (0 != ts) {
    cur_time_.set_timestamp(ts);
  } else {
    cur_time_.set_timestamp(session_val);
  }
}

int ObPhysicalPlanCtx::set_row_matched_count(int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(row_count));
  } else {
    row_matched_count_ = row_count;
  }
  return ret;
}

int ObPhysicalPlanCtx::set_row_duplicated_count(int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(row_count));
  } else {
    row_duplicated_count_ = row_count;
  }
  return ret;
}

int ObPhysicalPlanCtx::set_row_deleted_count(int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(row_count));
  } else {
    row_deleted_count_ = row_count;
  }
  return ret;
}

bool ObPhysicalPlanCtx::is_plain_select_stmt() const
{
  return phy_plan_ != NULL && phy_plan_->get_stmt_type() == stmt::T_SELECT && false == phy_plan_->has_for_update();
}

bool ObPhysicalPlanCtx::can_partition_retry() const
{
  return phy_plan_ != NULL && !phy_plan_->is_dml_write_stmt() && !phy_plan_->has_for_update();
}

bool ObPhysicalPlanCtx::has_for_update() const
{
  return phy_plan_ != NULL && phy_plan_->has_for_update();
}

int64_t ObPhysicalPlanCtx::get_ddl_task_id() const
{
  return phy_plan_ != NULL ? phy_plan_->get_ddl_task_id() : 0;
}

void ObPhysicalPlanCtx::set_phy_plan(const ObPhysicalPlan *phy_plan)
{
  phy_plan_ = phy_plan;
}
// Set batch update stmt's partition id and param index mapping relationship
// batch update stmtinparameterconstructed asnested tableobjectstorageon
//partition_ids: array index is the subscript of the parameter in the batch update stmt, value is the partition id corresponding to the param


int ObPhysicalPlanCtx::merge_implicit_cursors(const ObIArray<ObImplicitCursorInfo> &implicit_cursor)
{
  int ret = OB_SUCCESS;
  ARRAY_FOREACH(implicit_cursor, idx) {
    OZ(merge_implicit_cursor_info(implicit_cursor.at(idx)));
  }
  return ret;
}

int ObPhysicalPlanCtx::merge_implicit_cursor_info(const ObImplicitCursorInfo &implicit_cursor)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(implicit_cursor.stmt_id_ < 0)
      || OB_UNLIKELY(implicit_cursor.stmt_id_ >= implicit_cursor_infos_.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(implicit_cursor_infos_.at(implicit_cursor.stmt_id_).
                     merge_cursor(implicit_cursor))) {
  }
  LOG_DEBUG("merge implicit cursor info", K(ret), K(implicit_cursor), K(lbt()));
  return ret;
}

int ObPhysicalPlanCtx::replace_implicit_cursor_info(const ObImplicitCursorInfo &implicit_cursor)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(implicit_cursor.stmt_id_ < 0)
      || OB_UNLIKELY(implicit_cursor.stmt_id_ >= implicit_cursor_infos_.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(implicit_cursor_infos_.at(implicit_cursor.stmt_id_).
      replace_cursor(implicit_cursor))) {
  }
  LOG_DEBUG("merge implicit cursor info", K(ret), K(implicit_cursor), K(lbt()));
  return ret;
}


int ObPhysicalPlanCtx::switch_implicit_cursor()
{
  int ret = OB_SUCCESS;
  if (cur_stmt_id_ < 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (cur_stmt_id_ >= implicit_cursor_infos_.count()) {
    ret = OB_ITER_END;
  } else if (implicit_cursor_infos_.at(cur_stmt_id_).stmt_id_ != cur_stmt_id_
      && implicit_cursor_infos_.at(cur_stmt_id_).stmt_id_ >= 0) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObImplicitCursorInfo &cursor_info = implicit_cursor_infos_.at(cur_stmt_id_);
    set_affected_rows(cursor_info.affected_rows_);
    set_found_rows(cursor_info.found_rows_);
    set_row_matched_count(cursor_info.matched_rows_);
    set_row_duplicated_count(cursor_info.duplicated_rows_);
    set_row_deleted_count(cursor_info.deleted_rows_);
    set_last_insert_id_to_client(cursor_info.last_insert_id_);
    ++cur_stmt_id_;
  }
  return ret;
}

void ObPhysicalPlanCtx::reset_datum_frame(char *frame, int64_t expr_cnt)
{
  for (int64_t j = 0; j < expr_cnt; ++j) {
    new (frame + ScalarFrameLayout::datum_offset(j)) Datum();
  }
}

int ObPhysicalPlanCtx::reserve_param_frame(const int64_t input_capacity)
{
  int ret = OB_SUCCESS;
  const int64_t per_page = ScalarFrameLayout::slots_per_frame();
  if (input_capacity < 0 || original_param_cnt_ < 0
      || input_capacity > INT64_MAX - per_page || original_param_cnt_ > INT64_MAX - per_page) {
    ret = OB_INVALID_ARGUMENT;
  } else if (input_capacity > param_frame_capacity_) {
    const int64_t original_pages = ScalarFrameLayout::frame_count(original_param_cnt_);
    int64_t beyond = std::max(int64_t(0), input_capacity - original_param_cnt_);
    const int64_t beyond_pages = ScalarFrameLayout::frame_count(beyond);
    if (beyond_pages > 0) {
      const int64_t tail = beyond - (beyond_pages - 1) * per_page;
      beyond = (beyond_pages - 1) * per_page + std::min(per_page, next_pow2(tail));
    }
    const int64_t capacity = original_param_cnt_ + beyond;
    const int64_t pages = original_pages + beyond_pages;
    const int64_t old_beyond = std::max(int64_t(0), param_frame_capacity_ - original_param_cnt_);
    ObSEArray<char *, 8> staged;
    if (OB_FAIL(staged.reserve(pages))) {
    } else if (OB_FAIL(param_frame_ptrs_.reserve(pages))) {
    }
    for (int64_t page = 0; OB_SUCC(ret) && page < pages; ++page) {
      const bool original = page < original_pages;
      const int64_t local_page = original ? page : page - original_pages;
      const int64_t span = original ? original_param_cnt_ : beyond;
      const int64_t slots = std::min(per_page, span - local_page * per_page);
      const int64_t previous_span = original ? std::min(original_param_cnt_, param_frame_capacity_) : old_beyond;
      const int64_t old_slots = page < param_frame_ptrs_.count()
          ? std::max(int64_t(0), std::min(per_page, previous_span - local_page * per_page)) : 0;
      char *frame = old_slots >= slots ? param_frame_ptrs_.at(page) : nullptr;
      if (frame == nullptr) {
        frame = static_cast<char *>(allocator_.alloc(slots * ScalarFrameLayout::slot_size()));
        if (frame == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          MEMSET(frame, 0, slots * ScalarFrameLayout::slot_size());
          reset_datum_frame(frame, slots);
          if (old_slots > 0) {
            MEMCPY(frame, param_frame_ptrs_.at(page), old_slots * ScalarFrameLayout::slot_size());
          }
        }
      }
      if (OB_SUCC(ret)) { ret = staged.push_back(frame); }
    }
    if (OB_SUCC(ret)) {
      const int published = param_frame_ptrs_.assign(staged);
      OB_ASSERT(published == OB_SUCCESS);
      UNUSED(published);
      param_frame_capacity_ = capacity;
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::extend_param_frame(const int64_t old_size)
{
  int ret = OB_SUCCESS;
  if (old_size < 0 || old_size > param_store_.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(native_parameter_types_.reserve(param_store_.count()))) {
  } else if (OB_FAIL(reserve_param_frame(param_store_.count()))) {
  } else {
    while (OB_SUCC(ret) && native_parameter_types_.count() < param_store_.count()) {
      ret = native_parameter_types_.push_back(nullptr);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < old_size; ++i) {
      ret = refresh_parameter_view(i);
    }
    for (int64_t i = old_size; OB_SUCC(ret) && i < param_store_.count(); ++i) {
      const DatumParameter input = param_store_.at(i);
      const DatumParameter *effective = &input;
      if (input.is_ext_sql_array()) {
        const DatumParameterArray *array = input.get_array<DatumParameterArray>();
        if (array == nullptr || array->count_ <= 0) { ret = OB_INVALID_ARGUMENT; }
        else { effective = &array->data_[0]; }
      }
      Datum value;
      const SqlType *type = nullptr;
      if (OB_SUCC(ret) && OB_FAIL(prepare_runtime_parameter(*effective, value, type, i))) {
      } else if (OB_SUCC(ret)) {
        ret = publish_runtime_parameter(i, input, value, *type, input.is_ext_sql_array());
      }
    }
  }
  return ret;
}

OB_INLINE void ObPhysicalPlanCtx::get_param_frame_info(int64_t param_idx,
                                                       Datum *&datum,
                                                       ObEvalInfo *&eval_info)
{
  int64_t idx = 0;
  int64_t slot_idx = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, param_idx, idx, slot_idx);
  const int64_t off = ScalarFrameLayout::datum_offset(slot_idx);
  datum = reinterpret_cast<Datum*>(param_frame_ptrs_.at(idx) + off);
  eval_info = reinterpret_cast<ObEvalInfo *>(param_frame_ptrs_.at(idx)
      + ScalarFrameLayout::eval_info_offset(slot_idx));
  LOG_DEBUG("get_param_frame_info", K(param_idx), K(off), K(datum), K(lbt()));
}

int ObPhysicalPlanCtx::replace_batch_param_datum(const int64_t cur_group_id,
                                                 const int64_t start_param,
                                                 const int64_t param_cnt)
{
  int ret = OB_SUCCESS;
  if (start_param < 0 || param_cnt < 0 || start_param > param_store_.count()
      || param_cnt > param_store_.count() - start_param) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = start_param; OB_SUCC(ret) && i < start_param + param_cnt; ++i) {
    if (param_store_.at(i).is_ext_sql_array()) {
      const DatumParameterArray *array = param_store_.at(i).get_array<DatumParameterArray>();
      if (array == nullptr || cur_group_id < 0 || cur_group_id >= array->count_) {
        // Preserve the existing grouped-parameter API's error contract.
        ret = OB_ERR_UNEXPECTED;
      } else {
        Datum value;
        const SqlType *type = nullptr;
        if (OB_FAIL(prepare_runtime_parameter(array->data_[cur_group_id], value, type, i))) {
        } else {
          ret = publish_runtime_parameter(i, param_store_.at(i), value, *type, true);
        }
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObArrayParamGroup)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(row_count_);
  OB_UNIS_ENCODE(column_count_);
  OB_UNIS_ENCODE(start_param_idx_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObArrayParamGroup)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(row_count_);
  OB_UNIS_ADD_LEN(column_count_);
  OB_UNIS_ADD_LEN(start_param_idx_);
  return len;
}

OB_DEF_DESERIALIZE(ObArrayParamGroup)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(row_count_);
  OB_UNIS_DECODE(column_count_);
  OB_UNIS_DECODE(start_param_idx_);
  return ret;
}

OB_DEF_SERIALIZE(ObPhysicalPlanCtx)
{
  int ret = OB_SUCCESS;
  ParamStore empty_param_store;
  const ParamStore *param_store = NULL;
  const ObRowIdListArray *row_id_list_array = NULL;
  int64_t cursor_count = implicit_cursor_infos_.count();
  // used for function sys_view_bigint_param(idx), @note unused anymore
  int64_t obsolete_sys_view_param_count = 0;
  char message_[1] = {'\0'}; //error msg buffer, unused anymore
  if (exec_ctx_ != NULL) {
    row_id_list_array = &exec_ctx_->get_row_id_list_array();
  }
  if ((row_id_list_array != NULL && !row_id_list_array->empty() && phy_plan_ != NULL) ||
       is_multi_dml_) {
    param_store = &empty_param_store;
  } else {
    param_store = &param_store_;
  }
  // Follow the old sequence method
  OB_UNIS_ENCODE(tsc_snapshot_timestamp_);
  OB_UNIS_ENCODE(cur_time_);
  OB_UNIS_ENCODE(ts_timeout_us_);
  OB_UNIS_ENCODE(consistency_level_);
  OB_UNIS_ENCODE(*param_store);
  OB_UNIS_ENCODE(obsolete_sys_view_param_count);
  OB_UNIS_ENCODE(sql_mode_);
  OB_UNIS_ENCODE(autoinc_params_);
  OB_UNIS_ENCODE(tablet_autoinc_param_);
  OB_UNIS_ENCODE(last_insert_id_session_);
  OB_UNIS_ENCODE(message_);
  OB_UNIS_ENCODE(expr_op_size_);
  OB_UNIS_ENCODE(is_ignore_stmt_);
  if (OB_SUCC(ret) && row_id_list_array != NULL &&
      !row_id_list_array->empty() &&
      phy_plan_ != NULL && !(param_store_.empty())) {
    // Serialize param store as needed
    // Serialize param store slots
    // Need to serialize param store count
    // Serialize param index
    // Serialize param value
    OB_UNIS_ENCODE(param_store_.count());
    // Serialization is needed to know how many parameter values are serialized, so skip the count position first
    int64_t seri_param_cnt_pos = pos;
    int32_t real_param_cnt = 0;
    pos += serialization::encoded_length_i32(real_param_cnt);
    const ObIArray<int64_t> *param_idxs = NULL;
    if (phy_plan_->get_row_param_map().count() <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("row param map is empty");
    } else {
      param_idxs = &(phy_plan_->get_row_param_map().at(0));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < param_idxs->count(); ++i) {
      ++real_param_cnt;
      OB_UNIS_ENCODE(param_idxs->at(i));
      OB_UNIS_ENCODE(param_store_.at(param_idxs->at(i)));
    }
    for (int64_t k = 0; OB_SUCC(ret) && k < row_id_list_array->count(); ++k) {
      auto row_id_list = row_id_list_array->at(k);
      CK(OB_NOT_NULL(row_id_list));
      for (int64_t i = 0; OB_SUCC(ret) && i < row_id_list->count(); ++i) {
        // row_param_map starts from 1, because index=0 is used to store the common param
        int64_t row_idx = row_id_list->at(i) + 1;
        if (OB_UNLIKELY(row_idx >= phy_plan_->get_row_param_map().count()) || OB_UNLIKELY(row_idx < 0)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("row index is invalid", K(phy_plan_->get_row_param_map().count()), K(row_idx));
        } else {
          param_idxs = &(phy_plan_->get_row_param_map().at(row_idx));
        }
        for (int64_t j = 0; OB_SUCC(ret) && j < param_idxs->count(); ++j) {
          ++real_param_cnt;
          OB_UNIS_ENCODE(param_idxs->at(j));
          OB_UNIS_ENCODE(param_store_.at(param_idxs->at(j)));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(serialization::encode_i32(buf, buf_len, seri_param_cnt_pos, real_param_cnt))) {
      }
    }
  } else {
    /**
     * long long ago, param_store_.count() may be serialized or not, depend on if it is 0,
     * if > 0, param_store_.count() will be serialized,
     * if = 0, not.
     * but param_cnt will be always deserialized in deserialize operation, so it will
     * cause problem if we add other member in serialize and deserialize operation after
     * param_store_.count(), like foreign_key_checks_. the serialized data of new member
     * will be deserialized as param_cnt.
     * why everything goes right before, especially when param_store_.count() is 0 ?
     * the last member will consume all serialized data before param_cnt, so we get
     * 0 when try to deserialize param_cnt, and then finish.
     */
    int64_t param_cnt = 0;
    OB_UNIS_ENCODE(param_cnt);
  }
  OB_UNIS_ENCODE(foreign_key_checks_);
  OB_UNIS_ENCODE(runtime_schema_version_);
  OB_UNIS_ENCODE(cursor_count);
  OB_UNIS_ENCODE(plan_start_time_);
  OB_UNIS_ENCODE(last_trace_id_);
  OB_UNIS_ENCODE(srs_version_);
  OB_UNIS_ENCODE(original_param_cnt_);
  OB_UNIS_ENCODE(array_param_groups_.count());
  if (OB_SUCC(ret) && array_param_groups_.count() > 0) {
    for (int64_t i = 0 ; OB_SUCC(ret) && i < array_param_groups_.count(); i++) {
      OB_UNIS_ENCODE(array_param_groups_.at(i));
    }
  }
  OB_UNIS_ENCODE(all_local_session_vars_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < all_local_session_vars_.count(); ++i) {
    if (OB_ISNULL(all_local_session_vars_.at(i).get_local_vars())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      OB_UNIS_ENCODE(*all_local_session_vars_.at(i).get_local_vars());
    }
  }
  OB_UNIS_ENCODE(check_pdml_affected_rows_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPhysicalPlanCtx)
{
  int64_t len = 0;
  ParamStore empty_param_store;
  const ParamStore *param_store = NULL;
  const ObRowIdListArray *row_id_list_array = NULL;
  int64_t cursor_count = implicit_cursor_infos_.count();
  // used for function sys_view_bigint_param(idx), @note unused anymore
  int64_t obsolete_sys_view_param_count = 0;
  char message_[1] = {'\0'}; //error msg buffer, unused anymore
  if (exec_ctx_ != NULL) {
    row_id_list_array = &exec_ctx_->get_row_id_list_array();
  }
  if ((row_id_list_array != NULL && !row_id_list_array->empty() && phy_plan_ != NULL) ||
       is_multi_dml_) {
    param_store = &empty_param_store;
  } else {
    param_store = &param_store_;
  }
  // Follow the old sequence method
  OB_UNIS_ADD_LEN(tsc_snapshot_timestamp_);
  OB_UNIS_ADD_LEN(cur_time_);
  OB_UNIS_ADD_LEN(ts_timeout_us_);
  OB_UNIS_ADD_LEN(consistency_level_);
  OB_UNIS_ADD_LEN(*param_store);
  OB_UNIS_ADD_LEN(obsolete_sys_view_param_count);
  OB_UNIS_ADD_LEN(sql_mode_);
  OB_UNIS_ADD_LEN(autoinc_params_);
  OB_UNIS_ADD_LEN(tablet_autoinc_param_);
  OB_UNIS_ADD_LEN(last_insert_id_session_);
  OB_UNIS_ADD_LEN(message_);
  OB_UNIS_ADD_LEN(expr_op_size_);
  OB_UNIS_ADD_LEN(is_ignore_stmt_);
  if (row_id_list_array != NULL && !row_id_list_array->empty() &&
      phy_plan_ != NULL && !(param_store_.empty())) {
    // Serialize param store as needed
    // Need to serialize param store count
    // Serialize param index
    // Serialize param value
    OB_UNIS_ADD_LEN(param_store_.count());
    // Serialization is needed to know how many parameter values are serialized, so skip the count position first
    int32_t real_param_cnt = 0;
    len += serialization::encoded_length_i32(real_param_cnt);
    if (phy_plan_->get_row_param_map().count() > 0) {
      const ObIArray<int64_t> &param_idxs = phy_plan_->get_row_param_map().at(0);
      for (int64_t i = 0; i < param_idxs.count(); ++i) {
        OB_UNIS_ADD_LEN(param_idxs.at(i));
        OB_UNIS_ADD_LEN(param_store_.at(param_idxs.at(i)));
      }
    }
    for (int64_t k = 0; k < row_id_list_array->count(); ++k) {
      auto row_id_list = row_id_list_array->at(k);
      if (OB_NOT_NULL(row_id_list)) {
        for (int64_t i = 0; i < row_id_list->count(); ++i) {
          // row_param_map starts from 1, because index=0 is used to store the common param
          int64_t row_idx = row_id_list->at(i) + 1;
          if (row_idx < phy_plan_->get_row_param_map().count() && row_idx >= 0) {
            const ObIArray<int64_t> &param_idxs = phy_plan_->get_row_param_map().at(row_idx);
            for (int64_t j = 0; j < param_idxs.count(); ++j) {
              OB_UNIS_ADD_LEN(param_idxs.at(j));
              OB_UNIS_ADD_LEN(param_store_.at(param_idxs.at(j)));
            }
          }
        }
      }
    }
  } else {
    // see comment in OB_DEF_SERIALIZE(ObPhysicalPlanCtx).
    int64_t param_cnt = 0;
    OB_UNIS_ADD_LEN(param_cnt);
  }
  OB_UNIS_ADD_LEN(foreign_key_checks_);
  OB_UNIS_ADD_LEN(runtime_schema_version_);
  OB_UNIS_ADD_LEN(cursor_count);
  OB_UNIS_ADD_LEN(plan_start_time_);
  OB_UNIS_ADD_LEN(last_trace_id_);
  OB_UNIS_ADD_LEN(srs_version_);
  OB_UNIS_ADD_LEN(original_param_cnt_);
  OB_UNIS_ADD_LEN(array_param_groups_.count());
  if (array_param_groups_.count() > 0) {
    for (int64_t i = 0 ; i < array_param_groups_.count(); i++) {
      OB_UNIS_ADD_LEN(array_param_groups_.at(i));
    }
  }
  OB_UNIS_ADD_LEN(all_local_session_vars_.count());
  for (int64_t i = 0; i < all_local_session_vars_.count(); ++i) {
    if (OB_NOT_NULL(all_local_session_vars_.at(i).get_local_vars())) {
      OB_UNIS_ADD_LEN(*all_local_session_vars_.at(i).get_local_vars());
    }
  }
  OB_UNIS_ADD_LEN(check_pdml_affected_rows_);
  return len;
}

OB_DEF_DESERIALIZE(ObPhysicalPlanCtx)
{
  int ret = OB_SUCCESS;
  reset_datum_param_store();
  int64_t param_cnt = 0;
  int32_t real_param_cnt = 0;
  int64_t param_idx = OB_INVALID_INDEX;
  int64_t cursor_count = 0;
  int64_t local_var_array_cnt = 0;
  // used for function sys_view_bigint_param(idx), @note unused anymore
  int64_t obsolete_sys_view_param_count = 0;
  char message_[1] = {'\0'}; //error msg buffer, unused anymore
  auto publish_parameter = [this](int64_t index) {
    DatumParameter &parameter = param_store_.at(index);
    parameter.set_param_meta();
    if (parameter.has_lob_header()) { parameter.set_result_flag(HAS_LOB_HEADER_FLAG); }
    return OB_SUCCESS;
  };
  // Follow the old sequence method
  OB_UNIS_DECODE(tsc_snapshot_timestamp_);
  OB_UNIS_DECODE(cur_time_);
  OB_UNIS_DECODE(ts_timeout_us_);
  OB_UNIS_DECODE(consistency_level_);
  if (OB_SUCC(ret)) {
    ret = DatumParameterCodec::decode_store(buf, data_len, pos, allocator_, param_store_);
  }
  OB_UNIS_DECODE(obsolete_sys_view_param_count);
  if (OB_SUCC(ret) && (obsolete_sys_view_param_count < 0 ||
      obsolete_sys_view_param_count > (data_len - pos) / sizeof(SqlTypeMeta))) {
    ret = OB_DESERIALIZE_ERROR;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < obsolete_sys_view_param_count; ++i) {
    ObDatum discarded;
    SqlTypeMeta discarded_meta;
    ret = DatumParameterCodec::decode_value(buf, data_len, pos, allocator_, discarded, discarded_meta);
  }
  OB_UNIS_DECODE(sql_mode_);
  OB_UNIS_DECODE(autoinc_params_);
  OB_UNIS_DECODE(tablet_autoinc_param_);
  OB_UNIS_DECODE(last_insert_id_session_);
  OB_UNIS_DECODE(message_);
  OB_UNIS_DECODE(expr_op_size_);
  OB_UNIS_DECODE(is_ignore_stmt_);
  OB_UNIS_DECODE(param_cnt);
  if (OB_SUCC(ret) && param_cnt < 0) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_SUCC(ret) && param_cnt > 0) {
    if (OB_FAIL(serialization::decode_i32(buf, data_len, pos, &real_param_cnt))) {
    } else if (real_param_cnt < 0 || real_param_cnt > (data_len - pos) / 7 ||
               param_cnt > INT64_MAX / static_cast<int64_t>(sizeof(DatumParameter)) || !param_store_.empty()) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (OB_FAIL(param_store_.prepare_allocate(param_cnt))) {
    }
    // Unselected slots stay NULL. Repeated indexes intentionally keep the last
    // value, as the v2 sparse parameter stream permits duplicate row mappings.
    for (int64_t i = 0; OB_SUCC(ret) && i < real_param_cnt; ++i) {
      OB_UNIS_DECODE(param_idx);
      if (OB_FAIL(ret)) {
      } else if (param_idx < 0 || param_idx >= param_cnt) {
        ret = OB_DESERIALIZE_ERROR;
      } else if (OB_FAIL(DatumParameterCodec::decode_parameter(buf, data_len, pos, allocator_, param_store_.at(param_idx)))) {
      } else { ret = publish_parameter(param_idx); }
    }
  }
  OB_UNIS_DECODE(foreign_key_checks_);
  OB_UNIS_DECODE(runtime_schema_version_);
  OB_UNIS_DECODE(cursor_count);
  if (OB_SUCC(ret) && cursor_count > 0) {
    if (OB_FAIL(implicit_cursor_infos_.prepare_allocate(cursor_count))) {
    }
  }
  OB_UNIS_DECODE(plan_start_time_);
  if (OB_SUCC(ret)) {
    (void)ObSQLUtils::adjust_time_by_ntp_offset(plan_start_time_);
    (void)ObSQLUtils::adjust_time_by_ntp_offset(ts_timeout_us_);
  }
  OB_UNIS_DECODE(last_trace_id_);
  OB_UNIS_DECODE(srs_version_);
  OB_UNIS_DECODE(original_param_cnt_);
  int64_t array_group_count = 0;
  OB_UNIS_DECODE(array_group_count);
  if (OB_SUCC(ret) && array_group_count > 0) {
    for (int64_t i = 0 ; OB_SUCC(ret) && i < array_group_count; i++) {
      ObArrayParamGroup array_p_group;
      OB_UNIS_DECODE(array_p_group);
      if (OB_FAIL(array_param_groups_.push_back(array_p_group))) {
      }
    }
  }
  OB_UNIS_DECODE(local_var_array_cnt);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(all_local_session_vars_.reserve(local_var_array_cnt))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < local_var_array_cnt; ++i) {
    ObLocalSessionVar *local_vars = OB_NEWx(ObLocalSessionVar, &allocator_);
    if (NULL == local_vars) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(all_local_session_vars_.push_back(ObSolidifiedVarsContext(local_vars, &allocator_)))) {
    } else {
      local_vars->set_allocator(&allocator_);
      OB_UNIS_DECODE(*local_vars);
    }
  }
  OB_UNIS_DECODE(check_pdml_affected_rows_);
  if (OB_SUCC(ret) && param_cnt == 0) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_store_.count(); ++i) {
      ret = publish_parameter(i);
    }
  }
  if (OB_FAIL(ret)) { reset_datum_param_store(); }
  return ret;
}

void ObPhysicalPlanCtx::add_px_dml_row_info(const ObPxDmlRowInfo &dml_row_info)
{
  add_row_matched_count(dml_row_info.row_match_count_);
  add_row_duplicated_count(dml_row_info.row_duplicated_count_);
  add_row_deleted_count(dml_row_info.row_deleted_count_);
}


bool ObPhysicalPlanCtx::is_subschema_ctx_inited()
{
  bool b_ret = false;
  if (OB_NOT_NULL(phy_plan_)) {
    b_ret = phy_plan_->get_subschema_ctx().is_inited();
  } else {
    b_ret = subschema_ctx_.is_inited();
  }
  return b_ret;
}

int ObPhysicalPlanCtx::get_sqludt_meta_by_subschema_id(uint16_t subschema_id, ObSqlUDTMeta &udt_meta) const
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue value;
  if (subschema_id == ObMaxSystemUDTSqlType || subschema_id >= UINT_MAX16) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_NOT_NULL(phy_plan_)) { // physical plan exist, use subschema ctx on phy plan
    if (!phy_plan_->get_subschema_ctx().is_inited()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(phy_plan_->get_subschema_ctx().get_subschema(subschema_id, value))) {
      if (OB_HASH_NOT_EXIST != ret) {
      } else {
      }                                                                   
    } else {
      udt_meta = *(reinterpret_cast<ObSqlUDTMeta *>(value.value_));
    }
  } else if (!subschema_ctx_.is_inited()) { // no phy plan
    ret = OB_ISNULL(phy_plan_) ? OB_NOT_INIT : OB_ERR_UNEXPECTED;
  } else {
    if (OB_FAIL(subschema_ctx_.get_subschema(subschema_id, value))) {
    } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
      ret = OB_ERR_UNEXPECTED;
    } else { // Notice: shallow copy
      udt_meta = *(reinterpret_cast<ObSqlUDTMeta *>(value.value_));
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::get_sqludt_meta_by_subschema_id(uint16_t subschema_id, ObSubSchemaValue &sub_meta) const
{
  int ret = OB_SUCCESS;
  bool is_subschema_inited_in_plan = true;
  if (subschema_id == ObMaxSystemUDTSqlType || subschema_id >= UINT_MAX16) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_NOT_NULL(phy_plan_)) { // physical plan exist, use subschema ctx on phy plan
    if (!phy_plan_->get_subschema_ctx().is_inited()) {
      LOG_INFO("plan with empty subschema mapping", K(lbt()), K(phy_plan_->get_subschema_ctx()));
      is_subschema_inited_in_plan = false;
    } else if (OB_FAIL(phy_plan_->get_subschema_ctx().get_subschema(subschema_id, sub_meta))) {
      if (OB_HASH_NOT_EXIST != ret) {
      } else {
      }                                                                   
    }
  } 
  if (OB_SUCC(ret)
      && (OB_ISNULL(phy_plan_) || !is_subschema_inited_in_plan)) {
    if (!subschema_ctx_.is_inited()) { // no phy plan
      ret = OB_ISNULL(phy_plan_) ? OB_NOT_INIT : OB_ERR_UNEXPECTED;
    } else {
      if (OB_FAIL(subschema_ctx_.get_subschema(subschema_id, sub_meta))) {
      } else if (sub_meta.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::get_enumset_meta_by_subschema_id(uint16_t subschema_id,
                                                        bool is_in_pl,
                                                        const ObEnumSetMeta *&meta) const
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue value;
  if (subschema_id == ObMaxSystemUDTSqlType || subschema_id >= UINT_MAX16) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!is_in_pl && OB_NOT_NULL(phy_plan_) && phy_plan_->get_subschema_ctx().is_inited()) {
    // physical plan exist, use subschema ctx on phy plan
    if (OB_FAIL(phy_plan_->get_subschema_ctx().get_subschema(subschema_id, value))) {
      if (OB_HASH_NOT_EXIST != ret) {
      } else {
      }
    } else {
      meta = reinterpret_cast<const ObEnumSetMeta *>(value.value_);
    }
  } else if (!subschema_ctx_.is_inited()) { // no phy plan
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (OB_FAIL(subschema_ctx_.get_subschema(subschema_id, value))) {
    } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
      ret = OB_ERR_UNEXPECTED;
    } else { // Notice: shallow copy
      meta = reinterpret_cast<const ObEnumSetMeta *>(value.value_);
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::get_subschema_id_by_udt_id(uint64_t udt_type_id,
                                                  uint16_t &subschema_id,
                                                  share::schema::ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  uint16_t temp_subschema_id = ObMaxSystemUDTSqlType;
  bool found = false;
  if (!is_supported_sql_udt(udt_type_id)) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_NOT_NULL(phy_plan_)) { // physical plan exist, use subschema ctx on phy plan
    if (!phy_plan_->get_subschema_ctx().is_inited()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(phy_plan_->get_subschema_ctx().get_subschema_id(udt_type_id,
                                                                       OB_SUBSCHEMA_UDT_TYPE,
                                                                       temp_subschema_id))) {
      if (OB_HASH_NOT_EXIST != ret) {
      } else {
      }                                                                    
    } else {
      subschema_id = temp_subschema_id;
    }
  // no phy plan
  } else if (!subschema_ctx_.is_inited() && OB_FAIL(subschema_ctx_.init())) {
  } else if (OB_FAIL(subschema_ctx_.get_subschema_id(udt_type_id,
                                                     OB_SUBSCHEMA_UDT_TYPE,
                                                     temp_subschema_id))) {
    if (OB_HASH_NOT_EXIST != ret) {
    } else { // build new meta
      ret = OB_SUCCESS;
      uint16 new_subschema_id = ObMaxSystemUDTSqlType;
      ObSqlUDTMeta *udt_meta = NULL;
      ObSubSchemaValue value;
      if (OB_ISNULL(schema_guard)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (FALSE_IT(udt_meta = reinterpret_cast<ObSqlUDTMeta *>(allocator_.alloc(sizeof(ObSqlUDTMeta))))) {
      } else if (OB_ISNULL(udt_meta)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (FALSE_IT(MEMSET(udt_meta, 0, sizeof(ObSqlUDTMeta)))) {
      } else if (OB_FAIL(ObSqlUdtMetaUtils::generate_udt_meta_from_schema(schema_guard,
                                                                          &subschema_ctx_,
                                                                          allocator_,
                                                                          udt_type_id,
                                                                          *udt_meta))) {
      } else if (OB_FAIL(subschema_ctx_.get_subschema_id_from_fields(udt_type_id, new_subschema_id))) {
      } else if (new_subschema_id == ObInvalidSqlType // not get from fields, generate new
                 && OB_FAIL(subschema_ctx_.get_new_subschema_id(new_subschema_id))) {
      } else {
        value.type_ = OB_SUBSCHEMA_UDT_TYPE;
        value.signature_ = udt_type_id;
        value.value_ = static_cast<void *>(udt_meta);
        if (OB_FAIL(subschema_ctx_.set_subschema(new_subschema_id, value))) {
        } else {
          subschema_id = new_subschema_id;
        }
      }
    }
  } else { // success
    subschema_id = temp_subschema_id;
  }
  return ret;
}

int ObPhysicalPlanCtx::get_subschema_id_by_collection_elem_type(ObNestedType coll_type,
                                                                const ObDataType &elem_type,
                                                                uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  bool is_subschema_inited_in_plan = true;
  if (OB_NOT_NULL(phy_plan_)) { // physical plan exist, use subschema ctx on phy plan
    if (!phy_plan_->get_subschema_ctx().is_inited()) {
      LOG_INFO("plan with empty subschema mapping", K(phy_plan_->get_subschema_ctx()));
      is_subschema_inited_in_plan = false;
    } else if (OB_FAIL(phy_plan_->get_subschema_ctx().get_subschema_id_by_typedef(coll_type,
                                                                                             elem_type,
                                                                                             subschema_id))) {
    } 
  // no phy plan
  }
  if (OB_SUCC(ret) && (OB_ISNULL(phy_plan_) || !is_subschema_inited_in_plan)) {
    if (!subschema_ctx_.is_inited() && OB_FAIL(subschema_ctx_.init())) {
    } else if (OB_FAIL(subschema_ctx_.get_subschema_id_by_typedef(coll_type, elem_type, subschema_id))) {
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::get_subschema_id_by_type_string(const ObString &type_string, uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  bool is_subschema_inited_in_plan = true;
  if (OB_NOT_NULL(phy_plan_)) { // physical plan exist, use subschema ctx on phy plan
    if (!phy_plan_->get_subschema_ctx().is_inited()) {
      LOG_INFO("plan with empty subschema mapping", K(lbt()), K(phy_plan_->get_subschema_ctx()));
      is_subschema_inited_in_plan = false;
    } else if (OB_FAIL(phy_plan_->get_subschema_ctx().get_subschema_id_by_typedef(type_string, subschema_id))) {
    } 
  // no phy plan
  } 
  if (OB_SUCC(ret)
      && (OB_ISNULL(phy_plan_) || !is_subschema_inited_in_plan)) {
    if (!subschema_ctx_.is_inited() && OB_FAIL(subschema_ctx_.init())) {
    } else if (OB_FAIL(subschema_ctx_.get_subschema_id_by_typedef(type_string, subschema_id))) {
    }
  }
  return ret;
}


int ObPhysicalPlanCtx::get_subschema_id_by_type_info(const SqlTypeMeta &obj_meta,
                                                     const ObIArray<common::ObString> &type_info,
                                                     uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  if (!subschema_ctx_.is_inited() && OB_FAIL(subschema_ctx_.init())) {
  } else if (OB_FAIL(inner_get_subschema_id_by_type_info(obj_meta, type_info, subschema_id))) {
    if (OB_HASH_NOT_EXIST != ret) {
    } else {
      ObEnumSetMeta src_meta(obj_meta, &type_info);
      uint16 new_subschema_id = ObMaxSystemUDTSqlType;
      ObSubSchemaValue value;
      ObEnumSetMeta *dst_meta = NULL;
      if (OB_FAIL(src_meta.deep_copy(allocator_, dst_meta))) {
      } else if (OB_FAIL(subschema_ctx_.get_new_subschema_id(new_subschema_id))) {
      } else {
        value.type_ = OB_SUBSCHEMA_ENUM_SET_TYPE;
        value.signature_ = dst_meta->get_signature();
        value.value_ = static_cast<void *>(dst_meta);
        if (OB_FAIL(subschema_ctx_.set_subschema(new_subschema_id, value))) {
        } else {
          subschema_id = new_subschema_id;
        }
      }
    }
  }
  return ret;
}


int ObPhysicalPlanCtx::inner_get_subschema_id_by_type_info(const SqlTypeMeta &obj_meta,
                                                      const ObIArray<common::ObString> &type_info,
                                                      uint16_t &subschema_id) const
{
  int ret = OB_SUCCESS;
  uint16_t temp_subschema_id = ObMaxSystemUDTSqlType;
  ObEnumSetMeta src_meta(obj_meta, &type_info);
  if (!subschema_ctx_.is_inited()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(subschema_ctx_.get_subschema_id(OB_SUBSCHEMA_ENUM_SET_TYPE,
                                                     src_meta,
                                                     temp_subschema_id))) {
    if (OB_HASH_NOT_EXIST != ret) {
    }
  } else { // success
    subschema_id = temp_subschema_id;
  }
  return ret;
}

int ObPhysicalPlanCtx::set_all_local_session_vars(ObIArray<ObLocalSessionVar> &all_local_session_vars)
{
  int ret = OB_SUCCESS;
  if (!all_local_session_vars_.empty()) {
    all_local_session_vars_.reset();
  }
  if (!all_local_session_vars.empty()) {
    if (OB_FAIL(all_local_session_vars_.reserve(all_local_session_vars.count()))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < all_local_session_vars.count(); ++i) {
        if (OB_FAIL(all_local_session_vars_.push_back(ObSolidifiedVarsContext(&all_local_session_vars.at(i), &allocator_)))) {
        }
      }
    }
  }
  return ret;
}

// for ps protocal in jdbc, phy plan only exist in ObMPStmtExecute
// but udt meta needs to be used in ObMPStmtFetch, here rebuild subschema ctx by fields
// for sql udts, fields recorded both subschema id and udt id
int ObPhysicalPlanCtx::build_subschema_by_fields(const ColumnsFieldIArray *fields,
                                                 share::schema::ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(phy_plan_) || subschema_ctx_.get_subschema_count() > 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!subschema_ctx_.is_inited() && OB_FAIL(subschema_ctx_.init())) {
  } else {
    subschema_ctx_.set_fields(fields);

    for (uint32_t i = 0; OB_SUCC(ret) && i < fields->count(); i++) {
      if (fields->at(i).type_meta_.is_collection_sql_type()) {
        uint64_t udt_id = fields->at(i).accuracy_.get_accuracy();
        uint16_t subschema_id = 0;
        if (OB_FAIL(get_subschema_id_by_udt_id(udt_id, subschema_id, schema_guard))) {
        }
      }
    }
  }
  return ret;
}

// for ps with sql udt param without plan
int ObPhysicalPlanCtx::build_subschema_ctx_by_param_store(share::schema::ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  ParamStore *param_store = &param_store_;
  if (OB_NOT_NULL(phy_plan_) || subschema_ctx_.get_subschema_count() > 0) {
    ret = OB_ERR_UNEXPECTED;
  }

  for (uint32_t i = 0; OB_SUCC(ret) && i < param_store->count(); i++) {
    DatumParameter &param = param_store->at(i);
    if (param.is_collection_sql_type()) {
      uint64_t udt_id = param.get_accuracy().get_accuracy();
      uint16_t subschema_id = 0;
      if (!subschema_ctx_.is_inited() && OB_FAIL(subschema_ctx_.init())) {
      } else if(OB_FAIL(get_subschema_id_by_udt_id(udt_id, subschema_id, schema_guard))) {
      } else if (subschema_id == ObMaxSystemUDTSqlType) {
      } else {
        param.set_subschema_id(subschema_id);
      }
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::get_local_session_vars(int64_t local_var_array_id, const ObSolidifiedVarsContext *&local_vars)
{
  int ret = OB_SUCCESS;
  local_vars = NULL;
  if (local_var_array_id == OB_INVALID_INDEX_INT64) {
    //do nothing
  } else if (local_var_array_id + 1 > all_local_session_vars_.count() || local_var_array_id < 0) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    local_vars = &all_local_session_vars_.at(local_var_array_id);
  }
  return ret;
}


} //sql
} //oceanbase
