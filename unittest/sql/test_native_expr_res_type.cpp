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
#include "share/datum/datum_cast_context.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include "sql/resolver/expr/ob_raw_expr_deduce_type.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/session/ob_sql_session_info.h"
#include <gtest/gtest.h>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

namespace oceanbase { namespace sql {
// Only expose the actual feeder and its existing scratch-accounting bookend.
// No test replacement for the feeder, type kernel, or allocator.
class NativeExprResTypeTestPeer
{
public:
  static int feed(ObRawExprDeduceType &deducer, const ObRawExpr &raw, ObIExprResTypes &types)
  { return deducer.push_back_types(&raw, types); }
  static int64_t used(const ObRawExprDeduceType &deducer) { return deducer.alloc_.used(); }
};
} }

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
void same_meta(const SqlTypeMeta &expected, const SqlTypeMeta &actual)
{
  EXPECT_EQ(expected.get_type(), actual.get_type());
  EXPECT_EQ(expected.get_collation_type(), actual.get_collation_type());
  EXPECT_EQ(expected.get_collation_level(), actual.get_collation_level());
  EXPECT_EQ(expected.get_scale(), actual.get_scale());
}

void empty_parameter(const ObExprResType &type)
{
  EXPECT_TRUE(type.get_param_datum().is_null());
  EXPECT_EQ(ObNullType, type.get_param_value_meta().get_type());
  EXPECT_EQ(CS_TYPE_INVALID, type.get_param_value_meta().get_collation_type());
  EXPECT_EQ(CS_LEVEL_INVALID, type.get_param_value_meta().get_collation_level());
  EXPECT_EQ(-1, type.get_param_value_meta().get_scale());
}

class NativeExprResType : public ::testing::Test
{
protected:
  NativeExprResType()
    : factory_(owner_), execution_(owner_), demotion_(&session_, &factory_),
      deducer_(&session_, &factory_, false, nullptr, OB_INVALID_INDEX, demotion_)
  {}
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
    ObExprOperatorFactory::register_expr_operators();
  }
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session_.load_default_sys_variable(false, false));
    execution_.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, execution_.create_physical_plan_ctx());
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &execution_);
  }
  void TearDown() override
  { ObSQLSessionInfo::ExecCtxSessionRegister unregister(session_, nullptr); }

  int literal(ObItemType kind, const std::string &text, int64_t integer, ObConstRawExpr *&out)
  {
    int ret = factory_.create_raw_expr(kind, out);
    if (ret == OB_SUCCESS) {
      ParseNode node = {};
      node.type_ = kind; node.str_value_ = text.data(); node.str_len_ = text.size(); node.value_ = integer;
      ObTimeZoneInfo timezone; timezone.set_offset(0);
      ObString prefix;
      ret = ObResolverUtils::resolve_const(&node, stmt::T_SELECT, owner_,
          CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_GENERAL_CI, &timezone,
          *out, false, prefix, LS_CHAR, CS_TYPE_UTF8MB4_GENERAL_CI, nullptr, 0,
          true, oceanbase::share::COMPAT_MYSQL57, false, 8, false, false);
    }
    return ret;
  }
  int question(const ParamStore &parameters, int64_t index, ObConstRawExpr *&out)
  {
    ParseNode node = {}; node.type_ = T_QUESTIONMARK; node.str_value_ = "?"; node.str_len_ = 1; node.value_ = index;
    ObSEArray<ObQualifiedName, 1> columns; ObSEArray<ObVarInfo, 1> vars;
    ObSEArray<ObAggFunRawExpr *, 1> aggregates; ObSEArray<ObWinFunRawExpr *, 1> windows;
    ObSEArray<ObSubQueryInfo, 1> subqueries; ObSEArray<ObUDFInfo, 1> udfs;
    ObSEArray<ObOpRawExpr *, 1> operations;
    ObRawExpr *raw = nullptr;
    const int ret = ObRawExprUtils::build_raw_expr(factory_, session_, nullptr, nullptr,
        T_NONE_SCOPE, nullptr, &parameters, nullptr, node, raw, columns, vars,
        aggregates, windows, subqueries, udfs, operations, false);
    if (ret == OB_SUCCESS) { out = static_cast<ObConstRawExpr *>(raw); }
    return ret;
  }
  int function(ObItemType kind, const char *name, std::initializer_list<ObRawExpr *> values,
               ObSysFunRawExpr *&out)
  {
    int ret = factory_.create_raw_expr(kind, out);
    ObSEArray<ObRawExpr *, 3> arguments;
    for (ObRawExpr *value : values) {
      if (ret == OB_SUCCESS) { ret = arguments.push_back(value); }
    }
    if (ret == OB_SUCCESS) {
      out->set_func_name(ObString::make_string(name));
      ret = out->set_param_exprs(arguments);
    }
    return ret;
  }
  void evaluate(ObRawExpr &raw, const std::function<void(const ObExpr &, const Datum &)> &check)
  {
    ObArenaAllocator plan;
    ObRawExprUniqueSet roots(false);
    ASSERT_EQ(OB_SUCCESS, roots.append(&raw));
    ObExprFrameInfo info(plan);
    ObStaticEngineExprCG generator(plan, &session_, nullptr, 0, 0);
    ASSERT_EQ(OB_SUCCESS, generator.generate(roots, info));
    ObExpr *runtime = nullptr;
    for (int64_t i = 0; i < info.rt_exprs_.count(); ++i) {
      if (info.rt_exprs_.at(i).type_ == raw.get_expr_type()) {
        ASSERT_EQ(nullptr, runtime); runtime = &info.rt_exprs_.at(i);
      }
    }
    ASSERT_NE(nullptr, runtime);
    ObExecContext execution(plan);
    execution.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
    ASSERT_EQ(OB_SUCCESS, info.pre_alloc_exec_memory(execution));
    ASSERT_EQ(OB_SUCCESS, execution.init_expr_op(info.need_ctx_cnt_));
    ObEvalCtx context(execution);
    Datum *value = nullptr;
    ASSERT_EQ(OB_SUCCESS, runtime->eval(context, value));
    ASSERT_NE(nullptr, value);
    check(*runtime, *value); // Consume references while their real eval owner is live.
  }

  ObArenaAllocator owner_;
  ObRawExprFactory factory_;
  ObSQLSessionInfo session_;
  ObExecContext execution_;
  ObRawExprTypeDemotion demotion_;
  ObRawExprDeduceType deducer_;
};
} // namespace

TEST_F(NativeExprResType, actual_feeder_inlines_bound_scalars_without_legacy_materialization)
{
  ObConstRawExpr *minimum = nullptr, *maximum = nullptr, *unsigned_maximum = nullptr, *floating = nullptr;
  ASSERT_EQ(OB_SUCCESS, literal(T_INT, "-9223372036854775808", INT64_MIN, minimum));
  ASSERT_EQ(OB_SUCCESS, literal(T_INT, "9223372036854775807", INT64_MAX, maximum));
  ASSERT_EQ(OB_SUCCESS, literal(T_UINT64, "18446744073709551615", -1, unsigned_maximum));
  ASSERT_EQ(OB_SUCCESS, literal(T_DOUBLE, "1.5e0", 0, floating));
  ObRawExpr *index = minimum;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::create_param_expr(factory_, 37, index));
  auto *questionmark = static_cast<ObConstRawExpr *>(index);
  ASSERT_EQ(OB_SUCCESS, questionmark->set_questionmark_index(99));
  ObExprResTypes types;
  ASSERT_EQ(OB_SUCCESS, types.reserve(5));
  const int64_t before = NativeExprResTypeTestPeer::used(deducer_);
  for (ObConstRawExpr *raw : {minimum, maximum, unsigned_maximum, floating, questionmark}) {
    ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *raw, types));
  }
  EXPECT_EQ(before, NativeExprResTypeTestPeer::used(deducer_));
  ASSERT_EQ(5, types.count());
  for (int64_t i = 0; i < types.count(); ++i) { EXPECT_EQ(0U, types.at(i).get_param_datum().length()); }
  EXPECT_EQ(INT64_MIN, types.at(0).get_param_datum().get_int());
  EXPECT_EQ(INT64_MAX, types.at(1).get_param_datum().get_int());
  EXPECT_EQ(UINT64_MAX, types.at(2).get_param_datum().get_uint());
  EXPECT_DOUBLE_EQ(1.5, types.at(3).get_param_datum().get_double());
  EXPECT_TRUE(types.at(4).get_param_value_meta().is_unknown());
  EXPECT_EQ(37, types.at(4).get_param_datum().get_int());
  EXPECT_EQ(99, questionmark->get_questionmark_index());
  EXPECT_FALSE(types.at(4).is_literal()); // Its result is I64, its physical param is Unknown.
  Datum changed; changed.set_int(45);
  SqlTypeMeta integer; integer.set_int();
  ASSERT_EQ(OB_SUCCESS, minimum->set_value_borrowed(changed, integer));
  EXPECT_EQ(INT64_MIN, types.at(0).get_param_datum().get_int());
  for (ObConstRawExpr *raw : {maximum, unsigned_maximum, floating, questionmark}) {
    const int64_t i = raw == maximum ? 1 : raw == unsigned_maximum ? 2 : raw == floating ? 3 : 4;
    same_meta(raw->get_bound_param_value_meta(), types.at(i).get_param_value_meta());
  }
}

TEST_F(NativeExprResType, physical_parameter_meta_survives_result_rewrite_and_typed_null)
{
  ParamStore parameters{ObWrapperAllocator(owner_)};
  DatumParameter null; null.set_null();
  SqlTypeMeta declared; declared.set_char(); declared.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  null.set_param_meta(declared); null.set_length(12);
  ASSERT_EQ(OB_SUCCESS, parameters.push_back(null));
  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("12.345", 6, owner_));
  DatumParameter original_number;
  ASSERT_EQ(OB_SUCCESS, original_number.set_number(number, owner_));
  original_number.set_param_meta(); original_number.set_precision(5); original_number.set_scale(3);
  ASSERT_EQ(OB_SUCCESS, parameters.push_back(original_number));
  ObConstRawExpr *null_raw = nullptr, *number_raw = nullptr;
  ASSERT_EQ(OB_SUCCESS, question(parameters, 0, null_raw));
  ASSERT_EQ(OB_SUCCESS, question(parameters, 1, number_raw));
  ObRawExprResType decimal_result;
  decimal_result.set_decimal_int(); decimal_result.set_precision(20); decimal_result.set_scale(5);
  ASSERT_EQ(OB_SUCCESS, number_raw->set_dynamic_eval_questionmark(decimal_result));
  ObExprResTypes types;
  ASSERT_EQ(OB_SUCCESS, types.reserve(8));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *null_raw, types));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *number_raw, types));
  EXPECT_TRUE(types.at(0).get_param_datum().is_null());
  EXPECT_EQ(ObNullType, types.at(0).get_param_value_meta().get_type());
  EXPECT_EQ(ObCharType, types.at(0).get_type()); EXPECT_EQ(12, types.at(0).get_length());
  EXPECT_FALSE(types.at(0).is_literal());
  EXPECT_EQ(ObNumberType, types.at(1).get_param_value_meta().get_type());
  EXPECT_EQ(ObDecimalIntType, types.at(1).get_type());
  EXPECT_EQ(20, types.at(1).get_precision()); EXPECT_EQ(5, types.at(1).get_scale());
  EXPECT_EQ(ObNumberType, number_raw->get_orig_qm_type().get_type());
  EXPECT_EQ(0, number::ObNumber(types.at(1).get_param_datum().get_number()).compare(number));
  EXPECT_FALSE(types.at(1).is_literal());
  struct DecimalInput { const char *text; uint32_t width; };
  const DecimalInput decimals[] = {{"12.34", 4}, {"2147483648.01", 8}, {"12345678901234567890.12345", 16}};
  for (const DecimalInput &input : decimals) {
    ObConstRawExpr *raw = nullptr;
    ASSERT_EQ(OB_SUCCESS, literal(T_NUMBER, input.text, 0, raw));
    const uint32_t width = raw->get_bound_param_datum().length();
    ASSERT_EQ(input.width, width);
    ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *raw, types));
    const ObExprResType &copied = types.at(types.count()-1);
    EXPECT_EQ(width, copied.get_param_datum().length());
    EXPECT_EQ(raw->get_bound_param_datum().get_reference(), copied.get_param_datum().get_reference());
    same_meta(raw->get_bound_param_value_meta(), copied.get_param_value_meta());
  }
  // A real Raw owning-copy supplies the lifetime; ResType itself still borrows.
  char source[] = "bound bytes";
  ObDatum bytes; bytes.set_string(source, sizeof(source)-1);
  SqlTypeMeta text; text.set_varchar(); text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObConstRawExpr owned(owner_);
  DatumValue borrowed_bytes;
  ASSERT_EQ(OB_SUCCESS, borrowed_bytes.assign_legacy_borrowed(bytes, text));
  ASSERT_EQ(OB_SUCCESS, owned.set_value_borrowed(borrowed_bytes));
  ObPLExprCopier copier(factory_);
  ASSERT_EQ(OB_SUCCESS, owned.inner_deep_copy(copier));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, owned, types));
  MEMSET(source, 'x', sizeof(source)-1);
  EXPECT_EQ(ObString("bound bytes"), types.at(types.count()-1).get_param_datum().get_string());

  // This is the historical is_literal predicate, including typed NULL and
  // deliberately different scale/collation level. No semantic FullType is invented.
  Datum typed_null; typed_null.set_null();
  SqlTypeMeta physical; physical.set_char(); physical.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  physical.set_collation_level(CS_LEVEL_EXPLICIT); physical.set_scale(7);
  ObConstRawExpr typed(owner_);
  ASSERT_EQ(OB_SUCCESS, typed.set_value_borrowed(typed_null, physical));
  typed.set_collation_level(CS_LEVEL_IMPLICIT); typed.set_scale(2);
  ObExprResTypes null_types; ASSERT_EQ(OB_SUCCESS, null_types.reserve(1));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, typed, null_types));
  ObExprResType &literal = null_types.at(0);
  EXPECT_TRUE(literal.is_literal()); same_meta(physical, literal.get_param_value_meta());
  literal.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI); EXPECT_FALSE(literal.is_literal());
  literal.set_collation_type(CS_TYPE_UTF8MB4_BIN); literal.set_type(ObVarcharType);
  EXPECT_FALSE(literal.is_literal());
}

TEST_F(NativeExprResType, multi_operand_type_helpers_keep_independent_export_storage)
{
  for (bool left : {false, true}) {
    ObConstRawExpr *text = nullptr, *length = nullptr, *padding = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory_, ObIntType, 12, text));
    ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory_, ObIntType, 5, length));
    ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory_, ObIntType, 34, padding));
    ObSysFunRawExpr *raw = nullptr;
    ASSERT_EQ(OB_SUCCESS, function(left ? T_FUN_SYS_LPAD : T_FUN_SYS_RPAD,
        left ? "lpad" : "rpad", {text, length, padding}, raw));
    ASSERT_EQ(OB_SUCCESS, raw->formalize(&session_));
    const ObRawExprResType first_type = raw->get_result_type();
    ASSERT_EQ(OB_SUCCESS, raw->formalize(&session_));
    EXPECT_TRUE(first_type == raw->get_result_type());
    EXPECT_EQ(5, raw->get_result_type().get_length());
    ASSERT_NO_FATAL_FAILURE(evaluate(*raw, [left](const ObExpr &, const Datum &value) {
      EXPECT_EQ(ObString(left ? "34312" : "12343"), value.get_string());
    }));
  }
  ObConstRawExpr *text = nullptr, *position = nullptr, *length = nullptr;
  ASSERT_EQ(OB_SUCCESS, literal(T_VARCHAR, "abcdef", 0, text));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory_, ObIntType, 2, position));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory_, ObIntType, 3, length));
  ObSysFunRawExpr *substring = nullptr;
  ASSERT_EQ(OB_SUCCESS, function(T_FUN_SYS_SUBSTR, "substr", {text, position, length}, substring));
  ASSERT_EQ(OB_SUCCESS, substring->formalize(&session_));
  EXPECT_EQ(3, substring->get_result_type().get_length());
  ASSERT_NO_FATAL_FAILURE(evaluate(*substring, [](const ObExpr &, const Datum &value) {
    EXPECT_EQ(ObString("bcd"), value.get_string());
  }));
}

TEST_F(NativeExprResType, negative_boundary_type_deduction_keeps_exact_number_result)
{
  struct Input { ObItemType kind; const char *text; int64_t scalar; const char *expected; };
  const Input inputs[] = {{T_INT, "-9223372036854775808", INT64_MIN, "9223372036854775808"},
      {T_UINT64, "18446744073709551615", -1, "-18446744073709551615"}, {T_INT, "7", 7, "-7"}};
  for (const Input &input : inputs) {
    SCOPED_TRACE(input.text);
    ObConstRawExpr *operand = nullptr;
    ASSERT_EQ(OB_SUCCESS, literal(input.kind, input.text, input.scalar, operand));
    ObOpRawExpr *negative = nullptr;
    ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_OP_NEG, negative));
    ASSERT_EQ(OB_SUCCESS, negative->set_param_expr(operand));
    ASSERT_EQ(OB_SUCCESS, negative->formalize(&session_));
    EXPECT_EQ(input.scalar == 7 ? ObIntType : ObNumberType, negative->get_result_type().get_type());
    ASSERT_NO_FATAL_FAILURE(evaluate(*negative, [this, &input](const ObExpr &expr, const Datum &value) {
      if (input.scalar == 7) {
        EXPECT_EQ(ObIntType, expr.datum_meta_.get_type()); EXPECT_EQ(-7, value.get_int());
      } else {
        number::ObNumber expected;
        ASSERT_EQ(OB_SUCCESS, expected.from(input.expected, STRLEN(input.expected), owner_));
        EXPECT_EQ(0, number::ObNumber(value.get_number()).compare(expected));
      }
    }));
    if (input.kind == T_UINT64) { EXPECT_EQ(UINT64_MAX, operand->get_bound_param_datum().get_uint()); }
    else { EXPECT_EQ(input.scalar, operand->get_bound_param_datum().get_int()); }
  }
}

TEST_F(NativeExprResType, parameter_export_failure_does_not_publish_legacy_view)
{
  ObConstRawExpr *raw = nullptr;
  ASSERT_EQ(OB_SUCCESS, literal(T_DOUBLE, "1.5e0", 0, raw));
  ObExprResTypes types; ASSERT_EQ(OB_SUCCESS, types.reserve(1));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *raw, types));
  char sentinel[] = "sentinel";
  ObDatum old; old.set_string(sentinel, sizeof(sentinel)-1);
  SqlTypeMeta text; text.set_varchar(); text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, output.assign_legacy_borrowed(old, text));
  const DatumValue before = output;
  alignas(8) char first_scratch[64], second_scratch[64];
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, types.at(0).export_param(output, first_scratch, 7));
  EXPECT_EQ(0, MEMCMP(&before.datum_, &output.datum_, sizeof(Datum)));
  same_meta(before.meta_, output.meta_);
  ASSERT_EQ(OB_SUCCESS, types.at(0).export_param(output, first_scratch, sizeof(first_scratch)));
  EXPECT_DOUBLE_EQ(1.5, output.get_double());
  same_meta(raw->get_bound_param_value_meta(), output.meta_);
  DatumValue second;
  ASSERT_EQ(OB_SUCCESS, types.at(0).export_param(second, second_scratch, sizeof(second_scratch)));
  EXPECT_NE(&output.datum_, &second.datum_);
  EXPECT_EQ(0u, second.datum_.length());
  MEMSET(first_scratch, 0xa5, sizeof(first_scratch));
  EXPECT_DOUBLE_EQ(1.5, second.get_double());
  EXPECT_DOUBLE_EQ(1.5, types.at(0).get_param_datum().get_double());
  ObConstRawExpr *reference = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_string_expr(factory_, ObVarcharType,
      ObString("reference bytes"), CS_TYPE_UTF8MB4_BIN, reference));
  ObExprResTypes refs; ASSERT_EQ(OB_SUCCESS, refs.reserve(1));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *reference, refs));
  ASSERT_EQ(OB_SUCCESS, refs.at(0).export_param(output, nullptr, 0));
  EXPECT_EQ(reference->get_bound_param_datum().get_reference(), output.datum_.get_reference());
  EXPECT_EQ(ObString("reference bytes"), output.get_string());
}

TEST_F(NativeExprResType, expr_res_type_copy_reset_and_wire_reuse_keep_metadata_contract)
{
  ObConstRawExpr *raw = nullptr;
  ASSERT_EQ(OB_SUCCESS, literal(T_DOUBLE, "1.5e0", 0, raw));
  ObExprResTypes types; ASSERT_EQ(OB_SUCCESS, types.reserve(1));
  ASSERT_EQ(OB_SUCCESS, NativeExprResTypeTestPeer::feed(deducer_, *raw, types));
  ObExprResType original = types.at(0);
  original.set_accuracy(ObAccuracy(18, 4)); original.set_result_flag(NOT_NULL_FLAG);
  original.set_calc_meta(original); original.set_calc_accuracy(ObAccuracy(17, 3));
  original.add_decimal_int_cast_mode(CM_CONST_TO_DECIMAL_INT_DOWN);
  ObExprResType assigned;
  ASSERT_EQ(OB_SUCCESS, assigned.assign(original));
  EXPECT_DOUBLE_EQ(1.5, assigned.get_param_datum().get_double());
  same_meta(original.get_param_value_meta(), assigned.get_param_value_meta());
  ObExprResType copied(original);
  EXPECT_DOUBLE_EQ(1.5, copied.get_param_datum().get_double());
  copied.reset_param();
  ASSERT_NO_FATAL_FAILURE(empty_parameter(copied));
  EXPECT_TRUE(static_cast<const ObRawExprResType &>(original) == static_cast<const ObRawExprResType &>(copied));
  same_meta(original.get_calc_meta(), copied.get_calc_meta());
  EXPECT_EQ(original.get_calc_accuracy(), copied.get_calc_accuracy());
  EXPECT_EQ(original.get_result_flag(), copied.get_result_flag());
  EXPECT_EQ(original.get_cast_mode(), copied.get_cast_mode());
  const ObRawExprResType base = original;
  ObExprResType from_base(base);
  ASSERT_NO_FATAL_FAILURE(empty_parameter(from_base));
  const int64_t size = original.get_serialize_size();
  EXPECT_EQ(size, copied.get_serialize_size());
  std::string populated_wire(size, '\0'), empty_wire(size, '\0');
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, original.serialize(&populated_wire[0], size, pos)); ASSERT_EQ(size, pos);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, copied.serialize(&empty_wire[0], size, pos)); ASSERT_EQ(size, pos);
  EXPECT_EQ(populated_wire, empty_wire); // Compile-time param has never been part of this wire.
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, assigned.deserialize(populated_wire.data(), size, pos));
  ASSERT_EQ(size, pos); ASSERT_NO_FATAL_FAILURE(empty_parameter(assigned));
  EXPECT_TRUE(static_cast<const ObRawExprResType &>(original) == static_cast<const ObRawExprResType &>(assigned));
  same_meta(original.get_calc_meta(), assigned.get_calc_meta());
  EXPECT_EQ(original.get_calc_accuracy(), assigned.get_calc_accuracy());
  EXPECT_EQ(original.get_result_flag(), assigned.get_result_flag());
  for (int64_t length : {int64_t(0), size / 2, size - 1}) {
    ASSERT_EQ(OB_SUCCESS, assigned.assign(original));
    pos = 0;
    // The accepted decoder allows omitted tail fields, including an empty
    // input. Its entry-clear must not reset the receiver's other metadata.
    ASSERT_EQ(OB_SUCCESS, assigned.deserialize(populated_wire.data(), length, pos));
    ASSERT_NO_FATAL_FAILURE(empty_parameter(assigned));
    EXPECT_TRUE(static_cast<const ObRawExprResType &>(original) == static_cast<const ObRawExprResType &>(assigned));
    same_meta(original.get_calc_meta(), assigned.get_calc_meta());
    EXPECT_EQ(original.get_calc_accuracy(), assigned.get_calc_accuracy());
    EXPECT_EQ(original.get_result_flag(), assigned.get_result_flag());
    EXPECT_EQ(original.get_cast_mode(), assigned.get_cast_mode());
  }
  // A continuation byte without its next byte is a real malformed vi64.
  // Keep an accessible guard byte outside data_len: the old decoder peeks at
  // the following byte before checking the logical bound.
  const int64_t meta_size = static_cast<const SqlTypeMeta &>(original).get_serialize_size();
  ASSERT_LT(meta_size, size);
  std::string malformed(populated_wire.data(), meta_size);
  malformed.push_back(static_cast<char>(0x80));
  malformed.push_back(0);
  ASSERT_EQ(OB_SUCCESS, assigned.assign(original));
  pos = 0;
  EXPECT_EQ(OB_DESERIALIZE_ERROR, assigned.deserialize(malformed.data(), meta_size + 1, pos));
  ASSERT_NO_FATAL_FAILURE(empty_parameter(assigned));
  // Other metadata/position retain the old codec's error semantics; no
  // transactional rollback is asserted for a malformed field.
  assigned.reset();
  ASSERT_NO_FATAL_FAILURE(empty_parameter(assigned));
  EXPECT_EQ(ObNullType, assigned.get_type()); EXPECT_EQ(0U, assigned.get_result_flag());
}
