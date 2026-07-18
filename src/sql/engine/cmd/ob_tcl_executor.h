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

#ifndef OCRANBASE_SQL_ENGINE_CMD_OB_TCL_CMD_EXECUTOR_
#define OCRANBASE_SQL_ENGINE_CMD_OB_TCL_CMD_EXECUTOR_

#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObEndTransStmt;
class ObEndTransExecutor
{
public:
  ObEndTransExecutor() {}
  virtual ~ObEndTransExecutor() {}
  int execute(ObExecContext &ctx, ObEndTransStmt &stmt);
private:
  int end_trans(ObExecContext &ctx, ObEndTransStmt &stmt);
  DISALLOW_COPY_AND_ASSIGN(ObEndTransExecutor);
};

class ObStartTransStmt;
class ObStartTransExecutor
{
public:
  ObStartTransExecutor() {}
  virtual ~ObStartTransExecutor() {}
  int execute(ObExecContext &ctx, ObStartTransStmt &stmt);
private:
  int start_trans(ObExecContext &ctx, ObStartTransStmt &stmt);
  DISALLOW_COPY_AND_ASSIGN(ObStartTransExecutor);
};

class ObCreateSavePointStmt;
class ObCreateSavePointExecutor
{
public:
  ObCreateSavePointExecutor() {}
  virtual ~ObCreateSavePointExecutor() {}
  int execute(ObExecContext &ctx, ObCreateSavePointStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateSavePointExecutor);
};

class ObRollbackSavePointStmt;
class ObRollbackSavePointExecutor
{
public:
  ObRollbackSavePointExecutor() {}
  virtual ~ObRollbackSavePointExecutor() {}
  int execute(ObExecContext &ctx, ObRollbackSavePointStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObRollbackSavePointExecutor);
};

class ObReleaseSavePointStmt;
class ObReleaseSavePointExecutor
{
public:
  ObReleaseSavePointExecutor() {}
  virtual ~ObReleaseSavePointExecutor() {}
  int execute(ObExecContext &ctx, ObReleaseSavePointStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObReleaseSavePointExecutor);
};

}
}
#endif // OCRANBASE_SQL_ENGINE_CMD_OB_TCL_CMD_EXECUTOR_
