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
#include "ob_ddl_sql_service.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{
// A valid SQL execution context records the corresponding __all_ddl_operation row.
int ObDDLSqlService::log_operation(
  ObSchemaOperation &schema_operation,
  common::ObISQLClient &sql_client,
  common::ObSqlString *public_sql_string /*= NULL*/)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp_sql_string;
  ObSqlString *sql_string = (NULL != public_sql_string ? public_sql_string : &tmp_sql_string);
  ObDMLSqlSplicer ddl_operation_dml;
  int64_t affected_rows = 0;
  if (OB_ISNULL(sql_string)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(sql_string));
  } else if (FALSE_IT(sql_string->reuse())) {
  } else if (OB_FAIL(log_operation_dml(schema_operation, ddl_operation_dml))) {
    LOG_WARN("failed to get log_operation_dml", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.splice_insert_sql(OB_ALL_DDL_OPERATION_TNAME, *sql_string))) {
    LOG_WARN("failed to splice insert sql", KR(ret));
  } else if (OB_FAIL(sql_client.write(sql_string->ptr(), affected_rows))) {
    LOG_WARN("failed to write sql", KR(ret), K(*sql_string));
  } else if (affected_rows != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("affected_rows expect to 1, ", KR(ret), K(affected_rows));
  }
  return ret;
}

int ObDDLSqlService::log_operation_dml(
    const ObSchemaOperation &schema_operation,
    share::ObDMLSqlSplicer &ddl_operation_dml)
{
  int ret = OB_SUCCESS;
  auto *tsi_oper = GET_TSI(TSILastOper);
  if (OB_UNLIKELY(!schema_operation.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema_operation is invalid", K(schema_operation), K(ret));
  } else if (OB_ISNULL(tsi_oper)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Failed to get TSILatOper", KR(ret), K(schema_operation)); 
  } else {
    tsi_oper->last_operation_schema_version_ = schema_operation.schema_version_;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(gen_ddl_operation_dml(schema_operation, ddl_operation_dml))) {
    LOG_WARN("failed to gen ddl operation dml", KR(ret), K(schema_operation));
  }
  return ret;
}

int ObDDLSqlService::gen_ddl_operation_dml(
    const ObSchemaOperation &schema_operation,
    share::ObDMLSqlSplicer &ddl_operation_dml)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!schema_operation.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema_operation is invalid", K(schema_operation), K(ret));
  } else if (OB_FAIL(ddl_operation_dml.add_column("schema_version", schema_operation.schema_version_))) {
    LOG_WARN("failed to add column schema_version", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("user_id",
          fill_schema_id(schema_operation.user_id_)))) {
    LOG_WARN("failed to add column user_id", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("database_id",
          fill_schema_id(schema_operation.database_id_)))) {
    LOG_WARN("failed to add column database_id", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("database_name",
          ObHexEscapeSqlStr(schema_operation.database_name_?:"")))) {
    LOG_WARN("failed to add column database_name", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("column_id",
          fill_schema_id(schema_operation.column_id_)))) {
    LOG_WARN("failed to add column column_id", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("table_id",
          fill_schema_id(schema_operation.table_id_)))) {
    LOG_WARN("failed to add column table_id", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("table_name",
          ObHexEscapeSqlStr(schema_operation.table_name_?:"")))) {
    LOG_WARN("failed to add column table_name", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_column("operation_type", schema_operation.op_type_))) {
    LOG_WARN("failed to add column operation_type", KR(ret), K(schema_operation));
#ifdef __ANDROID__
  } else if (OB_FAIL(ddl_operation_dml.add_column("ddl_stmt_str",
          ObHexEscapeSqlStr(ObString::make_empty_string())))) {
    // Avoid large DDL text in __all_ddl_operation on embedded Android (LOB path).
#else
  } else if (OB_FAIL(ddl_operation_dml.add_column("ddl_stmt_str",
          ObHexEscapeSqlStr(schema_operation.ddl_stmt_str_?:"")))) {
#endif
    LOG_WARN("failed to add column ddl_stmt_str", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.add_gmt_modified())) {
    LOG_WARN("failed to add column gmt_modified", KR(ret), K(schema_operation));
  } else if (OB_FAIL(ddl_operation_dml.finish_row())) {
    LOG_WARN("failed to finish ddl_operation_dml", KR(ret));
  }
  return ret;
}

int ObDDLSqlService::log_nop_operation(const ObSchemaOperation &schema_operation,
                                       const int64_t new_schema_version,
                                       const common::ObString &ddl_sql_str,
                                       common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObSchemaOperation ddl_schema_op;
  ddl_schema_op = schema_operation;
  ddl_schema_op.ddl_stmt_str_ = ddl_sql_str;

  if (OB_INVALID_VERSION == new_schema_version) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get schema version", K(ret), K(new_schema_version), K(ddl_schema_op));
  } else {
    ddl_schema_op.schema_version_ = new_schema_version;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_INVALID_VERSION == ddl_schema_op.schema_version_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema version", K(ret), K(ddl_schema_op));
  } else if (OB_FAIL(log_operation(ddl_schema_op, sql_client))) {
    LOG_WARN("failed to log ddl operator", K(ret), K(ddl_schema_op));
  }
  return ret;
}

uint64_t ObDDLSqlService::fill_schema_id(const uint64_t schema_id)
{
  return schema_id;
}
} //end of schema
} //end of share
} //end of oceanbase
