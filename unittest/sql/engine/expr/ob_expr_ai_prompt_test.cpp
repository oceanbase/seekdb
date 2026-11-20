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
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"
#include "lib/allocator/ob_malloc.h"
#include "share/json/ob_json_helper.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObExprAIPromptTest : public ::testing::Test
{
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ObExprAIPromptTest, ConstructPromptObjectAndReplace)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonArray *args_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));

  ObJsonString *arg1 = NULL;
  ObJsonString *arg2 = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("Hello"), arg1));
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("World"), arg2));
  ASSERT_EQ(OB_SUCCESS, args_array->append(arg1));
  ASSERT_EQ(OB_SUCCESS, args_array->append(arg2));

  ObString template_str("{0} {1}!");
  ObJsonObject *prompt_obj = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, template_str, args_array, prompt_obj));
  ASSERT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_obj));

  ObString replaced_str;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, prompt_obj, replaced_str));
  EXPECT_EQ(0, replaced_str.case_compare("Hello World!"));
}

TEST_F(ObExprAIPromptTest, RepeatedPlaceholders)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonArray *args_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));

  ObJsonString *arg0 = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("AI"), arg0));
  ASSERT_EQ(OB_SUCCESS, args_array->append(arg0));

  ObString template_str("{0} and {0} are great.");
  ObJsonObject *prompt_obj = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, template_str, args_array, prompt_obj));

  ObString replaced_str;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, prompt_obj, replaced_str));
  EXPECT_EQ(0, replaced_str.case_compare("AI and AI are great."));
}

TEST_F(ObExprAIPromptTest, NestedPromptObject)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  
  // Inner prompt object
  ObJsonArray *inner_args = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, inner_args));
  ObJsonString *inner_arg0 = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("OceanBase"), inner_arg0));
  ASSERT_EQ(OB_SUCCESS, inner_args->append(inner_arg0));
  ObString inner_template("Database: {0}");
  ObJsonObject *inner_prompt = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, inner_template, inner_args, inner_prompt));

  // Outer args containing inner prompt object
  ObJsonArray *outer_args = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, outer_args));
  ASSERT_EQ(OB_SUCCESS, outer_args->append(inner_prompt));

  ObString outer_template("Info: {0}");
  ObJsonObject *outer_prompt = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, outer_template, outer_args, outer_prompt));
  ASSERT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(outer_prompt));

  ObString replaced_str;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, outer_prompt, replaced_str));
  EXPECT_EQ(0, replaced_str.case_compare("Info: Database: OceanBase"));
}

TEST_F(ObExprAIPromptTest, InvalidOutOfBoundsIndices)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonArray *args_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));
  ObJsonString *arg0 = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("val"), arg0));
  ASSERT_EQ(OB_SUCCESS, args_array->append(arg0));

  // Out of bounds index {5}
  ObString template_out_of_bounds("Value: {5}");
  ObJsonObject *prompt_obj1 = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, template_out_of_bounds, args_array, prompt_obj1));
  ObString replaced_str1;
  EXPECT_NE(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, prompt_obj1, replaced_str1));

  // Malformed placeholder {abc}
  ObString template_malformed("Value: {abc}");
  ObJsonObject *prompt_obj2 = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, template_malformed, args_array, prompt_obj2));
  ObString replaced_str2;
  EXPECT_NE(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, prompt_obj2, replaced_str2));
}

TEST_F(ObExprAIPromptTest, TextLongTextAndUnicodeEscapes)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObJsonArray *args_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));

  ObJsonString *arg0 = NULL;
  // Unicode string representation "\u4e16\u754c" (世界)
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("\u4e16\u754c"), arg0));
  ASSERT_EQ(OB_SUCCESS, args_array->append(arg0));

  ObString template_str("Hello {0}!");
  ObJsonObject *prompt_obj = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::construct_prompt_object(allocator, template_str, args_array, prompt_obj));

  ObString replaced_str;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, prompt_obj, replaced_str));
  EXPECT_TRUE(replaced_str.length() > 0);
}
