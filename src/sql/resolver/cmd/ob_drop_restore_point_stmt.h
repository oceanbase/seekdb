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

#ifndef OCEANBASE_SQL_OB_DROP_RESTORE_POINT_STMT_H_
#define OCEANBASE_SQL_OB_DROP_RESTORE_POINT_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/cmd/ob_system_cmd_stmt.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObDropRestorePointStmt : public ObSystemCmdStmt
{
public:
  explicit ObDropRestorePointStmt(common::ObIAllocator *name_pool)
    :  ObSystemCmdStmt(name_pool, stmt::T_DROP_RESTORE_POINT),
       drop_restore_point_arg_(),
       restore_point_name_()
       {}
  ObDropRestorePointStmt()
    :  ObSystemCmdStmt(stmt::T_DROP_RESTORE_POINT),
       drop_restore_point_arg_(),
       restore_point_name_()
       {}
  virtual ~ObDropRestorePointStmt() {}
  inline obrpc::ObDropRestorePointArg &get_drop_restore_point_arg()
  {
    return drop_restore_point_arg_;
  }
  void set_tenant_id(const int64_t tenant_id)
  {
    drop_restore_point_arg_.tenant_id_ = tenant_id;
  }
  void set_restore_point_name(const common::ObString &restore_point_name)
  {
    restore_point_name_ = restore_point_name;
    drop_restore_point_arg_.name_ = restore_point_name;
  }
  ObString get_restore_point_name() { return restore_point_name_; }
private:
  obrpc::ObDropRestorePointArg drop_restore_point_arg_;
  ObString restore_point_name_;
  DISALLOW_COPY_AND_ASSIGN(ObDropRestorePointStmt);
};

} /* sql */
} /* oceanbase */
#endif //OCEANBASE_SQL_OB_CREATE_TENANT_STMT_H_
