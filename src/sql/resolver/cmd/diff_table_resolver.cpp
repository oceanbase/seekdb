/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/resolver/cmd/diff_table_resolver.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/syntax.h"
#include "share/schema/ob_column_schema.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int DiffTableResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;

  ObString cur_table_name, cur_db_name, inc_table_name, inc_db_name;
  const ObTableSchema *cur_schema = NULL;
  const ObTableSchema *inc_schema = NULL;
  ObSEArray<ObString, 8> pk_cols;
  ObSEArray<ObString, 16> val_cols;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(resolve_table_names_(peg::diff_table_definition(parse_tree), cur_table_name, cur_db_name,
                                           inc_table_name, inc_db_name))) {
  } else if (OB_FAIL(get_table_schemas_(cur_db_name, cur_table_name,
                                         inc_db_name, inc_table_name,
                                         cur_schema, inc_schema))) {
  } else if (OB_FAIL(collect_and_validate_columns_(cur_schema, inc_schema, pk_cols, val_cols))) {
  }

  ObSqlString diff_sql;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(build_diff_sql_(cur_db_name, cur_table_name,
                                inc_db_name, inc_table_name,
                                pk_cols, val_cols, diff_sql))) {
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("DIFF TABLE generated SQL", "sql", diff_sql.string());
    if (OB_FAIL(parse_and_resolve_select_sql(diff_sql.string()))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(stmt_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_UNLIKELY(stmt::T_SELECT != stmt_->get_stmt_type())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObSelectStmt *select_stmt = static_cast<ObSelectStmt*>(stmt_);
      if (OB_ISNULL(select_stmt->get_query_ctx())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        select_stmt->get_query_ctx()->set_literal_stmt_type(stmt::T_DIFF_TABLE);
      }
    }
  }

  return ret;
}

int DiffTableResolver::resolve_table_names_(const peg::DiffTableDefinition &syntax,
                                               ObString &cur_table_name, ObString &cur_db_name,
                                               ObString &inc_table_name, ObString &inc_db_name)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resolve_table_relation_node(&syntax.current, cur_table_name, cur_db_name))) {
  } else if (OB_FAIL(resolve_table_relation_node(&syntax.incoming, inc_table_name, inc_db_name))) {
  }
  return ret;
}

int DiffTableResolver::get_table_schemas_(const ObString &cur_db_name, const ObString &cur_table_name,
                                             const ObString &inc_db_name, const ObString &inc_table_name,
                                             const ObTableSchema *&cur_schema,
                                             const ObTableSchema *&inc_schema)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_checker_->get_table_schema( cur_db_name, cur_table_name,
                                                 false, cur_schema))) {
  } else if (OB_ISNULL(cur_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else if (OB_FAIL(schema_checker_->get_table_schema( inc_db_name, inc_table_name,
                                                        false, inc_schema))) {
  } else if (OB_ISNULL(inc_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  }
  return ret;
}

int DiffTableResolver::collect_and_validate_columns_(const ObTableSchema *cur_schema,
                                                        const ObTableSchema *inc_schema,
                                                        ObIArray<ObString> &pk_cols,
                                                        ObIArray<ObString> &val_cols)
{
  return ObResolverUtils::collect_and_validate_columns(cur_schema, inc_schema,
                                                       pk_cols, val_cols, "DIFF TABLE");
}

int DiffTableResolver::build_diff_sql_(const ObString &cur_db_name, const ObString &cur_table_name,
                                          const ObString &inc_db_name, const ObString &inc_table_name,
                                          const ObIArray<ObString> &pk_cols,
                                          const ObIArray<ObString> &val_cols,
                                          ObSqlString &diff_sql)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(pk_cols.empty())) {
    ret = OB_ERR_UNEXPECTED;
    return ret;
  }
  const bool has_val_cols = (val_cols.count() > 0);
  const ObString &pk1 = pk_cols.at(0);

  ObSqlString pk_eq;
  ObSqlString val_cmp;
  if (OB_FAIL(ObResolverUtils::append_binary_cond(pk_eq, pk_cols, "="))) {
  } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_binary_cond(val_cmp, val_cols, "<=>"))) {
  }

  // SELECT header
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append("SELECT `__table`, `__flag`, "))) {
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, ""))) {
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "", true))) {
    } else if (OB_FAIL(diff_sql.append(" FROM ("))) {
    }
  }

  // Branch 1: current-only rows (LEFT JOIN, WHERE incoming IS NULL)
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append("SELECT "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                   diff_sql, cur_db_name, cur_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "c."))) {
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "c.", true))) {
    } else if (OB_FAIL(diff_sql.append(" FROM "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, cur_db_name, cur_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" c LEFT JOIN "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, inc_db_name, inc_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" i ON "))) {
    } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
    } else if (OB_FAIL(diff_sql.append(" WHERE i."))) {
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(diff_sql, pk1))) {
    } else if (OB_FAIL(diff_sql.append(" IS NULL"))) {
    }
  }

  // Branch 2: incoming-only rows (LEFT JOIN, WHERE current IS NULL)
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append(" UNION ALL SELECT "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                   diff_sql, inc_db_name, inc_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "i."))) {
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "i.", true))) {
    } else if (OB_FAIL(diff_sql.append(" FROM "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, inc_db_name, inc_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" i LEFT JOIN "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, cur_db_name, cur_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" c ON "))) {
    } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
    } else if (OB_FAIL(diff_sql.append(" WHERE c."))) {
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(diff_sql, pk1))) {
    } else if (OB_FAIL(diff_sql.append(" IS NULL"))) {
    }
  }

  // Branch 3 & 4: conflict rows (INNER JOIN, WHERE values differ)
  if (OB_SUCC(ret) && has_val_cols) {
    // current version of conflict rows
    if (OB_FAIL(diff_sql.append(" UNION ALL SELECT "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                   diff_sql, cur_db_name, cur_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "c."))) {
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "c.", true))) {
    } else if (OB_FAIL(diff_sql.append(" FROM "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, cur_db_name, cur_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" c JOIN "))) {
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   diff_sql, inc_db_name, inc_table_name))) {
    } else if (OB_FAIL(diff_sql.append(" i ON "))) {
    } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
    } else if (OB_FAIL(diff_sql.append(" WHERE NOT ("))) {
    } else if (OB_FAIL(diff_sql.append(val_cmp.ptr(), val_cmp.length()))) {
    } else if (OB_FAIL(diff_sql.append(")"))) {
    }

    // incoming version of conflict rows
    if (OB_SUCC(ret)) {
      if (OB_FAIL(diff_sql.append(" UNION ALL SELECT "))) {
      } else if (OB_FAIL(ObResolverUtils::append_qualified_name_literal(
                     diff_sql, inc_db_name, inc_table_name))) {
      } else if (OB_FAIL(diff_sql.append(" AS `__table`, 'INSERT' AS `__flag`, "))) {
      } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, "i."))) {
      } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, val_cols, "i.", true))) {
      } else if (OB_FAIL(diff_sql.append(" FROM "))) {
      } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                     diff_sql, cur_db_name, cur_table_name))) {
      } else if (OB_FAIL(diff_sql.append(" c JOIN "))) {
      } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                     diff_sql, inc_db_name, inc_table_name))) {
      } else if (OB_FAIL(diff_sql.append(" i ON "))) {
      } else if (OB_FAIL(diff_sql.append(pk_eq.ptr(), pk_eq.length()))) {
      } else if (OB_FAIL(diff_sql.append(" WHERE NOT ("))) {
      } else if (OB_FAIL(diff_sql.append(val_cmp.ptr(), val_cmp.length()))) {
      } else if (OB_FAIL(diff_sql.append(")"))) {
      }
    }
  }

  // Close subquery and ORDER BY
  if (OB_SUCC(ret)) {
    if (OB_FAIL(diff_sql.append(") diff_result ORDER BY "))) {
    } else if (OB_FAIL(ObResolverUtils::append_col_list(diff_sql, pk_cols, ""))) {
    } else if (OB_FAIL(diff_sql.append(", `__table`"))) {
    }
  }

  return ret;
}

int DiffTableResolver::parse_and_resolve_select_sql(const ObString &select_sql)
{
  int ret = OB_SUCCESS;
  ParsedSql query;
  ObParser parser(*params_.allocator_, session_info_->get_sql_mode());
  if (OB_FAIL(parser.parse(select_sql, query))) {
  } else {
    const ParsedSql *previous_syntax = params_.syntax_;
    params_.syntax_ = &query;
    ret = ObSelectResolver::resolve(*query.statement());
    params_.syntax_ = previous_syntax;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
