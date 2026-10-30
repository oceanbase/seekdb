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
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "share/system_variable/ob_sys_var_class_type.h"
#include "sql/parser/ob_parser.h"
#include "lib/string/ob_sql_string.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
const static int64_t EXPLAIN_FORMAT = 0;
const static int64_t EXPLAIN_DISPLAY_OPTION = 1;
const static int64_t EXPLAIN_INTO_TABLE = 2;
const static int64_t EXPLAIN_STMT_ID = 3;
const static int64_t EXPLAIN_CHILD_STMT = 4;

namespace {

bool is_describe_file_sql(const ParseNode &parse_tree, ObStmt &child_stmt)
{
  bool bret = false;
  const ParseNode *display_node = parse_tree.children_[EXPLAIN_DISPLAY_OPTION];
  if (NULL != display_node && T_DESCRIBE_COLUMNS == display_node->type_
      && NULL == parse_tree.children_[EXPLAIN_FORMAT]
      && NULL == parse_tree.children_[EXPLAIN_INTO_TABLE]
      && NULL == parse_tree.children_[EXPLAIN_STMT_ID]
      && stmt::T_SELECT == child_stmt.get_stmt_type()) {
    ObSelectStmt &select_stmt = static_cast<ObSelectStmt &>(child_stmt);
    const ObIArray<TableItem *> &table_items = select_stmt.get_table_items();
    for (int64_t i = 0; !bret && i < table_items.count(); ++i) {
      const TableItem *table_item = table_items.at(i);
      if (NULL != table_item && table_item->is_values_table()
          && NULL != table_item->values_table_def_
          && table_item->values_table_def_->column_names_.count() > 0) {
        bret = true;
      }
    }
  }
  return bret;
}

const char *file_sql_metadata_type_name(const ObObjType type)
{
  const char *name = "VARCHAR";
  switch (type) {
    case ObNullType:
      name = "NULL";
      break;
    case ObIntType:
    case ObInt32Type:
      name = "BIGINT";
      break;
    case ObTinyIntType:
      name = "BOOLEAN";
      break;
    case ObFloatType:
    case ObDoubleType:
    case ObNumberType:
      name = "DOUBLE";
      break;
    case ObDateType:
      name = "DATE";
      break;
    case ObDateTimeType:
      name = "DATETIME";
      break;
    default:
      name = "VARCHAR";
      break;
  }
  return name;
}

void file_sql_make_varchar(ObObj &obj, const ObString &value)
{
  obj.set_varchar(value);
  obj.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  obj.set_collation_level(CS_LEVEL_IMPLICIT);
}

}  // namespace

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
      } else if (is_describe_file_sql(parse_tree, *child_stmt)) {
        ObStmt *meta_stmt = NULL;
        if (OB_FAIL(resolve_file_sql_describe(static_cast<ObSelectStmt &>(*child_stmt),
                                             meta_stmt))) {
          LOG_INFO("failed to describe file sql", K(ret));
        } else if (OB_ISNULL(meta_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("null describe stmt", K(ret));
        } else {
          // DESCRIBE over a local file behaves like a plain SELECT of the metadata
          stmt_ = meta_stmt;
        }
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
      case T_DESCRIBE_COLUMNS:
        // DESCRIBE over a local file: the display option carries the marker only
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

int ObExplainResolver::resolve_file_sql_describe(ObSelectStmt &inner_stmt, ObStmt *&out_stmt)
{
  int ret = OB_SUCCESS;
  const ObValuesTableDef *file_def = NULL;
  ObSqlString sql;
  out_stmt = NULL;
  const ObIArray<TableItem *> &table_items = inner_stmt.get_table_items();
  for (int64_t i = 0; OB_SUCC(ret) && NULL == file_def && i < table_items.count(); ++i) {
    const TableItem *table_item = table_items.at(i);
    if (NULL != table_item && table_item->is_values_table()
        && NULL != table_item->values_table_def_
        && table_item->values_table_def_->column_names_.count() > 0) {
      file_def = table_item->values_table_def_;
    }
  }
  if (OB_ISNULL(file_def) || OB_UNLIKELY(file_def->column_types_.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no file table column to describe", K(ret));
  } else if (OB_FAIL(sql.append("select column_0 as column_name, column_1 as column_type, "
                                "column_2 as `null`, column_3 as `key`, column_4 as `default`, "
                                "column_5 as extra from (values "))) {
    LOG_WARN("failed to build describe sql", K(ret));
  } else {
    const ObIArray<SelectItem> &inner_select_items = inner_stmt.get_select_items();
    for (int64_t r = 0; OB_SUCC(ret) && r < inner_select_items.count(); ++r) {
      const SelectItem &inner_item = inner_select_items.at(r);
      ObString col_name = inner_item.alias_name_;
      if (col_name.empty()) {
        col_name = inner_item.expr_name_;
      }
      int64_t col_idx = -1;
      for (int64_t j = 0; -1 == col_idx && j < file_def->column_names_.count(); ++j) {
        if (file_def->column_names_.at(j).case_compare_equal(col_name)) {
          col_idx = j;
        }
      }
      if (-1 == col_idx) {
        col_idx = r < file_def->column_types_.count() ? r : 0;
      }
      if (OB_UNLIKELY(col_idx >= file_def->column_types_.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column index out of range", K(col_idx), K(ret));
      } else {
        const char *type_name =
            file_sql_metadata_type_name(file_def->column_types_.at(col_idx).get_type());
        const bool is_nullable = file_def->column_nullables_.count() > col_idx
                                 && 0 != file_def->column_nullables_.at(col_idx);
        const ObString &src_name = file_def->column_names_.at(col_idx);
        ObSqlString escaped_name;
        for (int64_t k = 0; OB_SUCC(ret) && k < src_name.length(); ++k) {
          const char c = src_name.ptr()[k];
          if ('\'' == c) {
            ret = escaped_name.append("''");
          } else {
            ret = escaped_name.append_fmt("%c", c);
          }
        }
        if (OB_SUCC(ret)) {
          ret = sql.append_fmt("%srow('%.*s','%s','%s',NULL,NULL,NULL)",
                               r > 0 ? "," : "",
                               static_cast<int>(escaped_name.length()), escaped_name.ptr(),
                               type_name, is_nullable ? "YES" : "NO");
        }
      }
    }
    if (OB_SUCC(ret)) {
      ret = sql.append(") v");
    }
  }
  if (OB_SUCC(ret)) {
    ObParser parser(*allocator_, session_info_->get_sql_mode(), session_info_->get_charsets4parser());
    ParseResult parse_result;
    if (OB_FAIL(parser.parse(sql.string(), parse_result))) {
      LOG_WARN("failed to parse describe sql", K(ret), K(sql));
    } else if (OB_ISNULL(parse_result.result_tree_)
               || OB_UNLIKELY(parse_result.result_tree_->num_child_ < 1)
               || OB_ISNULL(parse_result.result_tree_->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected describe parse result", K(ret), K(sql));
    } else {
      ObResolver resolver(params_);
      if (OB_FAIL(resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT,
                                   *parse_result.result_tree_->children_[0], out_stmt))) {
        LOG_WARN("failed to resolve describe sql", K(ret), K(sql));
      }
    }
  }
  return ret;
}
