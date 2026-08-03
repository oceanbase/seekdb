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

#include "ob_dbms_sched_job_master.h"
#include "ob_dbms_sched_job_executor.h"
#include "share/ob_ex_rpc.h"
#include "share/ob_internal_table_change_notifier.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "sql/session/ob_basic_session_info.h"
#define TO_TS(second) (1000000L * second)
namespace oceanbase
{
using namespace common;
using namespace lib;
using namespace share;
using namespace share::schema;
using namespace rootserver;
using namespace obutil;
using namespace obcall;

namespace dbms_scheduler
{

int ObDBMSSchedJobMaster::init(common::ObMySQLProxy *sql_proxy,
                          ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("dbms sched job master already inited", K(ret), K(inited_));
  } else if (OB_ISNULL(sql_proxy)
          || OB_ISNULL(schema_service)
          ) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret), K(sql_proxy), K(schema_service));
  } else if (OB_FAIL(table_operator_.init(sql_proxy))) {
    LOG_WARN("fail to init action record", K(ret));
  } else if (OB_FAIL(alive_jobs_.create(1024, ObMemAttr("DbmsSched_Job")))) {
    LOG_WARN("failed to create job hash set", K(ret));
  } else if (OB_FAIL(thread_cond_.init(ObWaitEventIds::REENTRANT_THREAD_COND_WAIT))) {
    LOG_WARN("failed to init thread cond", K(ret));
  } else if (OB_FAIL(ObInternalTableChangeNotifier::get_instance().register_table(
                 OB_ALL_SCHEDULER_JOB_TID))) {
    LOG_WARN("failed to register scheduler job table change tracking", K(ret));
  } else if (OB_ISNULL(ObCurTraceId::get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("trace id is null", K(ret));
  } else {
    schema_service_ = schema_service;
    inited_ = true;
  }
  LOG_INFO("dbms sched job master inited!", K(ret));
  return ret;
}

int ObDBMSSchedJobMaster::start()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init yet!", K(ret), K(inited_));
  } else {
    stoped_ = false;
  }
  LOG_INFO("dbms sched job master started", K(ret));
  return ret;
}

int ObDBMSSchedJobMaster::stop()
{
  int ret = OB_SUCCESS;
  stoped_ = true;
  wakeup();
  LOG_INFO("dbms sched job master begin stop", K(ret));
  return ret;
}

void ObDBMSSchedJobMaster::switch_to_leader()
{
  // Force a primary-role reconciliation even when the table sequence did not
  // change while this service was a follower.
  ATOMIC_STORE(&has_loaded_primary_jobs_, false);
  ATOMIC_STORE(&is_leader_, true);
  wakeup();
}

void ObDBMSSchedJobMaster::switch_to_follower()
{
  ATOMIC_STORE(&is_leader_, false);
  ATOMIC_STORE(&has_loaded_primary_jobs_, false);
  wakeup();
}

int64_t ObDBMSSchedJobMaster::calc_next_date(ObDBMSSchedJobInfo &job_info)
{
  int64_t ret = 0;
  int64_t next_date = 0;
  if (OB_FAIL(ObDBMSSchedJobUtils::calc_dbms_sched_repeat_expr(job_info, next_date))) {
    next_date = ObDBMSSchedJobInfo::DEFAULT_MAX_END_DATE;
    LOG_WARN("failed to calc next date", KR(ret), K(job_info));
  }
  return next_date;
}

int64_t ObDBMSSchedJobMaster::run_job(ObDBMSSchedJobInfo &job_info, ObDBMSSchedJobKey *job_key, int64_t next_date)
{
  int ret = OB_SUCCESS;
  if (ObTimeUtility::current_time() > job_info.get_end_date()) {
    LOG_INFO("job reach end date, not running", K(job_info));
  } else if (OB_FAIL(table_operator_.update_for_start(job_info, next_date, GCTX.self_addr()))) {
    LOG_WARN("failed to update for start", K(ret), K(job_info), KPC(job_key));
  } else {
    // Run the job asynchronously so the scheduler thread remains responsive.
    // async_call deep-copies the ObString argument before dispatch.
    const uint64_t run_job_id = job_key->get_job_id();
    ex_rpc::async_call<void>(job_key->get_job_name(),
      [run_job_id](const ObString &run_job_name) {
        ObDBMSSchedJobExecutor executor;
        if (OB_NOT_NULL(GCTX.sql_proxy_) && OB_NOT_NULL(GCTX.schema_service_)
            && OB_SUCCESS == executor.init(GCTX.sql_proxy_, GCTX.schema_service_)) {
          (void)executor.run_dbms_sched_job(run_job_id, run_job_name);
        }
      });
  }
  return ret;
}

int ObDBMSSchedJobMaster::scheduler()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not init yet", K(ret));
  } else {
    while (OB_SUCC(ret) && !stoped_) {
      const int64_t now = ObTimeUtility::current_time();
      const int64_t max_deadline = now + CHECK_NEW_INTERVAL;
      int64_t deadline_us = max_deadline;
      if (ATOMIC_LOAD(&is_leader_)) {
        const int check_ret = check_runtime_jobs();
        if (OB_SUCCESS != check_ret) {
          LOG_WARN("fail to check runtime scheduler jobs", K(check_ret));
        } else if (ATOMIC_LOAD(&is_leader_)
                   && ATOMIC_LOAD(&has_loaded_primary_jobs_)) {
          const int schedule_ret = schedule_due_jobs();
          if (OB_EAGAIN == schedule_ret) {
            // A role or table change raced with the previous check. Loop
            // immediately so check_runtime_jobs() reconciles before execution.
            deadline_us = ObTimeUtility::current_time();
          } else if (OB_SUCCESS != schedule_ret) {
            LOG_WARN("fail to schedule due dbms scheduler jobs", K(schedule_ret));
            // Do not reuse an already-due head after an execution error; that
            // would make idle() return immediately and spin this loop.
            deadline_us = ObTimeUtility::current_time() + MIN_SCHEDULER_INTERVAL;
          } else if (wait_vector_.count() > 0) {
            ObDBMSSchedJobKey *job_key = wait_vector_.at(0);
            if (OB_ISNULL(job_key) || !job_key->is_valid()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_ERROR("unexpected invalid scheduler job key", K(ret), KPC(job_key));
            } else {
              deadline_us = std::min(
                  job_key->get_execute_at(),
                  static_cast<uint64_t>(max_deadline));
            }
          }
        }
      } else {
        if (wait_vector_.count() > 0) {
          clear_wait_vector();
        }
        alive_jobs_.clear();
        ATOMIC_STORE(&has_loaded_primary_jobs_, false);
      }

      if (OB_SUCC(ret)) {
        (void)idle(deadline_us);
      }
    }
    clear_wait_vector();
    alive_jobs_.clear();
    ATOMIC_STORE(&has_loaded_primary_jobs_, false);
    LOG_INFO("dbms sched job master stoped", K(ret));
  }
  return ret;
}

int ObDBMSSchedJobMaster::schedule_due_jobs()
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && wait_vector_.count() > 0) {
    bool is_primary_server = false;
    if (!ATOMIC_LOAD(&is_leader_)) {
      ret = OB_EAGAIN;
    } else if (OB_FAIL(ObShareUtil::is_primary_server(is_primary_server))) {
      LOG_WARN("fail to verify scheduler primary role before execution", K(ret));
    } else if (!is_primary_server) {
      ret = OB_EAGAIN;
    } else {
      uint64_t target_seq = 0;
      const int seq_ret =
          ObInternalTableChangeNotifier::get_instance().get_change_seq(
              OB_ALL_SCHEDULER_JOB_TID, target_seq);
      if (OB_SUCCESS == seq_ret
          && target_seq != ATOMIC_LOAD(&scheduler_job_table_change_seq_)) {
        // Leave the due key in wait_vector_. The next scheduler loop performs
        // full reconciliation before any stale deadline can execute.
        ret = OB_EAGAIN;
      } else {
        // Sequence lookup failure is fail-open: check_runtime_jobs() has just
        // completed a full SQL reconciliation, and scheduler_job() re-reads the
        // individual row before taking action.
        ObDBMSSchedJobKey *job_key = wait_vector_.at(0);
        if (OB_ISNULL(job_key) || !job_key->is_valid()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("unexpected invalid scheduler job key", K(ret), KPC(job_key));
        } else {
          const int64_t delay =
              job_key->get_execute_at() - ObTimeUtility::current_time();
          if (delay > 0) {
            break;
          }
          common::ObCurTraceId::TraceId job_trace_id;
          job_trace_id.init(GCONF.self_addr_);
          ObTraceIdGuard trace_id_guard(job_trace_id);
          int tmp_ret = wait_vector_.remove(wait_vector_.begin());
          if (OB_SUCCESS != tmp_ret) {
            ret = tmp_ret;
            LOG_WARN("fail to remove scheduler job from wait vector", K(ret));
          } else if (OB_SUCCESS != (tmp_ret = scheduler_job(job_key))) {
            LOG_WARN("fail to schedule single dbms scheduler job", K(tmp_ret));
          }
        }
      }
    }
  }
  return ret;
}

bool ObDBMSSchedJobMaster::idle(int64_t deadline_us)
{
  ObThreadCondGuard guard(thread_cond_);
  while (!wokeup_ && !stoped_) {
    if (deadline_us > 0) {
      int64_t remaining = deadline_us - ObTimeUtility::current_time();
      if (remaining <= 0) break;
      thread_cond_.wait_us(remaining);
    } else {
      thread_cond_.wait();
    }
  }
  bool was_woken = wokeup_;
  wokeup_ = false;
  return was_woken;
}

void ObDBMSSchedJobMaster::wakeup()
{
  ObThreadCondGuard guard(thread_cond_);
  wokeup_ = true;
  thread_cond_.broadcast();
}

int ObDBMSSchedJobMaster::scheduler_job(ObDBMSSchedJobKey *job_key)
{
  int ret = OB_SUCCESS;

  ObAddr execute_addr;
  ObDBMSSchedJobInfo job_info;
  bool can_running = false;

  CK (OB_LIKELY(inited_));
  CK (OB_NOT_NULL(job_key));
  CK (OB_LIKELY(job_key->is_valid()));
  if (OB_FAIL(ret)) {
    LOG_WARN("fail to scheduler job", K(ret), KPC(job_key));
  } else {
    ObArenaAllocator allocator("DBMSSchedTmp");
    OZ (table_operator_.get_dbms_sched_job_info(
      job_key->get_job_id(), job_key->get_job_name(), allocator, job_info));
    const int64_t now = ObTimeUtility::current_time();
    int64_t next_check_date = now + MIN_SCHEDULER_INTERVAL;
    if (OB_FAIL(ret) || !job_info.valid()) {
      // A failed or not-found point read removes this key. Force a full-table
      // reconciliation on the next loop so a transient read cannot lose it.
      ATOMIC_STORE(&has_loaded_primary_jobs_, false);
      free_job_key(job_key);
      job_key = NULL;
      LOG_INFO("free invalid job", K(job_info));
    } else if (job_info.is_running()) {
      if (now > job_info.get_this_date() + TO_TS(job_info.get_max_run_duration())) {
        if (OB_FAIL(table_operator_.update_for_timeout(job_info))) {
          LOG_WARN("update for end failed for timeout job", K(ret));
        } else {
          LOG_WARN("job is timeout, force update for end", K(job_info), K(now));
        }
      } else {
        LOG_INFO("job is running now, retry later", K(job_info));
      }
    } else if (job_info.is_killed()) {
      free_job_key(job_key);
      job_key = NULL;
      int tmp = OB_SUCCESS;
      if (OB_SUCCESS != (tmp = table_operator_.update_for_kill(job_info))) {
        ATOMIC_STORE(&has_loaded_primary_jobs_, false);
        LOG_WARN("update for stop failed", K(tmp), K(job_info));
      } else {
        LOG_WARN("update for stop job", K(job_info));
      }
    } else if (job_info.is_disabled() || job_info.is_broken()) {
      free_job_key(job_key);
      job_key = NULL;
      LOG_INFO("free disable/broken job", K(job_info));
    } else if (now > job_info.get_end_date()) {
      int tmp = OB_SUCCESS;
      if (OB_SUCCESS != (tmp = table_operator_.update_for_enddate(job_info))) {
        ATOMIC_STORE(&has_loaded_primary_jobs_, false);
        LOG_WARN("update for end failed for auto drop job", K(tmp), K(job_info));
      } else {
        LOG_WARN("update for end for expired job", K(job_info), K(now));
      }
      free_job_key(job_key);
      job_key = NULL;
      LOG_INFO("free enddate job", K(job_info));
    } else if (now < job_info.get_next_date()) {
        next_check_date = job_info.get_next_date();
    } else {
      bool can_running = false;
      if (OB_FAIL(table_operator_.check_job_can_running(alive_jobs_.size(), can_running))) {
        LOG_WARN("failed to check job can running, retry later", K(ret));
      } else if (!can_running) {
        LOG_INFO("job concurrency reach limit, retry later", K(ret), K(job_info), K(can_running));
      } else if (now > job_info.get_next_date() + TO_TS(job_info.get_max_run_duration())) {
        LOG_WARN("job maybe missed, ignore it", K(now), K(job_info));
        int64_t new_next_date = calc_next_date(job_info);
        int tmp = OB_SUCCESS;
        if (OB_SUCCESS != (tmp = table_operator_.update_next_date(job_info, new_next_date))){
          LOG_WARN("update next date failed", K(tmp), K(job_info));
        } else {
          next_check_date = new_next_date;
        }
      } else {
        int64_t new_next_date = calc_next_date(job_info);
        if (OB_FAIL(run_job(job_info, job_key, new_next_date))) {
          LOG_WARN("failed to run job", K(ret), K(job_info), KPC(job_key));
        } else {
          next_check_date = new_next_date;
          next_check_date = min(next_check_date, now + TO_TS(job_info.get_max_run_duration()));
        }
      }
    }
    int tmp = OB_SUCCESS;
    if (OB_NOT_NULL(job_key) && OB_SUCCESS != (tmp = register_job(job_key, next_check_date))) {
      ATOMIC_STORE(&has_loaded_primary_jobs_, false);
      LOG_WARN("failed to register job", K(tmp), K(job_info));
      free_job_key(job_key);
      job_key = NULL;
    }
  }
  return ret;
}

int ObDBMSSchedJobMaster::destroy()
{
  allocator_.destroy();
  thread_cond_.destroy();
  inited_ = false;
  stoped_ = true;
  ATOMIC_STORE(&is_leader_, false);
  ATOMIC_STORE(&has_loaded_primary_jobs_, false);
  return OB_SUCCESS;
}

int ObDBMSSchedJobMaster::alloc_job_key(
  ObDBMSSchedJobKey *&job_key, uint64_t job_id, const ObString &job_name)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  job_key = NULL;
  if (OB_ISNULL(ptr = allocator_.alloc(sizeof(ObDBMSSchedJobKey)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret), K(ptr));
  } else if (OB_ISNULL(job_key =
    new(ptr)ObDBMSSchedJobKey(job_id, job_name))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to init scheduler job id", K(ret));
  } else {
    if (OB_FAIL(alive_jobs_.set_refactored(job_id))) {
      LOG_WARN("faile to add job to alive_jobs", K(ret), K(job_id));
      allocator_.free(job_key);
      job_key = NULL;
    }
  }
  return ret;
}

void ObDBMSSchedJobMaster::free_job_key(ObDBMSSchedJobKey *&job_key)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(job_key)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("job_key is null", K(ret));
  } else {
    OZ (alive_jobs_.erase_refactored(job_key->get_job_id()));
    allocator_.free(job_key);
    job_key = NULL;
  }
}

int ObDBMSSchedJobMaster::check_runtime_jobs()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dbms sched job not init yet", K(ret), K(inited_));
  } else {
    // Role checks deliberately stay outside the sequence gate. A primary to
    // standby (or reverse) transition may not write the scheduler table.
    bool is_primary_server = false;
    if (OB_FAIL(ObShareUtil::is_primary_server(is_primary_server))) {
      ATOMIC_STORE(&has_loaded_primary_jobs_, false);
      LOG_WARN("fail to check whether is primary server", KR(ret));
    } else if (!is_primary_server) {
      if (wait_vector_.count() > 0) {
        clear_wait_vector();
      }
      alive_jobs_.clear();
      ATOMIC_STORE(&has_loaded_primary_jobs_, false);
      LOG_INFO("server is standby; clear runtime scheduler jobs");
    } else {
      uint64_t target_seq = 0;
      const int seq_ret =
          ObInternalTableChangeNotifier::get_instance().get_change_seq(
              OB_ALL_SCHEDULER_JOB_TID, target_seq);
      const bool need_reconcile =
          !ATOMIC_LOAD(&has_loaded_primary_jobs_)
          || OB_SUCCESS != seq_ret
          || target_seq != ATOMIC_LOAD(&scheduler_job_table_change_seq_);
      if (need_reconcile) {
        ATOMIC_STORE(&has_loaded_primary_jobs_, false);
        if (OB_SUCCESS != seq_ret) {
          LOG_WARN("fail to get scheduler table change sequence; reconcile fail-open",
              K(seq_ret), K_(scheduler_job_table_change_seq));
        }
        if (OB_FAIL(check_new_jobs())) {
          LOG_WARN("fail to reconcile runtime scheduler jobs", K(ret));
        } else {
          if (OB_SUCCESS == seq_ret) {
            // Store the value captured before the full table read. A commit
            // racing with that read remains visible on the next loop.
            ATOMIC_STORE(&scheduler_job_table_change_seq_, target_seq);
          }
          ATOMIC_STORE(&has_loaded_primary_jobs_, true);
        }
      }
    }
  }
  const uint64_t current_seq = ATOMIC_LOAD(&scheduler_job_table_change_seq_);
  const bool has_loaded = ATOMIC_LOAD(&has_loaded_primary_jobs_);
  LOG_DEBUG("check runtime scheduler jobs", K(ret), K(current_seq), K(has_loaded));
  return ret;
}

int ObDBMSSchedJobMaster::check_new_jobs()
{
  int ret = OB_SUCCESS;
  ObSEArray<ObDBMSSchedJobInfo, 12> job_infos;
  ObArenaAllocator allocator("DBMSSchedTmp");
  OZ (table_operator_.get_dbms_sched_job_infos_in_runtime(allocator, job_infos));
  OZ (register_new_jobs(job_infos));
  LOG_INFO("check new jobs", K(ret), K(job_infos));
  return ret;
}

int ObDBMSSchedJobMaster::register_new_jobs(
    ObIArray<ObDBMSSchedJobInfo> &job_infos)
{
  int ret = OB_SUCCESS;
  // The full query succeeded, so replace the in-memory snapshot as one
  // scheduler-thread-owned generation. Deleted, disabled and broken jobs are
  // removed by omission; running/killed jobs retain their control deadlines.
  clear_wait_vector();
  alive_jobs_.clear();
  for (int64_t i = 0; OB_SUCC(ret) && i < job_infos.count(); ++i) {
    ObDBMSSchedJobInfo &job_info = job_infos.at(i);
    const bool should_schedule = job_info.valid()
        && (job_info.is_running()
            || job_info.is_killed()
            || (!job_info.is_disabled() && !job_info.is_broken()));
    if (should_schedule) {
      ObDBMSSchedJobKey *job_key = NULL;
      if (OB_FAIL(alloc_job_key(
              job_key,
              job_info.get_job_id(),
              job_info.get_job_name()))) {
        LOG_WARN("fail to allocate reconciled scheduler job", K(ret), K(job_info));
      } else if (OB_FAIL(register_job(
                     job_key, get_reconcile_deadline_(job_info)))) {
        LOG_WARN("fail to register reconciled scheduler job", K(ret), K(job_info));
        free_job_key(job_key);
      }
    }
  }
  if (OB_FAIL(ret)) {
    clear_wait_vector();
    alive_jobs_.clear();
  }
  return ret;
}

int64_t ObDBMSSchedJobMaster::get_reconcile_deadline_(
    ObDBMSSchedJobInfo &job_info) const
{
  const int64_t now = ObTimeUtility::current_time();
  int64_t deadline = job_info.get_next_date();
  if (job_info.is_running()) {
    const int64_t timeout_deadline =
        job_info.get_this_date() + TO_TS(job_info.get_max_run_duration());
    deadline = deadline > 0
        ? MIN(deadline, timeout_deadline)
        : timeout_deadline;
  } else if (job_info.is_killed() || now > job_info.get_end_date()) {
    deadline = now;
  } else if (deadline <= 0) {
    deadline = now;
  }
  return deadline;
}

int ObDBMSSchedJobMaster::register_job(ObDBMSSchedJobKey *job_key, int64_t next_date)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(job_key)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("job key is null", K(ret));
  } else if (next_date == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("next date should not be 0", K(ret), KPC(job_key), K(next_date));
  } else {
    job_key->set_execute_at(next_date);
    common::ObSortedVector<ObDBMSSchedJobKey *>::iterator iter;
    ObDBMSSchedJobKey *replace_job_key = NULL;
    OZ (wait_vector_.replace(job_key, iter, compare_job_key, equal_job_key, replace_job_key));
    if (OB_SUCC(ret) && OB_NOT_NULL(replace_job_key) && replace_job_key != job_key) {
      // replace() returns ownership of the overwritten key. alive_jobs_ still
      // belongs to the newly installed key with the same job id.
      allocator_.free(replace_job_key);
      replace_job_key = NULL;
    }
  }
  return ret;
}

void ObDBMSSchedJobMaster::clear_wait_vector()
{
  common::ObSortedVector<ObDBMSSchedJobKey *>::iterator iter;
  for (iter = wait_vector_.begin(); iter != wait_vector_.end(); ++iter) {
    ObDBMSSchedJobKey *job_key = *iter;
    allocator_.free(job_key);
  }
  wait_vector_.clear();
}
bool ObDBMSSchedJobMaster::compare_job_key(const ObDBMSSchedJobKey *lhs, const ObDBMSSchedJobKey *rhs)
{
  return lhs->get_execute_at() < rhs->get_execute_at()
    || (lhs->get_execute_at() == rhs->get_execute_at() && lhs->get_job_id() < rhs->get_job_id());
}

bool ObDBMSSchedJobMaster::equal_job_key(const ObDBMSSchedJobKey *lhs, const ObDBMSSchedJobKey *rhs)
{
  return lhs->get_job_id() == rhs->get_job_id() &&
         lhs->get_execute_at() == rhs->get_execute_at();
}

} // end for namespace dbms_scheduler
} // end for namespace oceanbase
