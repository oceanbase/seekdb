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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_ALTER_SYSTEM_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_OB_ALTER_SYSTEM_EXECUTOR_

#include "share/ob_define.h"
#include "sql/resolver/cmd/ob_alter_system_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObAdminZoneStmt;

#define DEF_SIMPLE_EXECUTOR(name)                          \
  class name##Executor                                     \
  {                                                        \
  public:                                                  \
    name##Executor() {}                                    \
    virtual ~name##Executor() {}                           \
    int execute(ObExecContext &ctx, name##Stmt &stmt);     \
  private:                                                 \
    DISALLOW_COPY_AND_ASSIGN(name##Executor);              \
  }

DEF_SIMPLE_EXECUTOR(ObFreeze);

DEF_SIMPLE_EXECUTOR(ObFlushCache);

DEF_SIMPLE_EXECUTOR(ObFlushKVCache);

DEF_SIMPLE_EXECUTOR(ObFlushIlogCache);

DEF_SIMPLE_EXECUTOR(ObFlushDagWarnings);


DEF_SIMPLE_EXECUTOR(ObAdminMerge);




DEF_SIMPLE_EXECUTOR(ObRefreshMemStat);

DEF_SIMPLE_EXECUTOR(ObWashMemFragmentation);

DEF_SIMPLE_EXECUTOR(ObRefreshIOCalibraiton);

DEF_SIMPLE_EXECUTOR(ObRefreshFullTextDict);

DEF_SIMPLE_EXECUTOR(ObSetConfig);

DEF_SIMPLE_EXECUTOR(ObClearMergeError);

DEF_SIMPLE_EXECUTOR(ObUpgradeVirtualSchema);




DEF_SIMPLE_EXECUTOR(ObSetTP);

DEF_SIMPLE_EXECUTOR(ObEnableSqlThrottle);

DEF_SIMPLE_EXECUTOR(ObDisableSqlThrottle);

DEF_SIMPLE_EXECUTOR(ObAddDisk);
DEF_SIMPLE_EXECUTOR(ObDropDisk);


DEF_SIMPLE_EXECUTOR(ObResetConfig);

class ObCancelTaskExecutor
{
public:
  ObCancelTaskExecutor()
  {
  }
  virtual ~ObCancelTaskExecutor()
  {
  }
  int execute(ObExecContext &ctx, ObCancelTaskStmt &stmt);

private:
  int parse_task_id(const common::ObString &task_id_str, share::ObTaskId &task_id);
  int fetch_sys_task_info(
      ObExecContext &ctx,
      const common::ObString &task_id,
      common::ObAddr &task_server);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCancelTaskExecutor);
};


#undef DEF_SIMPLE_EXECUTOR

} // end namespace sql
} // end namespace oceanbase
#endif // OCEANBASE_SQL_ENGINE_CMD_OB_ALTER_SYSTEM_EXECUTOR_
