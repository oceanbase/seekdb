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

#ifndef UNITTEST_SQL_SQL_TEST_PATHS_H_
#define UNITTEST_SQL_SQL_TEST_PATHS_H_

#include <cstdlib>
#include <string>

inline std::string sql_test_data_path(const char *relative_path)
{
  const char *test_srcdir = std::getenv("TEST_SRCDIR");
  const char *test_workspace = std::getenv("TEST_WORKSPACE");
  return nullptr != test_srcdir && nullptr != test_workspace
      ? std::string(test_srcdir) + "/" + test_workspace + "/unittest/sql/" + relative_path
      : std::string("unittest/sql/") + relative_path;
}

inline std::string sql_test_tmp_path(const char *file_name)
{
  const char *test_tmpdir = std::getenv("TEST_TMPDIR");
  return nullptr != test_tmpdir
      ? std::string(test_tmpdir) + "/" + file_name
      : std::string(file_name);
}

#endif // UNITTEST_SQL_SQL_TEST_PATHS_H_
