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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_CLEAR_BALANCE_TASK_H
#define OCEANBASE_SQL_RESOLVER_CMD_CLEAR_BALANCE_TASK_H
#include "sql/resolver/cmd/ob_system_cmd_stmt.h"
#include "share/ob_rpc_struct.h"
namespace oceanbase
{
namespace sql
{
class ObClearBalanceTaskStmt : public ObSystemCmdStmt
{
public:
  ObClearBalanceTaskStmt() : ObSystemCmdStmt(stmt::T_CLEAR_BALANCE_TASK),
    arg_()
  {
  }
  explicit ObClearBalanceTaskStmt(common::ObIAllocator *name_pool)
      : ObSystemCmdStmt(name_pool, stmt::T_CLEAR_BALANCE_TASK), arg_()
  {}

  virtual ~ObClearBalanceTaskStmt() {}

  common::ObIArray<uint64_t> &get_tenant_ids() { return arg_.tenant_ids_; }
  common::ObIArray<ObZone> &get_zone_names() { return arg_.zone_names_; }
  void set_type(const obrpc::ObAdminClearBalanceTaskArg::TaskType type) { arg_.type_ = type; }
  const obrpc::ObAdminClearBalanceTaskArg &get_rpc_arg() const { return arg_; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObClearBalanceTaskStmt);
  obrpc::ObAdminClearBalanceTaskArg arg_;
};
} //namespace sql
} //namespace oceanbase
#endif


