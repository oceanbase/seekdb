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

#ifndef OCEANBASE_SQL_ENGINE_EMPTY_QUERY_EXECUTOR_H__
#define OCEANBASE_SQL_ENGINE_EMPTY_QUERY_EXECUTOR_H__

#include "share/ob_define.h"
namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObEmptyQueryStmt;

class ObEmptyQueryExecutor
{
public:
  ObEmptyQueryExecutor() {}
  virtual ~ObEmptyQueryExecutor() {}
  int execute(ObExecContext &ctx, ObEmptyQueryStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObEmptyQueryExecutor);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_EMPTY_QUERY_EXECUTOR_H__ */
