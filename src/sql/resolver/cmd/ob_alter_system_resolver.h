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

#ifndef OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_RESOLVER_
#define OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_RESOLVER_

#include "sql/resolver/cmd/ob_system_cmd_resolver.h"
#include "sql/session/ob_sql_session_info.h" // ObSqlSessionInfo

namespace oceanbase
{
namespace sql
{

#define DEF_SIMPLE_CMD_RESOLVER(name) \
  class name : public ObSystemCmdResolver \
  { \
  public: \
    name(ObResolverParams &params) : ObSystemCmdResolver(params) {} \
    virtual ~name() {} \
    int resolve(const peg::Node &statement); \
  };

DEF_SIMPLE_CMD_RESOLVER(ObFreezeResolver);
DEF_SIMPLE_CMD_RESOLVER(MergeControlResolver);
DEF_SIMPLE_CMD_RESOLVER(ClearMergeErrorResolver);
DEF_SIMPLE_CMD_RESOLVER(CancelTaskResolver);
DEF_SIMPLE_CMD_RESOLVER(FlushDagWarningsResolver);
DEF_SIMPLE_CMD_RESOLVER(RefreshIoCalibrationResolver);
DEF_SIMPLE_CMD_RESOLVER(SwitchRoleResolver);
DEF_SIMPLE_CMD_RESOLVER(ObFlushCacheResolver);
DEF_SIMPLE_CMD_RESOLVER(ObFlushKVCacheResolver);
DEF_SIMPLE_CMD_RESOLVER(ObSetTPResolver);
DEF_SIMPLE_CMD_RESOLVER(ObSetConfigResolver);
DEF_SIMPLE_CMD_RESOLVER(ObResetConfigResolver);

#undef DEF_SIMPLE_CMD_RESOLVER

} // end namespace sql
} // end namespace oceanbase
#endif // OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_RESOLVER_
