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
#include <gtest/gtest.h>


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
  const lib::ObMemAttr attr(common::OB_SYS_TENANT_ID, ObModIds::OB_NUMBER);
  int ret = ObNumberConstValue::init(allocator_);
  ASSERT_EQ(OB_SUCCESS, ret);
}

TEST_F(TestObjCast, test_number_range_check_mysql_old)
{
  int ret = OB_SUCCESS;
  lib::CompatModeGuard tmp_mode(lib::Worker::CompatMode::MYSQL);
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
  lib::CompatModeGuard tmp_mode(lib::Worker::CompatMode::MYSQL);
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

} // end namespace share
} // end namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
