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
#include "sql/resolver/ddl/graph_ddl_resolver.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
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
ObString graph_identifier(const ParseNode &node)
{
  return ObString(static_cast<int32_t>(node.str_len_), node.str_value_);
}
}

int GraphDDLResolver::resolve_base_table(const ParseNode *node, uint64_t database_id,
                                        const ObTableSchema *&table)
{
  int ret = OB_SUCCESS;
  ObString table_name;
  ObString db_name;
  uint64_t db_id = OB_INVALID_ID;
  table = nullptr;
  if (OB_FAIL(resolve_table_relation_node(node, table_name, db_name))) {
  } else if (OB_FAIL(schema_checker_->get_database_id(db_name, db_id))) {
  } else if (db_id != database_id) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "cross-database property graph mappings");
  } else if (OB_FAIL(schema_checker_->get_schema_guard()->get_table_schema(db_id, table_name, false, table))) {
  } else if (table == nullptr) {
    ret = OB_TABLE_NOT_EXIST;
    ObCStringHelper helper;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(db_name), helper.convert(table_name));
  } else if (!table->is_user_table() || table->is_ctas_tmp_table() || table->is_in_recyclebin()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "property graph mappings of non-persistent user tables");
  }
  return ret;
}

int GraphDDLResolver::resolve_keys(const ParseNode &node, const ObTableSchema &table,
                                  uint64_t *columns, int64_t &count)
{
  int ret = OB_SUCCESS;
  count = node.num_child_;
  if (count < 1 || count > 2) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "property graph keys with other than one or two BIGINT columns");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const ObColumnSchemaV2 *column = table.get_column_schema(graph_identifier(*node.children_[i]));
    if (column == nullptr) {
      ret = OB_ERR_BAD_FIELD_ERROR;
      const ObString name = graph_identifier(*node.children_[i]);
      LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, name.length(), name.ptr(),
                     static_cast<int>(sizeof("property graph key") - 1), "property graph key");
    } else {
      columns[i] = column->get_column_id();
    }
  }
  return ret;
}

int GraphDDLResolver::resolve_elements(const ParseNode &node, GraphDDLStmt &stmt,
    ObIArray<GraphElement> &elements, ObIArray<GraphProperty> &properties,
    ObIArray<const ObTableSchema *> &tables)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < node.num_child_; ++i) {
    const ParseNode &mapping = *node.children_[i];
    const bool vertex = mapping.type_ == T_GRAPH_VERTEX;
    const ObTableSchema *table = nullptr;
    GraphElement element;
    element.id_ = elements.count() + 1;
    if (mapping.num_child_ != (vertex ? 4 : 10)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(resolve_base_table(mapping.children_[0], stmt.graph_.get_database_id(), table))) {
    } else if (OB_FAIL(resolve_keys(*mapping.children_[1], *table, element.key_columns_, element.key_count_))) {
    } else {
      element.table_id_ = table->get_table_id();
      element.label_ = graph_identifier(*mapping.children_[vertex ? 2 : 8]);
      for (int direction = 0; OB_SUCC(ret) && !vertex && direction < 2; ++direction) {
        const int offset = direction == 0 ? 2 : 5;
        const ObTableSchema *endpoint = nullptr;
        uint64_t refs[2] = {OB_INVALID_ID, OB_INVALID_ID};
        int64_t refs_count = 0;
        int64_t &key_count = direction == 0 ? element.source_key_count_ : element.destination_key_count_;
        uint64_t *keys = direction == 0 ? element.source_columns_ : element.destination_columns_;
        uint64_t &endpoint_id = direction == 0 ? element.source_id_ : element.destination_id_;
        if (OB_FAIL(resolve_base_table(mapping.children_[offset + 1], stmt.graph_.get_database_id(), endpoint))) {
        } else if (OB_FAIL(resolve_keys(*mapping.children_[offset], *table, keys, key_count))) {
        } else if (OB_FAIL(resolve_keys(*mapping.children_[offset + 2], *endpoint, refs, refs_count))) {
        } else {
          for (int64_t k = 0; k < elements.count(); ++k) {
            const GraphElement &candidate = elements.at(k);
            if (candidate.is_vertex() && candidate.table_id_ == endpoint->get_table_id()) {
              endpoint_id = candidate.id_;
              if (refs_count != candidate.key_count_ || key_count != refs_count
                  || refs[0] != candidate.key_columns_[0]
                  || (refs_count == 2 && refs[1] != candidate.key_columns_[1])) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph endpoint references that do not match the complete vertex key in order");
              }
            }
          }
          if (OB_SUCC(ret) && endpoint_id == OB_INVALID_ID) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph endpoints referencing tables outside VERTEX TABLES");
          }
        }
      }
      const ParseNode &props = *mapping.children_[vertex ? 3 : 9];
      for (int64_t k = 0; OB_SUCC(ret) && k < props.num_child_; ++k) {
        GraphProperty property;
        property.element_id_ = element.id_;
        property.name_ = graph_identifier(*props.children_[k]);
        const ObColumnSchemaV2 *column = table->get_column_schema(property.name_);
        if (column == nullptr) {
          ret = OB_ERR_BAD_FIELD_ERROR;
          LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, property.name_.length(), property.name_.ptr(),
                         static_cast<int>(sizeof("property graph properties") - 1), "property graph properties");
        } else {
          property.column_id_ = column->get_column_id();
          ret = properties.push_back(property);
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(elements.push_back(element))) {
      } else if (OB_FAIL(tables.push_back(table))) {
      }
    }
  }
  return ret;
}

int GraphDDLResolver::resolve(const ParseNode &node)
{
  int ret = OB_SUCCESS;
  GraphDDLStmt *stmt = nullptr;
  ObString name;
  ObString db_name;
  uint64_t db_id = OB_INVALID_ID;
  const bool create = node.type_ == T_CREATE_PROPERTY_GRAPH;
  if (session_info_ == nullptr || allocator_ == nullptr || schema_checker_ == nullptr
      || node.children_ == nullptr || node.num_child_ != (create ? 3 : 2)) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((stmt = create_stmt<GraphDDLStmt>()) == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(resolve_table_relation_node(node.children_[0], name, db_name))) {
  } else if (OB_FAIL(schema_checker_->get_database_id(db_name, db_id))) {
  } else if (db_id != session_info_->get_database_id()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "property graphs outside the current database");
  } else if (OB_FAIL(stmt->graph_.set_name(name))) {
  } else if (OB_FAIL(deep_copy_str(db_name, stmt->database_name_))) {
  } else {
    stmt_ = stmt;
    stmt->set_stmt_type(create ? stmt::T_CREATE_PROPERTY_GRAPH : stmt::T_DROP_PROPERTY_GRAPH);
    stmt->graph_.set_database_id(db_id);
    stmt->graph_.set_owner_id(session_info_->get_priv_user_id());
    if (!create) {
      stmt->if_exists_ = node.children_[1] != nullptr;
    } else {
      ObSEArray<GraphElement, 8> elements;
      ObSEArray<GraphProperty, 16> properties;
      ObSEArray<const ObTableSchema *, 8> tables;
      if (node.children_[1] == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolve_elements(*node.children_[1], *stmt, elements, properties, tables))) {
      } else if (node.children_[2] != nullptr
                 && OB_FAIL(resolve_elements(*node.children_[2], *stmt, elements, properties, tables))) {
      } else if (OB_FAIL(stmt->graph_.set_elements(elements))) {
      } else if (OB_FAIL(stmt->graph_.set_properties(properties))) {
      } else if (!stmt->graph_.is_valid_definition()) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "duplicate graph labels, properties or base table mappings");
      } else if (OB_FAIL(stmt->graph_.validate_tables(tables))) {
        if (ret == OB_INVALID_ARGUMENT || ret == OB_NOT_SUPPORTED) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph keys other than complete signed BIGINT primary keys, or incompatible property types");
        }
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
