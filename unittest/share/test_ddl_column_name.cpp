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
#include "lib/string/ob_sql_string.h"
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
TEST(TestDDLColumnName, escapes_names_in_all_output_forms)
{
  struct NameCase {
    const char *input;
    int32_t length;
    const char *escaped;
  };
  const NameCase cases[] = {
    {"ordinary", 8, "ordinary"},
    {"a`b", 3, "a``b"},
    {"``", 2, "````"},
    {"`edge`", 6, "``edge``"},
    {"select", 6, "select"},
    // Only the length-delimited prefix belongs to the identifier.
    {"a`b_ignored", 3, "a``b"},
  };
  for (const auto &test : cases) {
    const common::ObString name(test.length, test.input);
    for (int mask = 0; mask < 32; ++mask) {
      SCOPED_TRACE(::testing::Message() << test.input << ": " << mask);
      const bool origin = (mask & 1) != 0;
      const bool alias = (mask & 2) != 0;
      const bool comma = (mask & 4) != 0;
      const bool shadow = (mask & 8) != 0;
      const bool cast = (mask & 16) != 0;
      const ObColumnNameInfo info(name, shadow, cast);
      common::ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, sql));
      std::string expected = comma ? ", " : "";
      if (origin) {
        if (cast) { expected += "cast("; }
        expected += std::string("`") + test.escaped + "`";
        if (cast) { expected += " as unsigned)"; }
      }
      if (origin && alias) { expected += " AS "; }
      if (alias) {
        expected += shadow ? "`__SHADOW_" : "`";
        expected += std::string(test.escaped) + "`";
      }
      EXPECT_EQ(expected, sql.empty() ? std::string() : std::string(sql.ptr(), sql.length()));
    }
  }
}
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
