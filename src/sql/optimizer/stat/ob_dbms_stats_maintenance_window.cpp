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
#include "sql/optimizer/stat/ob_dbms_stats_maintenance_window.h"
#include "share/ob_scheduled_job_utils.h"
#include "share/ob_tenant_timezone_mgr.h"
#include "observer/dbms_scheduler/ob_dbms_sched_table_operator.h"
#include "observer/dbms_scheduler/ob_dbms_sched_job_utils.h"
#include "share/ob_sql_client_decorator.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase {

namespace common {

const char *windows_name[DAY_OF_WEEK] = {"MONDAY_WINDOW",
                                         "TUESDAY_WINDOW",
                                         "WEDNESDAY_WINDOW",
                                         "THURSDAY_WINDOW",
                                         "FRIDAY_WINDOW",
                                         "SATURDAY_WINDOW",
                                         "SUNDAY_WINDOW"};
const char *opt_stats_history_manager = "OPT_STATS_HISTORY_MANAGER";
const char *async_gather_stats_job_proc = "ASYNC_GATHER_STATS_JOB_PROC";
const int64_t OPT_STATS_HISTORY_MANAGER_JOB_ID = 8;

int ObDbmsStatsMaintenanceWindow::get_stats_maintenance_window_jobs_sql(const ObSysVariableSchema &sys_variable,
                                                                        common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  char buf[OB_MAX_PROC_ENV_LENGTH];
  int64_t current_time = ObTimeUtility::current_time();
  ObSqlString tmp_sql;
  ObSqlString job_action;
  int64_t job_id = 1;
  int64_t pos = 0;
  int32_t offset_sec = 0;
  if (OB_FAIL(sql::ObExecEnv::gen_exec_env(sys_variable, buf, OB_MAX_PROC_ENV_LENGTH, pos))) {
    LOG_WARN("failed to gen exec env", K(ret));
  } else if (OB_FAIL(get_time_zone_offset(sys_variable, offset_sec))) {
    LOG_WARN("failed to get time zone offset", K(ret));
  } else {
    ObString exec_env(pos, buf);
    HEAP_VAR(dbms_scheduler::ObDBMSSchedJobInfo, job_info) {
      //current_time = current_time + offset_sec * 1000000;
      for (int64_t i = 0; i < DAY_OF_WEEK; ++i) {
        int64_t start_usec = -1;
        ObSqlString job_action;
        if (OB_FAIL(get_window_job_info(current_time, i + 1, offset_sec, start_usec, job_action))) {
          LOG_WARN("failed to get window job info", K(ret));
        } else if (OB_UNLIKELY(start_usec == -1 || job_action.empty())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected error", K(ret), K(start_usec), K(job_action));
        } else {
          if (OB_FAIL(get_stat_window_job_info(
                                                    job_id,
                                                    windows_name[i],
                                                    exec_env,
                                                    start_usec,
                                                    job_action,
                                                    job_info))) {
            LOG_WARN("failed to get stat window job info", K(ret));
          } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::create_dbms_sched_job(sql_client, job_id, job_info))) {
            LOG_WARN("failed to create dbms sched job", K(ret), K(job_info));
          } else {
            ++ job_id;
          }
        }
      }
      if (OB_SUCC(ret)) {
        //set stats history manager job
        if (OB_FAIL(get_stats_history_manager_job_info(
                                                      job_id, exec_env, job_info))) {
          LOG_WARN("failed to get stats history manager job sql", K(ret));
        } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::create_dbms_sched_job(sql_client, job_id, job_info))) {
          LOG_WARN("failed to create dbms sched job", K(ret), K(job_info));
        } else {
          ++ job_id;
        }

        //set async gather stats job
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(get_async_gather_stats_job_info(
                                                  job_id, exec_env, job_info))) {
          LOG_WARN("failed to get async gather stats job sql", K(ret));
        } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::create_dbms_sched_job(sql_client, job_id, job_info))) {
          LOG_WARN("failed to create dbms sched job", K(ret), K(job_info));
        } else {
          ++ job_id;
        }
      }
    }
  }
  return ret;
}

int ObDbmsStatsMaintenanceWindow::get_stat_window_job_info(
                                                          const int64_t job_id,
                                                          const char *job_name,
                                                          const ObString &exec_env,
                                                          const int64_t start_usec,
                                                          ObSqlString &job_action,
                                                          dbms_scheduler::ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  int64_t interval_ts = DEFAULT_WEEK_INTERVAL_USEC;
  int64_t end_date = 64060560000000000;//4000-01-01 00:00:00.000000
  int64_t default_duration_sec = DEFAULT_WORKING_DAY_DURATION_SEC;
  
  job_info.job_name_ = ObString(job_name);
  job_info.job_ = job_id;
  job_info.job_action_ = job_action.string();
  job_info.lowner_ = ObString("root@%");
  job_info.powner_ = ObString("root@%");
  job_info.cowner_ = ObString("oceanbase");
  job_info.job_style_ = ObString("regular");
  job_info.job_type_ = ObString("STORED_PROCEDURE");
  job_info.job_class_ = ObString("DEFAULT_JOB_CLASS");
  job_info.start_date_ = start_usec;
  job_info.end_date_ = end_date;
  job_info.repeat_interval_ = ObString("FREQ=WEEKLY; INTERVAL=1");
  job_info.enabled_ = true;
  job_info.auto_drop_ = false;
  job_info.max_run_duration_ = default_duration_sec;
  job_info.exec_env_ = exec_env;
  job_info.comments_ = ObString("used to auto gather table stats");
  job_info.func_type_ = dbms_scheduler::ObDBMSSchedFuncType::STAT_MAINTENANCE_JOB;
  return ret;
}

int ObDbmsStatsMaintenanceWindow::get_stats_history_manager_job_info(
                                                                    const int64_t job_id,
                                                                    const ObString &exec_env,
                                                                    dbms_scheduler::ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  int64_t interval_ts = DEFAULT_DAY_INTERVAL_USEC;
  int64_t end_date = 64060560000000000;//4000-01-01 00:00:00.000000
  int64_t current = ObTimeUtility::current_time() + DEFAULT_DAY_INTERVAL_USEC;
  
  job_info.job_name_ = ObString(opt_stats_history_manager);
  job_info.job_ = job_id;
  job_info.job_action_ = ObString("DBMS_STATS.PURGE_STATS(NULL)");
  job_info.lowner_ = ObString("root@%");
  job_info.powner_ = ObString("root@%");
  job_info.cowner_ = ObString("oceanbase");
  job_info.job_style_ = ObString("regular");
  job_info.job_type_ = ObString("STORED_PROCEDURE");
  job_info.job_class_ = ObString("DEFAULT_JOB_CLASS");
  job_info.start_date_ = current;
  job_info.end_date_ = end_date;
  job_info.repeat_interval_ = ObString("FREQ=DAYLY; INTERVAL=1");
  job_info.enabled_ = true;
  job_info.auto_drop_ = false;
  job_info.max_run_duration_ = DEFAULT_HISTORY_MANAGER_DURATION_SEC;
  job_info.exec_env_ = exec_env;
  job_info.comments_ = ObString("used to stats history manager");
  job_info.func_type_ = dbms_scheduler::ObDBMSSchedFuncType::STAT_MAINTENANCE_JOB;
  return ret;
}

int ObDbmsStatsMaintenanceWindow::get_async_gather_stats_job_info(
                                                                 const int64_t job_id,
                                                                 const ObString &exec_env,
                                                                 dbms_scheduler::ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  int64_t interval_ts = DEFAULT_ASYNC_GATHER_STATS_INTERVAL_USEC;
  int64_t end_date = 64060560000000000;//4000-01-01 00:00:00.000000
  int64_t current = ObTimeUtility::current_time() + DEFAULT_ASYNC_GATHER_STATS_INTERVAL_USEC;
  
  job_info.job_name_ = ObString(async_gather_stats_job_proc);
  job_info.job_ = job_id;
  job_info.job_action_ = ObString("DBMS_STATS.ASYNC_GATHER_STATS_JOB_PROC(600000000)");
  job_info.lowner_ = ObString("root@%");
  job_info.powner_ = ObString("root@%");
  job_info.cowner_ = ObString("oceanbase");
  job_info.job_style_ = ObString("regular");
  job_info.job_type_ = ObString("STORED_PROCEDURE");
  job_info.job_class_ = ObString("DEFAULT_JOB_CLASS");
  job_info.start_date_ = current;
  job_info.end_date_ = end_date;
  job_info.repeat_interval_ = ObString("FREQ=MINUTELY; INTERVAL=15");
  job_info.enabled_ = true;
  job_info.auto_drop_ = false;
  job_info.max_run_duration_ = DEFAULT_ASYNC_GATHER_STATS_DURATION_SEC;
  job_info.exec_env_ = exec_env;
  job_info.comments_ = ObString("used to async gather stats");
  job_info.func_type_ = dbms_scheduler::ObDBMSSchedFuncType::STAT_MAINTENANCE_JOB;
  return ret;
}

/* Default statistics maintenance windows:
   *  WINDOW_NAME                   REPEAT_INTERVAL                       DURATION
   * MONDAY_WINDOW                freq=daily;byday=MON;byhour=22;          4 hours
   * TUESDAY_WINDOW               freq=daily;byday=TUE;byhour=22;          4 hours
   * WEDNESDAY_WINDOW             freq=daily;byday=WED;byhour=22;          4 hours
   * THURSDAY_WINDOW              freq=daily;byday=THU;byhour=22;          4 hours
   * FRIDAY_WINDOW                freq=daily;byday=FRI;byhour=22;          4 hours
   * SATURDAY_WINDOW              freq=daily;byday=SAT;byhour=6;           20 hours
   * SUNDAY_WINDOW                freq=daily;byday=SUN;byhour=6;           20 hours
   * 
   */
int ObDbmsStatsMaintenanceWindow::get_window_job_info(const int64_t current_time,
                                                      const int64_t nth_window,
                                                      const int64_t offset_sec,
                                                      int64_t &start_usec,
                                                      ObSqlString &job_action)
{
  int ret = OB_SUCCESS;
  ObTime ob_time;
  if (OB_UNLIKELY(nth_window < 1 || nth_window > DAYS_PER_WEEK || current_time <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(nth_window), K(current_time));
  } else if (OB_FAIL(ObTimeConverter::usec_to_ob_time(current_time + offset_sec * 1000000,
                                                      ob_time))) {
    LOG_WARN("failed to usec to ob time", K(ret), K(current_time), K(offset_sec));
  } else if (OB_UNLIKELY(ob_time.parts_[DT_WDAY] < 1 ||
                         ob_time.parts_[DT_WDAY] > DAYS_PER_WEEK)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(ob_time.parts_[DT_WDAY]));
  } else {
    //work day set default start time is 22:00 and non-work day set default start time is 6:00
    int64_t default_start_hour = DEFAULT_WORKING_DAY_START_HOHR;
    //work day set default duration time is 4 hours and non-work day set default duration time is 20 hours
    int64_t default_duration_usec = DEFAULT_WORKING_DAY_DURATION_USEC;
    int64_t total_hour_with_trunc = current_time / USEC_OF_HOUR;
    int64_t current_hour = ob_time.parts_[DT_HOUR];
    int64_t current_wday = ob_time.parts_[DT_WDAY];
    LOG_INFO("begin to get window job info", K(current_time), K(total_hour_with_trunc),
                                             K(current_hour), K(current_wday), K(nth_window),
                                             K(default_start_hour), K(default_duration_usec));
    if (OB_FAIL(job_action.append_fmt("DBMS_STATS.GATHER_DATABASE_STATS_JOB_PROC(%ld)",
                                      default_duration_usec))) {
      LOG_WARN("failed to append", K(ret));
    } else {
      int64_t offset_day = nth_window - current_wday;
      if (offset_day < 0) {
        offset_day = offset_day + DAY_OF_WEEK;
      } else if (offset_day  == 0) {
        offset_day = current_hour > default_start_hour ? DAY_OF_WEEK : offset_day;
      }
      int64_t offset_hour = default_start_hour - current_hour + offset_day * HOUR_OF_DAY;
      start_usec = (total_hour_with_trunc + offset_hour) * USEC_OF_HOUR;
      LOG_INFO("succeed to get window job info", K(start_usec), K(offset_hour), K(current_time),
                                                 K(total_hour_with_trunc), K(current_hour),
                                                 K(current_wday), K(nth_window), K(offset_sec),
                                                 K(default_start_hour), K(default_duration_usec),
                                                 K(job_action), K(offset_day));
    }
  }
  return ret;
}

int ObDbmsStatsMaintenanceWindow::is_stats_maintenance_window_attr(sql::ObExecContext &ctx,
                                                                   const ObString &job_name,
                                                                   const ObString &attr_name,
                                                                   const ObString &val_name,
                                                                   bool &is_window_attr,
                                                                   share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  is_window_attr = false;
  sql::ObSQLSessionInfo *session = ctx.get_my_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(session));
  } else if (is_stats_job(job_name)) {
    //now we just support modify job_action、start_date
    if (0 == attr_name.case_compare("job_action")) {
      const char *history_stats_job = "DBMS_STATS.PURGE_STATS(";
      const char *async_gather_stats_job = "DBMS_STATS.ASYNC_GATHER_STATS_JOB_PROC(";
      const char *maintenance_window_job = "DBMS_STATS.GATHER_DATABASE_STATS_JOB_PROC(";
      if ((0 == job_name.case_compare(opt_stats_history_manager) &&
           !val_name.empty() &&
           0 == strncasecmp(val_name.ptr(), history_stats_job, strlen(history_stats_job))) ||
          (0 == job_name.case_compare(async_gather_stats_job_proc) &&
           !val_name.empty() &&
           0 == strncasecmp(val_name.ptr(), async_gather_stats_job, strlen(async_gather_stats_job))) ||
          (0 != job_name.case_compare(opt_stats_history_manager) &&
           0 != job_name.case_compare(async_gather_stats_job_proc) &&
           !val_name.empty() &&
           0 == strncasecmp(val_name.ptr(), maintenance_window_job, strlen(maintenance_window_job)))) {
        if (OB_FAIL(dml.add_column("job_action", ObHexEscapeSqlStr(val_name)))) {
          LOG_WARN("failed to add column", K(ret));
        } else if (OB_FAIL(dml.add_column("what", ObHexEscapeSqlStr(val_name)))) {
          LOG_WARN("failed to add column", K(ret));
        } else {
          is_window_attr = true;
        }
      } else {
        ret = OB_ERR_DBMS_STATS_PL;
        LOG_WARN("the hour of interval must be between 0 and 24", K(ret));
        LOG_USER_ERROR(OB_ERR_DBMS_STATS_PL, "the hour of interval must be between 0 and 24");
      }
    } else if (0 == attr_name.case_compare("next_date")) {
      ObObj time_obj;
      ObObj src_obj;
      int64_t current_time = ObTimeUtility::current_time();
      ObArenaAllocator calc_buf("DbmsStatsWindow");
      ObCastCtx cast_ctx(&calc_buf, NULL, CM_NONE, ObCharset::get_system_collation());
      cast_ctx.dtc_params_ = session->get_dtc_params();
      int64_t specify_time = -1;
      int32_t offset_sec = 0;
      src_obj.set_string(ObVarcharType, val_name);
      const ObTimeZoneInfo* tz_info = get_timezone_info(session);
      if (NULL != tz_info) {
        if (OB_FAIL(tz_info->get_timezone_offset(ObTimeUtility::current_time(), offset_sec))) {
          LOG_WARN("failed to get timezone offset", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObObjCaster::to_type(ObDateTimeType, cast_ctx, src_obj, time_obj))) {
          LOG_WARN("failed to ObTimestampType type", K(ret));
        } else {
          specify_time = time_obj.get_datetime() - SEC_TO_USEC(offset_sec);
        }
      }
      if (OB_SUCC(ret)) {
        bool is_valid = false;
        if (OB_FAIL(check_date_validate(job_name, specify_time + SEC_TO_USEC(offset_sec),
                                        current_time + SEC_TO_USEC(offset_sec), is_valid))) {
          LOG_WARN("failed to check date valid", K(ret));
        } else if (!is_valid) {
          ret = OB_ERR_DBMS_STATS_PL;
          LOG_WARN("Invalid date", K(ret));
          LOG_USER_ERROR(OB_ERR_DBMS_STATS_PL,
                         "The date is invalid. Please check wether they are the same day in a week, or the day is passed.");
        } else if (OB_FAIL(dml.add_time_column("next_date", specify_time))) {
          LOG_WARN("failed to add column", K(ret));
        } else if (OB_FAIL(dml.add_time_column("start_date", specify_time))) {
          LOG_WARN("failed to add column", K(ret));
        } else {
          is_window_attr = true;
          LOG_TRACE("succeed to set next date", K(specify_time));
        }
      }
    } else if (0 == attr_name.case_compare("duration")) {
      // support set duration column.
      char* cname = NULL;
      int64_t specify_time = -1;
      if (OB_FAIL(ob_dup_cstring(ctx.get_allocator(), val_name, cname))) {
        LOG_WARN("failed to dup cstring", K(ret));
      } else if (OB_FAIL(common::ob_atoll(cname, specify_time))) {
        LOG_WARN("fail to parse from string", "string", val_name, K(ret));
      } else if (specify_time < 0 || specify_time > DEFAULT_DAY_INTERVAL_USEC) {
        ret = OB_ERR_DBMS_STATS_PL;
        LOG_WARN("the hour of interval must be between 0 and 24", K(ret));
        LOG_USER_ERROR(OB_ERR_DBMS_STATS_PL, "the hour of interval must be between 0 and 24");
      } else if (OB_FAIL(dml.add_column("max_run_duration", specify_time))) {
        LOG_WARN("fail to add column", K(ret));
      } else {
        is_window_attr = true;
        LOG_TRACE("succeed to set max_run_duration", K(val_name));
      }
    } else {/*do nothing*/
      ret = OB_ERR_DBMS_STATS_PL;
      ObSqlString errmsg;
      errmsg.append_fmt("%.*s is not a valid window attribute.", attr_name.length(), attr_name.ptr());
      LOG_USER_ERROR(OB_ERR_DBMS_STATS_PL, errmsg.ptr());
      LOG_WARN("not a valid window attribute", K(errmsg));
    }
  }
  return ret;
}

bool ObDbmsStatsMaintenanceWindow::is_stats_job(const ObString &job_name)
{
  bool is_true = false;
  for (int64_t i = 0; !is_true && i < DAY_OF_WEEK; ++i) {
    if (0 == job_name.case_compare(windows_name[i])) {
      is_true = true;
    }
  }
  if (!is_true) {
    is_true = (0 == job_name.case_compare(opt_stats_history_manager) ||
               0 == job_name.case_compare(async_gather_stats_job_proc));
  }
  return is_true;
}

int ObDbmsStatsMaintenanceWindow::get_time_zone_offset(const ObSysVariableSchema &sys_variable,
                                                       int32_t &offset_sec)
{
  // implementation extracted to share::ObScheduledJobUtils(common scheduling utility,independent of statistics)
  return share::ObScheduledJobUtils::get_time_zone_offset(sys_variable, offset_sec);
}

int ObDbmsStatsMaintenanceWindow::check_date_validate(const ObString &job_name,
                                                      const int64_t specify_time,
                                                      const int64_t current_time,
                                                      bool &is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = false;
  ObTime ob_time;
  if (specify_time <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(specify_time));
  } else if (current_time > specify_time) {
    is_valid = false;
  } else if (0 == job_name.case_compare(opt_stats_history_manager) ||
             0 == job_name.case_compare(async_gather_stats_job_proc)) {
    is_valid = true;
  } else if (OB_FAIL(ObTimeConverter::usec_to_ob_time(specify_time, ob_time))) {
    LOG_WARN("failed to usec to ob time", K(ret), K(specify_time));
  } else if (OB_UNLIKELY(ob_time.parts_[DT_WDAY] < 1 ||
                         ob_time.parts_[DT_WDAY] > DAYS_PER_WEEK)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(ob_time.parts_[DT_WDAY]));
  } else  {
    if (0 == job_name.case_compare(windows_name[ob_time.parts_[DT_WDAY]-1])) {
      is_valid = true;
    }
  }

  return ret;
}

int ObDbmsStatsMaintenanceWindow::get_async_gather_stats_job_for_upgrade(common::ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  int64_t job_id = 0;
  ObString exec_env;
  ObSqlString values_list;
  bool is_join_exists = false;
  //bug:
  ObArenaAllocator allocator("AsyncStatsJob");
  if (OB_FAIL(check_job_exists(sql_proxy, async_gather_stats_job_proc, is_join_exists))) {
    LOG_WARN("failed to check async gather job exists", K(ret));
  } else if (is_join_exists) {
    //do nothing
  } else if (OB_FAIL(get_next_job_id_and_exec_env(sql_proxy, allocator, job_id, exec_env))) {
    LOG_WARN("failed to get async gather stats job id and exec env", K(ret));
  } else if (OB_UNLIKELY(job_id > dbms_scheduler::ObDBMSSchedTableOperator::JOB_ID_OFFSET ||
                         exec_env.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(job_id), K(exec_env));
  } else {
    HEAP_VAR(dbms_scheduler::ObDBMSSchedJobInfo, job_info) {
      if (OB_FAIL(get_async_gather_stats_job_info(job_id, exec_env, job_info))) {
        LOG_WARN("failed to get async gather stats job info", K(ret), K(job_info));
      } else if (OB_FAIL(dbms_scheduler::ObDBMSSchedJobUtils::create_dbms_sched_job(*sql_proxy, job_id, job_info))) {
        LOG_WARN("failed to create dbms sched job", K(ret), K(job_info));
      }
    }
  }
  return ret;
}

int ObDbmsStatsMaintenanceWindow::get_next_job_id_and_exec_env(common::ObMySQLProxy *sql_proxy,
                                                               ObIAllocator &allocator,
                                                               int64_t &job_id,
                                                               ObString &exec_env)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  if (OB_FAIL(select_sql.append_fmt("SELECT tt.job, t.exec_env FROM"\
                                    " %s t, (SELECT max(job) + 1 AS job FROM %s"\
                                             " WHERE job <= %ld AND job > 0) tt"\
                                    " WHERE t.job_name = '%s' AND t.job = %ld;",
                                    share::OB_ALL_SCHEDULER_JOB_TNAME,
                                    share::OB_ALL_SCHEDULER_JOB_TNAME,
                                    dbms_scheduler::ObDBMSSchedTableOperator::JOB_ID_OFFSET,
                                    opt_stats_history_manager,
                                    OPT_STATS_HISTORY_MANAGER_JOB_ID))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *sql_proxy;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
        LOG_WARN("failed to execute sql", K(ret), K(select_sql));
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        int64_t get_rows = 0;
        //expected only get one row.
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          int64_t fisrt_col = 0;
          int64_t second_col = 1;
          ObObj obj;
          ObString tmp_exec_env;
          if (get_rows > 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected error, expected only one row", K(ret));
          } else if (OB_FAIL(client_result->get_obj(fisrt_col, obj))) {
            LOG_WARN("failed to get object", K(ret));
          } else if (OB_FAIL(obj.get_int(job_id))) {
            LOG_WARN("failed to get int", K(ret), K(obj));
          } else if (OB_FAIL(client_result->get_obj(second_col, obj))) {
            LOG_WARN("failed to get object", K(ret));
          } else if (OB_FAIL(obj.get_varchar(tmp_exec_env))) {
            LOG_WARN("failed to get int", K(ret), K(obj));
          } else if (OB_FAIL(ob_write_string(allocator, tmp_exec_env, exec_env))) {
            LOG_WARN("failed to ob write string", K(ret));
          } else {
            ++ get_rows;
          }
        }
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
    LOG_INFO("succeed to get next job id and exec env", K(ret), K(select_sql), K(job_id), K(exec_env));
  }
  return ret;
}

int ObDbmsStatsMaintenanceWindow::check_job_exists(common::ObMySQLProxy *sql_proxy,
                                                   const char* job_name,
                                                   bool &is_join_exists)
{
  // implementation extracted to share::ObScheduledJobUtils
  return share::ObScheduledJobUtils::check_job_exists(sql_proxy, job_name, is_join_exists);
}

} // namespace common
} // namespace oceanbase
