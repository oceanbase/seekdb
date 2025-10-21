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

#include "sql/resolver/ddl/ob_truncate_table_stmt.h"

namespace oceanbase
{

using namespace share::schema;

namespace sql
{

ObTruncateTableStmt::ObTruncateTableStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_TRUNCATE_TABLE)
{
}

ObTruncateTableStmt::ObTruncateTableStmt()
    : ObDDLStmt(stmt::T_TRUNCATE_TABLE)
{
}

ObTruncateTableStmt::~ObTruncateTableStmt()
{
}

void ObTruncateTableStmt::set_database_name(const common::ObString &database_name)
{
  truncate_table_arg_.database_name_ = database_name;
}

void ObTruncateTableStmt::set_table_name(const common::ObString &table_name)
{
  truncate_table_arg_.table_name_ = table_name;
}

} //namespace sql
} //namespace oceanbase

