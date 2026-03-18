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
#include "logservice/ob_log_service.h" // ObLogService
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_global_stat_proxy.h" // for ObGlobalStatProxy
#include "share/schema/ob_schema_struct.h" // for ObTenantSchema
#include "storage/tx/ob_ts_mgr.h" // for OB_TS_MGR
#include "observer/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share::schema;
namespace share
{

void ObIDGenerator::reset()
{
  inited_ = false;
  step_ = 0;
  start_id_ = common::OB_INVALID_ID;
  end_id_ = common::OB_INVALID_ID;
  current_id_ = common::OB_INVALID_ID;
}

int ObIDGenerator::init(
    const uint64_t step,
    const uint64_t start_id,
    const uint64_t end_id)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(start_id > end_id || 0 == step)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_id/end_id", KR(ret), K(start_id), K(end_id), K(step));
  } else {
    step_ = step;
    start_id_ = start_id;
    end_id_ = end_id;
    current_id_ = start_id - step_;
    inited_ = true;
  }
  return ret;
}

int ObIDGenerator::next(uint64_t &current_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else if (current_id_ >= end_id_) {
    ret = OB_ITER_END;
  } else {
    current_id_ += step_;
    current_id = current_id_;
  }
  return ret;
}

int ObIDGenerator::get_start_id(uint64_t &start_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    start_id = start_id_;
  }
  return ret;
}

int ObIDGenerator::get_current_id(uint64_t &current_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    current_id = current_id_;
  }
  return ret;
}

int ObIDGenerator::get_end_id(uint64_t &end_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    end_id = end_id_;
  }
  return ret;
}

int ObIDGenerator::get_id_cnt(uint64_t &cnt) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(end_id_ < start_id_
             || step_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_id/end_id/step", KR(ret), KPC(this));
  } else {
    cnt = (end_id_ - start_id_) / step_ + 1;
  }
  return ret;
}

int ObShareUtil::set_default_timeout_ctx(ObTimeoutCtx &ctx, const int64_t default_timeout)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout_ts = OB_INVALID_TIMESTAMP;
  int64_t ctx_timeout_ts = ctx.get_abs_timeout();
  int64_t worker_timeout_ts = THIS_WORKER.get_timeout_ts();
  if (0 < ctx_timeout_ts) {
    //ctx is already been set, use it
    abs_timeout_ts = ctx_timeout_ts;
  } else if (INT64_MAX == worker_timeout_ts) {
    //if worker's timeout_ts not be set，set to default timeout
    abs_timeout_ts = ObTimeUtility::current_time() + default_timeout;
  } else if (0 < worker_timeout_ts) {
    //use worker's timeout if only it is valid
    abs_timeout_ts = worker_timeout_ts;
  } else {
    //worker's timeout_ts is invalid, set to default timeout
    abs_timeout_ts = ObTimeUtility::current_time() + default_timeout;
  }
  if (OB_FAIL(ctx.set_abs_timeout(abs_timeout_ts))) {
    LOG_WARN("set timeout failed", KR(ret), K(abs_timeout_ts), K(ctx_timeout_ts),
        K(worker_timeout_ts), K(default_timeout));
  } else if (ctx.is_timeouted()) {
    ret = OB_TIMEOUT;
    LOG_WARN("timeouted", KR(ret), K(abs_timeout_ts), K(ctx_timeout_ts),
        K(worker_timeout_ts), K(default_timeout));
  } else {
    LOG_TRACE("set_default_timeout_ctx success", K(abs_timeout_ts),
        K(ctx_timeout_ts), K(worker_timeout_ts), K(default_timeout));
  }
  return ret;
}

int ObShareUtil::get_abs_timeout(const int64_t default_timeout, int64_t &abs_timeout)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, default_timeout))) {
    LOG_WARN("fail to set default timeout ctx", KR(ret), K(default_timeout));
  } else {
    abs_timeout = ctx.get_abs_timeout();
  }
  return ret;
}

int ObShareUtil::get_ctx_timeout(const int64_t default_timeout, int64_t &timeout)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, default_timeout))) {
    LOG_WARN("fail to set default timeout ctx", KR(ret), K(default_timeout));
  } else {
    timeout = ctx.get_timeout();
  }
  return ret;
}

int ObShareUtil::fetch_current_data_version(
    common::ObISQLClient &client,
    const uint64_t tenant_id,
    uint64_t &data_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant_id is invalid", KR(ret), K(tenant_id));
  } else {
    data_version = DATA_CURRENT_VERSION;
  }
  return ret;
}

int ObShareUtil::get_ora_rowscn(
    common::ObISQLClient &client,
    const uint64_t tenant_id,
    const ObSqlString &sql,
    SCN &ora_rowscn)
{
  int ret = OB_SUCCESS;
  uint64_t ora_rowscn_val = 0;
  ora_rowscn.set_invalid();
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObMySQLResult *result = NULL;
    if (OB_FAIL(client.read(res, tenant_id, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (NULL == (result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get sql result", KR(ret));
    } else if (OB_FAIL(result->next())) {
      LOG_WARN("fail to get next row", KR(ret));
    } else {
      EXTRACT_INT_FIELD_MYSQL(*result, "ORA_ROWSCN", ora_rowscn_val, int64_t);
      if (FAILEDx(ora_rowscn.convert_for_inner_table_field(ora_rowscn_val))) {
        LOG_WARN("fail to convert val to SCN", KR(ret), K(ora_rowscn_val));
      }
    }

    int tmp_ret = OB_SUCCESS;
    if (OB_FAIL(ret)) {
      //nothing todo
    } else if (OB_ITER_END != (tmp_ret = result->next())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get more row than one", KR(ret), KR(tmp_ret));
    }
  }
  return ret;
}

int ObShareUtil::mtl_get_tenant_role(const uint64_t tenant_id, ObTenantRole::Role &tenant_role)
{
  int ret = OB_SUCCESS;
  tenant_role = ObTenantRole::INVALID_TENANT;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", KR(ret), K(tenant_id));
  } else if (is_sys_tenant(tenant_id) || is_meta_tenant(tenant_id)) {
    tenant_role = ObTenantRole::PRIMARY_TENANT;
  } else {
    MTL_SWITCH(tenant_id) {
      tenant_role = MTL_GET_TENANT_ROLE_CACHE();
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(is_invalid_tenant(tenant_role))) {
    ret = OB_NEED_WAIT;
    LOG_WARN("tenant role is not ready, need wait", KR(ret), K(tenant_id), K(tenant_role));
  }
  return ret;
}

int ObShareUtil::mtl_check_if_tenant_role_is_primary(const uint64_t tenant_id, bool &is_primary)
{
  int ret = OB_SUCCESS;
  is_primary = false;
  ObTenantRole::Role tenant_role;
  if (OB_FAIL(mtl_get_tenant_role(tenant_id, tenant_role))) {
    LOG_WARN("fail to execute mtl_get_tenant_role", KR(ret), K(tenant_id));
  } else if (is_primary_tenant(tenant_role)) {
    is_primary = true;
  }
  return ret;
}

int ObShareUtil::mtl_check_if_tenant_role_is_standby(const uint64_t tenant_id, bool &is_standby)
{
  int ret = OB_SUCCESS;
  is_standby = false;
  ObTenantRole::Role tenant_role;
  if (OB_FAIL(mtl_get_tenant_role(tenant_id, tenant_role))) {
    LOG_WARN("fail to execute mtl_get_tenant_role", KR(ret), K(tenant_id));
  } else if (is_standby_tenant(tenant_role)) {
    is_standby = true;
  }
  return ret;
}
int ObShareUtil::table_get_tenant_role(const uint64_t tenant_id, ObTenantRole &tenant_role)
{
  int ret = OB_SUCCESS;
  tenant_role.reset();
  bool is_primary = true;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", KR(ret), K(tenant_id));
  } else if (is_sys_tenant(tenant_id) || is_meta_tenant(tenant_id)) {
    if (OB_FAIL(is_primary_cluster(is_primary))) {
      LOG_WARN("fail to check whether is primary cluster", K(is_primary));
    } else if (is_primary) {
      tenant_role = ObTenantRole::PRIMARY_TENANT;
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("standby cluster not supported now", KR(ret));
    }
  }
  return ret;
}

int ObShareUtil::table_check_if_tenant_role_is_primary(const uint64_t tenant_id, bool &is_primary)
{
  int ret = OB_SUCCESS;
  share::ObTenantRole tenant_role;
  is_primary = false;
  if (OB_FAIL(table_get_tenant_role(tenant_id, tenant_role))) {
    LOG_WARN("fail to execute table_get_tenant_role", KR(ret), K(tenant_id));
  } else if (tenant_role.is_primary()) {
    is_primary = true;
  }
  return ret;
}
int ObShareUtil::table_check_if_tenant_role_is_standby(const uint64_t tenant_id, bool &is_standby)
{
  int ret = OB_SUCCESS;
  share::ObTenantRole tenant_role;
  is_standby = false;
  if (OB_FAIL(table_get_tenant_role(tenant_id, tenant_role))) {
    LOG_WARN("fail to execute table_get_tenant_role", KR(ret), K(tenant_id));
  } else if (tenant_role.is_standby()) {
    is_standby = true;
  }
  return ret;
}
const char *ObShareUtil::replica_type_to_string(const ObReplicaType type)
{
  const char *str = NULL;
  switch (type) {
    case ObReplicaType::REPLICA_TYPE_FULL: {
      str = FULL_REPLICA_STR;
      break;
    }
    case ObReplicaType::REPLICA_TYPE_BACKUP: {
      str = BACKUP_REPLICA_STR;
      break;
    }
    case ObReplicaType::REPLICA_TYPE_LOGONLY: {
      str = LOGONLY_REPLICA_STR;
      break;
    }
    case ObReplicaType::REPLICA_TYPE_READONLY: {
      str = READONLY_REPLICA_STR;
      break;
    }
    case ObReplicaType::REPLICA_TYPE_MEMONLY: {
      str = MEMONLY_REPLICA_STR;
      break;
    }
    case ObReplicaType::REPLICA_TYPE_ENCRYPTION_LOGONLY: {
      str = ENCRYPTION_LOGONLY_REPLICA_STR;
      break;
    }
    case ObReplicaType::REPLICA_TYPE_COLUMNSTORE: {
      str = COLUMNSTORE_REPLICA_STR;
      break;
    }
    default: {
      str = "INVALID";
      break;
    }
  }
  return str;
}

// retrun REPLICA_TYPE_INVALID if str is invaild
ObReplicaType ObShareUtil::string_to_replica_type(const char *str)
{
  return string_to_replica_type(ObString(str));
}

// retrun REPLICA_TYPE_INVALID if str is invaild
ObReplicaType ObShareUtil::string_to_replica_type(const ObString &str)
{
  ObReplicaType replica_type = REPLICA_TYPE_INVALID;
  if (OB_UNLIKELY(str.empty())) {
    replica_type = REPLICA_TYPE_INVALID;
  } else if (0 == str.case_compare(FULL_REPLICA_STR) || 0 == str.case_compare(F_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_FULL;
  } else if (0 == str.case_compare(READONLY_REPLICA_STR) || 0 == str.case_compare(R_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_READONLY;
  } else if (0 == str.case_compare(COLUMNSTORE_REPLICA_STR) || 0 == str.case_compare(C_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_COLUMNSTORE;
  } else if (0 == str.case_compare(LOGONLY_REPLICA_STR) || 0 == str.case_compare(L_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_LOGONLY;
  } else if (0 == str.case_compare(ENCRYPTION_LOGONLY_REPLICA_STR) || 0 == str.case_compare(E_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_ENCRYPTION_LOGONLY;
  } else if (0 == str.case_compare(BACKUP_REPLICA_STR) || 0 == str.case_compare(B_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_BACKUP;
  } else if (0 == str.case_compare(MEMONLY_REPLICA_STR) || 0 == str.case_compare(M_REPLICA_STR)) {
    replica_type = REPLICA_TYPE_MEMONLY;
  } else {
    replica_type = REPLICA_TYPE_INVALID;
  }
  return replica_type;
}

int ObShareUtil::get_sys_ls_readable_scn(SCN &readable_scn)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_svr = MTL(ObLSService*);
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  share::SCN offline_scn;
  if (OB_ISNULL(ls_svr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("pointer is null", KR(ret), KP(ls_svr));
  } else if (OB_FAIL(ls_svr->get_ls(SYS_LS, ls_handle, ObLSGetMod::RS_MOD))) {
      LOG_WARN("get log stream failed", KR(ret));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream is null", KR(ret), K(ls_handle));
  } else if (OB_FAIL(ls->get_max_decided_scn(readable_scn))) {
    LOG_WARN("failed to get_max_decided_scn", KR(ret), KPC(ls));
  }
  return ret;
}

int ObShareUtil::check_clog_disk_full_or_hang(
    bool &clog_disk_is_full,
    bool &clog_disk_is_hang)
{
  int ret = OB_SUCCESS;
  clog_disk_is_full = false;
  clog_disk_is_hang = false;
  int64_t clog_disk_last_working_time = OB_INVALID_TIMESTAMP;
  const int64_t now = ObTimeUtility::current_time();
  bool is_disk_enough = true;
  logservice::ObLogService *log_service = MTL(logservice::ObLogService*);
  if (OB_ISNULL(log_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(log_service));
  } else if (OB_FAIL(log_service->get_io_start_time(clog_disk_last_working_time))) {
    LOG_WARN("get_io_start_time failed", KR(ret));
  } else if (OB_FAIL(log_service->check_disk_space_enough(is_disk_enough))) {
    LOG_WARN("check_disk_space_enough failed", KR(ret));
  } else {
    clog_disk_is_full = !is_disk_enough;
    clog_disk_is_hang = OB_INVALID_TIMESTAMP != clog_disk_last_working_time
                        && now - clog_disk_last_working_time > GCONF.log_storage_warning_tolerance_time;
  }
  return ret;
}

int ObShareUtil::check_data_disk_health_status(
    bool &is_data_disk_healthy)
{
  int ret = OB_SUCCESS;
  is_data_disk_healthy = true;
  ObDeviceHealthStatus data_disk_status;
  int64_t data_disk_error_start_ts = OB_INVALID_TIMESTAMP;
  if (OB_FAIL(OB_IO_MANAGER.get_device_health_detector().get_device_health_status(data_disk_status,
                                                                                  data_disk_error_start_ts))) {
    LOG_WARN("get_device_health_status failed", KR(ret));
  } else if (ObDeviceHealthStatus::DEVICE_HEALTH_NORMAL != data_disk_status) {
    is_data_disk_healthy = false;
  }
  return ret;
}

int ObShareUtil::get_tenant_gts(const uint64_t &tenant_id, SCN &gts_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant id is invalid", KR(ret), K(tenant_id));
  } else {
    ret = OB_EAGAIN;
    const transaction::MonotonicTs stc = transaction::MonotonicTs::current_time();
    transaction::MonotonicTs unused_ts(0);
    const int64_t start_time = ObTimeUtility::fast_current_time();
    const int64_t TIMEOUT = GCONF.rpc_timeout;
    while (OB_EAGAIN == ret) {
      if (ObTimeUtility::fast_current_time() - start_time > TIMEOUT) {
        ret = OB_TIMEOUT;
        LOG_WARN("stmt is timeout", KR(ret), K(start_time), K(TIMEOUT));
      } else if (OB_FAIL(OB_TS_MGR.get_gts(tenant_id, stc, NULL,
                                           gts_scn, unused_ts))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("failed to get gts", KR(ret), K(tenant_id));
        } else {
          // waiting 10ms
          ob_usleep(10L * 1000L);
        }
      }
    }
  }
  LOG_INFO("get tenant gts", KR(ret), K(tenant_id), K(gts_scn));
  return ret;
}

int ObShareUtil::gen_sys_unit(ObUnit &unit)
{
  int ret = OB_SUCCESS;
  unit.reset();
  if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", KR(ret), KP(GCTX.config_));
  } else {
    unit.unit_id_ = OB_SYS_UNIT_ID;
    unit.resource_pool_id_ = OB_SYS_RESOURCE_POOL_ID;
    unit.unit_group_id_ = OB_SYS_UNIT_GROUP_ID;
    unit.zone_ = GCTX.config_->zone.str();
    unit.server_ = GCTX.self_addr();
    unit.migrate_from_server_.reset();
    unit.is_manual_migrate_ = false;
    unit.status_ = ObUnit::UNIT_STATUS_ACTIVE;
    unit.replica_type_ = REPLICA_TYPE_FULL;
  }
  return ret;
}

int ObShareUtil::gen_sys_resource_pool(ObResourcePool &resource_pool)
{
  int ret = OB_SUCCESS;
  resource_pool.reset();
  if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server", KR(ret), KP(GCTX.config_));
  } else if (OB_FAIL(resource_pool.zone_list_.push_back(GCTX.config_->zone.str()))) {
    LOG_WARN("fail to push back zone into array", KR(ret));
  } else if (OB_FAIL(resource_pool.name_.assign("sys_pool"))) {
    LOG_WARN("fail to construct pool name", KR(ret));
  } else {
    resource_pool.resource_pool_id_ = OB_SYS_RESOURCE_POOL_ID;
    resource_pool.unit_count_ = 1;
    resource_pool.unit_config_id_ = ObUnitConfig::SYS_UNIT_CONFIG_ID;
    resource_pool.tenant_id_ = OB_SYS_TENANT_ID;
    resource_pool.replica_type_ = REPLICA_TYPE_FULL;
  }
  return ret;
}

int ObShareUtil::gen_default_sys_tenant_schema(schema::ObTenantSchema &tenant_schema)
{
  int ret = OB_SUCCESS;
  tenant_schema.reset();
  int64_t schema_version = 0;
  if (OB_ISNULL(GCTX.config_) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_), KP(GCTX.schema_service_));
  } else if (OB_FAIL(tenant_schema.set_tenant_name(OB_SYS_TENANT_NAME))) {
    LOG_WARN("set_tenant_name failed", "tenant_name", OB_SYS_TENANT_NAME, KR(ret));
  } else if (OB_FAIL(tenant_schema.set_comment("system tenant"))) {
    LOG_WARN("set_comment failed", "comment", "system tenant", KR(ret));
  } else if (OB_FAIL(tenant_schema.add_zone(GCTX.config_->zone.str()))) {
    LOG_WARN("fail to push back zone", KR(ret));
  } else {
    if (OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
    } else {
      ObGlobalStatProxy proxy(*GCTX.sql_proxy_, OB_SYS_TENANT_ID);
      if (OB_FAIL(proxy.get_baseline_schema_version(schema_version))) {
        LOG_WARN("get_baseline_schema_version failed", KR(ret));
      } else if (-1 == schema_version) {
        // in bootstrap procedure, add_tenant may raise error, just mock schema_version = 1 is ok
        LOG_INFO("in bootstrap procedure, mock schema_version to 1", KR(ret));
        schema_version = 1;
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      tenant_schema.set_tenant_id(OB_SYS_TENANT_ID);
      tenant_schema.set_schema_version(schema_version);
      tenant_schema.set_locked(false);
      tenant_schema.set_read_only(false);
      tenant_schema.set_default_tablegroup_id(OB_INVALID_ID);
      tenant_schema.set_in_recyclebin(false);
      tenant_schema.set_status(schema::ObTenantStatus::TENANT_STATUS_NORMAL);
      tenant_schema.set_compatibility_mode(ObCompatibilityMode::MYSQL_MODE);
      // charset_type_ not used, default is ok
      // collation_type_ not used, default is ok
      // name_case_mode_ not used, default is ok
      // default_tablegroup_name_ not used, default is ok
    }
  }
  LOG_INFO("finish construct sys tenant schema", KR(ret), K(tenant_schema), K(schema_version));
  return ret;
}

int ObShareUtil::is_primary_cluster(bool &is_primary)
{
  int ret = OB_SUCCESS;
  //TODO@jiage: get role from config
  is_primary = true;
  return ret;
}

} //end namespace share
} //end namespace oceanbase
