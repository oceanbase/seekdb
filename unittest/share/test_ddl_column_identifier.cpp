#include <gtest/gtest.h>
#include <string>
#include "share/ob_ddl_common.h"
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

TEST(DDLColumnIdentifier, quotes_original_alias_shadow_and_cast)
{
  const char *names[] = {"plain", "a`b", "`edge`", "a``b", "列`名", "a'b\\c"};
  const char *escaped[] = {"plain", "a``b", "``edge``", "a````b", "列``名", "a'b\\c"};
  for (int n = 0; n < 6; ++n) {
    for (int flags = 0; flags < 32; ++flags) {
      const bool origin = flags & 1;
      const bool alias = flags & 2;
      const bool shadow = flags & 4;
      const bool cast = flags & 8;
      const bool comma = flags & 16;
      ObColumnNameInfo info(ObString::make_string(names[n]), shadow, cast);
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, sql));
      std::string expected = comma ? "prefix, " : "prefix";
      if (origin) {
        if (cast) { expected += "cast("; }
        expected += std::string("`") + escaped[n] + "`";
        if (cast) { expected += " as unsigned)"; }
      }
      if (origin && alias) { expected += " AS "; }
      if (alias) {
        expected += std::string(shadow ? "`__SHADOW_" : "`") + escaped[n] + "`";
      }
      EXPECT_EQ(expected, std::string(sql.ptr(), sql.length())) << "name=" << names[n] << " flags=" << flags;
    }
  }
}

TEST(DDLColumnIdentifier, respects_string_length)
{
  const char storage[] = {'a', '`', 'b', 'x'};
  ObString name(3, storage);
  ObColumnNameInfo info(name, false);
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, true, true, false, sql));
  EXPECT_EQ("`a``b` AS `a``b`", std::string(sql.ptr(), sql.length()));
}
