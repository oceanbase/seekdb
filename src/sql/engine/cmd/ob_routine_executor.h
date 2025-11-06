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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_PROCEDURE_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_PROCEDURE_EXECUTOR_H_

#include "lib/container/ob_vector.h"
#include "lib/container/ob_iarray.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/ob_stmt_type.h"
#include "src/share/schema/ob_routine_info.h"

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
namespace common
{
class ObField;
class ObObjParam;
}
namespace share
{
namespace schema
{
class ObRoutineInfo;
class ObRoutineParam;
}
}
namespace sql
{
class ObExecContext;
class ObCreateRoutineStmt;
class ObAlterRoutineStmt;
class ObDropRoutineStmt;
class ObCallProcedureStmt;
class ObAnonymousBlockStmt;
class ObRawExpr;

class ObCreateRoutineExecutor
{
public:
  ObCreateRoutineExecutor() {}
  virtual ~ObCreateRoutineExecutor() {}
  int execute(ObExecContext &ctx, ObCreateRoutineStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateRoutineExecutor);
};

class ObAlterRoutineExecutor
{
public:
  ObAlterRoutineExecutor() {}
  virtual ~ObAlterRoutineExecutor() {}
  int execute(ObExecContext &ctx, ObAlterRoutineStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterRoutineExecutor);
};
// Reference alter system definition
DEF_SIMPLE_EXECUTOR(ObDropRoutine);

class ObCallProcedureExecutor
{
public:
  ObCallProcedureExecutor() {}
  virtual ~ObCallProcedureExecutor() {}
  int execute(ObExecContext &ctx, ObCallProcedureStmt &stmt);

private:
  DISALLOW_COPY_AND_ASSIGN(ObCallProcedureExecutor);
};
  
class ObAnonymousBlockExecutor
{
public:
  ObAnonymousBlockExecutor() {}
  virtual ~ObAnonymousBlockExecutor() {}

  int execute(ObExecContext &ctx, ObAnonymousBlockStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAnonymousBlockExecutor);
};


}//namespace sql
}//namespace oceanbase



#endif /* OCEANBASE_SRC_SQL_ENGINE_CMD_OB_PROCEDURE_EXECUTOR_H_ */
