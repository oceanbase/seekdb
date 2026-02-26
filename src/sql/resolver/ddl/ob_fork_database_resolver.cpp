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
#include "sql/resolver/ddl/ob_fork_database_resolver.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/ob_rpc_struct.h"
#include "sql/parser/parse_node.h"
#include "sql/ob_sql_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace share;
using namespace obrpc;
namespace sql
{

ObForkDatabaseResolver::ObForkDatabaseResolver(ObResolverParams &params)
  : ObDDLResolver(params)
{
}

ObForkDatabaseResolver::~ObForkDatabaseResolver()
{
}

int ObForkDatabaseResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObForkDatabaseStmt *fork_database_stmt = NULL;

  if (OB_ISNULL(session_info_) ||
      OB_ISNULL(allocator_) ||
      T_FORK_DATABASE != parse_tree.type_ ||
      MAX_NODE != parse_tree.num_child_ ||
      OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "invalid parse tree!", K(ret));
  } else if (OB_ISNULL(fork_database_stmt = create_stmt<ObForkDatabaseStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SQL_RESV_LOG(ERROR, "create fork database stmt failed", K(ret));
  } else {
    stmt_ = fork_database_stmt;
    obrpc::ObForkDatabaseArg &fork_database_arg = fork_database_stmt->get_fork_database_arg();
    fork_database_arg.tenant_id_ = session_info_->get_effective_tenant_id();
    fork_database_arg.if_not_exist_ = false;
  }

  if (OB_SUCC(ret)) {
    obrpc::ObForkDatabaseArg &fork_database_arg = fork_database_stmt->get_fork_database_arg();
    ParseNode *dst_database_node = parse_tree.children_[DST_DATABASE_NODE];
    ParseNode *src_database_node = parse_tree.children_[SRC_DATABASE_NODE];
    ObString dst_database_name;
    ObString src_database_name;

    if (OB_ISNULL(dst_database_node) || OB_ISNULL(src_database_node)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid parse tree!", K(ret));
    } else if (T_IDENT != dst_database_node->type_ || T_IDENT != src_database_node->type_) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid parse tree node type!", K(ret),
                   K(dst_database_node->type_), K(src_database_node->type_));
    } else {
      // Get destination database name
      dst_database_name.assign_ptr(dst_database_node->str_value_,
                                   static_cast<int32_t>(dst_database_node->str_len_));
      // Get source database name
      src_database_name.assign_ptr(src_database_node->str_value_,
                                   static_cast<int32_t>(src_database_node->str_len_));

      // Check and convert database names
      ObNameCaseMode mode = OB_NAME_CASE_INVALID;
      if (OB_FAIL(session_info_->get_name_case_mode(mode))) {
        SQL_RESV_LOG(WARN, "fail to get name case mode", K(mode), K(ret));
      } else {
        bool perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
        ObCollationType cs_type = CS_TYPE_INVALID;
        if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
          SQL_RESV_LOG(WARN, "fail to get collation_connection", K(ret));
        } else if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(
                    cs_type, perserve_lettercase, dst_database_name))) {
          SQL_RESV_LOG(WARN, "fail to check and convert dst database name", K(dst_database_name), K(ret));
        } else if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(
                    cs_type, perserve_lettercase, src_database_name))) {
          SQL_RESV_LOG(WARN, "fail to check and convert src database name", K(src_database_name), K(ret));
        } else if (OB_FAIL(deep_copy_str(dst_database_name, fork_database_arg.dst_database_name_))) {
          SQL_RESV_LOG(WARN, "failed to deep copy dst database name", K(ret));
        } else if (OB_FAIL(deep_copy_str(src_database_name, fork_database_arg.src_database_name_))) {
          SQL_RESV_LOG(WARN, "failed to deep copy src database name", K(ret));
        }
      }
    }
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
