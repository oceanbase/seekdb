/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "share/ob_ddl_identifier.h"

using oceanbase::common::OB_SUCCESS;
using oceanbase::common::OB_INVALID_ARGUMENT;
using oceanbase::common::OB_ALLOCATE_MEMORY_FAILED;
using oceanbase::share::append_ddl_column_identifier;

namespace
{
class SqlBuffer
{
public:
  int append(const char *text) { return append(text, std::strlen(text)); }
  int append(const char *text, const int64_t length)
  {
    ++calls_;
    if (calls_ == fail_at_) {
      return OB_ALLOCATE_MEMORY_FAILED;
    }
    output_.append(text, static_cast<size_t>(length));
    return OB_SUCCESS;
  }

  std::string output_;
  int calls_ = 0;
  int fail_at_ = -1;
};

void require(const bool condition, const char *message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_identifiers()
{
  const std::vector<std::pair<std::string, std::string>> cases = {
    {"column", "`column`"},
    {"select", "`select`"},
    {"", "``"},
    {"e`n", "`e``n`"},
    {"`edge`", "```edge```"},
    {"``", "``````"},
    {"列`名", "`列``名`"},
    {std::string("a\0`b", 4), std::string("`a\0``b`", 7)},
  };
  for (const auto &entry : cases) {
    SqlBuffer buffer;
    require(OB_SUCCESS == append_ddl_column_identifier(
        buffer, entry.first.data(), entry.first.size()), "identifier append failed");
    require(buffer.output_ == entry.second, "incorrect quoted identifier");
  }
}

void test_shadow_and_cast()
{
  SqlBuffer shadow;
  require(OB_SUCCESS == append_ddl_column_identifier(shadow, "e`n", 3, true), "shadow failed");
  require(shadow.output_ == "`__SHADOW_e``n`", "incorrect shadow identifier");

  SqlBuffer cast;
  require(OB_SUCCESS == cast.append("cast("), "cast prefix failed");
  require(OB_SUCCESS == append_ddl_column_identifier(cast, "e`n", 3), "cast identifier failed");
  require(OB_SUCCESS == cast.append(" as unsigned)"), "cast suffix failed");
  require(cast.output_ == "cast(`e``n` as unsigned)", "incorrect enum/set cast");
}

void test_invalid_input()
{
  SqlBuffer buffer;
  require(OB_INVALID_ARGUMENT == append_ddl_column_identifier(buffer, nullptr, 1), "null name accepted");
  require(OB_INVALID_ARGUMENT == append_ddl_column_identifier(buffer, "x", -1), "negative length accepted");
  require(buffer.calls_ == 0 && buffer.output_.empty(), "invalid input changed output");
  require(OB_SUCCESS == append_ddl_column_identifier(buffer, nullptr, 0), "empty name failed");
}

void test_append_failures()
{
  SqlBuffer success;
  require(OB_SUCCESS == append_ddl_column_identifier(success, "a`b`c", 5), "control failed");
  for (int failure = 1; failure <= success.calls_; ++failure) {
    SqlBuffer buffer;
    buffer.fail_at_ = failure;
    require(OB_ALLOCATE_MEMORY_FAILED == append_ddl_column_identifier(buffer, "a`b`c", 5),
        "append failure was lost");
    require(buffer.calls_ == failure, "continued appending after an error");
  }
}
} // namespace

int main()
{
  try {
    test_identifiers();
    test_shadow_and_cast();
    test_invalid_input();
    test_append_failures();
    std::cout << "DDL identifier regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
