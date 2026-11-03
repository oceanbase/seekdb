/**
 * (c) 2025 OceanBase
 * The copyright of OceanBase is the property of OceanBase Technology Inc.
 * The copyrighted work of licensed material is the property of OceanBase Technology Inc.
 * Unpublished rights reserved under the copyright laws of the United States.
 */

#include <gtest/gtest.h>
#include "lib/allocator/page_arena.h"
#include "sql/parser/ob_parser.h"
#include "sql/parser/parse_node.h"
#include "objit/common/ob_item_type.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

class TestFileSqlParser : public ::testing::Test
{
public:
  TestFileSqlParser() :
      allocator_(ObModIds::TEST),
      parser_(allocator_, DEFAULT_MYSQL_MODE)
  {
  }

  virtual ~TestFileSqlParser() {}

  int parse(const char *sql, ParseResult &result)
  {
    ObString sql_str = ObString::make_string(sql);
    return parser_.parse(sql_str, result);
  }

  // find the first node with the given type in the parse tree
  static const ParseNode *find_node(const ParseNode *root, ObItemType type)
  {
    const ParseNode *found = NULL;
    if (NULL != root) {
      if (type == root->type_) {
        found = root;
      }
      for (int32_t i = 0; NULL == found && i < root->num_child_; ++i) {
        found = find_node(root->children_[i], type);
      }
    }
    return found;
  }

  static int64_t count_node(const ParseNode *root, ObItemType type)
  {
    int64_t count = 0;
    if (NULL != root) {
      count += (type == root->type_) ? 1 : 0;
      for (int32_t i = 0; i < root->num_child_; ++i) {
        count += count_node(root->children_[i], type);
      }
    }
    return count;
  }

  static bool name_equals(const ParseNode *node, const char *name)
  {
    return NULL != node && NULL != node->str_value_
        && strlen(name) == static_cast<size_t>(node->str_len_)
        && 0 == strncasecmp(node->str_value_, name, node->str_len_);
  }

  static void expect_str_value(const ParseNode *node, const char *expect)
  {
    ASSERT_TRUE(NULL != node);
    if (NULL != node) {
      ASSERT_TRUE(NULL != node->str_value_);
      if (NULL != node->str_value_) {
        EXPECT_EQ(strlen(expect), static_cast<size_t>(node->str_len_));
        if (strlen(expect) == static_cast<size_t>(node->str_len_)) {
          EXPECT_EQ(0, strncmp(expect, node->str_value_, node->str_len_));
        }
      }
    }
  }

  // a read function in FROM clause is wrapped by T_ORG or T_ALIAS
  static const ParseNode *find_read_file_wrapper(const ParseNode *root,
                                                 const ParseNode *&func_node)
  {
    const ParseNode *wrapper = NULL;
    func_node = NULL;
    if (NULL != root) {
      const ParseNode *child = (root->num_child_ > 0) ? root->children_[0] : NULL;
      if (T_READ_FILE_FUNCTION == root->type_) {
        wrapper = root;
        func_node = root;
      } else if ((T_ORG == root->type_ || T_ALIAS == root->type_)
                 && NULL != child && T_READ_FILE_FUNCTION == child->type_) {
        wrapper = root;
        func_node = child;
      } else {
        for (int32_t i = 0; NULL == wrapper && i < root->num_child_; ++i) {
          wrapper = find_read_file_wrapper(root->children_[i], func_node);
        }
      }
    }
    return wrapper;
  }

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestFileSqlParser);
protected:
  ObArenaAllocator allocator_;
  ObParser parser_;
};

// read_csv('path'): the simplest form, one positional argument
TEST_F(TestFileSqlParser, test_read_csv_positional_arg)
{
  ParseResult result;
  ASSERT_EQ(OB_SUCCESS, parse("select * from read_csv('/tmp/t.csv')", result));

  const ParseNode *func_node = NULL;
  const ParseNode *wrapper = find_read_file_wrapper(result.result_tree_, func_node);
  ASSERT_TRUE(NULL != wrapper);
  ASSERT_TRUE(NULL != func_node);
  EXPECT_EQ(T_READ_FILE_FUNCTION, func_node->type_);
  EXPECT_EQ(2, func_node->num_child_);
  EXPECT_EQ(T_EXTERNAL_FILE_LOCATION, wrapper->value_);
  expect_str_value(func_node->children_[0], "read_csv");

  const ParseNode *args = func_node->children_[1];
  ASSERT_TRUE(NULL != args);
  EXPECT_EQ(T_READ_FILE_ARGS, args->type_);
  EXPECT_EQ(1, args->num_child_);
  ASSERT_TRUE(NULL != args->children_[0]);
  EXPECT_EQ(T_VARCHAR, args->children_[0]->type_);
  expect_str_value(args->children_[0], "/tmp/t.csv");
  parser_.free_result(result);
}

// named arguments with ':='
TEST_F(TestFileSqlParser, test_read_csv_named_args)
{
  ParseResult result;
  const char *sql = "select * from read_csv('/tmp/t.csv', delimiter := '|', header := true)";
  ASSERT_EQ(OB_SUCCESS, parse(sql, result));

  const ParseNode *func_node = NULL;
  ASSERT_TRUE(NULL != find_read_file_wrapper(result.result_tree_, func_node));
  const ParseNode *args = func_node->children_[1];
  ASSERT_TRUE(NULL != args);
  EXPECT_EQ(2, args->num_child_);

  EXPECT_EQ(T_VARCHAR, args->children_[0]->type_);
  expect_str_value(args->children_[0], "/tmp/t.csv");

  const ParseNode *named = args->children_[1];
  ASSERT_TRUE(NULL != named);
  EXPECT_EQ(T_READ_FILE_NAMED_ARG, named->type_);
  EXPECT_EQ(2, named->num_child_);
  expect_str_value(named->children_[0], "delimiter");
  ASSERT_TRUE(NULL != named->children_[1]);
  EXPECT_EQ(T_VARCHAR, named->children_[1]->type_);
  expect_str_value(named->children_[1], "|");

  named = args->children_[2];
  ASSERT_TRUE(NULL != named);
  EXPECT_EQ(T_READ_FILE_NAMED_ARG, named->type_);
  expect_str_value(named->children_[0], "header");
  ASSERT_TRUE(NULL != named->children_[1]);
  EXPECT_EQ(T_BOOL, named->children_[1]->type_);
  EXPECT_EQ(1, named->children_[1]->value_);
  parser_.free_result(result);
}

// option names which are non-reserved keywords, and the reserved word NULL
TEST_F(TestFileSqlParser, test_read_csv_keyword_option_names)
{
  const char *sqls[] = {
    "select * from read_csv('/tmp/t.csv', escape := '\\\\')",
    "select * from read_csv('/tmp/t.csv', compression := 'gzip')",
    "select * from read_csv('/tmp/t.csv', encoding := 'utf8mb4')",
    "select * from read_csv('/tmp/t.csv', null := '\\\\N')",
    "select * from read_csv('/tmp/t.csv', quote := '\"')",
    "select * from read_json('/tmp/t.jsonl', sample_size := 1)",
  };
  const char *names[] = {"escape", "compression", "encoding", "null", "quote", "sample_size"};
  for (int64_t i = 0; i < static_cast<int64_t>(ARRAYSIZEOF(sqls)); ++i) {
    ParseResult result;
    ASSERT_EQ(OB_SUCCESS, parse(sqls[i], result)) << sqls[i];
    const ParseNode *func_node = NULL;
    ASSERT_TRUE(NULL != find_read_file_wrapper(result.result_tree_, func_node)) << sqls[i];
    const ParseNode *args = func_node->children_[1];
    ASSERT_TRUE(NULL != args && 2 == args->num_child_) << sqls[i];
    const ParseNode *named = args->children_[1];
    ASSERT_TRUE(NULL != named && T_READ_FILE_NAMED_ARG == named->type_) << sqls[i];
    EXPECT_TRUE(name_equals(named->children_[0], names[i])) << sqls[i];
    EXPECT_TRUE(NULL != named->children_[1]) << sqls[i];
    parser_.free_result(result);
  }
}

// alias with and without AS
TEST_F(TestFileSqlParser, test_read_file_with_alias)
{
  const char *sqls[] = {
    "select a.c1 from read_csv('/tmp/t.csv') as a",
    "select a.c1 from read_csv('/tmp/t.csv') a",
  };
  for (int64_t i = 0; i < static_cast<int64_t>(ARRAYSIZEOF(sqls)); ++i) {
    ParseResult result;
    ASSERT_EQ(OB_SUCCESS, parse(sqls[i], result)) << sqls[i];
    const ParseNode *func_node = NULL;
    const ParseNode *wrapper = find_read_file_wrapper(result.result_tree_, func_node);
    ASSERT_TRUE(NULL != wrapper && NULL != func_node) << sqls[i];
    EXPECT_EQ(T_ALIAS, wrapper->type_) << sqls[i];
    EXPECT_EQ(5, wrapper->num_child_) << sqls[i];
    EXPECT_TRUE(name_equals(wrapper->children_[1], "a")) << sqls[i];
    EXPECT_EQ(T_EXTERNAL_FILE_LOCATION, wrapper->value_) << sqls[i];
    parser_.free_result(result);
  }
}

// all supported table functions
TEST_F(TestFileSqlParser, test_read_file_function_names)
{
  const char *sqls[] = {
    "select * from read_csv('/tmp/t.csv')",
    "select * from read_json('/tmp/t.jsonl')",
    "select * from read_ndjson('/tmp/t.ndjson')",
    "select * from read_parquet('/tmp/t.parquet')",
    "select * from glob('/tmp/*.csv')",
    "select * from READ_CSV('/tmp/t.csv')",
  };
  for (int64_t i = 0; i < static_cast<int64_t>(ARRAYSIZEOF(sqls)); ++i) {
    ParseResult result;
    ASSERT_EQ(OB_SUCCESS, parse(sqls[i], result)) << sqls[i];
    const ParseNode *func_node = NULL;
    ASSERT_TRUE(NULL != find_read_file_wrapper(result.result_tree_, func_node)) << sqls[i];
    parser_.free_result(result);
  }
}

// read functions in join and sub query
TEST_F(TestFileSqlParser, test_read_file_in_join)
{
  ParseResult result;
  const char *sql = "select a.id, b.name from read_csv('/tmp/a.csv') a "
                    "inner join read_parquet('/tmp/b.parquet') b on a.id = b.id";
  ASSERT_EQ(OB_SUCCESS, parse(sql, result));
  EXPECT_EQ(2, count_node(result.result_tree_, T_READ_FILE_FUNCTION));
  EXPECT_EQ(1, count_node(result.result_tree_, T_JOINED_TABLE));
  parser_.free_result(result);

  ParseResult result2;
  const char *sql2 = "select * from (select * from read_csv('/tmp/a.csv')) t";
  ASSERT_EQ(OB_SUCCESS, parse(sql2, result2));
  EXPECT_EQ(1, count_node(result2.result_tree_, T_READ_FILE_FUNCTION));
  parser_.free_result(result2);
}

// glob() takes a pattern and returns one column
TEST_F(TestFileSqlParser, test_glob_function)
{
  ParseResult result;
  ASSERT_EQ(OB_SUCCESS, parse("select file from glob('/tmp/data/*.csv')", result));
  const ParseNode *func_node = NULL;
  ASSERT_TRUE(NULL != find_read_file_wrapper(result.result_tree_, func_node));
  expect_str_value(func_node->children_[0], "glob");
  const ParseNode *args = func_node->children_[1];
  ASSERT_TRUE(NULL != args);
  EXPECT_EQ(1, args->num_child_);
  expect_str_value(args->children_[0], "/tmp/data/*.csv");
  parser_.free_result(result);
}

// DESCRIBE SELECT ... is marked as a column metadata request
TEST_F(TestFileSqlParser, test_describe_select)
{
  ParseResult result;
  ASSERT_EQ(OB_SUCCESS, parse("describe select * from read_csv('/tmp/t.csv')", result));
  const ParseNode *explain_node = find_node(result.result_tree_, T_EXPLAIN);
  ASSERT_TRUE(NULL != explain_node);
  EXPECT_EQ(1, explain_node->value_);
  EXPECT_EQ(1, count_node(result.result_tree_, T_READ_FILE_FUNCTION));
  parser_.free_result(result);

  // EXPLAIN SELECT keeps the normal explain behavior
  ParseResult result2;
  ASSERT_EQ(OB_SUCCESS, parse("explain select * from read_csv('/tmp/t.csv')", result2));
  const ParseNode *explain_node2 = find_node(result2.result_tree_, T_EXPLAIN);
  ASSERT_TRUE(NULL != explain_node2);
  EXPECT_EQ(0, explain_node2->value_);
  parser_.free_result(result2);

  // DESCRIBE a table name keeps the old shape
  ParseResult result3;
  ASSERT_EQ(OB_SUCCESS, parse("describe t1", result3));
  const ParseNode *show_columns = find_node(result3.result_tree_, T_SHOW_COLUMNS);
  ASSERT_TRUE(NULL != show_columns);
  EXPECT_EQ(0, count_node(result3.result_tree_, T_READ_FILE_FUNCTION));
  parser_.free_result(result3);
}

// COPY table or COPY (query) TO path with options
TEST_F(TestFileSqlParser, test_copy_stmt)
{
  const char *sqls[] = {
    "copy t1 to '/tmp/out.csv'",
    "copy (select * from t1) to '/tmp/out.csv'",
    "copy t1 to '/tmp/out.csv' (header true)",
    "copy t1 to '/tmp/out.csv' (delimiter ',', quote '\"', escape '\\\\', null '\\\\N')",
    "copy (select c1 from read_csv('/tmp/a.csv')) to '/tmp/out.csv' (header false)",
  };
  for (int64_t i = 0; i < static_cast<int64_t>(ARRAYSIZEOF(sqls)); ++i) {
    ParseResult result;
    ASSERT_EQ(OB_SUCCESS, parse(sqls[i], result)) << sqls[i];
    const ParseNode *copy_node = find_node(result.result_tree_, T_COPY);
    ASSERT_TRUE(NULL != copy_node) << sqls[i];
    EXPECT_EQ(3, copy_node->num_child_) << sqls[i];
    ASSERT_TRUE(NULL != copy_node->children_[1]) << sqls[i];
    EXPECT_EQ(T_VARCHAR, copy_node->children_[1]->type_) << sqls[i];
    expect_str_value(copy_node->children_[1], "/tmp/out.csv");
    parser_.free_result(result);
  }

  ParseResult result;
  const char *sql = "copy (select * from t1) to '/tmp/out.csv' (header true, delimiter ',')";
  ASSERT_EQ(OB_SUCCESS, parse(sql, result));
  const ParseNode *copy_node = find_node(result.result_tree_, T_COPY);
  ASSERT_TRUE(NULL != copy_node);
  const ParseNode *options = copy_node->children_[2];
  ASSERT_TRUE(NULL != options);
  EXPECT_EQ(T_COPY_OPTIONS, options->type_);
  EXPECT_EQ(2, options->num_child_);
  EXPECT_EQ(T_COPY_OPTION, options->children_[0]->type_);
  expect_str_value(options->children_[0]->children_[0], "header");
  ASSERT_TRUE(NULL != options->children_[0]->children_[1]);
  EXPECT_EQ(T_BOOL, options->children_[0]->children_[1]->type_);
  EXPECT_EQ(1, options->children_[0]->children_[1]->value_);
  expect_str_value(options->children_[1]->children_[0], "delimiter");
  expect_str_value(options->children_[1]->children_[1], ",");
  parser_.free_result(result);
}

// existing syntax must not be affected by the new table factor rules
TEST_F(TestFileSqlParser, test_no_regression_on_existing_syntax)
{
  const char *sqls[] = {
    "select 1",
    "select * from t1",
    "select * from t1 as a join t2 b on a.c1 = b.c1",
    "select * from db.t1 where c1 = 1 order by c2 limit 10",
    "select count(*), sum(c1) from t1 group by c2 having sum(c1) > 1",
    "insert into t1 values (1, 2)",
    "update t1 set c1 = 1 where c2 = 2",
    "delete from t1 where c1 = 1",
    "select * from t1 where c1 in (select c1 from t2)",
    "create table t1 (c1 int primary key, c2 varchar(64))",
    "select locate('bc', 'abcd') from dual",
    "select * from files(location = 's3://b/p' format = (type = 'csv')) f",
    "select load_file('/tmp/a') from dual",
    "select * from t1 read_consistency(weak)",
    "explain extended select * from t1",
    "explain select * from t1",
    "desc t1",
    "describe t1",
    "show columns from t1",
    "select a from (select 1 as a) x",
    "select * from t1 partition(p0)",
    "load data infile '/tmp/a.csv' into table t1",
    "select * from t1 into outfile '/tmp/a.csv'",
  };
  for (int64_t i = 0; i < static_cast<int64_t>(ARRAYSIZEOF(sqls)); ++i) {
    ParseResult result;
    int ret = parse(sqls[i], result);
    ASSERT_EQ(OB_SUCCESS, ret) << sqls[i] << " ret=" << ret;
    EXPECT_EQ(0, count_node(result.result_tree_, T_READ_FILE_FUNCTION)) << sqls[i];
    EXPECT_EQ(0, count_node(result.result_tree_, T_COPY)) << sqls[i];
    parser_.free_result(result);
  }
}

// invalid forms are still rejected by the parser
TEST_F(TestFileSqlParser, test_invalid_forms)
{
  const char *sqls[] = {
    "select * from read_csv()",
    "select * from read_csv(1)",
    "select * from read_csv('/tmp/t.csv',)",
    "select * from read_csv('/tmp/t.csv' delimiter := ',')",
    "copy t1 to out.csv",
    "copy t1",
    "copy (select * from t1)",
  };
  for (int64_t i = 0; i < static_cast<int64_t>(ARRAYSIZEOF(sqls)); ++i) {
    ParseResult result;
    int ret = parse(sqls[i], result);
    ASSERT_NE(OB_SUCCESS, ret) << sqls[i];
    parser_.free_result(result);
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
