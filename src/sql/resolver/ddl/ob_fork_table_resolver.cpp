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
#include "sql/resolver/ddl/ob_fork_table_resolver.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/ob_rpc_struct.h"
#include "sql/parser/parse_node.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace share;
using namespace obrpc;
namespace sql
{

ObForkTableResolver::ObForkTableResolver(ObResolverParams &params)
  : ObDDLResolver(params)
{
}

ObForkTableResolver::~ObForkTableResolver()
{
}

int ObForkTableResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObForkTableStmt *fork_table_stmt = NULL;

  if (OB_ISNULL(session_info_) ||
      OB_ISNULL(allocator_) ||
      T_FORK_TABLE != parse_tree.type_ ||
      MAX_NODE != parse_tree.num_child_ ||
      OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "invalid parse tree!", K(ret));
  } else if (OB_ISNULL(fork_table_stmt = create_stmt<ObForkTableStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SQL_RESV_LOG(ERROR, "create fork table stmt failed", K(ret));
  } else {
    stmt_ = fork_table_stmt;
    obrpc::ObForkTableArg &fork_table_arg = fork_table_stmt->get_fork_table_arg();
    fork_table_arg.tenant_id_ = session_info_->get_effective_tenant_id();
    fork_table_arg.if_not_exist_ = false;
  }

  if (OB_SUCC(ret)) {
    obrpc::ObForkTableArg &fork_table_arg = fork_table_stmt->get_fork_table_arg();
    ParseNode *dst_table_node = parse_tree.children_[DST_TABLE_NODE];
    ParseNode *src_table_node = parse_tree.children_[SRC_TABLE_NODE];
    ObString dst_database_name;
    ObString dst_table_name;
    ObString src_database_name;
    ObString src_table_name;

    if (OB_ISNULL(dst_table_node) || OB_ISNULL(src_table_node)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid parse tree!", K(ret));
    } else if (OB_FAIL(resolve_table_relation_node(dst_table_node, dst_table_name, dst_database_name))) {
      SQL_RESV_LOG(WARN, "failed to resolve destination table", K(ret));
    } else if (OB_FAIL(deep_copy_str(dst_database_name, fork_table_arg.dst_database_name_))) {
      SQL_RESV_LOG(WARN, "failed to deep copy dst database name", K(ret));
    } else if (OB_FAIL(deep_copy_str(dst_table_name, fork_table_arg.dst_table_name_))) {
      SQL_RESV_LOG(WARN, "failed to deep copy dst table name", K(ret));
    } else if (OB_FAIL(resolve_table_relation_node(src_table_node, src_table_name, src_database_name))) {
      SQL_RESV_LOG(WARN, "failed to resolve source table", K(ret));
    } else if (OB_FAIL(deep_copy_str(src_database_name, fork_table_arg.src_database_name_))) {
      SQL_RESV_LOG(WARN, "failed to deep copy src database name", K(ret));
    } else if (OB_FAIL(deep_copy_str(src_table_name, fork_table_arg.src_table_name_))) {
      SQL_RESV_LOG(WARN, "failed to deep copy src table name", K(ret));
    }
  }

  return ret;
}

}
}
