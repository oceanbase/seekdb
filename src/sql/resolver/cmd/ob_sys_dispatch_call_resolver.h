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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_CMD_OB_SYS_DISPATCH_CALL_H_
#define OCEANBASE_SRC_SQL_RESOLVER_CMD_OB_SYS_DISPATCH_CALL_H_

#include "sql/resolver/cmd/ob_cmd_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObSysDispatchCallResolver final : public ObCMDResolver
{
public:
  explicit ObSysDispatchCallResolver(ObResolverParams &params) : ObCMDResolver(params) {}
  virtual ~ObSysDispatchCallResolver() {}
  DISABLE_COPY_ASSIGN(ObSysDispatchCallResolver);

  virtual int resolve(const ParseNode &parse_tree);

private:
  int check_sys_dispatch_call_priv(const ParseNode &name_node);

  static const char *const WHITELIST[][2];
};

}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SRC_SQL_RESOLVER_CMD_OB_SYS_DISPATCH_CALL_H_
