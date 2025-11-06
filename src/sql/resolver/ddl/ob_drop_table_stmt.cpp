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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/ddl/ob_drop_table_stmt.h"

namespace oceanbase
{
using namespace common;
using obrpc::ObTableItem;
namespace sql
{
ObDropTableStmt::ObDropTableStmt(ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_DROP_TABLE),
    drop_table_arg_(),
    is_view_stmt_(false)
{
}

ObDropTableStmt::ObDropTableStmt()
  : ObDDLStmt(stmt::T_DROP_TABLE),
    drop_table_arg_(),
    is_view_stmt_(false)
{
}

ObDropTableStmt::~ObDropTableStmt()
{
}

int ObDropTableStmt::add_table_item(const ObTableItem &table_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(drop_table_arg_.tables_.push_back(table_item))) {
    LOG_WARN("failed to add table item!", K(table_item), K(ret));
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
