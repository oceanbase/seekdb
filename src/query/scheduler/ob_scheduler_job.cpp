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

#define USING_LOG_PREFIX SQL

#include "query/scheduler/ob_scheduler_job.h"
#include "lib/ob_check_macros.h"

namespace oceanbase
{
using namespace common;

namespace dbms_scheduler
{
namespace
{

bool is_legacy_stats_job(const ObString &job_name)
{
  static const char *STATS_JOB_NAMES[] = {
      "MONDAY_WINDOW",
      "TUESDAY_WINDOW",
      "WEDNESDAY_WINDOW",
      "THURSDAY_WINDOW",
      "FRIDAY_WINDOW",
      "SATURDAY_WINDOW",
      "SUNDAY_WINDOW",
      "OPT_STATS_HISTORY_MANAGER",
      "ASYNC_GATHER_STATS_JOB_PROC",
  };
  bool matches = false;
  for (int64_t i = 0;
       !matches && i < static_cast<int64_t>(sizeof(STATS_JOB_NAMES) / sizeof(STATS_JOB_NAMES[0]));
       ++i) {
    matches = (0 == job_name.case_compare(STATS_JOB_NAMES[i]));
  }
  return matches;
}

} // namespace

ObDBMSSchedFuncSet ObDBMSSchedFuncSet::instance_;

int ObDBMSSchedJobInfo::deep_copy(ObIAllocator &allocator, const ObDBMSSchedJobInfo &other)
{
  int ret = OB_SUCCESS;
  user_id_ = other.user_id_;
  database_id_ = other.database_id_;
  job_ = other.job_;
  last_modify_ = other.last_modify_;
  last_date_ = other.last_date_;
  this_date_ = other.this_date_;
  next_date_ = other.next_date_;
  total_ = other.total_;
  failures_ = other.failures_;
  flag_ = other.flag_;
  scheduler_flags_ = other.scheduler_flags_;
  start_date_ = other.start_date_;
  end_date_ = other.end_date_;
  enabled_ = other.enabled_;
  auto_drop_ = other.auto_drop_;
  interval_ts_ = other.interval_ts_;
  max_run_duration_ = other.max_run_duration_;
  max_failures_ = other.max_failures_;
  func_type_ = other.func_type_;
  this_exec_date_ = other.this_exec_date_;

  OZ (ob_write_string(allocator, other.lowner_, lowner_));
  OZ (ob_write_string(allocator, other.powner_, powner_));
  OZ (ob_write_string(allocator, other.cowner_, cowner_));
  OZ (ob_write_string(allocator, other.interval_, interval_));
  OZ (ob_write_string(allocator, other.repeat_interval_, repeat_interval_));
  OZ (ob_write_string(allocator, other.what_, what_));
  OZ (ob_write_string(allocator, other.nlsenv_, nlsenv_));
  OZ (ob_write_string(allocator, other.charenv_, charenv_));
  OZ (ob_write_string(allocator, other.exec_env_, exec_env_));
  OZ (ob_write_string(allocator, other.job_name_, job_name_));
  OZ (ob_write_string(allocator, other.job_class_, job_class_));
  OZ (ob_write_string(allocator, other.program_name_, program_name_));
  OZ (ob_write_string(allocator, other.state_, state_));
  OZ (ob_write_string(allocator, other.job_action_, job_action_));
  OZ (ob_write_string(allocator, other.job_type_, job_type_));
  OZ (ob_write_string(allocator, other.this_exec_trace_id_, this_exec_trace_id_));
  OZ (ob_write_string(allocator, "REGULAR", job_style_));
  return ret;
}

ObDBMSSchedFuncType ObDBMSSchedJobInfo::get_func_type() const
{
  ObDBMSSchedFuncType func_type = func_type_;
  if (ObDBMSSchedFuncType::USER_JOB == func_type) {
    if (is_legacy_stats_job(job_name_)) {
      func_type = ObDBMSSchedFuncType::STAT_MAINTENANCE_JOB;
    }
  }
  return func_type;
}

int ObDBMSSchedJobClassInfo::deep_copy(
    ObIAllocator &allocator, const ObDBMSSchedJobClassInfo &other)
{
  int ret = OB_SUCCESS;
  OZ (log_history_.from(other.log_history_, allocator));
  OZ (ob_write_string(allocator, other.job_class_name_, job_class_name_));
  OZ (ob_write_string(allocator, other.service_, service_));
  OZ (ob_write_string(allocator, other.logging_level_, logging_level_));
  OZ (ob_write_string(allocator, other.comments_, comments_));
  return ret;
}

} // namespace dbms_scheduler
} // namespace oceanbase
