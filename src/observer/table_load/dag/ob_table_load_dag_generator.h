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

#pragma once

#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace share
{
class ObITask;
} // namespace share
namespace observer
{
class ObTableLoadDagExecCtx;
class ObTableLoadDag;
class ObTableLoadOp;
class ObTableLoadTableOp;

class ObTableLoadDagGenerator
{
public:
  static int generate(ObTableLoadDagExecCtx &dag_exec_ctx);

private:
  // Generate a topological sort of the dependency graph
  static int generate_table_op_topological_order(ObTableLoadTableOp *root_op,
                                                 ObIArray<ObTableLoadTableOp *> &table_ops);

  static int table_op_list_to_executable_op_list(const ObIArray<ObTableLoadTableOp *> &table_ops,
                                                 ObIArray<ObTableLoadOp *> &executable_ops);
  static int executable_op_list_to_dag_task_list(const ObIArray<ObTableLoadOp *> &executable_ops,
                                                 ObTableLoadDag *dag,
                                                 ObIArray<share::ObITask *> &dag_tasks);
};

} // namespace observer
} // namespace oceanbase
