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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_TCL_OB_TCL_STMT_H_
#define OCEANBASE_SRC_SQL_RESOLVER_TCL_OB_TCL_STMT_H_ 1
#include "share/ob_rpc_struct.h"
#include "sql/resolver/ob_stmt.h"
#include "sql/resolver/ob_cmd.h"
namespace oceanbase
{
namespace sql
{
class ObTCLStmt : public ObStmt, public ObICmd
{
public:
  ObTCLStmt(common::ObIAllocator *name_pool, stmt::StmtType type)
  : ObStmt(name_pool, type)
  {
  }
  explicit ObTCLStmt(stmt::StmtType type): ObStmt(type)
  {
  }
  virtual ~ObTCLStmt() {}
  virtual int get_cmd_type() const { return get_stmt_type(); }
private:
  DISALLOW_COPY_AND_ASSIGN(ObTCLStmt);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_TCL_OB_TCL_STMT_H_ */
