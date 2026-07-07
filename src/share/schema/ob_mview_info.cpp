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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "share/schema/ob_mview_info.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/schema/ob_schema_utils.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace common;
using namespace sql;

ObMViewInfo::ObMViewInfo() { reset(); }

ObMViewInfo::ObMViewInfo(ObIAllocator *allocator) : ObSchema(allocator) { reset(); }

ObMViewInfo::ObMViewInfo(const ObMViewInfo &src_schema)
{
  reset();
  *this = src_schema;
}

ObMViewInfo::~ObMViewInfo() {}

ObMViewInfo &ObMViewInfo::operator=(const ObMViewInfo &src_schema)
{
  if (this != &src_schema) {
    reset();
    int &ret = error_ret_;
    
    mview_id_ = src_schema.mview_id_;
    build_mode_ = src_schema.build_mode_;
    refresh_mode_ = src_schema.refresh_mode_;
    refresh_method_ = src_schema.refresh_method_;
    refresh_start_ = src_schema.refresh_start_;
    last_refresh_scn_ = src_schema.last_refresh_scn_;
    last_refresh_type_ = src_schema.last_refresh_type_;
    last_refresh_date_ = src_schema.last_refresh_date_;
    last_refresh_time_ = src_schema.last_refresh_time_;
    schema_version_ = src_schema.schema_version_;
    refresh_dop_ = src_schema.refresh_dop_;
    data_sync_scn_ = src_schema.data_sync_scn_;
    is_synced_ = src_schema.is_synced_;
    nested_refresh_mode_ = src_schema.nested_refresh_mode_;
    if (OB_FAIL(deep_copy_str(src_schema.refresh_next_, refresh_next_))) {
      LOG_WARN("deep copy refresh next failed", KR(ret), K(src_schema.refresh_next_));
    } else if (OB_FAIL(deep_copy_str(src_schema.refresh_job_, refresh_job_))) {
      LOG_WARN("deep copy refresh job failed", KR(ret), K(src_schema.refresh_job_));
    } else if (OB_FAIL(deep_copy_str(src_schema.last_refresh_trace_id_, last_refresh_trace_id_))) {
      LOG_WARN("deep copy last refresh trace id failed", KR(ret),
               K(src_schema.last_refresh_trace_id_));
    }
  }
  return *this;
}

int ObMViewInfo::assign(const ObMViewInfo &other)
{
  int ret = OB_SUCCESS;
  this->operator=(other);
  ret = this->error_ret_;
  return ret;
}

bool ObMViewInfo::is_valid() const
{
  bool bret = false;
  if (OB_LIKELY(ObSchema::is_valid())) {
    bret = (OB_INVALID_ID != mview_id_ &&
            ObMViewBuildMode::MAX != build_mode_ && ObMVRefreshMode::MAX != refresh_mode_ &&
            ObMVRefreshMethod::MAX != refresh_method_ && OB_INVALID_VERSION != schema_version_ && 
            0 <= refresh_dop_);
  }
  return bret;
}

void ObMViewInfo::reset()
{
  
  mview_id_ = OB_INVALID_ID;
  build_mode_ = ObMViewBuildMode::MAX;
  refresh_mode_ = ObMVRefreshMode::MAX;
  refresh_method_ = ObMVRefreshMethod::MAX;
  refresh_start_ = OB_INVALID_TIMESTAMP;
  reset_string(refresh_next_);
  reset_string(refresh_job_);
  last_refresh_scn_ = OB_INVALID_SCN_VAL;
  last_refresh_type_ = ObMVRefreshType::MAX;
  last_refresh_date_ = OB_INVALID_TIMESTAMP;
  last_refresh_time_ = OB_INVALID_COUNT;
  reset_string(last_refresh_trace_id_);
  schema_version_ = OB_INVALID_VERSION;
  refresh_dop_ = 0;
  data_sync_scn_ = 0;
  is_synced_ = false;
  nested_refresh_mode_ = ObMVNestedRefreshMode::MAX;
  ObSchema::reset();
}

int64_t ObMViewInfo::get_convert_size() const
{
  int64_t len = 0;
  len += static_cast<int64_t>(sizeof(ObMViewInfo));
  len += refresh_next_.length() + 1;
  len += refresh_job_.length() + 1;
  len += last_refresh_trace_id_.length() + 1;
  return len;
}

OB_SERIALIZE_MEMBER(ObMViewInfo,
                    mview_id_,
                    build_mode_,
                    refresh_mode_,
                    refresh_method_,
                    refresh_start_,
                    refresh_next_,
                    refresh_job_,
                    last_refresh_scn_,
                    last_refresh_type_,
                    last_refresh_date_,
                    last_refresh_time_,
                    last_refresh_trace_id_,
                    schema_version_,
                    refresh_dop_,
                    data_sync_scn_,
                    is_synced_,
                    nested_refresh_mode_);

int ObMViewInfo::gen_insert_mview_dml(ObDMLSqlSplicer &dml) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_pk_column("mview_id", mview_id_)) ||
      OB_FAIL(dml.add_column("build_mode", build_mode_)) ||
      OB_FAIL(dml.add_column("refresh_mode", refresh_mode_)) ||
      OB_FAIL(dml.add_column("refresh_method", refresh_method_)) ||
      (OB_INVALID_TIMESTAMP != refresh_start_ &&
       OB_FAIL(dml.add_time_column("refresh_start", refresh_start_))) ||
      (!refresh_next_.empty() &&
       OB_FAIL(dml.add_column("refresh_next", ObHexEscapeSqlStr(refresh_next_)))) ||
      (!refresh_job_.empty() &&
       OB_FAIL(dml.add_column("refresh_job", ObHexEscapeSqlStr(refresh_job_)))) ||
      (OB_INVALID_SCN_VAL != last_refresh_scn_ &&
       OB_FAIL(dml.add_uint64_column("last_refresh_scn", last_refresh_scn_))) ||
      (ObMVRefreshType::MAX != last_refresh_type_ &&
       OB_FAIL(dml.add_column("last_refresh_type", last_refresh_type_))) ||
      (OB_INVALID_TIMESTAMP != last_refresh_date_ &&
       OB_FAIL(dml.add_time_column("last_refresh_date", last_refresh_date_))) ||
      (OB_INVALID_COUNT != last_refresh_time_ &&
       OB_FAIL(dml.add_column("last_refresh_time", last_refresh_time_))) ||
      (!last_refresh_trace_id_.empty() &&
       OB_FAIL(
         dml.add_column("last_refresh_trace_id", ObHexEscapeSqlStr(last_refresh_trace_id_)))) ||
      OB_FAIL(dml.add_column("schema_version", schema_version_))) {
    LOG_WARN("add column failed", KR(ret));
  } else if (OB_FAIL(dml.add_column("refresh_dop", refresh_dop_))) {
    LOG_WARN("fail to add refresh dop", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column("data_sync_scn", data_sync_scn_))) {
    LOG_WARN("fail to add data sync scn", K(ret)); 
  } else if (OB_FAIL(dml.add_column("is_synced", is_synced_))) {
    LOG_WARN("fail to add is synced", K(ret));
  } else if (OB_FAIL(dml.add_column("nested_refresh_mode", nested_refresh_mode_))) {
    LOG_WARN("fail to add nested refresh mode", K(ret));
  }
  return ret;
}

int ObMViewInfo::insert_mview_info(ObISQLClient &sql_client, const ObMViewInfo &mview_info)
{
  int ret = OB_SUCCESS;
  
  
  ObDMLSqlSplicer dml;
  if (OB_UNLIKELY(!mview_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(mview_info));
  } else if (OB_FAIL(mview_info.gen_insert_mview_dml(dml))) {
    LOG_WARN("fail to gen insert mview dml", KR(ret), K(mview_info));
  } else {
    ObDMLExecHelper exec(sql_client);
    int64_t affected_rows = 0;
    if (OB_FAIL(exec.exec_insert(OB_ALL_MVIEW_TNAME, dml, affected_rows))) {
      LOG_WARN("execute update failed", KR(ret));
    } else if (OB_UNLIKELY(!is_single_row(affected_rows))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("affected_rows unexpected to be one", KR(ret), K(affected_rows));
    }
  }
  // ObSqlString sql;
  // if (OB_FAIL(dml.splice_insert_sql(OB_ALL_MVIEW_TNAME, sql))) {
  //   LOG_WARN("splice sql failed", K(ret));
  // }
  // LOG_INFO("insert mview info", K(sql), K(ret));
  return ret;
}

int ObMViewInfo::gen_update_mview_attribute_dml(ObDMLSqlSplicer &dml) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_pk_column("mview_id", mview_id_)) ||
             OB_FAIL(dml.add_column("build_mode", build_mode_)) ||
             OB_FAIL(dml.add_column("refresh_mode", refresh_mode_)) ||
             OB_FAIL(dml.add_column("refresh_method", refresh_method_)) ||
             (OB_INVALID_TIMESTAMP != refresh_start_
                  ? OB_FAIL(dml.add_time_column("refresh_start", refresh_start_))
                  : OB_FAIL(dml.add_column(true, "refresh_start"))) ||
             (!refresh_next_.empty()
                  ? OB_FAIL(dml.add_column("refresh_next", ObHexEscapeSqlStr(refresh_next_)))
                  : OB_FAIL(dml.add_column(true, "refresh_next"))) ||
             (!refresh_job_.empty()
                  ? OB_FAIL(dml.add_column("refresh_job", ObHexEscapeSqlStr(refresh_job_)))
                  : OB_FAIL(dml.add_column(true, "refresh_next")))) {
    LOG_WARN("add column failed", KR(ret));
  } else if (OB_FAIL(dml.add_column("refresh_dop", refresh_dop_))) {
    LOG_WARN("fail to add refresh dop", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column("data_sync_scn", data_sync_scn_))) {
    LOG_WARN("fail to add data sync scn", K(ret)); 
  } else if (OB_FAIL(dml.add_column("is_synced", is_synced_))) {
    LOG_WARN("fail to add is_synced", K(ret)); 
  } else if (OB_FAIL(dml.add_column("nested_refresh_mode", nested_refresh_mode_))) {
    LOG_WARN("fail to add nested_refresh_mode", K(ret));
  }
  return ret;
}

int ObMViewInfo::update_mview_attribute(ObISQLClient &sql_client, const ObMViewInfo &mview_info)
{
  int ret = OB_SUCCESS;
  
  
  ObDMLSqlSplicer dml;
  if (OB_UNLIKELY(!mview_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(mview_info));
  } else if (OB_FAIL(mview_info.gen_update_mview_attribute_dml(dml))) {
    LOG_WARN("fail to gen update mview attribute dml", KR(ret), K(mview_info));
  } else {
    ObDMLExecHelper exec(sql_client);
    int64_t affected_rows = 0;
    if (OB_FAIL(exec.exec_update(OB_ALL_MVIEW_TNAME, dml, affected_rows))) {
      LOG_WARN("execute update failed", KR(ret));
    } else if (OB_UNLIKELY(!is_single_row(affected_rows))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("affected_rows unexpected to be one", KR(ret), K(affected_rows));
    }
  }
  return ret;
}

int ObMViewInfo::gen_update_mview_last_refresh_info_dml(ObDMLSqlSplicer &dml) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_SCN_VAL == last_refresh_scn_ ||
                         ObMVRefreshType::MAX == last_refresh_type_ ||
                         OB_INVALID_TIMESTAMP == last_refresh_date_ ||
                         OB_INVALID_COUNT == last_refresh_time_ ||
                         last_refresh_trace_id_.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mview last refresh info", KR(ret), KPC(this));
  } else if (OB_FAIL(dml.add_pk_column("mview_id", mview_id_)) ||
             OB_FAIL(dml.add_uint64_column("last_refresh_scn", last_refresh_scn_)) ||
             OB_FAIL(dml.add_column("last_refresh_type", last_refresh_type_)) ||
             OB_FAIL(dml.add_time_column("last_refresh_date", last_refresh_date_)) ||
             OB_FAIL(dml.add_column("last_refresh_time", last_refresh_time_)) ||
             OB_FAIL(dml.add_column("last_refresh_trace_id",
                                    ObHexEscapeSqlStr(last_refresh_trace_id_)))) {
    LOG_WARN("add column failed", KR(ret));
  } else if (OB_FAIL(dml.add_uint64_column("data_sync_scn", data_sync_scn_))) {
    LOG_WARN("fail to add data_sync_scn", KR(ret));
  } else if (OB_FAIL(dml.add_column("is_synced", is_synced_))) {
    LOG_WARN("fail to add is_synced", KR(ret));
  } else if (OB_FAIL(dml.add_column("nested_refresh_mode", nested_refresh_mode_))) {
    LOG_WARN("fail to add nested_refresh_mode", KR(ret));
  }
  return ret;
}

int ObMViewInfo::update_mview_last_refresh_info(ObISQLClient &sql_client,
                                                const ObMViewInfo &mview_info)
{
  int ret = OB_SUCCESS;
  
  
  ObDMLSqlSplicer dml;
  if (OB_UNLIKELY(!mview_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(mview_info));
  } else if (OB_FAIL(mview_info.gen_update_mview_last_refresh_info_dml(dml))) {
    LOG_WARN("fail to gen update mview last refresh info dml", KR(ret), K(mview_info));
  } else {
    ObDMLExecHelper exec(sql_client);
    int64_t affected_rows = 0;
    if (OB_FAIL(exec.exec_update(OB_ALL_MVIEW_TNAME, dml, affected_rows))) {
      LOG_WARN("execute update failed", KR(ret));
    } else if (OB_UNLIKELY(!is_single_row(affected_rows))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("affected_rows unexpected to be one", KR(ret), K(affected_rows));
    }
  }
  return ret;
}

int ObMViewInfo::drop_mview_info(ObISQLClient &sql_client, const ObMViewInfo &mview_info)
{
  int ret = OB_SUCCESS;
  
  if (OB_UNLIKELY(!mview_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(mview_info));
  } else if (OB_FAIL(drop_mview_info(sql_client,
                                     mview_info.get_mview_id()))) {
    LOG_WARN("fail to drop mview info", KR(ret), K(mview_info));
  }
  return ret;
}

int ObMViewInfo::drop_mview_info(ObISQLClient &sql_client,
                                 const uint64_t mview_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == mview_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(mview_id));
  } else {
    
    ObDMLSqlSplicer dml;
    if (OB_FAIL(dml.add_pk_column("mview_id", mview_id))) {
      LOG_WARN("add column failed", KR(ret));
    } else {
      ObDMLExecHelper exec(sql_client);
      int64_t affected_rows = 0;
      if (OB_FAIL(exec.exec_delete(OB_ALL_MVIEW_TNAME, dml, affected_rows))) {
        LOG_WARN("execute delete failed", KR(ret));
      } else if (OB_UNLIKELY(!is_single_row(affected_rows))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows unexpected to be one", KR(ret), K(affected_rows));
      }
    }
  }
  return ret;
}

int ObMViewInfo::fetch_mview_info(ObISQLClient &sql_client, uint64_t mview_id,
                                  ObMViewInfo &mview_info, bool for_update, bool nowait)
{
  int ret = OB_SUCCESS;
  
  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt("SELECT * FROM %s WHERE mview_id = %ld",
                              OB_ALL_MVIEW_TNAME, mview_id))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (for_update && !nowait && OB_FAIL(sql.append(" for update"))) {
      LOG_WARN("fail to append sql", KR(ret));
    } else if (for_update && nowait && OB_FAIL(sql.append(" for update nowait"))) {
      LOG_WARN("fail to append sql", KR(ret));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to get next", KR(ret));
      } else {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("mview info not exist", KR(ret), K(mview_id));
      }
    } else if (OB_FAIL(extract_mview_info(result, mview_info))){
      LOG_WARN("fail to extract mview info", K(ret));
    }
  }
  return ret;
}

int ObMViewInfo::batch_fetch_mview_ids(ObISQLClient &sql_client,
                                       uint64_t last_mview_id, ObIArray<uint64_t> &mview_ids,
                                       int64_t limit)
{
  int ret = OB_SUCCESS;
  mview_ids.reset();
  
  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    ObSqlString sql;
    uint64_t mview_id = OB_INVALID_ID;
    if (OB_FAIL(
          sql.assign_fmt("SELECT mview_id FROM %s WHERE 0 = 0", OB_ALL_MVIEW_TNAME))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_INVALID_ID != last_mview_id &&
               OB_FAIL(sql.append_fmt(" and mview_id > %ld", last_mview_id))) {
      LOG_WARN("fail to append sql", KR(ret));
    } else if (OB_FAIL(sql.append(" order by mview_id"))) {
      LOG_WARN("fail to append sql", KR(ret));
    } else if (limit > 0 && OB_FAIL(sql.append_fmt(" limit %ld", limit))) {
      LOG_WARN("fail to append sql", KR(ret));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    }
    while (OB_SUCC(ret)) {
      if (OB_FAIL(result->next())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to get next", KR(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      }
      EXTRACT_INT_FIELD_MYSQL(*result, "mview_id", mview_id, uint64_t);
      OZ(mview_ids.push_back(mview_id));
    }
  }
  return ret;
}

int ObMViewInfo::update_major_refresh_mview_scn(ObISQLClient &sql_client, const share::SCN &scn)
{
  int ret = OB_SUCCESS;

  if (!scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid scn", KR(ret), K(scn));
  } else {
    const uint64_t scn_val = scn.get_val_for_inner_table_field();
    const int64_t last_refresh_type = (int64_t)ObMVRefreshType::FAST;
    int64_t affected_rows = 0;
    ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt("UPDATE %s SET last_refresh_scn = %lu, \
                                last_refresh_type = %ld, \
                                last_refresh_date = now(6) \
                                WHERE refresh_mode = %ld and \
                                last_refresh_scn < %lu AND last_refresh_scn > 0",
                               OB_ALL_MVIEW_TNAME, scn_val, last_refresh_type,
                               ObMVRefreshMode::MAJOR_COMPACTION,
                               scn_val))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_UNLIKELY(affected_rows < 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected affected_rows", K(ret), K(affected_rows));
    }
  }

  return ret;
}

int ObMViewInfo::get_min_major_refresh_mview_scn(ObISQLClient &sql_client,
                                                 int64_t snapshot_for_tx, share::SCN &scn)
{
  int ret = OB_SUCCESS;
  scn.reset();
  ObSqlString sql;

  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    if (INT64_MAX == snapshot_for_tx) {
      if (OB_FAIL(sql.assign_fmt(
              "SELECT min(last_refresh_scn) min_refresh_scn FROM %s WHERE "
              "refresh_mode = %ld ",
              OB_ALL_MVIEW_TNAME, ObMVRefreshMode::MAJOR_COMPACTION))) {
        LOG_WARN("fail to assign sql", KR(ret));
      }
    } else {
      if (OB_FAIL(sql.assign_fmt(
              "SELECT min(last_refresh_scn) min_refresh_scn FROM %s as of snapshot %ld WHERE "
              "refresh_mode = %ld ",
              OB_ALL_MVIEW_TNAME, snapshot_for_tx, ObMVRefreshMode::MAJOR_COMPACTION))) {
        LOG_WARN("fail to assign sql", KR(ret));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else if (OB_FAIL(result->next())) {
      LOG_WARN("fail to get next", KR(ret));
    } else {
      uint64_t scn_val = 0;
      EXTRACT_UINT_FIELD_MYSQL(*result, "min_refresh_scn", scn_val, uint64_t);
      if (OB_SUCC(ret)) {
        scn.convert_for_inner_table_field(scn_val);
      }
    }
  }

  return ret;
}

int ObMViewInfo::contains_major_refresh_mview_in_creation(ObISQLClient &sql_client, bool &contains)
{
  int ret = OB_SUCCESS;
  contains = false;
  ObSqlString sql;

  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    if (OB_FAIL(sql.assign_fmt(
            "SELECT count(*) cnt FROM %s WHERE refresh_mode = %ld and last_refresh_scn = 0",
            OB_ALL_MVIEW_TNAME, ObMVRefreshMode::MAJOR_COMPACTION))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else if (OB_FAIL(result->next())) {
      LOG_WARN("fail to get next", KR(ret));
    } else {
      int64_t cnt = 0;
      EXTRACT_INT_FIELD_MYSQL(*result, "cnt", cnt, int64_t);
      if (OB_SUCC(ret) && cnt > 0) {
        contains = true;
      }
    }
  }

  return ret;
}
int ObMViewInfo::contains_major_refresh_mview(ObISQLClient &sql_client, bool &contains)
{
  int ret = OB_SUCCESS;
  contains = false;
  ObSqlString sql;

  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    if (OB_FAIL(sql.assign_fmt(
            "SELECT count(*) cnt FROM %s WHERE refresh_mode = %ld",
            OB_ALL_MVIEW_TNAME, ObMVRefreshMode::MAJOR_COMPACTION))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else if (OB_FAIL(result->next())) {
      LOG_WARN("fail to get next", KR(ret));
    } else {
      int64_t cnt = 0;
      EXTRACT_INT_FIELD_MYSQL(*result, "cnt", cnt, int64_t);
      if (OB_SUCC(ret) && cnt > 0) {
        contains = true;
      }
    }
  }

  return ret;
}


int ObMViewInfo::extract_mview_info(common::sqlclient::ObMySQLResult *result,
                                    ObMViewInfo &mview_info)
{
  int ret = OB_SUCCESS;
  mview_info.reset();
  if (OB_ISNULL(result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("result is null", KR(ret));
  } else {
    
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL(*result, mview_id, mview_info, uint64_t);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL(*result, build_mode, mview_info, ObMViewBuildMode);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL(*result, refresh_mode, mview_info, ObMVRefreshMode);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL(*result, refresh_method, mview_info, ObMVRefreshMethod);
    EXTRACT_TIMESTAMP_FIELD_TO_CLASS_MYSQL_SKIP_RET(*result, refresh_start, mview_info, nullptr);
    EXTRACT_VARCHAR_FIELD_TO_CLASS_MYSQL_SKIP_RET(*result, refresh_next, mview_info);
    EXTRACT_VARCHAR_FIELD_TO_CLASS_MYSQL_SKIP_RET(*result, refresh_job, mview_info);
    EXTRACT_UINT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(
      *result, last_refresh_scn, mview_info, uint64_t, true, false, OB_INVALID_SCN_VAL);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(
      *result, last_refresh_type, mview_info, ObMVRefreshType, true, false, ObMVRefreshType::MAX);
    EXTRACT_TIMESTAMP_FIELD_TO_CLASS_MYSQL_SKIP_RET(*result, last_refresh_date, mview_info,
                                                    nullptr);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(*result, last_refresh_time, mview_info,
                                                        int64_t, true, false, OB_INVALID_COUNT);
    EXTRACT_VARCHAR_FIELD_TO_CLASS_MYSQL_SKIP_RET(*result, last_refresh_trace_id, mview_info);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL(*result, schema_version, mview_info, int64_t);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(*result, refresh_dop, mview_info, int64_t, true, true, 0);
    EXTRACT_UINT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(*result, data_sync_scn, mview_info, uint64_t, true, true, 0);
    EXTRACT_BOOL_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(*result, is_synced, mview_info,
                                                         true /*ignore null*/, true /*ignore column error*/, false);
    EXTRACT_INT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(*result, nested_refresh_mode, mview_info,
                                                        ObMVNestedRefreshMode, true, true, 0);
  }
  return ret;
}

int ObMViewInfo::bacth_fetch_mview_infos(ObISQLClient &sql_client,
                                         const uint64_t refresh_scn,
                                         const ObIArray<uint64_t> &mview_ids,
                                         ObIArray<ObMViewInfo> &mview_infos)
{
  int ret = OB_SUCCESS;
  if (mview_ids.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res)
    {
      ObSqlString mview_id_array;
      ARRAY_FOREACH(mview_ids, idx) {
        if (OB_FAIL(mview_id_array.append_fmt((idx == 0) ?
                                              "%ld" : ",%ld", mview_ids.at(idx)))) {
          LOG_WARN("fail to append fmt", K(ret));
        }
      }
      common::sqlclient::ObMySQLResult *result = nullptr;
      ObSqlString sql;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql.assign_fmt("SELECT * FROM %s.%s ",
                          OB_SYS_DATABASE_NAME, OB_ALL_MVIEW_TNAME))) {
          LOG_WARN("fail to assign sql", K(ret));
        } else if (OB_INVALID_SCN_VAL != refresh_scn &&
                  OB_FAIL(sql.append_fmt(" AS OF SNAPSHOT %ld ", refresh_scn))) {
          LOG_WARN("fail to append fmt sql", K(ret));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(sql.append_fmt(" WHERE mview_id IN (%.*s)",
                         (int)mview_id_array.length(), mview_id_array.ptr()))) {
        LOG_WARN("fail to append fmt sql", K(ret));
      } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
        LOG_WARN("execute sql failed", KR(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", KR(ret));
      } else {
        ObMViewInfo mview_info;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result->next())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("fail to get next", KR(ret));
            } else {
              ret = OB_SUCCESS;
              break;
            }
          } else if (OB_FAIL(extract_mview_info(result, mview_info))) {
            LOG_WARN("fail to extract mview info", K(ret), K(mview_info)); 
          } else if (OB_FAIL(mview_infos.push_back(mview_info))) {
            LOG_WARN("fail to push back mivew inot", K(ret), K(mview_info));
          }
        }
        if (OB_SUCC(ret) && mview_infos.count() != mview_ids.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("mview infos count is not equal to mview ids count",
                    K(ret), K(mview_infos), K(mview_ids), K(sql));
        }
      }
      LOG_INFO("bacth get mview infos", K(sql), K(mview_infos));
    }
  }
  return ret;
}
                                          
// update data_sync_scn and is_synced
// ObMViewInfo::update_mview_data_attr moved definition to the upper-layer owner cpp(real upper-layer symbol user, declaration remains in this class header, transitional state)

int ObMViewInfo::check_satisfy_target_data_sync_scn(
                 const ObMViewInfo &mview_info,
                 const uint64_t target_data_sync_ts,
                 bool &satisfy)
{
  int ret = OB_SUCCESS;
  satisfy = false;
  if (mview_info.get_is_synced()
      && mview_info.get_data_sync_scn() ==
          target_data_sync_ts) {
    // do nothing
    satisfy = true;
  } else if (target_data_sync_ts > mview_info.get_data_sync_scn()) {
    LOG_INFO("dep mview not fit target data sync scn", K(ret), K(mview_info), K(target_data_sync_ts));
    satisfy = false;
  } else if (target_data_sync_ts < mview_info.get_data_sync_scn()) {
    ret = OB_ERR_MVIEW_CAN_NOT_NESTED_CONSISTENT_REFRESH;
    satisfy = false;
    LOG_WARN("nested consistent refresh mview can not success", K(ret), K(mview_info), K(target_data_sync_ts));
  }
  return ret;
}

int ObMViewInfo::get_mview_id_from_container_id(ObISQLClient &sql_client,
                                                uint64_t container_id, uint64_t &mview_id)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt(
            "SELECT table_id FROM %s WHERE table_type = %d and data_table_id = %ld",
            OB_ALL_TABLE_TNAME, share::schema::ObTableType::MATERIALIZED_VIEW, container_id))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to get next", KR(ret));
      } else {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("mview not exist", KR(ret), K(container_id));
      }
    } else {
      EXTRACT_INT_FIELD_MYSQL(*result, "table_id", mview_id, uint64_t);
    }
  }

  return ret;
}
} // namespace schema
} // namespace share
} // namespace oceanbase
