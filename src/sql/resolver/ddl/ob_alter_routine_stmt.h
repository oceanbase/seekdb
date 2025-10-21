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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_ALTER_ROUTINE_STMT_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_ALTER_ROUTINE_STMT_H_

#include "lib/allocator/ob_allocator.h"
#include "share/ob_rpc_struct.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "sql/resolver/ddl/ob_create_routine_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObAlterRoutineStmt : public ObCreateRoutineStmt
{
public:
  explicit ObAlterRoutineStmt(common::ObIAllocator *name_pool)
      : ObCreateRoutineStmt(name_pool, stmt::T_ALTER_ROUTINE) {}
  ObAlterRoutineStmt() : ObCreateRoutineStmt(stmt::T_ALTER_ROUTINE) {}
  virtual ~ObAlterRoutineStmt() {}

private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterRoutineStmt);
};

}
}



#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_ALTER_ROUTINE_STMT_H_ */
