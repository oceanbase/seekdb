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

#ifndef OCEANBASE_RESOLVER_CMD_OB_SWITCHOVER_TENANT_STMT_H
#define OCEANBASE_RESOLVER_CMD_OB_SWITCHOVER_TENANT_STMT_H

#include "sql/resolver/cmd/ob_system_cmd_stmt.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace sql
{
class ObSwitchTenantStmt : public ObSystemCmdStmt
{
public:
  ObSwitchTenantStmt() : ObSystemCmdStmt(stmt::T_SWITCHOVER), arg_()
  {
  }
  virtual ~ObSwitchTenantStmt()
  {
  }

  obrpc::ObSwitchTenantArg  &get_arg() { return arg_; }
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(arg));
private:
  obrpc::ObSwitchTenantArg arg_;
};

} //end sql
} //end oceanbase
#endif

