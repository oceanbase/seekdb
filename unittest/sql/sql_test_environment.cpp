/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
