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
    const ObSchemaOperationType op,
    ObISQLClient &sql_client,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, op, sql_client, ddl_stmt_str))) {
    LOG_WARN("replace_tenant failed", K(tenant_schema), K(op), K(ret));
  }
  return ret;
}

int ObTenantSqlService::replace_tenant(
    const ObTenantSchema &tenant_schema,
    const ObSchemaOperationType op,
    common::ObISQLClient &sql_client,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    ObCStringHelper helper;
    LOG_WARN("tenant_schema is invalid", "tenant_schema",
        helper.convert(tenant_schema), K(ret));
  } else if (OB_DDL_ADD_TENANT != op
             && OB_DDL_ADD_TENANT_START != op
             && OB_DDL_ADD_TENANT_END != op
             && OB_DDL_ALTER_TENANT != op
             && OB_DDL_DEL_TENANT_START != op
             && OB_DDL_DROP_TENANT_TO_RECYCLEBIN != op
             && OB_DDL_RENAME_TENANT != op
             && OB_DDL_FLASHBACK_TENANT != op) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid replace tenant op", K(op), K(ret));
  } else {
    // log ddl_operation
    if (OB_SUCC(ret)) {
      ObSchemaOperation tenant_op;
      tenant_op.tenant_id_ = tenant_schema.get_tenant_id();
      tenant_op.op_type_ = op;
      tenant_op.schema_version_ = tenant_schema.get_schema_version();
      tenant_op.ddl_stmt_str_ = ddl_stmt_str ? *ddl_stmt_str : ObString();
      int64_t sql_tenant_id = OB_SYS_TENANT_ID;
      if (OB_FAIL(log_operation(tenant_op, sql_client, sql_tenant_id))) {
        LOG_WARN("log add tenant ddl operation failed", K(ret));
      }
    }
  }
  return ret;
}

} //end of schema
} //end of share
} //end of oceanbase
