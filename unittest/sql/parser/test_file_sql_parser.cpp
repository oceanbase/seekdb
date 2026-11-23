/*
 * Copyright (c) 2026 OceanBase.
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

#include "lib/allocator/ob_allocator.h"
#include "sql/parser/ob_parser.h"
#include "sql/plan_cache/ob_sql_parameterization.h"

namespace oceanbase
{
namespace sql
{

TEST(TestFileSqlParser, duckdb_file_reader_syntax)
{
  const char *queries[] = {
    "select * from read_csv('/tmp/orders.csv') as f",
    "select * from read_csv_auto('/tmp/orders.csv')",
    "select * from read_csv('/tmp/orders.csv', header = true, delim = ',', sample_size = -1)",
    "select f.id from read_json('/tmp/orders.jsonl', format = 'newline_delimited') f",
    "select * from read_ndjson('/tmp/orders.jsonl')",
    "select * from read_json_auto('/tmp/orders.jsonl')",
    "select * from read_ndjson_auto('/tmp/orders.jsonl')",
    "select * from read_parquet('/tmp/a.parquet')",
    "select * from parquet_scan('/tmp/a.parquet') p",
    "select * from read_csv('/tmp/a.csv') a join read_csv('/tmp/b.csv') b on a.id = b.id",
    "select file from glob('/tmp/*.csv') as files",
    "describe select * from read_csv('/tmp/orders.csv')",
    "explain select * from read_csv('/tmp/orders.csv')",
    "copy orders to '/tmp/orders-copy.csv'",
    "copy orders (id, name) to '/tmp/orders-copy.csv' (header false)",
    "copy orders to '/tmp/orders-copy.csv' (format csv, header)",
    "copy (select id, name from orders where id > 10) to '/tmp/orders-copy.csv' "
      "with (format csv, header = false, delimiter ',', quote '\"', escape '\"', nullstr 'NULL')"
  };
  common::ObArenaAllocator allocator;
  for (int64_t i = 0; i < ARRAYSIZEOF(queries); ++i) {
    ObParser parser(allocator, DEFAULT_MYSQL_MODE);
    ParseResult result;
    ASSERT_EQ(common::OB_SUCCESS,
              parser.parse(common::ObString::make_string(queries[i]), result)) << queries[i];
    ASSERT_NE(nullptr, result.result_tree_);
    parser.free_result(result);
    allocator.reuse();
  }

  const char *removed_queries[] = {
    "select * from file_scan('/tmp/orders.csv') as f",
    "select * from file_list('/tmp') as files",
    "select * from file_schema('/tmp/orders.csv') schema_info"
  };
  for (int64_t i = 0; i < ARRAYSIZEOF(removed_queries); ++i) {
    ObParser parser(allocator, DEFAULT_MYSQL_MODE);
    ParseResult result;
    ASSERT_NE(common::OB_SUCCESS,
              parser.parse(common::ObString::make_string(removed_queries[i]), result))
        << removed_queries[i];
    parser.free_result(result);
    allocator.reuse();
  }
}

TEST(TestFileSqlParser, describe_select_preserves_explain_semantics)
{
  const struct {
    const char *sql;
    ObItemType type;
  } cases[] = {
    {"desc select * from test", T_EXPLAIN},
    {"describe select * from test", T_EXPLAIN},
    {"desc extended select * from test", T_EXPLAIN},
    {"describe update test set a = 1", T_EXPLAIN},
    {"explain select * from read_csv('/tmp/t.csv')", T_EXPLAIN},
    {"describe select id from read_csv('/tmp/t.csv')", T_EXPLAIN},
    {"describe select * from read_csv('/tmp/t.csv')", T_SELECT},
  };
  common::ObArenaAllocator allocator;
  for (const auto &test : cases) {
    ObParser parser(allocator, DEFAULT_MYSQL_MODE);
    ParseResult result;
    ASSERT_EQ(common::OB_SUCCESS, parser.parse(common::ObString::make_string(test.sql), result))
        << test.sql;
    const ParseNode *stmt = result.result_tree_->children_[0];
    ASSERT_EQ(test.type, stmt->type_) << test.sql;
    if (T_SELECT == test.type) {
      const ParseNode *file = stmt->children_[PARSE_SELECT_FROM]->children_[0];
      ASSERT_EQ(T_FILE_SCAN_TABLE, file->type_);
      EXPECT_EQ(1, file->value_);
    }
    parser.free_result(result);
    allocator.reuse();
  }
}

TEST(TestFileSqlParser, file_arguments_survive_plan_parameterization)
{
  common::ObArenaAllocator allocator;
  ObSQLSessionInfo session;
  ASSERT_EQ(common::OB_SUCCESS, session.test_init(0, 0, &allocator));

  const char *queries[] = {
    "select count(*) from read_csv('/tmp/t.csv')",
    "select * from read_csv('/tmp/t.csv', header = true, sample_size = -1)",
    "select * from glob('/tmp/*.csv')",
    "copy orders to '/tmp/out.csv' (header false, nullstr 'NULL')",
  };
  for (const char *query : queries) {
    ObParser parser(allocator, DEFAULT_MYSQL_MODE);
    ParseResult result;
    ASSERT_EQ(common::OB_SUCCESS, parser.parse(common::ObString::make_string(query), result));
    SqlInfo info;
    ParamStore params{ObWrapperAllocator(allocator)};
    ASSERT_EQ(common::OB_SUCCESS, ObSqlParameterization::transform_syntax_tree(
        allocator, session, nullptr, result.result_tree_, info, params, nullptr, false, TEXT_MODE))
        << query;
    EXPECT_EQ(0, params.count()) << query;
    EXPECT_GT(info.not_param_index_.num_members(), 0) << query;
    parser.free_result(result);
  }
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
