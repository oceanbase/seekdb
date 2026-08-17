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
#include "ob_expr_test_utils.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObAIPromptTest: public ::testing::Test
{
public:
  ObAIPromptTest();
  virtual ~ObAIPromptTest();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  ObAIPromptTest(const ObAIPromptTest &other);
  ObAIPromptTest& operator=(const ObAIPromptTest &other);
protected:
  // data members
};

ObAIPromptTest::ObAIPromptTest()
{
}

ObAIPromptTest::~ObAIPromptTest()
{
}

void ObAIPromptTest::SetUp()
{
}

void ObAIPromptTest::TearDown()
{
}

TEST_F(ObAIPromptTest, test_ob_is_vaild_prompt_object)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObString a("a");
  ObJsonString a_str(a);
  ObString raw_str("{\"template\": \"{0}+{1}={2} 吗？请回答true或false\", \"args\": [\"1\", \"2\", \"3\"]}");
  ObJsonObject *prompt_object = NULL;
  ObString prompt_str;
  ObAIFuncJsonUtils::get_json_object_form_str(allocator, raw_str, prompt_object);
  ASSERT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_object));
  prompt_object->add("a",&a_str);
  ASSERT_FALSE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_object));
}

TEST_F(ObAIPromptTest, test_replace_all_str_args_in_template)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObString a("a");
  ObJsonString a_str(a);
  ObString raw_str("{\"template\": \"{0}+{1}={2} 吗？请回答true或false\", \"args\": [\"1\", \"2\", \"3\"]}");
  ObJsonObject *prompt_object = NULL;
  ObString prompt_str;
  ObString result_str;
  ObAIFuncJsonUtils::get_json_object_form_str(allocator, raw_str, prompt_object);
  ASSERT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_object));
  ASSERT_EQ(OB_SUCCESS, ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, prompt_object, prompt_str));
  ob_write_string(allocator, prompt_str, result_str,true);
  ASSERT_EQ(result_str, "1+2=3 吗？请回答true或false");
  // std::cout << "prompt_str: " << prompt_str.ptr() << std::endl;
  // std::cout << "result_str: " << result_str.ptr() << std::endl;
}

TEST_F(ObAIPromptTest, test_repeated_and_json_arguments)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObString raw_str(
      "{\"template\":\"{0}/{0}/{1}/{2}\","
      "\"args\":[\"oceanbase\",{\"version\":4},[1,true,null]]}");
  ObJsonObject *prompt_object = nullptr;
  ObString prompt_str;
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncJsonUtils::get_json_object_form_str(allocator, raw_str, prompt_object));
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                allocator, prompt_object, prompt_str));
  ASSERT_EQ(ObString("oceanbase/oceanbase/{\"version\": 4}/[1, true, null]"), prompt_str);
}

TEST_F(ObAIPromptTest, test_nested_prompt_and_invalid_placeholder)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObString nested_raw(
      "{\"template\":\"nested={0}; again={0}\",\"args\":[{"
      "\"template\":\"{0}-{1}\",\"args\":[\"a\",{\"k\":2}]}]}");
  ObJsonObject *nested_object = nullptr;
  ObString prompt_str;
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncJsonUtils::get_json_object_form_str(allocator, nested_raw, nested_object));
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                allocator, nested_object, prompt_str));
  ASSERT_EQ(ObString("nested=a-{\"k\": 2}; again=a-{\"k\": 2}"), prompt_str);

  ObString invalid_raw("{\"template\":\"{1}\",\"args\":[\"only-zero\"]}");
  ObJsonObject *invalid_object = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncJsonUtils::get_json_object_form_str(allocator, invalid_raw, invalid_object));
  ASSERT_EQ(OB_INVALID_ARGUMENT,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                allocator, invalid_object, prompt_str));
}
