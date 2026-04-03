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

#define USING_LOG_PREFIX STANDBY

#include "ob_standby_service.h"              // ObStandbyService

#include "rootserver/ob_cluster_event.h"          // CLUSTER_EVENT_ADD_CONTROL
#include "rootserver/ob_tenant_event_def.h" // TENANT_EVENT
// Removed ObLSServiceHelper include - simplified for single LS scenario
#include "share/backup/ob_backup_config.h" // ObBackupConfigParserMgr
#include "share/ob_all_tenant_info.h"       // ObAllTenantInfo, ObAllTenantInfoProxy

ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_PREP_SW_TO_STANDBY);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_SWITCHING_TO_STANDBY);

namespace oceanbase
{
using namespace oceanbase;
using namespace common;
using namespace obrpc;
using namespace share;
using namespace rootserver;
using namespace storage;
using namespace tenant_event;
namespace standby
{

int ObStandbyService::init(
           ObMySQLProxy *sql_proxy,
           share::schema::ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_proxy)
      || OB_ISNULL(schema_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(sql_proxy), KP(schema_service));
  } else {
    sql_proxy_ = sql_proxy;
    schema_service_ = schema_service;
    inited_ = true;
  }
  return ret;
}

void ObStandbyService::destroy()
{
  if (OB_UNLIKELY(!inited_)) {
    LOG_INFO("ObStandbyService has been destroyed", K_(inited));
  } else {
    LOG_INFO("ObStandbyService begin to destroy", K_(inited));
    sql_proxy_ = NULL;
    schema_service_ = NULL;
    inited_ = false;
    LOG_INFO("ObStandbyService destroyed", K_(inited));
  }
}

int ObStandbyService::check_inner_stat_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_) || OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Member variables is NULL", KR(ret), KP(sql_proxy_), KP(schema_service_));
  }
  return ret;
}
#define PRINT_TENANT_INFO(tenant_info, tenant_info_buf) \
    do { \
        int64_t pos = 0; \
        size_t tenant_buf_size = sizeof(tenant_info_buf) / sizeof(tenant_info_buf[0]); \
        if ((tenant_info).is_valid()) { \
            (void)databuff_print_multi_objs(tenant_info_buf, tenant_buf_size, pos, tenant_info); \
        } else { \
            (void)databuff_printf(tenant_info_buf, tenant_buf_size, pos, "NULL"); \
        } \
    } while(0)

void ObStandbyService::tenant_event_start_(
    const obrpc::ObSwitchRoleArg &arg, int ret,
    int64_t begin_ts, const share::ObAllTenantInfo &tenant_info)
{
  char tenant_info_buf[1024] = "";
  PRINT_TENANT_INFO(tenant_info, tenant_info_buf);
  switch (arg.get_op_type()) {
      case ObSwitchRoleArg::SWITCH_TO_PRIMARY :
        TENANT_EVENT(OB_SYS_TENANT_ID, TENANT_ROLE_CHANGE, SWITCHOVER_TO_PRIMARY_START, begin_ts,
            ret, 0, ObHexEscapeSqlStr(arg.get_stmt_str()), ObHexEscapeSqlStr(tenant_info_buf));
        break;
      case ObSwitchRoleArg::SWITCH_TO_STANDBY :
        TENANT_EVENT(OB_SYS_TENANT_ID, TENANT_ROLE_CHANGE, SWITCHOVER_TO_STANDBY_START, begin_ts,
            ret, 0, ObHexEscapeSqlStr(arg.get_stmt_str()), ObHexEscapeSqlStr(tenant_info_buf));
        break;
      case ObSwitchRoleArg::FAILOVER_TO_PRIMARY :
        TENANT_EVENT(OB_SYS_TENANT_ID, TENANT_ROLE_CHANGE, FAILOVER_TO_PRIMARY_START, begin_ts,
            ret, 0, ObHexEscapeSqlStr(arg.get_stmt_str()), ObHexEscapeSqlStr(tenant_info_buf));
        break;
      default :break;
    }
}

void ObStandbyService::tenant_event_end_(
    const obrpc::ObSwitchRoleArg &arg,
    int ret, int64_t cost, int64_t end_ts, const share::SCN switch_scn,
    ObTenantRoleTransCostDetail &cost_detail)
{
  share::ObAllTenantInfo tenant_info;
  if (!THIS_WORKER.is_timeout()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(ObAllTenantInfoProxy::load_tenant_info(
      false,
      tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    }
  }
  char tenant_info_buf[1024] = "";
  PRINT_TENANT_INFO(tenant_info, tenant_info_buf);
  // For single LS scenario, only sys_ls exists
  const char *all_ls_str = "sys_ls";
  switch (arg.get_op_type()) {
    case ObSwitchRoleArg::SWITCH_TO_PRIMARY :
      TENANT_EVENT(OB_SYS_TENANT_ID, TENANT_ROLE_CHANGE, SWITCHOVER_TO_PRIMARY_END, end_ts,
          ret, cost, ObHexEscapeSqlStr(arg.get_stmt_str()), ObHexEscapeSqlStr(tenant_info_buf), switch_scn.get_val_for_inner_table_field(), cost_detail, all_ls_str);
      break;
    case ObSwitchRoleArg::SWITCH_TO_STANDBY :
      TENANT_EVENT(OB_SYS_TENANT_ID, TENANT_ROLE_CHANGE, SWITCHOVER_TO_STANDBY_END, end_ts,
          ret, cost, ObHexEscapeSqlStr(arg.get_stmt_str()), ObHexEscapeSqlStr(tenant_info_buf), switch_scn.get_val_for_inner_table_field(), cost_detail, all_ls_str);
      break;
    case ObSwitchRoleArg::FAILOVER_TO_PRIMARY :
      TENANT_EVENT(OB_SYS_TENANT_ID, TENANT_ROLE_CHANGE, FAILOVER_TO_PRIMARY_END, end_ts,
          ret, cost, ObHexEscapeSqlStr(arg.get_stmt_str()), ObHexEscapeSqlStr(tenant_info_buf), switch_scn.get_val_for_inner_table_field(), cost_detail, all_ls_str);
      break;
    default :break;
  }
}

int ObStandbyService::switch_role(const obrpc::ObSwitchRoleArg &arg)
{
  int ret = OB_SUCCESS;
  int64_t begin_ts = ObTimeUtility::current_time();
  bool is_verify = arg.get_is_verify();
  ObAllTenantInfo tenant_info;
  share::SCN switch_scn = SCN::min_scn();
  ObTenantRoleTransCostDetail cost_detail;
  cost_detail.set_start(begin_ts);
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(inited));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), KR(ret));
  } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
      false,
      tenant_info))) {
    LOG_WARN("failed to load tenant info", KR(ret));
  } else {
    if (!is_verify) {
      (void) tenant_event_start_(arg, ret, begin_ts, tenant_info);
    }

    switch (arg.get_op_type()) {
      case ObSwitchRoleArg::SWITCH_TO_PRIMARY :
        if (OB_FAIL(switch_to_primary(arg.get_op_type(), is_verify,
            switch_scn, cost_detail))) {
          LOG_WARN("failed to switch_to_primary", KR(ret), K(arg));
        }
        break;
      case ObSwitchRoleArg::SWITCH_TO_STANDBY :
        if (OB_FAIL(switch_to_standby(arg.get_op_type(), is_verify,
            tenant_info, switch_scn, cost_detail))) {
          LOG_WARN("failed to switch_to_standby", KR(ret), K(arg));
        }
        break;
      case ObSwitchRoleArg::FAILOVER_TO_PRIMARY :
        if (OB_FAIL(failover_to_primary(arg.get_op_type(), is_verify,
            tenant_info, switch_scn, cost_detail))) {
          LOG_WARN("failed to failover_to_primary", KR(ret), K(arg));
        }
        break;
      default :
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unkown op_type", KR(ret), K(arg));
    }
    // reset return code to TIMEOUT, to prevent the error code which not user unfriendly
    if (THIS_WORKER.is_timeout() && OB_ERR_EXCLUSIVE_LOCK_CONFLICT == ret) {
      ret = OB_TIMEOUT;
    }
    int64_t end_ts = ObTimeUtility::current_time();
    int64_t cost = end_ts - begin_ts;
    cost_detail.set_end(end_ts);
    FLOG_INFO("switch tenant end", KR(ret), K(arg), K(cost), K(cost_detail));
    if (!is_verify) {
      (void) tenant_event_end_(arg, ret, cost, end_ts, switch_scn, cost_detail);
    }
  }
  return ret;
}

int ObStandbyService::failover_to_primary(
    const obrpc::ObSwitchRoleArg::OpType &switch_optype,
    const bool is_verify,
    const share::ObAllTenantInfo &tenant_info,
    share::SCN &switch_scn,
    ObTenantRoleTransCostDetail &cost_detail)
{
  int ret = OB_SUCCESS;
  ObTenantRoleTransitionService role_transition_service;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(inited));
  } else if (OB_ISNULL(GCTX.srv_rpc_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(GCTX.srv_rpc_proxy_));
  } else if (OB_UNLIKELY(obrpc::ObSwitchRoleArg::OpType::INVALID == switch_optype)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid switch_optype", KR(ret), K(switch_optype));
  } else if (OB_FAIL(role_transition_service.init(
      switch_optype,
      is_verify,
      &cost_detail))) {
    LOG_WARN("fail to init role_transition_service", KR(ret), K(switch_optype),
         K(cost_detail));
  } else {
    share::ObLSRecoveryStat ls_recovery_stat;
    if (OB_FAIL(role_transition_service.failover_to_primary())) {
      LOG_WARN("failed to failover to primary", KR(ret));
    } else if (OB_FAIL(share::get_ls_recovery_stat(ls_recovery_stat))) {
      LOG_WARN("failed to get ls recovery stat for switch_scn", KR(ret));
    } else {
      switch_scn = ls_recovery_stat.get_sync_scn();
    }

    // Schema refresh trigger is now managed by MTL framework
    // It checks tenant role at runtime to decide whether to refresh schema
  }

  return ret;
}

int ObStandbyService::switch_to_primary(
    const obrpc::ObSwitchRoleArg::OpType &switch_optype,
    const bool is_verify,
    share::SCN &switch_scn,
    ObTenantRoleTransCostDetail &cost_detail)
{
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  ObTenantRoleTransitionService role_transition_service;
  ObAllTenantInfo tenant_info;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(inited));
  } else if (OB_ISNULL(GCTX.srv_rpc_proxy_) || OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(GCTX.srv_rpc_proxy_), KP(sql_proxy_));
  } else if (OB_UNLIKELY(obrpc::ObSwitchRoleArg::OpType::INVALID == switch_optype)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid switch_optype", KR(ret), K(switch_optype));
  } else if (OB_FAIL(role_transition_service.init(
      switch_optype,
      is_verify,
      &cost_detail))) {
    LOG_WARN("fail to init role_transition_service", KR(ret),
             KP(GCTX.srv_rpc_proxy_), K(cost_detail));
  } else {
    if (OB_FAIL(role_transition_service.failover_to_primary())) {
      LOG_WARN("fail to failover to primary", KR(ret));
    }
    switch_scn = role_transition_service.get_so_scn();

    // Schema refresh trigger is now managed by MTL framework
    // It checks tenant role at runtime to decide whether to refresh schema
  }
  return ret;
}

int ObStandbyService::switch_to_standby(
    const obrpc::ObSwitchRoleArg::OpType &switch_optype,
    const bool is_verify,
    share::ObAllTenantInfo &tenant_info,
    share::SCN &switch_scn,
    ObTenantRoleTransCostDetail &cost_detail)
{
  int ret = OB_SUCCESS;
  const int32_t group_id = share::OBCG_DBA_COMMAND;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(inited));
  } else if (OB_ISNULL(GCTX.srv_rpc_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(GCTX.srv_rpc_proxy_));
  } else if (OB_UNLIKELY(obrpc::ObSwitchRoleArg::OpType::INVALID == switch_optype)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid switch_optype", KR(ret), K(switch_optype));
  } else if (tenant_info.is_standby() && tenant_info.is_normal_status()) {
    LOG_INFO("already is standby tenant, no need switch", K(tenant_info));
  } else {
    switch(tenant_info.get_switchover_status().value()) {
      case share::ObTenantSwitchoverStatus::NORMAL_STATUS: {
        if (OB_FAIL(ret)) {
        } else if (!tenant_info.is_primary()) {
          ret = OB_OP_NOT_ALLOW;
          LOG_WARN("unexpected tenant role", KR(ret), K(tenant_info));
          LOG_USER_ERROR(OB_OP_NOT_ALLOW, "tenant role is not PRIMARY, switchover to standby is");
        } else if (is_verify) {
          // skip
        } else if (OB_FAIL(update_tenant_status_before_sw_to_standby_(
                            tenant_info.get_switchover_status(),
                            tenant_info.get_tenant_role(),
                            tenant_info))) {
          LOG_WARN("failed to update_tenant_status_before_sw_to_standby_", KR(ret), K(tenant_info));
        } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_PREP_SW_TO_STANDBY)) {
          LOG_WARN("errsim: after persist prep switching to standby", KR(ret));
        }
      }
      case share::ObTenantSwitchoverStatus::PREPARE_SWITCHING_TO_STANDBY_STATUS: {
        if (OB_FAIL(ret) || is_verify) {
        } else if (OB_FAIL(switch_to_standby_prepare_ls_status_(
                                                                tenant_info.get_switchover_status(),
                                                                tenant_info))) {
          LOG_WARN("failed to switch_to_standby_prepare_ls_status_", KR(ret),  K(tenant_info));
        } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_SWITCHING_TO_STANDBY)) {
          LOG_WARN("errsim: after persist switching to standby", KR(ret));
        }
      }
      case share::ObTenantSwitchoverStatus::SWITCHING_TO_STANDBY_STATUS: {
        ObTenantRoleTransitionService role_transition_service;
        if (OB_FAIL(ret) || is_verify) {
        } else if (OB_FAIL(role_transition_service.init(
            switch_optype,
            is_verify,
            &cost_detail))) {
          LOG_WARN("fail to init role_transition_service", KR(ret), K(switch_optype),
              KP(sql_proxy_), KP(GCTX.srv_rpc_proxy_), K(cost_detail));
        } else {
          if (OB_FAIL(role_transition_service.do_switch_access_mode_to_raw_rw(tenant_info))) {
            LOG_WARN("failed to do_switch_access_mode", KR(ret), K(tenant_info));
          }
          if (FAILEDx(role_transition_service.switchover_update_tenant_status(
                                                     false /* switch_to_standby */,
                                                     share::STANDBY_TENANT_ROLE,
                                                     tenant_info.get_switchover_status(),
                                                     share::NORMAL_SWITCHOVER_STATUS,
                                                     tenant_info))) {
            LOG_WARN("fail to switchover_update_tenant_status", KR(ret), K(tenant_info));
          }
          switch_scn = role_transition_service.get_so_scn();

          // Schema refresh trigger is now managed by MTL framework
          // It checks tenant role at runtime to decide whether to refresh schema
        }
        break;
      }
      default: {
        ret = OB_OP_NOT_ALLOW;
        LOG_WARN("switchover status not match", KR(ret), K(tenant_info));
        LOG_USER_ERROR(OB_OP_NOT_ALLOW, "switchover status not match, switchover to standby is");
        break;
      }
    }
  }

  return ret;
}

int ObStandbyService::update_tenant_status_before_sw_to_standby_(
    const ObTenantSwitchoverStatus cur_switchover_status,
    const ObTenantRole cur_tenant_role,
    ObAllTenantInfo &new_tenant_info)
{
  int ret = OB_SUCCESS;
  ObAllTenantInfo tenant_info;

  if (OB_UNLIKELY(!cur_switchover_status.is_valid()
                  || !cur_tenant_role.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(cur_switchover_status), K(cur_tenant_role));
  } else if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(inited));
  } else {
    if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                    false, tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    } else if (cur_switchover_status != tenant_info.get_switchover_status()) {
      ret = OB_NEED_RETRY;
      LOG_WARN("tenant not expect switchover status", KR(ret), K(tenant_info), K(cur_switchover_status));
    } else if (cur_tenant_role != tenant_info.get_tenant_role()) {
      ret = OB_NEED_RETRY;
      LOG_WARN("tenant not expect tenant role", KR(ret), K(tenant_info), K(cur_tenant_role));
    } else if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_role(
                  PRIMARY_TENANT_ROLE, cur_switchover_status,
                  share::PREP_SWITCHING_TO_STANDBY_SWITCHOVER_STATUS))) {
      LOG_WARN("failed to update tenant role", KR(ret), K(tenant_info));
    } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                    false, new_tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    }
  }

  // Removed transaction handling - not needed for KV storage
  CLUSTER_EVENT_ADD_LOG(ret, "update tenant before switchover to standby",
      "tenant id", OB_SYS_TENANT_ID,
      K(cur_switchover_status), K(cur_tenant_role));
  return ret;
}

int ObStandbyService::switch_to_standby_prepare_ls_status_(
    const ObTenantSwitchoverStatus &status,
    ObAllTenantInfo &new_tenant_info)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTenantSchema *tenant_schema = NULL;

  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service_ is NULL", KR(ret));
  } else if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("inner stat error", KR(ret), K_(inited));
  } else if (OB_UNLIKELY(!status.is_prepare_switching_to_standby_status())) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("switchover status not match, switchover to standby not allow", KR(ret), K(status));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "switchover status not match, switchover to standby is");
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_info(OB_SYS_TENANT_ID, tenant_schema))) {
    LOG_WARN("failed to get tenant ids", KR(ret));
  } else if (OB_ISNULL(tenant_schema)) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("tenant not exist", KR(ret));
  } else {
    // Simplified for single LS scenario:
    // - Only SYS_LS exists, no need to process multiple LS status consistency
    // - No transfer functionality, so no need to fix LS status mismatch
    // - Directly proceed to update tenant role
    LOG_INFO("skip process_status_to_steady for single LS scenario");
    if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_role(
                    share::STANDBY_TENANT_ROLE, status,
                    share::SWITCHING_TO_STANDBY_SWITCHOVER_STATUS))) {
      LOG_WARN("failed to update tenant role", KR(ret));
    } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                       false, new_tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    }

    DEBUG_SYNC(SWITCHING_TO_STANDBY);
  }

  return ret;
}

}
}
