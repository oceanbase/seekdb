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

#include "sql/engine/expr/ob_expr_is.h"
#include "ob_expr_test_utils.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObExprIsTest: public ::testing::Test
{
  public:
    ObExprIsTest();
    virtual ~ObExprIsTest();
    virtual void SetUp();
    virtual void TearDown();
  private:
    // disallow copy
    ObExprIsTest(const ObExprIsTest &other);
    ObExprIsTest& operator=(const ObExprIsTest &other);
  protected:
    // data members
};

ObExprIsTest::ObExprIsTest()
{
}

ObExprIsTest::~ObExprIsTest()
{
}

void ObExprIsTest::SetUp()
{
}

void ObExprIsTest::TearDown()
{
}

#define COMPARE_EXPECT_(cmp_op, ctx, func, type1, v1, type2, v2, res) \
     {                                                          \
       ObObj t1;                                            \
       ObObj t2;                                            \
       ObObj t3;                                            \
       t3.set_bool(false);                                  \
       ObObj vres;                                          \
       ObArenaAllocator alloc;\
       cmp_op op(alloc);                                               \
       t1.set_##type1(v1);                                      \
         t2.set_##type2(v2);                                    \
           int err = op.func(vres, t1, t2, t3, ctx);            \
           if (res == MY_ERROR)                                 \
           {                                                    \
             ASSERT_NE(OB_SUCCESS, err);                        \
           }                                                    \
           else                                                 \
           {                                                    \
             ASSERT_EQ(OB_SUCCESS, err);                        \
             switch(res)                                        \
             {                                                  \
               case MY_TRUE:                                    \
                 ASSERT_TRUE(vres.is_true());                   \
                 break;                                         \
               case MY_FALSE:                                   \
                 ASSERT_TRUE(vres.is_false());                  \
                 break;                                         \
               default:                                         \
                 ASSERT_TRUE(vres.is_null());                   \
                 break;                                         \
             }                                                  \
           }\
} while(0)

#define COMPARE_EXPECT_NULL1(cmp_op, ctx, func, type1, type2, v2, res) \
     {                                                          \
       ObObj t1;                                            \
       ObObj t2;                                            \
       ObObj t3;                                            \
       t3.set_bool(false);                                  \
       ObObj vres;                                          \
       ObArenaAllocator alloc;\
       cmp_op op(alloc);                                               \
       t1.set_##type1();                                      \
         t2.set_##type2(v2);                                    \
           int err = op.func(vres, t1, t2, t3, ctx);            \
           if (res == MY_ERROR)                                 \
           {                                                    \
             ASSERT_NE(OB_SUCCESS, err);                        \
           }                                                    \
           else                                                 \
           {                                                    \
             ASSERT_EQ(OB_SUCCESS, err);                        \
             switch(res)                                        \
             {                                                  \
               case MY_TRUE:                                    \
                 ASSERT_TRUE(vres.is_true());                   \
                 break;                                         \
               case MY_FALSE:                                   \
                 ASSERT_TRUE(vres.is_false());                  \
                 break;                                         \
               default:                                         \
                 ASSERT_TRUE(vres.is_null());                   \
                 break;                                         \
             }                                                  \
           }\
} while(0)

#define COMPARE_EXPECT_NULL2(cmp_op, ctx, func, type1, v1, type2, res) \
     {                                                          \
       ObObj t1;                                            \
       ObObj t2;                                            \
       ObObj t3;                                            \
       t3.set_bool(false);                                  \
       ObObj vres;                                          \
       ObArenaAllocator alloc;\
       cmp_op op(alloc);                                               \
       t1.set_##type1(v1);                                      \
         t2.set_##type2();                                    \
           int err = op.func(vres, t1, t2, t3, ctx);            \
           if (res == MY_ERROR)                                 \
           {                                                    \
             ASSERT_NE(OB_SUCCESS, err);                        \
           }                                                    \
           else                                                 \
           {                                                    \
             ASSERT_EQ(OB_SUCCESS, err);                        \
             switch(res)                                        \
             {                                                  \
               case MY_TRUE:                                    \
                 ASSERT_TRUE(vres.is_true());                   \
                 break;                                         \
               case MY_FALSE:                                   \
                 ASSERT_TRUE(vres.is_false());                  \
                 break;                                         \
               default:                                         \
                 ASSERT_TRUE(vres.is_null());                   \
                 break;                                         \
             }                                                  \
           }\
} while(0)

#define COMPARE_EXPECT_NULL1_NULL2(cmp_op, ctx, func, type1, type2, res) \
     {                                                          \
       ObObj t1;                                            \
       ObObj t2;                                            \
       ObObj t3;                                            \
       t3.set_bool(false);                                  \
       ObObj vres;                                          \
       ObArenaAllocator alloc;\
       cmp_op op(alloc);                                               \
       t1.set_##type1();                                      \
         t2.set_##type2();                                    \
           int err = op.func(vres, t1, t2, t3, ctx);            \
           if (res == MY_ERROR)                                 \
           {                                                    \
             ASSERT_NE(OB_SUCCESS, err);                        \
           }                                                    \
           else                                                 \
           {                                                    \
             ASSERT_EQ(OB_SUCCESS, err);                        \
             switch(res)                                        \
             {                                                  \
               case MY_TRUE:                                    \
                 ASSERT_TRUE(vres.is_true());                   \
                 break;                                         \
               case MY_FALSE:                                   \
                 ASSERT_TRUE(vres.is_false());                  \
                 break;                                         \
               default:                                         \
                 ASSERT_TRUE(vres.is_null());                   \
                 break;                                         \
             }                                                  \
           }\
} while(0)

#define T(t1, v1, t2, v2, res) COMPARE_EXPECT_(ObExprIs, ctx, calc_result3, t1, v1, t2, v2, res)
#define T_NULL1(t1, t2, v2, res) COMPARE_EXPECT_NULL1(ObExprIs, ctx, calc_result3, t1, t2, v2, res)
#define T_NULL2(t1, v1, t2, res) COMPARE_EXPECT_NULL2(ObExprIs, ctx, calc_result3, t1, v1, t2, res)
#define T_NULL1_NULL2(t1, t2, res) COMPARE_EXPECT_NULL1_NULL2(ObExprIs, ctx, calc_result3, t1, t2, res)
TEST_F(ObExprIsTest, basic_test)
{
  ObExprStringBuf buf;
  ObExprCtx ctx(NULL, NULL, NULL, &buf);
  T(bool, true, bool, true, MY_TRUE);
  T(bool, true, bool, false, MY_FALSE);
  T_NULL2(bool, true, null, MY_FALSE);
  T(bool, false, bool, true, MY_FALSE);
  T(bool, false, bool, false, MY_TRUE);
  T_NULL2(bool, true, null, MY_FALSE);
  T_NULL1(null, bool, true, MY_FALSE);
  T_NULL1(null, bool, false, MY_FALSE);
  T_NULL1_NULL2(null, null, MY_TRUE);

  T(int, 0, bool, true, MY_FALSE);
  T(int, 0, bool, false, MY_TRUE);
  T_NULL2(int, 0, null, MY_FALSE);
  T_NULL2(float, 0.0, null, MY_FALSE);
  T_NULL2(double, 0.0, null, MY_FALSE);
  //T(precise_datetime, 0, null, 0, MY_TRUE);
  //T(ctime, 0, null, 0, MY_TRUE);
  //T(mtime, 0, null, 0, MY_TRUE);

  T(varchar, "20080115", bool, true, MY_TRUE);
  T(varchar, "a", bool, true, MY_FALSE);
  T(varchar, "231#dsank2", bool, true, MY_TRUE);
  T(int, 1, bool, true, MY_TRUE);
}


int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
