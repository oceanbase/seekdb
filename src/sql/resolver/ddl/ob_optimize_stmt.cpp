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

#include "sql/resolver/ddl/ob_optimize_stmt.h"

namespace oceanbase
{
using namespace common;
using obrpc::ObTableItem;
namespace sql
{
ObOptimizeTableStmt::ObOptimizeTableStmt(ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_OPTIMIZE_TABLE), optimize_table_arg_()
{
}

ObOptimizeTableStmt::ObOptimizeTableStmt()
    : ObDDLStmt(stmt::T_OPTIMIZE_TABLE), optimize_table_arg_()
{
}

int ObOptimizeTableStmt::add_table_item(const ObTableItem &table_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(optimize_table_arg_.tables_.push_back(table_item))) {
    LOG_WARN("failed to add table item", K(ret), K(table_item));
  }
  return ret;
}

ObOptimizeTenantStmt::ObOptimizeTenantStmt(ObIAllocator *name_pool)
  : ObDDLStmt(name_pool, stmt::T_OPTIMIZE_TENANT), optimize_tenant_arg_()
{
}

ObOptimizeTenantStmt::ObOptimizeTenantStmt()
  : ObDDLStmt(stmt::T_OPTIMIZE_TENANT), optimize_tenant_arg_()
{
}

void ObOptimizeTenantStmt::set_tenant_name(const ObString &tenant_name)
{
  optimize_tenant_arg_.tenant_name_ = tenant_name;
}

ObOptimizeAllStmt::ObOptimizeAllStmt(ObIAllocator *name_pool)
  : ObDDLStmt(name_pool, stmt::T_OPTIMIZE_ALL), optimize_all_arg_()
{
}

ObOptimizeAllStmt::ObOptimizeAllStmt()
  : ObDDLStmt(stmt::T_OPTIMIZE_ALL), optimize_all_arg_()
{
}

}  // end namespace sql
}  // end namespace oceanbase
