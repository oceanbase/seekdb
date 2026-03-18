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

#ifndef OCEANBASE_RESOLVER_CMD_OB_SWITCHOVER_TENANT_RESOLVER_H
#define OCEANBASE_RESOLVER_CMD_OB_SWITCHOVER_TENANT_RESOLVER_H

#include "sql/resolver/cmd/ob_system_cmd_resolver.h"
#include "sql/resolver/cmd/ob_switch_role_stmt.h"

namespace oceanbase
{
namespace sql
{


class ObSwitchRoleResolver : public ObSystemCmdResolver
{
public:
  ObSwitchRoleResolver(ObResolverParams &params) : ObSystemCmdResolver(params)
  {
  }
  virtual ~ObSwitchRoleResolver();
  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_switch_role(const ParseNode &parse_tree);
};


} //end sql
} //end oceanbase

#endif
