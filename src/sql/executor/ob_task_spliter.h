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

#ifndef OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_
#define OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_

#include "lib/allocator/ob_allocator.h"
#include "sql/executor/ob_job.h"
#include "sql/engine/ob_phy_operator_type.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_engine_op_traits.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
class ObJob;
class ObTaskInfo;
class ObPhysicalPlanCtx;
class ObTaskExecutorCtx;
class ObTableModify;

#define ENG_OP typename ObEngineOpTraits<NEW_ENG>
class ObTaskSpliter
{
public:
  enum TaskSplitType {
    INVALID_SPLIT = 0,
    LOCAL_IDENTITY_SPLIT = 1,      // root(local) transmit task splitter
    REMOTE_IDENTITY_SPLIT = 2,     // remote transmit task splitter
    PARTITION_RANGE_SPLIT = 3,     // distributed transmit task splitter
    INTERM_SPLIT = 4,
    INSERT_SPLIT = 5,
    INTRA_PARTITION_SPLIT = 6,     // split single partition into multiple individual ranges
    DISTRIBUTED_SPLIT = 7,
    DETERMINATE_TASK_SPLIT = 8     // tasks are splited already
  };
public:
  ObTaskSpliter();
  virtual ~ObTaskSpliter();
  int init(ObPhysicalPlanCtx *plan_ctx,
           ObExecContext *exec_ctx,
           ObJob &job,
           common::ObIAllocator &allocator);
  // No more task then return OB_ITER_END
  virtual int get_next_task(ObTaskInfo *&task) = 0;
  virtual TaskSplitType get_type() const = 0;
  VIRTUAL_TO_STRING_KV(K_(server));

  static int find_scan_ops(common::ObIArray<const ObTableScanSpec*> &scan_ops, const ObOpSpec &op);

  static int find_insert_ops(common::ObIArray<const ObTableModifySpec *> &insert_ops,
                             const ObOpSpec &op);
  bool is_inited() const { return NULL != job_; }
protected:

  template <bool NEW_ENG>
  static int find_scan_ops_inner(common::ObIArray<const ENG_OP::TSC *> &scan_ops, const ENG_OP::Root &op);

  template <bool NEW_ENG>
  static int find_insert_ops_inner(common::ObIArray<const ENG_OP::TableModify *> &insert_ops,
                             const ENG_OP::Root &op);
protected:
  common::ObAddr server_;
  ObPhysicalPlanCtx *plan_ctx_;
  ObExecContext *exec_ctx_;
  common::ObIAllocator *allocator_;
  ObJob *job_;
  common::ObSEArray<ObTaskInfo *, 16> task_store_;
};

#undef ENG_OP

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_ */
//// end of header file
