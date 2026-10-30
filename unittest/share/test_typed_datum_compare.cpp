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

#include "share/datum/typed_datum_compare.h"
#include "common/datum/datum_parameter_codec.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Typed native value API must not require the legacy value definition"
#endif
#include "share/datum/ob_datum_cmp_func_def.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
namespace {
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result; std::istringstream in(line); std::string f;
  while (std::getline(in, f, '\t')) { result.push_back(f); } return result;
}
std::string unhex(const std::string &s) {
  std::string result;
  for (size_t i = 0; i < s.size(); i += 2) { result.push_back(std::stoi(s.substr(i, 2), nullptr, 16)); }
  return result;
}
}
void check_fixture(const char *path, int value_count, int pair_count)
{
  std::ifstream input(path);
  ASSERT_TRUE(input.good());
  ObArenaAllocator arena;
  std::vector<DatumValue> values;
  std::string line;
  int pairs = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    const auto f = fields(line);
    if (f[0] == "V") {
      ASSERT_EQ(8, f.size()); ASSERT_EQ(values.size(), std::stoi(f[1]));
      const auto wire = unhex(f[3]); int64_t pos = 0; DatumValue value;
      ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(wire.data(), wire.size(), pos, arena, value.datum_, value.meta_));
      ASSERT_EQ(wire.size(), pos); values.push_back(value);
      for (int i : {4, 6}) {
        uint64_t hash = 0;
        EXPECT_EQ(std::stoi(f[i]), hash_datum_value(value, i == 4 ? 0 : 42, hash)) << f[2];
        EXPECT_EQ(std::stoull(f[i + 1]), hash) << f[2];
      }
    } else if (f[0] == "P") {
      ASSERT_EQ(9, f.size());
      DatumValue left = values.at(std::stoi(f[2])), right = values.at(std::stoi(f[3]));
      if (f[1] == "1") { left.meta_.set_collation_type(CS_TYPE_BINARY); right.meta_.set_collation_type(CS_TYPE_BINARY); }
      SCOPED_TRACE(line);
      const bool can = can_compare_datum_values(left, right);
      EXPECT_EQ(std::stoi(f[4]) != 0, can);
      if (can) {
        int cmp = 0; bool equal = false;
        EXPECT_EQ(std::stoi(f[5]), compare_datum_values(left, right, cmp));
        EXPECT_EQ(std::stoi(f[6]), cmp);
        EXPECT_EQ(std::stoi(f[7]), datum_values_equal(left, right, equal));
        EXPECT_EQ(std::stoi(f[8]) != 0, equal);
      }
      ++pairs;
    }
  }
  EXPECT_EQ(value_count, values.size()); EXPECT_EQ(pair_count, pairs);
}

TEST(TypedDatum, frozen_legacy_results) {
  check_fixture("unittest/share/data/typed_values_compare.golden", 104, 21218);
}
TEST(TypedDatum, frozen_legacy_special_values) {
  check_fixture("unittest/share/data/typed_values_specials.golden", 38, 2888);
}
TEST(TypedDatum, metadata_only_no_cast_comparison_preflight) {
  SqlTypeMeta integer, floating, number, varchar, datetime, timestamp;
  integer.set_int(); floating.set_float(); number.set_number();
  varchar.set_varchar(); varchar.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  datetime.set_datetime(); timestamp.set_timestamp();
  EXPECT_TRUE(can_compare_types_without_cast(integer, floating));
  EXPECT_FALSE(can_compare_types_without_cast(floating, number));
  EXPECT_TRUE(can_compare_types_without_cast(varchar, varchar));
  SqlTypeMeta other_varchar = varchar;
  other_varchar.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  EXPECT_FALSE(can_compare_types_without_cast(varchar, other_varchar));
  EXPECT_FALSE(can_compare_types_without_cast(datetime, timestamp));
  EXPECT_TRUE(is_datetime_timestamp_type_pair(ObTimestampType, ObDateTimeType));
  EXPECT_FALSE(is_datetime_timestamp_type_pair(ObTimestampNanoType, ObDateTimeType));
}
TEST(TypedDatum, no_cast_preflight_has_explicit_metadata_results) {
  SqlTypeMeta integer, floating, number;
  integer.set_int(); floating.set_float(); number.set_number();
  // The native preflight is operator-independent. These results preserve the
  // metadata decisions previously checked for both CO_EQ and CO_CMP.
  EXPECT_TRUE(can_compare_types_without_cast(integer, floating));
  EXPECT_FALSE(can_compare_types_without_cast(floating, number));
}

namespace {
class RejectAllocator : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
DatumParameter integer(ObIAllocator &arena, int64_t n) {
  DatumParameter value; EXPECT_EQ(OB_SUCCESS, value.set_int(n, arena)); return value;
}
}
TEST(TypedDatum, empty_decimal_coefficient_is_zero) {
  ObArenaAllocator arena;
  DatumValue zero; zero.meta_.set_decimal_int(); zero.meta_.set_scale(0);
  zero.datum_.set_decimal_int_shallow(nullptr, 0);
  for (int64_t n : {-1, 0, 1}) {
    const DatumValue other = integer(arena, n).value_view();
    int cmp = 99; bool equal = false;
    ASSERT_EQ(OB_SUCCESS, compare_datum_values(zero, other, cmp)); EXPECT_EQ(-n, cmp);
    ASSERT_EQ(OB_SUCCESS, datum_values_equal(zero, other, equal)); EXPECT_EQ(n == 0, equal);
    ASSERT_EQ(OB_SUCCESS, compare_datum_values(other, zero, cmp)); EXPECT_EQ(n, cmp);
  }
  int32_t coefficient = 0;
  DatumValue concrete; concrete.meta_ = zero.meta_;
  concrete.datum_.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(&coefficient), sizeof(coefficient));
  for (int scale : {-3, 0, 6}) {
    concrete.meta_.set_scale(scale); int cmp = 99;
    ASSERT_EQ(OB_SUCCESS, compare_datum_values(zero, concrete, cmp)); EXPECT_EQ(0, cmp);
  }
}
TEST(TypedDatum, cloned_constants_own_payload_and_preserve_metadata) {
  ObArenaAllocator source, destination;
  DatumValue scalar, text;
  const DatumValue original_scalar = integer(source, 123).value_view();
  DatumParameter str; ASSERT_EQ(OB_SUCCESS, str.set_varchar(ObString("owned payload"), source));
  str.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS, clone_typed_value(original_scalar, scalar, destination));
  ASSERT_EQ(OB_SUCCESS, clone_typed_value(str.value_view(), text, destination));
  EXPECT_NE(original_scalar.datum_.ptr_, scalar.datum_.ptr_);
  EXPECT_NE(str.datum_.ptr_, text.datum_.ptr_);
  EXPECT_EQ(str.get_meta(), text.meta_);
  source.reset();
  EXPECT_EQ(123, scalar.datum_.get_int()); EXPECT_EQ(ObString("owned payload"), text.datum_.get_string());
  RejectAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, clone_typed_value(text, scalar, reject));
  EXPECT_EQ(123, scalar.datum_.get_int()); EXPECT_TRUE(scalar.meta_.is_int());
}
TEST(TypedDatum, cloned_sql_array_has_independent_elements) {
  ObArenaAllocator source, destination;
  DatumParameter array; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  array.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, array.alloc_datum_reserved_buff(meta, -1, source));
  auto *elements = DatumParameterArray::alloc(source, 2); ASSERT_NE(nullptr, elements);
  elements->data_[0] = integer(source, 77);
  ASSERT_EQ(OB_SUCCESS, elements->data_[1].set_varchar(ObString("array value"), source));
  array.set_array_address(elements);
  DatumValue copy; ASSERT_EQ(OB_SUCCESS, clone_typed_value(array.value_view(), copy, destination));
  const auto *owned = reinterpret_cast<const DatumParameterArray *>(copy.datum_.get_int());
  EXPECT_NE(elements, owned); EXPECT_NE(elements->data_[0].datum_.ptr_, owned->data_[0].datum_.ptr_);
  source.reset();
  EXPECT_EQ(77, owned->data_[0].get_int()); EXPECT_EQ(ObString("array value"), owned->data_[1].get_string());
  EXPECT_FALSE(can_compare_datum_values(copy, copy));
}
TEST(TypedDatum, invalid_scalar_views_are_rejected) {
  ObArenaAllocator arena;
  DatumValue valid = integer(arena, 3).value_view(), invalid = valid;
  invalid.datum_.set_string(ObString());
  EXPECT_FALSE(can_compare_datum_values(invalid, valid));
  int cmp = 99; EXPECT_EQ(OB_NOT_SUPPORTED, compare_datum_values(invalid, valid, cmp));
  uint64_t hash = 42; EXPECT_EQ(OB_INVALID_ARGUMENT, hash_datum_value(invalid, 0, hash));
  EXPECT_EQ(42, hash);
}

TEST(TypedDatum, question_index_comparison_and_marker_hash_keep_scalar_contract) {
  ObArenaAllocator owner;
  DatumValue first, second, marker;
  ASSERT_EQ(OB_SUCCESS,first.set_unknown(0,owner));
  ASSERT_EQ(OB_SUCCESS,second.set_unknown(1,owner));
  int cmp=0; bool equal=true;
  ASSERT_TRUE(can_compare_datum_values(first,second));
  ASSERT_EQ(OB_SUCCESS,compare_datum_values(first,second,cmp)); EXPECT_EQ(-1,cmp);
  ASSERT_EQ(OB_SUCCESS,datum_values_equal(first,second,equal)); EXPECT_FALSE(equal);
  // These are frozen legacy int1 hashes. Legacy Unknown/Extend hash
  // calculators use the identical 64-bit input, without metadata bytes.
  uint64_t hash=0;
  ASSERT_EQ(OB_SUCCESS,hash_datum_value(second,0,hash)); EXPECT_EQ(10357027354812746030ULL,hash);
  ASSERT_EQ(OB_SUCCESS,hash_datum_value(second,42,hash)); EXPECT_EQ(7191219851201888087ULL,hash);
  ASSERT_EQ(OB_SUCCESS,marker.set_extend(ObActionFlag::OP_MIN_OBJ,0,owner));
  ASSERT_EQ(OB_SUCCESS,hash_datum_value(marker,0,hash)); EXPECT_EQ(10357027354812746030ULL,hash);
  ASSERT_EQ(OB_SUCCESS,hash_datum_value(marker,42,hash)); EXPECT_EQ(7191219851201888087ULL,hash);
}
TEST(TypedDatum, extend_bound_sentinels_keep_ordering) {
  alignas(int64_t) char left_storage[DATUM_LEGACY_EXTEND_SIZE] = {};
  alignas(int64_t) char right_storage[DATUM_LEGACY_EXTEND_SIZE] = {};
  ObDatum min_value(left_storage, DATUM_LEGACY_EXTEND_SIZE, false);
  ObDatum max_value(right_storage, DATUM_LEGACY_EXTEND_SIZE, false);
  min_value.set_ext_value(DATUM_MIN_EXT_VALUE);
  max_value.set_ext_value(DATUM_MAX_EXT_VALUE);
  using ExtendCompare = datum_cmp::ObDatumTCCmp<ObExtendTC, ObExtendTC>;
  using ExtendIntCompare = datum_cmp::ObDatumTCCmp<ObExtendTC, ObIntTC>;
  using IntExtendCompare = datum_cmp::ObDatumTCCmp<ObIntTC, ObExtendTC>;

  int cmp = 99;
  ASSERT_EQ(OB_SUCCESS, ExtendCompare::cmp(min_value, min_value, cmp));
  EXPECT_EQ(0, cmp);
  ASSERT_EQ(OB_SUCCESS, ExtendCompare::cmp(max_value, max_value, cmp));
  EXPECT_EQ(0, cmp);
  ASSERT_EQ(OB_SUCCESS, ExtendCompare::cmp(min_value, max_value, cmp));
  EXPECT_EQ(-1, cmp);
  ASSERT_EQ(OB_SUCCESS, ExtendCompare::cmp(max_value, min_value, cmp));
  EXPECT_EQ(1, cmp);
  ASSERT_EQ(OB_SUCCESS, ExtendIntCompare::cmp(min_value, max_value, cmp));
  EXPECT_EQ(-1, cmp);
  ASSERT_EQ(OB_SUCCESS, IntExtendCompare::cmp(min_value, max_value, cmp));
  EXPECT_EQ(1, cmp);
}
TEST(TypedDatum, native_number_and_extended_constant_snapshots_outlive_source_owner) {
  ObArenaAllocator owner;
  DatumValue number, marker, unknown;
  {
    ObArenaAllocator source;
    number::ObNumber input; ASSERT_EQ(OB_SUCCESS,input.from("12.50",source));
    ASSERT_EQ(OB_SUCCESS,number.set_number(input,owner)); number.set_scale(2);
    DatumValue temporary;
    ASSERT_EQ(OB_SUCCESS,temporary.set_extend(ObActionFlag::OP_DEFAULT_NOW_FLAG,0,source,17));
    ASSERT_EQ(OB_SUCCESS,clone_typed_value(temporary,marker,owner));
    ASSERT_EQ(OB_SUCCESS,temporary.set_unknown(123,source));
    ASSERT_EQ(OB_SUCCESS,clone_typed_value(temporary,unknown,owner));
  }
  char text[128]; int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,number.print_sql_literal(text,sizeof(text),pos));
  EXPECT_EQ("12.50",std::string(text,pos));
  EXPECT_EQ(static_cast<int64_t>(ObActionFlag::OP_DEFAULT_NOW_FLAG),marker.get_ext()); EXPECT_EQ(17,marker.get_val_len());
  EXPECT_EQ(123,unknown.get_unknown());
  RejectAllocator reject;
  const SqlTypeMeta previous=number.get_meta(); const char *bytes=number.datum_.ptr_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,number.set_bool(true,reject));
  EXPECT_EQ(previous,number.get_meta()); EXPECT_EQ(bytes,number.datum_.ptr_);
}
TEST(TypedDatum, ast_binary_equality_keeps_trailing_spaces_and_numeric_coercion) {
  ObArenaAllocator owner;
  DatumValue lower, upper, padded, seven, number;
  ASSERT_EQ(OB_SUCCESS, lower.set_varchar(ObString("key"), owner));
  ASSERT_EQ(OB_SUCCESS, upper.set_varchar(ObString("KEY"), owner));
  ASSERT_EQ(OB_SUCCESS, padded.set_varchar(ObString("key "), owner));
  for (DatumValue *text : {&lower, &upper, &padded}) {
    text->set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  }
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, datum_values_equal(lower, upper, equal)); EXPECT_TRUE(equal);
  EXPECT_FALSE(binary_equal_datum_values(lower, upper));
  EXPECT_FALSE(binary_equal_datum_values(lower, padded));
  EXPECT_TRUE(strict_equal_datum_values(lower, lower));
  EXPECT_FALSE(strict_equal_datum_values(lower, upper));
  ASSERT_EQ(OB_SUCCESS, seven.set_int(7, owner));
  number::ObNumber parsed; ASSERT_EQ(OB_SUCCESS, parsed.from("7.00", owner));
  ASSERT_EQ(OB_SUCCESS, number.set_number(parsed, owner));
  EXPECT_TRUE(binary_equal_datum_values(seven, number));
  EXPECT_FALSE(strict_equal_datum_values(seven, number));
  DatumValue null1, null2;
  null2.meta_.set_int(); // semantic NULL with a declared raw type
  EXPECT_TRUE(binary_equal_datum_values(null1, null2));
  EXPECT_FALSE(strict_equal_datum_values(null1, null2));
}
TEST(TypedDatum, native_literal_scalar_widths_and_failed_replacement) {
  ObArenaAllocator owner;
  DatumValue date, year, flag, decimal;
  ASSERT_EQ(OB_SUCCESS, date.set_date(-1234, owner));
  ASSERT_EQ(OB_SUCCESS, year.set_year(99, owner));
  ASSERT_EQ(OB_SUCCESS, flag.set_tinyint(-2, owner));
  EXPECT_EQ(4, date.datum_.len_); EXPECT_EQ(-1234, date.get_date());
  EXPECT_EQ(1, year.datum_.len_); EXPECT_EQ(99, year.get_year());
  EXPECT_EQ(8, flag.datum_.len_); int8_t tiny=0;
  ASSERT_EQ(OB_SUCCESS, flag.get_tinyint(tiny)); EXPECT_EQ(-2, tiny);
  {
    int64_t coefficient = -12345;
    ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(sizeof(coefficient), 3,
        reinterpret_cast<const ObDecimalInt *>(&coefficient), owner));
    coefficient = 0;
  }
  char text[64]; int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS, decimal.print_sql_literal(text, sizeof(text), pos));
  EXPECT_EQ("-12.345", std::string(text, pos));
  RejectAllocator reject;
  const char *previous = decimal.datum_.ptr_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, decimal.set_int(99, reject));
  EXPECT_EQ(previous, decimal.datum_.ptr_); EXPECT_EQ(3, decimal.get_scale());
  pos=0; ASSERT_EQ(OB_SUCCESS, decimal.print_sql_literal(text, sizeof(text), pos));
  EXPECT_EQ("-12.345", std::string(text, pos));
}
int main(int argc, char **argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }

TEST(TypedDatum, clone_preserves_unspecified_string_collation_and_owns_bytes) {
  ObArenaAllocator destination;
  for (SqlValueType type : {ObVarcharType, ObCharType, ObLongTextType}) {
    DatumValue copy;
    SqlTypeMeta original_meta;
    {
      ObArenaAllocator source;
      DatumValue expression;
      ASSERT_EQ(OB_SUCCESS, expression.set_string(type, ObString("json_query(`j`,'')"), source));
      expression.set_collation_type(CS_TYPE_INVALID);
      expression.set_collation_level(CS_LEVEL_IMPLICIT);
      expression.set_scale(-1);
      original_meta = expression.meta_;
      ASSERT_EQ(OB_SUCCESS, clone_typed_value(expression, copy, destination));
      EXPECT_EQ(original_meta, copy.meta_);
      EXPECT_NE(expression.datum_.ptr_, copy.datum_.ptr_);
      MEMSET(const_cast<char *>(expression.datum_.ptr_), 'x', expression.datum_.len_);
      EXPECT_EQ(ObString("json_query(`j`,'')"), copy.get_string());
    }
    EXPECT_EQ(original_meta, copy.meta_);
    EXPECT_EQ(ObString("json_query(`j`,'')"), copy.get_string());
  }
}
TEST(TypedDatum, raw_string_clone_failures_preserve_previous_output) {
  ObArenaAllocator source, destination;
  DatumValue expression, output;
  ASSERT_EQ(OB_SUCCESS, expression.set_varchar(ObString("expression"), source));
  expression.set_collation_type(CS_TYPE_INVALID);
  ASSERT_EQ(OB_SUCCESS, output.set_int(123, destination));
  const char *previous_bytes = output.datum_.ptr_;
  const SqlTypeMeta previous_meta = output.meta_;
  RejectAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, clone_typed_value(expression, output, reject));
  EXPECT_EQ(previous_bytes, output.datum_.ptr_); EXPECT_EQ(previous_meta, output.meta_);
  EXPECT_EQ(123, output.get_int());
  expression.datum_.ptr_ = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, clone_typed_value(expression, output, destination));
  EXPECT_EQ(previous_bytes, output.datum_.ptr_); EXPECT_EQ(previous_meta, output.meta_);
  EXPECT_EQ(123, output.get_int());
}
TEST(TypedDatum, unspecified_string_collation_still_rejected_for_compare_and_hash) {
  ObArenaAllocator source, destination;
  DatumValue expression, copy;
  ASSERT_EQ(OB_SUCCESS, expression.set_varchar(ObString("expression"), source));
  expression.set_collation_type(CS_TYPE_INVALID);
  ASSERT_EQ(OB_SUCCESS, clone_typed_value(expression, copy, destination));
  EXPECT_FALSE(can_compare_datum_values(expression, copy));
  int cmp = 99;
  EXPECT_EQ(OB_NOT_SUPPORTED, compare_datum_values(expression, copy, cmp));
  uint64_t hash = 42;
  EXPECT_EQ(OB_INVALID_ARGUMENT, hash_datum_value(copy, 0, hash));
  EXPECT_EQ(42, hash);
}
