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

#include <gtest/gtest.h>
#include <limits>

#include "common/object/ob_obj_compare.h"

namespace oceanbase
{
namespace common
{
namespace
{

const ObCmpOp BOOL_CMP_OPS[] = {CO_EQ, CO_LE, CO_LT, CO_GE, CO_GT, CO_NE};

bool eval_cmp_op(const int cmp, const ObCmpOp op)
{
  bool result = false;
  switch (op) {
    case CO_EQ: result = (0 == cmp); break;
    case CO_LE: result = (cmp <= 0); break;
    case CO_LT: result = (cmp < 0); break;
    case CO_GE: result = (cmp >= 0); break;
    case CO_GT: result = (cmp > 0); break;
    case CO_NE: result = (0 != cmp); break;
    default: break;
  }
  return result;
}

void expect_bool_op(const ObObj &lhs,
                    const ObObj &rhs,
                    const ObCompareCtx &cmp_ctx,
                    const ObCmpOp op,
                    const bool expected)
{
  SCOPED_TRACE(::testing::Message() << "op=" << static_cast<int>(op));
  ObObj result;
  bool need_cast = true;
  ASSERT_EQ(OB_SUCCESS,
            ObObjCmpFuncs::compare(result, lhs, rhs, cmp_ctx, op, need_cast));
  ASSERT_FALSE(need_cast);
  ASSERT_FALSE(result.is_null());
  EXPECT_EQ(expected, result.is_true());
}

void expect_relation(const ObObj &lhs,
                     const ObObj &rhs,
                     const ObCompareCtx &cmp_ctx,
                     const int expected_cmp)
{
  ASSERT_TRUE(-1 == expected_cmp || 0 == expected_cmp || 1 == expected_cmp);
  for (const ObCmpOp op : BOOL_CMP_OPS) {
    expect_bool_op(lhs, rhs, cmp_ctx, op, eval_cmp_op(expected_cmp, op));
  }

  ObObj result;
  bool need_cast = true;
  ASSERT_EQ(OB_SUCCESS,
            ObObjCmpFuncs::compare(result, lhs, rhs, cmp_ctx, CO_CMP, need_cast));
  ASSERT_FALSE(need_cast);
  ASSERT_TRUE(result.is_int32());
  EXPECT_EQ(expected_cmp, result.get_int32());
}

void expect_ieee_unordered(const ObObj &lhs,
                           const ObObj &rhs,
                           const ObCompareCtx &cmp_ctx)
{
  // Boolean mixed integer/real comparisons deliberately retain native IEEE
  // unordered behavior.  CO_CMP has separate legacy semantics and is not part
  // of this invariant.
  for (const ObCmpOp op : BOOL_CMP_OPS) {
    expect_bool_op(lhs, rhs, cmp_ctx, op, CO_NE == op);
  }
}

void expect_null_result(const ObObj &lhs,
                        const ObObj &rhs,
                        const ObCompareCtx &cmp_ctx)
{
  const ObCmpOp ops[] = {CO_EQ, CO_LE, CO_LT, CO_GE, CO_GT, CO_NE, CO_CMP};
  for (const ObCmpOp op : ops) {
    SCOPED_TRACE(::testing::Message() << "op=" << static_cast<int>(op));
    ObObj result;
    result.set_int(1);
    bool need_cast = true;
    ASSERT_EQ(OB_SUCCESS,
              ObObjCmpFuncs::compare(result, lhs, rhs, cmp_ctx, op, need_cast));
    ASSERT_FALSE(need_cast);
    EXPECT_TRUE(result.is_null());
  }
}

TEST(TestObObjCompare, integer_relations_and_boundaries)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  ObObj lhs;
  ObObj rhs;

  lhs.set_int(std::numeric_limits<int64_t>::min());
  rhs.set_int(std::numeric_limits<int64_t>::max());
  expect_relation(lhs, rhs, cmp_ctx, -1);

  lhs.set_int(std::numeric_limits<int64_t>::max());
  rhs.set_int(std::numeric_limits<int64_t>::max());
  expect_relation(lhs, rhs, cmp_ctx, 0);

  lhs.set_int(std::numeric_limits<int64_t>::max());
  rhs.set_int(std::numeric_limits<int64_t>::min());
  expect_relation(lhs, rhs, cmp_ctx, 1);

  lhs.set_int(-1);
  rhs.set_uint64(0);
  expect_relation(lhs, rhs, cmp_ctx, -1);

  lhs.set_int(std::numeric_limits<int64_t>::max());
  rhs.set_uint64(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  expect_relation(lhs, rhs, cmp_ctx, 0);

  lhs.set_uint64(std::numeric_limits<uint64_t>::max());
  rhs.set_int(std::numeric_limits<int64_t>::max());
  expect_relation(lhs, rhs, cmp_ctx, 1);
}

TEST(TestObObjCompare, same_type_real_nan_total_order)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  ObObj finite;
  ObObj nan;

  finite.set_float(0.0F);
  nan.set_float(std::numeric_limits<float>::quiet_NaN());
  expect_relation(nan, nan, cmp_ctx, 0);
  expect_relation(nan, finite, cmp_ctx, 1);
  expect_relation(finite, nan, cmp_ctx, -1);

  finite.set_double(0.0);
  nan.set_double(std::numeric_limits<double>::quiet_NaN());
  expect_relation(nan, nan, cmp_ctx, 0);
  expect_relation(nan, finite, cmp_ctx, 1);
  expect_relation(finite, nan, cmp_ctx, -1);

  ObObj negative_infinity;
  ObObj positive_infinity;
  negative_infinity.set_double(-std::numeric_limits<double>::infinity());
  positive_infinity.set_double(std::numeric_limits<double>::infinity());
  expect_relation(negative_infinity, positive_infinity, cmp_ctx, -1);

  ObObj negative_zero;
  ObObj positive_zero;
  negative_zero.set_double(-0.0);
  positive_zero.set_double(0.0);
  expect_relation(negative_zero, positive_zero, cmp_ctx, 0);
}

TEST(TestObObjCompare, mixed_integer_double_nan_is_unordered)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  ObObj integer;
  ObObj nan;
  integer.set_int(0);
  nan.set_double(std::numeric_limits<double>::quiet_NaN());

  expect_ieee_unordered(integer, nan, cmp_ctx);
  expect_ieee_unordered(nan, integer, cmp_ctx);
}

TEST(TestObObjCompare, fixed_double_uses_declared_scale)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  ObObj lhs;
  ObObj near_rhs;
  ObObj far_rhs;
  lhs.set_double(1.000);
  near_rhs.set_double(1.004);
  far_rhs.set_double(1.006);
  lhs.set_scale(2);
  near_rhs.set_scale(2);
  far_rhs.set_scale(2);

  expect_relation(lhs, near_rhs, cmp_ctx, 0);
  expect_relation(lhs, far_rhs, cmp_ctx, -1);
  expect_relation(far_rhs, lhs, cmp_ctx, 1);
}

TEST(TestObObjCompare, string_collations_and_trailing_spaces)
{
  ObObj lhs;
  ObObj rhs;

  lhs.set_varchar("abc");
  rhs.set_varchar("abd");
  lhs.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  rhs.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  const ObCompareCtx binary_ctx(ObMaxType, CS_TYPE_UTF8MB4_BIN, true,
                                INVALID_TZ_OFF, NULL_FIRST);
  expect_relation(lhs, rhs, binary_ctx, -1);

  lhs.set_varchar("abc");
  rhs.set_varchar("ABC");
  lhs.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  rhs.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  const ObCompareCtx general_ci_ctx(ObMaxType, CS_TYPE_UTF8MB4_GENERAL_CI, true,
                                    INVALID_TZ_OFF, NULL_FIRST);
  expect_relation(lhs, rhs, general_ci_ctx, 0);

  lhs.set_varchar("abc");
  rhs.set_varchar("abc ");
  lhs.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  rhs.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  expect_relation(lhs, rhs, general_ci_ctx, 0);
}

TEST(TestObObjCompare, non_null_safe_comparison_propagates_null)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, false,
                             INVALID_TZ_OFF, NULL_FIRST);
  ObObj null_value;
  ObObj int_value;
  null_value.set_null();
  int_value.set_int(1);

  expect_null_result(null_value, null_value, cmp_ctx);
  expect_null_result(null_value, int_value, cmp_ctx);
  expect_null_result(int_value, null_value, cmp_ctx);
}

TEST(TestObObjCompare, null_safe_order_and_extend_sentinels)
{
  ObObj null_value;
  ObObj int_value;
  ObObj min_value;
  ObObj max_value;
  null_value.set_null();
  int_value.set_int(1);
  min_value.set_min_value();
  max_value.set_max_value();

  const ObCompareCtx null_first_ctx(ObMaxType, CS_TYPE_INVALID, true,
                                    INVALID_TZ_OFF, NULL_FIRST);
  expect_relation(null_value, null_value, null_first_ctx, 0);
  expect_relation(null_value, int_value, null_first_ctx, -1);
  expect_relation(int_value, null_value, null_first_ctx, 1);
  expect_relation(min_value, int_value, null_first_ctx, -1);
  expect_relation(int_value, max_value, null_first_ctx, -1);

  const ObCompareCtx null_last_ctx(ObMaxType, CS_TYPE_INVALID, true,
                                   INVALID_TZ_OFF, NULL_LAST);
  expect_relation(null_value, int_value, null_last_ctx, 1);
  expect_relation(int_value, null_value, null_last_ctx, -1);
  expect_relation(min_value, null_value, null_last_ctx, -1);
  expect_relation(null_value, max_value, null_last_ctx, -1);
}

} // namespace
} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
