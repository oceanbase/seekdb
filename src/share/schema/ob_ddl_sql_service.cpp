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
#include "share/schema/catalog_operation_recorder.h"
#include "share/ob_server_struct.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "common/mysqlclient/ob_mysql_result.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{
int ObDDLSqlService::check_extension_member_drop(
    ObISQLClient &sql_client, uint64_t database_id,
    ObSchemaType object_class, uint64_t object_id)
{
  int ret = OB_SUCCESS;
  auto *transaction = dynamic_cast<ObMySQLTransaction *>(&sql_client);
  ObSqlString sql;
  ObISQLClient::ReadResult result;
  common::sqlclient::ObMySQLResult *rows = nullptr;
  int64_t extension_id = 0;
  if (database_id == 0 || database_id == OB_INVALID_ID ||
      object_id == 0 || object_id == OB_INVALID_ID || static_cast<int>(object_class) <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr == transaction || !transaction->is_started()) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(sql.assign_fmt(
      "SELECT extension_id FROM oceanbase.%s WHERE tenant_id=1 AND database_id=%lu "
      "AND object_class=%u AND object_id=%lu FOR UPDATE",
      OB_ALL_EXTENSION_MEMBER_TNAME, database_id, static_cast<unsigned>(object_class), object_id))) {
  } else if (OB_FAIL(sql_client.read(result, sql.ptr()))) {
    // Missing membership table or failed reads must not mean 'no dependency'.
    LOG_WARN("failed to read extension membership before schema deletion", KR(ret), K(database_id), K(object_id));
  } else if (nullptr == (rows = result.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ITER_END == (ret = rows->next())) {
    ret = OB_SUCCESS;
  } else if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rows->get_int(static_cast<int64_t>(0), extension_id))) {
  } else if (extension_id <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid persisted extension member owner", KR(ret), K(extension_id), K(object_id));
  } else {
    ret = OB_OP_NOT_ALLOW;
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "dropping an object owned by an extension independently");
    LOG_WARN("schema object belongs to an extension", KR(ret), K(extension_id), K(database_id), K(object_class), K(object_id));
  }
  const int close_ret = result.close();
  if (OB_SUCC(ret) && OB_SUCCESS != close_ret) ret = close_ret;
  return ret;
}

// A valid SQL execution context records the corresponding __all_ddl_operation row.
int ObDDLSqlService::log_operation(
  ObSchemaOperation &schema_operation,
  common::ObISQLClient &sql_client,
  common::ObSqlString *public_sql_string /*= NULL*/)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp_sql_string;
  ObSqlString catalog_table;
  auto *recorder = dynamic_cast<ICatalogOperationRecorder *>(&sql_client);
  ObSqlString *sql_string = (NULL != public_sql_string ? public_sql_string : &tmp_sql_string);
  ObDMLSqlSplicer ddl_operation_dml;
  int64_t affected_rows = 0;
  if (recorder != nullptr && OB_FAIL(recorder->check_schema_operation())) {
  } else if (recorder != nullptr && schema_operation.schema_version_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(sql_string)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(sql_string->reuse())) {
  } else if (OB_FAIL(recorder == nullptr
      ? log_operation_dml(schema_operation, ddl_operation_dml)
      : gen_ddl_operation_dml(schema_operation, ddl_operation_dml))) {
  } else if (OB_FAIL(catalog_table.assign_fmt("%s.%s", OB_SYS_DATABASE_NAME, OB_ALL_DDL_OPERATION_TNAME))) {
  } else if (OB_FAIL(ddl_operation_dml.splice_insert_sql(catalog_table.ptr(), *sql_string))) {
  } else if (OB_FAIL(sql_client.write(sql_string->ptr(), affected_rows))) {
  } else if (affected_rows != 1) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (recorder != nullptr) {
    const int record_ret = recorder->finish_schema_operation(schema_operation.schema_version_, ret);
    if (ret == OB_SUCCESS) ret = record_ret;
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
  } else if (OB_ISNULL(tsi_oper)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    tsi_oper->last_operation_schema_version_ = schema_operation.schema_version_;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(gen_ddl_operation_dml(schema_operation, ddl_operation_dml))) {
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
  } else if (OB_FAIL(ddl_operation_dml.add_column("schema_version", schema_operation.schema_version_))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("user_id",
          fill_schema_id(schema_operation.user_id_)))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("database_id",
          fill_schema_id(schema_operation.database_id_)))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("database_name",
          ObHexEscapeSqlStr(schema_operation.database_name_?:"")))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("column_id",
          fill_schema_id(schema_operation.column_id_)))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("table_id",
          fill_schema_id(schema_operation.table_id_)))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("table_name",
          ObHexEscapeSqlStr(schema_operation.table_name_?:"")))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("operation_type", schema_operation.op_type_))) {
  } else if (OB_FAIL(ddl_operation_dml.add_column("ddl_stmt_str",
          ObHexEscapeSqlStr(schema_operation.ddl_stmt_str_?:"")))) {
  } else if (OB_FAIL(ddl_operation_dml.add_gmt_modified())) {
  } else if (OB_FAIL(ddl_operation_dml.finish_row())) {
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
  } else {
    ddl_schema_op.schema_version_ = new_schema_version;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_INVALID_VERSION == ddl_schema_op.schema_version_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(log_operation(ddl_schema_op, sql_client))) {
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
