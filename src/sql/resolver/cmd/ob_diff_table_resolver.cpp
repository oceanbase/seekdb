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
#include "sql/resolver/cmd/ob_diff_table_resolver.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/parser/ob_parser.h"
#include "share/schema/ob_column_schema.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObDiffTableResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(session_info_) ||
      OB_ISNULL(schema_checker_) ||
      OB_ISNULL(params_.allocator_) ||
      T_DIFF_TABLE != parse_tree.type_ ||
      DIFF_TABLE_NODE_COUNT != parse_tree.num_child_ ||
      OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree for DIFF TABLE", K(ret));
  }

  ObString cur_table_name, cur_db_name, inc_table_name, inc_db_name;
  const ObTableSchema *cur_schema = NULL;
  const ObTableSchema *inc_schema = NULL;
  ObSEArray<ObString, 8> pk_cols;
  ObSEArray<ObString, 16> val_cols;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(resolve_table_names_(parse_tree, cur_table_name, cur_db_name,
                                           inc_table_name, inc_db_name))) {
    LOG_WARN("failed to resolve table names", K(ret));
  } else if (OB_FAIL(get_table_schemas_(session_info_->get_effective_tenant_id(),
                                         cur_db_name, cur_table_name,
                                         inc_db_name, inc_table_name,
                                         cur_schema, inc_schema))) {
    LOG_WARN("failed to get table schemas", K(ret));
  } else if (OB_FAIL(collect_and_validate_columns_(cur_schema, inc_schema, pk_cols, val_cols))) {
    LOG_WARN("failed to collect and validate columns", K(ret));
  }

  ObSqlString diff_sql;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(build_diff_sql_(cur_db_name, cur_table_name,
                                inc_db_name, inc_table_name,
                                pk_cols, val_cols, diff_sql))) {
      LOG_WARN("failed to build diff sql", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("DIFF TABLE generated SQL", "sql", diff_sql.string());
    if (OB_FAIL(parse_and_resolve_select_sql(diff_sql.string()))) {
      LOG_WARN("failed to parse and resolve diff sql", K(ret), K(diff_sql));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(stmt_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("stmt is null after resolve", K(ret));
    } else if (OB_UNLIKELY(stmt::T_SELECT != stmt_->get_stmt_type())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected stmt type after diff resolve", K(ret), K(stmt_->get_stmt_type()));
    } else {
      ObSelectStmt *select_stmt = static_cast<ObSelectStmt*>(stmt_);
      if (OB_ISNULL(select_stmt->get_query_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("query ctx is null after diff resolve", K(ret));
      } else {
        select_stmt->get_query_ctx()->set_literal_stmt_type(stmt::T_DIFF_TABLE);
      }
    }
  }

  return ret;
}

int ObDiffTableResolver::resolve_table_names_(const ParseNode &parse_tree,
                                               ObString &cur_table_name, ObString &cur_db_name,
                                               ObString &inc_table_name, ObString &inc_db_name)
{
  int ret = OB_SUCCESS;
  ParseNode *cur_node = parse_tree.children_[CURRENT_TABLE_NODE];
  ParseNode *inc_node = parse_tree.children_[INCOMING_TABLE_NODE];
  if (OB_ISNULL(cur_node) || OB_ISNULL(inc_node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table node is NULL", K(ret));
  } else if (OB_FAIL(resolve_table_relation_node(cur_node, cur_table_name, cur_db_name))) {
    LOG_WARN("failed to resolve current table", K(ret));
  } else if (OB_FAIL(resolve_table_relation_node(inc_node, inc_table_name, inc_db_name))) {
    LOG_WARN("failed to resolve incoming table", K(ret));
  }
  return ret;
}

int ObDiffTableResolver::get_table_schemas_(const uint64_t tenant_id,
                                             const ObString &cur_db_name, const ObString &cur_table_name,
                                             const ObString &inc_db_name, const ObString &inc_table_name,
                                             const ObTableSchema *&cur_schema,
                                             const ObTableSchema *&inc_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_checker_ is null", K(ret));
  } else if (OB_FAIL(schema_checker_->get_table_schema(tenant_id, cur_db_name, cur_table_name,
                                                 false, cur_schema))) {
    LOG_WARN("failed to get current table schema", K(ret), K(cur_db_name), K(cur_table_name));
  } else if (OB_ISNULL(cur_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("current table not exist", K(ret), K(cur_db_name), K(cur_table_name));
  } else if (OB_FAIL(schema_checker_->get_table_schema(tenant_id, inc_db_name, inc_table_name,
                                                        false, inc_schema))) {
    LOG_WARN("failed to get incoming table schema", K(ret), K(inc_db_name), K(inc_table_name));
  } else if (OB_ISNULL(inc_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("incoming table not exist", K(ret), K(inc_db_name), K(inc_table_name));
  }
  return ret;
}

int ObDiffTableResolver::collect_and_validate_columns_(const ObTableSchema *cur_schema,
                                                        const ObTableSchema *inc_schema,
                                                        ObIArray<ObString> &pk_cols,
                                                        ObIArray<ObString> &val_cols)
{
  return ObResolverUtils::collect_and_validate_columns(cur_schema, inc_schema,
                                                       pk_cols, val_cols, "DIFF TABLE");
}

int ObDiffTableResolver::build_diff_sql_(const ObString &cur_db_name, const ObString &cur_table_name,
                                          const ObString &inc_db_name, const ObString &inc_table_name,
                                          const ObIArray<ObString> &pk_cols,
                                          const ObIArray<ObString> &val_cols,
                                          ObSqlString &diff_sql)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(pk_cols.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pk_cols should not be empty", K(ret));
    return ret;
  }
  const bool has_val_cols = (val_cols.count() > 0);
  const ObString &pk1 = pk_cols.at(0);

  ObSqlString pk_eq;
  ObSqlString val_cmp;
  if (OB_FAIL(ObResolverUtils::append_binary_cond(pk_eq, pk_cols, "="))) {
    LOG_WARN("failed to build pk join condition", K(ret));
  } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_binary_cond(val_cmp, val_cols, "<=>"))) {
    LOG_WARN("failed to build value comparison", K(ret));
  }

  // SELECT header
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append("SELECT `__table`, `__flag`, "))) {
      LOG_WARN("failed to append select header", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, ""))) {
      LOG_WARN("failed to append pk cols to header", K(ret));
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "", true))) {
      LOG_WARN("failed to append val cols to header", K(ret));
    } else if (OB_FAIL(diff_sql.append(" FROM ("))) {
      LOG_WARN("failed to append FROM clause", K(ret));
    }
  }

  // Branch 1: current-only rows (LEFT JOIN, WHERE incoming IS NULL)
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append("SELECT "))) {
      LOG_WARN("failed to append branch1 select prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                   diff_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append branch1 table literal", K(ret));
    } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
      LOG_WARN("failed to append branch1 select", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "c."))) {
      LOG_WARN("failed to append branch1 pk cols", K(ret));
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "c.", true))) {
      LOG_WARN("failed to append branch1 val cols", K(ret));
    } else if (OB_FAIL(diff_sql.append(" FROM "))) {
      LOG_WARN("failed to append branch1 from prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append branch1 current table", K(ret));
    } else if (OB_FAIL(diff_sql.append(" c LEFT JOIN "))) {
      LOG_WARN("failed to append branch1 join prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append branch1 incoming table", K(ret));
    } else if (OB_FAIL(diff_sql.append(" i ON "))) {
      LOG_WARN("failed to append branch1 join condition prefix", K(ret));
    } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
      LOG_WARN("failed to append branch1 join condition", K(ret));
    } else if (OB_FAIL(diff_sql.append(" WHERE i."))) {
      LOG_WARN("failed to append branch1 null-check prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(diff_sql, pk1))) {
      LOG_WARN("failed to append branch1 pk identifier", K(ret));
    } else if (OB_FAIL(diff_sql.append(" IS NULL"))) {
      LOG_WARN("failed to append branch1 from clause", K(ret));
    }
  }

  // Branch 2: incoming-only rows (LEFT JOIN, WHERE current IS NULL)
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append(" UNION ALL SELECT "))) {
      LOG_WARN("failed to append branch2 select prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                   diff_sql, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append branch2 table literal", K(ret));
    } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
      LOG_WARN("failed to append branch2 select", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "i."))) {
      LOG_WARN("failed to append branch2 pk cols", K(ret));
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "i.", true))) {
      LOG_WARN("failed to append branch2 val cols", K(ret));
    } else if (OB_FAIL(diff_sql.append(" FROM "))) {
      LOG_WARN("failed to append branch2 from prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append branch2 incoming table", K(ret));
    } else if (OB_FAIL(diff_sql.append(" i LEFT JOIN "))) {
      LOG_WARN("failed to append branch2 join prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append branch2 current table", K(ret));
    } else if (OB_FAIL(diff_sql.append(" c ON "))) {
      LOG_WARN("failed to append branch2 join condition prefix", K(ret));
    } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
      LOG_WARN("failed to append branch2 join condition", K(ret));
    } else if (OB_FAIL(diff_sql.append(" WHERE c."))) {
      LOG_WARN("failed to append branch2 null-check prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(diff_sql, pk1))) {
      LOG_WARN("failed to append branch2 pk identifier", K(ret));
    } else if (OB_FAIL(diff_sql.append(" IS NULL"))) {
      LOG_WARN("failed to append branch2 from clause", K(ret));
    }
  }

  // Branch 3 & 4: conflict rows (INNER JOIN, WHERE values differ)
  if (OB_SUCC(ret) && has_val_cols) {
    // current version of conflict rows
    if (OB_FAIL(diff_sql.append(" UNION ALL SELECT "))) {
      LOG_WARN("failed to append branch3 select prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                   diff_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append branch3 table literal", K(ret));
    } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
      LOG_WARN("failed to append branch3 select", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "c."))) {
      LOG_WARN("failed to append branch3 pk cols", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "c.", true))) {
      LOG_WARN("failed to append branch3 val cols", K(ret));
    } else if (OB_FAIL(diff_sql.append(" FROM "))) {
      LOG_WARN("failed to append branch3 from prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append branch3 current table", K(ret));
    } else if (OB_FAIL(diff_sql.append(" c JOIN "))) {
      LOG_WARN("failed to append branch3 join prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append branch3 incoming table", K(ret));
    } else if (OB_FAIL(diff_sql.append(" i ON "))) {
      LOG_WARN("failed to append branch3 join condition prefix", K(ret));
    } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
      LOG_WARN("failed to append branch3 join condition", K(ret));
    } else if (OB_FAIL(diff_sql.append(" WHERE NOT ("))) {
      LOG_WARN("failed to append branch3 diff predicate prefix", K(ret));
    } else if (OB_FAIL(diff_sql.append(val_cmp.ptr(), val_cmp.length()))) {
      LOG_WARN("failed to append branch3 diff predicate", K(ret));
    } else if (OB_FAIL(diff_sql.append(")"))) {
      LOG_WARN("failed to append branch3 from clause", K(ret));
    }

    // incoming version of conflict rows
    if (OB_SUCC(ret)) {
      if (OB_FAIL(diff_sql.append(" UNION ALL SELECT "))) {
        LOG_WARN("failed to append branch4 select prefix", K(ret));
      } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                     diff_sql, inc_db_name, inc_table_name))) {
        LOG_WARN("failed to append branch4 table literal", K(ret));
      } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
        LOG_WARN("failed to append branch4 select", K(ret));
      } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "i."))) {
        LOG_WARN("failed to append branch4 pk cols", K(ret));
      } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "i.", true))) {
        LOG_WARN("failed to append branch4 val cols", K(ret));
      } else if (OB_FAIL(diff_sql.append(" FROM "))) {
        LOG_WARN("failed to append branch4 from prefix", K(ret));
      } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                     diff_sql, cur_db_name, cur_table_name))) {
        LOG_WARN("failed to append branch4 current table", K(ret));
      } else if (OB_FAIL(diff_sql.append(" c JOIN "))) {
        LOG_WARN("failed to append branch4 join prefix", K(ret));
      } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                     diff_sql, inc_db_name, inc_table_name))) {
        LOG_WARN("failed to append branch4 incoming table", K(ret));
      } else if (OB_FAIL(diff_sql.append(" i ON "))) {
        LOG_WARN("failed to append branch4 join condition prefix", K(ret));
      } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
        LOG_WARN("failed to append branch4 join condition", K(ret));
      } else if (OB_FAIL(diff_sql.append(" WHERE NOT ("))) {
        LOG_WARN("failed to append branch4 diff predicate prefix", K(ret));
      } else if (OB_FAIL(diff_sql.append(val_cmp.ptr(), val_cmp.length()))) {
        LOG_WARN("failed to append branch4 diff predicate", K(ret));
      } else if (OB_FAIL(diff_sql.append(")"))) {
        LOG_WARN("failed to append branch4 from clause", K(ret));
      }
    }
  }

  // Close subquery and ORDER BY
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append(") diff_result ORDER BY "))) {
      LOG_WARN("failed to append order by prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, ""))) {
      LOG_WARN("failed to append order by cols", K(ret));
    } else if (OB_FAIL(diff_sql.append(", `__table`"))) {
      LOG_WARN("failed to append order by table", K(ret));
    }
  }

  return ret;
}

int ObDiffTableResolver::parse_and_resolve_select_sql(const ObString &select_sql)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_) || OB_ISNULL(params_.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data member is not init", K(ret), K(session_info_), K(params_.allocator_));
  } else {
    ParseResult select_result;
    ObParser parser(*params_.allocator_, session_info_->get_sql_mode());
    if (OB_FAIL(parser.parse(select_sql, select_result))) {
      LOG_WARN("parse select sql failed", K(select_sql), K(ret));
    } else if (OB_ISNULL(select_result.result_tree_) ||
               select_result.result_tree_->num_child_ != 1 ||
               OB_ISNULL(select_result.result_tree_->children_) ||
               OB_ISNULL(select_result.result_tree_->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("parse result tree is invalid", K(ret));
    } else {
      ParseNode *select_stmt_node = select_result.result_tree_->children_[0];
      if (OB_FAIL(ObSelectResolver::resolve(*select_stmt_node))) {
        LOG_WARN("resolve generated diff select failed", K(ret));
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
