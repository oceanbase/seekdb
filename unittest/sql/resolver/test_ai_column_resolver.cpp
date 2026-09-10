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

#define USING_LOG_PREFIX SQL_RESV

#include <gtest/gtest.h>
#include "sql/parser/ob_parser.h"
#include "sql/resolver/ddl/ob_ai_column_resolver.h"

using namespace oceanbase;
using namespace common;
using namespace share::schema;
using namespace sql;

namespace
{

const ParseNode *find_ai_column_definition(const ParseNode *node)
{
  const ParseNode *found = nullptr;
  if (ObAIColumnResolver::is_ai_column_definition(node)) {
    found = node;
  }
  for (int64_t i = 0; nullptr == found && nullptr != node
       && i < node->num_child_; ++i) {
    found = find_ai_column_definition(node->children_[i]);
  }
  return found;
}

void prepare_columns(ObColumnSchemaV2 &content,
                     ObColumnSchemaV2 &summary,
                     ObSEArray<const ObColumnSchemaV2 *, 2> &columns)
{
  content.set_table_id(500001);
  content.set_column_id(OB_APP_MIN_COLUMN_ID);
  content.set_data_type(ObLongTextType);
  ASSERT_EQ(OB_SUCCESS, content.set_column_name(ObString::make_string("content")));

  summary.set_table_id(500001);
  summary.set_column_id(OB_APP_MIN_COLUMN_ID + 1);
  summary.set_data_type(ObLongTextType);
  summary.set_charset_type(CHARSET_UTF8MB4);
  summary.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  summary.set_is_ai_column(true);
  ASSERT_EQ(OB_SUCCESS, summary.set_column_name(ObString::make_string("summary")));

  ASSERT_EQ(OB_SUCCESS, columns.push_back(&content));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(&summary));
}

int parse_and_resolve_definition(const char *sql,
                                 ObColumnSchemaV2 &content,
                                 ObColumnSchemaV2 &summary,
                                 ObAIColumnSchema &ai_schema,
                                 ObString &model_name,
                                 const bool content_is_ai = false,
                                 const bool summary_is_vector = false,
                                 const ObObjType summary_type = ObLongTextType)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator parser_allocator(ObModIds::TEST);
  ObParser parser(parser_allocator, SMO_DEFAULT);
  ParseResult result;
  ObSEArray<const ObColumnSchemaV2 *, 2> columns;
  prepare_columns(content, summary, columns);
  content.set_is_ai_column(content_is_ai);
  summary.set_data_type(summary_type);
  if (summary_is_vector) {
    ObSEArray<ObString, 1> extended_type_info;
    summary.set_data_type(ObCollectionSQLType);
    if (OB_FAIL(extended_type_info.push_back(
                    ObString::make_string("VECTOR(1024)")))) {
    } else if (OB_FAIL(summary.set_extended_type_info(extended_type_info))) {
    }
  }
  if (OB_FAIL(parser.parse(ObString::make_string(sql), result))) {
  } else {
    const ParseNode *definition = find_ai_column_definition(result.result_tree_);
    if (nullptr == definition) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = ObAIColumnResolver::resolve_definition(
          *definition, summary.get_table_id(), summary, columns,
          ai_schema, model_name);
    }
    parser.free_result(result);
  }
  return ret;
}

bool contains_text(const ObString &text, const char *needle)
{
  const int64_t needle_len = strlen(needle);
  bool found = false;
  for (int64_t i = 0;
       !found && i + needle_len <= text.length(); ++i) {
    found = 0 == MEMCMP(text.ptr() + i, needle, needle_len);
  }
  return found;
}

} // namespace

TEST(ObAIColumnResolver, resolve_definition_and_options)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObColumnSchemaV2 content;
  ObColumnSchemaV2 summary;
  ObAIColumnSchema ai_schema(&allocator);
  ObString model_name;
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary LONGTEXT AI COLUMN "
      "(AI_COMPLETE('m1', CONCAT('prefix:', content))) "
      "WITH (sync_mode='async', max_retries=7))",
      content, summary, ai_schema, model_name));
  EXPECT_EQ(0, model_name.case_compare("m1"));
  ASSERT_EQ(1, ai_schema.get_dependency_column_ids().count());
  EXPECT_EQ(content.get_column_id(), ai_schema.get_dependency_column_ids().at(0));
  EXPECT_EQ(7, ai_schema.get_max_retries());
  EXPECT_TRUE(summary.is_ai_column());
  EXPECT_FALSE(summary.is_generated_column());
}

TEST(ObAIColumnResolver, reject_ai_column_dependency)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObColumnSchemaV2 content;
  ObColumnSchemaV2 summary;
  ObAIColumnSchema ai_schema(&allocator);
  ObString model_name;
  EXPECT_EQ(OB_NOT_SUPPORTED, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary LONGTEXT AI COLUMN "
      "(AI_COMPLETE('m1', content)))",
      content, summary, ai_schema, model_name, true));
}

TEST(ObAIColumnResolver, accept_pipe_concat_but_reject_boolean_or)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObColumnSchemaV2 content;
  ObColumnSchemaV2 summary;
  ObAIColumnSchema ai_schema(&allocator);
  ObString model_name;
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary LONGTEXT AI COLUMN "
      "(AI_COMPLETE('m1', 'prefix:' || content)))",
      content, summary, ai_schema, model_name));
  ASSERT_EQ(1, ai_schema.get_dependency_column_ids().count());
  EXPECT_EQ(content.get_column_id(), ai_schema.get_dependency_column_ids().at(0));

  ObAIColumnSchema boolean_schema(&allocator);
  EXPECT_EQ(OB_NOT_SUPPORTED, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary LONGTEXT AI COLUMN "
      "(AI_COMPLETE('m1', content OR content)))",
      content, summary, boolean_schema, model_name));
}

TEST(ObAIColumnResolver, infer_embedding_dimension_from_target_column)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObColumnSchemaV2 content;
  ObColumnSchemaV2 summary;
  ObAIColumnSchema ai_schema(&allocator);
  ObString model_name;
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary VECTOR(1024) AI COLUMN "
      "(AI_EMBED('m1', content)))",
      content, summary, ai_schema, model_name, false, true));
  ASSERT_EQ(1, ai_schema.get_dependency_column_ids().count());

  ObAIColumnSchema mismatch_schema(&allocator);
  EXPECT_EQ(OB_ERR_INVALID_VECTOR_DIM, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary VECTOR(1024) AI COLUMN "
      "(AI_EMBED('m1', content, 512)))",
      content, summary, mismatch_schema, model_name, false, true));
}

TEST(ObAIColumnResolver, add_type_system_prompt_to_normalized_expression)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObColumnSchemaV2 content;
  ObColumnSchemaV2 summary;
  ObAIColumnSchema json_schema(&allocator);
  ObString model_name;
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary JSON AI COLUMN "
      "(AI_COMPLETE('m1', content)))",
      content, summary, json_schema, model_name,
      false, false, ObJsonType));
  EXPECT_FALSE(contains_text(json_schema.get_expr_text(),
                             "__ob_ai_system_prompt"));
  EXPECT_TRUE(contains_text(json_schema.get_normalized_expr(),
                            "__ob_ai_system_prompt"));
  EXPECT_TRUE(contains_text(json_schema.get_normalized_expr(),
                            "Return exactly one valid JSON value"));

  ObAIColumnSchema integer_schema(&allocator);
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary BIGINT AI COLUMN "
      "(AI_COMPLETE('m1', content)))",
      content, summary, integer_schema, model_name,
      false, false, ObIntType));
  EXPECT_TRUE(contains_text(integer_schema.get_normalized_expr(),
                            "base-10 integer"));

  ObAIColumnSchema date_schema(&allocator);
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary DATE AI COLUMN "
      "(AI_COMPLETE('m1', content)))",
      content, summary, date_schema, model_name,
      false, false, ObDateType));
  EXPECT_TRUE(contains_text(date_schema.get_normalized_expr(),
                            "YYYY-MM-DD"));

  ObAIColumnSchema string_schema(&allocator);
  ASSERT_EQ(OB_SUCCESS, parse_and_resolve_definition(
      "CREATE TABLE docs (content LONGTEXT, summary LONGTEXT AI COLUMN "
      "(AI_COMPLETE('m1', content)))",
      content, summary, string_schema, model_name));
  EXPECT_FALSE(contains_text(string_schema.get_normalized_expr(),
                             "__ob_ai_system_prompt"));
}
