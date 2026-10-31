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
#include "sql/engine/expr/ob_native_pl_value.h"
#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter_array.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_user_type.h"
#include "sql/pl/ob_pl_allocator.h"

namespace oceanbase
{
namespace sql
{
using namespace common;
using namespace pl;
namespace
{
constexpr int MAX_GRAPH_DEPTH = 64;

bool same_nominal(const SqlNominalType &left, const SqlNominalType &right)
{
  return left.specified == right.specified && (!left.specified
      || (left.tenant_id == right.tenant_id && left.object_id == right.object_id
          && left.schema_version == right.schema_version));
}

bool same_full_declaration(const SqlType &left, const SqlType &right, int depth = 0)
{
  if (depth > MAX_GRAPH_DEPTH || !left.equals(right)) { return false; }
  if (left.legacy_meta().is_pl_extend_type()
      && (!left.pl_declaration().is_specified()
          || !left.pl_declaration().valid_with(left.nominal())
          || !left.pl_declaration().equals(right.pl_declaration()))) { return false; }
  if (left.element() != nullptr && !same_full_declaration(*left.element(), *right.element(), depth + 1)) {
    return false;
  }
  if (left.key_type() != nullptr && !same_full_declaration(*left.key_type(), *right.key_type(), depth + 1)) {
    return false;
  }
  if (left.value_type() != nullptr && !same_full_declaration(*left.value_type(), *right.value_type(), depth + 1)) {
    return false;
  }
  for (int64_t i = 0; i < left.member_count(); ++i) {
    if (!same_full_declaration(*left.member(i).type, *right.member(i).type, depth + 1)) { return false; }
  }
  return true;
}

int resolve_node(const SqlType *requested, const DatumParameter &source,
                 const INativePlTypeResolver *resolver, SqlType &scalar,
                 const SqlType *&type, const ObRecordType *&declaration)
{
  int ret = OB_SUCCESS;
  type = requested;
  declaration = nullptr;
  const SqlTypeMeta &meta = source.is_null() && source.get_type_meta().is_null()
      ? source.get_param_meta() : source.get_type_meta();
  const bool record = meta.is_pl_extend_type()
      || (requested != nullptr && requested->legacy_meta().is_pl_extend_type());
  const bool complex = meta.is_ext_sql_array() || record || meta.is_collection_sql_type()
      || meta.is_enum_or_set() || meta.is_enumset_inner_type();
  const uint8_t subtype = meta.is_pl_extend_type() ? meta.get_extend_type()
      : requested == nullptr ? 0 : requested->legacy_meta().get_extend_type();
  if (record && subtype != PL_RECORD_TYPE) { return OB_NOT_SUPPORTED; }
  if (record || (type == nullptr && complex)) {
    const SqlType *resolved = nullptr;
    if (resolver == nullptr) { ret = OB_NOT_SUPPORTED; }
    else if (OB_FAIL(resolver->resolve(requested, source, resolved, declaration))) {}
    else if (resolved == nullptr || (requested != nullptr && !same_full_declaration(*requested, *resolved))) {
      ret = OB_INVALID_ARGUMENT;
    } else { type = resolved; }
  } else if (type == nullptr) {
    ret = SqlType::from_legacy(source.get_type_meta(), source.get_accuracy(), scalar);
    if (ret == OB_SUCCESS) { type = &scalar; }
  }
  if (ret == OB_SUCCESS && type == nullptr) { ret = OB_ERR_UNEXPECTED; }
  return ret;
}

int record_declaration(const SqlType &type, const DatumParameter &source,
                       const ObRecordType *declaration, ObPLRecord *&record)
{
  int ret = OB_SUCCESS;
  const uint64_t declaration_id = declaration == nullptr ? OB_INVALID_ID : declaration->get_user_type_id();
  const bool source_has_declaration = source.get_type_meta().is_pl_extend_type()
      || (source.is_null() && source.get_param_meta().is_pl_extend_type());
  if (declaration == nullptr
      || (!source.is_null() && (!source.datum_.has_ext_value() || source.get_ext() == 0))
      || type.legacy_meta().get_extend_type() != PL_RECORD_TYPE
      || (type.form() != SqlTypeForm::Record && type.form() != SqlTypeForm::UserDefined)
      || !type.pl_declaration().is_specified()
      || !type.pl_declaration().valid_with(type.nominal())
      || type.pl_declaration().declaration_id != declaration_id
      || !type.pl_declaration().equals(declaration->get_sql_declaration_identity())
      || !same_nominal(type.nominal(), declaration->get_sql_nominal_identity())
      || (source_has_declaration && source.get_accuracy().get_accuracy() != declaration_id)
      || type.member_count() != declaration->get_record_member_count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!source.is_null()) {
    record = reinterpret_cast<ObPLRecord *>(source.get_ext());
    if (record->get_type() != PL_RECORD_TYPE || record->get_id() != declaration_id
        || (record->get_count() != OB_INVALID_COUNT && record->get_count() != type.member_count())
        || (!record->is_inited() && record->get_allocator() != nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  for (int64_t i = 0; ret == OB_SUCCESS && i < type.member_count(); ++i) {
    const ObPLDataType *member = declaration->get_record_member_type(i);
    const ObString *name = declaration->get_record_member_name(i);
    const SqlTypeMember &full = type.member(i);
    if (member == nullptr || name == nullptr || *name != full.name
        || member->is_not_null() == full.nullable || member->get_obj_type() != full.type->kind()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (member->is_record_type()
        && member->get_user_type_id() != full.type->pl_declaration().declaration_id) {
      ret = OB_INVALID_ARGUMENT;
    } else if (record != nullptr && record->is_inited()
        && record->get_not_null()[i] != member->is_not_null()) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  // Schema nominal.object_id/version are compared only by FullType equality
  // against the resolver's actual declaration Type, never against record ID.
  return ret;
}

int check_typed_graph(const SqlType *requested, const DatumParameter &source,
                      const INativePlTypeResolver *resolver, int depth)
{
  if (depth > MAX_GRAPH_DEPTH) { return OB_SIZE_OVERFLOW; }
  const SqlTypeMeta &declared_meta = source.is_null() && source.get_type_meta().is_null()
      ? source.get_param_meta() : source.get_type_meta();
  if (source.is_null() && !declared_meta.is_pl_extend_type() && !declared_meta.is_ext_sql_array()
      && (requested == nullptr || !requested->requires_resource_clone())) { return OB_SUCCESS; }
  if (source.datum_.len_ > 0 && source.datum_.ptr_ == nullptr) { return OB_INVALID_ARGUMENT; }
  SqlType scalar;
  const SqlType *type = nullptr;
  const ObRecordType *declaration = nullptr;
  Datum imported;
  int ret = resolve_node(requested, source, resolver, scalar, type, declaration);
  if (ret != OB_SUCCESS) {}
  else if (source.is_null()) {
    if (type->legacy_meta().is_pl_extend_type()) {
      ObPLRecord *unused = nullptr;
      ret = record_declaration(*type, source, declaration, unused);
    }
  } else if (type->kind() != source.get_type()
      || (type->kind() == ObExtendType && type->legacy_meta().get_extend_type() != source.get_extend_type())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(DatumCopyPlan(*type).import_legacy(source.datum_, imported))) {}
  else if (source.is_ext_sql_array()) {
    const auto *array = source.get_array<DatumParameterArray>();
    if (type->form() != SqlTypeForm::Array || array == nullptr || array->count_ < 0
        || (array->count_ > 0 && array->data_ == nullptr)
        || (type->array_limit() >= 0 && array->count_ > type->array_limit())) { ret = OB_INVALID_ARGUMENT; }
    else {
      for (int64_t i = 0; ret == OB_SUCCESS && i < array->count_; ++i) {
        ret = check_typed_graph(type->element(), array->data_[i], resolver, depth + 1);
      }
    }
  } else if (source.get_type_meta().is_pl_extend_type()) {
    ObPLRecord *record = nullptr;
    ret = record_declaration(*type, source, declaration, record);
    // A declared but uninitialized record has no source data to inspect. Its
    // result gets NULL fields and metadata from the complete declaration.
    for (int64_t i = 0; ret == OB_SUCCESS && record->is_inited() && i < type->member_count(); ++i) {
      DatumParameter field;
      field.assign_value(record->get_element()[i]);
      field.set_accuracy(record->get_element_type()[i].get_accuracy());
      ret = check_typed_graph(type->member(i).type, field, resolver, depth + 1);
    }
  }
  return ret;
}

int release_graph(DatumParameter &value, ObIAllocator &allocator, ObSQLSessionInfo *session, int depth);
int clone_graph(const SqlType *requested, const DatumParameter &source, DatumParameter &output,
    ObIAllocator &allocator, const INativePlTypeResolver *resolver,
    ObSQLSessionInfo *session, bool need_new_allocator, int depth);

int release_array(DatumParameterArray *array, ObIAllocator &allocator,
                  ObSQLSessionInfo *session, int depth)
{
  int first_error = OB_SUCCESS;
  if (array == nullptr || array->count_ < 0 || (array->count_ > 0 && array->data_ == nullptr)) {
    first_error = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; i < array->count_; ++i) {
      const int ret = release_graph(array->data_[i], allocator, session, depth + 1);
      if (first_error == OB_SUCCESS && ret != OB_SUCCESS) { first_error = ret; }
    }
    if (first_error == OB_SUCCESS) {
      for (int64_t i = 0; i < array->count_; ++i) { array->data_[i].~DatumParameter(); }
      array->~DatumParameterArray();
      allocator.free(array);
    }
  }
  return first_error;
}

int release_record(ObPLRecord *record, ObIAllocator &allocator,
                   ObSQLSessionInfo *session, int depth)
{
  int first_error = OB_SUCCESS;
  if (record == nullptr || record->get_type() != PL_RECORD_TYPE || record->get_count() < 0) {
    first_error = OB_INVALID_ARGUMENT;
  } else if (record->get_allocator() == nullptr) {
    // init_data() failed before publishing its child allocator/data domain.
    if (record->is_inited()) { first_error = OB_ERR_UNEXPECTED; }
    else { record->~ObPLRecord(); allocator.free(record); }
  } else {
    auto *children = dynamic_cast<ObPLAllocator1 *>(record->get_allocator());
    if (children == nullptr || children->get_parent_allocator() != &allocator) {
      first_error = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; i < record->get_count(); ++i) {
        DatumValue *field = nullptr;
        int ret = record->get_element(i, field);
        if (ret == OB_SUCCESS) {
          DatumParameter parameter; parameter.assign_value(*field);
          ret = release_graph(parameter, *children, session, depth + 1);
          // Keep failed resources reachable; only successfully released fields
          // become NULL. Continue through all fields before returning an error.
          if (ret == OB_SUCCESS) { field->set_null(); }
        }
        if (first_error == OB_SUCCESS && ret != OB_SUCCESS) { first_error = ret; }
      }
      if (first_error == OB_SUCCESS) {
        for (int64_t i = 0; i < record->get_count(); ++i) { record->get_element()[i].~DatumValue(); }
        children->free(record->get_element());
        children->~ObPLAllocator1();
        allocator.free(children);
        record->set_allocator(nullptr);
        record->set_data(nullptr);
        record->~ObPLRecord();
        allocator.free(record);
      }
    }
  }
  return first_error;
}

int release_graph(DatumParameter &value, ObIAllocator &allocator, ObSQLSessionInfo *session, int depth)
{
  int ret = OB_SUCCESS;
  if (depth > MAX_GRAPH_DEPTH) { ret = OB_SIZE_OVERFLOW; }
  else if (value.is_null()) { value.reset(); }
  else {
    if (value.is_ext_sql_array()) {
      ret = release_array(value.get_array<DatumParameterArray>(), allocator, session, depth);
    } else if (value.is_pl_extend()) {
      if (value.get_extend_type() == PL_RECORD_TYPE) {
        ret = release_record(reinterpret_cast<ObPLRecord *>(value.get_ext()), allocator, session, depth);
      } else {
        // Do not close an unregistered copied cursor or discard its handle.
        ret = OB_NOT_SUPPORTED;
      }
    }
    if (ret == OB_SUCCESS) {
      allocator.free(const_cast<char *>(value.datum_.ptr_));
      value.reset();
    }
  }
  return ret;
}

int clone_array(const SqlType &type, const DatumParameter &source, DatumParameter &output,
    ObIAllocator &allocator, const INativePlTypeResolver *resolver,
    ObSQLSessionInfo *session, bool need_new_allocator, int depth)
{
  const auto *array = source.get_array<DatumParameterArray>();
  auto *copy = DatumParameterArray::alloc(allocator, array->count_);
  int ret = copy == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
  if (ret == OB_SUCCESS) {
    copy->element_ = array->element_;
    copy->source_value_size_ = array->source_value_size_;
    for (int64_t i = 0; ret == OB_SUCCESS && i < array->count_; ++i) {
      ret = clone_graph(type.element(), array->data_[i], copy->data_[i], allocator,
                        resolver, session, need_new_allocator, depth + 1);
    }
    DatumParameter candidate = source;
    candidate.datum_.ptr_ = nullptr; candidate.datum_.set_null();
    if (ret == OB_SUCCESS) {
      ret = candidate.set_extend(reinterpret_cast<int64_t>(copy), T_EXT_SQL_ARRAY, allocator);
    }
    if (ret == OB_SUCCESS) { output = candidate; }
    else {
      const int cleanup_ret = release_array(copy, allocator, session, depth);
      if (cleanup_ret != OB_SUCCESS) { LOG_ERROR("failed to rollback SQL array clone", K(cleanup_ret)); }
    }
  }
  return ret;
}

int clone_record(const SqlType &type, const ObRecordType &declaration,
    const DatumParameter &source, DatumParameter &output, ObIAllocator &allocator,
    const INativePlTypeResolver *resolver, ObSQLSessionInfo *session,
    bool need_new_allocator, int depth)
{
  const auto *record = reinterpret_cast<const ObPLRecord *>(source.get_ext());
  const int64_t count = declaration.get_record_member_count();
  const int64_t init_size = ObRecordType::get_init_size(count);
  DatumParameter candidate = source;
  candidate.datum_.ptr_ = nullptr; candidate.datum_.set_null();
  int ret = candidate.set_extend(0, PL_RECORD_TYPE, allocator, init_size);
  ObPLRecord *copy = nullptr;
  if (ret == OB_SUCCESS) {
    void *memory = allocator.alloc(init_size);
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      copy = new (memory) ObPLRecord(declaration.get_user_type_id(), count);
      candidate.datum_.set_extend(reinterpret_cast<int64_t>(copy), PL_RECORD_TYPE, init_size);
      ret = copy->init_data(allocator, need_new_allocator);
    }
  }
  if (ret == OB_SUCCESS) {
    copy->set_is_null(record->is_null());
    for (int64_t i = 0; ret == OB_SUCCESS && i < count; ++i) {
      const ObPLDataType &member = *declaration.get_record_member_type(i);
      copy->get_not_null()[i] = member.is_not_null();
      copy->get_element_type()[i] = type.member(i).type->attributes();
      if (record->is_inited()) {
        DatumParameter field, owned;
        field.assign_value(const_cast<ObPLRecord *>(record)->get_element()[i]);
        field.set_accuracy(const_cast<ObPLRecord *>(record)->get_element_type()[i].get_accuracy());
        ret = clone_graph(type.member(i).type, field, owned, *copy->get_allocator(),
                          resolver, session, false, depth + 1);
        if (ret == OB_SUCCESS) { copy->get_element()[i] = owned.value_view(); }
      }
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  else {
    if (copy != nullptr) {
      const int cleanup_ret = release_record(copy, allocator, session, depth);
      if (cleanup_ret != OB_SUCCESS) { LOG_ERROR("failed to rollback PL record clone", K(cleanup_ret)); }
    }
    if (candidate.datum_.ptr_ != nullptr) { allocator.free(const_cast<char *>(candidate.datum_.ptr_)); }
  }
  return ret;
}

int clone_graph(const SqlType *requested, const DatumParameter &source, DatumParameter &output,
    ObIAllocator &allocator, const INativePlTypeResolver *resolver,
    ObSQLSessionInfo *session, bool need_new_allocator, int depth)
{
  if (depth > MAX_GRAPH_DEPTH) { return OB_SIZE_OVERFLOW; }
  if (source.is_null()) {
    DatumParameter candidate = source;
    candidate.datum_.ptr_ = nullptr; candidate.datum_.set_null();
    output = candidate;
    return OB_SUCCESS;
  }
  SqlType scalar;
  const SqlType *type = nullptr;
  const ObRecordType *declaration = nullptr;
  int ret = resolve_node(requested, source, resolver, scalar, type, declaration);
  if (ret != OB_SUCCESS) {}
  else if (source.is_ext_sql_array()) {
    ret = clone_array(*type, source, output, allocator, resolver, session, need_new_allocator, depth);
  } else if (source.get_type_meta().is_pl_extend_type()) {
    ObPLRecord *record = nullptr;
    if (OB_FAIL(record_declaration(*type, source, declaration, record))) {}
    else {
      ret = clone_record(*type, *declaration, source, output, allocator,
                          resolver, session, need_new_allocator, depth);
    }
  } else { ret = output.deep_copy(source, allocator); }
  return ret;
}
} // namespace

int NativePlDeclarationResolver::resolve(const SqlType *requested,
    const DatumParameter &source, const SqlType *&resolved,
    const ObRecordType *&record_declaration) const
{
  int ret = OB_SUCCESS;
  const SqlTypeMeta &meta = requested != nullptr ? requested->legacy_meta()
      : source.is_null() && source.get_type_meta().is_null()
          ? source.get_param_meta() : source.get_type_meta();
  const ObAccuracy &accuracy = requested != nullptr ? requested->accuracy() : source.get_accuracy();
  const SqlType *candidate = nullptr;
  const ObRecordType *candidate_declaration = nullptr;
  if (meta.is_pl_extend_type() && meta.get_extend_type() != PL_RECORD_TYPE) {
    // In particular, this does not pretend that Cursor retaining is complete.
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(ObExprUtil::resolve_sql_type(context_, meta, accuracy,
                         meta.get_subschema_id(), metadata_owner_, candidate, &declarations_))) {
  } else if (candidate == nullptr
      || (requested != nullptr && !same_full_declaration(*requested, *candidate))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (meta.is_pl_extend_type()) {
    const ObUserDefinedType *definition = nullptr;
    const uint64_t id = candidate->pl_declaration().declaration_id;
    if (OB_FAIL(declarations_.get_user_type(id, definition))) {
    } else if (definition == nullptr || !definition->is_record_type()
        || definition->get_user_type_id() != id) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      candidate_declaration = static_cast<const ObRecordType *>(definition);
    }
  }
  if (OB_SUCC(ret)) {
    resolved = candidate;
    record_declaration = candidate_declaration;
  }
  return ret;
}

int clone_owned_parameter_graph(const SqlType &type, const DatumParameter &source,
    DatumParameter &output, ObIAllocator &allocator, const INativePlTypeResolver *resolver,
    ObSQLSessionInfo *session, bool need_new_allocator)
{
  const int ret = check_typed_graph(&type, source, resolver, 0);
  return ret == OB_SUCCESS
      ? clone_graph(&type, source, output, allocator, resolver, session, need_new_allocator, 0) : ret;
}

int release_owned_parameter_graph(DatumParameter &value, ObIAllocator &allocator,
                                  ObSQLSessionInfo *session)
{ return release_graph(value, allocator, session, 0); }

NativeSqlValueOwner::~NativeSqlValueOwner()
{
  const int ret = clear();
  if (ret != OB_SUCCESS) { LOG_ERROR("native SQL value owner cleanup failed before arena reuse", K(ret)); }
}

int NativeSqlValueOwner::clear()
{
  int first_error = OB_SUCCESS;
  Resource **cursor = &resources_;
  while (*cursor != nullptr) {
    Resource *resource = *cursor;
    const int ret = release_owned_parameter_graph(resource->value_, allocator_, session_);
    if (ret == OB_SUCCESS) {
      *cursor = resource->next_;
      resource->~Resource();
      allocator_.free(resource);
      --count_;
    } else {
      if (first_error == OB_SUCCESS) { first_error = ret; }
      cursor = &resource->next_;
    }
  }
  return first_error;
}

int clone_native_owned_value(const SqlType &type, const Datum &source,
                             NativeSqlValueOwner &owner, Datum &output)
{
  const DatumCopyPlan plan(type);
  int ret = OB_SUCCESS;
  if (source.is_null() && type.requires_resource_clone()) {
    // No resource/token is needed, but an actual Record declaration must still
    // agree with the full caller Type. NULL carries no payload from which an
    // origin or member declaration could be recovered.
    DatumParameter parameter;
    parameter.set_type_meta(type.legacy_meta());
    parameter.set_accuracy(type.accuracy());
    parameter.datum_.set_null();
    ret = check_typed_graph(&type, parameter, owner.resolver_, 0);
    if (OB_SUCC(ret)) { ret = plan.deep_copy(source, output, owner.allocator_); }
  } else if (!source.is_present() || !type.requires_resource_clone()) {
    ret = plan.deep_copy(source, output, owner.allocator_);
  } else {
    alignas(8) char scratch[64];
    ObDatum legacy;
    DatumParameter parameter;
    if (OB_FAIL(plan.legacy_view(source, legacy, scratch, sizeof(scratch)))) {}
    else {
      parameter.set_type_meta(type.legacy_meta());
      parameter.set_accuracy(type.accuracy());
      parameter.set_datum(legacy);
      if (OB_FAIL(check_typed_graph(&type, parameter, owner.resolver_, 0))) {}
      else {
        void *memory = owner.allocator_.alloc(sizeof(NativeSqlValueOwner::Resource));
        if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          auto *resource = new (memory) NativeSqlValueOwner::Resource;
          Datum candidate;
          ret = clone_owned_parameter_graph(type, parameter, resource->value_, owner.allocator_,
                                             owner.resolver_, owner.session_);
          if (ret == OB_SUCCESS) { ret = plan.import_legacy(resource->value_.datum_, candidate); }
          if (ret == OB_SUCCESS) {
            owner.keep(resource);
            output = candidate;
          } else {
            const int cleanup_ret = release_owned_parameter_graph(resource->value_, owner.allocator_, owner.session_);
            if (cleanup_ret != OB_SUCCESS) {
              // No output was published, but a failed cleanup must remain
              // reachable so clear() can retry before the arena is reused.
              owner.keep(resource);
              LOG_ERROR("failed to rollback native PL clone", K(cleanup_ret), K(ret));
            } else { resource->~Resource(); owner.allocator_.free(resource); }
          }
        }
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
