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

#define USING_LOG_PREFIX RS

#include "ob_dbms_job_master.h"
#include "ob_dbms_job_executor.h"
#include "lib/ob_running_mode.h"
#include "share/ob_ex_rpc.h"
#include "share/ob_internal_table_change_notifier.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/rc/ob_module_provider.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace oceanbase
{
using namespace common;
using namespace lib;
using namespace share;
using namespace share::schema;
using namespace rootserver;
using namespace obutil;
using namespace obcall;

namespace dbms_job
{

int ObDBMSJobTask::init(
    ObDBMSJobQueue *ready_queue,
    ObDBMSJobMaster *owner)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(inited_));
  } else if (OB_ISNULL(ready_queue) || OB_ISNULL(owner)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(ready_queue), KP(owner));
  } else if (OB_FAIL(timer_.init())) {
    LOG_WARN("fail to init timer", K(ret));
  } else {
    ready_queue_ = ready_queue;
    owner_ = owner;
    inited_ = true;
  }
  return ret;
}

int ObDBMSJobTask::start()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job task not inited", K(ret), K(inited_));
  }
  OZ (timer_.start());
  return ret;
}

int ObDBMSJobTask::stop()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job task not inited", K(ret), K(inited_));
  } else {
    timer_.cancel(*this);
    timer_.stop();
    timer_.wait();
    ObSpinLockGuard guard(lock_);
    wait_vector_.clear();
    job_key_ = NULL;
    ready_queue_ = NULL;
  }
  return ret;
}

int ObDBMSJobTask::destroy()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("scheduler task not inited", K(ret), K(inited_));
  } else {
    timer_.destroy();
  }
  return ret;
}

void ObDBMSJobTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  bool need_notify = false;
  {
    ObSpinLockGuard guard(lock_);
    if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("dbms job task not init", K(ret), K(inited_));
    } else if (OB_ISNULL(job_key_)
            || OB_ISNULL(ready_queue_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null ptr", K(ret), K(job_key_), K(ready_queue_));
    } else if (OB_FAIL(ready_queue_->push(job_key_, 0))) {
      LOG_WARN("fail to push ready job to queue", K(ret), K(*job_key_));
    } else {
      need_notify = true;
      job_key_ = NULL;
      if (wait_vector_.count() > 0) {
        job_key_ = wait_vector_[0];
        if (OB_FAIL(wait_vector_.remove(wait_vector_.begin()))) {
          job_key_ = NULL;
          LOG_WARN("fail to remove job_id from sorted vector", K(ret));
        } else if (OB_ISNULL(job_key_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL ptr", K(ret), K(job_key_));
        } else if (OB_FAIL(timer_.schedule(*this, job_key_->get_adjust_delay()))) {
          LOG_WARN("fail to schedule task", K(ret), K(*job_key_));
        }
      }
    }
    LOG_DEBUG("JobKEYS INFO HEADER ==== ", KPC(job_key_), K(wait_vector_.count()));
    int i = 0;
    for (WaitVectorIterator iter = wait_vector_.begin();
            OB_SUCC(ret) && iter != wait_vector_.end(); ++iter, ++i) {
      ObDBMSJobKey *job = *iter;
      LOG_DEBUG("JobKEYS INFO ELEMENT ====", K(i), KPC(job));
    }
  }
  if (need_notify && OB_NOT_NULL(owner_)) {
    const int tmp_ret = owner_->notify_background_source_();
    if (OB_SUCCESS != tmp_ret && OB_NOT_RUNNING != tmp_ret) {
      LOG_WARN("failed to notify dbms job source", K(tmp_ret));
    }
  }
  return;
}

int ObDBMSJobTask::scheduler(ObDBMSJobKey *job_key)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job task not init", K(ret));
  } else if (OB_ISNULL(job_key)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr for job id", K(ret), KPC(job_key));
  } else if (!job_key->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("job id is invalid", K(ret), KPC(job_key));
  } else if (0 == job_key->get_delay()) {
    OZ (immediately(job_key), KPC(job_key));
  } else {
    OZ (add_new_job(job_key), KPC(job_key));
  }

  return ret;
}

int ObDBMSJobTask::add_new_job(ObDBMSJobKey *new_job_key)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job not init", K(ret));
  } else if (OB_ISNULL(new_job_key)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), KPC(new_job_key));
  } else {
    ObSpinLockGuard guard(lock_);
    if (OB_ISNULL(job_key_)) {
      job_key_ = new_job_key;
      OZ (timer_.schedule(*this, job_key_->get_delay()));
    } else if (new_job_key->get_execute_at() >= job_key_->get_execute_at()) {
      WaitVectorIterator iter;
      ObDBMSJobKey *replace_job_key = NULL;
      OZ (wait_vector_.replace(new_job_key, iter, compare_job_key, equal_job_key, replace_job_key));
    } else {
      WaitVectorIterator iter;
      OX (timer_.cancel(*this));
      OZ (wait_vector_.insert(job_key_, iter, compare_job_key));
      OX (job_key_ = new_job_key);
      OZ (timer_.schedule(*this, job_key_->get_delay()));
    }
  }
  return ret;
}

int ObDBMSJobTask::immediately(ObDBMSJobKey *job_key)
{
  int ret = OB_SUCCESS;
  bool need_notify = false;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job not init", K(ret), K(inited_));
  } else if (OB_ISNULL(job_key) || OB_ISNULL(ready_queue_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(job_key), K(ready_queue_));
  } else {
    {
      ObSpinLockGuard guard(lock_);
      if (OB_FAIL(ready_queue_->push(job_key, 0))) {
        LOG_WARN("fail to push ready job to queue", K(ret), K(*job_key));
      } else {
        need_notify = true;
      }
    }
    if (need_notify && OB_NOT_NULL(owner_)) {
      const int tmp_ret = owner_->notify_background_source_();
      if (OB_SUCCESS != tmp_ret && OB_NOT_RUNNING != tmp_ret) {
        LOG_WARN("failed to notify dbms job source", K(tmp_ret));
      }
    }
  }
  return ret;
}

int ObDBMSJobTask::remove_job(
    const uint64_t job_id,
    ObDBMSJobKey *&job_key)
{
  int ret = OB_SUCCESS;
  job_key = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job task not initialized", K(ret));
  } else {
    ObSpinLockGuard guard(lock_);
    if (OB_NOT_NULL(job_key_)
        && !job_key_->is_check_new()
        && job_id == job_key_->get_job_id()) {
      (void)timer_.cancel(*this);
      job_key = job_key_;
      job_key_ = NULL;
      if (wait_vector_.count() > 0) {
        job_key_ = wait_vector_.at(0);
        if (OB_FAIL(wait_vector_.remove(wait_vector_.begin()))) {
          LOG_WARN("failed to remove next dbms job", K(ret), KPC(job_key_));
          job_key_ = NULL;
        } else if (OB_FAIL(timer_.schedule(*this, job_key_->get_adjust_delay()))) {
          LOG_WARN("failed to schedule next dbms job", K(ret), KPC(job_key_));
        }
      }
    } else {
      for (WaitVectorIterator iter = wait_vector_.begin();
           NULL == job_key && iter != wait_vector_.end(); ++iter) {
        ObDBMSJobKey *candidate = *iter;
        if (OB_NOT_NULL(candidate)
            && !candidate->is_check_new()
            && job_id == candidate->get_job_id()) {
          job_key = candidate;
          if (OB_FAIL(wait_vector_.remove(iter))) {
            LOG_WARN("failed to remove dbms job from wait vector",
                K(ret), K(job_id));
            job_key = NULL;
          }
        }
      }
    }
  }
  return ret;
}

bool ObDBMSJobTask::compare_job_key(const ObDBMSJobKey *lhs, const ObDBMSJobKey *rhs)
{
  return lhs->get_execute_at() < rhs->get_execute_at()
    || (lhs->get_execute_at() == rhs->get_execute_at() && lhs->get_job_id() < rhs->get_job_id());
}

bool ObDBMSJobTask::equal_job_key(const ObDBMSJobKey *lhs, const ObDBMSJobKey *rhs)
{
  return lhs->get_job_id() == rhs->get_job_id() && lhs->get_execute_at() == rhs->get_execute_at();
}

void ObDBMSJobThread::handle(void *task)
{
  int ret = OB_SUCCESS;
  ObDBMSJobMaster *master = NULL;
  if (OB_ISNULL(task)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null ptr", K(ret), K(task));
  } else if (FALSE_IT(master = static_cast<ObDBMSJobMaster *>(task))) {
  } else if (OB_FAIL(master->scheduler())) {
    LOG_ERROR("fail to run dbms job master", K(ret));
  }
  return;
}

ObDBMSJobMaster &ObDBMSJobMaster::get_instance()
{
  static ObDBMSJobMaster master_;
  return master_;
}

int ObDBMSJobMaster::init(ObISQLClient *sql_client,
                          ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("dbms job master already inited", K(ret), K(inited_));
  } else if (OB_ISNULL(sql_client)
          || OB_ISNULL(schema_service)
          ) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret), K(sql_client), K(schema_service));
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (FALSE_IT(ready_queue_.set_limit(MAX_READY_JOBS_CAPACITY))) {
    // do-nothing
  } else if (OB_FAIL(scheduler_task_.init(&ready_queue_, this))) {
    LOG_WARN("fail to init ready queue", K(ret));
  } else if (!use_shared_executor_
      && OB_FAIL(scheduler_thread_.init(1, 1))) {
    LOG_WARN("fail to init scheduler pool", K(ret));
  } else if (OB_FAIL(job_utils_.init(sql_client))) {
    LOG_WARN("fail to init action record", K(ret));
  } else if (OB_FAIL(alive_jobs_.create(1024))) {
    LOG_WARN("failed to create job hash set", K(ret));
  } else if (OB_FAIL(ObInternalTableChangeNotifier::get_instance().register_table(
                 OB_ALL_JOB_TID))) {
    LOG_WARN("failed to register dbms job table change tracking", K(ret));
  } else if (OB_ISNULL(ObCurTraceId::get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("trace id is null", K(ret));
  } else {
    trace_id_ = ObCurTraceId::get();
    inited_ = true;
  }
  LOG_INFO("dbms job master inited!", K(ret));
  return ret;
}

int ObDBMSJobMaster::start()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init yet!", K(ret), K(inited_));
  } else if (ATOMIC_LOAD(&running_)) {
    // alreay running , do nothing ...
  } else if (FALSE_IT(stoped_ = false)) {
  } else if (use_shared_executor_
      && OB_FAIL(register_background_source_())) {
    LOG_WARN("fail to register dbms job source", K(ret));
  } else if (!use_shared_executor_
      && OB_FAIL(scheduler_thread_.push(static_cast<void *>(this)))) {
    LOG_WARN("fail to start scheduler thread", K(ret));
  } else if (OB_FAIL(scheduler_task_.start())) {
    LOG_WARN("fail to start ready queue", K(ret));
  } else if (OB_FAIL(check_table_change_(NULL))) {
    LOG_WARN("fail to load all dbms jobs", K(ret));
  } else if (use_shared_executor_) {
    ATOMIC_STORE(&running_, true);
    if (ready_queue_.size() > 0
        && OB_FAIL(notify_background_source_())) {
      LOG_WARN("fail to notify pending dbms jobs", K(ret));
    }
  }
  if (OB_FAIL(ret) && use_shared_executor_) {
    ATOMIC_STORE(&stoped_, true);
    (void)scheduler_task_.stop();
    (void)unregister_background_source_(true);
    ATOMIC_STORE(&running_, false);
  }
  LOG_WARN("dbms job master started", K(ret));
  return ret;
}

int ObDBMSJobMaster::stop()
{
  int ret = OB_SUCCESS;
  ATOMIC_STORE(&stoped_, true);
  if (use_shared_executor_) {
    const int tmp_ret = unregister_background_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("failed to unregister dbms job source", K(tmp_ret));
      ret = tmp_ret;
    }
    scheduler_task_.stop();
    ATOMIC_STORE(&running_, false);
  } else {
    scheduler_task_.stop();
  }
  while (!use_shared_executor_ && ATOMIC_LOAD(&running_)) {
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
  }
  ATOMIC_STORE(&stoped_, false);
  LOG_INFO("dbms job master stoped", K(ret), K(lbt()));
  return ret;
}

int ObDBMSJobMaster::scheduler()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init yet", K(ret));
  } else if (OB_ISNULL(trace_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null trace_id_ ptr", K(ret), K(trace_id_));
  } else {
    ObCurTraceId::set(trace_id_);
    ATOMIC_STORE(&running_, true);
    LOG_INFO("NOTICE: DBMS Job master start running!", K(ret), K(running_));
    lib::set_thread_name("DBMS_JOB_MASTER");
    while (OB_SUCC(ret) && !ATOMIC_LOAD(&stoped_)) {
      ObLink* ptr = NULL;
      int64_t timeout = MIN_SCHEDULER_INTERVAL;
      ObDBMSJobKey *job_key = NULL;
      if (OB_FAIL(ready_queue_.pop(ptr, timeout))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          LOG_INFO("dbms job master wait timeout, no entry", K(ret));
          ret = OB_SUCCESS;
        } else {
          LOG_ERROR("fail to pop dbms job ready queue", K(ret), K(timeout));
        }
      } else if (OB_ISNULL(job_key = static_cast<ObDBMSJobKey *>(ptr)) || !job_key->is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected error, invalid job key found in ready queue!", K(ret), KPC(job_key));
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = scheduler_job(job_key))) {
          LOG_WARN("fail to scheduler single dbms job", K(ret), K(tmp_ret), KPC(job_key));
        } else {
          LOG_INFO("success to scheduler single dbms job", K(ret), K(tmp_ret), KPC(job_key));
        }
      }
    }
    LOG_INFO("NOTICE: DBMS Job master end running!", K(ret), K(running_));
    ATOMIC_STORE(&running_, false);
  }
  return ret;
}

int ObDBMSJobMaster::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  const int64_t saved_worker_timeout_ts = THIS_WORKER.get_timeout_ts();
  ObCurTraceId::TraceId saved_trace_id = *ObCurTraceId::get_trace_id();
  if (!use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (share::BG_TASK_NORMAL != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ATOMIC_LOAD(&stoped_)) {
  } else if (OB_ISNULL(trace_id_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObCurTraceId::set(trace_id_);
    ObLink *ptr = NULL;
    const int pop_ret = ready_queue_.pop(ptr, 0);
    if (OB_ENTRY_NOT_EXIST == pop_ret) {
    } else if (OB_SUCCESS != pop_ret) {
      ret = pop_ret;
      LOG_WARN("failed to pop dbms job ready queue", K(ret));
    } else {
      ObDBMSJobKey *job_key = static_cast<ObDBMSJobKey *>(ptr);
      if (OB_ISNULL(job_key) || !job_key->is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected invalid dbms job key", K(ret), KPC(job_key));
      } else {
        const int tmp_ret = scheduler_job(job_key);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("failed to schedule dbms job", K(tmp_ret), KPC(job_key));
        }
        result.processed_count_ = 1;
        result.has_more_ready_ = ready_queue_.size() > 0;
      }
    }
  }
  ObCurTraceId::set(saved_trace_id);
  THIS_WORKER.set_timeout_ts(saved_worker_timeout_ts);
  return ret;
}

int ObDBMSJobMaster::scheduler_job(ObDBMSJobKey *job_key, bool is_retry)
{
  int ret = OB_SUCCESS;

  ObDBMSJobInfo job_info;

  UNUSED(is_retry);

  CK (OB_LIKELY(inited_));
  CK (OB_NOT_NULL(job_key));
  CK (OB_LIKELY(job_key->is_valid()));

  if (OB_FAIL(ret)) {
  } else if (job_key->is_check_new()) {
    OZ (check_table_change_(job_key));
  } else {
    ObArenaAllocator allocator;
    OZ (job_utils_.get_dbms_job_info(
      job_key->get_job_id(), allocator, job_info));

    if (OB_FAIL(ret)) {
    } else if (job_info.valid()) {
      bool ignore_nextdate = false;
      if (!job_key->is_check() && !job_info.is_running() && !job_info.is_broken()) {
        bool can_running = false;
        OZ (job_utils_.check_job_can_running(can_running));
        if (OB_SUCC(ret) && can_running) {
          OZ (job_utils_.update_for_start(
            job_info,
            (job_info.next_date_ == job_key->get_execute_at())));
          const uint64_t run_job_id = job_key->get_job_id();
          ex_rpc::async_call([run_job_id]() {
            ObDBMSJobExecutor executor;
            if (OB_NOT_NULL(GCTX.sql_proxy_) && OB_NOT_NULL(GCTX.schema_service_)
                && OB_SUCCESS == executor.init(GCTX.sql_proxy_, GCTX.schema_service_)) {
              (void)executor.run_dbms_job(run_job_id);
            }
          });
        }
        ignore_nextdate = true;
      }
      int tmp_ret = OB_SUCCESS;
      // always add job to queue. we need this to check job status changes.
      if (OB_SUCCESS != (tmp_ret = register_job(job_info, job_key, ignore_nextdate))) {
        LOG_WARN("failed to register job to job queue", K(tmp_ret));
      }
    } else {
      int tmp = alive_jobs_.erase_refactored(job_key->get_job_id());
      if (tmp != OB_SUCCESS) {
        LOG_INFO("failed delete valid job from hash set", K(ret), K(job_info));
      }
      allocator_.free(job_key); // job deleted!
    }
    LOG_DEBUG("scheduler A real JOB!", K(ret), KPC(job_key));
  }
  return ret;
}

int ObDBMSJobMaster::destroy()
{
  (void)unregister_background_source_(true);
  ready_queue_.destroy();
  scheduler_task_.destroy();
  if (!use_shared_executor_) {
    scheduler_thread_.destroy();
  }
  allocator_.clear();
  background_executor_ = NULL;
  source_handle_.reset();
  inited_ = false;
  return OB_SUCCESS;
}

int ObDBMSJobMaster::register_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_ || source_handle_.is_valid()) {
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(background_executor_ =
          share::g_mp->background_task_executor())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("background task executor is null",
        K(ret), KP(share::g_mp), KP(background_executor_));
  } else {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "DBMS_JOB_MASTER";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("failed to register dbms job source", K(ret));
    }
  }
  return ret;
}

int ObDBMSJobMaster::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  if (use_shared_executor_ && OB_NOT_NULL(background_executor_)
      && source_handle_.is_valid()) {
    do {
      ret = background_executor_->unregister_source(source_handle_);
      if (wait_running && OB_EAGAIN == ret) {
        ob_usleep(1000);
      }
    } while (wait_running && OB_EAGAIN == ret);
    if (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret) {
      source_handle_.reset();
      ret = OB_SUCCESS;
    }
  }
  if (!source_handle_.is_valid()) {
    background_executor_ = NULL;
  }
  return ret;
}

int ObDBMSJobMaster::notify_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_) {
  } else if (OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()
      || !ATOMIC_LOAD(&running_)) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_NORMAL))) {
    LOG_WARN("failed to notify dbms job source", K(ret));
  }
  return ret;
}

int ObDBMSJobMaster::alloc_job_key(
  ObDBMSJobKey *&job_key, uint64_t job_id,
  uint64_t execute_at, uint64_t delay,
  bool check_job, bool check_new)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  void *ptr = NULL;
  job_key = NULL;
  if (OB_ISNULL(ptr = allocator_.alloc(sizeof(ObDBMSJobKey)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret), K(ptr));
  } else if (OB_ISNULL(job_key =
    new(ptr)ObDBMSJobKey(job_id,
                         execute_at, delay,
                         check_job, check_new))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to init scheduler job id", K(ret));
  }
  return ret;
}

int ObDBMSJobMaster::load_and_register_new_jobs(ObDBMSJobKey *job_key)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObDBMSJobInfo, 32> job_infos;
  ObArenaAllocator allocator;
  OZ (job_utils_.get_dbms_job_infos_in_runtime(allocator, job_infos));
  LOG_INFO("load and register new jobs", K(ret), KPC(job_key), K(job_key), K(job_infos));
  OZ (register_jobs(job_infos, job_key));
  return ret;
}

int ObDBMSJobMaster::check_table_change_(ObDBMSJobKey *check_key)
{
  int ret = OB_SUCCESS;
  uint64_t target_seq = 0;
  const int seq_ret =
      ObInternalTableChangeNotifier::get_instance().get_change_seq(
          OB_ALL_JOB_TID, target_seq);
  if (OB_SUCCESS != seq_ret) {
    // Preserve correctness if lifecycle wiring is broken. Do not advance the
    // local sequence, so the next control tick retries both detection and read.
    LOG_WARN("failed to get dbms job table change sequence",
        K(seq_ret), K(job_table_change_seq_));
    if (OB_FAIL(load_and_register_new_jobs(check_key))) {
      LOG_WARN("failed to reconcile dbms jobs after sequence lookup failure", K(ret));
      const int tmp_ret = schedule_change_check_(check_key);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("failed to reschedule dbms job change check", K(tmp_ret));
      }
    }
  } else if (target_seq == job_table_change_seq_) {
    if (OB_FAIL(schedule_change_check_(check_key))) {
      LOG_WARN("failed to schedule dbms job change check", K(ret));
    }
  } else if (OB_FAIL(load_and_register_new_jobs(check_key))) {
    LOG_WARN("failed to reconcile changed dbms job table", K(ret), K(target_seq));
    const int tmp_ret = schedule_change_check_(check_key);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("failed to reschedule dbms job change check", K(tmp_ret));
    }
  } else {
    // Use the value captured before the table read. A concurrent commit leaves
    // a newer value for the next control tick.
    job_table_change_seq_ = target_seq;
  }
  return ret;
}

int ObDBMSJobMaster::schedule_change_check_(ObDBMSJobKey *check_key)
{
  int ret = OB_SUCCESS;
  const int64_t now = ObTimeUtility::current_time();
  if (OB_ISNULL(check_key)) {
    if (OB_FAIL(alloc_job_key(
            check_key,
            0,
            now + MIN_SCHEDULER_INTERVAL,
            MIN_SCHEDULER_INTERVAL,
            false,
            true))) {
      LOG_WARN("failed to allocate dbms job change check key", K(ret));
    }
  } else if (OB_UNLIKELY(!check_key->is_check_new())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dbms job change check key", K(ret), KPC(check_key));
  } else {
    check_key->set_execute_at(now + MIN_SCHEDULER_INTERVAL);
    check_key->set_delay(MIN_SCHEDULER_INTERVAL);
  }
  if (OB_SUCC(ret) && OB_FAIL(scheduler_task_.scheduler(check_key))) {
    LOG_WARN("failed to schedule dbms job table change check", K(ret));
  }
  return ret;
}

int ObDBMSJobMaster::register_jobs(
  ObIArray<ObDBMSJobInfo> &job_infos, ObDBMSJobKey *job_key)
{
  int ret = OB_SUCCESS;
  ObDBMSJobInfo job_info;
  ObSEArray<uint64_t, 16> deleted_job_ids;

  for (int64_t i = 0; OB_SUCC(ret) && i < job_infos.count(); i++) {
    job_info = job_infos.at(i);
    if (job_info.valid()) {
      ObDBMSJobKey *existing_key = NULL;
      int tmp = alive_jobs_.exist_refactored(job_info.get_job_id());
      if (OB_HASH_EXIST == tmp) {
        if (OB_FAIL(scheduler_task_.remove_job(
                job_info.get_job_id(), existing_key))) {
          LOG_WARN("failed to remove existing dbms job before reconciliation",
              K(ret), K(job_info));
        }
      } else if (OB_HASH_NOT_EXIST == tmp) {
        if (OB_FAIL(alive_jobs_.set_refactored(job_info.get_job_id()))) {
          LOG_WARN("failed to add dbms job to alive set", K(ret), K(job_info));
        }
      } else {
        ret = tmp;
        LOG_ERROR("dbms job master check job exist failed", K(ret), K(job_info));
      }
      if (OB_SUCC(ret)) {
        if (job_info.is_broken()) {
          if (OB_NOT_NULL(existing_key)) {
            allocator_.free(existing_key);
          }
        } else if (OB_FAIL(register_job(job_info, existing_key))) {
          LOG_WARN("failed to register reconciled dbms job", K(ret), K(job_info));
        }
      }
    }
  }

  for (common::hash::ObHashSet<uint64_t>::iterator iter = alive_jobs_.begin();
       OB_SUCC(ret) && iter != alive_jobs_.end(); ++iter) {
    bool found = false;
    for (int64_t i = 0; !found && i < job_infos.count(); ++i) {
      found = job_infos.at(i).valid()
          && iter->first == job_infos.at(i).get_job_id();
    }
    if (!found && OB_FAIL(deleted_job_ids.push_back(iter->first))) {
      LOG_WARN("failed to collect deleted dbms job", K(ret), K(iter->first));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < deleted_job_ids.count(); ++i) {
    ObDBMSJobKey *deleted_key = NULL;
    const uint64_t deleted_job_id = deleted_job_ids.at(i);
    if (OB_FAIL(scheduler_task_.remove_job(deleted_job_id, deleted_key))) {
      LOG_WARN("failed to remove deleted dbms job", K(ret), K(deleted_job_id));
    } else if (OB_FAIL(alive_jobs_.erase_refactored(deleted_job_id))) {
      LOG_WARN("failed to erase deleted dbms job from alive set",
          K(ret), K(deleted_job_id));
    } else if (OB_NOT_NULL(deleted_key)) {
      allocator_.free(deleted_key);
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(schedule_change_check_(job_key))) {
    LOG_WARN("failed to schedule next dbms job table change check", K(ret));
  }

  return ret;
}

int ObDBMSJobMaster::register_job(
  ObDBMSJobInfo &job_info, ObDBMSJobKey *job_key, bool ignore_nextdate)
{
  int ret = OB_SUCCESS;

  int64_t execute_at = -1;
  int64_t delay = -1;
  bool check_job = false;
  int64_t now = ObTimeUtility::current_time();

  CK (OB_LIKELY(inited_));
  CK (job_info.valid());
  if (OB_FAIL(ret)) {
  } else if (job_info.is_broken()) {
    // Broken jobs are re-enabled by table-change reconciliation, not polling.
    delay = -1;
  } else if (job_info.is_running()) {
    execute_at = now + MIN_SCHEDULER_INTERVAL;
    delay = MIN_SCHEDULER_INTERVAL; // every 5s check job status
    check_job = true;
  } else {
    OZ (job_utils_.calc_execute_at(job_info, execute_at, delay, ignore_nextdate));
    if (OB_FAIL(ret) || delay < 0) {
      ret = OB_SUCCESS;
      // No executable deadline. Metadata changes are detected by change_seq.
    }
  }
  if (OB_FAIL(ret)) {
  } else if (delay < 0) {
    if (OB_NOT_NULL(job_key)) {
      allocator_.free(job_key);
      job_key = NULL;
    }
  } else if (OB_ISNULL(job_key)) {
    OZ (alloc_job_key(
      job_key,
      job_info.get_job_id(),
      execute_at,
      delay,
      check_job,
      false));
    CK (OB_NOT_NULL(job_key));
    CK (job_key->is_valid());
  } else {
    CK (true);
    CK (job_key->get_job_id() == job_info.get_job_id());
    OX (job_key->set_execute_at(execute_at));
    OX (job_key->set_delay(delay));
    OX (job_key->set_check_job(check_job));
    OX (job_key->set_check_new(false));
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(job_key)) {
    OZ (scheduler_task_.scheduler(job_key));
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(job_key)) {
    allocator_.free(job_key);
  }
  LOG_INFO("register new dbms job", K(ret), K(job_info), KPC(job_key), K(job_key));

  return ret;
}

} // end for namespace dbms_job
} // end for namespace oceanbase
