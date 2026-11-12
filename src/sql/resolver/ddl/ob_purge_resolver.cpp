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
#include "sql/resolver/ddl/ob_purge_resolver.h"

namespace oceanbase
{
using namespace common;

namespace sql
{
/**
 * Purge table
 */
int ObPurgeTableResolver::resolve(const peg::Node &parser_tree)
{
  int ret = OB_SUCCESS;
  ObPurgeTableStmt *purge_table_stmt = NULL;
  if (OB_ISNULL(session_info_) || peg::NodeKind::PurgeTable != parser_tree.kind()) {
    ret = OB_ERR_UNEXPECTED;
  }
  //create Purge table stmt
  if (OB_SUCC(ret)) {
    if (NULL == (purge_table_stmt = create_stmt<ObPurgeTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create purege table stmt", K(ret));
    } else {
      stmt_ = purge_table_stmt;
    }
  }
  if (OB_SUCC(ret)) {
    //Purge table
    const auto *table_node = &peg::purge_relation(parser_tree);
    
    uint64_t db_id = OB_INVALID_ID;
    ObString db_name;
    ObString table_name;
    if (OB_FAIL(resolve_table_relation_node(table_node, table_name, db_name))) {
    } else if (session_info_->get_database_name() != db_name) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "purge tables in recyclebin dropped from other schema");
    } else if (OB_FAIL(schema_checker_->get_database_id(db_name, db_id))) {
    } else if (table_name.empty()){
      ret = OB_INVALID_ARGUMENT;
    } else {
      
      purge_table_stmt->set_database_id(db_id);
      purge_table_stmt->set_table_name(table_name);
    }
  }
  return ret;
}

/**
 * Purge index
 */
int ObPurgeIndexResolver::resolve(const peg::Node &parser_tree)
{
  int ret = OB_SUCCESS;
  ObPurgeIndexStmt *purge_index_stmt = NULL;
  if (OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (peg::NodeKind::PurgeIndex != parser_tree.kind()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree",  K(parser_tree.kind()));
  }
  //create Purge table stmt
  if (OB_SUCC(ret)) {
    if (NULL == (purge_index_stmt = create_stmt<ObPurgeIndexStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create purege table stmt", K(ret));
    } else {
      stmt_ = purge_index_stmt;
    }
  }
  if (OB_SUCC(ret)) {
    //Purge table
    const auto *table_node = &peg::purge_relation(parser_tree);
    
    uint64_t db_id = OB_INVALID_ID;
    ObString db_name;
    ObString table_name;

    const share::schema::ObTableSchema *table_schema = NULL;
    if (OB_FAIL(resolve_table_relation_node(table_node,
                                           table_name,
                                           db_name))){
    } else if (session_info_->get_database_name() != db_name){
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "purge indexes in recyclebin dropped from other schema");
    } else if (OB_FAIL(schema_checker_->get_database_id(db_name, db_id))) {
    } else if (table_name.empty()){
      ret = OB_INVALID_ARGUMENT;
    } else {
      UNUSED(schema_checker_->get_table_schema(
                                               OB_RECYCLEBIN_SCHEMA_ID,
                                               table_name,
                                               true, /*is_index*/
                                               false, /*cte_table_fisrt*/
                                               false/*is_hidden*/,
                                               table_schema));
      
      purge_index_stmt->set_database_id(db_id);
      purge_index_stmt->set_table_name(table_name);
      purge_index_stmt->set_table_id(OB_NOT_NULL(table_schema) ? table_schema->get_table_id() : OB_INVALID_ID);
    }
  }
  return ret;
}
/**
 * Purge database
 */
int ObPurgeDatabaseResolver::resolve(const peg::Node &parser_tree)
{
  int ret = OB_SUCCESS;
  ObPurgeDatabaseStmt *purge_database_stmt = NULL;
  if (OB_ISNULL(session_info_) || peg::NodeKind::PurgeDatabase != parser_tree.kind()) {
    ret = OB_ERR_UNEXPECTED;
  }
  //create Purge table stmt
  if (OB_SUCC(ret)) {
    if (NULL == (purge_database_stmt = create_stmt<ObPurgeDatabaseStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create rename table stmt", K(ret));
    } else {
      stmt_ = purge_database_stmt;
    }
  }
  if (OB_SUCC(ret)) {
    
    ObString db_name;
    const auto &name = peg::database_name(parser_tree);
    const auto name_text = name.text();
    if (name_text.size() > OB_MAX_DATABASE_NAME_LENGTH) {
      ret = OB_ERR_TOO_LONG_IDENT;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, static_cast<int>(name_text.size()), syntax_text(name_text).ptr());
    } else if (OB_FAIL(ob_write_string(*allocator_, syntax_text(name_text), db_name))) {
    } else {
      if (db_name.empty()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        purge_database_stmt->set_db_name(db_name);
      }
    }
  }
  return ret;
}

/**
 * Purge Recyclebin
 */
int ObPurgeRecycleBinResolver::resolve(const peg::Node &parser_tree)
{
  int ret = OB_SUCCESS;
  ObPurgeRecycleBinStmt *purge_recyclebin_stmt = NULL;
  if (OB_ISNULL(session_info_) || peg::NodeKind::PurgeRecyclebin != parser_tree.kind()) {
    ret = OB_ERR_UNEXPECTED;
  }
  //create Purge table stmt
  if (OB_SUCC(ret)) {
    if (NULL == (purge_recyclebin_stmt = create_stmt<ObPurgeRecycleBinStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create purge statement", K(ret));
    } else {
      stmt_ = purge_recyclebin_stmt;
    }
  }
  if (OB_SUCC(ret)) {
    int64_t current_time = ObTimeUtility::current_time();
    
    purge_recyclebin_stmt->set_expire_time(current_time);
    purge_recyclebin_stmt->set_purge_num(obcall::ObPurgeRecycleBinArg::DEFAULT_PURGE_EACH_TIME);
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
