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
#include "ob_lock_table_stmt.h"

namespace oceanbase
{
namespace sql
{
int ObLockTableStmt::add_mysql_lock_node(const ObMySQLLockNode &node)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!node.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lock node invalid", K(ret), K(node));
  } else if (OB_FAIL(mysql_lock_list_.push_back(node))) {
    LOG_WARN("add mysql lock node failed", K(ret), K(node));
  }
  return ret;
}

} // sql
} // oceanbase
