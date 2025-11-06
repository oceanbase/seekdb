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

#ifndef OCEANBASE_SQL_ENGINE_CMD_ROLE_CMD_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_ROLE_CMD_EXECUTOR_
#include "lib/string/ob_string.h"
#include "lib/container/ob_array_serialization.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace obrpc
{
class ObCommonRpcProxy;
struct ObCreateRoleArg;
class ObDropUserArg;
struct ObAlterRoleArg;
}
namespace sql
{
class ObExecContext;
class ObCreateRoleStmt;
class ObDropRoleStmt;
class ObAlterRoleStmt;
class ObCreateRoleExecutor
{
public:
  ObCreateRoleExecutor() {}
  virtual ~ObCreateRoleExecutor() {}
  int execute(ObExecContext &ctx, ObCreateRoleStmt &stmt);
  static int encrypt_passwd(const common::ObString& passwd,
                            common::ObString& encrypted_passwd,
                            char *enc_buf,
                            int64_t buf_len);
private:
  int create_role(obrpc::ObCommonRpcProxy *rpc_proxy,
                  const obrpc::ObCreateRoleArg &arg) const;
  int drop_role(obrpc::ObCommonRpcProxy *rpc_proxy,
                  const obrpc::ObDropUserArg &arg) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateRoleExecutor);
};

class ObDropRoleExecutor
{
public:
  ObDropRoleExecutor() {}
  virtual ~ObDropRoleExecutor() {}
  int execute(ObExecContext &ctx, ObDropRoleStmt &stmt);
  static int encrypt_passwd(const common::ObString& passwd,
                            common::ObString& encrypted_passwd,
                            char *enc_buf,
                            int64_t buf_len);
private:
  int drop_role(obrpc::ObCommonRpcProxy *rpc_proxy,
                  const obrpc::ObDropUserArg &arg) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropRoleExecutor);
};

class ObAlterRoleExecutor
{
public:
  ObAlterRoleExecutor() {}
  virtual ~ObAlterRoleExecutor() {}
  int execute(ObExecContext &ctx, ObAlterRoleStmt &stmt);
  static int encrypt_passwd(const common::ObString& passwd,
                            common::ObString& encrypted_passwd,
                            char *enc_buf,
                            int64_t buf_len);
private:
  int alter_role(obrpc::ObCommonRpcProxy *rpc_proxy,
                 const obrpc::ObAlterRoleArg &arg) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterRoleExecutor);
};

}
}
#endif //OCEANBASE_SQL_ENGINE_CMD_USER_CMD_EXECUTOR_
