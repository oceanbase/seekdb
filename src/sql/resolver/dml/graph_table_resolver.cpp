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
#include "sql/resolver/dml/ob_dml_resolver.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
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
ObString graph_node_name(const ParseNode &node)
{
  return ObString(static_cast<int32_t>(node.str_len_), node.str_value_);
}

struct GraphBinding
{
  const GraphElement *element_;
  const ObTableSchema *table_;
  ParseNode *variable_;
  TO_STRING_KV(KP_(element), KP_(table), KP_(variable));
};

// Construct the relational syntax tree directly. No SQL string is generated or
// parsed again. The ordinary resolver then owns expressions, types, privileges,
// table versions and the derived relation's output columns.
class GraphRelationBuilder final
{
public:
  GraphRelationBuilder(ObIAllocator &allocator, const ObString &database_name)
    : allocator_(allocator), database_name_(database_name), error_(OB_SUCCESS) {}
  ParseNode *make(ObItemType type, int count)
  {
    ParseNode *node = nullptr;
    if (error_ == OB_SUCCESS) {
      node = new_node(&allocator_, type, count);
      if (node == nullptr) { error_ = OB_ALLOCATE_MEMORY_FAILED; }
      else { node->value_ = 0; }
    }
    return node;
  }
  ParseNode *identifier(const ObString &name)
  {
    ParseNode *node = make(T_IDENT, 0);
    if (node != nullptr) { node->str_value_ = name.ptr(); node->str_len_ = name.length(); }
    return node;
  }
  ParseNode *column(const GraphBinding &binding, uint64_t id)
  {
    const ObColumnSchemaV2 *schema = binding.table_->get_column_schema(id);
    ParseNode *ref = make(T_COLUMN_REF, 3);
    if (schema == nullptr) { error_ = OB_ERR_UNEXPECTED; }
    else if (ref != nullptr) {
      ref->children_[1] = binding.variable_;
      ref->children_[2] = identifier(schema->get_column_name_str());
    }
    return ref;
  }
  ParseNode *binary(ObItemType type, ParseNode *left, ParseNode *right)
  {
    ParseNode *node = make(type, 2);
    if (node != nullptr) { node->children_[0] = left; node->children_[1] = right; }
    return node;
  }
  void append_condition(ParseNode *condition, ParseNode *&where)
  {
    if (condition != nullptr) { where = where == nullptr ? condition : binary(T_OP_AND, where, condition); }
  }
  ParseNode *relation(const GraphBinding &binding)
  {
    ParseNode *name = make(T_RELATION_FACTOR, 2);
    ParseNode *alias = make(T_ALIAS, 5);
    if (name != nullptr && alias != nullptr) {
      // An outer CTE must not shadow the persistent table bound by the graph.
      name->children_[0] = identifier(database_name_);
      name->children_[1] = identifier(binding.table_->get_table_name_str());
      name->str_value_ = binding.table_->get_table_name_str().ptr();
      name->str_len_ = binding.table_->get_table_name_str().length();
      alias->children_[0] = name;
      alias->children_[1] = binding.variable_;
    }
    return alias;
  }
  int error() const { return error_; }
private:
  ObIAllocator &allocator_;
  ObString database_name_;
  int error_;
};

struct GraphExpressionNode
{
  const ParseNode *node_;
  TO_STRING_KV(KP_(node));
};

int check_graph_expression(const ParseNode &root, const GraphSchema &graph,
                           const ObIArray<GraphBinding> &bindings)
{
  int ret = OB_SUCCESS;
  ObSEArray<GraphExpressionNode, 16> pending;
  if (OB_FAIL(pending.push_back(GraphExpressionNode{&root}))) {
  }
  while (OB_SUCC(ret) && !pending.empty()) {
    const ParseNode *node = pending.at(pending.count() - 1).node_;
    pending.pop_back();
    if (node->type_ == T_SELECT || node->type_ == T_GRAPH_TABLE || IS_AGGR_FUN(node->type_)
        || node->type_ == T_WINDOW_FUNCTION) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "subqueries, aggregate or window functions inside GRAPH_TABLE");
    } else if (node->type_ == T_COLUMN_REF) {
      bool found = false;
      if (node->num_child_ == 3 && node->children_[0] == nullptr
          && node->children_[1] != nullptr && node->children_[2] != nullptr
          && node->children_[2]->type_ == T_IDENT) {
        const ObString variable = graph_node_name(*node->children_[1]);
        const ObString property = graph_node_name(*node->children_[2]);
        for (int64_t i = 0; !found && i < bindings.count(); ++i) {
          const GraphBinding &binding = bindings.at(i);
          if (variable.case_compare(graph_node_name(*binding.variable_)) == 0) {
            for (int64_t j = 0; !found && j < graph.get_properties().count(); ++j) {
              const GraphProperty &p = graph.get_properties().at(j);
              found = p.element_id_ == binding.element_->id_ && property.case_compare(p.name_) == 0;
            }
          }
        }
      }
      if (!found) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "GRAPH_TABLE references other than declared variable.property mappings");
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
        if (node->children_[i] != nullptr) { ret = pending.push_back(GraphExpressionNode{node->children_[i]}); }
      }
    }
  }
  return ret;
}
} // namespace

int ObDMLResolver::resolve_graph_table(const ParseNode &node, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  table_item = nullptr;
  ObString name;
  ObString database_name;
  uint64_t database_id = OB_INVALID_ID;
  const GraphSchema *graph = nullptr;
  ObSEArray<GraphBinding, 8> bindings;
  if (params_.resolver_scope_stmt_type_ == T_CREATE_VIEW || params_.is_in_view_) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "GRAPH_TABLE in persistent views");
  } else if (!get_stmt()->is_select_stmt()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "GRAPH_TABLE as a DML target");
  } else if (node.num_child_ != 4 || node.children_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(resolve_table_relation_node(node.children_[0], name, database_name))) {
  } else if (OB_FAIL(schema_checker_->get_database_id(database_name, database_id))) {
  } else if (database_id != session_info_->get_database_id()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "cross-database GRAPH_TABLE");
  } else if (OB_FAIL(schema_checker_->get_schema_guard()->get_graph_schema(database_id, name, graph))) {
  } else if (graph == nullptr) {
    ret = OB_TABLE_NOT_EXIST;
    ObCStringHelper helper;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(name));
  } else {
    const ParseNode &chain = *node.children_[1];
    if (chain.num_child_ < 1 || chain.num_child_ > 33 || (chain.num_child_ % 2) == 0) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph chains longer than 16 edges");
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < chain.num_child_; ++i) {
      const ParseNode &pattern = *chain.children_[i];
      GraphBinding binding = {nullptr, nullptr, pattern.children_[0]};
      binding.element_ = graph->get_element(graph_node_name(*pattern.children_[1]));
      if (binding.element_ == nullptr || binding.element_->is_vertex() != ((i % 2) == 0)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "unknown graph labels or labels used for the wrong element kind");
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < bindings.count(); ++j) {
        if (graph_node_name(*binding.variable_).case_compare(graph_node_name(*bindings.at(j).variable_)) == 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "redeclaring a graph variable or cyclic graph patterns");
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_checker_->get_schema_guard()->get_table_schema(binding.element_->table_id_, binding.table_))) {
      } else if (binding.table_ == nullptr) {
        ret = OB_TABLE_NOT_EXIST;
      } else if (OB_FAIL(bindings.push_back(binding))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < chain.num_child_; ++i) {
      const ParseNode *filter = chain.children_[i]->children_[2];
      if (filter != nullptr) { ret = check_graph_expression(*filter, *graph, bindings); }
    }
    if (OB_SUCC(ret)) { ret = check_graph_expression(*node.children_[2], *graph, bindings); }
    if (OB_SUCC(ret)) {
      GraphRelationBuilder builder(*allocator_, database_name);
      ParseNode *select = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
      ParseNode *from = builder.make(T_FROM_LIST, bindings.count());
      ParseNode *where = nullptr;
      if (select != nullptr && from != nullptr) {
        for (int64_t i = 0; i < bindings.count(); ++i) {
          from->children_[i] = builder.relation(bindings.at(i));
          builder.append_condition(chain.children_[i]->children_[2], where);
        }
        for (int64_t i = 1; i < bindings.count(); i += 2) {
          const GraphBinding &edge = bindings.at(i);
          const bool reverse = chain.children_[i]->value_ == 1;
          const GraphBinding &source = bindings.at(reverse ? i + 1 : i - 1);
          const GraphBinding &destination = bindings.at(reverse ? i - 1 : i + 1);
          if (edge.element_->source_id_ != source.element_->id_
              || edge.element_->destination_id_ != destination.element_->id_) {
            ParseNode *false_node = builder.make(T_BOOL, 0);
            builder.append_condition(false_node, where);
          } else {
            for (int64_t k = 0; k < source.element_->key_count_; ++k) {
              builder.append_condition(builder.binary(T_OP_EQ,
                  builder.column(edge, edge.element_->source_columns_[k]),
                  builder.column(source, source.element_->key_columns_[k])), where);
            }
            for (int64_t k = 0; k < destination.element_->key_count_; ++k) {
              builder.append_condition(builder.binary(T_OP_EQ,
                  builder.column(edge, edge.element_->destination_columns_[k]),
                  builder.column(destination, destination.element_->key_columns_[k])), where);
            }
          }
        }
        select->children_[PARSE_SELECT_FROM] = from;
        select->children_[PARSE_SELECT_SELECT] = node.children_[2];
        if (where != nullptr) {
          ParseNode *clause = builder.make(T_WHERE_CLAUSE, 2);
          if (clause != nullptr) { clause->children_[0] = where; }
          select->children_[PARSE_SELECT_WHERE] = clause;
        }
      }
      if (OB_FAIL(builder.error())) {
      } else {
        // Like persistent views, graph mappings never resolve session-local
        // temporary tables in place of their bound persistent table IDs.
        ObSchemaGetterGuard &guard = *schema_checker_->get_schema_guard();
        const int64_t saved_session_id = guard.get_session_id();
        guard.set_session_id(0);
        ret = resolve_generate_table(*select, node.children_[3], table_item);
        guard.set_session_id(saved_session_id);
        if (OB_SUCC(ret)) {
          ObSchemaObjVersion dependency(graph->get_graph_id(), graph->get_schema_version(), DEPENDENCY_PROPERTY_GRAPH);
          dependency.is_db_explicit_ = node.children_[0]->children_[0] != nullptr;
          ret = get_stmt()->add_global_dependency_table(dependency);
        }
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
