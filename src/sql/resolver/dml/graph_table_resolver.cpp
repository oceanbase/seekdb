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
  const GraphElement *element_ = nullptr;
  const ObTableSchema *table_ = nullptr;
  ParseNode *variable_ = nullptr;
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
    // Generated aliases are ordinary SQL identifiers. Use the full supported
    // column-name capacity instead of coupling future prefixes or path bounds
    // to an unrelated fixed buffer size.
    char buffer[OB_MAX_COLUMN_NAME_BUF_LENGTH] = {'\0'};
    if (error_ == OB_SUCCESS) {
      const int length = snprintf(buffer, sizeof(buffer), "%s%ld_%ld", prefix, group, index);
      if (length <= 0 || length >= static_cast<int>(sizeof(buffer))) {
        error_ = OB_ERR_UNEXPECTED;
      } else {
        node = identifier(ObString(length, buffer));
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
  ParseNode *choose(ParseNode *condition, ParseNode *yes, ParseNode *no)
  {
    ObArray<GraphParseNode> args;
    int ret = OB_SUCCESS;
    if (OB_FAIL(args.push_back(GraphParseNode{condition}))
        || OB_FAIL(args.push_back(GraphParseNode{yes}))
        || OB_FAIL(args.push_back(GraphParseNode{no}))) {
      set_error(ret);
    }
    return function("if", args);
  }
  ParseNode *join(ParseNode *left, ParseNode *right, ParseNode *condition,
                  bool outer = false)
  {
    ParseNode *node = make(T_JOINED_TABLE, 5);
    if (node != nullptr) {
      node->children_[0] = make(outer ? T_JOIN_LEFT : T_JOIN_INNER, 0);
      node->children_[1] = left;
      node->children_[2] = right;
      node->children_[3] = condition;
    }
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
// with every typed source/destination column in graph-mapping order.
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
      // access paths for the single-node graph feedback operator.
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
  const ParseNode *node_ = nullptr;
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

// A step iterator may visit different element mappings in a composed path.
// Its property/identity expression selects the binding for the current step;
// the selector is never used as an element identity.
struct GraphConditionalBinding
{
  GraphBinding binding_{};
  ParseNode *condition_ = nullptr;
  TO_STRING_KV(K_(binding), KP_(condition));
};

struct GraphExprBinding
{
  ObString name_{};
  GraphExprBindingKind kind_ = GraphExprBindingKind::SCALAR;
  const GraphBinding *binding_ = nullptr;
  const ObIArray<GraphBinding> *group_ = nullptr;
  const GraphRuntimeGroup *runtime_group_ = nullptr;
  const ObIArray<GraphConditionalBinding> *conditional_ = nullptr;
  int64_t element_number_ = -1;
  ParseNode *element_number_expr_ = nullptr;
  ParseNode *null_guard_ = nullptr;
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
                    bool allow_outer_reference,
                    bool allow_unqualified_outer_reference = false)
    : builder_(builder),
      graph_(graph),
      database_name_(database_name),
      bindings_(bindings),
      match_number_(match_number),
      allow_outer_reference_(allow_outer_reference),
      allow_unqualified_outer_reference_(allow_unqualified_outer_reference)
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
      if (allow_outer_reference_ && allow_unqualified_outer_reference_) {
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
      } else if (expr_binding->conditional_ != nullptr) {
        ret = rewrite_conditional(source, *expr_binding, false, result);
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

  int rewrite_conditional(const ParseNode &source, const GraphExprBinding &binding,
                          bool identity, ParseNode *&result)
  {
    int ret = OB_SUCCESS;
    result = builder_.null_value();
    bool found = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < binding.conditional_->count(); ++i) {
      const GraphConditionalBinding &alternative = binding.conditional_->at(i);
      if (!identity && find_graph_property(graph_, alternative.binding_.element_->id_,
              graph_node_name(*source.children_[2])) == nullptr) {
        // A property absent from one of the iterator's mappings is NULL for
        // that mapping. A name absent from every mapping is still an error.
        continue;
      }
      found = true;
      GraphExprBinding scalar = binding;
      scalar.conditional_ = nullptr;
      scalar.binding_ = &alternative.binding_;
      scalar.null_guard_ = nullptr;
      ObArray<GraphExprBinding> scalar_bindings;
      ParseNode *value = nullptr;
      if (OB_FAIL(scalar_bindings.push_back(scalar))) {
      } else {
        GraphExprRewriter rewriter(builder_, graph_, database_name_, scalar_bindings,
                                  nullptr, false);
        if (OB_FAIL(rewriter.rewrite(source, value))) {
        } else {
          result = builder_.choose(builder_.clone(*alternative.condition_), value, result);
        }
      }
    }
    if (OB_SUCC(ret) && !found) { ret = unsupported("an unknown graph property"); }
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
      ObSEArray<GraphParseNode, GRAPH_PATH_MAX_HOPS> values;
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
    } else if (expr_binding->conditional_ != nullptr) {
      ret = rewrite_conditional(source, *expr_binding, true, result);
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
          if (expr_binding->null_guard_ != nullptr) {
            result = builder_.choose(builder_.clone(*expr_binding->null_guard_),
                                     builder_.null_value(), result);
          }
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
    } else if (expr_binding->element_number_expr_ != nullptr) {
      result = builder_.clone(*expr_binding->element_number_expr_);
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
  bool allow_unqualified_outer_reference_;
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
                           const ObIArray<GraphParseNode> &ordering,
                           ParseNode *partition_key = nullptr,
                           bool dense_rank = false)
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
  ParseNode *row_number_function = builder.make(
      dense_rank ? T_WIN_FUN_DENSE_RANK : T_WIN_FUN_ROW_NUMBER, 0);
  ParseNode *row_number = builder.make(T_WINDOW_FUNCTION, 2);
  if (order_by != nullptr && window_clause != nullptr && new_window_clause != nullptr
      && row_number != nullptr) {
    order_by->children_[0] = sort_list;
    if (partition_key != nullptr) {
      ParseNode *partition = builder.make(T_EXPR_LIST, 1);
      if (partition != nullptr) {
        partition->children_[0] = partition_key;
        window_clause->children_[0] = partition;
      }
    }
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
                         bool allow_unqualified_outer,
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
      // Preserve references outside the graph-variable namespace for normal
      // LATERAL resolution. Each outer row may supply its own seed, edge
      // predicate or endpoint constraint. Other declared graph variables stay
      // forbidden here; they are not mistaken for outer correlation.
      // Edge/endpoint outer references must be qualified: an unqualified
      // name could otherwise bind to a hidden base-table column after lowering.
      // Keep the pre-existing unqualified seed-filter behavior unchanged.
      GraphExprRewriter rewriter(builder, graph, database_name, expr_bindings,
                                 nullptr, true, allow_unqualified_outer);
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

ParseNode *make_internal_column(GraphRelationBuilder &builder,
                                ParseNode *variable,
                                const char *prefix,
                                int64_t group,
                                int64_t index)
{
  ParseNode *name = builder.internal_identifier(prefix, group, index);
  return name == nullptr ? nullptr : builder.column(variable, graph_node_name(*name));
}

// Graph edge identity is (graph mapping, element mapping, complete typed key).
// The graph is fixed for one GRAPH_TABLE invocation, so different element
// mappings are always different identities. Edges from the same mapping are
// different when at least one non-null primary-key component differs.
int make_edge_identity_not_equal(GraphRelationBuilder &builder,
                                 const GraphBinding &left,
                                 const GraphBinding &right,
                                 ParseNode *&condition)
{
  int ret = OB_SUCCESS;
  condition = nullptr;
  if (left.element_ == nullptr || right.element_ == nullptr
      || left.table_ == nullptr || right.table_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (left.element_->id_ == right.element_->id_) {
    if (left.element_->key_count_ <= 0
        || left.element_->key_count_ != right.element_->key_count_) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < left.element_->key_count_; ++i) {
      ParseNode *key_differs = builder.binary(
          T_OP_NE,
          builder.column(left, left.element_->key_columns_[i]),
          builder.column(right, right.element_->key_columns_[i]));
      condition = condition == nullptr
          ? key_differs : builder.binary(T_OP_OR, condition, key_differs);
    }
  }
  if (OB_SUCC(ret) && builder.error() != OB_SUCCESS) {
    ret = builder.error();
  }
  return ret;
}

// TRAIL is a per-path constraint. It does not deduplicate different paths; it
// only rejects a row when two edge positions in that row bind the same typed
// graph edge identity.
int append_trail_edge_conditions(GraphRelationBuilder &builder,
                                 const ObIArray<GraphBinding> &edges,
                                 ParseNode *&where)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < edges.count(); ++i) {
    for (int64_t j = i + 1; OB_SUCC(ret) && j < edges.count(); ++j) {
      ParseNode *condition = nullptr;
      if (OB_FAIL(make_edge_identity_not_equal(builder, edges.at(i), edges.at(j),
                                               condition))) {
      } else {
        // A null condition means the two positions use different graph element
        // mappings and therefore cannot denote the same graph edge.
        builder.append_condition(condition, where);
      }
    }
  }
  return ret;
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

// Keep path qualification below row shaping. In particular, a volatile path
// predicate must not be reevaluated for each step. The ordinary view-merge
// rules retain non-deterministic derived queries. A derived table also supports
// correlated rescans, unlike a materialized CTE. Only needed columns cross it.
int qualify_paths_before_steps(GraphRelationBuilder &builder,
                               const ParseNode &base_from,
                               const ParseNode *base_where,
                               ParseNode *base_relation,
                               ParseNode *select)
{
  int ret = OB_SUCCESS;
  const ObString name = ObString::make_string("__g_step_matches");
  ParseNode *alias = builder.identifier(name);
  ParseNode *query = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
  ParseNode *where = base_where == nullptr ? nullptr : builder.make(T_WHERE_CLAUSE, 2);
  ObArray<GraphParseNode> pending;
  ObArray<GraphParseNode> columns;
  ObArray<GraphParseNode> names;
  if (query == nullptr || select == nullptr || base_relation == nullptr) {
    ret = builder.error() == OB_SUCCESS ? OB_ERR_UNEXPECTED : builder.error();
  } else {
    builder.force_serial(query);
    query->children_[PARSE_SELECT_FROM] = builder.clone(base_from);
    if (where != nullptr) {
      where->children_[0] = builder.clone(*base_where);
      query->children_[PARSE_SELECT_WHERE] = where;
    }
    // Do not lift references inside the traversal's WITH definition.
    if (OB_FAIL(pending.push_back(GraphParseNode{select->children_[PARSE_SELECT_FROM]}))) {
    } else if (OB_FAIL(pending.push_back(GraphParseNode{select->children_[PARSE_SELECT_SELECT]}))) {
    }
  }
  while (OB_SUCC(ret) && !pending.empty()) {
    ParseNode *node = pending.at(pending.count() - 1).node_;
    pending.pop_back();
    if (node == nullptr || node == base_relation) {
      continue;
    }
    bool local_column = false;
    if (node->type_ == T_COLUMN_REF && node->num_child_ == 3
        && node->children_[1] != nullptr && node->children_[2] != nullptr) {
      for (int64_t i = 0; !local_column && i < base_from.num_child_; ++i) {
        const ParseNode *relation = base_from.children_[i];
        local_column = relation->type_ == T_ALIAS && relation->num_child_ >= 2
            && graph_name_equal(graph_node_name(*relation->children_[1]),
                                graph_node_name(*node->children_[1]));
      }
    }
    if (local_column) {
      int64_t index = 0;
      for (; index < columns.count(); ++index) {
        const ParseNode *column = columns.at(index).node_;
        if (graph_name_equal(graph_node_name(*column->children_[1]),
                             graph_node_name(*node->children_[1]))
            && graph_name_equal(graph_node_name(*column->children_[2]),
                                graph_node_name(*node->children_[2]))) {
          break;
        }
      }
      if (index == columns.count()) {
        if (OB_FAIL(columns.push_back(GraphParseNode{builder.clone(*node)}))) {
        } else if (OB_FAIL(names.push_back(GraphParseNode{
            builder.internal_identifier("__g_match_column_", 0, index)}))) {
        }
      }
      if (OB_SUCC(ret) && builder.error() == OB_SUCCESS) {
        ParseNode *replacement = builder.column(alias, graph_node_name(*names.at(index).node_));
        if (replacement != nullptr) { *node = *replacement; }
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
        ret = pending.push_back(GraphParseNode{node->children_[i]});
      }
    }
  }
  if (OB_SUCC(ret) && builder.error() == OB_SUCCESS) {
    ParseNode *projects = builder.make(T_PROJECT_LIST, MAX(1, columns.count()));
    ParseNode *replacement = builder.make(T_ALIAS, 2);
    if (builder.error() == OB_SUCCESS) {
      for (int64_t i = 0; i < columns.count(); ++i) {
        projects->children_[i] = builder.project(columns.at(i).node_, names.at(i).node_);
      }
      if (columns.empty()) { projects->children_[0] = builder.project(builder.integer(1), nullptr); }
      query->children_[PARSE_SELECT_SELECT] = projects;
      replacement->children_[0] = query;
      replacement->children_[1] = alias;
      *base_relation = *replacement;
    }
  }
  return OB_SUCC(ret) ? builder.error() : ret;
}

// Expand one path relation against a tiny step-number relation. The feedback
// loop is referenced exactly once; UNION branches over paths would duplicate
// the traversal because recursive CTE references are inlined by the resolver.
int build_recursive_step_rows(GraphRelationBuilder &builder,
                              const GraphSchema &graph,
                              const ObString &database_name,
                              const GraphPathDesc &path_desc,
                              const GraphBinding &source_pattern,
                              const GraphBinding &edge_pattern,
                              const GraphBinding &target_pattern,
                              const GraphBinding &result_source,
                              const GraphBinding &result_target,
                              ParseNode *path_alias,
                              const ParseNode &base_from,
                              const ParseNode *base_where,
                              const ParseNode &shape,
                              const ParseNode &projects,
                              const GraphRuntimeGroup &runtime_group,
                              bool need_match_number,
                              ParseNode *with_list,
                              ParseNode *&select,
                              const ObIArray<GraphBinding> *composed_actual = nullptr,
                              const ObIArray<GraphExprBinding> *composed_expressions = nullptr,
                              int64_t quantified_index = 1)
{
  if (builder.error() != OB_SUCCESS) {
    return builder.error();
  }
  int ret = OB_SUCCESS;
  const ObString depth_name = ObString::make_string("__g_depth");
  const ObString step_name = ObString::make_string("__g_step");
  ParseNode *step_alias = builder.identifier(ObString::make_string("__g_steps"));
  ParseNode *step_number = builder.column(step_alias, step_name);
  ParseNode *zero = builder.binary(T_OP_EQ, step_number, builder.integer(0));
  const int64_t prefix_steps = (quantified_index - 1) / 2;
  const int64_t fixed_steps = composed_actual == nullptr ? 0 : (composed_actual->count() - 3) / 2;
  const int64_t first_step = fixed_steps == 0 && path_desc.lower_bound_ == 0 ? 0 : 1;
  const int64_t step_count = path_desc.upper_bound_ + fixed_steps - first_step + 1;
  ParseNode *quantified_step = builder.binary(T_OP_MINUS, step_number, builder.integer(prefix_steps));
  ParseNode *depth = builder.column(path_alias, depth_name);
  ParseNode *step_query = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
  ParseNode *step_set = step_count > 1 ? builder.make(T_SET_UNION_ALL, step_count) : nullptr;
  for (int64_t i = 0; i < step_count; ++i) {
    ParseNode *branch = step_count == 1 ? step_query : builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
    ParseNode *projection = builder.make(T_PROJECT_LIST, 1);
    if (branch != nullptr && projection != nullptr) {
      projection->children_[0] = builder.project(
          builder.integer(first_step + i), builder.identifier(step_name));
      branch->children_[PARSE_SELECT_SELECT] = projection;
      builder.force_serial(branch);
      if (step_set != nullptr) {
        step_set->children_[i] = branch;
      }
    }
  }
  if (step_query != nullptr && step_set != nullptr) {
    step_set->value_ = step_count;
    step_query->children_[PARSE_SELECT_SET] = step_set;
  }
  ParseNode *step_relation = builder.make(T_ALIAS, 2);
  if (step_relation != nullptr) {
    step_relation->children_[0] = step_query;
    step_relation->children_[1] = step_alias;
  }
  ParseNode *relation = builder.clone(*base_from.children_[0]);
  for (int64_t i = 1; i < base_from.num_child_; ++i) {
    relation = builder.join(relation, builder.clone(*base_from.children_[i]), builder.integer(1));
  }
  ParseNode *qualified_relation = relation;
  // Zero-hop paths emit step 0 only; non-empty paths emit steps 1..depth.
  ParseNode *step_condition = builder.choose(zero,
      builder.binary(T_OP_EQ, builder.column(path_alias, depth_name), builder.integer(0)),
      builder.binary(T_OP_LE, step_number,
          builder.binary(T_OP_ADD, depth, builder.integer(fixed_steps))));
  relation = builder.join(relation, step_relation, step_condition);
  GraphBinding iter_edge = {edge_pattern.element_, edge_pattern.table_,
      builder.identifier(ObString::make_string("__g_iter_edge"))};
  GraphBinding iter_from = {source_pattern.element_, source_pattern.table_,
      builder.identifier(ObString::make_string("__g_iter_from"))};
  GraphBinding iter_to = {target_pattern.element_, target_pattern.table_,
      builder.identifier(ObString::make_string("__g_iter_to"))};
  ParseNode *edge_condition = nullptr;
  for (int64_t key = 0; key < edge_pattern.element_->key_count_; ++key) {
    ParseNode *selected_key = builder.null_value();
    for (int64_t slot = path_desc.upper_bound_ - 1; slot >= 0; --slot) {
      selected_key = builder.choose(
          builder.binary(T_OP_EQ, quantified_step, builder.integer(slot + 1)),
          make_internal_column(builder, path_alias, "__g_trail_edge_key_", slot, key),
          selected_key);
    }
    builder.append_condition(builder.binary(T_OP_EQ,
        builder.column(iter_edge, edge_pattern.element_->key_columns_[key]), selected_key),
        edge_condition);
  }
  relation = builder.join(relation, builder.relation(iter_edge), edge_condition, true);
  const bool reverse = path_desc.direction_ == GraphPathDirection::IN;
  const uint64_t *from_keys = reverse ? edge_pattern.element_->destination_columns_
                                     : edge_pattern.element_->source_columns_;
  const uint64_t *to_keys = reverse ? edge_pattern.element_->source_columns_
                                   : edge_pattern.element_->destination_columns_;
  ParseNode *from_condition = nullptr;
  ParseNode *to_condition = nullptr;
  for (int64_t key = 0; key < source_pattern.element_->key_count_; ++key) {
    builder.append_condition(builder.binary(T_OP_EQ,
        builder.column(iter_from, source_pattern.element_->key_columns_[key]),
        builder.choose(zero,
            builder.column(result_source, source_pattern.element_->key_columns_[key]),
            builder.column(iter_edge, from_keys[key]))), from_condition);
  }
  for (int64_t key = 0; key < target_pattern.element_->key_count_; ++key) {
    builder.append_condition(builder.binary(T_OP_EQ,
        builder.column(iter_to, target_pattern.element_->key_columns_[key]),
        builder.column(iter_edge, to_keys[key])), to_condition);
  }
  relation = builder.join(relation, builder.relation(iter_from), from_condition,
                          composed_actual != nullptr);
  relation = builder.join(relation, builder.relation(iter_to), to_condition, true);

  ParseNode *match_number = nullptr;
  ObArray<GraphParseNode> ordering;
  if (need_match_number) {
    for (int64_t key = 0; OB_SUCC(ret) && key < source_pattern.element_->key_count_; ++key) {
      ret = ordering.push_back(GraphParseNode{builder.choose(
          builder.binary(T_OP_EQ, builder.column(path_alias, depth_name), builder.integer(0)),
          make_internal_column(builder, path_alias, "__g_source_key_", key), builder.null_value())});
    }
    for (int64_t hop = 0; OB_SUCC(ret) && hop < path_desc.upper_bound_; ++hop) {
      for (int64_t key = 0; OB_SUCC(ret) && key < edge_pattern.element_->key_count_; ++key) {
        ret = ordering.push_back(GraphParseNode{
            make_internal_column(builder, path_alias, "__g_trail_edge_key_", hop, key)});
      }
    }
    for (int64_t i = 1; OB_SUCC(ret) && composed_actual != nullptr
         && i < composed_actual->count(); i += 2) {
      if (i == quantified_index) { continue; }
      const GraphBinding &edge = composed_actual->at(i);
      for (int64_t k = 0; OB_SUCC(ret) && k < edge.element_->key_count_; ++k) {
        ret = ordering.push_back(GraphParseNode{
            builder.column(edge, edge.element_->key_columns_[k])});
      }
    }
    // The anchor has one row per typed source key. A non-empty edge-key
    // sequence uniquely identifies a WALK from that source. DENSE_RANK gives
    // all step copies of that path the same number without collapsing rows.
    ParseNode *rank = make_row_number(
        builder, ordering, builder.column(path_alias, depth_name), true);
    match_number = builder.binary(T_OP_ADD,
        builder.binary(T_OP_MUL, builder.binary(T_OP_MINUS, rank, builder.integer(1)),
                       builder.integer(path_desc.upper_bound_ - path_desc.lower_bound_ + 1)),
        builder.binary(T_OP_ADD,
            builder.binary(T_OP_MINUS, builder.column(path_alias, depth_name),
                           builder.integer(path_desc.lower_bound_)), builder.integer(1)));
  }
  ObArray<GraphExprBinding> bindings;
  if (OB_SUCC(ret) && OB_FAIL(add_expr_binding(bindings, *shape.children_[0],
          GraphExprBindingKind::SCALAR, &iter_from, nullptr, -1, false))) {
  } else if (OB_SUCC(ret) && OB_FAIL(add_expr_binding(bindings, *shape.children_[1],
          GraphExprBindingKind::SCALAR, &iter_edge, nullptr, -1, true))) {
  } else if (OB_SUCC(ret) && OB_FAIL(add_expr_binding(bindings, *shape.children_[2],
          GraphExprBindingKind::SCALAR, &iter_to, nullptr, -1, false))) {
  } else if (OB_SUCC(ret) && OB_FAIL(add_expr_binding(bindings, *source_pattern.variable_,
          GraphExprBindingKind::SCALAR, &result_source, nullptr, -1, false))) {
  } else if (OB_SUCC(ret) && OB_FAIL(add_expr_binding(bindings, *target_pattern.variable_,
          GraphExprBindingKind::SCALAR, &result_target, nullptr, -1, false))) {
  } else if (OB_SUCC(ret) && OB_FAIL(add_expr_binding(bindings, *edge_pattern.variable_,
          GraphExprBindingKind::GROUP, &edge_pattern, nullptr, -1, true, &runtime_group))) {
  }
  ObArray<GraphConditionalBinding> from_alternatives;
  ObArray<GraphConditionalBinding> edge_alternatives;
  ObArray<GraphConditionalBinding> to_alternatives;
  if (OB_SUCC(ret) && composed_actual != nullptr) {
    ParseNode *in_quantified = builder.binary(T_OP_AND,
        builder.binary(T_OP_GT, quantified_step, builder.integer(0)),
        builder.binary(T_OP_LE, quantified_step, depth));
    if (OB_FAIL(from_alternatives.push_back(GraphConditionalBinding{iter_from, in_quantified}))) {
    } else if (OB_FAIL(edge_alternatives.push_back(GraphConditionalBinding{iter_edge, in_quantified}))) {
    } else if (OB_FAIL(to_alternatives.push_back(GraphConditionalBinding{iter_to, in_quantified}))) {
    }
    for (int64_t i = 1; OB_SUCC(ret) && i < composed_actual->count(); i += 2) {
      if (i == quantified_index) { continue; }
      ParseNode *position = i < quantified_index ? builder.integer((i + 1) / 2)
          : builder.binary(T_OP_ADD, depth, builder.integer((i - 1) / 2));
      ParseNode *condition = builder.binary(T_OP_EQ, step_number, position);
      if (OB_FAIL(from_alternatives.push_back(GraphConditionalBinding{composed_actual->at(i - 1), condition}))) {
      } else if (OB_FAIL(edge_alternatives.push_back(GraphConditionalBinding{composed_actual->at(i), condition}))) {
      } else if (OB_FAIL(to_alternatives.push_back(GraphConditionalBinding{composed_actual->at(i + 1), condition}))) {
      }
    }
    if (OB_SUCC(ret)) {
      bindings.at(0).conditional_ = &from_alternatives;
      bindings.at(1).conditional_ = &edge_alternatives;
      bindings.at(2).conditional_ = &to_alternatives;
      // The existing six bindings cover the iterators and quantified segment.
      // Append only the additional singleton variables from the composition.
      for (int64_t i = 3; OB_SUCC(ret) && i < composed_expressions->count(); ++i) {
        ret = bindings.push_back(composed_expressions->at(i));
      }
    }
  }
  if (OB_SUCC(ret)) {
    ParseNode *edge_number = builder.binary(T_OP_MUL, step_number, builder.integer(2));
    bindings.at(0).element_number_expr_ = builder.choose(zero, builder.integer(1),
        builder.binary(T_OP_MINUS, edge_number, builder.integer(1)));
    bindings.at(1).element_number_expr_ = builder.choose(zero, builder.null_value(), edge_number);
    bindings.at(2).element_number_expr_ = builder.choose(zero, builder.null_value(),
        builder.binary(T_OP_ADD, edge_number, builder.integer(1)));
    bindings.at(1).null_guard_ = zero;
    bindings.at(2).null_guard_ = zero;
    ParseNode *rewritten = nullptr;
    ParseNode *from = builder.make(T_FROM_LIST, 1);
    select = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
    if (OB_FAIL(rewrite_graph_projects(
            builder, graph, database_name, projects, bindings, match_number, rewritten))) {
    } else if (select != nullptr && from != nullptr) {
      builder.force_serial(select);
      from->children_[0] = relation;
      select->children_[PARSE_SELECT_FROM] = from;
      select->children_[PARSE_SELECT_SELECT] = rewritten;
      select->children_[PARSE_SELECT_WITH] = with_list;
      ret = qualify_paths_before_steps(builder, base_from, base_where, qualified_relation, select);
    }
  }
  return OB_SUCC(ret) ? builder.error() : ret;
}

// Build a real breadth-first feedback loop for the common homogeneous
// path case. The recursive member performs exactly one edge hop,
// joins the target vertex on every iteration, and carries typed frontier keys,
// optional TRAIL edge-key history and JSON presentation arrays. Source
// predicates live exclusively in the anchor; terminal predicates live
// exclusively in the result query.
// Attach singleton segments to the two endpoints of the quantified segment.
// The loop still enumerates only its own segment; these joins retain each
// prefix/suffix binding rather than collapsing paths with equal endpoints.
int make_composed_alias(GraphRelationBuilder &builder, const ParseNode &chain,
                        const char *prefix, int64_t index, ParseNode *&alias)
{
  struct NodeRef
  {
    const ParseNode *node_ = nullptr;
    TO_STRING_KV(KP_(node));
  };
  int ret = OB_SUCCESS;
  ObArray<NodeRef> pending;
  ObArray<ObString> qualifiers;
  if (OB_FAIL(pending.push_back(NodeRef{&chain}))) {
  }
  while (OB_SUCC(ret) && !pending.empty()) {
    const ParseNode *node = pending.at(pending.count() - 1).node_;
    pending.pop_back();
    if (node->type_ == T_COLUMN_REF && node->num_child_ == 3
        && node->children_[1] != nullptr) {
      ret = qualifiers.push_back(graph_node_name(*node->children_[1]));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
      if (node->children_[i] != nullptr) { ret = pending.push_back(NodeRef{node->children_[i]}); }
    }
  }
  // Correlated references survive lowering. Generated aliases must not turn
  // one of those outer references into a reference to a hidden local table.
  // Check the entire chain because several element filters share SQL scopes.
  alias = nullptr;
  for (int64_t candidate = 0; OB_SUCC(ret) && alias == nullptr; ++candidate) {
    ParseNode *name = builder.internal_identifier(prefix, candidate, index);
    if (name == nullptr) {
      ret = builder.error();
    } else {
      bool conflicts = false;
      for (int64_t i = 0; !conflicts && i < qualifiers.count(); ++i) {
        conflicts = graph_name_equal(graph_node_name(*name), qualifiers.at(i));
      }
      if (!conflicts) { alias = name; }
    }
  }
  return ret;
}

int append_fixed_path_segments(GraphRelationBuilder &builder,
                               const GraphSchema &graph,
                               const ObString &database_name,
                               const ParseNode &chain,
                               const ObIArray<GraphBinding> &patterns,
                               int64_t quantified_index,
                               const GraphBinding &source,
                               const GraphBinding &target,
                               const GraphPathDesc &path_desc,
                               ParseNode *path_alias,
                               ObIArray<GraphBinding> &actual,
                               ObIArray<GraphExprBinding> &expressions,
                               ParseNode *&from,
                               ParseNode *&where,
                               const ObIArray<GraphBinding> *unrolled_edges = nullptr)
{
  int ret = OB_SUCCESS;
  // Reserve before taking pointers into this array for expression bindings.
  if (OB_FAIL(actual.reserve(patterns.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < patterns.count(); ++i) {
    GraphBinding binding = patterns.at(i);
    if (i == quantified_index - 1) {
      binding = source;
    } else if (i == quantified_index + 1) {
      binding = target;
    } else {
      ret = make_composed_alias(builder, chain, "__g_fixed_", i, binding.variable_);
    }
    if (OB_SUCC(ret)) { ret = actual.push_back(binding); }
  }
  ParseNode *extended = nullptr;
  if (OB_SUCC(ret)) {
    extended = builder.make(T_FROM_LIST, from->num_child_ + patterns.count() - 3);
    if (extended == nullptr) {
      ret = builder.error();
    } else {
      for (int64_t i = 0; i < from->num_child_; ++i) {
        extended->children_[i] = from->children_[i];
      }
      int64_t pos = from->num_child_;
      for (int64_t i = 0; OB_SUCC(ret) && i < patterns.count(); ++i) {
        if (i < quantified_index - 1 || i > quantified_index + 1) {
          extended->children_[pos++] = builder.relation(actual.at(i));
          ret = add_expr_binding(expressions, *patterns.at(i).variable_,
              GraphExprBindingKind::SCALAR, &actual.at(i), nullptr, -1, i % 2 == 1);
        }
      }
      from = extended;
    }
  }
  // Prefix keys carried by the loop bind the exact prefix that produced this
  // path. Joining only its terminal vertex would multiply equal-endpoint
  // prefixes a second time. Prefix predicates were already evaluated in the
  // anchor; do not evaluate them again after traversal.
  if (OB_SUCC(ret) && path_alias != nullptr) {
    for (int64_t i = 0; i < quantified_index - 1; ++i) {
      for (int64_t k = 0; k < actual.at(i).element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(actual.at(i), actual.at(i).element_->key_columns_[k]),
            make_internal_column(builder, path_alias, "__g_prefix_key_", i, k)), where);
      }
    }
  }
  ObArray<GraphBinding> fixed_edges;
  for (int64_t i = 1; OB_SUCC(ret) && i < patterns.count(); i += 2) {
    if (i == quantified_index) { continue; }
    const GraphBinding &edge = actual.at(i);
    const bool reverse = chain.children_[i]->value_ == 1;
    const GraphBinding &src = actual.at(reverse ? i + 1 : i - 1);
    const GraphBinding &dst = actual.at(reverse ? i - 1 : i + 1);
    if (edge.element_->source_id_ != src.element_->id_
        || edge.element_->destination_id_ != dst.element_->id_) {
      builder.append_condition(builder.false_value(), where);
    } else {
      for (int64_t k = 0; k < src.element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(edge, edge.element_->source_columns_[k]),
            builder.column(src, src.element_->key_columns_[k])), where);
      }
      for (int64_t k = 0; k < dst.element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(edge, edge.element_->destination_columns_[k]),
            builder.column(dst, dst.element_->key_columns_[k])), where);
      }
    }
    if (OB_FAIL(fixed_edges.push_back(edge))) {
    } else if (unrolled_edges == nullptr && path_desc.path_mode_ == GraphPathMode::TRAIL
               && edge.element_->id_ == patterns.at(quantified_index).element_->id_) {
      // Compare complete typed keys, not serialized EDGE_ID JSON. Unused
      // history slots do not constrain a zero/shorter quantified match.
      for (int64_t slot = 0; slot < path_desc.upper_bound_; ++slot) {
        ParseNode *different = nullptr;
        for (int64_t k = 0; k < edge.element_->key_count_; ++k) {
          ParseNode *key_differs = builder.binary(T_OP_NE,
              builder.column(edge, edge.element_->key_columns_[k]),
              make_internal_column(builder, path_alias, "__g_trail_edge_key_", slot, k));
          different = different == nullptr ? key_differs
              : builder.binary(T_OP_OR, different, key_differs);
        }
        builder.append_condition(builder.binary(T_OP_OR,
            builder.binary(T_OP_LE,
                builder.column(path_alias, ObString::make_string("__g_depth")),
                builder.integer(slot)), different), where);
      }
    }
  }
  if (OB_SUCC(ret) && unrolled_edges != nullptr) {
    for (int64_t i = 0; OB_SUCC(ret) && i < unrolled_edges->count(); ++i) {
      ret = fixed_edges.push_back(unrolled_edges->at(i));
    }
  }
  if (OB_SUCC(ret) && path_desc.path_mode_ == GraphPathMode::TRAIL) {
    ret = append_trail_edge_conditions(builder, fixed_edges, where);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < patterns.count(); ++i) {
    if (i >= quantified_index - 1 && i <= quantified_index + 1) { continue; }
    if (path_alias != nullptr && i < quantified_index - 1) { continue; }
    const ParseNode *filter = chain.children_[i]->children_[2];
    if (filter != nullptr) {
      ObArray<GraphExprBinding> filter_bindings;
      for (int64_t j = 0; OB_SUCC(ret) && j < patterns.count(); ++j) {
        ret = add_expr_binding(filter_bindings, *patterns.at(j).variable_,
            i == j ? GraphExprBindingKind::SCALAR : GraphExprBindingKind::FORBIDDEN,
            &actual.at(j), nullptr, -1, j % 2 == 1);
      }
      ParseNode *condition = nullptr;
      GraphExprRewriter rewriter(builder, graph, database_name, filter_bindings,
                                nullptr, true, false);
      if (OB_SUCC(ret) && OB_FAIL(rewriter.rewrite(*filter, condition))) {
      } else if (OB_SUCC(ret)) {
        builder.append_condition(condition, where);
      }
    }
  }
  return OB_SUCC(ret) ? builder.error() : ret;
}

// Seed the loop from complete prefix bindings, not every vertex in the graph.
// Carry typed keys through recursion so parallel prefixes keep their identity
// and filters (including correlated/volatile expressions) run only here.
int build_graph_prefix_seed(GraphRelationBuilder &builder,
                            const GraphSchema &graph,
                            const ObString &database_name,
                            const ParseNode &chain,
                            const ObIArray<GraphBinding> &patterns,
                            int64_t quantified_index,
                            const GraphBinding &seed,
                            ParseNode *frontier_alias,
                            ParseNode *&from,
                            ParseNode *&where,
                            ObIArray<GraphParseNode> &anchor_keys,
                            ObIArray<GraphParseNode> &recursive_keys,
                            ObIArray<GraphParseNode> &key_names)
{
  int ret = OB_SUCCESS;
  ObArray<GraphBinding> prefix;
  for (int64_t i = 0; OB_SUCC(ret) && i < quantified_index; ++i) {
    GraphBinding binding = patterns.at(i);
    if (i == quantified_index - 1) {
      binding = seed;
    } else {
      ret = make_composed_alias(builder, chain, "__g_prefix_", i, binding.variable_);
    }
    if (OB_SUCC(ret)) { ret = prefix.push_back(binding); }
  }
  ParseNode *extended = builder.make(T_FROM_LIST, quantified_index);
  if (OB_SUCC(ret) && extended == nullptr) {
    ret = builder.error();
  } else if (OB_SUCC(ret)) {
    // Preserve the seed's constant-false history-type join, if present.
    extended->children_[0] = from->children_[0];
    for (int64_t i = 0; i < quantified_index - 1; ++i) {
      extended->children_[i + 1] = builder.relation(prefix.at(i));
    }
    from = extended;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < quantified_index - 1; ++i) {
    const GraphBinding &binding = prefix.at(i);
    for (int64_t k = 0; OB_SUCC(ret) && k < binding.element_->key_count_; ++k) {
      ParseNode *name = builder.internal_identifier("__g_prefix_key_", i, k);
      if (name == nullptr) {
        ret = builder.error();
      } else if (OB_FAIL(anchor_keys.push_back(GraphParseNode{
                     builder.column(binding, binding.element_->key_columns_[k])}))) {
      } else if (OB_FAIL(recursive_keys.push_back(GraphParseNode{
                     builder.column(frontier_alias, graph_node_name(*name))}))) {
      } else if (OB_FAIL(key_names.push_back(GraphParseNode{name}))) {
      }
    }
    const ParseNode *filter = chain.children_[i]->children_[2];
    if (OB_SUCC(ret) && filter != nullptr) {
      // The complete graph namespace was checked before lowering, so unknown
      // names here can only be genuine outer references, not other variables.
      ParseNode *condition = nullptr;
      if (OB_FAIL(rewrite_graph_filter(builder, graph, database_name, filter,
                  *patterns.at(i).variable_, &binding, nullptr, nullptr, false, condition))) {
      } else {
        builder.append_condition(condition, where);
      }
    }
  }
  for (int64_t i = 1; OB_SUCC(ret) && i < quantified_index - 1; i += 2) {
    const GraphBinding &edge = prefix.at(i);
    const bool reverse = chain.children_[i]->value_ == 1;
    const GraphBinding &src = prefix.at(reverse ? i + 1 : i - 1);
    const GraphBinding &dst = prefix.at(reverse ? i - 1 : i + 1);
    if (edge.element_->source_id_ != src.element_->id_
        || edge.element_->destination_id_ != dst.element_->id_) {
      builder.append_condition(builder.false_value(), where);
    } else {
      for (int64_t k = 0; k < src.element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(edge, edge.element_->source_columns_[k]),
            builder.column(src, src.element_->key_columns_[k])), where);
      }
      for (int64_t k = 0; k < dst.element_->key_count_; ++k) {
        builder.append_condition(builder.binary(T_OP_EQ,
            builder.column(edge, edge.element_->destination_columns_[k]),
            builder.column(dst, dst.element_->key_columns_[k])), where);
      }
    }
  }
  return OB_SUCC(ret) ? builder.error() : ret;
}

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
                                const ParseNode *shape,
                                const ParseNode &projects,
                                ParseNode *&select,
                                const ParseNode *composed_chain = nullptr,
                                const ObIArray<GraphBinding> *composed_bindings = nullptr,
                                int64_t quantified_index = 1)
{
  int ret = OB_SUCCESS;
  static const ObString CTE_NAME = ObString::make_string("__g_walk_frontier");
  static const ObString DEPTH_NAME = ObString::make_string("__g_depth");
  static const char *SOURCE_KEY_PREFIX = "__g_source_key_";
  static const char *CURRENT_KEY_PREFIX = "__g_current_key_";
  static const char *TRAIL_EDGE_KEY_PREFIX = "__g_trail_edge_key_";
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
  GraphBinding history_types = {edge_pattern.element_, edge_pattern.table_,
      builder.identifier(ObString::make_string("__g_history_key_types"))};

  bool need_match_number = false;
  bool need_edge_identity = false;
  const bool need_trail_history = path_desc.path_mode_ == GraphPathMode::TRAIL;
  const bool need_edge_history = path_desc.needs_edge_key_history();
  ObSEArray<const GraphProperty *, 4> edge_properties;
  if (OB_FAIL(collect_path_projection_requirements(
          projects, graph, edge_pattern, need_match_number,
          need_edge_identity, edge_properties))) {
  }

  ObSEArray<GraphParseNode, 32> anchor_exprs;
  ObSEArray<GraphParseNode, 32> recursive_exprs;
  ObSEArray<GraphParseNode, 32> cte_column_names;
  ObArray<GraphParseNode> prefix_recursive_keys;
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
  // TRAIL and PER_STEP keep one typed edge-key slot per possible hop. Slot N is
  // populated while expanding a frontier at depth N. Keeping key components as
  // SQL values avoids using the JSON returned by EDGE_ID() for internal
  // equality, and the recursive CTE row store accounts the extra memory.
  for (int64_t slot = 0; OB_SUCC(ret) && need_edge_history
       && slot < path_desc.upper_bound_; ++slot) {
    for (int64_t key = 0; OB_SUCC(ret) && key < edge_pattern.element_->key_count_; ++key) {
      ParseNode *name = builder.internal_identifier(TRAIL_EDGE_KEY_PREFIX, slot, key);
      if (OB_FAIL(anchor_exprs.push_back(GraphParseNode{
              builder.column(history_types, edge_pattern.element_->key_columns_[key])}))) {
      } else if (OB_FAIL(cte_column_names.push_back(GraphParseNode{name}))) {
      }
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
      if (need_edge_history && path_desc.upper_bound_ > 0) {
        // A constant-false outer join supplies typed NULLs without inventing a
        // CAST mapping for every SQL key type/collation. No edge row is read.
        // Recursive CTE column types are fixed by their anchor, so an untyped
        // NULL would make every later edge-history slot remain NULL.
        ParseNode *join = builder.make(T_JOINED_TABLE, 5);
        if (join != nullptr) {
          join->children_[0] = builder.make(T_JOIN_LEFT, 0);
          join->children_[1] = anchor_from->children_[0];
          join->children_[2] = builder.relation(history_types);
          join->children_[3] = builder.false_value();
          anchor_from->children_[0] = join;
        }
      }
    }
    if (OB_SUCC(ret) && source_filter != nullptr
        && OB_FAIL(rewrite_graph_filter(builder, graph, database_name, source_filter,
                                        *source_pattern.variable_, &seed,
                                        &edge_pattern, &target_pattern, true,
                                        condition))) {
    } else if (OB_SUCC(ret)) {
      builder.append_condition(condition, anchor_where);
    }
    if (OB_SUCC(ret) && composed_chain != nullptr && quantified_index > 1) {
      ret = build_graph_prefix_seed(builder, graph, database_name,
          *composed_chain, *composed_bindings, quantified_index, seed, frontier_alias,
          anchor_from, anchor_where, anchor_exprs, prefix_recursive_keys, cte_column_names);
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
    for (int64_t slot = 0; OB_SUCC(ret) && need_edge_history
         && slot < path_desc.upper_bound_; ++slot) {
      for (int64_t key = 0; OB_SUCC(ret) && key < edge_pattern.element_->key_count_; ++key) {
        ObSEArray<GraphParseNode, 3> arguments;
        if (OB_FAIL(arguments.push_back(GraphParseNode{builder.binary(
                T_OP_EQ,
                builder.column(frontier_alias, DEPTH_NAME),
                builder.integer(slot))}))) {
        } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                       builder.column(edge, edge_pattern.element_->key_columns_[key])}))) {
        } else if (OB_FAIL(arguments.push_back(GraphParseNode{
                       make_internal_column(builder, frontier_alias,
                                            TRAIL_EDGE_KEY_PREFIX, slot, key)}))) {
        } else if (OB_FAIL(recursive_exprs.push_back(GraphParseNode{
                       builder.function("if", arguments)}))) {
        }
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
    // Only slots before the current depth contain previously traversed edges.
    // For every active slot, require at least one primary-key component of the
    // candidate edge to differ. This is a per-frontier-row check, so paths that
    // happen to use the same edge remain separate paths rather than being
    // globally deduplicated.
    for (int64_t slot = 0; OB_SUCC(ret) && need_trail_history
         && slot < path_desc.upper_bound_; ++slot) {
      ParseNode *edge_differs = nullptr;
      for (int64_t key = 0; key < edge_pattern.element_->key_count_; ++key) {
        ParseNode *key_differs = builder.binary(
            T_OP_NE,
            builder.column(edge, edge_pattern.element_->key_columns_[key]),
            make_internal_column(builder, frontier_alias,
                                 TRAIL_EDGE_KEY_PREFIX, slot, key));
        edge_differs = edge_differs == nullptr
            ? key_differs : builder.binary(T_OP_OR, edge_differs, key_differs);
      }
      ParseNode *slot_unused = builder.binary(
          T_OP_LE,
          builder.column(frontier_alias, DEPTH_NAME),
          builder.integer(slot));
      builder.append_condition(builder.binary(T_OP_OR, slot_unused, edge_differs),
                               recursive_where);
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
    for (int64_t i = 0; OB_SUCC(ret) && i < prefix_recursive_keys.count(); ++i) {
      ret = recursive_exprs.push_back(prefix_recursive_keys.at(i));
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
      set->int16_values_[3] = static_cast<int16_t>(path_desc.path_mode_);
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
    ObArray<GraphBinding> composed_actual;
    if (OB_SUCC(ret) && composed_chain != nullptr) {
      ret = append_fixed_path_segments(builder, graph, database_name,
          *composed_chain, *composed_bindings, quantified_index,
          result_source, result_target, path_desc, path_alias,
          composed_actual, expr_bindings, from, where);
    }
    if (OB_SUCC(ret) && path_desc.row_shape_ == GraphPathRowShape::PER_STEP) {
      if (OB_ISNULL(shape)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = build_recursive_step_rows(builder, graph, database_name, path_desc,
            source_pattern, edge_pattern, target_pattern, result_source, result_target,
            path_alias, *from, where, *shape, projects, runtime_group, need_match_number,
            with_list, select, composed_chain == nullptr ? nullptr : &composed_actual,
            composed_chain == nullptr ? nullptr : &expr_bindings, quantified_index);
      }
      return ret;
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
                            ParseNode *&select,
                            const ParseNode *composed_chain = nullptr,
                            const ObIArray<GraphBinding> *composed_bindings = nullptr,
                            int64_t quantified_index = 1)
{
  int ret = OB_SUCCESS;
  ObSEArray<GraphBinding, GRAPH_PATH_MAX_HOPS + 1> vertices;
  ObSEArray<GraphBinding, GRAPH_PATH_MAX_HOPS> edges;
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
  if (OB_SUCC(ret) && path_desc.path_mode_ == GraphPathMode::TRAIL
      && OB_FAIL(append_trail_edge_conditions(builder, edges, where))) {
  }
  if (OB_SUCC(ret) && needs_separate_terminal) {
    builder.append_condition(builder.false_value(), where);
  }
  if (OB_SUCC(ret) && (hop < path_desc.lower_bound_ || hop > path_desc.upper_bound_)) {
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
               || shape->children_[2] == nullptr || step < 0
               || step > hop + (composed_chain == nullptr ? 0 : (composed_chain->num_child_ - 3) / 2)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const bool zero_hop = hop == 0;
      // Composed iteration bindings are replaced below using whole-path step
      // positions. Use a valid temporary segment binding while constructing
      // the common source/group/target expression namespace.
      const int64_t segment_step = composed_chain == nullptr ? step : (zero_hop ? 0 : 1);
      const GraphExprBindingKind edge_kind = zero_hop ? GraphExprBindingKind::NULL_SCALAR
                                                       : GraphExprBindingKind::SCALAR;
      const GraphExprBindingKind target_kind = zero_hop ? GraphExprBindingKind::NULL_SCALAR
                                                         : GraphExprBindingKind::SCALAR;
      if (OB_FAIL(add_expr_binding(expr_bindings, *shape->children_[0],
                                   GraphExprBindingKind::SCALAR,
                                   &vertices.at(zero_hop ? 0 : segment_step - 1), nullptr,
                                   zero_hop ? 1 : 2 * step - 1, false))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *shape->children_[1], edge_kind,
                                          zero_hop ? nullptr : &edges.at(segment_step - 1), nullptr,
                                          zero_hop ? -1 : 2 * step, true))) {
      } else if (OB_FAIL(add_expr_binding(expr_bindings, *shape->children_[2], target_kind,
                                          zero_hop ? nullptr : &vertices.at(segment_step), nullptr,
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
  ObArray<GraphBinding> composed_actual;
  if (OB_SUCC(ret) && composed_chain != nullptr) {
    ret = append_fixed_path_segments(builder, graph, database_name,
        *composed_chain, *composed_bindings, quantified_index,
        vertices.at(0), *terminal, path_desc, nullptr,
        composed_actual, expr_bindings, from, where, &edges);
  }
  ObArray<GraphConditionalBinding> from_alternatives;
  ObArray<GraphConditionalBinding> edge_alternatives;
  ObArray<GraphConditionalBinding> to_alternatives;
  ParseNode *qualified_base_from = nullptr;
  ParseNode *qualified_base_relation = nullptr;
  if (OB_SUCC(ret) && composed_chain != nullptr
      && path_desc.row_shape_ == GraphPathRowShape::PER_STEP) {
    // Enumerate steps after qualifying the one heterogeneous path, rather
    // than copying the path query (and its volatile filters) into each step.
    qualified_base_from = builder.clone(*from);
    qualified_base_relation = from->children_[0];
    for (int64_t i = 1; i < from->num_child_; ++i) {
      qualified_base_relation = builder.join(
          qualified_base_relation, from->children_[i], builder.integer(1));
    }
    const int64_t step_count = hop + (composed_chain->num_child_ - 3) / 2;
    const ObString step_name = ObString::make_string("__g_step");
    ParseNode *step_alias = builder.identifier(ObString::make_string("__g_steps"));
    ParseNode *step_number = builder.column(step_alias, step_name);
    ParseNode *step_query = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
    ParseNode *step_set = builder.make(T_SET_UNION_ALL, step_count);
    ParseNode *step_relation = builder.make(T_ALIAS, 2);
    for (int64_t i = 0; builder.error() == OB_SUCCESS && i < step_count; ++i) {
      ParseNode *branch = builder.make(T_SELECT, PARSE_SELECT_MAX_IDX);
      ParseNode *projection = builder.make(T_PROJECT_LIST, 1);
      if (builder.error() == OB_SUCCESS) {
        projection->children_[0] = builder.project(builder.integer(i + 1), builder.identifier(step_name));
        branch->children_[PARSE_SELECT_SELECT] = projection;
        builder.force_serial(branch);
        step_set->children_[i] = branch;
      }
    }
    if (builder.error() == OB_SUCCESS) {
      step_set->value_ = step_count;
      step_query->children_[PARSE_SELECT_SET] = step_set;
      step_relation->children_[0] = step_query;
      step_relation->children_[1] = step_alias;
      from = builder.make(T_FROM_LIST, 1);
      if (from != nullptr) {
        from->children_[0] = builder.join(qualified_base_relation, step_relation, builder.integer(1));
      }
    }
    ObArray<GraphParseNode> ordering;
    int64_t position = 1;
    for (int64_t i = 1; OB_SUCC(ret) && i < composed_actual.count(); i += 2) {
      const int64_t edge_count = i == quantified_index ? edges.count() : 1;
      for (int64_t j = 0; OB_SUCC(ret) && j < edge_count; ++j, ++position) {
        const GraphBinding &left = i == quantified_index ? vertices.at(j) : composed_actual.at(i - 1);
        const GraphBinding &edge = i == quantified_index ? edges.at(j) : composed_actual.at(i);
        const GraphBinding &right = i == quantified_index ? vertices.at(j + 1) : composed_actual.at(i + 1);
        ParseNode *condition = builder.binary(T_OP_EQ, step_number, builder.integer(position));
        if (OB_FAIL(from_alternatives.push_back(GraphConditionalBinding{left, condition}))) {
        } else if (OB_FAIL(edge_alternatives.push_back(GraphConditionalBinding{edge, condition}))) {
        } else if (OB_FAIL(to_alternatives.push_back(GraphConditionalBinding{right, condition}))) {
        }
        for (int64_t k = 0; OB_SUCC(ret) && k < edge.element_->key_count_; ++k) {
          ret = ordering.push_back(GraphParseNode{
              builder.column(edge, edge.element_->key_columns_[k])});
        }
      }
    }
    if (OB_SUCC(ret)) {
      expr_bindings.at(0).conditional_ = &from_alternatives;
      expr_bindings.at(1).conditional_ = &edge_alternatives;
      expr_bindings.at(2).conditional_ = &to_alternatives;
      for (int64_t i = 0; i < 3; ++i) {
        expr_bindings.at(i).kind_ = GraphExprBindingKind::SCALAR;
        expr_bindings.at(i).element_number_expr_ = builder.binary(T_OP_ADD,
            builder.binary(T_OP_MUL, step_number, builder.integer(2)), builder.integer(i - 1));
      }
      // All step copies order the complete edge sequence, including fixed
      // edges. A dense rank therefore names the same path in every copy.
      match_number = builder.binary(T_OP_ADD,
          builder.binary(T_OP_MUL,
              builder.binary(T_OP_MINUS, make_row_number(builder, ordering, nullptr, true),
                             builder.integer(1)),
              builder.integer(path_desc.upper_bound_ - path_desc.lower_bound_ + 1)),
          builder.integer(hop - path_desc.lower_bound_ + 1));
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
      if (qualified_base_from != nullptr) {
        ret = qualify_paths_before_steps(builder, *qualified_base_from, where,
                                         qualified_base_relation, select);
      } else if (where != nullptr) {
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
  GraphPathMode path_mode = GraphPathMode::WALK;
  ObSEArray<GraphBinding, 8> bindings;
  if (params_.resolver_scope_stmt_type_ == T_CREATE_VIEW || params_.is_in_view_) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "GRAPH_TABLE in persistent views");
  } else if (!get_stmt()->is_select_stmt()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "GRAPH_TABLE as a DML target");
  } else if (node.num_child_ != 6 || node.children_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (node.children_[GRAPH_TABLE_PATH_MODE] != nullptr
             && (node.children_[GRAPH_TABLE_PATH_MODE]->type_ != T_GRAPH_PATH_MODE
                 || node.children_[GRAPH_TABLE_PATH_MODE]->value_
                        < static_cast<int64_t>(GraphPathMode::TRAIL)
                 || node.children_[GRAPH_TABLE_PATH_MODE]->value_
                        > static_cast<int64_t>(GraphPathMode::ACYCLIC))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (node.children_[GRAPH_TABLE_PATH_MODE] != nullptr
             && node.children_[GRAPH_TABLE_PATH_MODE]->value_
                    != static_cast<int64_t>(GraphPathMode::TRAIL)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "SIMPLE and ACYCLIC graph path modes");
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
    path_mode = node.children_[GRAPH_TABLE_PATH_MODE] == nullptr
        ? GraphPathMode::WALK
        : static_cast<GraphPathMode>(node.children_[GRAPH_TABLE_PATH_MODE]->value_);
    const ParseNode &chain = *node.children_[GRAPH_TABLE_PATTERN];
    const ParseNode *shape = node.children_[GRAPH_TABLE_SHAPE];
    const ParseNode &projects = *node.children_[GRAPH_TABLE_PROJECTS];
    int64_t quantified_segments = 0;
    int64_t quantified_index = 1;
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
        quantified_index = i;
      }
    }
    if (OB_SUCC(ret) && quantified_segments > 1) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "multiple or nested quantified graph path segments");
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
        if (OB_SUCC(ret) && path_mode == GraphPathMode::TRAIL) {
          ObSEArray<GraphBinding, GRAPH_PATH_MAX_HOPS> edges;
          for (int64_t i = 1; OB_SUCC(ret) && i < bindings.count(); i += 2) {
            ret = edges.push_back(bindings.at(i));
          }
          if (OB_SUCC(ret)) {
            ret = append_trail_edge_conditions(builder, edges, where);
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
        path_desc.source_element_id_ = bindings.at(quantified_index - 1).element_->id_;
        path_desc.edge_element_id_ = bindings.at(quantified_index).element_->id_;
        path_desc.target_element_id_ = bindings.at(quantified_index + 1).element_->id_;
        path_desc.lower_bound_ = quantifier->children_[0] == nullptr
            ? 0 : quantifier->children_[0]->value_;
        path_desc.upper_bound_ = quantifier->value_ == 0
            ? path_desc.lower_bound_ : quantifier->children_[1]->value_;
        path_desc.direction_ = chain.children_[quantified_index]->value_ == 1
            ? GraphPathDirection::IN : GraphPathDirection::OUT;
        path_desc.path_mode_ = path_mode;
        path_desc.row_shape_ = shape != nullptr && shape->value_ == 1
            ? GraphPathRowShape::PER_STEP : GraphPathRowShape::PER_MATCH;
        path_desc.need_path_ = path_desc.path_mode_ == GraphPathMode::TRAIL
            || path_desc.row_shape_ == GraphPathRowShape::PER_STEP
            || contains_item_type(projects, T_FUN_JSON_ARRAYAGG);
        if (path_desc.lower_bound_ < 0 || path_desc.upper_bound_ < 0
            || path_desc.lower_bound_ > path_desc.upper_bound_) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph path bounds with lower greater than upper");
        } else if (path_desc.upper_bound_ > GRAPH_PATH_MAX_HOPS) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "graph path upper bounds greater than 16");
        } else if (path_desc.upper_bound_ + (chain.num_child_ - 3) / 2 > GRAPH_PATH_MAX_HOPS) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "composed graph paths longer than 16 edges");
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
      // Validate against the complete graph-variable namespace before lowering
      // the quantified segment. Otherwise a prefix variable could accidentally
      // be interpreted as a same-named outer LATERAL alias inside its filter.
      if (chain.num_child_ != 3) {
        for (int64_t i = 0; OB_SUCC(ret) && i < bindings.count(); ++i) {
          const ParseNode *filter = chain.children_[i]->children_[2];
          if (filter == nullptr) { continue; }
          ObArray<GraphExprBinding> filter_bindings;
          for (int64_t j = 0; OB_SUCC(ret) && j < bindings.count(); ++j) {
            ret = add_expr_binding(filter_bindings, *bindings.at(j).variable_,
                i == j ? GraphExprBindingKind::SCALAR : GraphExprBindingKind::FORBIDDEN,
                &bindings.at(j), nullptr, -1, j % 2 == 1);
          }
          ParseNode *validated = nullptr;
          GraphExprRewriter rewriter(builder, *graph, database_name,
                                    filter_bindings, nullptr, true, false);
          if (OB_SUCC(ret)) { ret = rewriter.rewrite(*filter, validated); }
        }
      }
      ObString adjacency_index;
      if (OB_SUCC(ret) && OB_FAIL(find_adjacency_index(
              bindings.at(quantified_index), path_desc.direction_,
              *schema_checker_->get_schema_guard(), adjacency_index))) {
        LOG_WARN("failed to select graph adjacency index", K(ret), K(path_desc));
      }
      const bool use_feedback_loop =
          bindings.at(quantified_index - 1).element_->id_
              == bindings.at(quantified_index + 1).element_->id_;
      const bool composed = chain.num_child_ != 3;
      if (OB_SUCC(ret) && use_feedback_loop && adjacency_index.empty()) {
        // The native fallback requires a base-table scan. Graph element keys
        // are the complete base-table primary key, so PRIMARY is always valid.
        // Do not force this hint on unrolled plans, where a selective ordinary
        // index remains a legitimate access path.
        adjacency_index = ObString::make_string("PRIMARY");
      }
      const ObString *adjacency_index_ptr = adjacency_index.empty()
          ? nullptr : &adjacency_index;
      if (OB_SUCC(ret) && use_feedback_loop) {
        ret = build_recursive_graph_match(builder, *graph, database_name, path_desc,
                                           adjacency_index_ptr,
                                           bindings.at(quantified_index - 1),
                                           bindings.at(quantified_index),
                                           bindings.at(quantified_index + 1),
                                           chain.children_[quantified_index - 1]->children_[2],
                                           chain.children_[quantified_index]->children_[2],
                                           chain.children_[quantified_index + 1]->children_[2],
                                           shape, projects, select,
                                           composed ? &chain : nullptr,
                                           composed ? &bindings : nullptr, quantified_index);
      } else if (OB_SUCC(ret)) {
        ObSEArray<GraphParseNode, 32> branches;
        // With different source/target element mappings the same quantified
        // edge cannot be traversed twice, and zero hops cannot change mapping.
        // A typed one-hop branch also describes an empty result when bounds
        // exclude one. Keep the legacy single-segment lowering unchanged.
        const bool composed_steps = composed && path_desc.row_shape_ == GraphPathRowShape::PER_STEP;
        for (int64_t hop = composed_steps ? 1 : path_desc.lower_bound_;
             OB_SUCC(ret) && hop <= (composed_steps ? 1 : path_desc.upper_bound_);
             ++hop) {
          const int64_t first_step = path_desc.row_shape_ == GraphPathRowShape::PER_STEP
              ? (composed ? 1 : (hop == 0 ? 0 : 1)) : -1;
          const int64_t last_step = composed_steps ? first_step : path_desc.row_shape_ == GraphPathRowShape::PER_STEP
              ? hop + (composed ? (chain.num_child_ - 3) / 2 : 0) : -1;
          for (int64_t step = first_step; OB_SUCC(ret) && step <= last_step; ++step) {
            ParseNode *branch = nullptr;
            if (OB_FAIL(build_graph_path_branch(builder, *graph, database_name, path_desc,
                                                 adjacency_index_ptr,
                                                 bindings.at(quantified_index - 1),
                                                 bindings.at(quantified_index),
                                                 bindings.at(quantified_index + 1),
                                                 chain.children_[quantified_index - 1]->children_[2],
                                                 chain.children_[quantified_index]->children_[2],
                                                 chain.children_[quantified_index + 1]->children_[2],
                                                 shape, projects, hop, step, branch,
                                                 composed ? &chain : nullptr,
                                                 composed ? &bindings : nullptr, quantified_index))) {
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
        const GraphPathDesc *saved_path_desc = params_.internal_graph_path_desc_;
        guard.set_session_id(0);
        if (use_feedback_loop) {
          // The generated T_GRAPH_FEEDBACK_LOOP node only has two relational
          // children. Carry the typed graph mapping through resolver context
          // instead of hiding persistent IDs in generated aliases or columns.
          params_.internal_graph_path_desc_ = &path_desc;
        }
        ret = resolve_generate_table(*select, node.children_[GRAPH_TABLE_ALIAS], table_item);
        params_.internal_graph_path_desc_ = saved_path_desc;
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
