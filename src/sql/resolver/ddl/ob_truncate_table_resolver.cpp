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

#define USING_LOG_PREFIX SERVER
#include "sql/resolver/ddl/ob_truncate_table_resolver.h"

namespace oceanbase
{
using namespace share;
using namespace share::schema;
using namespace common;

namespace sql
{

ObTruncateTableResolver::ObTruncateTableResolver(ObResolverParams &params)
    : ObDDLResolver(params)
{
}

ObTruncateTableResolver::~ObTruncateTableResolver()
{
}

int ObTruncateTableResolver::resolve(const ParseNode &parser_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parser_tree);
  ObTruncateTableStmt *truncate_table_stmt = NULL;
  bool is_mysql_tmp_table = false;
  if (OB_ISNULL(session_info_) || OB_ISNULL(node) ||
      T_TRUNCATE_TABLE != node->type_ ||
      OB_ISNULL(node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info_ is null or parser error", K(ret));
  }
  //create alter table stmt
  if (OB_SUCC(ret)) {
    if (NULL == (truncate_table_stmt = create_stmt<ObTruncateTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create alter table stmt", K(ret));
    } else {
      stmt_ = truncate_table_stmt;
    }
    ParseNode *relation_node = node->children_[TABLE_NODE];
    if (OB_SUCC(ret)) {
      if (NULL != relation_node) {
      //resolve table
        ObString table_name;
        ObString database_name;
        if (OB_FAIL(resolve_table_relation_node(relation_node,
                                                table_name,
                                                database_name))) {
          LOG_WARN("failed to resolve table name.",
                       K(table_name), K(database_name), K(ret));
        } else {
          truncate_table_stmt->set_table_name(table_name);
          truncate_table_stmt->set_database_name(database_name);
          
        }
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("relation node should not be null!", K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    const ObTableSchema *orig_table_schema = NULL;
    if (OB_FAIL(schema_checker_->get_table_schema(
                                                  truncate_table_stmt->get_database_name(),
                                                  truncate_table_stmt->get_table_name(),
                                                  false,
                                                  orig_table_schema))) {
      LOG_WARN("fail to get table schema", K(ret), K(truncate_table_stmt->get_table_name()));
      if (NULL == orig_table_schema && OB_TABLE_NOT_EXIST == ret) {
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST,
                       helper.convert(truncate_table_stmt->get_database_name()),
                       helper.convert(truncate_table_stmt->get_table_name()));
      }
    } else {
      if (orig_table_schema->is_mysql_tmp_table()) {
        is_mysql_tmp_table = true; 
      }

    }
  }

  return ret;
}

} //namespace common
} //namespace oceanbase
