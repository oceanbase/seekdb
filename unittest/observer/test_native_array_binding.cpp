/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
// The existing handler header relies on the production include/using context.
#include "share/ob_server_struct.h"
#include "sql/ob_query_retry_ctrl.h"
using oceanbase::share::ObGlobalContext;
using oceanbase::sql::ObQueryRetryCtrl;
#include "observer/mysql/obmp_stmt_execute.h"
#include "sql/pl/ob_pl_user_type.h"
#include "common/datum/datum_value_buffer.h"
#include "common/datum/datum_sql_print.h"
#include "common/datum/datum_parameter_codec.h"
#include "sql/ob_spi.h"
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include <set>
#include <cstdlib>
#include <gtest/gtest.h>
#include <cstring>
#include <string>

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;
using namespace oceanbase::observer;
using namespace oceanbase::obmysql;

namespace oceanbase { namespace observer {
struct NativeArrayBindingTestAccess {
  static void configure(ObMPStmtExecute &handler, ParamStore &arrays, ParamStore &row,
                        sql::stmt::StmtType type)
  {
    handler.arraybinding_params_ = &arrays;
    handler.params_ = &row;
    handler.stmt_type_ = type;
  }
  static int row(ObMPStmtExecute &handler, int64_t index, ObIAllocator &owner)
  { return handler.construct_execute_param_for_arraybinding(index, owner); }
  static int release(ObMPStmtExecute &handler, ObIAllocator &owner, ObSQLSessionInfo &session)
  { return handler.release_arraybinding_row(owner, session); }
  static int parse_complex(ObMPStmtExecute &handler, ObIAllocator &owner,
                           TypeInfo &type, const char *&input, DatumParameter &out)
  {
    return handler.parse_complex_param_value(owner, CHARSET_UTF8MB4,
        CS_TYPE_UTF8MB4_BIN, input, nullptr, &type, out);
  }
};
} }

namespace {
void attach(ObPLCollection &collection, DatumValueBuffer &values, int64_t count)
{
  collection.set_data(values.data(), values.capacity());
  collection.set_count(count);
  collection.set_column_count(1);
  collection.set_first(1);
  collection.set_last(count);
}
void store_collection(ObPLCollection &collection, ParamStore &arrays,
                      ParamStore &row, ObIAllocator &owner)
{
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(&collection),
      PL_NESTED_TABLE_TYPE, owner));
  ASSERT_EQ(OB_SUCCESS, arrays.push_back(parameter));
  ASSERT_EQ(OB_SUCCESS, parameter.set_int(99, owner));
  ASSERT_EQ(OB_SUCCESS, row.push_back(parameter));
}
std::string literal(const DatumParameter &value)
{
  char buffer[256]; int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, value.print_sql_literal(buffer, sizeof(buffer), pos));
  return std::string(buffer, pos);
}
}

TEST(NativeArrayBinding, actual_collection_assign_and_two_elements_use_native_layout)
{
  // DatumValue and Obj both happen to be16 bytes. Matching stride does not
  // make their pointer/pack/metadata and metadata/length/union layouts alike.
  static_assert(sizeof(DatumValue) == 16, "native element layout");
  ObArenaAllocator source_owner, collection_owner, output_owner;
  DatumValueBuffer source(source_owner);
  ASSERT_EQ(OB_SUCCESS, source.reserve(2));
  ASSERT_EQ(OB_SUCCESS, source.set_int(0, 11));
  ASSERT_EQ(OB_SUCCESS, source.set_int(1, 22));
  ObPLCollection input(PL_NESTED_TABLE_TYPE, OB_INVALID_ID);
  attach(input, source, 2);
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID);
  ASSERT_EQ(OB_SUCCESS, collection.assign(&input, &collection_owner));
  source.reset(); source_owner.reset();
  ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
  ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, output_owner));
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_ANONYMOUS_BLOCK);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, output_owner));
  EXPECT_EQ(ObIntType, row.at(0).get_type()); EXPECT_EQ(11, row.at(0).get_int());
  DatumParameter first = row.at(0);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 1, output_owner));
  EXPECT_EQ(ObIntType, row.at(0).get_type()); EXPECT_EQ(22, row.at(0).get_int());
  collection.set_data(nullptr, 0); collection.set_count(0); collection_owner.reset();
  EXPECT_EQ(11, first.get_int()); EXPECT_EQ(22, row.at(0).get_int());
}

TEST(NativeArrayBinding, prepared_statement_numeric_values_decode_directly_to_datum)
{
  ObArenaAllocator owner;
  const ObTimeZoneInfo *tz_info = nullptr;
  const auto check_integer = [&](uint32_t wire_type, int64_t wire_value,
                                 bool is_unsigned, SqlValueType expected_type) {
    char bytes[sizeof(int64_t)] = {};
    MEMCPY(bytes, &wire_value, sizeof(wire_value));
    const char *native_input = bytes;
    DatumParameter native;
    EXPECT_EQ(OB_SUCCESS, ObMPStmtExecute::parse_basic_param_value(
        owner, wire_type, nullptr, CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_BIN,
        native_input, tz_info, native, false, nullptr, is_unsigned));
    EXPECT_EQ(bytes + sizeof(int64_t), native_input);
    EXPECT_EQ(expected_type, native.get_type());
    EXPECT_EQ(CS_LEVEL_NUMERIC, native.get_collation_level());
    switch (expected_type) {
      case ObTinyIntType:
        EXPECT_EQ(static_cast<int8_t>(wire_value), native.value_view().get_int());
        break;
      case ObUTinyIntType:
        EXPECT_EQ(static_cast<uint8_t>(wire_value), native.value_view().get_uint64());
        break;
      case ObSmallIntType:
        EXPECT_EQ(static_cast<int16_t>(wire_value), native.value_view().get_int());
        break;
      case ObUSmallIntType:
        EXPECT_EQ(static_cast<uint16_t>(wire_value), native.value_view().get_uint64());
        break;
      case ObInt32Type:
        EXPECT_EQ(static_cast<int32_t>(wire_value), native.value_view().get_int32());
        break;
      case ObUInt32Type:
        EXPECT_EQ(static_cast<uint32_t>(wire_value), native.value_view().get_uint64());
        break;
      case ObIntType:
        EXPECT_EQ(wire_value, native.value_view().get_int());
        break;
      case ObUInt64Type:
        EXPECT_EQ(static_cast<uint64_t>(wire_value), native.value_view().get_uint64());
        break;
      default:
        FAIL() << "unexpected integer type " << expected_type;
    }
  };

  check_integer(MYSQL_TYPE_TINY, -12, false, ObTinyIntType);
  check_integer(MYSQL_TYPE_TINY, 250, true, ObUTinyIntType);
  check_integer(MYSQL_TYPE_SHORT, -1234, false, ObSmallIntType);
  check_integer(MYSQL_TYPE_SHORT, 60000, true, ObUSmallIntType);
  check_integer(MYSQL_TYPE_LONG, -12345678, false, ObInt32Type);
  check_integer(MYSQL_TYPE_LONG, 4000000000LL, true, ObUInt32Type);
  check_integer(MYSQL_TYPE_LONGLONG, -1234567890123LL, false, ObIntType);
  check_integer(MYSQL_TYPE_LONGLONG, static_cast<int64_t>(UINT64_MAX), true, ObUInt64Type);

  const struct { uint32_t wire_type; SqlValueType expected_type; bool is_float; } float_cases[] = {
      {MYSQL_TYPE_FLOAT, ObFloatType, true},
      {MYSQL_TYPE_ORA_BINARY_FLOAT, ObFloatType, true},
      {MYSQL_TYPE_DOUBLE, ObDoubleType, false},
      {MYSQL_TYPE_ORA_BINARY_DOUBLE, ObDoubleType, false}};
  for (const auto &test_case : float_cases) {
    const uint32_t wire_type = test_case.wire_type;
    const SqlValueType expected_type = test_case.expected_type;
    const bool is_float = test_case.is_float;
    char bytes[sizeof(double)] = {};
    if (is_float) {
      const float expected = 1.25f;
      MEMCPY(bytes, &expected, sizeof(expected));
    } else {
      const double expected = -9.5;
      MEMCPY(bytes, &expected, sizeof(expected));
    }
    const char *native_input = bytes;
    DatumParameter native;
    ASSERT_EQ(OB_SUCCESS, ObMPStmtExecute::parse_basic_param_value(
        owner, wire_type, nullptr, CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_BIN,
        native_input, tz_info, native));
    EXPECT_EQ(bytes + (is_float ? sizeof(float) : sizeof(double)), native_input);
    EXPECT_EQ(expected_type, native.get_type());
    if (is_float) {
      EXPECT_EQ(1.25f, native.value_view().get_float());
    } else {
      EXPECT_EQ(-9.5, native.value_view().get_double());
    }
  }

  int16_t wire_year = 2025;
  const char *native_input = reinterpret_cast<const char *>(&wire_year);
  DatumParameter native_year;
  ASSERT_EQ(OB_SUCCESS, ObMPStmtExecute::parse_basic_param_value(
      owner, MYSQL_TYPE_YEAR, nullptr, CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_BIN,
      native_input, tz_info, native_year));
  EXPECT_EQ(reinterpret_cast<const char *>(&wire_year) + sizeof(wire_year), native_input);
  EXPECT_EQ(ObYearType, native_year.get_type());
  EXPECT_EQ(125, native_year.value_view().get_year()); // YEAR stores the offset from 1900.
}

TEST(NativeArrayBinding, prepared_statement_temporal_string_and_decimal_values_decode_to_datum)
{
  ObArenaAllocator owner;
  const auto parse = [&](uint32_t wire_type, const char *bytes, int64_t size,
                         SqlValueType expected_type) {
    const char *native_input = bytes;
    DatumParameter native;
    const int ret = ObMPStmtExecute::parse_basic_param_value(
        owner, wire_type, nullptr, CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_BIN,
        native_input, nullptr, native, false, nullptr);
    EXPECT_EQ(OB_SUCCESS, ret);
    EXPECT_EQ(bytes + size, native_input);
    EXPECT_EQ(expected_type, native.get_type());
    return native;
  };

  const char date[] = {4, static_cast<char>(0xE8), 0x07, 2, 29};
  DatumParameter date_value = parse(MYSQL_TYPE_DATE, date, sizeof(date), ObDateType);
  EXPECT_EQ(19782, date_value.value_view().get_date()); // 2024-02-29, days since epoch
  const char datetime[] = {11, static_cast<char>(0xE8), 0x07, 2, 29,
                           12, 34, 56, 0x40, 0xE2, 0x01, 0x00};
  const int64_t datetime_value = 19782LL * USECS_PER_DAY + 12LL * USECS_PER_HOUR +
      34LL * USECS_PER_MINUTE + 56LL * USECS_PER_SEC + 123456;
  DatumParameter datetime_datum = parse(MYSQL_TYPE_DATETIME, datetime, sizeof(datetime), ObDateTimeType);
  EXPECT_EQ(datetime_value, datetime_datum.value_view().get_datetime());
  DatumParameter timestamp_datum = parse(MYSQL_TYPE_TIMESTAMP, datetime, sizeof(datetime), ObTimestampType);
  EXPECT_EQ(datetime_value, timestamp_datum.value_view().get_timestamp());
  const char time[] = {12, 1, 2, 0, 0, 0, 3, 4, 5,
                       0x20, 0xA1, 0x07, 0x00};
  DatumParameter time_value = parse(MYSQL_TYPE_TIME, time, sizeof(time), ObTimeType);
  EXPECT_EQ(-(26LL * USECS_PER_HOUR + 3LL * USECS_PER_MINUTE + 4LL * USECS_PER_SEC + 500000),
            time_value.value_view().get_time());
  const char string[] = {5, 'h', 'e', 'l', 'l', 'o'};
  DatumParameter string_value = parse(MYSQL_TYPE_VARCHAR, string, sizeof(string), ObVarcharType);
  EXPECT_EQ("hello", string_value.get_varchar());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, string_value.get_collation_type());
  const char decimal[] = {8, '1', '2', '3', '4', '.', '5', '0', '0'};
  DatumParameter decimal_value = parse(MYSQL_TYPE_NEWDECIMAL, decimal, sizeof(decimal), ObNumberType);
  number::ObNumber expected_decimal;
  ASSERT_EQ(OB_SUCCESS, expected_decimal.from("1234.500", owner));
  EXPECT_EQ(0, expected_decimal.compare(decimal_value.get_number()));
}

TEST(NativeArrayBinding, unsupported_oracle_timestamp_wire_types_preserve_parse_error)
{
  ObArenaAllocator owner;
  const uint32_t wire_types[] = {
      MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE,
      MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE,
      MYSQL_TYPE_OB_TIMESTAMP_NANO};
  for (const uint32_t wire_type : wire_types) {
    SCOPED_TRACE(wire_type);
    const char bytes[] = {0};
    const char *native_input = bytes;
    DatumParameter native;
    const int native_ret = ObMPStmtExecute::parse_basic_param_value(
        owner, wire_type, nullptr, CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_BIN,
        native_input, nullptr, native, false, nullptr);
    EXPECT_EQ(OB_OBJ_TYPE_ERROR, native_ret);
    // Parsing consumes the length byte before type mapping fails, but does not
    // consume the payload or publish a value.
    EXPECT_EQ(bytes + 1, native_input);
    EXPECT_TRUE(native.is_null());
  }
}

TEST(NativeArrayBinding, real_sql_binding_promotes_only_the_six_integer_families)
{
  ObArenaAllocator owner;
  DatumValueBuffer values(owner);
  ASSERT_EQ(OB_SUCCESS, values.reserve(7));
  DatumParameter source;
  ASSERT_EQ(OB_SUCCESS, source.set_smallint(INT16_MIN, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(0, source.value_view()));
  ASSERT_EQ(OB_SUCCESS, source.set_usmallint(UINT16_MAX, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(1, source.value_view()));
  ASSERT_EQ(OB_SUCCESS, source.set_int32(INT32_MIN, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(2, source.value_view()));
  ASSERT_EQ(OB_SUCCESS, source.set_uint32(UINT32_MAX, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(3, source.value_view()));
  ASSERT_EQ(OB_SUCCESS, source.set_int(INT64_MIN, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(4, source.value_view()));
  ASSERT_EQ(OB_SUCCESS, source.set_uint64(UINT64_MAX, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(5, source.value_view()));
  ASSERT_EQ(OB_SUCCESS, source.set_tinyint(7, owner));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(6, source.value_view()));
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID);
  attach(collection, values, 7);
  ParamStore arrays{ObWrapperAllocator(owner)}, row{ObWrapperAllocator(owner)};
  ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, owner));
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_INSERT);
  const char *expected[] = {"-32768", "65535", "-2147483648", "4294967295",
      "-9223372036854775808", "18446744073709551615", "7"};
  for (int64_t i = 0; i < 7; ++i) {
    SCOPED_TRACE(i);
    ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, i, owner));
    EXPECT_EQ(i < 6 ? ObNumberType : ObTinyIntType, row.at(0).get_type());
    EXPECT_EQ(expected[i], literal(row.at(0)));
  }
}

TEST(NativeArrayBinding, string_and_typed_null_are_owned_without_integer_dereference)
{
  ObArenaAllocator source_owner, output_owner;
  DatumValueBuffer values(source_owner);
  ASSERT_EQ(OB_SUCCESS, values.reserve(2));
  char input[] = "a collection element longer than sixteen bytes";
  ASSERT_EQ(OB_SUCCESS, values.set_varchar_view(0, ObString::make_string(input)));
  values.at(0).set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, values.set_null(1));
  values.at(1).meta_.set_int();
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID);
  attach(collection, values, 2);
  ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
  ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, output_owner));
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_INSERT);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, output_owner));
  DatumParameter saved = row.at(0);
  ASSERT_NE(input, saved.datum_.ptr_);
  std::memset(input, '~', sizeof(input) - 1);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 1, output_owner));
  EXPECT_TRUE(row.at(0).is_null());
  values.reset(); source_owner.reset();
  EXPECT_EQ(ObString::make_string("a collection element longer than sixteen bytes"), saved.get_string());
}

TEST(NativeArrayBinding, per_cell_oom_and_invalid_position_do_not_publish_or_read_elements)
{
  ObArenaAllocator owner;
  DatumValueBuffer values(owner);
  ASSERT_EQ(OB_SUCCESS, values.reserve(1));
  ASSERT_EQ(OB_SUCCESS, values.set_int(0, 123));
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID);
  attach(collection, values, 1);
  ParamStore arrays{ObWrapperAllocator(owner)}, row{ObWrapperAllocator(owner)};
  ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, owner));
  row.at(0).set_precision(12); row.at(0).set_scale(3);
  const DatumParameter previous = row.at(0);
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_INSERT);
  ObWrapperAllocator failing;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, NativeArrayBindingTestAccess::row(handler, 0, failing));
  EXPECT_EQ(previous.datum_.ptr_, row.at(0).datum_.ptr_);
  EXPECT_EQ(99, row.at(0).get_int());
  EXPECT_EQ(12, row.at(0).get_precision()); EXPECT_EQ(3, row.at(0).get_scale());
  EXPECT_EQ(OB_ERR_UNEXPECTED, NativeArrayBindingTestAccess::row(handler, -1, owner));
  EXPECT_EQ(OB_ERR_UNEXPECTED, NativeArrayBindingTestAccess::row(handler, 1, owner));
  EXPECT_EQ(previous.datum_.ptr_, row.at(0).datum_.ptr_);
}

TEST(NativeArrayBinding, actual_ce_complex_producer_remains_unsupported_before_element_read)
{
  ObArenaAllocator owner;
  TypeInfo type;
  type.is_basic_type_ = false; type.is_elem_type_ = true;
  type.elem_type_.set_obj_type(ObSmallIntType);
  char wire[] = {2, 0, 11, 0, 22, 0};
  const char *position = wire;
  DatumParameter result;
  ASSERT_EQ(OB_SUCCESS, result.set_int(99, owner));
  const char *original = result.datum_.ptr_;
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  EXPECT_EQ(OB_NOT_SUPPORTED, NativeArrayBindingTestAccess::parse_complex(
      handler, owner, type, position, result));
  EXPECT_EQ(wire, position); EXPECT_EQ(original, result.datum_.ptr_);
  EXPECT_EQ(99, result.get_int());
}

namespace {
std::vector<std::string> fields(const std::string &line)
{
  std::vector<std::string> out; std::istringstream stream(line); std::string field;
  while (std::getline(stream, field, '\t')) { out.push_back(field); }
  return out;
}
std::string hex(const void *data, int64_t length)
{
  static const char digits[] = "0123456789abcdef";
  const unsigned char *bytes = static_cast<const unsigned char *>(data);
  std::string result;
  for (int64_t i = 0; i < length; ++i) {
    result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15];
  }
  return result;
}
std::string unhex(const std::string &text)
{
  std::string result;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    result += static_cast<char>(std::strtoul(text.substr(i, 2).c_str(), nullptr, 16));
  }
  return result;
}
std::string parameter_wire(const DatumParameter &value)
{
  int64_t size = 0, pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::parameter_size(value, size));
  std::string bytes(size, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_parameter(value, &bytes[0], size, pos));
  return hex(bytes.data(), pos);
}
std::string flag_wire(const ParamFlag &flag)
{
  std::string bytes(flag.get_serialize_size(), '\0'); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, flag.serialize(&bytes[0], bytes.size(), pos));
  return hex(bytes.data(), pos);
}
void seed(DatumParameter &value, int mode, ObIAllocator &owner)
{
  value = DatumParameter();
  if (mode == 1) {
    ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString::make_string("seed"), owner));
    value.set_precision(12); value.set_scale(3);
    SqlTypeMeta declared; declared.set_int(); value.set_param_meta(declared);
    value.set_result_flag(0x102); value.set_need_to_check_type(true);
    value.set_need_to_check_bool_value(true); value.set_raw_text_info(17, 23);
  } else if (mode == 2) {
    value.set_null(); value.set_precision(PRECISION_UNKNOWN_YET);
    value.set_scale(NUMBER_SCALE_UNKNOWN_YET);
    SqlTypeMeta declared; declared.set_timestamp(); declared.set_scale(6);
    value.set_param_meta(declared); value.set_result_flag(0x40);
    value.set_expected_bool_value(true); value.set_raw_text_info(31, 37);
  }
}
std::string capture_fields(int mode, int initial, const std::string &name,
                           int64_t index, int ret, const DatumParameter &out)
{
  std::ostringstream result;
  result << "R\t" << mode << '\t' << initial << '\t' << name << '\t' << index << '\t' << ret
    << '\t' << hex(&out.get_meta(), sizeof(SqlTypeMeta))
    << '\t' << hex(&out.get_param_meta(), sizeof(SqlTypeMeta))
    << '\t' << hex(&out.get_accuracy(), sizeof(ObAccuracy))
    << '\t' << out.get_result_flag() << '\t' << flag_wire(out.get_param_flag())
    << '\t' << out.get_raw_text_pos() << '\t' << out.get_raw_text_len()
    << '\t' << parameter_wire(out);
  return result.str();
}
ObPLRecord *record(ObIAllocator &owner, int64_t value)
{
  void *memory = owner.alloc(ObRecordType::get_init_size(1));
  if (memory == nullptr) { return nullptr; }
  auto *result = new (memory) ObPLRecord(42, 1);
  if (result->init_data(owner, true) != OB_SUCCESS) { return nullptr; }
  new (result->get_element_type()) ObDataType();
  SqlTypeMeta meta; meta.set_int(); result->get_element_type()[0].set_meta_type(meta);
  if (result->get_element()[0].set_int(value, *result->get_allocator()) != OB_SUCCESS) { return nullptr; }
  return result;
}
class BudgetOwner : public ObIAllocator
{
public:
  BudgetOwner() : budget(0), reject_size(-1) {}
  void *alloc(const int64_t size) override
  {
    void *pointer = size != reject_size && budget-- > 0 ? arena.alloc(size) : nullptr;
    if (pointer != nullptr) { live.insert(pointer); }
    return pointer;
  }
  void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { live.erase(pointer); arena.free(pointer); }
  int budget;
  int64_t reject_size;
  std::set<void *> live;
  ObArenaAllocator arena;
};
}

TEST(NativeArrayBinding, actual_old_234_results_keep_every_metadata_flag_and_wire_field)
{
  std::ifstream input("unittest/observer/data/native_array_binding.golden");
  ASSERT_TRUE(input.good());
  std::map<std::string, std::vector<std::string>> cases;
  std::vector<std::string> results;
  std::string line;
  while (std::getline(input, line)) {
    const auto parts = fields(line);
    if (parts.size() == 4 && parts[0] == "S") { cases[parts[1]].push_back(parts[3]); }
    else if (parts.size() == 14 && parts[0] == "R") { results.push_back(line); }
    else { FAIL() << "invalid actual-old fixture record"; }
  }
  ASSERT_EQ(13, cases.size()); ASSERT_EQ(234, results.size());
  size_t output = 0;
  // Preserve the producer's actual fixture order instead of a map sort order.
  for (int mode = 0; mode < 3; ++mode) {
    for (int initial = 0; initial < 3; ++initial) {
      for (int testcase = 0; testcase < 13; ++testcase) {
        const auto first = fields(results[output]); const std::string name = first[3];
        SCOPED_TRACE(mode);
        SCOPED_TRACE(initial);
        SCOPED_TRACE(name);
        ASSERT_EQ(2, cases[name].size());
        ObArenaAllocator owner;
        DatumValueBuffer values(owner); ASSERT_EQ(OB_SUCCESS, values.reserve(2));
        for (int64_t i = 0; i < 2; ++i) {
          std::string bytes = unhex(cases[name][i]); int64_t pos = 0; DatumValue value;
          ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(bytes.data(), bytes.size(), pos,
              owner, value.datum_, value.meta_));
          ASSERT_EQ(bytes.size(), pos); ASSERT_EQ(OB_SUCCESS, values.assign_view(i, value));
        }
        ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 2);
        ParamStore arrays{ObWrapperAllocator(owner)}, row{ObWrapperAllocator(owner)};
        ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, owner));
        ASSERT_NO_FATAL_FAILURE(seed(row.at(0), initial, owner));
        ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
        NativeArrayBindingTestAccess::configure(handler, arrays, row,
            mode == 0 ? stmt::T_ANONYMOUS_BLOCK : mode == 1 ? stmt::T_INSERT : stmt::T_CALL_PROCEDURE);
        for (int64_t i = 0; i < 2; ++i) {
          const int ret = NativeArrayBindingTestAccess::row(handler, i, owner);
          EXPECT_EQ(results[output++], capture_fields(mode, initial, name, i, ret, row.at(0)));
        }
      }
    }
  }
  EXPECT_EQ(234, output);
}

TEST(NativeArrayBinding, native_typed_null_maps_only_value_to_old_null_without_new_numeric_accuracy)
{
  for (int mode = 0; mode < 3; ++mode) {
    for (int initial = 0; initial < 3; ++initial) {
      for (SqlValueType type : {ObIntType, ObNumberType, ObVarcharType, ObBitType, ObExtendType}) {
        SCOPED_TRACE(mode);
        SCOPED_TRACE(initial);
        SCOPED_TRACE(type);
        ObArenaAllocator owner; DatumValueBuffer values(owner);
        ASSERT_EQ(OB_SUCCESS, values.reserve(2));
        ASSERT_EQ(OB_SUCCESS, values.set_null(0)); values.at(0).meta_.set_type(type);
        ASSERT_EQ(OB_SUCCESS, values.set_null(1));
        ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 2);
        ParamStore arrays{ObWrapperAllocator(owner)}, row{ObWrapperAllocator(owner)};
        ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, owner));
        ASSERT_NO_FATAL_FAILURE(seed(row.at(0), initial, owner));
        ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
        NativeArrayBindingTestAccess::configure(handler, arrays, row,
            mode == 0 ? stmt::T_ANONYMOUS_BLOCK : mode == 1 ? stmt::T_INSERT : stmt::T_CALL_PROCEDURE);
        ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, owner));
        const auto typed = capture_fields(mode, initial, "null", 0, OB_SUCCESS, row.at(0));
        ASSERT_NO_FATAL_FAILURE(seed(row.at(0), initial, owner));
        ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 1, owner));
        EXPECT_EQ(typed, capture_fields(mode, initial, "null", 0, OB_SUCCESS, row.at(0)));
        EXPECT_TRUE(row.at(0).get_meta().is_null());
      }
    }
  }
}

TEST(NativeArrayBinding, record_snapshot_and_partial_row_cleanup_do_not_mutate_collection_pointee)
{
  ObArenaAllocator source_owner, output_owner;
  DatumValueBuffer values(source_owner); ASSERT_EQ(OB_SUCCESS, values.reserve(1));
  auto *source = record(source_owner, 7); ASSERT_NE(nullptr, source);
  DatumValue root;
  ASSERT_EQ(OB_SUCCESS, root.set_extend(reinterpret_cast<int64_t>(source), PL_RECORD_TYPE,
      source_owner, source->get_init_size()));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(0, root));
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 1);
  ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
  ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, output_owner));
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_ANONYMOUS_BLOCK);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, output_owner));
  ASSERT_NE(source, reinterpret_cast<ObPLRecord *>(row.at(0).get_ext()));
  auto *copy = reinterpret_cast<ObPLRecord *>(row.at(0).get_ext());
  ASSERT_EQ(OB_SUCCESS, source->get_element()[0].set_int(99, *source->get_allocator()));
  EXPECT_EQ(7, copy->get_element()[0].get_int());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, output_owner, session));
  EXPECT_TRUE(row.at(0).is_null()); EXPECT_EQ(1, source->get_count());
  EXPECT_EQ(99, source->get_element()[0].get_int());
  // A failed later parameter still retires the successful preceding snapshot.
  DatumValueBuffer strings(source_owner); ASSERT_EQ(OB_SUCCESS, strings.reserve(1));
  std::string text(12347, 'x');
  ASSERT_EQ(OB_SUCCESS, strings.set_varchar_view(0, ObString(text.size(), text.data())));
  strings.at(0).set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObPLCollection string_collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID);
  attach(string_collection, strings, 1);
  DatumParameter array, previous;
  ASSERT_EQ(OB_SUCCESS, array.set_extend(reinterpret_cast<int64_t>(&string_collection),
      PL_NESTED_TABLE_TYPE, output_owner));
  ASSERT_EQ(OB_SUCCESS, arrays.push_back(array));
  ASSERT_EQ(OB_SUCCESS, previous.set_int(55, output_owner));
  ASSERT_EQ(OB_SUCCESS, row.push_back(previous));
  BudgetOwner budget; budget.budget = INT_MAX; budget.reject_size = text.size();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, NativeArrayBindingTestAccess::row(handler, 0, budget));
  ASSERT_TRUE(row.at(0).is_pl_extend());
  EXPECT_NE(source, reinterpret_cast<ObPLRecord *>(row.at(0).get_ext()));
  EXPECT_EQ(previous.datum_.ptr_, row.at(1).datum_.ptr_); EXPECT_EQ(55, row.at(1).get_int());
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, budget, session));
  EXPECT_TRUE(row.at(0).is_null()); EXPECT_EQ(99, source->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, root));
}

TEST(NativeArrayBinding, failed_record_and_cursor_clone_roll_back_descriptor_without_source_cleanup)
{
  ObArenaAllocator source_owner, output_owner;
  ObSQLSessionInfo session;
  auto *source_record = record(source_owner, 7); ASSERT_NE(nullptr, source_record);
  DatumParameter sources[2];
  ASSERT_EQ(OB_SUCCESS, sources[0].set_extend(reinterpret_cast<int64_t>(source_record),
      PL_RECORD_TYPE, source_owner, source_record->get_init_size()));
  ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_cursor_alloc(source_owner, sources[1]));
  auto *source_cursor = reinterpret_cast<ObPLCursorInfo *>(sources[1].get_ext());
  ObSPICursor *spi = nullptr;
  ASSERT_EQ(OB_SUCCESS, source_cursor->prepare_spi_cursor(spi, 1L << 20, false, &session));
  source_cursor->open(spi);
  for (int kind = 0; kind < 2; ++kind) {
    DatumValueBuffer values(source_owner); ASSERT_EQ(OB_SUCCESS, values.reserve(1));
    ASSERT_EQ(OB_SUCCESS, values.assign_view(0, sources[kind].value_view()));
    ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 1);
    ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
    ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, output_owner));
    ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
    NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_ANONYMOUS_BLOCK);
    for (int allowance : {0, 1}) {
      BudgetOwner failing; failing.budget = allowance; const DatumParameter previous = row.at(0);
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, NativeArrayBindingTestAccess::row(handler, 0, failing));
      EXPECT_EQ(previous.datum_.ptr_, row.at(0).datum_.ptr_); EXPECT_EQ(99, row.at(0).get_int());
      EXPECT_TRUE(failing.live.empty());
      EXPECT_EQ(7, source_record->get_element()[0].get_int()); EXPECT_TRUE(source_cursor->isopen());
    }
    ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, output_owner));
    EXPECT_NE(sources[kind].get_ext(), row.at(0).get_ext());
    ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, output_owner, session));
    EXPECT_EQ(7, source_record->get_element()[0].get_int()); EXPECT_TRUE(source_cursor->isopen());
  }
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, sources[0], &session));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, sources[1], &session));
}

TEST(NativeArrayBinding, closed_cursor_is_independently_owned_and_live_destination_is_preserved)
{
  ObArenaAllocator source_owner, output_owner;
  ObSQLSessionInfo session;
  DatumParameter source;
  ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_cursor_alloc(source_owner, source));
  auto *closed = reinterpret_cast<ObPLCursorInfo *>(source.get_ext());
  ASSERT_FALSE(closed->isopen()); ASSERT_EQ(nullptr, closed->get_spi_cursor());
  closed->set_implicit(); closed->set_scrollable(); closed->set_for_update();
  closed->set_hidden_rowid(); closed->set_last_execute_time(9876);
  char text[] = "a retained SQL string on a closed cursor";
  closed->get_sql_text() = ObString::make_string(text);
  std::strcpy(closed->get_sql_id(), "0123456789abcdef0123456789abcdef");
  for (int allowance : {0, 1, 2}) {
    BudgetOwner failing; failing.budget = allowance;
    DatumParameter previous; ASSERT_EQ(OB_SUCCESS, previous.set_int(55, output_owner));
    const char *original = previous.datum_.ptr_;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObUserDefinedType::clone_parameter(failing, source, previous));
    EXPECT_EQ(original, previous.datum_.ptr_); EXPECT_EQ(55, previous.get_int());
    EXPECT_TRUE(failing.live.empty());
    EXPECT_FALSE(closed->isopen()); EXPECT_EQ(nullptr, closed->get_spi_cursor());
  }
  DatumParameter snapshot;
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::clone_parameter(output_owner, source, snapshot));
  ASSERT_NE(source.get_ext(), snapshot.get_ext()); ASSERT_NE(source.datum_.ptr_, snapshot.datum_.ptr_);
  auto *copy = reinterpret_cast<ObPLCursorInfo *>(snapshot.get_ext());
  EXPECT_FALSE(copy->isopen()); EXPECT_EQ(nullptr, copy->get_spi_cursor());
  EXPECT_TRUE(copy->is_scrollable()); EXPECT_TRUE(copy->is_for_update()); EXPECT_TRUE(copy->has_hidden_rowid());
  EXPECT_EQ(9876, copy->get_last_execute_time()); EXPECT_EQ(closed->get_sql_text(), copy->get_sql_text());
  EXPECT_NE(closed->get_sql_text().ptr(), copy->get_sql_text().ptr());
  EXPECT_STREQ(closed->get_sql_id(), copy->get_sql_id());
  bool found = false, isnull = false;
  // Only an implicit cursor returns success/NULL while closed.
  EXPECT_EQ(OB_SUCCESS, copy->get_found(found, isnull)); EXPECT_TRUE(isnull);
  DatumParameter live;
  ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_cursor_alloc(output_owner, live));
  auto *live_cursor = reinterpret_cast<ObPLCursorInfo *>(live.get_ext()); ObSPICursor *spi = nullptr;
  ASSERT_EQ(OB_SUCCESS, live_cursor->prepare_spi_cursor(spi, 1L << 20, false, &session));
  live_cursor->open(spi);
  EXPECT_EQ(OB_ERR_UNEXPECTED, live_cursor->deep_copy(*closed, &output_owner));
  EXPECT_TRUE(live_cursor->isopen()); EXPECT_EQ(spi, live_cursor->get_spi_cursor());
  EXPECT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(output_owner, live, &session));
  DatumValueBuffer values(source_owner); ASSERT_EQ(OB_SUCCESS, values.reserve(1));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(0, source.value_view()));
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 1);
  for (stmt::StmtType mode : {stmt::T_ANONYMOUS_BLOCK, stmt::T_INSERT, stmt::T_CALL_PROCEDURE}) {
    ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
    ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, output_owner));
    ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
    NativeArrayBindingTestAccess::configure(handler, arrays, row, mode);
    ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, output_owner));
    EXPECT_TRUE(row.at(0).is_pl_extend()); EXPECT_NE(source.get_ext(), row.at(0).get_ext());
    ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, output_owner, session));
    EXPECT_FALSE(closed->isopen()); EXPECT_EQ(nullptr, closed->get_spi_cursor());
    EXPECT_EQ(ObString::make_string(text), closed->get_sql_text());
  }
  ObSPICursor *closed_spi = nullptr;
  ASSERT_EQ(OB_SUCCESS, closed->prepare_spi_cursor(closed_spi, 1L << 20, false, &session));
  closed->open(closed_spi);
  ASSERT_EQ(OB_SUCCESS, closed->close(session));
  EXPECT_EQ(nullptr, closed->get_spi_cursor()); EXPECT_FALSE(closed->isopen());
  DatumParameter after_close;
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::clone_parameter(output_owner, source, after_close));
  auto *closed_copy = reinterpret_cast<ObPLCursorInfo *>(after_close.get_ext());
  EXPECT_FALSE(closed_copy->isopen()); EXPECT_EQ(nullptr, closed_copy->get_spi_cursor());
  EXPECT_EQ(OB_ER_SP_CURSOR_NOT_OPEN, closed_copy->get_found(found, isnull));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(output_owner, after_close, &session));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, source, &session));
  std::memset(text, '~', sizeof(text) - 1);
  EXPECT_EQ(ObString::make_string("a retained SQL string on a closed cursor"), copy->get_sql_text());
  EXPECT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(output_owner, snapshot, &session));
}

TEST(NativeArrayBinding, actual_old_closed_and_open_array_metadata_matches_with_independent_pointees)
{
  std::ifstream input("unittest/observer/data/native_array_binding_closed.golden");
  ASSERT_TRUE(input.good()); std::map<std::pair<int, int>, std::vector<std::string>> expected;
  std::string line;
  while (std::getline(input, line)) {
    const auto parts = fields(line);
    if (parts.size() == 12 && parts[0] == "ARRAY") {
      expected[{std::stoi(parts[1]), std::stoi(parts[2])}] = parts;
    }
  }
  ASSERT_EQ(12, expected.size());
  for (int state = 0; state < 4; ++state) {
    ObArenaAllocator source_owner, output_owner; ObSQLSessionInfo session;
    DatumParameter source; ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_cursor_alloc(source_owner, source));
    auto *cursor = reinterpret_cast<ObPLCursorInfo *>(source.get_ext());
    if (state == 1 || state == 2) {
      ObSPICursor *spi = nullptr;
      ASSERT_EQ(OB_SUCCESS, cursor->prepare_spi_cursor(spi, 1L << 20, false, &session));
      cursor->open(spi);
      if (state == 1) { ASSERT_EQ(OB_SUCCESS, cursor->close(session)); }
    } else if (state == 3) { cursor->open(); }
    DatumValueBuffer values(source_owner); ASSERT_EQ(OB_SUCCESS, values.reserve(1));
    ASSERT_EQ(OB_SUCCESS, values.assign_view(0, source.value_view()));
    ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 1);
    for (int mode = 0; mode < 3; ++mode) {
      SCOPED_TRACE(state);
      SCOPED_TRACE(mode);
      ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
      ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, output_owner));
      ASSERT_NO_FATAL_FAILURE(seed(row.at(0), 1, output_owner)); const DatumParameter previous = row.at(0);
      ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
      NativeArrayBindingTestAccess::configure(handler, arrays, row,
          mode == 0 ? stmt::T_ANONYMOUS_BLOCK : mode == 1 ? stmt::T_INSERT : stmt::T_CALL_PROCEDURE);
      const int ret = NativeArrayBindingTestAccess::row(handler, 0, output_owner);
      const auto &golden = expected.at({state, mode});
      if (state == 3) {
        // Old shallow assignment accepted this invalid open/no-handler object.
        // The native clone explicitly rejects it without publishing a value.
        EXPECT_EQ(OB_SUCCESS, std::stoi(golden[3])); EXPECT_EQ(OB_ERR_UNEXPECTED, ret);
        EXPECT_EQ(previous.datum_.ptr_, row.at(0).datum_.ptr_);
        EXPECT_EQ(previous.get_accuracy(), row.at(0).get_accuracy());
      } else {
        ASSERT_EQ(std::stoi(golden[3]), ret); ASSERT_TRUE(row.at(0).is_pl_extend());
        EXPECT_NE(source.get_ext(), row.at(0).get_ext());
        const std::vector<std::string> actual = {
          hex(&row.at(0).get_meta(), sizeof(SqlTypeMeta)),
          hex(&row.at(0).get_param_meta(), sizeof(SqlTypeMeta)),
          hex(&row.at(0).get_accuracy(), sizeof(ObAccuracy)),
          std::to_string(row.at(0).get_result_flag()), flag_wire(row.at(0).get_param_flag()),
          std::to_string(row.at(0).get_raw_text_pos()), std::to_string(row.at(0).get_raw_text_len())};
        for (size_t i = 0; i < actual.size(); ++i) { EXPECT_EQ(golden[5 + i], actual[i]); }
        ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, output_owner, session));
        EXPECT_EQ(state == 2, cursor->isopen()); EXPECT_EQ(state == 2, cursor->get_spi_cursor() != nullptr);
      }
    }
    ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, source, &session));
  }
}

TEST(NativeArrayBinding, local_copyout_descriptor_retires_before_request_owned_snapshot)
{
  ObArenaAllocator source_owner, request_owner, sql_owner; ObSQLSessionInfo session;
  auto *original = record(source_owner, 77); ASSERT_NE(nullptr, original);
  DatumValue source;
  ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(original), PL_RECORD_TYPE,
      source_owner, original->get_init_size()));
  DatumValueBuffer values(source_owner); ASSERT_EQ(OB_SUCCESS, values.reserve(1));
  ASSERT_EQ(OB_SUCCESS, values.assign_view(0, source));
  ObPLCollection collection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID); attach(collection, values, 1);
  ParamStore arrays{ObWrapperAllocator(request_owner)}, row{ObWrapperAllocator(request_owner)};
  ASSERT_NO_FATAL_FAILURE(store_collection(collection, arrays, row, request_owner));
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_ANONYMOUS_BLOCK);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, request_owner));
  const char *request_descriptor = row.at(0).datum_.ptr_;
  const int64_t snapshot_address = row.at(0).get_ext();
  DatumParameter local = row.at(0), callee = local;
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(sql_owner, callee, local));
  EXPECT_NE(request_descriptor, local.datum_.ptr_); EXPECT_EQ(snapshot_address, local.get_ext());
  auto *snapshot = reinterpret_cast<ObPLRecord *>(snapshot_address);
  EXPECT_EQ(77, snapshot->get_element()[0].get_int());
  DatumParameterArray::release_owned(local, sql_owner); // Descriptor only; shared execution pointee stays alive.
  sql_owner.reset();
  EXPECT_EQ(request_descriptor, row.at(0).datum_.ptr_);
  EXPECT_EQ(77, snapshot->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, request_owner, session));
  EXPECT_EQ(77, original->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, source, &session));
}

TEST(NativeArrayBinding, actual_fresh_store_and_three_column_middle_failure_can_continue_next_row)
{
  ObArenaAllocator source_owner, output_owner; ObSQLSessionInfo session;
  auto *left = record(source_owner, 7); auto *right = record(source_owner, 9);
  ASSERT_NE(nullptr, left); ASSERT_NE(nullptr, right);
  DatumValue roots[2];
  ASSERT_EQ(OB_SUCCESS, roots[0].set_extend(reinterpret_cast<int64_t>(left), PL_RECORD_TYPE,
      source_owner, left->get_init_size()));
  ASSERT_EQ(OB_SUCCESS, roots[1].set_extend(reinterpret_cast<int64_t>(right), PL_RECORD_TYPE,
      source_owner, right->get_init_size()));
  DatumValueBuffer first(source_owner), middle(source_owner), last(source_owner);
  ASSERT_EQ(OB_SUCCESS, first.reserve(3)); ASSERT_EQ(OB_SUCCESS, middle.reserve(3));
  ASSERT_EQ(OB_SUCCESS, last.reserve(3)); std::string text(12347, 'x');
  for (int64_t i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, first.assign_view(i, roots[0]));
    ASSERT_EQ(OB_SUCCESS, middle.set_varchar_view(i, ObString(text.size(), text.data())));
    middle.at(i).set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, last.assign_view(i, roots[1]));
  }
  ObPLCollection collections[3] = {
      ObPLCollection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID),
      ObPLCollection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID),
      ObPLCollection(PL_NESTED_TABLE_TYPE, OB_INVALID_ID)};
  attach(collections[0], first, 3); attach(collections[1], middle, 3); attach(collections[2], last, 3);
  // Same fresh construction/prepare_allocate as before_process/request_params.
  ParamStore arrays{ObWrapperAllocator(output_owner)}, row{ObWrapperAllocator(output_owner)};
  ASSERT_EQ(OB_SUCCESS, row.prepare_allocate(3)); ASSERT_EQ(OB_SUCCESS, arrays.prepare_allocate(3));
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_TRUE(row.at(i).is_null()); EXPECT_FALSE(row.at(i).is_pl_extend());
    ASSERT_EQ(OB_SUCCESS, arrays.at(i).set_extend(reinterpret_cast<int64_t>(&collections[i]),
        PL_NESTED_TABLE_TYPE, output_owner));
  }
  ObMPStmtExecute handler(oceanbase::share::ObGlobalContext::get_instance());
  NativeArrayBindingTestAccess::configure(handler, arrays, row, stmt::T_INSERT);
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 0, output_owner));
  ASSERT_TRUE(row.at(0).is_pl_extend()); ASSERT_TRUE(row.at(2).is_pl_extend());
  EXPECT_NE(roots[0].get_ext(), row.at(0).get_ext()); EXPECT_NE(roots[1].get_ext(), row.at(2).get_ext());
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, output_owner, session));
  EXPECT_TRUE(row.at(0).is_null()); EXPECT_TRUE(row.at(2).is_null());
  EXPECT_TRUE(row.at(0).get_param_meta().is_ext()); EXPECT_TRUE(row.at(2).get_param_meta().is_ext());
  EXPECT_EQ(7, left->get_element()[0].get_int()); EXPECT_EQ(9, right->get_element()[0].get_int());
  const DatumParameter previous_middle = row.at(1);
  BudgetOwner failing; failing.budget = INT_MAX; failing.reject_size = text.size();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, NativeArrayBindingTestAccess::row(handler, 1, failing));
  ASSERT_TRUE(row.at(0).is_pl_extend()); EXPECT_NE(roots[0].get_ext(), row.at(0).get_ext());
  EXPECT_EQ(previous_middle.datum_.ptr_, row.at(1).datum_.ptr_);
  // Last column is not updated this iteration: it must be the retired NULL,
  // never a stale source-owned PL pointer scanned by release_arraybinding_row.
  EXPECT_TRUE(row.at(2).is_null()); EXPECT_FALSE(row.at(2).is_pl_extend());
  EXPECT_TRUE(row.at(2).get_param_meta().is_ext());
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, failing, session));
  EXPECT_TRUE(row.at(0).is_null()); EXPECT_TRUE(row.at(2).is_null());
  EXPECT_EQ(1, left->get_count()); EXPECT_EQ(1, right->get_count());
  EXPECT_EQ(7, left->get_element()[0].get_int()); EXPECT_EQ(9, right->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::row(handler, 2, output_owner));
  ASSERT_TRUE(row.at(0).is_pl_extend()); ASSERT_TRUE(row.at(2).is_pl_extend());
  EXPECT_NE(roots[0].get_ext(), row.at(0).get_ext()); EXPECT_NE(roots[1].get_ext(), row.at(2).get_ext());
  ASSERT_EQ(OB_SUCCESS, NativeArrayBindingTestAccess::release(handler, output_owner, session));
  EXPECT_TRUE(row.at(0).is_null()); EXPECT_TRUE(row.at(2).is_null());
  EXPECT_EQ(7, left->get_element()[0].get_int()); EXPECT_EQ(9, right->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, roots[0], &session));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_owner, roots[1], &session));
}
