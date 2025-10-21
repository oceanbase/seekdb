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
#include "sql/ob_sql_init.h"
#include "sql/engine/set/ob_merge_union.h"
#include "sql/engine/table/ob_fake_table.h"
#include "sql/engine/recursive_cte/ob_recursive_cte_util.h"
#include "observer/ob_server.h"
#include "observer/ob_server_struct.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::observer;

class RecursiveCTETester : public ObRecursiveUnionAll
{
public:
  RecursiveCTETester() :ObRecursiveUnionAll(alloc_) {}
  virtual ~RecursiveCTETester() {}
};

class TestRecusiveUnionAll: public ::testing::Test
{
public:
  TestRecusiveUnionAll();
  virtual ~TestRecusiveUnionAll();
  virtual void SetUp();
  virtual void TearDown();
  void except_result_with_row_count(ObExecContext& _ctx, ObFakeTable& _result_table,
  		uint64_t _count, RecursiveCTETester& op,
			uint64_t start_idx,
			uint64_t end_idx,
			ObCollationType& collation) {
  	int ret = OB_SUCCESS;
    UNUSED(start_idx);
    UNUSED(end_idx);
    UNUSED(collation);
		if (OB_SUCC(ret)) {
			const ObNewRow *result_row = NULL;
			uint64_t j = 0;
			while(j < _count && (OB_SUCCESS == (ret = op.get_next_row(_ctx, result_row)))) {
				j++;
				const ObNewRow *except_row = NULL;
        //ObCStringHelper helper;
				//printf("row=%s\n", helper.convert(*result_row));
				ASSERT_EQ(OB_SUCCESS, _result_table.get_next_row(_ctx, except_row));
				//printf("except_row=%s\n", helper.convert(*except_row));
				ASSERT_TRUE(except_row->count_ == result_row->count_);
				/*for (int64_t i = start_idx; i < end_idx; ++i) {
          helper.reset();
					printf("index=%ld, cell=%s, respect_cell=%s\n", i, helper.convert(result_row->cells_[i]), helper.convert(except_row->cells_[i]));
					ASSERT_TRUE(0 == except_row->cells_[i].compare(result_row->cells_[i], collation));
				}*/
				result_row = NULL;
			}
		}
  }
protected:
private:
  // disallow copy
  TestRecusiveUnionAll(const TestRecusiveUnionAll &other);
  TestRecusiveUnionAll& operator=(const TestRecusiveUnionAll &other);
private:
  // data members
};
TestRecusiveUnionAll::TestRecusiveUnionAll()
{
}

TestRecusiveUnionAll::~TestRecusiveUnionAll()
{
}

void TestRecusiveUnionAll::SetUp()
{
}

void TestRecusiveUnionAll::TearDown()
{
}

TEST_F(TestRecusiveUnionAll, test_recursive_cte_muti_round_breadth)
{
  int ret = OB_SUCCESS;
  ObExecContext ctx;
  const ObNewRow* row = NULL;
  ObFakeTable &planA_output_table = TestRecursiveCTEFactory::get_planA_op_();
  ObFakeTable &planB_output_table = TestRecursiveCTEFactory::get_planB_op_();
  planB_output_table.set_no_rescan();
  ObFakeTable &result_table = TestRecursiveCTEFactory::get_result_table();
  RecursiveCTETester recursive_cte;
  recursive_cte.init_cte_pseudo_column();
  ObFakeCTETable fake_cte(alloc_);
  ObCollationType agg_cs_type = CS_TYPE_UTF8MB4_BIN;
  fake_cte.add_column_involved_offset(0);
  fake_cte.add_column_involved_offset(1);
  fake_cte.add_column_involved_offset(2);
  fake_cte.add_column_involved_offset(3);
  fake_cte.add_column_involved_offset(4);

  TestRecursiveCTEFactory::init(ctx, &recursive_cte, &fake_cte, 5);

  ASSERT_EQ(OB_SUCCESS, recursive_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.open(ctx));
  // The first round will not generate data, because planb will not be accessed, but there will be data output to the fake cte table, i.e., A
  // The data that comes out next is the data retrieved from planb for A
  ADD_ROW(planA_output_table, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  ADD_ROW(planA_output_table, COL(2), COL(3), COL(7), COL("B"), COL("oceanbase"));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));

  ADD_ROW(result_table, COL(2), COL(3), COL(7), COL("B"), COL("oceanbase"));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("BA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_TRUE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("BA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_TRUE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("BA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));

  const ObNewRow *except_row = NULL;
  ASSERT_TRUE( OB_ITER_END == recursive_cte.get_next_row( ctx, except_row));

  ASSERT_EQ(OB_SUCCESS, recursive_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.close(ctx));
}

TEST_F(TestRecusiveUnionAll, test_recursive_cte_muti_round_depth)
{
  int ret = OB_SUCCESS;
  ObExecContext ctx;
  const ObNewRow* row = NULL;
  ObFakeTable &planA_output_table = TestRecursiveCTEFactory::get_planA_op_();
  ObFakeTable &planB_output_table = TestRecursiveCTEFactory::get_planB_op_();
  planB_output_table.set_no_rescan();
  ObFakeTable &result_table = TestRecursiveCTEFactory::get_result_table();
  RecursiveCTETester recursive_cte;
  recursive_cte.init_cte_pseudo_column();
  ObFakeCTETable fake_cte(alloc_);
  ObCollationType agg_cs_type = CS_TYPE_UTF8MB4_BIN;
  fake_cte.add_column_involved_offset(0);
  fake_cte.add_column_involved_offset(1);
  fake_cte.add_column_involved_offset(2);
  fake_cte.add_column_involved_offset(3);
  fake_cte.add_column_involved_offset(4);

  TestRecursiveCTEFactory::init_depth(ctx, &recursive_cte, &fake_cte, 5);

  ASSERT_EQ(OB_SUCCESS, recursive_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.open(ctx));
  // The first round will not generate data, because planb will not be accessed, but there will be data output to the fake cte table, i.e., A
  // The data that comes out next is the data retrieved from planb for A
  ADD_ROW(planA_output_table, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  ADD_ROW(planA_output_table, COL(2), COL(3), COL(7), COL("B"), COL("oceanbase"));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AC"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AAB"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AAB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AAB"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AC"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AC"), COL(null));

  ADD_ROW(result_table, COL(2), COL(3), COL(7), COL("B"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(3), COL(7), COL("B"), COL(null));

  const ObNewRow *except_row = NULL;
  ASSERT_TRUE( OB_ITER_END == recursive_cte.get_next_row( ctx, except_row));

  ASSERT_EQ(OB_SUCCESS, recursive_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.close(ctx));
}

TEST_F(TestRecusiveUnionAll, test_recursive_cte_muti_round_depth_search_by_col3)
{
  int ret = OB_SUCCESS;
  ObExecContext ctx;
  const ObNewRow* row = NULL;
  ObFakeTable &planA_output_table = TestRecursiveCTEFactory::get_planA_op_();
  ObFakeTable &planB_output_table = TestRecursiveCTEFactory::get_planB_op_();
  planB_output_table.set_no_rescan();
  ObFakeTable &result_table = TestRecursiveCTEFactory::get_result_table();
  RecursiveCTETester recursive_cte;
  recursive_cte.init_cte_pseudo_column();
  ObFakeCTETable fake_cte(alloc_);
  ObCollationType agg_cs_type = CS_TYPE_UTF8MB4_BIN;
  fake_cte.add_column_involved_offset(0);
  fake_cte.add_column_involved_offset(1);
  fake_cte.add_column_involved_offset(2);
  fake_cte.add_column_involved_offset(3);
  fake_cte.add_column_involved_offset(4);

  TestRecursiveCTEFactory::init_depth_search_by_col3(ctx, &recursive_cte, &fake_cte, 5);

  ASSERT_EQ(OB_SUCCESS, recursive_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.open(ctx));

  ADD_ROW(planA_output_table, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  ADD_ROW(planA_output_table, COL(2), COL(3), COL(7), COL("B"), COL("oceanbase"));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("A"), COL(null));
  // Since union all itself turns off the sorting logic, the results must be added in order here
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4.0), COL("AC"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AD"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAB"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAC"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAD"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAE"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1.0), COL("AAF"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAB"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAC"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAC"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAD"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAD"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAE"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAE"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AAF"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AAF"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AB"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4.0), COL("AC"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4.0), COL("AC"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1.0), COL("AD"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1.0), COL("AD"), COL(null));

  ADD_ROW(result_table, COL(2), COL(3), COL(7), COL("B"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(3), COL(7), COL("B"), COL(null));

  const ObNewRow *except_row = NULL;
  ASSERT_TRUE( OB_ITER_END == recursive_cte.get_next_row( ctx, except_row));

  ASSERT_EQ(OB_SUCCESS, recursive_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.close(ctx));
}

TEST_F(TestRecusiveUnionAll, test_recursive_cte_muti_round_breadth_search_by_col3_cycle_by_col2)
{
  int ret = OB_SUCCESS;
  ObExecContext ctx;
  const ObNewRow* row = NULL;
  ObFakeTable &planA_output_table = TestRecursiveCTEFactory::get_planA_op_();
  ObFakeTable &planB_output_table = TestRecursiveCTEFactory::get_planB_op_();
  planB_output_table.set_no_rescan();
  ObFakeTable &result_table = TestRecursiveCTEFactory::get_result_table();
  RecursiveCTETester recursive_cte;
  recursive_cte.init_cte_pseudo_column();
  ObFakeCTETable fake_cte(alloc_);
  ObCollationType agg_cs_type = CS_TYPE_UTF8MB4_BIN;
  fake_cte.add_column_involved_offset(0);
  fake_cte.add_column_involved_offset(1);
  fake_cte.add_column_involved_offset(2);
  fake_cte.add_column_involved_offset(3);
  fake_cte.add_column_involved_offset(4);

  TestRecursiveCTEFactory::init_breadth_search_by_col3_cyc_by_col2(ctx, &recursive_cte, &fake_cte, 5);

  ASSERT_EQ(OB_SUCCESS, recursive_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.open(ctx));
  //
  // The figure below shows the test graph, where AAA and ancestor A have duplicate values in column 2, making it a duplicate column. In Oracle testing, duplicate specified values at the same level are not considered a cycle
  //                  A （1）       B （2）
  //           AA（3）  AB（4）    BA（5）
  //     AAA（1）   ABA（5）       BAA (4)
  //
  //The first round will not generate data, because planb will not be accessed, but there will be data output to the fake cte table, i.e., A
  // The data that comes out next is the data retrieved from planb for A
  ADD_ROW(planA_output_table, COL(1), COL(2), COL(1), COL("A"), COL(null));
  ADD_ROW(planA_output_table, COL(2), COL(3), COL(2), COL("B"), COL("oceanbase"));

  ADD_ROW(result_table, COL(1), COL(2), COL(1), COL("A"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1), COL("A"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(3), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4), COL("AB"), COL(null));

  ADD_ROW(result_table, COL(2), COL(3), COL(2), COL("B"), COL("oceanbase"));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(3), COL(2), COL("B"), COL("oceanbase"));
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(3), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(5), COL("BA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(3), COL("AA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_TRUE(NULL == row);
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1), COL("AAA"), COL(null));

  ADD_ROW(result_table, COL(2), COL(2), COL(4), COL("AB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4), COL("AB"), COL(null));
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(5), COL("BA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(5), COL("ABA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(5), COL("BA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_TRUE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(5), COL("BA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(4), COL("BAA"), COL(null));
  // Column 2 value of AAA is 1, which duplicates column 2 value of A being 1, A is the ancestor of AAA, so AAA will not enter the fake table and will be dequeued directly
  ADD_ROW(result_table, COL(1), COL(2), COL(1), COL("AAA"), COL(null));
  ADD_ROW(result_table, COL(1), COL(2), COL(5), COL("ABA"), COL(null));
  except_result_with_row_count(ctx, result_table, 2, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(4), COL("BAA"), COL(null));
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(5), COL("ABA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(4), COL("BAA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_TRUE(NULL == row);
  //EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(4), COL("BAA"), COL(null));

  const ObNewRow *except_row = NULL;
  ASSERT_TRUE( OB_ITER_END == recursive_cte.get_next_row( ctx, except_row));

  ASSERT_EQ(OB_SUCCESS, recursive_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.close(ctx));
}


TEST_F(TestRecusiveUnionAll, test_recursive_cte_muti_round_depth_search_by_col3_cycle_by_col2)
{
  int ret = OB_SUCCESS;
  ObExecContext ctx;
  const ObNewRow* row = NULL;
  ObFakeTable &planA_output_table = TestRecursiveCTEFactory::get_planA_op_();
  ObFakeTable &planB_output_table = TestRecursiveCTEFactory::get_planB_op_();
  planB_output_table.set_no_rescan();
  ObFakeTable &result_table = TestRecursiveCTEFactory::get_result_table();
  RecursiveCTETester recursive_cte;
  recursive_cte.init_cte_pseudo_column();
  ObFakeCTETable fake_cte(alloc_);
  ObCollationType agg_cs_type = CS_TYPE_UTF8MB4_BIN;
  fake_cte.add_column_involved_offset(0);
  fake_cte.add_column_involved_offset(1);
  fake_cte.add_column_involved_offset(2);
  fake_cte.add_column_involved_offset(3);
  fake_cte.add_column_involved_offset(4);

  TestRecursiveCTEFactory::init_depth_search_by_col3_cyc_by_col2(ctx, &recursive_cte, &fake_cte, 5);

  ASSERT_EQ(OB_SUCCESS, recursive_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.open(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.open(ctx));
  //
  // The figure below shows the test graph, where AAA and ancestor A have duplicate values in column 2, making it a duplicate column. In Oracle testing, duplicate specified values at the same level are not considered a cycle
  // The right figure is the offset of the node in the array tree
  //                  A （1）       B （2）                  uint64      uint64
  //           AA（3）  AB（4）    BA（5）               0       0            1
  //     AAA（1）   ABA（5）       BAA (4)          2         3              4
  //
  // The first round will not generate data, because planb will not be accessed, but there will be data output to the fake cte table, i.e., A
  // The data that comes out next is the data retrieved from planb for A
  ADD_ROW(planA_output_table, COL(1), COL(2), COL(1), COL("A"), COL(null));
  ADD_ROW(planA_output_table, COL(2), COL(3), COL(2), COL("B"), COL("oceanbase"));

  ADD_ROW(result_table, COL(1), COL(2), COL(1), COL("A"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(1), COL("A"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(3), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(2), COL(2), COL(4), COL("AB"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(3), COL("AA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(3), COL("AA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(1), COL("AAA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(1), COL("AAA"), COL(null));//Since the value of column AAA at row 2 is 1, which is the same as the value of the top node "A", therefore AAA will not enter the fake cte table operator
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  //AAA will not enter the fake table, as expected

  ADD_ROW(result_table, COL(2), COL(2), COL(4), COL("AB"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(2), COL(4), COL("AB"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(5), COL("ABA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(5), COL("ABA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(5), COL("ABA"), COL(null));

  ADD_ROW(result_table, COL(2), COL(3), COL(2), COL("B"), COL("oceanbase"));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(2), COL(3), COL(2), COL("B"), COL("oceanbase"));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(5), COL("BA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(5), COL("BA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(5), COL("BA"), COL(null));
  ADD_ROW(planB_output_table, COL(1), COL(2), COL(4), COL("BAA"), COL(null));

  ADD_ROW(result_table, COL(1), COL(2), COL(4), COL("BAA"), COL(null));
  except_result_with_row_count(ctx, result_table, 1, recursive_cte, 0, 4, agg_cs_type);//During testing, the RS stack in memory is B BA BAA as expected
  row = NULL;
  fake_cte.get_next_row(ctx, row);
  ASSERT_FALSE(NULL == row);
  EXPECT_SAME_ROW((*row), result_table, 0, 4, agg_cs_type, COL(1), COL(2), COL(4), COL("BAA"), COL(null));

  const ObNewRow *except_row = NULL;
  ASSERT_TRUE( OB_ITER_END == recursive_cte.get_next_row( ctx, except_row));

  ASSERT_EQ(OB_SUCCESS, recursive_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, fake_cte.close(ctx));
  ASSERT_EQ(OB_SUCCESS, result_table.close(ctx));
}

int main(int argc, char **argv)
{
  system("rm -f test_recursive_cte.log");
  init_global_memory_pool();
  init_sql_factories();
  ::testing::InitGoogleTest(&argc,argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
