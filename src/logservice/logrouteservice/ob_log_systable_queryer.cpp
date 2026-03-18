/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX OBLOG

#include "ob_log_systable_queryer.h"
#include "share/inner_table/ob_inner_table_schema_constants.h" // OB_***_TNAME
#include "share/ob_server_struct.h" // GCTX

using namespace oceanbase::share;
namespace oceanbase
{
namespace logservice
{
ObLogSysTableQueryer::ObLogSysTableQueryer() :
    is_inited_(false),
    is_across_cluster_(false),
    cluster_id_(OB_INVALID_CLUSTER_ID),
    sql_proxy_(NULL),
    err_handler_(NULL)
{

}

ObLogSysTableQueryer::~ObLogSysTableQueryer()
{
  destroy();
}

int ObLogSysTableQueryer::init(const int64_t cluster_id,
    const bool is_across_cluster,
    common::ObISQLClient &sql_proxy,
    logfetcher::IObLogErrHandler *err_handler)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObLogSysTableQueryer has inited", KR(ret));
  } else {
    is_across_cluster_ = is_across_cluster;
    cluster_id_ = cluster_id;
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
    err_handler_ = err_handler;
  }

  return ret;
}

void ObLogSysTableQueryer::destroy()
{
  is_inited_ = false;
  is_across_cluster_ = false;
  cluster_id_ = OB_INVALID_CLUSTER_ID;
  sql_proxy_ = NULL;
  err_handler_ = NULL;
}

int ObLogSysTableQueryer::get_ls_log_info(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    ObLSLogInfo &ls_log_info)
{
  int ret = OB_SUCCESS;
  const char *select_fields = "ROLE, BEGIN_LSN, END_LSN";
  const char *log_stat_view_name = OB_GV_OB_LOG_STAT_TNAME;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogSysTableQueryer not init", KR(ret));
  } else if (OB_INVALID_TENANT_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KT(tenant_id));
  } else if (OB_FAIL(ls_log_info.init(tenant_id, ls_id))) {
    LOG_WARN("fail to init ls palf info", KR(ret), K(tenant_id), K(ls_id));
  } else {
    ObSqlString sql;
    int64_t record_count;

    SMART_VAR(ObISQLClient::ReadResult, result) {
      if (OB_FAIL(sql.assign_fmt(
          "SELECT %s FROM %s",
          select_fields, log_stat_view_name))) {
        LOG_WARN("assign sql string failed", KR(ret), K(tenant_id));
      // Use OB_SYS_TENANT_ID to query the GV$OB_LOG_STAT
      } else if (OB_FAIL(do_query_(OB_SYS_TENANT_ID, sql, result))) {
        LOG_WARN("do_query_ failed", KR(ret), K(cluster_id_), K(tenant_id), "sql", sql.ptr());
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get mysql result failed", KR(ret));
      } else if (OB_FAIL(get_records_template_(
              *result.get_result(),
              ls_log_info,
              "ObLSLogInfo",
              record_count))) {
        LOG_WARN("construct log stream palf stat info failed", KR(ret), K(ls_log_info));
      }
    } // SMART_VAR
  }

  return ret;
}

////////////////////////////////////// QueryAllUnitsInfo /////////////////////////////////
int ObLogSysTableQueryer::get_all_units_info(
    const uint64_t tenant_id,
    ObUnitsRecordInfo &units_record_info)
{
  int ret = OB_SUCCESS;
  ObUnitsRecord units_record;
  if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else {
    ObString zone_str = ObString::make_string(GCTX.config_->zone.str());
    common::ObZoneType zone_type = common::ZONE_TYPE_READWRITE;
    ObString region_str = ObString::make_string(DEFAULT_REGION_NAME);
    if (OB_FAIL(units_record.init(GCTX.self_addr(), zone_str, zone_type, region_str))) {
      LOG_WARN("units_record init failed", KR(ret), K(zone_str), K(zone_type), K(region_str));
    } else if (OB_FAIL(units_record_info.add(units_record))) {
      LOG_WARN("units_record_info add failed", KR(ret), K(units_record));
    }
  }
  return ret;
}


////////////////////////////////////// QueryAllServerInfo /////////////////////////////////
int ObLogSysTableQueryer::get_all_server_info(
    const uint64_t tenant_id,
    ObAllServerInfo &all_server_info)
{
  int ret = OB_SUCCESS;
  AllServerRecord server_record;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogSysTableQueryer not init", KR(ret));
  } else if (OB_FAIL(all_server_info.init(cluster_id_))) {
    LOG_WARN("fail to init all_server_info", KR(ret), K(cluster_id_));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else {
    ObString zone_str = ObString::make_string(GCTX.config_->zone.str());
    AllServerRecord::StatusType server_status = share::ObServerStatus::OB_SERVER_ACTIVE;
    if (OB_FAIL(server_record.init(OB_INIT_SERVER_ID, GCTX.self_addr(),
                       server_status, zone_str))) {
      LOG_WARN("fail to init server record", KR(ret), K(server_status), K(zone_str));
    } else if (OB_FAIL(all_server_info.add(server_record))) {
      LOG_WARN("fail to add server record", KR(ret), K(server_record));
    }
  }

  return ret;
}

////////////////////////////////////// QueryAllZoneInfo /////////////////////////////////
int ObLogSysTableQueryer::get_all_zone_info(
    const uint64_t tenant_id,
    ObAllZoneInfo &all_zone_info)
{
  int ret = OB_SUCCESS;
  AllZoneRecord zone_record;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogSysTableQueryer not init", KR(ret));
  } else if (OB_FAIL(all_zone_info.init(cluster_id_))) {
    LOG_WARN("fail to init all_zone_info", KR(ret), K(cluster_id_));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else {
    ObString zone_str = ObString::make_string(GCTX.config_->zone.str());
    ObString region_str = ObString::make_string(DEFAULT_REGION_NAME);
    if (OB_FAIL(zone_record.init(zone_str, region_str))) {
      LOG_WARN("fail to init zone record", KR(ret), K(zone_str), K(region_str));
    } else if (OB_FAIL(all_zone_info.add(zone_record))) {
      LOG_WARN("fail to add zone record", KR(ret), K(zone_record));
    }
  }
  return ret;
}

////////////////////////////////////// QueryAllZoneTypeInfo /////////////////////////////////
int ObLogSysTableQueryer::get_all_zone_type_info(
    const uint64_t tenant_id,
    ObAllZoneTypeInfo &all_zone_type_info)
{
  int ret = OB_SUCCESS;
  AllZoneTypeRecord zone_type_record;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogSysTableQueryer not init", KR(ret));
  } else if (OB_FAIL(all_zone_type_info.init(cluster_id_))) {
    LOG_WARN("fail to init all_zone_type_info", KR(ret), K(cluster_id_));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else {
    ObString zone_str = ObString::make_string(GCTX.config_->zone.str());
    common::ObZoneType zone_type = common::ZONE_TYPE_READWRITE;
    if (OB_FAIL(zone_type_record.init(zone_str, zone_type))) {
      LOG_WARN("fail to init zone type record", KR(ret), K(zone_str), K(zone_type));
    } else if (OB_FAIL(all_zone_type_info.add(zone_type_record))) {
      LOG_WARN("fail to add zone type record", KR(ret), K(zone_type_record));
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_FETCH_LOG_SYS_QUERY_FAILED);
int ObLogSysTableQueryer::do_query_(const uint64_t tenant_id,
    ObSqlString &sql,
    ObISQLClient::ReadResult &result)
{
  int ret = OB_SUCCESS;
  ObTaskId trace_id(*ObCurTraceId::get_trace_id());

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogSysTableQueryer not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("SqlProxy is NULL", KR(ret));
  } else if (is_across_cluster_) {
    if (OB_FAIL(sql_proxy_->read(result, cluster_id_, tenant_id, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(cluster_id_), K(tenant_id), "sql", sql.ptr());
    }
  } else {
    if (OB_FAIL(sql_proxy_->read(result, tenant_id, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(tenant_id), "sql", sql.ptr());
    }
  }
  if (OB_SUCC(ret) && ERRSIM_FETCH_LOG_SYS_QUERY_FAILED) {
    ret = ERRSIM_FETCH_LOG_SYS_QUERY_FAILED;
    LOG_WARN("errsim do query error", K(ERRSIM_FETCH_LOG_SYS_QUERY_FAILED));
  }
  if (OB_NOT_NULL(err_handler_) && (-ER_CONNECT_FAILED == ret || -ER_ACCESS_DENIED_ERROR == ret
    || OB_SERVER_IS_INIT == ret || OB_TENANT_NOT_EXIST == ret || OB_TENANT_NOT_IN_SERVER == ret
    || OB_SIZE_OVERFLOW == ret || OB_TIMEOUT == ret)) {
    err_handler_->handle_error(share::SYS_LS, logfetcher::IObLogErrHandler::ErrType::FETCH_LOG, trace_id,
      palf::LSN(palf::LOG_INVALID_LSN_VAL)/*no need to pass lsn*/, ret, "%s");
  }

  return ret;
}

template <typename RecordsType>
int ObLogSysTableQueryer::get_records_template_(common::sqlclient::ObMySQLResult &res,
    RecordsType &records,
    const char *event,
    int64_t &record_count)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogSysTableQueryer not init", KR(ret));
  } else {
    record_count = 0;

    while (OB_SUCC(ret)) {
      if (OB_FAIL(res.next())) {
        if (OB_ITER_END == ret) {
          // End of iteration
        } else {
          LOG_WARN("get next result failed", KR(ret), K(event));
        }
      } else if (OB_FAIL(parse_record_from_row_(res, records))) {
        LOG_WARN("parse_record_from_row_ failed", KR(ret), K(records));
      } else {
        record_count++;
      }
    } // while

    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

int ObLogSysTableQueryer::parse_record_from_row_(common::sqlclient::ObMySQLResult &res,
    ObLSLogInfo &ls_log_info)
{
  int ret = OB_SUCCESS;
  LogStatRecord log_stat_record;
  common::ObAddr server;
  ObString role_string;
  ObRole role;
  int64_t begin_lsn_int = 0;
  int64_t end_lsn_int = 0;

  (void)GET_COL_IGNORE_NULL(res.get_varchar, "ROLE", role_string);
  (void)GET_COL_IGNORE_NULL(res.get_int, "BEGIN_LSN", begin_lsn_int);
  (void)GET_COL_IGNORE_NULL(res.get_int, "END_LSN", end_lsn_int);

  palf::LSN begin_lsn(static_cast<palf::offset_t>(begin_lsn_int));
  palf::LSN end_lsn(static_cast<palf::offset_t>(end_lsn_int));

  // In oceanbase-lite, SVR_IP and SVR_PORT are no longer available from GV$OB_LOG_STAT.
  // Use current server address instead.
  server = GCTX.self_addr();

  if (OB_FAIL(common::string_to_role(role_string, role))) {
    LOG_WARN("string_tor_role failed", KR(ret), K(role_string), K(role));
  } else {
    log_stat_record.reset(server, role, begin_lsn, end_lsn);

    if (OB_FAIL(ls_log_info.add(log_stat_record))) {
      LOG_WARN("ls_log_info add failed", KR(ret), K(log_stat_record));
    }
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
