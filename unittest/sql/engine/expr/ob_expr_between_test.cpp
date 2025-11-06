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

#include "sql/engine/expr/ob_expr_between.h"
#include "ob_expr_test_utils.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObExprBetweenTest: public ::testing::Test
{
  public:
    ObExprBetweenTest();
    virtual ~ObExprBetweenTest();
    virtual void SetUp();
    virtual void TearDown();
  private:
    // disallow copy
    ObExprBetweenTest(const ObExprBetweenTest &other);
    ObExprBetweenTest& operator=(const ObExprBetweenTest &other);
  protected:
    // data members
};

ObExprBetweenTest::ObExprBetweenTest()
{
}

ObExprBetweenTest::~ObExprBetweenTest()
{
}

void ObExprBetweenTest::SetUp()
{
}

void ObExprBetweenTest::TearDown()
{
}

#define T(t1, v1, t2, v2, t3, v3, res) COMPARE3_EXPECT(ObExprBetween, &buf, calc_result3, t1, v1, t2, v2, t3, v3, res)
TEST_F(ObExprBetweenTest, basic_test)
{
  // special value
  ObExprStringBuf buf;
  T(min, 0, min, 0, min, 0, MY_TRUE);
  T(min, 0, min, 0, null, 0, MY_NULL);
  T(min, 0, min, 0, max, 0, MY_TRUE);
  T(min, 0, null, 0, min, 0, MY_NULL);
  T(min, 0, null, 0, null, 0, MY_NULL);
  T(min, 0, null, 0, max, 0, MY_NULL);
  T(min, 0, max, 0, min, 0, MY_FALSE);
  T(min, 0, max, 0, null, 0, MY_FALSE);
  T(min, 0, max, 0, max, 0, MY_FALSE);
  T(null, 0, min, 0, min, 0, MY_NULL);
  T(null, 0, min, 0, null, 0, MY_NULL);
  T(null, 0, min, 0, max, 0, MY_NULL);
  T(null, 0, null, 0, min, 0, MY_NULL);
  T(null, 0, null, 0, null, 0, MY_NULL);
  T(null, 0, null, 0, max, 0, MY_NULL);
  T(null, 0, max, 0, min, 0, MY_NULL);
  T(null, 0, max, 0, null, 0, MY_NULL);
  T(null, 0, max, 0, max, 0, MY_NULL);
  T(max, 0, min, 0, min, 0, MY_FALSE);
  T(max, 0, min, 0, null, 0, MY_NULL);
  T(max, 0, min, 0, max, 0, MY_TRUE);
  T(max, 0, null, 0, min, 0, MY_FALSE);
  T(max, 0, null, 0, null, 0, MY_NULL);
  T(max, 0, null, 0, max, 0, MY_NULL);
  T(max, 0, max, 0, min, 0, MY_FALSE);
  T(max, 0, max, 0, null, 0, MY_NULL);
  T(max, 0, max, 0, max, 0, MY_TRUE);

  // int
  T(int, -1, int, 0, int, 2, MY_FALSE);
  T(int, 0, int, 0, int, 2, MY_TRUE);
  T(int, 1, int, 0, int, 2, MY_TRUE);
  T(int, 2, int, 0, int, 2, MY_TRUE);
  T(int, 3, int, 0, int, 2, MY_FALSE);
  T(int, -1, int, 0, int, 0, MY_FALSE);
  T(int, 0, int, 0, int, 0, MY_TRUE);
  T(int, 1, int, 0, int, 0, MY_FALSE);
  T(int, 0, int, 2, int, 0, MY_FALSE);
  T(int, 2, int, 2, int, 0, MY_FALSE);

  // int vs special
  T(int, 0, int, 1, null, 0, MY_FALSE);
  T(int, 1, int, 0, null, 0, MY_NULL);
  T(int, 0, null, 0, int, 2, MY_NULL);
  T(null, 0, int, 0, int, 2, MY_NULL);
  T(min, 0, int, 0, int, 2, MY_FALSE);
  T(max, 0, int, 0, int, 2, MY_FALSE);
  T(null, 0, min, 0, int, 2, MY_NULL);
  T(null, 0, min, 0, max, 0, MY_NULL);
  T(min, 0, min, 0, int, 2, MY_TRUE);
  T(max, 0, int, 0, max, 0, MY_TRUE);

  // int vs varchar
  T(int, -1,varchar, "0", int, 2, MY_FALSE);
  T(int, 0, varchar, "0", int, 2, MY_TRUE);
  T(int, 1, varchar, "0", int, 2, MY_TRUE);
  T(int, 2, varchar, "0", int, 2, MY_TRUE);
  T(int, 3, varchar, "0", int, 2, MY_FALSE);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
