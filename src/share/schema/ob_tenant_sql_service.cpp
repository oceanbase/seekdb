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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_tenant_sql_service.h"
#include "sql/ob_sql_utils.h"
#include "rootserver/ob_rs_job_table_operator.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

int ObTenantSqlService::insert_tenant(
    const ObTenantSchema &tenant_schema,
    ObISQLClient &sql_client,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, sql_client, ddl_stmt_str))) {
  }
  return ret;
}

int ObTenantSqlService::replace_tenant(
    const ObTenantSchema &tenant_schema,
    common::ObISQLClient &sql_client,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  UNUSED(sql_client);
  UNUSED(ddl_stmt_str);
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    ObCStringHelper helper;
    LOG_WARN("tenant_schema is invalid", "tenant_schema",
        helper.convert(tenant_schema), K(ret));
  }
  // OB_DDL_ADD_TENANT op-logging removed: single-tenant, op never consumed.
  return ret;
}

} //end of schema
} //end of share
} //end of oceanbase
