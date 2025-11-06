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

const char *ObLoadDataUtils::NULL_STRING = "NULL";
const char ObLoadDataUtils::NULL_VALUE_FLAG = '\xff';

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


int ObLoadDataUtils::append_values_in_remote_process(int64_t table_column_count,
                                                     int64_t append_values_count,
                                                     const ObExprValueBitSet &expr_bitset,
                                                     const ObIArray<ObString> &insert_values,
                                                     ObSqlString &insertsql,
                                                     ObDataBuffer &data_buffer,
                                                     int64_t skipped_row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!insertsql.is_valid())
      || OB_UNLIKELY(append_values_count + skipped_row_count * table_column_count > insert_values.count())
      || OB_UNLIKELY(0 == table_column_count)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("insert values are invalid", K(ret), K(insertsql), K(append_values_count), K(insert_values.count()));
  } else {
    int64_t row_count = append_values_count/table_column_count;
    if (OB_FAIL(insertsql.append(" values "))) {
      LOG_WARN("append failed", K(ret), K(insertsql.length()));
    }
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_count; ++row_idx) {
      if (OB_FAIL(append_values_for_one_row(table_column_count,
                                            expr_bitset,
                                            insert_values,
                                            insertsql,
                                            data_buffer,
                                            row_idx + skipped_row_count))) {
        LOG_WARN("append values for one row in remote process failed", K(ret));
      } else {
        if (row_idx + 1 != row_count) {
          if (OB_FAIL(insertsql.append(","))) {
            LOG_WARN("append failed", K(ret), K(insertsql.length()));
          }
        }
      }
    }
  }
  return ret;
}


int ObLoadDataUtils::append_values_for_one_row(const int64_t table_column_count,
                                               const ObExprValueBitSet &expr_value_bitset,
                                               const ObIArray<ObString> &insert_values,
                                               ObSqlString &insertsql,
                                               ObDataBuffer &data_buffer,
                                               const int64_t skipped_row_count)
{
  int ret = OB_SUCCESS;
  int64_t value_offset = skipped_row_count * table_column_count;

  if (OB_UNLIKELY(skipped_row_count * table_column_count + table_column_count > insert_values.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(skipped_row_count), K(table_column_count), K(insert_values.count()));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(insertsql.append("("))) {
      LOG_WARN("append failed", K(ret), K(insertsql.length()));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < table_column_count; ++i) {
    const ObString &value = insert_values.at(i + value_offset);
    bool is_expr_value = expr_value_bitset.has_member(i);
    ObString cur_column_str;
    if (!is_expr_value) {
      cur_column_str = escape_quotation(value, data_buffer);
      remove_last_slash(cur_column_str);
    } else {
      cur_column_str = value;
    }
    if (i != 0) {
      if (OB_FAIL(insertsql.append(","))) {
        LOG_WARN("append failed", K(ret), K(insertsql.length()));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(append_value(cur_column_str, insertsql, is_expr_value))) {
        LOG_WARN("append failed", K(ret), K(insertsql.length()), K(cur_column_str));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(insertsql.append(")"))) {
      LOG_WARN("append failed", K(ret), K(insertsql.length()));
    }
  }
  return ret;
}

int ObLoadDataUtils::append_value(const ObString &cur_column_str, ObSqlString &sqlstr_values, bool is_expr_value)
{
  int ret = OB_SUCCESS;
  if (!is_expr_value) {
    if (is_null_field(cur_column_str)) {
      if (OB_FAIL(sqlstr_values.append(NULL_STRING))) {
        LOG_WARN("append failed", K(ret));
      }
    } else {
      if (OB_FAIL(sqlstr_values.append_fmt("'%.*s'", cur_column_str.length(), cur_column_str.ptr()))) {
        LOG_WARN("append failed", K(ret));
      }
    }
  } else {
    if (OB_FAIL(sqlstr_values.append(cur_column_str))) {
      LOG_WARN("append failed", K(ret));
    }
  }
  return ret;
}




ObString ObLoadDataUtils::escape_quotation(const ObString &value, ObDataBuffer &data_buf)
{
  char *buf = data_buf.get_data();
  ObString result;

  if (OB_ISNULL(buf)) {
    LOG_WARN_RET(OB_NOT_INIT, "data buf is not inited");
  } else {
    //check if escape is needed
    bool need_escape = false;
    const char *src = value.ptr();
    int64_t str_len = value.length();
    ObLoadEscapeSM escape_sm;
    escape_sm.set_escape_char(ObLoadEscapeSM::ESCAPE_CHAR_MYSQL);
    for (int64_t i = 0; !need_escape && i < str_len; ++i) {
      if (*(src + i) == '\'' && !escape_sm.is_escaping()) {
        need_escape = true;
      }
      escape_sm.shift_by_input(*(src + i));
    }

    if (!need_escape) {
      result = value;
    } else {
      int64_t pos = 0;
      escape_sm.reset();
      for (int64_t i = 0; i < str_len && pos < data_buf.get_capacity(); ++i) {
        if (*(src + i) == '\'' && !escape_sm.is_escaping()) {
          buf[pos++] = static_cast<char>(ObLoadEscapeSM::ESCAPE_CHAR_MYSQL);
        }
        buf[pos++] = src[i];
        escape_sm.shift_by_input(*(src + i));
      }
      if (OB_UNLIKELY(pos >= data_buf.get_capacity())) {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "data is too long"); //this should never happened, just for protection
        result.reset();
      } else {
        result.assign_ptr(buf, static_cast<int32_t>(pos));
      }
    }
  }

  return result;
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
  int64_t append = 0;
  int64_t gather_optimizer_statistics = 0;
  need_opt_stat_gather = false;
  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", KR(ret));
  } else if (OB_FAIL(session->get_sys_variable(share::SYS_VAR__OPTIMIZER_GATHER_STATS_ON_LOAD, obj))) {
    LOG_WARN("fail to get sys variable", K(ret));
  } else if (OB_FAIL(hint.get_value(ObLoadDataHint::APPEND, append))) {
    LOG_WARN("fail to get value of APPEND", K(ret));
  } else if (OB_FAIL(hint.get_value(ObLoadDataHint::GATHER_OPTIMIZER_STATISTICS, gather_optimizer_statistics))) {
    LOG_WARN("fail to get value of APPEND", K(ret));
  } else if (((append != 0) || (gather_optimizer_statistics != 0)) && obj.get_bool()) {
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
  ObMemAttr attr(OB_SERVER_TENANT_ID, ObModIds::OB_SQL_LOAD_DATA);
  SET_USE_500(attr);
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

OB_SERIALIZE_MEMBER(ObLoadTaskStatus, task_status_);

OB_SERIALIZE_MEMBER(ObLoadDataGID, id);


}
}
