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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_DCL_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_OB_DCL_EXECUTOR_

#include "share/ob_define.h"

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace obrpc
{
class ObCommonRpcProxy;
}

namespace sql
{
class ObExecContext;
class ObGrantStmt;
class ObGrantExecutor
{
public:
  ObGrantExecutor() {}
  virtual ~ObGrantExecutor() {}
  int execute(ObExecContext &ctx, ObGrantStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObGrantExecutor);
};

class ObRevokeStmt;
class ObRevokeExecutor
{
public:
  ObRevokeExecutor() {}
  virtual ~ObRevokeExecutor() {}
  int execute(ObExecContext &ctx, ObRevokeStmt &stmt);
private:
  int revoke_user(obrpc::ObCommonRpcProxy *rpc_proxy,
                  ObRevokeStmt &stmt);
  int revoke_catalog(obrpc::ObCommonRpcProxy *rpc_proxy,
                     ObRevokeStmt &stmt);
  int revoke_db(obrpc::ObCommonRpcProxy *rpc_proxy,
                ObRevokeStmt &stmt);
  int revoke_table(obrpc::ObCommonRpcProxy *rpc_proxy,
                   ObRevokeStmt &stmt,
                   ObExecContext &ctx);

  int revoke_routine(obrpc::ObCommonRpcProxy *rpc_proxy,
                     ObRevokeStmt &stmt,
                     ObExecContext &ctx);
  int revoke_object(obrpc::ObCommonRpcProxy *rpc_proxy,
                    ObRevokeStmt &stmt,
                    ObExecContext &ctx);
private:
  DISALLOW_COPY_AND_ASSIGN(ObRevokeExecutor);
};
}
}
#endif //OCEANBASE_SQL_ENGINE_CMD_OB_DCL_EXECUTOR_
