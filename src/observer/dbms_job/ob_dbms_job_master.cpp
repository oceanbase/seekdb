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
#include "lib/atomic/ob_atomic.h"
#include "share/ob_ex_rpc.h"
#include "share/ob_internal_table_change_notifier.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
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
    bool *needs_reconcile)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(inited_));
  } else if (OB_ISNULL(ready_queue) || OB_ISNULL(needs_reconcile)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(ready_queue), K(needs_reconcile));
  } else if (OB_FAIL(timer_.init())) {
    LOG_WARN("fail to init timer", K(ret));
  } else {
    ready_queue_ = ready_queue;
    needs_reconcile_ = needs_reconcile;
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
  ObSpinLockGuard guard(lock_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job task not init", K(ret), K(inited_));
  } else if (reconfiguring_) {
    // A canceled timer token may already have been dispatched. The
    // reconciliation thread waits for this callback before freeing keys.
    LOG_DEBUG("skip dbms job timer callback during reconciliation");
  } else if (OB_ISNULL(job_key_)
          || OB_ISNULL(ready_queue_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret), K(job_key_), K(ready_queue_));
  } else if (OB_FAIL(ready_queue_->push(job_key_, 0))) {
    LOG_WARN("fail to push ready job to queue", K(ret), K(*job_key_));
    const int recover_ret = recover_unscheduled_head_();
    if (OB_SUCCESS == recover_ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to recover dbms job after ready queue error",
          K(recover_ret), KPC(job_key_));
    }
  } else {
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
        const int recover_ret = recover_unscheduled_head_();
        if (OB_SUCCESS != recover_ret) {
          ATOMIC_STORE(needs_reconcile_, true);
          LOG_WARN("fail to recover unscheduled dbms job head",
              K(recover_ret), KPC(job_key_));
        } else {
          ret = OB_SUCCESS;
        }
      }
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(needs_reconcile_)) {
    ATOMIC_STORE(needs_reconcile_, true);
  }
  LOG_DEBUG("JobKEYS INFO HEADER ==== ", KPC(job_key_), K(wait_vector_.count()));
  int i = 0;
  for (WaitVectorIterator iter = wait_vector_.begin();
          OB_SUCC(ret) && iter != wait_vector_.end(); ++iter, ++i) {
    ObDBMSJobKey *job = *iter;
    LOG_DEBUG("JobKEYS INFO ELEMENT ====", K(i), KPC(job));
  }
  return;
}

int ObDBMSJobTask::scheduler(
    ObDBMSJobKey *job_key,
    ObDBMSJobKey *&replaced_job_key)
{
  int ret = OB_SUCCESS;
  replaced_job_key = NULL;
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
    OZ (add_new_job(job_key, replaced_job_key), KPC(job_key));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(needs_reconcile_)) {
    ATOMIC_STORE(needs_reconcile_, true);
  }

  return ret;
}

int ObDBMSJobTask::add_new_job(
    ObDBMSJobKey *new_job_key,
    ObDBMSJobKey *&replaced_job_key)
{
  int ret = OB_SUCCESS;
  bool replace_head = false;
  replaced_job_key = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job not init", K(ret));
  } else if (OB_ISNULL(new_job_key)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), KPC(new_job_key));
  } else {
    {
      ObSpinLockGuard guard(lock_);
      if (reconfiguring_) {
        ret = OB_STATE_NOT_MATCH;
        LOG_WARN("dbms job task is being reconfigured", K(ret));
      } else if (OB_ISNULL(job_key_)) {
        job_key_ = new_job_key;
        if (OB_FAIL(timer_.schedule(*this, job_key_->get_delay()))) {
          job_key_ = NULL;
          LOG_WARN("fail to schedule task", K(ret), KPC(new_job_key));
        }
      } else if (new_job_key->get_execute_at() >= job_key_->get_execute_at()) {
        WaitVectorIterator iter;
        if (OB_FAIL(wait_vector_.replace(
                new_job_key, iter, compare_job_key, equal_job_key, replaced_job_key))) {
          LOG_WARN("fail to insert dbms job into wait vector", K(ret), KPC(new_job_key));
        }
      } else {
        // cancel() does not wait for an already dispatched callback. Mark the
        // task reconfiguring while holding lock_, then wait without lock_ so a
        // dispatched callback can observe the marker and return.
        reconfiguring_ = true;
        if (OB_FAIL(timer_.cancel(*this))) {
          reconfiguring_ = false;
          LOG_WARN("fail to cancel current dbms job timer", K(ret), KPC(job_key_));
        } else {
          replace_head = true;
        }
      }
    }
    if (OB_SUCC(ret) && replace_head) {
      if (OB_FAIL(timer_.wait_task(*this))) {
        LOG_WARN("fail to wait canceled dbms job timer", K(ret));
      }
      ObSpinLockGuard guard(lock_);
      if (OB_SUCC(ret)) {
        WaitVectorIterator iter;
        ObDBMSJobKey *old_head = job_key_;
        if (OB_FAIL(wait_vector_.insert(old_head, iter, compare_job_key))) {
          LOG_WARN("fail to preserve old dbms job head", K(ret), KPC(old_head));
          const int recover_ret = recover_unscheduled_head_();
          if (OB_SUCCESS != recover_ret) {
            LOG_WARN("fail to recover old dbms job head after insert error",
                K(recover_ret), KPC(old_head));
          }
        } else {
          job_key_ = new_job_key;
          if (OB_FAIL(timer_.schedule(*this, job_key_->get_delay()))) {
            LOG_WARN("fail to schedule replacement dbms job head", K(ret), KPC(job_key_));
            const int schedule_ret = ret;
            const int recover_ret = recover_unscheduled_head_();
            if (OB_SUCCESS == recover_ret) {
              // The retry owns new_job_key either as the timer head or as a
              // ready-queue entry. The old head remains in wait_vector_.
              ret = OB_SUCCESS;
            } else {
              // Return ownership of new_job_key to the caller. The recovery
              // flag makes the master's queue timeout drain the old vector.
              job_key_ = NULL;
              ret = schedule_ret;
              LOG_WARN("fail to recover replacement dbms job head",
                  K(recover_ret), KPC(new_job_key));
            }
          }
        }
      } else {
        const int recover_ret = recover_unscheduled_head_();
        if (OB_SUCCESS != recover_ret) {
          LOG_WARN("fail to recover dbms job head after wait error",
              K(recover_ret), KPC(job_key_));
        }
      }
      reconfiguring_ = false;
    }
  }
  return ret;
}

int ObDBMSJobTask::recover_unscheduled_head_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(job_key_) || OB_ISNULL(ready_queue_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot recover null dbms job head", K(ret), KPC(job_key_), K(ready_queue_));
  } else {
    const uint64_t adjusted_delay = job_key_->get_adjust_delay();
    const uint64_t recovery_delay = adjusted_delay < RECOVERY_INTERVAL
        ? RECOVERY_INTERVAL : adjusted_delay;
    if (OB_FAIL(timer_.schedule(*this, recovery_delay))) {
      const int schedule_ret = ret;
      const int push_ret = ready_queue_->push(job_key_, 0);
      if (OB_SUCCESS == push_ret) {
        LOG_WARN("hand off unscheduled dbms job head to ready queue",
            K(schedule_ret), KPC(job_key_));
        job_key_ = NULL;
        ret = OB_SUCCESS;
      } else {
        ret = push_ret;
        LOG_WARN("fail to hand off unscheduled dbms job head",
            K(ret), K(schedule_ret), KPC(job_key_));
      }
    }
  }
  return ret;
}

int ObDBMSJobTask::immediately(ObDBMSJobKey *job_key)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job not init", K(ret), K(inited_));
  } else if (OB_ISNULL(job_key) || OB_ISNULL(ready_queue_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret), K(job_key), K(ready_queue_));
  } else {
    ObSpinLockGuard guard(lock_);
    if (OB_FAIL(ready_queue_->push(job_key, 0))) {
      LOG_WARN("fail to push ready job to queue", K(ret), K(*job_key));
    }
  }
  return ret;
}

int ObDBMSJobTask::pause_and_wait()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms job task not initialized", K(ret));
  } else {
    {
      ObSpinLockGuard guard(lock_);
      reconfiguring_ = true;
      if (OB_FAIL(timer_.cancel(*this))) {
        LOG_WARN("fail to cancel dbms job timer for reconciliation", K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(timer_.wait_task(*this))) {
      LOG_WARN("fail to wait dbms job timer for reconciliation", K(ret));
    }
  }
  return ret;
}

int ObDBMSJobTask::pop_waiting_job(ObDBMSJobKey *&job_key)
{
  int ret = OB_SUCCESS;
  job_key = NULL;
  ObSpinLockGuard guard(lock_);
  if (!inited_) {
    ret = OB_NOT_INIT;
  } else if (!reconfiguring_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_NOT_NULL(job_key_)) {
    job_key = job_key_;
    job_key_ = NULL;
  } else if (wait_vector_.count() > 0) {
    job_key = wait_vector_.at(0);
    if (OB_FAIL(wait_vector_.remove(wait_vector_.begin()))) {
      job_key = NULL;
      LOG_WARN("fail to remove dbms job from wait vector", K(ret));
    }
  } else {
    ret = OB_ITER_END;
  }
  return ret;
}

void ObDBMSJobTask::resume()
{
  ObSpinLockGuard guard(lock_);
  reconfiguring_ = false;
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
  } else if (FALSE_IT(ready_queue_.set_limit(MAX_READY_JOBS_CAPACITY))) {
    // do-nothing
  } else if (OB_FAIL(scheduler_task_.init(&ready_queue_, &needs_reconcile_))) {
    LOG_WARN("fail to init ready queue", K(ret));
  } else if (OB_FAIL(scheduler_thread_.init(1, 1))) {
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
  } else if (running_) {
    // alreay running , do nothing ...
  } else if (OB_FAIL(scheduler_task_.start())) {
    LOG_WARN("fail to start ready queue", K(ret));
  } else {
    // Build the first generation before the consumer thread can pop an
    // immediate deadline stamped with that generation.
    bool handled = false;
    if (OB_FAIL(check_table_change_(NULL, handled))) {
      LOG_WARN("fail to load all dbms jobs", K(ret));
    } else if (OB_FAIL(scheduler_thread_.push(static_cast<void *>(this)))) {
      LOG_WARN("fail to start scheduler thread", K(ret));
    }
  }
  LOG_WARN("dbms job master started", K(ret));
  return ret;
}

int ObDBMSJobMaster::stop()
{
  int ret = OB_SUCCESS;
  scheduler_task_.stop();
  stoped_ = true;
  while (running_) {
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
  }
  stoped_ = false;
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
    running_ = true;
    LOG_INFO("NOTICE: DBMS Job master start running!", K(ret), K(running_));
    lib::set_thread_name("DBMS_JOB_MASTER");
    while (OB_SUCC(ret) && !stoped_) {
      ObLink* ptr = NULL;
      int64_t timeout = MIN_SCHEDULER_INTERVAL;
      ObDBMSJobKey *job_key = NULL;
      if (OB_FAIL(ready_queue_.pop(ptr, timeout))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          LOG_INFO("dbms job master wait timeout, no entry", K(ret));
          ret = OB_SUCCESS;
          if (ATOMIC_LOAD(&needs_reconcile_)) {
            bool handled = false;
            const int reconcile_ret = check_table_change_(NULL, handled);
            if (OB_SUCCESS != reconcile_ret) {
              LOG_WARN("fail to recover dbms job schedule after queue timeout",
                  K(reconcile_ret), K(handled));
            }
          }
        } else {
          LOG_ERROR("fail to pop dbms job ready queue", K(ret), K(timeout));
        }
      } else if (OB_ISNULL(job_key = static_cast<ObDBMSJobKey *>(ptr)) || !job_key->is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected error, invalid job key found in ready queue!", K(ret), KPC(job_key));
      } else {
        const uint64_t job_id = job_key->get_job_id();
        const uint64_t generation = job_key->get_generation();
        const int tmp_ret = scheduler_job(job_key);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("fail to scheduler single dbms job",
              K(ret), K(tmp_ret), K(job_id), K(generation));
        } else {
          LOG_INFO("success to scheduler single dbms job",
              K(ret), K(tmp_ret), K(job_id), K(generation));
        }
      }
    }
    LOG_INFO("NOTICE: DBMS Job master end running!", K(ret), K(running_));
    running_ = false;
  }
  return ret;
}

int ObDBMSJobMaster::scheduler_job(ObDBMSJobKey *job_key, bool is_retry)
{
  int ret = OB_SUCCESS;
  bool handled = false;
  UNUSED(is_retry);

  CK (OB_LIKELY(inited_));
  CK (OB_NOT_NULL(job_key));
  CK (OB_LIKELY(job_key->is_valid()));

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(check_table_change_(job_key, handled))) {
    LOG_WARN("fail to check dbms job table change", K(ret));
  } else if (handled) {
    // The key was rescheduled, replaced by reconciliation, or discarded as
    // stale. Ownership has already been transferred or released.
  } else {
    ObDBMSJobInfo job_info;
    ObArenaAllocator allocator;
    if (OB_FAIL(job_utils_.get_dbms_job_info(
            job_key->get_job_id(), allocator, job_info))) {
      LOG_WARN("fail to load dbms job, retry later", K(ret), KPC(job_key));
      job_key->set_execute_at(ObTimeUtility::current_time() + MIN_SCHEDULER_INTERVAL);
      job_key->set_delay(MIN_SCHEDULER_INTERVAL);
      job_key->set_check_job(true);
      ObDBMSJobKey *replaced_job_key = NULL;
      const int tmp_ret = scheduler_task_.scheduler(job_key, replaced_job_key);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("fail to reschedule dbms job after read error", K(tmp_ret), KPC(job_key));
        allocator_.free(job_key);
        job_key = NULL;
      }
      if (OB_NOT_NULL(replaced_job_key)) {
        allocator_.free(replaced_job_key);
        replaced_job_key = NULL;
      }
    } else if (job_info.valid()) {
      bool ignore_nextdate = false;
      bool retry_current_occurrence = false;
      const int64_t now = ObTimeUtility::current_time();
      const bool deadline_due = job_info.get_next_date() <= now;
      if (!job_key->is_check()
          && !job_info.is_running()
          && !job_info.is_broken()
          && deadline_due) {
        bool can_running = false;
        int tmp_ret = job_utils_.check_job_can_running(can_running);
        if (OB_SUCCESS != tmp_ret) {
          ret = tmp_ret;
          retry_current_occurrence = true;
          LOG_WARN("fail to check whether dbms job can run; retry current occurrence",
              K(ret), K(job_info));
        } else if (!can_running) {
          retry_current_occurrence = true;
          LOG_INFO("dbms job concurrency limit reached; retry current occurrence",
              K(job_info));
        } else if (OB_SUCCESS != (tmp_ret = job_utils_.update_for_start(job_info, true))) {
          ret = tmp_ret;
          retry_current_occurrence = true;
          LOG_WARN("fail to update dbms job for start; retry current occurrence",
              K(ret), K(job_info));
        } else {
          const uint64_t run_job_id = job_key->get_job_id();
          ex_rpc::async_call([run_job_id]() {
            ObDBMSJobExecutor executor;
            if (OB_NOT_NULL(GCTX.sql_proxy_) && OB_NOT_NULL(GCTX.schema_service_)
                && OB_SUCCESS == executor.init(GCTX.sql_proxy_, GCTX.schema_service_)) {
              (void)executor.run_dbms_job(run_job_id);
            }
          });
          // Only a successfully claimed occurrence may advance its deadline.
          ignore_nextdate = true;
        }
      }

      if (retry_current_occurrence) {
        job_key->set_execute_at(now + MIN_SCHEDULER_INTERVAL);
        job_key->set_delay(MIN_SCHEDULER_INTERVAL);
        job_key->set_check_job(false);
        job_key->set_check_new(false);
        ObDBMSJobKey *replaced_job_key = NULL;
        const int tmp_ret = scheduler_task_.scheduler(job_key, replaced_job_key);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("fail to reschedule current dbms job occurrence",
              K(tmp_ret), K(job_info), KPC(job_key));
          allocator_.free(job_key);
          job_key = NULL;
          if (OB_SUCC(ret)) {
            ret = tmp_ret;
          }
        }
        if (OB_NOT_NULL(replaced_job_key)) {
          allocator_.free(replaced_job_key);
          replaced_job_key = NULL;
        }
      } else {
        const int tmp_ret = register_job(job_info, job_key, ignore_nextdate);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("failed to register job to job queue", K(tmp_ret), K(job_info));
          if (OB_SUCC(ret)) {
            ret = tmp_ret;
          }
        }
      }
    } else {
      const int tmp_ret = alive_jobs_.erase_refactored(job_key->get_job_id());
      if (OB_SUCCESS != tmp_ret && OB_HASH_NOT_EXIST != tmp_ret) {
        LOG_INFO("failed to delete invalid job from hash set", K(tmp_ret), KPC(job_key));
      }
      allocator_.free(job_key);
      job_key = NULL;
    }
  }
  return ret;
}

int ObDBMSJobMaster::destroy()
{
  ready_queue_.destroy();
  scheduler_task_.destroy();
  scheduler_thread_.destroy();
  allocator_.clear();
  return OB_SUCCESS;
}

int ObDBMSJobMaster::alloc_job_key(
  ObDBMSJobKey *&job_key, uint64_t job_id,
  uint64_t execute_at, uint64_t delay,
  bool check_job, bool check_new,
  uint64_t generation)
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
                         check_job, check_new,
                         generation))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to init scheduler job id", K(ret));
  }
  return ret;
}

int ObDBMSJobMaster::load_and_register_new_jobs(
    bool can_advance_seq,
    uint64_t target_seq)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObDBMSJobInfo, 32> job_infos;
  ObArenaAllocator allocator;
  ATOMIC_STORE(&needs_reconcile_, true);
  if (OB_FAIL(job_utils_.get_dbms_job_infos_in_runtime(allocator, job_infos))) {
    LOG_WARN("fail to load dbms jobs for reconciliation", K(ret));
  } else {
    const uint64_t next_generation = schedule_generation_ + 1;
    if (OB_FAIL(clear_scheduled_jobs_())) {
      LOG_WARN("fail to clear old dbms job deadlines", K(ret));
    } else {
      alive_jobs_.clear();
      // From this point, any scheduling failure may race in from the timer
      // callback and must survive a successful SQL rebuild.
      ATOMIC_STORE(&needs_reconcile_, false);
      if (OB_FAIL(register_jobs(job_infos, next_generation))) {
        ATOMIC_STORE(&needs_reconcile_, true);
        LOG_WARN("fail to rebuild dbms job deadlines", K(ret), K(next_generation));
        const int cleanup_ret = clear_scheduled_jobs_();
        if (OB_SUCCESS != cleanup_ret) {
          LOG_WARN("fail to clean partial dbms job reconciliation", K(cleanup_ret));
        }
        alive_jobs_.clear();
      } else {
        schedule_generation_ = next_generation;
        if (can_advance_seq) {
          // target_seq was captured before the table read. A concurrent commit
          // remains visible to the next dequeued key or control tick.
          job_table_change_seq_ = target_seq;
        }
      }
    }
  }
  LOG_INFO("load and reconcile dbms jobs", K(ret), K(can_advance_seq),
      K(target_seq), K(schedule_generation_), K(job_infos));
  return ret;
}

int ObDBMSJobMaster::register_jobs(
  ObIArray<ObDBMSJobInfo> &job_infos,
  uint64_t generation)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < job_infos.count(); ++i) {
    ObDBMSJobInfo &job_info = job_infos.at(i);
    if (job_info.valid()) {
      if (!job_info.is_broken()
          && OB_FAIL(register_job(job_info, NULL, false, generation))) {
        LOG_WARN("fail to register reconciled dbms job", K(ret), K(job_info));
      } else if (OB_FAIL(alive_jobs_.set_refactored(job_info.get_job_id()))) {
        LOG_WARN("fail to add reconciled dbms job to alive set", K(ret), K(job_info));
      }
    }
  }
  if (OB_SUCC(ret)
      && OB_FAIL(schedule_change_check_(NULL, generation))) {
    LOG_WARN("fail to schedule dbms job change check", K(ret), K(generation));
  }
  return ret;
}

int ObDBMSJobMaster::register_job(
  ObDBMSJobInfo &job_info,
  ObDBMSJobKey *job_key,
  bool ignore_nextdate,
  uint64_t generation)
{
  int ret = OB_SUCCESS;
  int64_t execute_at = -1;
  int64_t delay = -1;
  bool check_job = false;
  const int64_t now = ObTimeUtility::current_time();

  CK (OB_LIKELY(inited_));
  CK (job_info.valid());
  if (OB_FAIL(ret)) {
  } else if (job_info.is_broken()) {
    // Broken jobs are re-enabled by table-change reconciliation.
  } else if (job_info.is_running()) {
    execute_at = now + MIN_SCHEDULER_INTERVAL;
    delay = MIN_SCHEDULER_INTERVAL;
    check_job = true;
  } else if (OB_FAIL(job_utils_.calc_execute_at(
                 job_info, execute_at, delay, ignore_nextdate))) {
    LOG_WARN("fail to calculate dbms job deadline", K(ret), K(job_info));
  }

  if (OB_FAIL(ret)) {
  } else if (delay < 0) {
    if (OB_NOT_NULL(job_key)) {
      allocator_.free(job_key);
      job_key = NULL;
    }
  } else if (OB_ISNULL(job_key)) {
    if (OB_FAIL(alloc_job_key(
            job_key,
            job_info.get_job_id(),
            execute_at,
            delay,
            check_job,
            false,
            generation))) {
      LOG_WARN("fail to allocate dbms job deadline", K(ret), K(job_info));
    }
  } else {
    CK (job_key->get_job_id() == job_info.get_job_id());
    if (OB_SUCC(ret)) {
      job_key->set_execute_at(execute_at);
      job_key->set_delay(delay);
      job_key->set_check_job(check_job);
      job_key->set_check_new(false);
    }
  }
  ObDBMSJobKey *replaced_job_key = NULL;
  if (OB_SUCC(ret) && OB_NOT_NULL(job_key)
      && OB_FAIL(scheduler_task_.scheduler(job_key, replaced_job_key))) {
    LOG_WARN("fail to schedule dbms job deadline", K(ret), K(job_info), KPC(job_key));
    allocator_.free(job_key);
    job_key = NULL;
  }
  if (OB_FAIL(ret)) {
    ATOMIC_STORE(&needs_reconcile_, true);
    if (OB_NOT_NULL(job_key)) {
      // register_job() always consumes a supplied key. Do not leave a popped
      // ready-queue key unreachable when deadline calculation fails.
      allocator_.free(job_key);
      job_key = NULL;
    }
  }
  if (OB_NOT_NULL(replaced_job_key)) {
    // ObSortedVector::replace() transfers the overwritten pointer back to the
    // caller. It is no longer timer-owned and can be released here.
    allocator_.free(replaced_job_key);
    replaced_job_key = NULL;
  }
  LOG_INFO("register dbms job deadline", K(ret), K(job_info), KPC(job_key));
  return ret;
}

int ObDBMSJobMaster::check_table_change_(ObDBMSJobKey *job_key, bool &handled)
{
  int ret = OB_SUCCESS;
  handled = false;
  if (OB_NOT_NULL(job_key)
      && job_key->get_generation() != schedule_generation_) {
    LOG_INFO("discard stale dbms job key", K(schedule_generation_), KPC(job_key));
    allocator_.free(job_key);
    handled = true;
  } else {
    uint64_t target_seq = 0;
    const int seq_ret =
        ObInternalTableChangeNotifier::get_instance().get_change_seq(
            OB_ALL_JOB_TID, target_seq);
    const bool need_reconcile = OB_ISNULL(job_key)
        || ATOMIC_LOAD(&needs_reconcile_)
        || OB_SUCCESS != seq_ret
        || target_seq != job_table_change_seq_;
    if (need_reconcile) {
      if (OB_SUCCESS != seq_ret) {
        LOG_WARN("fail to get dbms job table change sequence; reconcile fail-open",
            K(seq_ret), K(job_table_change_seq_));
      }
      ret = load_and_register_new_jobs(OB_SUCCESS == seq_ret, target_seq);
      // The dequeued key is never reused across a generation change. This
      // keeps ownership unambiguous if rebuilding or scheduling the new
      // control key fails part-way through.
      if (OB_NOT_NULL(job_key)) {
        allocator_.free(job_key);
        job_key = NULL;
      }
      if (OB_FAIL(ret)) {
        const int retry_ret = schedule_change_check_(NULL, schedule_generation_);
        if (OB_SUCCESS != retry_ret) {
          LOG_WARN("fail to schedule dbms job reconciliation retry", K(retry_ret));
        }
      }
      handled = true;
    } else if (job_key->is_check_new()) {
      if (OB_FAIL(schedule_change_check_(job_key, schedule_generation_))) {
        LOG_WARN("fail to reschedule dbms job change check", K(ret));
      }
      handled = true;
    }
  }
  return ret;
}

int ObDBMSJobMaster::schedule_change_check_(
    ObDBMSJobKey *check_key,
    uint64_t generation)
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
            true,
            generation))) {
      LOG_WARN("fail to allocate dbms job change check", K(ret));
    }
  } else if (OB_UNLIKELY(!check_key->is_check_new())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dbms job change check key", K(ret), KPC(check_key));
  } else {
    check_key->set_job_id(0);
    check_key->set_execute_at(now + MIN_SCHEDULER_INTERVAL);
    check_key->set_delay(MIN_SCHEDULER_INTERVAL);
    check_key->set_generation(generation);
  }
  ObDBMSJobKey *replaced_job_key = NULL;
  if (OB_SUCC(ret)
      && OB_FAIL(scheduler_task_.scheduler(check_key, replaced_job_key))) {
    LOG_WARN("fail to schedule dbms job change check", K(ret), KPC(check_key));
    allocator_.free(check_key);
    check_key = NULL;
  }
  if (OB_FAIL(ret)) {
    ATOMIC_STORE(&needs_reconcile_, true);
    if (OB_NOT_NULL(check_key)) {
      allocator_.free(check_key);
      check_key = NULL;
    }
  }
  if (OB_NOT_NULL(replaced_job_key)) {
    allocator_.free(replaced_job_key);
    replaced_job_key = NULL;
  }
  return ret;
}

int ObDBMSJobMaster::clear_scheduled_jobs_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(scheduler_task_.pause_and_wait())) {
    LOG_WARN("fail to pause dbms job timer", K(ret));
    scheduler_task_.resume();
  } else {
    ObDBMSJobKey *job_key = NULL;
    int pop_ret = OB_SUCCESS;
    while (OB_SUCCESS == (pop_ret = scheduler_task_.pop_waiting_job(job_key))) {
      if (OB_NOT_NULL(job_key)) {
        allocator_.free(job_key);
        job_key = NULL;
      }
    }
    if (OB_ITER_END != pop_ret) {
      ret = pop_ret;
      LOG_WARN("fail to drain dbms job timer keys", K(ret));
    }
    scheduler_task_.resume();
  }
  return ret;
}

} // end for namespace dbms_job
} // end for namespace oceanbase
