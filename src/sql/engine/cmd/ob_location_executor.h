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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_LOCATION_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_LOCATION_EXECUTOR_H_
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreateLocationStmt;
class ObDropLocationStmt;

class ObCreateLocationExecutor
{
public:
  ObCreateLocationExecutor() {}
  virtual ~ObCreateLocationExecutor() {}
  int execute(ObExecContext &ctx, ObCreateLocationStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateLocationExecutor);
};

class ObDropLocationExecutor
{
public:
  ObDropLocationExecutor() {}
  virtual ~ObDropLocationExecutor() {}
  int execute(ObExecContext &ctx, ObDropLocationStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropLocationExecutor);
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SRC_SQL_ENGINE_CMD_OB_LOCATION_EXECUTOR_H_
