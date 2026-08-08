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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/ob_lock_table_executor.h"
#include "sql/resolver/ddl/ob_lock_table_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/tablelock/ob_mysql_lock_table_executor.h"
#include "data_plane/tablelock/ob_table_lock_target.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace transaction::tablelock;
namespace sql
{

int ObLockTableExecutor::execute(ObExecContext &ctx,
                                 ObLockTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("execute mysql lock table");
  ret = execute_mysql_(ctx, stmt);
  return ret;
}

int ObLockTableExecutor::execute_mysql_(ObExecContext &ctx,
                                        ObLockTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  // only execute normally after enable lock_priority configuration, otherwise
  // it will directly throw OB_SUCCESS, which is an empty implementation
  if (GCONF.enable_lock_priority) {
    switch(stmt.get_lock_stmt_type()) {
    case ObLockTableStmt::MYSQL_LOCK_TABLE_STMT: {
      ObMySQLLockTableExecutor executor;
      ObSEArray<data_plane::ObTableLockTarget, 2> lock_targets;
      const ObIArray<ObMySQLLockNode> &lock_nodes = stmt.get_mysql_lock_list();
      for (int64_t i = 0; OB_SUCC(ret) && i < lock_nodes.count(); ++i) {
        const ObMySQLLockNode &node = lock_nodes.at(i);
        if (OB_UNLIKELY(!node.is_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("lock node invalid", K(ret), K(node));
        } else if (OB_FAIL(lock_targets.push_back(data_plane::ObTableLockTarget(
                       node.table_item_->ref_id_,
                       static_cast<ObTableLockMode>(node.lock_mode_))))) {
          LOG_WARN("failed to append table lock target", K(ret));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(executor.execute(ctx, lock_targets))) {
        LOG_WARN("lock table failed", K(ret));
      }
      break;
    }
    case ObLockTableStmt::MYSQL_UNLOCK_TABLE_STMT: {
      ObMySQLUnlockTableExecutor executor;
      if (OB_FAIL(executor.execute(ctx))) {
        LOG_WARN("unlock table failed", K(ret));
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown lock statement type", K(ret), K(stmt.get_lock_stmt_type()));
    }
    }
  }
  LOG_DEBUG("execute mysql lock table", K(ctx), K(stmt));
  return ret;
}

} // namespace sql
} // namespace oceanbase
