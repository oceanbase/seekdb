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

#define USING_LOG_PREFIX SQL

#include <gtest/gtest.h>
#include "sql/parser/ob_parser.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{

bool contains_node_type(const ParseNode *node, const ObItemType type)
{
  bool found = NULL != node && type == node->type_;
  for (int64_t i = 0; !found && NULL != node && i < node->num_child_; ++i) {
    found = contains_node_type(node->children_[i], type);
  }
  return found;
}

void parse_and_expect(ObArenaAllocator &allocator,
                      const char *sql,
                      const ObItemType expected_type)
{
  ObParser parser(allocator, SMO_DEFAULT);
  ParseResult result;
  ASSERT_EQ(OB_SUCCESS, parser.parse(ObString::make_string(sql), result)) << sql;
  ASSERT_NE(nullptr, result.result_tree_) << sql;
  EXPECT_TRUE(contains_node_type(result.result_tree_, expected_type)) << sql;
  parser.free_result(result);
}

} // namespace

TEST(ObAIColumnParser, create_add_and_modify)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  parse_and_expect(
      allocator,
      "CREATE TABLE docs (id BIGINT PRIMARY KEY, content LONGTEXT, "
      "summary LONGTEXT AI COLUMN (AI_COMPLETE('m1', content)) "
      "WITH (sync_mode='async', max_retries=5))",
      T_AI_COLUMN_DEFINITION);
  parse_and_expect(
      allocator,
      "ALTER TABLE docs ADD COLUMN embedding VECTOR(16) AI COLUMN "
      "(AI_EMBED('m2', content, 16)) WITH (max_retries=3)",
      T_AI_COLUMN_DEFINITION);
  parse_and_expect(
      allocator,
      "ALTER TABLE docs MODIFY COLUMN summary LONGTEXT AI COLUMN "
      "(AI_COMPLETE('m3', content)) WITH (max_retries=8)",
      T_AI_COLUMN_DEFINITION);
}

TEST(ObAIColumnParser, show)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  parse_and_expect(allocator,
                   "SHOW AI COLUMNS FROM docs",
                   T_SHOW_AI_COLUMNS);
}
