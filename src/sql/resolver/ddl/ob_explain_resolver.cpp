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
#include "sql/parser/ob_parser.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
const static int64_t EXPLAIN_FORMAT = 0;
const static int64_t EXPLAIN_DISPLAY_OPTION = 1;
const static int64_t EXPLAIN_INTO_TABLE = 2;
const static int64_t EXPLAIN_STMT_ID = 3;
const static int64_t EXPLAIN_CHILD_STMT = 4;

static int append_file_metadata_text(ObSqlString &sql, const ObString &value)
{
  if (value.empty()) return sql.append("CONVERT('' USING utf8mb4)");
  int ret = sql.append("CONVERT(0x");
  const char digits[] = "0123456789abcdef";
  for (int64_t i = 0; OB_SUCC(ret) && i < value.length(); ++i) {
    const unsigned char byte = value.ptr()[i];
    const char encoded[] = {digits[byte >> 4], digits[byte & 15]};
    ret = sql.append(ObString(2, encoded));
  }
  if (OB_SUCC(ret)) ret = sql.append(" USING utf8mb4)");
  return ret;
}

static const char *file_metadata_type(ObObjType type)
{
  switch (type) {
    case ObNullType: return "NULL";
    case ObIntType: return "BIGINT";
    case ObUInt64Type: return "UBIGINT";
    case ObDoubleType: return "DOUBLE";
    case ObTinyIntType: return "BOOLEAN";
    case ObVarcharType: return "VARCHAR";
    case ObDateType: return "DATE";
    case ObDateTimeType: return "DATETIME";
    case ObNumberType: case ObUNumberType: case ObDecimalIntType: return "DECIMAL";
    default: return ob_obj_type_str(type);
  }
}

static int resolve_file_metadata(ObResolverParams &params, ObSelectStmt &source, ObStmt *&result)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  OZ (sql.append("SELECT column_name,column_type,`null`,`key`,`default`,extra FROM ("));
  for (int64_t i = 0; OB_SUCC(ret) && i < source.get_select_item_size(); ++i) {
    const SelectItem &item = source.get_select_item(i);
    if (item.expr_ == nullptr) return OB_ERR_UNEXPECTED;
    const bool not_null = item.expr_->get_result_type().has_result_flag(NOT_NULL_FLAG);
    OZ (sql.append(i == 0 ? "SELECT " : " UNION ALL SELECT "));
    OZ (sql.append_fmt("%ld AS __file_sql_pos,", i));
    OZ (append_file_metadata_text(sql, item.alias_name_.empty() ? item.expr_name_ : item.alias_name_));
    OZ (sql.append(" AS column_name,"));
    OZ (append_file_metadata_text(sql, ObString(file_metadata_type(item.expr_->get_data_type()))));
    OZ (sql.append(" AS column_type,"));
    OZ (append_file_metadata_text(sql, ObString(not_null ? "NO" : "YES")));
    OZ (sql.append(" AS `null`,NULL AS `key`,NULL AS `default`,NULL AS extra"));
    if (sql.length() > 16 * 1024 * 1024) return OB_SIZE_OVERFLOW;
  }
  OZ (sql.append(") __file_sql_metadata ORDER BY __file_sql_pos"));
  if (OB_SUCC(ret)) {
    ParseResult parsed;
    ObParser parser(*params.allocator_, params.session_info_->get_sql_mode());
    if (OB_FAIL(parser.parse(sql.string(), parsed))) {
    } else if (parsed.result_tree_ == nullptr || parsed.result_tree_->num_child_ != 1 ||
               parsed.result_tree_->children_ == nullptr || parsed.result_tree_->children_[0] == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObResolver resolver(params);
      ret = resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT, *parsed.result_tree_->children_[0], result);
    }
  }
  return ret;
}

static bool is_file_sql_describe(const ParseNode &parse_tree)
{
  const ParseNode *marker = parse_tree.children_[EXPLAIN_FORMAT];
  return marker != nullptr && T_INT == marker->type_ && 1 == marker->value_;
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
      } else if (is_file_sql_describe(parse_tree) && child_stmt->is_select_stmt() &&
                 static_cast<ObSelectStmt *>(child_stmt)->get_query_ctx()->has_file_sql_) {
        ObStmt *metadata = nullptr;
        ret = resolve_file_metadata(params_, *static_cast<ObSelectStmt *>(child_stmt), metadata);
        if (OB_SUCC(ret)) set_basic_stmt(metadata);
        return ret;
      } else {
        explain_stmt->set_explain_query_stmt(static_cast<ObDMLStmt *>(child_stmt));
        ParseNode *format = parse_tree.children_[EXPLAIN_FORMAT];
        if (is_file_sql_describe(parse_tree)) {
          // Plain DESCRIBE SELECT marker: no display format was requested.
          format = NULL;
        }
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
