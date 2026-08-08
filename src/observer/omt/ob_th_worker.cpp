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

#define USING_LOG_PREFIX SERVER_OMT

#include "ob_th_worker.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_server_runtime.h"
#include "observer/ob_server.h"
#include "storage/memtable/ob_lock_wait_mgr.h"
#include "sql/executor/ob_memory_tracker.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "lib/thread/threads.h"
#include "share/interrupt/ob_global_interrupt_call.h"

using namespace oceanbase;
using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::omt;
using namespace oceanbase::rpc;
using namespace oceanbase::rpc::frame;

namespace oceanbase
{

namespace omt
{
int create_worker(ObThWorker* &worker, ObServerRuntime *runtime)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(worker = OB_NEW(ObThWorker,
                                       ObMemAttr("OMT_Worker",
                                       ObCtxIds::DEFAULT_CTX_ID, OB_NORMAL_ALLOC)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create worker fail", K(ret), K(runtime->id()));
  } else if (OB_FAIL(worker->init())) {
    LOG_ERROR("init worker fail", K(ret), K(runtime->id()));
    ob_delete(worker);
    worker = nullptr;
  } else {
    worker->reset();
    worker->set_runtime(runtime);
    worker->set_worker_level(0);
    worker->set_group(nullptr);
    if (OB_FAIL(worker->start())) {
      ob_delete(worker);
      worker = nullptr;
      LOG_ERROR("worker start failed", K(ret), K(runtime->id()));
    }
  }
  return ret;
}

int destroy_worker(ObThWorker *worker)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(worker)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(worker), K(ret));
  } else {
    worker->stop();
    worker->wait();
    worker->destroy();
    ob_delete(worker);
  }
  return ret;
}
}// end of namespace omt
}// end of namespace oceanbase

ObThWorker::ObThWorker()
    : procor_(ObServer::get_instance().get_net_frame().get_xlator(), ObServer::get_instance().get_self()),
      is_inited_(false), runtime_(nullptr),
      run_cond_(),
      pause_flag_(false),
      query_start_time_(0), query_enqueue_time_(0), last_check_time_(0),
      can_retry_(true), need_retry_(false),
      idle_us_(0), is_doing_ddl_(nullptr)
{
  module_name_[0] = '\0';
}

ObThWorker::~ObThWorker()
{
}

int ObThWorker::init()
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("the ObThWorker has been inited, ", K(ret));
  } else if (OB_FAIL(run_cond_.init(ObWaitEventIds::TH_WORKER_COND_WAIT))) {
  } else {
    set_is_th_worker(true);
    is_inited_ = true;
  }

  return ret;
}

void ObThWorker::destroy()
{
  if (is_inited_) {
    run_cond_.destroy();
    is_inited_ = false;
  }
}

// by other thread
void ObThWorker::resume()
{
  ObThreadCondGuard guard(run_cond_);
  pause_flag_ = false;
  run_cond_.signal();
}


thread_local bool ObThWorker::thread_name_set_ = false;

// by self thread
ObThWorker::Status ObThWorker::check_wait()
{
  Status st = WS_NOWAIT;
  if (OB_ISNULL(runtime_) || OB_UNLIKELY(runtime_->has_stopped())) {
    st = WS_INVALID;
  } else if (OB_UNLIKELY(true == get_disable_wait_flag())) {
  }
  return st;
}

inline void ObThWorker::process_request(rpc::ObRequest &req)
{
  // reset retry flags
  can_retry_ = true;
  need_retry_ = false;

  bool need_wait_lock = false;
  int ret = OB_SUCCESS;
  set_req_flag(&req);

  ::oceanbase::share::server_service<::oceanbase::memtable::ObLockWaitMgr>()->setup(req.get_lock_wait_node(), req.get_receive_timestamp());
  memtable::advance_tlocal_request_lock_wait_stat(rpc::RequestLockWaitStat::RequestStat::EXECUTE);
  if (OB_FAIL(procor_.process(req))) {
  }
  bool wait_succ = ::oceanbase::share::server_service<::oceanbase::memtable::ObLockWaitMgr>()->post_process(need_retry_, need_wait_lock);
  if (OB_LIKELY(wait_succ)) {
    need_retry_ = false;
  }
  // need_retry_ can be set in procor_.process() via THIS_WORKER.set_need_retry()
  if (OB_UNLIKELY(need_retry_)) {
    int32_t retry_times = req.get_retry_times();
    req.set_retry_times(retry_times + 1);
    if (need_wait_lock) {
      if (!wait_succ) {
        if (OB_FAIL(runtime_->recv_request(req))) {
        }
      }
    } else if (retry_times) {
      if (1 == retry_times) {
        LOG_WARN("runtime push retry request to wait queue", "runtime", runtime_->id(), K(req));
      }
      uint64_t curr_timestamp = common::ObClockGenerator::getClock();
      uint64_t delta_us = curr_timestamp - req.get_receive_timestamp();
      uint64_t timestamp = curr_timestamp + min(delta_us, 100 * 1000UL);
      if (OB_FAIL(runtime_->push_retry_queue(req, timestamp))) {
      }
    } else {
      // first retry, do not put the req to retry_queue
      if (OB_FAIL(runtime_->recv_request(req))) {
      }
    }

    if (OB_FAIL(ret)) {
      can_retry_ = false;
      need_retry_ = false;
      if (OB_FAIL(procor_.process(req))) {
      }
    }
  }

  set_req_flag(NULL);
}

void ObThWorker::set_th_worker_thread_name()
{
  if (!thread_name_set_) {
    thread_name_set_ = true;
    lib::set_thread_name("ReqWorker");
  }
}

void ObThWorker::worker(int64_t &tid, int64_t &req_recv_timestamp, int32_t &worker_level)
{
  int ret = OB_SUCCESS;
  Worker::set_worker_to_thread_local(static_cast<lib::Worker*>(this));
  int64_t wait_start_time = 0;
  int64_t wait_end_time = 0;
  procor_.th_created();
  is_doing_ddl_ = &Thread::is_doing_ddl_;
  static constexpr int64_t POLL_INTERVAL = 100 * 1000L;
  // Avoid adding and deleting entities from the root node for every request, the parameters are meaningless
  CREATE_WITH_TEMP_ENTITY(RESOURCE_OWNER, OB_SERVER_RUNTIME_ID) {
    auto *pm = common::ObPageManager::thread_local_instance();
    snprintf(module_name_, MAX_MODULE_NAME_LEN, "ReqWorker");
    int64_t idle_since = 0;
    while (!has_set_stop()) {
      worker_level = get_worker_level();
      if (OB_NOT_NULL(runtime_)) {
        tid = runtime_->id();
      }
      if (OB_NOT_NULL(pm)) {
        if (pm->get_used() != 0) {
          LOG_ERROR("page manager's used should be 0, unexpected!!!", KP(pm));
        } else {
          // Ignore the above warning
          ret = pm->set_ctx(ObCtxIds::DEFAULT_CTX_ID);
        }
      }
      CLEAR_INTERRUPTABLE();
      set_th_worker_thread_name();
      lib::ContextTLOptGuard guard(true);
      lib::ContextParam param;
      param.set_mem_attr(ObModIds::OB_SQL_EXECUTOR, ObCtxIds::DEFAULT_CTX_ID)
        .set_page_size(OB_MALLOC_REQ_NORMAL_BLOCK_SIZE)
        .set_properties(lib::USE_TL_PAGE_OPTIONAL)
        .set_ablock_size(lib::INTACT_MIDDLE_AOBJECT_SIZE);
      CREATE_WITH_TEMP_CONTEXT(param) {
        MEM_TRACKER_GUARD(CURRENT_CONTEXT);
        const uint64_t owner_id = runtime_->id();
        CREATE_WITH_TEMP_ENTITY(RESOURCE_OWNER, owner_id) {
          class AllocatorGuard {
          public:
            AllocatorGuard(ObIAllocator **allocator)
              : allocator_(allocator)
            {
              *allocator_ = &CURRENT_CONTEXT->get_arena_allocator();
            }
            ~AllocatorGuard()
            {
              *allocator_ = nullptr;
            }
          private:
            ObIAllocator **allocator_;
          } allocator_guard(&allocator_);
          rpc::ObRequest *req = NULL;
          bool expand = false;
          {
            // get request from queue and process it
            wait_start_time = ObTimeUtility::current_time();
            ret = runtime_->pop_with_idle([&]() {
              return runtime_->get_new_request(POLL_INTERVAL, req);
            }, expand);
            wait_end_time = ObTimeUtility::current_time();
          }
          if (OB_SUCC(ret)) {
            if (OB_NOT_NULL(req)) {
              idle_since = 0;
              if (expand) {
                runtime_->try_expand_one(runtime_->min_worker_cnt());
              }
              EVENT_INC(REQUEST_DEQUEUE_COUNT);
              req_recv_timestamp = req->get_receive_timestamp();
              EVENT_ADD(REQUEST_QUEUE_TIME, wait_end_time - req->get_enqueue_timestamp());
              req->set_push_pop_diff(wait_end_time);
              query_start_time_ = wait_end_time;
              query_enqueue_time_ = req->get_enqueue_timestamp();
              last_check_time_ = wait_end_time;
              process_request(*req);
              runtime_->completion_cnt_.fetch_add(1, std::memory_order_relaxed);
              query_enqueue_time_ = INT64_MAX;
              query_start_time_ = INT64_MAX;
            } else {
                ret = OB_ERR_UNEXPECTED;
                LOG_ERROR(
                    "got NULL request from runtime",
                    K(runtime_), K(ret), K(req));
              }
            } else if (OB_ENTRY_NOT_EXIST == ret) {
              if (idle_since == 0) {
                idle_since = wait_end_time;
              } else if (wait_end_time - idle_since >= ObServerRuntime::KEEP_ALIVE_TIMEOUT) {
                if (runtime_->try_shrink_one(0)) {
                  stop();
                  break;
                }
                idle_since = 0;
              }
              ret = OB_SUCCESS;
            }
            IGNORE_RETURN ATOMIC_FAA(&idle_us_, (wait_end_time - wait_start_time));
          }
        }
      }
    }
  procor_.th_destroy();
}

void ObThWorker::run(int64_t idx)
{
  UNUSED(idx);
  // The information that needs to be printed in the backtrace is placed in the parameter
  int64_t tid = -1;
  int64_t req_recv_timestamp = -1;
  int32_t worker_level = -1;
  this->worker(tid, req_recv_timestamp, worker_level);
}

int ObThWorker::check_status()
{
  int ret = OB_SUCCESS;
  if (nullptr != session_) {
    session_->is_terminate(ret);
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY((OB_SUCCESS != (ret = CHECK_MEM_STATUS())))) {
    } else if (is_timeout()) {
      ret = OB_TIMEOUT;
    } else if (IS_INTERRUPTED()) {
      ObInterruptCode &ic = GET_INTERRUPT_CODE();
      ret = ic.code_;
      LOG_WARN("received a interrupt", K(ic), K(ret));
    }
  }
  return ret;
}
