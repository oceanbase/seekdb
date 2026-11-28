// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/lock_table_resolver.h"

#include "rust/config.h"
#include "sql/resolver/ddl/ob_lock_table_stmt.h"

namespace oceanbase::sql {
using namespace common;

int LockTableResolver::resolve(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObLockTableStmt *statement = nullptr;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(statement = create_stmt<ObLockTableStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (config::enable_lock_priority()) {
    if (node.kind() == peg::NodeKind::UnlockTables) {
      statement->set_lock_stmt_type(ObLockTableStmt::MYSQL_UNLOCK_TABLE_STMT);
    } else {
      for (const auto &entry : peg::lock_tables(node)) {
        if (OB_FAIL(ret)) { break; }
        const auto lock = peg::table_lock(entry);
        TableItem *table = nullptr;
        if (OB_FAIL(ObDMLResolver::resolve_table(lock.table, table))) {
        } else if (OB_ISNULL(table)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (table->is_function_table() || table->is_json_table()) {
          ret = OB_WRONG_TABLE_NAME;
        } else {
          ObMySQLLockNode resolved;
          resolved.table_item_ = table;
          resolved.lock_mode_ = lock.write ? 1 : 2;
          OZ(statement->add_mysql_lock_node(resolved));
        }
      }
      OX(statement->set_lock_stmt_type(ObLockTableStmt::MYSQL_LOCK_TABLE_STMT));
    }
  }
  return ret;
}

}
