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
#include "sql/resolver/ddl/ob_recyclebin_restore_resolver.h"

namespace oceanbase
{
using namespace common;

namespace sql
{
/**
 * Restore table from recyclebin.
 */
int ObRecyclebinRestoreTableResolver::resolve(const ParseNode &parser_tree)
{
  int ret = OB_SUCCESS;
  ObRecyclebinRestoreTableStmt *restore_table_from_recyclebin_stmt = NULL;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info is null", K(ret));
  } else if (T_RECYCLEBIN_RESTORE_TABLE != parser_tree.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree",  K(parser_tree.type_));
  }
  // create recyclebin restore table stmt
  if (OB_SUCC(ret)) {
    if (NULL == (restore_table_from_recyclebin_stmt = create_stmt<ObRecyclebinRestoreTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create rename table stmt", K(ret));
    } else {
      stmt_ = restore_table_from_recyclebin_stmt;
    }
  }
  if (OB_SUCC(ret)) {

    // restore table
    ParseNode *table_node = parser_tree.children_[ORIGIN_TABLE_NODE];
    ObString origin_table_name;
    ObString origin_db_name;
    if (OB_ISNULL(table_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table_node should not be null", K(ret));
    } else if (OB_FAIL(resolve_table_relation_node(table_node,
                                                   origin_table_name,
                                                   origin_db_name,
                                                   true /*get origin db_name*/))) {
      LOG_WARN("failed to resolve_table_relation_node", K(ret));
    } else {
      OX (restore_table_from_recyclebin_stmt->set_origin_table_name(origin_table_name));
      OX (restore_table_from_recyclebin_stmt->set_origin_table_id(OB_INVALID_ID));
    }

    if (OB_SUCC(ret)) {
      //rename to new table_name
      ParseNode *rename_node = parser_tree.children_[NEW_TABLE_NODE];
      if (NULL != rename_node) {
        ObString new_table_name;
        ObString new_db_name;
        if (OB_FAIL(resolve_table_relation_node(rename_node,
                                                new_table_name,
                                                new_db_name))) {
          LOG_WARN("failed to resolve_table_relation_node", K(ret));
        } else if (ObString(OB_RECYCLEBIN_SCHEMA_NAME) == new_db_name
                   || ObString(OB_PUBLIC_SCHEMA_NAME) == new_db_name) {
          ret = OB_OP_NOT_ALLOW;
          LOG_WARN("can't not restore table to recyclebin database", K(ret));
        } else {
          restore_table_from_recyclebin_stmt->set_new_db_name(new_db_name);
          restore_table_from_recyclebin_stmt->set_new_table_name(new_table_name);
        }
      }
    }
    // Support using the original table name to recover tables from recyclebin.
    // Reuse origin_db_name to specify which database the table was deleted from.
    if (OB_SUCC(ret)) {
      if (origin_db_name.empty()) {
        if (OB_ISNULL(session_info_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("session_info_ is null", K(ret));
        } else if (OB_UNLIKELY(session_info_->get_database_name().empty())) {
          ret = OB_ERR_NO_DB_SELECTED;
          LOG_WARN("database not specified", K(ret));
        } else {
          restore_table_from_recyclebin_stmt->set_origin_db_name(
              session_info_->get_database_name());
        }
      } else {
        restore_table_from_recyclebin_stmt->set_origin_db_name(origin_db_name);
      }
    }

  }
  return ret;
}

/**
 * Restore database from recyclebin.
 */
int ObRecyclebinRestoreDatabaseResolver::resolve(const ParseNode &parser_tree)
{
  int ret = OB_SUCCESS;
  ObRecyclebinRestoreDatabaseStmt *restore_database_stmt = NULL;
  int32_t max_database_name_length = OB_MAX_DATABASE_NAME_LENGTH;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info is null", K(ret));
  } else if (T_RECYCLEBIN_RESTORE_DATABASE != parser_tree.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree",  K(parser_tree.type_));
  }
  // create recyclebin restore database stmt
  if (OB_SUCC(ret)) {
    if (NULL == (restore_database_stmt = create_stmt<ObRecyclebinRestoreDatabaseStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create rename table stmt", K(ret));
    } else {
      stmt_ = restore_database_stmt;
    }
  }
  if (OB_SUCC(ret)) {

    ObString origin_db_name;
    ParseNode *origin_dbname_node = parser_tree.children_[ORIGIN_DB_NODE];
    if (OB_ISNULL(origin_dbname_node) || OB_UNLIKELY(T_IDENT != origin_dbname_node->type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid parse tree", K(ret));
    } else if (OB_UNLIKELY(
            static_cast<int32_t>(origin_dbname_node->str_len_) > max_database_name_length)) {
      ret = OB_ERR_TOO_LONG_IDENT;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, (int)origin_dbname_node->str_len_, origin_dbname_node->str_value_);
    } else {
      origin_db_name.assign_ptr(origin_dbname_node->str_value_,
                                static_cast<int32_t>(origin_dbname_node->str_len_));
      restore_database_stmt->set_origin_db_name(origin_db_name);
    }
  }

  if (OB_SUCC(ret)) {
    ParseNode *new_db_node = parser_tree.children_[NEW_DB_NODE];
    if (NULL != new_db_node) {
      ObString new_db_name;
      if (OB_UNLIKELY(T_IDENT != new_db_node->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid parse tree", K(ret));
      } else if (OB_UNLIKELY(
          static_cast<int32_t>(new_db_node->str_len_) > max_database_name_length)) {
        ret = OB_ERR_TOO_LONG_IDENT;
        LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, (int)new_db_node->str_len_, new_db_node->str_value_);
      } else {
        new_db_name.assign_ptr(new_db_node->str_value_,
                               static_cast<int32_t>(new_db_node->str_len_));
        restore_database_stmt->set_new_db_name(new_db_name);
      }
    }
  }
  return ret;
}
} //namespace common
}
