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

#ifndef OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_FACTORY_
#define OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_FACTORY_

#include "sql/executor/ob_task_spliter.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
class ObTaskSpliterFactory
{
public:
  ObTaskSpliterFactory();
  virtual ~ObTaskSpliterFactory();
  int create(ObExecContext &exec_ctx, ObJob &job, int spliter_type, ObTaskSpliter *&spliter);
private:
  common::ObSEArray<ObTaskSpliter*, 4> store_;
private:
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObTaskSpliterFactory);
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_FACTORY_ */
//// end of header file

