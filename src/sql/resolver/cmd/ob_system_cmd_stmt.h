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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_SYSTEM_CMD_STMT_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_SYSTEM_CMD_STMT_

#include "sql/resolver/ob_stmt.h"
#include "sql/resolver/ob_cmd.h"
namespace oceanbase
{
namespace sql
{
class ObSystemCmdStmt : public ObStmt, public ObICmd
{
public:
  ObSystemCmdStmt(common::ObIAllocator* name_pool, stmt::StmtType type)
  : ObStmt(name_pool, type)
  {}
  explicit ObSystemCmdStmt(stmt::StmtType type) : ObStmt(type)
  {}
  virtual ~ObSystemCmdStmt() {}
  virtual int get_cmd_type() const { return get_stmt_type(); }
private:
  DISALLOW_COPY_AND_ASSIGN(ObSystemCmdStmt);
};
} // namespace sql
} // namespace oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_CMD_OB_SYSTEM_CMD_STMT_ */
