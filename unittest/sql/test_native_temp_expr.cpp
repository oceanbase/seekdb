/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
class NativeTempExpr : public testing::Test {
protected:
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  ObTempExpr expression;
  ObExecContext context;
  NativeTempExpr() : expression(arena), context(arena) {}
  void SetUp() override {
    context.set_my_session(&session);
    ASSERT_EQ(OB_SUCCESS, expression.rt_exprs_.prepare_allocate(1));
    ASSERT_EQ(OB_SUCCESS, expression.datum_frame_.init(1));
    ASSERT_EQ(OB_SUCCESS, expression.idx_col_arr_.init(1));
    ASSERT_EQ(OB_SUCCESS, expression.datum_frame_.push_back(ObFrameInfo(1, 0, 512, 0, 512)));
    ASSERT_EQ(OB_SUCCESS, expression.idx_col_arr_.push_back(RowIdxColumnPair(0, 0)));
    expression.expr_idx_ = 0;
    ObExpr &input = expression.rt_exprs_.at(0);
    input.type_ = T_REF_COLUMN;
    input.frame_idx_ = 0; input.datum_off_ = 0; input.eval_info_off_ = 32;
    input.res_buf_off_ = 128; input.res_buf_len_ = 128;
    input.obj_meta_.set_varchar(); input.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    input.datum_meta_ = ObDatumMeta(ObVarcharType, CS_TYPE_UTF8MB4_BIN, -1);
    input.obj_datum_map_ = OBJ_DATUM_STRING;
  }
};

TEST_F(NativeTempExpr, result_owns_bytes_across_input_and_context_reuse) {
  for (bool cached : {false, true}) {
    context.set_use_temp_expr_ctx_cache(cached);
    ObArenaAllocator output_owner;
    ObDatum previous;
    for (int i = 0; i < 3; ++i) {
      ObArenaAllocator source;
      DatumValue value;
      const std::string text = "row-" + std::to_string(i);
      ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString(text.size(), text.data()), source));
      value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      ObSEArray<DatumValue, 1> row;
      ASSERT_EQ(OB_SUCCESS, row.push_back(value));
      ObDatum result;
      ASSERT_EQ(OB_SUCCESS, expression.eval_datum(context, row, output_owner, result));
      ASSERT_NE(value.datum_.ptr_, result.ptr_);
      std::memset(const_cast<char *>(value.datum_.ptr_), 'x', value.datum_.len_);
      EXPECT_EQ(text, std::string(result.ptr_, result.len_));
      if (i > 0) { EXPECT_EQ("row-" + std::to_string(i - 1), std::string(previous.ptr_, previous.len_)); }
      previous = result;
    }
  }
}

TEST_F(NativeTempExpr, null_rows_do_not_require_matching_payload_type) {
  DatumValue value;
  ObSEArray<DatumValue, 1> row;
  ASSERT_EQ(OB_SUCCESS, row.push_back(value));
  ObArenaAllocator output_owner;
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, expression.eval_datum(context, row, output_owner, result));
  EXPECT_TRUE(result.is_null());
}

TEST_F(NativeTempExpr, invalid_row_mapping_and_type_preserve_existing_result) {
  ObArenaAllocator output_owner;
  DatumValue input, old;
  ASSERT_EQ(OB_SUCCESS, input.set_int(19, arena));
  ASSERT_EQ(OB_SUCCESS, old.set_int(47, output_owner));
  ObDatum result = old.datum_;
  const char *previous = result.ptr_;
  ObSEArray<DatumValue, 1> row;
  ASSERT_EQ(OB_SUCCESS, row.push_back(input));
  EXPECT_EQ(OB_ERR_UNEXPECTED, expression.eval_datum(context, row, output_owner, result));
  EXPECT_EQ(previous, result.ptr_); EXPECT_EQ(47, result.get_int());
  expression.idx_col_arr_.at(0).idx_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, expression.eval_datum(context, row, output_owner, result));
  expression.idx_col_arr_.at(0).idx_ = 0;
  expression.idx_col_arr_.at(0).expr_pos_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, expression.eval_datum(context, row, output_owner, result));
  expression.expr_idx_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, expression.eval_datum(context, row, output_owner, result));
  EXPECT_EQ(previous, result.ptr_); EXPECT_EQ(47, result.get_int());
}

class RejectOutputAllocator : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};

TEST_F(NativeTempExpr, failed_output_clone_preserves_output_and_can_retry) {
  DatumValue input, old;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("retry"), arena));
  input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObArenaAllocator output_owner;
  ASSERT_EQ(OB_SUCCESS, old.set_int(47, output_owner));
  ObDatum result = old.datum_;
  const char *previous = result.ptr_;
  ObSEArray<DatumValue, 1> row;
  ASSERT_EQ(OB_SUCCESS, row.push_back(input));
  RejectOutputAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, expression.eval_datum(context, row, reject, result));
  EXPECT_EQ(previous, result.ptr_); EXPECT_EQ(47, result.get_int());
  ASSERT_EQ(OB_SUCCESS, expression.eval_datum(context, row, output_owner, result));
  EXPECT_EQ("retry", std::string(result.ptr_, result.len_));
}

TEST_F(NativeTempExpr, extended_row_payload_is_copied_out_of_normalization_scratch) {
  context.set_use_temp_expr_ctx_cache(true);
  ObExpr &input = expression.rt_exprs_.at(0);
  input.obj_meta_.set_ext(); input.datum_meta_ = ObDatumMeta(ObExtendType, CS_TYPE_BINARY, -1);
  input.obj_datum_map_ = OBJ_DATUM_EXTEND;
  DatumValue minimum;
  minimum.set_min_value();
  ObSEArray<DatumValue, 1> row;
  ASSERT_EQ(OB_SUCCESS, row.push_back(minimum));
  ObArenaAllocator output_owner;
  ObDatum result;
  ASSERT_EQ(OB_SUCCESS, expression.eval_datum(context, row, output_owner, result));
  EXPECT_TRUE(result.is_min()); EXPECT_EQ(DATUM_MIN_EXT_VALUE, result.get_ext());
  const char *previous = result.ptr_;
  row.at(0).set_max_value();
  ObDatum next;
  ASSERT_EQ(OB_SUCCESS, expression.eval_datum(context, row, output_owner, next));
  EXPECT_TRUE(next.is_max()); EXPECT_NE(previous, next.ptr_);
  EXPECT_TRUE(result.is_min()); EXPECT_EQ(DATUM_MIN_EXT_VALUE, result.get_ext());
}
} // namespace
int main(int argc, char **argv) {
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
