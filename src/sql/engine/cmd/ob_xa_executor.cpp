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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/ob_xa_executor.h"
#include "pl/ob_pl.h"

namespace oceanbase
{
using namespace common;
using namespace transaction;
namespace sql
{


// for mysql xa start
int ObXaStartExecutor::execute(ObExecContext &ctx, ObXaStartStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa start", K(ret));
  return ret;
}

// for mysql xa end
int ObXaEndExecutor::execute(ObExecContext &ctx, ObXaEndStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa end", K(ret));
  return ret;
}

// for mysql xa prepare
int ObXaPrepareExecutor::execute(ObExecContext &ctx, ObXaPrepareStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa prepare", K(ret));
  return ret;
}

// for mysql xa commit
int ObXaCommitExecutor::execute(ObExecContext &ctx, ObXaCommitStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa commit", K(ret));
  return ret;
}

// for mysql xa rollback
int ObXaRollbackExecutor::execute(ObExecContext &ctx, ObXaRollBackStmt &stmt)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_INFO("mysql xa rollback", K(ret));
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
