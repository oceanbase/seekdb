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
#include <string>
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"
#include "lib/allocator/ob_allocator.h"

using namespace oceanbase::common;

namespace {
int render(ObIAllocator &allocator, const std::string &pattern,
           const std::string &argument, ObString &result)
{
  ObJsonString text(pattern.data(), pattern.size());
  ObJsonString value(argument.data(), argument.size());
  ObJsonArray args(&allocator);
  ObJsonObject prompt(&allocator);
  int ret = args.array_append(&value);
  if (OB_SUCCESS == ret) { ret = prompt.add(ObAIFuncPromptObjectUtils::prompt_template_key, &text); }
  if (OB_SUCCESS == ret) { ret = prompt.add(ObAIFuncPromptObjectUtils::prompt_args_key, &args); }
  if (OB_SUCCESS == ret) { ret = ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(allocator, &prompt, result); }
  return ret;
}

TEST(AIPromptRendering, repeated_large_argument_survives_temporary_buffers)
{
  ObArenaAllocator allocator;
  ObString result;
  const std::string argument(8192, 'x');
  ASSERT_EQ(OB_SUCCESS, render(allocator, "{0}/{0}/{0}", argument, result));
  const std::string expected = argument + "/" + argument + "/" + argument;
  EXPECT_EQ(expected, std::string(result.ptr(), result.length()));
}

TEST(AIPromptRendering, missing_and_overflowing_indices_are_rejected)
{
  ObArenaAllocator allocator;
  ObString result;
  EXPECT_EQ(OB_INVALID_ARGUMENT, render(allocator, "{1}", "value", result));
  EXPECT_EQ(OB_INVALID_ARGUMENT, render(allocator, "{92233720368547758080}", "value", result));
}

TEST(AIPromptRendering, literal_braces_and_empty_arguments_are_preserved)
{
  ObArenaAllocator allocator;
  ObString result;
  ASSERT_EQ(OB_SUCCESS, render(allocator, "prefix {} {x} {0} tail", "", result));
  EXPECT_EQ("prefix {} {x}  tail", std::string(result.ptr(), result.length()));
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
