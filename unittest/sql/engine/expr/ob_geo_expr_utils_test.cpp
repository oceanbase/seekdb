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
#include "sql/engine/expr/ob_geo_expr_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObExprGeoUtilsTest : public ::testing::Test
{
public:
  ObExprGeoUtilsTest();
  virtual ~ObExprGeoUtilsTest();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprGeoUtilsTest);
};

ObExprGeoUtilsTest::ObExprGeoUtilsTest()
{
}

ObExprGeoUtilsTest::~ObExprGeoUtilsTest()
{
}

void ObExprGeoUtilsTest::SetUp()
{
}

void ObExprGeoUtilsTest::TearDown()
{
}

TEST_F(ObExprGeoUtilsTest, parse_axis_order_test)
{
  ObGeoAxisOrder axis_order;
  ASSERT_EQ(OB_SUCCESS, ObGeoExprUtils::parse_axis_order(ObString("axis-order=lat-long"), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::LAT_LONG, axis_order);
  ASSERT_EQ(OB_SUCCESS, ObGeoExprUtils::parse_axis_order(ObString("   axis-order= long-lat "), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::LONG_LAT, axis_order);
  ASSERT_EQ(OB_SUCCESS, ObGeoExprUtils::parse_axis_order(ObString("   axis-order= srid-defined "), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::SRID_DEFINED, axis_order);

  ASSERT_EQ(OB_ERR_PARSER_SYNTAX, ObGeoExprUtils::parse_axis_order(ObString("   axis-order: = long-lat ,"), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::INVALID, axis_order);
  ASSERT_EQ(OB_ERR_PARSER_SYNTAX, ObGeoExprUtils::parse_axis_order(ObString("   axis-order= long-lat ,"), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::INVALID, axis_order);
  ASSERT_EQ(OB_ERR_PARSER_SYNTAX, ObGeoExprUtils::parse_axis_order(ObString("   axis-order= srid-defined, axis-order = srid-defined"), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::INVALID, axis_order);
  ASSERT_EQ(OB_ERR_PARSER_SYNTAX, ObGeoExprUtils::parse_axis_order(ObString(" . = ."), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::INVALID, axis_order);
  ASSERT_EQ(OB_ERR_PARSER_SYNTAX, ObGeoExprUtils::parse_axis_order(ObString(""), "unit_test", axis_order));
  ASSERT_EQ(ObGeoAxisOrder::INVALID, axis_order);
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("DEBUG");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
