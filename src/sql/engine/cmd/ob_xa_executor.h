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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_XA_CMD_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_OB_XA_CMD_EXECUTOR_

#include "lib/utility/ob_macro_utils.h"
#include "lib/string/ob_string.h"
#include "sql/resolver/xa/ob_xa_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObXaStartStmt;
class ObXaExecutorUtil
{
public:
  static int get_org_cluster_id(ObSQLSessionInfo *session, int64_t &org_cluster_id);
};

class ObXaStartExecutor
{
public:
  ObXaStartExecutor() {}
  ~ObXaStartExecutor() {}
  int execute(ObExecContext &ctx, ObXaStartStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaStartExecutor);
};

class ObXaEndStmt;
class ObXaEndExecutor
{
public:
  ObXaEndExecutor() {}
  ~ObXaEndExecutor() {}
  int execute(ObExecContext &ctx, ObXaEndStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaEndExecutor);
};

class ObXaPrepareStmt;
class ObXaPrepareExecutor
{
public:
  ObXaPrepareExecutor() {}
  ~ObXaPrepareExecutor() {}
  int execute(ObExecContext &ctx, ObXaPrepareStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaPrepareExecutor);
};

class ObXaCommitStmt;
class ObXaRollBackStmt;

class ObXaCommitExecutor
{
public:
  ObXaCommitExecutor() {}
  ~ObXaCommitExecutor() {}
  int execute(ObExecContext &ctx, ObXaCommitStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaCommitExecutor);
};

class ObXaRollbackExecutor
{
public:
  ObXaRollbackExecutor() {}
  ~ObXaRollbackExecutor() {}
  int execute(ObExecContext &ctx, ObXaRollBackStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaRollbackExecutor);
};

} // end namespace sql
} // end namespace oceanbase


#endif
