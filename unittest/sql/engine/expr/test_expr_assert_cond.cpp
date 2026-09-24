/*
 * Copyright (c) 2026 OceanBase.
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

#include <gtest/gtest.h>
#include <csignal>
#include "lib/allocator/page_arena.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_quarter.h"
#include "sql/engine/expr/ob_expr_date_add.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/expr/ob_expr_vec_ivf_pq_center_vector.h"
#include "sql/engine/expr/ob_expr_vec_vid.h"
#include "sql/engine/sort/ob_sort_op_impl.h"
#include "sql/resolver/expr/ob_raw_expr.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

TEST(ExprAssertCond, valid_parameters_install_evaluator)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  ObExprQuarter op(allocator);
  ObOpRawExpr raw;
  ObExpr arg, expr;
  ObExpr *args[] = {&arg};
  expr.arg_cnt_ = 1;
  expr.args_ = args;
  EXPECT_EQ(OB_SUCCESS, op.cg_expr(ctx, raw, expr));
  EXPECT_EQ(&ObExprQuarter::calc_quater, expr.eval_func_);
}

TEST(ExprAssertCond, resolver_rejects_invalid_arity_before_codegen)
{
  ObExprOperatorFactory::register_expr_operators();
  ObArenaAllocator allocator;
  ObSysFunRawExpr raw(allocator);
  raw.set_expr_type(T_FUN_SYS_QUARTER);
  raw.set_func_name(ObString::make_string("quarter"));
  EXPECT_EQ(OB_ERR_PARAM_SIZE, raw.check_param_num(0));
  EXPECT_EQ(OB_SUCCESS, raw.check_param_num(1));
  EXPECT_EQ(OB_ERR_PARAM_SIZE, raw.check_param_num(2));
}

TEST(ExprAssertCond, stricter_than_declared_arity_still_returns_error)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  // MORE_THAN_ZERO does not establish the one-or-four argument restriction.
  ObExprVecIVFPQCenterVector op(allocator);
  ObOpRawExpr raw;
  ObExpr expr;
  expr.arg_cnt_ = 2;
  expr.args_ = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, op.cg_expr(ctx, raw, expr));
  EXPECT_EQ(nullptr, expr.eval_func_);
}

TEST(ExprAssertCond, uninitialized_public_sort_still_returns_error)
{
  ObSortOpImpl sorter;
  EXPECT_EQ(OB_NOT_INIT, sorter.sort());
}

TEST(ExprAssertCond, optional_zero_argument_array_remains_valid)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  ObExprVecVid op(allocator);
  ObOpRawExpr raw;
  ObExpr expr;
  expr.arg_cnt_ = 0;
  expr.args_ = nullptr;
  EXPECT_EQ(OB_SUCCESS, op.cg_expr(ctx, raw, expr));
  EXPECT_EQ(&ObExprVecVid::generate_vec_id, expr.eval_func_);
}

#if GTEST_HAS_DEATH_TEST && !defined(_WIN32)
TEST(ExprAssertCondDeathTest, invalid_internal_arity_terminates)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  ObExprQuarter op(allocator);
  ObOpRawExpr raw;
  ObExpr expr;
  expr.arg_cnt_ = 0;
  expr.args_ = nullptr;
  EXPECT_EXIT(op.cg_expr(ctx, raw, expr), ::testing::KilledBySignal(SIGABRT),
              "ASSERT_COND failed: condition=rt_expr.arg_cnt_ == 1");
}

TEST(ExprAssertCondDeathTest, missing_argument_array_terminates)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  ObExprQuarter op(allocator);
  ObOpRawExpr raw;
  ObExpr expr;
  expr.arg_cnt_ = 1;
  expr.args_ = nullptr;
  EXPECT_EXIT(op.cg_expr(ctx, raw, expr), ::testing::KilledBySignal(SIGABRT),
              "ASSERT_COND failed:.*rt_expr.args_");
}

TEST(ExprAssertCondDeathTest, missing_child_terminates)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  ObExprQuarter op(allocator);
  ObOpRawExpr raw;
  ObExpr expr;
  ObExpr *args[] = {nullptr};
  expr.arg_cnt_ = 1;
  expr.args_ = args;
  EXPECT_EXIT(op.cg_expr(ctx, raw, expr), ::testing::KilledBySignal(SIGABRT),
              "ASSERT_COND failed");
}

TEST(ExprAssertCondDeathTest, chained_checks_stop_at_missing_array)
{
  ObArenaAllocator allocator;
  ObExprCGCtx ctx(allocator, nullptr, nullptr);
  ObExprDateAdd op(allocator);
  ObOpRawExpr raw;
  ObExpr expr;
  expr.arg_cnt_ = 3;
  expr.args_ = nullptr;
  EXPECT_EXIT(op.cg_expr(ctx, raw, expr), ::testing::KilledBySignal(SIGABRT),
              "ASSERT_COND failed: condition=rt_expr.args_ != nullptr");
}
#endif
