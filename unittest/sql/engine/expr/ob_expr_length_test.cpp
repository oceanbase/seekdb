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
#include "sql/engine/expr/ob_expr_length.h"
#include "ob_expr_test_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObExprLengthTest : public ::testing::Test
{
public:
  ObExprLengthTest();
  virtual ~ObExprLengthTest();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  ObExprLengthTest(const ObExprLengthTest &other);
  ObExprLengthTest& operator=(const ObExprLengthTest &other);
private:
  // data members
};
ObExprLengthTest::ObExprLengthTest()
{
}

ObExprLengthTest::~ObExprLengthTest()
{
}

void ObExprLengthTest::SetUp()
{
}

void ObExprLengthTest::TearDown()
{
}

#define T(obj, t1, v1, ref_type, ref_value) EXPECT_RESULT1(obj, &buf, calc_result1, t1, v1, ref_type, ref_value)
#define F(obj, t1, v1) EXPECT_FAIL_RESULT1(obj, &buf, calc_result1, t1, v1)
#define F0(obj, t1) EXPECT_FAIL_RESULT0(obj, &buf, calc_result1, t1)
#define T0(obj, t1, ref_type, ref_value)  EXPECT_RESULT0(obj, &buf, calc_result1, t1, ref_type, ref_value)

TEST_F(ObExprLengthTest, basic_test)
{
  ObArenaAllocator buf(ObModIds::OB_SQL_SESSION);
  ObExprLength length(buf);
  //ObExprStringBuf buf;
  ASSERT_EQ(1, length.get_param_num());

  // null
  //T0(length, max_value, int, 0);
  //T0(length, min_value, int, 0);
  T0(length, null, null, 0);
  T(length, varchar, "helo", int, 4);
  T(length, varchar, "", int, 0);
  // Escape character depends on the sql_parser process
  //T(length, varchar, "\\_", int, 2);
  //T(length, varchar, "\\%", int, 2);
  //T(length, varchar, "\\\\", int, 1);
  //T(length, varchar, "\\t", int, 1);
  //T(length, varchar, "\\v", int, 1);
  T(length, int, 1, int, 1);
  T(length, int, 12, int, 2);
  // Related to precision, temporarily not handled
  //T(length, double, 12.32, int, 5);
  //T(length, double, 0.00, int, 4);
  T(length, varchar, "好", int, 3);
}

TEST_F(ObExprLengthTest, fail_test)
{
  ObArenaAllocator buf(ObModIds::OB_SQL_SESSION);
  ObExprLength length(buf);
  ASSERT_EQ(1, length.get_param_num());
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("DEBUG");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}

