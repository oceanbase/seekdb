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

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_se_array.h"
#include "sql/engine/aggregate/ob_pushdown_aggregate_program.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
namespace
{

class ExactCountSegment final
  : public share::aggregate::ObIAggregateInputSegment
{
public:
  explicit ExactCountSegment(const int64_t row_count)
    : selection_(), row_count_(row_count)
  {
    selection_.count_ = row_count;
  }

  const share::aggregate::ObAggregateSelectionView &selection() const override
  {
    return selection_;
  }

  int can_read_values(
      const share::aggregate::ObAggregateInputSlot slot,
      bool &can_read) const override
  {
    can_read = 0 == slot;
    return can_read ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }

  int try_reduce(
      const share::aggregate::ObAggregateInputSlot slot,
      const uint32_t requested,
      share::aggregate::ObAggregateReduction &reduction) override
  {
    int ret = OB_SUCCESS;
    if (0 != slot) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      reduction = share::aggregate::ObAggregateReduction();
      reduction.present_ = requested;
      reduction.row_count_ = row_count_;
    }
    return ret;
  }

  int read_values(
      const share::aggregate::ObAggregateInputSlot slot,
      share::aggregate::ObAggregateValueBatchView &values) override
  {
    UNUSEDx(slot, values);
    return OB_NOT_SUPPORTED;
  }

  int try_dictionary(
      const share::aggregate::ObAggregateInputSlot slot,
      share::aggregate::ObAggregateDictionaryView &dictionary) override
  {
    UNUSEDx(slot, dictionary);
    return OB_NOT_SUPPORTED;
  }

private:
  share::aggregate::ObAggregateSelectionView selection_;
  int64_t row_count_;
};

TEST(PushdownAggregatePlan, ProgramsOwnIndependentScanState)
{
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  ObExpr count_star;
  count_star.type_ = T_FUN_COUNT;
  count_star.arg_cnt_ = 0;
  common::ObSEArray<ObExpr *, 1> aggregate_exprs;
  ASSERT_EQ(OB_SUCCESS, aggregate_exprs.push_back(&count_star));

  share::aggregate::ObIPushdownAggregatePlan *plan = nullptr;
  ASSERT_EQ(OB_SUCCESS, create_pushdown_aggregate_plan(
      eval_ctx, aggregate_exprs, false, allocator, plan));
  ASSERT_NE(nullptr, plan);

  share::aggregate::ObIPushdownAggregateProgram *first = nullptr;
  share::aggregate::ObIPushdownAggregateProgram *second = nullptr;
  ASSERT_EQ(OB_SUCCESS, plan->create_program(first));
  ASSERT_EQ(OB_SUCCESS, plan->create_program(second));
  ASSERT_NE(nullptr, first);
  ASSERT_NE(nullptr, second);
  EXPECT_NE(first, second);

  ExactCountSegment first_segment(3);
  ASSERT_EQ(OB_SUCCESS, first->consume(first_segment));
  ASSERT_EQ(OB_SUCCESS, first->seal());
  EXPECT_EQ(share::aggregate::AGG_PROGRAM_SEALED, first->state());
  EXPECT_EQ(share::aggregate::AGG_PROGRAM_NEW, second->state());

  ExactCountSegment second_segment(5);
  bool can_consume = false;
  ASSERT_EQ(OB_SUCCESS, second->can_consume(second_segment, can_consume));
  ASSERT_TRUE(can_consume);
  ASSERT_EQ(OB_SUCCESS, second->consume(second_segment));
  EXPECT_EQ(share::aggregate::AGG_PROGRAM_CONSUMING, second->state());

  ASSERT_EQ(OB_SUCCESS, first->reset_scan());
  EXPECT_EQ(share::aggregate::AGG_PROGRAM_NEW, first->state());
  EXPECT_EQ(share::aggregate::AGG_PROGRAM_CONSUMING, second->state());

  first->destroy();
  second->destroy();
  destroy_pushdown_aggregate_plan(plan);
}

} // namespace
} // namespace sql
} // namespace oceanbase
