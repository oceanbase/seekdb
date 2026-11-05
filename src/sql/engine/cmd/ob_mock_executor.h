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

#ifndef OCEANBASE_SQL_ENGINE_MOCK_EXECUTOR_H_
#define OCEANBASE_SQL_ENGINE_MOCK_EXECUTOR_H_

#include "share/ob_define.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/cmd/ob_mock_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObMockExecutor
{
public:
  ObMockExecutor() = default;
  virtual ~ObMockExecutor() = default;
  int execute(ObExecContext &exec_ctx, ObMockStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObMockExecutor);
};

}
}

#endif
