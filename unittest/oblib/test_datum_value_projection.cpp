/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/datum/datum_value_projection.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;

namespace {
class ProjectionAllocator final : public ObIAllocator
{
public:
  void *alloc(int64_t bytes) override
  { ++calls; last = reject ? nullptr : arena.alloc(bytes); return last; }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *pointer) override { arena.free(pointer); }
  ObArenaAllocator arena;
  int64_t calls = 0;
  bool reject = false;
  void *last = nullptr;
};
}

TEST(DatumValueProjection, decimal_int_accepts_negative_scale)
{
  int32_t coefficient = 1234;
  ObDatum datum;
  datum.set_decimal_int_shallow(reinterpret_cast<ObDecimalInt *>(&coefficient), sizeof(coefficient));
  SqlTypeMeta meta;
  meta.set_decimal_int(-9);
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, project_datum_value_view(
      datum, meta, OBJ_DATUM_DECIMALINT, SqlTypeMeta(), output));
  EXPECT_EQ(-9, output.get_scale());
  EXPECT_EQ(sizeof(coefficient), output.datum_.length());
  EXPECT_EQ(coefficient, *reinterpret_cast<const int32_t *>(output.datum_.get_reference()));
}

TEST(DatumValueProjection, decimal_int_rejects_unknown_scale_without_overwriting_output)
{
  int32_t coefficient = 1234;
  ObDatum datum;
  datum.set_decimal_int_shallow(reinterpret_cast<ObDecimalInt *>(&coefficient), sizeof(coefficient));
  SqlTypeMeta meta;
  meta.set_decimal_int(SCALE_UNKNOWN_YET);
  Datum borrowed; ASSERT_EQ(OB_SUCCESS, borrowed.set_reference(&coefficient, sizeof(coefficient)));
  DatumValue output(borrowed, SqlTypeMeta());
  SqlTypeMeta previous_meta = output.meta_;
  EXPECT_EQ(OB_ERR_UNEXPECTED, project_datum_value_view(
      datum, meta, OBJ_DATUM_DECIMALINT, SqlTypeMeta(), output));
  EXPECT_EQ(previous_meta, output.meta_);
  EXPECT_EQ(reinterpret_cast<char *>(&coefficient), output.datum_.get_reference());
}

TEST(DatumValueProjection, native_scalar_and_reference_views_borrow_without_allocation)
{
  ProjectionAllocator owner;
  SqlTypeMeta integer_meta; integer_meta.set_int();
  SqlType integer;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(integer_meta, ObAccuracy(), integer));
  Datum source; source.set_int(INT64_MIN);
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(source, integer, integer_meta,
      OBJ_DATUM_8BYTE_DATA, SqlTypeMeta(), output, owner));
  EXPECT_EQ(0, owner.calls); EXPECT_EQ(INT64_MIN, output.get_int());
  source.set_int(77);
  EXPECT_EQ(INT64_MIN, output.get_int()); EXPECT_EQ(0, output.datum_.length());

  SqlTypeMeta string_meta; string_meta.set_varchar();
  string_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  string_meta.set_collation_level(CS_LEVEL_IMPLICIT);
  SqlType string_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(string_meta, ObAccuracy(), string_type));
  char text[] = "abc";
  ASSERT_EQ(OB_SUCCESS, source.set_string(text, 3));
  ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(source, string_type, string_meta,
      OBJ_DATUM_STRING, SqlTypeMeta(), output, owner));
  EXPECT_EQ(0, owner.calls); EXPECT_EQ(text, output.datum_.get_reference());
  text[0] = 'z'; EXPECT_EQ('z', output.get_string().ptr()[0]);
}

TEST(DatumValueProjection, native_normalized_decimal_and_marker_use_stable_owner_bytes)
{
  ProjectionAllocator owner;
  SqlTypeMeta meta; meta.set_decimal_int(2);
  SqlType type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(40, 2), type));
  int64_t coefficient = -1234;
  Datum source;
  ASSERT_EQ(OB_SUCCESS, source.set_reference(&coefficient, sizeof(coefficient)));
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(source, type, meta,
      OBJ_DATUM_DECIMALINT, SqlTypeMeta(), output, owner));
  ASSERT_EQ(1, owner.calls); ASSERT_EQ(32, output.datum_.length());
  EXPECT_EQ(owner.last, output.datum_.get_reference());
  coefficient = 0; source.set_null();
  // Source and scratch reuse must not alter the normalized legacy coefficient.
  volatile char poison[128];
  for (uint32_t i = 0; i < sizeof(poison); ++i) { poison[i] = 0x5a; }
  int256_t copied;
  MEMCPY(&copied, output.datum_.get_reference(), sizeof(copied));
  EXPECT_EQ(int256_t(-1234), copied);

  SqlTypeMeta integer_meta; integer_meta.set_int();
  SqlType integer;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(integer_meta, ObAccuracy(), integer));
  source.set_max();
  SqlTypeMeta extended; extended.set_ext();
  ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(source, integer, extended,
      OBJ_DATUM_EXTEND, SqlTypeMeta(), output, owner));
  EXPECT_EQ(2, owner.calls); EXPECT_EQ(0, output.datum_.length());
  source.set_int(0); EXPECT_TRUE(output.datum_.is_max());
}

TEST(DatumValueProjection, native_failure_preserves_destination_and_null_projection_contract)
{
  ProjectionAllocator owner;
  SqlTypeMeta meta; meta.set_decimal_int(-2);
  SqlType type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(40, -2), type));
  int64_t coefficient = 42;
  Datum source;
  ASSERT_EQ(OB_SUCCESS, source.set_reference(&coefficient, sizeof(coefficient)));
  int64_t sentinel = 91;
  ObDatum old; old.ptr_ = reinterpret_cast<const char *>(&sentinel); old.pack_ = 8;
  SqlTypeMeta previous; previous.set_int();
  previous.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  previous.set_collation_level(CS_LEVEL_IMPLICIT);
  DatumValue output; ASSERT_EQ(OB_SUCCESS, output.assign_legacy_borrowed(old, previous));
  const Datum before = output.datum_;
  owner.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, project_native_datum_value_view(source, type, meta,
      OBJ_DATUM_DECIMALINT, previous, output, owner));
  EXPECT_EQ(previous, output.meta_); EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));
  EXPECT_EQ(91, output.get_int());
  owner.reject = false;
  EXPECT_EQ(OB_ERR_UNEXPECTED, project_native_datum_value_view(source, type, meta,
      OBJ_DATUM_MAPPING_MAX, previous, output, owner));
  EXPECT_EQ(previous, output.meta_); EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));
  ASSERT_EQ(OB_SUCCESS, source.set_reference(&coefficient, 7));
  const int64_t calls = owner.calls;
  EXPECT_EQ(OB_INVALID_ARGUMENT, project_native_datum_value_view(source, type, meta,
      OBJ_DATUM_DECIMALINT, previous, output, owner));
  EXPECT_EQ(calls, owner.calls); EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));

  source.set_null();
  SqlTypeMeta null_meta; null_meta.set_null();
  null_meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  null_meta.set_collation_level(CS_LEVEL_COERCIBLE);
  ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(source, type, null_meta,
      OBJ_DATUM_MAPPING_MAX, previous, output, owner, true));
  EXPECT_TRUE(output.is_null()); EXPECT_EQ(null_meta, output.meta_);
  EXPECT_EQ(calls, owner.calls);
}


TEST(DatumValueProjection, inline_extended_projection_keeps_transformed_value_and_owns_only_needed_bytes)
{
  ObArenaAllocator metadata_owner;
  SqlTypeSpec spec; spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(1);
  spec.attributes.set_udt_id(908); spec.form = SqlTypeForm::Record;
  spec.pl_declaration.source = SqlPLDeclarationSource::Local;
  spec.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID; spec.pl_declaration.declaration_id = 908;
  spec.pl_declaration.compilation_scope.low = 0x8642;
  const SqlType *type = nullptr; ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, metadata_owner, type));
  ProjectionAllocator owner; owner.reject = true;
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    Datum input; input.set_inline_extended(65536, 1, -17, CS_LEVEL_EXPLICIT, CS_TYPE_UTF8MB4_BIN, legacy16);
    DatumValue output; ASSERT_EQ(OB_SUCCESS, output.set_int(99, owner));
    ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(input, *type, type->legacy_meta(),
        OBJ_DATUM_EXTEND, SqlTypeMeta(), output, owner));
    EXPECT_TRUE(output.datum_.is_inline_extended()); EXPECT_EQ(65536, output.get_ext());
    EXPECT_EQ(-17, output.get_val_len()); EXPECT_EQ(CS_LEVEL_EXPLICIT, output.get_collation_level());
    EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, output.get_collation_type()); EXPECT_EQ(0, owner.calls);
    // The mapping may intentionally discard or reinterpret the carrier.
    ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(input, *type, type->legacy_meta(),
        OBJ_DATUM_NULL, SqlTypeMeta(), output, owner));
    EXPECT_TRUE(output.is_null()); EXPECT_EQ(0, owner.calls);
    SqlTypeMeta string_meta; string_meta.set_varbinary();
    const Datum previous = output.datum_; const SqlTypeMeta previous_meta = output.meta_;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, project_native_datum_value_view(input, *type, string_meta,
        OBJ_DATUM_STRING, SqlTypeMeta(), output, owner));
    EXPECT_EQ(0, MEMCMP(&previous, &output.datum_, sizeof(Datum))); EXPECT_EQ(previous_meta, output.meta_);
    owner.reject = false;
    char expected[16]; ASSERT_EQ(OB_SUCCESS, input.write_inline_extended_payload(expected, sizeof(expected)));
    ASSERT_EQ(OB_SUCCESS, project_native_datum_value_view(input, *type, string_meta,
        OBJ_DATUM_STRING, SqlTypeMeta(), output, owner));
    input.set_null();
    EXPECT_EQ(legacy16 ? 16U : 15U, output.datum_.length());
    EXPECT_EQ(0, MEMCMP(expected, output.datum_.get_reference(), output.datum_.length()));
    owner.reject = true; owner.calls = 0;
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
