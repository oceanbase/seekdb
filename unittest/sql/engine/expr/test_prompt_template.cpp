// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include <gtest/gtest.h>
#include "sql/engine/expr/ob_expr_ai/prompt_template.h"
#include <string>
#include <vector>
using oceanbase::common::visit_prompt_template;

namespace {
bool render(const std::string &text, const std::vector<std::string> &args, std::string &out)
{
  out.clear();
  return visit_prompt_template(text.data(), text.size(), args.size(),
      [&](const char *literal, int64_t count, int64_t index) {
        if (index >= 0) { out += args[index]; }
        else { out.append(literal, count); }
        return true;
      });
}
}
TEST(PromptTemplate, RepeatedLongArgumentDoesNotExhaustOutput)
{
  std::string out;
  const std::string value(100000, 'x');
  ASSERT_TRUE(render("{0}/{0}/{0}", {value}, out));
  EXPECT_EQ(value + "/" + value + "/" + value, out);
}
TEST(PromptTemplate, UnicodeEscapesAndEmbeddedNulRemainIntact)
{
  std::string out;
  const std::string value("a\0b", 3);
  ASSERT_TRUE(render("你好：{0}\\{1}\n", {value, "世界"}, out));
  EXPECT_EQ(std::string("你好：") + value + "\\世界\n", out);
}
TEST(PromptTemplate, EmptyArgumentsAndUnusedArguments)
{
  std::string out;
  ASSERT_TRUE(render("{0}{0}", {""}, out));
  EXPECT_EQ("", out);
  ASSERT_TRUE(render("literal", {"unused"}, out));
  EXPECT_EQ("literal", out);
}
TEST(PromptTemplate, ReplacementTextIsNotRecursivelySubstituted)
{
  std::string out;
  ASSERT_TRUE(render("{0}-{1}", {"{1}", "value"}, out));
  EXPECT_EQ("{1}-value", out);
}
TEST(PromptTemplate, OutOfRangeAndOverflowAreRejected)
{
  std::string out;
  EXPECT_FALSE(render("{1}", {"only"}, out));
  EXPECT_FALSE(render("{0}", {}, out));
  EXPECT_FALSE(render("{9223372036854775808}", {"only"}, out));
  EXPECT_FALSE(render("{18446744073709551616}", {"only"}, out));
  EXPECT_FALSE(render("{" + std::string(500, '9') + "}", {"only"}, out));
}
TEST(PromptTemplate, LiteralBracesAndLeadingZeros)
{
  std::string out;
  ASSERT_TRUE(render("json {key} {} {x} {0 {00}", {"ok"}, out));
  EXPECT_EQ("json {key} {} {x} {0 ok", out);
}
TEST(PromptTemplate, CallbackFailureStopsTraversal)
{
  int calls = 0;
  EXPECT_FALSE(visit_prompt_template("{0}{0}", 6, 1,
      [&](const char *, int64_t, int64_t) { ++calls; return false; }));
  EXPECT_EQ(1, calls);
}
