/*
 * Copyright (c) 2026 OceanBase.
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
// Standalone regression test:
// c++ -std=c++17 -Wall -Wextra -Werror -Isrc unittest/share/test_ddl_quoted_identifier.cpp -o /tmp/test_ddl_quoted_identifier && /tmp/test_ddl_quoted_identifier
#include "share/ddl/ob_ddl_quoted_identifier.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

int main()
{
  using oceanbase::share::append_ddl_quoted_identifier;
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"column", "`column`"},
      {"select", "`select`"},
      {"a`b", "`a``b`"},
      {"`", "````"},
      {"a``b", "`a````b`"},
      {"", "``"},
      {"列`名", "`列``名`"},
      {std::string("x\0y", 3), std::string("`x\0y`", 5)},
  };
  for (const auto &test : cases) {
    for (const bool shadow : {false, true}) {
      std::string output;
      const auto append = [&](const char *s, size_t len) {
        output.append(s, len);
        return 0;
      };
      const int ret = append_ddl_quoted_identifier(
          test.first.data(), test.first.size(), shadow, append);
      assert(ret == 0);
      std::string expected = test.second;
      if (shadow) {
        expected.insert(1, "__SHADOW_");
      }
      assert(output == expected);
    }
  }

  // An append failure must be propagated without further writes.
  for (int fail_at = 1; fail_at <= 6; ++fail_at) {
    int calls = 0;
    const auto append = [&](const char *, size_t) {
      ++calls;
      return calls == fail_at ? -4013 : 0;
    };
    const int ret = append_ddl_quoted_identifier("a`b", 3, false, append);
    assert(ret == (fail_at <= 5 ? -4013 : 0));
    assert(calls == (fail_at <= 5 ? fail_at : 5));
  }
  return 0;
}
