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
#include "sql/engine/ob_physical_plan.h"
#include "share/ob_autoincrement_service.h"
#include "src/sql/engine/ob_des_exec_context.h"
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/ob_sql_context.h"
#include "sql/pl/ob_pl.h"
#include "share/sql_udt_support.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace transaction;
namespace sql
{
namespace
{
// SQL equality intentionally ignores display attributes. An execution binding
// must retain those attributes too, including on nested child declarations.
bool same_parameter_type(const SqlType &left, const SqlType &right)
{
  const ObDataType &a = left.attributes();
  const ObDataType &b = right.attributes();
  bool same = left.equals(right) && a.meta_ == b.meta_
      && a.meta_.get_scale() == b.meta_.get_scale()
      && a.accuracy_.get_accuracy() == b.accuracy_.get_accuracy()
      && a.is_zero_fill_ == b.is_zero_fill_ && a.charset_ == b.charset_
      && a.is_binary_collation_ == b.is_binary_collation_;
  if (same && left.element() != nullptr) { same = same_parameter_type(*left.element(), *right.element()); }
  if (same && left.key_type() != nullptr) { same = same_parameter_type(*left.key_type(), *right.key_type()); }
  if (same && left.value_type() != nullptr) { same = same_parameter_type(*left.value_type(), *right.value_type()); }
  for (int64_t i = 0; same && i < left.member_count(); ++i) {
    same = same_parameter_type(*left.member(i).type, *right.member(i).type);
  }
  return same;
}
// Physical graph cloning deliberately has no Type ownership. The explicit
// PlanCtx construction edit domain additionally carries whatever real producer
// declarations were supplied; absent historical declarations remain absent.
int clone_construction_type_bindings(const DatumParameter &source, DatumParameter &copy,
    ObIAllocator &bulk_owner, int depth)
{
  if (depth > 64) { return OB_SIZE_OVERFLOW; }
  if (!source.is_ext_sql_array() || source.is_null()) { return OB_SUCCESS; }
  const DatumParameterArray *input = source.get_array<DatumParameterArray>();
  DatumParameterArray *output = copy.get_array<DatumParameterArray>();
  if (input == nullptr || output == nullptr || input->count_ != output->count_) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < input->count_; ++i) {
    const SqlType *type = input->get_element_type(i);
    const SqlType *owned = nullptr;
    if (type != nullptr && OB_FAIL(type->clone_to(bulk_owner, owned))) {
    } else if (type != nullptr && OB_FAIL(output->set_element_type_borrowed(i, *owned, bulk_owner))) {
    } else { ret = clone_construction_type_bindings(input->data_[i], output->data_[i], bulk_owner, depth + 1); }
  }
  return ret;
}
} // namespace

int ParameterValuePage::allocate(ObIAllocator &owner, int64_t capacity, ParameterValuePage *&out)
{
  if (capacity <= 0 || capacity > ScalarFrameLayout::slots_per_frame()) { return OB_INVALID_ARGUMENT; }
  void *memory = owner.alloc(allocation_bytes(capacity));
  if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  MEMSET(memory, 0, allocation_bytes(capacity));
  ParameterValuePage *candidate = new (memory) ParameterValuePage(capacity);
  for (int64_t i = 0; i < capacity; ++i) { new (&candidate->eval_info(i)) ObEvalInfo{}; }
  out = candidate;
  return OB_SUCCESS;
}

Datum::State ParameterValuePage::state(int64_t slot) const
{
  OB_ASSERT(slot >= 0 && slot < capacity_);
  return static_cast<Datum::State>(states()[slot] & STATE_MASK);
}

ObEvalInfo &ParameterValuePage::eval_info(int64_t slot) const
{
  OB_ASSERT(slot >= 0 && slot < capacity_);
  return reinterpret_cast<ObEvalInfo *>(reinterpret_cast<char *>(const_cast<ParameterValuePage *>(this))
      + eval_offset(capacity_))[slot];
}

int ParameterValuePage::read(int64_t slot, Datum &value) const
{
  if (slot < 0 || slot >= capacity_) { return OB_INVALID_ARGUMENT; }
  Datum candidate;
  switch (state(slot)) {
    case Datum::State::Null: candidate.set_null(); break;
    case Datum::State::Nop: candidate.set_nop(); break;
    case Datum::State::Min: candidate.set_min(); break;
    case Datum::State::Max: candidate.set_max(); break;
    case Datum::State::Present:
      if ((states()[slot] & INLINE_I64) != 0) {
        int64_t payload = 0;
        MEMCPY(&payload, words() + slot, sizeof(payload));
        candidate.set_int(payload);
      } else if (words()[slot] == 0) { return OB_ERR_UNEXPECTED; }
      else { candidate = *reinterpret_cast<const Datum *>(words()[slot]); }
      break;
    default: return OB_ERR_UNEXPECTED;
  }
  value = candidate;
  return OB_SUCCESS;
}

bool ParameterValuePage::read_i64(int64_t slot, int64_t &value) const
{
  Datum::State state = Datum::State::Null;
  return read_i64(slot, value, state) && state == Datum::State::Present;
}

bool ParameterValuePage::read_i64(int64_t slot, int64_t &value, Datum::State &state) const
{
  if (slot < 0 || slot >= capacity_) { return false; }
  const uint8_t encoded = states()[slot];
  const uint8_t present = INLINE_I64 | static_cast<uint8_t>(Datum::State::Present);
  const uint8_t null = INLINE_I64 | static_cast<uint8_t>(Datum::State::Null);
  if (encoded != present && encoded != null) { return false; }
  int64_t candidate = 0;
  const Datum::State candidate_state = encoded == present ? Datum::State::Present : Datum::State::Null;
  if (candidate_state == Datum::State::Present) {
    MEMCPY(&candidate, words() + slot, sizeof(candidate));
    value = candidate;
  }
  state = candidate_state;
  return true;
}

bool ParameterValuePage::matches_layout(int64_t slot, const SqlType &type) const
{
  const bool compact = type.kind() == ObIntType
      && type.canonical_representation() == DatumRepresentation::Inline64;
  return slot >= 0 && slot < capacity_ && compact == ((states()[slot] & INLINE_I64) != 0);
}

Datum *ParameterValuePage::find_materialized(int64_t slot) const
{
  return slot < 0 || slot >= capacity_ || projections_ == nullptr ? nullptr : projections_[slot];
}

int ParameterValuePage::assign(int64_t slot, const Datum &value, const SqlType &type, ObIAllocator &owner)
{
  if (slot < 0 || slot >= capacity_) { return OB_INVALID_ARGUMENT; }
  const Datum candidate = value;
  const bool compact = type.kind() == ObIntType
      && type.canonical_representation() == DatumRepresentation::Inline64;
  uint64_t word = 0;
  Datum *generic = nullptr;
  if (candidate.is_present()) {
    if (compact) {
      if (candidate.length() != 0 || candidate.reference_flags() != Datum::NoReferenceFlags) {
        return OB_INVALID_ARGUMENT;
      }
      const int64_t payload = candidate.get_int();
      MEMCPY(&word, &payload, sizeof(word));
    } else {
      // Reuse a real generic owner only when it already exists. No generic
      // descriptor is allocated for an I64 or a non-present value.
      if ((states()[slot] & INLINE_I64) == 0 && state(slot) == Datum::State::Present) {
        generic = reinterpret_cast<Datum *>(words()[slot]);
      }
      if (generic == nullptr) {
        void *memory = owner.alloc(sizeof(Datum));
        if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
        generic = new (memory) Datum();
      }
      word = reinterpret_cast<uint64_t>(generic);
    }
  }
  // Everything below is non-fallible. Publish owner, state and any already
  // requested ABI value together; an unused parameter gets no projection.
  if (generic != nullptr) { *generic = candidate; }
  words()[slot] = word;
  states()[slot] = static_cast<uint8_t>(candidate.state()) | (compact ? INLINE_I64 : 0);
  Datum *projection = find_materialized(slot);
  if (projection != nullptr) { *projection = candidate; }
  eval_info(slot).evaluated_ = false;
  eval_info(slot).projected_ = false;
  return OB_SUCCESS;
}

int ParameterValuePage::materialize(int64_t slot, ObIAllocator &owner, Datum *&value)
{
  Datum candidate;
  int ret = read(slot, candidate);
  Datum *result = find_materialized(slot);
  if (OB_SUCC(ret) && result == nullptr) {
    Datum **directory = projections_;
    if (directory == nullptr) {
      directory = static_cast<Datum **>(owner.alloc(capacity_ * sizeof(Datum *)));
      if (directory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { MEMSET(directory, 0, capacity_ * sizeof(Datum *)); }
    }
    void *memory = OB_SUCC(ret) ? owner.alloc(sizeof(Datum)) : nullptr;
    if (OB_SUCC(ret) && memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    if (OB_SUCC(ret)) {
      result = new (memory) Datum(candidate);
      directory[slot] = result;
      projections_ = directory;
    } else if (directory != nullptr && directory != projections_) {
      // No directory or entry becomes observable on failure. Arena allocators
      // may retain the attempted block until execution teardown.
      owner.free(directory);
    }
  }
  if (OB_SUCC(ret)) { *result = candidate; value = result; }
  return ret;
}

int ParameterValuePage::prepare_existing_projections(const ParameterValuePage &source, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (source.projections_ != nullptr) {
    for (int64_t slot = 0; OB_SUCC(ret) && slot < capacity_ && slot < source.capacity_; ++slot) {
      if (source.projections_[slot] != nullptr) {
        Datum *unused = nullptr;
        ret = materialize(slot, owner, unused);
      }
    }
  }
  return ret;
}

ObPhysicalPlanCtx::ObPhysicalPlanCtx(common::ObIAllocator &allocator)
    : allocator_(allocator),
      tsc_snapshot_timestamp_(0),
      ts_timeout_us_(0),
      consistency_level_(INVALID_CONSISTENCY),
      parameter_input_allocator_("ExecParamInput"),
      param_store_((ObWrapperAllocator(&parameter_input_allocator_))),
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
  const int64_t old_count = get_parameter_count();
  if (param_count < 0) { return OB_INVALID_ARGUMENT; }
  if (param_count <= old_count) { return OB_SUCCESS; }
  if (!native_parameters_ready_) {
    if (OB_FAIL(param_store_.reserve(param_count))) {
    }
    while (OB_SUCC(ret) && param_store_.count() < param_count) {
      ret = param_store_.push_back(DatumParameter());
    }
    if (OB_FAIL(ret)) {
      while (param_store_.count() > old_count) { param_store_.pop_back(); }
    }
  } else {
    SqlType null_declaration;
    SqlTypeMeta null_meta;
    null_meta.set_null();
    const SqlType *null_type = nullptr;
    if (OB_FAIL(SqlType::from_legacy(null_meta, ObAccuracy(), null_declaration))) {
    } else if (OB_FAIL(null_declaration.clone_to(allocator_, null_type))) {
    } else if (OB_FAIL(native_parameter_metadata_.reserve(param_count))) {
    } else if (OB_FAIL(native_parameter_types_.reserve(param_count))) {
    } else if (OB_FAIL(reserve_param_frame(param_count))) {
    } else {
      // All pages, descriptors and the NULL declaration are prepared. Existing
      // parameter pages never relocate, so compiled Expr frame aliases survive.
      Datum null_value;
      DatumParameterMetadata null_attributes;
      while (native_parameter_metadata_.count() < param_count) {
        const int64_t index = native_parameter_metadata_.count();
        const int meta_ret = native_parameter_metadata_.push_back(null_attributes);
        const int type_ret = native_parameter_types_.push_back(null_type);
        OB_ASSERT(meta_ret == OB_SUCCESS && type_ret == OB_SUCCESS);
        UNUSED(meta_ret); UNUSED(type_ret);
        const int publish_ret = publish_runtime_parameter(index, null_attributes, null_value, *null_type);
        OB_ASSERT(publish_ret == OB_SUCCESS);
        UNUSED(publish_ret);
      }
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::clone_construction_parameter(const DatumParameter &source, DatumParameter &output)
{
  DatumParameter candidate;
  int ret = DatumParameterArray::clone_parameter(source, candidate, parameter_input_allocator_);
  if (OB_SUCC(ret)) { ret = clone_construction_type_bindings(source, candidate, parameter_input_allocator_, 0); }
  if (OB_SUCC(ret)) { output = candidate; }
  return ret;
}

int ObPhysicalPlanCtx::append_parameter_input(const DatumParameter &parameter)
{
  return native_parameters_ready_ ? OB_STATE_NOT_MATCH : param_store_.push_back(parameter);
}

int ObPhysicalPlanCtx::set_parameter_input(int64_t index, const DatumParameter &parameter)
{
  if (native_parameters_ready_) { return OB_STATE_NOT_MATCH; }
  if (index < 0 || index >= param_store_.count()) { return OB_INVALID_ARGUMENT; }
  // This is an explicit construction/PL-variable assignment, preserving the
  // source pointee identity. The producer owns borrowed references until bind.
  param_store_.at(index) = parameter;
  return OB_SUCCESS;
}

int ObPhysicalPlanCtx::assign_parameter_inputs(const ParamStore &parameters)
{
  if (native_parameters_ready_) { return OB_STATE_NOT_MATCH; }
  if (&parameters == &param_store_) { return OB_SUCCESS; }
  int ret = param_store_.reserve(parameters.count());
  if (OB_SUCC(ret)) { ret = param_store_.assign(parameters); }
  return ret;
}

int ObPhysicalPlanCtx::truncate_runtime_parameters(int64_t count)
{
  int ret = OB_SUCCESS;
  if (count < original_param_cnt_ || count < 0 || count > get_parameter_count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!native_parameters_ready_) {
    while (param_store_.count() > count) { param_store_.pop_back(); }
  } else {
    while (native_parameter_metadata_.count() > count) { native_parameter_metadata_.pop_back(); }
    while (native_parameter_types_.count() > count) { native_parameter_types_.pop_back(); }
    for (int64_t i = native_group_inputs_.count(); i > 0; --i) {
      if (native_group_inputs_.at(i - 1).index_ >= count) { native_group_inputs_.remove(i - 1); }
    }
    // Capacity can remain for compiled frame aliases. Every accessor and wire
    // sender gates by the independent logical metadata count, never capacity.
  }
  return ret;
}

void ObPhysicalPlanCtx::mark_batch_parameters()
{
  if (native_parameters_ready_) {
    for (int64_t i = 0; i < native_parameter_metadata_.count(); ++i) {
      native_parameter_metadata_.at(i).flag_.is_batch_parameter_ = true;
    }
  } else {
    for (int64_t i = 0; i < param_store_.count(); ++i) {
      param_store_.at(i).get_param_flag().is_batch_parameter_ = true;
    }
  }
}

int ObPhysicalPlanCtx::build_resolution_inputs(ParamStore &inputs, ObIAllocator &statement_owner) const
{
  if (!inputs.empty()) { return OB_INVALID_ARGUMENT; }
  const int64_t count = get_parameter_count();
  int ret = inputs.reserve(count);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    DatumParameter construction;
    if (!native_parameters_ready_) {
      // Preserve the construction domain: clone physical array graphs and
      // their real leaf declarations, while PL pointees keep their producer.
      const DatumParameter &source = param_store_.at(i);
      if (OB_FAIL(DatumParameterArray::clone_parameter(source, construction, statement_owner))) {
      } else { ret = clone_construction_type_bindings(source, construction, statement_owner, 0); }
    } else {
      NativeTypedDatum borrowed, owned;
      DatumParameterMetadata metadata = *get_parameter_metadata(i);
      const GroupParameterInput *group = nullptr;
      for (int64_t j = 0; group == nullptr && j < native_group_inputs_.count(); ++j) {
        if (native_group_inputs_.at(j).index_ == i) { group = &native_group_inputs_.at(j); }
      }
      if (group == nullptr) {
        ret = capture_native_parameter(i, borrowed, metadata);
      } else {
        borrowed.datum_ = group->input_.datum_;
        borrowed.type_ = group->type_;
        const ParamFlag flags = metadata.flag_;
        metadata = group->input_.get_metadata();
        metadata.flag_ = flags;
      }
      alignas(8) char scratch[64];
      ObDatum physical;
      if (OB_SUCC(ret) && OB_FAIL(clone_native_parameter_value(borrowed, statement_owner, owned))) {
      } else if (OB_SUCC(ret) && OB_FAIL(export_native_parameter_value(owned, physical, scratch, sizeof(scratch)))) {
      } else if (OB_SUCC(ret) && OB_FAIL(construction.assign_legacy_owned(physical, metadata.type_meta_, statement_owner))) {
      } else if (OB_SUCC(ret)) { construction.set_metadata(metadata); }
    }
    if (OB_SUCC(ret)) { ret = inputs.push_back(construction); }
  }
  if (OB_FAIL(ret)) { inputs.reuse(); }
  return ret;
}

int ObPhysicalPlanCtx::merge_resolution_parameter_flags(const ParamStore &inputs)
{
  if (inputs.count() != get_parameter_count()) { return OB_STATE_NOT_MATCH; }
  for (int64_t i = 0; i < inputs.count(); ++i) {
    if (native_parameters_ready_) {
      native_parameter_metadata_.at(i).flag_ = inputs.at(i).get_param_flag();
    } else {
      param_store_.at(i).set_param_flag(inputs.at(i).get_param_flag());
    }
  }
  return OB_SUCCESS;
}

int ObPhysicalPlanCtx::init_datum_param_store()
{
  if (native_parameters_ready_) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  const int64_t count = param_store_.count();
  native_parameter_metadata_.reuse();
  native_parameter_types_.reuse();
  native_group_inputs_.reuse();
  param_frame_ptrs_.reuse();
  param_frame_capacity_ = 0;
  if (original_param_cnt_ < 0 || original_param_cnt_ > count) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(native_parameter_metadata_.reserve(count))) {
  } else if (OB_FAIL(native_parameter_types_.reserve(count))) {
  } else if (OB_FAIL(reserve_param_frame(count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const DatumParameter &input = param_store_.at(i);
    Datum value;
    const SqlType *type = nullptr;
    DatumParameterMetadata attributes = input.get_metadata();
    if (input.is_ext_sql_array()) {
      // An array envelope is a distinct logical value. Keep only actual array
      // indexes, owning its graph/leaf declarations before inputs can retire.
      Datum array_value;
      const SqlType *array_type = nullptr;
      if (OB_FAIL(prepare_runtime_parameter(input, array_value, array_type, i))) {
      } else {
        GroupParameterInput group;
        group.index_ = i;
        group.type_ = array_type;
        group.input_ = input;
        group.input_.set_datum(array_value);
        const DatumParameterArray *array = group.input_.get_array<DatumParameterArray>();
        Datum selected;
        const SqlType *selected_type = nullptr;
        if (array == nullptr || array->count_ <= 0) { ret = OB_INVALID_ARGUMENT; }
        else if (OB_FAIL(array->get_native_element(0, selected, selected_type))) {
        } else {
          NativeTypedDatum borrowed, snapshot;
          borrowed.datum_ = selected;
          borrowed.type_ = selected_type;
          if (OB_FAIL(clone_native_parameter_value(borrowed, allocator_, snapshot))) {
          } else {
            value = snapshot.datum_;
            type = snapshot.type_;
            const DatumParameterMetadata &leaf = array->data_[0].get_metadata();
            attributes.type_meta_ = leaf.type_meta_;
            attributes.accuracy_ = leaf.accuracy_;
            attributes.param_meta_ = leaf.param_meta_;
            ret = native_group_inputs_.push_back(group);
          }
        }
      }
    } else {
      ret = prepare_runtime_parameter(input, value, type, i);
    }
    // Initial binding retains all producer attributes independently of the
    // prepared Type. In particular, do not fill accuracy or rewrite declared meta.
    if (OB_SUCC(ret) && OB_FAIL(native_parameter_metadata_.push_back(attributes))) {
    } else if (OB_SUCC(ret) && OB_FAIL(native_parameter_types_.push_back(type))) {
    } else if (OB_SUCC(ret)) {
      ret = publish_runtime_parameter(i, attributes, value, *type);
    }
  }
  if (OB_SUCC(ret)) {
    // Final slots, Types and all surviving group graphs now have the execution
    // owner. The construction arena can physically release its pages, rather
    // than retaining an ordinary value array behind a zero logical count.
    native_parameters_ready_ = true;
    param_store_.destroy();
    parameter_input_allocator_.reset();
  } else {
    // No ready getter has exposed any staging slot. Original construction
    // descriptors and bytes remain available for a corrected/retried bind.
    native_parameter_metadata_.reuse();
    native_parameter_types_.reuse();
    native_group_inputs_.reuse();
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
  const SqlTypeMeta &meta = source.is_null() && !source.get_param_meta().is_null()
      ? source.get_param_meta() : source.get_type_meta();
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
      const SqlType *previous = index >= 0 && index < native_parameter_types_.count()
          ? native_parameter_types_.at(index) : nullptr;
      const SqlType *adjacent = index > 0 && index - 1 < native_parameter_types_.count()
          ? native_parameter_types_.at(index - 1) : nullptr;
      if (previous != nullptr && same_parameter_type(*previous, resolved)) { candidate_type = previous; }
      else if (adjacent != nullptr && same_parameter_type(*adjacent, resolved)) { candidate_type = adjacent; }
      if (candidate_type == nullptr) { ret = resolved.clone_to(allocator_, candidate_type); }
    }
  } else {
    ObArenaAllocator temporary("ParamTypePrep");
    const SqlType *resolved = nullptr;
    if (meta.is_ext_sql_array()) {
      // SQL array envelopes need no session/schema lookup. Their real leaf
      // declarations are validated and owned by clone_native_parameter_value.
      SqlTypeSpec specification;
      specification.attributes.set_meta_type(meta);
      specification.attributes.set_accuracy(source.get_accuracy());
      specification.form = SqlTypeForm::Array;
      specification.heterogeneous = true;
      ret = SqlType::create(specification, temporary, resolved);
    } else if (exec_ctx_ == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // Runtime type resolution may populate this execution context's lazy type cache.
      ObExecContext &execution = *const_cast<ObExecContext *>(exec_ctx_);
      pl::ObPLContext *stack = execution.get_pl_stack_ctx();
      const pl::ObPLINS *declarations = stack == nullptr ? nullptr : stack->get_current_ctx();
      ret = ObExprUtil::resolve_sql_type(execution, meta, source.get_accuracy(),
          source.get_subschema_id(), temporary, resolved, declarations);
    }
    if (OB_SUCC(ret)) {
      const SqlType *previous = index >= 0 && index < native_parameter_types_.count()
          ? native_parameter_types_.at(index) : nullptr;
      if (previous != nullptr && same_parameter_type(*previous, *resolved)) { candidate_type = previous; }
      else { ret = resolved->clone_to(allocator_, candidate_type); }
    }
  }
  if (OB_SUCC(ret)) {
    const DatumCopyPlan plan(*candidate_type);
    Datum imported;
    Datum owned;
    alignas(8) char source_scratch[64];
    ObDatum source_view;
    if (source.get_type() == ObIntType && source.datum_.is_present()
        && source.datum_.length() == 0
        && source.datum_.reference_flags() == Datum::NoReferenceFlags
        && candidate_type->kind() == ObIntType
        && candidate_type->canonical_representation() == DatumRepresentation::Inline64) {
      // Generic and semantic signed BIGINT share the canonical inline word.
      // Match import's descriptor normalization, then keep its checked final
      // copy; all other physical/state domains use the original bridge below.
      imported.set_int(source.datum_.get_int());
      ret = plan.deep_copy(imported, owned, allocator_);
    } else if (OB_FAIL(source.export_legacy(source_view, source_scratch, sizeof(source_scratch)))) {
    } else if (OB_FAIL(plan.import_legacy(source_view, imported))) {
    } else if (candidate_type->legacy_meta().is_ext_sql_array()) {
      NativeTypedDatum borrowed, snapshot;
      borrowed.datum_ = imported;
      borrowed.type_ = candidate_type;
      ret = clone_native_parameter_value(borrowed, allocator_, snapshot);
      if (OB_SUCC(ret)) { owned = snapshot.datum_; candidate_type = snapshot.type_; }
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

int ObPhysicalPlanCtx::normalize_parameter_metadata(const Datum &value, const SqlType &type,
    const DatumParameterMetadata &source, DatumParameterMetadata &output) const
{
  DatumParameterMetadata candidate = source;
  // Metadata is a separate producer-owned record, not a default inferred from
  // the semantic Type. Only adapt the current physical kind/state when needed;
  // retain accuracy, declared meta, flags and source locations verbatim.
  if (value.is_null()) {
    candidate.type_meta_.set_null();
  } else if (value.is_present()) {
    if (candidate.type_meta_.get_type() != type.kind()) {
      candidate.type_meta_ = type.legacy_meta();
    }
  } else if (!candidate.type_meta_.is_ext() || candidate.type_meta_.get_extend_type() != 0) {
    candidate.type_meta_.set_ext();
  }
  output = candidate;
  return OB_SUCCESS;
}

int ObPhysicalPlanCtx::publish_runtime_parameter(int64_t index,
    const DatumParameterMetadata &attributes, const Datum &value, const SqlType &type)
{
  if (index < 0 || index >= native_parameter_metadata_.count()
      || index >= native_parameter_types_.count() || index >= param_frame_capacity_) {
    return OB_INVALID_ARGUMENT;
  }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  if (frame < 0 || frame >= param_frame_ptrs_.count() || param_frame_ptrs_.at(frame) == nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  const DatumParameterMetadata metadata = attributes;
  ParameterValuePage &page = ParameterValuePage::from_handle(param_frame_ptrs_.at(frame));
  int ret = page.assign(slot, value, type, allocator_);
  if (OB_SUCC(ret)) {
    native_parameter_types_.at(index) = &type;
    native_parameter_metadata_.at(index) = metadata;
  }
  return ret;
}

bool ObPhysicalPlanCtx::has_native_parameter(int64_t index) const
{
  bool valid = native_parameters_ready_ && index >= 0 && index < native_parameter_metadata_.count()
      && index < param_frame_capacity_ && index < native_parameter_types_.count()
      && native_parameter_types_.at(index) != nullptr;
  if (valid) {
    int64_t frame = 0, slot = 0;
    ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
    valid = frame >= 0 && frame < param_frame_ptrs_.count() && param_frame_ptrs_.at(frame) != nullptr
        && slot < ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).capacity();
  }
  return valid;
}

const SqlType *ObPhysicalPlanCtx::get_native_parameter_type(int64_t index) const
{
  return has_native_parameter(index) ? native_parameter_types_.at(index) : nullptr;
}

const DatumParameterMetadata *ObPhysicalPlanCtx::get_parameter_metadata(int64_t index) const
{
  return has_native_parameter(index) ? &native_parameter_metadata_.at(index) : nullptr;
}

bool ObPhysicalPlanCtx::read_native_parameter_state(int64_t index, Datum::State &state) const
{
  if (!has_native_parameter(index)) { return false; }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  state = ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).state(slot);
  return true;
}

int ObPhysicalPlanCtx::read_native_parameter(int64_t index, Datum &value) const
{
  if (!has_native_parameter(index)) { return OB_INVALID_ARGUMENT; }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  return ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).read(slot, value);
}

bool ObPhysicalPlanCtx::read_native_bigint_parameter(int64_t index, int64_t &value) const
{
  Datum::State state = Datum::State::Null;
  return read_native_bigint_parameter(index, value, state) && state == Datum::State::Present;
}

bool ObPhysicalPlanCtx::read_native_bigint_parameter(
    int64_t index, int64_t &value, Datum::State &state) const
{
  if (!native_parameters_ready_ || index < 0 || index >= native_parameter_metadata_.count()
      || index >= param_frame_capacity_ || index >= native_parameter_types_.count()) {
    return false;
  }
  const SqlType *type = native_parameter_types_.at(index);
  if (type == nullptr || type->kind() != ObIntType
      || type->canonical_representation() != DatumRepresentation::Inline64) {
    return false;
  }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  if (frame < 0 || frame >= param_frame_ptrs_.count() || param_frame_ptrs_.at(frame) == nullptr) {
    return false;
  }
  return ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).read_i64(slot, value, state);
}

int ObPhysicalPlanCtx::materialize_parameter_for_expr(int64_t index, Datum *&value)
{
  if (!has_native_parameter(index)) { return OB_INVALID_ARGUMENT; }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  return ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).materialize(slot, allocator_, value);
}

Datum *ObPhysicalPlanCtx::locate_materialized_parameter(int64_t index) const
{
  if (!has_native_parameter(index)) { return nullptr; }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  return ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).find_materialized(slot);
}

ObEvalInfo *ObPhysicalPlanCtx::get_parameter_eval_info(int64_t index) const
{
  if (!has_native_parameter(index)) { return nullptr; }
  int64_t frame = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, index, frame, slot);
  return &ParameterValuePage::from_handle(param_frame_ptrs_.at(frame)).eval_info(slot);
}

const DatumParameter *ObPhysicalPlanCtx::get_group_parameter_input(int64_t index) const
{
  if (!native_parameters_ready_ || index < 0 || index >= get_parameter_count()) { return nullptr; }
  for (int64_t i = 0; i < native_group_inputs_.count(); ++i) {
    if (native_group_inputs_.at(i).index_ == index) { return &native_group_inputs_.at(i).input_; }
  }
  return nullptr;
}

int ObPhysicalPlanCtx::capture_native_parameter(int64_t index, NativeTypedDatum &value,
    DatumParameterMetadata &attributes) const
{
  NativeTypedDatum candidate;
  int ret = read_native_parameter(index, candidate.datum_);
  if (OB_SUCC(ret)) {
    candidate.type_ = native_parameter_types_.at(index);
    const DatumParameterMetadata metadata = native_parameter_metadata_.at(index);
    value = candidate;
    attributes = metadata;
  }
  return ret;
}

int ObPhysicalPlanCtx::capture_runtime_parameter_slot(int64_t index,
    NativeParameterSlotSnapshot &snapshot) const
{
  NativeParameterSlotSnapshot candidate;
  int ret = capture_native_parameter(index, candidate.value_, candidate.attributes_);
  if (OB_SUCC(ret)) {
    candidate.owner_ = this;
    candidate.generation_ = parameter_snapshot_generation_;
    candidate.index_ = index;
    int64_t page = 0, slot = 0;
    ScalarFrameLayout::locate_parameter(original_param_cnt_, index, page, slot);
    const ParameterValuePage &values = ParameterValuePage::from_handle(param_frame_ptrs_.at(page));
    candidate.word_ = values.words()[slot];
    candidate.state_ = values.states()[slot];
    ret = validate_runtime_parameter_slot(candidate);
  }
  if (OB_SUCC(ret)) { snapshot = candidate; }
  return ret;
}

int ObPhysicalPlanCtx::validate_runtime_parameter_slot(const NativeParameterSlotSnapshot &snapshot) const
{
  if (snapshot.owner_ != this || snapshot.generation_ != parameter_snapshot_generation_
      || !has_native_parameter(snapshot.index_) || snapshot.value_.type_ == nullptr) {
    return OB_STATE_NOT_MATCH;
  }
  int64_t page_index = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, snapshot.index_, page_index, slot);
  const ParameterValuePage &page = ParameterValuePage::from_handle(param_frame_ptrs_.at(page_index));
  const SqlType &type = *snapshot.value_.type_;
  const Datum &value = snapshot.value_.datum_;
  const bool compact = type.kind() == ObIntType
      && type.canonical_representation() == DatumRepresentation::Inline64;
  if (!page.matches_layout(slot, *native_parameter_types_.at(snapshot.index_))
      || compact != ((snapshot.state_ & ParameterValuePage::INLINE_I64) != 0)
      || static_cast<uint8_t>(value.state()) != (snapshot.state_ & ParameterValuePage::STATE_MASK)) {
    return OB_INVALID_ARGUMENT;
  }
  // Validate current state too; its Type may legitimately differ after a rebind.
  Datum current;
  int ret = page.read(slot, current);
  int64_t bytes = 0;
  if (OB_SUCC(ret)) { ret = DatumCopyPlan(type).carrier_span_size(value, bytes); }
  if (OB_SUCC(ret) && value.is_present()) {
    if (compact && (value.length() != 0 || value.reference_flags() != Datum::NoReferenceFlags)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (!compact && snapshot.word_ == 0) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

void ObPhysicalPlanCtx::restore_runtime_parameter_slot(const NativeParameterSlotSnapshot &snapshot)
{
  OB_ASSERT(validate_runtime_parameter_slot(snapshot) == OB_SUCCESS);
  int64_t page_index = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(original_param_cnt_, snapshot.index_, page_index, slot);
  ParameterValuePage &page = ParameterValuePage::from_handle(param_frame_ptrs_.at(page_index));
  const Datum value = snapshot.value_.datum_;
  if (value.is_present() && (snapshot.state_ & ParameterValuePage::INLINE_I64) == 0) {
    // A setter can overwrite the old cell and later replace its slot by NULL
    // or I64. Restore its saved descriptor contents before publishing the word.
    *reinterpret_cast<Datum *>(snapshot.word_) = value;
  }
  page.words()[slot] = snapshot.word_;
  page.states()[slot] = snapshot.state_;
  if (Datum *projection = page.find_materialized(slot)) { *projection = value; }
  native_parameter_types_.at(snapshot.index_) = snapshot.value_.type_;
  native_parameter_metadata_.at(snapshot.index_) = snapshot.attributes_;
  // Unlike assign()/set_runtime_parameter(), this keeps current EvalInfo and
  // evaluated bitmaps unchanged. Payload spans still borrow statement lifetime.
}

int ObPhysicalPlanCtx::capture_native_group(int64_t group_index, NativeParameterGroup &output) const
{
  if (!native_parameters_ready_ || group_index < 0 || group_index >= native_group_inputs_.count()) {
    return OB_INVALID_ARGUMENT;
  }
  const GroupParameterInput &group = native_group_inputs_.at(group_index);
  NativeParameterGroup result;
  result.index_ = group.index_;
  result.value_.datum_ = group.input_.datum_;
  result.value_.type_ = group.type_;
  result.attributes_ = group.input_.get_metadata();
  // Runtime resolution may have updated the independent parameter flags.
  result.attributes_.flag_ = native_parameter_metadata_.at(group.index_).flag_;
  output = result;
  return OB_SUCCESS;
}

int ObPhysicalPlanCtx::runtime_parameter_legacy_size(int64_t index, int64_t &bytes) const
{
  Datum value;
  int ret = read_native_parameter(index, value);
  if (OB_SUCC(ret)) { ret = DatumCopyPlan(*native_parameter_types_.at(index)).legacy_payload_size(value, bytes); }
  return ret;
}

int ObPhysicalPlanCtx::export_runtime_parameter_legacy(int64_t index, ObDatum &out,
    void *caller_scratch, int64_t capacity) const
{
  Datum value;
  int ret = read_native_parameter(index, value);
  if (OB_SUCC(ret)) { ret = DatumCopyPlan(*native_parameter_types_.at(index)).legacy_view(
      value, out, caller_scratch, capacity); }
  return ret;
}

int ObPhysicalPlanCtx::set_runtime_parameter(int64_t index, const Datum &source,
    const SqlType &supplied_type, const DatumParameterMetadata &attributes)
{
  int ret = OB_SUCCESS;
  if (!native_parameters_ready_) { return OB_NOT_INIT; }
  const SqlType *type = get_native_parameter_type(index);
  if (type == nullptr) { return OB_INVALID_ARGUMENT; }
  // Both arguments may alias this binding. Snapshot before any preparation or
  // metadata normalization, and leave the previous slot untouched on failure.
  const Datum input = source;
  const DatumParameterMetadata source_attributes = attributes;
  if (!same_parameter_type(*type, supplied_type)) {
    ret = supplied_type.clone_to(allocator_, type);
  }
  Datum owned;
  if (OB_SUCC(ret)) {
    const DatumCopyPlan plan(*type);
    if (type->legacy_meta().is_ext_sql_array()) {
      NativeTypedDatum borrowed, snapshot;
      borrowed.datum_ = input;
      borrowed.type_ = type;
      ret = clone_native_parameter_value(borrowed, allocator_, snapshot);
      if (OB_SUCC(ret)) { owned = snapshot.datum_; type = snapshot.type_; }
    } else if (type->requires_resource_clone()) {
      int64_t bytes = 0;
      if (OB_FAIL(plan.carrier_span_size(input, bytes))) {
      } else {
        char *buffer = bytes == 0 ? nullptr : static_cast<char *>(allocator_.alloc(bytes));
        if (bytes > 0 && buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          int64_t position = 0;
          // PL lvalues keep their existing statement/session pointee owner.
          ret = plan.copy_carrier_span_borrowing_resources(input, owned, buffer, bytes, position);
        }
      }
    } else {
      ret = plan.deep_copy(input, owned, allocator_);
    }
    DatumParameterMetadata metadata;
    if (OB_SUCC(ret) && OB_FAIL(normalize_parameter_metadata(owned, *type, source_attributes, metadata))) {
    } else if (OB_SUCC(ret)) {
      ret = publish_runtime_parameter(index, metadata, owned, *type);
    }
  }
  return ret;
}


int ObPhysicalPlanCtx::validate_parameter_page_rebind(const ObIArray<char *> &pages) const
{
  if (exec_ctx_ == nullptr) { return OB_SUCCESS; }
  char **directories[] = {exec_ctx_->get_frames(), exec_ctx_->get_ori_frames()};
  const uint64_t counts[] = {exec_ctx_->get_frame_cnt(), exec_ctx_->get_ori_frame_cnt()};
  for (int64_t d = 0; d < 2; ++d) {
    for (uint64_t i = 0; directories[d] != nullptr && i < counts[d]; ++i) {
      for (int64_t j = 0; j < param_frame_ptrs_.count(); ++j) {
        if (directories[d][i] == param_frame_ptrs_.at(j) && j >= pages.count()) {
          return OB_STATE_NOT_MATCH;
        }
      }
    }
  }
  return OB_SUCCESS;
}

void ObPhysicalPlanCtx::rebind_parameter_page_aliases(const ObIArray<char *> &pages)
{
  if (exec_ctx_ == nullptr) { return; }
  char **directories[] = {exec_ctx_->get_frames(), exec_ctx_->get_ori_frames()};
  const uint64_t counts[] = {exec_ctx_->get_frame_cnt(), exec_ctx_->get_ori_frame_cnt()};
  // ExecContext owns these independent frame directories; ObEvalCtx and an
  // Expr evaluation guard borrow the directory, rather than ParamCtx's array.
  for (int64_t d = 0; d < 2; ++d) {
    for (uint64_t i = 0; directories[d] != nullptr && i < counts[d]; ++i) {
      for (int64_t j = 0; j < param_frame_ptrs_.count(); ++j) {
        if (directories[d][i] == param_frame_ptrs_.at(j)) {
          OB_ASSERT(j < pages.count());
          directories[d][i] = pages.at(j);
          break;
        }
      }
    }
  }
}

int ObPhysicalPlanCtx::adopt_runtime_parameters(ObPhysicalPlanCtx &staged)
{
  if (!staged.native_parameters_ready_) { return OB_NOT_INIT; }
  int ret = OB_SUCCESS;
  if (exec_ctx_ != nullptr && exec_ctx_->get_frames() != nullptr
      && native_parameters_ready_ && original_param_cnt_ != staged.original_param_cnt_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(validate_parameter_page_rebind(staged.param_frame_ptrs_))) {
  } else if (OB_FAIL(native_parameter_metadata_.reserve(staged.native_parameter_metadata_.count()))) {
  } else if (OB_FAIL(native_parameter_types_.reserve(staged.native_parameter_types_.count()))) {
  } else if (OB_FAIL(native_group_inputs_.reserve(staged.native_group_inputs_.count()))) {
  } else if (OB_FAIL(param_frame_ptrs_.reserve(staged.param_frame_ptrs_.count()))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_frame_ptrs_.count()
        && i < staged.param_frame_ptrs_.count(); ++i) {
      ret = ParameterValuePage::from_handle(staged.param_frame_ptrs_.at(i)).prepare_existing_projections(
          ParameterValuePage::from_handle(param_frame_ptrs_.at(i)), allocator_);
    }
  }
  if (OB_SUCC(ret)) {
    rebind_parameter_page_aliases(staged.param_frame_ptrs_);
    const int a = native_parameter_metadata_.assign(staged.native_parameter_metadata_);
    const int b = native_parameter_types_.assign(staged.native_parameter_types_);
    const int c = native_group_inputs_.assign(staged.native_group_inputs_);
    const int d = param_frame_ptrs_.assign(staged.param_frame_ptrs_);
    OB_ASSERT(a == OB_SUCCESS && b == OB_SUCCESS && c == OB_SUCCESS && d == OB_SUCCESS);
    UNUSED(a); UNUSED(b); UNUSED(c); UNUSED(d);
    original_param_cnt_ = staged.original_param_cnt_;
    param_frame_capacity_ = staged.param_frame_capacity_;
    native_parameters_ready_ = true;
    param_store_.destroy();
    parameter_input_allocator_.reset();
  }
  return ret;
}

int ObPhysicalPlanCtx::rebuild_runtime_parameters(const ParamStore &parameters)
{
  ObPhysicalPlanCtx staged(allocator_);
  staged.exec_ctx_ = exec_ctx_;
  staged.original_param_cnt_ = std::min(original_param_cnt_, parameters.count());
  int ret = staged.assign_parameter_inputs(parameters);
  if (OB_SUCC(ret) && OB_FAIL(staged.init_datum_param_store())) {
  } else if (OB_SUCC(ret)) { ret = adopt_runtime_parameters(staged); }
  return ret;
}

int ObPhysicalPlanCtx::clone_runtime_parameters_to(ObPhysicalPlanCtx &destination) const
{
  if (!native_parameters_ready_) { return OB_NOT_INIT; }
  if (&destination == this) { return OB_SUCCESS; }
  ObPhysicalPlanCtx staged(destination.allocator_);
  staged.original_param_cnt_ = original_param_cnt_;
  const int64_t count = get_parameter_count();
  int ret = staged.native_parameter_metadata_.reserve(count);
  if (OB_SUCC(ret) && OB_FAIL(staged.native_parameter_types_.reserve(count))) {
  } else if (OB_SUCC(ret) && OB_FAIL(staged.native_group_inputs_.reserve(native_group_inputs_.count()))) {
  } else if (OB_SUCC(ret) && OB_FAIL(staged.reserve_param_frame(count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    NativeTypedDatum borrowed, owned;
    DatumParameterMetadata metadata;
    if (OB_FAIL(capture_native_parameter(i, borrowed, metadata))) {
    } else if (OB_FAIL(clone_native_parameter_value(borrowed, destination.allocator_, owned))) {
    } else if (OB_FAIL(staged.native_parameter_metadata_.push_back(metadata))) {
    } else if (OB_FAIL(staged.native_parameter_types_.push_back(owned.type_))) {
    } else { ret = staged.publish_runtime_parameter(i, metadata, owned.datum_, *owned.type_); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < native_group_inputs_.count(); ++i) {
    const GroupParameterInput &input = native_group_inputs_.at(i);
    NativeTypedDatum borrowed, owned;
    borrowed.datum_ = input.input_.datum_;
    borrowed.type_ = input.type_;
    if (OB_FAIL(clone_native_parameter_value(borrowed, destination.allocator_, owned))) {
    } else {
      GroupParameterInput group;
      group.index_ = input.index_;
      group.type_ = owned.type_;
      group.input_ = input.input_;
      group.input_.set_datum(owned.datum_);
      ret = staged.native_group_inputs_.push_back(group);
    }
  }
  if (OB_SUCC(ret)) {
    staged.native_parameters_ready_ = true;
    ret = destination.adopt_runtime_parameters(staged);
  }
  return ret;
}

int ObPhysicalPlanCtx::restore_native_parameter_frames(const ObIArray<char *> &frames,
    const ObIArray<const SqlType *> &types, int64_t original_count,
    const ObIArray<DatumParameterMetadata> *metadata, const ObIArray<NativeParameterGroup> *groups)
{
  int ret = OB_SUCCESS;
  const int64_t count = types.count();
  if (count > INT64_MAX - ScalarFrameLayout::slots_per_frame()
      || original_count < 0 || original_count > count
      || (count != get_parameter_count() && (metadata == nullptr || groups == nullptr))
      || (metadata != nullptr && metadata->count() != count)
      || frames.count() != ScalarFrameLayout::frame_count(original_count)
          + ScalarFrameLayout::frame_count(count - original_count)) {
    return OB_INVALID_ARGUMENT;
  }
  ObPhysicalPlanCtx staged(allocator_);
  staged.original_param_cnt_ = original_count;
  if (OB_FAIL(staged.native_parameter_metadata_.reserve(count))) {
  } else if (OB_FAIL(staged.native_parameter_types_.reserve(count))) {
  } else if (OB_FAIL(staged.param_frame_ptrs_.reserve(frames.count()))) {
  } else if (OB_FAIL(staged.native_group_inputs_.reserve(native_group_inputs_.count()))) {
  }
  const int64_t original_pages = ScalarFrameLayout::frame_count(original_count);
  const int64_t slots_per_page = ScalarFrameLayout::slots_per_frame();
  for (int64_t i = 0; OB_SUCC(ret) && i < frames.count(); ++i) {
    char *handle = frames.at(i);
    const int64_t logical_slots = i < original_pages
        ? std::min(slots_per_page, original_count - i * slots_per_page)
        : std::min(slots_per_page, count - original_count - (i - original_pages) * slots_per_page);
    const int64_t required_capacity = i < original_pages ? logical_slots : slots_per_page;
    if (handle == nullptr || reinterpret_cast<uintptr_t>(handle) % alignof(ParameterValuePage) != 0) {
      ret = OB_INVALID_ARGUMENT;
    } else if (ParameterValuePage::from_handle(handle).capacity() < logical_slots
        || ParameterValuePage::from_handle(handle).capacity() > slots_per_page) {
      ret = OB_INVALID_ARGUMENT;
    } else if (ParameterValuePage::from_handle(handle).capacity() < required_capacity) {
      ParameterValuePage *expanded = nullptr;
      if (OB_FAIL(ParameterValuePage::allocate(allocator_, required_capacity, expanded))) {
      } else {
        const ParameterValuePage &source = ParameterValuePage::from_handle(handle);
        const int64_t begin = i < original_pages ? i * slots_per_page
            : original_count + (i - original_pages) * slots_per_page;
        for (int64_t j = 0; OB_SUCC(ret) && j < logical_slots; ++j) {
          Datum value;
          if (types.at(begin + j) == nullptr
              || !source.matches_layout(j, *types.at(begin + j))) { ret = OB_INVALID_ARGUMENT; }
          else if (OB_FAIL(source.read(j, value))) {
          } else if (OB_FAIL(expanded->assign(j, value, *types.at(begin + j), allocator_))) {
          } else { expanded->eval_info(j) = source.eval_info(j); }
        }
        if (OB_SUCC(ret)) { handle = reinterpret_cast<char *>(expanded); }
      }
    }
    if (OB_SUCC(ret)) { ret = staged.param_frame_ptrs_.push_back(handle); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    int64_t page = 0, slot = 0;
    ScalarFrameLayout::locate_parameter(original_count, i, page, slot);
    Datum native;
    DatumParameterMetadata attributes = metadata != nullptr ? metadata->at(i)
        : native_parameters_ready_ ? native_parameter_metadata_.at(i) : param_store_.at(i).get_metadata();
    int64_t span = 0;
    const ParameterValuePage &values = ParameterValuePage::from_handle(frames.at(page));
    if (types.at(i) == nullptr || !values.matches_layout(slot, *types.at(i))) { ret = OB_INVALID_ARGUMENT; }
    else if (OB_FAIL(values.read(slot, native))) {
    } else if (OB_FAIL(DatumCopyPlan(*types.at(i)).carrier_span_size(native, span))) {
    } else if (OB_FAIL(staged.native_parameter_metadata_.push_back(attributes))) {
    } else { ret = staged.native_parameter_types_.push_back(types.at(i)); }
  }
  if (OB_SUCC(ret) && groups != nullptr) {
    for (int64_t i = 0; OB_SUCC(ret) && i < groups->count(); ++i) {
      const NativeParameterGroup &incoming = groups->at(i);
      if (incoming.index_ < 0 || incoming.index_ >= count || incoming.value_.type_ == nullptr
          || !incoming.value_.type_->legacy_meta().is_ext_sql_array()) {
        ret = OB_DESERIALIZE_ERROR;
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
        if (groups->at(j).index_ == incoming.index_) { ret = OB_DESERIALIZE_ERROR; }
      }
      if (OB_SUCC(ret)) {
        NativeTypedDatum owned;
        // Validate every leaf declaration and own the surviving group graph
        // before the independent physical construction arena can retire.
        ret = clone_native_parameter_value(incoming.value_, allocator_, owned);
        if (OB_SUCC(ret)) {
          GroupParameterInput group;
          group.index_ = incoming.index_; group.type_ = owned.type_;
          alignas(8) char scratch[64];
          ObDatum physical;
          if (OB_FAIL(export_native_parameter_value(owned, physical, scratch, sizeof(scratch)))) {
          } else if (OB_FAIL(group.input_.assign_legacy_owned(physical, incoming.attributes_.type_meta_, allocator_))) {
          } else {
            group.input_.set_metadata(incoming.attributes_);
            ret = staged.native_group_inputs_.push_back(group);
          }
        }
      }
    }
  } else if (OB_SUCC(ret) && native_parameters_ready_) {
    ret = staged.native_group_inputs_.assign(native_group_inputs_);
  } else if (OB_SUCC(ret)) {
    // Historical v1 physical graphs need actual producer declarations before
    // activation. Never manufacture child Types from the array envelope.
    for (int64_t i = 0; OB_SUCC(ret) && i < param_store_.count(); ++i) {
      if (param_store_.at(i).is_ext_sql_array()) {
        Datum value;
        const SqlType *type = nullptr;
        if (OB_FAIL(staged.prepare_runtime_parameter(param_store_.at(i), value, type, i))) {
        } else {
          GroupParameterInput group;
          group.index_ = i; group.type_ = type; group.input_ = param_store_.at(i);
          group.input_.set_datum(value);
          ret = staged.native_group_inputs_.push_back(group);
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    staged.param_frame_capacity_ = original_count
        + (frames.count() - original_pages) * slots_per_page;
    staged.native_parameters_ready_ = true;
    ret = adopt_runtime_parameters(staged);
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



int ObPhysicalPlanCtx::reserve_param_frame(const int64_t input_capacity)
{
  int ret = OB_SUCCESS;
  const int64_t per_page = ScalarFrameLayout::slots_per_frame();
  if (input_capacity < 0 || original_param_cnt_ < 0
      || input_capacity > INT64_MAX - per_page || original_param_cnt_ > INT64_MAX - per_page) {
    return OB_INVALID_ARGUMENT;
  }
  if (input_capacity > param_frame_capacity_) {
    const int64_t original_pages = ScalarFrameLayout::frame_count(original_param_cnt_);
    const int64_t beyond_pages = ScalarFrameLayout::frame_count(
        std::max(int64_t(0), input_capacity - original_param_cnt_));
    const int64_t pages = original_pages + beyond_pages;
    ObSEArray<char *, 8> staged;
    if (OB_FAIL(staged.reserve(pages))) {
    } else if (OB_FAIL(param_frame_ptrs_.reserve(pages))) {
    }
    for (int64_t page = 0; OB_SUCC(ret) && page < pages; ++page) {
      char *frame = page < param_frame_ptrs_.count() ? param_frame_ptrs_.at(page) : nullptr;
      if (frame == nullptr) {
        const int64_t slots = page < original_pages
            ? std::min(per_page, original_param_cnt_ - page * per_page) : per_page;
        ParameterValuePage *page_value = nullptr;
        ret = ParameterValuePage::allocate(allocator_, slots, page_value);
        if (OB_SUCC(ret)) { frame = reinterpret_cast<char *>(page_value); }
      }
      if (OB_SUCC(ret)) { ret = staged.push_back(frame); }
    }
    if (OB_SUCC(ret)) {
      const int published = param_frame_ptrs_.assign(staged);
      OB_ASSERT(published == OB_SUCCESS);
      UNUSED(published);
      param_frame_capacity_ = original_param_cnt_ + beyond_pages * per_page;
    }
  }
  return ret;
}

int ObPhysicalPlanCtx::replace_batch_param_datum(const int64_t cur_group_id,
    const int64_t start_param, const int64_t param_cnt)
{
  int ret = OB_SUCCESS;
  const int64_t count = get_parameter_count();
  if (!native_parameters_ready_) { return OB_NOT_INIT; }
  if (start_param < 0 || param_cnt < 0 || start_param > count || param_cnt > count - start_param) {
    return OB_INVALID_ARGUMENT;
  }
  for (int64_t i = start_param; OB_SUCC(ret) && i < start_param + param_cnt; ++i) {
    const DatumParameter *group = get_group_parameter_input(i);
    if (group != nullptr) {
      const DatumParameterArray *array = group->get_array<DatumParameterArray>();
      if (array == nullptr || cur_group_id < 0 || cur_group_id >= array->count_) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        Datum value;
        const SqlType *type = nullptr;
        DatumParameterMetadata attributes = *get_parameter_metadata(i);
        const DatumParameterMetadata &leaf = array->data_[cur_group_id].get_metadata();
        attributes.type_meta_ = leaf.type_meta_;
        attributes.accuracy_ = leaf.accuracy_;
        attributes.param_meta_ = leaf.param_meta_;
        if (OB_FAIL(array->get_native_element(cur_group_id, value, type))) {
        } else { ret = set_runtime_parameter(i, value, *type, attributes); }
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

int ObPhysicalPlanCtx::parameter_wire_view(int64_t index, DatumParameter &output,
    void *scratch, int64_t capacity) const
{
  if (index < 0 || index >= get_parameter_count()) { return OB_INVALID_ARGUMENT; }
  if (!native_parameters_ready_) { output = param_store_.at(index); return OB_SUCCESS; }
  const DatumParameter *group = get_group_parameter_input(index);
  if (group != nullptr) { output = *group; return OB_SUCCESS; }
  ObDatum legacy;
  const DatumParameterMetadata *attributes = get_parameter_metadata(index);
  if (attributes == nullptr) { return OB_NOT_INIT; }
  int ret = export_runtime_parameter_legacy(index, legacy, scratch, capacity);
  if (OB_SUCC(ret) && OB_FAIL(output.assign_legacy_borrowed(legacy, attributes->type_meta_))) {
  } else if (OB_SUCC(ret)) { output.set_metadata(*attributes); }
  return ret;
}

OB_DEF_SERIALIZE(ObPhysicalPlanCtx)
{
  int ret = OB_SUCCESS;
  auto encode_parameter = [&](int64_t index) {
    alignas(8) char scratch[64];
    DatumParameter view;
    int result = parameter_wire_view(index, view, scratch, sizeof(scratch));
    if (result == OB_SUCCESS) {
      result = DatumParameterCodec::encode_parameter(view, buf, buf_len, pos);
    }
    return result;
  };
  bool send_full_parameters = false;
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
    send_full_parameters = false;
  } else {
    send_full_parameters = true;
  }
  // Follow the old sequence method
  OB_UNIS_ENCODE(tsc_snapshot_timestamp_);
  OB_UNIS_ENCODE(cur_time_);
  OB_UNIS_ENCODE(ts_timeout_us_);
  OB_UNIS_ENCODE(consistency_level_);
  const int64_t full_count = send_full_parameters ? get_parameter_count() : 0;
  OB_UNIS_ENCODE(full_count);
  for (int64_t i = 0; OB_SUCC(ret) && i < full_count; ++i) {
    ret = encode_parameter(i);
  }
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
      phy_plan_ != NULL && get_parameter_count() > 0) {
    // Serialize param store as needed
    // Serialize param store slots
    // Need to serialize param store count
    // Serialize param index
    // Serialize param value
    OB_UNIS_ENCODE(get_parameter_count());
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
      ret = encode_parameter(param_idxs->at(i));
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
          ret = encode_parameter(param_idxs->at(j));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(serialization::encode_i32(buf, buf_len, seri_param_cnt_pos, real_param_cnt))) {
      }
    }
  } else {
    /**
     * long long ago, get_parameter_count() may be serialized or not, depend on if it is 0,
     * if > 0, get_parameter_count() will be serialized,
     * if = 0, not.
     * but param_cnt will be always deserialized in deserialize operation, so it will
     * cause problem if we add other member in serialize and deserialize operation after
     * get_parameter_count(), like foreign_key_checks_. the serialized data of new member
     * will be deserialized as param_cnt.
     * why everything goes right before, especially when get_parameter_count() is 0 ?
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
  auto parameter_size = [&](int64_t index) {
    alignas(8) char scratch[64];
    DatumParameter view;
    int64_t bytes = 0;
    int result = parameter_wire_view(index, view, scratch, sizeof(scratch));
    if (result == OB_SUCCESS) { result = DatumParameterCodec::parameter_size(view, bytes); }
    return result == OB_SUCCESS ? bytes : int64_t(-1);
  };
  bool send_full_parameters = false;
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
    send_full_parameters = false;
  } else {
    send_full_parameters = true;
  }
  // Follow the old sequence method
  OB_UNIS_ADD_LEN(tsc_snapshot_timestamp_);
  OB_UNIS_ADD_LEN(cur_time_);
  OB_UNIS_ADD_LEN(ts_timeout_us_);
  OB_UNIS_ADD_LEN(consistency_level_);
  const int64_t full_count = send_full_parameters ? get_parameter_count() : 0;
  OB_UNIS_ADD_LEN(full_count);
  for (int64_t i = 0; i < full_count; ++i) {
    const int64_t parameter_bytes = parameter_size(i);
    if (parameter_bytes < 0) { return 0; }
    len += parameter_bytes;
  }
  OB_UNIS_ADD_LEN(obsolete_sys_view_param_count);
  OB_UNIS_ADD_LEN(sql_mode_);
  OB_UNIS_ADD_LEN(autoinc_params_);
  OB_UNIS_ADD_LEN(tablet_autoinc_param_);
  OB_UNIS_ADD_LEN(last_insert_id_session_);
  OB_UNIS_ADD_LEN(message_);
  OB_UNIS_ADD_LEN(expr_op_size_);
  OB_UNIS_ADD_LEN(is_ignore_stmt_);
  if (row_id_list_array != NULL && !row_id_list_array->empty() &&
      phy_plan_ != NULL && get_parameter_count() > 0) {
    // Serialize param store as needed
    // Need to serialize param store count
    // Serialize param index
    // Serialize param value
    OB_UNIS_ADD_LEN(get_parameter_count());
    // Serialization is needed to know how many parameter values are serialized, so skip the count position first
    int32_t real_param_cnt = 0;
    len += serialization::encoded_length_i32(real_param_cnt);
    if (phy_plan_->get_row_param_map().count() > 0) {
      const ObIArray<int64_t> &param_idxs = phy_plan_->get_row_param_map().at(0);
      for (int64_t i = 0; i < param_idxs.count(); ++i) {
        OB_UNIS_ADD_LEN(param_idxs.at(i));
        len += parameter_size(param_idxs.at(i));
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
              len += parameter_size(param_idxs.at(j));
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
    ret = DatumParameterCodec::decode_store(buf, data_len, pos, parameter_input_allocator_, param_store_);
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
      } else if (OB_FAIL(DatumParameterCodec::decode_parameter(buf, data_len, pos, parameter_input_allocator_, param_store_.at(param_idx)))) {
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
  if (native_parameters_ready_) { return OB_SUCCESS; }
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
