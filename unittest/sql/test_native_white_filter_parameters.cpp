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

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "lib/charset/ob_charset.h"
#include "common/datum/datum.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
class NativeWhiteFilterParameters : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
    ObExprOperatorFactory::register_expr_operators();
  }
};

// Build the actual IN Expr and constants through Raw formalization and full CG.
// Only the standard WhiteFilter initialization consumes those runtime args.
class WhiteFilterInputs
{
public:
  WhiteFilterInputs()
      : owner_("WhiteParamTest"), session_(), exec_(owner_), eval_(exec_),
        info_(owner_), cg_(owner_, &session_, nullptr, 0, 0), factory_(owner_),
        push_spec_(owner_), push_(eval_, push_spec_), node_(owner_),
        filter_(owner_, node_, push_), expression_(nullptr)
  {}
  ~WhiteFilterInputs()
  { ObSQLSessionInfo::ExecCtxSessionRegister unregister(session_, nullptr); }

  int start()
  {
    int ret = session_.test_init(1, 1, nullptr);
    if (ret == OB_SUCCESS) { ret = session_.load_default_sys_variable(false, false); }
    if (ret == OB_SUCCESS) { exec_.set_my_session(&session_); ret = exec_.create_physical_plan_ctx(); }
    if (ret == OB_SUCCESS) { ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &exec_); }
    return ret;
  }

  int integers(SqlValueType column_kind, std::initializer_list<int64_t> values)
  {
    int ret = start();
    ObSEArray<ObRawExpr *, 8> arguments;
    for (int64_t value : values) {
      ObConstRawExpr *constant = nullptr;
      if (ret == OB_SUCCESS) { ret = ObRawExprUtils::build_const_int_expr(factory_, ObIntType, value, constant); }
      if (ret == OB_SUCCESS) { ret = arguments.push_back(constant); }
    }
    ObRawExpr *null_value = nullptr;
    if (ret == OB_SUCCESS) { ret = ObRawExprUtils::build_null_expr(factory_, null_value); }
    if (ret == OB_SUCCESS) { ret = arguments.push_back(null_value); }
    if (ret == OB_SUCCESS) { ret = build(column_kind, arguments); }
    return ret;
  }

  int strings(const std::vector<std::string> &values)
  {
    int ret = start();
    ObSEArray<ObRawExpr *, 8> arguments;
    for (const auto &value : values) {
      ObConstRawExpr *constant = nullptr;
      if (ret == OB_SUCCESS) {
        ret = ObRawExprUtils::build_const_string_expr(factory_, ObVarcharType,
            ObString(value.size(), value.data()), CS_TYPE_UTF8MB4_BIN, constant);
      }
      if (ret == OB_SUCCESS) { ret = arguments.push_back(constant); }
    }
    ObRawExpr *null_value = nullptr;
    if (ret == OB_SUCCESS) { ret = ObRawExprUtils::build_null_expr(factory_, null_value); }
    if (ret == OB_SUCCESS) { ret = arguments.push_back(null_value); }
    if (ret == OB_SUCCESS) { ret = build(ObVarcharType, arguments); }
    return ret;
  }

  int build(SqlValueType column_kind, const ObIArray<ObRawExpr *> &arguments)
  {
    ObColumnRefRawExpr *column = nullptr;
    ObOpRawExpr *row = nullptr, *in = nullptr;
    int ret = factory_.create_raw_expr(T_REF_COLUMN, column);
    if (ret == OB_SUCCESS) {
      column->set_ref_id(990001, 16);
      column->set_column_attr(ObString::make_string("source"), ObString::make_string("value"));
      ObRawExprResType type;
      type.set_type(column_kind);
      type.set_collation_type(column_kind == ObVarcharType ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
      type.set_collation_level(column_kind == ObVarcharType ? CS_LEVEL_IMPLICIT : CS_LEVEL_NUMERIC);
      if (column_kind == ObVarcharType) { type.set_length(64); }
      else { type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[column_kind]); }
      column->set_result_type(type);
      ret = column->add_flag(IS_COLUMN);
      if (ret == OB_SUCCESS) { ret = column->add_flag(CNT_COLUMN); }
    }
    if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_OP_ROW, row); }
    if (ret == OB_SUCCESS) { ret = row->set_param_exprs(arguments); }
    if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_OP_IN, in); }
    if (ret == OB_SUCCESS) { ret = in->set_param_exprs(column, row); }
    if (ret == OB_SUCCESS) { ret = in->formalize(&session_); }
    ObRawExprUniqueSet roots(false);
    if (ret == OB_SUCCESS) { ret = roots.append(in); }
    if (ret == OB_SUCCESS) { ret = cg_.generate(roots, info_); }
    if (ret == OB_SUCCESS) { ret = info_.pre_alloc_exec_memory(exec_); }
    if (ret == OB_SUCCESS) {
      eval_.frames_ = exec_.get_frames();
      expression_ = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*in));
      if (expression_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else { node_.expr_ = expression_; ret = node_.set_op_type(*in); }
    }
    if (ret == OB_SUCCESS) { ret = node_.column_exprs_.init(1); }
    if (ret == OB_SUCCESS) { ret = node_.column_exprs_.push_back(expression_->args_[0]); }
    return ret;
  }

  int initialize()
  {
    bool valid = false;
    const int ret = filter_.init_evaluated_datums(valid);
    return ret == OB_SUCCESS && !valid ? OB_ERR_UNEXPECTED : ret;
  }

  // After initialize() none of the probes below evaluates the Expr tree again:
  // filter_datum/exist_in_set consume only the owned storage predicate cache.
  void retire_integer_frames()
  {
    ObExpr &values = *expression_->args_[1];
    for (uint32_t i = 0; i < values.arg_cnt_; ++i) {
      Datum &value = values.args_[i]->locate_datum_for_write(eval_);
      if (!value.is_null()) { value.set_int(777); }
    }
  }
  void retire_string_frames()
  {
    ObExpr &values = *expression_->args_[1];
    for (uint32_t i = 0; i < values.arg_cnt_; ++i) {
      Datum &value = values.args_[i]->locate_datum_for_write(eval_);
      if (!value.is_null()) {
        char *bytes = const_cast<char *>(value.get_string().ptr());
        if (value.length() > 0) { MEMSET(bytes, 'x', value.length()); }
        value.set_null();
      }
    }
  }

  ObArenaAllocator owner_;
  ObSQLSessionInfo session_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  ObExprFrameInfo info_;
  ObStaticEngineExprCG cg_;
  ObRawExprFactory factory_;
  ObPushdownExprSpec push_spec_;
  ObPushdownOperator push_;
  ObPushdownWhiteFilterNode node_;
  ObWhiteFilterExecutor filter_;
  ObExpr *expression_;
};

void expect_membership(ObWhiteFilterExecutor &filter, const ObDatum &value, bool exists)
{
  bool found = !exists, filtered = exists;
  ASSERT_EQ(OB_SUCCESS, filter.exist_in_set(value, found));
  EXPECT_EQ(exists, found);
  ASSERT_EQ(OB_SUCCESS, filter.filter_datum(value, filtered));
  EXPECT_EQ(!exists, filtered);
}
}

TEST_F(NativeWhiteFilterParameters, actual_in_hash_owns_descriptors_after_initializer_return_and_array_sort)
{
  WhiteFilterInputs input;
  ASSERT_EQ(OB_SUCCESS, input.integers(ObIntType, {9, 1, 5, 1, 3}));
  ASSERT_NE(nullptr, input.expression_);
  EXPECT_EQ(0U, input.expression_->inner_func_cnt_); // Real native CG has no legacy function array.
  ASSERT_EQ(6U, input.expression_->args_[1]->arg_cnt_);
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  ASSERT_EQ(4, input.filter_.get_datums().count());
  const int64_t sorted[] = {1, 3, 5, 9};
  for (int64_t i = 0; i < 4; ++i) { EXPECT_EQ(sorted[i], input.filter_.get_datums().at(i).get_int()); }
  input.retire_integer_frames();
  for (int64_t value : sorted) {
    ObDatum probe(reinterpret_cast<const char *>(&value), sizeof(value), false);
    ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, true));
  }
  for (int64_t value : {int64_t(-1), int64_t(0), int64_t(2), int64_t(10)}) {
    ObDatum probe(reinterpret_cast<const char *>(&value), sizeof(value), false);
    ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, false));
  }
  ObDatum null_value; null_value.set_null(); bool filtered = false;
  ASSERT_EQ(OB_SUCCESS, input.filter_.filter_datum(null_value, filtered)); EXPECT_TRUE(filtered);
  // Container staging/logging is safe even before function pointers are bound.
  ObWhiteFilterParam empty, copy(empty);
  char buffer[256] = {};
  EXPECT_GT(empty.to_string(buffer, sizeof(buffer)), 0);
  EXPECT_GT(copy.to_string(buffer, sizeof(buffer)), 0);
}

TEST_F(NativeWhiteFilterParameters, actual_string_in_keeps_owned_payloads_empty_bytes_and_null_semantics)
{
  WhiteFilterInputs input;
  const std::string binary("a\0b", 3);
  ASSERT_EQ(OB_SUCCESS, input.strings({"zeta", "alpha", "zeta", binary, ""}));
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  ASSERT_EQ(4, input.filter_.get_datums().count());
  input.retire_string_frames();
  for (const std::string &value : {std::string(""), binary, std::string("alpha"), std::string("zeta")}) {
    ObDatum probe; probe.set_string(value.data(), value.size());
    ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, true));
  }
  for (const std::string &value : {std::string("absent"), std::string("a"), std::string("xxxx")}) {
    ObDatum probe; probe.set_string(value.data(), value.size());
    ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, false));
  }
  ObDatum null_value; null_value.set_null(); bool filtered = false;
  ASSERT_EQ(OB_SUCCESS, input.filter_.filter_datum(null_value, filtered)); EXPECT_TRUE(filtered);
}

TEST_F(NativeWhiteFilterParameters, actual_unsigned_column_compares_signed_in_keys_without_reinterpreting_extremes)
{
  WhiteFilterInputs input;
  ASSERT_EQ(OB_SUCCESS, input.integers(ObUInt64Type, {9, -1, 1, 3, 1}));
  ASSERT_EQ(T_REF_COLUMN, input.expression_->args_[0]->type_);
  EXPECT_EQ(ObUInt64Type, input.expression_->args_[0]->obj_meta_.get_type());
  EXPECT_EQ(ObIntType, input.expression_->args_[1]->args_[0]->obj_meta_.get_type());
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  ASSERT_EQ(4, input.filter_.get_datums().count());
  input.retire_integer_frames();
  for (uint64_t value : {uint64_t(1), uint64_t(3), uint64_t(9)}) {
    ObDatum probe(reinterpret_cast<const char *>(&value), sizeof(value), false);
    ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, true));
  }
  for (uint64_t value : {uint64_t(0), uint64_t(2), UINT64_MAX}) {
    ObDatum probe(reinterpret_cast<const char *>(&value), sizeof(value), false);
    ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, false));
  }
}
