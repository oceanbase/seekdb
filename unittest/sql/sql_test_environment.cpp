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

#include "sql/ob_sql_init.h"

namespace oceanbase
{
namespace sql
{
namespace
{

class SqlTestEnvironment final : public ::testing::Environment
{
public:
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, init_sql_factories());
  }
};

::testing::Environment *const SQL_TEST_ENVIRONMENT =
    ::testing::AddGlobalTestEnvironment(new SqlTestEnvironment());

} // namespace
} // namespace sql
} // namespace oceanbase
