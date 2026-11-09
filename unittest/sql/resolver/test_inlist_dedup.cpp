/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include "sql/resolver/dml/ob_inlist_resolver.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/ob_sql_context.h"
#include "sql/plan_cache/ob_plan_set.h"
#include "sql/parser/parse_define.h"
#include "sql/plan_cache/ob_sql_parameterization.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/optimizer/ob_select_log_plan.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
ObPCParamEqualInfo equal_constraint(int64_t first, int64_t second, bool use_abs_cmp = false)
{
  ObPCParamEqualInfo result;
  result.first_param_idx_ = first;
  result.second_param_idx_ = second;
  result.use_abs_cmp_ = use_abs_cmp;
  return result;
}

class FailingConstraintAllocator : public ObWrapperAllocator
{
public:
  explicit FailingConstraintAllocator(ObIAllocator &allocator)
      : ObWrapperAllocator(allocator), fail_(false) {}
  void *alloc(const int64_t size) override
  { return fail_ ? NULL : ObWrapperAllocator::alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &attr) override
  { return fail_ ? NULL : ObWrapperAllocator::alloc(size, attr); }
  bool fail_;
};

class InListFixture
{
public:
  InListFixture() : factory_(allocator_), ctx_(factory_, NULL, OB_ORIGIN_AND_INSENSITIVE),
                    params_(ObWrapperAllocator(allocator_))
  {
    ctx_.session_info_ = &session_;
    ctx_.query_ctx_ = &query_ctx_;
    ctx_.param_list_ = &params_;
    ctx_.current_scope_ = T_WHERE_SCOPE;
    ctx_.stmt_ = &stmt_;
    stmt_.set_query_ctx(&query_ctx_);
  }
  ParseNode *node(ObItemType type, int64_t value = 0, int32_t count = 0)
  {
    ParseNode *result = new_node(&allocator_, type, count);
    EXPECT_NE(nullptr, result);
    if (NULL != result) { result->value_ = value; }
    return result;
  }
  ParseNode *list(std::initializer_list<ParseNode *> children)
  {
    ParseNode *result = node(T_EXPR_LIST, 0, children.size());
    int64_t i = 0;
    for (ParseNode *child : children) { result->children_[i++] = child; }
    return result;
  }
  ParseNode *integer(int64_t value)
  {
    ParseNode *result = node(T_INT, value);
    result->str_len_ = std::to_string(value).size();
    return result;
  }
  ParseNode *parameter(int64_t value)
  {
    ObObjParam param;
    param.set_int(value);
    param.set_param_meta();
    EXPECT_EQ(OB_SUCCESS, params_.push_back(param));
    return node(T_QUESTIONMARK, params_.count() - 1);
  }
  void threshold(int64_t value)
  {
    ObObj bound;
    bound.set_int(value);
    EXPECT_EQ(OB_SUCCESS, query_ctx_.get_query_hint_for_update().get_global_hint().opt_params_.
        add_opt_param_hint(ObOptParamHint::INLIST_REWRITE_THRESHOLD, bound));
  }
  const ParseNode *compact(const ParseNode *input, ObItemType type = T_OP_IN)
  {
    const ParseNode *result = NULL;
    EXPECT_EQ(OB_SUCCESS, ObInListResolver::try_deduplicate_inlist(
        ctx_, type, column_, true, input, result));
    return result;
  }
  void retained(ParseNode *input)
  {
    EXPECT_EQ(input, compact(input));
    EXPECT_EQ(0, query_ctx_.all_equal_param_constraints_.count());
  }
  int remove_duplicate_constraints(ObIAllocator &allocator)
  {
    ObAddr addr;
    ObOptimizerContext optimizer_ctx(&session_, NULL, NULL, NULL, allocator,
        &params_, addr, query_ctx_.get_global_hint(), factory_, &stmt_, false, &query_ctx_);
    ObSelectLogPlan plan(optimizer_ctx, &stmt_);
    return plan.remove_duplicate_constraints();
  }
  int remove_duplicate_constraints() { return remove_duplicate_constraints(allocator_); }
  ObArenaAllocator allocator_;
  ObRawExprFactory factory_;
  ObExprResolveContext ctx_;
  ParamStore params_;
  ObSQLSessionInfo session_;
  ObQueryCtx query_ctx_;
  ObSelectStmt stmt_;
  ObColumnRefRawExpr column_; // Deliberately unbound, like the real entrance.
};
class InListDedupTest : public ::testing::Test, public InListFixture {};

TEST_F(InListDedupTest, EqualConstraintHashMatchesSymmetricEquality)
{
  const int64_t indices[] = {0, 1, 17, std::numeric_limits<int64_t>::max()};
  for (int64_t first : indices) {
    for (int64_t second : indices) {
      for (bool use_abs_cmp : {false, true}) {
        const ObPCParamEqualInfo forward = equal_constraint(first, second, use_abs_cmp);
        const ObPCParamEqualInfo reverse = equal_constraint(second, first, use_abs_cmp);
        uint64_t forward_hash = 0;
        uint64_t reverse_hash = 1;
        ASSERT_EQ(OB_SUCCESS, forward.hash(forward_hash));
        ASSERT_EQ(OB_SUCCESS, reverse.hash(reverse_hash));
        EXPECT_TRUE(forward == reverse);
        EXPECT_EQ(forward_hash, reverse_hash);
        EXPECT_FALSE(forward == equal_constraint(first, second, !use_abs_cmp));
      }
    }
  }
}

TEST_F(InListDedupTest, EqualConstraintsKeepFirstOccurrenceAndComparisonMode)
{
  const ObPCParamEqualInfo expected[] = {
    equal_constraint(9, 2), equal_constraint(4, 7),
    equal_constraint(9, 2, true), equal_constraint(5, 5)
  };
  for (const ObPCParamEqualInfo &constraint : {
      expected[0], expected[1], equal_constraint(2, 9), equal_constraint(7, 4),
      expected[2], equal_constraint(2, 9, true), expected[3], expected[3]}) {
    ASSERT_EQ(OB_SUCCESS, query_ctx_.all_equal_param_constraints_.push_back(constraint));
  }
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints());
  ASSERT_EQ(4, query_ctx_.all_equal_param_constraints_.count());
  for (int64_t i = 0; i < 4; ++i) {
    const ObPCParamEqualInfo &actual = query_ctx_.all_equal_param_constraints_.at(i);
    EXPECT_EQ(expected[i].first_param_idx_, actual.first_param_idx_);
    EXPECT_EQ(expected[i].second_param_idx_, actual.second_param_idx_);
    EXPECT_EQ(expected[i].use_abs_cmp_, actual.use_abs_cmp_);
  }
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints());
  EXPECT_EQ(4, query_ctx_.all_equal_param_constraints_.count());
}

TEST_F(InListDedupTest, EqualConstraintsPreserveThousandsOfUniqueGuardsAndExistingPrefix)
{
  std::vector<ObPCParamEqualInfo> expected{
    equal_constraint(100, 3), equal_constraint(8, 9, true), equal_constraint(4, 4)
  };
  for (int64_t i = 0; i < 8192; ++i) {
    expected.push_back(equal_constraint(i % 3, i + 1000, i % 2 != 0));
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    ASSERT_EQ(OB_SUCCESS, query_ctx_.all_equal_param_constraints_.push_back(expected[i]));
    if (i % 11 == 0) {
      ASSERT_EQ(OB_SUCCESS, query_ctx_.all_equal_param_constraints_.push_back(
          equal_constraint(expected[i].second_param_idx_, expected[i].first_param_idx_,
                           expected[i].use_abs_cmp_)));
    }
  }
  for (int64_t i = 2; i >= 0; --i) {
    ASSERT_EQ(OB_SUCCESS, query_ctx_.all_equal_param_constraints_.push_back(expected[i]));
  }
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints());
  ASSERT_EQ(expected.size(), query_ctx_.all_equal_param_constraints_.count());
  for (size_t i = 0; i < expected.size(); ++i) {
    const ObPCParamEqualInfo &actual = query_ctx_.all_equal_param_constraints_.at(i);
    EXPECT_EQ(expected[i].first_param_idx_, actual.first_param_idx_);
    EXPECT_EQ(expected[i].second_param_idx_, actual.second_param_idx_);
    EXPECT_EQ(expected[i].use_abs_cmp_, actual.use_abs_cmp_);
  }
}

TEST_F(InListDedupTest, EqualConstraintAllocationFailurePreservesOriginalGuards)
{
  const ObPCParamEqualInfo expected[] = {
    equal_constraint(9, 2), equal_constraint(2, 9), equal_constraint(5, 6, true)
  };
  for (const ObPCParamEqualInfo &constraint : expected) {
    ASSERT_EQ(OB_SUCCESS, query_ctx_.all_equal_param_constraints_.push_back(constraint));
  }
  FailingConstraintAllocator allocator(allocator_);
  allocator.fail_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, remove_duplicate_constraints(allocator));
  ASSERT_EQ(3, query_ctx_.all_equal_param_constraints_.count());
  for (int64_t i = 0; i < 3; ++i) {
    const ObPCParamEqualInfo &actual = query_ctx_.all_equal_param_constraints_.at(i);
    EXPECT_EQ(expected[i].first_param_idx_, actual.first_param_idx_);
    EXPECT_EQ(expected[i].second_param_idx_, actual.second_param_idx_);
    EXPECT_EQ(expected[i].use_abs_cmp_, actual.use_abs_cmp_);
  }
  allocator.fail_ = false;
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints(allocator));
  EXPECT_EQ(2, query_ctx_.all_equal_param_constraints_.count());
}

TEST_F(InListDedupTest, EmptyAndSingletonEqualConstraintsNeedNoAllocation)
{
  FailingConstraintAllocator allocator(allocator_);
  allocator.fail_ = true;
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints(allocator));
  ASSERT_EQ(OB_SUCCESS, query_ctx_.all_equal_param_constraints_.push_back(equal_constraint(7, 3)));
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints(allocator));
  ASSERT_EQ(1, query_ctx_.all_equal_param_constraints_.count());
  EXPECT_EQ(7, query_ctx_.all_equal_param_constraints_.at(0).first_param_idx_);
  EXPECT_EQ(3, query_ctx_.all_equal_param_constraints_.at(0).second_param_idx_);
}

TEST_F(InListDedupTest, Issue1512Shuffled3072IntegerLiterals)
{
  threshold(1000);
  std::vector<int64_t> values;
  for (int64_t i = 0; i < 1024; ++i) { for (int64_t v : {1, 2, 3}) { values.push_back(v); } }
  std::shuffle(values.begin(), values.end(), std::mt19937(1024));
  ParseNode *input = node(T_EXPR_LIST, 0, values.size());
  for (size_t i = 0; i < values.size(); ++i) { input->children_[i] = integer(values[i]); }
  const ParseNode *result = compact(input);
  ASSERT_NE(input, result);
  ASSERT_EQ(3, result->num_child_);
  EXPECT_EQ(3072, input->num_child_);
  EXPECT_EQ(input->children_[0], result->children_[0]);
  EXPECT_EQ(0, query_ctx_.all_equal_param_constraints_.count());
}

// Exercise the actual ordinary-SQL fast parse / full parse / parameterization
// pipeline, rather than manufacturing ObObjParam values with test metadata.
TEST_F(InListDedupTest, OrdinarySqlAutomaticParameterizationIsEligible)
{
  // A standalone unit binary does not execute the server startup that creates
  // the shared system-variable defaults and the session cache base values.
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ASSERT_EQ(OB_SUCCESS, ObBasicSessionInfo::init_sys_vars_cache_base_values());
  ASSERT_EQ(OB_SUCCESS, session_.load_all_sys_vars_default());
  threshold(1000);
  std::string sql = "SELECT id FROM inlist_items WHERE category IN (";
  for (int64_t i = 0; i < 3072; ++i) {
    if (i > 0) { sql += ","; }
    sql += std::to_string(1 + i % 3);
  }
  sql += ")";
  ObString source(sql.size(), sql.data());
  ObString parameterized;
  ObSEArray<ObPCParam *, 16> raw_params;
  ASSERT_EQ(OB_SUCCESS, ObSqlParameterization::raw_fast_parameterize_sql(
      allocator_, session_, source, parameterized, raw_params));
  ASSERT_EQ(3072, raw_params.count());
  ObParser parser(allocator_, session_.get_sql_mode(), session_.get_charsets4parser());
  ParseResult parsed;
  ASSERT_EQ(OB_SUCCESS, parser.parse(parameterized, parsed));
  ASSERT_NE(nullptr, parsed.result_tree_);
  SqlInfo info;
  ASSERT_EQ(OB_SUCCESS, ObSqlParameterization::transform_syntax_tree(
      allocator_, session_, &raw_params, parsed.result_tree_, info,
      params_, NULL, false, TEXT_MODE));
  ASSERT_EQ(3072, params_.count());
  for (int64_t i = 0; i < params_.count(); ++i) {
    const ObObjParam &param = params_.at(i);
    ASSERT_TRUE(param.need_to_check_type());
    ASSERT_EQ(ObIntType, param.get_type());
    ASSERT_EQ(1 + i % 3, param.get_int());
    ASSERT_TRUE(param.get_meta() == params_.at(0).get_meta());
    ASSERT_TRUE(param.get_param_meta() == params_.at(0).get_param_meta());
    ASSERT_TRUE(param.get_accuracy() == params_.at(0).get_accuracy());
  }
  std::vector<ParseNode *> pending{parsed.result_tree_};
  ParseNode *rhs = NULL;
  while (!pending.empty()) {
    ParseNode *current = pending.back();
    pending.pop_back();
    if (T_OP_IN == current->type_ && 2 == current->num_child_) {
      rhs = current->children_[1];
      break;
    }
    for (int64_t i = 0; i < current->num_child_; ++i) {
      if (NULL != current->children_[i]) { pending.push_back(current->children_[i]); }
    }
  }
  ASSERT_NE(nullptr, rhs);
  ASSERT_EQ(T_EXPR_LIST, rhs->type_);
  ASSERT_EQ(3072, rhs->num_child_);
  ASSERT_EQ(3, compact(rhs)->num_child_);
  ASSERT_EQ(3069, query_ctx_.all_equal_param_constraints_.count());
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints());
  ASSERT_EQ(3069, query_ctx_.all_equal_param_constraints_.count());
  parser.free_result(parsed);
}

TEST_F(InListDedupTest, AutomaticParametersHaveOneGuardPerRemovedValue)
{
  threshold(1000);
  ParseNode *input = node(T_EXPR_LIST, 0, 3072);
  for (int64_t i = 0; i < 3072; ++i) { input->children_[i] = parameter(1 + i % 3); }
  const ParseNode *result = compact(input);
  ASSERT_NE(input, result);
  ASSERT_EQ(3, result->num_child_);
  ASSERT_EQ(3069, query_ctx_.all_equal_param_constraints_.count());
  ASSERT_EQ(OB_SUCCESS, remove_duplicate_constraints());
  ASSERT_EQ(3069, query_ctx_.all_equal_param_constraints_.count());
  for (int64_t i = 0; i < 3069; ++i) {
    const ObPCParamEqualInfo &guard = query_ctx_.all_equal_param_constraints_.at(i);
    EXPECT_EQ(i % 3, guard.first_param_idx_);
    EXPECT_EQ(i + 3, guard.second_param_idx_);
    EXPECT_FALSE(guard.use_abs_cmp_);
  }
}

TEST_F(InListDedupTest, ThresholdAndContinuousValuesAccessFallback)
{
  threshold(3);
  ParseNode *input = list({parameter(1), parameter(2), parameter(3), parameter(1)});
  retained(input);
  for (int64_t i = 0; i < 4; ++i) { EXPECT_EQ(i, input->children_[i]->value_); }
  ParseNode *below = list({parameter(1), parameter(2), parameter(1)});
  EXPECT_EQ(2, compact(below)->num_child_);
  EXPECT_EQ(1, query_ctx_.all_equal_param_constraints_.count());
}

TEST_F(InListDedupTest, LiteralNullPreservesInAndNotInThreeValuedLogic)
{
  threshold(1000);
  ParseNode *input = list({node(T_NULL), integer(1), node(T_NULL), integer(1)});
  for (ObItemType type : {T_OP_IN, T_OP_NOT_IN}) {
    const ParseNode *result = compact(input, type);
    ASSERT_EQ(2, result->num_child_);
    EXPECT_EQ(T_NULL, result->children_[0]->type_);
    EXPECT_EQ(1, result->children_[1]->value_);
  }
}

TEST_F(InListDedupTest, SignedUnsignedBoundariesAndLiteralPrecision)
{
  threshold(1000);
  int64_t max = std::numeric_limits<int64_t>::max();
  int64_t min = std::numeric_limits<int64_t>::min();
  EXPECT_EQ(3, compact(list({integer(-1), integer(min), integer(max), integer(-1), integer(max)}))->num_child_);
  const uint64_t umax = std::numeric_limits<uint64_t>::max();
  ParseNode *u1 = node(T_UINT64, static_cast<int64_t>(umax));
  ParseNode *u2 = node(T_UINT64, static_cast<int64_t>(umax));
  u1->str_len_ = u2->str_len_ = 20;
  EXPECT_EQ(1, compact(list({u1, u2}))->num_child_);
  retained(list({integer(-1), u1, integer(-1)}));
  ParseNode *wide = integer(1);
  wide->str_len_ = 4;
  EXPECT_EQ(2, compact(list({integer(1), wide, integer(1)}))->num_child_);
}

TEST_F(InListDedupTest, UnknownNullBatchPrepareAndMixedOriginsAreRetained)
{
  threshold(1000);
  ParseNode *a = parameter(1);
  ParseNode *b = parameter(1);
  ParseNode *input = list({a, b});
  params_.at(1).set_null(); retained(input);
  params_.at(1).set_int(1);
  params_.at(1).get_param_flag().is_batch_parameter_ = true; retained(input);
  params_.at(1).get_param_flag().is_batch_parameter_ = false;
  params_.at(1).set_need_to_check_type(false); retained(input);
  params_.at(1).set_need_to_check_type(true);
  query_ctx_.set_is_prepare_stmt(true); retained(input);
  query_ctx_.set_is_prepare_stmt(false);
  ctx_.is_for_dynamic_sql_ = true; retained(input);
  ctx_.is_for_dynamic_sql_ = false;
  retained(list({a, integer(1), b}));
  retained(list({node(T_QUESTIONMARK, 123), node(T_QUESTIONMARK, 123)}));
  ctx_.param_list_ = NULL; retained(input);
}

TEST_F(InListDedupTest, StringsExpressionsRowsAndUnsafeScopesAreRetained)
{
  threshold(1000);
  for (ObItemType type : {T_VARCHAR, T_NUMBER, T_FUN_SYS_RAND, T_OP_ADD, T_EXPR_LIST}) {
    ParseNode *child = node(type);
    retained(list({child, child}));
  }
  ParseNode *input = list({integer(1), integer(1)});
  ctx_.current_scope_ = T_FIELD_LIST_SCOPE; retained(input);
  ctx_.current_scope_ = T_WHERE_SCOPE;
  ctx_.is_need_print_ = true; retained(input);
  ctx_.is_need_print_ = false;
  const ParseNode *result = NULL;
  ASSERT_EQ(OB_SUCCESS, ObInListResolver::try_deduplicate_inlist(ctx_, T_OP_IN, column_, false, input, result));
  EXPECT_EQ(input, result);
  ObOpRawExpr row;
  row.set_expr_type(T_OP_ROW);
  ASSERT_EQ(OB_SUCCESS, ObInListResolver::try_deduplicate_inlist(ctx_, T_OP_IN, row, true, input, result));
  EXPECT_EQ(input, result);
}

TEST_F(InListDedupTest, BoundedDistinctAndChildWork)
{
  threshold(1000);
  ParseNode *input = node(T_EXPR_LIST, 0, 130);
  for (int64_t i = 0; i < 130; ++i) { input->children_[i] = integer(i % 129); }
  retained(input);
  ParseNode *too_big = node(T_EXPR_LIST, 0, 16385);
  for (int64_t i = 0; i < 16385; ++i) { too_big->children_[i] = integer(1); }
  retained(too_big);
}
} // namespace

namespace test {
// The existing cache test friend exercises production match_constraint.
TEST(TestPlanSet, basic)
{
  InListFixture fixture;
  fixture.threshold(1000);
  ParseNode *input = fixture.list({fixture.parameter(1), fixture.parameter(1), fixture.parameter(1)});
  ASSERT_EQ(1, fixture.compact(input)->num_child_);
  ASSERT_EQ(OB_SUCCESS, fixture.remove_duplicate_constraints());
  ASSERT_EQ(2, fixture.query_ctx_.all_equal_param_constraints_.count());
  ObSqlPlanSet plan_set;
  plan_set.all_equal_param_constraints_.set_allocator(&plan_set.alloc_);
  ASSERT_EQ(OB_SUCCESS, plan_set.all_equal_param_constraints_.init(2));
  for (int64_t i = 0; i < fixture.query_ctx_.all_equal_param_constraints_.count(); ++i) {
    ASSERT_EQ(OB_SUCCESS, plan_set.all_equal_param_constraints_.push_back(
        fixture.query_ctx_.all_equal_param_constraints_.at(i)));
  }
  bool matched = false;
  ASSERT_EQ(OB_SUCCESS, plan_set.match_constraint(fixture.params_, matched)); EXPECT_TRUE(matched);
  fixture.params_.at(1).set_int(2);
  ASSERT_EQ(OB_SUCCESS, plan_set.match_constraint(fixture.params_, matched)); EXPECT_FALSE(matched);
  fixture.params_.at(1).set_int(1);
  ASSERT_EQ(OB_SUCCESS, plan_set.match_constraint(fixture.params_, matched)); EXPECT_TRUE(matched);
  fixture.params_.at(2).set_int(2);
  ASSERT_EQ(OB_SUCCESS, plan_set.match_constraint(fixture.params_, matched)); EXPECT_FALSE(matched);
  fixture.params_.at(2).set_null();
  ASSERT_EQ(OB_SUCCESS, plan_set.match_constraint(fixture.params_, matched)); EXPECT_FALSE(matched);
}
} // namespace test

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
