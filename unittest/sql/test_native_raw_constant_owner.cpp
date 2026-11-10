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

#include "common/datum/datum_parameter_codec.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
class NativeRawConstantOwner : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
  }
};

class RawOwnerAllocator : public ObIAllocator
{
public:
  RawOwnerAllocator() : arena_("RawOwnerTest"), calls_(0), fail_at_(0) {}
  void *alloc(const int64_t size) override
  {
    ++calls_;
    return fail_at_ > 0 && calls_ >= fail_at_ ? nullptr : arena_.alloc(size);
  }
  void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override { arena_.free(ptr); }
  void fail_after(int64_t successful_calls) { fail_at_ = calls_ + successful_calls + 1; }
  void allow() { fail_at_ = 0; }
  int64_t calls() const { return calls_; }
private:
  ObArenaAllocator arena_;
  int64_t calls_;
  int64_t fail_at_;
};

void same_meta(const SqlTypeMeta &left, const SqlTypeMeta &right)
{
  // SqlTypeMeta::operator== deliberately omits scale; the raw owner must not.
  EXPECT_EQ(0, MEMCMP(&left, &right, sizeof(SqlTypeMeta)));
}

std::string native_text(const ObConstRawExpr &node)
{
  char buffer[512];
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, node.get_name_internal(buffer, sizeof(buffer), pos, EXPLAIN_BASIC));
  return std::string(buffer, pos);
}

DatumValue legacy_value(const ObDatum &physical, const SqlTypeMeta &meta)
{
  DatumValue value;
  EXPECT_EQ(OB_SUCCESS, value.assign_legacy_borrowed(physical, meta));
  return value;
}

std::string legacy_text(const DatumValue &value)
{
  char buffer[512];
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, value.print_sql_literal(buffer, sizeof(buffer), pos));
  return std::string(buffer, pos);
}

DatumValue string_view(char *text, int64_t length, ObCollationType collation = CS_TYPE_UTF8MB4_BIN)
{
  DatumValue value;
  value.meta_.set_varchar();
  value.meta_.set_collation_type(collation);
  value.meta_.set_collation_level(CS_LEVEL_COERCIBLE);
  value.datum_.set_string(text, length);
  return value;
}

static_assert(std::is_same<decltype(std::declval<const ObConstRawExpr &>().get_value_datum()),
                           const Datum &>::value, "raw values are native and read only");
static_assert(std::is_same<decltype(std::declval<const ObConstRawExpr &>().get_bound_param_datum()),
                           const Datum &>::value, "bound raw params have no legacy shadow");
} // namespace

TEST_F(NativeRawConstantOwner, signed_unsigned_and_unknown_are_inline_without_payload_allocations)
{
  RawOwnerAllocator owner;
  ObConstRawExpr node(owner);
  ObRawExprFactory factory(owner);
  ObPLExprCopier copier(factory);
  const int64_t initial_calls = owner.calls();
  owner.fail_after(0);
  for (int64_t expected : {INT64_MIN, int64_t(-1), int64_t(0), INT64_MAX}) {
    int64_t source = expected;
    ObDatum legacy;
    legacy.ptr_ = reinterpret_cast<const char *>(&source);
    legacy.set_int(source);
    SqlTypeMeta meta;
    meta.set_int();
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(legacy_value(legacy, meta)));
    source = 17;
    EXPECT_EQ(expected, node.get_value_datum().get_int());
    EXPECT_EQ(expected, node.get_bound_param_datum().get_int());
    EXPECT_EQ(0, node.get_value_datum().length());
    ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
    EXPECT_EQ(expected, node.get_value_datum().get_int());
    EXPECT_EQ(std::to_string(expected), native_text(node));
  }
  Datum value;
  SqlTypeMeta meta;
  meta.set_uint64();
  value.set_uint(UINT64_MAX);
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(value, meta));
  ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
  EXPECT_EQ(UINT64_MAX, node.get_value_datum().get_uint());
  EXPECT_EQ("18446744073709551615", native_text(node));
  meta.set_unknown();
  value.set_int(7);
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(value, meta));
  ASSERT_EQ(OB_SUCCESS, node.set_questionmark_index(OB_INVALID_INDEX));
  EXPECT_EQ(OB_INVALID_INDEX, node.get_questionmark_index());
  EXPECT_EQ(7, node.get_bound_param_datum().get_int());
  ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
  alignas(8) char scratch[64];
  DatumValue exported;
  ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
  EXPECT_TRUE(exported.is_unknown());
  EXPECT_EQ(OB_INVALID_INDEX, exported.get_unknown());
  EXPECT_EQ(initial_calls, owner.calls());
}

TEST_F(NativeRawConstantOwner, floating_bits_and_old_hash_print_semantics_survive_inline_import)
{
  RawOwnerAllocator owner;
  owner.fail_after(0);
  ObConstRawExpr node(owner);
  for (uint64_t bits : {uint64_t(0), UINT64_C(0x8000000000000000),
                        UINT64_C(0x3ff8000000000000), UINT64_C(0x7ff8000000000051)}) {
    double number;
    MEMCPY(&number, &bits, sizeof(bits));
    ObDatum legacy;
    legacy.ptr_ = reinterpret_cast<const char *>(&number);
    legacy.set_double(number);
    SqlTypeMeta meta;
    meta.set_double();
    meta.set_scale(7);
    const DatumValue input = legacy_value(legacy, meta);
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(input));
    const double actual = node.get_value_datum().get_double();
    uint64_t actual_bits = 0;
    MEMCPY(&actual_bits, &actual, sizeof(actual_bits));
    EXPECT_EQ(bits, actual_bits);
    uint64_t expected_hash = 19;
    ASSERT_EQ(OB_SUCCESS, hash_datum_value(input, 19, expected_hash));
    EXPECT_EQ(expected_hash, node.hash_internal(19));
    EXPECT_EQ(legacy_text(input), native_text(node));
    same_meta(meta, node.get_value_meta());
  }
  const uint32_t bits = UINT32_C(0x80000000);
  float number;
  MEMCPY(&number, &bits, sizeof(bits));
  Datum value;
  value.set_float(number);
  SqlTypeMeta meta;
  meta.set_float();
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(value, meta));
  alignas(8) char scratch[64];
  DatumValue exported;
  ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
  uint32_t actual_bits = 0;
  const float exported_float = exported.get_float();
  MEMCPY(&actual_bits, &exported_float, sizeof(actual_bits));
  EXPECT_EQ(bits, actual_bits);
  EXPECT_EQ(0, owner.calls());
}

TEST_F(NativeRawConstantOwner, writer_scopes_and_typed_null_keep_distinct_value_metadata)
{
  RawOwnerAllocator owner;
  ObConstRawExpr node(owner);
  Datum index;
  index.set_int(2);
  SqlTypeMeta unknown;
  unknown.set_unknown();
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(index, unknown));
  ObRawExprResType inferred;
  inferred.set_type(ObVarcharType);
  inferred.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  inferred.set_accuracy(ObAccuracy(55));
  node.set_result_type(inferred);
  SqlTypeMeta declaration;
  declaration.set_char();
  declaration.set_scale(7);
  declaration.set_collation_type(CS_TYPE_UTF8MB4_UNICODE_CI);
  declaration.set_collation_level(CS_LEVEL_IMPLICIT);
  node.set_expr_obj_meta(declaration);
  Datum null;
  ASSERT_EQ(OB_SUCCESS, node.set_bound_param_borrowed(null, declaration));
  ASSERT_EQ(OB_SUCCESS, node.set_questionmark_index(OB_INVALID_INDEX));
  EXPECT_EQ(OB_INVALID_INDEX, node.get_questionmark_index());
  EXPECT_TRUE(node.get_bound_param_datum().is_null());
  same_meta(declaration, node.get_bound_param_value_meta());
  same_meta(declaration, node.get_expr_obj_meta());
  EXPECT_TRUE(inferred == node.get_result_type());
  alignas(8) char scratch[64];
  DatumValue exported;
  ASSERT_EQ(OB_SUCCESS, node.export_bound_param(exported, scratch, sizeof(scratch)));
  EXPECT_TRUE(exported.is_null());
  same_meta(declaration, exported.meta_);
  // A later value-only write must not replay the initial full setter behavior.
  Datum replacement;
  replacement.set_int(99);
  SqlTypeMeta integer;
  integer.set_int();
  ASSERT_EQ(OB_SUCCESS, node.replace_value_borrowed(replacement, integer));
  EXPECT_EQ(99, node.get_value_datum().get_int());
  EXPECT_TRUE(node.get_bound_param_datum().is_null());
  same_meta(declaration, node.get_bound_param_value_meta());
  EXPECT_TRUE(inferred == node.get_result_type());
  int64_t untouched = 123;
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, node.get_questionmark_index(untouched));
  EXPECT_EQ(123, untouched);
}

TEST_F(NativeRawConstantOwner, decimal_actual_width_is_owned_without_result_precision_normalization)
{
  ObArenaAllocator owner("RawDecimal");
  ObRawExprFactory factory(owner);
  ObPLExprCopier copier(factory);
  for (int width : {0, 4, 8, 16, 32, 64}) {
    alignas(8) char bytes[64];
    MEMSET(bytes, 0xa5, sizeof(bytes));
    ObDatum legacy;
    legacy.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(bytes), width);
    SqlTypeMeta meta;
    meta.set_decimal_int(5);
    ObConstRawExpr node(owner);
    node.set_expr_type(T_NUMBER);
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(legacy_value(legacy, meta)));
    node.set_accuracy(ObAccuracy(18, 2));
    EXPECT_EQ(width, node.get_value_datum().length());
    EXPECT_EQ(bytes, node.get_value_datum().get_reference());
    ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
    EXPECT_NE(bytes, node.get_value_datum().get_reference());
    MEMSET(bytes, 0, sizeof(bytes));
    alignas(8) char scratch[64];
    DatumValue exported;
    ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
    EXPECT_EQ(width, exported.datum_.length());
    EXPECT_EQ(5, exported.get_scale());
    for (int i = 0; i < width; ++i) {
      EXPECT_EQ(0xa5, static_cast<const unsigned char *>(exported.datum_.get_reference())[i]);
    }
    same_meta(meta, exported.meta_);
    EXPECT_EQ(18, node.get_accuracy().get_precision());
    EXPECT_EQ(2, node.get_accuracy().get_scale());
  }
}

TEST_F(NativeRawConstantOwner, shallow_assign_borrows_but_real_attribute_copy_owns_values_and_prefix)
{
  ObArenaAllocator owner("RawCopy");
  ObRawExprFactory factory(owner);
  ObPLExprCopier copier(factory);
  char text[] = "value text", bound[] = "bound text", prefix[] = "DATE";
  ObConstRawExpr source(owner), copied(owner);
  source.set_expr_type(T_VARCHAR);
  copied.set_expr_type(T_VARCHAR);
  ASSERT_EQ(OB_SUCCESS, source.set_value_borrowed(string_view(text, sizeof(text)-1)));
  ASSERT_EQ(OB_SUCCESS, source.set_bound_param_borrowed(string_view(bound, sizeof(bound)-1)));
  source.set_literal_prefix(ObString(sizeof(prefix)-1, prefix));
  ASSERT_EQ(OB_SUCCESS, copied.assign(source));
  EXPECT_EQ(source.get_value_datum().get_reference(), copied.get_value_datum().get_reference());
  EXPECT_EQ(source.get_bound_param_datum().get_reference(), copied.get_bound_param_datum().get_reference());
  ASSERT_EQ(OB_SUCCESS, copied.inner_deep_copy(copier));
  EXPECT_NE(source.get_value_datum().get_reference(), copied.get_value_datum().get_reference());
  EXPECT_NE(source.get_bound_param_datum().get_reference(), copied.get_bound_param_datum().get_reference());
  EXPECT_NE(prefix, copied.get_literal_prefix().ptr());
  MEMSET(text, 'x', sizeof(text)-1);
  MEMSET(bound, 'y', sizeof(bound)-1);
  MEMSET(prefix, 'z', sizeof(prefix)-1);
  EXPECT_EQ(ObString("value text"), copied.get_value_datum().get_string());
  EXPECT_EQ(ObString("bound text"), copied.get_bound_param_datum().get_string());
  EXPECT_EQ(ObString("DATE"), copied.get_literal_prefix());
  EXPECT_EQ(ObString("xxxxxxxxxx"), source.get_value_datum().get_string());
}

TEST_F(NativeRawConstantOwner, deep_copy_failure_does_not_publish_either_value_or_prefix)
{
  for (int failed_allocation = 1; failed_allocation <= 3; ++failed_allocation) {
    RawOwnerAllocator owner;
    ObRawExprFactory factory(owner);
    ObPLExprCopier copier(factory);
    char text[] = "left", bound[] = "right", prefix[] = "DATE";
    ObConstRawExpr node(owner);
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(string_view(text, 4)));
    ASSERT_EQ(OB_SUCCESS, node.set_bound_param_borrowed(string_view(bound, 5)));
    node.set_literal_prefix(ObString(4, prefix));
    const Datum original = node.get_value_datum(), parameter = node.get_bound_param_datum();
    const SqlTypeMeta original_meta = node.get_value_meta(), parameter_meta = node.get_bound_param_value_meta();
    owner.fail_after(failed_allocation-1);
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, node.inner_deep_copy(copier));
    EXPECT_EQ(0, MEMCMP(&original, &node.get_value_datum(), sizeof(Datum)));
    EXPECT_EQ(0, MEMCMP(&parameter, &node.get_bound_param_datum(), sizeof(Datum)));
    same_meta(original_meta, node.get_value_meta());
    same_meta(parameter_meta, node.get_bound_param_value_meta());
    EXPECT_EQ(prefix, node.get_literal_prefix().ptr());
    owner.allow();
    ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
    EXPECT_NE(text, node.get_value_datum().get_reference());
    EXPECT_NE(bound, node.get_bound_param_datum().get_reference());
    EXPECT_EQ(ObString("DATE"), node.get_literal_prefix());
  }
}

TEST_F(NativeRawConstantOwner, old_sql_array_graph_clone_survives_source_arena_reset)
{
  ObArenaAllocator source("RawArraySrc"), owner("RawArrayDst");
  ObRawExprFactory factory(owner);
  ObPLExprCopier copier(factory);
  ObConstRawExpr node(owner);
  auto *inner = DatumParameterArray::alloc(source, 1);
  auto *outer = DatumParameterArray::alloc(source, 2);
  ASSERT_NE(nullptr, inner);
  ASSERT_NE(nullptr, outer);
  ASSERT_EQ(OB_SUCCESS, inner->data_[0].set_int(INT64_MIN, source));
  inner->element_.set_int();
  inner->source_value_size_ = 71;
  ASSERT_EQ(OB_SUCCESS, outer->data_[0].set_varchar(ObString("nested value"), source));
  outer->data_[0].set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, outer->data_[1].set_extend(reinterpret_cast<int64_t>(inner), T_EXT_SQL_ARRAY, source));
  outer->source_value_size_ = 173;
  DatumParameter envelope;
  ASSERT_EQ(OB_SUCCESS, envelope.set_extend(reinterpret_cast<int64_t>(outer), T_EXT_SQL_ARRAY, source));
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(envelope));
  ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
  alignas(8) char scratch[64];
  DatumValue exported;
  ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
  auto *copied = reinterpret_cast<DatumParameterArray *>(exported.get_ext());
  ASSERT_NE(nullptr, copied);
  EXPECT_NE(outer, copied);
  auto *copied_inner = copied->data_[1].get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, copied_inner);
  EXPECT_NE(inner, copied_inner);
  source.reset();
  MEMSET(scratch, 0xcc, sizeof(scratch));
  ASSERT_EQ(2, copied->count_);
  EXPECT_EQ(173, copied->source_value_size_);
  EXPECT_EQ(ObString("nested value"), copied->data_[0].get_string());
  ASSERT_EQ(1, copied_inner->count_);
  EXPECT_EQ(71, copied_inner->source_value_size_);
  EXPECT_EQ(INT64_MIN, copied_inner->data_[0].get_int());
}

TEST_F(NativeRawConstantOwner, markers_and_reference_flags_preserve_original_physical_bytes)
{
  ObArenaAllocator owner("RawMarkers");
  ObRawExprFactory factory(owner);
  ObPLExprCopier copier(factory);
  for (int64_t marker : {DATUM_MIN_EXT_VALUE, DATUM_MAX_EXT_VALUE, int64_t(ObActionFlag::OP_NOP)}) {
    alignas(8) char payload[DATUM_EXTEND_PAYLOAD_SIZE];
    SqlTypeMeta meta;
    meta.set_ext();
    ObDatum legacy;
    legacy.ptr_ = payload;
    legacy.set_extend(marker, 0, 0, meta.get_cs_level(), meta.get_cs_type());
    const std::string expected(payload, sizeof(payload));
    ObConstRawExpr node(owner);
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(legacy_value(legacy, meta)));
    EXPECT_FALSE(node.get_value_datum().is_present());
    ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
    alignas(8) char scratch[64];
    DatumValue exported;
    ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
    ObDatum exported_physical; alignas(8) char physical_scratch[64];
    ASSERT_EQ(OB_SUCCESS, exported.export_legacy(exported_physical, physical_scratch, sizeof(physical_scratch)));
    EXPECT_EQ(expected, std::string(exported_physical.ptr_, exported_physical.len_));
    // A nonzero wrapper size cannot be reconstructed from meta and must remain a reference.
    legacy.set_extend(marker, 0, 17, meta.get_cs_level(), meta.get_cs_type());
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(legacy_value(legacy, meta)));
    EXPECT_TRUE(node.get_value_datum().is_present());
    EXPECT_EQ(Datum::LegacyExtended, node.get_value_datum().reference_flags());
    ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
    MEMSET(payload, 0, sizeof(payload));
    ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
    EXPECT_EQ(marker, exported.get_ext());
    EXPECT_EQ(17, exported.datum_.get_extend_size());
  }
  char bytes[] = "lob wrapper";
  for (int flag : {1, 2}) {
    DatumValue legacy = string_view(bytes, sizeof(bytes)-1);
    if (flag == 1) { legacy.datum_.set_outrow(); }
    else { legacy.datum_.set_has_lob_header(); }
    ObConstRawExpr node(owner);
    ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(legacy));
    ASSERT_EQ(OB_SUCCESS, node.inner_deep_copy(copier));
    alignas(8) char scratch[64];
    DatumValue exported;
    ASSERT_EQ(OB_SUCCESS, node.export_value(exported, scratch, sizeof(scratch)));
    EXPECT_EQ(legacy.datum_.state(), exported.datum_.state());
    EXPECT_EQ(legacy.datum_.length(), exported.datum_.length());
    EXPECT_EQ(legacy.datum_.reference_flags(), exported.datum_.reference_flags());
    EXPECT_NE(legacy.datum_.get_reference(), exported.datum_.get_reference());
    EXPECT_EQ(legacy.get_string(), exported.get_string());
  }
}

TEST_F(NativeRawConstantOwner, failed_import_native_write_and_short_export_leave_prior_state)
{
  ObArenaAllocator owner("RawFailure");
  ObConstRawExpr node(owner);
  Datum original;
  original.set_int(42);
  SqlTypeMeta integer;
  integer.set_int();
  integer.set_scale(7);
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(original, integer));
  const ObRawExprResType result = node.get_result_type();
  char short_payload[] = "short";
  ObDatum broken;
  broken.ptr_ = short_payload;
  broken.pack_ = 5;
  DatumValue broken_value;
  EXPECT_EQ(OB_INVALID_ARGUMENT, broken_value.assign_legacy_borrowed(broken, integer));
  EXPECT_TRUE(broken_value.is_null());
  broken_value.meta_ = integer;
  ASSERT_EQ(OB_SUCCESS, broken_value.datum_.set_reference(short_payload, 5));
  EXPECT_EQ(OB_INVALID_ARGUMENT, node.set_value_borrowed(broken_value));
  Datum invalid_decimal;
  ASSERT_EQ(OB_SUCCESS, invalid_decimal.set_reference(short_payload, 5));
  SqlTypeMeta decimal;
  decimal.set_decimal_int(3);
  EXPECT_EQ(OB_INVALID_ARGUMENT, node.replace_value_borrowed(invalid_decimal, decimal));
  EXPECT_EQ(OB_INVALID_ARGUMENT, node.set_bound_param_borrowed(invalid_decimal, decimal));
  EXPECT_EQ(42, node.get_value_datum().get_int());
  EXPECT_EQ(42, node.get_bound_param_datum().get_int());
  same_meta(integer, node.get_value_meta());
  same_meta(integer, node.get_bound_param_value_meta());
  same_meta(integer, node.get_expr_obj_meta());
  EXPECT_TRUE(result == node.get_result_type());
  char sentinel[] = "sentinel";
  DatumValue output = string_view(sentinel, sizeof(sentinel)-1);
  const DatumValue before = output;
  alignas(8) char scratch[8] = {};
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, node.export_value(output, scratch, 7));
  EXPECT_EQ(0, MEMCMP(&before.datum_, &output.datum_, sizeof(Datum)));
  same_meta(before.meta_, output.meta_);
  RawOwnerAllocator failing;
  failing.fail_after(0);
  Datum output_native;
  output_native.set_int(99);
  Datum reference;
  ASSERT_EQ(OB_SUCCESS, reference.set_reference(sentinel, sizeof(sentinel)-1));
  SqlTypeMeta text;
  text.set_varchar();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      DatumParameterCodec::clone_raw_constant_value(reference, text, failing, output_native));
  EXPECT_EQ(99, output_native.get_int());
}

TEST_F(NativeRawConstantOwner, dynamic_number_parameter_and_binary_raw_comparison_keep_old_contracts)
{
  ObArenaAllocator owner("RawSemantics");
  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("123.45", owner));
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_number(number, owner));
  ObConstRawExpr node(owner);
  node.set_expr_type(T_QUESTIONMARK);
  Datum index;
  index.set_int(3);
  SqlTypeMeta unknown;
  unknown.set_unknown();
  ASSERT_EQ(OB_SUCCESS, node.set_value_borrowed(index, unknown));
  ASSERT_EQ(OB_SUCCESS, node.set_bound_param_borrowed(parameter));
  ObRawExprResType source_type;
  source_type.set_type(ObNumberType);
  source_type.set_accuracy(ObAccuracy(9, 4));
  node.set_result_type(source_type);
  ObRawExprResType target_type;
  target_type.set_type(ObDecimalIntType);
  target_type.set_accuracy(ObAccuracy(18, 2));
  ASSERT_EQ(OB_SUCCESS, node.set_dynamic_eval_questionmark(target_type));
  EXPECT_TRUE(source_type == node.get_orig_qm_type());
  EXPECT_EQ(ObNumberType, node.get_bound_param_value_meta().get_type());
  EXPECT_EQ(3, node.get_questionmark_index());
  EXPECT_EQ(legacy_text(parameter.value_view()), native_text(node));
  alignas(8) char scratch[64];
  DatumValue old_index;
  ASSERT_EQ(OB_SUCCESS, node.export_value(old_index, scratch, sizeof(scratch)));
  uint64_t expected = 31;
  ASSERT_EQ(OB_SUCCESS, hash_datum_value(old_index, 31, expected));
  const ObPrecision precision = 18;
  const ObScale scale = 2;
  expected = murmurhash(&precision, sizeof(precision), expected);
  expected = murmurhash(&scale, sizeof(scale), expected);
  EXPECT_EQ(expected, node.hash_internal(31));
  char upper[] = "A", lower[] = "a";
  ObConstRawExpr left(owner), right(owner);
  left.set_expr_type(T_VARCHAR);
  right.set_expr_type(T_VARCHAR);
  ASSERT_EQ(OB_SUCCESS, left.set_value_borrowed(string_view(upper, 1, CS_TYPE_UTF8MB4_GENERAL_CI)));
  ASSERT_EQ(OB_SUCCESS, right.set_value_borrowed(string_view(lower, 1, CS_TYPE_UTF8MB4_GENERAL_CI)));
  // Raw expression identity intentionally compares string bytes using BINARY.
  EXPECT_FALSE(left.inner_same_as(right, nullptr));
  ASSERT_EQ(OB_SUCCESS, right.set_value_borrowed(string_view(upper, 1, CS_TYPE_UTF8MB4_GENERAL_CI)));
  EXPECT_TRUE(left.inner_same_as(right, nullptr));
  SqlTypeMeta changed = left.get_value_meta();
  changed.set_collation_level(CS_LEVEL_IMPLICIT);
  const SqlTypeMeta prior_parameter_meta = left.get_bound_param_value_meta();
  ASSERT_EQ(OB_SUCCESS, left.replace_value_borrowed(left.get_value_datum(), changed));
  same_meta(prior_parameter_meta, left.get_bound_param_value_meta());
  same_meta(changed, left.get_value_meta());
  ObExprEqualCheckContext context;
  context.err_code_ = OB_ALLOCATE_MEMORY_FAILED;
  EXPECT_FALSE(context.compare_const(left, right));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, context.err_code_);
}
