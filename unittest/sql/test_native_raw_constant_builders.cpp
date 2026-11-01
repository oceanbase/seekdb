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
#include "common/datum/datum_parameter_array.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "lib/utility/ob_defer.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace common = oceanbase::common; // DEFER uses the qualified common namespace.
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
class RawAllocationGate final : public ObIAllocator
{
public:
  ObArenaAllocator arena_;
  std::vector<int64_t> sizes_;
  int64_t fail_size_ = -1;
  bool fail_all_ = false;
  void *alloc(int64_t bytes) override
  {
    sizes_.push_back(bytes);
    return fail_all_ || bytes == fail_size_ ? nullptr : arena_.alloc(bytes);
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *) override {}
};

ParseNode literal_node(ObItemType kind, const std::string &text, int64_t integer = 0)
{
  ParseNode node = {};
  node.type_ = kind;
  node.str_value_ = text.data();
  node.str_len_ = text.size();
  node.value_ = integer;
  return node;
}

template <typename Value>
int parse_literal(ParseNode &node, ObIAllocator &owner, Value &out, ObString &prefix)
{
  ObTimeZoneInfo timezone;
  timezone.set_offset(0);
  return ObResolverUtils::resolve_const(&node, stmt::T_SELECT, owner,
      CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_GENERAL_CI, &timezone,
      out, false, prefix, LS_CHAR, CS_TYPE_UTF8MB4_GENERAL_CI, nullptr, 0,
      true, oceanbase::share::COMPAT_MYSQL57, false, 8, false, false);
}

std::string meta_bytes(const SqlTypeMeta &meta)
{
  return std::string(reinterpret_cast<const char *>(&meta), sizeof(meta));
}

int resolve_node(ObRawExprFactory &factory, ObSQLSessionInfo &session,
                 const ParseNode &node, ObRawExpr *&out,
                 const ParamStore *parameters = nullptr, bool preparing = false)
{
  ObSEArray<ObQualifiedName, 1> columns;
  ObSEArray<ObVarInfo, 1> vars;
  ObSEArray<ObAggFunRawExpr *, 1> aggregates;
  ObSEArray<ObWinFunRawExpr *, 1> windows;
  ObSEArray<ObSubQueryInfo, 1> subqueries;
  ObSEArray<ObUDFInfo, 1> udfs;
  ObSEArray<ObOpRawExpr *, 1> operations;
  return ObRawExprUtils::build_raw_expr(factory, session, nullptr, nullptr,
      T_NONE_SCOPE, nullptr, parameters, nullptr, node, out, columns, vars,
      aggregates, windows, subqueries, udfs, operations, preparing);
}
class NativeRawConstantBuilders : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
    ObExprOperatorFactory::register_expr_operators();
  }
};
} // namespace

TEST_F(NativeRawConstantBuilders, actual_literal_finish_inlines_scalars_without_old_payload_allocation)
{
  struct Input { ObItemType kind; const char *text; int64_t integer; };
  const Input inputs[] = {{T_INT, "-9223372036854775808", INT64_MIN},
      {T_INT, "9223372036854775807", INT64_MAX}, {T_UINT64, "18446744073709551615", -1},
      {T_FLOAT, "-0.0", 0}, {T_DOUBLE, "1.5", 0}, {T_BOOL, "true", 1},
      {T_QUESTIONMARK, "?", 19}, {T_NULL, "NULL", 0}};
  for (const auto &input : inputs) {
    SCOPED_TRACE(input.text);
    std::string text(input.text);
    ParseNode node = literal_node(input.kind, text, input.integer);
    RawAllocationGate gate;
    gate.fail_all_ = true;
    ObConstRawExpr raw;
    ObString prefix;
    ASSERT_EQ(OB_SUCCESS, parse_literal(node, gate, raw, prefix));
    EXPECT_TRUE(gate.sizes_.empty());
    EXPECT_EQ(0U, raw.get_value_datum().length());
    EXPECT_EQ(0U, raw.get_bound_param_datum().length());
    if (input.kind == T_INT) { EXPECT_EQ(input.integer, raw.get_value_datum().get_int()); }
    else if (input.kind == T_UINT64) { EXPECT_EQ(UINT64_MAX, raw.get_value_datum().get_uint()); }
    else if (input.kind == T_FLOAT) {
      const float value = raw.get_value_datum().get_float();
      uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits));
      EXPECT_EQ(0x80000000U, bits);
    } else if (input.kind == T_DOUBLE) { EXPECT_EQ(1.5, raw.get_value_datum().get_double()); }
    else if (input.kind == T_BOOL) { EXPECT_TRUE(raw.get_value_datum().get_bool()); }
    else if (input.kind == T_QUESTIONMARK) { EXPECT_EQ(19, raw.get_questionmark_index()); }
    else { EXPECT_TRUE(raw.get_value_datum().is_null()); }
  }
}

TEST_F(NativeRawConstantBuilders, literal_native_and_original_parameter_boundaries_agree)
{
  struct Input { ObItemType kind; const char *text; int64_t integer; };
  const Input inputs[] = {{T_INT, "42", 42}, {T_FLOAT, "3.25", 0},
      {T_NUMBER, "123456789012345.67890", 0}, {T_NUMBER, "1e-99", 0},
      {T_VARCHAR, "customer", 0}, {T_CHAR, "", 0}, {T_NULL, "NULL", 0},
      {T_QUESTIONMARK, "?", 7}, {T_DATE, "2024-02-29", 0}};
  for (const auto &input : inputs) {
    SCOPED_TRACE(input.text);
    std::string text(input.text);
    ParseNode node = literal_node(input.kind, text, input.integer);
    ObArenaAllocator old_owner, native_owner;
    DatumParameter legacy;
    ObConstRawExpr raw;
    ObString old_prefix, native_prefix;
    ASSERT_EQ(OB_SUCCESS, parse_literal(node, old_owner, legacy, old_prefix));
    ASSERT_EQ(OB_SUCCESS, parse_literal(node, native_owner, raw, native_prefix));
    alignas(8) char scratch[64];
    DatumValue exported;
    ASSERT_EQ(OB_SUCCESS, raw.export_value(exported, scratch, sizeof(scratch)));
    EXPECT_EQ(meta_bytes(legacy.get_type_meta()), meta_bytes(exported.get_meta()));
    EXPECT_EQ(meta_bytes(legacy.get_param_meta()), meta_bytes(raw.get_expr_obj_meta()));
    alignas(8) char old_physical_scratch[64], exported_physical_scratch[64];
    ObDatum old_physical, exported_physical;
    ASSERT_EQ(OB_SUCCESS, legacy.export_legacy(old_physical, old_physical_scratch, sizeof(old_physical_scratch)));
    ASSERT_EQ(OB_SUCCESS, exported.export_legacy(exported_physical, exported_physical_scratch, sizeof(exported_physical_scratch)));
    EXPECT_EQ(old_physical.pack_, exported_physical.pack_);
    EXPECT_EQ(std::string(old_physical.ptr_ ? old_physical.ptr_ : "", old_physical.len_),
              std::string(exported_physical.ptr_ ? exported_physical.ptr_ : "", exported_physical.len_));
    if (!legacy.is_unknown()) {
      EXPECT_EQ(legacy.get_accuracy().get_accuracy(), raw.get_accuracy().get_accuracy());
      EXPECT_EQ(legacy.get_result_flag(), raw.get_result_type().get_result_flag());
    }
  }
}

TEST_F(NativeRawConstantBuilders, literal_reference_ownership_and_failure_leave_old_node_unchanged)
{
  std::string text("a\0b", 3);
  ParseNode node = literal_node(T_VARCHAR, text);
  RawAllocationGate owner;
  ObConstRawExpr raw;
  ObString prefix;
  ASSERT_EQ(OB_SUCCESS, parse_literal(node, owner, raw, prefix));
  EXPECT_NE(text.data(), raw.get_value_datum().get_reference());
  text.assign(3, 'x');
  EXPECT_EQ(std::string("a\0b", 3), std::string(raw.get_value_datum().get_string().ptr(), raw.get_value_datum().length()));
  const Datum saved = raw.get_value_datum();
  const std::string saved_meta = meta_bytes(raw.get_value_meta());
  const std::string saved_param_meta = meta_bytes(raw.get_bound_param_value_meta());
  const ObRawExprResType saved_type = raw.get_result_type();
  owner.fail_all_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, parse_literal(node, owner, raw, prefix));
  EXPECT_EQ(saved.get_reference(), raw.get_value_datum().get_reference());
  EXPECT_EQ(saved_meta, meta_bytes(raw.get_value_meta()));
  EXPECT_EQ(saved_param_meta, meta_bytes(raw.get_bound_param_value_meta()));
  EXPECT_EQ(saved_type.get_accuracy().get_accuracy(), raw.get_accuracy().get_accuracy());
}

TEST_F(NativeRawConstantBuilders, actual_scalar_builders_and_structural_param_keep_independent_roles)
{
  RawAllocationGate owner;
  ObRawExprFactory factory(owner);
  ObConstRawExpr *integer = nullptr, *unsigned_integer = nullptr, *real = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, INT64_MIN, integer));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_uint_expr(factory, ObUInt64Type, UINT64_MAX, unsigned_integer));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_double_expr(factory, ObDoubleType, 1.25, real));
  EXPECT_EQ(INT64_MIN, integer->get_value_datum().get_int());
  EXPECT_EQ(UINT64_MAX, unsigned_integer->get_value_datum().get_uint());
  EXPECT_EQ(1.25, real->get_value_datum().get_double());
  EXPECT_EQ(0U, integer->get_value_datum().length());
  EXPECT_EQ(0U, unsigned_integer->get_value_datum().length());
  EXPECT_EQ(0U, real->get_value_datum().length());
  for (int64_t bytes : owner.sizes_) { EXPECT_NE(static_cast<int64_t>(sizeof(int64_t)), bytes); }
  ObRawExpr *parameter = integer;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::create_param_expr(factory, 37, parameter));
  auto *constant = static_cast<ObConstRawExpr *>(parameter);
  EXPECT_EQ(37, constant->get_questionmark_index());
  EXPECT_TRUE(constant->get_bound_param_value_meta().is_unknown());
  EXPECT_EQ(37, constant->get_bound_param_datum().get_int());
  EXPECT_TRUE(constant->get_result_type().is_int());
  const ObRawExprResType declared = constant->get_result_type();
  const std::string object_meta = meta_bytes(constant->get_expr_obj_meta());
  ASSERT_EQ(OB_SUCCESS, constant->set_questionmark_index(OB_INVALID_INDEX));
  EXPECT_EQ(OB_INVALID_INDEX, constant->get_questionmark_index());
  EXPECT_EQ(37, constant->get_bound_param_datum().get_int());
  EXPECT_EQ(object_meta, meta_bytes(constant->get_expr_obj_meta()));
  EXPECT_EQ(declared.get_accuracy().get_accuracy(), constant->get_accuracy().get_accuracy());
}

TEST_F(NativeRawConstantBuilders, package_value_only_writer_does_not_change_parameter_or_declarations)
{
  ObArenaAllocator owner;
  ObRawExprFactory factory(owner);
  ObConstRawExpr *package = nullptr, *index = nullptr;
  ObOpRawExpr *expression = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_uint_expr(factory, ObUInt64Type, 17, package));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, 3, index));
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_OP_GET_PACKAGE_VAR, expression));
  ASSERT_EQ(OB_SUCCESS, expression->set_param_exprs(package, index));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::set_package_var_ids(expression, UINT64_MAX, -2));
  uint64_t package_id = 0; int64_t variable = 0;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::get_package_var_ids(expression, package_id, variable));
  EXPECT_EQ(UINT64_MAX, package_id); EXPECT_EQ(-2, variable);
  EXPECT_EQ(17U, package->get_bound_param_datum().get_uint());
  EXPECT_EQ(3, index->get_bound_param_datum().get_int());
  EXPECT_TRUE(package->get_result_type().is_uint64());
  EXPECT_TRUE(index->get_expr_obj_meta().is_int());
}

TEST_F(NativeRawConstantBuilders, actual_resolver_preserves_bound_null_array_and_original_number_roles)
{
  ObArenaAllocator owner;
  ObRawExprFactory factory(owner);
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext execution(owner);
  execution.set_my_session(&session);
  ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
  DEFER(ObSQLSessionInfo::ExecCtxSessionRegister unregister(session, nullptr));
  std::string question("?");
  ParseNode node = literal_node(T_QUESTIONMARK, question, 0);
  ParamStore parameters{ObWrapperAllocator(owner)};
  DatumParameter null;
  null.set_null();
  SqlTypeMeta declared; declared.set_char(); declared.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  null.set_param_meta(declared);
  null.set_length(12);
  ASSERT_EQ(OB_SUCCESS, parameters.push_back(null));
  ObRawExpr *resolved = nullptr;
  ASSERT_EQ(OB_SUCCESS, resolve_node(factory, session, node, resolved, &parameters));
  auto *constant = static_cast<ObConstRawExpr *>(resolved);
  EXPECT_TRUE(constant->get_value_meta().is_unknown());
  EXPECT_EQ(0, constant->get_questionmark_index());
  EXPECT_TRUE(constant->get_bound_param_datum().is_null());
  EXPECT_TRUE(constant->get_bound_param_value_meta().is_null());
  EXPECT_TRUE(constant->get_result_type().is_char());
  EXPECT_EQ(12, constant->get_accuracy().get_length());
  DatumParameterArray *array = DatumParameterArray::alloc(owner, 1);
  ASSERT_NE(nullptr, array);
  array->element_.set_obj_type(ObIntType);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(81, owner));
  DatumParameter array_param;
  ASSERT_EQ(OB_SUCCESS, array_param.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, owner));
  parameters.at(0) = array_param;
  resolved = nullptr;
  ASSERT_EQ(OB_SUCCESS, resolve_node(factory, session, node, resolved, &parameters));
  constant = static_cast<ObConstRawExpr *>(resolved);
  EXPECT_TRUE(constant->get_result_type().is_int());
  EXPECT_TRUE(constant->get_bound_param_value_meta().is_ext_sql_array());
  alignas(8) char scratch[64]; DatumValue wrapper;
  ASSERT_EQ(OB_SUCCESS, constant->export_bound_param(wrapper, scratch, sizeof(scratch)));
  EXPECT_EQ(reinterpret_cast<int64_t>(array), wrapper.get_ext());
  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("12.345", 6, owner));
  DatumParameter number_param;
  ASSERT_EQ(OB_SUCCESS, number_param.set_number(number, owner));
  number_param.set_param_meta(); number_param.set_precision(5); number_param.set_scale(3);
  parameters.at(0) = number_param;
  resolved = nullptr;
  ASSERT_EQ(OB_SUCCESS, resolve_node(factory, session, node, resolved, &parameters));
  constant = static_cast<ObConstRawExpr *>(resolved);
  ObRawExprResType decimal; decimal.set_decimal_int(); decimal.set_precision(20); decimal.set_scale(5);
  ASSERT_EQ(OB_SUCCESS, constant->set_dynamic_eval_questionmark(decimal));
  EXPECT_TRUE(constant->get_result_type().is_decimal_int());
  EXPECT_TRUE(constant->get_orig_qm_type().is_number());
  EXPECT_TRUE(constant->get_bound_param_value_meta().is_number());
  ASSERT_EQ(OB_SUCCESS, constant->export_bound_param(wrapper, scratch, sizeof(scratch)));
  EXPECT_EQ(0, wrapper.get_number().compare(number));
}

TEST_F(NativeRawConstantBuilders, actual_negative_prefix_failure_restores_the_entire_constant)
{
  RawAllocationGate owner;
  ObRawExprFactory factory(owner);
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  std::string text("7");
  ParseNode value = literal_node(T_INT, text, 7);
  value.is_date_unit_ = true; // Existing parser retains this exact literal prefix.
  ParseNode negative = {};
  ParseNode *children[] = {&value};
  negative.type_ = T_OP_NEG; negative.num_child_ = 1; negative.children_ = children;
  owner.fail_size_ = 2; // Only the prefix copy "-7", not AST/frame allocation.
  ObRawExpr *out = nullptr;
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, resolve_node(factory, session, negative, out));
  EXPECT_EQ(nullptr, out);
  int checked = 0;
  DLIST_FOREACH_NORET(entry, factory.get_expr_store().get_obj_list()) {
    if (entry->get_obj() != nullptr && entry->get_obj()->get_expr_type() == T_INT) {
      auto *constant = static_cast<ObConstRawExpr *>(entry->get_obj());
      EXPECT_EQ(7, constant->get_value_datum().get_int());
      EXPECT_EQ(7, constant->get_bound_param_datum().get_int());
      EXPECT_TRUE(constant->get_expr_obj_meta().is_int());
      EXPECT_EQ("7", std::string(constant->get_literal_prefix().ptr(), constant->get_literal_prefix().length()));
      ++checked;
    }
  }
  EXPECT_EQ(1, checked);
}

TEST_F(NativeRawConstantBuilders, actual_type_deduction_accepts_stack_raw_without_inner_allocator)
{
  ObConstRawExpr left, right;
  Datum left_value, right_value;
  SqlTypeMeta meta;
  meta.set_int(); left_value.set_int(7); right_value.set_int(11);
  ASSERT_EQ(nullptr, left.get_value_allocator());
  ASSERT_EQ(OB_SUCCESS, left.set_value_borrowed(left_value, meta));
  ASSERT_EQ(OB_SUCCESS, right.set_value_borrowed(right_value, meta));
  left.set_expr_type(T_INT); right.set_expr_type(T_INT);
  left.set_precision(20); left.set_scale(0); right.set_precision(20); right.set_scale(0);
  ObArenaAllocator owner;
  ObRawExprFactory factory(owner);
  ObOpRawExpr *sum = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_add_expr(factory, &left, &right, sum));
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ASSERT_EQ(OB_SUCCESS, sum->formalize(&session));
  EXPECT_TRUE(sum->get_result_type().is_int());
  EXPECT_EQ(7, left.get_value_datum().get_int());
  EXPECT_EQ(11, right.get_value_datum().get_int());
  EXPECT_EQ(nullptr, left.get_value_allocator());
}

TEST_F(NativeRawConstantBuilders, public_codegen_normalizes_small_decimal_and_owns_reference_after_source_reuse)
{
  ObArenaAllocator source, plan;
  ObRawExprFactory factory(source);
  ObConstRawExpr *small = nullptr, *wide = nullptr;
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_NUMBER, small));
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_NUMBER, wide));
  std::string small_text("12.34"), wide_text("12345678901234567890.12345");
  ParseNode small_node = literal_node(T_NUMBER, small_text), wide_node = literal_node(T_NUMBER, wide_text);
  ObString prefix;
  ASSERT_EQ(OB_SUCCESS, parse_literal(small_node, source, *small, prefix));
  ASSERT_EQ(OB_SUCCESS, parse_literal(wide_node, source, *wide, prefix));
  ASSERT_GT(small->get_value_datum().length(), 0U); // Raw Decimal is always actual-width Reference.
  ObRawExprUniqueSet expressions(false); // generate owns flattening and IS_MARKED.
  ASSERT_EQ(OB_SUCCESS, expressions.append(small)); ASSERT_EQ(OB_SUCCESS, expressions.append(wide));
  ObExprFrameInfo info(plan);
  ObStaticEngineExprCG generator(plan, nullptr, nullptr, 0, 0);
  ASSERT_EQ(OB_SUCCESS, generator.generate(expressions, info));
  ASSERT_EQ(2, info.rt_exprs_.count()); ASSERT_EQ(1, info.const_frame_ptrs_.count());
  auto *frame = info.const_frame_ptrs_.at(0);
  Datum *small_value = reinterpret_cast<Datum *>(frame + info.rt_exprs_.at(0).datum_off_);
  Datum *wide_value = reinterpret_cast<Datum *>(frame + info.rt_exprs_.at(1).datum_off_);
  EXPECT_EQ(0U, small_value->length()); EXPECT_EQ(1234, small_value->get_int());
  ASSERT_GT(wide_value->length(), 0U);
  EXPECT_NE(wide->get_value_datum().get_reference(), wide_value->get_reference());
  const std::string expected(static_cast<const char *>(wide_value->get_reference()), wide_value->length());
  factory.destory(); source.reuse();
  EXPECT_EQ(1234, small_value->get_int());
  EXPECT_EQ(expected, std::string(static_cast<const char *>(wide_value->get_reference()), wide_value->length()));
  ObExecContext execution(plan);
  execution.set_frames(info.const_frame_ptrs_.get_data());
  execution.set_frame_cnt(info.const_frame_ptrs_.count());
  ObEvalCtx context(execution);
  Datum *evaluated = nullptr;
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.at(0).eval(context, evaluated));
  EXPECT_EQ(small_value, evaluated); EXPECT_EQ(1234, evaluated->get_int());
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.at(1).eval(context, evaluated));
  EXPECT_EQ(wide_value, evaluated);
  EXPECT_EQ(expected, std::string(static_cast<const char *>(evaluated->get_reference()), evaluated->length()));
}

TEST_F(NativeRawConstantBuilders, name_const_double_cache_keeps_metadata_without_temporary_param)
{
  ObArenaAllocator source, plan;
  ObRawExprFactory factory(source);
  ObConstRawExpr *name = nullptr, *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_string_expr(factory, ObVarcharType,
      ObString::make_string("n"), CS_TYPE_UTF8MB4_BIN, name));
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_DOUBLE, value));
  std::string text("1.5e0");
  ParseNode node = literal_node(T_DOUBLE, text);
  ObString prefix;
  ASSERT_EQ(OB_SUCCESS, parse_literal(node, source, *value, prefix));
  ASSERT_EQ(ObDoubleType, value->get_value_meta().get_type());
  ASSERT_EQ(0U, value->get_value_datum().length());
  ASSERT_EQ(0U, value->get_bound_param_datum().length());
  ASSERT_DOUBLE_EQ(1.5, value->get_value_datum().get_double());
  // Exercise metadata separately from the temporary legacy scalar span.
  value->set_accuracy(ObAccuracy(18, 4));
  value->set_result_flag(NOT_NULL_FLAG);
  const ObRawExprResType input_type = value->get_result_type();
  const std::string input_value_meta = meta_bytes(value->get_value_meta());
  const std::string input_parameter_meta = meta_bytes(value->get_bound_param_value_meta());
  ObSysFunRawExpr *expression = nullptr;
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_FUN_SYS_NAME_CONST, expression));
  expression->set_func_name(ObString::make_string("name_const"));
  ASSERT_EQ(OB_SUCCESS, expression->set_param_exprs(name, value));
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ASSERT_EQ(OB_SUCCESS, expression->formalize(&session));
  // formalize has returned and the deducer's arena is gone. Inspect only
  // the empty descriptor, never bytes from the retired temporary owner.
  ASSERT_NE(nullptr, expression->get_op());
  const ObExprResType &cached = expression->get_op()->get_result_type();
  EXPECT_TRUE(cached.get_param_datum().is_null());
  EXPECT_EQ(ObNullType, cached.get_param_value_meta().get_type());
  EXPECT_EQ(CS_LEVEL_INVALID, cached.get_param_value_meta().get_collation_level());
  EXPECT_EQ(CS_TYPE_INVALID, cached.get_param_value_meta().get_collation_type());
  EXPECT_EQ(-1, cached.get_param_value_meta().get_scale());
  char diagnostic[2048];
  EXPECT_GT(expression->get_op()->to_string(diagnostic, sizeof(diagnostic)), 0);
  EXPECT_EQ(ObDoubleType, cached.get_type());
  EXPECT_EQ(18, cached.get_precision()); EXPECT_EQ(4, cached.get_scale());
  EXPECT_EQ(ObDoubleType, cached.get_calc_type());
  EXPECT_EQ(18, cached.get_calc_accuracy().get_precision());
  EXPECT_EQ(4, cached.get_calc_scale());
  EXPECT_EQ(input_type.get_collation_type(), cached.get_collation_type());
  EXPECT_EQ(input_type.get_collation_level(), cached.get_collation_level());
  EXPECT_EQ(input_type.get_collation_type(), cached.get_calc_collation_type());
  EXPECT_EQ(input_type.get_collation_level(), cached.get_calc_collation_level());
  EXPECT_EQ(input_type.get_result_flag(), cached.get_result_flag());
  EXPECT_EQ(0U, cached.get_cast_mode());
  EXPECT_EQ(ObDoubleType, expression->get_result_type().get_type());
  EXPECT_EQ(18, expression->get_result_type().get_precision());
  EXPECT_EQ(4, expression->get_result_type().get_scale());
  EXPECT_EQ(input_type.get_result_flag(), expression->get_result_type().get_result_flag());
  EXPECT_EQ(input_value_meta, meta_bytes(value->get_value_meta()));
  EXPECT_EQ(input_parameter_meta, meta_bytes(value->get_bound_param_value_meta()));
  EXPECT_EQ(input_type.get_accuracy(), value->get_result_type().get_accuracy());
  EXPECT_EQ(input_type.get_result_flag(), value->get_result_type().get_result_flag());
  EXPECT_DOUBLE_EQ(1.5, value->get_value_datum().get_double());
  EXPECT_DOUBLE_EQ(1.5, value->get_bound_param_datum().get_double());

  ObRawExprUniqueSet expressions(false); // generate owns flattening and IS_MARKED.
  ASSERT_EQ(OB_SUCCESS, expressions.append(expression));
  ObExprFrameInfo info(plan);
  ObStaticEngineExprCG generator(plan, &session, nullptr, 0, 0);
  ASSERT_EQ(OB_SUCCESS, generator.generate(expressions, info));
  ObExpr *runtime = nullptr;
  for (int64_t i = 0; i < info.rt_exprs_.count(); ++i) {
    if (info.rt_exprs_.at(i).type_ == T_FUN_SYS_NAME_CONST) {
      ASSERT_EQ(nullptr, runtime);
      runtime = &info.rt_exprs_.at(i);
    }
  }
  ASSERT_NE(nullptr, runtime);
  EXPECT_EQ(ObDoubleType, runtime->datum_meta_.get_type());
  EXPECT_EQ(18, runtime->get_sql_type().accuracy().get_precision());
  EXPECT_EQ(4, runtime->get_sql_type().accuracy().get_scale());
  ObSEArray<char *, 1> no_parameters;
  uint64_t frame_count = 0;
  char **frames = nullptr;
  ASSERT_EQ(OB_SUCCESS, info.alloc_frame(plan, no_parameters, frame_count, frames));
  factory.destory(); source.reuse();
  ObExecContext execution(plan);
  execution.set_my_session(&session);
  execution.set_frames(frames); execution.set_frame_cnt(frame_count);
  ObEvalCtx context(execution);
  Datum *evaluated = nullptr;
  ASSERT_EQ(OB_SUCCESS, runtime->eval(context, evaluated));
  ASSERT_NE(nullptr, evaluated);
  EXPECT_EQ(0U, evaluated->length());
  EXPECT_DOUBLE_EQ(1.5, evaluated->get_double());
}

TEST_F(NativeRawConstantBuilders, substr_literal_position_monotonicity_has_no_parameter_constraint)
{
  for (int64_t start : {int64_t(1), int64_t(2)}) {
    for (bool with_length : {false, true}) {
      SCOPED_TRACE(::testing::Message() << start << "/" << with_length);
      ObArenaAllocator owner;
      ObRawExprFactory factory(owner);
      ObSQLSessionInfo session;
      ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
      ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
      ObExecContext execution(owner);
      execution.set_my_session(&session);
      ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
      ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
      DEFER(ObSQLSessionInfo::ExecCtxSessionRegister unregister(session, nullptr));
      ObColumnRefRawExpr *column = nullptr;
      ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_REF_COLUMN, column));
      column->set_ref_id(500100, 16);
      column->set_column_attr(ObString::make_string("source"), ObString::make_string("text"));
      ObRawExprResType column_type;
      column_type.set_varchar(); column_type.set_length(64);
      column_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      column_type.set_collation_level(CS_LEVEL_IMPLICIT);
      column->set_result_type(column_type);
      ObConstRawExpr *position = nullptr, *length = nullptr;
      ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, start, position));
      ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, 3, length));
      ObSysFunRawExpr *substring = nullptr;
      ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_FUN_SYS_SUBSTR, substring));
      substring->set_func_name(ObString::make_string("substr"));
      if (with_length) {
        ASSERT_EQ(OB_SUCCESS, substring->set_param_exprs(column, position, length));
      } else {
        ASSERT_EQ(OB_SUCCESS, substring->set_param_exprs(column, position));
      }
      ASSERT_EQ(OB_SUCCESS, substring->formalize(&session));
      ASSERT_EQ(T_INT, position->get_expr_type());
      ASSERT_EQ(start, position->get_value_datum().get_int());
      ObPCConstParamInfo constraint;
      Monotonicity monotonicity = Monotonicity::DESC;
      bool strict = true;
      ASSERT_EQ(OB_SUCCESS, ObOptimizerUtil::get_expr_monotonicity(substring, column,
          execution, monotonicity, strict, *execution.get_physical_plan_ctx(), constraint));
      EXPECT_EQ(start == 1 ? Monotonicity::ASC : Monotonicity::NONE_MONO, monotonicity);
      EXPECT_FALSE(strict);
      EXPECT_TRUE(constraint.cache_eligible_);
      EXPECT_EQ(0, constraint.const_idx_.count());
      EXPECT_EQ(0, constraint.const_params_.count());
      EXPECT_EQ(start, position->get_bound_param_datum().get_int());
    }
  }
}

TEST_F(NativeRawConstantBuilders, substr_questionmark_monotonicity_constrains_the_actual_parameter_slot)
{
  const int64_t parameter_index = 3; // Deliberately distinct from the required value1.
  for (int64_t start : {int64_t(1), int64_t(2)}) {
    for (bool with_length : {false, true}) {
      SCOPED_TRACE(::testing::Message() << start << "/" << with_length);
      ObArenaAllocator owner;
      ObRawExprFactory factory(owner);
      ObSQLSessionInfo session;
      ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
      ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
      ObExecContext execution(owner);
      execution.set_my_session(&session);
      ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
      ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
      DEFER(ObSQLSessionInfo::ExecCtxSessionRegister unregister(session, nullptr));
      ParamStore inputs{ObWrapperAllocator(owner)};
      for (int64_t i = 0; i <= parameter_index; ++i) {
        DatumParameter input;
        ASSERT_EQ(OB_SUCCESS, input.set_int(i == parameter_index ? start : 100 + i, owner));
        input.set_param_meta();
        ASSERT_EQ(OB_SUCCESS, inputs.push_back(input));
      }
      ASSERT_EQ(OB_SUCCESS, execution.get_physical_plan_ctx()->assign_parameter_inputs(inputs));
      const ParamStore &parameters = inputs; // The resolver consumes this explicit construction domain.
      std::string question("?");
      ParseNode question_node = literal_node(T_QUESTIONMARK, question, parameter_index);
      ObRawExpr *position_expr = nullptr;
      ASSERT_EQ(OB_SUCCESS, resolve_node(factory, session, question_node, position_expr, &parameters));
      ASSERT_NE(nullptr, position_expr);
      ASSERT_EQ(T_QUESTIONMARK, position_expr->get_expr_type());
      auto *position = static_cast<ObConstRawExpr *>(position_expr);
      int64_t actual_index = -1;
      ASSERT_EQ(OB_SUCCESS, position->get_questionmark_index(actual_index));
      ASSERT_EQ(parameter_index, actual_index);
      ASSERT_EQ(start, position->get_bound_param_datum().get_int());
      ObColumnRefRawExpr *column = nullptr;
      ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_REF_COLUMN, column));
      column->set_ref_id(500100, 16);
      column->set_column_attr(ObString::make_string("source"), ObString::make_string("text"));
      ObRawExprResType column_type;
      column_type.set_varchar(); column_type.set_length(64);
      column_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      column_type.set_collation_level(CS_LEVEL_IMPLICIT);
      column->set_result_type(column_type);
      ObConstRawExpr *length = nullptr;
      ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, 3, length));
      ObSysFunRawExpr *substring = nullptr;
      ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_FUN_SYS_SUBSTR, substring));
      substring->set_func_name(ObString::make_string("substr"));
      if (with_length) {
        ASSERT_EQ(OB_SUCCESS, substring->set_param_exprs(column, position, length));
      } else {
        ASSERT_EQ(OB_SUCCESS, substring->set_param_exprs(column, position));
      }
      ASSERT_EQ(OB_SUCCESS, substring->formalize(&session));
      execution.get_physical_plan_ctx()->set_original_param_cnt(inputs.count());
      ASSERT_EQ(OB_SUCCESS, execution.get_physical_plan_ctx()->init_datum_param_store());
      ObPCConstParamInfo constraint;
      Monotonicity monotonicity = Monotonicity::DESC;
      bool strict = true;
      ASSERT_EQ(OB_SUCCESS, ObOptimizerUtil::get_expr_monotonicity(substring, column,
          execution, monotonicity, strict, *execution.get_physical_plan_ctx(), constraint));
      EXPECT_EQ(start == 1 ? Monotonicity::ASC : Monotonicity::NONE_MONO, monotonicity);
      EXPECT_FALSE(strict);
      EXPECT_TRUE(constraint.cache_eligible_);
      ASSERT_EQ(start == 1 ? 1 : 0, constraint.const_idx_.count());
      ASSERT_EQ(constraint.const_idx_.count(), constraint.const_params_.count());
      if (start == 1) {
        EXPECT_EQ(parameter_index, constraint.const_idx_.at(0));
        const NativeTypedDatum &captured = constraint.const_params_.at(0);
        ASSERT_NE(nullptr, captured.type_);
        EXPECT_EQ(ObIntType, captured.type_->kind());
        EXPECT_EQ(0U, captured.datum_.length());
        EXPECT_EQ(1, captured.datum_.get_int());
        int comparison = 9;
        ASSERT_EQ(OB_SUCCESS, ObPCConstParamInfo::compare_constant(captured, parameters.at(parameter_index), comparison));
        EXPECT_EQ(0, comparison);
        ASSERT_EQ(OB_SUCCESS, ObPCConstParamInfo::compare_constant(captured, parameters.at(0), comparison));
        EXPECT_NE(0, comparison); // Must not bind value1 to unrelated slot0.
      }
      EXPECT_EQ(start, position->get_bound_param_datum().get_int());
      EXPECT_EQ(start, parameters.at(parameter_index).value_view().get_int());
    }
  }
}


TEST_F(NativeRawConstantBuilders, null_typed_integer_builder_reaches_native_null_constant_frame)
{
  ObArenaAllocator source, plan;
  ObRawExprFactory factory(source);
  ObConstRawExpr *missing_default = nullptr, *ordinary_integer = nullptr;
  // resolve_udf_param_exprs uses this real builder for an omitted UDF
  // argument. This is not the separate IS_PL_MOCK_DEFAULT_EXPR CALL path.
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(
      factory, ObNullType, 0, missing_default));
  ASSERT_NE(nullptr, missing_default);
  EXPECT_TRUE(missing_default->get_value_datum().is_null());
  EXPECT_TRUE(missing_default->get_value_meta().is_null());
  EXPECT_TRUE(missing_default->get_result_type().is_null());
  EXPECT_FALSE(missing_default->has_flag(IS_PL_MOCK_DEFAULT_EXPR));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(
      factory, ObIntType, 17, ordinary_integer));
  EXPECT_FALSE(ordinary_integer->get_value_datum().is_null());
  EXPECT_EQ(17, ordinary_integer->get_value_datum().get_int());

  ObRawExprUniqueSet roots(false); // public generate owns flattening/IS_MARKED.
  ASSERT_EQ(OB_SUCCESS, roots.append(missing_default));
  ASSERT_EQ(OB_SUCCESS, roots.append(ordinary_integer));
  ObExprFrameInfo info(plan);
  ObStaticEngineExprCG generator(plan, nullptr, nullptr, 0, 0);
  // Keep the preceding state checks nonfatal: the old producer must still
  // reach the actual full-CG frame allocation and expose its error.
  ASSERT_EQ(OB_SUCCESS, generator.generate(roots, info));
  ASSERT_EQ(2, info.rt_exprs_.count());
  ASSERT_EQ(1, info.const_frame_ptrs_.count());
  auto *null_runtime = static_cast<ObExpr *>(
      generator.get_left_value_rt_expr(*missing_default));
  auto *integer_runtime = static_cast<ObExpr *>(
      generator.get_left_value_rt_expr(*ordinary_integer));
  ASSERT_NE(nullptr, null_runtime); ASSERT_NE(nullptr, integer_runtime);
  EXPECT_EQ(ObNullType, null_runtime->get_sql_type().kind());
  factory.destory(); source.reuse();

  ObExecContext execution(plan);
  execution.set_frames(info.const_frame_ptrs_.get_data());
  execution.set_frame_cnt(info.const_frame_ptrs_.count());
  ObEvalCtx context(execution);
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, null_runtime->eval(context, value));
  ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
  EXPECT_EQ(0U, value->length());
  ASSERT_EQ(OB_SUCCESS, integer_runtime->eval(context, value));
  ASSERT_NE(nullptr, value); EXPECT_FALSE(value->is_null());
  EXPECT_EQ(17, value->get_int()); EXPECT_EQ(0U, value->length());
}

TEST_F(NativeRawConstantBuilders, inner_is_true_range_marker_survives_real_deduction_codegen_and_eval)
{
  struct Input { SqlValueType type; int64_t value; bool truth; };
  const Input inputs[] = {{ObIntType, 0, false}, {ObIntType, 1, true},
      {ObIntType, -7, true}, {ObNullType, 0, false}};
  for (int64_t is_start : {int64_t(0), int64_t(1)}) {
    for (const Input &input : inputs) {
      SCOPED_TRACE(is_start);
      SCOPED_TRACE(input.value);
      SCOPED_TRACE(input.type);
      ObArenaAllocator source, plan;
      ObRawExprFactory factory(source);
      ObSQLSessionInfo session;
      ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
      ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
      // NULL needs an implicit Number cast during formalize; that production
      // deduction path requires the session's actual execution context.
      ObExecContext execution(plan);
      execution.set_my_session(&session);
      ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
      DEFER(ObSQLSessionInfo::ExecCtxSessionRegister unregister(session, nullptr));
      ObConstRawExpr *value = nullptr, *position = nullptr;
      ObSysFunRawExpr *endpoint = nullptr;
      ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, input.type, input.value, value));
      ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, is_start, position));
      // These are the actual producer steps used by
      // ObExprRangeConverter::generate_deduce_const_expr, including deduction.
      ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_FUNC_SYS_INNER_IS_TRUE, endpoint));
      ASSERT_EQ(OB_SUCCESS, endpoint->set_param_exprs(value, position));
      ASSERT_EQ(OB_SUCCESS, endpoint->formalize(&session));
      EXPECT_TRUE(endpoint->get_result_type().is_ext());
      EXPECT_EQ(0, static_cast<int>(endpoint->get_result_type().get_extend_type()));
      EXPECT_FALSE(endpoint->get_result_type().is_pl_extend_type());
      // Keep the metadata checks nonfatal so the original broken producer
      // still reaches the complete public CG and exposes its -4007 return.
      ObRawExprUniqueSet roots(false);
      ASSERT_EQ(OB_SUCCESS, roots.append(endpoint));
      ObExprFrameInfo info(plan);
      ObStaticEngineExprCG generator(plan, &session, nullptr, 0, 0);
      ASSERT_EQ(OB_SUCCESS, generator.generate(roots, info));
      auto *runtime = static_cast<ObExpr *>(generator.get_left_value_rt_expr(*endpoint));
      ASSERT_NE(nullptr, runtime);
      EXPECT_EQ(ObExtendType, runtime->get_sql_type().kind());
      EXPECT_EQ(0, static_cast<int>(runtime->get_sql_type().legacy_meta().get_extend_type()));
      EXPECT_FALSE(runtime->get_sql_type().requires_resource_clone());
      ObSEArray<char *, 1> no_parameters;
      uint64_t frame_count = 0; char **frames = nullptr;
      ASSERT_EQ(OB_SUCCESS, info.alloc_frame(plan, no_parameters, frame_count, frames));
      factory.destory(); source.reuse();
      execution.set_frames(frames); execution.set_frame_cnt(frame_count);
      ObEvalCtx context(execution);
      Datum *result = nullptr;
      ASSERT_EQ(OB_SUCCESS, runtime->eval(context, result));
      ASSERT_NE(nullptr, result);
      const bool expect_min = is_start != 0 ? input.truth : !input.truth;
      EXPECT_EQ(expect_min, result->is_min());
      EXPECT_EQ(!expect_min, result->is_max());
    }
  }
  // A genuine PL declaration still cannot be reconstructed from four-byte
  // metadata; the selective range producer fix must not relax this guard.
  SqlTypeMeta pl_meta; pl_meta.set_ext(); pl_meta.set_extend_type(1);
  SqlType incomplete_pl_type;
  EXPECT_EQ(OB_NOT_SUPPORTED, SqlType::from_legacy(pl_meta, ObAccuracy(), incomplete_pl_type));
}

TEST_F(NativeRawConstantBuilders, public_codegen_reuses_functions_with_complete_type_semantics)
{
  ObArenaAllocator source, execution_owner;
  RawAllocationGate plan;
  ObRawExprFactory factory(source);
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext execution(execution_owner);
  execution.set_my_session(&session);
  ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
  DEFER(ObSQLSessionInfo::ExecCtxSessionRegister unregister(session, nullptr));

  ObConstRawExpr *integer = nullptr, *ci = nullptr, *binary = nullptr;
  ObConstRawExpr *small = nullptr, *wide = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, -17, integer));
  integer->set_precision(20); integer->set_scale(0);
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_string_expr(factory, ObVarcharType,
      ObString::make_string("AbC"), CS_TYPE_UTF8MB4_GENERAL_CI, ci));
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_string_expr(factory, ObVarcharType,
      ObString::make_string("AbC"), CS_TYPE_UTF8MB4_BIN, binary));
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_NUMBER, small));
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_NUMBER, wide));
  std::string small_text("12.34"), wide_text("12345678901234567890.12345");
  ParseNode small_node = literal_node(T_NUMBER, small_text);
  ParseNode wide_node = literal_node(T_NUMBER, wide_text);
  ObString prefix;
  ASSERT_EQ(OB_SUCCESS, parse_literal(small_node, source, *small, prefix));
  ASSERT_EQ(OB_SUCCESS, parse_literal(wide_node, source, *wide, prefix));
  small->set_precision(8); small->set_scale(2);
  wide->set_precision(30); wide->set_scale(5);

  // Use the actual request SubSchema declaration, not four-byte scalar
  // metadata, for the non-scalar preparation branch.
  uint16_t array_id = 0;
  ASSERT_EQ(OB_SUCCESS, execution.get_subschema_id_by_type_string(
      ObString::make_string("ARRAY(INT)"), array_id));
  ObColumnRefRawExpr *array_column = nullptr;
  ASSERT_EQ(OB_SUCCESS, factory.create_raw_expr(T_REF_COLUMN, array_column));
  array_column->set_ref_id(500100, 16);
  array_column->set_column_attr(ObString::make_string("source"), ObString::make_string("array_value"));
  ObRawExprResType array_type;
  array_type.set_type(ObCollectionSQLType);
  array_type.set_subschema_id(array_id); array_type.set_has_lob_header();
  array_type.set_length(LENGTH_UNKNOWN_YET);
  array_column->set_result_type(array_type);

  ObRawExpr *raw[] = {integer, ci, binary, small, wide, array_column};
  ObRawExprUniqueSet roots(false);
  for (ObRawExpr *expr : raw) { ASSERT_EQ(OB_SUCCESS, roots.append(expr)); }
  ObExprFrameInfo info(plan);
  ObStaticEngineExprCG generator(plan, &session, nullptr, 0, 0);
  ASSERT_EQ(OB_SUCCESS, generator.generate(roots, info));
  // Collection columns also contribute attribute expressions to this directory.
  ASSERT_GE(info.rt_exprs_.count(), roots.get_expr_array().count());
  int64_t function_allocations = 0;
  for (int64_t bytes : plan.sizes_) {
    if (bytes == static_cast<int64_t>(sizeof(DatumBasicFuncs))) { ++function_allocations; }
  }
  // The old successful CG allocates this same owner object twice per Expr.
  // Keep this check nonfatal so its semantic controls run on that old body.
  EXPECT_EQ(info.rt_exprs_.count(), function_allocations);
  ObExpr *runtime[6] = {};
  for (int64_t i = 0; i < 6; ++i) {
    runtime[i] = static_cast<ObExpr *>(generator.get_left_value_rt_expr(*raw[i]));
    ASSERT_NE(nullptr, runtime[i]);
    int64_t directory_matches = 0;
    for (int64_t entry = 0; entry < info.rt_exprs_.count(); ++entry) {
      if (runtime[i] == &info.rt_exprs_.at(entry)) { ++directory_matches; }
    }
    ASSERT_EQ(1, directory_matches);
    for (int64_t previous = 0; previous < i; ++previous) {
      EXPECT_NE(runtime[previous], runtime[i]);
    }
    ASSERT_NE(nullptr, runtime[i]->basic_funcs_);
    EXPECT_EQ(&runtime[i]->get_sql_type(), runtime[i]->basic_funcs_->default_hash_.type());
    EXPECT_TRUE(runtime[i]->basic_funcs_->default_hash_.is_valid());
  }
  EXPECT_EQ(ObIntType, runtime[0]->get_sql_type().kind());
  EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, runtime[1]->get_sql_type().collation());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, runtime[2]->get_sql_type().collation());
  EXPECT_EQ(8, runtime[3]->get_sql_type().precision());
  EXPECT_EQ(2, runtime[3]->get_sql_type().scale());
  EXPECT_EQ(30, runtime[4]->get_sql_type().precision());
  EXPECT_EQ(5, runtime[4]->get_sql_type().scale());
  EXPECT_EQ(SqlTypeForm::Array, runtime[5]->get_sql_type().form());
  ASSERT_NE(nullptr, runtime[5]->get_sql_type().element());
  EXPECT_EQ(ObInt32Type, runtime[5]->get_sql_type().element()->kind());

  ObSEArray<char *, 1> no_parameters;
  uint64_t frame_count = 0; char **frames = nullptr;
  ASSERT_EQ(OB_SUCCESS, info.alloc_frame(plan, no_parameters, frame_count, frames));
  factory.destory(); source.reuse();
  small_text.assign(small_text.size(), 'x'); wide_text.assign(wide_text.size(), 'x');
  execution.set_frames(frames); execution.set_frame_cnt(frame_count);
  ObEvalCtx context(execution);
  for (int64_t i = 0; i < 6; ++i) {
    SCOPED_TRACE(i);
    Datum *value = nullptr;
    if (i == 5) {
      // A typed column starts as SQL NULL. Complex declaration/functions must
      // remain complete even when no collection payload is being evaluated.
      value = &runtime[i]->locate_expr_datum(context);
      ASSERT_TRUE(value->is_null());
    } else {
      ASSERT_EQ(OB_SUCCESS, runtime[i]->eval(context, value));
      ASSERT_NE(nullptr, value);
    }
    if (i == 0) { EXPECT_EQ(-17, value->get_int()); }
    if (i == 1 || i == 2) {
      EXPECT_EQ(std::string("AbC"), std::string(value->get_string().ptr(), value->length()));
    }
    if (i == 3) { EXPECT_EQ(0U, value->length()); EXPECT_EQ(1234, value->get_int()); }
    if (i == 4) { EXPECT_EQ(16U, value->length()); }

    DatumBasicFuncs independent;
    ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(runtime[i]->get_sql_type(), independent));
    const DatumBasicFuncs &functions = *runtime[i]->basic_funcs_;
    EXPECT_EQ(independent.legacy_functions(), functions.legacy_functions());
    const DatumHashOp actual_hashes[] = {functions.default_hash_, functions.default_hash_batch_,
        functions.murmur_hash_, functions.murmur_hash_batch_, functions.xx_hash_, functions.xx_hash_batch_,
        functions.wy_hash_, functions.wy_hash_batch_, functions.murmur_hash_v2_, functions.murmur_hash_v2_batch_};
    const DatumHashOp expected_hashes[] = {independent.default_hash_, independent.default_hash_batch_,
        independent.murmur_hash_, independent.murmur_hash_batch_, independent.xx_hash_, independent.xx_hash_batch_,
        independent.wy_hash_, independent.wy_hash_batch_, independent.murmur_hash_v2_, independent.murmur_hash_v2_batch_};
    for (int64_t hash = 0; hash < 10; ++hash) {
      uint64_t actual = 0, expected = 0;
      ASSERT_EQ(OB_SUCCESS, actual_hashes[hash](*value, 73, actual));
      ASSERT_EQ(OB_SUCCESS, expected_hashes[hash](*value, 73, expected));
      EXPECT_EQ(expected, actual);
    }
    Datum null;
    int actual = 9, expected = 8;
    ASSERT_EQ(OB_SUCCESS, functions.null_first_cmp_(*value, null, actual));
    ASSERT_EQ(OB_SUCCESS, independent.null_first_cmp_(*value, null, expected));
    EXPECT_EQ(expected, actual); EXPECT_EQ(i == 5 ? 0 : 1, actual);
    ASSERT_EQ(OB_SUCCESS, functions.null_last_cmp_(*value, null, actual));
    ASSERT_EQ(OB_SUCCESS, independent.null_last_cmp_(*value, null, expected));
    EXPECT_EQ(expected, actual); EXPECT_EQ(i == 5 ? 0 : -1, actual);
    if (i == 1 || i == 2) {
      Datum lower; ASSERT_EQ(OB_SUCCESS, lower.set_string("aBc", 3));
      ASSERT_EQ(OB_SUCCESS, functions.null_first_cmp_(*value, lower, actual));
      if (i == 1) { EXPECT_EQ(0, actual); } else { EXPECT_LT(actual, 0); }
    }
  }
}

TEST_F(NativeRawConstantBuilders, public_codegen_retains_actual_function_owner_oom)
{
  ObArenaAllocator source;
  RawAllocationGate plan;
  plan.fail_size_ = sizeof(DatumBasicFuncs);
  ObRawExprFactory factory(source);
  ObConstRawExpr *integer = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObRawExprUtils::build_const_int_expr(factory, ObIntType, 17, integer));
  ObRawExprUniqueSet roots(false);
  ASSERT_EQ(OB_SUCCESS, roots.append(integer));
  ObExprFrameInfo info(plan);
  ObStaticEngineExprCG generator(plan, nullptr, nullptr, 0, 0);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, generator.generate(roots, info));
  int64_t function_requests = 0;
  for (int64_t bytes : plan.sizes_) {
    if (bytes == static_cast<int64_t>(sizeof(DatumBasicFuncs))) { ++function_requests; }
  }
  EXPECT_EQ(1, function_requests);
  ASSERT_EQ(1, info.rt_exprs_.count());
  EXPECT_EQ(nullptr, info.rt_exprs_.at(0).sql_type_);
  EXPECT_EQ(nullptr, info.rt_exprs_.at(0).basic_funcs_);
  EXPECT_EQ(0, info.const_frame_ptrs_.count());
}
