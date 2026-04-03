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
#include "sql/resolver/cmd/ob_merge_table_resolver.h"
#include "sql/resolver/cmd/ob_merge_table_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_column_schema.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObMergeTableResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(session_info_) ||
      OB_ISNULL(schema_checker_) ||
      OB_ISNULL(params_.allocator_) ||
      T_MERGE_TABLE != parse_tree.type_ ||
      MERGE_TABLE_NODE_COUNT != parse_tree.num_child_ ||
      OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree for MERGE TABLE", K(ret));
  }

  ObMergeTableStmt *merge_stmt = NULL;
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(merge_stmt = create_stmt<ObMergeTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate merge table stmt", K(ret));
    } else {
      stmt_ = merge_stmt;
    }
  }

  ObString inc_table_name, inc_db_name, cur_table_name, cur_db_name;
  const ObTableSchema *inc_schema = NULL;
  const ObTableSchema *cur_schema = NULL;
  ObSEArray<ObString, 8> pk_cols;
  ObSEArray<ObString, 16> val_cols;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(resolve_table_names_and_strategy_(parse_tree, *merge_stmt,
                                                        inc_table_name, inc_db_name,
                                                        cur_table_name, cur_db_name))) {
    LOG_WARN("failed to resolve table names and strategy", K(ret));
  } else if (OB_FAIL(get_table_schemas_(session_info_->get_effective_tenant_id(),
                                         cur_db_name, cur_table_name,
                                         inc_db_name, inc_table_name,
                                         cur_schema, inc_schema))) {
    LOG_WARN("failed to get table schemas", K(ret));
  } else if (OB_FAIL(collect_and_validate_columns_(cur_schema, inc_schema, pk_cols, val_cols))) {
    LOG_WARN("failed to collect and validate columns", K(ret));
  } else if (OB_FAIL(build_merge_sqls_(*merge_stmt, cur_db_name, cur_table_name,
                                        inc_db_name, inc_table_name,
                                        pk_cols, val_cols))) {
    LOG_WARN("failed to build merge sqls", K(ret));
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("MERGE TABLE resolved",
             "strategy", merge_stmt->get_strategy(),
             "has_insert", !merge_stmt->get_insert_sql().empty(),
             "has_update", !merge_stmt->get_update_sql().empty());
  }

  return ret;
}

int ObMergeTableResolver::resolve_table_names_and_strategy_(
    const ParseNode &parse_tree, ObMergeTableStmt &stmt,
    ObString &inc_table_name, ObString &inc_db_name,
    ObString &cur_table_name, ObString &cur_db_name)
{
  int ret = OB_SUCCESS;
  ParseNode *inc_node = parse_tree.children_[INCOMING_TABLE_NODE];
  ParseNode *cur_node = parse_tree.children_[CURRENT_TABLE_NODE];
  ParseNode *strategy_node = parse_tree.children_[STRATEGY_NODE];
  if (OB_ISNULL(inc_node) || OB_ISNULL(cur_node) || OB_ISNULL(strategy_node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node is NULL", K(ret));
  } else if (OB_FAIL(resolve_table_relation_node(inc_node, inc_table_name, inc_db_name))) {
    LOG_WARN("failed to resolve incoming table", K(ret));
  } else if (OB_FAIL(resolve_table_relation_node(cur_node, cur_table_name, cur_db_name))) {
    LOG_WARN("failed to resolve current table", K(ret));
  } else {
    stmt.set_cur_db_name(cur_db_name);
    stmt.set_cur_table_name(cur_table_name);
    stmt.set_inc_db_name(inc_db_name);
    stmt.set_inc_table_name(inc_table_name);
    int64_t strategy_val = strategy_node->value_;
    if (strategy_val == 0) {
      stmt.set_strategy(MERGE_STRATEGY_FAIL);
    } else if (strategy_val == 1) {
      stmt.set_strategy(MERGE_STRATEGY_THEIRS);
    } else if (strategy_val == 2) {
      stmt.set_strategy(MERGE_STRATEGY_OURS);
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown merge strategy", K(ret), K(strategy_val));
    }
  }
  return ret;
}

int ObMergeTableResolver::get_table_schemas_(const uint64_t tenant_id,
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

int ObMergeTableResolver::collect_and_validate_columns_(const ObTableSchema *cur_schema,
                                                         const ObTableSchema *inc_schema,
                                                         ObIArray<ObString> &pk_cols,
                                                         ObIArray<ObString> &val_cols)
{
  return ObResolverUtils::collect_and_validate_columns(cur_schema, inc_schema,
                                                       pk_cols, val_cols, "MERGE TABLE");
}

int ObMergeTableResolver::build_merge_sqls_(
    ObMergeTableStmt &stmt,
    const ObString &cur_db_name, const ObString &cur_table_name,
    const ObString &inc_db_name, const ObString &inc_table_name,
    const ObIArray<ObString> &pk_cols, const ObIArray<ObString> &val_cols)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params_.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator is null", K(ret));
  } else if (OB_UNLIKELY(pk_cols.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pk_cols should not be empty", K(ret));
  }
  if (OB_FAIL(ret)) {
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

  // INSERT for incoming-only rows (all strategies need this)
  if (OB_SUCC(ret)) {
    ObSqlString insert_sql;
    if (OB_FAIL(insert_sql.append("INSERT INTO "))) {
      LOG_WARN("failed to append insert prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   insert_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append insert prefix", K(ret));
    } else if (OB_FAIL(insert_sql.append(" ("))) {
      LOG_WARN("failed to append insert column prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(insert_sql, pk_cols, ""))) {
      LOG_WARN("failed to append insert pk cols", K(ret));
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(insert_sql, val_cols, "", true))) {
      LOG_WARN("failed to append insert val cols", K(ret));
    } else if (OB_FAIL(insert_sql.append(") SELECT "))) {
      LOG_WARN("failed to append select keyword", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_col_list(insert_sql, pk_cols, "i."))) {
      LOG_WARN("failed to append select pk cols", K(ret));
    } else if (has_val_cols && OB_FAIL(ObResolverUtils::append_col_list(insert_sql, val_cols, "i.", true))) {
      LOG_WARN("failed to append select val cols", K(ret));
    } else if (OB_FAIL(insert_sql.append(" FROM "))) {
      LOG_WARN("failed to append insert from prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   insert_sql, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append incoming table for insert", K(ret));
    } else if (OB_FAIL(insert_sql.append(" i LEFT JOIN "))) {
      LOG_WARN("failed to append insert join prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   insert_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append current table for insert", K(ret));
    } else if (OB_FAIL(insert_sql.append(" c ON "))) {
      LOG_WARN("failed to append insert join condition prefix", K(ret));
    } else if (OB_FAIL(insert_sql.append(pk_eq.ptr(), pk_eq.length()))) {
      LOG_WARN("failed to append insert join condition", K(ret));
    } else if (OB_FAIL(insert_sql.append(" WHERE c."))) {
      LOG_WARN("failed to append insert null-check prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(insert_sql, pk1))) {
      LOG_WARN("failed to append insert pk identifier", K(ret));
    } else if (OB_FAIL(insert_sql.append(" IS NULL"))) {
      LOG_WARN("failed to build insert sql from clause", K(ret));
    } else {
      ObString str;
      if (OB_FAIL(ob_write_string(*params_.allocator_, insert_sql.string(), str))) {
        LOG_WARN("failed to copy insert sql", K(ret));
      } else {
        stmt.set_insert_sql(str);
      }
    }
  }

  // UPDATE for conflict rows (THEIRS strategy only)
  if (OB_SUCC(ret) && has_val_cols && stmt.get_strategy() == MERGE_STRATEGY_THEIRS) {
    ObSqlString update_sql;
    ObSqlString set_clause;
    if (OB_FAIL(append_set_clause_(set_clause, val_cols))) {
      LOG_WARN("failed to build set clause", K(ret));
    } else if (OB_FAIL(update_sql.append("UPDATE "))) {
      LOG_WARN("failed to append update prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   update_sql, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append current table for update", K(ret));
    } else if (OB_FAIL(update_sql.append(" c JOIN "))) {
      LOG_WARN("failed to append update join prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   update_sql, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append incoming table for update", K(ret));
    } else if (OB_FAIL(update_sql.append(" i ON "))) {
      LOG_WARN("failed to append update join condition prefix", K(ret));
    } else if (OB_FAIL(update_sql.append(pk_eq.ptr(), pk_eq.length()))) {
      LOG_WARN("failed to append update join condition", K(ret));
    } else if (OB_FAIL(update_sql.append(" SET "))) {
      LOG_WARN("failed to append update set prefix", K(ret));
    } else if (OB_FAIL(update_sql.append(set_clause.ptr(), set_clause.length()))) {
      LOG_WARN("failed to append update set clause", K(ret));
    } else if (OB_FAIL(update_sql.append(" WHERE NOT ("))) {
      LOG_WARN("failed to append update predicate prefix", K(ret));
    } else if (OB_FAIL(update_sql.append(val_cmp.ptr(), val_cmp.length()))) {
      LOG_WARN("failed to append update predicate", K(ret));
    } else if (OB_FAIL(update_sql.append(")"))) {
      LOG_WARN("failed to build update sql", K(ret));
    } else {
      ObString str;
      if (OB_FAIL(ob_write_string(*params_.allocator_, update_sql.string(), str))) {
        LOG_WARN("failed to copy update sql", K(ret));
      } else {
        stmt.set_update_sql(str);
      }
    }
  }

  // Conflict check SQL (FAIL strategy)
  if (OB_SUCC(ret) && has_val_cols && stmt.get_strategy() == MERGE_STRATEGY_FAIL) {
    ObSqlString conflict_check;
    if (OB_FAIL(conflict_check.append("SELECT COUNT(*) AS conflict_cnt FROM "))) {
      LOG_WARN("failed to append conflict-check prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   conflict_check, cur_db_name, cur_table_name))) {
      LOG_WARN("failed to append current table for conflict check", K(ret));
    } else if (OB_FAIL(conflict_check.append(" c JOIN "))) {
      LOG_WARN("failed to append conflict-check join prefix", K(ret));
    } else if (OB_FAIL(ObResolverUtils::append_qualified_identifier(
                   conflict_check, inc_db_name, inc_table_name))) {
      LOG_WARN("failed to append incoming table for conflict check", K(ret));
    } else if (OB_FAIL(conflict_check.append(" i ON "))) {
      LOG_WARN("failed to append conflict-check join condition prefix", K(ret));
    } else if (OB_FAIL(conflict_check.append(pk_eq.ptr(), pk_eq.length()))) {
      LOG_WARN("failed to append conflict-check join condition", K(ret));
    } else if (OB_FAIL(conflict_check.append(" WHERE NOT ("))) {
      LOG_WARN("failed to append conflict-check predicate prefix", K(ret));
    } else if (OB_FAIL(conflict_check.append(val_cmp.ptr(), val_cmp.length()))) {
      LOG_WARN("failed to append conflict-check predicate", K(ret));
    } else if (OB_FAIL(conflict_check.append(") FOR UPDATE"))) {
      LOG_WARN("failed to build conflict check sql", K(ret));
    } else {
      ObString str;
      if (OB_FAIL(ob_write_string(*params_.allocator_, conflict_check.string(), str))) {
        LOG_WARN("failed to copy conflict check sql", K(ret));
      } else {
        stmt.set_conflict_check_sql(str);
      }
    }
  }

  return ret;
}

int ObMergeTableResolver::append_set_clause_(ObSqlString &sql,
                                              const ObIArray<ObString> &cols)
{
  int ret = OB_SUCCESS;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < cols.count(); idx++) {
    const ObString &col = cols.at(idx);
    const char *sep = (idx > 0) ? ", " : "";
    if (OB_FAIL(sql.append(sep))) {
    } else if (OB_FAIL(sql.append("c."))) {
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(sql, col))) {
    } else if (OB_FAIL(sql.append(" = i."))) {
    } else if (OB_FAIL(ObResolverUtils::append_escaped_identifier(sql, col))) {
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("failed to append set clause", K(ret));
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
