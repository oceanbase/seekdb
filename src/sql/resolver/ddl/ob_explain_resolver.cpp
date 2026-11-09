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
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/ob_resolver.h"
#include "sql/resolver/ob_resolver_utils.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
const static int64_t EXPLAIN_FORMAT = 0;
const static int64_t EXPLAIN_DISPLAY_OPTION = 1;
const static int64_t EXPLAIN_INTO_TABLE = 2;
const static int64_t EXPLAIN_STMT_ID = 3;
const static int64_t EXPLAIN_CHILD_STMT = 4;
const static int64_t DESCRIBE_MARKER_VALUE = 1;
const static int64_t DESCRIBE_COLUMN_CNT = 6;

static const char *const DESCRIBE_COLUMN_NAMES[DESCRIBE_COLUMN_CNT] = {
    "column_name", "column_type", "null", "key", "default", "extra"};

static const char *describe_type_name(const ObRawExprResType &type)
{
  const char *name = NULL;
  switch (type.get_type()) {
    case ObTinyIntType:
      name = (1 == type.get_accuracy().get_precision()) ? "BOOLEAN" : "TINYINT";
      break;
    case ObSmallIntType:
      name = "SMALLINT";
      break;
    case ObMediumIntType:
      name = "MEDIUMINT";
      break;
    case ObInt32Type:
      name = "INT";
      break;
    case ObIntType:
      name = "BIGINT";
      break;
    case ObUTinyIntType:
      name = "TINYINT UNSIGNED";
      break;
    case ObUSmallIntType:
      name = "SMALLINT UNSIGNED";
      break;
    case ObUMediumIntType:
      name = "MEDIUMINT UNSIGNED";
      break;
    case ObUInt32Type:
      name = "INT UNSIGNED";
      break;
    case ObUInt64Type:
      name = "BIGINT UNSIGNED";
      break;
    case ObFloatType:
      name = "FLOAT";
      break;
    case ObDoubleType:
      name = "DOUBLE";
      break;
    case ObNumberType:
    case ObDecimalIntType:
      name = "DECIMAL";
      break;
    case ObVarcharType:
      name = "VARCHAR";
      break;
    case ObCharType:
      name = "CHAR";
      break;
    case ObDateType:
      name = "DATE";
      break;
    case ObDateTimeType:
      name = "DATETIME";
      break;
    case ObTimestampType:
      name = "TIMESTAMP";
      break;
    case ObTimeType:
      name = "TIME";
      break;
    case ObYearType:
      name = "YEAR";
      break;
    default:
      name = ob_sql_type_str(type.get_type());
      break;
  }
  return NULL == name ? "UNKNOWN" : name;
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
  } else if (OB_NOT_NULL(parse_tree.children_) &&
             OB_NOT_NULL(parse_tree.children_[EXPLAIN_FORMAT]) &&
             T_INT == parse_tree.children_[EXPLAIN_FORMAT]->type_ &&
             DESCRIBE_MARKER_VALUE == parse_tree.children_[EXPLAIN_FORMAT]->value_) {
    // DESCRIBE <select statement> is rewritten into a metadata query.
    if (OB_ISNULL(parse_tree.children_[EXPLAIN_CHILD_STMT])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty parse tree for DESCRIBE", K(ret));
    } else {
      ret = resolve_describe_select(*parse_tree.children_[EXPLAIN_CHILD_STMT]);
    }
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

int ObExplainResolver::resolve_describe_select(const ParseNode &child_node)
{
  int ret = OB_SUCCESS;
  ObStmt *child_stmt = NULL;
  ObSelectStmt *described_stmt = NULL;
  ObSelectStmt *result_stmt = NULL;
  if (T_SELECT != child_node.type_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("DESCRIBE only supports select statement", K(ret), "type", child_node.type_);
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "DESCRIBE non-select statement");
  } else {
    ObResolver resolver(params_);
    if (OB_FAIL(resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT, child_node, child_stmt))) {
      LOG_WARN("failed to resolve the described statement", K(ret));
    } else if (OB_ISNULL(child_stmt) || !child_stmt->is_select_stmt()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid described statement", K(ret));
    } else if (OB_ISNULL(described_stmt = static_cast<ObSelectStmt *>(child_stmt))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(result_stmt = create_stmt<ObSelectStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to create describe result stmt", K(ret));
    } else if (OB_FAIL(build_describe_stmt(*described_stmt, *result_stmt))) {
      LOG_WARN("failed to build describe result stmt", K(ret));
    }
  }
  return ret;
}

int ObExplainResolver::make_describe_string(const ObString &src, ObObjParam &obj)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (src.empty()) {
    obj.set_string(ObVarcharType, ObString());
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator_->alloc(src.length())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, src.ptr(), src.length());
    obj.set_string(ObVarcharType, ObString(static_cast<int32_t>(src.length()), buf));
  }
  if (OB_SUCC(ret)) {
    obj.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    obj.set_accuracy(ObAccuracy(src.length()));
    obj.set_param_meta();
  }
  return ret;
}

int ObExplainResolver::build_describe_stmt(const ObSelectStmt &described_stmt,
                                           ObSelectStmt &result_stmt)
{
  int ret = OB_SUCCESS;
  ObValuesTableDef *table_def = NULL;
  void *buffer = NULL;
  const ObIArray<SelectItem> &select_items = described_stmt.get_select_items();
  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(buffer = allocator_->alloc(sizeof(ObValuesTableDef)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    table_def = new (buffer) ObValuesTableDef();
    table_def->column_cnt_ = DESCRIBE_COLUMN_CNT;
    table_def->row_cnt_ = select_items.count();
    table_def->access_type_ = ObValuesTableDef::ACCESS_OBJ;
    table_def->is_const_ = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < DESCRIBE_COLUMN_CNT; ++i) {
      ObRawExprResType res_type;
      res_type.set_type(ObVarcharType);
      res_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      res_type.set_collation_level(CS_LEVEL_IMPLICIT);
      res_type.set_length(512);
      res_type.set_accuracy(ObAccuracy(512));
      if (OB_FAIL(table_def->column_types_.push_back(res_type))) {
        LOG_WARN("failed to push describe column type", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < select_items.count(); ++i) {
      const SelectItem &item = select_items.at(i);
      const ObRawExprResType *res_type = NULL;
      if (OB_ISNULL(item.expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("select item expr is null", K(ret));
      } else {
        res_type = &item.expr_->get_result_type();
        ObString column_name = item.alias_name_.empty() ? item.expr_name_ : item.alias_name_;
        ObString column_type = ObString::make_string(describe_type_name(*res_type));
        bool nullable = !res_type->has_result_flag(NOT_NULL_FLAG);
        ObString null_flag = nullable ? ObString::make_string("YES") : ObString::make_string("NO");
        ObObjParam values[DESCRIBE_COLUMN_CNT];
        if (OB_FAIL(make_describe_string(column_name, values[0]))) {
        } else if (OB_FAIL(make_describe_string(column_type, values[1]))) {
        } else if (OB_FAIL(make_describe_string(null_flag, values[2]))) {
        } else {
          values[3].set_null();
          values[4].set_null();
          values[5].set_null();
          for (int64_t j = 0; OB_SUCC(ret) && j < DESCRIBE_COLUMN_CNT; ++j) {
            if (OB_FAIL(table_def->access_objs_.push_back(values[j]))) {
              LOG_WARN("failed to push describe value", K(ret));
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObResolverUtils::create_values_table_query(session_info_, allocator_,
                                                                params_.expr_factory_,
                                                                params_.query_ctx_,
                                                                &result_stmt, table_def))) {
    LOG_WARN("failed to create describe values table query", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < DESCRIBE_COLUMN_CNT; ++i) {
      ColumnItem *column_item = result_stmt.get_column_item(i);
      if (OB_ISNULL(column_item) || OB_ISNULL(column_item->expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("describe column item is null", K(ret));
      } else {
        ObColumnRefRawExpr *column_expr = column_item->expr_;
        ObString column_name = ObString::make_string(DESCRIBE_COLUMN_NAMES[i]);
        column_expr->set_column_attr(column_expr->get_table_name(), column_name);
        column_item->column_name_ = column_expr->get_column_name();
        SelectItem &select_item = result_stmt.get_select_items().at(i);
        select_item.alias_name_ = column_expr->get_column_name();
        select_item.expr_name_ = column_expr->get_column_name();
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(result_stmt.formalize_stmt(session_info_))) {
      LOG_WARN("failed to formalize describe stmt", K(ret));
    }
  }
  return ret;
}
