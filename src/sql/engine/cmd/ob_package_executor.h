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

#ifndef OCEANBASE_SQL_OB_PACKAGE_EXECUTOR_H_
#define OCEANBASE_SQL_OB_PACKAGE_EXECUTOR_H_

#include "lib/container/ob_vector.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/ob_stmt_type.h"
#include "share/schema/ob_package_info.h"

#define DEF_SIMPLE_EXECUTOR(name)                          \
  class name##Executor                                     \
  {                                                        \
  public:                                                  \
    name##Executor() {}                                    \
    virtual ~name##Executor() {}                           \
    int execute(ObExecContext &ctx, name##Stmt &stmt);     \
  private:                                                 \
    DISALLOW_COPY_AND_ASSIGN(name##Executor);              \
  }

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreatePackageStmt;
class ObAlterPackageStmt;
class ObDropPackageStmt;

class ObCreatePackageExecutor
{
public:
  ObCreatePackageExecutor() {}
  virtual ~ObCreatePackageExecutor() {}
  int execute(ObExecContext &ctx, ObCreatePackageStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreatePackageExecutor);
};

class ObAlterPackageExecutor
{
public:
  ObAlterPackageExecutor() {}
  virtual ~ObAlterPackageExecutor() {}
  int execute(ObExecContext &ctx, ObAlterPackageStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterPackageExecutor);
};

DEF_SIMPLE_EXECUTOR(ObDropPackage);
}//namespace sql
}//namespace oceanbase
#endif /* OCEANBASE_SQL_OB_PACKAGE_EXECUTOR_H_ */
