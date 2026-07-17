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
#include "sql/engine/cmd/ob_udf_executor.h"
#include "sql/resolver/ddl/ob_create_func_stmt.h"
#include "sql/resolver/ddl/ob_drop_func_stmt.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObCreateFuncExecutor::execute(ObExecContext &ctx, ObCreateFuncStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObString first_stmt;
  UNUSED(ctx);
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("create_udf not supported in seekdb", K(ret));
  }
  return ret;
}

int ObDropFuncExecutor::execute(ObExecContext &ctx, ObDropFuncStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObString first_stmt;
  UNUSED(ctx);
  if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("fail to get first stmt" , K(ret));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("drop_udf not supported in seekdb", K(ret));
  }
  return ret;
}
} //end namespace sql
} //end namespace oceanbase

