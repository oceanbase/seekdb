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
class ObTaskInfo;
class ObTableModify;

#define ENG_OP typename ObEngineOpTraits<NEW_ENG>
class ObTaskSpliter
{
public:
  enum TaskSplitType {
    INVALID_SPLIT = 0,
    PARTITION_RANGE_SPLIT = 1,
    INTERM_SPLIT = 2,
    INSERT_SPLIT = 3,
    INTRA_PARTITION_SPLIT = 4,
    DISTRIBUTED_SPLIT = 5,
    DETERMINATE_TASK_SPLIT = 6
  };
public:
  static int find_scan_ops(common::ObIArray<const ObTableScanSpec*> &scan_ops, const ObOpSpec &op);

  static int find_insert_ops(common::ObIArray<const ObTableModifySpec *> &insert_ops,
                             const ObOpSpec &op);
  template <bool NEW_ENG>
  static int find_scan_ops_inner(common::ObIArray<const ENG_OP::TSC *> &scan_ops, const ENG_OP::Root &op);

  template <bool NEW_ENG>
  static int find_insert_ops_inner(common::ObIArray<const ENG_OP::TableModify *> &insert_ops,
                             const ENG_OP::Root &op);
};

#undef ENG_OP

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_TASK_SPLITER_ */
//// end of header file
