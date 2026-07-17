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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/dag/ob_table_load_dag_task.h"
#include "observer/table_load/dag/ob_table_load_dag.h"
#include "observer/table_load/dag/ob_table_load_dag_compact_table_task.h"
#include "observer/table_load/dag/ob_table_load_dag_direct_write.h"
#include "observer/table_load/dag/ob_table_load_dag_insert_sstable_task.h"
#include "observer/table_load/dag/ob_table_load_dag_mem_sort.h"
#include "observer/table_load/dag/ob_table_load_dag_pre_sort_write.h"
#include "observer/table_load/dag/ob_table_load_dag_store_write.h"
#include "observer/table_load/ob_table_load_store_ctx.h"
#include "observer/table_load/ob_table_load_store_table_ctx.h"
#include "observer/table_load/plan/ob_table_load_table_op.h"

namespace oceanbase
{
namespace observer
{
using namespace share;
using namespace storage;

/**
 * ObTableLoadDagTaskBase
 */

ObTableLoadDagTaskBase::ObTableLoadDagTaskBase(ObTableLoadDag *dag)
  : store_ctx_(dag->store_ctx_), dag_(dag)
{
}

ObTableLoadDagTaskBase::~ObTableLoadDagTaskBase() {}

/**
 * ObTableLoadDagOpTaskBase
 */

ObTableLoadDagOpTaskBase::ObTableLoadDagOpTaskBase(ObTableLoadDag *dag, ObTableLoadOp *op)
  : ObTableLoadDagTaskBase(dag), op_(op)
{
}

ObTableLoadDagOpTaskBase::~ObTableLoadDagOpTaskBase() {}

int ObTableLoadDagOpTaskBase::create_op_task(ObTableLoadDag *dag, ObTableLoadOp *op,
                                             ObITask *&op_task)
{
  int ret = OB_SUCCESS;
  switch (op->get_op_type()) {
#define OP_TASK_CREATE_SWITCH(op_type, OpType, OpTaskType) \
  case ObTableLoadOpType::op_type: {                       \
    OpTaskType *task = nullptr;                            \
    if (OB_FAIL(dag->alloc_task(task, dag, op))) {         \
      LOG_WARN("fail to alloc task", KR(ret));             \
    } else {                                               \
      op_task = task;                                      \
    }                                                      \
    break;                                                 \
  }

    OB_TABLE_LOAD_DAG_OP_TASK(OP_TASK_CREATE_SWITCH)

#undef OP_TASK_CREATE_SWITCH
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected op type", KR(ret), KPC(op));
      break;
  }
  return ret;
}

// start_merge
ObTableLoadDagStartMergeTask::ObTableLoadDagStartMergeTask(ObTableLoadDag *dag)
  : ObITask(TASK_TYPE_DIRECT_LOAD_START_MERGE), ObTableLoadDagTaskBase(dag)
{
}

ObITask::ObITaskPriority ObTableLoadDagStartMergeTask::get_priority()
{
  return ObTableLoadDagTaskBase::get_priority(store_ctx_->is_status_merging());
}

int ObTableLoadDagStartMergeTask::process()
{
  FLOG_INFO("[DIRECT_LOAD_OP] start merge");
  return OB_SUCCESS;
}

// open
ObTableLoadDagTableOpOpenOpTask::ObTableLoadDagTableOpOpenOpTask(ObTableLoadDag *dag,
                                                                 ObTableLoadOp *op)
  : ObITask(TASK_TYPE_DIRECT_LOAD_TABLE_OP_OPEN_OP), ObTableLoadDagOpTaskBase(dag, op)
{
}

int ObTableLoadDagTableOpOpenOpTask::process()
{
  int ret = OB_SUCCESS;
  ObTableLoadTableOpOpenOp *op = static_cast<ObTableLoadTableOpOpenOp *>(op_);
  ObTableLoadTableOp *table_op = op->table_op_;
  FLOG_INFO("[DIRECT_LOAD_OP] table op open", KP(op), KP(table_op), "op_type",
            ObTableLoadOpType::get_type_string(table_op->get_op_type()), "table_id",
            table_op->op_ctx_->store_table_ctx_->table_id_);
  table_op->start_time_ = ObTimeUtil::current_time();

  if (OB_FAIL(table_op->open())) {
    LOG_WARN("fail to open table op", KR(ret), KPC(table_op));
  } else {
    ObSEArray<ObITask *, 1> ddl_start_tasks;
    if (OB_FAIL(dag_->generate_start_tasks(ddl_start_tasks))) {
      LOG_WARN("fail to generate start tasks", KR(ret));
    } else if (!ddl_start_tasks.empty()) {
      ObITask *last_task = ddl_start_tasks.at(ddl_start_tasks.count() - 1);
      if (OB_FAIL(last_task->deep_copy_children(get_child_nodes()))) {
        LOG_WARN("fail to deep copy children", KR(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_start_tasks.count(); ++i) {
      ObITask *task = ddl_start_tasks.at(i);
      if (OB_FAIL(dag_->add_task(*task))) {
        LOG_WARN("fail to add task", KR(ret));
      }
    }
  }
  return ret;
}

// close
ObTableLoadDagTableOpCloseOpTask::ObTableLoadDagTableOpCloseOpTask(ObTableLoadDag *dag,
                                                                   ObTableLoadOp *op)
  : ObITask(TASK_TYPE_DIRECT_LOAD_TABLE_OP_CLOSE_OP), ObTableLoadDagOpTaskBase(dag, op)
{
}

int ObTableLoadDagTableOpCloseOpTask::process()
{
  int ret = OB_SUCCESS;
  ObTableLoadTableOpCloseOp *op = static_cast<ObTableLoadTableOpCloseOp *>(op_);
  ObTableLoadTableOp *table_op = op->table_op_;
  if (OB_FAIL(table_op->close())) {
    LOG_WARN("fail to close table op", KR(ret), KPC(table_op));
  }

  table_op->end_time_ = ObTimeUtil::current_time();
  FLOG_INFO("[DIRECT_LOAD_OP] table op close", KP(op), KP(table_op), "op_type",
            ObTableLoadOpType::get_type_string(table_op->get_op_type()), "table_id",
            table_op->op_ctx_->store_table_ctx_->table_id_, "time_cost",
            table_op->end_time_ - table_op->start_time_);
  return ret;
}

// finish
ObTableLoadDagFinishOpTask::ObTableLoadDagFinishOpTask(ObTableLoadDag *dag, ObTableLoadOp *op)
  : ObITask(TASK_TYPE_DIRECT_LOAD_FINISH_OP), ObTableLoadDagOpTaskBase(dag, op)
{
}

int ObTableLoadDagFinishOpTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(store_ctx_->set_status_merged())) {
    LOG_WARN("fail to set status merged", KR(ret));
  }
  FLOG_INFO("[DIRECT_LOAD_OP] finish");
  return ret;
}

} // namespace observer
} // namespace oceanbase
