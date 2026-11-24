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

#include "sql/resolver/ddl/ob_ai_column_resolver.h"

#include "lib/hash_func/murmur_hash.h"
#include "lib/string/ob_sql_string.h"
#include "share/schema/ob_ai_model_mgr.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "query/parser/ob_item_type.h"
#include "sql/resolver/ddl/ob_vec_index_builder_util.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_basic_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;

namespace sql
{
namespace
{

static const int64_t DEFAULT_MAX_RETRIES = 5;
static const int64_t MAX_RETRIES = 100;
static const char *AI_SYSTEM_PROMPT_CONFIG_KEY = "__ob_ai_system_prompt";

ObString node_string(const ParseNode &node)
{
  return ObString(static_cast<int32_t>(node.str_len_), node.str_value_);
}

bool is_string_literal(const ParseNode &node)
{
  return T_VARCHAR == node.type_ || T_CHAR == node.type_;
}

bool is_integer_literal(const ParseNode &node)
{
  return T_INT == node.type_ || T_UINT64 == node.type_;
}

bool contains_pipe_concat(const ObString &text)
{
  bool found = false;
  for (int64_t i = 0; !found && i + 1 < text.length(); ++i) {
    found = '|' == text.ptr()[i] && '|' == text.ptr()[i + 1];
  }
  return found;
}

ObString get_type_system_prompt(const ObColumnSchemaV2 &column)
{
  const ObObjType type = column.get_data_type();
  ObString prompt;
  if (ob_is_json(type)) {
    prompt = ObString::make_string(
        "Return exactly one valid JSON value compatible with the target JSON "
        "column. Do not use Markdown code fences and do not add text before "
        "or after the JSON.");
  } else if (ob_is_integer_type(type)) {
    prompt = ob_is_unsigned_type(type)
        ? ObString::make_string(
              "Return only one non-negative base-10 integer with no units, "
              "labels, or surrounding text.")
        : ObString::make_string(
              "Return only one base-10 integer with no units, labels, or "
              "surrounding text.");
  } else if (ob_is_numeric_type(type)) {
    prompt = ObString::make_string(
        "Return only one finite base-10 number with no units, labels, or "
        "surrounding text.");
  } else if (ob_is_date_tc(type) || ob_is_mysql_date_tc(type)) {
    prompt = ObString::make_string(
        "Return only one date in YYYY-MM-DD format with no surrounding text.");
  } else if (ob_is_time_tc(type)) {
    prompt = ObString::make_string(
        "Return only one time in HH:MM:SS format, optionally followed by "
        "fractional seconds, with no surrounding text.");
  } else if (ob_is_year_tc(type)) {
    prompt = ObString::make_string(
        "Return only one four-digit year with no surrounding text.");
  } else if (ob_is_datetime_tc(type) || ob_is_mysql_datetime_tc(type)
             || ob_is_otimestampe_tc(type)) {
    prompt = ObString::make_string(
        "Return only one date and time in YYYY-MM-DD HH:MM:SS format, "
        "optionally followed by fractional seconds, with no surrounding text.");
  }
  return prompt;
}

int build_normalized_expr(const ObString &expr_text,
                          const ObColumnSchemaV2 &column,
                          const bool is_embed,
                          ObSqlString &normalized_expr)
{
  int ret = OB_SUCCESS;
  const ObString system_prompt = is_embed
      ? ObString() : get_type_system_prompt(column);
  if (system_prompt.empty()) {
    ret = normalized_expr.append(expr_text);
  } else {
    int64_t close_pos = expr_text.length() - 1;
    while (close_pos >= 0
           && (' ' == expr_text.ptr()[close_pos]
               || '\t' == expr_text.ptr()[close_pos]
               || '\r' == expr_text.ptr()[close_pos]
               || '\n' == expr_text.ptr()[close_pos])) {
      --close_pos;
    }
    if (close_pos < 0 || ')' != expr_text.ptr()[close_pos]) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI column expression has no closing parenthesis",
               K(ret), K(expr_text));
    } else if (OB_FAIL(normalized_expr.append(ObString(
                   static_cast<int32_t>(close_pos), expr_text.ptr())))) {
      LOG_WARN("failed to append AI column expression prefix", K(ret));
    } else if (OB_FAIL(normalized_expr.append_fmt(
                   ", '{\"%s\":\"%.*s\"}')",
                   AI_SYSTEM_PROMPT_CONFIG_KEY,
                   system_prompt.length(), system_prompt.ptr()))) {
      LOG_WARN("failed to append AI column system prompt", K(ret));
    }
  }
  return ret;
}

bool is_allowed_nested_function(const ObString &name)
{
  static const char *FUNCTIONS[] = {
      "concat", "json_object", "json_array", "coalesce", "ifnull",
      "lower", "upper", "trim", "substr", "substring", "replace",
      "cast", "json_extract", "json_value", "json_query"};
  bool allowed = false;
  for (int64_t i = 0; !allowed && i < ARRAYSIZEOF(FUNCTIONS); ++i) {
    allowed = 0 == name.case_compare(FUNCTIONS[i]);
  }
  return allowed;
}

int find_dependency_column(
    const ObString &column_name,
    const ObColumnSchemaV2 &target_column,
    const ObIArray<const ObColumnSchemaV2 *> &all_columns,
    const ObColumnSchemaV2 *&dependency)
{
  int ret = OB_SUCCESS;
  dependency = nullptr;
  for (int64_t i = 0; nullptr == dependency && i < all_columns.count(); ++i) {
    const ObColumnSchemaV2 *candidate = all_columns.at(i);
    if (nullptr != candidate
        && 0 == candidate->get_column_name_str().case_compare(column_name)) {
      dependency = candidate;
    }
  }
  if (nullptr == dependency) {
    ret = OB_ERR_BAD_FIELD_ERROR;
    LOG_WARN("AI column dependency does not exist", K(ret), K(column_name));
  } else if (0 == dependency->get_column_name_str().case_compare(
                     target_column.get_column_name_str())
             || (OB_INVALID_ID != dependency->get_column_id()
                 && dependency->get_column_id() == target_column.get_column_id())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI column cannot depend on itself", K(ret), K(column_name));
  } else if (OB_INVALID_ID == dependency->get_column_id()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("AI column dependency must have a stable column id", K(ret), K(column_name));
  } else if (dependency->is_ai_column()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("AI column chaining is not supported", K(ret), K(column_name));
  } else if (dependency->is_hidden()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("AI column cannot depend on a hidden column", K(ret), K(column_name));
  }
  return ret;
}

int add_dependency_once(const uint64_t column_id, ObAIColumnSchema &schema)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  const ObIArray<uint64_t> &ids = schema.get_dependency_column_ids();
  for (int64_t i = 0; !exists && i < ids.count(); ++i) {
    exists = column_id == ids.at(i);
  }
  if (!exists && OB_FAIL(schema.add_dependency_column_id(column_id))) {
    LOG_WARN("failed to add AI column dependency", K(ret), K(column_id));
  }
  return ret;
}

int resolve_safe_expr(
    const ParseNode &node,
    const ObColumnSchemaV2 &target_column,
    const ObIArray<const ObColumnSchemaV2 *> &all_columns,
    ObAIColumnSchema &schema,
    const bool allow_pipe_concat)
{
  int ret = OB_SUCCESS;
  switch (node.type_) {
    case T_COLUMN_REF: {
      const ObColumnSchemaV2 *dependency = nullptr;
      if (3 != node.num_child_ || nullptr == node.children_
          || nullptr == node.children_[2]) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid AI column reference", K(ret));
      } else if (nullptr != node.children_[0] || nullptr != node.children_[1]) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("qualified AI column dependency is not supported", K(ret));
      } else if (OB_FAIL(find_dependency_column(
                     node_string(*node.children_[2]), target_column,
                     all_columns, dependency))) {
        LOG_WARN("failed to resolve AI column dependency", K(ret));
      } else if (OB_FAIL(add_dependency_once(dependency->get_column_id(), schema))) {
        LOG_WARN("failed to record AI column dependency", K(ret));
      }
      break;
    }
    case T_FUN_SYS: {
      if (node.num_child_ < 1 || nullptr == node.children_
          || nullptr == node.children_[0]) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid nested function in AI column expression", K(ret));
      } else if (!is_allowed_nested_function(node_string(*node.children_[0]))) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("function is not allowed in AI column expression",
                 K(ret), "function", node_string(*node.children_[0]));
      } else if (node.num_child_ > 1 && nullptr != node.children_[1]
                 && OB_FAIL(resolve_safe_expr(*node.children_[1], target_column,
                                              all_columns, schema,
                                              allow_pipe_concat))) {
        LOG_WARN("failed to resolve nested AI column expression", K(ret));
      }
      break;
    }
    case T_OP_OR: {
      if (!allow_pipe_concat) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("boolean OR is not allowed in AI column expression", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < node.num_child_; ++i) {
        if (nullptr != node.children_[i]
            && OB_FAIL(resolve_safe_expr(*node.children_[i], target_column,
                                         all_columns, schema,
                                         allow_pipe_concat))) {
          LOG_WARN("failed to resolve pipe concat child", K(ret), K(i));
        }
      }
      break;
    }
    case T_EXPR_LIST:
    case T_OP_CNN:
    case T_OP_ADD:
    case T_OP_MINUS:
    case T_OP_MUL:
    case T_OP_DIV:
    case T_OP_MOD:
    case T_FUN_SYS_COALESCE:
    case T_FUN_SYS_IFNULL:
    case T_FUN_SYS_JSON_OBJECT:
    case T_FUN_SYS_JSON_ARRAY:
    case T_FUN_SYS_JSON_VALUE:
    case T_FUN_SYS_JSON_QUERY: {
      for (int64_t i = 0; OB_SUCC(ret) && i < node.num_child_; ++i) {
        if (nullptr != node.children_[i]
            && OB_FAIL(resolve_safe_expr(*node.children_[i], target_column,
                                         all_columns, schema,
                                         allow_pipe_concat))) {
          LOG_WARN("failed to resolve AI column expression child", K(ret), K(i));
        }
      }
      break;
    }
    case T_NULL:
    case T_INT:
    case T_UINT64:
    case T_NUMBER:
    case T_FLOAT:
    case T_DOUBLE:
    case T_VARCHAR:
    case T_CHAR:
    case T_BOOL:
      break;
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("expression node is not allowed in AI column",
               K(ret), "node_type", node.type_);
      break;
  }
  return ret;
}

int resolve_options(const ParseNode *options, ObAIColumnSchema &schema)
{
  int ret = OB_SUCCESS;
  uint64_t seen = 0;
  if (nullptr == options) {
    // defaults only
  } else if (T_AI_COLUMN_OPTION_LIST != options->type_
             || nullptr == options->children_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI column option list", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < options->num_child_; ++i) {
      const ParseNode *option = options->children_[i];
      if (nullptr == option || T_AI_COLUMN_OPTION != option->type_
          || 2 != option->num_child_ || nullptr == option->children_
          || nullptr == option->children_[0] || nullptr == option->children_[1]) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid AI column option", K(ret), K(i));
      } else {
        const ObString name = node_string(*option->children_[0]);
        const ParseNode &value_node = *option->children_[1];
        uint64_t option_bit = 0;
        if (0 == name.case_compare("sync_mode")) {
          option_bit = 1L << 0;
          if (!is_string_literal(value_node)
              || 0 != node_string(value_node).case_compare("async")) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("only async AI column sync mode is supported", K(ret));
          }
        } else if (0 == name.case_compare("max_retries")) {
          option_bit = 1L << 1;
          if (!is_integer_literal(value_node)
              || value_node.value_ < 0 || value_node.value_ > MAX_RETRIES) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            schema.set_max_retries(value_node.value_);
          }
        } else {
          // Reject every unimplemented option instead of accepting a no-op.
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("unsupported AI column option", K(ret), K(name));
        }
        if (OB_SUCC(ret) && 0 != (seen & option_bit)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("duplicate AI column option", K(ret), K(name));
        } else {
          seen |= option_bit;
        }
      }
    }
  }
  return ret;
}

int validate_root_and_dependencies(
    const ParseNode &expr,
    const ObColumnSchemaV2 &column,
    const ObIArray<const ObColumnSchemaV2 *> &all_columns,
    ObAIColumnSchema &schema,
    ObString &model_name,
    bool &is_embed)
{
  int ret = OB_SUCCESS;
  is_embed = false;
  const ParseNode *params = nullptr;
  const bool allow_pipe_concat = contains_pipe_concat(node_string(expr));
  if (T_FUN_SYS != expr.type_ || expr.num_child_ < 2 || nullptr == expr.children_
      || nullptr == expr.children_[0] || nullptr == expr.children_[1]) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("AI column expression must be a supported AI function", K(ret));
  } else {
    const ObString function_name = node_string(*expr.children_[0]);
    is_embed = 0 == function_name.case_compare("ai_embed");
    if (!is_embed && 0 != function_name.case_compare("ai_complete")) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("unsupported AI column root function", K(ret), K(function_name));
    } else {
      params = expr.children_[1];
    }
  }
  if (OB_SUCC(ret)
      && (T_EXPR_LIST != params->type_ || nullptr == params->children_
          || (!is_embed && 2 != params->num_child_)
          || (is_embed && 2 != params->num_child_
              && 3 != params->num_child_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid AI column function arity", K(ret), K(is_embed));
  } else if (OB_SUCC(ret)
             && (nullptr == params->children_[0]
                 || !is_string_literal(*params->children_[0]))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI model name must be a string literal", K(ret));
  } else if (OB_SUCC(ret)) {
    model_name = node_string(*params->children_[0]);
    if (model_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("AI model name cannot be empty", K(ret));
    }
  }
  int64_t vector_dim = 0;
  if (OB_SUCC(ret) && is_embed && !column.is_collection()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI_EMBED requires a VECTOR result column", K(ret));
  } else if (OB_SUCC(ret) && is_embed
             && OB_FAIL(ObVectorIndexUtil::get_vector_dim_from_extend_type_info(
                            column.get_extended_type_info(), vector_dim))) {
    LOG_WARN("failed to read AI_EMBED target VECTOR dimension", K(ret), K(column));
  } else if (OB_SUCC(ret) && is_embed && 3 == params->num_child_
             && (nullptr == params->children_[2]
                 || !is_integer_literal(*params->children_[2])
                 || params->children_[2]->value_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI_EMBED dimension must be a positive integer literal", K(ret));
  } else if (OB_SUCC(ret) && is_embed && 3 == params->num_child_
             && params->children_[2]->value_ != vector_dim) {
    ret = OB_ERR_INVALID_VECTOR_DIM;
    LOG_USER_ERROR(OB_ERR_INVALID_VECTOR_DIM,
                   static_cast<uint32_t>(vector_dim),
                   static_cast<uint32_t>(params->children_[2]->value_));
    LOG_WARN("AI_EMBED dimension does not match target VECTOR",
             K(ret), K(vector_dim), "argument_dim", params->children_[2]->value_);
  } else if (OB_SUCC(ret) && !is_embed && column.is_collection()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI_COMPLETE cannot target a VECTOR column", K(ret));
  }
  for (int64_t i = 1; OB_SUCC(ret) && i < params->num_child_; ++i) {
    if (is_embed && 2 == i) {
      // dimension was validated above and is not an input dependency
    } else if (nullptr == params->children_[i]) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(resolve_safe_expr(*params->children_[i], column,
                                         all_columns, schema,
                                         allow_pipe_concat))) {
      LOG_WARN("failed to resolve AI function argument", K(ret), K(i));
    }
  }
  if (OB_SUCC(ret) && schema.get_dependency_column_ids().empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI column expression must reference at least one base column", K(ret));
  }
  return ret;
}

} // namespace

ObAIColumnResolver::ObAIColumnResolver(ObBasicSessionInfo &session_info,
                                       ObSchemaChecker &schema_checker)
  : session_info_(session_info),
    schema_checker_(schema_checker)
{
}

bool ObAIColumnResolver::is_ai_column_definition(const ParseNode *column_definition)
{
  return nullptr != column_definition
      && T_COLUMN_DEFINITION == column_definition->type_
      && 8 == column_definition->num_child_
      && nullptr != column_definition->children_
      && nullptr != column_definition->children_[7]
      && T_AI_COLUMN_DEFINITION == column_definition->children_[7]->type_;
}

int ObAIColumnResolver::resolve_definition(
    const ParseNode &column_definition,
    const uint64_t table_id,
    const ObColumnSchemaV2 &column,
    const ObIArray<const ObColumnSchemaV2 *> &all_columns,
    ObAIColumnSchema &ai_column_schema,
    ObString &model_name)
{
  int ret = OB_SUCCESS;
  const ParseNode *ai_definition = nullptr;
  const ParseNode *expr = nullptr;
  bool is_embed = false;
  ObSqlString normalized_expr;
  model_name.reset();
  ai_column_schema.reset();
  if (!is_ai_column_definition(&column_definition)
      || column.get_column_name_str().empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid AI column definition", K(ret), K(table_id), K(column));
  } else if (OB_ISNULL(ai_definition = column_definition.children_[7])
             || 2 != ai_definition->num_child_ || nullptr == ai_definition->children_
             || OB_ISNULL(expr = ai_definition->children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid AI column parse tree", K(ret));
  } else {
    const ObString expr_text = node_string(*expr);
    ai_column_schema.set_table_id(table_id);
    ai_column_schema.set_column_id(column.get_column_id());
    ai_column_schema.set_spec_version(1);
    ai_column_schema.set_max_retries(DEFAULT_MAX_RETRIES);
    if (OB_FAIL(ai_column_schema.set_column_name(column.get_column_name_str()))) {
      LOG_WARN("failed to save AI column name", K(ret));
    } else if (expr_text.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI column expression text is empty", K(ret));
    } else if (OB_FAIL(ai_column_schema.set_expr_text(expr_text))) {
      LOG_WARN("failed to save AI column expression", K(ret));
    } else if (OB_FAIL(validate_root_and_dependencies(
                   *expr, column, all_columns, ai_column_schema, model_name,
                   is_embed))) {
      LOG_WARN("failed to validate AI column expression", K(ret));
    } else if (OB_FAIL(build_normalized_expr(
                   expr_text, column, is_embed, normalized_expr))) {
      LOG_WARN("failed to build normalized AI column expression", K(ret));
    } else if (OB_FAIL(ai_column_schema.set_normalized_expr(
                   normalized_expr.string()))) {
      LOG_WARN("failed to save normalized AI column expression", K(ret));
    } else if (OB_FAIL(resolve_options(ai_definition->children_[1], ai_column_schema))) {
      LOG_WARN("failed to resolve AI column options", K(ret));
    }
  }
  return ret;
}

int ObAIColumnResolver::resolve(
    const ParseNode &column_definition,
    const uint64_t table_id,
    const ObColumnSchemaV2 &column,
    const ObIArray<const ObColumnSchemaV2 *> &all_columns,
    ObAIColumnSchema &ai_column_schema) const
{
  int ret = OB_SUCCESS;
  ObString model_name;
  const ObAiModelSchema *model_schema = nullptr;
  ObSchemaGetterGuard *schema_guard = schema_checker_.get_schema_guard();
  if (OB_FAIL(resolve_definition(column_definition, table_id, column,
                                 all_columns, ai_column_schema, model_name))) {
    LOG_WARN("failed to resolve AI column definition", K(ret));
  } else if (nullptr == schema_guard) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema guard is null", K(ret));
  } else if (OB_FAIL(schema_guard->get_ai_model_schema(model_name, model_schema))) {
    LOG_WARN("failed to resolve AI model", K(ret), K(model_name));
  } else if (nullptr == model_schema) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("resolved AI model schema is null", K(ret), K(model_name));
  } else {
    ObString time_zone;
    ObSQLMode ai_sql_mode = session_info_.get_sql_mode();
    if (contains_pipe_concat(ai_column_schema.get_normalized_expr())) {
      ai_sql_mode |= SMO_PIPES_AS_CONCAT;
    }
    ai_column_schema.set_sql_mode(ai_sql_mode);
    if (OB_FAIL(session_info_.get_sys_variable(SYS_VAR_TIME_ZONE, time_zone))) {
      LOG_WARN("failed to read session time zone", K(ret));
    } else if (OB_FAIL(ai_column_schema.set_time_zone(time_zone))) {
      LOG_WARN("failed to save session time zone", K(ret));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
