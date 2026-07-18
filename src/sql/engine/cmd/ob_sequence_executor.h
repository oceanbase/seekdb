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

#ifndef __OB_SQL_SEQUENCE_EXECUTOR_H__
#define __OB_SQL_SEQUENCE_EXECUTOR_H__
#include "share/ob_define.h"
namespace oceanbase
{
namespace sql
{
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

class ObExecContext;
class ObCreateSequenceStmt;
class ObDropSequenceStmt;
class ObAlterSequenceStmt;

DEF_SIMPLE_EXECUTOR(ObCreateSequence);
DEF_SIMPLE_EXECUTOR(ObDropSequence);
DEF_SIMPLE_EXECUTOR(ObAlterSequence);

#undef DEF_SIMPLE_EXECUTOR
}
}
#endif /* __OB_SQL_SEQUENCE_EXECUTOR_H__ */
//// end of header file
