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

#define USING_LOG_PREFIX  SQL_ENG

#include "sql/engine/cmd/ob_load_data_executor.h"

#include "sql/engine/cmd/ob_load_data_impl.h"

namespace oceanbase
{
namespace sql
{

int ObLoadDataExecutor::execute(ObExecContext &ctx, ObLoadDataStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObLoadDataBase *load_impl = NULL;
  if (!stmt.get_load_arguments().is_csv_format_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("invalid resolver results", K(ret));
  } else if (OB_ISNULL(load_impl = OB_NEWx(ObLoadDataSPImpl, (&ctx.get_allocator())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(load_impl->execute(ctx, stmt))) {
    } else {
    }
    load_impl->~ObLoadDataBase();
  }
  return ret;
}

} // sql
} // oceanbase
