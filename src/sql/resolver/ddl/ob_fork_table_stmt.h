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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_FORK_TABLE_STMT_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_FORK_TABLE_STMT_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObForkTableStmt : public ObDDLStmt
{
public:
  explicit ObForkTableStmt(common::ObIAllocator *name_pool);
  ObForkTableStmt();
  virtual ~ObForkTableStmt();

  const obrpc::ObForkTableArg &get_fork_table_arg() const { return fork_table_arg_; }
  obrpc::ObForkTableArg &get_fork_table_arg() { return fork_table_arg_; }
  virtual bool cause_implicit_commit() const { return true; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return fork_table_arg_; }

  TO_STRING_KV(K_(stmt_type), K_(fork_table_arg));
private:
  obrpc::ObForkTableArg fork_table_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObForkTableStmt);
};

}
}

#endif //OCEANBASE_SQL_RESOLVER_DDL_OB_FORK_TABLE_STMT_
