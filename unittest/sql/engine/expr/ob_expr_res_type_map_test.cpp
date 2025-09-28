/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
