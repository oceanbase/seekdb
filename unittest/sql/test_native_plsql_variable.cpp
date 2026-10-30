/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/engine/expr/ob_expr_plsql_variable.h"
#include "sql/engine/expr/ob_expr_pl_get_cursor_attr.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include "common/datum/datum_parameter_codec.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
namespace pl = oceanbase::pl;

namespace {
class CursorDatumExtraInfo : public ObIExprExtraInfo
{
public:
  CursorDatumExtraInfo(ObIAllocator &allocator, const ObDatum *datum)
    : ObIExprExtraInfo(allocator, T_FUN_PL_GET_CURSOR_ATTR), datum_(datum) {}
  int serialize(char *, const int64_t, int64_t &) const override { return OB_NOT_SUPPORTED; }
  int deserialize(const char *, const int64_t, int64_t &) override { return OB_NOT_SUPPORTED; }
  int64_t get_serialize_size() const override { return 0; }
  int deep_copy(ObIAllocator &, const ObExprOperatorType, ObIExprExtraInfo *&) const override
  { return OB_NOT_SUPPORTED; }
  const ObDatum *datum_;
};
int eval_cursor_attr_test_arg(const ObExpr &expr, ObEvalCtx &, ObDatum &result)
{
  result = *static_cast<const CursorDatumExtraInfo *>(expr.extra_info_)->datum_;
  return OB_SUCCESS;
}

std::string plsql_hex(const char *p, int64_t n) {
  const char *digits = "0123456789abcdef"; std::string out;
  for (int64_t i = 0; i < n; ++i) { const uint8_t b = p[i]; out += digits[b >> 4]; out += digits[b & 15]; }
  return out.empty() ? "-" : out;
}
std::string plsql_unhex(const std::string &text) {
  std::string out;
  if (text != "-") { for (size_t i = 0; i < text.size(); i += 2) { out.push_back(std::stoi(text.substr(i, 2), nullptr, 16)); } }
  return out;
}
std::vector<std::string> plsql_fields(const std::string &line) {
  std::vector<std::string> fields; std::istringstream stream(line); std::string field;
  while (std::getline(stream, field, '\t')) { fields.push_back(field); } return fields;
}
template <class T> std::string plsql_wire(const T &value) {
  std::vector<char> buffer(value.get_serialize_size() + 64); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, value.serialize(buffer.data(), buffer.size(), pos));
  return plsql_hex(buffer.data(), pos);
}

class NativePLSQLVariable : public testing::Test {
protected:
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ObExecContext context;
  ObPLSQLVariableInfo info;
  ObExpr expr;
  char *frames[1] = {nullptr};
  std::unique_ptr<ObEvalCtx> eval;
  NativePLSQLVariable() : context(owner), info(owner, T_FUN_PLSQL_VARIABLE) {}
  void SetUp() override {
    ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
    context.set_my_session(&session);
    frames[0] = static_cast<char *>(owner.alloc(8192));
    ASSERT_NE(nullptr, frames[0]);
    std::memset(frames[0], 0, 8192);
    context.set_frames(frames);
    context.set_frame_cnt(1);
    eval.reset(new ObEvalCtx(context));
    expr.type_ = T_FUN_PLSQL_VARIABLE;
    expr.extra_info_ = &info;
    expr.frame_idx_ = 0;
    expr.datum_off_ = 0;
    expr.eval_info_off_ = 32;
    expr.dyn_buf_header_offset_ = 96;
    expr.res_buf_off_ = 128;
    expr.res_buf_len_ = 256;
    new (frames[0]) ObDatum();
    new (frames[0] + 32) ObEvalInfo();
    auto *dynamic = new (frames[0] + 96) ObDynReserveBuf();
    dynamic->magic_ = ObDynReserveBuf::MAGIC_NUM;
    dynamic->len_ = expr.res_buf_len_;
    dynamic->mem_ = frames[0] + expr.res_buf_off_;
  }
  void TearDown() override {
    eval.reset();
    context.set_frames(nullptr);
    context.set_frame_cnt(0);
  }
  void configure(const char *name, const SqlTypeMeta &meta) {
    info.plsql_variable_ = ObString::make_string(name);
    expr.obj_meta_ = meta;
    expr.datum_meta_ = ObDatumMeta(meta.get_type(), meta.get_collation_type(), meta.get_scale());
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(meta.get_type());
  }
  int fetch(ObDatum &result) {
    return ObExprPLSQLVariable::eval_plsql_variable(expr, *eval, result);
  }
};

TEST_F(NativePLSQLVariable, line_is_copied_from_stack_to_expression_frame) {
  SqlTypeMeta meta;
  meta.set_int32();
  configure("PLSQL_LINE", meta);
  info.plsql_line_ = 2147483647;
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_EQ(2147483647, result.get_int());
  EXPECT_EQ(8, result.len_);
  EXPECT_EQ(expr.locate_expr_datum(*eval).ptr_, result.ptr_);
  info.plsql_line_ = -1;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_EQ(-1, result.get_int());
}

TEST_F(NativePLSQLVariable, unsigned_system_variable_preserves_all_64_bits) {
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(UINT64_MAX, owner));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_LAST_INSERT_ID, value));
  DatumValue stored;
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable_by_name(ObString::make_string("last_insert_id"), stored));
  configure("last_insert_id", stored.get_meta());
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_EQ(UINT64_MAX, result.get_uint());
  EXPECT_NE(stored.datum_.ptr_, result.ptr_);
}

TEST_F(NativePLSQLVariable, borrowed_system_string_survives_replacement_and_source_owner_reset) {
  ObBasicSysVar *variable = nullptr;
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_TX_ISOLATION, variable));
  ASSERT_NE(nullptr, variable);
  const std::string text(4096, 'v');
  ObDatum result;
  {
    ObArenaAllocator source;
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString(text.size(), text.data()), source));
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, variable->set_value(value, source));
    configure("tx_isolation", value.get_meta());
    ASSERT_EQ(OB_SUCCESS, fetch(result));
    EXPECT_NE(variable->get_value().datum_.ptr_, result.ptr_);
    DatumValue replacement;
    ASSERT_EQ(OB_SUCCESS, replacement.set_varchar(ObString::make_string("replacement"), owner));
    replacement.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, variable->set_value(replacement, owner));
    source.reuse();
    std::memset(source.alloc(65536), 0x6d, 65536);
  }
  EXPECT_EQ(text, std::string(result.ptr_, result.len_));
  EXPECT_EQ(expr.locate_expr_datum(*eval).ptr_, result.ptr_);
}

TEST_F(NativePLSQLVariable, headerless_and_inrow_lob_system_values_use_native_adjustment) {
  // Exercise the existing generic value branch through its actual provider;
  // normal built-in system variables do not currently declare LOB types.
  ObBasicSysVar *variable = nullptr;
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_TX_ISOLATION, variable));
  ASSERT_NE(nullptr, variable);
  for (bool has_header : {false, true}) {
    DatumValue value;
    if (has_header) {
      ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::str_to_lob_storage_obj(
          owner, ObString::make_string("native-lob"), value));
    } else {
      ASSERT_EQ(OB_SUCCESS, value.set_string(ObLongTextType, ObString::make_string("native-lob"), owner));
      value.meta_.set_inrow();
    }
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, variable->set_value(value, owner));
    SqlTypeMeta result_meta;
    result_meta.set_type(ObLongTextType);
    result_meta.set_inrow();
    result_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (!has_header) { result_meta.set_has_lob_header(); }
    configure("tx_isolation", result_meta);
    ObDatum result;
    ASSERT_EQ(OB_SUCCESS, fetch(result));
    ObString content = result.get_string();
    if (!has_header) {
      ObLobLocatorV2 locator(content, true);
      ASSERT_EQ(OB_SUCCESS, locator.get_inrow_data(content));
    }
    EXPECT_EQ(ObString::make_string("native-lob"), content);
    EXPECT_NE(variable->get_value().datum_.ptr_, result.ptr_);
  }
}

TEST_F(NativePLSQLVariable, unknown_name_is_null_and_resolver_keeps_system_collation) {
  SqlTypeMeta meta;
  meta.set_null();
  configure("native_unknown_system_variable", meta);
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_TRUE(result.is_null());
  ObExprPLSQLVariable function(owner);
  ASSERT_EQ(OB_SUCCESS, function.deep_copy_plsql_variable(info.plsql_variable_));
  ObExprTypeCtx types;
  types.set_session(&session);
  ObExprResType type;
  ASSERT_EQ(OB_SUCCESS, function.calc_result_type0(type, types));
  EXPECT_EQ(ObNullType, type.get_type());
  EXPECT_EQ(ObCharset::get_system_collation(), type.get_collation_type());
}

TEST_F(NativePLSQLVariable, invalid_info_or_absent_pl_frame_preserves_failed_output) {
  SqlTypeMeta meta;
  meta.set_varchar();
  meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  configure("PLSQL_UNIT", meta);
  ObDatum result;
  result.set_string("previous", 8);
  const char *previous = result.ptr_;
  EXPECT_EQ(OB_ERR_UNEXPECTED, fetch(result));
  EXPECT_EQ(previous, result.ptr_);
  EXPECT_EQ(ObString::make_string("previous"), result.get_string());
  expr.extra_info_ = nullptr;
  EXPECT_EQ(OB_ERR_UNEXPECTED, fetch(result));
  EXPECT_EQ(previous, result.ptr_);
}
TEST_F(NativePLSQLVariable, immutable_old_evaluator_and_type_resolver_oracle) {
  std::ifstream input("unittest/sql/data/native_plsql_variable.golden");
  ASSERT_TRUE(input.good()); std::string line; int cases = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    const auto fields = plsql_fields(line); SCOPED_TRACE(line);
    ASSERT_EQ(14, fields.size()); ASSERT_EQ("P", fields[0]);
    const std::string name = plsql_unhex(fields[2]);
    const int provider_id = std::stoi(fields[4]);
    if (provider_id >= 0) {
      const std::string bytes = plsql_unhex(fields[5]); int64_t pos = 0; DatumValue value;
      ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(bytes.data(), bytes.size(), pos,
          owner, value.datum_, value.meta_));
      ASSERT_EQ(bytes.size(), pos); ObBasicSysVar *provider = nullptr;
      ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(static_cast<ObSysVarClassType>(provider_id), provider));
      ASSERT_NE(nullptr, provider); ASSERT_EQ(OB_SUCCESS, provider->set_value(value, owner));
    }
    const std::string meta_bytes = plsql_unhex(fields[6]); int64_t pos = 0; SqlTypeMeta meta;
    ASSERT_EQ(OB_SUCCESS, meta.deserialize(meta_bytes.data(), meta_bytes.size(), pos));
    ASSERT_EQ(meta_bytes.size(), pos); const int invalid = std::stoi(fields[7]);
    ObExprPLSQLVariable function(owner);
    ASSERT_EQ(OB_SUCCESS, function.deep_copy_plsql_variable(ObString(name.size(), name.data())));
    ObExprTypeCtx type_context; type_context.set_session(invalid == 2 ? nullptr : &session);
    ObExprResType type;
    EXPECT_EQ(std::stoi(fields[8]), function.calc_result_type0(type, type_context));
    EXPECT_EQ(fields[9], plsql_wire(type));
    configure(name.c_str(), meta); info.plsql_line_ = std::stoll(fields[3]);
    expr.extra_info_ = invalid == 1 ? nullptr : &info;
    ObDatum &result = expr.locate_expr_datum(*eval);
    std::memcpy(frames[0] + expr.res_buf_off_, "previous", 8);
    result.set_string(frames[0] + expr.res_buf_off_, 8);
    context.set_my_session(invalid == 2 ? nullptr : &session);
    const int ret = fetch(result); context.set_my_session(&session);
    EXPECT_EQ(std::stoi(fields[10]), ret); EXPECT_EQ(std::stoull(fields[11]), result.pack_);
    EXPECT_EQ(std::stoi(fields[12]) != 0, result.is_null());
    EXPECT_EQ(fields[13], plsql_hex(result.ptr_, result.len_));
    expr.extra_info_ = &info; ++cases;
  }
  EXPECT_EQ(28, cases);
}

TEST_F(NativePLSQLVariable, explicit_cursor_attribute_reads_native_datum_payload)
{
  pl::ObPLGetCursorAttrInfo cursor_attr;
  cursor_attr.type_ = pl::ObPLGetCursorAttrInfo::PL_CURSOR_ISOPEN;
  cursor_attr.is_explicit_ = true;
  ObExprPLGetCursorAttr::ExtraInfo cursor_attr_info(owner, T_FUN_PL_GET_CURSOR_ATTR);
  cursor_attr_info.pl_cursor_info_ = cursor_attr;

  ObExpr cursor_expr;
  ObExpr cursor_arg;
  ObExpr *cursor_args[1] = {&cursor_arg};
  cursor_expr.extra_info_ = &cursor_attr_info;
  cursor_expr.args_ = cursor_args;
  cursor_expr.arg_cnt_ = 1;
  cursor_arg.frame_idx_ = 0;
  cursor_arg.datum_off_ = 2048;
  cursor_arg.eval_info_off_ = 2080;
  cursor_arg.dyn_buf_header_offset_ = 2112;
  cursor_arg.res_buf_off_ = 2144;
  cursor_arg.res_buf_len_ = 64;
  cursor_arg.extra_info_ = nullptr;
  cursor_arg.eval_func_ = eval_cursor_attr_test_arg;
  cursor_arg.obj_meta_.set_ext();
  cursor_arg.datum_meta_.type_ = ObExtendType;
  new (frames[0] + cursor_arg.datum_off_) ObDatum();
  new (frames[0] + cursor_arg.eval_info_off_) ObEvalInfo();
  auto *dynamic = new (frames[0] + cursor_arg.dyn_buf_header_offset_) ObDynReserveBuf();
  dynamic->magic_ = ObDynReserveBuf::MAGIC_NUM;
  dynamic->len_ = cursor_arg.res_buf_len_;
  dynamic->mem_ = frames[0] + cursor_arg.res_buf_off_;

  pl::ObPLCursorInfo cursor;
  int spi_cursor_token = 0;
  cursor.open(&spi_cursor_token);
  char ext_payload[DATUM_EXTEND_PAYLOAD_SIZE];
  ObDatum input;
  input.ptr_ = ext_payload;
  input.set_extend(reinterpret_cast<int64_t>(&cursor), pl::PL_CURSOR_TYPE);
  CursorDatumExtraInfo input_info(owner, &input);
  cursor_arg.extra_info_ = &input_info;

  char result_buffer[sizeof(int64_t)] = {};
  ObDatum result;
  result.ptr_ = result_buffer;
  auto evaluate = [&]() {
    cursor_arg.get_eval_info(*eval).clear_evaluated_flag();
    return ObExprPLGetCursorAttr::calc_pl_get_cursor_attr(cursor_expr, *eval, result);
  };
  ASSERT_EQ(OB_SUCCESS, evaluate());
  EXPECT_FALSE(result.is_null());
  EXPECT_TRUE(result.get_bool());

  // The legacy 16-byte payload carries the same pointer and subtype. Keep
  // accepting it while reading the value directly from Datum bytes.
  char legacy_payload[DATUM_LEGACY_EXTEND_SIZE];
  input.get_wire_ext_payload(legacy_payload);
  input.ptr_ = legacy_payload;
  input.set_ext(DATUM_LEGACY_EXTEND_SIZE);
  ASSERT_EQ(OB_SUCCESS, evaluate());
  EXPECT_TRUE(result.get_bool());

  // Null cursor remains legal for ISOPEN and evaluates to false.
  input.set_null();
  ASSERT_EQ(OB_SUCCESS, evaluate());
  EXPECT_FALSE(result.get_bool());

  // The legacy 16-byte extended-null representation is also a legal null
  // cursor. Its first byte identifies ObNullType in the old inline object.
  std::memset(legacy_payload, 0, sizeof(legacy_payload));
  input.ptr_ = legacy_payload;
  input.set_ext(DATUM_LEGACY_EXTEND_SIZE);
  ASSERT_TRUE(input.is_extended_null());
  ASSERT_EQ(OB_SUCCESS, evaluate());
  EXPECT_FALSE(result.get_bool());

  // A valid Ext with a different subtype retains the established error.
  input.ptr_ = ext_payload;
  input.set_extend(reinterpret_cast<int64_t>(&cursor), pl::PL_OPAQUE_TYPE);
  EXPECT_EQ(OB_ERR_CURSOR_ATTR_APPLY,
            evaluate());

  // Do not read the payload fields when its descriptor has an unsupported size.
  input.len_ = DATUM_EXTEND_PAYLOAD_SIZE - 1;
  EXPECT_EQ(OB_ERR_CURSOR_ATTR_APPLY,
        evaluate());
}

} // namespace
