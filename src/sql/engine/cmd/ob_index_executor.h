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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_INDEX_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_INDEX_EXECUTOR_H_
#include "lib/allocator/ob_allocator.h"
namespace oceanbase
{
namespace obrpc
{
struct ObCreateIndexArg;
struct ObDropIndexArg;
struct ObAlterTableRes;
class ObCommonRpcProxy;
}

namespace sql
{
class ObExecContext;

class ObCreateIndexStmt;
class ObSQLSessionInfo;

class ObCreateIndexExecutor
{
public:
  friend class ObAlterTableExecutor;
  ObCreateIndexExecutor();
  virtual ~ObCreateIndexExecutor();
  int execute(ObExecContext &ctx, ObCreateIndexStmt &stmt);
private:
  int set_drop_index_stmt_str(
      obrpc::ObDropIndexArg &drop_index_arg,
      common::ObIAllocator &allocator);
  int sync_check_index_status(sql::ObSQLSessionInfo &my_session,
        obrpc::ObCommonRpcProxy &common_rpc_proxy,
        const obrpc::ObCreateIndexArg &create_index_arg,
        const obrpc::ObAlterTableRes &res,
        common::ObIAllocator &allocator,
        bool is_update_global_indexes = false);
  int handle_session_exception(ObSQLSessionInfo &session);
};

class ObDropIndexStmt;
class ObDropIndexExecutor
{
public:
  ObDropIndexExecutor();
  virtual ~ObDropIndexExecutor();

  int execute(ObExecContext &ctx, ObDropIndexStmt &stmt);
  static int wait_drop_index_finish(
      const uint64_t tenant_id,
      const int64_t task_id,
      sql::ObSQLSessionInfo &session);
};

class ObFlashBackIndexStmt;
class ObFlashBackIndexExecutor {
public:
  ObFlashBackIndexExecutor() {}
  virtual ~ObFlashBackIndexExecutor() {}
  int execute(ObExecContext &ctx, ObFlashBackIndexStmt &stmt);
private:
};

class ObPurgeIndexStmt;
class ObPurgeIndexExecutor {
public:
  ObPurgeIndexExecutor() {}
  virtual ~ObPurgeIndexExecutor() {}
  int execute(ObExecContext &ctx, ObPurgeIndexStmt &stmt);
private:
};

}
}
#endif /* OCEANBASE_SRC_SQL_ENGINE_CMD_OB_INDEX_EXECUTOR_H_ */

