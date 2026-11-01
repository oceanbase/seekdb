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

#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "common/datum/datum_parameter_codec.h"
#include "lib/allocator/ob_allocator.h"
#include "share/schema/ob_column_schema.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace {
class SchemaValueAllocator final : public ObIAllocator
{
public:
  void *alloc(int64_t bytes) override
  {
    ++calls_;
    requested_.push_back(bytes);
    return fail_ ? nullptr : arena_.alloc(bytes);
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *memory) override { arena_.free(memory); }
  ObArenaAllocator arena_;
  bool fail_ = false;
  int64_t calls_ = 0;
  std::vector<int64_t> requested_;
};

std::string schema_bytes(const ObColumnSchemaV2 &column)
{
  std::string result(column.get_serialize_size(), '\0');
  int64_t position = 0;
  EXPECT_EQ(OB_SUCCESS, column.serialize(&result[0], result.size(), position));
  EXPECT_EQ(result.size(), position);
  return result;
}

std::string legacy_bytes(const DatumValue &value)
{
  int64_t bytes = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::native_value_size(value.datum_, value.meta_, bytes));
  std::string result(bytes, '\0');
  int64_t position = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_native_value(value.datum_, value.meta_,
      &result[0], result.size(), position));
  EXPECT_EQ(result.size(), position);
  return result;
}

std::string native_bytes(const Datum &value, const SqlTypeMeta &meta)
{
  int64_t bytes = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::schema_native_value_size(value, meta, bytes));
  std::string result(bytes, '\0');
  int64_t position = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_schema_native_value(value, meta,
      &result[0], result.size(), position));
  EXPECT_EQ(result.size(), position);
  return result;
}
}

TEST(NativeSchemaDefaultValues, integer_defaults_are_inline_without_schema_scalar_allocations)
{
  SchemaValueAllocator owner;
  ObColumnSchemaV2 column(&owner);
  const int64_t calls_before = owner.calls_;
  owner.fail_ = true; // Inline capture must not ask for a legacy scalar payload.
  for (const int64_t number : {INT64_MIN, int64_t(0), INT64_MAX}) {
    ObArenaAllocator input_owner;
    DatumValue source;
    ASSERT_EQ(OB_SUCCESS, source.set_int(number, input_owner));
    const std::string encoded = legacy_bytes(source);
    ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(source));
    ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(source, false));
    source.datum_.set_int(0x5a5a5a5a5a5a5a5aLL);
    input_owner.reset();
    EXPECT_EQ(ObIntType, column.get_orig_default_meta().get_type());
    EXPECT_EQ(number, column.get_orig_default_datum().get_int());
    EXPECT_EQ(number, column.get_cur_default_datum().get_int());
    EXPECT_EQ(0, column.get_orig_default_datum().length());
    EXPECT_EQ(0, column.get_cur_default_datum().length());
    Datum decoded, cloned;
    SqlTypeMeta decoded_meta;
    int64_t position = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_schema_native_value(
        encoded.data(), encoded.size(), position, owner, decoded, decoded_meta));
    EXPECT_EQ(encoded.size(), position); EXPECT_EQ(number, decoded.get_int());
    EXPECT_EQ(0, decoded.length()); EXPECT_EQ(ObIntType, decoded_meta.get_type());
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::clone_schema_native_value(decoded, decoded_meta, owner, cloned));
    EXPECT_EQ(number, cloned.get_int()); EXPECT_EQ(0, cloned.length());
  }
  ObArenaAllocator input_owner;
  DatumValue unsigned_source;
  ASSERT_EQ(OB_SUCCESS, unsigned_source.set_uint(ObUInt64Type, UINT64_MAX, input_owner));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(unsigned_source, false));
  EXPECT_EQ(UINT64_MAX, column.get_cur_default_datum().get_uint());
  EXPECT_EQ(ObUInt64Type, column.get_cur_default_meta().get_type());
  EXPECT_EQ(INT64_MAX, column.get_orig_default_datum().get_int());
  EXPECT_EQ(calls_before, owner.calls_);
}

TEST(NativeSchemaDefaultValues, reference_defaults_own_both_slots_and_survive_source_and_wire_poison)
{
  ObArenaAllocator owner;
  ObColumnSchemaV2 assigned(&owner), decoded(&owner);
  const char text[] = {'a', '\0', 'b', 'c'};
  const ObString expected(sizeof(text), text);
  std::string original_wire;
  {
    ObArenaAllocator source_owner;
    ObColumnSchemaV2 source(&source_owner);
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, value.set_varchar(expected, source_owner));
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, source.set_orig_default_value(value));
    ASSERT_EQ(OB_SUCCESS, source.set_cur_default_value(value, true));
    EXPECT_NE(value.datum_.get_reference(), source.get_cur_default_datum().get_reference());
    EXPECT_NE(source.get_orig_default_datum().get_reference(), source.get_cur_default_datum().get_reference());
    ASSERT_EQ(OB_SUCCESS, assigned.assign(source));
    EXPECT_NE(source.get_cur_default_datum().get_reference(), assigned.get_cur_default_datum().get_reference());
    original_wire = schema_bytes(source);
    std::string input = original_wire;
    int64_t position = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(input.data(), input.size(), position));
    EXPECT_EQ(input.size(), position);
    MEMSET(&input[0], 0x5a, input.size());
    MEMSET(const_cast<void *>(value.datum_.get_reference()), 0x5a, sizeof(text));
    MEMSET(const_cast<void *>(source.get_orig_default_datum().get_reference()), 0x5a, sizeof(text));
    MEMSET(const_cast<void *>(source.get_cur_default_datum().get_reference()), 0x5a, sizeof(text));
  }
  for (const ObColumnSchemaV2 *column : {&assigned, &decoded}) {
    EXPECT_EQ(expected, column->get_orig_default_datum().get_string());
    EXPECT_EQ(expected, column->get_cur_default_datum().get_string());
    EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, column->get_cur_default_meta().get_collation_type());
    EXPECT_TRUE(column->is_default_expr_v2_column());
    EXPECT_EQ(original_wire, schema_bytes(*column));
  }
}

TEST(NativeSchemaDefaultValues, decimal_historical_width_and_scale_do_not_follow_column_precision)
{
  for (const int32_t width : {0, 4, 8, 16, 32, 64}) {
    SCOPED_TRACE(width);
    ObArenaAllocator owner, input_owner;
    ObColumnSchemaV2 column(&owner), decoded(&owner);
    column.set_data_type(ObDecimalIntType);
    column.set_data_precision(18); column.set_data_scale(2);
    alignas(8) unsigned char coefficient[64];
    for (int32_t i = 0; i < 64; ++i) { coefficient[i] = static_cast<unsigned char>(0x80 + i); }
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, value.set_decimal_int(width, 5,
        reinterpret_cast<const ObDecimalInt *>(coefficient), input_owner));
    ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(value));
    ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(value, false));
    EXPECT_EQ(width, column.get_orig_default_datum().length());
    EXPECT_EQ(5, column.get_orig_default_meta().get_scale());
    // Captured historical meta bytes: DecimalInt / numeric / binary / scale5;
    // actual width and scale are repeated by the value wire, then raw coefficient.
    std::string expected("\x27\x05\x3f\x05", 4);
    expected.push_back(static_cast<char>(width)); expected.push_back(5);
    expected.append(reinterpret_cast<const char *>(coefficient), width);
    EXPECT_EQ(expected, legacy_bytes(value));
    EXPECT_EQ(expected, native_bytes(column.get_orig_default_datum(), column.get_orig_default_meta()));
    alignas(8) char scratch[64]; DatumValue exported;
    ASSERT_EQ(OB_SUCCESS, column.export_orig_default_value(exported, scratch, sizeof(scratch)));
    EXPECT_EQ(width, exported.datum_.length());
    EXPECT_EQ(5, exported.get_scale());
    if (width > 0) { EXPECT_EQ(0, MEMCMP(coefficient, exported.datum_.get_reference(), width)); }
    const std::string bytes = schema_bytes(column); int64_t position = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(bytes.data(), bytes.size(), position));
    EXPECT_EQ(18, decoded.get_data_precision()); EXPECT_EQ(2, decoded.get_data_scale());
    EXPECT_EQ(width, decoded.get_cur_default_datum().length());
    EXPECT_EQ(expected, native_bytes(decoded.get_cur_default_datum(), decoded.get_cur_default_meta()));
    EXPECT_EQ(bytes, schema_bytes(decoded));
  }
}

TEST(NativeSchemaDefaultValues, null_now_and_expression_defaults_keep_distinct_states)
{
  ObArenaAllocator owner, inputs;
  ObColumnSchemaV2 column(&owner), decoded(&owner);
  column.set_data_type(ObTimestampType);
  column.set_nullable(false);
  EXPECT_TRUE(column.get_cur_default_datum().is_null());
  EXPECT_FALSE(column.get_cur_default_datum().is_nop());
  DatumParameter typed_null;
  SqlTypeMeta declared; declared.set_timestamp();
  typed_null.set_type_meta(declared); typed_null.set_param_meta(declared); typed_null.set_null();
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(typed_null));
  EXPECT_EQ(ObNullType, column.get_orig_default_meta().get_type());
  for (const ObScale scale : {ObScale(0), ObScale(6)}) {
    DatumValue now;
    ASSERT_EQ(OB_SUCCESS, now.set_extend(ObActionFlag::OP_DEFAULT_NOW_FLAG, 0, inputs));
    now.meta_.set_scale(scale);
    ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(now, false));
    EXPECT_EQ(ObExtendType, column.get_cur_default_meta().get_type());
    EXPECT_EQ(scale, column.get_cur_default_meta().get_scale());
    EXPECT_EQ(int64_t(ObActionFlag::OP_DEFAULT_NOW_FLAG), column.get_cur_default_datum().get_ext());
    EXPECT_FALSE(column.get_cur_default_datum().is_nop());
    EXPECT_FALSE(column.is_default_expr_v2_column());
    const std::string bytes = schema_bytes(column); int64_t position = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(bytes.data(), bytes.size(), position));
    EXPECT_EQ(scale, decoded.get_cur_default_meta().get_scale());
    EXPECT_EQ(int64_t(ObActionFlag::OP_DEFAULT_NOW_FLAG), decoded.get_cur_default_datum().get_ext());
    EXPECT_EQ(bytes, schema_bytes(decoded));
  }
  DatumValue expression, evaluated;
  ASSERT_EQ(OB_SUCCESS, expression.set_varchar(ObString("7 + 8"), inputs));
  expression.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, evaluated.set_int(15, inputs));
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(evaluated));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(expression, true));
  EXPECT_EQ(15, column.get_orig_default_datum().get_int());
  EXPECT_EQ(ObString("7 + 8"), column.get_cur_default_datum().get_string());
  EXPECT_TRUE(column.is_default_expr_v2_column());
  const std::string expression_wire = schema_bytes(column);
  ObColumnSchemaV2 expression_decoded(&owner); int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, expression_decoded.deserialize(expression_wire.data(), expression_wire.size(), position));
  EXPECT_EQ(15, expression_decoded.get_orig_default_datum().get_int());
  EXPECT_EQ(ObString("7 + 8"), expression_decoded.get_cur_default_datum().get_string());
  EXPECT_TRUE(expression_decoded.is_default_expr_v2_column());
  EXPECT_EQ(expression_wire, schema_bytes(expression_decoded));
}

TEST(NativeSchemaDefaultValues, enum_values_can_move_physically_before_labels_are_prepared)
{
  ObArenaAllocator owner;
  ObColumnSchemaV2 assigned(&owner), decoded(&owner);
  {
    ObArenaAllocator source_owner;
    ObColumnSchemaV2 source(&source_owner);
    source.set_data_type(ObEnumType); source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    source.set_charset_type(CHARSET_UTF8MB4);
    DatumValue value; ASSERT_EQ(OB_SUCCESS, value.set_enum(2, source_owner));
    ASSERT_EQ(OB_SUCCESS, source.set_orig_default_value(value));
    ASSERT_EQ(0, source.get_extended_type_info().count());
    EXPECT_EQ(2, source.get_orig_default_datum().get_enum());
    char red[] = "red", blue[] = "blue";
    ObSEArray<ObString, 2> labels;
    ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString(red)));
    ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString(blue)));
    ASSERT_EQ(OB_SUCCESS, source.set_extended_type_info(labels));
    MEMSET(red, 'x', 3); MEMSET(blue, 'x', 4);
    ASSERT_EQ(OB_SUCCESS, source.set_cur_default_value(value, false));
    ASSERT_EQ(OB_SUCCESS, assigned.assign(source));
    EXPECT_NE(source.get_extended_type_info().at(1).ptr(), assigned.get_extended_type_info().at(1).ptr());
    std::string bytes = schema_bytes(source); int64_t position = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(bytes.data(), bytes.size(), position));
    MEMSET(&bytes[0], 0x5a, bytes.size());
  }
  for (const ObColumnSchemaV2 *column : {&assigned, &decoded}) {
    ASSERT_EQ(2, column->get_extended_type_info().count());
    EXPECT_EQ(ObString("red"), column->get_extended_type_info().at(0));
    EXPECT_EQ(ObString("blue"), column->get_extended_type_info().at(1));
    EXPECT_EQ(ObEnumType, column->get_cur_default_meta().get_type());
    EXPECT_EQ(2, column->get_cur_default_datum().get_enum());
    alignas(8) char scratch[64]; DatumValue old_boundary;
    ASSERT_EQ(OB_SUCCESS, column->export_cur_default_value(old_boundary, scratch, sizeof(scratch)));
    EXPECT_EQ(ObEnumType, old_boundary.get_type()); EXPECT_EQ(2, old_boundary.get_enum());
  }
}

TEST(NativeSchemaDefaultValues, malformed_import_and_short_export_do_not_publish_values_or_flags)
{
  ObArenaAllocator owner, inputs;
  ObColumnSchemaV2 column(&owner);
  DatumValue initial; ASSERT_EQ(OB_SUCCESS, initial.set_int(17, inputs));
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(initial));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(initial, false));
  const std::string before = schema_bytes(column);
  DatumValue malformed; malformed.meta_.set_int();
  const char malformed_payload = 0;
  ASSERT_EQ(OB_SUCCESS, malformed.datum_.set_reference(&malformed_payload, 1));
  EXPECT_EQ(OB_INVALID_ARGUMENT, column.set_orig_default_value(malformed));
  EXPECT_EQ(OB_INVALID_ARGUMENT, column.set_cur_default_value(malformed, true));
  EXPECT_FALSE(column.is_default_expr_v2_column());
  EXPECT_EQ(before, schema_bytes(column));
  Datum output; output.set_int(23);
  ObDatum malformed_legacy; malformed_legacy.ptr_ = nullptr; malformed_legacy.pack_ = sizeof(int64_t);
  EXPECT_EQ(OB_INVALID_ARGUMENT, DatumParameterCodec::import_schema_native_value(
      malformed_legacy, malformed.meta_, owner, output));
  EXPECT_EQ(23, output.get_int()); EXPECT_EQ(0, output.length());
  alignas(8) char existing_payload[8], scratch[7];
  ObDatum old_output; old_output.ptr_ = existing_payload; old_output.set_int(42);
  const char *const old_address = old_output.ptr_; const uint32_t old_pack = old_output.pack_;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, DatumParameterCodec::export_schema_legacy_value(
      column.get_cur_default_datum(), column.get_cur_default_meta(), old_output, scratch, sizeof(scratch)));
  EXPECT_EQ(old_address, old_output.ptr_); EXPECT_EQ(old_pack, old_output.pack_); EXPECT_EQ(42, old_output.get_int());
  int64_t position = 1, full_size = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::schema_native_value_size(
      column.get_cur_default_datum(), column.get_cur_default_meta(), full_size));
  std::string short_buffer(full_size, 'x');
  EXPECT_EQ(OB_SIZE_OVERFLOW, DatumParameterCodec::encode_schema_native_value(
      column.get_cur_default_datum(), column.get_cur_default_meta(), &short_buffer[0], short_buffer.size(), position));
  EXPECT_EQ(1, position); // Legacy codec may write partial bytes; cursor must not publish.
}

TEST(NativeSchemaDefaultValues, native_decode_and_clone_failures_preserve_output_metadata_and_cursor)
{
  ObArenaAllocator inputs;
  DatumValue text; ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString("owned string"), inputs));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  const std::string encoded = legacy_bytes(text);
  SchemaValueAllocator owner;
  Datum output; output.set_int(23);
  SqlTypeMeta output_meta; output_meta.set_int();
  const SqlTypeMeta initial_meta = output_meta;
  int64_t position = 0;
  owner.fail_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterCodec::decode_schema_native_value(
      encoded.data(), encoded.size(), position, owner, output, output_meta));
  EXPECT_EQ(0, position); EXPECT_EQ(23, output.get_int());
  EXPECT_EQ(initial_meta, output_meta); EXPECT_EQ(initial_meta.get_scale(), output_meta.get_scale());
  owner.fail_ = false;
  EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_schema_native_value(
      encoded.data(), encoded.size() - 6, position, owner, output, output_meta));
  EXPECT_EQ(0, position); EXPECT_EQ(23, output.get_int()); EXPECT_EQ(initial_meta, output_meta);
  EXPECT_EQ(initial_meta.get_scale(), output_meta.get_scale());
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_schema_native_value(
      encoded.data(), encoded.size(), position, owner, output, output_meta));
  EXPECT_EQ(encoded.size(), position); EXPECT_EQ(ObString("owned string"), output.get_string());
  EXPECT_EQ(text.meta_, output_meta);
  Datum cloned; cloned.set_int(99);
  owner.fail_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterCodec::clone_schema_native_value(output, output_meta, owner, cloned));
  EXPECT_EQ(99, cloned.get_int());
  owner.fail_ = false;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::clone_schema_native_value(output, output_meta, owner, cloned));
  EXPECT_NE(output.get_reference(), cloned.get_reference());
  MEMSET(const_cast<void *>(output.get_reference()), 0x5a, output.length());
  EXPECT_EQ(ObString("owned string"), cloned.get_string());
}

TEST(NativeSchemaDefaultValues, explicit_legacy_copy_retains_in_the_callers_owner)
{
  ObArenaAllocator retained_owner;
  DatumValue retained;
  {
    ObArenaAllocator source_owner;
    ObColumnSchemaV2 source(&source_owner);
    DatumValue input; ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString("retained"), source_owner));
    input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, source.set_cur_default_value(input, false));
    ASSERT_EQ(OB_SUCCESS, source.copy_cur_default_value(retained, retained_owner));
    EXPECT_NE(source.get_cur_default_datum().get_reference(), retained.datum_.get_reference());
  }
  EXPECT_EQ(ObString("retained"), retained.get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, retained.get_collation_type());
  SchemaValueAllocator failing_owner;
  failing_owner.fail_ = true;
  ObColumnSchemaV2 source(&retained_owner);
  ASSERT_EQ(OB_SUCCESS, source.set_cur_default_value(retained, false));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, source.copy_cur_default_value(retained, failing_owner));
  EXPECT_EQ(ObString("retained"), retained.get_string());
}
