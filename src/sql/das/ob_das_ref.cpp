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

#define USING_LOG_PREFIX SQL_DAS
#include "rust/config.h"
#include "ob_das_ref.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/das/ob_data_access_service.h"
#include "sql/das/ob_das_retry_ctrl.h"
#include "sql/ob_query_retry_ctrl.h"

namespace oceanbase
{
using namespace common;
using namespace observer;
namespace sql
{

bool DasRefKey::operator==(const DasRefKey &other) const
{
  return (tablet_access_ == other.tablet_access_ && op_type_ == other.op_type_
          && vec_datum_kind_ == other.vec_datum_kind_);
}

uint64_t DasRefKey::hash() const
{
  uint64_t hash = 0;
  hash = murmurhash(&tablet_access_, sizeof(tablet_access_), hash);
  hash = murmurhash(&op_type_, sizeof(op_type_), hash);
  hash = murmurhash(&vec_datum_kind_, sizeof(vec_datum_kind_), hash);
  return hash;
}

ObDASRef::ObDASRef(ObEvalCtx &eval_ctx, ObExecContext &exec_ctx)
  : das_alloc_(exec_ctx.get_allocator()),
    reuse_alloc_(nullptr),
    das_factory_(das_alloc_),
    batched_tasks_(das_alloc_),
    exec_ctx_(exec_ctx),
    eval_ctx_(eval_ctx),
    expr_frame_info_(nullptr),
    del_aggregated_tasks_(das_alloc_),
    aggregated_tasks_(das_alloc_),
    lookup_cnt_(0),
    task_cnt_(0),
    init_mem_used_(exec_ctx.get_allocator().used()),
    task_map_(),
    flags_(0)
{
}

DASOpResultIter ObDASRef::begin_result_iter()
{
  return DASOpResultIter(batched_tasks_.begin());
}

ObIDASTaskOp* ObDASRef::find_das_task(const DASRuntimeTabletAccess *tablet_access, ObDASOpType op_type,
                                    DasVecDatumKind vec_datum_kind)
{
  int ret = OB_SUCCESS;
  ObIDASTaskOp *das_task = nullptr;
  lookup_cnt_++;
  if (task_map_.created()) {
    DasRefKey key(tablet_access, op_type, vec_datum_kind);
    if (OB_FAIL(task_map_.get_refactored(key, das_task))) {
      if (OB_HASH_NOT_EXIST != ret) {
      }
    }
  }
  if (OB_SUCC(ret) && NULL != das_task) {
    // found in hash map
  } else if (OB_HASH_NOT_EXIST == ret) {
    // key not found
  } else {
    DASTaskIter task_iter(batched_tasks_.get_header_node()->get_next(), batched_tasks_.get_header_node());
    for (; nullptr == das_task && !task_iter.is_end(); ++task_iter) {
      ObIDASTaskOp *tmp_task = *task_iter;
      if (tmp_task != nullptr &&
          tmp_task->get_tablet_access() == tablet_access &&
          tmp_task->get_type() == op_type &&
          tmp_task->get_vec_datum_kind() == vec_datum_kind &&
          !tmp_task->is_write_buff_full()) {
        das_task = tmp_task;
      }
    }
  }
  if (OB_FAIL(ret) || task_map_.created()) {
    // do nothing
  } else if (lookup_cnt_ > DAS_REF_TASK_LOOKUP_THRESHOLD
             && task_cnt_ > DAS_REF_TASK_SIZE_THRESHOLD
             && OB_FAIL(create_task_map())) {
  }
  return das_task;
}

int ObDASRef::create_task_map()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(task_map_.created())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(task_map_.create(DAS_REF_MAP_BUCKET_SIZE, ObModIds::OB_HASH_BUCKET))) {
  } else {
    DASTaskIter task_iter(batched_tasks_.get_header_node()->get_next(), batched_tasks_.get_header_node());
    for (; OB_SUCC(ret) && !task_iter.is_end(); ++task_iter) {
      ObIDASTaskOp *task = *task_iter;
      if (OB_ISNULL(task)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (task->is_write_buff_full()) {
      } else {
        DasRefKey key(task->get_tablet_access(), task->get_type(), task->get_vec_datum_kind());
        if (OB_FAIL(task_map_.set_refactored(key, task))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
      task_map_.destroy();
    }
  }
  return ret;
}

int ObDASRef::add_batched_task(ObIDASTaskOp *das_task)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(batched_tasks_.store_obj(das_task))) {
  } else if (task_map_.created()) {
    DasRefKey key(das_task->get_tablet_access(), das_task->get_type(), das_task->get_vec_datum_kind());
    if (OB_FAIL(task_map_.set_refactored(key, das_task))) {
    }
  }
  if (OB_SUCC(ret)) {
    task_cnt_++;
  }
  return ret;
}

void ObDASRef::print_all_das_task()
{
  DASTaskIter task_iter(batched_tasks_.get_header_node()->get_next(), batched_tasks_.get_header_node());
  int i = 0;
  for (; !task_iter.is_end(); ++task_iter) {
    i++;
    ObIDASTaskOp *tmp_task = task_iter.get_item();
    if (tmp_task != nullptr) {
      LOG_INFO("dump one das task", K(i), K(tmp_task),
               K(tmp_task->get_tablet_id()), K(tmp_task->get_type()));
    }
  }
}
/*
 [header] -> [node1] -> [node2] -> [node3] -> [header]
 */
int ObDASRef::pick_del_task_to_first()
{
  int ret = OB_SUCCESS;
#if !defined(NDEBUG)
//  LOG_DEBUG("print all das_task before sort");
//  print_all_das_task();
#endif
  DasOpNode *head_node = batched_tasks_.get_obj_list().get_header();
  DasOpNode *curr_node = batched_tasks_.get_obj_list().get_first();
  DasOpNode *next_node = curr_node->get_next();
  // if list only have header，then: next_node == head_node == curr_node
  // if list only have one data node，then: next_node == head_node, not need remove delete task
  // if list only have much data node，then: next_node != head_node, need remove delete task
  while(OB_SUCC(ret) && curr_node != head_node) {
    if (OB_ISNULL(curr_node)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (curr_node->get_obj()->get_type() == ObDASOpType::DAS_OP_TABLE_DELETE) {
      if (!(batched_tasks_.get_obj_list().move_to_first(curr_node))) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret)) {
      curr_node = next_node;
      next_node = curr_node->get_next();
    }
  }
#if !defined(NDEBUG)
  LOG_DEBUG("print all das_task after sort");
  print_all_das_task();
#endif
  return ret;
}

int ObDASRef::execute_all_task()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(execute_all_task(del_aggregated_tasks_))) {
  } else if (OB_FAIL(execute_all_task(aggregated_tasks_))) {
  } else {
    DASTaskIter task_iter = begin_task_iter();
    while (OB_SUCC(ret) && !task_iter.is_end()) {
      int end_ret = OB_SUCCESS;
      ObIDASTaskOp *das_op = nullptr;
      if (OB_ISNULL(das_op = *task_iter)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(das_op->record_task_result_to_rtdef())) {
      } else {
        ++task_iter;
      }
    }
  }

  return ret;
}


int ObDASRef::retry_all_fail_tasks(common::ObIArray<ObIDASTaskOp *> &failed_tasks)
{
  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < failed_tasks.count(); i++) {
    ObIDASTaskOp *failed_task = failed_tasks.at(i);
    if (!config::_enable_partition_level_retry() || !failed_task->can_part_retry()) {
      ret = failed_task->errcode_;
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObDataAccessService>()->retry_das_task(*this, *failed_tasks.at(i)))) {
    }
  }
  return ret;
}

int ObDASRef::execute_all_task(DasAggregatedTaskList &agg_task_list)
{
  int ret = OB_SUCCESS;
  uint32_t finished_cnt = 0;
  while (finished_cnt < agg_task_list.get_size() && OB_SUCC(ret)) {
    finished_cnt = 0;
    DLIST_FOREACH_X(curr, agg_task_list.get_obj_list(), OB_SUCC(ret)) {
      ObDasAggregatedTask *agg_task = curr->get_obj();
      if (agg_task->has_unstart_tasks() &&
          OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObDataAccessService>()->execute_das_task(*this, *agg_task))) {
      }
    }

    if (OB_FAIL(ret) && check_rcode_can_retry(ret)) {
      ret = OB_SUCCESS;
    }

    // check das task status.
    DLIST_FOREACH_X(curr, agg_task_list.get_obj_list(), OB_SUCC(ret)) {
      ObDasAggregatedTask* aggregated_task = curr->get_obj();
      if (aggregated_task->has_not_execute_task()) {
        // There are still tasks not completed
        if (aggregated_task->has_failed_tasks()) {
          // retry all failed tasks.
          common::ObSEArray<ObIDASTaskOp *, 2> failed_tasks;
          int tmp_ret = OB_SUCCESS;
          if (OB_TMP_FAIL(aggregated_task->get_failed_tasks(failed_tasks))) {
          } else if (failed_tasks.count() == 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("failed to get failed tasks");
          } else if (OB_FAIL(retry_all_fail_tasks(failed_tasks))) {
          }
        }
      } else {
        ++finished_cnt;
        LOG_DEBUG("check finish agg_task print agg_list", K(finished_cnt), K(agg_task_list.get_size()), KPC(aggregated_task));
      }
    }
  }
  return ret;
}

bool ObDASRef::check_rcode_can_retry(int ret)
{
  bool bret = false;
  ObDASRetryCtrl::retry_func retry_func = nullptr;

  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(ObQueryRetryCtrl::get_das_retry_func(ret, retry_func))) {
  } else if (retry_func != nullptr) {
    bret = true;
  }
  return bret;
}

int ObDASRef::close_all_task()
{
  int ret = OB_SUCCESS;
  int last_end_ret = OB_SUCCESS;
  if (has_task()) {
    ObSQLSessionInfo *session = nullptr;
    DASTaskIter task_iter = begin_task_iter();
    while (!task_iter.is_end()) {
      int end_ret = OB_SUCCESS;
      if (OB_SUCCESS != (end_ret = ::oceanbase::share::server_service<::oceanbase::sql::ObDataAccessService>()->end_das_task(*this, **task_iter))) {
      }
      ++task_iter;
      last_end_ret = (last_end_ret == OB_SUCCESS ? end_ret : last_end_ret);
    }

    ret = last_end_ret;

    if (OB_ISNULL(session = exec_ctx_.get_my_session())) {
      ret = COVER_SUCC(OB_NOT_INIT);
    }
    bool merge_trans_result_fail = (ret != OB_SUCCESS);
    // any fail during merge trans_result,
    // need set trans_result incomplete, in order to
    // indicate transaction write state info unknown
    if (merge_trans_result_fail && OB_NOT_NULL(session)) {
      session->get_trans_result().set_incomplete();
    }
    batched_tasks_.destroy();
    del_aggregated_tasks_.destroy();
    aggregated_tasks_.destroy();
    if (task_map_.created()) {
      task_map_.destroy();
    }
  }
  return ret;
}

int ObDASRef::create_das_task(const DASRuntimeTabletAccess *tablet_access,
                              ObDASOpType op_type,
                              ObIDASTaskOp *&task_op,
                              DasVecDatumKind vec_datum_kind)
{
  int ret = OB_SUCCESS;
  ObDASTaskFactory &das_factory = get_das_factory();
  ObSQLSessionInfo *session = get_exec_ctx().get_my_session();
  int64_t task_id = 0;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObDataAccessService>()->get_das_task_id(task_id))) {
  } else if (OB_FAIL(das_factory.create_das_task_op(op_type, task_op))) {
  } else {
    task_op->set_trans_desc(session->get_tx_desc());
    task_op->set_snapshot(&get_exec_ctx().get_das_ctx().get_snapshot());
    task_op->set_write_branch_id(get_exec_ctx().get_das_ctx().get_write_branch_id());
    
    task_op->set_task_id(0);
    task_op->in_stmt_retry_ = session->get_is_in_retry();
    task_op->set_tablet_id(tablet_access->tablet_id_);
    task_op->set_tablet_access(tablet_access);
    task_op->set_vec_datum_kind(vec_datum_kind);
    if (is_snapshot_opt_enabled() && OB_FAIL(task_op->init_das_snapshot_opt_info(session->get_tx_isolation()))) {
    } else if (OB_FAIL(add_aggregated_task(task_op, op_type))) {
    }
  }
  return ret;
}

int ObDASRef::find_agg_task(ObDASOpType op_type, ObDasAggregatedTask *&agg_task)
{
  DasAggregatedTaskList &tasks = DAS_OP_TABLE_DELETE == op_type ? del_aggregated_tasks_ : aggregated_tasks_;
  if (!tasks.empty()) {
    agg_task = tasks.get_obj_list().get_first()->get_obj();
  }
  return OB_SUCCESS;
}

int ObDASRef::create_agg_task(ObDASOpType op_type, ObDasAggregatedTask *&agg_task)
{
  int ret = OB_SUCCESS;
  // create agg_task
  void *buf = das_alloc_.alloc(sizeof(ObDasAggregatedTask));
  ObDasAggregatedTask *tmp_agg_task = nullptr;
  agg_task = nullptr;
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(tmp_agg_task = new(buf) ObDasAggregatedTask())) {
  } else if (DAS_OP_TABLE_DELETE == op_type &&
      OB_FAIL(del_aggregated_tasks_.store_obj(tmp_agg_task))) {
  } else if (DAS_OP_TABLE_DELETE != op_type &&
      OB_FAIL(aggregated_tasks_.store_obj(tmp_agg_task))) {
  } else {
    agg_task = tmp_agg_task;
  }
  return ret;
}

int ObDASRef::add_aggregated_task(ObIDASTaskOp *das_task, ObDASOpType op_type)
{
  int ret = OB_SUCCESS;
  bool aggregated = false;
  ObDasAggregatedTask *agg_task = nullptr;

  if (OB_FAIL(find_agg_task(op_type, agg_task))) {
  } else if (OB_NOT_NULL(agg_task)) {
    if (OB_FAIL(OB_FAIL(agg_task->push_back_task(das_task)))) {
    }
  } else {
    // create agg_task
    if (OB_FAIL(create_agg_task(op_type, agg_task))) {
    } else if (OB_FAIL(agg_task->push_back_task(das_task))) {
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(add_batched_task(das_task))) {
  }
  return ret;
}

void ObDASRef::reset()
{
  das_factory_.cleanup();
  batched_tasks_.destroy();
  del_aggregated_tasks_.destroy();
  aggregated_tasks_.destroy();
  lookup_cnt_ = 0;
  task_cnt_ = 0;
  init_mem_used_ = 0;
  if (task_map_.created()) {
    task_map_.destroy();
  }
  flags_ = false;
  expr_frame_info_ = nullptr;
  if (reuse_alloc_ != nullptr) {
    reuse_alloc_->reset();
    reuse_alloc_ = nullptr;
  }
}

void ObDASRef::reuse()
{
  das_factory_.cleanup();
  batched_tasks_.destroy();
  del_aggregated_tasks_.destroy();
  aggregated_tasks_.destroy();
  lookup_cnt_ = 0;
  task_cnt_ = 0;
  init_mem_used_ = 0;
  if (task_map_.created()) {
    task_map_.destroy();
  }
  if (reuse_alloc_ != nullptr) {
    reuse_alloc_->reset_remain_one_page();
  } else {
    reuse_alloc_ = new(&reuse_alloc_buf_) common::ObArenaAllocator();
    reuse_alloc_->set_attr(das_alloc_.get_attr());
    das_alloc_.set_alloc(reuse_alloc_);
  }
}

void ObDasAggregatedTask::reset()
{
  tasks_.reset();
  failed_tasks_.reset();
  success_tasks_.reset();
}

void ObDasAggregatedTask::reuse()
{
  tasks_.reset();
  failed_tasks_.reset();
  success_tasks_.reset();
}

int ObDasAggregatedTask::push_back_task(ObIDASTaskOp *das_task)
{
  int ret = OB_SUCCESS;
  if (das_task->get_cur_agg_list()) {
    // if task already have linked list (in task retry), remove it first
    das_task->get_cur_agg_list()->remove(&das_task->get_node());
  }
  if (OB_UNLIKELY(!tasks_.add_last(&das_task->get_node()))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    das_task->set_cur_agg_list(&tasks_);
  }
  if (OB_SUCC(ret) && !das_task->get_agg_task()) {
    das_task->set_agg_task(this);
  }
  return ret;
}

int ObDasAggregatedTask::get_aggregated_tasks(common::ObIArray<ObIDASTaskOp *> &tasks) {
  int ret = OB_SUCCESS;
  ObIDASTaskOp *cur_task = nullptr;
  // 1. if have failed tasks, should explicitly get failed task via get_failed_tasks().
  if (OB_UNLIKELY(failed_tasks_.get_size() != 0)) {
    ret = OB_ERR_UNEXPECTED;
  }

  // 3. if no unfinished high priority aggregated tasks exist, return all normal aggregated tasks.
  if (tasks.count() == 0 && OB_SUCC(ret)) {
    DLIST_FOREACH_X(curr, tasks_, OB_SUCC(ret)) {
      cur_task = curr->get_data();
      OB_ASSERT(cur_task != nullptr);
      OB_ASSERT(cur_task->get_cur_agg_list() == &tasks_);
      OB_ASSERT(ObDasTaskStatus::UNSTART == cur_task->get_task_status());
      if (OB_FAIL(tasks.push_back(cur_task))) {
      }
    }
  }
  return ret;
}

int ObDasAggregatedTask::move_to_success_tasks(ObIDASTaskOp *das_task)
{
  int ret = OB_SUCCESS;
  das_task->get_cur_agg_list()->remove(&das_task->get_node());
  if (OB_UNLIKELY(!success_tasks_.add_last(&das_task->get_node()))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    das_task->set_cur_agg_list(&success_tasks_);
  }
  return ret;
}

int ObDasAggregatedTask::move_to_failed_tasks(ObIDASTaskOp *das_task)
{
  int ret = OB_SUCCESS;
  das_task->get_cur_agg_list()->remove(&das_task->get_node());
  if (OB_UNLIKELY(!failed_tasks_.add_last(&das_task->get_node()))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    das_task->set_cur_agg_list(&failed_tasks_);
  }
  return ret;
}

int ObDasAggregatedTask::get_failed_tasks(common::ObSEArray<ObIDASTaskOp *, 2> &tasks)
{
  int ret = OB_SUCCESS;
  ObIDASTaskOp *cur_task = nullptr;
  DLIST_FOREACH_X(curr, failed_tasks_, OB_SUCC(ret)) {
    cur_task = curr->get_data();
    OB_ASSERT(cur_task != nullptr);
    OB_ASSERT(ObDasTaskStatus::FAILED == cur_task->get_task_status());
    if (OB_FAIL(tasks.push_back(cur_task))) {
    }
  }
  return ret;
}

bool ObDasAggregatedTask::has_unstart_tasks() const
{
  return tasks_.get_size() != 0 ;
}

}  // namespace sql
}  // namespace oceanbase
