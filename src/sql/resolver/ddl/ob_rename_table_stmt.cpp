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

#include "sql/resolver/ddl/ob_rename_table_stmt.h"
#include "storage/tablelock/ob_lock_executor.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{

using namespace share::schema;
using namespace common;
using namespace transaction::tablelock;


namespace sql
{

ObRenameTableStmt::ObRenameTableStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_RENAME_TABLE)
{
}

ObRenameTableStmt::ObRenameTableStmt()
    : ObDDLStmt(stmt::T_RENAME_TABLE)
{
}

ObRenameTableStmt::~ObRenameTableStmt()
{
}

int ObRenameTableStmt::add_rename_table_item(const obrpc::ObRenameTableItem &rename_table_item){
  int ret = OB_SUCCESS;
  if (OB_FAIL(rename_table_arg_.rename_table_items_.push_back(rename_table_item))) {
    SQL_RESV_LOG(WARN, "failed to add rename table item to rename table arg!", K(ret));
  }
  return ret;
}


int ObRenameTableStmt::set_lock_priority(sql::ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = session->get_effective_tenant_id();
  const int64_t min_cluster_version = GET_MIN_CLUSTER_VERSION();
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id));
  if (!tenant_config.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "tenant config invalid, can not do rename", K(ret), K(tenant_id));
  } else if (tenant_config->enable_lock_priority) {
    if (!ObLockExecutor::proxy_is_support(session)) {
      ret = OB_NOT_SUPPORTED;
      SQL_RESV_LOG(WARN, "is in proxy_mode and not support rename", K(ret), KPC(session));
    } else {
      rename_table_arg_.lock_priority_ = ObTableLockPriority::HIGH1;
    }
  }
  return ret;
}

} //namespace sql
}

