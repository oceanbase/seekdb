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

#define USING_LOG_PREFIX SHARE

#include "share/storage/ob_rootservice_job_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObRootServiceJobTableStorage::ObRootServiceJobTableStorage()
  : pool_(nullptr)
{
}

ObRootServiceJobTableStorage::~ObRootServiceJobTableStorage()
{
}

int ObRootServiceJobTableStorage::init(ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_ = pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else if (OB_FAIL(create_table_if_not_exists_())) {
    LOG_WARN("failed to create table", K(ret));
  }
  if (OB_FAIL(ret)) {
    pool_ = NULL;
  }
  return ret;
}

int ObRootServiceJobTableStorage::create_table_if_not_exists_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("pool not set", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_ROOTSERVICE_JOB, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObRootServiceJobTableStorage::create_job(const ObRootServiceJobEntry &entry)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *insert_sql =
      "INSERT INTO __all_rootservice_job "
      "(gmt_create, gmt_modified, job_id, job_type, job_status, result_code) "
      "VALUES (?, ?, ?, ?, ?, ?);";

    if (OB_SUCC(ret)) {
      const int64_t now = ObTimeUtility::current_time();
      const int64_t gmt_create = (entry.gmt_create_ > 0) ? entry.gmt_create_ : now;
      const int64_t gmt_modified = (entry.gmt_modified_ > 0) ? entry.gmt_modified_ : gmt_create;
      auto binder = [&](ObSQLiteBinder &b) -> int {
        b.bind_int64(gmt_create);
        b.bind_int64(gmt_modified);
        b.bind_int64(entry.job_id_);
        b.bind_text(entry.job_type_.empty() ? "" : entry.job_type_.ptr(), entry.job_type_.length());
        b.bind_text(entry.job_status_.empty() ? "" : entry.job_status_.ptr(), entry.job_status_.length());
        b.bind_int64(entry.result_code_);
        return OB_SUCCESS;
      };
      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->execute(insert_sql, binder))) {
        LOG_WARN("failed to execute insert", K(ret));
      }
    }
  }
  LOG_INFO("finish create job", KR(ret));
  return ret;
}

int ObRootServiceJobTableStorage::complete_job(
    const int64_t job_id,
    const common::ObString &job_status,
    const int64_t result_code)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (job_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid job id", K(ret), K(job_id));
  } else {
    const char *update_sql =
      "UPDATE __all_rootservice_job "
      "SET gmt_modified = ?, job_status = ?, result_code = ? "
      "WHERE job_id = ?;";
    const int64_t gmt_modified = ObTimeUtility::current_time();
    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(gmt_modified);
      b.bind_text(job_status.empty() ? "" : job_status.ptr(), job_status.length());
      b.bind_int64(result_code);
      b.bind_int64(job_id);
      return OB_SUCCESS;
    };
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(update_sql, binder))) {
      LOG_WARN("failed to execute update", K(ret));
    }
  }
  LOG_INFO("finish complete job", KR(ret), K(job_id), K(job_status), K(result_code));
  return ret;
}

int ObRootServiceJobTableStorage::get_max_job_id(int64_t &max_job_id)
{
  int ret = OB_SUCCESS;
  max_job_id = -1;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    const char *select_sql =
      "SELECT max(job_id) as max_job_id FROM __all_rootservice_job";
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      max_job_id = reader.get_int64();
      return OB_ITER_END;
    };
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, nullptr, row_processor))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        max_job_id = 0;
      } else {
        LOG_WARN("failed to query inprogress job", K(ret));
      }
    }
  }
  LOG_INFO("finish get max job id and row count", KR(ret), K(max_job_id));
  return ret;
}

int ObRootServiceJobTableStorage::get_job_count(const common::ObString &job_type, int64_t &job_count)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(job_type.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    const char *select_sql =
      "SELECT count(*) as job_count FROM __all_rootservice_job "
      "WHERE job_type = ? ";
    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_text(job_type.ptr(), job_type.length());
      return OB_SUCCESS;
    };
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      job_count = reader.get_int64();
      return OB_ITER_END;
    };
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      LOG_WARN("failed to query inprogress job", K(ret));
    }
  }
  LOG_INFO("finish get job count", KR(ret), K(job_type), K(job_count));
  return ret;
}

int ObRootServiceJobTableStorage::find_job(const common::ObString &job_type, int64_t &job_id)
{
  int ret = OB_SUCCESS;
  job_id = -1;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(job_type.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    const char *select_sql =
      "SELECT job_id FROM __all_rootservice_job "
      "WHERE job_status = 'INPROGRESS' AND job_type = ? ";
    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_text(job_type.ptr(), job_type.length());
      return OB_SUCCESS;
    };
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      job_id = reader.get_int64();
      return OB_ITER_END;
    };
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      LOG_WARN("failed to query inprogress job", K(ret));
    } else if (OB_UNLIKELY(job_id < 1)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("find an invalid job", KR(ret), K(job_id));
    }
  }
  LOG_INFO("finish find job", KR(ret), K(job_id), K(job_type));
  return ret;
}

} // namespace share
} // namespace oceanbase
