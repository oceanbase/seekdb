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
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_topn_filter.h"
#include "sql/engine/sort/ob_sort_basic_info.h"
#include "sql/engine/px/p2p_datahub/ob_pushdown_topn_filter_msg.h"
#include "storage/blocksstable/encoding/ob_const_decoder.h"
#include "storage/blocksstable/index_block/ob_skip_index_filter_executor.h"

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
  explicit WhiteFilterInputs(ObIAllocator *predicate_owner = nullptr)
      : owner_("WhiteParamTest"), session_(), exec_(owner_), eval_(exec_),
        info_(owner_), cg_(owner_, &session_, nullptr, 0, 0), factory_(owner_),
        push_spec_(owner_), push_(eval_, push_spec_), node_(owner_),
        filter_(predicate_owner == nullptr ? owner_ : *predicate_owner, node_, push_), expression_(nullptr)
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

  int between_integers(int64_t lower, int64_t upper)
  {
    int ret = start();
    ObConstRawExpr *left = nullptr, *right = nullptr;
    if (ret == OB_SUCCESS) { ret = ObRawExprUtils::build_const_int_expr(factory_, ObIntType, lower, left); }
    if (ret == OB_SUCCESS) { ret = ObRawExprUtils::build_const_int_expr(factory_, ObIntType, upper, right); }
    if (ret == OB_SUCCESS) { ret = build_between(ObIntType, left, right); }
    return ret;
  }

  int between_strings(const std::string &lower, const std::string &upper)
  {
    int ret = start();
    ObConstRawExpr *left = nullptr, *right = nullptr;
    if (ret == OB_SUCCESS) {
      ret = ObRawExprUtils::build_const_string_expr(factory_, ObVarcharType,
          ObString(lower.size(), lower.data()), CS_TYPE_UTF8MB4_BIN, left);
    }
    if (ret == OB_SUCCESS) {
      ret = ObRawExprUtils::build_const_string_expr(factory_, ObVarcharType,
          ObString(upper.size(), upper.data()), CS_TYPE_UTF8MB4_BIN, right);
    }
    if (ret == OB_SUCCESS) { ret = build_between(ObVarcharType, left, right); }
    return ret;
  }

  int between_decimals(int16_t precision, int32_t width)
  {
    int ret = start();
    ObConstRawExpr *ends[2] = {};
    for (int i = 0; ret == OB_SUCCESS && i < 2; ++i) {
      alignas(8) unsigned char coefficient[16] = {};
      coefficient[0] = i == 0 ? 10 : 90;
      DatumValue value;
      ret = value.set_decimal_int(width, 2, reinterpret_cast<const ObDecimalInt *>(coefficient), owner_);
      if (ret == OB_SUCCESS) { ret = ObRawExprUtils::build_const_obj_expr(factory_, value, ends[i]); }
      if (ret == OB_SUCCESS) { ends[i]->set_accuracy(ObAccuracy(precision, 2)); }
    }
    if (ret == OB_SUCCESS) { ret = build_between(ObDecimalIntType, ends[0], ends[1], precision); }
    return ret;
  }

  int build_between(SqlValueType kind, ObRawExpr *left, ObRawExpr *right, int16_t precision = 0)
  {
    ObColumnRefRawExpr *column = nullptr;
    ObOpRawExpr *between = nullptr;
    int ret = factory_.create_raw_expr(T_REF_COLUMN, column);
    if (ret == OB_SUCCESS) {
      ObRawExprResType type;
      type.set_type(kind);
      type.set_collation_type(kind == ObVarcharType ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
      type.set_collation_level(kind == ObVarcharType ? CS_LEVEL_IMPLICIT : CS_LEVEL_NUMERIC);
      if (kind == ObVarcharType) { type.set_length(64); }
      else if (kind == ObDecimalIntType) { type.set_accuracy(ObAccuracy(precision, 2)); }
      else { type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[kind]); }
      column->set_result_type(type);
      column->set_ref_id(990002, 16);
      ret = column->add_flag(IS_COLUMN);
      if (ret == OB_SUCCESS) { ret = column->add_flag(CNT_COLUMN); }
    }
    if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_OP_BTW, between); }
    if (ret == OB_SUCCESS) { ret = between->set_param_exprs(column, left, right); }
    if (ret == OB_SUCCESS) { ret = between->formalize(&session_); }
    ObRawExprUniqueSet roots(false);
    if (ret == OB_SUCCESS) { ret = roots.append(between); }
    if (ret == OB_SUCCESS) { ret = cg_.generate(roots, info_); }
    if (ret == OB_SUCCESS) { ret = info_.pre_alloc_exec_memory(exec_); }
    if (ret == OB_SUCCESS) {
      eval_.frames_ = exec_.get_frames();
      expression_ = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*between));
      if (expression_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else { node_.expr_ = expression_; ret = node_.set_op_type(*between); }
    }
    if (ret == OB_SUCCESS) { ret = node_.column_exprs_.init(1); }
    if (ret == OB_SUCCESS) { ret = node_.column_exprs_.push_back(expression_->args_[0]); }
    return ret;
  }

  int topn_string()
  {
    int ret = start();
    ObColumnRefRawExpr *column = nullptr;
    ObOpRawExpr *topn = nullptr;
    if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_REF_COLUMN, column); }
    if (ret == OB_SUCCESS) {
      ObRawExprResType type;
      type.set_varchar(); type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      type.set_collation_level(CS_LEVEL_IMPLICIT); type.set_length(64);
      column->set_result_type(type); column->set_ref_id(990003, 16);
      ret = column->add_flag(IS_COLUMN);
      if (ret == OB_SUCCESS) { ret = column->add_flag(CNT_COLUMN); }
    }
    if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_OP_PUSHDOWN_TOPN_FILTER, topn); }
    if (ret == OB_SUCCESS) { ret = topn->init_param_exprs(1); }
    if (ret == OB_SUCCESS) { ret = topn->add_param_expr(column); }
    if (ret == OB_SUCCESS) { ret = topn->formalize(&session_); }
    ObRawExprUniqueSet roots(false);
    if (ret == OB_SUCCESS) { ret = roots.append(topn); }
    if (ret == OB_SUCCESS) { ret = cg_.generate(roots, info_); }
    if (ret == OB_SUCCESS) { ret = info_.pre_alloc_exec_memory(exec_); }
    if (ret == OB_SUCCESS) { ret = exec_.init_expr_op(info_.need_ctx_cnt_); }
    if (ret == OB_SUCCESS) {
      eval_.frames_ = exec_.get_frames();
      expression_ = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*topn));
      if (expression_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
      exec_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
    }
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







namespace {
class WhitePredicateOwner final : public ObIAllocator
{
public:
  void *alloc(const int64_t size) override
  {
    ++calls_;
    return fail_ || calls_ == fail_at_ ? nullptr : arena_.alloc(size);
  }
  void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *value) override { arena_.free(value); }
  ObArenaAllocator arena_;
  int64_t calls_ = 0;
  int64_t fail_at_ = -1;
  bool fail_ = false;
};

template<class Tag, typename Tag::Type Member> struct WhitePrivateMember
{ friend typename Tag::Type white_member(Tag) { return Member; } };
struct WhiteSkipBetween
{
  using Type = int (oceanbase::blocksstable::ObSkipIndexFilterExecutor::*)(
      const ObWhiteFilterExecutor &, const ObDatum &, const bool &,
      const ObDatum &, const bool &, ObBoolMask &);
  friend Type white_member(WhiteSkipBetween);
};
template struct WhitePrivateMember<WhiteSkipBetween,
    &oceanbase::blocksstable::ObSkipIndexFilterExecutor::bt_operator>;
struct WhiteDynamicPrepare
{
  using Type = int (ObDynamicFilterExecutor::*)();
  friend Type white_member(WhiteDynamicPrepare);
};
template struct WhitePrivateMember<WhiteDynamicPrepare, &ObDynamicFilterExecutor::try_preparing_data>;
struct WhiteDynamicUpdate
{
  using Type = int (ObDynamicFilterExecutor::*)();
  friend Type white_member(WhiteDynamicUpdate);
};
template struct WhitePrivateMember<WhiteDynamicUpdate, &ObDynamicFilterExecutor::try_updating_data>;

void expect_between(ObWhiteFilterExecutor &filter, const ObDatum &value, bool included)
{
  bool filtered = included;
  ASSERT_EQ(OB_SUCCESS, filter.filter_datum(value, filtered));
  EXPECT_EQ(!included, filtered);
}

// A real CONST column record, decoded by the public decoder entry. The encoded
// row format remains the storage ABI; only the predicate's retained values move.
void expect_const_block(WhiteFilterInputs &input, const ObDatum &stored, int64_t expected_rows)
{
  using namespace oceanbase::blocksstable;
  std::vector<char> bytes(sizeof(ObConstMetaHeader) + stored.len_);
  auto *meta = new (bytes.data()) ObConstMetaHeader;
  meta->const_ref_ = stored.is_null() ? 1 : 0;
  meta->offset_ = sizeof(ObConstMetaHeader);
  if (!stored.is_null() && stored.len_ > 0) { MEMCPY(meta->payload_, stored.ptr_, stored.len_); }
  ObColumnHeader column;
  column.type_ = ObColumnHeader::CONST;
  column.obj_type_ = input.expression_->args_[0]->obj_meta_.get_type();
  column.length_ = bytes.size();
  ObMicroBlockHeader micro;
  ObColumnDecoderCtx context;
  context.fill(input.expression_->args_[0]->obj_meta_, &micro, &column, &input.owner_, 0);
  ObConstDecoder decoder;
  ASSERT_EQ(OB_SUCCESS, decoder.init(micro, column, bytes.data()));
  PushdownFilterInfo info;
  info.start_ = 0; info.count_ = 5;
  ObBitmap bitmap(input.owner_);
  ASSERT_EQ(OB_SUCCESS, bitmap.init(info.count_));
  ASSERT_EQ(OB_SUCCESS, decoder.pushdown_operator(nullptr, context, input.filter_,
      bytes.data(), nullptr, info, bitmap));
  EXPECT_EQ(expected_rows, bitmap.popcnt());
}
}

TEST_F(NativeWhiteFilterParameters, actual_between_inline_capture_and_decoder_keep_two_live_values_without_payload_allocations)
{
  WhitePredicateOwner owner;
  WhiteFilterInputs input(&owner);
  ASSERT_EQ(OB_SUCCESS, input.between_integers(-11, 37));
  ASSERT_EQ(OB_SUCCESS, input.initialize()); // Reserve the real parameter directory first.
  const int64_t calls = owner.calls_;
  owner.fail_ = true;
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  EXPECT_EQ(calls, owner.calls_);
  ASSERT_EQ(2, input.filter_.get_datums().count());
  EXPECT_EQ(0U, input.filter_.get_min_param().get_datum().length());
  EXPECT_EQ(0U, input.filter_.get_max_param().get_datum().length());
  input.expression_->args_[1]->locate_datum_for_write(input.eval_).set_int(999);
  input.expression_->args_[2]->locate_datum_for_write(input.eval_).set_int(-999);
  alignas(8) char left_scratch[64], right_scratch[64];
  ObDatum lower, upper;
  ASSERT_EQ(OB_SUCCESS, input.filter_.export_parameter(0, lower, left_scratch, sizeof(left_scratch)));
  ASSERT_EQ(OB_SUCCESS, input.filter_.export_parameter(1, upper, right_scratch, sizeof(right_scratch)));
  EXPECT_EQ(-11, lower.get_int()); EXPECT_EQ(37, upper.get_int());
  EXPECT_NE(lower.ptr_, upper.ptr_);
  for (int64_t value : {int64_t(-12), int64_t(-11), int64_t(0), int64_t(37), int64_t(38)}) {
    ObDatum probe(reinterpret_cast<const char *>(&value), sizeof(value), false);
    ASSERT_NO_FATAL_FAILURE(expect_between(input.filter_, probe, value >= -11 && value <= 37));
    ASSERT_NO_FATAL_FAILURE(expect_const_block(input, probe, value >= -11 && value <= 37 ? 5 : 0));
  }
  oceanbase::blocksstable::ObSkipIndexFilterExecutor skip;
  ObBoolMask mask;
  int64_t min = -5, max = 20;
  ObDatum minimum(reinterpret_cast<const char *>(&min), 8, false);
  ObDatum maximum(reinterpret_cast<const char *>(&max), 8, false);
  const bool prefix = false;
  ASSERT_EQ(OB_SUCCESS, (skip.*white_member(WhiteSkipBetween{}))(
      input.filter_, minimum, prefix, maximum, prefix, mask));
  EXPECT_TRUE(mask.is_always_true());
  min = 40; max = 50;
  ASSERT_EQ(OB_SUCCESS, (skip.*white_member(WhiteSkipBetween{}))(
      input.filter_, minimum, prefix, maximum, prefix, mask));
  EXPECT_TRUE(mask.is_always_false());
  ObDatum untouched = lower;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, input.filter_.export_parameter(1, untouched, right_scratch, 1));
  EXPECT_EQ(lower.ptr_, untouched.ptr_); EXPECT_EQ(-11, untouched.get_int());
  EXPECT_EQ(OB_INVALID_ARGUMENT, input.filter_.export_parameter(2, untouched, right_scratch, sizeof(right_scratch)));
}

TEST_F(NativeWhiteFilterParameters, actual_between_reference_capture_retires_source_and_preserves_failure_prefix)
{
  WhitePredicateOwner owner;
  WhiteFilterInputs input(&owner);
  ASSERT_EQ(OB_SUCCESS, input.between_strings(std::string("a\0a", 3), std::string("z\0z", 3)));
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  const char *owned_left = input.filter_.get_min_param().get_string().ptr();
  const char *owned_right = input.filter_.get_max_param().get_string().ptr();
  for (int i = 1; i <= 2; ++i) {
    Datum &source = input.expression_->args_[i]->locate_datum_for_write(input.eval_);
    EXPECT_NE(source.get_string().ptr(), i == 1 ? owned_left : owned_right);
    MEMSET(const_cast<char *>(source.get_string().ptr()), 'x', source.length());
    source.set_null();
  }
  std::string inside("m\0m", 3), outside("zzzz");
  ObDatum probe; probe.set_string(inside.data(), inside.size());
  ASSERT_NO_FATAL_FAILURE(expect_between(input.filter_, probe, true));
  ASSERT_NO_FATAL_FAILURE(expect_const_block(input, probe, 5));
  probe.set_string(outside.data(), outside.size());
  ASSERT_NO_FATAL_FAILURE(expect_between(input.filter_, probe, false));
  // Re-evaluation publishes the successful first value, then rejects the real
  // second reference allocation. It does not promise whole-array rollback.
  std::string lower("b"), upper("y");
  input.expression_->args_[1]->locate_datum_for_write(input.eval_).set_string(lower.data(), lower.size());
  input.expression_->args_[2]->locate_datum_for_write(input.eval_).set_string(upper.data(), upper.size());
  const int64_t calls = owner.calls_;
  owner.fail_at_ = calls + 2;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, input.initialize());
  EXPECT_EQ(calls + 2, owner.calls_);
  ASSERT_EQ(1, input.filter_.get_datums().count());
  EXPECT_EQ("b", std::string(input.filter_.get_min_param().get_string().ptr(), 1));
  owner.fail_at_ = -1;
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  ASSERT_EQ(2, input.filter_.get_datums().count());
  lower[0] = 'q'; upper[0] = 'q';
  EXPECT_EQ("b", std::string(input.filter_.get_min_param().get_string().ptr(), 1));
  EXPECT_EQ("y", std::string(input.filter_.get_max_param().get_string().ptr(), 1));
}

TEST_F(NativeWhiteFilterParameters, actual_decimal_between_owns_the_prepared_physical_width_and_declared_null)
{
  for (int16_t precision : {int16_t(18), int16_t(38)}) {
    WhiteFilterInputs input;
    const int32_t width = precision == 18 ? 8 : 16;
    ASSERT_EQ(OB_SUCCESS, input.between_decimals(precision, width));
    ASSERT_EQ(OB_SUCCESS, input.initialize());
    for (int64_t i = 0; i < 2; ++i) {
      const DatumValue &value = input.filter_.get_datums().at(i);
      EXPECT_EQ(ObDecimalIntType, value.get_type()); EXPECT_EQ(2, value.get_scale());
      EXPECT_EQ(static_cast<uint32_t>(width), value.get_datum().length());
      EXPECT_EQ(i == 0 ? 10 : 90, *static_cast<const unsigned char *>(value.get_datum().get_reference()));
      input.expression_->args_[i + 1]->locate_datum_for_write(input.eval_).set_null();
    }
    alignas(8) unsigned char coefficient[16] = {}; coefficient[0] = 50;
    ObDatum probe; probe.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(coefficient), width);
    ASSERT_NO_FATAL_FAILURE(expect_between(input.filter_, probe, true));
    ASSERT_NO_FATAL_FAILURE(expect_const_block(input, probe, 5));
    ASSERT_EQ(OB_SUCCESS, input.initialize());
    ASSERT_EQ(2, input.filter_.get_datums().count());
    EXPECT_TRUE(input.filter_.null_param_contained());
    for (int64_t i = 0; i < 2; ++i) {
      const DatumValue &value = input.filter_.get_datums().at(i);
      EXPECT_TRUE(value.is_null());
      EXPECT_EQ(input.expression_->args_[i + 1]->obj_meta_.get_type(), value.get_type());
      EXPECT_EQ(input.expression_->args_[i + 1]->obj_meta_.get_scale(), value.get_scale());
    }
  }
}

namespace {
// Use actual StoredRow packing and the TopN message producer, not a replacement
// callback or a synthesized parameter directory.
void replace_topn_string(ObPushDownTopNFilterMsg &message, const std::string &text)
{
  using Row = ObChunkDatumStore::StoredRow;
  ObDatum value; value.set_string(text.data(), text.size());
  const uint32_t extra = (8 - (sizeof(Row) + sizeof(StoredDatumCell)) % 8) % 8;
  const int64_t size = ObChunkDatumStore::Block::row_store_size(&value, 1, extra);
  std::vector<char> memory(size);
  Row *row = new (memory.data()) Row;
  ASSERT_EQ(OB_SUCCESS, row->copy_shadow_datums(&value, 1, row->payload_, size - sizeof(Row), size, extra));
  bool changed = false;
  ASSERT_EQ(OB_SUCCESS, message.update_filter_data(row, changed)); ASSERT_TRUE(changed);
  MEMSET(memory.data(), 0xA5, memory.size());
}

struct WhiteTopNContextLink
{
  explicit WhiteTopNContextLink(ObExprTopNFilterContext &context) : context_(context) {}
  ~WhiteTopNContextLink() { context_.topn_filter_msg_ = nullptr; }
  ObExprTopNFilterContext &context_;
};
}

TEST_F(NativeWhiteFilterParameters, actual_topn_callback_prepare_update_owns_native_values_and_reports_reference_oom)
{
  WhiteFilterInputs input;
  ASSERT_EQ(OB_SUCCESS, input.topn_string());
  ASSERT_NE(nullptr, input.expression_);
  ASSERT_EQ(&ObExprTopNFilter::eval_topn_filter, input.expression_->eval_func_);
  ObPushDownTopNFilterInfo details(input.owner_);
  ObSEArray<ObTopNFilterCmpMeta, 1> metas;
  ObSEArray<ObSortFieldCollation, 1> collations;
  const ObExpr &column = *input.expression_->args_[0];
  ObTopNFilterCmpMeta meta;
  meta.obj_meta_ = column.obj_meta_;
  meta.cmp_func_ = storage_predicate_functions(column)->null_last_cmp_;
  ASSERT_EQ(OB_SUCCESS, metas.push_back(meta));
  ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(0, CS_TYPE_UTF8MB4_BIN, true, NULL_LAST)));
  ASSERT_EQ(OB_SUCCESS, details.init(89, 1, 1, metas, ObP2PDatahubMsgBase::PD_TOPN_FILTER_MSG,
      0, false, false, 1, 0.5));
  ObPushDownTopNFilterMsg message;
  message.set_msg_type(ObP2PDatahubMsgBase::PD_TOPN_FILTER_MSG);
  ASSERT_EQ(OB_SUCCESS, message.init(&details, &collations, &input.exec_, 1, false));
  ASSERT_NO_FATAL_FAILURE(replace_topn_string(message, std::string("m\0z", 3)));
  ObExprTopNFilterContext *context = nullptr;
  ASSERT_EQ(OB_SUCCESS, input.exec_.create_expr_op_ctx(input.expression_->expr_ctx_id_, context));
  ASSERT_NE(nullptr, context);
  WhiteTopNContextLink link(*context);
  context->topn_filter_msg_ = &message;
  context->state_ = ObExprTopNFilterContext::FilterState::ENABLE;
  WhitePredicateOwner owner;
  ObPushdownDynamicFilterNode node(input.owner_);
  node.expr_ = input.expression_; node.set_col_idx(0); node.set_dynamic_filter_type(PD_TOPN_FILTER);
  ObDynamicFilterExecutor filter(owner, node, input.push_);
  filter.set_type(DYNAMIC_FILTER_EXECUTOR);
  ASSERT_EQ(OB_SUCCESS, (filter.*white_member(WhiteDynamicPrepare{}))());
  ASSERT_TRUE(filter.is_data_prepared()); ASSERT_EQ(1, filter.get_datums().count());
  EXPECT_EQ(ObVarcharType, filter.get_min_param().get_type());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, filter.get_min_param().get_collation_type());
  EXPECT_EQ(std::string("m\0z", 3), std::string(filter.get_min_param().get_string().ptr(), 3));
  const char *first_owned = filter.get_min_param().get_string().ptr();
  ASSERT_NO_FATAL_FAILURE(replace_topn_string(message, "h"));
  // Message replacement cannot mutate an already published predicate.
  EXPECT_EQ(std::string("m\0z", 3), std::string(first_owned, 3));
  ASSERT_EQ(OB_SUCCESS, (filter.*white_member(WhiteDynamicUpdate{}))());
  EXPECT_EQ("h", std::string(filter.get_min_param().get_string().ptr(), 1));
  std::string low("a"), high("z");
  ObDatum probe; probe.set_string(low.data(), low.size());
  ASSERT_NO_FATAL_FAILURE(expect_between(filter, probe, true));
  probe.set_string(high.data(), high.size());
  ASSERT_NO_FATAL_FAILURE(expect_between(filter, probe, false));
  ASSERT_NO_FATAL_FAILURE(replace_topn_string(message, "another-bound"));
  const int64_t calls = owner.calls_;
  owner.fail_at_ = calls + 2; // Callback copy succeeds; actual native reference capture fails.
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (filter.*white_member(WhiteDynamicUpdate{}))());
  EXPECT_EQ(calls + 2, owner.calls_); EXPECT_EQ(0, filter.get_datums().count());
  // Keep the existing error-prefix policy; retry through explicit clear and
  // prepare, rather than pretending the callback/version mutation rolled back.
  filter.clear(); EXPECT_FALSE(filter.is_data_prepared());
  const int64_t retry_calls = owner.calls_;
  owner.fail_at_ = retry_calls + 2;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (filter.*white_member(WhiteDynamicPrepare{}))());
  EXPECT_FALSE(filter.is_data_prepared()); EXPECT_EQ(0, filter.get_datums().count());
  owner.fail_at_ = -1;
  ASSERT_EQ(OB_SUCCESS, (filter.*white_member(WhiteDynamicPrepare{}))());
  EXPECT_TRUE(filter.is_data_prepared()); ASSERT_EQ(1, filter.get_datums().count());
  context->topn_filter_msg_ = nullptr;
  ASSERT_EQ(OB_SUCCESS, message.destroy());
  EXPECT_EQ("another-bound", std::string(filter.get_min_param().get_string().ptr(), 13));
  probe.set_string(low.data(), low.size());
  ASSERT_NO_FATAL_FAILURE(expect_between(filter, probe, true));
}

TEST_F(NativeWhiteFilterParameters, actual_in_clear_reinitializes_native_hash_keys_and_sorted_bounds)
{
  WhitePredicateOwner owner;
  WhiteFilterInputs input(&owner);
  ASSERT_EQ(OB_SUCCESS, input.integers(ObIntType, {7, -3, 7, 2}));
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  EXPECT_EQ(-3, input.filter_.get_min_param().get_int()); EXPECT_EQ(7, input.filter_.get_max_param().get_int());
  input.filter_.clear();
  EXPECT_EQ(0, input.filter_.get_datums().count());
  int64_t key = 7;
  ObDatum probe(reinterpret_cast<const char *>(&key), 8, false);
  bool found = true;
  ASSERT_EQ(OB_SUCCESS, input.filter_.exist_in_set(probe, found)); EXPECT_FALSE(found);
  const int64_t calls = owner.calls_;
  owner.fail_ = true;
  ASSERT_EQ(OB_SUCCESS, input.initialize());
  EXPECT_EQ(calls, owner.calls_);
  ASSERT_NO_FATAL_FAILURE(expect_membership(input.filter_, probe, true));
  int64_t position = -1;
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, input.filter_.lower_bound_param(probe, position, equal));
  EXPECT_EQ(2, position); EXPECT_TRUE(equal);
  const char truncated = 7;
  ObDatum invalid(&truncated, 1, false);
  found = true;
  EXPECT_EQ(OB_INVALID_ARGUMENT, input.filter_.exist_in_set(invalid, found));
  EXPECT_FALSE(found);
}
