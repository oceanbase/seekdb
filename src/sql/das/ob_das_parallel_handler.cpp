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
#include "sql/das/ob_das_parallel_handler.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/das/ob_data_access_service.h"
#include "lib/profile/ob_trace_id.h"
#include "sql/engine/ob_exec_context.h"
using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::lib;
using namespace oceanbase::share;
int64_t ObDASParallelTaskFactory::alloc_count_;
int64_t ObDASParallelTaskFactory::free_count_;

void OB_WEAK_SYMBOL request_finish_callback();

int ObDASParallelHandler::init(rpc::ObSrvTask *task)
{
  int ret = OB_SUCCESS;
  if (NULL == task) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(task));
  } else {
    task_ = task;
  }
  return ret;
}

int ObDASParallelHandler::deep_copy_all_das_tasks(ObDASTaskFactory &das_factory,
                                                  ObIAllocator &alloc,
                                                  ObIArray<ObIDASTaskOp*> &src_task_list,
                                                  ObIArray<ObIDASTaskOp*> &new_task_list,
                                                  ObDASCopyContext &copy_context,
                                                  ObDasAggregatedTask &das_task_wrapper)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObDataAccessService>()->collect_das_copy_refs(src_task_list,
                                                                        copy_context))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < src_task_list.count(); i++) {
      ObIDASTaskOp *das_op = nullptr;
      if (OB_FAIL(deep_copy_das_task(das_factory, src_task_list.at(i), das_op, alloc))) {
      } else if (OB_ISNULL(das_op)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null ptr", K(ret));
      } else if (OB_FAIL(new_task_list.push_back(das_op))) {
      } else if (OB_FAIL(das_task_wrapper.push_back_task(das_op))) {
      }
    }
  }
  return ret;
}


int ObDASParallelHandler::deep_copy_das_task(ObDASTaskFactory &das_factory,
                                             ObIDASTaskOp *src_op,
                                             ObIDASTaskOp *&dst_op,
                                             ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  ObIDASTaskOp *das_op = nullptr;
  if (OB_ISNULL(src_op->get_agg_task()) || OB_ISNULL(src_op->get_cur_agg_list())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null agg_task", K(ret), K(src_op->get_agg_task()), K(src_op->get_cur_agg_list()));
  } else if (OB_FAIL(das_factory.create_das_task_op(src_op->get_type(), das_op))) {
  } else if (OB_FAIL(das_op->init_task_info(ObDASWriteBuffer::DAS_ROW_DEFAULT_EXTEND_SIZE))) {
  } else {
    int64_t ser_pos = 0;
    int64_t des_pos = 0;
    void *ser_ptr = NULL;
    das_op->trans_desc_ = src_op->trans_desc_;
    das_op->snapshot_ = src_op->snapshot_;
    int64_t ser_arg_len = src_op->get_serialize_size();
    if (OB_ISNULL(ser_ptr = alloc.alloc(ser_arg_len))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail alloc memory", K(ser_arg_len), KP(ser_ptr), K(ret));
    } else if (OB_FAIL(src_op->serialize(static_cast<char *>(ser_ptr), ser_arg_len, ser_pos))) {
    } else if (OB_FAIL(das_op->deserialize(static_cast<const char *>(ser_ptr), ser_pos, des_pos))) {
    } else if (ser_pos != des_pos) {
      ret = OB_DESERIALIZE_ERROR;
      LOG_WARN("data_len and pos mismatch", K(ser_arg_len), K(ser_pos), K(des_pos), K(ret));
    } else {
      das_op->set_tablet_loc(src_op->get_tablet_loc());
      dst_op = das_op;
    }
  }
  return ret;
}
int ObDASParallelHandler::record_status_and_op_result(ObIDASTaskOp *src_op, ObIDASTaskOp *dst_op)
{
  int ret = OB_SUCCESS;
  // record all affected_row and other info
  if (dst_op->get_task_status() == ObDasTaskStatus::UNSTART) {
    src_op->set_task_status(ObDasTaskStatus::UNSTART);
  } else if (dst_op->get_task_status() == ObDasTaskStatus::FINISHED) {
    src_op->set_task_status(ObDasTaskStatus::FINISHED);
    src_op->errcode_ = OB_SUCCESS;
    if (OB_FAIL(src_op->state_advance())) {
    } else if (OB_FAIL(src_op->assign_task_result(dst_op))) {
    }
  } else if (dst_op->get_task_status() == ObDasTaskStatus::FAILED) {
    src_op->set_task_status(ObDasTaskStatus::FAILED);
    src_op->errcode_ = dst_op->errcode_;
    if (OB_FAIL(src_op->state_advance())) {
    }
  }
  return ret;
}

int ObDASParallelHandler::run()
{
  int ret = OB_SUCCESS;
  // execute all das_tasks
  ObDASParallelTask *task = static_cast<ObDASParallelTask *>(task_);
  common::ObSEArray<ObIDASTaskOp*, 4> new_task_list;
  common::ObSEArray<ObIDASTaskOp*, 4> src_task_list;
  lib::MemoryContext mem_context = nullptr;
  common::ObCurTraceId::set(task->get_trace_id());
  THIS_WORKER.set_timeout_ts(task->get_timeout_ts());
  // Single-process resource owner.
  static const uint64_t PROCESS_OWNER_ID = 1;
  CREATE_WITH_TEMP_ENTITY(RESOURCE_OWNER, PROCESS_OWNER_ID) {
    int interrupted_code = task->get_das_ref_count_ctx().get_interrupted_err_code();
    if (interrupted_code != OB_SUCCESS) {
      task->get_agg_task()->set_save_ret(interrupted_code);
      LOG_WARN("this task is interrupted,ret_code is", K(interrupted_code));
    } else if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(mem_context,
        lib::ContextParam().set_mem_attr("DASParallelTask")))) {
    } else {
      WITH_CONTEXT(mem_context) {
        ObDASCopyContext copy_context;
        ObArenaAllocator tmp_alloc;
        ObDASTaskFactory das_factory(mem_context->get_arena_allocator());
        ObDasAggregatedTask das_task_wrapper;
        if (OB_FAIL(task->get_agg_task()->get_aggregated_tasks(src_task_list))) {
        } else {
          ObDASCopyContext *saved_context = ObDASCopyContext::get_copy_context();
          ObDASCopyContext::get_copy_context() = &copy_context;
          ret = deep_copy_all_das_tasks(das_factory,
                                        mem_context->get_arena_allocator(),
                                        src_task_list,
                                        new_task_list,
                                        copy_context,
                                        das_task_wrapper);
          ObDASCopyContext::get_copy_context() = saved_context;
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObDataAccessService>()->parallel_execute_das_task(new_task_list))) {
          }
        }

        // close new_task_list and copy all task execute result
        int last_ret = OB_SUCCESS;
        for (int64_t i = 0; i < new_task_list.count(); i++) {
          int tmp_ret = OB_SUCCESS;
          if (OB_SUCCESS != (tmp_ret = record_status_and_op_result(src_task_list.at(i), new_task_list.at(i)))) {
          }
          last_ret = OB_SUCCESS == last_ret ? tmp_ret : last_ret;
          if (OB_SUCCESS != (tmp_ret = new_task_list.at(i)->end_das_task())) {
          }
          last_ret = OB_SUCCESS == last_ret ? tmp_ret : last_ret;
        }
        ret = OB_SUCCESS == ret ? last_ret : ret;
      } // end for mem_context
    }
  }
  if (nullptr != mem_context) {
    DESTROY_CONTEXT(mem_context);
    mem_context = NULL;
  }

  task->get_agg_task()->set_save_ret(ret);
  if (ret != OB_SUCCESS) {
    task->get_das_ref_count_ctx().interrupt_other_workers(ret);
  }
  // whether success or failure,we must dec the reference_count
  task->get_das_ref_count_ctx().inc_concurrency_limit_with_signal();
  request_finish_callback();
  // cover the error code
  ret = OB_SUCCESS;
  return ret;
}

int ObDASParallelTask::init(ObDasAggregatedTask *agg_task, int64_t timeout_ts)
{
  int ret = OB_SUCCESS;
  if (NULL == agg_task) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task is null, unexpected error", K(ret));
  } else if (OB_FAIL(handler_.init(this))) {
  } else {
    agg_task_ = agg_task;
    timeout_ts_ = timeout_ts;
    trace_id_.set(*ObCurTraceId::get_trace_id());
    set_type(ObRequest::OB_DAS_PARALLEL_TASK);
  }
  return ret;
}
ObDASParallelTask *ObDASParallelTaskFactory::alloc(DASRefCountContext &ref_count_ctx)
{
  ObDASParallelTask *task = NULL;
  if (NULL != (task = op_alloc_args(ObDASParallelTask, ref_count_ctx))) {
    (void)ATOMIC_FAA(&alloc_count_, 1);
    if (REACH_TIME_INTERVAL(3 * 1000 * 1000)) {
      LOG_INFO("DAS parallel task allocation statistics", K_(alloc_count), K_(free_count));
    }
  }
  return task;
}
void ObDASParallelTaskFactory::free(ObDASParallelTask *task)
{
  if (NULL != task) {
    op_reclaim_free(task);
    task = NULL;
    (void)ATOMIC_FAA(&free_count_, 1);
  }
}
