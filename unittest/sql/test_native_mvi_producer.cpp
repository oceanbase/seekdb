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

#include "sql/engine/expr/ob_expr_json_query.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/json_type/ob_json_parse.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase { namespace sql {
class JsonQueryMultivalueAccess {
public:
  static int run(bool array, ObEvalCtx &context, ObIAllocator &owner,
      ObIJsonBase *json, const ObExpr &expr, ObCollationType collation,
      ObAccuracy &accuracy, SqlValueType type, ObDatum &result) {
    ObJsonCastParam parameters(type, CS_TYPE_UTF8MB4_BIN, collation, 0);
    return ObExprJsonQuery::set_multivalue_result(array, context, owner, json,
        expr, 0, CS_TYPE_UTF8MB4_BIN, collation, nullptr, accuracy, parameters, result);
  }
};
} }
namespace {
// Shared fixture for the actual old capture and the native production tests.
// No conversion, sorting, deduplication or encoding algorithm is implemented here.
struct MviProducerState {
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  ObExecContext context;
  char *frames[1];
  ObExpr returning_type;
  ObExpr output;
  ObExpr *arguments[3];
  std::unique_ptr<ObEvalCtx> eval;
  MviProducerState() : arena(), session(), context(arena), frames{nullptr}, arguments{nullptr,nullptr,nullptr} {
    context.set_my_session(&session);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &context);
    frames[0] = static_cast<char *>(arena.alloc(8192));
    MEMSET(frames[0], 0, 8192);
    context.set_frames(frames); context.set_frame_cnt(1);
    eval.reset(new ObEvalCtx(context));
    returning_type.type_ = T_EXEC_VAR;
    returning_type.frame_idx_ = 0; returning_type.datum_off_ = 0;
    returning_type.eval_info_off_ = 32; returning_type.res_buf_off_ = 128;
    returning_type.res_buf_len_ = 128; returning_type.dyn_buf_header_offset_ = 96;
    returning_type.obj_meta_.set_int(); returning_type.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
    returning_type.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 19);
    new (frames[0]) ObDatum(); new (frames[0] + 32) ObEvalInfo();
    output.type_ = T_FUN_SYS_JSON_QUERY;
    output.frame_idx_ = 0; output.datum_off_ = 256; output.eval_info_off_ = 288;
    output.res_buf_off_ = 512; output.res_buf_len_ = 4096; output.dyn_buf_header_offset_ = 480;
    output.obj_meta_.set_varchar(); output.obj_meta_.set_collation_type(CS_TYPE_BINARY);
    output.obj_datum_map_ = OBJ_DATUM_STRING;
    output.datum_meta_ = ObDatumMeta(ObVarcharType, CS_TYPE_BINARY, -1);
    new (frames[0] + 256) ObDatum(); new (frames[0] + 288) ObEvalInfo();
    ObDynReserveBuf *dynamic = new (frames[0] + 480) ObDynReserveBuf();
    dynamic->magic_ = ObDynReserveBuf::MAGIC_NUM;
    dynamic->len_ = output.res_buf_len_; dynamic->mem_ = frames[0] + output.res_buf_off_;
    arguments[2] = &returning_type; output.args_ = arguments; output.arg_cnt_ = 3;
  }
  ~MviProducerState() {
    eval.reset(); context.set_frames(nullptr); context.set_frame_cnt(0);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, nullptr);
  }
  void set_type(SqlValueType type, int32_t length, int16_t precision, int16_t scale) {
    ParseNode node; node.value_ = 0;
    node.int16_values_[OB_NODE_CAST_TYPE_IDX] = type;
    if (ob_is_string_tc(type)) { node.int32_values_[OB_NODE_CAST_C_LEN_IDX] = length; }
    else { node.int16_values_[OB_NODE_CAST_N_PREC_IDX] = precision; node.int16_values_[OB_NODE_CAST_N_SCALE_IDX] = scale; }
    returning_type.locate_datum_for_write(*eval).set_int(node.value_);
    returning_type.get_eval_info(*eval).evaluated_ = true;
  }
};

int produce(MviProducerState &state, const char *json_text, SqlValueType type,
    ObAccuracy accuracy, bool array, std::string &packed, ObDatum **scalar = nullptr) {
  state.set_type(type, accuracy.get_length(), accuracy.get_precision(), accuracy.get_scale());
  ObJsonNode *tree = nullptr;
  int ret = ObJsonParser::get_tree(&state.arena, ObString::make_string(json_text), tree);
  ObDatum &result = state.output.locate_datum_for_write(*state.eval);
  if (ret == OB_SUCCESS) {
    ret = JsonQueryMultivalueAccess::run(array, *state.eval, state.arena, tree,
        state.output, CS_TYPE_UTF8MB4_BIN, accuracy, type, result);
  }
  if (ret == OB_SUCCESS && array) {
    ObString data = result.get_string();
    ObJsonBin binary(data.ptr(), data.length(), &state.arena);
    ret = binary.reset_iter();
    if (ret == OB_SUCCESS) { packed.assign(binary.get_data(), binary.get_data_length()); }
  }
  if (ret == OB_SUCCESS && scalar != nullptr) { *scalar = &result; }
  return ret;
}
int decode_values(const std::string &packed, ObIAllocator &owner,
    uint32_t &count, std::vector<DatumValue> &values) {
  if (packed.size() < sizeof(count)) { return OB_INVALID_ARGUMENT; }
  MEMCPY(&count, packed.data(), sizeof(count));
  int64_t offset = sizeof(count);
  for (uint32_t i = 0; i < (count == 0 ? 1 : count); ++i) {
    DatumValue value;
    int ret = DatumParameterCodec::decode_value(packed.data(), packed.size(), offset,
                                                owner, value.datum_, value.meta_);
    if (ret != OB_SUCCESS) { return ret; }
    values.push_back(value);
  }
  return offset == packed.size() ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}
class RejectingMviOwner : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
} // namespace

TEST(NativeMviProducer, actual_producer_keeps_descending_sort_and_duplicate_count) {
  MviProducerState state;
  ObAccuracy accuracy; accuracy.set_precision(20); accuracy.set_scale(0);
  std::string packed;
  ASSERT_EQ(OB_SUCCESS, produce(state, "[2,1,2,3]", ObIntType, accuracy, true, packed));
  uint32_t count = 0; std::vector<DatumValue> values;
  ASSERT_EQ(OB_SUCCESS, decode_values(packed, state.arena, count, values));
  ASSERT_EQ(3, count); ASSERT_EQ(3, values.size());
  EXPECT_EQ(3, values[0].get_int()); EXPECT_EQ(2, values[1].get_int()); EXPECT_EQ(1, values[2].get_int());
}
TEST(NativeJsonValueCast, scalar_cast_keeps_result_in_datum_form) {
  MviProducerState state;
  state.returning_type.obj_meta_.set_int();
  state.returning_type.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 19);
  state.output.obj_meta_.set_double();
  state.output.datum_meta_ = ObDatumMeta(ObDoubleType, CS_TYPE_BINARY, 0, 15);
  state.output.max_length_ = 8;

  alignas(int64_t) char source_storage[sizeof(int64_t)] = {};
  ObDatum source(source_storage, sizeof(source_storage), false);
  source.set_int(17);
  ObDatum &result = state.output.locate_datum_for_write(*state.eval);
  ASSERT_EQ(OB_SUCCESS, ObJsonExprHelper::cast_to_res(
      state.arena, source, state.output, state.returning_type, *state.eval,
      result, true));
  EXPECT_DOUBLE_EQ(17.0, result.get_double());
}
TEST(NativeMviProducer, binary_destination_still_deduplicates_with_general_ci) {
  MviProducerState state;
  ObAccuracy accuracy; accuracy.set_length(16); accuracy.set_scale(-1);
  std::string packed;
  ASSERT_EQ(OB_SUCCESS, produce(state, "[\"b\",\"A\",\"a\",\"B\"]", ObVarcharType, accuracy, true, packed));
  uint32_t count = 0; std::vector<DatumValue> values;
  ASSERT_EQ(OB_SUCCESS, decode_values(packed, state.arena, count, values));
  ASSERT_EQ(2, count); ASSERT_EQ(2, values.size());
  EXPECT_TRUE(values[0].get_string() == ObString::make_string("b") || values[0].get_string() == ObString::make_string("B"));
  EXPECT_TRUE(values[1].get_string() == ObString::make_string("a") || values[1].get_string() == ObString::make_string("A"));
}
TEST(NativeMviProducer, zero_count_keeps_serialized_min_or_null_sentinel) {
  MviProducerState state;
  ObAccuracy accuracy; accuracy.set_precision(8); accuracy.set_scale(2);
  for (SqlValueType type : {ObIntType, ObDecimalIntType}) {
    std::string packed;
    ASSERT_EQ(OB_SUCCESS, produce(state, "[]", type, accuracy, true, packed));
    uint32_t count = 1; std::vector<DatumValue> values;
    ASSERT_EQ(OB_SUCCESS, decode_values(packed, state.arena, count, values));
    ASSERT_EQ(0, count); ASSERT_EQ(1, values.size());
    if (type == ObIntType) { EXPECT_TRUE(values[0].is_min_value()); EXPECT_EQ(ObExtendType, values[0].get_type()); }
    else { EXPECT_TRUE(values[0].is_null()); EXPECT_EQ(ObNullType, values[0].get_type()); }
  }
}
TEST(NativeMviProducer, scalar_path_preserves_last_array_element_and_frame_bytes) {
  MviProducerState state;
  ObAccuracy accuracy; accuracy.set_precision(20); accuracy.set_scale(0);
  std::string ignored; ObDatum *scalar = nullptr;
  ASSERT_EQ(OB_SUCCESS, produce(state, "[2,1]", ObIntType, accuracy, false, ignored, &scalar));
  ASSERT_NE(nullptr, scalar); EXPECT_EQ(1, scalar->get_int());
}
TEST(NativeMviProducer, json_scalar_result_owns_stack_and_source_bytes_and_failed_replace_is_atomic) {
  MviProducerState state;
  ObArenaAllocator owner;
  DatumValue integer, text, replacement;
  ObAccuracy accuracy; accuracy.set_precision(20); accuracy.set_scale(0); accuracy.set_length(64);
  ObJsonInt original(918);
  ASSERT_EQ(OB_SUCCESS, ObJsonUtil::cast_json_scalar_to_sql_value(owner, *state.eval,
      &original, CS_TYPE_BINARY, accuracy, ObIntType, 0, integer));
  ObJsonInt later(456);
  ASSERT_EQ(OB_SUCCESS, ObJsonUtil::cast_json_scalar_to_sql_value(owner, *state.eval,
      &later, CS_TYPE_BINARY, accuracy, ObIntType, 0, replacement));
  EXPECT_EQ(918, integer.get_int()); EXPECT_EQ(456, replacement.get_int());
  {
    std::string source = "customer_客户";
    ObJsonString string(source.data(), source.size());
    ASSERT_EQ(OB_SUCCESS, ObJsonUtil::cast_json_scalar_to_sql_value(owner, *state.eval,
        &string, CS_TYPE_UTF8MB4_BIN, accuracy, ObVarcharType, -1, text));
    source.assign(source.size(), 'x');
  }
  EXPECT_EQ(ObString::make_string("customer_客户"), text.get_string());
  const char *previous = integer.datum_.ptr_;
  RejectingMviOwner reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObJsonUtil::cast_json_scalar_to_sql_value(reject,
      *state.eval, &later, CS_TYPE_BINARY, accuracy, ObIntType, 0, integer));
  EXPECT_EQ(previous, integer.datum_.ptr_); EXPECT_EQ(918, integer.get_int());
}
TEST(NativeMviProducer, decimal_scalar_coefficient_and_null_metadata_are_native) {
  MviProducerState state;
  ObAccuracy accuracy; accuracy.set_precision(8); accuracy.set_scale(2);
  ObJsonDouble input(123.456);
  DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, ObJsonUtil::cast_json_scalar_to_sql_value(state.arena, *state.eval,
      &input, CS_TYPE_BINARY, accuracy, ObDecimalIntType, 2, decimal));
  ASSERT_EQ(sizeof(int32_t), decimal.get_int_bytes());
  EXPECT_EQ(12346, *reinterpret_cast<const int32_t *>(decimal.get_decimal_int()));
  EXPECT_EQ(2, decimal.get_scale());
  ObJsonNull null;
  ASSERT_EQ(OB_SUCCESS, ObJsonUtil::cast_json_scalar_to_sql_value(state.arena, *state.eval,
      &null, CS_TYPE_BINARY, accuracy, ObDecimalIntType, 2, decimal));
  EXPECT_TRUE(decimal.is_null()); EXPECT_EQ(ObNullType, decimal.get_type()); EXPECT_EQ(2, decimal.get_scale());
}
TEST(NativeMviProducer, actual_producer_rejects_null_object_and_nested_array) {
  MviProducerState state;
  ObAccuracy accuracy; accuracy.set_precision(20); accuracy.set_scale(0);
  std::string packed;
  EXPECT_EQ(OB_ERR_JSON_VALUE_CAST_FUNCTION_INDEX,
      produce(state, "null", ObIntType, accuracy, true, packed));
  EXPECT_EQ(OB_NOT_MULTIVALUE_SUPPORT,
      produce(state, "{}", ObIntType, accuracy, true, packed));
  EXPECT_EQ(OB_ERR_JSON_CONTAINER_CAST_SCALAR,
      produce(state, "[[1]]", ObIntType, accuracy, true, packed));
}

namespace {
std::vector<std::string> mvi_fixture_fields(const std::string &line) {
  std::vector<std::string> fields;
  size_t begin = 0;
  while (true) {
    size_t end = line.find('\t', begin);
    if (end == std::string::npos) { fields.push_back(line.substr(begin)); break; }
    fields.push_back(line.substr(begin, end - begin)); begin = end + 1;
  }
  return fields;
}
bool mvi_fixture_unhex(const std::string &hex, std::string &bytes) {
  if (hex.size() % 2 != 0) { return false; }
  bytes.clear(); bytes.reserve(hex.size() / 2);
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
  };
  for (size_t i = 0; i < hex.size(); i += 2) {
    int high = digit(hex[i]), low = digit(hex[i + 1]);
    if (high < 0 || low < 0) { return false; }
    bytes.push_back(static_cast<char>(high * 16 + low));
  }
  return true;
}
} // namespace

// All expected errno, metadata and bytes were captured by calling the exported
// production method in the immutable 0b964 baseline library. There is no old
// producer algorithm or legacy value compatibility implementation in this test.
TEST(NativeMviProducer, matches_immutable_old_producer_full_wire_fixture) {
  std::ifstream input("unittest/sql/data/mvi_producer.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    const std::vector<std::string> fields = mvi_fixture_fields(line);
    ASSERT_EQ(13, fields.size());
    SCOPED_TRACE(fields[0]);
    const SqlValueType type = static_cast<SqlValueType>(std::strtol(fields[1].c_str(), nullptr, 10));
    const int16_t precision = std::strtol(fields[2].c_str(), nullptr, 10);
    const int16_t scale = std::strtol(fields[3].c_str(), nullptr, 10);
    const int32_t length = std::strtol(fields[4].c_str(), nullptr, 10);
    const ObCollationType collation = static_cast<ObCollationType>(std::strtol(fields[5].c_str(), nullptr, 10));
    const bool array = fields[6] == "1";
    std::string json;
    ASSERT_TRUE(mvi_fixture_unhex(fields[7], json));
    const int expected_ret = std::strtol(fields[8].c_str(), nullptr, 10);
    const int64_t expected_accuracy = std::strtoll(fields[9].c_str(), nullptr, 10);
    MviProducerState state;
    state.set_type(type, length, precision, scale);
    ObAccuracy accuracy;
    if (ob_is_string_tc(type)) { accuracy.set_length(length); }
    else { accuracy.set_precision(precision); accuracy.set_scale(scale); }
    ObJsonNode *tree = nullptr;
    int ret = json == "@absent" ? OB_SUCCESS : ObJsonParser::get_tree(
        &state.arena, json.data(), json.size(), tree);
    ObDatum &result = state.output.locate_datum_for_write(*state.eval);
    if (ret == OB_SUCCESS) {
      ret = JsonQueryMultivalueAccess::run(array, *state.eval, state.arena, tree,
          state.output, collation, accuracy, type, result);
    }
    EXPECT_EQ(expected_ret, ret);
    EXPECT_EQ(expected_accuracy, accuracy.get_accuracy());
    if (ret == OB_SUCCESS && ret == expected_ret) {
      std::string actual;
      if (array) {
        const ObString data = result.get_string();
        ObJsonBin binary(data.ptr(), data.length(), &state.arena);
        const int decode_ret = binary.reset_iter();
        EXPECT_EQ(std::strtol(fields[10].c_str(), nullptr, 10), decode_ret);
        if (decode_ret == OB_SUCCESS) { actual.assign(binary.get_data(), binary.get_data_length()); }
      } else {
        EXPECT_EQ(std::strtoull(fields[11].c_str(), nullptr, 10), result.pack_);
        if (!result.is_null()) { actual.assign(result.ptr_, result.len_); }
      }
      std::string expected;
      ASSERT_TRUE(mvi_fixture_unhex(fields[12], expected));
      EXPECT_EQ(expected, actual);
    }
    ++checked;
  }
  EXPECT_EQ(150, checked);
}

int main(int argc, char **argv) {
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  ObArenaAllocator constants;
  if (ObNumberConstValue::init(constants) != OB_SUCCESS || wide::ObDecimalIntConstValue::init_const_values(constants) != OB_SUCCESS) { return 3; }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
