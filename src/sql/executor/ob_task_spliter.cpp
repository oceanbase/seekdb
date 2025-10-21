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

#define USING_LOG_PREFIX SQL_EXE

#include "sql/executor/ob_task_spliter.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/px/exchange/ob_receive_op.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

#define ENG_OP typename ObEngineOpTraits<NEW_ENG>
ObTaskSpliter::ObTaskSpliter()
    : server_(),
      plan_ctx_(NULL),
      exec_ctx_(NULL),
      allocator_(NULL),
      job_(NULL),
      task_store_(ObModIds::OB_SQL_EXECUTOR_TASK_SPLITER, OB_MALLOC_NORMAL_BLOCK_SIZE)
{
}

ObTaskSpliter::~ObTaskSpliter()
{
  FOREACH_CNT(t, task_store_) {
    if (NULL != *t) {
      (*t)->~ObTaskInfo();
      *t = NULL;
    }
  }
  task_store_.reset();
}

int ObTaskSpliter::init(ObPhysicalPlanCtx *plan_ctx,
                        ObExecContext *exec_ctx,
                        ObJob &job,
                        common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObOpSpec *op_spec_ = job.get_root_spec(); // for static engine
  plan_ctx_ = plan_ctx;
  exec_ctx_ = exec_ctx;
  job_ = &job;
  allocator_ = &allocator;
  if (OB_I(t1) (
        OB_ISNULL(plan_ctx)
        || OB_ISNULL(exec_ctx)
        || OB_ISNULL(op_spec_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid NULL ptr", K(plan_ctx), K(exec_ctx), K(op_spec_));
  } else {
    server_ = exec_ctx->get_task_exec_ctx().get_self_addr();
  }
  return ret;
}

int ObTaskSpliter::find_scan_ops(ObIArray<const ObTableScanSpec*> &scan_ops, const ObOpSpec &op)
{
  return find_scan_ops_inner<true>(scan_ops, op);
}

template <bool NEW_ENG>
int ObTaskSpliter::find_scan_ops_inner(ObIArray<const ENG_OP::TSC *> &scan_ops, const ENG_OP::Root &op)
{
  // Post-order traversal, ensuring scan_ops.at(0) is the leftmost leaf node
  int ret = OB_SUCCESS;
  if (!IS_RECEIVE(op.get_type())) {
    for (int32_t i = 0; OB_SUCC(ret) && i < op.get_child_num(); ++i) {
      const ENG_OP::Root *child_op = op.get_child(i);
      if (OB_ISNULL(child_op)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObTaskSpliter::find_scan_ops(scan_ops, *child_op))) {
        LOG_WARN("fail to find child scan ops",
                 K(ret), K(i), "op_id", op.get_id(), "child_id", child_op->get_id());
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (op.is_table_scan() && op.get_type() != PHY_FAKE_CTE_TABLE) {
    if (static_cast<const ENG_OP::TSC &>(op).use_dist_das()) {
      //do nothing, use DAS to execute TSC, DAS will handle DAS-related information, no need for the scheduler to be aware of TSC
    } else if (OB_FAIL(scan_ops.push_back(static_cast<const ENG_OP::TSC *>(&op)))) {
      LOG_WARN("fail to push back table scan op", K(ret));
    }
  }
  return ret;
}

int ObTaskSpliter::find_insert_ops(ObIArray<const ObTableModifySpec *> &insert_ops, const ObOpSpec &op)
{
  return find_insert_ops_inner<true>(insert_ops, op);
}

template <bool NEW_ENG>
int ObTaskSpliter::find_insert_ops_inner(ObIArray<const ENG_OP::TableModify *> &insert_ops, const ENG_OP::Root &op)
{
  int ret = OB_SUCCESS;
  if (IS_TABLE_INSERT(op.get_type())) { // INSERT, REPLACE, INSERT UPDATE, INSERT RETURNING
    if (OB_FAIL(insert_ops.push_back(static_cast<const ENG_OP::TableModify *>(&op)))) {
      LOG_WARN("fail to push back table insert op", K(ret));
    }
  }
  if (OB_SUCC(ret) && !IS_RECEIVE(op.get_type())) {
    for (int32_t i = 0; OB_SUCC(ret) && i < op.get_child_num(); ++i) {
      const ENG_OP::Root *child_op = op.get_child(i);
      if (OB_ISNULL(child_op)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObTaskSpliter::find_insert_ops(insert_ops, *child_op))) {
        LOG_WARN("fail to find child insert ops",
                 K(ret), K(i), "op_id", op.get_id(), "child_id", child_op->get_id());
      }
    }
  }
  return ret;
}


}/* ns ns*/
}/* ns oceanbase */
