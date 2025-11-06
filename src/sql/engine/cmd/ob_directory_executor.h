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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_DIRECTORY_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_DIRECTORY_EXECUTOR_H_
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreateDirectoryStmt;
class ObAlterDirectoryStmt;
class ObDropDirectoryStmt;

class ObCreateDirectoryExecutor
{
public:
  ObCreateDirectoryExecutor() {}
  virtual ~ObCreateDirectoryExecutor() {}
  int execute(ObExecContext &ctx, ObCreateDirectoryStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateDirectoryExecutor);
};

class ObDropDirectoryExecutor
{
public:
  ObDropDirectoryExecutor() {}
  virtual ~ObDropDirectoryExecutor() {}
  int execute(ObExecContext &ctx, ObDropDirectoryStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropDirectoryExecutor);
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SRC_SQL_ENGINE_CMD_OB_DIRECTORY_EXECUTOR_H_
