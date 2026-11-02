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

#include "sql/engine/expr/ob_native_pl_value.h"
#include "query/engine/expr/ob_expr.h"
#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter_array.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_user_type.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <unordered_set>
#include <vector>
#include <string>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;

namespace
{
class FailingAllocator final : public ObIAllocator
{
public:
  explicit FailingAllocator(int64_t failure = -1) : failure_(failure) {}
  ~FailingAllocator() { for (void *pointer : live_) { std::free(pointer); } }
  void *alloc(int64_t size) override
  {
    if (size == DATUM_EXTEND_PAYLOAD_SIZE) { ++carrier_calls_; }
    if (++calls_ == failure_) { return nullptr; }
    void *pointer = std::malloc(size > 0 ? size : 1);
    if (pointer != nullptr) { live_.insert(pointer); }
    return pointer;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override
  {
    if (pointer != nullptr) {
      const auto found = live_.find(pointer);
      EXPECT_NE(live_.end(), found); // Never free a borrowed/source descriptor.
      if (found != live_.end()) { live_.erase(found); std::free(pointer); }
    }
  }
  int64_t failure_;
  int64_t calls_ = 0;
  int64_t carrier_calls_ = 0;
  std::unordered_set<void *> live_;
};

DatumParameter record(ObIAllocator &owner, uint64_t id,
                      const std::vector<DatumParameter> &fields)
{
  DatumParameter result;
  void *memory = owner.alloc(ObRecordType::get_init_size(fields.size()));
  EXPECT_NE(nullptr, memory);
  if (memory != nullptr) {
    auto *value = new (memory) ObPLRecord(id, fields.size());
    EXPECT_EQ(OB_SUCCESS, value->init_data(owner, false));
    for (size_t i = 0; i < fields.size(); ++i) {
      value->get_element()[i] = fields[i].value_view();
      value->get_element_type()[i].set_meta_type(fields[i].get_type_meta());
      value->get_element_type()[i].set_accuracy(fields[i].get_accuracy());
      value->get_not_null()[i] = false; // Declared nullable, independent of the current value.
    }
    EXPECT_EQ(OB_SUCCESS, result.set_extend(reinterpret_cast<int64_t>(value),
        PL_RECORD_TYPE, owner, value->get_init_size()));
    result.set_udt_id(id);
  }
  return result;
}

DatumParameter array(ObIAllocator &owner, const std::vector<DatumParameter> &values)
{
  DatumParameter result;
  auto *value = DatumParameterArray::alloc(owner, values.size());
  EXPECT_NE(nullptr, value);
  if (value != nullptr) {
    for (size_t i = 0; i < values.size(); ++i) { value->data_[i] = values[i]; }
    if (!values.empty()) {
      value->element_.set_meta_type(values[0].get_type_meta());
      value->element_.set_accuracy(values[0].get_accuracy());
    }
    value->source_value_size_ = 177;
    EXPECT_EQ(OB_SUCCESS, result.set_extend(reinterpret_cast<int64_t>(value), T_EXT_SQL_ARRAY, owner));
  }
  return result;
}

ObPLRecord *as_record(const DatumParameter &value)
{ return reinterpret_cast<ObPLRecord *>(value.get_ext()); }

DatumParameter field(const DatumParameter &value, int64_t index)
{
  DatumParameter result;
  result.assign_value(as_record(value)->get_element()[index]);
  result.set_accuracy(as_record(value)->get_element_type()[index].get_accuracy());
  return result;
}

class FixtureDeclarations final : public INativePlTypeResolver, public ObPLINS
{
public:
  explicit FixtureDeclarations(ObIAllocator &owner) : owner_(owner), execution_(owner) {}
  ~FixtureDeclarations() { for (const Entry &entry : entries_) { entry.definition->~ObRecordType(); } }
  int add(const SqlType &type)
  {
    void *memory = owner_.alloc(sizeof(ObRecordType));
    if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    auto *definition = new (memory) ObRecordType();
    definition->set_user_type_id(type.pl_declaration().declaration_id);
    int ret = definition->set_sql_declaration_identity(type.pl_declaration(), type.nominal());
    if (ret == OB_SUCCESS) { ret = definition->record_members_init(&owner_, type.member_count()); }
    for (int64_t i = 0; ret == OB_SUCCESS && i < type.member_count(); ++i) {
      const SqlTypeMember &member = type.member(i);
      ObPLDataType pl_member(member.type->kind());
      if (member.type->legacy_meta().is_pl_extend_type()) {
        pl_member.set_user_type_id(static_cast<ObPLType>(member.type->legacy_meta().get_extend_type()),
                                   member.type->pl_declaration().declaration_id);
      } else { pl_member.set_data_type(member.type->attributes()); }
      pl_member.set_not_null(!member.nullable);
      // The actual declaration supports NOT NULL members only with a default.
      ret = definition->add_record_member(member.name, pl_member, member.nullable ? OB_INVALID_INDEX : 0);
    }
    if (ret == OB_SUCCESS) { entries_.push_back({&type, definition}); }
    else { definition->~ObRecordType(); owner_.free(definition); }
    return ret;
  }
  int resolve(const SqlType *requested, const DatumParameter &source,
              const SqlType *&resolved, const ObRecordType *&declaration) const override
  {
    ++calls_;
    NativePlDeclarationResolver resolver(execution_, *this, owner_);
    return resolver.resolve(requested, source, resolved, declaration);
  }
  int get_user_type(uint64_t id, const ObUserDefinedType *&definition,
                    ObIAllocator *allocator = nullptr) const override
  {
    UNUSED(allocator);
    // Actual namespace lookup, as consumed by the production resolver. These
    // synthetic declarations exercise retaining, not real producer provenance.
    for (const Entry &entry : entries_) {
      if (entry.definition->get_user_type_id() == id) {
        definition = entry.definition;
        return OB_SUCCESS;
      }
    }
    return OB_NOT_SUPPORTED;
  }
  struct Entry { const SqlType *type; ObRecordType *definition; };
  ObIAllocator &owner_;
  mutable ObExecContext execution_;
  std::vector<Entry> entries_;
  mutable int64_t calls_ = 0;
};

const SqlType *record_type(ObIAllocator &owner, FixtureDeclarations &registry, uint64_t id,
                          const std::vector<const SqlType *> &fields,
                          const SqlNominalType &nominal = SqlNominalType(), bool register_definition = true,
                          uint64_t local_scope = 0x7a001)
{
  std::vector<SqlTypeMember> members(fields.size());
  std::vector<std::string> names;
  for (size_t i = 0; i < fields.size(); ++i) { names.push_back("f" + std::to_string(i)); }
  for (size_t i = 0; i < fields.size(); ++i) {
    members[i].type = fields[i]; members[i].name = ObString(names[i].size(), names[i].data());
  }
  SqlTypeSpec declaration;
  declaration.attributes.meta_.set_ext(); declaration.attributes.meta_.set_extend_type(PL_RECORD_TYPE);
  declaration.attributes.set_udt_id(id);
  declaration.form = SqlTypeForm::Record; declaration.nominal = nominal;
  declaration.pl_declaration.source = nominal.specified
      ? SqlPLDeclarationSource::PackageSpec : SqlPLDeclarationSource::Local;
  declaration.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  declaration.pl_declaration.declaration_id = id;
  if (!nominal.specified) { declaration.pl_declaration.compilation_scope.low = local_scope; }
  declaration.members = members.data(); declaration.member_count = members.size();
  const SqlType *result = nullptr;
  EXPECT_EQ(OB_SUCCESS, SqlType::create(declaration, owner, result));
  if (result != nullptr && register_definition) { EXPECT_EQ(OB_SUCCESS, registry.add(*result)); }
  return result;
}

const SqlType *array_type(ObIAllocator &owner, const SqlType &element)
{
  SqlTypeSpec declaration;
  declaration.attributes.meta_.set_ext(); declaration.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  declaration.form = SqlTypeForm::Array; declaration.element = &element;
  const SqlType *result = nullptr;
  EXPECT_EQ(OB_SUCCESS, SqlType::create(declaration, owner, result));
  return result;
}

const SqlType *heterogeneous_array_type(ObIAllocator &owner)
{
  SqlTypeSpec declaration;
  declaration.attributes.meta_.set_ext(); declaration.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  declaration.form = SqlTypeForm::Array; declaration.heterogeneous = true;
  const SqlType *result = nullptr;
  EXPECT_EQ(OB_SUCCESS, SqlType::create(declaration, owner, result));
  return result;
}

int import_parameter(const SqlType &type, const DatumParameter &parameter, Datum &value)
{
  alignas(8) char scratch[64];
  ObDatum physical;
  int ret = parameter.export_legacy(physical, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS) { ret = DatumCopyPlan(type).import_legacy(physical, value); }
  if (ret == OB_SUCCESS && value.is_present() && parameter.datum_.is_inline_extended()) {
    value = parameter.datum_; // The fixture must not return its local legacy scratch.
  }
  return ret;
}

DatumParameter view(const SqlType &type, const Datum &value, char (&scratch)[64])
{
  DatumParameter result;
  ObDatum legacy;
  EXPECT_EQ(OB_SUCCESS, DatumCopyPlan(type).legacy_view(value, legacy, scratch, sizeof(scratch)));
  if (!legacy.is_null() && type.legacy_meta().is_ext_sql_array() && legacy.has_ext_value()) {
    const int64_t address = legacy.get_ext();
    MEMCPY(scratch, &address, sizeof(address));
    legacy.set_string(scratch, sizeof(address));
  }
  EXPECT_EQ(OB_SUCCESS, result.assign_legacy_borrowed(legacy, type.legacy_meta()));
  result.set_accuracy(type.accuracy()); result.set_param_meta(type.legacy_meta());
  return result;
}

TEST(NativePlValue, primitive_values_remain_inline_without_cleanup_token_or_allocation)
{
  FailingAllocator allocator;
  NativeSqlValueOwner owner(allocator);
  SqlTypeMeta meta; meta.set_int();
  SqlType type; ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), type));
  Datum source, output; source.set_int(INT64_MIN); output.set_int(71);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(type, source, owner, output));
  EXPECT_EQ(INT64_MIN, output.get_int());
  source.set_null();
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(type, source, owner, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(0, owner.resource_count()); EXPECT_EQ(0, allocator.calls_);
  EXPECT_EQ(OB_SUCCESS, owner.clear());
}

TEST(NativePlValue, token_allocation_failure_does_not_publish_or_clone_resources)
{
  ObArenaAllocator source_owner, type_owner;
  FixtureDeclarations registry(type_owner);
  SqlType string_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, string_type));
  const SqlType *type = record_type(type_owner, registry, 61, {&string_type});
  DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("resource leaf"), source_owner));
  text.set_type_meta(meta); text.set_accuracy(accuracy);
  DatumParameter source_record = record(source_owner, 61, {text});
  Datum source, output; output.set_int(91);
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, source_record, source));
  FailingAllocator allocator(1);
  NativeSqlValueOwner owner(allocator, nullptr, &registry);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, clone_native_owned_value(*type, source, owner, output));
  EXPECT_EQ(91, output.get_int()); EXPECT_EQ(0, owner.resource_count());
  EXPECT_EQ(1, allocator.calls_); EXPECT_TRUE(allocator.live_.empty());
}

TEST(NativePlValue, full_type_mismatch_is_rejected_before_token_or_payload_allocation)
{
  ObArenaAllocator source_owner, type_owner;
  FixtureDeclarations registry(type_owner);
  SqlType string_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, string_type));
  const SqlType *type = record_type(type_owner, registry, 63, {&string_type});
  DatumParameter integer; ASSERT_EQ(OB_SUCCESS, integer.set_int(77, source_owner));
  DatumParameter source_record = record(source_owner, 63, {integer});
  Datum source, output; output.set_int(93);
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, source_record, source));
  FailingAllocator allocator;
  NativeSqlValueOwner owner(allocator, nullptr, &registry);
  EXPECT_EQ(OB_INVALID_ARGUMENT, clone_native_owned_value(*type, source, owner, output));
  EXPECT_EQ(93, output.get_int()); EXPECT_EQ(0, owner.resource_count()); EXPECT_EQ(0, allocator.calls_);
}

TEST(NativePlValue, record_array_record_graph_keeps_metadata_and_payload_after_source_reset)
{
  ObArenaAllocator source_owner, type_owner, target_owner;
  FixtureDeclarations registry(type_owner);
  SqlType integer_type, string_type;
  SqlTypeMeta int_meta; int_meta.set_int();
  SqlTypeMeta text_meta; text_meta.set_varchar(); text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(int_meta, ObAccuracy(20, 0), integer_type));
  ObAccuracy text_accuracy; text_accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(text_meta, text_accuracy, string_type));
  const SqlType *inner_type = record_type(type_owner, registry, 101, {&string_type, &integer_type});
  // A PL T_EXT_SQL_ARRAY field declares a heterogeneous SQL array. The leaf
  // Record's complete origin comes from the explicit actual namespace.
  const SqlType *records_type = heterogeneous_array_type(type_owner);
  const SqlType *outer_type = record_type(type_owner, registry, 102, {records_type, &string_type});
  ASSERT_NE(nullptr, outer_type);
  DatumParameter text, integer;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("independent PL field bytes"), source_owner));
  text.set_type_meta(text_meta); text.set_accuracy(text_accuracy);
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MIN, source_owner));
  DatumParameter inner = record(source_owner, 101, {text, integer});
  DatumParameter rows = array(source_owner, {inner});
  rows.set_result_flags(0x132); rows.set_raw_text_info(3, 4);
  rows.get_param_flag().need_to_check_type_ = true;
  DatumParameter source_record = record(source_owner, 102, {rows, text});
  Datum source, output;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*outer_type, source_record, source));
  NativeSqlValueOwner owner(target_owner, nullptr, &registry);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*outer_type, source, owner, output));
  EXPECT_EQ(1, owner.resource_count());
  alignas(8) char scratch[64];
  const DatumParameter result = view(*outer_type, output, scratch);
  DatumParameter cloned_rows = field(result, 0);
  const auto *cloned_array = cloned_rows.get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, cloned_array); ASSERT_EQ(1, cloned_array->count_);
  EXPECT_NE(as_record(source_record), as_record(result));
  EXPECT_NE(rows.get_array<DatumParameterArray>(), cloned_array);
  EXPECT_NE(as_record(inner), as_record(cloned_array->data_[0]));
  EXPECT_NE(text.get_datum().get_reference(), field(cloned_array->data_[0], 0).get_datum().get_reference());
  EXPECT_NE(text.get_datum().get_reference(), field(result, 1).get_datum().get_reference());
  MEMSET(const_cast<void *>(text.get_datum().get_reference()), 'x', text.get_datum().length());
  // The actual source Record field is now inline; poison that field rather
  // than interpreting inactive scalar union bits as an old payload address.
  ASSERT_EQ(OB_SUCCESS, as_record(inner)->get_element()[1].set_int(0, source_owner));
  EXPECT_EQ(0, field(inner, 1).get_int());
  source_owner.reset();
  EXPECT_EQ(102, as_record(result)->get_id());
  EXPECT_EQ(177, cloned_array->source_value_size_);
  EXPECT_EQ(101, cloned_array->element_.get_accuracy().get_accuracy());
  EXPECT_EQ(101, as_record(cloned_array->data_[0])->get_id());
  EXPECT_EQ(ObString::make_string("independent PL field bytes"), field(cloned_array->data_[0], 0).get_string());
  EXPECT_EQ(INT64_MIN, field(cloned_array->data_[0], 1).get_int());
  EXPECT_EQ(ObString::make_string("independent PL field bytes"), field(result, 1).get_string());
  EXPECT_FALSE(as_record(result)->get_not_null()[0]);
  EXPECT_EQ(OB_SUCCESS, owner.clear()); EXPECT_EQ(0, owner.resource_count());
  EXPECT_EQ(OB_SUCCESS, owner.clear());
}

TEST(NativePlValue, standalone_array_clone_preserves_parameter_flags_and_rolls_back_allocation_failures)
{
  ObArenaAllocator source_owner, type_owner;
  FixtureDeclarations registry(type_owner);
  SqlType string_type; SqlTypeMeta string_meta; string_meta.set_varchar();
  string_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(string_meta, ObAccuracy(), string_type));
  const SqlType *inner_type = record_type(type_owner, registry, 501, {&string_type});
  const SqlType *type = array_type(type_owner, *inner_type);
  DatumParameter text; ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("retained nested field"), source_owner));
  text.set_type_meta(string_meta);
  DatumParameter inner = record(source_owner, 501, {text});
  DatumParameter source = array(source_owner, {inner});
  source.set_result_flags(0x132); source.set_raw_text_info(7, 9);
  source.get_param_flag().need_to_check_type_ = true;
  DatumParameter sentinel; ASSERT_EQ(OB_SUCCESS, sentinel.set_int(999, source_owner));
  bool succeeded = false;
  for (int64_t failure = 1; failure < 40 && !succeeded; ++failure) {
    FailingAllocator allocator(failure);
    DatumParameter output = sentinel;
    const int ret = clone_owned_parameter_graph(*type, source, output, allocator, &registry, nullptr, false);
    if (ret == OB_SUCCESS) {
      succeeded = true;
      EXPECT_EQ(source.get_result_flag(), output.get_result_flag());
      EXPECT_EQ(7, output.get_raw_text_pos()); EXPECT_EQ(9, output.get_raw_text_len());
      EXPECT_TRUE(output.need_to_check_type());
      EXPECT_EQ(OB_SUCCESS, release_owned_parameter_graph(output, allocator));
      EXPECT_TRUE(output.is_null());
    } else {
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret);
      EXPECT_EQ(999, output.get_int()); EXPECT_EQ(ObIntType, output.get_type());
    }
    EXPECT_TRUE(allocator.live_.empty()) << "failure allocation " << failure;
    EXPECT_EQ(ObString::make_string("retained nested field"), field(inner, 0).get_string());
  }
  EXPECT_TRUE(succeeded);
}

TEST(NativePlValue, retaining_rejects_cursor_graph_without_publishing_or_touching_session_state)
{
  ObArenaAllocator source_owner;
  ObPLCursorInfo cursor;
  DatumParameter handle;
  ASSERT_EQ(OB_SUCCESS, handle.set_extend(reinterpret_cast<int64_t>(&cursor), PL_CURSOR_TYPE, source_owner));
  DatumParameter source = array(source_owner, {handle});
  ObArenaAllocator type_owner;
  SqlTypeSpec declaration;
  declaration.attributes.meta_.set_ext(); declaration.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  declaration.form = SqlTypeForm::Array; declaration.heterogeneous = true;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(declaration, type_owner, type));
  DatumParameter output; ASSERT_EQ(OB_SUCCESS, output.set_int(913, source_owner));
  FailingAllocator allocator;
  EXPECT_EQ(OB_NOT_SUPPORTED, clone_owned_parameter_graph(*type, source, output, allocator));
  EXPECT_EQ(913, output.get_int()); EXPECT_EQ(0, allocator.calls_);
  EXPECT_FALSE(cursor.isopen());
  // This is a refusal test, not evidence that a closed/open/server cursor has
  // acquired a new registration, retain count or independently owned handle.
}

TEST(NativePlValue, clear_keeps_failed_resource_reachable_but_visits_other_tokens)
{
  ObArenaAllocator source_owner, type_owner, target_owner;
  FixtureDeclarations registry(type_owner);
  SqlType string_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, string_type));
  const SqlType *type = record_type(type_owner, registry, 901, {&string_type, &string_type});
  DatumParameter text, null;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("owned field"), source_owner));
  text.set_type_meta(meta); text.set_accuracy(accuracy);
  DatumParameter source_record = record(source_owner, 901, {text, null});
  Datum source, first, second;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, source_record, source));
  NativeSqlValueOwner owner(target_owner, nullptr, &registry);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, first));
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, second));
  alignas(8) char scratch[64];
  DatumParameter second_record = view(*type, second, scratch);
  ObPLCursorInfo cursor;
  DatumParameter unsupported;
  ASSERT_EQ(OB_SUCCESS, unsupported.set_extend(reinterpret_cast<int64_t>(&cursor), PL_CURSOR_TYPE, target_owner));
  // Simulate a mutable PL field being rebound after it was retained. Cleanup
  // must retain this unhandled handle rather than silently clearing it.
  as_record(second_record)->get_element()[1] = unsupported.value_view();
  EXPECT_EQ(OB_NOT_SUPPORTED, owner.clear());
  EXPECT_EQ(1, owner.resource_count());
  EXPECT_TRUE(as_record(second_record)->get_element()[0].is_null());
  EXPECT_FALSE(as_record(second_record)->get_element()[1].is_null());
  EXPECT_FALSE(cursor.isopen());
  as_record(second_record)->get_element()[1].set_null();
  EXPECT_EQ(OB_SUCCESS, owner.clear()); EXPECT_EQ(0, owner.resource_count());
}
TEST(NativePlValue, uninitialized_and_null_record_use_actual_declaration_without_reading_absent_data)
{
  ObArenaAllocator type_owner;
  FixtureDeclarations registry(type_owner);
  SqlType text_type, int_type;
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text_type));
  meta.set_int(); ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), int_type));
  const SqlType *type = record_type(type_owner, registry, 121, {&text_type, &int_type});
  for (bool is_null : {false, true}) {
    SCOPED_TRACE(is_null);
    ObArenaAllocator source_owner, target_owner;
    void *memory = source_owner.alloc(sizeof(ObPLRecord)); ASSERT_NE(nullptr, memory);
    auto *uninitialized = new (memory) ObPLRecord();
    uninitialized->set_id(121); uninitialized->set_is_null(is_null);
    ASSERT_EQ(OB_INVALID_COUNT, uninitialized->get_count());
    ASSERT_FALSE(uninitialized->is_inited());
    DatumParameter parameter;
    ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(uninitialized),
        PL_RECORD_TYPE, source_owner, sizeof(ObPLRecord)));
    parameter.set_udt_id(121);
    Datum source, output;
    ASSERT_EQ(OB_SUCCESS, import_parameter(*type, parameter, source));
    NativeSqlValueOwner owner(target_owner, nullptr, &registry);
    ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output));
    source_owner.reset();
    alignas(8) char scratch[64];
    const DatumParameter result = view(*type, output, scratch);
    ASSERT_TRUE(as_record(result)->is_inited());
    EXPECT_EQ(121, as_record(result)->get_id()); EXPECT_EQ(2, as_record(result)->get_count());
    EXPECT_EQ(is_null, as_record(result)->is_null());
    EXPECT_TRUE(field(result, 0).is_null()); EXPECT_TRUE(field(result, 1).is_null());
    EXPECT_EQ(ObVarcharType, as_record(result)->get_element_type()[0].get_obj_type());
    EXPECT_EQ(128, as_record(result)->get_element_type()[0].get_length());
    EXPECT_EQ(ObIntType, as_record(result)->get_element_type()[1].get_obj_type());
    EXPECT_EQ(20, as_record(result)->get_element_type()[1].get_precision());
    EXPECT_EQ(OB_SUCCESS, owner.clear()); EXPECT_EQ(0, owner.resource_count());
    // A native typed NULL requires no pointee or cleanup token.
    source.set_null();
    ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output));
    EXPECT_TRUE(output.is_null()); EXPECT_EQ(0, owner.resource_count());
  }
}

TEST(NativePlValue, heterogeneous_resource_leaf_requires_explicit_registry_type_before_publication)
{
  ObArenaAllocator source_owner, type_owner, target_owner;
  FixtureDeclarations registry(type_owner);
  SqlType text_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text_type));
  const SqlType *record_declaration = record_type(type_owner, registry, 131, {&text_type});
  ASSERT_NE(nullptr, record_declaration);
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  spec.form = SqlTypeForm::Array; spec.heterogeneous = true;
  const SqlType *type = nullptr; ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, type_owner, type));
  DatumParameter text, integer;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("explicit heterogeneous resource"), source_owner));
  text.set_type_meta(meta); text.set_accuracy(accuracy);
  ASSERT_EQ(OB_SUCCESS, integer.set_int(53, source_owner));
  DatumParameter known = array(source_owner, {record(source_owner, 131, {text}), integer});
  known.get_array<DatumParameterArray>()->data_[0].set_raw_text_info(4, 8);
  Datum source, output;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, known, source));
  NativeSqlValueOwner owner(target_owner, nullptr, &registry);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output));

  DatumParameter unknown = array(source_owner, {record(source_owner, 132, {text})});
  Datum unknown_source, sentinel; sentinel.set_int(91);
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, unknown, unknown_source));
  FailingAllocator rejected_allocator;
  NativeSqlValueOwner rejected(rejected_allocator, nullptr, &registry);
  EXPECT_EQ(OB_NOT_SUPPORTED, clone_native_owned_value(*type, unknown_source, rejected, sentinel));
  EXPECT_EQ(91, sentinel.get_int()); EXPECT_EQ(0, rejected.resource_count());
  EXPECT_EQ(0, rejected_allocator.calls_);
  unknown.get_array<DatumParameterArray>()->data_[0].datum_.set_null();
  EXPECT_EQ(OB_NOT_SUPPORTED, clone_native_owned_value(*type, unknown_source, rejected, sentinel));
  EXPECT_EQ(91, sentinel.get_int()); EXPECT_EQ(0, rejected_allocator.calls_);

  source_owner.reset();
  alignas(8) char scratch[64];
  const DatumParameter result = view(*type, output, scratch);
  const auto *values = result.get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, values); ASSERT_EQ(2, values->count_);
  EXPECT_EQ(ObString::make_string("explicit heterogeneous resource"), field(values->data_[0], 0).get_string());
  EXPECT_EQ(4, values->data_[0].get_raw_text_pos()); EXPECT_EQ(8, values->data_[0].get_raw_text_len());
  EXPECT_EQ(53, values->data_[1].get_int());
  EXPECT_EQ(OB_SUCCESS, owner.clear());
}

TEST(NativePlValue, schema_object_identity_is_distinct_from_record_declaration_and_must_match_resolver)
{
  ObArenaAllocator source_owner, type_owner, target_owner;
  FixtureDeclarations registry(type_owner);
  SqlType text_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text_type));
  SqlNominalType nominal;
  nominal.specified = true; nominal.tenant_id = OB_SERVER_RUNTIME_ID;
  nominal.object_id = 7001; nominal.schema_version = 99;
  const uint64_t declaration_id = combine_pl_type_id(nominal.object_id, 4);
  ASSERT_NE(nominal.object_id, declaration_id);
  const SqlType *type = record_type(type_owner, registry, declaration_id, {&text_type}, nominal);
  DatumParameter text; ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("schema-bound field"), source_owner));
  text.set_type_meta(meta); text.set_accuracy(accuracy);
  DatumParameter parameter = record(source_owner, declaration_id, {text});
  Datum source, output;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, parameter, source));
  NativeSqlValueOwner owner(target_owner, nullptr, &registry);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output));
  alignas(8) char scratch[64];
  EXPECT_EQ(declaration_id, as_record(view(*type, output, scratch))->get_id());

  nominal.schema_version = 100;
  const SqlType *wrong_version = record_type(type_owner, registry, declaration_id, {&text_type}, nominal, false);
  FailingAllocator rejected_allocator;
  NativeSqlValueOwner rejected(rejected_allocator, nullptr, &registry);
  Datum sentinel; sentinel.set_int(93);
  EXPECT_EQ(OB_INVALID_ARGUMENT, clone_native_owned_value(*wrong_version, source, rejected, sentinel));
  EXPECT_EQ(93, sentinel.get_int()); EXPECT_EQ(0, rejected_allocator.calls_);

  // Same payload ID does not allow a different target member nullability.
  SqlTypeMember member = type->member(0); member.nullable = false;
  SqlTypeSpec mismatch;
  mismatch.attributes = type->attributes(); mismatch.form = SqlTypeForm::Record;
  mismatch.pl_declaration = type->pl_declaration();
  mismatch.nominal = type->nominal(); mismatch.members = &member; mismatch.member_count = 1;
  const SqlType *wrong_nullable = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(mismatch, type_owner, wrong_nullable));
  EXPECT_EQ(OB_INVALID_ARGUMENT, clone_native_owned_value(*wrong_nullable, source, rejected, sentinel));
  EXPECT_EQ(93, sentinel.get_int()); EXPECT_EQ(0, rejected_allocator.calls_);
  EXPECT_EQ(OB_SUCCESS, owner.clear());
}

TEST(NativePlValue, same_local_id_in_another_scope_and_typed_null_require_the_actual_declaration)
{
  ObArenaAllocator type_owner, source_owner;
  FixtureDeclarations registry(type_owner);
  SqlType integer_type; SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer_type));
  const SqlType *actual = record_type(type_owner, registry, 707, {&integer_type},
      SqlNominalType(), true, 0xabc123);
  const SqlType *other_scope = record_type(type_owner, registry, 707, {&integer_type},
      SqlNominalType(), false, 0xabc124);
  ASSERT_NE(nullptr, actual); ASSERT_NE(nullptr, other_scope);
  ASSERT_FALSE(actual->equals(*other_scope));
  DatumParameter integer; ASSERT_EQ(OB_SUCCESS, integer.set_int(53, source_owner));
  DatumParameter parameter = record(source_owner, 707, {integer});
  Datum source, output; output.set_int(999);
  ASSERT_EQ(OB_SUCCESS, import_parameter(*actual, parameter, source));
  FailingAllocator allocator;
  NativeSqlValueOwner owner(allocator, nullptr, &registry);
  EXPECT_EQ(OB_INVALID_ARGUMENT, clone_native_owned_value(*other_scope, source, owner, output));
  EXPECT_EQ(999, output.get_int()); EXPECT_EQ(0, allocator.calls_);
  source.set_null();
  EXPECT_EQ(OB_INVALID_ARGUMENT, clone_native_owned_value(*other_scope, source, owner, output));
  EXPECT_EQ(999, output.get_int()); EXPECT_EQ(0, allocator.calls_);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*actual, source, owner, output));
  EXPECT_TRUE(output.is_null()); EXPECT_EQ(0, owner.resource_count()); EXPECT_EQ(0, allocator.calls_);
}

TEST(NativePlValue, unknown_typed_null_resolver_keeps_both_metadata_outputs_and_value_unpublished)
{
  ObArenaAllocator type_owner, source_owner;
  FixtureDeclarations registry(type_owner);
  SqlType integer_type; SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer_type));
  const SqlType *known = record_type(type_owner, registry, 808, {&integer_type});
  const SqlType *unknown = record_type(type_owner, registry, 809, {&integer_type}, SqlNominalType(), false);
  DatumParameter typed_null;
  typed_null.set_type_meta(unknown->legacy_meta()); typed_null.set_accuracy(unknown->accuracy());
  typed_null.datum_.set_null();
  const SqlType *resolved = known;
  const ObRecordType *declaration = registry.entries_[0].definition;
  EXPECT_EQ(OB_NOT_SUPPORTED, registry.resolve(nullptr, typed_null, resolved, declaration));
  EXPECT_EQ(known, resolved); EXPECT_EQ(registry.entries_[0].definition, declaration);
  FailingAllocator allocator;
  NativeSqlValueOwner owner(allocator, nullptr, &registry);
  Datum source, output; source.set_null(); output.set_int(901);
  EXPECT_EQ(OB_NOT_SUPPORTED, clone_native_owned_value(*unknown, source, owner, output));
  EXPECT_EQ(901, output.get_int()); EXPECT_EQ(0, owner.resource_count()); EXPECT_EQ(0, allocator.calls_);
}

TEST(NativePlValue, native_owner_rolls_back_each_parent_allocation_before_publication)
{
  ObArenaAllocator source_owner, type_owner;
  FixtureDeclarations registry(type_owner);
  SqlType text_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text_type));
  const SqlType *type = record_type(type_owner, registry, 811, {&text_type});
  DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("rollback real record fields"), source_owner));
  text.set_type_meta(meta); text.set_accuracy(accuracy);
  DatumParameter parameter = record(source_owner, 811, {text});
  Datum source; ASSERT_EQ(OB_SUCCESS, import_parameter(*type, parameter, source));
  bool succeeded = false;
  for (int64_t failure = 1; failure < 40 && !succeeded; ++failure) {
    FailingAllocator allocator(failure);
    NativeSqlValueOwner owner(allocator, nullptr, &registry);
    Datum output; output.set_int(313);
    const int ret = clone_native_owned_value(*type, source, owner, output);
    if (ret == OB_SUCCESS) {
      succeeded = true;
      EXPECT_EQ(1, owner.resource_count());
      alignas(8) char scratch[64];
      const DatumParameter result = view(*type, output, scratch);
      EXPECT_EQ(ObString::make_string("rollback real record fields"), field(result, 0).get_string());
    } else {
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret);
      EXPECT_EQ(313, output.get_int()); EXPECT_EQ(0, owner.resource_count());
    }
    EXPECT_EQ(OB_SUCCESS, owner.clear()); EXPECT_EQ(0, owner.resource_count());
    EXPECT_TRUE(allocator.live_.empty()) << "parent allocation " << failure;
  }
  EXPECT_TRUE(succeeded);
}

TEST(NativePlValue, typed_null_parameter_preserves_declaration_and_flags_without_allocating_a_graph)
{
  ObArenaAllocator source_owner, type_owner;
  FixtureDeclarations registry(type_owner);
  SqlType integer_type; SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer_type));
  const SqlType *type = record_type(type_owner, registry, 812, {&integer_type});
  DatumParameter source;
  source.set_type_meta(type->legacy_meta()); source.set_accuracy(type->accuracy());
  source.set_param_meta(type->legacy_meta()); source.datum_.set_null();
  source.set_result_flags(0x232); source.set_raw_text_info(17, 19);
  source.set_need_to_check_type(true);
  DatumParameter output; ASSERT_EQ(OB_SUCCESS, output.set_int(49, source_owner));
  FailingAllocator allocator;
  ASSERT_EQ(OB_SUCCESS, clone_owned_parameter_graph(*type, source, output, allocator, &registry));
  EXPECT_TRUE(output.is_null()); EXPECT_TRUE(output.get_type_meta().is_pl_extend_type());
  EXPECT_EQ(812, output.get_accuracy().get_accuracy());
  EXPECT_EQ(source.get_param_meta(), output.get_param_meta());
  EXPECT_EQ(source.get_result_flag(), output.get_result_flag());
  EXPECT_EQ(17, output.get_raw_text_pos()); EXPECT_EQ(19, output.get_raw_text_len());
  EXPECT_TRUE(output.need_to_check_type()); EXPECT_EQ(0, allocator.calls_);
  EXPECT_TRUE(output.get_datum().is_null());
  EXPECT_EQ(0u, output.get_datum().length());
  EXPECT_EQ(Datum::NoReferenceFlags, output.get_datum().reference_flags());
  EXPECT_EQ(OB_SUCCESS, release_owned_parameter_graph(output, allocator));
  EXPECT_TRUE(output.is_null()); EXPECT_TRUE(allocator.live_.empty());
  EXPECT_EQ(812, source.get_accuracy().get_accuracy());
}

TEST(NativePlValue, clear_releases_real_record_domains_before_reusing_the_outer_arena)
{
  ObArenaAllocator source_owner, type_owner, target_owner;
  FixtureDeclarations registry(type_owner);
  SqlType text_type; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text_type));
  const SqlType *type = record_type(type_owner, registry, 813, {&text_type});
  DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("repeat owner lifetime"), source_owner));
  text.set_type_meta(meta); text.set_accuracy(accuracy);
  DatumParameter parameter = record(source_owner, 813, {text});
  Datum source; ASSERT_EQ(OB_SUCCESS, import_parameter(*type, parameter, source));
  NativeSqlValueOwner owner(target_owner, nullptr, &registry);
  for (int i = 0; i < 3; ++i) {
    Datum output;
    ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output));
    EXPECT_EQ(1, owner.resource_count());
    alignas(8) char scratch[64];
    const DatumParameter result = view(*type, output, scratch);
    EXPECT_EQ(ObString::make_string("repeat owner lifetime"), field(result, 0).get_string());
    ASSERT_EQ(OB_SUCCESS, owner.clear());
    EXPECT_EQ(0, owner.resource_count());
    target_owner.reuse(); // No retained resource token may point into this arena.
  }
}

TEST(NativePlValue, prepared_native_record_clones_without_a_live_namespace_and_keeps_nested_values)
{
  ObArenaAllocator metadata;
  FixtureDeclarations unused_registry(metadata);
  SqlType text, integer;
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text));
  meta.set_int(); ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer));
  const SqlType *inner = record_type(metadata, unused_registry, 771, {&text}, SqlNominalType(), false);
  const SqlType *items = array_type(metadata, *inner);
  const SqlType *outer = record_type(metadata, unused_registry, 772, {&integer, items}, SqlNominalType(), false);
  ObArenaAllocator source_owner;
  FailingAllocator target_owner;
  DatumParameter message, number;
  ASSERT_EQ(OB_SUCCESS, message.set_varchar(ObString::make_string("kept beyond the callee"), source_owner));
  ASSERT_EQ(OB_SUCCESS, number.set_int(INT64_MIN, source_owner));
  const DatumParameter nested = record(source_owner, 771, {message});
  const DatumParameter original = record(source_owner, 772, {number, array(source_owner, {nested})});
  Datum source, output;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*outer, original, source));
  NativeSqlValueOwner owner(target_owner);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*outer, source, owner, output, nullptr, false));
  EXPECT_EQ(0, unused_registry.calls_);
  source_owner.reset();
  alignas(8) char scratch[64];
  const DatumParameter retained = view(*outer, output, scratch);
  EXPECT_EQ(INT64_MIN, field(retained, 0).get_int());
  const DatumParameter retained_items = field(retained, 1);
  const auto *values = retained_items.get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, values); ASSERT_EQ(1, values->count_);
  EXPECT_EQ(ObString::make_string("kept beyond the callee"), field(values->data_[0], 0).get_string());
  ASSERT_EQ(OB_SUCCESS, owner.enforce_empty_before_destroy());
  EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(target_owner.live_.empty());
}

TEST(NativePlValue, prepared_native_null_and_uninitialized_record_do_not_read_a_declaration_owner)
{
  ObArenaAllocator metadata;
  FixtureDeclarations unused_registry(metadata);
  SqlType integer; SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer));
  const SqlType *type = record_type(metadata, unused_registry, 773, {&integer}, SqlNominalType(), false);
  ObArenaAllocator source_owner, target_owner;
  void *memory = source_owner.alloc(sizeof(ObPLRecord)); ASSERT_NE(nullptr, memory);
  auto *source_record = new (memory) ObPLRecord(); source_record->set_id(773);
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(source_record),
      PL_RECORD_TYPE, source_owner, sizeof(ObPLRecord)));
  parameter.set_udt_id(773);
  Datum source, output;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, parameter, source));
  NativeSqlValueOwner owner(target_owner);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output, nullptr, false));
  alignas(8) char scratch[64];
  const DatumParameter retained = view(*type, output, scratch);
  ASSERT_TRUE(as_record(retained)->is_inited()); EXPECT_TRUE(field(retained, 0).is_null());
  ASSERT_EQ(OB_SUCCESS, owner.clear());
  source.set_null();
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output, nullptr, false));
  EXPECT_TRUE(output.is_null()); EXPECT_EQ(0, owner.resource_count()); EXPECT_EQ(0, unused_registry.calls_);
}

TEST(NativePlValue, prepared_native_unknown_heterogeneous_resource_leaf_fails_before_publication)
{
  ObArenaAllocator metadata, source_owner;
  FixtureDeclarations registry(metadata);
  SqlType integer; SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer));
  const SqlType *record_declaration = record_type(metadata, registry, 774, {&integer});
  ASSERT_NE(nullptr, record_declaration);
  const SqlType *type = heterogeneous_array_type(metadata);
  DatumParameter number; ASSERT_EQ(OB_SUCCESS, number.set_int(27, source_owner));
  const DatumParameter original = array(source_owner, {record(source_owner, 774, {number})});
  Datum source, output; output.set_int(991);
  ASSERT_EQ(OB_SUCCESS, import_parameter(*type, original, source));
  FailingAllocator target_owner;
  NativeSqlValueOwner owner(target_owner);
  EXPECT_EQ(OB_NOT_SUPPORTED, clone_native_owned_value(*type, source, owner, output, nullptr, false));
  EXPECT_EQ(991, output.get_int()); EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(target_owner.live_.empty());
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, source, owner, output, &registry, false));
  EXPECT_EQ(1, owner.resource_count()); ASSERT_EQ(OB_SUCCESS, owner.clear()); EXPECT_TRUE(target_owner.live_.empty());
}

TEST(NativePlValue, graph_depth64_uninitialized_record_clears_synthetic_null_fields_but_depth65_rejects)
{
  ObArenaAllocator metadata;
  FixtureDeclarations registry(metadata);
  SqlType integer; SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer));
  ASSERT_NE(nullptr, record_type(metadata, registry, 775, {&integer}));
  const SqlType *type = heterogeneous_array_type(metadata);
  for (int64_t record_depth : {64, 65}) {
    SCOPED_TRACE(record_depth);
    ObArenaAllocator source_owner;
    void *memory = source_owner.alloc(sizeof(ObPLRecord)); ASSERT_NE(nullptr, memory);
    auto *record_value = new (memory) ObPLRecord(); record_value->set_id(775);
    DatumParameter original;
    ASSERT_EQ(OB_SUCCESS, original.set_extend(reinterpret_cast<int64_t>(record_value),
        PL_RECORD_TYPE, source_owner, sizeof(ObPLRecord)));
    original.set_udt_id(775);
    for (int64_t i = 0; i < record_depth; ++i) { original = array(source_owner, {original}); }
    Datum source, output; output.set_int(992);
    ASSERT_EQ(OB_SUCCESS, import_parameter(*type, original, source));
    FailingAllocator target_owner;
    NativeSqlValueOwner owner(target_owner);
    const int ret = clone_native_owned_value(*type, source, owner, output, &registry, false);
    if (record_depth == 64) {
      ASSERT_EQ(OB_SUCCESS, ret);
      source_owner.reset();
      ASSERT_EQ(OB_SUCCESS, owner.enforce_empty_before_destroy());
      EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(target_owner.live_.empty());
    } else {
      EXPECT_EQ(OB_SIZE_OVERFLOW, ret); EXPECT_EQ(992, output.get_int());
      EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(target_owner.live_.empty());
    }
  }
}


TEST(NativePlValue, prepared_native_record_allocation_failures_never_publish_or_orphan_a_token)
{
  ObArenaAllocator metadata, source_owner;
  FixtureDeclarations unused_registry(metadata);
  SqlType text; SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text));
  const SqlType *type = record_type(metadata, unused_registry, 776, {&text}, SqlNominalType(), false);
  DatumParameter message;
  ASSERT_EQ(OB_SUCCESS, message.set_varchar(ObString::make_string("atomic native snapshot"), source_owner));
  const DatumParameter original = record(source_owner, 776, {message});
  Datum source; ASSERT_EQ(OB_SUCCESS, import_parameter(*type, original, source));
  bool observed_success = false;
  for (int64_t failure = 1; failure <= 24; ++failure) {
    FailingAllocator allocator(failure);
    NativeSqlValueOwner owner(allocator);
    Datum output; output.set_int(993);
    const int ret = clone_native_owned_value(*type, source, owner, output, nullptr, false);
    if (ret == OB_SUCCESS) { observed_success = true; }
    else { EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret); EXPECT_EQ(993, output.get_int()); }
    EXPECT_EQ(OB_SUCCESS, owner.enforce_empty_before_destroy());
    EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(allocator.live_.empty());
    if (observed_success) { break; }
  }
  EXPECT_TRUE(observed_success);
}


TEST(NativePlValue, typed_heterogeneous_graph_owns_leaf_types_until_checked_clear_succeeds)
{
  ObArenaAllocator root_metadata, source_types, source_owner;
  FixtureDeclarations unused_registry(source_types);
  const SqlType *outer_type = heterogeneous_array_type(root_metadata);
  const SqlType *nested_type = heterogeneous_array_type(source_types);
  SqlTypeMeta int_meta; int_meta.set_int();
  SqlTypeMeta decimal_meta; decimal_meta.set_decimal_int(5);
  SqlTypeMeta text_meta; text_meta.set_varchar(); text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy text_accuracy; text_accuracy.set_length(128);
  const SqlType *integer_type = nullptr, *decimal_type = nullptr, *text_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(int_meta, ObAccuracy(20, 0), source_types, integer_type));
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(decimal_meta, ObAccuracy(20, 5), source_types, decimal_type));
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(text_meta, text_accuracy, source_types, text_type));
  const SqlType *record_declaration = record_type(source_types, unused_registry, 884, {text_type},
                                                 SqlNominalType(), false);
  ASSERT_NE(nullptr, record_declaration);
  DatumParameter integer, decimal_null, text;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MIN, source_owner));
  integer.set_accuracy(ObAccuracy(20, 0)); integer.set_param_meta(int_meta);
  decimal_null.set_type_meta(decimal_meta); decimal_null.set_param_meta(decimal_meta);
  decimal_null.set_accuracy(ObAccuracy(20, 5)); decimal_null.datum_.set_null();
  decimal_null.set_raw_text_info(37, 41); decimal_null.set_result_flags(0x321);
  decimal_null.set_need_to_check_type(true);
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("leaf declaration owner retires"), source_owner));
  text.set_type_meta(text_meta); text.set_accuracy(text_accuracy); text.set_param_meta(text_meta);
  const DatumParameter record_value = record(source_owner, 884, {text});
  DatumParameter nested = array(source_owner, {record_value});
  ASSERT_EQ(OB_SUCCESS, nested.get_array<DatumParameterArray>()->set_element_type_borrowed(
      0, *record_declaration, source_types));
  DatumParameter original = array(source_owner, {integer, decimal_null, text, nested});
  auto *source_array = original.get_array<DatumParameterArray>();
  ASSERT_EQ(OB_SUCCESS, source_array->set_element_type_borrowed(0, *integer_type, source_types));
  ASSERT_EQ(OB_SUCCESS, source_array->set_element_type_borrowed(1, *decimal_type, source_types));
  ASSERT_EQ(OB_SUCCESS, source_array->set_element_type_borrowed(2, *text_type, source_types));
  ASSERT_EQ(OB_SUCCESS, source_array->set_element_type_borrowed(3, *nested_type, source_types));
  Datum source, output;
  ASSERT_EQ(OB_SUCCESS, import_parameter(*outer_type, original, source));
  FailingAllocator target_owner;
  NativeSqlValueOwner owner(target_owner);
  ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*outer_type, source, owner, output, nullptr, false));
  alignas(8) char scratch[64];
  const DatumParameter retained = view(*outer_type, output, scratch);
  const auto *owned = retained.get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, owned); ASSERT_EQ(4, owned->count_);
  ASSERT_NE(nullptr, owned->get_element_type(1));
  EXPECT_NE(decimal_type, owned->get_element_type(1));
  EXPECT_EQ(20, owned->get_element_type(1)->accuracy().get_precision());
  EXPECT_EQ(5, owned->get_element_type(1)->accuracy().get_scale());
  source_owner.reset(); source_types.reset();
  Datum leaf; const SqlType *leaf_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, owned->get_native_element(0, leaf, leaf_type));
  EXPECT_EQ(INT64_MIN, leaf.get_int()); EXPECT_EQ(ObIntType, leaf_type->kind());
  ASSERT_EQ(OB_SUCCESS, owned->get_native_element(1, leaf, leaf_type));
  EXPECT_TRUE(leaf.is_null()); EXPECT_EQ(ObDecimalIntType, leaf_type->kind());
  EXPECT_EQ(20, leaf_type->accuracy().get_precision()); EXPECT_EQ(5, leaf_type->accuracy().get_scale());
  EXPECT_EQ(37, owned->data_[1].get_raw_text_pos()); EXPECT_EQ(41, owned->data_[1].get_raw_text_len());
  EXPECT_EQ(0x321u, owned->data_[1].get_result_flag()); EXPECT_TRUE(owned->data_[1].need_to_check_type());
  ASSERT_EQ(OB_SUCCESS, owned->get_native_element(2, leaf, leaf_type));
  EXPECT_EQ(ObString::make_string("leaf declaration owner retires"), leaf.get_string());
  const auto *nested_owned = owned->data_[3].get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, nested_owned); ASSERT_EQ(1, nested_owned->count_);
  ASSERT_NE(nullptr, nested_owned->get_element_type(0));
  EXPECT_EQ(884u, nested_owned->get_element_type(0)->pl_declaration().declaration_id);
  EXPECT_EQ(0x7a001u, nested_owned->get_element_type(0)->pl_declaration().compilation_scope.low);
  EXPECT_EQ(ObString::make_string("leaf declaration owner retires"), field(nested_owned->data_[0], 0).get_string());
  // A checked failure retains the graph and its metadata Arena for retry.
  auto *owned_record = as_record(nested_owned->data_[0]);
  owned_record->set_count(-1);
  EXPECT_EQ(OB_INVALID_ARGUMENT, owner.clear()); EXPECT_EQ(1, owner.resource_count());
  EXPECT_EQ(884u, nested_owned->get_element_type(0)->pl_declaration().declaration_id);
  owned_record->set_count(1);
  EXPECT_EQ(OB_SUCCESS, owner.enforce_empty_before_destroy());
  EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(target_owner.live_.empty());
  EXPECT_EQ(0, unused_registry.calls_);
}

TEST(NativePlValue, typed_array_metadata_arena_rolls_back_with_each_parent_allocation_failure)
{
  ObArenaAllocator metadata, source_owner;
  const SqlType *type = heterogeneous_array_type(metadata);
  SqlTypeMeta text_meta; text_meta.set_varchar(); text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy text_accuracy; text_accuracy.set_length(128);
  const SqlType *leaf_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(text_meta, text_accuracy, metadata, leaf_type));
  DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("metadata rollback"), source_owner));
  text.set_type_meta(text_meta); text.set_accuracy(text_accuracy);
  DatumParameter original = array(source_owner, {text});
  ASSERT_EQ(OB_SUCCESS, original.get_array<DatumParameterArray>()->set_element_type_borrowed(0, *leaf_type, metadata));
  Datum source; ASSERT_EQ(OB_SUCCESS, import_parameter(*type, original, source));
  bool succeeded = false;
  for (int64_t failure = 1; failure <= 32 && !succeeded; ++failure) {
    FailingAllocator allocator(failure);
    NativeSqlValueOwner owner(allocator);
    Datum output; output.set_int(997);
    const int ret = clone_native_owned_value(*type, source, owner, output, nullptr, false);
    if (ret == OB_SUCCESS) {
      succeeded = true;
      alignas(8) char scratch[64];
      const DatumParameter result = view(*type, output, scratch);
      const auto *values = result.get_array<DatumParameterArray>();
      ASSERT_NE(nullptr, values); ASSERT_NE(nullptr, values->get_element_type(0));
      EXPECT_NE(leaf_type, values->get_element_type(0));
      EXPECT_EQ(ObString::make_string("metadata rollback"), values->data_[0].get_string());
    } else { EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret); EXPECT_EQ(997, output.get_int()); }
    EXPECT_EQ(OB_SUCCESS, owner.enforce_empty_before_destroy());
    EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(allocator.live_.empty()) << failure;
  }
  EXPECT_TRUE(succeeded);
}


TEST(NativePlValue, owned_record_carrier_remains_inline_after_legacy_source_and_scratch_retire)
{
  ObArenaAllocator type_owner;
  FixtureDeclarations registry(type_owner);
  SqlTypeMeta integer_meta; integer_meta.set_int();
  SqlType integer; ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(integer_meta, ObAccuracy(), integer));
  const SqlType *type = record_type(type_owner, registry, 1907, {&integer}); ASSERT_NE(nullptr, type);
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    FailingAllocator target;
    NativeSqlValueOwner owner(target, nullptr, &registry);
    Datum output; output.set_int(93);
    {
      ObArenaAllocator source_owner;
      DatumParameter field_value; ASSERT_EQ(OB_SUCCESS, field_value.set_int(739, source_owner));
      DatumParameter source = record(source_owner, 1907, {field_value});
      Datum native; native.set_inline_extended(source.get_ext(), PL_RECORD_TYPE, source.get_val_len(),
          CS_LEVEL_INVALID, CS_TYPE_INVALID, legacy16);
      char wrapper[16]; ASSERT_EQ(OB_SUCCESS, native.write_inline_extended_payload(wrapper, sizeof(wrapper)));
      Datum borrowed; ASSERT_EQ(OB_SUCCESS, borrowed.set_reference(wrapper, legacy16 ? 16 : 15, Datum::LegacyExtended));
      ASSERT_EQ(OB_SUCCESS, clone_native_owned_value(*type, borrowed, owner, output));
      ASSERT_TRUE(output.is_inline_extended()); EXPECT_NE(source.get_ext(), output.get_ext());
      EXPECT_EQ(1, owner.resource_count()); EXPECT_EQ(0, target.carrier_calls_);
      MEMSET(wrapper, 0xa5, sizeof(wrapper));
      ASSERT_EQ(OB_SUCCESS, release_owned_parameter_graph(source, source_owner));
    }
    // The output carrier is not a Reference into NativeSqlValueOwner::clone's
    // returned scratch. The actual cloned record survives source destruction.
    ASSERT_TRUE(output.is_inline_extended());
    auto *record_value = reinterpret_cast<ObPLRecord *>(output.get_ext());
    ASSERT_NE(nullptr, record_value); ASSERT_EQ(1, record_value->get_count());
    EXPECT_EQ(739, record_value->get_element()[0].get_int());
    EXPECT_EQ(0, target.carrier_calls_);
    EXPECT_EQ(OB_SUCCESS, owner.enforce_empty_before_destroy());
    EXPECT_EQ(0, owner.resource_count()); EXPECT_TRUE(target.live_.empty());
  }
}


TEST(NativePlValue, expr_compatibility_clone_preserves_native_carrier_without_returning_scratch)
{
  ObArenaAllocator metadata;
  FixtureDeclarations registry(metadata);
  const SqlType *type = record_type(metadata, registry, 1908, {}); ASSERT_NE(nullptr, type);
  FailingAllocator owner;
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    Datum native; native.set_inline_extended(65536, PL_RECORD_TYPE, -19,
        CS_LEVEL_INVALID, CS_TYPE_INVALID, legacy16);
    char wrapper[16]; ASSERT_EQ(OB_SUCCESS, native.write_inline_extended_payload(wrapper, sizeof(wrapper)));
    Datum borrowed; ASSERT_EQ(OB_SUCCESS, borrowed.set_reference(wrapper, legacy16 ? 16 : 15, Datum::LegacyExtended));
    Datum output; output.set_int(93);
    // This compatibility cloner retains only the old carrier/resource borrow
    // policy. It does not invent ownership of the pointed record.
    ASSERT_EQ(OB_SUCCESS, ObExpr::clone_native_value(*type, borrowed, owner, output));
    ASSERT_TRUE(output.is_inline_extended()); EXPECT_EQ(65536, output.get_ext());
    EXPECT_EQ(-19, output.get_extend_size()); EXPECT_EQ(legacy16 ? 16U : 15U, output.inline_extended_payload_size());
    MEMSET(wrapper, 0xa5, sizeof(wrapper));
    EXPECT_EQ(65536, output.get_ext()); EXPECT_EQ(0, owner.carrier_calls_); EXPECT_TRUE(owner.live_.empty());
  }
}

TEST(NativePlValue, expr_carrier_copy_normalizes_legacy_pl_only_after_full_type_validation)
{
  ObArenaAllocator metadata;
  FixtureDeclarations registry(metadata);
  const SqlType *type = record_type(metadata, registry, 1909, {}); ASSERT_NE(nullptr, type);
  FailingAllocator owner;
  ObExecContext execution(owner);
  ObEvalCtx evaluation(execution);
  alignas(8) char frame[128] = {};
  char *frames[] = {frame};
  evaluation.frames_ = frames;
  ObExpr expression;
  expression.frame_idx_ = 0; expression.datum_off_ = 0;
  expression.eval_info_off_ = sizeof(Datum);
  expression.res_buf_off_ = 24; expression.res_buf_len_ = 16;
  new (frame) Datum(); new (frame + expression.eval_info_off_) ObEvalInfo{};
  ASSERT_EQ(OB_SUCCESS, expression.bind_sql_type(*type, metadata));
  Datum &slot = expression.locate_expr_datum(evaluation);
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    Datum native; native.set_inline_extended(65536, PL_RECORD_TYPE, -29, 0xa7, 0xef, legacy16);
    char wrapper[16]; ASSERT_EQ(OB_SUCCESS, native.write_inline_extended_payload(wrapper, sizeof(wrapper)));
    Datum borrowed; ASSERT_EQ(OB_SUCCESS, borrowed.set_reference(wrapper, legacy16 ? 16 : 15, Datum::LegacyExtended));
    const int64_t calls = owner.calls_;
    owner.failure_ = calls + 1;
    slot.set_int(71);
    ASSERT_EQ(OB_SUCCESS, expression.deep_copy_datum(evaluation, borrowed));
    ASSERT_TRUE(slot.is_inline_extended()); EXPECT_EQ(65536, slot.get_ext());
    EXPECT_EQ(-29, slot.get_extend_size()); EXPECT_EQ(0xa7, slot.get_extend_collation_level());
    EXPECT_EQ(0xef, slot.get_extend_collation());
    EXPECT_EQ(legacy16 ? 16U : 15U, slot.inline_extended_payload_size());
    EXPECT_EQ(calls, owner.calls_); EXPECT_EQ(0, owner.carrier_calls_);
    MEMSET(wrapper, 0x5a, sizeof(wrapper));
    ASSERT_EQ(OB_SUCCESS, expression.deep_copy_datum(evaluation, slot));
    EXPECT_EQ(65536, slot.get_ext()); EXPECT_EQ(calls, owner.calls_);
    // A physically valid carrier for another PL subtype still fails the
    // prepared Record declaration before publishing to the actual Expr slot.
    Datum incompatible; incompatible.set_inline_extended(65536, PL_CURSOR_TYPE, -29);
    const Datum previous = slot;
    EXPECT_EQ(OB_INVALID_ARGUMENT, expression.deep_copy_datum(evaluation, incompatible));
    EXPECT_EQ(0, MEMCMP(&previous, &slot, sizeof(Datum))); EXPECT_EQ(calls, owner.calls_);
    Datum null_value;
    ASSERT_EQ(OB_SUCCESS, expression.deep_copy_datum(evaluation, null_value));
    EXPECT_TRUE(slot.is_null()); EXPECT_FALSE(slot.is_inline_extended());
    EXPECT_EQ(calls, owner.calls_);
  }
}

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
