/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_assign.h"
#include "sql/engine/expr/ob_expr_get_user_var.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase { namespace sql {
// Existing expression evaluator, not a test wrapper or a value conversion.
int calc_assign_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
} }

namespace {
class NativeUserVariable : public testing::Test {
protected:
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  ObExecContext context;
  char *frames[1] = {nullptr};
  ObExpr key, input, output;
  ObExpr *arguments[2] = {&key, &input};
  std::unique_ptr<ObEvalCtx> eval;

  NativeUserVariable() : context(arena) {}
  void configure(ObExpr &expr, int64_t base, SqlValueType type) {
    expr.type_ = T_EXEC_VAR;
    expr.frame_idx_ = 0; expr.datum_off_ = base; expr.eval_info_off_ = base + 32;
    expr.dyn_buf_header_offset_ = base + 96;
    expr.res_buf_off_ = base + 128; expr.res_buf_len_ = 256;
    expr.obj_meta_.set_type(type); expr.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    expr.datum_meta_ = ObDatumMeta(type, CS_TYPE_UTF8MB4_BIN, -1);
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
    new (frames[0] + base) ObDatum();
    new (frames[0] + base + 32) ObEvalInfo();
    auto *dynamic = new (frames[0] + base + 96) ObDynReserveBuf();
    dynamic->magic_ = ObDynReserveBuf::MAGIC_NUM;
    dynamic->len_ = expr.res_buf_len_; dynamic->mem_ = frames[0] + expr.res_buf_off_;
  }
  void SetUp() override {
    ASSERT_EQ(OB_SUCCESS, session.get_user_var_val_map().init(1024 * 1024, 16, nullptr));
    context.set_my_session(&session);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &context);
    frames[0] = static_cast<char *>(arena.alloc(8192));
    ASSERT_NE(nullptr, frames[0]); std::memset(frames[0], 0, 8192);
    context.set_frames(frames); context.set_frame_cnt(1);
    eval.reset(new ObEvalCtx(context));
    configure(key, 0, ObVarcharType); configure(input, 512, ObVarcharType);
    configure(output, 1536, ObVarcharType);
    key.locate_datum_for_write(*eval).set_string("u", 1);
    key.get_eval_info(*eval).evaluated_ = true;
    output.args_ = arguments; output.arg_cnt_ = 1;
  }
  void TearDown() override {
    eval.reset(); context.set_frames(nullptr); context.set_frame_cnt(0);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, nullptr);
  }
  int store(const DatumValue &value, const SqlTypeMeta *declaration = nullptr) {
    ObSessionVariable record;
    record.value_ = value; record.meta_ = declaration == nullptr ? value.meta_ : *declaration;
    return session.ObBasicSessionInfo::replace_user_variable(
        ObString::make_string("u"), record, false);
  }
  int fetch(ObDatum &result) {
    output.arg_cnt_ = 1;
    return ObExprGetUserVar::eval_get_user_var(output, *eval, result);
  }
  void set_output_type(SqlValueType type, int16_t scale = -1, int16_t precision = -1) {
    output.obj_meta_.set_type(type);
    output.obj_meta_.set_scale(scale);
    output.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    output.datum_meta_ = ObDatumMeta(type, CS_TYPE_UTF8MB4_BIN, scale, precision);
    output.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
  }
};

TEST_F(NativeUserVariable, raw_getters_borrow_map_snapshot_and_keep_independent_metadata) {
  DatumValue source;
  ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString::make_string("owned-by-map"), arena));
  source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  SqlTypeMeta declaration = source.meta_;
  declaration.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI); declaration.set_scale(12);
  ASSERT_EQ(OB_SUCCESS, store(source, &declaration));
  std::memset(const_cast<char *>(source.datum_.ptr_), 'x', source.datum_.len_);
  DatumValue raw;
  ASSERT_EQ(OB_SUCCESS, session.get_user_variable_value(ObString::make_string("u"), raw));
  const DatumValue *pointer = session.get_user_variable_value(ObString::make_string("u"));
  ASSERT_NE(nullptr, pointer); EXPECT_EQ(raw.datum_.ptr_, pointer->datum_.ptr_);
  EXPECT_NE(source.datum_.ptr_, raw.datum_.ptr_);
  EXPECT_EQ(ObString::make_string("owned-by-map"), raw.get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, raw.get_collation_type());
  ObSessionVariable record;
  ASSERT_EQ(OB_SUCCESS, session.get_user_variable(ObString::make_string("u"), record));
  EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, record.meta_.get_collation_type());
  EXPECT_EQ(12, record.meta_.get_scale());
}

TEST_F(NativeUserVariable, getter_error_contract_and_empty_record_name_are_unchanged) {
  DatumValue sentinel;
  ASSERT_EQ(OB_SUCCESS, sentinel.set_int(47, arena));
  const char *old = sentinel.datum_.ptr_;
  EXPECT_EQ(OB_INVALID_ARGUMENT, session.get_user_variable_value(ObString(), sentinel));
  EXPECT_EQ(old, sentinel.datum_.ptr_); EXPECT_EQ(47, sentinel.get_int());
  EXPECT_EQ(OB_ERR_USER_VARIABLE_UNKNOWN,
      session.get_user_variable_value(ObString::make_string("absent"), sentinel));
  EXPECT_EQ(old, sentinel.datum_.ptr_); EXPECT_EQ(nullptr, session.get_user_variable_value(ObString()));
  ObSessionVariable record; record.value_ = sentinel;
  EXPECT_EQ(OB_SUCCESS, session.get_user_variable(ObString(), record));
  EXPECT_EQ(old, record.value_.datum_.ptr_);
}

TEST_F(NativeUserVariable, assignment_copies_frame_bytes_before_next_input_evaluation) {
  char text[] = "frame-content";
  input.locate_datum_for_write(*eval).set_string(text, sizeof(text) - 1);
  input.get_eval_info(*eval).evaluated_ = true;
  output.arg_cnt_ = 2;
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, calc_assign_expr(output, *eval, result));
  std::memset(text, 'x', sizeof(text) - 1);
  DatumValue stored;
  ASSERT_EQ(OB_SUCCESS, session.get_user_variable_value(ObString::make_string("u"), stored));
  EXPECT_EQ(ObString::make_string("frame-content"), stored.get_string());
  EXPECT_NE(text, stored.datum_.ptr_);
}

TEST_F(NativeUserVariable, null_assignment_canonicalizes_raw_value_but_retains_session_collation) {
  input.locate_datum_for_write(*eval).set_null();
  input.get_eval_info(*eval).evaluated_ = true;
  output.arg_cnt_ = 2;
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, calc_assign_expr(output, *eval, result));
  ObSessionVariable record;
  ASSERT_EQ(OB_SUCCESS, session.get_user_variable(ObString::make_string("u"), record));
  EXPECT_TRUE(record.value_.is_null()); EXPECT_EQ(ObNullType, record.value_.get_type());
  EXPECT_EQ(ObNullType, record.meta_.get_type());
  EXPECT_EQ(CS_LEVEL_IMPLICIT, record.meta_.get_collation_level());
  EXPECT_EQ(ObCharset::get_default_collation(ObCharset::get_default_charset()),
      record.meta_.get_collation_type());
}

TEST_F(NativeUserVariable, result_frame_survives_map_replacement_and_source_arena_reset) {
  ObDatum result;
  const std::string text(2048, 'q'); // force the expression's dynamic result buffer
  {
    ObArenaAllocator source_owner;
    DatumValue source;
    ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString(text.size(), text.data()), source_owner));
    source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, store(source));
    ASSERT_EQ(OB_SUCCESS, fetch(result));
    const DatumValue *stored = session.get_user_variable_value(ObString::make_string("u"));
    ASSERT_NE(nullptr, stored); EXPECT_NE(stored->datum_.ptr_, result.ptr_);
  }
  DatumValue replacement;
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(123, arena));
  ASSERT_EQ(OB_SUCCESS, store(replacement));
  EXPECT_EQ(text, std::string(result.ptr_, result.len_));
  EXPECT_EQ(result.ptr_, output.locate_expr_datum(*eval).ptr_);
}

TEST_F(NativeUserVariable, changed_value_type_is_cast_to_the_prepared_expression_type) {
  DatumValue integer;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(12345, arena));
  ASSERT_EQ(OB_SUCCESS, store(integer));
  set_output_type(ObVarcharType);
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_EQ(ObString::make_string("12345"), result.get_string());
  const char *previous = result.ptr_;
  context.get_eval_tmp_allocator().reuse();
  EXPECT_EQ(ObString::make_string("12345"), result.get_string());
  EXPECT_EQ(previous, output.locate_expr_datum(*eval).ptr_);
}

TEST_F(NativeUserVariable, decimal_coefficient_is_extended_to_expression_frame_width) {
  const int32_t coefficient = -12345;
  DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(sizeof(coefficient), 2,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), arena));
  ASSERT_EQ(OB_SUCCESS, store(decimal));
  set_output_type(ObDecimalIntType, 2, 65);
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_EQ(sizeof(int512_t), result.get_int_bytes());
  EXPECT_EQ(int512_t(coefficient), result.get_decimal_int512());
  EXPECT_NE(decimal.datum_.ptr_, result.ptr_);
}

TEST_F(NativeUserVariable, expression_projects_independent_decimal_declaration_scale) {
  const int32_t coefficient = 12345;
  DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(sizeof(coefficient), 2,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), arena));
  SqlTypeMeta declaration = decimal.meta_; declaration.set_scale(3);
  ASSERT_EQ(OB_SUCCESS, store(decimal, &declaration));
  set_output_type(ObVarcharType);
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result));
  EXPECT_EQ(ObString::make_string("12.345"), result.get_string());
  DatumValue raw;
  ASSERT_EQ(OB_SUCCESS, session.get_user_variable_value(ObString::make_string("u"), raw));
  EXPECT_EQ(2, raw.get_scale()); // projection never rewrites the stored raw value
}

TEST_F(NativeUserVariable, lob_results_adjust_both_header_directions_before_frame_copy) {
  for (bool source_has_header : {false, true}) {
    DatumValue source;
    if (source_has_header) {
      ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::str_to_lob_storage_obj(
          arena, ObString::make_string("lob-content"), source));
    } else {
      ASSERT_EQ(OB_SUCCESS, source.set_string(
          ObLongTextType, ObString::make_string("lob-content"), arena));
      source.meta_.set_inrow();
    }
    source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, store(source));
    set_output_type(ObLongTextType);
    output.obj_meta_.set_inrow();
    if (!source_has_header) { output.obj_meta_.set_has_lob_header(); }
    ObDatum result;
    ASSERT_EQ(OB_SUCCESS, fetch(result));
    ObString content = result.get_string();
    if (!source_has_header) {
      ObLobLocatorV2 locator(content, true);
      ASSERT_EQ(OB_SUCCESS, locator.get_inrow_data(content));
    }
    EXPECT_EQ(ObString::make_string("lob-content"), content);
    EXPECT_NE(source.datum_.ptr_, result.ptr_);
  }
}

TEST_F(NativeUserVariable, unknown_variable_returns_null_without_map_insertion) {
  key.locate_datum_for_write(*eval).set_string("absent", 6);
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, fetch(result)); EXPECT_TRUE(result.is_null());
  EXPECT_EQ(0, session.get_user_var_val_map().size());
}
} // namespace
