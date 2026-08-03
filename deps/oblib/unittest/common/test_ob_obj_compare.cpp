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
#include "lib/allocator/page_arena.h"

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

void expect_bidirectional_relation(const ObObj &lhs,
                                   const ObObj &rhs,
                                   const ObCompareCtx &cmp_ctx,
                                   const int expected_cmp)
{
  expect_relation(lhs, rhs, cmp_ctx, expected_cmp);
  expect_relation(rhs, lhs, cmp_ctx, -expected_cmp);
}

void expect_comparison_error(const ObObj &lhs,
                             const ObObj &rhs,
                             const ObCompareCtx &cmp_ctx)
{
  for (const ObCmpOp op : BOOL_CMP_OPS) {
    obj_cmp_func func = nullptr;
    ASSERT_EQ(OB_SUCCESS,
              ObObjCmpFuncs::get_cmp_func(lhs.get_type_class(),
                                          rhs.get_type_class(),
                                          op,
                                          func));
    ASSERT_NE(nullptr, func);
    EXPECT_EQ(ObObjCmpFuncs::CR_OB_ERROR, func(lhs, rhs, cmp_ctx));
  }

  ObObj result;
  bool need_cast = true;
  EXPECT_EQ(OB_ERR_UNEXPECTED,
            ObObjCmpFuncs::compare(result, lhs, rhs, cmp_ctx, CO_CMP, need_cast));
  EXPECT_FALSE(need_cast);
}

template <typename T>
void set_decimal_int(ObObj &obj, T &value, const ObScale scale)
{
  obj.set_decimal_int(sizeof(T), scale, reinterpret_cast<ObDecimalInt *>(&value));
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

TEST(TestObObjCompare, decimal_int_with_different_scales)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  int32_t scale_2_value = 1234;
  int64_t equal_scale_4_value = 123400;
  int64_t greater_scale_4_value = 123401;
  int32_t negative_scale_2_value = -1234;
  int64_t smaller_scale_4_value = -123401;
  ObObj scale_2;
  ObObj equal_scale_4;
  ObObj greater_scale_4;
  ObObj negative_scale_2;
  ObObj smaller_scale_4;
  set_decimal_int(scale_2, scale_2_value, 2);
  set_decimal_int(equal_scale_4, equal_scale_4_value, 4);
  set_decimal_int(greater_scale_4, greater_scale_4_value, 4);
  set_decimal_int(negative_scale_2, negative_scale_2_value, 2);
  set_decimal_int(smaller_scale_4, smaller_scale_4_value, 4);

  expect_bidirectional_relation(scale_2, equal_scale_4, cmp_ctx, 0);
  expect_bidirectional_relation(scale_2, greater_scale_4, cmp_ctx, -1);
  expect_bidirectional_relation(negative_scale_2, smaller_scale_4, cmp_ctx, 1);
}

TEST(TestObObjCompare, decimal_int_with_integer_type_classes)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);

  int32_t equal_int_value = -420;
  int32_t smaller_int_value = -425;
  ObObj equal_decimal;
  ObObj smaller_decimal;
  ObObj int_value;
  set_decimal_int(equal_decimal, equal_int_value, 1);
  set_decimal_int(smaller_decimal, smaller_int_value, 1);
  int_value.set_int(-42);
  expect_bidirectional_relation(equal_decimal, int_value, cmp_ctx, 0);
  expect_bidirectional_relation(smaller_decimal, int_value, cmp_ctx, -1);

  int128_t max_uint_decimal_value = static_cast<int128_t>(
      std::numeric_limits<uint64_t>::max());
  int128_t above_max_uint_decimal_value = max_uint_decimal_value + 1;
  ObObj max_uint_decimal;
  ObObj above_max_uint_decimal;
  ObObj max_uint;
  set_decimal_int(max_uint_decimal, max_uint_decimal_value, 0);
  set_decimal_int(above_max_uint_decimal, above_max_uint_decimal_value, 0);
  max_uint.set_uint64(std::numeric_limits<uint64_t>::max());
  expect_bidirectional_relation(max_uint_decimal, max_uint, cmp_ctx, 0);
  expect_bidirectional_relation(above_max_uint_decimal, max_uint, cmp_ctx, 1);

  int32_t negative_decimal_value = -1;
  ObObj negative_decimal;
  ObObj zero_uint;
  set_decimal_int(negative_decimal, negative_decimal_value, 0);
  zero_uint.set_uint64(0);
  expect_bidirectional_relation(negative_decimal, zero_uint, cmp_ctx, -1);

  int32_t equal_enum_value = 700;
  int32_t greater_enum_value = 701;
  ObObj equal_enum_decimal;
  ObObj greater_enum_decimal;
  ObObj enum_value;
  set_decimal_int(equal_enum_decimal, equal_enum_value, 2);
  set_decimal_int(greater_enum_decimal, greater_enum_value, 2);
  enum_value.set_enum(7);
  expect_bidirectional_relation(equal_enum_decimal, enum_value, cmp_ctx, 0);
  expect_bidirectional_relation(greater_enum_decimal, enum_value, cmp_ctx, 1);
}

TEST(TestObObjCompare, decimal_int_with_number_type_class)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  ObArenaAllocator allocator;
  number::ObNumber equal_number;
  number::ObNumber smaller_number;
  ASSERT_EQ(OB_SUCCESS, equal_number.from("12.3400", allocator));
  ASSERT_EQ(OB_SUCCESS, smaller_number.from("-12.35", allocator));

  int32_t positive_decimal_value = 1234;
  int32_t negative_decimal_value = -1234;
  ObObj positive_decimal;
  ObObj negative_decimal;
  ObObj equal_number_obj;
  ObObj smaller_number_obj;
  set_decimal_int(positive_decimal, positive_decimal_value, 2);
  set_decimal_int(negative_decimal, negative_decimal_value, 2);
  equal_number_obj.set_number(equal_number);
  smaller_number_obj.set_number(smaller_number);

  expect_bidirectional_relation(positive_decimal, equal_number_obj, cmp_ctx, 0);
  expect_bidirectional_relation(negative_decimal, smaller_number_obj, cmp_ctx, 1);
}

TEST(TestObObjCompare, enumset_inner_with_decimal_int)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  char serialized[128] = {};
  int64_t pos = 0;
  ObString display_value("seven");
  ObEnumSetInnerValue inner_value(7, display_value);
  ASSERT_EQ(OB_SUCCESS,
            inner_value.serialize(serialized, sizeof(serialized), pos));

  ObObj enum_inner;
  enum_inner.set_enum_inner(serialized,
                            static_cast<ObString::obstr_size_t>(pos));
  int32_t equal_decimal_value = 700;
  int32_t greater_decimal_value = 701;
  ObObj equal_decimal;
  ObObj greater_decimal;
  set_decimal_int(equal_decimal, equal_decimal_value, 2);
  set_decimal_int(greater_decimal, greater_decimal_value, 2);

  expect_relation(enum_inner, equal_decimal, cmp_ctx, 0);
  expect_relation(enum_inner, greater_decimal, cmp_ctx, -1);
}

TEST(TestObObjCompare, decimal_int_comparison_errors)
{
  const ObCompareCtx cmp_ctx(ObMaxType, CS_TYPE_INVALID, true,
                             INVALID_TZ_OFF, NULL_FIRST);
  int64_t decimal_value = 42;
  ObObj invalid_decimal;
  invalid_decimal.set_decimal_int(
      3, 0, reinterpret_cast<ObDecimalInt *>(&decimal_value));
  ObObj int_value;
  int_value.set_int(42);
  expect_comparison_error(invalid_decimal, int_value, cmp_ctx);
  expect_comparison_error(int_value, invalid_decimal, cmp_ctx);

  const char malformed[] = "";
  ObObj malformed_enum_inner;
  malformed_enum_inner.set_enum_inner(malformed, 0);
  ObObj valid_decimal;
  set_decimal_int(valid_decimal, decimal_value, 0);
  expect_comparison_error(malformed_enum_inner, valid_decimal, cmp_ctx);
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
