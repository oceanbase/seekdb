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

#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/das/ob_das_utils.h"
#include "sql/engine/expr/ob_expr_pl_integer_checker.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_spi.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "common/datum/datum_parameter_array.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include <gtest/gtest.h>
#include <string>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
struct NativeCastContext {
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  ObExecContext context;
  NativeCastContext() : arena(), session(), context(arena) {
    context.set_my_session(&session);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &context);
  }
  ~NativeCastContext() { ObSQLSessionInfo::ExecCtxSessionRegister registration(session, nullptr); }
};
ObExprResType target(SqlValueType type, int64_t length = -1,
                      ObCollationType collation = CS_TYPE_BINARY) {
  ObExprResType result;
  result.set_type(type); result.set_collation_type(collation);
  result.set_collation_level(CS_LEVEL_IMPLICIT);
  result.set_length(length); result.set_scale(-1);
  return result;
}
class FailingCastAllocator : public ObIAllocator {
public:
  ObArenaAllocator arena;
  bool reject = false;
  int64_t allocations = 0, frees = 0;
  void *alloc(int64_t bytes) override { if (reject) { return nullptr; } ++allocations; return arena.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *p) override { if (p != nullptr) { ++frees; } arena.free(p); }
};
}
TEST(NativeColumnCast, no_sql_frames_needed_and_output_survives_source_and_caster_reuse) {
  NativeCastContext state;
  ObArenaAllocator owner;
  DatumParameter output;
  EXPECT_EQ(nullptr, state.context.get_frames());
  {
    ObArenaAllocator source_owner;
    DatumParameter input;
    ASSERT_EQ(OB_SUCCESS, input.set_int(12345, source_owner));
    ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
        input, target(ObVarcharType, 32, CS_TYPE_UTF8MB4_BIN), CM_STRICT_MODE, owner, output));
    EXPECT_EQ(ObString::make_string("12345"), output.get_string());
    EXPECT_NE(input.datum_.ptr_, output.datum_.ptr_);
  }
  ObDatumCaster *first = nullptr, *second = nullptr;
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(first));
  char *replacement[1] = {nullptr};
  state.context.set_frames(replacement); state.context.set_frame_cnt(1);
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(second));
  EXPECT_EQ(first, second);
  DatumParameter input, later;
  ASSERT_EQ(OB_SUCCESS, input.set_int(67890, owner));
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, target(ObVarcharType, 32, CS_TYPE_UTF8MB4_BIN), CM_STRICT_MODE, owner, later));
  EXPECT_EQ(ObString::make_string("12345"), output.get_string());
  EXPECT_EQ(ObString::make_string("67890"), later.get_string());
  state.context.set_frames(nullptr); state.context.set_frame_cnt(0);
}
TEST(NativeColumnCast, nullable_and_not_null_checks_preserve_previous_output_on_error) {
  NativeCastContext state;
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(42, state.arena));
  ObExprResType type = target(ObIntType);
  type.set_result_flag(NOT_NULL_WRITE_FLAG);
  EXPECT_EQ(OB_BAD_NULL_ERROR, ObExprColumnConv::convert_with_null_check(state.context,
      input, type, CM_STRICT_MODE, state.arena, output));
  EXPECT_EQ(42, output.get_int());
  type.unset_result_flag(NOT_NULL_WRITE_FLAG);
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, type, CM_STRICT_MODE, state.arena, output));
  EXPECT_TRUE(output.is_null()); EXPECT_EQ(ObIntType, output.get_param_meta().get_type());
}
TEST(NativeColumnCast, datum_zero_values_and_fixed_string_padding) {
  ObArenaAllocator allocator;
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, get_datum_zero_value(ObInt32Type, CS_TYPE_BINARY, allocator, value));
  EXPECT_EQ(ObInt32Type, value.get_type());
  EXPECT_EQ(0, value.datum_.get_int());
  ASSERT_EQ(OB_SUCCESS, get_datum_zero_value(ObFloatType, CS_TYPE_BINARY, allocator, value));
  EXPECT_EQ(ObFloatType, value.get_type());
  EXPECT_FLOAT_EQ(0.0f, value.datum_.get_float());
  ASSERT_EQ(OB_SUCCESS, get_datum_zero_value(ObDateType, CS_TYPE_BINARY, allocator, value));
  EXPECT_EQ(ObTimeConverter::ZERO_DATE, value.datum_.get_date());
  ASSERT_EQ(OB_SUCCESS, get_datum_zero_value(ObDecimalIntType, CS_TYPE_BINARY, allocator, value));
  EXPECT_FALSE(value.is_null());
  EXPECT_TRUE(value.is_decimal_int());
  EXPECT_EQ(0, value.get_int_bytes());
  ASSERT_EQ(OB_SUCCESS, get_datum_zero_value(ObLongTextType, CS_TYPE_BINARY, allocator, value));
  EXPECT_TRUE(value.is_lob_storage());
  EXPECT_TRUE(value.has_lob_header());
  EXPECT_EQ(sizeof(ObLobCommon), value.get_string_len());

  ObDatum datum;
  datum.set_string("a  ", 3);
  SqlTypeMeta char_meta;
  char_meta.set_char();
  char_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  DatumValue fixed_char(datum, char_meta);
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::padding_fixed_string_value(8, allocator, fixed_char));
  EXPECT_EQ(1, fixed_char.get_string_len());
  EXPECT_EQ('a', fixed_char.get_string_ptr()[0]);

  datum.set_string("a", 1);
  char_meta.set_collation_type(CS_TYPE_BINARY);
  DatumValue fixed_binary(datum, char_meta);
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::padding_fixed_string_value(3, allocator, fixed_binary));
  EXPECT_EQ(3, fixed_binary.get_string_len());
  EXPECT_EQ('a', fixed_binary.get_string_ptr()[0]);
  EXPECT_EQ(0, fixed_binary.get_string_ptr()[1]);
  EXPECT_EQ(0, fixed_binary.get_string_ptr()[2]);
}
TEST(NativeColumnCast, unicode_character_limit_and_strict_truncation) {
  NativeCastContext state;
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("客户甲"), state.arena));
  input.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
  input.set_length(3); input.set_length_semantics(LS_CHAR);
  ObExprResType type = target(ObVarcharType, 3, CS_TYPE_UTF8MB4_BIN);
  type.set_length_semantics(LS_CHAR);
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, type, CM_STRICT_MODE, state.arena, output));
  EXPECT_EQ(input.get_string(), output.get_string());
  type.set_length(2);
  EXPECT_EQ(OB_ERR_DATA_TOO_LONG, ObExprColumnConv::convert_with_null_check(state.context,
      input, type, CM_STRICT_MODE, state.arena, output));
  EXPECT_EQ(input.get_string(), output.get_string());
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, type, CM_WARN_ON_FAIL, state.arena, output));
  EXPECT_EQ(ObString::make_string("客户"), output.get_string());
}
TEST(NativeColumnCast, decimal_scale_precision_and_native_metadata) {
  NativeCastContext state;
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("123.456"), state.arena));
  input.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObExprResType type = target(ObDecimalIntType);
  type.set_precision(8); type.set_scale(2);
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, type, CM_STRICT_MODE, state.arena, output));
  EXPECT_EQ(2, output.get_scale()); EXPECT_EQ(2, output.get_meta().get_scale());
  ObAccuracy declaration = output.get_accuracy(); declaration.set_scale(5);
  output.set_accuracy(declaration); // value still has scale 2
  DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      output, target(ObVarcharType, 32, CS_TYPE_UTF8MB4_BIN), CM_STRICT_MODE, state.arena, text));
  EXPECT_EQ(ObString::make_string("123.46"), text.get_string());
}
TEST(NativeColumnCast, enum_string_uses_explicit_labels) {
  NativeCastContext state;
  ObSEArray<ObString, 2> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("alice")));
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("bob")));
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("bob"), state.arena));
  input.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, target(ObEnumType, 5, CS_TYPE_UTF8MB4_BIN), CM_STRICT_MODE, state.arena, output, &labels));
  EXPECT_EQ(ObEnumType, output.get_type()); EXPECT_EQ(2, output.get_enum());
}
TEST(NativeColumnCast, failed_output_clone_and_malformed_scalar_are_transactional) {
  NativeCastContext state;
  FailingCastAllocator owner;
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_int(123, state.arena));
  ASSERT_EQ(OB_SUCCESS, output.set_int(42, owner));
  const char *old = output.datum_.ptr_;
  owner.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObExprColumnConv::convert_with_null_check(state.context,
      input, target(ObVarcharType, 32, CS_TYPE_UTF8MB4_BIN), CM_STRICT_MODE, owner, output));
  EXPECT_EQ(42, output.get_int()); EXPECT_EQ(old, output.datum_.ptr_);
  input.datum_.len_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObExprColumnConv::convert_with_null_check(state.context,
      input, target(ObIntType), CM_STRICT_MODE, owner, output));
  EXPECT_EQ(42, output.get_int()); EXPECT_EQ(old, output.datum_.ptr_);
}
TEST(NativeColumnCast, caster_allocation_failure_retry_and_destroy_own_single_frame_block) {
  FailingCastAllocator allocator;
  ObExecContext context(allocator);
  ObDatumCaster caster;
  const int64_t before = allocator.allocations;
  allocator.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, caster.init(context, false));
  EXPECT_EQ(OB_SUCCESS, caster.destroy());
  allocator.reject = false;
  ASSERT_EQ(OB_SUCCESS, caster.init(context, false));
  EXPECT_EQ(before + 1, allocator.allocations);
  ASSERT_EQ(OB_SUCCESS, caster.init(context, false));
  EXPECT_EQ(before + 1, allocator.allocations);
  const int64_t frees = allocator.frees;
  EXPECT_EQ(OB_SUCCESS, caster.destroy()); EXPECT_EQ(frees + 1, allocator.frees);
  EXPECT_EQ(OB_SUCCESS, caster.destroy()); EXPECT_EQ(frees + 1, allocator.frees);
}
TEST(NativeColumnCast, expression_caster_keeps_original_parent_frames) {
  NativeCastContext state;
  char *frames[1] = {static_cast<char *>(state.arena.alloc(512))};
  memset(frames[0], 0, 512);
  state.context.set_frames(frames); state.context.set_frame_cnt(1);
  ObExpr expression; expression.frame_idx_ = 0; expression.datum_off_ = 0;
  expression.eval_info_off_ = 32; expression.res_buf_off_ = 128; expression.res_buf_len_ = 128;
  expression.obj_meta_.set_int(); expression.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
  expression.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 19);
  ObEvalCtx input_context(state.context);
  new (frames[0]) ObDatum(); new (frames[0] + 32) ObEvalInfo();
  expression.locate_datum_for_write(input_context).set_int(42);
  expression.get_eval_info(input_context).evaluated_ = true;
  ObDatumCaster caster; ASSERT_EQ(OB_SUCCESS, caster.init(state.context));
  ObDatum *output = nullptr;
  ASSERT_EQ(OB_SUCCESS, caster.to_type(ObDatumMeta(ObVarcharType, CS_TYPE_UTF8MB4_BIN, -1),
                                      expression, CM_NONE, output));
  EXPECT_EQ(ObString::make_string("42"), output->get_string());
  ASSERT_EQ(OB_SUCCESS, caster.destroy());
  state.context.set_frames(nullptr); state.context.set_frame_cnt(0);
}
TEST(NativeColumnCast, native_pl_integer_wrap_snapshot_and_null) {
  using namespace oceanbase::pl;
  NativeCastContext state;
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_int(2147483648LL, state.arena));
  ObPLIntegerRange range; range.set_range(INT32_MIN, INT32_MAX);
  ASSERT_EQ(OB_SUCCESS, ObExprPLIntegerChecker::calc(output, input, PL_SIMPLE_INTEGER, range, state.arena));
  EXPECT_EQ(INT32_MIN, output.get_int()); EXPECT_EQ(2147483648LL, input.get_int());
  EXPECT_NE(output.datum_.ptr_, input.datum_.ptr_);
  input.set_null();
  ASSERT_EQ(OB_SUCCESS, ObExprPLIntegerChecker::calc(output, input, PL_SIMPLE_INTEGER, range, state.arena));
  EXPECT_TRUE(output.is_null());
}

TEST(NativeColumnCast, datetime_text_roundtrip_keeps_fractional_precision) {
  NativeCastContext state;
  ASSERT_EQ(OB_SUCCESS, state.context.create_physical_plan_ctx());
  DatumParameter input, text, restored;
  ASSERT_EQ(OB_SUCCESS, input.set_datetime(1234567123456LL, state.arena));
  input.set_scale(6);
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      input, target(ObVarcharType, 64, CS_TYPE_UTF8MB4_BIN), CM_STRICT_MODE, state.arena, text));
  ObExprResType date_type = target(ObDateTimeType); date_type.set_scale(6);
  ASSERT_EQ(OB_SUCCESS, ObExprColumnConv::convert_with_null_check(state.context,
      text, date_type, CM_STRICT_MODE, state.arena, restored));
  EXPECT_EQ(input.get_datetime(), restored.get_datetime()); EXPECT_EQ(6, restored.get_scale());
}
TEST(NativeColumnCast, spi_facade_native_result_is_owned_and_preserves_previous_on_failure) {
  NativeCastContext state;
  DatumParameter input, result;
  ASSERT_EQ(OB_SUCCESS, input.set_int(123, state.arena));
  ObArenaAllocator output_owner;
  ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_convert(&state.session, &output_owner, input,
      target(ObVarcharType, 32, CS_TYPE_UTF8MB4_BIN), result, nullptr, nullptr));
  EXPECT_EQ(ObString::make_string("123"), result.get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, result.get_collation_type());
  const char *old = result.datum_.ptr_;
  EXPECT_NE(OB_SUCCESS, ObSPIService::spi_convert(nullptr, &output_owner, input,
      target(ObIntType), result, nullptr, nullptr));
  EXPECT_EQ(old, result.datum_.ptr_); EXPECT_EQ(ObString::make_string("123"), result.get_string());
}

TEST(NativeStatsValue, string_prefix_borrows_without_splitting_utf8_and_keeps_metadata) {
  ObArenaAllocator owner, scratch;
  std::string text;
  for (int i=0;i<OPT_STATS_MAX_VALUE_CHAR_LEN+12;++i) { text+="\xe4\xb8\xad"; }
  DatumValue source;
  ASSERT_EQ(OB_SUCCESS,source.set_varchar(ObString(text.size(),text.data()),owner));
  source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  source.set_collation_level(CS_LEVEL_COERCIBLE);
  DatumValue *prefix=nullptr;
  ASSERT_EQ(OB_SUCCESS,ObDbmsStatsUtils::truncate_string_for_opt_stats(&source,scratch,prefix,nullptr));
  ASSERT_NE(nullptr,prefix); EXPECT_NE(&source,prefix);
  EXPECT_EQ(OPT_STATS_MAX_VALUE_CHAR_LEN*3,prefix->get_string_len());
  EXPECT_EQ(source.get_string_ptr(),prefix->get_string_ptr());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN,prefix->get_collation_type());
  EXPECT_EQ(CS_LEVEL_COERCIBLE,prefix->get_collation_level());
  EXPECT_EQ((OPT_STATS_MAX_VALUE_CHAR_LEN+12)*3,source.get_string_len());
  ASSERT_EQ(OB_SUCCESS,ObDbmsStatsUtils::truncate_string_for_opt_stats(source,scratch,nullptr));
  EXPECT_EQ(prefix->get_string(),source.get_string());
}
TEST(NativeStatsValue, null_reuse_and_failed_prefix_allocation_do_not_publish_partial_output) {
  ObArenaAllocator owner;
  FailingCastAllocator scratch;
  DatumValue null_value;
  null_value.meta_.set_varchar(); // typed runtime NULL must not inspect payload
  DatumValue *result=nullptr;
  ASSERT_EQ(OB_SUCCESS,ObDbmsStatsUtils::truncate_string_for_opt_stats(&null_value,scratch,result,nullptr));
  EXPECT_EQ(&null_value,result); EXPECT_TRUE(result->is_null());
  DatumValue source;
  std::string text(OPT_STATS_MAX_VALUE_CHAR_LEN+1,'x');
  ASSERT_EQ(OB_SUCCESS,source.set_varchar(ObString(text.size(),text.data()),owner));
  source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  const char *bytes=source.get_string_ptr(); scratch.reject=true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ObDbmsStatsUtils::truncate_string_for_opt_stats(&source,scratch,result,nullptr));
  EXPECT_EQ(&null_value,result); EXPECT_EQ(bytes,source.get_string_ptr());
  EXPECT_EQ(text.size(),source.get_string_len());
}
TEST(NativeStatsValue, native_temporary_lob_owns_bytes_and_preserves_output_on_failure) {
  FailingCastAllocator owner;
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS,output.set_int(7,owner));
  {
    std::string input="native LOB";
    ASSERT_EQ(OB_SUCCESS,ObTextStringHelper::str_to_lob_storage_obj(owner,ObString(input.size(),input.data()),output));
  }
  EXPECT_EQ(ObLongTextType,output.get_type()); EXPECT_TRUE(output.has_lob_header());
  EXPECT_EQ(CS_TYPE_BINARY,output.get_collation_type());
  EXPECT_EQ(CS_LEVEL_IMPLICIT,output.get_collation_level());
  ObLobLocatorV2 locator(output.get_string(),output.has_lob_header());
  ObString data; ASSERT_EQ(OB_SUCCESS,locator.get_inrow_data(data));
  EXPECT_EQ(ObString::make_string("native LOB"),data);
  const char *bytes=output.datum_.ptr_; owner.reject=true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ObTextStringHelper::str_to_lob_storage_obj(owner,ObString::make_string("replace"),output));
  EXPECT_EQ(bytes,output.datum_.ptr_); EXPECT_EQ(ObLongTextType,output.get_type());
}

TEST(NativeColumnItem, default_payload_uses_plan_owner_and_failed_replace_is_atomic) {
  FailingCastAllocator owner;
  ObColumnRefRawExpr expr;
  expr.set_allocator(owner);
  ColumnItem item; item.expr_ = &expr;
  {
    ObArenaAllocator input_owner;
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString::make_string("customer_客户"), input_owner));
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, item.set_default_value(value));
    EXPECT_NE(value.datum_.ptr_, item.default_value_.datum_.ptr_);
  }
  EXPECT_EQ(ObString::make_string("customer_客户"), item.default_value_.get_string());
  const char *before = item.default_value_.datum_.ptr_;
  ObArenaAllocator input_owner;
  DatumValue replacement;
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(918, input_owner));
  owner.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, item.set_default_value(replacement));
  EXPECT_EQ(before, item.default_value_.datum_.ptr_);
  EXPECT_EQ(ObString::make_string("customer_客户"), item.default_value_.get_string());
}
TEST(NativeColumnItem, nonnull_defaults_require_a_plan_arena_and_null_does_not) {
  ColumnItem item;
  DatumValue value;
  ObArenaAllocator owner;
  ASSERT_EQ(OB_SUCCESS, value.set_int(42, owner));
  EXPECT_EQ(OB_ERR_UNEXPECTED, item.set_default_value(value));
  EXPECT_TRUE(item.default_value_.is_null());
  EXPECT_EQ(OB_SUCCESS, item.set_default_value(DatumValue()));
}

TEST(NativeColumnItem, typed_null_default_does_not_become_an_enum_payload) {
  ObArenaAllocator owner;
  ObColumnRefRawExpr expr;
  expr.set_allocator(owner);
  expr.set_data_type(ObEnumType);
  expr.set_subschema_id(17);
  ColumnItem item; item.expr_ = &expr;
  DatumValue value;
  value.meta_.set_enum();
  ASSERT_TRUE(value.is_null());
  ASSERT_EQ(OB_SUCCESS, item.set_default_value(value));
  EXPECT_TRUE(item.default_value_.is_null());
  EXPECT_EQ(ObNullType, item.default_value_.get_type());
  EXPECT_EQ(ObEnumType, value.get_type());
  EXPECT_EQ(ObEnumType, expr.get_data_type());
}

TEST(NativeDefaultChecks, pure_bit_cast_does_not_become_decimal_text_during_accuracy_check) {
  NativeCastContext state;
  ObDatumCaster *caster = nullptr;
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(caster));
  ObArenaAllocator owner;
  DatumParameter output;
  {
    ObArenaAllocator input_owner;
    DatumParameter input, cast_value;
    ASSERT_EQ(OB_SUCCESS, input.set_uint64(1, input_owner));
    input.set_type(ObBitType); input.set_scale(1); input.set_precision(1);
    ObExprResType text = target(ObVarcharType, 4, CS_TYPE_BINARY);
    ASSERT_EQ(OB_SUCCESS, caster->to_value(input, text, CM_NONE, owner, cast_value));
    ASSERT_EQ(1, cast_value.get_string().length());
    EXPECT_EQ(1, static_cast<unsigned char>(cast_value.get_string().ptr()[0]));
    ASSERT_EQ(OB_SUCCESS, caster->check_accuracy_and_collation(cast_value,
        text.get_accuracy(), CS_TYPE_BINARY, CM_NONE, true, owner, output));
    EXPECT_EQ(1, static_cast<unsigned char>(output.get_string().ptr()[0]));
    EXPECT_EQ(1, input.get_bit());
    // The row-column conversion rules intentionally produce different bytes.
    DatumParameter column_output;
    ASSERT_EQ(OB_SUCCESS, caster->to_column(input, text, CM_NONE, owner, column_output));
    EXPECT_EQ(ObString::make_string("1"), column_output.get_string());
  }
  ASSERT_EQ(1, output.get_string().length());
  EXPECT_EQ(1, static_cast<unsigned char>(output.get_string().ptr()[0]));
}

TEST(NativeDefaultChecks, numeric_json_cast_retains_non_column_conversion_mode) {
  NativeCastContext state;
  ObDatumCaster *caster = nullptr;
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(caster));
  DatumParameter input, cast_value, checked, text;
  ASSERT_EQ(OB_SUCCESS, input.set_int(1, state.arena));
  ObExprResType json = target(ObJsonType, -1, CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, caster->to_value(input, json, CM_NONE, state.arena, cast_value));
  ASSERT_EQ(OB_SUCCESS, caster->check_accuracy_and_collation(cast_value,
      json.get_accuracy(), CS_TYPE_BINARY, CM_NONE, true, state.arena, checked));
  EXPECT_EQ(ObJsonType, checked.get_type());
  ASSERT_EQ(OB_SUCCESS, caster->to_value(checked,
      target(ObVarcharType, 16, CS_TYPE_UTF8MB4_BIN), CM_NONE, state.arena, text));
  EXPECT_EQ(ObString::make_string("1"), text.get_string());
  DatumParameter column_output;
  EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT,
      caster->to_column(input, json, CM_NONE, state.arena, column_output));
  EXPECT_EQ(1, input.get_int());
}

TEST(NativeDefaultChecks, strict_character_error_precedes_length_and_preserves_source_and_output) {
  NativeCastContext state;
  ObDatumCaster *caster = nullptr;
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(caster));
  DatumParameter input, output;
  const char invalid[] = {'a', static_cast<char>(0xff), 'b'};
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString(sizeof(invalid), invalid), state.arena));
  input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, output.set_int(42, state.arena));
  const char *source_bytes = input.datum_.ptr_;
  const char *old_output = output.datum_.ptr_;
  ObExprResType text = target(ObVarcharType, 1, CS_TYPE_UTF8MB4_BIN);
  EXPECT_EQ(OB_ERR_INCORRECT_STRING_VALUE,
      caster->check_accuracy_and_collation(input, text.get_accuracy(),
          CS_TYPE_UTF8MB4_BIN, CM_NONE, true, state.arena, output));
  EXPECT_EQ(source_bytes, input.datum_.ptr_);
  EXPECT_EQ(0, MEMCMP(invalid, input.datum_.ptr_, sizeof(invalid)));
  EXPECT_EQ(old_output, output.datum_.ptr_); EXPECT_EQ(42, output.get_int());
  ASSERT_EQ(OB_SUCCESS, caster->check_accuracy_and_collation(input,
      text.get_accuracy(), CS_TYPE_UTF8MB4_BIN, CM_NONE, false, state.arena, output));
  EXPECT_EQ(ObString::make_string("a"), output.get_string());
  EXPECT_EQ(0, MEMCMP(invalid, input.datum_.ptr_, sizeof(invalid)));
}

TEST(NativeDefaultChecks, decimal_accuracy_owns_output_and_never_writes_source_coefficient) {
  NativeCastContext state;
  ObDatumCaster *caster = nullptr;
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(caster));
  DatumParameter input, decimal, checked, text;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("123.456"), state.arena));
  input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObExprResType type = target(ObDecimalIntType);
  type.set_precision(8); type.set_scale(2);
  ASSERT_EQ(OB_SUCCESS, caster->to_value(input, type, CM_NONE, state.arena, decimal));
  const std::string coefficient(decimal.datum_.ptr_, decimal.datum_.len_);
  const char *source_bytes = decimal.datum_.ptr_;
  ObArenaAllocator owner;
  ASSERT_EQ(OB_SUCCESS, caster->check_accuracy_and_collation(decimal,
      type.get_accuracy(), CS_TYPE_BINARY, CM_NONE, true, owner, checked));
  EXPECT_NE(source_bytes, checked.datum_.ptr_);
  EXPECT_EQ(source_bytes, decimal.datum_.ptr_);
  EXPECT_EQ(coefficient, std::string(decimal.datum_.ptr_, decimal.datum_.len_));
  EXPECT_EQ(2, checked.get_scale()); EXPECT_EQ(2, checked.get_meta().get_scale());
  ASSERT_EQ(OB_SUCCESS, caster->to_value(checked,
      target(ObVarcharType, 32, CS_TYPE_UTF8MB4_BIN), CM_NONE, state.arena, text));
  EXPECT_EQ(ObString::make_string("123.46"), text.get_string());
  const char *previous = checked.datum_.ptr_;
  type.set_precision(2);
  EXPECT_NE(OB_SUCCESS, caster->check_accuracy_and_collation(decimal,
      type.get_accuracy(), CS_TYPE_BINARY, CM_NONE, true, owner, checked));
  EXPECT_EQ(previous, checked.datum_.ptr_);
  EXPECT_EQ(coefficient, std::string(decimal.datum_.ptr_, decimal.datum_.len_));
}

TEST(NativeDefaultChecks, float_precision_and_failed_owner_clone_leave_source_and_output_unchanged) {
  NativeCastContext state;
  ObDatumCaster *caster = nullptr;
  ASSERT_EQ(OB_SUCCESS, state.context.get_native_datum_caster(caster));
  DatumParameter input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_double(12.3456, state.arena));
  ObExprResType type = target(ObDoubleType); type.set_precision(5); type.set_scale(2);
  ASSERT_EQ(OB_SUCCESS, caster->check_accuracy_and_collation(input,
      type.get_accuracy(), CS_TYPE_BINARY, CM_NONE, true, state.arena, output));
  EXPECT_DOUBLE_EQ(12.35, output.get_double()); EXPECT_DOUBLE_EQ(12.3456, input.get_double());
  FailingCastAllocator reject;
  reject.reject = true;
  const char *previous = output.datum_.ptr_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, caster->check_accuracy_and_collation(input,
      type.get_accuracy(), CS_TYPE_BINARY, CM_NONE, true, reject, output));
  EXPECT_EQ(previous, output.datum_.ptr_); EXPECT_DOUBLE_EQ(12.35, output.get_double());
  EXPECT_DOUBLE_EQ(12.3456, input.get_double());
}

int main(int argc, char **argv) {
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  ObArenaAllocator constants;
  if (ObNumberConstValue::init(constants) != OB_SUCCESS
      || wide::ObDecimalIntConstValue::init_const_values(constants) != OB_SUCCESS) { return 3; }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


TEST(NativeColumnCast, direct_native_pl_range_symbol_and_integer_widths) {
  using oceanbase::pl::ObPLIntegerRange;
  NativeCastContext state;
  ObPLIntegerRange signed_range; signed_range.set_range(-2, 2);
  const SqlValueType signed_types[] = {ObTinyIntType, ObSmallIntType,
      ObMediumIntType, ObInt32Type, ObIntType};
  const int64_t signed_values[] = {-3, -2, 0, 2, 3};
  for (SqlValueType type : signed_types) {
    for (int64_t input : signed_values) {
      SCOPED_TRACE(type);
      SCOPED_TRACE(input);
      DatumValue value;
      ASSERT_EQ(OB_SUCCESS, value.set_int(input, state.arena));
      value.meta_.set_type(type);
      const char *bytes = value.datum_.ptr_;
      const uint32_t pack = value.datum_.pack_;
      const int expected = input < -2 || input > 2 ? OB_NUMERIC_OVERFLOW : OB_SUCCESS;
      EXPECT_EQ(expected, ObExprPLIntegerChecker::check_range<DatumValue>(
          value, type, signed_range.range_));
      EXPECT_EQ(bytes, value.datum_.ptr_); EXPECT_EQ(pack, value.datum_.pack_);
      EXPECT_EQ(input, value.get_int());
    }
  }
  ObPLIntegerRange unsigned_range; unsigned_range.set_range(0, 2);
  const SqlValueType unsigned_types[] = {ObUTinyIntType, ObUSmallIntType,
      ObUMediumIntType, ObUInt32Type, ObUInt64Type};
  const uint64_t unsigned_values[] = {0, 1, 2, 3};
  for (SqlValueType type : unsigned_types) {
    for (uint64_t input : unsigned_values) {
      SCOPED_TRACE(type);
      SCOPED_TRACE(input);
      DatumValue value;
      ASSERT_EQ(OB_SUCCESS, value.set_uint64(input, state.arena));
      value.meta_.set_type(type);
      // Preserve the existing unsigned-integer algorithm's lower bound of 1
      // when the declared lower bound is nonpositive; this link fix does not
      // redefine PL range rules.
      const int expected = input == 0 || input > 2 ? OB_NUMERIC_OVERFLOW : OB_SUCCESS;
      EXPECT_EQ(expected, ObExprPLIntegerChecker::check_range<DatumValue>(
          value, type, unsigned_range.range_));
      EXPECT_EQ(input, value.get_uint64());
    }
  }
}

TEST(NativeColumnCast, direct_native_pl_range_float_and_compact_number_bounds) {
  using oceanbase::pl::ObPLIntegerRange;
  NativeCastContext state;
  ObPLIntegerRange range; range.set_range(-2, 2);
  const int64_t inputs[] = {-3, -2, 0, 2, 3};
  for (SqlValueType type : {ObFloatType, ObDoubleType, ObNumberType}) {
    for (int64_t input : inputs) {
      SCOPED_TRACE(type);
      SCOPED_TRACE(input);
      DatumValue value;
      if (type == ObFloatType) {
        ASSERT_EQ(OB_SUCCESS, value.set_float(static_cast<float>(input), state.arena));
      } else if (type == ObDoubleType) {
        ASSERT_EQ(OB_SUCCESS, value.set_double(static_cast<double>(input), state.arena));
      } else {
        number::ObNumber number;
        ASSERT_EQ(OB_SUCCESS, number.from(input, state.arena));
        ASSERT_EQ(OB_SUCCESS, value.set_number(number, state.arena));
      }
      const char *bytes = value.datum_.ptr_;
      const int expected = input < -2 || input > 2 ? OB_NUMERIC_OVERFLOW : OB_SUCCESS;
      EXPECT_EQ(expected, ObExprPLIntegerChecker::check_range<DatumValue>(
          value, type, range.range_));
      EXPECT_EQ(bytes, value.datum_.ptr_);
    }
  }
  ObPLIntegerRange unsigned_range; unsigned_range.set_range(0, 2);
  for (SqlValueType type : {ObUFloatType, ObUDoubleType, ObUNumberType}) {
    for (int64_t input : {0, 1, 2, 3}) {
      SCOPED_TRACE(type);
      SCOPED_TRACE(input);
      DatumValue value;
      if (type == ObUFloatType) {
        ASSERT_EQ(OB_SUCCESS, value.set_float(static_cast<float>(input), state.arena));
      } else if (type == ObUDoubleType) {
        ASSERT_EQ(OB_SUCCESS, value.set_double(static_cast<double>(input), state.arena));
      } else {
        number::ObNumber number;
        ASSERT_EQ(OB_SUCCESS, number.from(input, state.arena));
        ASSERT_EQ(OB_SUCCESS, value.set_number(number, state.arena));
      }
      value.meta_.set_type(type);
      const bool below = input == 0 && type != ObUNumberType;
      const int expected = below || input > 2 ? OB_NUMERIC_OVERFLOW : OB_SUCCESS;
      EXPECT_EQ(expected, ObExprPLIntegerChecker::check_range<DatumValue>(
          value, type, unsigned_range.range_));
    }
  }
}

TEST(NativeColumnCast, direct_native_pl_range_null_and_invalid_range_do_not_read_payload) {
  using oceanbase::pl::ObPLIntegerRange;
  NativeCastContext state;
  ObPLIntegerRange range; range.set_range(1, 2);
  for (SqlValueType type : {ObNullType, ObIntType, ObUInt64Type, ObFloatType,
                         ObDoubleType, ObNumberType, ObUNumberType}) {
    DatumValue value;
    value.meta_.set_type(type);
    ASSERT_TRUE(value.is_null()); ASSERT_EQ(nullptr, value.datum_.ptr_);
    EXPECT_EQ(OB_SUCCESS, ObExprPLIntegerChecker::check_range<DatumValue>(
        value, type, range.range_));
    EXPECT_TRUE(value.is_null()); EXPECT_EQ(nullptr, value.datum_.ptr_);
  }
  DatumValue outside;
  ASSERT_EQ(OB_SUCCESS, outside.set_int(999, state.arena));
  ObPLIntegerRange invalid;
  EXPECT_FALSE(invalid.valid());
  EXPECT_EQ(OB_SUCCESS, ObExprPLIntegerChecker::check_range<DatumValue>(
      outside, outside.get_type(), invalid.range_));
}
