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
#include "ob_deallocate_executor.h"
#include "sql/resolver/prepare/ob_deallocate_stmt.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObDeallocateExecutor::execute(ObExecContext &ctx, ObDeallocateStmt &stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx.get_sql_ctx()) || OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql ctx or session is NULL", K(ctx.get_sql_ctx()), K(ctx.get_my_session()), K(ret));
  } else {
    if (OB_FAIL(ctx.get_my_session()->remove_prepare(stmt.get_prepare_name()))) {
      LOG_WARN("failed to remove prepare", K(stmt.get_prepare_name()), K(ret));
    } else if (OB_FAIL(ctx.get_my_session()->close_ps_stmt(stmt.get_prepare_id()))) {
      LOG_WARN("fail to deallocate ps stmt", K(ret), K(stmt.get_prepare_id()));
    } else { /*do nothing*/ }
  }
  return ret;
}

}
}


