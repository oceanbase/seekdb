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
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

namespace oceanbase
{
namespace common
{

namespace
{

std::string to_std_string(const ObString &str)
{
  return str.empty() ? std::string() : std::string(str.ptr(), static_cast<size_t>(str.length()));
}

} // namespace

// The template replacer is the piece of ai_prompt which decides what the model
// actually receives, so it is tested on its own instead of going through a
// mocked http service.
class TestObExprAIPrompt : public ::testing::Test
{
public:
  TestObExprAIPrompt() : allocator_() {}
  virtual ~TestObExprAIPrompt() {}
  virtual void SetUp() { allocator_.reset(); }
  virtual void TearDown() { allocator_.reset(); }

protected:
  // Build a prompt object from a json literal, e.g.
  //   {"template": "{0}+{1}", "args": ["1", "2"]}
  int build_prompt(const char *json_literal, ObJsonObject *&prompt)
  {
    int ret = OB_SUCCESS;
    ObString json_str = ObString::make_string(json_literal);
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(allocator_, json_str, prompt))) {
    }
    return ret;
  }

  int render(const char *json_literal, ObString &result)
  {
    int ret = OB_SUCCESS;
    ObJsonObject *prompt = NULL;
    if (OB_FAIL(build_prompt(json_literal, prompt))) {
    } else if (OB_FAIL(ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                 allocator_, prompt, result))) {
    }
    return ret;
  }

  // Assert that the template is expanded into `expected` exactly.
  void expect_render(const char *json_literal, const char *expected)
  {
    ObString result;
    ASSERT_EQ(OB_SUCCESS, render(json_literal, result)) << json_literal;
    EXPECT_EQ(std::string(expected), to_std_string(result)) << json_literal;
  }

  // Assert that the template is kept verbatim, malformed placeholders included.
  void expect_untouched(const char *json_literal, const char *expected)
  {
    expect_render(json_literal, expected);
  }

protected:
  ObArenaAllocator allocator_;
};

TEST_F(TestObExprAIPrompt, replace_all_placeholders)
{
  expect_render("{\"template\": \"{0}+{1}={2}\", \"args\": [\"a\", \"b\", \"c\"]}",
                "a+b=c");
}

TEST_F(TestObExprAIPrompt, one_argument_can_be_referenced_several_times)
{
  expect_render("{\"template\": \"{0}-{0}-{0}\", \"args\": [\"xy\"]}", "xy-xy-xy");
}

TEST_F(TestObExprAIPrompt, repeated_placeholder_with_long_argument)
{
  // 20 references to a 4096 byte argument: the result is much larger than
  // template_length + sum(argument_length), which used to look like an
  // overflow of the pre-allocated buffer.
  const std::string long_arg(4096, 'x');
  std::string json = "{\"template\": \"";
  for (int i = 0; i < 20; i++) {
    json.append("{0}");
  }
  json.append("\", \"args\": [\"");
  json.append(long_arg);
  json.append("\"]}");

  ObString result;
  ASSERT_EQ(OB_SUCCESS, render(json.c_str(), result));
  std::string expected;
  for (int i = 0; i < 20; i++) {
    expected.append(long_arg);
  }
  ASSERT_EQ(expected.size(), static_cast<size_t>(result.length()));
  EXPECT_EQ(expected, to_std_string(result));
}

TEST_F(TestObExprAIPrompt, mixed_order_and_gaps_between_arguments)
{
  expect_render("{\"template\": \"{2}{0}{1}{0}\", \"args\": [\"A\", \"B\", \"C\"]}",
                "CABA");
}

TEST_F(TestObExprAIPrompt, out_of_range_placeholder_is_rejected)
{
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            render("{\"template\": \"{1}\", \"args\": [\"only-one\"]}", result));
}

TEST_F(TestObExprAIPrompt, out_of_range_placeholder_is_rejected_even_without_arguments)
{
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT, render("{\"template\": \"{0}\", \"args\": []}", result));
}

TEST_F(TestObExprAIPrompt, malformed_placeholders_are_kept_as_text)
{
  // {}      -> no index
  // {a}     -> not a digit
  // {0 }    -> trailing space before the brace
  // {-1}    -> negative
  // { 0}    -> leading space
  // {0      -> unterminated
  expect_untouched("{\"template\": \"{}|{a}|{0 }|{-1}|{ 0}|{0\", \"args\": [\"v\"]}",
                   "{}|{a}|{0 }|{-1}|{ 0}|{0");
}

TEST_F(TestObExprAIPrompt, brace_without_index_is_literal_text)
{
  expect_untouched("{\"template\": \"select '{}' as x, '{1}' as y\", \"args\": [\"v\"]}",
                   "select '{}' as x, '{1}' as y");
}

TEST_F(TestObExprAIPrompt, overflowed_index_is_literal_text)
{
  // A 20 digit index does not fit into int64, it must not be treated as a
  // placeholder (nor wrap around into a small valid index).
  expect_untouched("{\"template\": \"{99999999999999999999}\", \"args\": [\"v\"]}",
                   "{99999999999999999999}");
}

TEST_F(TestObExprAIPrompt, json_object_argument_is_serialized)
{
  expect_render("{\"template\": \"cfg={0}\", \"args\": [{\"a\": 1}]}", "cfg={\"a\": 1}");
}

TEST_F(TestObExprAIPrompt, json_array_argument_is_serialized)
{
  expect_render("{\"template\": \"v={0}\", \"args\": [[1, 2, 3]]}", "v=[1,2,3]");
}

TEST_F(TestObExprAIPrompt, json_null_argument_becomes_null_text)
{
  expect_render("{\"template\": \"v={0}\", \"args\": [null]}", "v=null");
}

TEST_F(TestObExprAIPrompt, nested_prompt_argument_is_serialized)
{
  // ai_prompt('Q: {0}', ai_prompt('{0}?', 'why')) is a legal nested call: the
  // inner prompt object arrives as a json object argument.
  ObString result;
  ASSERT_EQ(OB_SUCCESS,
            render("{\"template\": \"Q: {0}\", \"args\": [{\"template\": \"{0}?\", \"args\": [\"why\"]}]}",
                   result));
  const std::string actual = to_std_string(result);
  EXPECT_EQ(0, actual.rfind("Q: {", 0)) << actual;
  EXPECT_NE(std::string::npos, actual.find("\"template\"")) << actual;
  EXPECT_NE(std::string::npos, actual.find("\"args\"")) << actual;
  EXPECT_NE(std::string::npos, actual.find("why")) << actual;
}

TEST_F(TestObExprAIPrompt, unicode_and_escape_sequences_are_preserved)
{
  // The template holds multi byte characters, a real tab and a real backslash;
  // the argument holds multi byte characters too. Nothing is escaped, mangled
  // or truncated on the way.
  expect_render("{\"template\": \"\xe4\xb8\xad\xe6\x96\x87{0}\xe7\xbb\x93\xe6\x9d\x9f\\t\\\\\","
                " \"args\": [\"\xe5\x80\xbc\"]}",
                "\xe4\xb8\xad\xe6\x96\x87\xe5\x80\xbc\xe7\xbb\x93\xe6\x9d\x9f\t\\");
}

TEST_F(TestObExprAIPrompt, template_without_placeholder_is_returned_as_is)
{
  expect_render("{\"template\": \"just a prompt\", \"args\": []}", "just a prompt");
}

TEST_F(TestObExprAIPrompt, empty_template_is_rejected)
{
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT, render("{\"template\": \"\", \"args\": []}", result));
}

TEST_F(TestObExprAIPrompt, missing_template_is_rejected)
{
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT, render("{\"args\": []}", result));
}

TEST_F(TestObExprAIPrompt, missing_args_is_rejected)
{
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT, render("{\"template\": \"{0}\"}", result));
}

TEST_F(TestObExprAIPrompt, null_prompt_object_is_rejected)
{
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator_, NULL, result));
}

TEST_F(TestObExprAIPrompt, valid_prompt_object)
{
  ObJsonObject *prompt = NULL;
  ASSERT_EQ(OB_SUCCESS, build_prompt("{\"template\": \"{0}\", \"args\": [\"a\"]}", prompt));
  EXPECT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));

  // A nested prompt is a json object argument, it must stay valid.
  ASSERT_EQ(OB_SUCCESS,
            build_prompt("{\"template\": \"{0}\", \"args\": [{\"template\": \"{1}\", \"args\": [\"b\"]}]}",
                         prompt));
  EXPECT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));

  // A json array argument is serialized before substitution, so it is valid too.
  ASSERT_EQ(OB_SUCCESS, build_prompt("{\"template\": \"{0}\", \"args\": [[1, 2]]}", prompt));
  EXPECT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));
}

TEST_F(TestObExprAIPrompt, invalid_prompt_object)
{
  ObJsonObject *prompt = NULL;

  EXPECT_FALSE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(NULL));

  // template is not a string
  ASSERT_EQ(OB_SUCCESS, build_prompt("{\"template\": 1, \"args\": []}", prompt));
  EXPECT_FALSE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));

  // args is not an array
  ASSERT_EQ(OB_SUCCESS, build_prompt("{\"template\": \"{0}\", \"args\": \"a\"}", prompt));
  EXPECT_FALSE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));

  // a third member makes the object ambiguous
  ASSERT_EQ(OB_SUCCESS,
            build_prompt("{\"template\": \"{0}\", \"args\": [\"a\"], \"extra\": 1}", prompt));
  EXPECT_FALSE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));
}

TEST_F(TestObExprAIPrompt, construct_prompt_object)
{
  ObJsonObject *prompt = NULL;
  ObJsonArray *args = NULL;
  ObString template_str = ObString::make_string("{0} / {1}");
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator_, args));
  ObJsonString *first = NULL;
  ObJsonString *second = NULL;
  ObString first_str = ObString::make_string("a");
  ObString second_str = ObString::make_string("b");
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator_, first_str, first));
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator_, second_str, second));
  ASSERT_EQ(OB_SUCCESS, args->append(first));
  ASSERT_EQ(OB_SUCCESS, args->append(second));

  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncPromptObjectUtils::construct_prompt_object(allocator_, template_str, args, prompt));
  ASSERT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt));

  ObString result;
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator_, prompt, result));
  EXPECT_EQ(std::string("a / b"), to_std_string(result));
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
