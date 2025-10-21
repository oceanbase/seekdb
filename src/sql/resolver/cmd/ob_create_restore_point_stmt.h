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

#ifndef OCEANBASE_SQL_OB_CREATE_RESTORE_POINT_STMT_H_
#define OCEANBASE_SQL_OB_CREATE_RESTORE_POINT_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/cmd/ob_system_cmd_stmt.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObCreateRestorePointStmt : public ObSystemCmdStmt
{
public:
  explicit ObCreateRestorePointStmt(common::ObIAllocator *name_pool)
    :  ObSystemCmdStmt(name_pool, stmt::T_CREATE_RESTORE_POINT),
       create_restore_point_arg_(),
       restore_point_name_()
       {}
  ObCreateRestorePointStmt()
    :  ObSystemCmdStmt(stmt::T_CREATE_RESTORE_POINT),
       create_restore_point_arg_(),
       restore_point_name_()
       {}
  virtual ~ObCreateRestorePointStmt() {}
  inline obrpc::ObCreateRestorePointArg &get_create_restore_point_arg()
  {
    return create_restore_point_arg_;
  }

  void set_tenant_id(const int64_t tenant_id)
  {
    create_restore_point_arg_.tenant_id_ = tenant_id;
  }
  void set_restore_point_name(const common::ObString &restore_point_name)
  {
    restore_point_name_ = restore_point_name;
    create_restore_point_arg_.name_ = restore_point_name;
  }
  ObString get_restore_point_name() { return restore_point_name_; }
private:
  obrpc::ObCreateRestorePointArg create_restore_point_arg_;
  ObString restore_point_name_;
  DISALLOW_COPY_AND_ASSIGN(ObCreateRestorePointStmt);
};

} /* sql */
} /* oceanbase */
#endif //OCEANBASE_SQL_OB_CREATE_TENANT_STMT_H_
