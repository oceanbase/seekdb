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

#include "common/datum/datum_core.h"
#include "common/datum/datum_parameter.h"
#include "common/datum/datum_param_store.h"
#include "common/datum/datum_parameter_array.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/wide_integer/ob_wide_integer_helper.h"

#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native Datum headers must not include the legacy value definition"
#endif

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unordered_set>
#include <type_traits>
#include <gtest/gtest.h>
#include "lib/allocator/ob_allocator.h"

using namespace oceanbase::common;

namespace {
static_assert(sizeof(ObDatum) == 12, "The Datum slot ABI must stay unchanged");
static_assert(sizeof(SqlTypeMeta) == 4, "The metadata ABI must stay unchanged");
}

TEST(DatumCore, scalar_parameter_and_copy_need_no_legacy_definition)
{
  ObArenaAllocator arena;
  DatumParameter value, copy;
  SqlTypeMeta meta; meta.set_uint64(); value.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, value.alloc_datum_reserved_buff(meta, -1, arena));
  value.datum_.set_uint(UINT64_MAX);
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(value, arena));
  EXPECT_EQ(UINT64_MAX, copy.datum_.get_uint());
  EXPECT_NE(copy.datum_.ptr_, value.datum_.ptr_);
  copy.datum_.set_null(); EXPECT_TRUE(copy.is_null());
}

TEST(DatumCore, native_store_growth_preserves_payloads)
{
  ObArenaAllocator arena;
  DatumParamStore store{ObWrapperAllocator(arena)};
  const int64_t count = store.get_block_size() / sizeof(DatumParameter) + 17;
  SqlTypeMeta meta;
  meta.set_int();
  for (int64_t i = 0; i < count; ++i) {
    DatumParameter value;
    value.set_type_meta(meta);
    value.set_precision(19);
    ASSERT_EQ(OB_SUCCESS, value.alloc_datum_reserved_buff(meta, 19, arena));
    value.datum_.set_int(i * 7);
    ASSERT_EQ(OB_SUCCESS, store.push_back(value));
  }
  ASSERT_EQ(count, store.count());
  EXPECT_GT(store.mem_used(), store.get_block_size());
  for (int64_t i = 0; i < count; ++i) {
    EXPECT_EQ(i * 7, store.at(i).datum_.get_int());
    EXPECT_EQ(ObIntType, store.at(i).get_type());
    EXPECT_EQ(19, store.at(i).get_precision());
  }
}

TEST(DatumCore, metadata_keeps_legacy_four_byte_order_and_all_attributes)
{
  SqlTypeMeta meta; meta.set_varchar();
  meta.set_collation_level(CS_LEVEL_COERCIBLE);
  meta.set_collation_type(CS_TYPE_UTF8MB4_BIN); meta.set_scale(6);
  const unsigned char expected[] = {22, 4, 46, 6};
  char wire[64]; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(wire, sizeof(wire), pos));
  ASSERT_EQ(sizeof(expected), pos); EXPECT_EQ(pos, meta.get_serialize_size());
  EXPECT_EQ(0, memcmp(expected, wire, pos));
  SqlTypeMeta decoded; int64_t read = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire, pos, read));
  EXPECT_EQ(6, decoded.get_scale());
  EXPECT_EQ(meta.get_collation_type(), decoded.get_collation_type());
  EXPECT_EQ(meta.get_collation_level(), decoded.get_collation_level());
  meta.set_collection(0xfedc); pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(wire, sizeof(wire), pos)); read = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire, pos, read));
  EXPECT_EQ(0xfedc, decoded.get_subschema_id());
}

TEST(DatumCore, parameter_flags_keep_version_one_wire_and_runtime_extension)
{
  ParamFlag flags; flags.flag_ = 0xa5; flags.extend_flag_ = 0x5a;
  const unsigned char expected[] = {1, 0x81, 0x80, 0x80, 0x80, 0, 0xa5};
  char wire[64]; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, flags.serialize(wire, sizeof(wire), pos));
  ASSERT_EQ(sizeof(expected), pos); EXPECT_EQ(pos, flags.get_serialize_size());
  EXPECT_EQ(0, memcmp(wire, expected, pos));
  ParamFlag decoded; int64_t read = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire, pos, read));
  EXPECT_EQ(0xa5, decoded.flag_);
  EXPECT_EQ(0, decoded.extend_flag_); // Runtime extension is absent from version 1.
  ParamFlag copy(flags); EXPECT_EQ(0x5a, copy.extend_flag_);
}

TEST(DatumCore, native_parameter_reports_real_nop_sentinel)
{
  char bytes[DATUM_LEGACY_EXTEND_SIZE]; DatumParameter value;
  SqlTypeMeta meta; meta.set_ext(); value.set_type_meta(meta);
  value.datum_.ptr_ = bytes; value.datum_.set_ext_value(ObActionFlag::OP_NOP);
  EXPECT_TRUE(value.is_nop_value());
  value.datum_.set_ext_value(DATUM_MIN_EXT_VALUE); EXPECT_FALSE(value.is_nop_value());
  value.datum_.set_null(); EXPECT_FALSE(value.is_nop_value());
}

TEST(DatumCore, enumset_payload_decodes_without_value_bridge)
{
  ObString text("中文"); ObEnumSetInnerValue source(42, text), decoded;
  char wire[128]; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(wire, sizeof(wire), pos));
  ObDatum value; value.set_enumset_inner(wire, pos);
  ASSERT_EQ(OB_SUCCESS, value.get_enumset_inner(decoded));
  EXPECT_EQ(42, decoded.numberic_value_); EXPECT_EQ(text, decoded.string_value_);
}

TEST(DatumCore, lob_views_keep_borrowed_locator_and_storage_header)
{
  ObMemLobCommon memory_header(TEMP_FULL_LOB, true);
  ObDatum value;
  value.set_string(reinterpret_cast<const char *>(&memory_header), sizeof(memory_header));
  value.set_has_lob_header();
  ObLobLocatorV2 locator;
  value.get_mem_lob(locator);
  EXPECT_EQ(reinterpret_cast<char *>(&memory_header), locator.ptr_);
  EXPECT_EQ(sizeof(memory_header), locator.size_);
  EXPECT_TRUE(locator.has_lob_header_);
  EXPECT_TRUE(locator.is_valid(false));
  ObLobCommon storage_header;
  value.set_lob_data(storage_header, sizeof(storage_header));
  EXPECT_EQ(&storage_header, &value.get_lob_data());
}

TEST(DatumCore, native_array_checks_count_and_keeps_element_metadata)
{
  ObArenaAllocator arena;
  EXPECT_EQ(nullptr, DatumParameterArray::alloc(arena, -1));
  EXPECT_EQ(nullptr, DatumParameterArray::alloc(arena, INT64_MAX));
  DatumParameterArray *empty = DatumParameterArray::alloc(arena, 0);
  ASSERT_NE(nullptr, empty);
  EXPECT_EQ(0, empty->count_);
  DatumParameterArray *array = DatumParameterArray::alloc(arena, 2);
  ASSERT_NE(nullptr, array);
  SqlTypeMeta integer, text;
  integer.set_uint64(); text.set_varchar();
  array->data_[0].set_type_meta(integer);
  array->data_[0].set_precision(20);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].alloc_datum_reserved_buff(integer, 20, arena));
  array->data_[0].datum_.set_uint(UINT64_MAX);
  array->data_[1].set_type_meta(text);
  array->data_[1].set_length(37);
  array->data_[1].datum_.set_string("bytes", 5);
  EXPECT_EQ(UINT64_MAX, array->data_[0].datum_.get_uint());
  EXPECT_EQ(20, array->data_[0].get_precision());
  EXPECT_EQ(ObVarcharType, array->data_[1].get_type());
  EXPECT_EQ(37, array->data_[1].get_length());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {
class ParameterTestAllocator : public ObIAllocator
{
public:
  int64_t calls_ = 0;
  int64_t fail_at_ = INT64_MAX;
  std::unordered_set<void *> live_;
  void *alloc(int64_t size) override
  {
    if (++calls_ == fail_at_) { return nullptr; }
    void *p = std::malloc(size);
    if (p != nullptr) { live_.insert(p); }
    return p;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override
  {
    if (p != nullptr) {
      EXPECT_EQ(1, live_.erase(p));
      std::free(p);
    }
  }
  ~ParameterTestAllocator()
  {
    EXPECT_TRUE(live_.empty());
    for (void *p : live_) { std::free(p); }
  }
};
}

TEST(DatumCore, scalar_writers_own_bytes_and_keep_parameter_metadata)
{
  ObArenaAllocator arena;
  DatumParameter value;
  value.set_precision(31); value.set_scale(7); value.set_result_flag(0x1234);
  value.set_raw_text_info(11, 19);
#define CHECK_SIGNED_WRITE(setter, input, type) \
  ASSERT_EQ(OB_SUCCESS, value.setter(input, arena)); \
  EXPECT_EQ(type, value.get_type()); \
  EXPECT_EQ(static_cast<int64_t>(input), value.get_int()); \
  EXPECT_EQ(0, value.get_val_len());
  CHECK_SIGNED_WRITE(set_tinyint, INT8_MIN, ObTinyIntType)
  CHECK_SIGNED_WRITE(set_smallint, INT16_MIN, ObSmallIntType)
  CHECK_SIGNED_WRITE(set_mediumint, -8388608, ObMediumIntType)
  CHECK_SIGNED_WRITE(set_int32, INT32_MIN, ObInt32Type)
  CHECK_SIGNED_WRITE(set_int, INT64_MIN, ObIntType)
#undef CHECK_SIGNED_WRITE
#define CHECK_UNSIGNED_WRITE(setter, input, type) \
  ASSERT_EQ(OB_SUCCESS, value.setter(input, arena)); \
  EXPECT_EQ(type, value.get_type()); \
  EXPECT_EQ(static_cast<uint64_t>(input), value.get_uint64());
  CHECK_UNSIGNED_WRITE(set_utinyint, UINT8_MAX, ObUTinyIntType)
  CHECK_UNSIGNED_WRITE(set_usmallint, UINT16_MAX, ObUSmallIntType)
  CHECK_UNSIGNED_WRITE(set_umediumint, 16777215, ObUMediumIntType)
  CHECK_UNSIGNED_WRITE(set_uint32, UINT32_MAX, ObUInt32Type)
  CHECK_UNSIGNED_WRITE(set_uint64, UINT64_MAX, ObUInt64Type)
#undef CHECK_UNSIGNED_WRITE
  ASSERT_EQ(OB_SUCCESS, value.set_float(1.25f, arena));
  EXPECT_EQ(1.25f, value.get_float());
  ASSERT_EQ(OB_SUCCESS, value.set_double(-1.25, arena));
  EXPECT_EQ(-1.25, value.get_double());
  ASSERT_EQ(OB_SUCCESS, value.set_date(INT32_MIN, arena));
  EXPECT_EQ(INT32_MIN, value.get_date());
  ASSERT_EQ(OB_SUCCESS, value.set_datetime(INT64_MAX, arena));
  EXPECT_EQ(INT64_MAX, value.get_datetime());
  ASSERT_EQ(OB_SUCCESS, value.set_timestamp(INT64_MIN, arena));
  EXPECT_EQ(INT64_MIN, value.get_timestamp());
  ASSERT_EQ(OB_SUCCESS, value.set_time(-123456789, arena));
  EXPECT_EQ(-123456789, value.get_time());
  ASSERT_EQ(OB_SUCCESS, value.set_year(UINT8_MAX, arena));
  EXPECT_EQ(UINT8_MAX, value.get_year());
  ASSERT_EQ(OB_SUCCESS, value.set_unknown(27, arena));
  EXPECT_EQ(27, value.get_unknown());
  EXPECT_EQ(31, value.get_precision());
  EXPECT_EQ(7, value.get_scale());
  EXPECT_EQ(0x1234, value.get_result_flag());
  EXPECT_EQ(11, value.get_raw_text_pos());
  EXPECT_EQ(19, value.get_raw_text_len());
}

TEST(DatumCore, owned_parameter_writers_fail_without_changing_old_output)
{
  ObArenaAllocator arena;
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString("original"), arena));
  value.set_param_meta(); value.set_precision(17); value.set_result_flag(0x1234);
  const char *old_bytes = value.datum_.ptr_;
  const SqlTypeMeta old_meta = value.get_meta();
  ParameterTestAllocator reject; reject.fail_at_ = 1;
#define CHECK_FAILED_WRITE(expression) \
  reject.calls_ = 0; \
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, expression); \
  EXPECT_EQ(old_bytes, value.datum_.ptr_); \
  EXPECT_EQ(ObString("original"), value.get_string()); \
  EXPECT_EQ(old_meta, value.get_meta()); \
  EXPECT_EQ(17, value.get_precision()); \
  EXPECT_EQ(0x1234, value.get_result_flag());
  CHECK_FAILED_WRITE(value.set_int(42, reject))
  CHECK_FAILED_WRITE(value.set_year(126, reject))
  CHECK_FAILED_WRITE(value.set_varchar(ObString("new"), reject))
  CHECK_FAILED_WRITE(value.set_extend(42, uint8_t(1), reject, 123))
  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("123.45", 6, arena));
  CHECK_FAILED_WRITE(value.set_number(number, reject))
#undef CHECK_FAILED_WRITE
  ASSERT_EQ(OB_SUCCESS, value.set_number(number, arena));
  EXPECT_EQ(0, number.compare(value.get_number()));
  EXPECT_EQ(static_cast<int32_t>(number.d_.desc_), value.get_val_len());
  EXPECT_EQ(0, memcmp(value.datum_.ptr_ + sizeof(number::ObCompactNumber),
                      number.get_digits(), number.get_length() * sizeof(uint32_t)));
  SqlTypeMeta declared; declared.set_varchar(); value.set_param_meta(declared);
  SqlTypeMeta null_meta; null_meta.set_null(); value.set_param_meta(null_meta);
  value.set_null(); value.set_param_meta();
  EXPECT_EQ(declared, value.get_param_meta());
  EXPECT_EQ(0, value.get_val_len());
}

TEST(DatumCore, array_clone_failure_releases_partial_tree_and_preserves_output)
{
  ObArenaAllocator source;
  DatumParameter input;
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  input.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, input.alloc_datum_reserved_buff(meta, -1, source));
  auto *outer = DatumParameterArray::alloc(source, 2);
  ASSERT_NE(nullptr, outer); input.set_array_address(outer);
  ASSERT_EQ(OB_SUCCESS, outer->data_[0].set_varchar(ObString("outer"), source));
  auto *inner = DatumParameterArray::alloc(source, 1);
  ASSERT_NE(nullptr, inner);
  outer->data_[1].set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, outer->data_[1].alloc_datum_reserved_buff(meta, -1, source));
  outer->data_[1].set_array_address(inner);
  ASSERT_EQ(OB_SUCCESS, inner->data_[0].set_int(42, source));
  for (int64_t fail_at = 1; fail_at <= 7; ++fail_at) {
    ParameterTestAllocator target; target.fail_at_ = fail_at;
    DatumParameter output;
    ASSERT_EQ(OB_SUCCESS, output.set_varchar(ObString("unchanged"), source));
    const int ret = DatumParameterArray::clone_parameter(input, output, target);
    if (ret == OB_SUCCESS) {
      EXPECT_EQ(42, output.get_array<DatumParameterArray>()->data_[1]
                       .get_array<DatumParameterArray>()->data_[0].get_int());
      DatumParameterArray::release_owned(output, target);
      EXPECT_TRUE(output.is_null());
    } else {
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret);
      EXPECT_EQ(ObString("unchanged"), output.get_string());
    }
    EXPECT_TRUE(target.live_.empty()) << "failed allocation=" << fail_at;
    EXPECT_EQ(ObString("outer"), outer->data_[0].get_string());
  }
}

TEST(DatumCore, value_assignment_and_mutations_preserve_parameter_declaration)
{
  ObArenaAllocator arena;
  DatumParameter target, source, action;
  ASSERT_EQ(OB_SUCCESS, target.set_int(1, arena));
  ASSERT_EQ(OB_SUCCESS, source.set_int(42, arena));
  target.set_precision(31); target.set_scale(6); target.set_result_flag(0xa5);
  target.set_raw_text_info(7, 13);
  SqlTypeMeta declared; declared.set_varchar(); target.set_param_meta(declared);
  target.set_need_to_check_type(true); source.set_precision(2);
  ASSERT_EQ(OB_SUCCESS, target.apply(source));
  EXPECT_EQ(42, target.get_int());
  EXPECT_EQ(31, target.get_precision()); EXPECT_EQ(6, target.get_scale());
  EXPECT_EQ(0xa5, target.get_result_flag()); EXPECT_TRUE(target.need_to_check_type());
  EXPECT_EQ(declared, target.get_param_meta()); EXPECT_EQ(7, target.get_raw_text_pos());
  ASSERT_EQ(OB_SUCCESS, action.set_ext(ObActionFlag::OP_NOP, arena));
  ASSERT_EQ(OB_SUCCESS, target.apply(action)); EXPECT_EQ(42, target.get_int());
  ASSERT_EQ(OB_SUCCESS, action.set_ext(ObActionFlag::OP_DEL_ROW, arena));
  ASSERT_EQ(OB_SUCCESS, target.apply(action)); EXPECT_TRUE(target.is_null());
  EXPECT_EQ(31, target.get_precision()); EXPECT_EQ(declared, target.get_param_meta());
  ASSERT_EQ(OB_SUCCESS, target.set_int(99, arena));
  ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString("wrong type"), arena));
  EXPECT_EQ(OB_INVALID_ARGUMENT, target.apply(source)); EXPECT_EQ(99, target.get_int());
  ASSERT_EQ(OB_SUCCESS, action.set_ext(0x123456, arena));
  EXPECT_EQ(OB_INVALID_ARGUMENT, target.apply(action)); EXPECT_EQ(99, target.get_int());
  source.set_null(); ASSERT_EQ(OB_SUCCESS, target.apply(source)); EXPECT_TRUE(target.is_null());
  ASSERT_EQ(OB_SUCCESS, target.set_ext(ObActionFlag::OP_ROW_DOES_NOT_EXIST, arena));
  ASSERT_EQ(OB_SUCCESS, action.set_ext(ObActionFlag::OP_NOP, arena));
  ASSERT_EQ(OB_SUCCESS, target.apply(action)); EXPECT_TRUE(target.is_null());
}

TEST(DatumCore, checked_parameter_readers_do_not_read_null_or_wrong_typed_payloads)
{
  ObArenaAllocator arena;
  DatumParameter value; SqlTypeMeta type; type.set_int32(); value.set_type_meta(type);
  int32_t i32 = 123; int64_t i64 = 456;
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_int32(i32)); EXPECT_EQ(123, i32);
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_int(i64)); EXPECT_EQ(456, i64);
  ASSERT_EQ(OB_SUCCESS, value.set_int32(INT32_MIN, arena));
  ASSERT_EQ(OB_SUCCESS, value.get_int32(i32)); EXPECT_EQ(INT32_MIN, i32);
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_int(i64)); EXPECT_EQ(456, i64);
  ASSERT_EQ(OB_SUCCESS, value.set_int(INT64_MIN, arena));
  ASSERT_EQ(OB_SUCCESS, value.get_int(i64)); EXPECT_EQ(INT64_MIN, i64);
  number::ObNumber number;
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_number(number));
  ObString text("unchanged");
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_varchar(text)); EXPECT_EQ(ObString("unchanged"), text);
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString("native bytes"), arena));
  ASSERT_EQ(OB_SUCCESS, value.get_varchar(text)); EXPECT_EQ(ObString("native bytes"), text);
  value.set_null(); EXPECT_TRUE(value.is_null_or_empty_string());
  type.set_ext(); type.set_extend_type(uint8_t(1)); value.set_type_meta(type);
  EXPECT_FALSE(value.is_pl_extend()); EXPECT_EQ(0, value.get_ext());
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_string(text));
}

TEST(DatumCore, character_length_and_string_views_preserve_byte_and_character_rules)
{
  DatumParameter value; ObAccuracy accuracy; int32_t length = -1;
  value.set_string_view(ObCharType, ObString("中文ab"));
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, value.get_char_length(accuracy, length)); EXPECT_EQ(4, length);
  value.set_collation_type(CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_UNEXPECTED, value.get_char_length(accuracy, length));
  EXPECT_EQ(4, length); // BINARY padding uses bytes through a separate path.
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  value.set_varchar_view(ObString("")); EXPECT_TRUE(value.is_null_or_empty_string());
  EXPECT_EQ(OB_ERR_UNEXPECTED, value.get_char_length(accuracy, length));
  value.set_string_view(ObJsonType, ObString("")); EXPECT_FALSE(value.is_null_or_empty_string());
}

TEST(DatumCore, strict_boolean_conditions_keep_null_and_non_integer_distinct)
{
  ObArenaAllocator arena;
  DatumParameter value;
  EXPECT_FALSE(value.is_true()); EXPECT_FALSE(value.is_false());
  ASSERT_EQ(OB_SUCCESS, value.set_int(0, arena));
  EXPECT_FALSE(value.is_true()); EXPECT_TRUE(value.is_false());
  ASSERT_EQ(OB_SUCCESS, value.set_int(-7, arena));
  EXPECT_TRUE(value.is_true()); EXPECT_FALSE(value.is_false());
  ASSERT_EQ(OB_SUCCESS, value.set_tinyint(1, arena)); EXPECT_TRUE(value.is_true());
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(1, arena));
  EXPECT_FALSE(value.is_true()); EXPECT_FALSE(value.is_false());
  value.set_null(); EXPECT_FALSE(value.is_true()); EXPECT_FALSE(value.is_false());
}

TEST(DatumCore, checked_boolean_parameter_matches_tinyint_contract)
{
  ObArenaAllocator arena;
  DatumParameter value;
  bool output = true;
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_bool(output)); EXPECT_TRUE(output);
  ASSERT_EQ(OB_SUCCESS, value.set_tinyint(0, arena));
  EXPECT_EQ(OB_SUCCESS, value.get_bool(output)); EXPECT_FALSE(output);
  ASSERT_EQ(OB_SUCCESS, value.set_tinyint(-128, arena));
  EXPECT_EQ(OB_SUCCESS, value.get_bool(output)); EXPECT_TRUE(output);
  ASSERT_EQ(OB_SUCCESS, value.set_int(0, arena));
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_bool(output)); EXPECT_TRUE(output);
  value.set_null();
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, value.get_bool(output)); EXPECT_TRUE(output);
}

TEST(DatumCore, mysql_date_parameters_preserve_native_bit_patterns)
{
  ObArenaAllocator arena;
  DatumParameter datetime, date;
  const ObMySQLDateTime packed_datetime(INT64_C(0x123456789abcde));
  const ObMySQLDate packed_date(0x123456);
  ASSERT_EQ(OB_SUCCESS, datetime.set_mysql_datetime(packed_datetime, arena));
  ASSERT_TRUE(datetime.is_mysql_datetime()); EXPECT_FALSE(datetime.is_datetime());
  EXPECT_EQ(packed_datetime, datetime.get_mysql_datetime());
  ASSERT_EQ(OB_SUCCESS, date.set_mysql_date(packed_date, arena));
  ASSERT_TRUE(date.is_mysql_date()); EXPECT_FALSE(date.is_date());
  EXPECT_EQ(packed_date, date.get_mysql_date());
  DatumParameter copy;
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(datetime, arena));
  EXPECT_NE(datetime.datum_.ptr_, copy.datum_.ptr_);
  EXPECT_EQ(packed_datetime, copy.get_mysql_datetime());
}

TEST(DatumCore, compact_member_setters_keep_sixteen_byte_layout_and_owned_payloads)
{
  EXPECT_EQ(16, sizeof(DatumValue));
  ParameterTestAllocator owner;
  DatumValue member;
  ASSERT_EQ(OB_SUCCESS, member.set_int(42, owner));
  EXPECT_EQ(42, member.get_int()); EXPECT_EQ(8, member.datum_.len_);
  DatumParameter view; view.set_precision(19); view.set_result_flag(123); view.set_param_meta();
  view.assign_value(member);
  EXPECT_EQ(42, view.get_int()); EXPECT_EQ(member.datum_.ptr_, view.datum_.ptr_);
  EXPECT_EQ(19, view.get_precision()); EXPECT_EQ(123, view.get_result_flag());
  DatumParameterArray::release_owned(view, owner); member.set_null();
  ASSERT_EQ(OB_SUCCESS, member.set_extend(65536, 3, owner, 4096));
  EXPECT_TRUE(member.is_pl_extend()); EXPECT_EQ(65536, member.get_ext());
  EXPECT_EQ(4096, member.get_val_len()); EXPECT_EQ(DATUM_EXTEND_PAYLOAD_SIZE, member.datum_.len_);
  view.assign_value(member); DatumParameterArray::release_owned(view, owner); member.set_null();
  EXPECT_TRUE(owner.live_.empty());
}
TEST(DatumCore, compact_member_allocation_failure_preserves_previous_value)
{
  ParameterTestAllocator owner;
  DatumValue member;
  ASSERT_EQ(OB_SUCCESS, member.set_int(42, owner));
  const char *old = member.datum_.ptr_; const SqlTypeMeta meta = member.get_meta();
  owner.fail_at_ = owner.calls_ + 1;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, member.set_extend(65536, 3, owner, 123));
  EXPECT_EQ(old, member.datum_.ptr_); EXPECT_EQ(meta, member.get_meta()); EXPECT_EQ(42, member.get_int());
  owner.fail_at_ = owner.calls_ + 1;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, member.set_int(99, owner));
  EXPECT_EQ(old, member.datum_.ptr_); EXPECT_EQ(42, member.get_int());
  DatumParameter view; view.assign_value(member); DatumParameterArray::release_owned(view, owner);
}
TEST(DatumCore, compact_member_snapshot_survives_source_arena_and_preserves_raw_metadata)
{
  ParameterTestAllocator owner;
  DatumParameter snapshot;
  {
    ObArenaAllocator source_owner;
    DatumParameter text;
    ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("客户"), source_owner));
    text.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
    DatumValue member = text.value_view();
    DatumParameter view; view.assign_value(member);
    ASSERT_EQ(OB_SUCCESS, DatumParameterArray::clone_parameter(view, snapshot, owner));
    EXPECT_NE(member.datum_.ptr_, snapshot.datum_.ptr_);
    EXPECT_EQ(member.get_meta(), snapshot.get_meta());
  }
  EXPECT_EQ(ObString::make_string("客户"), snapshot.get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, snapshot.get_collation_type());
  DatumParameterArray::release_owned(snapshot, owner);
  EXPECT_TRUE(owner.live_.empty());
}

namespace {
int init_default_constants()
{
  static ObArenaAllocator constants;
  static const int result = wide::ObDecimalIntConstValue::init_const_values(constants);
  return result;
}
std::string default_value_wire(const DatumValue &value)
{
  int64_t size = 0, pos = 0;
  if (DatumParameterCodec::value_size(value.datum_, value.meta_, size) != OB_SUCCESS) { return "SIZE_ERROR"; }
  std::string bytes(size, '\0');
  if (DatumParameterCodec::encode_value(value.datum_, value.meta_, &bytes[0], size, pos) != OB_SUCCESS) {
    return "ENCODE_ERROR";
  }
  const char *digits = "0123456789abcdef";
  std::string output;
  for (unsigned char byte : bytes) { output += digits[byte >> 4]; output += digits[byte & 15]; }
  return output;
}
}

TEST(DatumCore, native_defaults_match_frozen_original_generator)
{
  ASSERT_EQ(OB_SUCCESS, init_default_constants());
  std::ifstream input("unittest/oblib/data/default_values.golden");
  ASSERT_TRUE(input.good());
  ObArenaAllocator owner;
  std::string line;
  int64_t checked = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    std::istringstream fields(line);
    int type, precision, collation, scale, level, expected;
    std::string wire;
    ASSERT_TRUE(static_cast<bool>(fields >> type >> precision >> collation >> scale >> level >> expected >> wire));
    SCOPED_TRACE(line);
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, value.set_int(918, owner));
    value.set_type(static_cast<SqlValueType>(type));
    value.set_scale(scale);
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    value.set_collation_level(static_cast<ObCollationLevel>(level));
    const SqlTypeMeta original_meta = value.get_meta();
    const char *original_payload = value.datum_.ptr_;
    const int result = value.build_not_strict_default_value(precision,
        static_cast<ObCollationType>(collation), owner);
    ASSERT_EQ(expected, result);
    if (result == OB_SUCCESS) {
      EXPECT_EQ(wire, default_value_wire(value));
    } else {
      EXPECT_EQ(original_meta, value.get_meta());
      EXPECT_EQ(original_payload, value.datum_.ptr_);
    }
    ++checked;
  }
  EXPECT_EQ(5676, checked);
}

TEST(DatumCore, native_default_payloads_survive_input_arena)
{
  ASSERT_EQ(OB_SUCCESS, init_default_constants());
  ObArenaAllocator owner;
  std::vector<DatumValue> results;
  const SqlValueType types[] = {ObIntType, ObNumberType, ObJsonType, ObDecimalIntType, ObEnumType};
  for (SqlValueType type : types) {
    DatumValue value;
    {
      ObArenaAllocator source;
      ASSERT_EQ(OB_SUCCESS, value.set_int(918, source));
      value.set_type(type);
      ASSERT_EQ(OB_SUCCESS, value.build_not_strict_default_value(18, CS_TYPE_UTF8MB4_BIN, owner));
    }
    results.push_back(value);
  }
  EXPECT_EQ(0, results[0].get_int());
  EXPECT_TRUE(results[1].get_number().is_zero());
  ASSERT_EQ(2, results[2].get_string_len());
  EXPECT_EQ(0, results[2].datum_.ptr_[0]); EXPECT_EQ(0, results[2].datum_.ptr_[1]);
  EXPECT_TRUE(wide::is_zero(results[3].get_decimal_int(), results[3].get_int_bytes()));
  EXPECT_EQ(1, results[4].get_enum());
}

TEST(DatumCore, native_default_owner_failure_does_not_publish_partial_state)
{
  ASSERT_EQ(OB_SUCCESS, init_default_constants());
  ObArenaAllocator source;
  const SqlValueType types[] = {ObIntType, ObNumberType, ObJsonType, ObDecimalIntType, ObEnumType};
  for (SqlValueType type : types) {
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, value.set_int(918, source));
    value.set_type(type);
    const char *payload = value.datum_.ptr_;
    const SqlTypeMeta meta = value.get_meta();
    ParameterTestAllocator failure;
    failure.fail_at_ = 1;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
        value.build_not_strict_default_value(18, CS_TYPE_UTF8MB4_BIN, failure));
    EXPECT_EQ(payload, value.datum_.ptr_);
    EXPECT_EQ(meta, value.get_meta());
    EXPECT_EQ(918, value.datum_.get_int());
    EXPECT_TRUE(failure.live_.empty());
  }
}
