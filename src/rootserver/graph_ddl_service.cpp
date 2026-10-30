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

#define USING_LOG_PREFIX RS
#include "rootserver/graph_ddl_service.h"
#include "rootserver/ob_ddl_service.h"
#include "share/schema/graph_sql_service.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace rootserver
{
namespace
{
int finish_graph_ddl(ObDDLSQLTransaction &transaction, ObDDLService &service, int ret)
{
  if (transaction.is_started()) {
    const int end_ret = transaction.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end_ret; }
  }
  if (OB_SUCC(ret)) { ret = service.publish_schema(); }
  return ret;
}
} // namespace

int GraphDDLService::create_graph(const GraphSchema &definition, const ObString &ddl)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  GraphSchema graph;
  const GraphSchema *existing_graph = nullptr;
  const ObTableSchema *existing_table = nullptr;
  const ObDatabaseSchema *database = nullptr;
  ObSEArray<const ObTableSchema *, 8> tables;
  ObMultiVersionSchemaService &multi_version_schema_service = service_.get_schema_service();
  ObSchemaService *schema_service = multi_version_schema_service.get_schema_service();
  ObDDLSQLTransaction transaction(&multi_version_schema_service);
  int64_t refreshed_version = OB_INVALID_VERSION;
  int64_t version = OB_INVALID_VERSION;
  uint64_t id = OB_INVALID_ID;
  if (!definition.is_valid_definition()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (schema_service == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(service_.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
  } else if (OB_FAIL(guard.get_schema_version(refreshed_version))) {
  } else if (OB_FAIL(transaction.start(&service_.get_sql_proxy(), refreshed_version))) {
  } else if (OB_FAIL(guard.get_database_schema(definition.get_database_id(), database))) {
  } else if (database == nullptr || database->is_in_recyclebin()) {
    ret = OB_ERR_BAD_DATABASE;
  } else if (OB_FAIL(guard.get_graph_schema(definition.get_database_id(), definition.get_name(), existing_graph))) {
  } else if (OB_FAIL(guard.get_table_schema(definition.get_database_id(), definition.get_name(), false, existing_table))) {
  } else if (existing_graph != nullptr || existing_table != nullptr) {
    ret = OB_ERR_TABLE_EXIST;
    LOG_USER_ERROR(OB_ERR_TABLE_EXIST, definition.get_name().length(), definition.get_name().ptr());
  } else if (OB_FAIL(graph.assign(definition))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < graph.get_elements().count(); ++i) {
      const ObTableSchema *table = nullptr;
      if (OB_FAIL(guard.get_table_schema(graph.get_elements().at(i).table_id_, table))) {
      } else if (table == nullptr) {
        ObSqlString missing_table;
        if (OB_FAIL(missing_table.append_fmt("table_id=%lu", graph.get_elements().at(i).table_id_))) {
        } else {
          ret = OB_TABLE_NOT_EXIST;
          LOG_USER_ERROR(OB_TABLE_NOT_EXIST, database->get_database_name(), missing_table.ptr());
        }
      } else if (OB_FAIL(tables.push_back(table))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(graph.validate_tables(tables))) {
    // Share the global relation ID sequence, while retaining a distinct Schema
    // type and catalog. A dropped graph ID is never reused by a new definition.
    } else if (OB_FAIL(schema_service->fetch_new_table_id(id))) {
    } else if (OB_FAIL(multi_version_schema_service.gen_new_schema_version(version))) {
    } else {
      graph.set_graph_id(id);
      graph.set_schema_version(version);
      ret = schema_service->get_graph_sql_service().create_graph(graph, ddl, transaction);
    }
  }
  return finish_graph_ddl(transaction, service_, ret);
}

int GraphDDLService::drop_graph(uint64_t database_id, const ObString &name, bool if_exists,
                               const ObString &ddl)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  const GraphSchema *graph = nullptr;
  ObMultiVersionSchemaService &multi_version_schema_service = service_.get_schema_service();
  ObSchemaService *schema_service = multi_version_schema_service.get_schema_service();
  ObDDLSQLTransaction transaction(&multi_version_schema_service);
  int64_t refreshed_version = OB_INVALID_VERSION;
  int64_t version = OB_INVALID_VERSION;
  if (database_id == OB_INVALID_ID || name.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (schema_service == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(service_.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
  } else if (OB_FAIL(guard.get_schema_version(refreshed_version))) {
  } else if (OB_FAIL(transaction.start(&service_.get_sql_proxy(), refreshed_version))) {
  } else if (OB_FAIL(guard.get_graph_schema(database_id, name, graph))) {
  } else if (graph == nullptr) {
    ret = if_exists ? OB_SUCCESS : OB_TABLE_NOT_EXIST;
    // There is no DDL operation to commit for IF EXISTS on an absent graph.
    const int end_ret = transaction.end(false);
    return OB_SUCC(ret) ? end_ret : ret;
  } else if (OB_FAIL(multi_version_schema_service.gen_new_schema_version(version))) {
  } else if (OB_FAIL(schema_service->get_graph_sql_service().drop_graph(*graph, version, ddl, transaction))) {
  }
  return finish_graph_ddl(transaction, service_, ret);
}
} // namespace rootserver
} // namespace oceanbase
