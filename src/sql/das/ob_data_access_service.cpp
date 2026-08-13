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
#include "query/runtime/ob_query_runtime_environment.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/das/ob_data_access_service.h"
#include "sql/das/ob_das_utils.h"
#include "sql/das/ob_das_parallel_handler.h"
#include "sql/ob_query_retry_ctrl.h"

namespace oceanbase
{
using namespace share;
using namespace storage;
using namespace common;
using namespace transaction;
namespace sql
{

ObDataAccessService::ObDataAccessService()
  : id_allocator_(),
    das_concurrency_limit_(INT32_MAX)
{
}


void ObDataAccessService::server_module_destroy(ObDataAccessService *&das)
{
  if (das != nullptr) {
    das->~ObDataAccessService();
    oceanbase::common::ob_delete(das);
    das = nullptr;
  }
}

int ObDataAccessService::execute_das_task(
    ObDASRef &das_ref, ObDasAggregatedTask &task_ops) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(execute_local_das_task(task_ops))) {
  }
  DAS_CTX(das_ref.get_exec_ctx()).save_cur_exec_status(ret);
  return ret;
}

int ObDataAccessService::get_das_task_id(int64_t &das_id)
{
  return id_allocator_.get_next_id(das_id);
}


OB_NOINLINE int ObDataAccessService::execute_local_das_task(
    ObDasAggregatedTask &task_ops) {
  int ret = OB_SUCCESS;
  common::ObSEArray<ObIDASTaskOp *, 2> task_list;
  if (OB_FAIL(task_ops.get_aggregated_tasks(task_list))) {
  } else if (task_list.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected empty task_list", K(ret));
  } else if (OB_FAIL(do_local_das_task(task_list))) {
  }
  return ret;
}

int ObDataAccessService::clear_task_exec_env(ObDASRef &das_ref, ObIDASTaskOp &task_op)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(task_op.end_das_task())) {
  }
  DAS_CTX(das_ref.get_exec_ctx()).save_cur_exec_status(OB_SUCCESS);
  return ret;
}

int ObDataAccessService::refresh_task_location_info(ObDASRef &das_ref, ObIDASTaskOp &task_op)
{
  int ret = OB_SUCCESS;
  ObExecContext &exec_ctx = das_ref.get_exec_ctx();
  ObDASTabletLoc *tablet_loc = const_cast<ObDASTabletLoc*>(task_op.get_tablet_loc());
  int64_t retry_cnt = DAS_CTX(exec_ctx).get_cur_retry_cnt();
  if (OB_FAIL(ObDASUtils::wait_das_retry(retry_cnt))) {
  } else if (OB_FAIL(DAS_CTX(exec_ctx).build_local_tablet_loc(tablet_loc->loc_meta_->ref_table_id_,
                                                              tablet_loc->tablet_id_,
                                                              *tablet_loc))) {
  }
  return ret;
}

int ObDataAccessService::retry_das_task(ObDASRef &das_ref, ObIDASTaskOp &task_op)
{
  int ret = task_op.errcode_;
  ObDasAggregatedTask das_task_wrapper;
  bool retry_continue = false;
  ObDASCtx &das_ctx = DAS_CTX(das_ref.get_exec_ctx());
  das_ctx.reset_cur_retry_cnt();
  do {
    ObDASRetryCtrl::retry_func retry_func = nullptr;

    retry_continue = false;
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(ObQueryRetryCtrl::get_das_retry_func(task_op.errcode_, retry_func))) {
    } else if (retry_func != nullptr) {
      bool need_retry = false;
      const ObDASTabletLoc *tablet_loc = task_op.get_tablet_loc();
      const ObDASTableLocMeta *loc_meta = tablet_loc != nullptr ? tablet_loc->loc_meta_ : nullptr;
      retry_func(das_ref, task_op, need_retry);
      LOG_INFO("[DAS RETRY] check if need tablet level retry",
               KR(task_op.errcode_), K(need_retry), K(task_op.task_flag_),
               "continuous_retry_cnt", das_ctx.get_cur_retry_cnt(),
               "total_retry_cnt", das_ctx.get_total_retry_cnt(),
               KPC(loc_meta), KPC(tablet_loc));
      if (need_retry &&
          task_op.get_inner_rescan() &&
          das_ctx.get_total_retry_cnt() > 100) { //hard code retry 100 times.
        // disable das retry for rescan.
        need_retry = false;
        retry_continue = false;
        LOG_INFO("[DAS RETRY] The rescan task has retried too many times and has exited the DAS retry process");
      }
      if (need_retry) {
        task_op.in_part_retry_ = true;
        das_ctx.set_last_errno(task_op.get_errcode());
        das_ctx.inc_cur_retry_cnt();
        if (OB_TMP_FAIL(clear_task_exec_env(das_ref, task_op))) {
        }
        if (OB_FAIL(das_ref.get_exec_ctx().check_status())) {
        } else if (OB_FAIL(refresh_task_location_info(das_ref, task_op))) {
        } else {
          LOG_INFO("[DAS RETRY] Start retrying the DAS task now", KPC(task_op.get_tablet_loc()));
          das_task_wrapper.reuse();
          task_op.set_task_status(ObDasTaskStatus::UNSTART);
          if (OB_FAIL(das_task_wrapper.push_back_task(&task_op))) {
          } else if (OB_FAIL(execute_local_das_task(das_task_wrapper))) {
          }
          if (OB_SUCCESS == ret) {
            LOG_INFO("[DAS RETRY] DAS Task succeeds after multiple retries",
                     "continuous_retry_cnt", das_ctx.get_cur_retry_cnt(),
                     "total_retry_cnt", das_ctx.get_total_retry_cnt(),
                     KPC(task_op.get_tablet_loc()));
          } else {
            int64_t cur_retry_cnt = das_ctx.get_cur_retry_cnt();
            int64_t total_retry_cnt = das_ctx.get_total_retry_cnt();
            if (cur_retry_cnt >= 100 && cur_retry_cnt % 50L == 0) {
              LOG_INFO("[DAS RETRY] The DAS task has been retried multiple times without success, "
                       "and the execution may be blocked by a specific exception", KR(ret),
                       "continuous_retry_cnt", cur_retry_cnt,
                       "total_retry_cnt", das_ctx.get_total_retry_cnt(),
                       KPC(task_op.get_tablet_loc()));
            }
          }
        }
        task_op.errcode_ = ret;
        retry_continue = (OB_SUCCESS != ret);
        if (!retry_continue) {
          das_ctx.accumulate_retry_count();
        }
        if (retry_continue && IS_INTERRUPTED()) {
          retry_continue = false;
          LOG_INFO("[DAS RETRY] Retry is interrupted by worker interrupt signal", KR(ret));
        }
      } else {
        ret = task_op.errcode_;
      }
    }
  } while (retry_continue);


  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(task_op.state_advance())) {
    }
  }
  OB_ASSERT(das_task_wrapper.has_unstart_tasks() == false &&
      das_task_wrapper.success_tasks_.get_size() == 0);
  return ret;
}

int ObDataAccessService::end_das_task(ObDASRef &das_ref, ObIDASTaskOp &task_op)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(task_op.end_das_task())) {
  }
  return ret;
}

int ObDataAccessService::rescan_das_task(ObDASRef &das_ref, ObDASScanOp &scan_op)
{
  int ret = OB_SUCCESS;

  ObDasAggregatedTask das_task_wrapper;
  if (scan_op.is_local_task()) {
    if (OB_FAIL(scan_op.rescan())) {
    }
  } else if (OB_FAIL(das_task_wrapper.push_back_task(&scan_op))) {
  } else if (OB_FAIL(execute_local_das_task(das_task_wrapper))) {
    scan_op.errcode_ = ret;
    LOG_WARN("execute local das task failed", K(ret));
  }
  OB_ASSERT(scan_op.errcode_ == ret);
  if (OB_FAIL(ret) && GCONF._enable_partition_level_retry && scan_op.can_part_retry()) {
    //only fast select can be retry with partition level
    if (OB_FAIL(retry_das_task(das_ref, scan_op))) {
    }
  }
  return ret;
}

int ObDataAccessService::do_local_das_task(ObIArray<ObIDASTaskOp*> &task_list) {
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < task_list.count(); i++) {
    if (OB_FAIL(task_list.at(i)->start_das_task())) {
      LOG_WARN("start local das task failed", K(ret));
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(task_list.at(i)->state_advance())) {
      }
      break;
    } else {
      if (OB_FAIL(task_list.at(i)->state_advance())) {
      }
    }
  }
  return ret;
}

int ObDataAccessService::push_parallel_task(ObDASRef &das_ref, ObDasAggregatedTask &agg_task)
{
  int ret = OB_SUCCESS;
  ObDASParallelTask *task = nullptr;
  query::ObIQueryRuntimeEnvironment *runtime =
      das_ref.get_exec_ctx().get_query_runtime_environment();
  ObPhysicalPlanCtx *plan_ctx = das_ref.get_exec_ctx().get_physical_plan_ctx();
  int64_t timeout_ts = plan_ctx->get_timeout_timestamp();
  if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "query runtime environment is null", KR(ret));
  } else if (OB_ISNULL(task = ObDASParallelTaskFactory::alloc(das_ref.get_das_ref_count_ctx()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory failed", K(ret));
  } else if (OB_FAIL(task->init(&agg_task, timeout_ts))) {
  } else {
    
    if (OB_FAIL(runtime->submit_current_tenant_request(*task))) {
    } else {
    }
  }
  if (OB_FAIL(ret)) {
    ObDASParallelTaskFactory::free(task);
    task = NULL;
  }
  return ret;
}
int ObDataAccessService::parallel_execute_das_task(common::ObIArray<ObIDASTaskOp *> &task_list)
{
  int ret = OB_SUCCESS;
  if (task_list.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected empty task_list", K(ret));
  } else if (OB_FAIL(do_local_das_task(task_list))) {
  }
  return ret;
}
int ObDataAccessService::parallel_submit_das_task(ObDASRef &das_ref, ObDasAggregatedTask &agg_task)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = das_ref.get_exec_ctx().get_my_session();
  int64_t timeout_ts = session->get_query_timeout_ts();
  if (OB_FAIL(das_ref.get_das_ref_count_ctx().acquire_task_execution_resource(timeout_ts))) {
  } else if (OB_FAIL(push_parallel_task(das_ref, agg_task))) {
    // NOTICE: if error occur, must release the reference count
    das_ref.get_das_ref_count_ctx().inc_concurrency_limit();
    LOG_WARN("fail to push parallel task", K(ret));
  }
  return ret;
}

int ObDataAccessService::collect_das_copy_refs(ObIArray<ObIDASTaskOp*> &task_ops,
                                               ObDASCopyContext &copy_context)
{
  int ret = OB_SUCCESS;
  ObIDASTaskOp *task_op = nullptr;
  for (int i = 0; OB_SUCC(ret) && i < task_ops.count(); i++) {
    task_op = task_ops.at(i);
    if (task_op->get_ctdef() != nullptr) {
      if (OB_FAIL(add_var_to_array_no_dup(copy_context.ctdefs_, task_op->get_ctdef()))) {
      }
    }
    if (OB_SUCC(ret) && task_op->get_rtdef() != nullptr) {
      if (OB_FAIL(add_var_to_array_no_dup(copy_context.rtdefs_, task_op->get_rtdef()))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(append_array_no_dup(copy_context.ctdefs_, task_op->get_related_ctdefs()))) {
      } else if (OB_FAIL(append_array_no_dup(copy_context.rtdefs_, task_op->get_related_rtdefs()))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(collect_das_copy_attach_refs(copy_context, task_op->get_attach_rtdef()))) {
      }
    }
  }
  return ret;
}

int ObDataAccessService::collect_das_copy_attach_refs(ObDASCopyContext &copy_context,
                                                       ObDASBaseRtDef *attach_rtdef)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(attach_rtdef)) {
    if (attach_rtdef->ctdef_ != nullptr) {
      if (OB_FAIL(add_var_to_array_no_dup(copy_context.ctdefs_, attach_rtdef->ctdef_))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(add_var_to_array_no_dup(copy_context.rtdefs_, attach_rtdef))) {
      }
    }
    for (int i = 0; OB_SUCC(ret) && i < attach_rtdef->children_cnt_; ++i) {
      if (OB_FAIL(collect_das_copy_attach_refs(copy_context, attach_rtdef->children_[i]))) {
      }
    }
  }
  return ret;
}

void ObDataAccessService::set_max_concurrency(int32_t cpu_count)
{
  das_concurrency_limit_ = 1;
}

}  // namespace sql
}  // namespace oceanbase
