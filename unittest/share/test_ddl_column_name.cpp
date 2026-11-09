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
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLColumnName, quote_origins_and_aliases)
{
  const char *names[] = {"plain", "select", "a`b", "`edge`", "a``b", ""};
  const char *quoted[] = {"`plain`", "`select`", "`a``b`", "```edge```", "`a````b`", "``"};
  const char *shadow[] = {"`__SHADOW_plain`", "`__SHADOW_select`", "`__SHADOW_a``b`",
                        "`__SHADOW_``edge```", "`__SHADOW_a````b`", "`__SHADOW_`"};
  for (int64_t i = 0; i < 6; ++i) {
    for (int flags = 0; flags < 32; ++flags) {
      const bool origin = flags & 1;
      const bool alias = flags & 2;
      const bool comma = flags & 4;
      const bool is_shadow = flags & 8;
      const bool cast = flags & 16;
      SCOPED_TRACE(::testing::Message() << names[i] << " flags=" << flags);
      ObSqlString actual;
      ObSqlString expected;
      ASSERT_EQ(OB_SUCCESS, actual.append("prefix"));
      ASSERT_EQ(OB_SUCCESS, expected.append("prefix"));
      if (comma) {
        ASSERT_EQ(OB_SUCCESS, expected.append(", "));
      }
      if (origin) {
        if (cast) {
          ASSERT_EQ(OB_SUCCESS, expected.append("cast("));
        }
        ASSERT_EQ(OB_SUCCESS, expected.append(quoted[i]));
        if (cast) {
          ASSERT_EQ(OB_SUCCESS, expected.append(" as unsigned)"));
        }
      }
      if (origin && alias) {
        ASSERT_EQ(OB_SUCCESS, expected.append(" AS "));
      }
      if (alias) {
        ASSERT_EQ(OB_SUCCESS, expected.append(is_shadow ? shadow[i] : quoted[i]));
      }
      ObColumnNameInfo info(ObString::make_string(names[i]), is_shadow, cast);
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, actual));
      EXPECT_STREQ(expected.ptr(), actual.ptr());
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
