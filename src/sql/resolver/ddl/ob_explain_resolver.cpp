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
#include "sql/parser/ob_parser.h"
#include "sql/resolver/ob_resolver.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
const static int64_t EXPLAIN_FORMAT = 0;
const static int64_t EXPLAIN_DISPLAY_OPTION = 1;
const static int64_t EXPLAIN_INTO_TABLE = 2;
const static int64_t EXPLAIN_STMT_ID = 3;
const static int64_t EXPLAIN_CHILD_STMT = 4;
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
      } else if (NULL != parse_tree.children_[EXPLAIN_FORMAT]
                 && T_SHOW_COLUMNS == parse_tree.children_[EXPLAIN_FORMAT]->type_
                 && stmt::T_SELECT == child_stmt->get_stmt_type()) {
        // DESCRIBE <query>: emit the query's output schema instead of a plan
        if (OB_FAIL(resolve_describe_query(*static_cast<ObSelectStmt *>(child_stmt)))) {
          LOG_WARN("resolve describe query failed", K(ret));
        } else {
          return ret;
        }
      }
      if (OB_SUCC(ret)) {
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
          case T_SHOW_COLUMNS: {
            // marker of DESCRIBE/DESC; handled above
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

namespace oceanbase
{
namespace sql
{

// Map the result type of a select item to the type name DuckDB prints in
// DESCRIBE. File SQL columns are inferred as BIGINT / DOUBLE / BOOLEAN / DATE /
// DATETIME / VARCHAR, so keep the mapping aligned with that inference.
static const char *describe_type_name(const ObObjType type)
{
  const char *name = "VARCHAR";
  if (ObNullType == type) {
    // an all-NULL column keeps the NULL type instead of widening to VARCHAR
    name = "NULL";
  } else if (ObTinyIntType == type || ObUTinyIntType == type) {
    name = "BOOLEAN";
  } else if (ob_is_integer_type(type)) {
    name = "BIGINT";
  } else if (ObNumberType == type || ObDoubleType == type || ObFloatType == type
             || ObONumberFloatType == type) {
    name = "DOUBLE";
  } else if (ObMySQLDateType == type) {
    name = "DATE";
  } else if (ObMySQLDateTimeType == type || ObDateTimeType == type || ObTimestampType == type) {
    name = "DATETIME";
  } else if (ob_is_string_type(type) || ObJsonType == type || ObHexStringType == type) {
    name = "VARCHAR";
  }
  return name;
}

int ObExplainResolver::build_describe_sql(const ObSelectStmt &sel, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  const int64_t item_cnt = sel.get_select_item_size();
  if (OB_UNLIKELY(item_cnt <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("empty select item list for describe", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < item_cnt; ++i) {
    const SelectItem &item = sel.get_select_item(i);
    if (OB_ISNULL(item.expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null select expr", K(ret), K(i));
    } else {
      const ObExprResType &res_type = item.expr_->get_result_type();
      const char *type_name = describe_type_name(res_type.get_type());
      const bool not_null = res_type.has_result_flag(NOT_NULL_FLAG);
      if (i > 0 && OB_FAIL(sql.append(" union all "))) {
        LOG_WARN("append sql failed", K(ret));
      } else if (OB_FAIL(sql.append_fmt(
                   "select '%.*s' as column_name, '%s' as column_type, '%s' as `null`, "
                   "null as `key`, null as `default`, null as `extra`",
                   item.alias_name_.length(), item.alias_name_.ptr(), type_name,
                   not_null ? "NO" : "YES"))) {
        LOG_WARN("append describe item failed", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObExplainResolver::resolve_describe_query(ObSelectStmt &sel)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ParseResult parse_result;
  ObStmt *new_stmt = NULL;
  if (OB_ISNULL(params_.session_info_) || OB_ISNULL(params_.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid resolver params", K(ret));
  } else if (OB_FAIL(build_describe_sql(sel, sql))) {
    LOG_WARN("build describe sql failed", K(ret));
  } else if (OB_UNLIKELY(sql.empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObParser parser(*params_.allocator_, params_.session_info_->get_sql_mode());
    ParseNode *stmt_node = NULL;
    if (OB_FAIL(parser.parse(sql.string(), parse_result))) {
      LOG_WARN("parse describe sql failed", K(ret), K(sql));
    } else if (OB_ISNULL(parse_result.result_tree_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty parse tree for describe sql", K(ret));
    } else if (OB_ISNULL(parse_result.result_tree_->children_)
               || OB_UNLIKELY(parse_result.result_tree_->num_child_ != 1)
               || OB_ISNULL(stmt_node = parse_result.result_tree_->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("bad parse tree for describe sql", K(ret), K(sql));
    } else {
      ObResolver resolver(params_);
      if (OB_FAIL(resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT,
                                   *stmt_node,
                                   new_stmt))) {
        LOG_WARN("resolve describe sql failed", K(ret), K(sql));
      } else if (OB_ISNULL(new_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null stmt for describe sql", K(ret));
      } else {
        stmt_ = new_stmt;
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
