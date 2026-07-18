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

#include "observer/table_load/dag/ob_table_load_dag_task.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadMemSortOp;

// start
class ObTableLoadMemSortOpTask final : public share::ObITask, public ObTableLoadDagOpTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadMemSortOpTask(ObTableLoadDag *dag, ObTableLoadOp *op);
  virtual ~ObTableLoadMemSortOpTask() = default;
  int process() override;
};

// finish
class ObTableLoadMemSortOpFinishTask final : public share::ObITask, public ObTableLoadDagOpTaskBase
{
public:
  ObTableLoadMemSortOpFinishTask(ObTableLoadDag *dag, ObTableLoadOp *op);
  virtual ~ObTableLoadMemSortOpFinishTask() = default;
  int process() override;
  static void reset_op(ObTableLoadMemSortOp *op);
};

} // namespace observer
} // namespace oceanbase
