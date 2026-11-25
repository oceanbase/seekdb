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
#include "sql/resolver/dml/graph_show_resolver.h"
#include "sql/resolver/ob_schema_checker.h"
#include "share/schema/graph_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_column_schema.h"
namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
namespace
{
int graph_quote_name(ObSqlString &sql, const ObString &name)
{
  int ret = sql.append("`");
  for (int64_t i = 0; OB_SUCC(ret) && i < name.length(); ++i) {
    if (name.ptr()[i] == '`') { ret = sql.append("``"); }
    else { ret = sql.append(name.ptr() + i, 1); }
  }
  if (OB_SUCC(ret)) { ret = sql.append("`"); }
  return ret;
}

int graph_print_columns(ObSqlString &sql, const ObTableSchema &table,
                         const uint64_t *ids, int64_t count)
{
  int ret = sql.append("(");
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const ObColumnSchemaV2 *column = table.get_column_schema(ids[i]);
    if (column == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else if (i != 0 && OB_FAIL(sql.append(", "))) {
    } else { ret = graph_quote_name(sql, column->get_column_name_str()); }
  }
  if (OB_SUCC(ret)) { ret = sql.append(")"); }
  return ret;
}

int graph_print_definition(const GraphSchema &graph, ObSchemaGetterGuard &guard, ObSqlString &sql)
{
  int ret = sql.append("CREATE PROPERTY GRAPH ");
  if (OB_SUCC(ret)) { ret = graph_quote_name(sql, graph.get_name()); }
  for (int pass = 0; OB_SUCC(ret) && pass < 2; ++pass) {
    bool first = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < graph.get_elements().count(); ++i) {
      const GraphElement &element = graph.get_elements().at(i);
      if (element.is_vertex() != (pass == 0)) { continue; }
      const ObTableSchema *table = nullptr;
      if (OB_FAIL(guard.get_table_schema(element.table_id_, table))) {
      } else if (table == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(sql.append(first ? (pass == 0 ? "\n  VERTEX TABLES (\n    " : "\n  EDGE TABLES (\n    ") : ",\n    "))) {
      } else if (OB_FAIL(graph_quote_name(sql, table->get_table_name_str()))) {
      } else if (OB_FAIL(sql.append(" KEY "))) {
      } else if (OB_FAIL(graph_print_columns(sql, *table, element.key_columns_, element.key_count_))) {
      }
      first = false;
      for (int direction = 0; OB_SUCC(ret) && !element.is_vertex() && direction < 2; ++direction) {
        const GraphElement *vertex = graph.get_element(direction == 0 ? element.source_id_ : element.destination_id_);
        const ObTableSchema *endpoint = nullptr;
        if (vertex == nullptr) { ret = OB_ERR_UNEXPECTED; }
        else if (OB_FAIL(guard.get_table_schema(vertex->table_id_, endpoint))) {
        } else if (endpoint == nullptr) { ret = OB_ERR_UNEXPECTED; }
        else if (OB_FAIL(sql.append(direction == 0 ? " SOURCE KEY " : " DESTINATION KEY "))) {
        } else if (OB_FAIL(graph_print_columns(sql, *table,
                     direction == 0 ? element.source_columns_ : element.destination_columns_,
                     direction == 0 ? element.source_key_count_ : element.destination_key_count_))) {
        } else if (OB_FAIL(sql.append(" REFERENCES "))) {
        } else if (OB_FAIL(graph_quote_name(sql, endpoint->get_table_name_str()))) {
        } else if (OB_FAIL(sql.append(" "))) {
        } else if (OB_FAIL(graph_print_columns(sql, *endpoint, vertex->key_columns_, vertex->key_count_))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(sql.append(" LABEL "))) {
      } else if (OB_FAIL(graph_quote_name(sql, element.label_))) {
      } else if (OB_FAIL(sql.append(" PROPERTIES ("))) {
      } else {
        bool first_property = true;
        for (int64_t j = 0; OB_SUCC(ret) && j < graph.get_properties().count(); ++j) {
          const GraphProperty &property = graph.get_properties().at(j);
          if (property.element_id_ == element.id_) {
            if (!first_property && OB_FAIL(sql.append(", "))) {
            } else { ret = graph_quote_name(sql, property.name_); }
            first_property = false;
          }
        }
        if (OB_SUCC(ret)) { ret = sql.append(")"); }
      }
    }
    if (OB_SUCC(ret) && !first) { ret = sql.append("\n  )"); }
  }
  return ret;
}
}

int GraphShowResolver::resolve(const ParseNode &node)
{
  int ret = OB_SUCCESS;
  const GraphSchema *graph = nullptr;
  ObString name;
  ObString database_name;
  uint64_t database_id = OB_INVALID_ID;
  ObSqlString definition;
  ObSessionPrivInfo privileges;
  if (node.num_child_ != 1 || node.children_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
  else if (OB_FAIL(resolve_table_relation_node(node.children_[0], name, database_name))) {
  } else if (OB_FAIL(session_info_->get_session_priv_info(privileges))) {
  } else if (OB_FAIL(schema_checker_->check_db_access(privileges, session_info_->get_enable_role_array(), database_name))) {
  } else if (OB_FAIL(schema_checker_->get_database_id(database_name, database_id))) {
  } else if (OB_FAIL(schema_checker_->get_schema_guard()->get_graph_schema(database_id, name, graph))) {
  } else if (graph == nullptr) {
    ret = OB_TABLE_NOT_EXIST;
    ObCStringHelper helper;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(name));
  } else if (OB_FAIL(graph_print_definition(*graph, *schema_checker_->get_schema_guard(), definition))) {
  } else {
    ParseNode *select = new_node(allocator_, T_SELECT, PARSE_SELECT_MAX_IDX);
    ParseNode *projects = new_node(allocator_, T_PROJECT_LIST, 2);
    if (select == nullptr || projects == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      select->value_ = 0;
      select->children_[PARSE_SELECT_SELECT] = projects;
      for (int i = 0; OB_SUCC(ret) && i < 2; ++i) {
        ParseNode *value = new_node(allocator_, T_VARCHAR, 0);
        ParseNode *alias = new_node(allocator_, T_ALIAS, 2);
        ParseNode *label = new_node(allocator_, T_IDENT, 0);
        ParseNode *project = new_node(allocator_, T_PROJECT_STRING, 1);
        ObString copied;
        if (value == nullptr || alias == nullptr || label == nullptr || project == nullptr) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(ob_write_string(*allocator_, i == 0 ? graph->get_name() : definition.string(), copied))) {
        } else {
          const ObString heading = ObString::make_string(i == 0 ? "Graph" : "Create Property Graph");
          value->str_value_ = copied.ptr(); value->str_len_ = copied.length();
          label->str_value_ = heading.ptr(); label->str_len_ = heading.length();
          alias->children_[0] = value; alias->children_[1] = label;
          alias->str_value_ = heading.ptr(); alias->str_len_ = heading.length();
          project->children_[0] = alias;
          projects->children_[i] = project;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObSelectResolver::resolve(*select))) {
      } else {
        // The result contains catalog text and its database access check runs
        // during resolution. Recheck access on every SHOW, including after REVOKE.
        get_stmt()->get_query_ctx()->get_query_hint_for_update().get_global_hint()
            .merge_plan_cache_hint(OB_USE_PLAN_CACHE_NONE);
        ObSchemaObjVersion dependency(graph->get_graph_id(), graph->get_schema_version(), DEPENDENCY_PROPERTY_GRAPH);
        dependency.is_db_explicit_ = true;
        ret = get_stmt()->add_global_dependency_table(dependency);
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
