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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_DROP_PROCEDURE_STMT_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_DROP_PROCEDURE_STMT_H_

#include "lib/container/ob_se_array.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"
#include "common/object/ob_object.h"
#include "share/schema/ob_table_schema.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObDropRoutineStmt : public ObDDLStmt
{
public:
  explicit ObDropRoutineStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_DROP_ROUTINE) {}
  ObDropRoutineStmt()
    : ObDDLStmt(stmt::T_DROP_ROUTINE) {}
  virtual ~ObDropRoutineStmt() {}

  obrpc::ObDropRoutineArg &get_routine_arg() { return routine_arg_; }
  const obrpc::ObDropRoutineArg &get_routine_arg() const { return routine_arg_; }
  virtual obrpc::ObDDLArg &get_ddl_arg() { return routine_arg_; }
  TO_STRING_KV(K_(routine_arg));
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropRoutineStmt);
private:
  obrpc::ObDropRoutineArg routine_arg_;
};

}
}


#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_DROP_PROCEDURE_STMT_H_ */
