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

#include "sql/engine/cmd/ob_empty_query_executor.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
int ObEmptyQueryExecutor::execute(ObExecContext &ctx, ObEmptyQueryStmt &stmt)
{
  UNUSED(ctx);
  UNUSED(stmt);
  int ret = OB_SUCCESS;
  return ret;
}

}// sql
}// oceanbase
