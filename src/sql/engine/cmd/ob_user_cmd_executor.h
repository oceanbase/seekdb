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

#ifndef OCEANBASE_SQL_ENGINE_CMD_USER_CMD_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_USER_CMD_EXECUTOR_
#include "share/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_array_serialization.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace obrpc
{
class ObCommonRpcProxy;
struct ObCreateUserArg;
struct ObLockUserArg;
struct ObRenameUserArg;
struct ObDropUserArg;
}
namespace sql
{
class ObExecContext;
class ObCreateUserStmt;
class ObDropUserExecutor;
class ObCreateUserExecutor
{
public:
  ObCreateUserExecutor() {}
  virtual ~ObCreateUserExecutor() {}
  int execute(ObExecContext &ctx, ObCreateUserStmt &stmt);
  static int encrypt_passwd(const common::ObString& passwd,
                            common::ObString& encrypted_passwd,
                            char *enc_buf,
                            int64_t buf_len);
  static int userinfo_extract_user_name(
      const common::ObIArray<share::schema::ObUserInfo> &user_infos,
      const common::ObIArray<int64_t> &index,
      common::ObIArray<common::ObString> &users,
      common::ObIArray<common::ObString> &hosts);

  static int check_user_valid(share::schema::ObSchemaGetterGuard& schema_guard, 
                              uint64_t priv_set,
                              int64_t tenant_id,
                              const common::ObString &user_name,
                              const common::ObString &host_name,
                              const common::ObString &opreation_name);
private:
  int create_user(obrpc::ObCommonRpcProxy *rpc_proxy,
                  const obrpc::ObCreateUserArg &arg) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateUserExecutor);
};

class ObDropUserStmt;
class ObDropUserExecutor
{
public:
  ObDropUserExecutor() {}
  virtual ~ObDropUserExecutor() {}
  static int build_fail_msg(const common::ObIArray<common::ObString> &users,
                            const common::ObIArray<common::ObString> &hosts,
                            common::ObSqlString &msg);
  static int string_array_index_extract(const common::ObIArray<common::ObString> &src_users,
                                        const common::ObIArray<common::ObString> &src_hosts,
                                        const common::ObIArray<int64_t> &index,
                                        common::ObIArray<common::ObString> &dst_users,
                                        common::ObIArray<common::ObString> &dst_hosts);

  static int build_fail_msg_for_one(const ObString &user, 
                                    const ObString &host, 
                                    common::ObSqlString &msg);
  static int drop_user(obrpc::ObCommonRpcProxy *rpc_proxy,
                       const obrpc::ObDropUserArg &arg,
                       bool if_exists);
  int execute(ObExecContext &ctx, ObDropUserStmt &stmt);

private:
  DISALLOW_COPY_AND_ASSIGN(ObDropUserExecutor);
};

class ObLockUserStmt;
class ObLockUserExecutor
{
public:
  ObLockUserExecutor() {}
  virtual ~ObLockUserExecutor() {}
  int execute(ObExecContext &ctx, ObLockUserStmt &stmt);

private:
  int lock_user(obrpc::ObCommonRpcProxy *rpc_proxy,
                const obrpc::ObLockUserArg &arg);
  DISALLOW_COPY_AND_ASSIGN(ObLockUserExecutor);
};

class ObAlterUserProfileStmt;
class ObAlterUserProfileExecutor
{
private:
  int set_role_exec(ObExecContext &ctx, ObAlterUserProfileStmt &stmt);  
public:
  ObAlterUserProfileExecutor() {}
  virtual ~ObAlterUserProfileExecutor() {}
  int execute(ObExecContext &ctx, ObAlterUserProfileStmt &stmt);

  DISALLOW_COPY_AND_ASSIGN(ObAlterUserProfileExecutor);
};

class ObRenameUserStmt;
class ObRenameUserExecutor
{
public:
  ObRenameUserExecutor() {}
  virtual ~ObRenameUserExecutor() {}
  int execute(ObExecContext &ctx, ObRenameUserStmt &stmt);

private:
  int rename_user(obrpc::ObCommonRpcProxy *rpc_proxy,
                  const obrpc::ObRenameUserArg &arg);
  DISALLOW_COPY_AND_ASSIGN(ObRenameUserExecutor);
};

class ObAlterUserPrimaryZoneStmt;
class ObAlterUserPrimaryZoneExecutor
{
public:
  ObAlterUserPrimaryZoneExecutor() {}
  virtual ~ObAlterUserPrimaryZoneExecutor() {}
  int execute(ObExecContext &ctx, ObAlterUserPrimaryZoneStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterUserPrimaryZoneExecutor);
};

}
}
#endif //OCEANBASE_SQL_ENGINE_CMD_USER_CMD_EXECUTOR_
