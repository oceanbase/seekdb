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

#include "common/datum/datum.h"
#include "common/enumset/ob_enum_set_meta.h"
#include "lib/charset/ob_charset.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_add.h"
#include "sql/engine/expr/ob_expr_equal.h"
#include "sql/engine/expr/ob_expr_int2ip.h"
#include "sql/engine/expr/ob_expr_func_round.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_expr_type_to_str.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include <gtest/gtest.h>
#include <array>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
struct SubschemaAttachment
{
  SubschemaAttachment(ObExecContext &exec, ObPhysicalPlanCtx &plan, ObSQLSessionInfo *session = nullptr)
    : exec_(exec), previous_(exec.get_physical_plan_ctx()), session_(session),
      previous_session_exec_(session == nullptr ? nullptr : session->get_cur_exec_ctx())
  {
    exec_.set_physical_plan_ctx(&plan);
    if (session_ != nullptr) { ObSQLSessionInfo::ExecCtxSessionRegister registration(*session_, &exec_); }
  }
  ~SubschemaAttachment()
  {
    if (session_ != nullptr) { ObSQLSessionInfo::ExecCtxSessionRegister registration(*session_, previous_session_exec_); }
    exec_.set_physical_plan_ctx(previous_);
  }
  ObExecContext &exec_;
  ObPhysicalPlanCtx *previous_;
  ObSQLSessionInfo *session_;
  ObExecContext *previous_session_exec_;
};

class NativeExprCallbacks : public ::testing::Test
{
protected:
  static constexpr int64_t BATCH = 35;
  static constexpr int64_t BLOCK = 16384;
  static constexpr int64_t RESULT = 2;
  NativeExprCallbacks()
    : arena_("NativeExprTest"), exec_(arena_), frame_{}, frames_{frame_.data()},
      eval_(exec_), batch_(eval_), expressions_{}, args_{&expressions_[0], &expressions_[1]},
      add_(arena_), equal_(arena_), round_(arena_), cg_(arena_, nullptr, nullptr), raw_(arena_),
      left_raw_(arena_), right_raw_(arena_), skip_memory_{} {}

  void SetUp() override
  {
    exec_.set_frames(frames_); eval_.frames_ = frames_; eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH); batch_.set_batch_idx(4);
    left_raw_.set_expr_type(T_REF_COLUMN); right_raw_.set_expr_type(T_REF_COLUMN);
    ASSERT_EQ(OB_SUCCESS, raw_.init_param_exprs(2));
    ASSERT_EQ(OB_SUCCESS, raw_.add_param_expr(&left_raw_));
    ASSERT_EQ(OB_SUCCESS, raw_.add_param_expr(&right_raw_));
    ASSERT_NO_FATAL_FAILURE(configure(ObNumberType, 38, 0, false, false));
  }

  void configure(SqlValueType type, ObPrecision precision, ObScale scale,
                 bool left_batch, bool right_batch)
  {
    for (int64_t col = 0; col < 3; ++col) {
      ObExpr &expr = expressions_[col];
      const bool is_batch = col == RESULT ? left_batch || right_batch
          : col == 0 ? left_batch : right_batch;
      expr.type_ = col == RESULT ? T_OP_ADD : T_REF_COLUMN;
      expr.obj_meta_.set_type(type); expr.obj_meta_.set_scale(scale);
      expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
      expr.datum_meta_ = ObDatumMeta(type, CS_TYPE_BINARY, scale, precision);
      expr.obj_datum_map_ = type == ObNumberType ? OBJ_DATUM_NUMBER
          : type == ObDecimalIntType ? OBJ_DATUM_DECIMALINT : OBJ_DATUM_8BYTE_DATA;
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(precision, scale), arena_));
      expr.frame_idx_ = 0; expr.datum_off_ = col * BLOCK;
      expr.eval_info_off_ = col * BLOCK + 640; expr.eval_flags_off_ = col * BLOCK + 704;
      expr.pvt_skip_off_ = col * BLOCK + 768;
      expr.res_buf_off_ = col * BLOCK + 1024;
      expr.res_buf_len_ = expr.get_sql_type().canonical_representation() == DatumRepresentation::Reference
          ? number::ObNumber::MAX_BYTE_LEN : 0;
      expr.dyn_buf_header_offset_ = 0;
      expr.batch_result_ = is_batch; expr.batch_idx_mask_ = is_batch ? UINT64_MAX : 0;
      expr.eval_func_ = nullptr; expr.eval_batch_func_ = nullptr;
      for (int64_t row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum();
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo{};
      expr.get_eval_info(eval_).evaluated_ = col != RESULT;
      expr.get_eval_info(eval_).notnull_ = true;
      expr.get_evaluated_flags(eval_).reset(BATCH); expr.get_pvt_skip(eval_).reset(BATCH);
    }
    skip().reset(BATCH); result().args_ = args_; result().arg_cnt_ = 2;
    raw_.set_expr_type(T_OP_ADD);
    ASSERT_EQ(OB_SUCCESS, add_.cg_expr(cg_, raw_, result()));
  }

  ObExpr &result() { return expressions_[RESULT]; }
  Datum &datum(int64_t col, int64_t row = 0)
  { return expressions_[col].locate_batch_datums(eval_)[row]; }
  ObBitVector &skip() { return *to_bit_vector(skip_memory_.data()); }
  void set_number(int64_t col, int64_t row, int64_t value)
  {
    number::ObNumber number;
    ObNumStackAllocator<> temporary;
    ASSERT_EQ(OB_SUCCESS, number.from(value, temporary));
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::set_number(expressions_[col], eval_, datum(col, row), number, row));
  }
  void set_decimal(int64_t col, int64_t row, int64_t value, int32_t bytes = sizeof(int64_t))
  {
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::set_decimal_int(expressions_[col], eval_, datum(col, row),
        reinterpret_cast<const ObDecimalInt *>(&value), bytes, row));
  }
  void expect_number(int64_t row, int64_t expected)
  {
    ASSERT_FALSE(datum(RESULT, row).is_null());
    number::ObNumber number(datum(RESULT, row).get_number());
    int64_t value = 0;
    ASSERT_TRUE(number.is_valid_int64(value)); EXPECT_EQ(expected, value);
    const char *bytes = static_cast<const char *>(datum(RESULT, row).get_reference());
    EXPECT_GE(bytes, frame_.data() + result().res_buf_off_);
    EXPECT_LT(bytes, frame_.data() + (RESULT + 1) * BLOCK);
  }
  void expect_decimal(int64_t row, int64_t expected)
  {
    oceanbase::common::ObDatum legacy;
    alignas(8) char scratch[64];
    ASSERT_EQ(OB_SUCCESS, result().get_datum_copy_plan().get_decimal(
        datum(RESULT, row), legacy, scratch, sizeof(scratch)));
    bool valid = false; int64_t value = 0;
    ASSERT_EQ(OB_SUCCESS, wide::check_range_valid_int64(
        legacy.get_decimal_int(), legacy.get_int_bytes(), valid, value));
    ASSERT_TRUE(valid); EXPECT_EQ(expected, value);
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 3 * BLOCK> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExpr expressions_[3]; ObExpr *args_[2];
  ObExprAdd add_; ObExprEqual equal_; ObExprFuncRound round_;
  ObExprCGCtx cg_; ObOpRawExpr raw_; ObColumnRefRawExpr left_raw_; ObColumnRefRawExpr right_raw_;
  alignas(8) std::array<char, 64> skip_memory_;
};

TEST_F(NativeExprCallbacks, scalar_number_callback_owns_stack_math_result)
{
  ASSERT_EQ(&ObExprAdd::add_number_number, result().eval_func_);
  set_number(0, 0, 19); set_number(1, 0, 23);
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
  ASSERT_EQ(&datum(RESULT), value); expect_number(0, 42);
  MEMSET(frame_.data() + expressions_[0].res_buf_off_, 0, number::ObNumber::MAX_BYTE_LEN);
  MEMSET(frame_.data() + expressions_[1].res_buf_off_, 0, number::ObNumber::MAX_BYTE_LEN);
  datum(0).set_null(); datum(1).set_null();
  expect_number(0, 42);
}

TEST_F(NativeExprCallbacks, batch_number_broadcast_null_skip_evaluated_and_tail_have_independent_owners)
{
  for (bool left_batch : {false, true}) {
    ASSERT_NO_FATAL_FAILURE(configure(ObNumberType, 38, 0, left_batch, !left_batch));
    ASSERT_EQ(&ObExprAdd::add_number_number_batch, result().eval_batch_func_);
    set_number(left_batch ? 1 : 0, 0, 7);
    for (int64_t row = 0; row < BATCH; ++row) { set_number(left_batch ? 0 : 1, row, row - 17); }
    datum(left_batch ? 0 : 1, 2).set_null(); skip().set(3);
    set_number(RESULT, 4, 999); result().get_evaluated_flags(eval_).set(4);
    ASSERT_EQ(OB_SUCCESS, result().eval_batch_func_(result(), eval_, skip(), BATCH));
    for (int64_t row = 0; row < BATCH; ++row) {
      if (row == 3) { EXPECT_TRUE(datum(RESULT, row).is_null()); EXPECT_FALSE(result().get_evaluated_flags(eval_).at(row)); }
      else if (row == 2) { EXPECT_TRUE(datum(RESULT, row).is_null()); EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row)); }
      else { expect_number(row, row == 4 ? 999 : row - 10); EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row)); }
    }
    EXPECT_EQ(4, eval_.get_batch_idx());
  }
}

TEST_F(NativeExprCallbacks, scalar_small_decimal_uses_inline_canonical_coefficient)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObDecimalIntType, 9, 2, false, false));
  ASSERT_EQ(&ObExprAdd::add_decimalint32, result().eval_func_);
  set_decimal(0, 0, -123); set_decimal(1, 0, 423);
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
  EXPECT_EQ(300, value->get_decimal_int32()); EXPECT_EQ(0, value->length());
  EXPECT_EQ(Datum::NoReferenceFlags, value->reference_flags());
  datum(0).set_null(); result().get_eval_info(eval_).clear_evaluated_flag();
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value)); EXPECT_TRUE(value->is_null());
}

TEST_F(NativeExprCallbacks, wide_decimal_callback_sign_extends_short_actual_spans_and_owns_result)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObDecimalIntType, 38, 5, false, false));
  ASSERT_EQ(&ObExprAdd::add_decimalint128, result().eval_func_);
  set_decimal(0, 0, -123, sizeof(int32_t)); set_decimal(1, 0, 423, sizeof(int32_t));
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value)); expect_decimal(0, 300);
  EXPECT_EQ(sizeof(int128_t), value->length());
  MEMSET(frame_.data(), 0, 2 * BLOCK); expect_decimal(0, 300);
}

TEST_F(NativeExprCallbacks, malformed_wide_input_error_does_not_publish_batch_slot_and_can_retry)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObDecimalIntType, 38, 5, true, false));
  set_decimal(1, 0, 7);
  for (int64_t row = 0; row < BATCH; ++row) { set_decimal(0, row, row); }
  const char invalid[3] = {1, 2, 3};
  ASSERT_EQ(OB_SUCCESS, datum(0, 2).set_reference(invalid, sizeof(invalid)));
  EXPECT_NE(OB_SUCCESS, result().eval_batch_func_(result(), eval_, skip(), 5));
  EXPECT_FALSE(result().get_evaluated_flags(eval_).at(2));
  EXPECT_FALSE(result().get_evaluated_flags(eval_).at(3));
  set_decimal(0, 2, 2);
  ASSERT_EQ(OB_SUCCESS, result().eval_batch_func_(result(), eval_, skip(), 5));
  for (int64_t row = 0; row < 5; ++row) { expect_decimal(row, row + 7); }
  EXPECT_EQ(4, eval_.get_batch_idx());
}

TEST_F(NativeExprCallbacks, codegen_decimal_comparison_uses_bound_domain_and_sql_null_contract)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObDecimalIntType, 38, 5, false, false));
  result().type_ = T_OP_EQ; result().obj_meta_.set_int();
  result().datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 20);
  ASSERT_EQ(OB_SUCCESS, result().prepare_scalar_type(result().obj_meta_, ObAccuracy(20, 0), arena_));
  raw_.set_expr_type(T_OP_EQ);
  ASSERT_EQ(OB_SUCCESS, equal_.cg_expr(cg_, raw_, result()));
  ASSERT_EQ(1, result().get_cmp_op_count());
  set_decimal(0, 0, -300, sizeof(int32_t)); set_decimal(1, 0, -300);
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value)); EXPECT_TRUE(value->get_bool());
  result().get_eval_info(eval_).clear_evaluated_flag(); datum(1).set_null();
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value)); EXPECT_TRUE(value->is_null());
}

TEST_F(NativeExprCallbacks, real_round_callback_applies_scale_before_native_publication)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObDecimalIntType, 18, 2, false, false));
  result().type_ = T_FUN_SYS_ROUND; result().arg_cnt_ = 1;
  result().datum_meta_.scale_ = 0; result().obj_meta_.set_scale(0);
  ASSERT_EQ(OB_SUCCESS, result().prepare_scalar_type(result().obj_meta_, ObAccuracy(18, 0), arena_));
  ASSERT_EQ(OB_SUCCESS, round_.cg_expr(cg_, raw_, result()));
  const int64_t cases[][2] = {{125, 1}, {150, 2}, {-150, -2}, {0, 0}};
  for (const auto &item : cases) {
    set_decimal(0, 0, item[0]); result().get_eval_info(eval_).clear_evaluated_flag();
    Datum *value = nullptr;
    ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value)); expect_decimal(0, item[1]);
  }
}

TEST_F(NativeExprCallbacks, explicit_legacy_export_owns_inline_payload_after_source_overwrite)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObDecimalIntType, 9, 2, false, false));
  set_decimal(0, 0, -123);
  oceanbase::common::ObDatum legacy;
  ASSERT_EQ(OB_SUCCESS, ObExprUtil::export_legacy_value(expressions_[0], datum(0), arena_, legacy));
  datum(0).set_int(999);
  EXPECT_EQ(sizeof(int32_t), legacy.get_int_bytes()); EXPECT_EQ(-123, *legacy.get_decimal_int()->int32_v_);
}

TEST_F(NativeExprCallbacks, runtime_enum_metadata_uses_definition_collation_and_preserves_requested_kind)
{
  ObSEArray<ObString, 2> labels;
  char first[] = "alpha"; char second[] = "beta";
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string(first)));
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string(second)));
  // SeekDB enables binary and UTF8MB4 collations; Latin1 metadata has no
  // supported charset mapping. Exercise a real nondefault definition here.
  ASSERT_TRUE(ObCharset::is_valid_collation(CS_TYPE_UTF8MB4_UNICODE_CI));
  SqlTypeMeta enum_meta; enum_meta.set_enum();
  enum_meta.set_collation_type(CS_TYPE_UTF8MB4_UNICODE_CI); enum_meta.set_collation_level(CS_LEVEL_IMPLICIT);
  SqlTypeMeta set_meta = enum_meta; set_meta.set_type_simple(ObSetType);
  ObEnumSetMeta definitions[2] = {ObEnumSetMeta(enum_meta, &labels), ObEnumSetMeta(set_meta, &labels)};
  ObPhysicalPlanCtx plan(arena_);
  SubschemaAttachment attachment(exec_, plan);
  ASSERT_EQ(OB_SUCCESS, plan.get_subschema_ctx().init());
  uint16_t ids[2];
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, plan.get_subschema_ctx().get_new_subschema_id(ids[i]));
    ObSubSchemaValue value; value.type_ = OB_SUBSCHEMA_ENUM_SET_TYPE;
    value.value_ = &definitions[i]; value.signature_ = i + 1;
    ASSERT_EQ(OB_SUCCESS, plan.get_subschema_ctx().set_subschema(ids[i], value));
  }
  ObAccuracy accuracy; accuracy.set_length(32); accuracy.set_length_semantics(LS_CHAR);
  const SqlValueType kinds[] = {ObEnumType, ObEnumInnerType, ObSetType, ObSetInnerType};
  const SqlType *retained = nullptr;
  for (const auto kind : kinds) {
    SqlTypeMeta requested; requested.set_type(kind); requested.set_subschema_id(ids[kind == ObEnumType || kind == ObEnumInnerType ? 0 : 1]);
    const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_sql_type(exec_, requested, accuracy,
        ids[kind == ObEnumType || kind == ObEnumInnerType ? 0 : 1], arena_, type));
    ASSERT_NE(nullptr, type); EXPECT_EQ(kind, type->kind());
    EXPECT_EQ(CS_TYPE_UTF8MB4_UNICODE_CI, type->collation());
    EXPECT_EQ(CHARSET_UTF8MB4, type->attributes().get_charset_type());
    EXPECT_EQ(CS_LEVEL_IMPLICIT, type->legacy_meta().get_collation_level());
    EXPECT_EQ(accuracy.get_accuracy(), type->accuracy().get_accuracy());
    ASSERT_EQ(2, type->label_count()); EXPECT_EQ(ObString::make_string("alpha"), type->label(0));
    EXPECT_EQ(ObString::make_string("beta"), type->label(1)); retained = type;
  }
  MEMSET(first, 'x', sizeof(first) - 1); MEMSET(second, 'y', sizeof(second) - 1);
  EXPECT_EQ(ObString::make_string("alpha"), retained->label(0));
  EXPECT_EQ(ObString::make_string("beta"), retained->label(1));
  const SqlType *unchanged = retained;
  EXPECT_NE(OB_SUCCESS, ObExprUtil::resolve_sql_type(exec_, enum_meta, accuracy, UINT16_MAX, arena_, unchanged));
  EXPECT_EQ(retained, unchanged);
}

TEST_F(NativeExprCallbacks, raw_enum_sql_and_pl_subschema_states_recover_actual_collation)
{
  ObSEArray<ObString, 2> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("alpha")));
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("beta")));
  SqlTypeMeta enum_meta; enum_meta.set_enum();
  enum_meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI); enum_meta.set_collation_level(CS_LEVEL_IMPLICIT);
  SqlTypeMeta set_meta = enum_meta; set_meta.set_type_simple(ObSetType);
  ObEnumSetMeta definitions[2] = {ObEnumSetMeta(enum_meta, &labels), ObEnumSetMeta(set_meta, &labels)};
  ObPhysicalPlanCtx plan(arena_); ObSQLSessionInfo session;
  SubschemaAttachment attachment(exec_, plan, &session);
  ASSERT_EQ(OB_SUCCESS, plan.get_subschema_ctx().init());
  uint16_t ids[2];
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, plan.get_subschema_ctx().get_new_subschema_id(ids[i]));
    ObSubSchemaValue value; value.type_ = OB_SUBSCHEMA_ENUM_SET_TYPE;
    value.value_ = &definitions[i]; value.signature_ = i + 1;
    ASSERT_EQ(OB_SUCCESS, plan.get_subschema_ctx().set_subschema(ids[i], value));
  }
  for (int i = 0; i < 2; ++i) {
    for (const auto state : {ObEnumSetMeta::SQL, ObEnumSetMeta::PL}) {
      ObRawExprResType resolved;
      resolved.set_type(i == 0 ? ObEnumType : ObSetType);
      resolved.set_subschema_id(ids[i]); resolved.set_length(32);
      resolved.mark_enum_set_with_subschema(state);
      const SqlType *type = nullptr;
      ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_raw_sql_type(resolved, &session, nullptr, arena_, type));
      ASSERT_NE(nullptr, type); EXPECT_EQ(resolved.get_type(), type->kind());
      EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, type->collation());
      EXPECT_EQ(CHARSET_UTF8MB4, type->attributes().get_charset_type());
      EXPECT_EQ(CS_LEVEL_IMPLICIT, type->legacy_meta().get_collation_level());
      EXPECT_EQ(resolved.get_accuracy().get_accuracy(), type->accuracy().get_accuracy());
      ASSERT_EQ(2, type->label_count()); EXPECT_EQ(ObString::make_string("beta"), type->label(1));
    }
  }
}

TEST_F(NativeExprCallbacks, actual_inner_conversion_cg_keeps_complete_labels_and_native_result)
{
  ObSEArray<ObString, 2> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("alpha")));
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("beta")));
  for (bool is_set : {false, true}) {
    ASSERT_NO_FATAL_FAILURE(configure(ObNumberType, 38, 0, false, false));
    ObAccuracy accuracy; accuracy.set_length(32);
    SqlTypeMeta source_meta; source_meta.set_type(is_set ? ObSetType : ObEnumType);
    source_meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI); source_meta.set_collation_level(CS_LEVEL_IMPLICIT);
    const SqlType *source_type = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_enum_sql_type(source_meta, accuracy, labels, arena_, source_type));
    expressions_[1].obj_meta_ = source_meta;
    expressions_[1].datum_meta_ = ObDatumMeta(source_meta.get_type(), CS_TYPE_UTF8MB4_GENERAL_CI, 0, 0);
    ASSERT_EQ(OB_SUCCESS, expressions_[1].bind_sql_type(*source_type, arena_));
    if (is_set) { datum(1).set_set(3); } else { datum(1).set_enum(2); }
    SqlTypeMeta actual_meta = source_meta; actual_meta.set_type_simple(is_set ? ObSetInnerType : ObEnumInnerType);
    result().obj_meta_ = actual_meta;
    result().datum_meta_ = ObDatumMeta(actual_meta.get_type(), CS_TYPE_UTF8MB4_GENERAL_CI, 0, 0);
    result().type_ = is_set ? T_FUN_SET_TO_INNER_TYPE : T_FUN_ENUM_TO_INNER_TYPE;
    result().res_buf_len_ = 256;
    const SqlType *result_type = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_enum_sql_type(actual_meta, accuracy, labels, arena_, result_type));
    ASSERT_EQ(OB_SUCCESS, result().bind_sql_type(*result_type, arena_));
    // This is the same actual operation's labels source used by early CG;
    // its production cg_expr still owns/copies its runtime ObEnumSetInfo.
    ObExprEnumToInnerType enum_op(arena_); ObExprSetToInnerType set_op(arena_);
    ObExprTypeToStr &operation = is_set ? static_cast<ObExprTypeToStr &>(set_op) : static_cast<ObExprTypeToStr &>(enum_op);
    ASSERT_EQ(OB_SUCCESS, operation.shallow_copy_str_values(labels));
    ASSERT_EQ(OB_SUCCESS, operation.cg_expr(cg_, raw_, result()));
    ASSERT_EQ(is_set ? &ObExprSetToInnerType::calc_to_inner_expr : &ObExprEnumToInnerType::calc_to_inner_expr, result().eval_func_);
    Datum *value = nullptr;
    ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
    ObEnumSetInnerValue inner;
    ASSERT_EQ(OB_SUCCESS, value->get_enumset_inner(inner));
    EXPECT_EQ(is_set ? 3U : 2U, inner.numberic_value_);
    EXPECT_EQ(ObString::make_string(is_set ? "alpha,beta" : "beta"), inner.string_value_);
    EXPECT_EQ(actual_meta.get_type(), result().get_sql_type().kind());
    EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, result().get_sql_type().collation());
    ASSERT_EQ(2, result().get_sql_type().label_count());
    EXPECT_EQ(ObString::make_string("beta"), result().get_sql_type().label(1));
  }
}

TEST_F(NativeExprCallbacks, int2ip_callback_uses_expression_buffer_from_fresh_null_native_slot)
{
  ASSERT_NO_FATAL_FAILURE(configure(ObIntType, 19, 0, false, false));
  result().type_ = T_FUN_SYS_INT2IP;
  result().obj_meta_.set_varchar();
  result().obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  result().obj_meta_.set_collation_level(CS_LEVEL_COERCIBLE);
  result().datum_meta_ = ObDatumMeta(ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI, 0, 0);
  result().obj_datum_map_ = OBJ_DATUM_STRING;
  ObAccuracy accuracy; accuracy.set_length(16);
  ASSERT_EQ(OB_SUCCESS, result().prepare_scalar_type(result().obj_meta_, accuracy, arena_));
  result().res_buf_len_ = 16;
  result().arg_cnt_ = 1;
  ObExprInt2ip operation(arena_);
  ASSERT_EQ(OB_SUCCESS, operation.cg_expr(cg_, raw_, result()));
  const int64_t inputs[] = {0, 1, 2130706433, UINT32_MAX, -1, INT64_C(1) << 32};
  const char *expected[] = {"0.0.0.0", "0.0.0.1", "127.0.0.1", "255.255.255.255", nullptr, nullptr};
  for (int64_t i = 0; i < 6; ++i) {
    datum(0).set_int(inputs[i]);
    datum(RESULT).set_null();
    result().get_eval_info(eval_).evaluated_ = false;
    Datum *output = nullptr;
    ASSERT_EQ(OB_SUCCESS, result().eval(eval_, output));
    ASSERT_NE(nullptr, output);
    if (expected[i] == nullptr) {
      EXPECT_TRUE(output->is_null());
    } else {
      ASSERT_FALSE(output->is_null());
      EXPECT_EQ(ObString::make_string(expected[i]), output->get_string());
      EXPECT_EQ(frame_.data() + result().res_buf_off_, output->get_string().ptr());
    }
  }
  datum(0).set_null(); datum(RESULT).set_null();
  result().get_eval_info(eval_).evaluated_ = false;
  Datum *output = nullptr;
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, output));
  ASSERT_NE(nullptr, output); EXPECT_TRUE(output->is_null());
  datum(0).set_int(1); datum(RESULT).set_null();
  result().res_buf_len_ = 15;
  EXPECT_EQ(OB_ERR_UNEXPECTED, operation.int2ip_varchar(result(), eval_, datum(RESULT)));
  EXPECT_TRUE(datum(RESULT).is_null());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
