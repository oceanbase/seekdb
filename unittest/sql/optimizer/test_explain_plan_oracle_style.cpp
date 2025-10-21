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

#include "sql/optimizer/ob_log_plan.h"
#include <gtest/gtest.h>


#define BUF_LEN 102400 // 100K

using namespace oceanbase::sql;
using namespace oceanbase::common;

class ObLogPlanTest: public ::testing::Test
{
  public:
	  ObLogPlanTest();
    virtual ~ObLogPlanTest();
    virtual void SetUp();
    virtual void TearDown();
  private:
    // disallow copy
    ObLogPlanTest(const ObLogPlanTest &other);
    ObLogPlanTest& operator=(const ObLogPlanTest &other);
  private:
    // data members
};

ObLogPlanTest::ObLogPlanTest()
{
}

ObLogPlanTest::~ObLogPlanTest()
{
}

void ObLogPlanTest::SetUp()
{
}

void ObLogPlanTest::TearDown()
{
}

TEST_F(ObLogPlanTest, ob_explain_test)
{

  // create a plan
  ObSQLSessionInfo session_info
  ASSERT_EQ(OB_SUCCESS, session_info.test_init(0, 0, 0, NULL));
  ObOptimizerContext ctx(NULL, NULL);
  ObSelectStmt *select_stmt = NULL;
	ObSelectLogPlan plan(ctx, select_stmt);

  // create the operator tree
  ObLogTableScan *left =
    reinterpret_cast<ObLogTableScan *>(log_op_factory_.allocate(LOG_TABLE_SCAN));
  ObLogTableScan *right =
    reinterpret_cast<ObLogTableScan *>(log_op_factory_.allocate(LOG_TABLE_SCAN));
  ObLogJoin *join =
    reinterpret_cast<ObLogJoin *>(log_op_factory_.allocate(LOG_JOIN));
  ObLogGroupBy *group_by =
    reinterpret_cast<ObLogGroupBy *>(log_op_factory_.allocate(LOG_GROUP_BY));
  ObLogOrderBy *order_by =
    reinterpret_cast<ObLogOrderBy *>(log_op_factory_.allocate(LOG_ORDER_BY));
  ObLogLimit *limit =
    reinterpret_cast<ObLogLimit *>(log_op_factory_.allocate(LOG_LIMIT));

  ASSERT_EQ(OB_SUCCESS, plan.set_plan_root(limit));
  ASSERT_EQ(OB_SUCCESS, limit.set_child(first_child, order_by));
  ASSERT_EQ(OB_SUCCESS, order_by.set_parent(limit));
  ASSERT_EQ(OB_SUCCESS, order_by->set_child(first_child, group_by));
  ASSERT_EQ(OB_SUCCESS, group_by->set_parent(order_by));
  ASSERT_EQ(OB_SUCCESS, group_by->set_child(first_child, join));
  ASSERT_EQ(OB_SUCCESS, join->set_parent(group_by));
  ASSERT_EQ(OB_SUCCESS, join->set_child(first_child, left));
  ASSERT_EQ(OB_SUCCESS, left->set_parent(join));
  ASSERT_EQ(OB_SUCCESS, join->set_child(second_child, right));
  ASSERT_EQ(OB_SUCCESS, right->set_parent(join));

  // print a plan
  char buf[BUF_LEN];
	ASSERT_EQ(OB_SUCCESS,	plan.to_string(buf, BUF_LEN));
	printf("%s\n", buf);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
