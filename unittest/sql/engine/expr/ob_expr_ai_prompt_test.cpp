/**
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
#include <string>
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "common/json_type/ob_json_common.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

/**
 * ai_prompt 的单元测试：
 * 1. JSON 参数（对象/数组）被允许，并且保留原始结构；
 * 2. 模板中重复出现的占位符都会被替换；
 * 3. 参数本身是嵌套 JSON 时，替换使用的是完整的 JSON 文本；
 * 4. 模板中的占位符下标越界时报错。
 */
class ObExprAIPromptTest : public ::testing::Test
{
public:
  ObExprAIPromptTest() = default;
  virtual ~ObExprAIPromptTest() = default;

  // 构造一个 prompt 对象：template 为模板，args 为参数数组
  static int build_prompt(ObIAllocator &allocator,
                          const char *template_str,
                          ObJsonArray *args_array,
                          ObJsonObject *&prompt_object)
  {
    int ret = OB_SUCCESS;
    ObString tmpl(template_str);
    if (OB_FAIL(ObAIFuncPromptObjectUtils::construct_prompt_object(
                    allocator, tmpl, args_array, prompt_object))) {
    }
    return ret;
  }

  // 追加一个字符串参数
  static int append_str_arg(ObIAllocator &allocator, ObJsonArray *args_array, const char *value)
  {
    int ret = OB_SUCCESS;
    ObString str(value);
    ObJsonString *json_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, str, json_str))) {
    } else if (OB_FAIL(args_array->append(json_str))) {
    }
    return ret;
  }

  static std::string to_std_str(const ObString &str)
  {
    return std::string(str.ptr(), str.length());
  }
};

// 参数类型：字符串/对象/数组合法，其它类型非法
TEST_F(ObExprAIPromptTest, test_prompt_arg_type_is_valid)
{
  ASSERT_TRUE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_STRING));
  ASSERT_TRUE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_OBJECT));
  ASSERT_TRUE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_ARRAY));
  ASSERT_FALSE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_INT));
  ASSERT_FALSE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_DOUBLE));
  ASSERT_FALSE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_BOOL));
  ASSERT_FALSE(ObAIFuncJsonUtils::ob_is_valid_prompt_arg_type(ObJsonNodeType::J_NULL));
}

// 整个参数数组的合法性：只要有一个参数类型不合法，整个数组就不合法
TEST_F(ObExprAIPromptTest, test_prompt_args_array_is_valid)
{
  ObArenaAllocator allocator;
  ObJsonArray *args_array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));
  ASSERT_NE(nullptr, args_array);
  ASSERT_TRUE(ObAIFuncJsonUtils::ob_is_json_array_valid_prompt_args(args_array));

  ASSERT_EQ(OB_SUCCESS, append_str_arg(allocator, args_array, "hello"));
  ASSERT_TRUE(ObAIFuncJsonUtils::ob_is_json_array_valid_prompt_args(args_array));

  // 追加一个 JSON 对象参数，原始结构被保留
  ObString json_obj_str("{\"k\":2}");
  ObJsonObject *json_obj = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object_form_str(allocator, json_obj_str, json_obj));
  ASSERT_NE(nullptr, json_obj);
  ASSERT_EQ(OB_SUCCESS, args_array->append(static_cast<ObJsonNode *>(json_obj)));
  ASSERT_TRUE(ObAIFuncJsonUtils::ob_is_json_array_valid_prompt_args(args_array));
  ASSERT_EQ(2, args_array->element_count());

  // 追加一个非法类型（数字），整个参数数组不再合法
  ObJsonInt *json_int = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 1, json_int));
  ASSERT_EQ(OB_SUCCESS, args_array->append(static_cast<ObJsonNode *>(json_int)));
  ASSERT_FALSE(ObAIFuncJsonUtils::ob_is_json_array_valid_prompt_args(args_array));
}

// JSON 参数保留原始结构，不做字符串化
TEST_F(ObExprAIPromptTest, test_prompt_json_arg_keeps_structure)
{
  ObArenaAllocator allocator;
  ObJsonArray *args_array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));

  // json_array(1, true)
  ObJsonInt *json_int = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 1, json_int));
  ASSERT_EQ(OB_SUCCESS, args_array->append(static_cast<ObJsonNode *>(json_int)));
  ObJsonBoolean *json_bool = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_boolean(allocator, true, json_bool));
  ASSERT_EQ(OB_SUCCESS, args_array->append(static_cast<ObJsonNode *>(json_bool)));
  // json_object('k', 2)
  ObJsonObject *json_obj = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, json_obj));
  ObJsonString *key = nullptr;
  ObString key_str("k");
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, key_str, key));
  ObJsonInt *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 2, value));
  ASSERT_EQ(OB_SUCCESS, json_obj->add(key, value));
  ASSERT_EQ(OB_SUCCESS, args_array->append(static_cast<ObJsonNode *>(json_obj)));

  ObJsonObject *prompt_object = nullptr;
  ASSERT_EQ(OB_SUCCESS, build_prompt(allocator, "{0}/{0}/{1}", args_array, prompt_object));
  ASSERT_NE(nullptr, prompt_object);
  ASSERT_TRUE(ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_object));

  // prompt 对象由 template 与 args 两部分组成
  ObJsonString *template_node = static_cast<ObJsonString *>(
      prompt_object->get_value(ObAIFuncPromptObjectUtils::prompt_template_key));
  ASSERT_NE(nullptr, template_node);
  ASSERT_EQ("{0}/{0}/{1}", to_std_str(template_node->get_str()));

  ObJsonArray *args_node = static_cast<ObJsonArray *>(
      prompt_object->get_value(ObAIFuncPromptObjectUtils::prompt_args_key));
  ASSERT_NE(nullptr, args_node);
  ASSERT_EQ(3, args_node->element_count());
  // 参数保留原始的 JSON 类型
  ASSERT_EQ(ObJsonNodeType::J_INT, args_node->get_value(0)->json_type());
  ASSERT_EQ(1, static_cast<ObJsonInt *>(args_node->get_value(0))->value());
  ASSERT_EQ(ObJsonNodeType::J_BOOL, args_node->get_value(1)->json_type());
  ASSERT_EQ(ObJsonNodeType::J_OBJECT, args_node->get_value(2)->json_type());
  ObJsonObject *k_object = static_cast<ObJsonObject *>(args_node->get_value(2));
  ASSERT_NE(nullptr, k_object->get_value("k"));
  ASSERT_EQ(2, static_cast<ObJsonInt *>(k_object->get_value("k"))->value());
}

// 重复出现的占位符都会被替换
TEST_F(ObExprAIPromptTest, test_prompt_repeated_placeholder)
{
  ObArenaAllocator allocator;
  ObJsonArray *args_array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));
  ASSERT_EQ(OB_SUCCESS, append_str_arg(allocator, args_array, "first"));
  ASSERT_EQ(OB_SUCCESS, append_str_arg(allocator, args_array, "second"));

  ObJsonObject *prompt_object = nullptr;
  ASSERT_EQ(OB_SUCCESS, build_prompt(allocator, "{0}/{0}/{1}", args_array, prompt_object));

  ObString replaced;
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                allocator, prompt_object, replaced));
  ASSERT_EQ("first/first/second", to_std_str(replaced));
}

// 参数是嵌套 JSON 时，替换使用完整的 JSON 文本
TEST_F(ObExprAIPromptTest, test_prompt_nested_json_arg)
{
  ObArenaAllocator allocator;
  ObJsonArray *args_array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));

  // {"outer": {"inner": [1, 2]}}
  ObJsonObject *inner = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, inner));
  ObJsonArray *inner_array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, inner_array));
  ObJsonInt *num1 = nullptr;
  ObJsonInt *num2 = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 1, num1));
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 2, num2));
  ASSERT_EQ(OB_SUCCESS, inner_array->append(static_cast<ObJsonNode *>(num1)));
  ASSERT_EQ(OB_SUCCESS, inner_array->append(static_cast<ObJsonNode *>(num2)));
  ObString inner_key_str("inner");
  ObJsonString *inner_key = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, inner_key_str, inner_key));
  ASSERT_EQ(OB_SUCCESS, inner->add(inner_key, inner_array));

  ObJsonObject *outer = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, outer));
  ObString outer_key_str("outer");
  ObJsonString *outer_key = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, outer_key_str, outer_key));
  ASSERT_EQ(OB_SUCCESS, outer->add(outer_key, inner));
  ASSERT_EQ(OB_SUCCESS, args_array->append(static_cast<ObJsonNode *>(outer)));

  ObJsonObject *prompt_object = nullptr;
  ASSERT_EQ(OB_SUCCESS, build_prompt(allocator, "prefix {0} suffix", args_array, prompt_object));

  ObString replaced;
  ASSERT_EQ(OB_SUCCESS,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                allocator, prompt_object, replaced));
  // 嵌套 JSON 参数以其 JSON 文本参与替换
  ASSERT_EQ("prefix {\"outer\": {\"inner\": [1, 2]}} suffix", to_std_str(replaced));
}

// 占位符下标越界时报错
TEST_F(ObExprAIPromptTest, test_prompt_invalid_placeholder_index)
{
  ObArenaAllocator allocator;
  ObJsonArray *args_array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, args_array));
  ASSERT_EQ(OB_SUCCESS, append_str_arg(allocator, args_array, "only-one"));

  ObJsonObject *prompt_object = nullptr;
  ASSERT_EQ(OB_SUCCESS, build_prompt(allocator, "{0} {1}", args_array, prompt_object));

  ObString replaced;
  ASSERT_EQ(OB_INVALID_ARGUMENT,
            ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(
                allocator, prompt_object, replaced));
}

} // namespace sql
} // namespace oceanbase
