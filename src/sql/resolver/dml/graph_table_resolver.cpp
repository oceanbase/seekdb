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
#include "sql/engine/graph/graph_path_spec.h"

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

struct GraphParseNode
{
  ParseNode *node_ = nullptr;
  TO_STRING_KV(KP_(node));
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
    char *buffer = nullptr;
    if (node != nullptr) {
      buffer = static_cast<char *>(allocator_.alloc(name.length() + 1));
      if (buffer == nullptr) {
        error_ = OB_ALLOCATE_MEMORY_FAILED;
        node = nullptr;
      } else {
        MEMCPY(buffer, name.ptr(), name.length());
        buffer[name.length()] = '\0';
        node->str_value_ = buffer;
        node->str_len_ = name.length();
      }
    }
    return node;
  }
  ParseNode *internal_identifier(const char *prefix, int64_t group, int64_t index)
  {
    ParseNode *node = nullptr;
    char *buffer = nullptr;
    if (error_ == OB_SUCCESS) {
      buffer = static_cast<char *>(allocator_.alloc(48));
      if (buffer == nullptr) {
        error_ = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        const int length = snprintf(buffer, 48, "%s%ld_%ld", prefix, group, index);
        if (length <= 0 || length >= 48) {
          error_ = OB_ERR_UNEXPECTED;
        } else {
          node = identifier(ObString(length, buffer));
        }
      }
    }
    return node;
  }
  ParseNode *integer(int64_t value)
  {
    ParseNode *node = make(T_INT, 0);
    if (node != nullptr) { node->value_ = value; }
    return node;
  }
  ParseNode *null_value()
  {
    return make(T_NULL, 0);
  }
  ParseNode *string(const ObString &value)
  {
    ParseNode *node = make(T_VARCHAR, 0);
    char *buffer = nullptr;
    if (node != nullptr) {
      buffer = static_cast<char *>(allocator_.alloc(value.length() + 1));
      if (buffer == nullptr) {
        error_ = OB_ALLOCATE_MEMORY_FAILED;
        node = nullptr;
      } else {
        MEMCPY(buffer, value.ptr(), value.length());
        buffer[value.length()] = '\0';
        node->str_value_ = buffer;
        node->str_len_ = value.length();
      }
    }
    return node;
  }
  ParseNode *function(const char *name, const ObIArray<GraphParseNode> &arguments)
  {
    // The parser represents a no-argument generic function with only its name
    // child; a two-child node with a null argument list is rejected by the raw
    // expression resolver.
    const ObItemType type = 0 == STRCASECMP(name, "if") ? T_FUN_SYS_IF : T_FUN_SYS;
    ParseNode *function = make(type, arguments.empty() ? 1 : 2);
    ParseNode *params = arguments.empty() ? nullptr : make(T_EXPR_LIST, arguments.count());
    if (function != nullptr) {
      function->children_[0] = identifier(ObString::make_string(name));
      if (!arguments.empty()) {
        function->children_[1] = params;
      }
      for (int64_t i = 0; params != nullptr && i < arguments.count(); ++i) {
        params->children_[i] = arguments.at(i).node_;
      }
    }
    return function;
  }
  ParseNode *clone(const ParseNode &source)
  {
    ParseNode *copy = make(source.type_, source.num_child_);
    if (copy != nullptr) {
      ParseNode **children = copy->children_;
      *copy = source;
      copy->children_ = children;
      for (int64_t i = 0; error_ == OB_SUCCESS && i < source.num_child_; ++i) {
        copy->children_[i] = source.children_[i] == nullptr ? nullptr : clone(*source.children_[i]);
      }
    }
    return copy;
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
  ParseNode *column(ParseNode *variable, const ObString &name)
  {
    ParseNode *ref = make(T_COLUMN_REF, 3);
    if (ref != nullptr) {
      ref->children_[1] = variable;
      ref->children_[2] = identifier(name);
    }
    return ref;
  }
  ParseNode *project(ParseNode *expression, ParseNode *alias)
  {
    ParseNode *project = make(T_PROJECT_STRING, 1);
    ParseNode *alias_node = alias == nullptr ? nullptr : make(T_ALIAS, 2);
    if (project != nullptr) {
      if (alias == nullptr) {
        project->children_[0] = expression;
      } else if (alias_node != nullptr) {
        alias_node->children_[0] = expression;
        alias_node->children_[1] = alias;
        alias_node->str_value_ = alias->str_value_;
        alias_node->str_len_ = alias->str_len_;
        project->children_[0] = alias_node;
      }
    }
    return project;
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
  ParseNode *index_hint(const ObString &index_name)
  {
    ParseNode *hint_type = make(T_USE, 0);
    ParseNode *index_list = make(T_NAME_LIST, 1);
    ParseNode *definition = make(T_INDEX_HINT_DEF, 2);
    ParseNode *hint_list = make(T_INDEX_HINT_LIST, 1);
    if (hint_type != nullptr && index_list != nullptr
        && definition != nullptr && hint_list != nullptr) {
      index_list->children_[0] = identifier(index_name);
      definition->children_[0] = hint_type;
      definition->children_[1] = index_list;
      hint_list->children_[0] = definition;
    }
    return hint_list;
  }
  ParseNode *relation(const GraphBinding &binding,
                      const ObString *index_name = nullptr)
  {
    ParseNode *name = make(T_RELATION_FACTOR, 2);
    ParseNode *alias = make(T_ALIAS, 5);
    if (name != nullptr && alias != nullptr) {
      // An outer CTE must not shadow the persistent table bound by the graph.
      name->children_[0] = identifier(database_name_);
      name->children_[1] = identifier(binding.table_->get_table_name_str());
      name->str_value_ = name->children_[1]->str_value_;
      name->str_len_ = name->children_[1]->str_len_;
      alias->children_[0] = name;
      alias->children_[1] = binding.variable_;
      if (index_name != nullptr && !index_name->empty()) {
        alias->children_[2] = index_hint(*index_name);
      }
    }
    return alias;
  }
  ParseNode *relation(const ObString &name, ParseNode *variable)
  {
    ParseNode *relation = make(T_RELATION_FACTOR, 2);
    ParseNode *alias = make(T_ALIAS, 5);
    if (relation != nullptr && alias != nullptr) {
      relation->children_[1] = identifier(name);
      relation->str_value_ = relation->children_[1]->str_value_;
      relation->str_len_ = relation->children_[1]->str_len_;
      alias->children_[0] = relation;
      alias->children_[1] = variable;
    }
    return alias;
  }
  ParseNode *false_value()
  {
    return make(T_BOOL, 0);
  }
  void force_serial(ParseNode *select)
  {
    ParseNode *hint_list = make(T_HINT_OPTION_LIST, 1);
    ParseNode *no_parallel = make(T_NO_PARALLEL, 0);
    if (select == nullptr || hint_list == nullptr || no_parallel == nullptr) {
      if (error_ == OB_SUCCESS) { error_ = OB_ERR_UNEXPECTED; }
    } else if (select->type_ != T_SELECT || select->num_child_ <= PARSE_SELECT_HINTS) {
      error_ = OB_ERR_UNEXPECTED;
    } else {
      hint_list->children_[0] = no_parallel;
      select->children_[PARSE_SELECT_HINTS] = hint_list;
    }
  }
  void set_error(int error)
  {
    if (error_ == OB_SUCCESS) { error_ = error; }
  }
  int error() const { return error_; }
private:
  ObIAllocator &allocator_;
  ObString database_name_;
  int error_;
};

// Pick only a readable, visible ordinary index whose user-declared key starts
// with every typed source/destination column in graph-mapping order. The
// generated USE INDEX is a correctness-neutral preference: absence of such an
// index leaves the recursive member on its one-scan-per-hop fallback.
int find_adjacency_index(const GraphBinding &edge,
                         GraphPathDirection direction,
                         ObSchemaGetterGuard &schema_guard,
                         ObString &index_name)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObAuxTableMetaInfo, 8> index_infos;
  const bool reverse = direction == GraphPathDirection::IN;
  const uint64_t *columns = reverse ? edge.element_->destination_columns_
                                    : edge.element_->source_columns_;
  const int64_t column_count = reverse ? edge.element_->destination_key_count_
                                       : edge.element_->source_key_count_;
  index_name.reset();
  if (OB_FAIL(edge.table_->get_simple_index_infos(index_infos))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && index_name.empty()
       && i < index_infos.count(); ++i) {
    const ObTableSchema *index_schema = nullptr;
    if (OB_FAIL(schema_guard.get_table_schema(index_infos.at(i).table_id_,
                                               index_schema))) {
    } else if (OB_ISNULL(index_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("adjacency index schema is missing", K(ret), K(index_infos.at(i)));
    } else if ((!index_schema->is_normal_index() && !index_schema->is_unique_index())
               || index_schema->is_global_index_table()
               || !index_schema->can_read_index()
               || !index_schema->is_index_visible()
               || index_schema->is_final_invalid_index()) {
      // Special, remote, invisible or unavailable indexes are not adjacency
      // access paths for the single-node WALK operator.
    } else {
      const ObIndexInfo &index_columns = index_schema->get_index_info();
      bool prefix_matches = index_columns.get_size() >= column_count;
      for (int64_t j = 0; prefix_matches && j < column_count; ++j) {
        const ObIndexColumn *column = index_columns.get_column(j);
        prefix_matches = column != nullptr && column->column_id_ == columns[j];
      }
      if (prefix_matches && OB_FAIL(index_schema->get_index_name(index_name))) {
        LOG_WARN("failed to read adjacency index name", K(ret), KPC(index_schema));
      }
    }
  }
  return ret;
}

struct GraphExpressionNode
{
  const ParseNode *node_;
  TO_STRING_KV(KP_(node));
};

bool contains_item_type(const ParseNode &root, ObItemType type)
{
  bool found = root.type_ == type;
  for (int64_t i = 0; !found && i < root.num_child_; ++i) {
    if (root.children_[i] != nullptr) {
      found = contains_item_type(*root.children_[i], type);
    }
  }
  return found;
}

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
          if (ObSchemaNameComparator().compare(variable, graph_node_name(*binding.variable_)) == 0) {
            for (int64_t j = 0; !found && j < graph.get_properties().count(); ++j) {
              const GraphProperty &p = graph.get_properties().at(j);
              found = p.element_id_ == binding.element_->id_
                  && ObSchemaNameComparator().compare(property, p.name_) == 0;
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

enum class GraphExprBindingKind
{
  SCALAR,
  GROUP,
  NULL_SCALAR,
  FORBIDDEN
};

struct GraphRuntimePropertyArray
{
  uint64_t column_id_ = common::OB_INVALID_ID;
  ParseNode *array_ = nullptr;
  TO_STRING_KV(K_(column_id), KP_(array));
};

struct GraphRuntimeGroup
{
  ParseNode *count_ = nullptr;
  ParseNode *identity_array_ = nullptr;
  const ObIArray<GraphRuntimePropertyArray> *property_arrays_ = nullptr;
  TO_STRING_KV(KP_(count), KP_(identity_array), KP_(property_arrays));
};

struct GraphExprBinding
{
  ObString name_;
  GraphExprBindingKind kind_ = GraphExprBindingKind::SCALAR;
  const GraphBinding *binding_ = nullptr;
  const ObIArray<GraphBinding> *group_ = nullptr;
  const GraphRuntimeGroup *runtime_group_ = nullptr;
  int64_t element_number_ = -1;
  bool is_edge_ = false;
  TO_STRING_KV(K_(name), K_(kind), KP_(binding), KP_(group), KP_(runtime_group),
               K_(element_number), K_(is_edge));
};

bool graph_name_equal(const ObString &left, const ObString &right)
{
  return ObSchemaNameComparator().compare(left, right) == 0;
}

bool get_graph_variable_name(const ParseNode &node, ObString &name)
{
  bool found = false;
  if (node.type_ == T_IDENT) {
    name = graph_node_name(node);
    found = true;
  } else if (node.type_ == T_COLUMN_REF && node.num_child_ == 3
             && node.children_[0] == nullptr && node.children_[1] == nullptr
             && node.children_[2] != nullptr && node.children_[2]->type_ == T_IDENT) {
    name = graph_node_name(*node.children_[2]);
    found = true;
  }
  return found;
}

bool is_graph_function(const ParseNode &node, const char *name)
{
  bool match = false;
  if (node.type_ == T_FUN_SYS && node.num_child_ >= 1 && node.children_[0] != nullptr) {
    match = graph_node_name(*node.children_[0]).case_compare(name) == 0;
  }
  return match;
}

const ParseNode *get_single_function_argument(const ParseNode &node)
{
  const ParseNode *argument = nullptr;
  if (node.type_ == T_FUN_SYS && node.num_child_ == 2 && node.children_[1] != nullptr) {
    const ParseNode *params = node.children_[1];
    if (params->type_ == T_EXPR_LIST && params->num_child_ == 1) {
      argument = params->children_[0];
    }
  }
  return argument;
}

const GraphProperty *find_graph_property(const GraphSchema &graph,
                                         uint64_t element_id,
                                         const ObString &name)
{
  const GraphProperty *property = nullptr;
  for (int64_t i = 0; property == nullptr && i < graph.get_properties().count(); ++i) {
    const GraphProperty &candidate = graph.get_properties().at(i);
    if (candidate.element_id_ == element_id && graph_name_equal(candidate.name_, name)) {
      property = &candidate;
    }
  }
  return property;
}

int collect_path_projection_requirements(
    const ParseNode &node,
    const GraphSchema &graph,
    const GraphBinding &edge,
    bool &need_match_number,
    bool &need_edge_identity,
    ObIArray<const GraphProperty *> &edge_properties)
{
  int ret = OB_SUCCESS;
  if (is_graph_function(node, "matchnum")) {
    need_match_number = true;
    need_edge_identity = true;
  } else if (node.type_ == T_FUN_JSON_ARRAYAGG && node.num_child_ == 2
             && node.children_ != nullptr && node.children_[1] != nullptr) {
    const ParseNode &argument = *node.children_[1];
    ObString variable;
    if (is_graph_function(argument, "edge_id")) {
      const ParseNode *identity_argument = get_single_function_argument(argument);
      if (identity_argument != nullptr
          && get_graph_variable_name(*identity_argument, variable)
          && graph_name_equal(variable, graph_node_name(*edge.variable_))) {
        need_edge_identity = true;
      }
    } else if (argument.type_ == T_COLUMN_REF && argument.num_child_ == 3
               && argument.children_[0] == nullptr
               && argument.children_[1] != nullptr
               && argument.children_[2] != nullptr
               && argument.children_[2]->type_ == T_IDENT
               && graph_name_equal(graph_node_name(*argument.children_[1]),
                                   graph_node_name(*edge.variable_))) {
      const GraphProperty *property = find_graph_property(
          graph, edge.element_->id_, graph_node_name(*argument.children_[2]));
      bool exists = false;
      for (int64_t i = 0; property != nullptr && !exists
                          && i < edge_properties.count(); ++i) {
        exists = edge_properties.at(i)->column_id_ == property->column_id_;
      }
      if (property != nullptr && !exists) {
        ret = edge_properties.push_back(property);
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < node.num_child_; ++i) {
    if (node.children_[i] != nullptr) {
      ret = collect_path_projection_requirements(
          *node.children_[i], graph, edge, need_match_number,
          need_edge_identity, edge_properties);
    }
  }
  return ret;
}

class GraphExprRewriter final
{
public:
  GraphExprRewriter(GraphRelationBuilder &builder,
                    const GraphSchema &graph,
                    const ObString &database_name,
                    const ObIArray<GraphExprBinding> &bindings,
                    const ParseNode *match_number,
                    bool allow_outer_reference)
    : builder_(builder),
      graph_(graph),
      database_name_(database_name),
      bindings_(bindings),
      match_number_(match_number),
      allow_outer_reference_(allow_outer_reference)
  {}

  int rewrite(const ParseNode &source, ParseNode *&result)
  {
    int ret = OB_SUCCESS;
    result = nullptr;
    if (source.type_ == T_SELECT || source.type_ == T_GRAPH_TABLE
        || source.type_ == T_WINDOW_FUNCTION) {
      ret = unsupported("subqueries, nested GRAPH_TABLE or window functions inside a graph path expression");
    } else if (source.type_ == T_COLUMN_REF) {
      ret = rewrite_column(source, result);
    } else if (source.type_ == T_FUN_COUNT || source.type_ == T_FUN_JSON_ARRAYAGG) {
      ret = rewrite_group_aggregate(source, result);
    } else if (is_graph_function(source, "vertex_id")) {
      ret = rewrite_identity(source, false, result);
    } else if (is_graph_function(source, "edge_id")) {
      ret = rewrite_identity(source, true, result);
    } else if (is_graph_function(source, "matchnum")) {
      const bool has_no_arguments = source.num_child_ == 1
          || (source.num_child_ == 2 && source.children_ != nullptr
              && source.children_[1] == nullptr);
      if (has_no_arguments && match_number_ != nullptr) {
        result = builder_.clone(*match_number_);
      } else {
        ret = unsupported("MATCHNUM() without arguments in a path result");
      }
    } else if (is_graph_function(source, "element_number")) {
      ret = rewrite_element_number(source, result);
    } else if (IS_AGGR_FUN(source.type_)) {
      ret = unsupported("only COUNT and JSON_ARRAYAGG over the quantified edge variable");
    } else {
      result = builder_.make(source.type_, source.num_child_);
      if (result == nullptr) {
        ret = builder_.error();
      } else {
        ParseNode **children = result->children_;
        *result = source;
        result->children_ = children;
        for (int64_t i = 0; OB_SUCC(ret) && i < source.num_child_; ++i) {
          if (source.children_[i] != nullptr) {
            ret = rewrite(*source.children_[i], result->children_[i]);
          } else {
            result->children_[i] = nullptr;
          }
        }
      }
    }
    if (OB_SUCC(ret) && builder_.error() != OB_SUCCESS) {
      ret = builder_.error();
    }
    return ret;
  }

private:
  int unsupported(const char *feature)
  {
    const int ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, feature);
    builder_.set_error(ret);
    return ret;
  }

  const GraphExprBinding *find_binding(const ObString &name) const
  {
    const GraphExprBinding *binding = nullptr;
    for (int64_t i = 0; binding == nullptr && i < bindings_.count(); ++i) {
      if (graph_name_equal(name, bindings_.at(i).name_)) {
        binding = &bindings_.at(i);
      }
    }
    return binding;
  }

  int rewrite_column(const ParseNode &source, ParseNode *&result)
  {
    int ret = OB_SUCCESS;
    if (source.num_child_ != 3 || source.children_[0] != nullptr
        || source.children_[2] == nullptr || source.children_[2]->type_ != T_IDENT) {
      ret = unsupported("qualified graph variable.property references");
    } else if (source.children_[1] == nullptr) {
      if (allow_outer_reference_) {
        result = builder_.clone(source);
      } else {
        ret = unsupported("unqualified columns inside GRAPH_TABLE path expressions");
      }
    } else {
      const ObString variable = graph_node_name(*source.children_[1]);
      const ObString property_name = graph_node_name(*source.children_[2]);
      const GraphExprBinding *expr_binding = find_binding(variable);
      if (expr_binding == nullptr) {
        if (allow_outer_reference_) {
          result = builder_.clone(source);
        } else {
          ret = unsupported("references other than declared graph path variables");
        }
      } else if (expr_binding->kind_ == GraphExprBindingKind::GROUP) {
        ret = unsupported("a quantified edge property outside COUNT or JSON_ARRAYAGG");
      } else if (expr_binding->kind_ == GraphExprBindingKind::FORBIDDEN) {
        ret = unsupported("a graph variable outside its element filter");
      } else if (expr_binding->kind_ == GraphExprBindingKind::NULL_SCALAR) {
        result = builder_.null_value();
      } else if (expr_binding->binding_ == nullptr || expr_binding->binding_->element_ == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const GraphProperty *property = find_graph_property(
            graph_, expr_binding->binding_->element_->id_, property_name);
        if (property == nullptr) {
          ret = unsupported("an unknown graph property");
        } else {
          result = builder_.column(*expr_binding->binding_, property->column_id_);
        }
      }
    }
    return ret;
  }

  int rewrite_group_aggregate(const ParseNode &source, ParseNode *&result)
  {
    int ret = OB_SUCCESS;
    const ParseNode *argument = nullptr;
    const GraphExprBinding *group_binding = nullptr;
    if (source.num_child_ != 2 || source.children_[1] == nullptr
        || (source.children_[0] != nullptr && source.children_[0]->type_ == T_DISTINCT)) {
      ret = unsupported("DISTINCT or malformed graph group-variable aggregates");
    } else {
      argument = source.children_[1];
      ObString group_name;
      if (argument->type_ == T_COLUMN_REF && argument->num_child_ == 3
          && argument->children_[0] == nullptr
          && argument->children_[1] != nullptr && argument->children_[2] != nullptr
          && argument->children_[2]->type_ == T_IDENT) {
        group_name = graph_node_name(*argument->children_[1]);
      } else if (is_graph_function(*argument, "edge_id")) {
        const ParseNode *identity_argument = get_single_function_argument(*argument);
        if (identity_argument != nullptr) {
          (void)get_graph_variable_name(*identity_argument, group_name);
        }
      }
      group_binding = group_name.empty() ? nullptr : find_binding(group_name);
      if (group_binding == nullptr || group_binding->kind_ != GraphExprBindingKind::GROUP
          || !group_binding->is_edge_
          || (group_binding->group_ == nullptr && group_binding->runtime_group_ == nullptr)) {
        ret = unsupported("COUNT or JSON_ARRAYAGG over the quantified edge variable");
      } else if (argument->type_ == T_COLUMN_REF
                 && (group_binding->binding_ == nullptr
                     || group_binding->binding_->element_ == nullptr
                     || find_graph_property(graph_, group_binding->binding_->element_->id_,
                                            graph_node_name(*argument->children_[2])) == nullptr)) {
        ret = unsupported("an unknown graph property");
      }
    }
    if (OB_SUCC(ret) && group_binding->runtime_group_ != nullptr) {
      const GraphRuntimeGroup &runtime = *group_binding->runtime_group_;
      if (source.type_ == T_FUN_COUNT && runtime.count_ != nullptr) {
        result = builder_.clone(*runtime.count_);
      } else if (is_graph_function(*argument, "edge_id")
                 && runtime.identity_array_ != nullptr) {
        result = builder_.clone(*runtime.identity_array_);
      } else if (argument->type_ == T_COLUMN_REF
                 && runtime.property_arrays_ != nullptr) {
        const GraphProperty *property = find_graph_property(
            graph_, group_binding->binding_->element_->id_,
            graph_node_name(*argument->children_[2]));
        for (int64_t i = 0; result == nullptr
             && property != nullptr && i < runtime.property_arrays_->count(); ++i) {
          const GraphRuntimePropertyArray &candidate = runtime.property_arrays_->at(i);
          if (candidate.column_id_ == property->column_id_ && candidate.array_ != nullptr) {
            result = builder_.clone(*candidate.array_);
          }
        }
        if (result == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else if (OB_SUCC(ret) && source.type_ == T_FUN_COUNT) {
      result = builder_.integer(group_binding->group_->count());
    } else if (OB_SUCC(ret) && group_binding->group_->empty()) {
      // Match the existing aggregate-on-empty-set result.
      result = builder_.null_value();
    } else if (OB_SUCC(ret)) {
      ObSEArray<GraphParseNode, GRAPH_WALK_MAX_HOPS> values;
      for (int64_t i = 0; OB_SUCC(ret) && i < group_binding->group_->count(); ++i) {
        ObSEArray<GraphExprBinding, 8> scalar_bindings;
        if (OB_FAIL(scalar_bindings.assign(bindings_))) {
        } else {
          for (int64_t j = 0; j < scalar_bindings.count(); ++j) {
            if (graph_name_equal(scalar_bindings.at(j).name_, group_binding->name_)) {
              scalar_bindings.at(j).kind_ = GraphExprBindingKind::SCALAR;
              scalar_bindings.at(j).binding_ = &group_binding->group_->at(i);
              scalar_bindings.at(j).group_ = nullptr;
              scalar_bindings.at(j).element_number_ = i + 1;
            }
          }
          ParseNode *value = nullptr;
          GraphExprRewriter scalar_rewriter(builder_, graph_, database_name_,
                                             scalar_bindings, match_number_, false);
          if (OB_FAIL(scalar_rewriter.rewrite(*argument, value))) {
          } else if (OB_FAIL(values.push_back(GraphParseNode{value}))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        result = builder_.function("json_array", values);
      }
    }
    return ret;
  }

  int rewrite_identity(const ParseNode &source, bool expect_edge, ParseNode *&result)
  {
    int ret = OB_SUCCESS;
    ObString variable;
    const ParseNode *argument = get_single_function_argument(source);
    const GraphExprBinding *expr_binding = nullptr;
    if (argument == nullptr || !get_graph_variable_name(*argument, variable)
        || (expr_binding = find_binding(variable)) == nullptr) {
      ret = unsupported(expect_edge ? "EDGE_ID(edge_variable)" : "VERTEX_ID(vertex_variable)");
    } else if (expr_binding->kind_ == GraphExprBindingKind::FORBIDDEN) {
      ret = unsupported("a graph variable outside its element filter");
    } else if (expr_binding->kind_ == GraphExprBindingKind::GROUP) {
      ret = unsupported("EDGE_ID of a quantified edge outside COUNT or JSON_ARRAYAGG");
    } else if (expr_binding->is_edge_ != expect_edge) {
      ret = unsupported(expect_edge ? "EDGE_ID on an edge variable" : "VERTEX_ID on a vertex variable");
    } else if (expr_binding->kind_ == GraphExprBindingKind::NULL_SCALAR) {
      result = builder_.null_value();
    } else if (expr_binding->binding_ == nullptr || expr_binding->binding_->element_ == nullptr
               || expr_binding->binding_->table_ == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const GraphBinding &binding = *expr_binding->binding_;
      ObSEArray<GraphParseNode, 8> key_arguments;
      ObSEArray<GraphParseNode, 8> identity_arguments;
      for (int64_t i = 0; OB_SUCC(ret) && i < binding.element_->key_count_; ++i) {
        const ObColumnSchemaV2 *column = binding.table_->get_column_schema(
            binding.element_->key_columns_[i]);
        if (column == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(key_arguments.push_back(
                       GraphParseNode{builder_.string(column->get_column_name_str())}))) {
        } else if (OB_FAIL(key_arguments.push_back(GraphParseNode{
                       builder_.column(binding, binding.element_->key_columns_[i])}))) {
        }
      }
      ParseNode *key_object = nullptr;
      if (OB_SUCC(ret)) {
        key_object = builder_.function("json_object", key_arguments);
        if (OB_FAIL(identity_arguments.push_back(GraphParseNode{
                builder_.string(ObString::make_string("GRAPH_OWNER"))}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{builder_.string(database_name_)}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{
                       builder_.string(ObString::make_string("GRAPH_NAME"))}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{builder_.string(graph_.get_name())}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{
                       builder_.string(ObString::make_string("ELEM_TABLE"))}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{
                       builder_.string(binding.table_->get_table_name_str())}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{
                       builder_.string(ObString::make_string("KEY_VALUE"))}))) {
        } else if (OB_FAIL(identity_arguments.push_back(GraphParseNode{key_object}))) {
        } else {
          result = builder_.function("json_object", identity_arguments);
        }
      }
    }
    return ret;
  }

  int rewrite_element_number(const ParseNode &source, ParseNode *&result)
  {
    int ret = OB_SUCCESS;
    ObString variable;
    const ParseNode *argument = get_single_function_argument(source);
    const GraphExprBinding *expr_binding = nullptr;
    if (argument == nullptr || !get_graph_variable_name(*argument, variable)
        || (expr_binding = find_binding(variable)) == nullptr
        || expr_binding->kind_ == GraphExprBindingKind::GROUP) {
      ret = unsupported("ELEMENT_NUMBER on a ONE ROW PER STEP iteration variable");
    } else if (expr_binding->kind_ == GraphExprBindingKind::FORBIDDEN) {
      ret = unsupported("a graph variable outside its element filter");
    } else if (expr_binding->kind_ == GraphExprBindingKind::NULL_SCALAR) {
      result = builder_.null_value();
    } else if (expr_binding->element_number_ < 0) {
      ret = unsupported("ELEMENT_NUMBER on a ONE ROW PER STEP iteration variable");
    } else {
      result = builder_.integer(expr_binding->element_number_);
    }
    return ret;
  }

private:
  GraphRelationBuilder &builder_;
  const GraphSchema &graph_;
  ObString database_name_;
  const ObIArray<GraphExprBinding> &bindings_;
  const ParseNode *match_number_;
  bool allow_outer_reference_;
};

int add_expr_binding(ObIArray<GraphExprBinding> &bindings,
                     const ParseNode &name,
                     GraphExprBindingKind kind,
                     const GraphBinding *binding,
                     const ObIArray<GraphBinding> *group,
                     int64_t element_number,
                     bool is_edge,
                     const GraphRuntimeGroup *runtime_group = nullptr)
{
  GraphExprBinding item;
  item.name_ = graph_node_name(name);
  item.kind_ = kind;
  item.binding_ = binding;
  item.group_ = group;
  item.runtime_group_ = runtime_group;
  item.element_number_ = element_number;
  item.is_edge_ = is_edge;
  return bindings.push_back(item);
}

ParseNode *make_row_number(GraphRelationBuilder &builder,
                           const ObIArray<GraphParseNode> &ordering)
{
  ParseNode *sort_list = builder.make(T_SORT_LIST, ordering.count());
  for (int64_t i = 0; sort_list != nullptr && i < ordering.count(); ++i) {
    ParseNode *sort_key = builder.make(T_SORT_KEY, 2);
    ParseNode *sort_direction = builder.make(T_SORT_ASC, 0);
    if (sort_key != nullptr && sort_direction != nullptr) {
      sort_key->children_[0] = ordering.at(i).node_;
      sort_key->children_[1] = sort_direction;
      sort_direction->value_ = 2;
      sort_direction->is_empty_ = 1;
      sort_list->children_[i] = sort_key;
    }
  }
  ParseNode *order_by = builder.make(T_ORDER_BY, 1);
  ParseNode *window_clause = builder.make(T_WIN_GENERALIZED_WINDOW, 3);
  ParseNode *new_window_clause = builder.make(T_WIN_NEW_GENERALIZED_WINDOW, 2);
  ParseNode *row_number_function = builder.make(T_WIN_FUN_ROW_NUMBER, 0);
  ParseNode *row_number = builder.make(T_WINDOW_FUNCTION, 2);
  if (order_by != nullptr && window_clause != nullptr && new_window_clause != nullptr
      && row_number != nullptr) {
    order_by->children_[0] = sort_list;
    window_clause->children_[1] = order_by;
    new_window_clause->children_[1] = window_clause;
    row_number->children_[0] = row_number_function;
    row_number->children_[1] = new_window_clause;
  }
  return row_number;
}

ParseNode *make_match_number(GraphRelationBuilder &builder,
                             const ObIArray<GraphBinding> &vertices,
                             const ObIArray<GraphBinding> &edges,
                             int64_t hop,
                             int64_t lower_bound,
                             int64_t upper_bound)
{
  ParseNode *sort_list = builder.make(T_SORT_LIST,
      edges.empty() ? vertices.at(0).element_->key_count_
                    : edges.count() * edges.at(0).element_->key_count_);
  int64_t sort_index = 0;
  if (sort_list != nullptr) {
    if (edges.empty()) {
      for (int64_t i = 0; i < vertices.at(0).element_->key_count_; ++i) {
        ParseNode *sort_key = builder.make(T_SORT_KEY, 2);
        ParseNode *sort_direction = builder.make(T_SORT_ASC, 0);
        if (sort_key != nullptr && sort_direction != nullptr) {
          sort_key->children_[0] = builder.column(
              vertices.at(0), vertices.at(0).element_->key_columns_[i]);
          sort_key->children_[1] = sort_direction;
          sort_direction->value_ = 2;
          sort_direction->is_empty_ = 1;
          sort_list->children_[sort_index++] = sort_key;
        }
      }
    } else {
      for (int64_t e = 0; e < edges.count(); ++e) {
        for (int64_t i = 0; i < edges.at(e).element_->key_count_; ++i) {
          ParseNode *sort_key = builder.make(T_SORT_KEY, 2);
          ParseNode *sort_direction = builder.make(T_SORT_ASC, 0);
          if (sort_key != nullptr && sort_direction != nullptr) {
            sort_key->children_[0] = builder.column(
                edges.at(e), edges.at(e).element_->key_columns_[i]);
            sort_key->children_[1] = sort_direction;
            sort_direction->value_ = 2;
            sort_direction->is_empty_ = 1;
            sort_list->children_[sort_index++] = sort_key;
          }
        }
      }
    }
  }
  ParseNode *order_by = builder.make(T_ORDER_BY, 1);
  ParseNode *window_clause = builder.make(T_WIN_GENERALIZED_WINDOW, 3);
  ParseNode *new_window_clause = builder.make(T_WIN_NEW_GENERALIZED_WINDOW, 2);
  ParseNode *row_number_function = builder.make(T_WIN_FUN_ROW_NUMBER, 0);
  ParseNode *row_number = builder.make(T_WINDOW_FUNCTION, 2);
  if (order_by != nullptr && window_clause != nullptr && new_window_clause != nullptr
      && row_number != nullptr) {
    order_by->children_[0] = sort_list;
    window_clause->children_[1] = order_by;
    new_window_clause->children_[1] = window_clause;
    row_number->children_[0] = row_number_function;
    row_number->children_[1] = new_window_clause;
  }
  // Interleave the bounded-hop branches so every path gets a distinct positive
  // number while all step rows for that path keep the same value. Filtered
  // candidates may still leave gaps, as allowed by MATCHNUM semantics.
  const int64_t branch_count = upper_bound - lower_bound + 1;
  const int64_t branch_number = hop - lower_bound + 1;
  return builder.binary(T_OP_ADD,
      builder.binary(T_OP_MUL,
                     builder.binary(T_OP_MINUS, row_number, builder.integer(1)),
                     builder.integer(branch_count)),
      builder.integer(branch_number));
}

int rewrite_graph_filter(GraphRelationBuilder &builder,
                         const GraphSchema &graph,
                         const ObString &database_name,
                         const ParseNode *filter,
                         const ParseNode &logical_name,
                         const GraphBinding *binding,
                         const GraphBinding *forbidden_one,
                         const GraphBinding *forbidden_two,
                         bool allow_outer,
                         ParseNode *&condition)
{
  int ret = OB_SUCCESS;
  if (filter != nullptr) {
    ObSEArray<GraphExprBinding, 1> expr_bindings;
    if (OB_FAIL(add_expr_binding(expr_bindings, logical_name,
                                GraphExprBindingKind::SCALAR, binding,
                                nullptr, -1, !binding->element_->is_vertex()))) {
    } else if (forbidden_one != nullptr
               && OB_FAIL(add_expr_binding(expr_bindings, *forbidden_one->variable_,
                                           GraphExprBindingKind::FORBIDDEN,
                                           forbidden_one, nullptr, -1,
                                           !forbidden_one->element_->is_vertex()))) {
    } else if (forbidden_two != nullptr
               && OB_FAIL(add_expr_binding(expr_bindings, *forbidden_two->variable_,
                                           GraphExprBindingKind::FORBIDDEN,
                                           forbidden_two, nullptr, -1,
                                           !forbidden_two->element_->is_vertex()))) {
    } else {
      GraphExprRewriter rewriter(builder, graph, database_name, expr_bindings,
                                 nullptr, allow_outer);
      ret = rewriter.rewrite(*filter, condition);
    }
  }
  return ret;
}

int rewrite_graph_projects(GraphRelationBuilder &builder,
                           const GraphSchema &graph,
                           const ObString &database_name,
                           const ParseNode &projects,
                           const ObIArray<GraphExprBinding> &expr_bindings,
                           const ParseNode *match_number,
                           ParseNode *&rewritten_projects)
{
  int ret = OB_SUCCESS;
  rewritten_projects = builder.make(T_PROJECT_LIST, projects.num_child_);
  if (rewritten_projects == nullptr) {
    ret = builder.error();
  }
  GraphExprRewriter rewriter(builder, graph, database_name, expr_bindings,
                             match_number, false);
  for (int64_t i = 0; OB_SUCC(ret) && i < projects.num_child_; ++i) {
    const ParseNode *project = projects.children_[i];
    if (project == nullptr || project->type_ != T_PROJECT_STRING || project->num_child_ != 1
        || project->children_[0] == nullptr || project->children_[0]->type_ != T_ALIAS
        || project->children_[0]->num_child_ < 2 || project->children_[0]->children_[0] == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ParseNode *new_project = builder.clone(*project);
      ParseNode *expression = nullptr;
      if (new_project == nullptr) {
        ret = builder.error();
      } else if (OB_FAIL(rewriter.rewrite(*project->children_[0]->children_[0], expression))) {
      } else {
        new_project->children_[0]->children_[0] = expression;
        rewritten_projects->children_[i] = new_project;
      }
    }
  }
  return ret;
}

ParseNode *make_internal_column(GraphRelationBuilder &builder,
                                ParseNode *variable,
                                const char *prefix,
                                int64_t index)
{
  ParseNode *name = builder.internal_identifier(prefix, 0, index);
  return name == nullptr ? nullptr : builder.column(variable, graph_node_name(*name));
}

int make_project_list(GraphRelationBuilder &builder,
                      const ObIArray<GraphParseNode> &expressions,
                      ParseNode *&projects)
{
  int ret = OB_SUCCESS;
  projects = builder.make(T_PROJECT_LIST, expressions.count());
  if (projects == nullptr) {
    ret = builder.error();
  } else {
    for (int64_t i = 0; i < expressions.count(); ++i) {
      projects->children_[i] = builder.project(expressions.at(i).node_, nullptr);
    }
  }
  return ret;
}

int make_edge_identity(GraphRelationBuilder &builder,
                       const GraphSchema &graph,
                       const ObString &database_name,
                       const GraphBinding &logical_edge,
                       const GraphBinding &physical_edge,
                       ParseNode *&identity)
{
  int ret = OB_SUCCESS;
  ObSEArray<GraphParseNode, 1> arguments;
  ObSEArray<GraphExprBinding, 1> bindings;
  ParseNode *edge_id = nullptr;
  if (OB_FAIL(arguments.push_back(GraphParseNode{logical_edge.variable_}))) {
  } else if (OB_ISNULL(edge_id = builder.function("edge_id", arguments))) {
    ret = builder.error();
  } else if (OB_FAIL(add_expr_binding(bindings, *logical_edge.variable_,
                                      GraphExprBindingKind::SCALAR,
                                      &physical_edge, nullptr, -1, true))) {
  } else {
    GraphExprRewriter rewriter(builder, graph, database_name, bindings,
                               nullptr, false);
    ret = rewriter.rewrite(*edge_id, identity);
  }
  return ret;
}

// Build a real breadth-first feedback loop for the common homogeneous
// ONE ROW PER MATCH case. The recursive member performs exactly one edge hop,
// joins the target vertex on every iteration, and carries only typed frontier
// keys plus JSON presentation arrays. Source predicates live exclusively in
// the anchor; terminal predicates live exclusively in the result query.
int build_recursive_graph_match(GraphRelationBuilder &builder,
                                const GraphSchema &graph,
                                const ObString &database_name,
                                const GraphPathDesc &path_desc,
                                const ObString *adjacency_index,
                                const GraphBinding &source_pattern,
                                const GraphBinding &edge_pattern,
                                const GraphBinding &target_pattern,
                                const ParseNode *source_filter,
                                const ParseNode *edge_filter,
                                const ParseNode *target_filter,
                                const ParseNode &projects,
                                ParseNode *&select)
{
  int ret = OB_SUCCESS;
  static const ObString CTE_NAME = ObString::make_string("__g_walk_frontier");
  static const ObString DEPTH_NAME = ObString::make_string("__g_depth");
  static const char *SOURCE_KEY_PREFIX = "__g_source_key_";
  static const char *CURRENT_KEY_PREFIX = "__g_current_key_";
  static const char *EDGE_PROPERTY_PREFIX = "__g_edge_property_";
  static const ObString EDGE_IDENTITY_NAME = ObString::make_string("__g_edge_identity");

  ParseNode *seed_alias = builder.identifier(ObString::make_string("__g_seed_vertex"));
  ParseNode *frontier_alias = builder.identifier(ObString::make_string("__g_frontier"));
  ParseNode *edge_alias = builder.identifier(ObString::make_string("__g_step_edge"));
  ParseNode *step_vertex_alias = builder.identifier(ObString::make_string("__g_step_vertex"));
  ParseNode *path_alias = builder.identifier(ObString::make_string("__g_path"));
  ParseNode *result_source_alias = builder.identifier(ObString::make_string("__g_result_source"));
  ParseNode *result_target_alias = builder.identifier(ObString::make_string("__g_result_target"));
  GraphBinding seed = {source_pattern.element_, source_pattern.table_, seed_alias};
  GraphBinding edge = {edge_pattern.element_, edge_pattern.table_, edge_alias};
  GraphBinding step_vertex = {target_pattern.element_, target_pattern.table_, step_vertex_alias};
  GraphBinding result_source = {source_pattern.element_, source_pattern.table_, result_source_alias};
  GraphBinding result_target = {target_pattern.element_, target_pattern.table_, result_target_alias};

  bool need_match_number = false;
  bool need_edge_identity = false;
  ObSEArray<const GraphProperty *, 4> edge_properties;
  if (OB_FAIL(collect_path_projection_requirements(
          projects, graph, edge_pattern, need_match_number,
          need_edge_identity, edge_properties))) {
  }

  ObSEArray<GraphParseNode, 32> anchor_exprs;
  ObSEArray<GraphParseNode, 32> recursive_exprs;
  ObSEArray<GraphParseNode, 32> cte_column_names;
  if (OB_SUCC(ret)) {
    ret = anchor_exprs.push_back(GraphParseNode{builder.integer(0)});
  }
  if (OB_SUCC(ret)) {
    ret = cte_column_names.push_back(GraphParseNode{builder.identifier(DEPTH_NAME)});
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < source_pattern.element_->key_count_; ++i) {
    ParseNode *name = builder.internal_identifier(SOURCE_KEY_PREFIX, 0, i);
    if (OB_FAIL(anchor_exprs.push_back(GraphParseNode{
            builder.column(seed, source_pattern.element_->key_columns_[i])}))) {
    } else if (OB_FAIL(cte_column_names.push_back(GraphParseNode{name}))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < target_pattern.element_->key_count_; ++i) {
    ParseNode *name = builder.internal_identifier(CURRENT_KEY_PREFIX, 0, i);
    if (OB_FAIL(anchor_exprs.push_back(GraphParseNode{
            builder.column(seed, source_pattern.element_->key_columns_[i])}))) {
    } else if (OB_FAIL(cte_column_names.push_back(GraphParseNode{name}))) {
    }
  }
  if (OB_SUCC(ret) && need_edge_identity) {
    ObSEArray<GraphParseNode, 1> no_arguments;
    if (OB_FAIL(anchor_exprs.push_back(GraphParseNode{
            builder.function("json_array", no_arguments)}))) {
    } else if (OB_FAIL(cte_column_names.push_back(GraphParseNode{
                   builder.identifier(EDGE_IDENTITY_NAME)}))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_properties.count(); ++i) {
    ObSEArray<GraphParseNode, 1> no_arguments;
    ParseNode *name = builder.internal_identifier(EDGE_PROPERTY_PREFIX, 0, i);
    if (OB_FAIL(anchor_exprs.push_back(GraphParseNode{
            builder.function("json_array", no_arguments)}))) {
    } else if (OB_FAIL(cte_column_names.push_back(GraphParseNode{name}))) {
    }
  }

  ParseNode *anchor = nullptr;
  if (OB_SUCC(ret)) {
    ParseNode *anchor_from = builder.make(T_FROM_LIST, 1);
    ParseNode *anchor_projects = nullptr;
    ParseNode *anchor_where = nullptr;
    ParseNode *condition = nullptr;
    if (anchor_from == nullptr) {
      ret = builder.error();
    } else {
      anchor_from->children_[0] = builder.relation(seed);
    }
    if (OB_SUCC(ret) && source_filter != nullptr
        && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, source_filter,
                                        *source_pattern.variable_, &seed,
                                        &edge_pattern, &target_pattern, true,
                                        condition))) {
    } else if (OB_SUCC(ret)) {
      builder.append_condition(condition, anchor_where);
    }
    if (OB_SUCC(ret) && OB_FAIL(make_project_list(builder, anchor_exprs, anchor_projects))) {
    } else if (OB_SUCC(ret) && OB_ISNULL(anchor = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX))) {
      ret = builder.error();
    } else if (OB_SUCC(ret)) {
      builder.force_serial(anchor);
      anchor->children_[PARSE_SELECT_FROM] = anchor_from;
      anchor->children_[PARSE_SELECT_SELECT] = anchor_projects;
      if (anchor_where != nullptr) {
        ParseNode *clause = builder.make(T_WHERE_CLAUSE, 2);
        if (clause == nullptr) {
          ret = builder.error();
        } else {
          clause->children_[0] = anchor_where;
          anchor->children_[PARSE_SELECT_WHERE] = clause;
        }
      }
    }
  }

  ParseNode *recursive = nullptr;
  if (OB_SUCC(ret)) {
    ParseNode *recursive_from = builder.make(T_FROM_LIST, 3);
    ParseNode *recursive_projects = nullptr;
    ParseNode *recursive_where = nullptr;
    if (recursive_from == nullptr) {
      ret = builder.error();
    } else {
      recursive_from->children_[0] = builder.relation(CTE_NAME, frontier_alias);
      recursive_from->children_[1] = builder.relation(edge, adjacency_index);
      recursive_from->children_[2] = builder.relation(step_vertex);
    }
    if (OB_SUCC(ret)) {
      ParseNode *depth = builder.column(frontier_alias, DEPTH_NAME);
      if (OB_FAIL(recursive_exprs.push_back(GraphParseNode{
              builder.binary(T_OP_ADD, depth, builder.integer(1))}))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < source_pattern.element_->key_count_; ++i) {
      if (OB_FAIL(recursive_exprs.push_back(GraphParseNode{
              make_internal_column(builder, frontier_alias, SOURCE_KEY_PREFIX, i)}))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < target_pattern.element_->key_count_; ++i) {
      if (OB_FAIL(recursive_exprs.push_back(GraphParseNode{
              builder.column(step_vertex, target_pattern.element_->key_columns_[i])}))) {
      }
    }
    if (OB_SUCC(ret) && need_edge_identity) {
      ParseNode *edge_identity = nullptr;
      ObSEArray<GraphParseNode, 3> arguments;
      if (OB_FAIL(make_edge_identity(builder, graph, database_name,
                                     edge_pattern, edge, edge_identity))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                     builder.column(frontier_alias, EDGE_IDENTITY_NAME)}))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                     builder.string(ObString::make_string("$"))}))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{edge_identity}))) {
      } else if (OB_FAIL(recursive_exprs.push_back(GraphParseNode{
                     builder.function("json_array_append", arguments)}))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < edge_properties.count(); ++i) {
      ObSEArray<GraphParseNode, 3> arguments;
      if (OB_FAIL(arguments.push_back(GraphParseNode{
              make_internal_column(builder, frontier_alias, EDGE_PROPERTY_PREFIX, i)}))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                     builder.string(ObString::make_string("$"))}))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                     builder.column(edge, edge_properties.at(i)->column_id_)}))) {
      } else if (OB_FAIL(recursive_exprs.push_back(GraphParseNode{
                     builder.function("json_array_append", arguments)}))) {
      }
    }
    if (OB_SUCC(ret)) {
      builder.append_condition(builder.binary(T_OP_LT,
          builder.column(frontier_alias, DEPTH_NAME), builder.integer(path_desc.upper_bound_)),
          recursive_where);
      ParseNode *condition = nullptr;
      if (edge_filter != nullptr
          && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, edge_filter,
                                          *edge_pattern.variable_, &edge,
                                          &source_pattern, &target_pattern, false,
                                          condition))) {
      } else {
        builder.append_condition(condition, recursive_where);
      }
    }
    if (OB_SUCC(ret)) {
      const bool reverse = path_desc.direction_ == GraphPathDirection::IN;
      const uint64_t expected_current = reverse ? edge.element_->destination_id_
                                                : edge.element_->source_id_;
      const uint64_t expected_next = reverse ? edge.element_->source_id_
                                             : edge.element_->destination_id_;
      if (source_pattern.element_->id_ != expected_current
          || target_pattern.element_->id_ != expected_next) {
        builder.append_condition(builder.false_value(), recursive_where);
      } else {
        const uint64_t *current_columns = reverse ? edge.element_->destination_columns_
                                                  : edge.element_->source_columns_;
        const uint64_t *next_columns = reverse ? edge.element_->source_columns_
                                               : edge.element_->destination_columns_;
        for (int64_t i = 0; i < source_pattern.element_->key_count_; ++i) {
          builder.append_condition(builder.binary(T_OP_EQ,
              builder.column(edge, current_columns[i]),
              make_internal_column(builder, frontier_alias, CURRENT_KEY_PREFIX, i)),
              recursive_where);
        }
        for (int64_t i = 0; i < target_pattern.element_->key_count_; ++i) {
          builder.append_condition(builder.binary(T_OP_EQ,
              builder.column(edge, next_columns[i]),
              builder.column(step_vertex, target_pattern.element_->key_columns_[i])),
              recursive_where);
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(make_project_list(builder, recursive_exprs,
                                                  recursive_projects))) {
    } else if (OB_SUCC(ret)
               && OB_ISNULL(recursive = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX))) {
      ret = builder.error();
    } else if (OB_SUCC(ret)) {
      builder.force_serial(recursive);
      recursive->children_[PARSE_SELECT_FROM] = recursive_from;
      recursive->children_[PARSE_SELECT_SELECT] = recursive_projects;
      if (recursive_where != nullptr) {
        ParseNode *clause = builder.make(T_WHERE_CLAUSE, 2);
        if (clause == nullptr) {
          ret = builder.error();
        } else {
          clause->children_[0] = recursive_where;
          recursive->children_[PARSE_SELECT_WHERE] = clause;
        }
      }
    }
  }

  ParseNode *with_list = nullptr;
  if (OB_SUCC(ret)) {
    ParseNode *cte_query = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
    ParseNode *set = builder.make(T_GRAPH_FEEDBACK_LOOP, 2);
    ParseNode *column_list = builder.make(T_COLUMN_LIST, cte_column_names.count());
    ParseNode *with_as = builder.make(T_WITH_CLAUSE_AS, 3);
    with_list = builder.make(T_WITH_CLAUSE_LIST, 1);
    if (cte_query == nullptr || set == nullptr || column_list == nullptr
        || with_as == nullptr || with_list == nullptr) {
      ret = builder.error();
    } else {
      set->int16_values_[0] = static_cast<int16_t>(path_desc.lower_bound_);
      set->int16_values_[1] = static_cast<int16_t>(path_desc.upper_bound_);
      set->int16_values_[2] = static_cast<int16_t>(
          path_desc.direction_ == GraphPathDirection::IN ? 1 : 0);
      set->children_[0] = anchor;
      set->children_[1] = recursive;
      builder.force_serial(cte_query);
      cte_query->children_[PARSE_SELECT_SET] = set;
      for (int64_t i = 0; i < cte_column_names.count(); ++i) {
        column_list->children_[i] = cte_column_names.at(i).node_;
      }
      with_as->children_[0] = builder.identifier(CTE_NAME);
      with_as->children_[1] = column_list;
      with_as->children_[2] = cte_query;
      with_list->children_[0] = with_as;
      with_list->value_ = 1;
    }
  }

  if (OB_SUCC(ret)) {
    ParseNode *from = builder.make(T_FROM_LIST, 3);
    ParseNode *where = nullptr;
    if (from == nullptr) {
      ret = builder.error();
    } else {
      from->children_[0] = builder.relation(CTE_NAME, path_alias);
      from->children_[1] = builder.relation(result_source);
      from->children_[2] = builder.relation(result_target);
    }
    if (OB_SUCC(ret)) {
      builder.append_condition(builder.binary(T_OP_GE,
          builder.column(path_alias, DEPTH_NAME), builder.integer(path_desc.lower_bound_)), where);
      for (int64_t i = 0; i < source_pattern.element_->key_count_; ++i) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(result_source, source_pattern.element_->key_columns_[i]),
            make_internal_column(builder, path_alias, SOURCE_KEY_PREFIX, i)), where);
      }
      for (int64_t i = 0; i < target_pattern.element_->key_count_; ++i) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(result_target, target_pattern.element_->key_columns_[i]),
            make_internal_column(builder, path_alias, CURRENT_KEY_PREFIX, i)), where);
      }
      ParseNode *condition = nullptr;
      if (target_filter != nullptr
          && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, target_filter,
                                          *target_pattern.variable_, &result_target,
                                          &source_pattern, &edge_pattern, false,
                                          condition))) {
      } else {
        builder.append_condition(condition, where);
      }
    }

    ObSEArray<GraphRuntimePropertyArray, 16> runtime_properties;
    GraphRuntimeGroup runtime_group;
    ParseNode *depth = builder.column(path_alias, DEPTH_NAME);
    runtime_group.count_ = depth;
    ObSEArray<GraphParseNode, 3> if_identity_arguments;
    if (OB_SUCC(ret) && need_edge_identity) {
      if (OB_FAIL(if_identity_arguments.push_back(GraphParseNode{
              builder.binary(T_OP_EQ, builder.column(path_alias, DEPTH_NAME),
                             builder.integer(0))}))) {
      } else if (OB_FAIL(if_identity_arguments.push_back(GraphParseNode{builder.null_value()}))) {
      } else if (OB_FAIL(if_identity_arguments.push_back(GraphParseNode{
                     builder.column(path_alias, EDGE_IDENTITY_NAME)}))) {
      } else {
        runtime_group.identity_array_ = builder.function("if", if_identity_arguments);
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < edge_properties.count(); ++i) {
      ObSEArray<GraphParseNode, 3> arguments;
      GraphRuntimePropertyArray runtime_property;
      runtime_property.column_id_ = edge_properties.at(i)->column_id_;
      if (OB_FAIL(arguments.push_back(GraphParseNode{
              builder.binary(T_OP_EQ, builder.column(path_alias, DEPTH_NAME),
                             builder.integer(0))}))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{builder.null_value()}))) {
      } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                     make_internal_column(builder, path_alias, EDGE_PROPERTY_PREFIX, i)}))) {
      } else if (OB_ISNULL(runtime_property.array_ = builder.function("if", arguments))) {
        ret = builder.error();
      } else if (OB_FAIL(runtime_properties.push_back(runtime_property))) {
      }
    }
    runtime_group.property_arrays_ = &runtime_properties;

    ObSEArray<GraphParseNode, 8> match_ordering;
    if (need_match_number) {
      for (int64_t i = 0; OB_SUCC(ret) && i < source_pattern.element_->key_count_; ++i) {
        ret = match_ordering.push_back(GraphParseNode{
            make_internal_column(builder, path_alias, SOURCE_KEY_PREFIX, i)});
      }
      if (OB_SUCC(ret)) {
        ret = match_ordering.push_back(GraphParseNode{builder.column(path_alias, DEPTH_NAME)});
      }
      if (OB_SUCC(ret)) {
        ret = match_ordering.push_back(GraphParseNode{
            builder.column(path_alias, EDGE_IDENTITY_NAME)});
      }
    }
    ParseNode *match_number = need_match_number && OB_SUCC(ret)
        ? make_row_number(builder, match_ordering) : nullptr;
    ObSEArray<GraphExprBinding, 3> expr_bindings;
    if (OB_SUCC(ret)
        && OB_FAIL(add_expr_binding(expr_bindings, *source_pattern.variable_,
                                    GraphExprBindingKind::SCALAR,
                                    &result_source, nullptr, -1, false))) {
    } else if (OB_SUCC(ret)
               && OB_FAIL(add_expr_binding(expr_bindings, *edge_pattern.variable_,
                                           GraphExprBindingKind::GROUP,
                                           &edge_pattern, nullptr, -1, true,
                                           &runtime_group))) {
    } else if (OB_SUCC(ret)
               && OB_FAIL(add_expr_binding(expr_bindings, *target_pattern.variable_,
                                           GraphExprBindingKind::SCALAR,
                                           &result_target, nullptr, -1, false))) {
    }
    ParseNode *rewritten_projects = nullptr;
    if (OB_SUCC(ret)
        && OB_FAIL(rewrite_graph_projects(builder, graph, database_name, projects,
                                          expr_bindings, match_number,
                                          rewritten_projects))) {
    } else if (OB_SUCC(ret)
               && OB_ISNULL(select = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX))) {
      ret = builder.error();
    } else if (OB_SUCC(ret)) {
      builder.force_serial(select);
      select->children_[PARSE_SELECT_WITH] = with_list;
      select->children_[PARSE_SELECT_FROM] = from;
      select->children_[PARSE_SELECT_SELECT] = rewritten_projects;
      if (where != nullptr) {
        ParseNode *clause = builder.make(T_WHERE_CLAUSE, 2);
        if (clause == nullptr) {
          ret = builder.error();
        } else {
          clause->children_[0] = where;
          select->children_[PARSE_SELECT_WHERE] = clause;
        }
      }
    }
  }
  if (OB_SUCC(ret) && builder.error() != OB_SUCCESS) {
    ret = builder.error();
  }
  return ret;
}

int build_graph_path_branch(GraphRelationBuilder &builder,
                            const GraphSchema &graph,
                            const ObString &database_name,
                            const GraphPathDesc &path_desc,
                            const ObString *adjacency_index,
                            const GraphBinding &source_pattern,
                            const GraphBinding &edge_pattern,
                            const GraphBinding &target_pattern,
                            const ParseNode *source_filter,
                            const ParseNode *edge_filter,
                            const ParseNode *target_filter,
                            const ParseNode *shape,
                            const ParseNode &projects,
                            int64_t hop,
                            int64_t step,
                            ParseNode *&select)
{
  int ret = OB_SUCCESS;
  ObSEArray<GraphBinding, GRAPH_WALK_MAX_HOPS + 1> vertices;
  ObSEArray<GraphBinding, GRAPH_WALK_MAX_HOPS> edges;
  const GraphElement *traversal_target = target_pattern.element_;
  const ObTableSchema *traversal_target_table = target_pattern.table_;
  const char *vertex_prefix = path_desc.direction_ == GraphPathDirection::OUT
      ? "__g_walk_out_v_" : "__g_walk_in_v_";
  const char *edge_prefix = path_desc.direction_ == GraphPathDirection::OUT
      ? "__g_walk_out_e_" : "__g_walk_in_e_";
  for (int64_t i = 0; OB_SUCC(ret) && i <= hop; ++i) {
    GraphBinding binding = {i == 0 ? source_pattern.element_ : traversal_target,
                            i == 0 ? source_pattern.table_ : traversal_target_table,
                            builder.internal_identifier(vertex_prefix, hop, i)};
    if (binding.element_ == nullptr || binding.table_ == nullptr || binding.variable_ == nullptr) {
      ret = builder.error() == OB_SUCCESS ? OB_ERR_UNEXPECTED : builder.error();
    } else if (OB_FAIL(vertices.push_back(binding))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < hop; ++i) {
    GraphBinding binding = {edge_pattern.element_, edge_pattern.table_,
                            builder.internal_identifier(edge_prefix, hop, i)};
    if (binding.variable_ == nullptr) {
      ret = builder.error() == OB_SUCCESS ? OB_ERR_UNEXPECTED : builder.error();
    } else if (OB_FAIL(edges.push_back(binding))) {
    }
  }
  const bool needs_separate_terminal = hop == 0
      && source_pattern.element_->id_ != target_pattern.element_->id_;
  GraphBinding separate_terminal = {target_pattern.element_, target_pattern.table_, nullptr};
  const GraphBinding *terminal = nullptr;
  if (OB_SUCC(ret)) {
    if (needs_separate_terminal) {
      separate_terminal.variable_ = builder.internal_identifier(
          path_desc.direction_ == GraphPathDirection::OUT
              ? "__g_walk_out_terminal_" : "__g_walk_in_terminal_",
          hop, 0);
      terminal = &separate_terminal;
    } else {
      terminal = &vertices.at(hop);
    }
    if (terminal->element_ == nullptr || terminal->table_ == nullptr
        || terminal->variable_ == nullptr) {
      ret = builder.error() == OB_SUCCESS ? OB_ERR_UNEXPECTED : builder.error();
    }
  }
  ParseNode *from = nullptr;
  ParseNode *where = nullptr;
  if (OB_SUCC(ret)) {
    from = builder.make(T_FROM_LIST,
                        vertices.count() + edges.count() + (needs_separate_terminal ? 1 : 0));
    if (from == nullptr) {
      ret = builder.error();
    } else {
      int64_t from_index = 0;
      for (int64_t i = 0; i < vertices.count(); ++i) {
        from->children_[from_index++] = builder.relation(vertices.at(i));
      }
      for (int64_t i = 0; i < edges.count(); ++i) {
        from->children_[from_index++] = builder.relation(edges.at(i), adjacency_index);
      }
      if (needs_separate_terminal) {
        from->children_[from_index++] = builder.relation(*terminal);
      }
    }
  }
  if (OB_SUCC(ret)) {
    ParseNode *condition = nullptr;
    if (source_filter != nullptr
        && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, source_filter,
                                        *source_pattern.variable_, &vertices.at(0),
                                        &edge_pattern, &target_pattern, true,
                                        condition))) {
    } else {
      builder.append_condition(condition, where);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < edges.count(); ++i) {
      condition = nullptr;
      if (edge_filter != nullptr
          && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, edge_filter,
                                          *edge_pattern.variable_, &edges.at(i),
                                          &source_pattern, &target_pattern, false,
                                          condition))) {
      } else {
        builder.append_condition(condition, where);
      }
    }
    condition = nullptr;
      if (OB_SUCC(ret) && target_filter != nullptr
        && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, target_filter,
                                        *target_pattern.variable_, terminal,
                                        &source_pattern, &edge_pattern, false,
                                        condition))) {
    } else if (OB_SUCC(ret)) {
      builder.append_condition(condition, where);
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edges.count(); ++i) {
    const GraphBinding &current = vertices.at(i);
    const GraphBinding &edge = edges.at(i);
    const GraphBinding &next = vertices.at(i + 1);
    const bool reverse = path_desc.direction_ == GraphPathDirection::IN;
    const uint64_t expected_current = reverse ? edge.element_->destination_id_ : edge.element_->source_id_;
    const uint64_t expected_next = reverse ? edge.element_->source_id_ : edge.element_->destination_id_;
    if (current.element_->id_ != expected_current || next.element_->id_ != expected_next) {
      builder.append_condition(builder.false_value(), where);
    } else {
      const uint64_t *current_columns = reverse ? edge.element_->destination_columns_
                                                : edge.element_->source_columns_;
      const uint64_t *next_columns = reverse ? edge.element_->source_columns_
                                             : edge.element_->destination_columns_;
      for (int64_t k = 0; k < current.element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(edge, current_columns[k]),
            builder.column(current, current.element_->key_columns_[k])), where);
      }
      for (int64_t k = 0; k < next.element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(edge, next_columns[k]),
            builder.column(next, next.element_->key_columns_[k])), where);
      }
    }
  }
  if (OB_SUCC(ret) && needs_separate_terminal) {
    builder.append_condition(builder.false_value(), where);
  }
  ObSEArray<GraphExprBinding, 8> expr_bindings;
  ParseNode *match_number = nullptr;
  if (OB_SUCC(ret)) {
    match_number = make_match_number(builder, vertices, edges, hop,
                                     path_desc.lower_bound_, path_desc.upper_bound_);
    if (path_desc.row_shape_ == GraphPathRowShape::PER_MATCH) {
      if (OB_FAIL(add_expr_binding(expr_bindings, *source_pattern.variable_,
                                   GraphExprBindingKind::SCALAR, &vertices.at(0), nullptr,
                                   -1, false))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *edge_pattern.variable_,
                                          GraphExprBindingKind::GROUP, &edge_pattern, &edges,
                                          -1, true))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *target_pattern.variable_,
                                          GraphExprBindingKind::SCALAR, terminal, nullptr,
                                          -1, false))) {
      }
    } else if (shape == nullptr || shape->num_child_ != 3 || shape->children_ == nullptr
               || shape->children_[0] == nullptr || shape->children_[1] == nullptr
               || shape->children_[2] == nullptr || step < 0 || step > hop) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const bool zero_hop = hop == 0;
      const GraphExprBindingKind edge_kind = zero_hop ? GraphExprBindingKind::NULL_SCALAR
                                                       : GraphExprBindingKind::SCALAR;
      const GraphExprBindingKind target_kind = zero_hop ? GraphExprBindingKind::NULL_SCALAR
                                                         : GraphExprBindingKind::SCALAR;
      if (OB_FAIL(add_expr_binding(expr_bindings, *shape->children_[0],
                                   GraphExprBindingKind::SCALAR,
                                   &vertices.at(zero_hop ? 0 : step - 1), nullptr,
                                   zero_hop ? 1 : 2 * step - 1, false))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *shape->children_[1], edge_kind,
                                          zero_hop ? nullptr : &edges.at(step - 1), nullptr,
                                          zero_hop ? -1 : 2 * step, true))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *shape->children_[2], target_kind,
                                          zero_hop ? nullptr : &vertices.at(step), nullptr,
                                          zero_hop ? -1 : 2 * step + 1, false))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *source_pattern.variable_,
                                          GraphExprBindingKind::SCALAR, &vertices.at(0), nullptr,
                                          -1, false))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *target_pattern.variable_,
                                          GraphExprBindingKind::SCALAR, terminal, nullptr,
                                          -1, false))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *edge_pattern.variable_,
                                          GraphExprBindingKind::GROUP, &edge_pattern, &edges,
                                          -1, true))) {
      }
    }
  }
  ParseNode *rewritten_projects = nullptr;
  if (OB_SUCC(ret) && OB_FAIL(rewrite_graph_projects(builder, graph, database_name,
                                                      projects, expr_bindings,
                                                      match_number, rewritten_projects))) {
  } else if (OB_SUCC(ret)) {
    select = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
    if (select == nullptr) {
      ret = builder.error();
    } else {
      builder.force_serial(select);
      select->children_[PARSE_SELECT_FROM] = from;
      select->children_[PARSE_SELECT_SELECT] = rewritten_projects;
      if (where != nullptr) {
        ParseNode *clause = builder.make(T_WHERE_CLAUSE, 2);
        if (clause == nullptr) {
          ret = builder.error();
        } else {
          clause->children_[0] = where;
          select->children_[PARSE_SELECT_WHERE] = clause;
        }
      }
    }
  }
  if (OB_SUCC(ret) && builder.error() != OB_SUCCESS) {
    ret = builder.error();
  }
  return ret;
}
} // namespace

int ObDMLResolver::resolve_graph_table(const ParseNode &node, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  static const int64_t GRAPH_TABLE_GRAPH = 0;
  static const int64_t GRAPH_TABLE_PATH_MODE = 1;
  static const int64_t GRAPH_TABLE_PATTERN = 2;
  static const int64_t GRAPH_TABLE_SHAPE = 3;
  static const int64_t GRAPH_TABLE_PROJECTS = 4;
  static const int64_t GRAPH_TABLE_ALIAS = 5;
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
  } else if (node.num_child_ != 6 || node.children_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (node.children_[GRAPH_TABLE_PATH_MODE] != nullptr) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "TRAIL, SIMPLE and ACYCLIC graph path modes");
  } else if (OB_FAIL(resolve_table_relation_node(node.children_[GRAPH_TABLE_GRAPH], name, database_name))) {
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
    const ParseNode &chain = *node.children_[GRAPH_TABLE_PATTERN];
    const ParseNode *shape = node.children_[GRAPH_TABLE_SHAPE];
    const ParseNode &projects = *node.children_[GRAPH_TABLE_PROJECTS];
    int64_t quantified_segments = 0;
    const ParseNode *quantifier = nullptr;
    if (chain.num_child_ < 1 || chain.num_child_ > 33 || (chain.num_child_ % 2) == 0) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph chains longer than 16 edges");
    }
    for (int64_t i = 1; OB_SUCC(ret) && i < chain.num_child_; i += 2) {
      const ParseNode *candidate = chain.children_[i]->num_child_ >= 4
          ? chain.children_[i]->children_[3] : nullptr;
      if (candidate != nullptr) {
        ++quantified_segments;
        quantifier = candidate;
      }
    }
    if (OB_SUCC(ret) && quantified_segments > 1) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "multiple or nested quantified graph path segments");
    } else if (OB_SUCC(ret) && quantified_segments == 1 && chain.num_child_ != 3) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "a quantified segment combined with another graph segment");
    } else if (OB_SUCC(ret) && shape != nullptr && shape->value_ == 2) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "ONE ROW PER VERTEX graph results");
    } else if (OB_SUCC(ret) && quantified_segments == 0
               && shape != nullptr && shape->value_ != 0) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "path row shapes without a quantified graph segment");
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < chain.num_child_; ++i) {
      const ParseNode &pattern = *chain.children_[i];
      GraphBinding binding = {nullptr, nullptr, pattern.children_[0]};
      if ((i % 2) == 1 && pattern.value_ != 0 && pattern.value_ != 1) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "undirected graph path segments");
      } else {
        binding.element_ = graph->get_element(graph_node_name(*pattern.children_[1]));
      }
      if (OB_FAIL(ret)) {
      } else if (binding.element_ == nullptr
                 || binding.element_->is_vertex() != ((i % 2) == 0)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "unknown graph labels or labels used for the wrong element kind");
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < bindings.count(); ++j) {
        if (ObSchemaNameComparator().compare(
                graph_node_name(*binding.variable_),
                graph_node_name(*bindings.at(j).variable_)) == 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "redeclaring a graph variable or cyclic graph patterns");
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_checker_->get_schema_guard()->get_table_schema(
                     binding.element_->table_id_, binding.table_))) {
      } else if (binding.table_ == nullptr) {
        ret = OB_TABLE_NOT_EXIST;
      } else if (OB_FAIL(bindings.push_back(binding))) {
      }
    }
    if (OB_SUCC(ret) && quantified_segments == 0) {
      for (int64_t i = 0; OB_SUCC(ret) && i < chain.num_child_; ++i) {
        const ParseNode *filter = chain.children_[i]->children_[2];
        if (filter != nullptr) { ret = check_graph_expression(*filter, *graph, bindings); }
      }
      if (OB_SUCC(ret)) { ret = check_graph_expression(projects, *graph, bindings); }
    }
    if (OB_SUCC(ret) && quantified_segments == 0) {
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
        select->children_[PARSE_SELECT_SELECT] = node.children_[GRAPH_TABLE_PROJECTS];
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
        ret = resolve_generate_table(*select, node.children_[GRAPH_TABLE_ALIAS], table_item);
        guard.set_session_id(saved_session_id);
        if (OB_SUCC(ret)) {
          ObSchemaObjVersion dependency(graph->get_graph_id(), graph->get_schema_version(), DEPENDENCY_PROPERTY_GRAPH);
          dependency.is_db_explicit_ = node.children_[GRAPH_TABLE_GRAPH]->children_[0] != nullptr;
          ret = get_stmt()->add_global_dependency_table(dependency);
        }
      }
    } else if (OB_SUCC(ret)) {
      GraphPathDesc path_desc;
      if (quantifier == nullptr || quantifier->type_ != T_GRAPH_QUANTIFIER
          || quantifier->num_child_ != 2) {
        ret = OB_ERR_UNEXPECTED;
      } else if (quantifier->value_ == 2 || quantifier->value_ == 3
                 || (quantifier->value_ == 1 && quantifier->children_[1] == nullptr)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "unbounded graph path quantifiers (*, + or {n,})");
      } else {
        path_desc.graph_id_ = graph->get_graph_id();
        path_desc.graph_version_ = graph->get_schema_version();
        path_desc.source_element_id_ = bindings.at(0).element_->id_;
        path_desc.edge_element_id_ = bindings.at(1).element_->id_;
        path_desc.target_element_id_ = bindings.at(2).element_->id_;
        path_desc.lower_bound_ = quantifier->children_[0] == nullptr
            ? 0 : quantifier->children_[0]->value_;
        path_desc.upper_bound_ = quantifier->value_ == 0
            ? path_desc.lower_bound_ : quantifier->children_[1]->value_;
        path_desc.direction_ = chain.children_[1]->value_ == 1
            ? GraphPathDirection::IN : GraphPathDirection::OUT;
        path_desc.row_shape_ = shape != nullptr && shape->value_ == 1
            ? GraphPathRowShape::PER_STEP : GraphPathRowShape::PER_MATCH;
        path_desc.need_path_ = path_desc.row_shape_ == GraphPathRowShape::PER_STEP
            || contains_item_type(projects, T_FUN_JSON_ARRAYAGG);
        if (path_desc.lower_bound_ < 0 || path_desc.upper_bound_ < 0
            || path_desc.lower_bound_ > path_desc.upper_bound_) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph path bounds with lower greater than upper");
        } else if (path_desc.upper_bound_ > GRAPH_WALK_MAX_HOPS) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph path upper bounds greater than 16");
        } else if (!path_desc.is_valid()) {
          ret = OB_ERR_UNEXPECTED;
        }
      }
      if (OB_SUCC(ret) && shape != nullptr && shape->value_ == 1) {
        if (shape->num_child_ != 3 || shape->children_[0] == nullptr
            || shape->children_[1] == nullptr || shape->children_[2] == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < shape->num_child_; ++i) {
            for (int64_t j = i + 1; OB_SUCC(ret) && j < shape->num_child_; ++j) {
              if (graph_name_equal(graph_node_name(*shape->children_[i]),
                                   graph_node_name(*shape->children_[j]))) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "duplicate ONE ROW PER STEP iteration variables");
              }
            }
            for (int64_t j = 0; OB_SUCC(ret) && j < bindings.count(); ++j) {
              if (graph_name_equal(graph_node_name(*shape->children_[i]),
                                   graph_node_name(*bindings.at(j).variable_))) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                               "an iterator variable redeclared from the graph pattern");
              }
            }
          }
        }
      }
      GraphRelationBuilder builder(*allocator_, database_name);
      ParseNode *select = nullptr;
      ObString adjacency_index;
      if (OB_SUCC(ret) && OB_FAIL(find_adjacency_index(
              bindings.at(1), path_desc.direction_,
              *schema_checker_->get_schema_guard(), adjacency_index))) {
        LOG_WARN("failed to select graph adjacency index", K(ret), K(path_desc));
      }
      const ObString *adjacency_index_ptr = adjacency_index.empty()
          ? nullptr : &adjacency_index;
      const bool use_feedback_loop = path_desc.row_shape_ == GraphPathRowShape::PER_MATCH
          && bindings.at(0).element_->id_ == bindings.at(2).element_->id_;
      if (OB_SUCC(ret) && use_feedback_loop) {
        ret = build_recursive_graph_match(builder, *graph, database_name, path_desc,
                                           adjacency_index_ptr,
                                           bindings.at(0), bindings.at(1), bindings.at(2),
                                           chain.children_[0]->children_[2],
                                           chain.children_[1]->children_[2],
                                           chain.children_[2]->children_[2],
                                           projects, select);
      } else if (OB_SUCC(ret)) {
        ObSEArray<GraphParseNode, 32> branches;
        for (int64_t hop = path_desc.lower_bound_;
             OB_SUCC(ret) && hop <= path_desc.upper_bound_;
             ++hop) {
          const int64_t first_step = path_desc.row_shape_ == GraphPathRowShape::PER_STEP
              ? (hop == 0 ? 0 : 1) : -1;
          const int64_t last_step = path_desc.row_shape_ == GraphPathRowShape::PER_STEP
              ? (hop == 0 ? 0 : hop) : -1;
          for (int64_t step = first_step; OB_SUCC(ret) && step <= last_step; ++step) {
            ParseNode *branch = nullptr;
            if (OB_FAIL(build_graph_path_branch(builder, *graph, database_name, path_desc,
                                                 adjacency_index_ptr,
                                                 bindings.at(0), bindings.at(1), bindings.at(2),
                                                 chain.children_[0]->children_[2],
                                                 chain.children_[1]->children_[2],
                                                 chain.children_[2]->children_[2],
                                                 shape, projects, hop, step, branch))) {
            } else if (OB_FAIL(branches.push_back(GraphParseNode{branch}))) {
            }
          }
        }
        if (OB_SUCC(ret) && branches.count() == 1) {
          select = branches.at(0).node_;
        } else if (OB_SUCC(ret)) {
          select = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
          ParseNode *set = builder.make(T_SET_UNION_ALL, branches.count());
          if (select == nullptr || set == nullptr) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            builder.force_serial(select);
            set->value_ = branches.count();
            for (int64_t i = 0; i < branches.count(); ++i) {
              set->children_[i] = branches.at(i).node_;
            }
            select->children_[PARSE_SELECT_SET] = set;
          }
        }
      }
      if (OB_SUCC(ret) && builder.error() != OB_SUCCESS) {
        ret = builder.error();
      }
      if (OB_SUCC(ret)) {
        ObSchemaGetterGuard &guard = *schema_checker_->get_schema_guard();
        const int64_t saved_session_id = guard.get_session_id();
        guard.set_session_id(0);
        ret = resolve_generate_table(*select, node.children_[GRAPH_TABLE_ALIAS], table_item);
        guard.set_session_id(saved_session_id);
        if (OB_SUCC(ret)) {
          ObSchemaObjVersion dependency(graph->get_graph_id(), graph->get_schema_version(),
                                        DEPENDENCY_PROPERTY_GRAPH);
          dependency.is_db_explicit_ = node.children_[GRAPH_TABLE_GRAPH]->children_[0] != nullptr;
          ret = get_stmt()->add_global_dependency_table(dependency);
        }
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
