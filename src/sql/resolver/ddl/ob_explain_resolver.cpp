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
#include "sql/resolver/ddl/ob_explain_resolver.h"
#include "sql/resolver/ob_resolver.h"
#include "sql/parser/parse_malloc.h"
#include "sql/parser/sql_parser_base.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
const static int64_t EXPLAIN_FORMAT = 0;
const static int64_t EXPLAIN_DISPLAY_OPTION = 1;
const static int64_t EXPLAIN_INTO_TABLE = 2;
const static int64_t EXPLAIN_STMT_ID = 3;
const static int64_t EXPLAIN_CHILD_STMT = 4;

namespace
{
// The columns of the describe result of a file table, which follows the style of
// duckdb: column_name, column_type, null, key, default, extra.
const char *const FILE_TABLE_DESCRIBE_COLUMNS[] = {"column_name", "column_type", "null",
                                                   "key", "default", "extra"};
const int64_t FILE_TABLE_DESCRIBE_COLUMN_COUNT = 6;

ParseNode *find_file_table_node(ParseNode *node)
{
  if (OB_ISNULL(node)) {
    return NULL;
  }
  if (T_FILE_TABLE_EXPRESSION == node->type_) {
    return node;
  }
  for (int32_t i = 0; i < node->num_child_; ++i) {
    ParseNode *found = find_file_table_node(node->children_[i]);
    if (OB_NOT_NULL(found)) {
      return found;
    }
  }
  return NULL;
}

const char *file_table_default_alias(int64_t func_type, int32_t &alias_len)
{
  switch (func_type) {
    case 1:
      alias_len = 8;
      return "read_csv";
    case 2:
      alias_len = 9;
      return "read_json";
    case 3:
      alias_len = 12;
      return "read_parquet";
    default:
      alias_len = 4;
      return "glob";
  }
}
} // namespace

int ObExplainResolver::build_file_table_describe_stmt(const ParseNode &parse_tree,
                                                      common::ObIAllocator &allocator,
                                                      ParseNode *&describe_stmt_tree)
{
  int ret = OB_SUCCESS;
  describe_stmt_tree = NULL;
  if (T_EXPLAIN != parse_tree.type_ || 5 != parse_tree.num_child_) {
    return OB_SUCCESS;
  }
  ParseNode *select_node = parse_tree.children_[EXPLAIN_CHILD_STMT];
  if (OB_ISNULL(select_node) || T_SELECT != select_node->type_) {
    return OB_SUCCESS;
  }
  ParseNode *file_node = find_file_table_node(select_node);
  if (OB_ISNULL(file_node)) {
    // it does not describe a file table, keep the normal explain behavior
    return OB_SUCCESS;
  }
  // the alias name of the file table, it is used as the table name of the
  // column references of the describe result
  const char *alias = NULL;
  int32_t alias_len = 0;
  ParseNode *alias_node = (file_node->num_child_ > 2) ? file_node->children_[2] : NULL;
  if (OB_NOT_NULL(alias_node) && OB_NOT_NULL(alias_node->str_value_) && alias_node->str_len_ > 0) {
    alias = alias_node->str_value_;
    alias_len = static_cast<int32_t>(alias_node->str_len_);
  } else {
    alias = file_table_default_alias(file_node->value_, alias_len);
  }
  // rebuild the select items of the statement
  ParseNode *items[FILE_TABLE_DESCRIBE_COLUMN_COUNT];
  for (int64_t i = 0; i < FILE_TABLE_DESCRIBE_COLUMN_COUNT; ++i) {
    items[i] = NULL;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < FILE_TABLE_DESCRIBE_COLUMN_COUNT; ++i) {
    ParseNode *table_node = new_terminal_node(&allocator, T_IDENT);
    ParseNode *column_node = new_terminal_node(&allocator, T_IDENT);
    if (OB_ISNULL(table_node) || OB_ISNULL(column_node)) {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    table_node->str_value_ = parse_strdup(alias, &allocator, &table_node->str_len_);
    column_node->str_value_ = parse_strdup(FILE_TABLE_DESCRIBE_COLUMNS[i], &allocator,
                                           &column_node->str_len_);
    if (OB_ISNULL(table_node->str_value_) || OB_ISNULL(column_node->str_value_)) {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    items[i] = new_non_terminal_node(&allocator, T_COLUMN_REF, 3, table_node, (ParseNode *)NULL, column_node);
    if (OB_ISNULL(items[i])) {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }
  if (OB_SUCC(ret)) {
    ParseNode *project_list = new_non_terminal_node(&allocator, T_PROJECT_LIST,
                                                    FILE_TABLE_DESCRIBE_COLUMN_COUNT,
                                                    items[0], items[1], items[2],
                                                    items[3], items[4], items[5]);
    if (OB_ISNULL(project_list)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      select_node->children_[PARSE_SELECT_SELECT] = project_list;
      // mark the file table, so that it outputs the column metadata
      ParseNode *describe_flag = new_terminal_node(&allocator, T_INT);
      if (OB_ISNULL(describe_flag)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        describe_flag->value_ = 1;
        file_node->children_[1] = describe_flag;
        describe_stmt_tree = select_node;
      }
    }
  }
  return ret;
}

int ObExplainResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObExplainStmt *explain_stmt = NULL;
  if (!(T_EXPLAIN == parse_tree.type_  && 5 == parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid EXPLAIN syntax",
             "type", parse_tree.type_,
             "# of child", parse_tree.num_child_);
  } else if (NULL == (explain_stmt = create_stmt<ObExplainStmt>())) {
    ret = OB_SQL_RESOLVER_NO_MEMORY;
    LOG_WARN("failed to create explain stmt");
  } else {
    ParseNode *child_node = parse_tree.children_[EXPLAIN_CHILD_STMT];
    ObStmt *child_stmt = NULL;
    ObExplainDisplayOpt opt;
    opt.with_tree_line_ = true;
    if (OB_ISNULL(child_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty parse tree for stmt");
    } else if (!(T_SELECT == child_node->type_
                 || T_DELETE == child_node->type_
                 || T_INSERT == child_node->type_
                 || T_UPDATE == child_node->type_
                 || T_REPLACE == child_node->type_)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("invalid stmt type for EXPLAIN", "type", child_node->type_);
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "stmt type for EXPLAIN");
    } else {
      ObResolver resolver(params_);
      if (OB_FAIL(resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT,
                                   *child_node,
                                   child_stmt))) {
      } else if (OB_ISNULL(child_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to generate stmt for child stmt");
      } else {
        explain_stmt->set_explain_query_stmt(static_cast<ObDMLStmt *>(child_stmt));
        ParseNode *format = parse_tree.children_[EXPLAIN_FORMAT];
        if (NULL != format) {
          switch (format->type_) {
          case T_OUTLINE:
          {
            explain_stmt->set_explain_format(EXPLAIN_OUTLINE);
            opt.with_tree_line_ = true;
          } break;
          case T_EXTENDED:
          {
            explain_stmt->set_explain_format(EXPLAIN_EXTENDED);
            opt.with_tree_line_ = true;
          } break;
          case T_PARTITIONS:
          {
            explain_stmt->set_explain_format(EXPLAIN_PARTITIONS);
            opt.with_tree_line_ = true;
          } break;
          case T_TRADITIONAL:
          {
            explain_stmt->set_explain_format(EXPLAIN_TRADITIONAL);
            opt.with_tree_line_ = true;
          } break;
          case T_FORMAT_JSON:
          {
            explain_stmt->set_explain_format(EXPLAIN_FORMAT_JSON);
            opt.with_tree_line_ = false;
          } break;
          case T_BASIC: {
            explain_stmt->set_explain_format(EXPLAIN_BASIC);
            opt.with_tree_line_ = true;
            break;
          }
          case T_EXTENDED_NOADDR: {
            explain_stmt->set_explain_format(EXPLAIN_EXTENDED_NOADDR);
            opt.with_tree_line_ = true;
            break;
          }
          case T_PLANREGRESS: {
            explain_stmt->set_explain_format(EXPLAIN_PLANREGRESS);
            opt.with_tree_line_ = true;
            break;
          }
          default:
            ret = OB_ERROR;
            LOG_WARN("unknown explain format", "format", format->type_);
            break;
          }
        }
      }
    }
    ParseNode *opt_node = parse_tree.children_[EXPLAIN_DISPLAY_OPTION];
    if (OB_SUCC(ret) && NULL != opt_node) {
      switch (opt_node->type_) {
      case T_PRETTY_COLOR:
        opt.with_color_ = true;
        // pass
      case T_PRETTY:
        opt.with_tree_line_ = true;
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret)) {
      explain_stmt->set_display_opt(opt);
    }
    ParseNode *into_table_node = parse_tree.children_[EXPLAIN_INTO_TABLE];
    if (OB_SUCC(ret) && NULL != into_table_node) {
      ObString into_table(into_table_node->str_len_, into_table_node->str_value_);
      explain_stmt->set_into_table(into_table);
    }
    ParseNode *statement_id_node = parse_tree.children_[EXPLAIN_STMT_ID];
    if (OB_SUCC(ret) && NULL != statement_id_node) {
      ObString statement_id(statement_id_node->str_len_, statement_id_node->str_value_);
      explain_stmt->set_statement_id(statement_id);
    }
  }

  return ret;
}
