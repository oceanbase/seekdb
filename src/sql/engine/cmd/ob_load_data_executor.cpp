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

#include "sql/engine/cmd/ob_load_data_direct_impl.h"
#include "sql/optimizer/ob_direct_load_optimizer_ctx.h"

namespace oceanbase
{
namespace sql
{

int ObLoadDataExecutor::execute(ObExecContext &ctx, ObLoadDataStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTableDirectInsertCtx &table_direct_insert_ctx = ctx.get_table_direct_insert_ctx();
  ObLoadDataBase *load_impl = NULL;
  ObDirectLoadOptimizerCtx optimizer_ctx;
  stmt.set_optimizer_ctx(&optimizer_ctx);
  if (stmt.is_load_data_url()) {
    if (OB_ISNULL(load_impl = OB_NEWx(ObLoadDataURLImpl, (&ctx.get_allocator())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    }
  } else if (!stmt.get_load_arguments().is_csv_format_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("invalid resolver results", K(ret));
  } else if (OB_FAIL(optimizer_ctx.init_direct_load_ctx(&ctx, stmt))) {
    LOG_WARN("fail to init direct load ctx", K(ret), K(stmt));
  } else {
    if (optimizer_ctx.can_use_direct_load()) {
      optimizer_ctx.set_use_direct_load();
    }
    table_direct_insert_ctx.set_is_direct(optimizer_ctx.use_direct_load());
    if (!table_direct_insert_ctx.get_is_direct()) {
      if (OB_ISNULL(load_impl = OB_NEWx(ObLoadDataSPImpl, (&ctx.get_allocator())))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      }
    } else {
      if (OB_ISNULL(load_impl = OB_NEWx(ObLoadDataDirectImpl, (&ctx.get_allocator())))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else if (stmt.get_table_assignment().count() > 0) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("direct load not support");
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(load_impl->execute(ctx, stmt))) {
      LOG_WARN("failed to execute load data stmt", K(ret));
    } else {
      LOG_TRACE("load data success");
    }
    load_impl->~ObLoadDataBase();
  }
  return ret;
}

} // sql
} // oceanbase
