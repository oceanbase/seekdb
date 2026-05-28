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
#include "sql/resolver/cmd/ob_diff_table_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_column_schema.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObDiffTableResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)
      || OB_ISNULL(params_.allocator_)
      || T_DIFF_TABLE != parse_tree.type_
      || DIFF_TABLE_NODE_COUNT != parse_tree.num_child_
      || OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree for DIFF TABLE", K(ret));
  }

  ObDiffTableStmt *diff_stmt = NULL;
  if (OB_SUCC(ret)) {
    diff_stmt = create_stmt<ObDiffTableStmt>();
    if (OB_ISNULL(diff_stmt)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc diff table stmt failed", K(ret));
    } else {
      stmt_ = diff_stmt;
      diff_stmt->set_tenant_id(session_info_->get_effective_tenant_id());
    }
  }

  ObString cur_table, cur_db, inc_table, inc_db;
  const ObTableSchema *cur_schema = NULL;
  const ObTableSchema *inc_schema = NULL;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(resolve_table_names_(parse_tree, cur_table, cur_db, inc_table, inc_db))) {
      LOG_WARN("resolve table names failed", K(ret));
    } else if (OB_FAIL(get_schemas_(diff_stmt->get_tenant_id(), cur_db, cur_table,
                                     inc_db, inc_table, cur_schema, inc_schema))) {
      LOG_WARN("get table schemas failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObString cur_db_copy, cur_tbl_copy, inc_db_copy, inc_tbl_copy;
    if (OB_FAIL(ob_write_string(*params_.allocator_, cur_db, cur_db_copy))) {
    } else if (OB_FAIL(ob_write_string(*params_.allocator_, cur_table, cur_tbl_copy))) {
    } else if (OB_FAIL(ob_write_string(*params_.allocator_, inc_db, inc_db_copy))) {
    } else if (OB_FAIL(ob_write_string(*params_.allocator_, inc_table, inc_tbl_copy))) {
    } else {
      diff_stmt->set_cur_db(cur_db_copy);
      diff_stmt->set_cur_table(cur_tbl_copy);
      diff_stmt->set_inc_db(inc_db_copy);
      diff_stmt->set_inc_table(inc_tbl_copy);
      diff_stmt->set_cur_db_id(cur_schema->get_database_id());
      diff_stmt->set_inc_db_id(inc_schema->get_database_id());
      diff_stmt->set_cur_table_id(cur_schema->get_table_id());
      diff_stmt->set_inc_table_id(inc_schema->get_table_id());
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(collect_columns_(*cur_schema, *inc_schema, *diff_stmt))) {
      LOG_WARN("collect columns failed", K(ret));
    } else if (diff_stmt->pk_cols().empty()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "DIFF TABLE on table without primary key");
    } else if (OB_FAIL(build_output_cols_(*cur_schema, *diff_stmt))) {
      LOG_WARN("build output cols failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("DIFF TABLE resolved",
             "cur", diff_stmt->get_cur_table(),
             "inc", diff_stmt->get_inc_table(),
             "pk_cnt", diff_stmt->pk_cols().count(),
             "val_cnt", diff_stmt->val_cols().count(),
             "out_cnt", diff_stmt->out_cols().count());
  }
  return ret;
}

int ObDiffTableResolver::resolve_table_names_(const ParseNode &parse_tree,
    ObString &cur_table, ObString &cur_db,
    ObString &inc_table, ObString &inc_db)
{
  int ret = OB_SUCCESS;
  ParseNode *cur_node = parse_tree.children_[CURRENT_TABLE_NODE];
  ParseNode *inc_node = parse_tree.children_[INCOMING_TABLE_NODE];
  if (OB_ISNULL(cur_node) || OB_ISNULL(inc_node)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(resolve_table_relation_node(cur_node, cur_table, cur_db))) {
    LOG_WARN("resolve cur failed", K(ret));
  } else if (OB_FAIL(resolve_table_relation_node(inc_node, inc_table, inc_db))) {
    LOG_WARN("resolve inc failed", K(ret));
  }
  return ret;
}

int ObDiffTableResolver::get_schemas_(uint64_t tenant_id,
    const ObString &cur_db, const ObString &cur_table,
    const ObString &inc_db, const ObString &inc_table,
    const ObTableSchema *&cur_schema, const ObTableSchema *&inc_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(schema_checker_->get_table_schema(tenant_id, cur_db, cur_table, false, cur_schema))) {
    LOG_WARN("get cur schema failed", K(ret), K(cur_db), K(cur_table));
  } else if (OB_ISNULL(cur_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else if (OB_FAIL(schema_checker_->get_table_schema(tenant_id, inc_db, inc_table, false, inc_schema))) {
    LOG_WARN("get inc schema failed", K(ret), K(inc_db), K(inc_table));
  } else if (OB_ISNULL(inc_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  }
  return ret;
}

int ObDiffTableResolver::collect_columns_(const ObTableSchema &cur_schema,
    const ObTableSchema &inc_schema, ObDiffTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 8> pk_cols;
  ObSEArray<ObString, 16> val_cols;
  if (OB_FAIL(ObResolverUtils::collect_and_validate_columns(&cur_schema, &inc_schema,
                                                            pk_cols, val_cols, "DIFF TABLE"))) {
    LOG_WARN("collect_and_validate_columns failed", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < pk_cols.count(); ++i) {
      ObString copy;
      if (OB_FAIL(ob_write_string(*params_.allocator_, pk_cols.at(i), copy))) {
      } else if (OB_FAIL(stmt.pk_cols().push_back(copy))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < val_cols.count(); ++i) {
      ObString copy;
      if (OB_FAIL(ob_write_string(*params_.allocator_, val_cols.at(i), copy))) {
      } else if (OB_FAIL(stmt.val_cols().push_back(copy))) {
      }
    }
  }
  return ret;
}

int ObDiffTableResolver::build_output_cols_(const ObTableSchema &src,
    ObDiffTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  const ObCollationType default_cs = ObCharset::get_default_collation(
      ObCharset::get_default_charset());

  // __table
  {
    ObDiffOutputCol c;
    c.name_ = ObString::make_string("__table");
    c.obj_type_ = ObVarcharType;
    c.collation_type_ = default_cs;
    c.length_ = OB_MAX_TABLE_NAME_LENGTH * 2 + 2;
    c.is_synth_ = true;
    if (OB_FAIL(stmt.out_cols().push_back(c))) {}
  }
  // __flag
  if (OB_SUCC(ret)) {
    ObDiffOutputCol c;
    c.name_ = ObString::make_string("__flag");
    c.obj_type_ = ObVarcharType;
    c.collation_type_ = default_cs;
    c.length_ = 8;
    c.is_synth_ = true;
    if (OB_FAIL(stmt.out_cols().push_back(c))) {}
  }

  // PK columns
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.pk_cols().count(); ++i) {
    const ObColumnSchemaV2 *col = src.get_column_schema(stmt.pk_cols().at(i));
    if (OB_ISNULL(col)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObDiffOutputCol c;
      ObString name_copy;
      if (OB_FAIL(ob_write_string(*params_.allocator_, col->get_column_name_str(), name_copy))) {
      } else {
        c.name_ = name_copy;
        c.obj_type_ = col->get_data_type();
        c.collation_type_ = col->get_collation_type();
        c.length_ = col->get_data_length();
        c.is_pk_ = true;
        c.col_id_ = col->get_column_id();
        if (OB_FAIL(stmt.out_cols().push_back(c))) {}
      }
    }
  }
  // Value columns
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.val_cols().count(); ++i) {
    const ObColumnSchemaV2 *col = src.get_column_schema(stmt.val_cols().at(i));
    if (OB_ISNULL(col)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObDiffOutputCol c;
      ObString name_copy;
      if (OB_FAIL(ob_write_string(*params_.allocator_, col->get_column_name_str(), name_copy))) {
      } else {
        c.name_ = name_copy;
        c.obj_type_ = col->get_data_type();
        c.collation_type_ = col->get_collation_type();
        c.length_ = col->get_data_length();
        c.col_id_ = col->get_column_id();
        if (OB_FAIL(stmt.out_cols().push_back(c))) {}
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
