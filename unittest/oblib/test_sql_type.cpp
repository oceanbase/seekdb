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
#include <gtest/gtest.h>
#include <type_traits>

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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
