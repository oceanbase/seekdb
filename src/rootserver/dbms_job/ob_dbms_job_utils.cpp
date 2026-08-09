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

#include "ob_dbms_job_utils.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/ob_share_util.h"

#include "share/ob_server_struct.h"


namespace oceanbase
{

using namespace common;
using namespace share;
using namespace share::schema;
using namespace sqlclient;

namespace dbms_job
{
int ObDBMSJobInfo::deep_copy(ObIAllocator &allocator, const ObDBMSJobInfo &other)
{
  int ret = OB_SUCCESS;
  job_ = other.job_;
  last_modify_ = other.last_modify_;
  last_date_ = other.last_date_;
  this_date_ = other.this_date_;
  next_date_ = other.next_date_;
  total_ = other.total_;
  failures_ = other.failures_;
  flag_ = other.flag_;
  scheduler_flags_ = other.scheduler_flags_;

  OZ (ob_write_string(allocator, other.lowner_, lowner_));
  OZ (ob_write_string(allocator, other.powner_, powner_));
  OZ (ob_write_string(allocator, other.cowner_, cowner_));

  OZ (ob_write_string(allocator, other.interval_, interval_));

  OZ (ob_write_string(allocator, other.what_, what_));
  OZ (ob_write_string(allocator, other.nlsenv_, nlsenv_));
  OZ (ob_write_string(allocator, other.charenv_, charenv_));
  OZ (ob_write_string(allocator, other.exec_env_, exec_env_));
  return ret;
}

int ObDBMSJobUtils::update_for_start(
  ObDBMSJobInfo &job_info, bool update_nextdate)
{
  int ret = OB_SUCCESS;

  ObDMLSqlSplicer dml;
  ObSqlString sql;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();
  int64_t delay = 0;
  int64_t dummy_execute_at = 0;

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));

  OZ (calc_execute_at(
    job_info, (update_nextdate ? job_info.next_date_ : dummy_execute_at), delay, true));

  OX (job_info.this_date_ = now);
  OZ (dml.add_gmt_modified(now));
  OZ (dml.add_pk_column("job", job_info.job_));
  OZ (dml.add_time_column("this_date", job_info.this_date_));
  OZ (dml.splice_update_sql(OB_ALL_JOB_TNAME, sql));
  OZ (sql_proxy_->write(sql.ptr(), affected_rows));

  return ret;
}

int ObDBMSJobUtils::update_nextdate(
  ObDBMSJobInfo &job_info)
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
  OZ (dml.add_time_column("next_date", job_info.next_date_));
  OZ (dml.splice_update_sql(OB_ALL_JOB_TNAME, sql));
  OZ (sql_proxy_->write(sql.ptr(), affected_rows));

  return ret;
}


int ObDBMSJobUtils::update_for_end(
  ObDBMSJobInfo &job_info, int err, const ObString &errmsg)
{
  int ret = OB_SUCCESS;

  ObDMLSqlSplicer dml1;
  ObSqlString sql1;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();

  UNUSED(err);
  UNUSED(errmsg);

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_info.job_ != OB_INVALID_ID));

  OX (job_info.failures_ = errmsg.empty() ? 0 : (job_info.failures_ + 1));
  // when dbms_job run end, if failures > 16 then set broken flag else clear it.
  OX (job_info.flag_ = job_info.failures_ > 15 ? (job_info.flag_ | 0x1) : (job_info.flag_ & 0xfffffffffffffffE));
  if (OB_SUCC(ret) && ((job_info.flag_ & 0x1) != 0)) {
    job_info.next_date_ = 64060560000000000; // 4000-01-01
  }
  CK (job_info.this_date_ > 0);
  OX (job_info.total_ += (now - job_info.this_date_));
  OZ (dml1.add_gmt_modified(now));
  OZ (dml1.add_pk_column("job", job_info.job_));
  OZ (dml1.add_column(true, "this_date"));
  OZ (dml1.add_time_column("last_date", job_info.this_date_));
  OZ (dml1.add_time_column("next_date", job_info.next_date_));
  OZ (dml1.add_column("failures", job_info.failures_));
  OZ (dml1.add_column("flag", job_info.failures_ > 16 ? 1 : job_info.flag_));
  OZ (dml1.add_column("total", job_info.total_));
  OZ (dml1.splice_update_sql(OB_ALL_JOB_TNAME, sql1));

  OZ (sql_proxy_->write(sql1.ptr(), affected_rows), sql1);

  return ret;
}

int ObDBMSJobUtils::check_job_can_running(bool &can_running)
{
  int ret = OB_SUCCESS;
  uint64_t job_queue_processor = 0;
  uint64_t job_running_cnt = 0;
  ObSqlString sql;
  OX (can_running = false);
  CK (true);
  OX (job_queue_processor = GCONF.job_queue_processes);
  // found current running job count
  OZ (sql.append("select count(*) from __all_job where this_date is not null"));

  // Jobs can run only on the primary server.
  bool is_primary = false;
  if (FAILEDx(ObShareUtil::is_primary_server(is_primary))) {
    LOG_WARN("fail to check whether server is primary", KR(ret));
  } else if (is_primary && job_queue_processor > 0) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
      } else if (OB_NOT_NULL(result.get_result())) {
        if (OB_SUCCESS == (ret = result.get_result()->next())) {
          int64_t int_value = 0;
          if (OB_FAIL(result.get_result()->get_int(static_cast<const int64_t>(0), int_value))) {
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
  return ret;
}

int ObDBMSJobUtils::extract_info(
  sqlclient::ObMySQLResult &result, ObIAllocator &allocator, ObDBMSJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObDBMSJobInfo job_info_local;

  
  EXTRACT_INT_FIELD_MYSQL(result, "job", job_info_local.job_, uint64_t);
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
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "last_date", job_info_local.last_date_);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "this_date", job_info_local.this_date_);
  EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(result, "next_date", job_info_local.next_date_);
  EXTRACT_INT_FIELD_MYSQL_SKIP_RET(result, "total", job_info_local.total_, uint64_t);

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

  OZ (job_info.deep_copy(allocator, job_info_local));

  return ret;
}

int ObDBMSJobUtils::get_dbms_job_info(
  uint64_t job_id, ObIAllocator &allocator, ObDBMSJobInfo &job_info)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;

  CK (OB_NOT_NULL(sql_proxy_));
  CK (OB_LIKELY(job_id != OB_INVALID_ID));

  OZ (sql.append_fmt("select * from %s where job = %ld", OB_ALL_JOB_TNAME, job_id));

  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
      } else if (OB_NOT_NULL(result.get_result())) {
        if (OB_SUCCESS == (ret = result.get_result()->next())) {
          OZ (extract_info(*(result.get_result()), allocator, job_info));
          if (OB_SUCC(ret) && (result.get_result()->next()) != OB_ITER_END) {
            LOG_ERROR("got more than one row for dbms job!", K(ret), K(job_id));
            ret = OB_ERR_UNEXPECTED;
          }
        } else if (OB_ITER_END == ret) {
          LOG_INFO("job not exists, may delete alreay!", K(ret), K(job_id));
          ret = OB_SUCCESS; // job not exist, do nothing ...
        } else {
          LOG_WARN("failed to get next", K(ret), K(job_id));
        }
      }
    }
  }
  return ret;
}

int ObDBMSJobUtils::get_dbms_job_infos_in_runtime(
  ObIAllocator &allocator, ObIArray<ObDBMSJobInfo> &job_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;

  CK (OB_NOT_NULL(sql_proxy_));

  OZ (sql.append_fmt("select * from %s", OB_ALL_JOB_TNAME));

  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy_->read(result, sql.ptr()))) {
      } else if (OB_NOT_NULL(result.get_result())) {
        do {
          if (OB_FAIL(result.get_result()->next())) {
            if (ret != OB_ITER_END) {
              LOG_WARN("failed to get result from result", K(ret));
            }
          } else {
            ObDBMSJobInfo job_info;
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

int ObDBMSJobUtils::calc_execute_at(
  ObDBMSJobInfo &job_info, int64_t &execute_at, int64_t &delay, bool ignore_nextdate)
{
  int ret = OB_SUCCESS;

  ObString &interval = job_info.get_interval();

  const int64_t now = ObTimeUtility::current_time();
  int64_t last_sub_next =
    (job_info.get_last_modify() / 1000 / 1000) - (job_info.get_next_date() / 1000/ 1000);
  if (job_info.get_next_date() != 0 && (!ignore_nextdate || job_info.get_next_date() != execute_at)) {
    if (job_info.get_next_date() > now) {
      execute_at = job_info.get_next_date();
      delay = job_info.get_next_date() - now;
    } else if (last_sub_next < 5 && last_sub_next >= -5) {
      execute_at = now;
      delay = 0;
    } else {
      delay = -1;
    }
  } else {
    delay = -1;
  }

  if (delay < 0 && !interval.empty()) {
    ObSqlString sql;
    ObMySQLProxy *inner_proxy = static_cast<ObMySQLProxy *>(sql_proxy_);
    // NOTE: we need utc timestamp.
    OZ (sql.append_fmt(
      "select to_date(to_char(sys_extract_utc(to_timestamp(to_char(sysdate, 'YYYY-MM-DD HH24:MI:SS'),"
               "'YYYY-MM-DD HH24:MI:SS')), 'YYYY-MM-DD HH24:MI:SS'), 'YYYY-MM-DD HH24:MI:SS'),"
      " to_date(to_char(sys_extract_utc(to_timestamp(to_char(%.*s, 'YYYY-MM-DD HH24:MI:SS'),"
                "'YYYY-MM-DD HH24:MI:SS')), 'YYYY-MM-DD HH24:MI:SS'), 'YYYY-MM-DD HH24:MI:SS') from dual;",
      interval.length(), interval.ptr()));
    if (OB_SUCC(ret)) {
      SMART_VAR(ObMySQLProxy::MySQLResult, result) {
        if (OB_FAIL(inner_proxy->read(result, sql.ptr()))) {
        } else if (OB_NOT_NULL(result.get_result())) {
          if (OB_FAIL(result.get_result()->next())) {
          } else {
            int64_t sysdate = 0;
            int64_t col_idx = 0;
            OZ (result.get_result()->get_datetime(col_idx, sysdate));
            if (OB_SUCC(ret)
                && OB_FAIL(result.get_result()->get_datetime(col_idx + 1, execute_at))) {
              if (OB_ERR_NULL_VALUE == ret) {
                ret = OB_SUCCESS;
                delay = -1;
              }
            }
            if (OB_FAIL(ret)) {
            } else if (job_info.get_next_date() > execute_at) {
              execute_at = job_info.get_next_date();
              delay = execute_at - sysdate;
            } else {
              delay = execute_at - sysdate;
            }
            if (OB_SUCC(ret)) {
              OX (job_info.next_date_ = execute_at);
              OZ (update_nextdate(job_info));
            }
          }
        }
      }
    }
  }

  return ret;
}

} // end for namespace dbms_job
} // end for namespace oceanbase
