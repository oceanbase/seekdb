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

#include "sql/resolver/tcl/transaction_resolver.h"
#include "sql/resolver/statement_factory.h"
#include "rust/sql_parser.h"
#include "sql/ob_sql_context.h"
#include "sql/resolver/ob_resolver_define.h"
#include "sql/resolver/tcl/ob_end_trans_stmt.h"
#include "sql/resolver/tcl/ob_savepoint_stmt.h"
#include "sql/resolver/tcl/ob_start_trans_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase::sql
{
using namespace common;

namespace
{
int resolve_begin(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObStartTransStmt *start_stmt = nullptr;
  const auto options = peg::transaction_options(node);
  const share::schema::ObSimpleSysVariableSchema *variables = nullptr;
  if (OB_FAIL(create_resolved_statement(params, start_stmt))) {
  } else if (OB_ISNULL(params.schema_checker_)
             || OB_ISNULL(params.schema_checker_->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(params.schema_checker_->get_schema_guard()->get_sys_variable_schema(variables))) {
  } else if (OB_ISNULL(variables)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!params.session_info_->has_user_super_privilege()
             && variables->get_read_only()
             && options.access == peg::TransactionAccess::ReadWrite) {
    ret = OB_ERR_OPTION_PREVENTS_STATEMENT;
    LOG_WARN("the server is running with read_only, cannot execute stmt");
  } else {
    start_stmt->set_read_only(options.access == peg::TransactionAccess::Default
        ? params.session_info_->get_tx_read_only()
        : options.access == peg::TransactionAccess::ReadOnly);
    start_stmt->set_with_consistent_snapshot(options.consistent_snapshot);
    stmt = start_stmt;
  }
  return ret;
}

template <typename Statement>
int resolve_savepoint(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  Statement *savepoint_stmt = nullptr;
  ObString name;
  const auto node_text = node.text();
  const ObString source_name(syntax_text(node_text));
  if (OB_FAIL(create_resolved_statement(params, savepoint_stmt))) {
  } else if (OB_FAIL(ob_write_string(*params.allocator_, source_name, name))) {
  } else if (OB_FAIL(savepoint_stmt->set_sp_name(name.ptr(), name.length()))) {
  } else {
    stmt = savepoint_stmt;
  }
  return ret;
}
}

int resolve_transaction(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  stmt = nullptr;
  if (OB_ISNULL(params.allocator_) || OB_ISNULL(params.stmt_factory_)
      || OB_ISNULL(params.session_info_) || OB_ISNULL(params.query_ctx_)
      || OB_ISNULL(params.stmt_factory_->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    switch (node.kind()) {
      case peg::NodeKind::Begin:
        ret = resolve_begin(node, params, stmt);
        break;
      case peg::NodeKind::Commit:
      case peg::NodeKind::Rollback: {
        ObEndTransStmt *end_stmt = nullptr;
        if (OB_FAIL(create_resolved_statement(params, end_stmt))) {
        } else {
          end_stmt->set_is_rollback(node.kind() == peg::NodeKind::Rollback);
          stmt = end_stmt;
        }
        break;
      }
      case peg::NodeKind::Savepoint:
        ret = resolve_savepoint<ObCreateSavePointStmt>(node, params, stmt);
        break;
      case peg::NodeKind::RollbackSavepoint:
        ret = resolve_savepoint<ObRollbackSavePointStmt>(node, params, stmt);
        break;
      case peg::NodeKind::ReleaseSavepoint:
        ret = resolve_savepoint<ObReleaseSavePointStmt>(node, params, stmt);
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}
}
