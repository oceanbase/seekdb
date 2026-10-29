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

#include <string>
#include <gtest/gtest.h>
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "query/api/query/parser/parse_node.h"
#include "sql/parser/ob_parser.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{

// Parses one statement and hands back the root node. The result tree lives in
// the allocator owned by the fixture, so nothing is freed per case.
class ParserTest : public ::testing::Test
{
protected:
  void SetUp() override { allocator_.init(); }
  void TearDown() override { allocator_.reset(); }

  // Returns the statement root, or nullptr when the statement does not parse.
  ParseNode *parse(const std::string &sql)
  {
    ParseResult result;
    ObParser parser(allocator_, DEFAULT_MYSQL_MODE);
    ObString query(static_cast<int32_t>(sql.size()), sql.c_str());
    if (OB_SUCCESS != parser.parse(query, result)) {
      return nullptr;
    }
    return result.result_tree_;
  }

  ObArenaAllocator allocator_;
};

// True when the subtree contains a node of the given type.
bool contains(const ParseNode *node, const ObItemType type)
{
  bool found = false;
  if (node != nullptr) {
    if (node->type_ == type) {
      found = true;
    } else {
      for (int32_t i = 0; !found && i < node->num_child_; ++i) {
        found = contains(node->children_[i], type);
      }
    }
  }
  return found;
}

// The statement kind of the first select in a statement, or T_INVALID.
ObItemType table_expression_type(ParseNode *root)
{
  ObItemType type = T_INVALID;
  if (root != nullptr && root->num_child_ > 0) {
    const ParseNode *stmt = root->children_[0];
    if (stmt != nullptr && contains(stmt, T_FILE_TABLE_EXPRESSION)) {
      type = T_FILE_TABLE_EXPRESSION;
    }
  }
  return type;
}

} // namespace

// read_csv, read_json, read_ndjson and read_parquet are non-reserved keywords,
// so each of them starts a file table expression.
TEST_F(ParserTest, read_functions_parse)
{
  const char *sqls[] = {
    "select * from read_csv('/tmp/a.csv')",
    "select * from read_json('/tmp/a.jsonl')",
    "select * from read_ndjson('/tmp/a.jsonl')",
    "select * from read_parquet('/tmp/a.parquet')",
  };
  for (const char *sql : sqls) {
    ParseNode *root = parse(sql);
    ASSERT_NE(nullptr, root) << sql;
    EXPECT_EQ(T_FILE_TABLE_EXPRESSION, table_expression_type(root)) << sql;
  }
}

// The four names stay usable as identifiers elsewhere, which is why they were
// added as non-reserved keywords rather than reserved ones.
TEST_F(ParserTest, names_usable_as_identifiers)
{
  const char *sqls[] = {
    "create table t_read_csv(id int)",
    "create table t_read_json(id int)",
    "create table t_read_ndjson(id int)",
    "create table t_read_parquet(id int)",
  };
  for (const char *sql : sqls) {
    EXPECT_NE(nullptr, parse(sql)) << sql;
  }
}

// An alias after the closing parenthesis names the table and must not be parsed
// as a column reference of the file function.
TEST_F(ParserTest, alias_is_accepted)
{
  EXPECT_NE(nullptr, parse("select * from read_csv('/tmp/a.csv') f"));
  EXPECT_NE(nullptr, parse("select f.id from read_csv('/tmp/a.csv') as f"));
}

// Named options are ordinary comparisons inside the argument list, so they reach
// the resolver as T_OP_EQ nodes.
TEST_F(ParserTest, named_options_parse)
{
  EXPECT_NE(nullptr, parse("select * from read_csv('/tmp/a.csv', header = true)"));
  EXPECT_NE(nullptr, parse("select * from read_csv('/tmp/a.csv', header = true, sample_size = -1)"));
  EXPECT_NE(nullptr, parse("select * from read_json('/tmp/a.jsonl', format = 'newline_delimited')"));
}

// glob() is a file table too, with a single `file` column.
TEST_F(ParserTest, glob_parses)
{
  ParseNode *root = parse("select * from glob('/tmp/*')");
  ASSERT_NE(nullptr, root);
  EXPECT_EQ(T_FILE_TABLE_EXPRESSION, table_expression_type(root));
}

// DESCRIBE over a file table query is how the inferred schema is inspected, so
// it must parse as a describe statement rather than a plain select.
TEST_F(ParserTest, describe_select_parses)
{
  ParseNode *root = parse("describe select * from read_csv('/tmp/a.csv')");
  ASSERT_NE(nullptr, root);
  EXPECT_TRUE(contains(root, T_FILE_TABLE_EXPRESSION));
}

// COPY (query) TO 'path' (options) is the export direction. The option names are
// plain identifiers, so no new keyword is needed.
TEST_F(ParserTest, copy_to_parses)
{
  EXPECT_NE(nullptr, parse("copy (select 1) to '/tmp/out.csv'"));
  EXPECT_NE(nullptr, parse("copy (select 1) to '/tmp/out.csv' (format csv)"));
  EXPECT_NE(nullptr, parse("copy (select 1 as a) to '/tmp/out.csv' (format csv, header, nullstr 'NULL')"));
}

// A second positional argument is accepted by the grammar; the resolver decides
// whether it names the same format as the function.
TEST_F(ParserTest, second_positional_argument_parses)
{
  EXPECT_NE(nullptr, parse("select * from read_csv('/tmp/a.csv', 'csv')"));
  EXPECT_NE(nullptr, parse("select * from read_csv('/tmp/a.csv', 'xml')"));
}

// Ordinary SQL must keep parsing exactly as before.
TEST_F(ParserTest, plain_sql_unaffected)
{
  EXPECT_NE(nullptr, parse("select 1"));
  EXPECT_NE(nullptr, parse("select 1 from dual"));
  EXPECT_NE(nullptr, parse("select id, count(*) from t group by id having count(*) > 1"));
  EXPECT_EQ(T_FILE_TABLE_EXPRESSION, table_expression_type(parse("select * from glob('/tmp/*')")));
  EXPECT_EQ(T_INVALID, table_expression_type(parse("select * from t")));
}

// A statement that does not close its parenthesis must fail to parse instead of
// producing a partial tree.
TEST_F(ParserTest, malformed_input_rejected)
{
  EXPECT_EQ(nullptr, parse("select * from read_csv('/tmp/a.csv'"));
  EXPECT_EQ(nullptr, parse("select * from read_csv"));
}
