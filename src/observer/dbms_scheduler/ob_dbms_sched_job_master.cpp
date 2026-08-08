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
#include "share/ob_share_util.h"
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
  } else if (OB_FAIL(alive_jobs_.create(1024, ObMemAttr("DbmsSched_Job")))) {
  } else if (OB_FAIL(thread_cond_.init(ObWaitEventIds::REENTRANT_THREAD_COND_WAIT))) {
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
  is_leader_ = true;
}
void ObDBMSSchedJobMaster::switch_to_follower()
{
  is_leader_ = false;
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
    bool first_iter = true;
    while (OB_SUCC(ret) && !stoped_) {
      int64_t deadline_us;
      int64_t now = ObTimeUtility::current_time();
      int64_t max_deadline = now + CHECK_NEW_INTERVAL;
      if (is_leader_) {
        schedule_due_jobs();
        if (wait_vector_.count() > 0) {
          ObDBMSSchedJobKey *job_key = wait_vector_[0];
          if (OB_ISNULL(job_key) || !job_key->is_valid()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_ERROR("unexpected error, invalid job key in ready queue!", K(ret), KPC(job_key));
            break;
          }
          deadline_us = std::min(job_key->get_execute_at(), static_cast<uint64_t>(max_deadline));
        } else {
          deadline_us = max_deadline;
        }
      } else {
        clear_wait_vector();
        alive_jobs_.clear();
        deadline_us = max_deadline;
      }

      idle(deadline_us);

      if (is_leader_ && (first_iter || TC_REACH_TIME_INTERVAL(CHECK_NEW_INTERVAL))) {
        check_runtime_jobs();
      }
      first_iter = false;

    }
    clear_wait_vector();
    alive_jobs_.clear();
    LOG_INFO("dbms sched job master stoped", K(ret));
  }
  return ret;
}

int ObDBMSSchedJobMaster::schedule_due_jobs()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  while (OB_SUCC(ret) && wait_vector_.count() > 0) {
    ObDBMSSchedJobKey *job_key = wait_vector_[0];
    if (OB_ISNULL(job_key) || !job_key->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("unexpected error, invalid job key in ready queue!", K(ret), KPC(job_key));
      break;
    }
    int64_t delay = job_key->get_execute_at() - ObTimeUtility::current_time();
    if (delay > 0) {
      break; // not yet due
    }
    common::ObCurTraceId::TraceId job_trace_id;
    job_trace_id.init(GCONF.self_addr_);
    ObTraceIdGuard trace_id_guard(job_trace_id);
    if (OB_SUCCESS != (tmp_ret = wait_vector_.remove(wait_vector_.begin()))) {
    } else if (OB_SUCCESS != (tmp_ret = scheduler_job(job_key))) {
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
  } else {
    ObArenaAllocator allocator("DBMSSchedTmp");
    OZ (table_operator_.get_dbms_sched_job_info(
      job_key->get_job_id(), job_key->get_job_name(), allocator, job_info));
    const int64_t now = ObTimeUtility::current_time();
    int64_t next_check_date = now + MIN_SCHEDULER_INTERVAL;
    if (OB_FAIL(ret) || !job_info.valid()) {
      free_job_key(job_key);
      job_key = NULL;
      LOG_INFO("free invalid job", K(job_info));
    } else if (job_info.is_running()) {
      if (now > job_info.get_this_date() + TO_TS(job_info.get_max_run_duration())) {
        if (OB_FAIL(table_operator_.update_for_timeout(job_info))) {
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
      } else if (!can_running) {
        LOG_INFO("job concurrency reach limit, retry later", K(ret), K(job_info), K(can_running));
      } else if (now > job_info.get_next_date() + TO_TS(job_info.get_max_run_duration())) {
        LOG_WARN("job maybe missed, ignore it", K(now), K(job_info));
        int64_t new_next_date = calc_next_date(job_info);
        int tmp = OB_SUCCESS;
        if (OB_SUCCESS != (tmp = table_operator_.update_next_date(job_info, new_next_date))){
        } else {
          next_check_date = new_next_date;
        }
      } else {
        int64_t new_next_date = calc_next_date(job_info);
        if (OB_FAIL(run_job(job_info, job_key, new_next_date))) {
        } else {
          next_check_date = new_next_date;
          next_check_date = min(next_check_date, now + TO_TS(job_info.get_max_run_duration()));
        }
      }
    }
    int tmp = OB_SUCCESS;
    if (OB_NOT_NULL(job_key) && OB_SUCCESS != (tmp = register_job(job_key, next_check_date))) {
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
  is_leader_ = false;
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
    bool is_primary_server = true;
    if (OB_FAIL(ObShareUtil::is_primary_server(is_primary_server))) {
    } else if (!is_primary_server) {
      clear_wait_vector();
      alive_jobs_.clear();
      LOG_INFO("server is standby, not check new jobs, and remove exist jobs");
    } else {
      OZ (check_new_jobs());
    }
  }
  LOG_INFO("check runtime scheduler jobs", K(ret));
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

int ObDBMSSchedJobMaster::register_new_jobs(ObIArray<ObDBMSSchedJobInfo> &job_infos)
{
  int ret = OB_SUCCESS;
  ObDBMSSchedJobInfo job_info;
  for (int64_t i = 0; OB_SUCC(ret) && i < job_infos.count(); i++) {
    job_info = job_infos.at(i);
    if (job_info.valid() && !job_info.is_disabled() && !job_info.is_broken()) {
      int tmp = alive_jobs_.exist_refactored(job_info.get_job_id());
      if (OB_HASH_EXIST == tmp) {
        // Job exists in memory, but its NEXT_DATE may have changed (e.g. via set_attribute).
        // Find the existing key in wait_vector_, remove it, update execute_at, and re-insert.
        int64_t new_next_date = job_info.get_next_date();
        common::ObSortedVector<ObDBMSSchedJobKey *>::iterator iter;
        for (iter = wait_vector_.begin(); iter != wait_vector_.end(); ++iter) {
          ObDBMSSchedJobKey *exist_key = *iter;
          if (exist_key->get_job_id() == job_info.get_job_id()) {
            wait_vector_.remove(iter);
            if (OB_FAIL(register_job(exist_key, new_next_date))) {
              LOG_WARN("failed to update existing job next_date", K(ret), K(job_info));
              free_job_key(exist_key);
            }
            break;
          }
        }
      } else if (OB_HASH_NOT_EXIST == tmp) {
        ObDBMSSchedJobKey *job_key = NULL;
        if (OB_FAIL(alloc_job_key(
          job_key,
          job_info.get_job_id(),
          job_info.get_job_name()))) {
        } else if (OB_FAIL(register_job(job_key, ObTimeUtility::current_time()))) {
          LOG_WARN("failed to register job", K(ret), K(job_info));
          free_job_key(job_key);
          job_key = NULL;
        }
        LOG_INFO("register new job", K(ret), K(job_info));
      } else {
        LOG_ERROR("dbms sched job master check job exist failed", K(tmp), K(job_info));
      }
    }
  }
  return ret;
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
