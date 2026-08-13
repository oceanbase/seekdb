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

#define USING_LOG_PREFIX SHARE
#include "share/object/ob_obj_cast.h"
#include "share/object/ob_obj_cast_util.h"
#include <gtest/gtest.h>
#include <limits>


namespace oceanbase
{
namespace common
{
using namespace number;

class TestObjCast : public ::testing::Test
{
public:
  virtual void SetUp();
  virtual void TearDown() {}

  ObArenaAllocator allocator_;
};

void TestObjCast::SetUp()
{
  const lib::ObMemAttr attr(ObModIds::OB_NUMBER);
  int ret = ObNumberConstValue::init(allocator_);
  ASSERT_EQ(OB_SUCCESS, ret);
}

TEST_F(TestObjCast, test_number_range_check_mysql_old)
{
  int ret = OB_SUCCESS;
  ObNumber zero_number;
  zero_number.set_zero();
  ObObj obj1;
  obj1.set_number(zero_number);
  const ObObj *res_obj = &obj1;
  ObObjCastParams params;
  params.allocator_v2_ = &allocator_;
  params.cast_mode_ |= CM_WARN_ON_FAIL;
  int64_t get_range_beg = ObTimeUtility::current_time();
  for (int16_t precision = OB_MIN_DECIMAL_PRECISION; OB_SUCC(ret) && precision <= ObNumber::MAX_PRECISION; ++precision) {
    for (int16_t scale = 0; OB_SUCC(ret) && precision >= scale && scale <= ObNumber::MAX_SCALE; ++scale) {
      ObAccuracy accuracy(precision, scale);
      ret = number_range_check(params, accuracy, obj1, obj1, res_obj, params.cast_mode_);
      ASSERT_EQ(OB_SUCCESS, ret);
    }
  }
  int64_t get_range_cost = ObTimeUtility::current_time() - get_range_beg;
  _OB_LOG(INFO, "test_number_range_check_mysql_old(%d) cost time: %f", (ObNumber::MAX_PRECISION + 1) * (ObNumber::MAX_SCALE + 1) / 2, (double)get_range_cost / (double)1000);
}

TEST_F(TestObjCast, test_number_range_check_mysql_new)
{
  int ret = OB_SUCCESS;
  ObNumber zero_number;
  zero_number.set_zero();
  ObObj obj1;
  obj1.set_number(zero_number);
  const ObObj *res_obj = &obj1;
  ObObjCastParams params;
  params.allocator_v2_ = &allocator_;
  params.cast_mode_ |= CM_WARN_ON_FAIL;
  int64_t get_range_beg = ObTimeUtility::current_time();
  for (int16_t precision = OB_MIN_DECIMAL_PRECISION; OB_SUCC(ret) && precision <= ObNumber::MAX_PRECISION; ++precision) {
    for (int16_t scale = 0; OB_SUCC(ret) && precision >= scale && scale <= ObNumber::MAX_SCALE; ++scale) {
      ObAccuracy accuracy(precision, scale);
      ret = number_range_check_v2(params, accuracy, obj1, obj1, res_obj, params.cast_mode_);
      ASSERT_EQ(OB_SUCCESS, ret);
    }
  }
  int64_t get_range_cost = ObTimeUtility::current_time() - get_range_beg;
  _OB_LOG(INFO, "test_number_range_check_mysql_new(%d) cost time: %f", (ObNumber::MAX_PRECISION + 1) * (ObNumber::MAX_SCALE + 1) / 2, (double)get_range_cost / (double)1000);
}

TEST_F(TestObjCast, deterministic_floating_to_integer)
{
  EXPECT_EQ(INT64_MIN, truncate_floating_to_int64_clamped(-2.0e28));
  EXPECT_EQ(INT64_MAX, truncate_floating_to_int64_clamped(2.0e28));
  EXPECT_EQ(INT64_MAX,
            truncate_floating_to_int64_clamped(
                static_cast<float>(INT64_UPPER_BOUND_AS_DOUBLE)));
  EXPECT_EQ(-1, truncate_floating_to_int64_clamped(-1.9));
  EXPECT_EQ(0,
            truncate_floating_to_int64_clamped(
                std::numeric_limits<double>::quiet_NaN()));

  uint64_t out_val = 0;
  EXPECT_EQ(OB_SUCCESS, round_floating_to_uint64(-0.5, true, true, out_val));
  EXPECT_EQ(0, out_val);
  EXPECT_EQ(OB_DATA_OUT_OF_RANGE,
            round_floating_to_uint64(
                static_cast<double>(static_cast<float>(-0.50001)),
                true,
                true,
                out_val));
  EXPECT_EQ(0, out_val);
  EXPECT_EQ(OB_DATA_OUT_OF_RANGE,
            round_floating_to_uint64(
                static_cast<double>(static_cast<float>(-2.0e28)),
                true,
                true,
                out_val));
  EXPECT_EQ(0, out_val);
  EXPECT_EQ(OB_DATA_OUT_OF_RANGE,
            round_floating_to_uint64(
                static_cast<double>(static_cast<float>(2.0e28)),
                true,
                true,
                out_val));
  EXPECT_EQ(UINT64_MAX, out_val);
  EXPECT_EQ(OB_DATA_OUT_OF_RANGE,
            round_floating_to_uint64(-2.0e28, false, true, out_val));
  EXPECT_EQ(0, out_val);
  EXPECT_EQ(OB_DATA_OUT_OF_RANGE,
            round_floating_to_uint64(2.0e28, false, true, out_val));
  EXPECT_EQ(UINT64_MAX, out_val);
  EXPECT_EQ(OB_SUCCESS,
            round_floating_to_uint64(
                static_cast<double>(
                    static_cast<float>(INT64_UPPER_BOUND_AS_DOUBLE)),
                true,
                false,
                out_val));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MIN), out_val);
  EXPECT_EQ(OB_SUCCESS,
            round_floating_to_uint64(
                static_cast<double>(static_cast<float>(1.2e19)),
                true,
                false,
                out_val));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MIN), out_val);
  EXPECT_EQ(OB_SUCCESS,
            round_floating_to_uint64(
                INT64_UPPER_BOUND_AS_DOUBLE,
                false,
                false,
                out_val));
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX), out_val);
}

} // end namespace share
} // end namespace oceanbase
