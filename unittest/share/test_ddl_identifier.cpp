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
#include <utility>
#include "share/ob_ddl_identifier.h"

namespace oceanbase
{
namespace share
{
namespace
{

class StringBuffer
{
public:
  int append(const char *str, const int64_t length)
  {
    value_.append(str, static_cast<size_t>(length));
    return common::OB_SUCCESS;
  }
  std::string value_;
};

TEST(DDLIdentifier, quotes_names_without_changing_other_bytes)
{
  const std::pair<std::string, std::string> cases[] = {
      {"column", "`column`"},
      {"", "``"},
      {"`", "````"},
      {"a`b", "`a``b`"},
      {"`a`", "```a```"},
      {"a``b", "`a````b`"},
      {"select", "`select`"},
      {"a\\b", "`a\\b`"},
      {"a\"b'c", "`a\"b'c`"},
      {"列`名", "`列``名`"},
      {std::string("a\0`b", 4), std::string("`a\0``b`", 7)},
  };
  for (const auto &item : cases) {
    StringBuffer sql;
    ASSERT_EQ(common::OB_SUCCESS,
              append_ddl_identifier(sql, item.first.data(), item.first.size()));
    EXPECT_EQ(item.second, sql.value_);
  }
}

TEST(DDLIdentifier, quotes_shadow_prefix_and_name_together)
{
  StringBuffer sql;
  ASSERT_EQ(common::OB_SUCCESS, append_ddl_identifier(sql, "a`b", 3, true));
  EXPECT_EQ("`__SHADOW_a``b`", sql.value_);
}

TEST(DDLIdentifier, appends_to_existing_sql)
{
  StringBuffer sql;
  sql.value_ = "cast(";
  ASSERT_EQ(common::OB_SUCCESS, append_ddl_identifier(sql, "e`n", 3));
  ASSERT_EQ(common::OB_SUCCESS, sql.append(" as unsigned) AS ", 17));
  ASSERT_EQ(common::OB_SUCCESS, append_ddl_identifier(sql, "e`n", 3));
  EXPECT_EQ("cast(`e``n` as unsigned) AS `e``n`", sql.value_);
}

TEST(DDLIdentifier, validates_input_before_appending)
{
  StringBuffer sql;
  sql.value_ = "unchanged";
  EXPECT_EQ(common::OB_INVALID_ARGUMENT, append_ddl_identifier(sql, nullptr, 1));
  EXPECT_EQ(common::OB_INVALID_ARGUMENT, append_ddl_identifier(sql, "x", -1));
  EXPECT_EQ("unchanged", sql.value_);
  sql.value_.clear();
  EXPECT_EQ(common::OB_SUCCESS, append_ddl_identifier(sql, nullptr, 0));
  EXPECT_EQ("``", sql.value_);
}

class FailingBuffer
{
public:
  explicit FailingBuffer(const int fail_at) : fail_at_(fail_at) {}
  int append(const char *, const int64_t)
  {
    ++calls_;
    return calls_ == fail_at_ ? common::OB_ALLOCATE_MEMORY_FAILED : common::OB_SUCCESS;
  }
  int fail_at_;
  int calls_ = 0;
};

TEST(DDLIdentifier, propagates_each_append_failure_without_continuing)
{
  // Opening quote, first segment, duplicated backtick, tail, closing quote.
  for (int fail_at = 1; fail_at <= 5; ++fail_at) {
    FailingBuffer sql(fail_at);
    EXPECT_EQ(common::OB_ALLOCATE_MEMORY_FAILED, append_ddl_identifier(sql, "a`b", 3));
    EXPECT_EQ(fail_at, sql.calls_);
  }
}

} // namespace
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
