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
#include "sql/resolver/expr/ob_raw_expr.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
class ParamExprAllocator : public ObIAllocator
{
public:
  void *alloc(const int64_t size) override
  {
    ++calls_;
    return !fail_ && size > 0 && size <= sizeof(buffer_) ? buffer_ : nullptr;
  }
  void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}

  bool fail_ = false;
  int calls_ = 0;
  alignas(ObRawExpr *) char buffer_[3 * sizeof(ObRawExpr *)];
};

int set_params(ObOpRawExpr &expr, int count, ObRawExpr *first,
               ObRawExpr *second, ObRawExpr *third)
{
  return count == 1 ? expr.set_param_expr(first)
       : count == 2 ? expr.set_param_exprs(first, second)
                    : expr.set_param_exprs(first, second, third);
}
}

TEST(RawExprAssertSucc, inserts_and_reuses_reserved_pointer_slots)
{
  ObConstRawExpr first, second, third;
  ObRawExpr *params[] = {&first, &second, &third};
  for (int count = 1; count <= 3; ++count) {
    ParamExprAllocator allocator;
    ObOpRawExpr expr(allocator);
    expr.set_expr_type(T_OP_ADD);
    ASSERT_EQ(OB_SUCCESS, set_params(expr, count, &first, &second, &third));
    ASSERT_EQ(count, expr.get_param_count());
    EXPECT_EQ(1, allocator.calls_);
    for (int i = 0; i < count; ++i) {
      EXPECT_EQ(params[i], expr.get_param_expr(i));
    }
    EXPECT_EQ(OB_ERR_UNEXPECTED, set_params(expr, count, &first, &second, &third));
    EXPECT_EQ(count, expr.get_param_count());

    expr.reuse_child();
    allocator.fail_ = true; // Reusing reserved slots must not allocate.
    ASSERT_EQ(OB_SUCCESS, set_params(expr, count, &third, &second, &first));
    EXPECT_EQ(1, allocator.calls_);
    ASSERT_EQ(count, expr.get_param_count());
    EXPECT_EQ(&third, expr.get_param_expr(0));
    if (count == 3) {
      EXPECT_EQ(&first, expr.get_param_expr(2));
    }
  }
}

TEST(RawExprAssertSucc, propagates_allocation_failure_before_writing_params)
{
  ObConstRawExpr first, second, third;
  for (int count = 1; count <= 3; ++count) {
    ParamExprAllocator allocator;
    allocator.fail_ = true;
    ObOpRawExpr expr(allocator);
    ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, set_params(expr, count, &first, &second, &third));
    EXPECT_EQ(0, expr.get_param_count());
    EXPECT_EQ(1, allocator.calls_);
    allocator.fail_ = false;
    EXPECT_EQ(OB_SUCCESS, set_params(expr, count, &first, &second, &third));
    EXPECT_EQ(count, expr.get_param_count());
  }
}

TEST(RawExprAssertSucc, preserves_invalid_argument_and_capacity_errors)
{
  ParamExprAllocator allocator;
  ObOpRawExpr expr(allocator);
  ObConstRawExpr first, second, third;
  EXPECT_EQ(OB_INVALID_ARGUMENT, expr.set_param_exprs(nullptr, &second));
  EXPECT_EQ(OB_INVALID_ARGUMENT, expr.set_param_exprs(&first, nullptr));
  EXPECT_EQ(0, allocator.calls_);
  ASSERT_EQ(OB_SUCCESS, expr.init_param_exprs(1));
  EXPECT_EQ(OB_SIZE_OVERFLOW, expr.set_param_exprs(&first, &second));
  EXPECT_EQ(OB_SIZE_OVERFLOW, expr.set_param_exprs(&first, &second, &third));
  EXPECT_EQ(0, expr.get_param_count());
  EXPECT_EQ(1, allocator.calls_);
}
