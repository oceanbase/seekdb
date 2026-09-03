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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_px_worker.h"
#include "share/rc/ob_server_runtime.h"
#include "query/runtime/ob_query_runtime_environment.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/engine/px/ob_px_admission.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::lib;
using namespace oceanbase::share;


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

ObPxCoroWorker::ObPxCoroWorker(const share::ObGlobalContext &gctx,
                               common::ObIAllocator &alloc)
  : gctx_(gctx),
    alloc_(alloc),
    exec_ctx_(alloc, share::server_service<ObSQLSessionMgr>()),
    phy_plan_(),
    task_arg_(),
    task_proc_(gctx, task_arg_),
    task_co_id_(0)
{
}

int ObPxCoroWorker::run(ObPxInitTaskArgs &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(deep_copy_assign(arg, task_arg_))) {
  } else {
  }
  return ret;
}

int ObPxCoroWorker::exit()
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_INIT;
  return ret;
}

int ObPxCoroWorker::deep_copy_assign(const ObPxInitTaskArgs &src,
                                     ObPxInitTaskArgs &dest)
{
  int ret = OB_SUCCESS;
  dest.set_deserialize_param(exec_ctx_, phy_plan_, &alloc_);
  // Deep copy all elements in arg, into session, op tree, etc.
  // Temporarily complete through serialization+deserialization
  int64_t ser_pos = 0;
  int64_t des_pos = 0;
  void *ser_ptr = NULL;
  int64_t ser_arg_len = src.get_serialize_size();

  if (OB_ISNULL(ser_ptr = alloc_.alloc(ser_arg_len))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail alloc memory", K(ser_arg_len), KP(ser_ptr), K(ret));
  } else if (OB_FAIL(src.serialize(static_cast<char *>(ser_ptr), ser_arg_len, ser_pos))) {
  } else if (OB_FAIL(dest.deserialize(static_cast<const char *>(ser_ptr), ser_pos, des_pos))) {
  } else if (ser_pos != des_pos) {
    ret = OB_DESERIALIZE_ERROR;
    LOG_WARN("data_len and pos mismatch", K(ser_arg_len), K(ser_pos), K(des_pos), K(ret));
  } else {
    dest.exec_ctx_->set_runtime_services(
        src.exec_ctx_->get_runtime_services());
    // PLACE_HOLDER: if want to shared trans_desc
    // dest.exec_ctx_->get_my_session()->set_effective_trans_desc(src.exec_ctx_->get_my_session()->get_effective_trans_desc());
  }
  return ret;
}



//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
class SQCHandlerGuard
{
public:
  SQCHandlerGuard(ObPxSqcHandler *h) : sqc_handler_(h)
  {
    if (OB_LIKELY(sqc_handler_)) {
      sqc_handler_->get_notifier().worker_start(GETTID());
    }
  }
  ~SQCHandlerGuard()
  {
    if (OB_LIKELY(sqc_handler_)) {
      sqc_handler_->worker_end_hook();
      int report_ret = OB_SUCCESS;
      ObPxSqcHandler::release_handler(sqc_handler_, report_ret);
      sqc_handler_ = nullptr;
    }
  }
private:
  ObPxSqcHandler *sqc_handler_;
};

void PxWorkerFunctor::operator ()(bool need_exec)
{
  int ret = OB_SUCCESS;
  const char *px_parallel_rule_str = nullptr;
  if (task_arg_.op_spec_root_ != nullptr && task_arg_.op_spec_root_->plan_ != nullptr) {
    PXParallelRule px_parallel_rule = task_arg_.op_spec_root_->plan_->get_px_parallel_rule();
    px_parallel_rule_str = ob_px_parallel_rule_str(px_parallel_rule);
  }
  ObCurTraceId::set(env_arg_.get_trace_id());
  /**
   * The interrupt must cover the release handler, because its process involves sqc sending messages to qc,
   * requiring an interrupt check. The interrupt itself is thread-local and runtime-independent.
   */
  ObPxInterruptGuard px_int_guard(task_arg_.task_.get_interrupt_id().px_interrupt_id_);
  ObPxSqcHandler *sqc_handler = task_arg_.get_sqc_handler();
  SQCHandlerGuard sqc_handler_guard(sqc_handler);
  lib::MemoryContext mem_context = nullptr;
  //ensure PX worker skip updating timeout_ts_ by ntp offset
  THIS_WORKER.set_ntp_offset(0);
  if (!need_exec) {
    LOG_INFO("px pool already stopped, do not execute the task.");
  } else if (OB_FAIL(px_int_guard.get_interrupt_reg_ret())) {
  } else if (OB_NOT_NULL(sqc_handler) && OB_LIKELY(!sqc_handler->has_interrupted())) {
    THIS_WORKER.set_worker_level(sqc_handler->get_request_level());
    THIS_WORKER.set_curr_request_level(sqc_handler->get_request_level());
    // Do not set thread local log level while log level upgrading (OB_LOGGER.is_info_as_wdiag)
    if (OB_LOGGER.is_info_as_wdiag()) {
      ObThreadLogLevelUtils::clear();
    } else {
      if (OB_LOG_LEVEL_NONE != env_arg_.get_log_level()) {
        ObThreadLogLevelUtils::init(env_arg_.get_log_level());
      }
    }
    // Single-process resource owner.
    static const uint64_t PROCESS_OWNER_ID = 1;
    SERVER_MODULE_SCOPE {
      CREATE_WITH_TEMP_ENTITY(RESOURCE_OWNER, PROCESS_OWNER_ID) {
        if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(mem_context,
            lib::ContextParam().set_mem_attr(ObModIds::OB_SQL_PX)))) {
        } else {
          WITH_CONTEXT(mem_context) {
            lib::ContextTLOptGuard guard(true);
            // In the worker thread, perform a deep copy of args to alleviate the burden on the sqc thread.
            ObPxInitTaskArgs runtime_arg;
            if (OB_FAIL(runtime_arg.init_deserialize_param(
                    task_arg_, mem_context, *env_arg_.get_gctx()))) {
            } else if (OB_FAIL(runtime_arg.deep_copy_assign(task_arg_, mem_context->get_arena_allocator()))) {
            } else {
              // Bind sqc_handler, convenient for the operator to get sqc_handle anywhere
              runtime_arg.sqc_handler_ = sqc_handler;
            }
            // Execute
            ObPxTaskProcess worker(*env_arg_.get_gctx(), runtime_arg);
            if (OB_SUCC(ret)) {
              worker.run();
            }
            runtime_arg.destroy();
          }
        }
      }
      if (nullptr != mem_context) {
        DESTROY_CONTEXT(mem_context);
        mem_context = NULL;
      }
    }
    ObThreadLogLevelUtils::clear();
  } else if (OB_ISNULL(sqc_handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("Unexpected null sqc handler", K(sqc_handler));
  } else {
    LOG_WARN("already interrupted");
  }

  //if start worker failed, still need set task state, interrupt qc
  if (OB_FAIL(ret)) {
    if (task_arg_.sqc_task_ptr_ != NULL) {
      task_arg_.sqc_task_ptr_->set_task_state(SQC_TASK_EXIT);
    }
    (void) ObInterruptUtil::interrupt_qc(task_arg_.task_, ret);
  }

  PxWorkerFinishFunctor on_func_finish;
  on_func_finish();
  ObCurTraceId::reset();
}

void PxWorkerFinishFunctor::operator ()()
{
  // Each worker ends, a slot is released
  ObPxSubAdmission::release(1);
}


ObPxThreadWorker::ObPxThreadWorker(const share::ObGlobalContext &gctx)
  : gctx_(gctx),
    task_co_id_(0)
{
}

ObPxThreadWorker::~ObPxThreadWorker()
{
}
// Execute in the px_pool corresponding to the group
int ObPxThreadWorker::run(ObPxInitTaskArgs &task_arg)
{
  int ret = OB_SUCCESS;
  static constexpr int64_t DEFAULT_PX_GROUP_ID = 0;
  query::ObIQueryRuntimeEnvironment *runtime = OB_ISNULL(task_arg.exec_ctx_)
      ? nullptr : task_arg.exec_ctx_->get_query_runtime_environment();
  if (OB_ISNULL(runtime)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("query runtime environment is unavailable", K(ret));
  } else {
    ObPxWorkerEnvArgs env_args;
    env_args.set_enqueue_timestamp(ObTimeUtility::current_time());
    env_args.set_trace_id(*ObCurTraceId::get_trace_id());
    env_args.set_gctx(&gctx_);
    if (OB_LOG_LEVEL_NONE != common::ObThreadLogLevelUtils::get_level()) {
      env_args.set_log_level(common::ObThreadLogLevelUtils::get_level());
    }
    PxWorkerFunctor task(env_args, task_arg);
    if (OB_FAIL(runtime->submit_px_task(DEFAULT_PX_GROUP_ID, task))) {
    }
  }
  return ret;
}

int ObPxThreadWorker::exit()
{
  // SQC will wait all PxWorker finish.
  // Just return success.
  return OB_SUCCESS;
}

int ObPxLocalWorker::run(ObPxInitTaskArgs &task_arg)
{
  int ret = OB_SUCCESS;

  {
    ObPxTaskProcess task_proc(gctx_, task_arg);
    ret = task_proc.process();
  }

  return ret;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

ObPxThreadWorker * ObPxThreadWorkerFactory::create_worker()
{
  ObPxThreadWorker *worker = NULL;
  int ret = OB_SUCCESS;
  void *ptr = alloc_.alloc(sizeof(ObPxThreadWorker));
  if (OB_NOT_NULL(ptr)) {
    worker = new(ptr)ObPxThreadWorker(gctx_);
    if (OB_FAIL(workers_.push_back(worker))) {
    }
    if (OB_SUCCESS != ret) {
      worker->~ObPxThreadWorker();
      worker = NULL;
    }
  }
  return worker;
}

int ObPxThreadWorkerFactory::join()
{
  int ret = OB_SUCCESS;
  int eret = OB_SUCCESS;
  for (int64_t i = 0; i < workers_.count(); ++i) {
    if (OB_SUCCESS != (eret = workers_.at(i)->exit())) {
      ret = eret; // try join as many workers as possible, return last error
      LOG_ERROR("fail join px thread workers", K(ret));
    }
  }
  return ret;
}

void ObPxThreadWorkerFactory::destroy()
{
  for (int64_t i = 0; i < workers_.count(); ++i) {
    workers_.at(i)->~ObPxThreadWorker();
  }
  workers_.reset();
}

ObPxThreadWorkerFactory::~ObPxThreadWorkerFactory()
{
  destroy();
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////




void ObPxCoroWorkerFactory::destroy()
{
  for (int64_t i = 0; i < workers_.count(); ++i) {
    workers_.at(i)->~ObPxCoroWorker();
  }
}

ObPxCoroWorkerFactory::~ObPxCoroWorkerFactory()
{
  destroy();
}


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


ObPxWorkerRunnable *ObPxLocalWorkerFactory::create_worker()
{
  return &worker_;
}

void ObPxLocalWorkerFactory::destroy()
{
}

ObPxLocalWorkerFactory::~ObPxLocalWorkerFactory()
{
  destroy();
}



//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
int ObPxWorker::check_status()
{
  int ret = OB_SUCCESS;
  if (nullptr != session_) {
    session_->is_terminate(ret);
  }

  if (OB_SUCC(ret)) {
    if (is_timeout()) {
      ret = OB_TIMEOUT;
    } else if (IS_INTERRUPTED()) {
      ObInterruptCode &ic = GET_INTERRUPT_CODE();
      ret = ic.code_;
      LOG_WARN("px execution was interrupted", K(ic), K(ret));
    }
  }
  return ret;
}
