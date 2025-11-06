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

#include "sql/resolver/ddl/ob_lock_tenant_stmt.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

ObLockTenantStmt::ObLockTenantStmt(common::ObIAllocator *name_pool)
  : ObDDLStmt(name_pool, stmt::T_LOCK_TENANT),
    lock_tenant_arg_()
{
}

ObLockTenantStmt::ObLockTenantStmt()
  : ObDDLStmt(stmt::T_LOCK_TENANT),
    lock_tenant_arg_()
{
}

ObLockTenantStmt::~ObLockTenantStmt()
{
}

void ObLockTenantStmt::print(FILE *fp, int32_t level, int32_t index)
{
  UNUSED(index);
  UNUSED(fp);
  UNUSED(level);
}


void ObLockTenantStmt::set_tenant_name(const ObString &tenant_name)
{
  lock_tenant_arg_.tenant_name_ = tenant_name;
}

void ObLockTenantStmt::set_locked(const bool is_locked)
{
  lock_tenant_arg_.is_locked_ = is_locked;
}

} /* sql */
} /* oceanbase */
