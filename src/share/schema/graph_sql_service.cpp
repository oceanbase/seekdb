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
#include "share/schema/graph_sql_service.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "share/schema/ob_column_schema.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/inner_table/ob_inner_table_schema.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{
namespace
{
int fill_graph_columns(const GraphSchema &schema, ObIAllocator &allocator,
                       ObDMLSqlSplicer &sql)
{
  int ret = OB_SUCCESS;
  const int64_t size = schema.get_serialize_size();
  char *buffer = static_cast<char *>(allocator.alloc(size));
  int64_t pos = 0;
  if (buffer == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(schema.serialize(buffer, size, pos))) {
  } else if (OB_FAIL(sql.add_pk_column("graph_id", schema.get_graph_id()))) {
  } else if (OB_FAIL(sql.add_column("database_id", schema.get_database_id()))) {
  } else if (OB_FAIL(sql.add_column("define_user_id", schema.get_define_user_id()))) {
  } else if (OB_FAIL(sql.add_column("name", ObHexEscapeSqlStr(schema.get_name())))) {
  } else if (OB_FAIL(sql.add_column("definition", ObHexEscapeSqlStr(ObString(pos, buffer))))) {
  }
  return ret;
}

int execute_graph_write(ObISQLClient &client, const ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  int64_t affected = 0;
  if (OB_FAIL(client.write(sql.ptr(), affected))) {
  } else if (affected != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected graph catalog write count", K(ret), K(affected));
  }
  return ret;
}
} // namespace

int GraphSqlService::load_database_graphs(uint64_t database_id, ObISQLClient &client,
                                          ObIArray<GraphSchema> &graphs)
{
  int ret = OB_SUCCESS;
  graphs.reset();
  ObSqlString sql;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    sqlclient::ObMySQLResult *result = nullptr;
    if (OB_FAIL(sql.append_fmt("SELECT definition FROM %s WHERE database_id = %lu",
                               OB_ALL_PROPERTY_GRAPH_TNAME, database_id))) {
    } else if (OB_FAIL(client.read(res, sql.ptr()))) {
    } else if ((result = res.get_result()) == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret) && OB_SUCC(ret = result->next())) {
        ObString definition;
        GraphSchema graph;
        int64_t pos = 0;
        if (OB_FAIL(result->get_varchar("definition", definition))) {
        } else if (OB_FAIL(graph.deserialize(definition.ptr(), definition.length(), pos))) {
        } else if (pos != definition.length() || graph.get_database_id() != database_id) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(graphs.push_back(graph))) {
        }
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
  }
  return ret;
}

int GraphSqlService::check_relation_name(ObISQLClient &client, const ObTableSchema &table)
{
  int ret = OB_SUCCESS;
  ObSEArray<GraphSchema, 4> graphs;
  if (!table.is_user_table() && !table.is_user_view()) {
  } else if (OB_FAIL(load_database_graphs(table.get_database_id(), client, graphs))) {
  } else {
    ObSchemaNameComparator comparator(table.get_name_case_mode(), table.get_database_id());
    for (int64_t i = 0; OB_SUCC(ret) && i < graphs.count(); ++i) {
      if (comparator.compare(graphs.at(i).get_name(), table.get_table_name_str()) == 0) {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST, table.get_table_name_str().length(), table.get_table_name_str().ptr());
      }
    }
  }
  return ret;
}

int GraphSqlService::check_table_ddl(ObISQLClient &client, const ObTableSchema &original,
                                    const ObTableSchema *replacement, uint64_t deleted_column,
                                    const ObColumnSchemaV2 *changed_column)
{
  int ret = OB_SUCCESS;
  ObSEArray<GraphSchema, 4> graphs;
  if (!original.is_user_table()) {
    // System catalog and auxiliary-index updates have no graph mappings.
  } else if (OB_FAIL(load_database_graphs(original.get_database_id(), client, graphs))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < graphs.count(); ++i) {
      const GraphSchema &graph = graphs.at(i);
      if (graph.references_table(original.get_table_id())) {
        bool breaks_mapping = replacement == nullptr;
        if (replacement != nullptr) {
          breaks_mapping = original.get_table_id() != replacement->get_table_id()
              || original.get_database_id() != replacement->get_database_id()
              || original.get_table_name_str() != replacement->get_table_name_str()
              || replacement->is_in_recyclebin()
              || graph.references_column(original.get_table_id(), deleted_column);
          for (int64_t j = 0; !breaks_mapping && j < graph.get_elements().count(); ++j) {
            const GraphElement &element = graph.get_elements().at(j);
            if (element.table_id_ == original.get_table_id()) {
              breaks_mapping = replacement->get_rowkey_info().get_size() != element.key_count_;
              for (int64_t k = 0; OB_SUCC(ret) && !breaks_mapping && k < element.key_count_; ++k) {
                uint64_t key = OB_INVALID_ID;
                if (OB_FAIL(replacement->get_rowkey_info().get_column_id(k, key))) {
                } else { breaks_mapping = key != element.key_columns_[k]; }
              }
            }
          }
          for (ObTableSchema::const_column_iterator column = original.column_begin();
               !breaks_mapping && column != original.column_end(); ++column) {
            if (*column != nullptr && graph.references_column(original.get_table_id(), (*column)->get_column_id())) {
              const ObColumnSchemaV2 *next = changed_column != nullptr
                  && changed_column->get_column_id() == (*column)->get_column_id()
                  ? changed_column : replacement->get_column_schema((*column)->get_column_id());
              breaks_mapping = next == nullptr || next->get_column_name_str() != (*column)->get_column_name_str()
                  || next->get_meta_type() != (*column)->get_meta_type()
                  || next->get_accuracy() != (*column)->get_accuracy()
                  || next->is_nullable() != (*column)->is_nullable()
                  || next->get_rowkey_position() != (*column)->get_rowkey_position()
                  || next->is_generated_column() != (*column)->is_generated_column();
              if (!breaks_mapping) {
                const ObIArray<ObString> &old_values = (*column)->get_extended_type_info();
                const ObIArray<ObString> &new_values = next->get_extended_type_info();
                breaks_mapping = old_values.count() != new_values.count();
                for (int64_t k = 0; !breaks_mapping && k < old_values.count(); ++k) {
                  breaks_mapping = old_values.at(k) != new_values.at(k);
                }
              }
            }
          }
        }
        if (OB_SUCC(ret) && breaks_mapping) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "DDL that changes a property graph's mapped table, column or primary key; drop the graph first");
        }
      }
    }
  }
  return ret;
}

int GraphSqlService::create_graph(const GraphSchema &schema, const ObString &ddl,
                                 ObISQLClient &client)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
  ObDMLSqlSplicer columns;
  ObSqlString sql;
  if (!schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(fill_graph_columns(schema, allocator, columns))) {
  } else if (OB_FAIL(columns.splice_insert_sql(OB_ALL_PROPERTY_GRAPH_TNAME, sql))) {
  } else if (OB_FAIL(execute_graph_write(client, sql))) {
  } else if (OB_FAIL(write_history(schema, schema.get_schema_version(), false, client))) {
  } else if (OB_FAIL(log_graph_operation(schema, schema.get_schema_version(), false, ddl, client))) {
  }
  return ret;
}

int GraphSqlService::drop_graph(const GraphSchema &schema, int64_t version,
                               const ObString &ddl, ObISQLClient &client)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer columns;
  ObSqlString sql;
  if (!schema.is_valid() || version <= schema.get_schema_version()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(columns.add_pk_column("graph_id", schema.get_graph_id()))) {
  } else if (OB_FAIL(columns.splice_delete_sql(OB_ALL_PROPERTY_GRAPH_TNAME, sql))) {
  } else if (OB_FAIL(execute_graph_write(client, sql))) {
  } else if (OB_FAIL(write_history(schema, version, true, client))) {
  } else if (OB_FAIL(log_graph_operation(schema, version, true, ddl, client))) {
  }
  return ret;
}

int GraphSqlService::write_history(const GraphSchema &schema, int64_t version,
                                  bool deleted, ObISQLClient &client)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
  ObDMLSqlSplicer columns;
  ObSqlString sql;
  if (OB_FAIL(fill_graph_columns(schema, allocator, columns))) {
  } else if (OB_FAIL(columns.add_pk_column("schema_version", version))) {
  } else if (OB_FAIL(columns.add_column("is_deleted", deleted ? 1 : 0))) {
  } else if (OB_FAIL(columns.splice_insert_sql(OB_ALL_PROPERTY_GRAPH_HISTORY_TNAME, sql))) {
  } else if (OB_FAIL(execute_graph_write(client, sql))) {
  }
  return ret;
}

int GraphSqlService::log_graph_operation(const GraphSchema &schema, int64_t version,
                                        bool deleted, const ObString &ddl, ObISQLClient &client)
{
  ObSchemaOperation operation;
  operation.graph_id_ = schema.get_graph_id();
  operation.graph_name_ = schema.get_name();
  operation.database_id_ = schema.get_database_id();
  operation.schema_version_ = version;
  operation.op_type_ = deleted ? OB_DDL_DROP_PROPERTY_GRAPH : OB_DDL_CREATE_PROPERTY_GRAPH;
  operation.ddl_stmt_str_ = ddl;
  return log_operation(operation, client);
}
} // namespace schema
} // namespace share
} // namespace oceanbase
