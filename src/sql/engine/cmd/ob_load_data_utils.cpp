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

#define USING_LOG_PREFIX  SQL_ENG

#include "sql/engine/cmd/ob_load_data_utils.h"

namespace oceanbase {
using namespace common;
namespace sql {

const char ObLoadDataUtils::NULL_VALUE_FLAG = '\xff';

int ObParallelTaskController::init(int64_t max_parallelism)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(max_parallelism <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(vacant_cond_.init(common::ObWaitEventIds::DEFAULT_COND_WAIT))) {
    LOG_WARN("init vacant condition failed", K(ret));
  } else {
    max_parallelism_ = max_parallelism;
  }
  return ret;
}

int ObParallelTaskController::on_next_task()
{
  int ret = OB_SUCCESS;
  ObThreadCondGuard guard(vacant_cond_);
  if (ATOMIC_AAF(&processing_cnt_, 1) > max_parallelism_) {
    ret = vacant_cond_.wait();
  }
  return ret;
}

int ObParallelTaskController::on_task_finished()
{
  int ret = OB_SUCCESS;
  if (max_parallelism_ == ATOMIC_AAF(&processing_cnt_, -1)) {
    ObThreadCondGuard guard(vacant_cond_);
    ret = vacant_cond_.signal();
  }
  return ret;
}

void ObParallelTaskController::wait_all_task_finish(const char *task_name, int64_t until_ts)
{
  int64_t wait_duration_ms = 0;
  const int64_t begin_ts = ObTimeUtil::current_time();
  bool is_too_long = false;
  while (get_processing_task_cnt() > 0) {
    ob_usleep(10 * 1000);
    wait_duration_ms += 10;
    if (0 == wait_duration_ms % 1000 && ObTimeUtil::current_time() > until_ts) {
      LOG_ERROR_RET(OB_TIMEOUT, "waiting local load data task exceeded deadline",
                    K(task_name), K(begin_ts), K(until_ts));
    }
    if (!is_too_long && wait_duration_ms > 10 * 1000) {
      is_too_long = true;
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "waiting local load data task too long",
                   K(task_name), "processing_count", get_processing_task_cnt(),
                   K(wait_duration_ms), K(until_ts));
    }
  }
}

int ObLoadDataUtils::build_insert_sql_string_head(ObLoadDupActionType insert_mode,
                                                  const ObString &table_name,
                                                  const ObIArray<ObString> &insert_keys,
                                                  ObSqlString &insertsql_keys,
                                                  bool need_gather_opt_stat)
{
  int ret = OB_SUCCESS;
  static const char *replace_stmt = "replace into ";
  static const char *insert_stmt = "insert into ";
  static const char *insert_stmt_gather_opt_stat = "insert /*+GATHER_OPTIMIZER_STATISTICS*/ into ";
  static const char *insert_ignore_stmt = "insert ignore into ";

  const char *stmt_head = NULL;
  switch (insert_mode) {
  case ObLoadDupActionType::LOAD_REPLACE:
    stmt_head = replace_stmt;
    break;
  case ObLoadDupActionType::LOAD_IGNORE:
    stmt_head = insert_ignore_stmt;
    break;
  case ObLoadDupActionType::LOAD_STOP_ON_DUP: {
    if (need_gather_opt_stat) {
      stmt_head = insert_stmt_gather_opt_stat;
    } else {
      stmt_head = insert_stmt;
    }
    break;
  }
  default:
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not suppport insert mode", K(insert_mode));
  }

  insertsql_keys.reuse();
  OZ (insertsql_keys.reserve(OB_MEDIUM_SQL_LENGTH));
  OZ (insertsql_keys.assign(stmt_head));
  OZ (insertsql_keys.append(table_name));
  OZ (insertsql_keys.append("("));
  for (int64_t i = 0; i < insert_keys.count(); ++i) {
    if (i != 0) {
      OZ (insertsql_keys.append(","));
    }
    OZ (insertsql_keys.append_fmt("`%.*s`",
                                  insert_keys.at(i).length(), insert_keys.at(i).ptr()));
  }
  OZ (insertsql_keys.append(")"));

  if (OB_FAIL(ret)) {
    LOG_WARN("append failed", K(ret), K(insertsql_keys.length()));
  }

  return ret;
}


int ObLoadDataUtils::check_session_status(ObSQLSessionInfo &session, int64_t reserved_us) {
  int ret = OB_SUCCESS;
  bool is_timeout = false;
  int64_t worker_query_timeout = THIS_WORKER.get_timeout_ts();
  int64_t current_time = ObTimeUtil::current_time();

  if (OB_FAIL(session.is_timeout(is_timeout))) {
    LOG_WARN("get session timeout info failed", K(ret));
  } else if (OB_UNLIKELY(worker_query_timeout < current_time + reserved_us)) {
    ret = OB_TIMEOUT;
    LOG_WARN("query is timeout", K(ret));
  } else if (OB_UNLIKELY(is_timeout)) {
    ret = OB_TIMEOUT;
    LOG_WARN("session is timeout", K(ret));
  } else if (OB_FAIL(session.check_session_status())) {
    LOG_WARN("session's state is not OB_SUCCESS", K(ret));
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("LOAD DATA timeout", K(ret), K(session.get_server_sid()), K(worker_query_timeout), K(current_time), K(reserved_us));
  }
  return ret;
}

int ObLoadDataUtils::check_need_opt_stat_gather(ObExecContext &ctx,
                                                ObLoadDataStmt &load_stmt,
                                                bool &need_opt_stat_gather)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = nullptr;
  const ObLoadDataHint &hint = load_stmt.get_hints();
  ObObj obj;
  int64_t gather_optimizer_statistics = 0;
  need_opt_stat_gather = false;
  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", KR(ret));
  } else if (OB_FAIL(session->get_sys_variable(share::SYS_VAR__OPTIMIZER_GATHER_STATS_ON_LOAD, obj))) {
    LOG_WARN("fail to get sys variable", K(ret));
  } else if (OB_FAIL(hint.get_value(ObLoadDataHint::GATHER_OPTIMIZER_STATISTICS, gather_optimizer_statistics))) {
    LOG_WARN("fail to get GATHER_OPTIMIZER_STATISTICS hint", K(ret));
  } else if (gather_optimizer_statistics != 0 && obj.get_bool()) {
    need_opt_stat_gather = true;
  }
  return ret;
}

/////////////////

ObGetAllJobStatusOp::ObGetAllJobStatusOp()
    : job_status_array_(),
      current_job_index_(0)
{
}

ObGetAllJobStatusOp::~ObGetAllJobStatusOp()
{
  reset();
}

void ObGetAllJobStatusOp::reset()
{
  ObLoadDataStat *job_status;
  for (int64_t i = 0; i < job_status_array_.count(); ++i) {
    job_status = job_status_array_.at(i);
    job_status->release();
  }
  job_status_array_.reset();
  current_job_index_ = 0;
}

int ObGetAllJobStatusOp::operator()(common::hash::HashMapPair<ObLoadDataGID, ObLoadDataStat *> &entry)
{
  int ret = OB_SUCCESS;
  entry.second->aquire();
  if (OB_FAIL(job_status_array_.push_back(entry.second))) {
    entry.second->release();
    LOG_WARN("push_back ObLoadDataStat failed", K(ret));
  }
  return ret;
}

int ObGetAllJobStatusOp::get_next_job_status(ObLoadDataStat *&job_status)
{
  int ret = OB_SUCCESS;
  if (current_job_index_ >= job_status_array_.count()) {
    ret = OB_ITER_END;
  } else {
    job_status = job_status_array_.at(current_job_index_++);
  }
  return ret;
}

int ObGlobalLoadDataStatMap::init()
{
  int ret = OB_SUCCESS;
  ObMemAttr attr(ObModIds::OB_SQL_LOAD_DATA);
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(map_.create(bucket_num,
                                 attr,
                                 attr))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create hash table failed", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObGlobalLoadDataStatMap::register_job(const ObLoadDataGID &id, ObLoadDataStat *job_status)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  OZ (map_.set_refactored(id, job_status));
  return ret;
}

int ObGlobalLoadDataStatMap::unregister_job(const ObLoadDataGID &id, ObLoadDataStat *&job_status)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  OZ (map_.erase_refactored(id, &job_status));
  return ret;
}

int ObGlobalLoadDataStatMap::get_job_status(const ObLoadDataGID &id, ObLoadDataStat *&job_status)
{
  int ret = OB_SUCCESS;
  auto get_and_add_ref = [&](hash::HashMapPair<ObLoadDataGID, ObLoadDataStat*> &entry) -> void
  {
    entry.second->aquire();
    job_status = entry.second;
  };
  OZ (map_.read_atomic(id, get_and_add_ref));
  return ret;
}

int ObGlobalLoadDataStatMap::get_all_job_status(ObGetAllJobStatusOp &job_status_op)
{
  int ret = OB_SUCCESS;
  OZ (map_.foreach_refactored(job_status_op));
  return ret;
}


ObGlobalLoadDataStatMap *ObGlobalLoadDataStatMap::getInstance()
{
  return instance_;
}

ObGlobalLoadDataStatMap *ObGlobalLoadDataStatMap::instance_ = new ObGlobalLoadDataStatMap();

volatile int64_t ObLoadDataGID::GlobalLoadDataID = 0;


}
}
