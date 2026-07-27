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

#include "ob_dbms_sched_table_operator.h"

#include "sql/optimizer/stat/ob_dbms_stats_maintenance_window.h"

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace share::schema;
using namespace sqlclient;
using namespace storage;

namespace dbms_scheduler
{

int ObDBMSSchedTableOperator::update_next_date(
  ObDBMSSchedJobInfo &job_info, int64_t next_date)
{
  int ret = OB_SUCCESS;

  ObDMLSqlSplicer dml;
  ObSqlString sql;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));

  OZ (dml.add_gmt_modified(now));
  OZ (dml.add_pk_column("job", job_info.job_));
  OZ (dml.add_pk_column("job_name", job_info.job_name_));
  OZ (dml.add_time_column("next_date", next_date));
  OZ (dml.splice_update_sql(OB_ALL_SCHEDULER_JOB_TNAME, sql));
  OZ (sql_proxy_->write(sql.ptr(), affected_rows));
  return ret;
}


int ObDBMSSchedTableOperator::update_for_start(
  ObDBMSSchedJobInfo &job_info, int64_t next_date, ObAddr execute_addr)
{
  int ret = OB_SUCCESS;

  ObDMLSqlSplicer dml;
  ObSqlString sql;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();
  uint64_t data_version = 0;

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));

  OX (job_info.this_date_ = now);
  OZ (dml.add_gmt_modified(now));
  OZ (dml.add_pk_column("job", job_info.job_));
  OZ (dml.add_pk_column("job_name", job_info.job_name_));
  OZ (dml.add_time_column("this_date", job_info.this_date_));
  OZ (dml.add_time_column("next_date", next_date));
  OZ (dml.add_column("state", "SCHEDULED"));
  char ip_port_buf[common::OB_IP_PORT_STR_BUFF];
  char trace_id_buf[common::OB_MAX_TRACE_ID_BUFFER_SIZE];
  OZ (execute_addr.ip_port_to_string(ip_port_buf, common::OB_IP_PORT_STR_BUFF));
  OV (0 < common::ObCurTraceId::get_trace_id()->to_string(trace_id_buf, common::OB_MAX_TRACE_ID_BUFFER_SIZE), OB_SIZE_OVERFLOW);
  OZ (dml.add_column("this_exec_addr", ip_port_buf));
  OZ (dml.add_column("this_exec_trace_id", trace_id_buf));
  OZ (dml.splice_update_sql(OB_ALL_SCHEDULER_JOB_TNAME, sql));
  OZ (sql.append_fmt(" and this_date is null"));
  OZ (sql_proxy_->write(sql.ptr(), affected_rows));
  CK (affected_rows == 1);
  return ret;
}

int ObDBMSSchedTableOperator::update_for_start_execute(
  ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  ObSqlString sql;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();
  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));
  OZ (dml.add_gmt_modified(now));
  OZ (dml.add_pk_column("job", job_info.job_));
  OZ (dml.add_pk_column("job_name", job_info.job_name_));
  OZ (dml.add_time_column("this_exec_date", now));
  OZ (dml.splice_update_sql(OB_ALL_SCHEDULER_JOB_TNAME, sql));
  OZ (sql_proxy_->write(sql.ptr(), affected_rows));
  job_info.this_exec_date_ = now;
  return ret;
}

int ObDBMSSchedTableOperator::_build_job_drop_dml(int64_t now, ObDBMSSchedJobInfo &job_info, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  
  OZ (dml.add_gmt_modified(now));
  OZ (dml.add_pk_column("job_name", job_info.job_name_));
  OZ (dml.splice_delete_sql(OB_ALL_SCHEDULER_JOB_TNAME, sql));
  return ret;
}

int ObDBMSSchedTableOperator::_build_job_finished_dml(int64_t now, ObDBMSSchedJobInfo &job_info, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  
  OZ (dml.add_gmt_modified(now));
  OZ (dml.add_pk_column("job", job_info.job_));
  OZ (dml.add_pk_column("job_name", job_info.job_name_));
  OZ (dml.add_column("state", job_info.state_));
  if (job_info.is_completed() || job_info.is_broken()) { // broken job should be disabled
    OZ (dml.add_column("enabled", false));
  }
  OZ (dml.add_column(true, "this_date"));
  OZ (dml.add_time_column("last_date", job_info.this_date_));
  OZ (dml.add_column("failures", job_info.failures_));
  OZ (dml.add_column("total", job_info.total_));
  OZ (dml.add_column(true, "this_exec_date"));
  OZ (dml.add_column(true, "this_exec_addr"));
  OZ (dml.add_column(true, "this_exec_trace_id"));
  // job reach end_date before first scheduled shoule updated too
  OZ (dml.get_extra_condition().assign_fmt("(state is NULL OR state!='BROKEN') AND (last_date is null OR last_date<=usec_to_time(%ld))", job_info.last_date_));
  OZ (dml.splice_update_sql(OB_ALL_SCHEDULER_JOB_TNAME, sql));
  return ret;
}


int ObDBMSSchedTableOperator::_build_job_rollback_start_dml(ObDBMSSchedJobInfo &job_info, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  
  OZ (dml.add_pk_column("job", job_info.job_));
  OZ (dml.add_pk_column("job_name", job_info.job_name_));
  OZ (dml.add_column(true, "this_date"));
  OZ (dml.add_time_column("next_date", job_info.next_date_));// roll back to old next date
  OZ (dml.splice_update_sql(OB_ALL_SCHEDULER_JOB_TNAME, sql));
  return ret;
}

int ObDBMSSchedTableOperator::update_for_rollback(ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  ObSqlString sql1;
  int64_t affected_rows = 0;
  
  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));
  OZ (_build_job_rollback_start_dml(job_info, sql1));

  OZ (trans.start(sql_proxy_, true));
  OZ (trans.write(sql1.ptr(), affected_rows));
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_ERROR("failed to commit trans", KR(ret), KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }

  return ret;
}

int ObDBMSSchedTableOperator::update_for_enddate(ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  OZ (update_for_end(job_info, 0, "check job enddate"));
  return ret;
}

int ObDBMSSchedTableOperator::update_for_timeout(ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  OZ (update_for_end(job_info, -4012, "check job timeout"));
  return ret;
}


int ObDBMSSchedTableOperator::update_for_end(ObDBMSSchedJobInfo &job_info, int err, const ObString &errmsg)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  ObSqlString sql1;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();
  UNUSED(errmsg);
  
  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));
  if (OB_FAIL(ret)) {
  } else if ((now >= job_info.end_date_ || (job_info.get_interval_ts() == 0 && (job_info.get_repeat_interval().empty() || 0 == job_info.get_repeat_interval().case_compare("null")))) && (true == job_info.auto_drop_)) {
    job_info.state_ = ObString("COMPLETED");
    OZ (_build_job_drop_dml(now, job_info, sql1));
  } else {
    OX (job_info.failures_ = (err == 0) ? 0 : (job_info.failures_ + 1));
    OX (job_info.total_ += (job_info.this_date_ > 0 ? now - job_info.this_date_ : 0));
    if (OB_SUCC(ret) && job_info.max_failures_ > 0 && job_info.failures_ >= job_info.max_failures_) {
      // when if failures > max_failures then set broken state, and disable job
      job_info.state_ = ObString("BROKEN");
    } else if (now >= job_info.end_date_ || (job_info.get_interval_ts() == 0 && (job_info.get_repeat_interval().empty() || 0 == job_info.get_repeat_interval().case_compare("null")))) {
      // when end_date is reach and auto_drop is set false, disable set completed state.
      // for once job, not wait until end date, set completed state when running end
      job_info.state_ = ObString("COMPLETED");
    }
    OZ (_build_job_finished_dml(now, job_info, sql1));
  }

  OZ (trans.start(sql_proxy_, true));
  OZ (trans.write(sql1.ptr(), affected_rows));
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_ERROR("failed to commit trans", KR(ret), KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret;
}

int ObDBMSSchedTableOperator::update_for_kill(ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  ObSqlString sql1;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();
  
  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));
  OZ (_build_job_drop_dml(now, job_info, sql1));
  OZ (trans.start(sql_proxy_, true));
  OZ (trans.write(sql1.ptr(), affected_rows));
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_ERROR("failed to commit trans", KR(ret), KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret; 
}

int ObDBMSSchedTableOperator::check_job_can_running(int64_t alive_job_count, bool &can_running)
{
  int ret = OB_SUCCESS;
  uint64_t job_queue_processor = 0;
  uint64_t job_running_cnt = 0;
  ObSqlString sql;
  OX (can_running = false);
  CK (true);
  OX (job_queue_processor = GCONF.job_queue_processes);
  // found current running job count
  if (OB_FAIL(ret)) {
  } else if (alive_job_count <= job_queue_processor) {
    can_running = true;
  } else {
    OZ (sql.append_fmt("select count(*) from %s where this_date is not null", OB_ALL_SCHEDULER_JOB_TNAME));

    if (OB_SUCC(ret) && job_queue_processor > 0) {
      SMART_VAR(ObMySQLProxy::MySQLResult, result) {
        if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
          LOG_WARN("execute query failed", K(ret), K(sql));
        } else if (OB_ISNULL(result.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get result failed", K(ret), K(sql));
        } else {
          if (OB_SUCCESS == (ret = result.get_result()->next())) {
            int64_t int_value = 0;
            if (OB_FAIL(result.get_result()->get_int(static_cast<const int64_t>(0), int_value))) {
              LOG_WARN("failed to get column in row. ", K(ret));
            } else {
              job_running_cnt = static_cast<uint64_t>(int_value);
            }
          } else {
            LOG_WARN("failed to calc all running job, no row return", K(ret));
          }
        }
      }
      OX (can_running = (job_queue_processor > job_running_cnt));
    }
  }
  return ret;
}

int ObDBMSSchedTableOperator::extract_info(
  sqlclient::ObMySQLResult &result,
  ObIAllocator &allocator, ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObDBMSSchedJobInfo job_info_local;

  
  EXTRACT_INT_FIELD_MYSQL(result, "job", job_info_local.job_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL(result, "user_id", job_info_local.user_id_, uint64_t);
  if (OB_ERR_NULL_VALUE == ret || OB_ERR_COLUMN_NOT_FOUND == ret) {
    ret = OB_SUCCESS;
    job_info_local.user_id_ = OB_INVALID_ID;
  }
  EXTRACT_INT_FIELD_MYSQL(result, "database_id", job_info_local.database_id_, uint64_t);
  if (OB_ERR_NULL_VALUE == ret || OB_ERR_COLUMN_NOT_FOUND == ret) {
    ret = OB_SUCCESS;
    job_info_local.database_id_ = OB_INVALID_ID;
  }
  uint64_t func_type = 0;
  EXTRACT_INT_FIELD_MYSQL(result, "func_type", func_type, uint64_t);
  if (OB_ERR_NULL_VALUE == ret || OB_ERR_COLUMN_NOT_FOUND == ret) {
    ret = OB_SUCCESS;
  }
  job_info_local.func_type_ = static_cast<ObDBMSSchedFuncType>(func_type);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "lowner", job_info_local.lowner_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "powner", job_info_local.powner_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "cowner", job_info_local.cowner_);

#define EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, col_name, v)   \
do {                                                                  \
  ObObj obj;                                                          \
  OZ ((result).get_obj(col_name, obj));                               \
  if (OB_SUCC(ret)) {                                                 \
    if (obj.is_null()) {                                              \
      v = static_cast<int64_t>(0);                                    \
    } else {                                                          \
      OZ (obj.get_timestamp(v));                                      \
    }                                                                 \
  } else if (OB_ERR_COLUMN_NOT_FOUND == ret) {                        \
    ret = OB_SUCCESS;                                                 \
    v = static_cast<int64_t>(0);                                      \
  }                                                                   \
} while (false)

#define EXTRACT_NUMBER_FIELD_MYSQL_SKIP_RET(result, col_name, v)      \
do {                                                                  \
  common::number::ObNumber nmb_val;                                   \
  OZ ((result).get_number(col_name, nmb_val));                        \
  if (OB_ERR_NULL_VALUE == ret || OB_ERR_COLUMN_NOT_FOUND == ret) {   \
    ret = OB_SUCCESS;                                                 \
    v = static_cast<int64_t>(0);                                     \
  } else if (OB_SUCCESS == ret) {                                     \
    OZ (nmb_val.extract_valid_int64_with_trunc(v));                  \
  }                                                                   \
} while (false)

  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "gmt_modified", job_info_local.last_modify_);
  //lowner not used
  //powner not used
  //cowner not used
  //last_modify not used
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "last_date", job_info_local.last_date_);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "this_date", job_info_local.this_date_);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "next_date", job_info_local.next_date_);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "total", job_info_local.total_, uint64_t);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "start_date", job_info_local.start_date_);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "end_date", job_info_local.end_date_);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "this_exec_date", job_info_local.this_exec_date_);

#undef EXTRACT_NUMBER_FIELD_MYSQL_SKIP_RET
#undef EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET

  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "interval#", job_info_local.interval_);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "failures", job_info_local.failures_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "flag", job_info_local.flag_, uint64_t);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "what", job_info_local.what_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "nlsenv", job_info_local.nlsenv_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "charenv", job_info_local.charenv_);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "scheduler_flags", job_info_local.scheduler_flags_, uint64_t);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "exec_env", job_info_local.exec_env_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "job_name", job_info_local.job_name_);
  //job_style not used
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "job_class", job_info_local.job_class_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "program_name", job_info_local.program_name_);
  //job_type not used
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "job_action", job_info_local.job_action_);
  //number_of_argument not used
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "repeat_interval", job_info_local.repeat_interval_);
  EXTRACT_BOOL_FIELD_MYSQL_SKIP_RET(result, "enabled", job_info_local.enabled_);
  EXTRACT_BOOL_FIELD_MYSQL_SKIP_RET(result, "auto_drop", job_info_local.auto_drop_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "state", job_info_local.state_);
  //run_count not used
  //retry_count not used
  //last_run_duration not used
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "interval_ts", job_info_local.interval_ts_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "max_run_duration", job_info_local.max_run_duration_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "max_failures", job_info_local.max_failures_, uint64_t);
  //comments not used
  //credential_name not used
  //destination_name not used
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "this_exec_addr", job_info_local.this_exec_addr_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "this_exec_trace_id", job_info_local.this_exec_trace_id_);
  OZ (job_info.deep_copy(allocator, job_info_local));
  return ret;
}

int ObDBMSSchedTableOperator::get_dbms_sched_job_is_killed(const ObDBMSSchedJobInfo &job_info, bool &is_killed)
{
  int ret = OB_SUCCESS;
  is_killed = false;
  ObArenaAllocator allocator("SchedStateTmp");
  ObDBMSSchedJobInfo update_job_info;
  OZ(get_dbms_sched_job_info(job_info.job_, job_info.job_name_, allocator, update_job_info));
  if (OB_SUCC(ret) && update_job_info.is_killed()) {
    is_killed = true;
  }
  return ret;
}

int ObDBMSSchedTableOperator::get_dbms_sched_job_info(
  uint64_t job_id, const common::ObString &job_name,
  ObIAllocator &allocator, ObDBMSSchedJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_id != OB_INVALID_ID));

  if (!job_name.empty()) {
    OZ (sql.append_fmt("select * from %s where job_name = \'%.*s\' and job = %ld",
        OB_ALL_SCHEDULER_JOB_TNAME,
        job_name.length(),
        job_name.ptr(),
        job_id));
  } else {
    OZ (sql.append_fmt("select * from %s where job = %ld",
        OB_ALL_SCHEDULER_JOB_TNAME,
        job_id));
  }


  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
        LOG_WARN("execute query failed", K(ret), K(sql), K(job_id));
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get result", K(ret), K(job_id));
      } else {
        if (OB_SUCCESS == (ret = result.get_result()->next())) {
          OZ (extract_info(*(result.get_result()), allocator, job_info));
          if (OB_SUCC(ret)) {
            int tmp_ret = result.get_result()->next();
            if (OB_SUCCESS == tmp_ret) {
              ret = OB_ERR_UNEXPECTED;
              LOG_ERROR("got more than one row for dbms sched job!", K(ret), K(job_id));
            } else if (tmp_ret != OB_ITER_END) {
              ret = tmp_ret;
              LOG_ERROR("got next row for dbms sched job failed", K(ret), K(job_id));
            }
          }
        } else if (OB_ITER_END == ret) {
          LOG_WARN("job not exists, may delete alreay!", K(ret), K(job_id));
        } else {
          LOG_WARN("failed to get next", K(ret), K(job_id));
        }
      }
    }
  }
  return ret;
}

int ObDBMSSchedTableOperator::get_dbms_sched_job_infos_in_runtime(
  ObIAllocator &allocator, ObIArray<ObDBMSSchedJobInfo> &job_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;

  CK (OB_NOT_NULL(sql_proxy_));

  OZ (sql.append_fmt("select * from %s where job > 0 and job_name != \'%s\' and (state is NULL or state != \'%s\')",
      OB_ALL_SCHEDULER_JOB_TNAME,
      "__dummy_guard",
      "COMPLETED"));

  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
        LOG_WARN("execute query failed", K(ret), K(sql));
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get result failed", K(ret), K(sql));
      } else {
        do {
          if (OB_FAIL(result.get_result()->next())) {
            LOG_INFO("failed to get result", K(ret));
          } else {
            ObDBMSSchedJobInfo job_info;
            OZ (extract_info(*(result.get_result()), allocator, job_info));
            OZ (job_infos.push_back(job_info));
          }
        } while (OB_SUCC(ret));
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
    }
  }

  return ret;
}

int ObDBMSSchedTableOperator::extract_job_class_info(
  sqlclient::ObMySQLResult &result,
  ObIAllocator &allocator, ObDBMSSchedJobClassInfo &job_class_info)
{
  int ret = OB_SUCCESS;
  ObDBMSSchedJobClassInfo job_class_info_local;

  
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "job_class_name", job_class_info_local.job_class_name_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "service", job_class_info_local.service_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "logging_level", job_class_info_local.logging_level_);
  EXTRACT_VARCHAR_FIELD_MYSQL_SKIP_RET(result, "comments", job_class_info_local.comments_);
  EXTRACT_NUMBER_FIELD_MYSQL(result, log_history, job_class_info_local.log_history_);
  if (OB_ERR_NULL_VALUE == ret || OB_ERR_COLUMN_NOT_FOUND == ret) {
    ret = OB_SUCCESS;
  }
  OZ (job_class_info.deep_copy(allocator, job_class_info_local));

  return ret;
}

int ObDBMSSchedTableOperator::get_dbms_sched_job_class_info(
  const common::ObString job_class_name,
  common::ObIAllocator &allocator, ObDBMSSchedJobClassInfo &job_class_info) {
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(!job_class_name.empty()));
  OZ (sql.append_fmt("select * from %s where job_class_name = \'%.*s\'",
      OB_ALL_SCHEDULER_JOB_CLASS_TNAME, job_class_name.length(), job_class_name.ptr()));
  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
        LOG_WARN("execute query failed", K(ret), K(sql), K(job_class_name));
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get result failed", K(ret), K(sql), K(job_class_name));
      } else {
        if (OB_SUCCESS == (ret = result.get_result()->next())) {
          OZ (extract_job_class_info(*(result.get_result()), allocator, job_class_info));
          if (OB_SUCC(ret)) {
            int tmp_ret = result.get_result()->next();
            if (OB_SUCCESS == tmp_ret) {
              ret = OB_ERR_UNEXPECTED;
              LOG_ERROR("got more than one row for dbms sched job class!", K(ret), K(job_class_name));
            } else if (tmp_ret != OB_ITER_END) {
              ret = tmp_ret;
              LOG_ERROR("got next row for dbms sched job class failed", K(ret), K(job_class_name));
            }
          }
        } else if (OB_ITER_END == ret) {
          LOG_INFO("job_class_name not exists, may delete alreay!", K(ret), K(job_class_name));
          ret = OB_SUCCESS; // job not exist, do nothing ...
        } else {
          LOG_WARN("failed to get next", K(ret), K(job_class_name));
        }
      }
    }
  }
  return ret;
}

int ObDBMSSchedTableOperator::get_dbms_sched_job_class_infos_in_runtime(
  ObIAllocator &allocator, ObIArray<ObDBMSSchedJobClassInfo> &job_class_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  CK (OB_NOT_NULL(sql_proxy_));

  OZ (sql.append_fmt("select * from %s order by log_history desc",
      OB_ALL_SCHEDULER_JOB_CLASS_TNAME));

  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
        LOG_WARN("execute query failed", K(ret), K(sql));
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get result failed", K(ret), K(sql));
      } else {
        do {
          if (OB_FAIL(result.get_result()->next())) {
            LOG_INFO("failed to get result", K(ret));
          } else {
            ObDBMSSchedJobClassInfo job_class_info;
            OZ (extract_job_class_info(*(result.get_result()), allocator, job_class_info));
            OZ (job_class_infos.push_back(job_class_info));
          }
        } while (OB_SUCC(ret));
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
    }
  }

  return ret;
}

} // end for namespace dbms_scheduler
} // end for namespace oceanbase
