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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_DATABASE_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_DATABASE_EXECUTOR_H_
#include "share/ob_define.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreateDatabaseStmt;
class ObDropDatabaseStmt;
class ObUseDatabaseStmt;
class ObAlterDatabaseStmt;
class ObFlashBackDatabaseStmt;
class ObPurgeDatabaseStmt;
class ObCreateDatabaseExecutor
{
public:
  ObCreateDatabaseExecutor();
  virtual ~ObCreateDatabaseExecutor();
  int execute(ObExecContext &ctx, ObCreateDatabaseStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateDatabaseExecutor);
};

///////////////////////
class ObUseDatabaseExecutor
{
public:
  ObUseDatabaseExecutor();
  virtual ~ObUseDatabaseExecutor();
  int execute(ObExecContext &ctx, ObUseDatabaseStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObUseDatabaseExecutor);
};

///////////////////////
class ObAlterDatabaseExecutor
{
public:
  ObAlterDatabaseExecutor();
  virtual ~ObAlterDatabaseExecutor();
  int execute(ObExecContext &ctx, ObAlterDatabaseStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterDatabaseExecutor);
};

/////////////////////
class ObDropDatabaseExecutor
{
public:
  ObDropDatabaseExecutor();
  virtual ~ObDropDatabaseExecutor();
  int execute(ObExecContext &ctx, ObDropDatabaseStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropDatabaseExecutor);
};


/* *
 *
 * */
class ObFlashBackDatabaseExecutor
{
public:
  ObFlashBackDatabaseExecutor() {}
  virtual ~ObFlashBackDatabaseExecutor() {}
  int execute(ObExecContext &ctx, ObFlashBackDatabaseStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObFlashBackDatabaseExecutor);
};

class ObPurgeDatabaseExecutor
{
public:
  ObPurgeDatabaseExecutor() {}
  virtual ~ObPurgeDatabaseExecutor() {}
  int execute(ObExecContext &ctx, ObPurgeDatabaseStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObPurgeDatabaseExecutor);
};

}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_ENGINE_CMD_OB_DATABASE_EXECUTOR_H_ */
