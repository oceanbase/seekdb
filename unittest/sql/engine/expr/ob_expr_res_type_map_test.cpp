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
#include "ob_expr_test_utils.h"
#include "sql/engine/expr/ob_expr_res_type_map.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

class TestArithRuleMap : public ::testing::Test, public ObArithResultTypeMap
{
  virtual void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, init());
  }

  virtual void TearDown() override
  {
  }
  int define_rules()
  {
    using C = ObArithResultTypeChoice;
    int ret = OB_SUCCESS;
    ObArithRule rule;
    //type+type
    OZ (new_rules(ObNullType, ObNullType, ADD|SUB).result_as(ObNumberType).get_ret());
    //type+tc
    OZ (new_rules(ObNullType, ObOTimestampTC, MUL|DIV).result_as(ObVarcharType).get_ret());
    //tc+tc
    OZ (new_rules(ObStringTC, ObOTimestampTC, DIV).result_as(ObIntType).cast_param1_as(ObNumberType).get_ret());
    //type + func
    //func + func
    //ObArithResultTypeChoice
    OZ (new_rules(ob_is_interval_tc, ObStringTC, ADD).result_as(C::FIRST).cast_param1_as(ObNumberType).cast_param2_as(C::SECOND).get_ret()); 
    return ret; 
  }
};

TEST_F(TestArithRuleMap, get_rule)
{
  ObArithRule rule;
  ObArithRule default_rule;
  //type+type
  //OZ (new_rules(ObNullType, ObNullType, ADD|SUB).result_as(ObNumberType).get_ret());
  rule.result_type = ObNumberType;
  rule.param1_calc_type = ObMaxType;
  rule.param2_calc_type = ObMaxType;
  ASSERT_TRUE(rule == get_rule(ObNullType, ObNullType, ADD));
  ASSERT_TRUE(rule == get_rule(ObNullType, ObNullType, SUB));
  ASSERT_TRUE(default_rule == get_rule(ObNullType, ObNullType, MUL));
  ASSERT_TRUE(default_rule == get_rule(ObNullType, ObNullType, DIV));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_arith_rule_map.log", true);
  return RUN_ALL_TESTS();
}
