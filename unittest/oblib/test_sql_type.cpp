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

#include "common/datum/sql_type.h"
#include "common/datum/datum_copy_plan.h"
#include <gtest/gtest.h>
#include <type_traits>
#include <vector>
#include <cstring>

#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Complete native types must not depend on legacy value objects"
#endif

using namespace oceanbase::common;

namespace {
static_assert(!std::is_copy_assignable<SqlType>::value, "Published type descriptors are immutable");

SqlTypeSpec decimal_spec(ObPrecision precision, ObScale scale)
{
  SqlTypeSpec spec;
  spec.attributes.meta_.set_decimal_int(scale);
  spec.attributes.set_precision(precision);
  spec.attributes.set_scale(scale);
  spec.specified = SqlTypeSpec::Precision | SqlTypeSpec::Scale;
  return spec;
}

class RejectTypeAllocator final : public ObIAllocator
{
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};

int numeric_cast_resolver(const SqlType &source, const SqlType &target,
                          void *context, bool &possible)
{
  ++*static_cast<int *>(context);
  possible = source.kind() == ObIntType && target.kind() == ObDoubleType;
  return OB_SUCCESS;
}

int rejecting_cast_resolver(const SqlType &, const SqlType &, void *, bool &possible)
{
  possible = false;
  return OB_INVALID_ARGUMENT;
}

SqlTypeSpec local_record_spec(uint64_t declaration_id = 9)
{
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext();
  // The stable PL_RECORD_TYPE wire discriminator is 1. oblib must not depend
  // on the PL declaration headers; actual Record producers validate that type.
  spec.attributes.meta_.set_extend_type(1);
  spec.attributes.set_udt_id(declaration_id);
  spec.attributes.set_charset_type(CHARSET_INVALID);
  spec.form = SqlTypeForm::Record;
  spec.pl_declaration.source = SqlPLDeclarationSource::Local;
  spec.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  spec.pl_declaration.declaration_id = declaration_id;
  spec.pl_declaration.compilation_scope.high = 1;
  spec.pl_declaration.compilation_scope.low = 2;
  return spec;
}

SqlTypeSpec package_record_spec()
{
  SqlTypeSpec spec = local_record_spec(combine_pl_type_id(42, 9));
  spec.pl_declaration.source = SqlPLDeclarationSource::PackageSpec;
  spec.pl_declaration.compilation_scope = SqlPLCompilationScope();
  spec.nominal.specified = true;
  spec.nominal.tenant_id = spec.pl_declaration.tenant_id;
  spec.nominal.object_id = 42;
  spec.nominal.schema_version = 7;
  return spec;
}

class LimitedTypeAllocator final : public ObIAllocator
{
public:
  explicit LimitedTypeAllocator(int64_t successful_allocations)
    : remaining_(successful_allocations), owner_() {}
  void *alloc(int64_t size) override
  { return remaining_-- > 0 ? owner_.alloc(size) : nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {} // This test allocator deliberately has arena lifetime.
private:
  int64_t remaining_;
  ObArenaAllocator owner_;
};

// Independent wire fixtures: v1's four meta bytes, raw accuracy, display /
// charset fields, form/mask/time, nominal quartet, -1 array bound, and seven
// zero shape fields. v2 adds only the five explicit identity fields after the
// nominal quartet. These bytes do not call SqlType's writer to form the oracle.
const uint8_t V1_SQL_RECORD[] = {
  1, 38, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 1,
  0, 0, 0, 0, 0, 0, 0
};
const uint8_t V2_LOCAL_RECORD[] = {
  2, 25, 7, 0, 1, 9, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0,
  1, 1, 9, 1, 2, // source, tenant, declaration, scope high, scope low
  255, 255, 255, 255, 255, 255, 255, 255, 255, 1,
  0, 0, 0, 0, 0, 0, 0
};
}

TEST(SqlType, scalar_legacy_bridge_needs_no_owner_and_publishes_only_on_success)
{
  SqlType type;
  SqlTypeMeta meta; meta.set_int();
  ObAccuracy accuracy;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, type));
  EXPECT_EQ(ObIntType, type.kind());
  EXPECT_EQ(DatumRepresentation::Inline64, type.canonical_representation());
  meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  EXPECT_EQ(OB_NOT_SUPPORTED, SqlType::from_legacy(meta, accuracy, type));
  EXPECT_EQ(ObIntType, type.kind());
  meta.set_type(ObMaxType);
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::from_legacy(meta, accuracy, type));
  EXPECT_EQ(ObIntType, type.kind());
}

TEST(SqlType, every_ordinary_integer_has_one_inline_representation)
{
  const SqlValueType kinds[] = {ObTinyIntType, ObSmallIntType, ObMediumIntType, ObInt32Type, ObIntType,
      ObUTinyIntType, ObUSmallIntType, ObUMediumIntType, ObUInt32Type, ObUInt64Type};
  for (const SqlValueType kind : kinds) {
    SqlTypeMeta meta; meta.set_type(kind);
    SqlType type;
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
    EXPECT_EQ(DatumRepresentation::Inline64, type.canonical_representation());
  }
}

TEST(SqlType, precision_scale_and_decoder_compatibility_are_distinct)
{
  ObArenaAllocator owner;
  const SqlType *left = nullptr, *same_layout = nullptr, *other_scale = nullptr, *small = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal_spec(19, 2), owner, left));
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal_spec(20, 2), owner, same_layout));
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal_spec(20, 5), owner, other_scale));
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal_spec(18, 2), owner, small));
  EXPECT_FALSE(left->equals(*same_layout));
  EXPECT_TRUE(left->physically_compatible(*same_layout));
  EXPECT_FALSE(left->equals(*other_scale));
  EXPECT_FALSE(left->physically_compatible(*other_scale));
  EXPECT_FALSE(left->physically_compatible(*small));
  EXPECT_EQ(DatumRepresentation::Reference, left->canonical_representation());
  EXPECT_EQ(DatumRepresentation::Inline64, small->canonical_representation());
  const SqlType *independent = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal_spec(19, 2), owner, independent));
  EXPECT_NE(left, independent);
  EXPECT_TRUE(left->equals(*independent));
}

TEST(SqlType, unknown_precision_never_infers_a_value_dependent_decimal_layout)
{
  SqlTypeMeta meta; meta.set_decimal_int(5);
  SqlType type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
  EXPECT_FALSE(type.has_precision());
  EXPECT_TRUE(type.has_scale());
  EXPECT_EQ(5, type.scale());
  EXPECT_EQ(DatumRepresentation::Reference, type.canonical_representation());
  ObArenaAllocator owner;
  SqlTypeSpec specified = decimal_spec(20, -1);
  const SqlType *negative_scale = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(specified, owner, negative_scale));
  EXPECT_TRUE(negative_scale->has_scale());
  EXPECT_EQ(-1, negative_scale->scale());
  specified.specified &= ~SqlTypeSpec::Scale;
  const SqlType *unspecified_scale = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(specified, owner, unspecified_scale));
  EXPECT_FALSE(negative_scale->equals(*unspecified_scale));
}

TEST(SqlType, overlaid_accuracy_fields_cannot_be_published_as_independent_attributes)
{
  ObArenaAllocator owner;
  const SqlType *output = nullptr;
  SqlTypeSpec numeric = decimal_spec(19, 2);
  numeric.specified |= SqlTypeSpec::Length;
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(numeric, owner, output));
  EXPECT_EQ(nullptr, output);
  SqlTypeSpec string;
  string.attributes.meta_.set_varchar();
  string.attributes.set_length(16);
  string.specified = SqlTypeSpec::Length | SqlTypeSpec::Precision;
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(string, owner, output));
  EXPECT_EQ(nullptr, output);
}

TEST(SqlType, string_keeps_full_collation_length_and_length_semantics)
{
  ObDataType attributes;
  attributes.meta_.set_varchar();
  attributes.set_collation_type(CS_TYPE_UTF8MB4_ZH_0900_AS_CS);
  attributes.set_collation_level(CS_LEVEL_IMPLICIT);
  attributes.set_charset_type(CHARSET_UTF8MB4);
  attributes.set_length(16);
  attributes.set_length_semantics(LS_CHAR);
  SqlType characters, bytes, other_collation;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(attributes, characters));
  EXPECT_EQ(CS_TYPE_UTF8MB4_ZH_0900_AS_CS, characters.collation());
  EXPECT_EQ(16, characters.length());
  EXPECT_TRUE(characters.has_length_semantics());
  EXPECT_EQ(LS_CHAR, characters.length_semantics());
  attributes.set_length_semantics(LS_BYTE);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(attributes, bytes));
  EXPECT_FALSE(characters.equals(bytes));
  EXPECT_TRUE(characters.physically_compatible(bytes));
  attributes.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(attributes, other_collation));
  EXPECT_FALSE(bytes.equals(other_collation));
  EXPECT_TRUE(bytes.physically_compatible(other_collation));
}

TEST(SqlType, time_semantics_survive_as_part_of_the_type)
{
  SqlTypeMeta meta; meta.set_timestamp();
  ObAccuracy accuracy; accuracy.set_scale(6);
  SqlType instant;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, instant));
  EXPECT_EQ(SqlTimeSemantics::Instant, instant.time_semantics());
  ObArenaAllocator owner;
  SqlTypeSpec local;
  local.attributes.meta_ = meta; local.attributes.accuracy_ = accuracy;
  local.specified = SqlTypeSpec::Scale;
  const SqlType *defaulted = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local, owner, defaulted));
  EXPECT_EQ(SqlTimeSemantics::Instant, defaulted->time_semantics());
  EXPECT_TRUE(instant.equals(*defaulted));
  EXPECT_EQ(DatumRepresentation::Inline64, defaulted->canonical_representation());
  local.time = SqlTimeSemantics::SessionLocal;
  const SqlType *session_local = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local, owner, session_local));
  EXPECT_FALSE(instant.equals(*session_local));
  EXPECT_FALSE(instant.physically_compatible(*session_local));
  EXPECT_EQ(DatumRepresentation::Reference, session_local->canonical_representation());
  local.time = SqlTimeSemantics::WithTimeZone;
  const SqlType *with_zone = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local, owner, with_zone));
  EXPECT_EQ(DatumRepresentation::Reference, with_zone->canonical_representation());
}

TEST(SqlType, coercibility_and_display_flags_do_not_change_logical_type_identity)
{
  ObDataType attributes;
  attributes.meta_.set_varchar();
  attributes.set_collation_type(CS_TYPE_UTF8MB4_ZH_0900_AS_CS);
  attributes.set_collation_level(CS_LEVEL_IMPLICIT);
  attributes.set_charset_type(CHARSET_UTF8MB4);
  attributes.set_length(16); attributes.set_length_semantics(LS_CHAR);
  SqlType implicit, explicit_type, binary_marker;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(attributes, implicit));
  attributes.set_collation_level(CS_LEVEL_EXPLICIT);
  attributes.set_zero_fill(true);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(attributes, explicit_type));
  EXPECT_TRUE(implicit.equals(explicit_type));
  EXPECT_TRUE(implicit.physically_compatible(explicit_type));
  EXPECT_EQ(CS_LEVEL_EXPLICIT, explicit_type.legacy_meta().get_collation_level());
  EXPECT_TRUE(explicit_type.attributes().is_zero_fill());
  attributes.set_binary_collation(true);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(attributes, binary_marker));
  EXPECT_FALSE(implicit.equals(binary_marker));

  ObDataType numeric;
  numeric.meta_.set_int();
  SqlType ordinary, decorated;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(numeric, ordinary));
  numeric.set_charset_type(CHARSET_BINARY); numeric.set_binary_collation(true);
  numeric.set_zero_fill(true);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(numeric, decorated));
  EXPECT_TRUE(ordinary.equals(decorated));
  EXPECT_TRUE(ordinary.physically_compatible(decorated));
}

TEST(SqlType, enum_and_set_ordinals_inline_but_inner_strings_reference)
{
  ObArenaAllocator owner;
  const SqlValueType kinds[] = {ObEnumType, ObSetType, ObEnumInnerType, ObSetInnerType};
  ObString labels[] = {ObString("first"), ObString("second")};
  for (const SqlValueType kind : kinds) {
    SqlTypeSpec spec;
    spec.attributes.meta_.set_type(kind);
    spec.form = kind == ObEnumType || kind == ObEnumInnerType ? SqlTypeForm::Enum : SqlTypeForm::Set;
    spec.labels = labels; spec.label_count = 2;
    const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
    const DatumRepresentation expected = kind == ObEnumType || kind == ObSetType
        ? DatumRepresentation::Inline64 : DatumRepresentation::Reference;
    EXPECT_EQ(expected, type->canonical_representation());
  }
}

TEST(SqlType, complex_children_names_and_enum_labels_belong_to_the_destination_owner)
{
  ObArenaAllocator source, destination;
  SqlTypeMeta integer; integer.set_int();
  const SqlType *integer_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(integer, ObAccuracy(), source, integer_type));
  SqlTypeSpec array;
  array.attributes.meta_.set_collection(3); array.form = SqlTypeForm::Array;
  array.element = integer_type; array.array_limit = 12;
  const SqlType *array_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(array, source, array_type));
  char name[] = "items";
  SqlTypeMember member;
  member.name.assign_ptr(name, 5); member.type = array_type; member.nullable = false;
  SqlTypeSpec record;
  record.attributes.meta_.set_type(ObUserDefinedSQLType);
  record.attributes.meta_.set_subschema_id(4); record.form = SqlTypeForm::Record;
  record.members = &member; record.member_count = 1;
  const SqlType *record_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(record, destination, record_type));
  char first[] = "ready", second[] = "busy";
  ObString labels[] = {ObString::make_string(first), ObString::make_string(second)};
  SqlTypeSpec enumeration;
  enumeration.attributes.meta_.set_enum(); enumeration.form = SqlTypeForm::Enum;
  enumeration.labels = labels; enumeration.label_count = 2;
  const SqlType *enum_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(enumeration, destination, enum_type));
  source.reset(); name[0] = 'X'; first[0] = 'X';
  ASSERT_EQ(1, record_type->member_count());
  EXPECT_EQ(ObString("items"), record_type->member(0).name);
  EXPECT_FALSE(record_type->member(0).nullable);
  ASSERT_NE(nullptr, record_type->member(0).type->element());
  EXPECT_EQ(ObIntType, record_type->member(0).type->element()->kind());
  EXPECT_EQ(12, record_type->member(0).type->array_limit());
  EXPECT_EQ(ObString("ready"), enum_type->label(0));
  EXPECT_EQ(ObString("busy"), enum_type->label(1));
}

TEST(SqlType, nominal_versions_and_enum_label_order_are_not_interchangeable)
{
  ObArenaAllocator owner;
  SqlTypeSpec nominal;
  nominal.attributes.meta_.set_type(ObUserDefinedSQLType);
  nominal.attributes.meta_.set_subschema_id(8); nominal.form = SqlTypeForm::UserDefined;
  nominal.nominal.specified = true; nominal.nominal.tenant_id = 1001;
  nominal.nominal.object_id = 42; nominal.nominal.schema_version = 7;
  const SqlType *old = nullptr, *updated = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(nominal, owner, old));
  nominal.nominal.schema_version = 8;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(nominal, owner, updated));
  EXPECT_FALSE(old->equals(*updated));
  EXPECT_FALSE(old->physically_compatible(*updated));
  ObString labels[] = {ObString("a"), ObString("b")};
  SqlTypeSpec enumeration;
  enumeration.attributes.meta_.set_set(); enumeration.form = SqlTypeForm::Set;
  enumeration.labels = labels; enumeration.label_count = 2;
  const SqlType *forward = nullptr, *reverse = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(enumeration, owner, forward));
  labels[0] = ObString("b"); labels[1] = ObString("a");
  ASSERT_EQ(OB_SUCCESS, SqlType::create(enumeration, owner, reverse));
  EXPECT_FALSE(forward->equals(*reverse));
  EXPECT_FALSE(forward->physically_compatible(*reverse));
}

TEST(SqlType, castability_is_delegated_and_never_substituted_for_type_equality)
{
  SqlTypeMeta meta; meta.set_int();
  SqlType integer, floating;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), integer));
  meta.set_double();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), floating));
  bool possible = false; int resolver_calls = 0;
  ASSERT_EQ(OB_SUCCESS, integer.can_cast_to(floating, numeric_cast_resolver, &resolver_calls, possible));
  EXPECT_TRUE(possible); EXPECT_EQ(1, resolver_calls);
  EXPECT_FALSE(integer.equals(floating));
  EXPECT_FALSE(integer.physically_compatible(floating));
  ASSERT_EQ(OB_SUCCESS, integer.can_cast_to(integer, nullptr, nullptr, possible));
  EXPECT_TRUE(possible);
  EXPECT_EQ(OB_NOT_SUPPORTED, integer.can_cast_to(floating, nullptr, nullptr, possible));
  EXPECT_TRUE(possible);
  EXPECT_EQ(OB_INVALID_ARGUMENT, integer.can_cast_to(floating, rejecting_cast_resolver, nullptr, possible));
  EXPECT_TRUE(possible);
}

TEST(SqlType, missing_complex_information_and_failed_allocations_do_not_publish_a_type)
{
  SqlType type;
  SqlTypeMeta meta; meta.set_enum();
  EXPECT_EQ(OB_NOT_SUPPORTED, SqlType::from_legacy(meta, ObAccuracy(), type));
  meta.set_collection(3);
  EXPECT_EQ(OB_NOT_SUPPORTED, SqlType::from_legacy(meta, ObAccuracy(), type));
  RejectTypeAllocator reject;
  const SqlType *output = &type;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, SqlType::create(decimal_spec(19, 2), reject, output));
  EXPECT_EQ(&type, output);
  SqlTypeSpec invalid = decimal_spec(155, 2);
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(invalid, reject, output));
  EXPECT_EQ(&type, output);
}

TEST(SqlType, full_tree_clone_and_wire_own_names_labels_and_nonlegacy_attributes)
{
  ObArenaAllocator source_owner, clone_owner, wire_owner;
  SqlTypeSpec decimal = decimal_spec(20, -1);
  decimal.specified = SqlTypeSpec::Scale; // explicitly specified -1, unknown precision
  const SqlType *coefficient = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal, source_owner, coefficient));
  char first[] = "red", second[] = "\xe7\xbb\xbf";
  ObString labels[] = {ObString(first), ObString(second)};
  SqlTypeSpec enumeration;
  enumeration.attributes.meta_.set_enum(); enumeration.form = SqlTypeForm::Enum;
  enumeration.attributes.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  enumeration.attributes.set_charset_type(CHARSET_UTF8MB4);
  enumeration.attributes.set_binary_collation(true);
  enumeration.attributes.set_zero_fill(true);
  enumeration.labels = labels; enumeration.label_count = 2;
  const SqlType *choices = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(enumeration, source_owner, choices));
  SqlTypeSpec array;
  array.attributes.meta_.set_type(ObCollectionSQLType); array.form = SqlTypeForm::Array;
  array.element = choices; array.array_limit = 37;
  const SqlType *collection = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(array, source_owner, collection));
  SqlTypeMember members[2];
  members[0].name = ObString("amount"); members[0].type = coefficient; members[0].nullable = false;
  members[1].name = ObString("choices"); members[1].type = collection;
  SqlTypeSpec record;
  record.attributes.meta_.set_type(ObUserDefinedSQLType);
  record.attributes.set_subschema_id(237); record.attributes.meta_.set_udt_flags(3);
  record.attributes.set_udt_id(991);
  record.form = SqlTypeForm::UserDefined;
  record.nominal.specified = true; record.nominal.tenant_id = 1001;
  record.nominal.object_id = 991; record.nominal.schema_version = 27;
  record.members = members; record.member_count = 2;
  const SqlType *type = nullptr, *clone = nullptr, *decoded = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(record, source_owner, type));
  ASSERT_EQ(OB_SUCCESS, type->clone_to(clone_owner, clone));
  ASSERT_NE(type, clone); EXPECT_TRUE(type->equals(*clone));
  const int64_t size = type->get_serialize_size(); ASSERT_GT(size, 0);
  std::vector<char> bytes(size + 4, '\x5a');
  int64_t position = 3;
  ASSERT_EQ(OB_SUCCESS, type->serialize(bytes.data(), size + 3, position));
  ASSERT_EQ(size + 3, position); EXPECT_EQ('\x5a', bytes.back());
  int64_t read = 3;
  ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(bytes.data(), position, read, wire_owner, decoded));
  EXPECT_EQ(position, read); ASSERT_NE(type, decoded); EXPECT_TRUE(type->equals(*decoded));
  EXPECT_EQ(type->attributes(), decoded->attributes());
  EXPECT_EQ(type->member(1).type->element()->attributes(), decoded->member(1).type->element()->attributes());
  EXPECT_EQ(237, decoded->legacy_meta().get_subschema_id());
  EXPECT_EQ(3, decoded->legacy_meta().get_udt_flags());
  EXPECT_EQ(991, decoded->attributes().get_udt_id());
  source_owner.reset(); std::memset(bytes.data(), 0, bytes.size());
  EXPECT_TRUE(clone->equals(*decoded));
  EXPECT_EQ(ObString("amount"), decoded->member(0).name);
  EXPECT_FALSE(decoded->member(0).nullable);
  EXPECT_FALSE(decoded->member(0).type->has_precision());
  EXPECT_TRUE(decoded->member(0).type->has_scale());
  EXPECT_EQ(-1, decoded->member(0).type->scale());
  EXPECT_EQ(37, decoded->member(1).type->array_limit());
  const auto *decoded_enum = decoded->member(1).type->element();
  EXPECT_EQ(ObString("red"), decoded_enum->label(0));
  EXPECT_EQ(ObString("\xe7\xbb\xbf"), decoded_enum->label(1));
  EXPECT_TRUE(decoded_enum->attributes().is_binary_collation());
  EXPECT_TRUE(decoded_enum->attributes().is_zero_fill());
}

TEST(SqlType, wire_short_buffers_and_impossible_counts_do_not_publish)
{
  SqlType scalar;
  SqlTypeMeta meta; meta.set_int();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), scalar));
  const int64_t size = scalar.get_serialize_size(); ASSERT_GT(size, 2);
  std::vector<char> bytes(size, '\x35');
  int64_t position = 0;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, scalar.serialize(bytes.data(), size - 1, position));
  EXPECT_EQ(0, position);
  for (char byte : bytes) { EXPECT_EQ('\x35', byte); }
  ASSERT_EQ(OB_SUCCESS, scalar.serialize(bytes.data(), size, position)); EXPECT_EQ(size, position);
  for (int64_t length = 0; length < size; ++length) {
    ObArenaAllocator owner;
    const SqlType *output = &scalar;
    int64_t read = 0;
    EXPECT_NE(OB_SUCCESS, SqlType::deserialize(bytes.data(), length, read, owner, output));
    EXPECT_EQ(&scalar, output); EXPECT_EQ(0, read);
  }
  // Scalar's final fields are the zero member and label counts. A single-byte
  // count 127 cannot be satisfied by the one/zero remaining input bytes.
  for (int64_t count_position : {size - 2, size - 1}) {
    std::vector<char> invalid = bytes; invalid[count_position] = 127;
    ObArenaAllocator owner; const SqlType *output = &scalar; int64_t read = 0;
    EXPECT_EQ(OB_DESERIALIZE_ERROR, SqlType::deserialize(invalid.data(), size, read, owner, output));
    EXPECT_EQ(&scalar, output); EXPECT_EQ(0, read);
  }
  RejectTypeAllocator reject;
  const SqlType *output = &scalar; int64_t read = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, SqlType::deserialize(bytes.data(), size, read, reject, output));
  EXPECT_EQ(&scalar, output); EXPECT_EQ(0, read);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, scalar.clone_to(reject, output)); EXPECT_EQ(&scalar, output);
}

TEST(SqlType, wire_preserves_length_semantics_and_temporal_interpretation)
{
  ObArenaAllocator owner, decoded_owner;
  SqlTypeSpec spec;
  spec.attributes.meta_.set_varchar(); spec.attributes.set_length(19);
  spec.attributes.set_length_semantics(LS_CHAR);
  spec.attributes.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  spec.attributes.set_charset_type(CHARSET_UTF8MB4);
  spec.specified = SqlTypeSpec::Length | SqlTypeSpec::LengthSemantics;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  std::vector<char> bytes(type->get_serialize_size()); int64_t write = 0, read = 0;
  ASSERT_EQ(OB_SUCCESS, type->serialize(bytes.data(), bytes.size(), write));
  const SqlType *decoded = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(bytes.data(), bytes.size(), read, decoded_owner, decoded));
  EXPECT_EQ(write, read); EXPECT_EQ(LS_CHAR, decoded->length_semantics()); EXPECT_EQ(19, decoded->length());
  SqlTypeSpec temporal;
  temporal.attributes.meta_.set_type(ObTimestampLTZType); temporal.time = SqlTimeSemantics::SessionLocal;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(temporal, owner, type));
  bytes.resize(type->get_serialize_size()); write = read = 0;
  ASSERT_EQ(OB_SUCCESS, type->serialize(bytes.data(), bytes.size(), write));
  ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(bytes.data(), bytes.size(), read, decoded_owner, decoded));
  EXPECT_EQ(ObTimestampLTZType, decoded->kind());
  EXPECT_EQ(SqlTimeSemantics::SessionLocal, decoded->time_semantics());
}

TEST(SqlType, wire_requires_bounded_varints_and_complete_owned_strings)
{
  ObArenaAllocator source_owner;
  ObString label("abc");
  SqlTypeSpec enumeration;
  enumeration.attributes.meta_.set_enum(); enumeration.form = SqlTypeForm::Enum;
  enumeration.labels = &label; enumeration.label_count = 1;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(enumeration, source_owner, type));
  const int64_t size = type->get_serialize_size();
  std::vector<char> bytes(size); int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, type->serialize(bytes.data(), size, position));
  ASSERT_GT(size, 5); ASSERT_EQ(3, bytes[size - 5]); ASSERT_EQ('\0', bytes.back());
  for (int64_t length = 0; length < size; ++length) {
    ObArenaAllocator owner; const SqlType *output = type; int64_t read = 0;
    EXPECT_NE(OB_SUCCESS, SqlType::deserialize(bytes.data(), length, read, owner, output));
    EXPECT_EQ(type, output); EXPECT_EQ(0, read);
  }
  for (int mutation = 0; mutation < 4; ++mutation) {
    std::vector<char> invalid = bytes;
    if (mutation == 0) { invalid.back() = 'x'; } // missing vstr terminator
    else if (mutation == 1) { invalid[size - 5] = 4; } // consumes terminator as data
    else if (mutation == 2) { invalid.assign(10, char(0x80)); } // no bounded version terminator
    else { invalid.assign(10, char(0xff)); invalid.back() = 2; } // overflow in final vi64 byte
    ObArenaAllocator owner; const SqlType *output = type; int64_t read = 0;
    EXPECT_EQ(OB_DESERIALIZE_ERROR,
        SqlType::deserialize(invalid.data(), invalid.size(), read, owner, output));
    EXPECT_EQ(type, output); EXPECT_EQ(0, read);
  }
}

TEST(SqlType, map_dense_and_sparse_vectors_keep_real_shape_in_identity_clone_and_wire)
{
  ObArenaAllocator owner, copy_owner;
  SqlType key, element;
  SqlTypeMeta key_meta, value_meta; key_meta.set_uint64(); value_meta.set_float();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(key_meta, ObAccuracy(), key));
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(value_meta, ObAccuracy(), element));
  SqlTypeSpec dense;
  dense.attributes.meta_.set_type(ObCollectionSQLType); dense.form = SqlTypeForm::Vector;
  dense.element = &element; dense.dimension = 128;
  const SqlType *vector = nullptr, *other_vector = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(dense, owner, vector));
  dense.dimension = 129;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(dense, owner, other_vector));
  EXPECT_FALSE(vector->equals(*other_vector));
  EXPECT_EQ(DatumRepresentation::Reference, vector->canonical_representation());
  SqlTypeSpec map;
  map.attributes.meta_.set_type(ObCollectionSQLType); map.form = SqlTypeForm::Map;
  map.key = &key; map.value = &element;
  const SqlType *map_type = nullptr, *sparse = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(map, owner, map_type));
  map.form = SqlTypeForm::SparseVector;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(map, owner, sparse));
  EXPECT_FALSE(map_type->equals(*sparse)); EXPECT_FALSE(sparse->requires_resource_clone());
  for (const SqlType *type : {vector, map_type, sparse}) {
    const SqlType *clone = nullptr, *decoded = nullptr;
    ASSERT_EQ(OB_SUCCESS, type->clone_to(copy_owner, clone));
    ASSERT_TRUE(type->equals(*clone));
    std::vector<char> bytes(type->get_serialize_size()); int64_t write = 0, read = 0;
    ASSERT_EQ(OB_SUCCESS, type->serialize(bytes.data(), bytes.size(), write));
    ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(bytes.data(), bytes.size(), read, copy_owner, decoded));
    EXPECT_EQ(write, read); EXPECT_TRUE(type->equals(*decoded));
    if (type->form() == SqlTypeForm::Vector) {
      EXPECT_EQ(128, decoded->vector_dimension()); ASSERT_NE(nullptr, decoded->element());
      EXPECT_EQ(ObFloatType, decoded->element()->kind());
    } else {
      ASSERT_NE(nullptr, decoded->key_type()); ASSERT_NE(nullptr, decoded->value_type());
      EXPECT_EQ(ObUInt64Type, decoded->key_type()->kind());
      EXPECT_EQ(ObFloatType, decoded->value_type()->kind());
    }
  }
  const SqlType *unchanged = vector;
  map.key = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(map, owner, unchanged)); EXPECT_EQ(vector, unchanged);
  dense.dimension = 0;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(dense, owner, unchanged));
  EXPECT_EQ(0, unchanged->vector_dimension());
  dense.element = nullptr; const SqlType *zero_dimension = unchanged;
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(dense, owner, unchanged)); EXPECT_EQ(zero_dimension, unchanged);
}

TEST(SqlType, pl_record_schema_dependency_and_declaration_are_distinct)
{
  ObArenaAllocator owner;
  SqlTypeSpec spec = package_record_spec();
  const SqlType *type = nullptr;
  ASSERT_NE(spec.nominal.object_id, spec.pl_declaration.declaration_id);
  ASSERT_TRUE(spec.pl_declaration.valid_with(spec.nominal));
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  EXPECT_EQ(42, type->nominal().object_id);
  EXPECT_EQ(combine_pl_type_id(42, 9), type->pl_declaration().declaration_id);
  EXPECT_EQ(type->pl_declaration().declaration_id,
            static_cast<uint64_t>(type->accuracy().get_accuracy()));
  EXPECT_EQ(DatumRepresentation::Reference, type->canonical_representation());
  EXPECT_TRUE(type->requires_resource_clone());
  for (const SqlPLDeclarationSource source : {SqlPLDeclarationSource::PackageSpec,
      SqlPLDeclarationSource::PackageBody, SqlPLDeclarationSource::Trigger,
      SqlPLDeclarationSource::SchemaType}) {
    SqlTypeSpec schema_spec = spec; schema_spec.pl_declaration.source = source;
    const SqlType *schema_type = nullptr, *decoded = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::create(schema_spec, owner, schema_type));
    std::vector<char> bytes(schema_type->get_serialize_size()); int64_t write = 0, read = 0;
    ASSERT_EQ(OB_SUCCESS, schema_type->serialize(bytes.data(), bytes.size(), write));
    ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(bytes.data(), write, read, owner, decoded));
    EXPECT_EQ(write, read); EXPECT_TRUE(schema_type->equals(*decoded));
    EXPECT_EQ(source, decoded->pl_declaration().source);
    EXPECT_EQ(combine_pl_type_id(42, 9), decoded->pl_declaration().declaration_id);
    EXPECT_EQ(7, decoded->nominal().schema_version);
  }

  // A true table rowtype uses the table ID as its declaration ID. A trigger
  // package is a separate origin and must not inherit this equality rule.
  spec = local_record_spec(42);
  spec.pl_declaration.source = SqlPLDeclarationSource::TableRow;
  spec.pl_declaration.compilation_scope = SqlPLCompilationScope();
  spec.nominal = type->nominal();
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  spec.pl_declaration.declaration_id = 43;
  spec.attributes.set_udt_id(43);
  const SqlType *unchanged = type;
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(spec, owner, unchanged));
  EXPECT_EQ(type, unchanged);
  spec.pl_declaration.source = SqlPLDeclarationSource::Trigger;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, unchanged));
  EXPECT_EQ(42, unchanged->nominal().object_id);
  EXPECT_EQ(43, unchanged->pl_declaration().declaration_id);
}

TEST(SqlType, pl_record_identity_scope_tenant_source_and_version_are_not_interchangeable)
{
  ObArenaAllocator owner;
  const SqlTypeSpec local = local_record_spec(UINT64_C(0xff00000000000009));
  const SqlType *original = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local, owner, original));
  for (int change = 0; change < 4; ++change) {
    SqlTypeSpec changed = local;
    if (change == 0) { ++changed.pl_declaration.compilation_scope.high; }
    else if (change == 1) { ++changed.pl_declaration.compilation_scope.low; }
    else if (change == 2) { ++changed.pl_declaration.tenant_id; }
    else {
      ++changed.pl_declaration.declaration_id;
      changed.attributes.set_udt_id(changed.pl_declaration.declaration_id);
    }
    const SqlType *other = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::create(changed, owner, other));
    EXPECT_FALSE(original->equals(*other));
    EXPECT_FALSE(original->physically_compatible(*other));
  }
  const SqlTypeSpec package = package_record_spec();
  ASSERT_EQ(OB_SUCCESS, SqlType::create(package, owner, original));
  for (int change = 0; change < 4; ++change) {
    SqlTypeSpec changed = package;
    if (change == 0) { changed.pl_declaration.source = SqlPLDeclarationSource::PackageBody; }
    else if (change == 1) { ++changed.nominal.schema_version; }
    else if (change == 2) { ++changed.nominal.object_id; }
    else { ++changed.nominal.tenant_id; ++changed.pl_declaration.tenant_id; }
    const SqlType *other = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::create(changed, owner, other));
    EXPECT_FALSE(original->equals(*other));
    EXPECT_FALSE(original->physically_compatible(*other));
  }
}

TEST(SqlType, local_record_typed_null_clone_and_wire_preserve_imported_origin)
{
  ObArenaAllocator source_owner, clone_owner, decoded_owner;
  SqlTypeSpec child_spec = local_record_spec(10);
  child_spec.pl_declaration.compilation_scope.high = UINT64_C(0x123456789abcdef0);
  child_spec.pl_declaration.compilation_scope.low = UINT64_C(0xfedcba9876543210);
  const SqlType *child = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(child_spec, source_owner, child));
  char name[] = "parent_type";
  SqlTypeMember member;
  member.name = ObString(name); member.type = child; member.nullable = false;
  SqlTypeSpec outer_spec = local_record_spec();
  outer_spec.members = &member; outer_spec.member_count = 1;
  const SqlType *outer = nullptr, *clone = nullptr, *decoded = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(outer_spec, source_owner, outer));
  ObDatum legacy_null; legacy_null.set_null();
  Datum null_value; null_value.set_int(77);
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*outer).import_legacy(legacy_null, null_value));
  EXPECT_TRUE(null_value.is_null());
  EXPECT_TRUE(outer->pl_declaration().is_local());
  ASSERT_EQ(OB_SUCCESS, outer->clone_to(clone_owner, clone));
  ASSERT_TRUE(outer->equals(*clone));
  ASSERT_TRUE(outer->physically_compatible(*clone));
  std::vector<char> bytes(outer->get_serialize_size() + 2, '\x5a');
  int64_t write = 1, read = 1;
  ASSERT_EQ(OB_SUCCESS, outer->serialize(bytes.data(), bytes.size() - 1, write));
  EXPECT_EQ(outer->get_serialize_size() + 1, write);
  ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(bytes.data(), write, read, decoded_owner, decoded));
  EXPECT_EQ(write, read); EXPECT_EQ('\x5a', bytes.back());
  source_owner.reset(); name[0] = 'X'; std::memset(bytes.data(), 0, bytes.size());
  EXPECT_TRUE(clone->equals(*decoded));
  EXPECT_FALSE(decoded->nominal().specified);
  EXPECT_EQ(ObString("parent_type"), decoded->member(0).name);
  EXPECT_FALSE(decoded->member(0).nullable);
  const auto &imported = decoded->member(0).type->pl_declaration();
  EXPECT_EQ(10, imported.declaration_id);
  EXPECT_TRUE(imported.compilation_scope.equals(child_spec.pl_declaration.compilation_scope));
  EXPECT_FALSE(imported.compilation_scope.equals(decoded->pl_declaration().compilation_scope));
  // This is metadata import/clone, not a live parent AST or PL resource clone.
}

TEST(SqlType, pl_record_identity_validation_does_not_publish_invalid_metadata)
{
  SqlType sentinel;
  RejectTypeAllocator reject;
  for (int change = 0; change < 12; ++change) {
    SqlTypeSpec spec = local_record_spec();
    if (change == 0) { spec.pl_declaration = SqlPLDeclarationIdentity(); }
    else if (change == 1) { spec.pl_declaration.source = static_cast<SqlPLDeclarationSource>(255); }
    else if (change == 2) { spec.pl_declaration.compilation_scope = SqlPLCompilationScope(); }
    else if (change == 3) { spec.pl_declaration.tenant_id = 0; }
    else if (change == 4) { spec.pl_declaration.declaration_id = OB_INVALID_ID; }
    else if (change == 5) { spec.nominal = package_record_spec().nominal; }
    else if (change == 6) { spec.attributes.set_udt_id(10); }
    else if (change == 7) { spec.specified = SqlTypeSpec::Scale; }
    else if (change == 8) { spec = package_record_spec(); spec.nominal.specified = false; }
    else if (change == 9) { spec = package_record_spec(); ++spec.nominal.tenant_id; }
    else if (change == 10) { spec = package_record_spec(); spec.nominal.schema_version = -1; }
    else { spec = package_record_spec(); spec.pl_declaration.compilation_scope.low = 2; }
    const SqlType *output = &sentinel;
    EXPECT_EQ(change == 0 ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT,
              SqlType::create(spec, reject, output));
    EXPECT_EQ(&sentinel, output);
  }
  // Generic SQL Record and UDT must not be relabelled as PL declarations.
  SqlTypeSpec sql_record = local_record_spec();
  sql_record.attributes.meta_.set_type(ObUserDefinedSQLType);
  const SqlType *output = &sentinel;
  EXPECT_EQ(OB_INVALID_ARGUMENT, SqlType::create(sql_record, reject, output));
  EXPECT_EQ(&sentinel, output);
}

TEST(SqlType, pl_record_v2_wire_matches_independent_field_fixture)
{
  ObArenaAllocator owner, decoded_owner;
  const SqlType *type = nullptr, *decoded = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local_record_spec(), owner, type));
  ASSERT_EQ(sizeof(V2_LOCAL_RECORD), type->get_serialize_size());
  std::vector<char> bytes(sizeof(V2_LOCAL_RECORD)); int64_t write = 0, read = 0;
  ASSERT_EQ(OB_SUCCESS, type->serialize(bytes.data(), bytes.size(), write));
  EXPECT_EQ(0, std::memcmp(V2_LOCAL_RECORD, bytes.data(), sizeof(V2_LOCAL_RECORD)));
  ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(reinterpret_cast<const char *>(V2_LOCAL_RECORD),
      sizeof(V2_LOCAL_RECORD), read, decoded_owner, decoded));
  EXPECT_EQ(write, read); EXPECT_TRUE(type->equals(*decoded));
  EXPECT_EQ(SqlPLDeclarationSource::Local, decoded->pl_declaration().source);
  EXPECT_EQ(1, decoded->pl_declaration().tenant_id);
  EXPECT_EQ(9, decoded->pl_declaration().declaration_id);
  EXPECT_EQ(1, decoded->pl_declaration().compilation_scope.high);
  EXPECT_EQ(2, decoded->pl_declaration().compilation_scope.low);
}

TEST(SqlType, pl_record_v2_truncation_and_corrupt_identity_leave_output_and_position)
{
  SqlType sentinel;
  for (size_t length = 0; length < sizeof(V2_LOCAL_RECORD); ++length) {
    ObArenaAllocator owner;
    const SqlType *output = &sentinel; int64_t read = 0;
    EXPECT_NE(OB_SUCCESS, SqlType::deserialize(reinterpret_cast<const char *>(V2_LOCAL_RECORD),
        length, read, owner, output));
    EXPECT_EQ(&sentinel, output); EXPECT_EQ(0, read);
  }
  for (int change = 0; change < 7; ++change) {
    std::vector<uint8_t> invalid(V2_LOCAL_RECORD, V2_LOCAL_RECORD + sizeof(V2_LOCAL_RECORD));
    if (change == 0) { invalid[16] = 0; } // v2 may not silently drop its identity
    else if (change == 1) { invalid[16] = 255; }
    else if (change == 2) { invalid[17] = 0; } // absent tenant
    else if (change == 3) { invalid[19] = 0; invalid[20] = 0; }
    else if (change == 4) { invalid[18] = 10; } // raw accuracy remains declaration 9
    else if (change == 5) { invalid[16] = 3; } // schema source with no nominal dependency
    else { invalid[0] = 3; } // unsupported future node version
    ObArenaAllocator owner;
    const SqlType *output = &sentinel; int64_t read = 0;
    EXPECT_NE(OB_SUCCESS, SqlType::deserialize(reinterpret_cast<const char *>(invalid.data()),
        invalid.size(), read, owner, output));
    EXPECT_EQ(&sentinel, output); EXPECT_EQ(0, read);
  }
  ObArenaAllocator source;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local_record_spec(), source, type));
  std::vector<char> bytes(type->get_serialize_size(), '\x5a'); int64_t position = 1;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, type->serialize(bytes.data(), bytes.size(), position));
  EXPECT_EQ(1, position);
  for (char byte : bytes) { EXPECT_EQ('\x5a', byte); }
}

TEST(SqlType, pl_record_clone_and_decode_partial_allocation_failure_do_not_publish)
{
  ObArenaAllocator source;
  const SqlType *child = nullptr, *record = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(local_record_spec(10), source, child));
  SqlTypeMember member; member.name = ObString("child"); member.type = child;
  SqlTypeSpec spec = local_record_spec(); spec.members = &member; spec.member_count = 1;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, source, record));
  std::vector<char> bytes(record->get_serialize_size()); int64_t write = 0;
  ASSERT_EQ(OB_SUCCESS, record->serialize(bytes.data(), bytes.size(), write));
  for (int operation = 0; operation < 3; ++operation) {
    bool completed = false;
    for (int64_t limit = 0; limit < 16 && !completed; ++limit) {
      LimitedTypeAllocator owner(limit);
      const SqlType *output = record; int64_t read = 0;
      const int ret = operation == 0 ? SqlType::create(spec, owner, output)
          : operation == 1 ? record->clone_to(owner, output)
          : SqlType::deserialize(bytes.data(), bytes.size(), read, owner, output);
      if (ret == OB_SUCCESS) {
        completed = true;
        ASSERT_NE(record, output); EXPECT_TRUE(record->equals(*output));
        if (operation == 2) { EXPECT_EQ(write, read); }
      } else {
        EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret);
        EXPECT_EQ(record, output); EXPECT_EQ(0, read);
      }
    }
    EXPECT_TRUE(completed);
  }
}

TEST(SqlType, historical_v1_nonpl_record_and_udt_keep_exact_wire_bytes)
{
  for (bool nominal : {false, true}) {
    std::vector<uint8_t> fixture(V1_SQL_RECORD, V1_SQL_RECORD + sizeof(V1_SQL_RECORD));
    if (nominal) {
      fixture[9] = 5; // UserDefined form
      fixture[12] = 1; fixture[13] = 1; fixture[14] = 42; fixture[15] = 7;
    }
    ObArenaAllocator owner, clone_owner;
    const SqlType *decoded = nullptr, *clone = nullptr; int64_t read = 0;
    ASSERT_EQ(OB_SUCCESS, SqlType::deserialize(reinterpret_cast<const char *>(fixture.data()),
        fixture.size(), read, owner, decoded));
    EXPECT_EQ(fixture.size(), read);
    EXPECT_EQ(ObUserDefinedSQLType, decoded->kind());
    EXPECT_EQ(nominal ? SqlTypeForm::UserDefined : SqlTypeForm::Record, decoded->form());
    EXPECT_FALSE(decoded->pl_declaration().is_specified());
    EXPECT_EQ(nominal, decoded->nominal().specified);
    ASSERT_EQ(OB_SUCCESS, decoded->clone_to(clone_owner, clone));
    EXPECT_TRUE(decoded->equals(*clone)); EXPECT_TRUE(decoded->physically_compatible(*clone));
    ASSERT_EQ(fixture.size(), decoded->get_serialize_size());
    std::vector<char> encoded(fixture.size()); int64_t write = 0;
    ASSERT_EQ(OB_SUCCESS, decoded->serialize(encoded.data(), encoded.size(), write));
    EXPECT_EQ(read, write); EXPECT_EQ(0, std::memcmp(fixture.data(), encoded.data(), encoded.size()));
  }
}

TEST(SqlType, historical_v1_pl_record_without_origin_is_not_guessed)
{
  std::vector<uint8_t> fixture(V1_SQL_RECORD, V1_SQL_RECORD + sizeof(V1_SQL_RECORD));
  fixture[1] = 25; fixture[2] = 7; fixture[4] = 1; fixture[5] = 9;
  ObArenaAllocator owner; SqlType sentinel;
  const SqlType *output = &sentinel; int64_t read = 0;
  EXPECT_EQ(OB_NOT_SUPPORTED, SqlType::deserialize(reinterpret_cast<const char *>(fixture.data()),
      fixture.size(), read, owner, output));
  EXPECT_EQ(&sentinel, output); EXPECT_EQ(0, read);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
