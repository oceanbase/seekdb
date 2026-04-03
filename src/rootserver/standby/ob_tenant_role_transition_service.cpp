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
#include "ob_tenant_role_transition_service.h"
#include "logservice/ob_log_service.h"
#include "rootserver/ob_cluster_event.h"// CLUSTER_EVENT_ADD_CONTROL
#include "rootserver/ob_tenant_event_def.h" // TENANT_EVENT
// Removed ObLSServiceHelper include - no transfer functionality needed for single LS
#include "share/ob_global_stat_proxy.h"//ObGlobalStatProxy
#include "share/ob_all_tenant_info.h"  // ObAllTenantInfo, ObAllTenantInfoProxy
#include "storage/tx/ob_timestamp_service.h"  // ObTimestampService
#include "rootserver/standby/ob_standby_service.h" // ObStandbyService
#include "share/oracle_errno.h"//oracle error code
// Removed ob_service_name_command.h include - ObServiceName functionality not needed for single tenant/single LS scenario
#include "share/restore/ob_log_restore_source.h" // ObLogRestoreSourceItem
#include "share/backup/ob_log_restore_struct.h" // ObRestoreSourceServiceAttr
#include "share/config/ob_server_config.h" // GCONF
#include "rootserver/standby/ob_service_grpc.h"  // ObServiceGrpcClient
#include "share/ob_ls_id.h"  // SYS_LS
#include "share/scn.h"  // SCN
#include "lib/ob_errno.h"  // OB_SUCCESS, OB_ERR_UNEXPECTED
#include "storage/tx_storage/ob_ls_service.h"  // ObLSService
#include "storage/ls/ob_ls.h"  // ObLS
#include "common/ob_smart_call.h"  // MTL_SWITCH
#include "logservice/ob_log_handler.h"  // ObLogHandler, palf_handle_
#include "observer/ob_service.h"  // ObService::get_ls_sync_scn
#include "share/ob_server_struct.h"  // GCTX

using namespace oceanbase::common::sqlclient;

namespace oceanbase
{
using namespace share;
using namespace palf;
using namespace common;
using namespace tenant_event;

namespace share
{
// Function to get SYS_LS recovery stat from memory (simplified for single LS scenario)
int get_ls_recovery_stat(
    ObLSRecoveryStat &ls_recovery_stat)
{
  int ret = OB_SUCCESS;
  // Get recovery stat from LS meta (simplified for single LS scenario)
  ls_recovery_stat.ls_id_ = SYS_LS;
  // For single LS scenario, get SCN from LS meta directly via MTL
  MTL_SWITCH(OB_SYS_TENANT_ID) {
    storage::ObLSService *ls_service = MTL(storage::ObLSService*);
    storage::ObLSHandle ls_handle;
    if (OB_ISNULL(ls_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls_service is null", KR(ret));
    } else if (OB_FAIL(ls_service->get_ls(SYS_LS, ls_handle, storage::ObLSGetMod::RS_MOD))) {
      LOG_WARN("get ls failed", KR(ret));
    } else {
      storage::ObLS *ls = ls_handle.get_ls();
      if (OB_ISNULL(ls)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls is null", KR(ret));
      } else {
        // Get sync_scn and readable_scn from LS log handler (simplified for single LS scenario)
        share::SCN sync_scn;
        share::SCN readable_scn;
        share::SCN create_scn;
        if (OB_FAIL(ls->get_end_scn(sync_scn))) {
          LOG_WARN("get end_scn failed", KR(ret));
        } else if (OB_FAIL(ls->get_max_decided_scn(readable_scn))) {
          LOG_WARN("get max_decided_scn failed", KR(ret));
        } else {
          const storage::ObLSMeta &ls_meta = ls->get_ls_meta();
          create_scn = ls_meta.get_clog_checkpoint_scn();
          ls_recovery_stat.sync_scn_ = sync_scn;
          ls_recovery_stat.readable_scn_ = readable_scn;
          ls_recovery_stat.create_scn_ = create_scn;
        }
      }
    }
  }
  return ret;
}
} // namespace share

namespace rootserver
{
/*
  The macro's usage scenario: It is used in the process of switchover to primary,
  to translate the connection information and certain error checking messages
  from the source primary tenant into USER ERROR,
  facilitating the troubleshooting of cross-tenant connection issues.
*/
#define SOURCE_TENANT_CHECK_USER_ERROR_FOR_SWITCHOVER_TO_PRIMARY                                                                          \
  int tmp_ret = OB_SUCCESS;                                                                                                               \
  ObSqlString str;                                                                                                                        \
  switch (ret) {                                                                                                                          \
    case -ER_TABLEACCESS_DENIED_ERROR:                                                                                                    \
    case OB_ERR_NO_TABLE_PRIVILEGE:                                                                                                       \
    case -OER_TABLE_OR_VIEW_NOT_EXIST:                                                                                                    \
    case -ER_DBACCESS_DENIED_ERROR:                                                                                                       \
    case OB_ERR_NO_DB_PRIVILEGE:                                                                                                          \
    case OB_ERR_NULL_VALUE:                                                                                                               \
    case OB_ERR_WAIT_REMOTE_SCHEMA_REFRESH:                                                                                               \
    case -ER_CONNECT_FAILED:                                                                                                              \
    case OB_PASSWORD_WRONG:                                                                                                               \
    case -ER_ACCESS_DENIED_ERROR:                                                                                                         \
    case OB_ERR_NO_LOGIN_PRIVILEGE:                                                                                                       \
    case -OER_INTERNAL_ERROR_CODE:                                                                                                        \
      if (OB_TMP_FAIL(str.assign_fmt("query primary failed(original error code: %d), switchover to primary is", ret))) {                         \
        LOG_WARN("tenant role trans user error str assign failed");                                                                       \
      } else {                                                                                                                            \
        ret = OB_OP_NOT_ALLOW;                                                                                                            \
        LOG_USER_ERROR(OB_OP_NOT_ALLOW, str.ptr());                                                                                       \
      }                                                                                                                                   \
      break;                                                                                                                               \
    case -ER_ACCOUNT_HAS_BEEN_LOCKED:                                                                                                      \
      ret = OB_OP_NOT_ALLOW;                                                                                                              \
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, "primary tenant's user account is locked, switchover to primary is");                                                       \
      break;                                                                                                                     \
    case OB_ERR_TENANT_IS_LOCKED:                                                                                                         \
      ret = OB_OP_NOT_ALLOW;                                                                                                              \
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, "primary tenant is locked, switchover to primary is");                                                       \
      break;                                                                                                                              \
    case OB_SOURCE_LS_STATE_NOT_MATCH:                                                                                                    \
      LOG_USER_ERROR(OB_SOURCE_LS_STATE_NOT_MATCH);                                                                                       \
      break;                                                                                                                              \
  }


const char* const ObTenantRoleTransitionConstants::SWITCH_TO_PRIMARY_LOG_MOD_STR = "SWITCH_TO_PRIMARY";
const char* const ObTenantRoleTransitionConstants::SWITCH_TO_STANDBY_LOG_MOD_STR = "SWITCH_TO_STANDBY";
const char* const ObTenantRoleTransitionConstants::RESTORE_TO_STANDBY_LOG_MOD_STR = "RESTORE_TO_STANDBY";

///////////ObTenantRoleTransCostDetail/////////////////
const char* ObTenantRoleTransCostDetail::type_to_str(CostType type) const
{
  static const char *strs[] = { "WAIT_LOG_SYNC", "LOG_FLASHBACK",
      "WAIT_LOG_END", "CHANGE_ACCESS_MODE" }; // LOG_FLASHBACK kept for enum compatibility but not used
  STATIC_ASSERT(MAX_COST_TYPE == ARRAYSIZEOF(strs), "status string array size mismatch");
  const char* str = "UNKNOWN";
  if (type < 0 || type >= MAX_COST_TYPE) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid type", K(type));
  } else {
    str = strs[type];
  }
  return str;
}
void ObTenantRoleTransCostDetail::add_cost(CostType type, int64_t cost)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(type < 0 || type >= MAX_COST_TYPE || cost < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", KR(ret), K(type), K(cost));
  } else {
    cost_type_[type] = cost;
  }
}
int64_t ObTenantRoleTransCostDetail::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  int64_t counted_cost = 0;
  for (int i = 0 ; i < MAX_COST_TYPE; i++) {
    if (cost_type_[i] > 0) {
      counted_cost += cost_type_[i];
      CostType type = static_cast<CostType>(i);
      BUF_PRINTF("%s: %ld, ", type_to_str(type), cost_type_[i]);
    }
  }
  BUF_PRINTF("OTHERS: %ld", end_ - start_ - counted_cost);
  return pos;
}

////////////ObTenantRoleTransitionService//////////////

int ObTenantRoleTransitionService::init(
      const obrpc::ObSwitchRoleArg::OpType &switch_optype,
      const bool is_verify,
      ObTenantRoleTransCostDetail *cost_detail)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cost_detail)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret),
        K(switch_optype), KP(cost_detail));
  } else {
    switch_optype_ = switch_optype;
    so_scn_.set_min();
    cost_detail_ = cost_detail;
    has_restore_source_ = false;
    is_verify_ = is_verify;
  }
  return ret;
}
int ObTenantRoleTransitionService::check_inner_stat()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cost_detail_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected", KR(ret),
        KP(cost_detail_));
  }
  return ret;
}
int ObTenantRoleTransitionService::failover_to_primary()
{
  int ret = OB_SUCCESS;
  LOG_INFO("[ROLE_TRANSITION] start to failover to primary", KR(ret), K(is_verify_));
  const int64_t start_service_time = ObTimeUtility::current_time();
  ObAllTenantInfo tenant_info;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                                                    false, tenant_info))) {
    LOG_WARN("failed to load tenant info", KR(ret));
  } else if (tenant_info.is_primary() && tenant_info.is_valid()) {
    LOG_INFO("is primary tenant, no need failover", K(tenant_info));
    // Skip failover for valid primary tenant from KV
    ret = OB_SUCCESS;  // Not an error, just skip
  }

  if (OB_SUCC(ret) && !tenant_info.is_primary()) {
    if (tenant_info.is_normal_status()) {
      if (OB_FAIL(do_failover_to_primary_(tenant_info))) {
        LOG_WARN("fail to do failover to primary", KR(ret), K(tenant_info));
      }
    } else if (tenant_info.is_prepare_flashback_for_failover_to_primary_status()
               || tenant_info.is_prepare_flashback_for_switch_to_primary_status()) {
      // Re-entry: prepare flashback stage
      if (!is_verify_ && OB_FAIL(do_prepare_flashback_(tenant_info))) {
        LOG_WARN("fail to prepare flashback", KR(ret), K(tenant_info));
      }
    } else if (tenant_info.is_flashback_status()) {
      // Re-entry: flashback stage
      if (!is_verify_ && OB_FAIL(do_flashback_())) {
        LOG_WARN("fail to flashback", KR(ret), K(tenant_info));
      }
    } else if (tenant_info.is_switching_to_primary_status()) {
      // Re-entry: switch access mode stage
      if (!is_verify_ && OB_FAIL(do_switch_access_mode_to_append(tenant_info, share::PRIMARY_TENANT_ROLE))) {
        LOG_WARN("fail to switch access mode", KR(ret), K(tenant_info));
      }
    } else {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("switchover status not match", KR(ret), K(is_verify_), K(tenant_info));
      TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(OB_OP_NOT_ALLOW, "switchover status not match", switch_optype_);
    }
  }

  const int64_t cost = ObTimeUtility::current_time() - start_service_time;
  LOG_INFO("[ROLE_TRANSITION] finish failover to primary", KR(ret), K(tenant_info), K(is_verify_), K(cost));
  return ret;
}
ERRSIM_POINT_DEF(ERRSIM_TENANT_ROLE_TRANS_WAIT_SYNC_ERROR);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_PREPARE_FLASHBACK);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_FLASHBACK);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_SWITCHING_TO_PRIMARY);
int ObTenantRoleTransitionService::do_failover_to_primary_(share::ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_PRIMARY != switch_optype_
                         && obrpc::ObSwitchRoleArg::OpType::FAILOVER_TO_PRIMARY != switch_optype_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected switch tenant action", KR(ret),
             K_(switch_optype), K(tenant_info));
  } else if (OB_UNLIKELY(!(tenant_info.is_normal_status())
      || tenant_info.is_primary())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant switchover status not valid", KR(ret), K(tenant_info));
  } else if (obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_PRIMARY == switch_optype_
      && OB_FAIL(wait_sys_ls_sync_to_latest_until_timeout_(tenant_info))) {
    LOG_WARN("fail to execute wait_sys_ls_sync_to_latest_until_timeout_", KR(ret), K(tenant_info));
    SOURCE_TENANT_CHECK_USER_ERROR_FOR_SWITCHOVER_TO_PRIMARY;
  }
  /*The switchover to primary verify command ends here.
    This command cannot update the switchover status nor execute the further logic.
    We update the switchover status right after sys ls being synced, The reason is as follows:
        The tenant fetches log with reference to tenant_sync_scn + 3s.
        If two ls' sync_scn have an extremely large difference,
        e.g. tenant_sync_scn = ls_1001 sync_scn + 3s << ls_1002 sync_scn,
        there is a possibility that ls_1002's log cannot be fetched completely.
    To ensure all ls' log are fetched completely, we update the switchover status as PREPARE_xxx.
    Then the tenant fetching log will no longer utilize tenant_sync_scn + 3s as a reference point.
  **/
  if (OB_FAIL(ret) || is_verify_) {
  } else {
    // Update tenant status to PREPARE_FLASHBACK_FOR_XXX (keep STANDBY role)
    const share::ObTenantSwitchoverStatus prepare_flashback_status =
        (obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_PRIMARY == switch_optype_)
        ? share::PREPARE_FLASHBACK_FOR_SWITCH_TO_PRIMARY_SWITCHOVER_STATUS
        : share::PREPARE_FLASHBACK_FOR_FAILOVER_TO_PRIMARY_SWITCHOVER_STATUS;
    if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_status(
            share::STANDBY_TENANT_ROLE, tenant_info.get_switchover_status(),
            prepare_flashback_status))) {
      LOG_WARN("failed to update tenant status", KR(ret), K(tenant_info));
    } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
        false, tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_PREPARE_FLASHBACK)) {
      LOG_WARN("errsim: after persist prepare flashback", KR(ret));
    } else if (OB_FAIL(do_prepare_flashback_(tenant_info))) {
      LOG_WARN("failed to prepare flashback", KR(ret), K(tenant_info));
    }
  }
  return ret;
}

int ObTenantRoleTransitionService::do_prepare_flashback_(share::ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(!(tenant_info.is_prepare_flashback_for_failover_to_primary_status()
                           || tenant_info.is_prepare_flashback_for_switch_to_primary_status()))) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("switchover status not match, switch tenant is not allowed", KR(ret), K(tenant_info));
    TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(OB_OP_NOT_ALLOW, "switchover status not match", switch_optype_);
  } else if (obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_PRIMARY == switch_optype_) {
    if (OB_FAIL(do_prepare_flashback_for_switch_to_primary_(tenant_info))) {
      LOG_WARN("failed to do_prepare_flashback_for_switch_to_primary_", KR(ret), K(tenant_info));
    }
  } else if (obrpc::ObSwitchRoleArg::OpType::FAILOVER_TO_PRIMARY == switch_optype_) {
    if (OB_FAIL(do_prepare_flashback_for_failover_to_primary_(tenant_info))) {
      LOG_WARN("failed to do_prepare_flashback_for_failover_to_primary_", KR(ret), K(tenant_info));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected switch tenant action", KR(ret), K_(switch_optype), K(tenant_info));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_FLASHBACK)) {
    LOG_WARN("errsim: after persist flashback", KR(ret));
  } else if (OB_FAIL(do_flashback_())) {
    LOG_WARN("failed to do flashback", KR(ret), K(tenant_info));
  }
  return ret;
}

int ObTenantRoleTransitionService::do_prepare_flashback_for_switch_to_primary_(
    share::ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  LOG_INFO("start to do_prepare_flashback_for_switch_to_primary_", KR(ret));

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(!tenant_info.is_prepare_flashback_for_switch_to_primary_status())) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("switchover status not match, switch to primary not allow", KR(ret), K(tenant_info));
    TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(OB_OP_NOT_ALLOW, "switchover status not match", switch_optype_);
  } else if (OB_FAIL(switchover_update_tenant_status(
                          true /* switch_to_primary */,
                          tenant_info.get_tenant_role(),
                          tenant_info.get_switchover_status(),
                          share::FLASHBACK_SWITCHOVER_STATUS,
                          tenant_info))) {
    LOG_WARN("failed to switchover_update_tenant_status", KR(ret), K(tenant_info));
  }
  return ret;
}

int ObTenantRoleTransitionService::do_prepare_flashback_for_failover_to_primary_(
    share::ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(!tenant_info.is_prepare_flashback_for_failover_to_primary_status())) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("switchover status not match, failover to primary not allow", KR(ret), K(tenant_info));
    TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(OB_OP_NOT_ALLOW, "switchover status not match", switch_optype_);
  } else if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_switchover_status(
                     tenant_info.get_switchover_status(), share::FLASHBACK_SWITCHOVER_STATUS))) {
    LOG_WARN("failed to update tenant switchover status", KR(ret), K(tenant_info));
  } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(false, tenant_info))) {
    LOG_WARN("failed to load tenant info", KR(ret));
  }

  return ret;
}

int ObTenantRoleTransitionService::do_flashback_()
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  ObAllTenantInfo tenant_info;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(false, tenant_info))) {
    LOG_WARN("failed to load tenant info", KR(ret));
  } else if (OB_UNLIKELY(!tenant_info.is_flashback_status())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant switchover status not valid", KR(ret), K(tenant_info));
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.internal_sql_execute_timeout))) {
    LOG_WARN("failed to set default timeout", KR(ret));
  } else {
    int64_t begin_time = ObTimeUtility::current_time();
    // For single LS/single replica, implement flashback directly via PALF:
    // 1. Change access mode: RAW_WRITE -> PREPARE_FLASHBACK -> FLASHBACK
    // 2. Call palf_handle.flashback() (no-op for single replica since flashback_scn >= max_scn)
    MTL_SWITCH(OB_SYS_TENANT_ID) {
      storage::ObLSService *ls_service = MTL(storage::ObLSService*);
      storage::ObLSHandle ls_handle;
      storage::ObLS *sys_ls = nullptr;
      logservice::ObLogHandler *log_handler = nullptr;

      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls_service is null", KR(ret));
      } else if (OB_FAIL(ls_service->get_ls(share::SYS_LS, ls_handle, storage::ObLSGetMod::RS_MOD))) {
        LOG_WARN("get sys ls failed", KR(ret));
      } else if (OB_ISNULL(sys_ls = ls_handle.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sys_ls is null", KR(ret));
      } else if (OB_ISNULL(log_handler = sys_ls->get_log_handler())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log_handler is null", KR(ret));
      } else {
        int64_t mode_version = palf::INVALID_PROPOSAL_ID;
        palf::AccessMode current_mode = palf::AccessMode::INVALID_ACCESS_MODE;
        share::SCN flashback_scn;

        // Get current access mode and end_scn (as flashback point)
        if (OB_FAIL(log_handler->get_access_mode(mode_version, current_mode))) {
          LOG_WARN("get_access_mode failed", KR(ret));
        } else if (OB_FAIL(log_handler->get_end_scn(flashback_scn))) {
          LOG_WARN("get_end_scn failed", KR(ret));
        }

        // Step 1: Change to PREPARE_FLASHBACK (if not already)
        if (OB_SUCC(ret) && current_mode != palf::AccessMode::PREPARE_FLASHBACK
                         && current_mode != palf::AccessMode::FLASHBACK) {
          if (OB_FAIL(log_handler->change_access_mode(mode_version,
                  palf::AccessMode::PREPARE_FLASHBACK, share::SCN::min_scn()))) {
            LOG_WARN("failed to change access mode to PREPARE_FLASHBACK", KR(ret));
          } else {
            LOG_INFO("[ROLE_TRANSITION] changed access mode to PREPARE_FLASHBACK");
            // Refresh mode_version after mode change
            if (OB_FAIL(log_handler->get_access_mode(mode_version, current_mode))) {
              LOG_WARN("get_access_mode failed after PREPARE_FLASHBACK", KR(ret));
            }
          }
        }

        // Step 2: Change to FLASHBACK (if not already)
        if (OB_SUCC(ret) && current_mode != palf::AccessMode::FLASHBACK) {
          if (OB_FAIL(log_handler->change_access_mode(mode_version,
                  palf::AccessMode::FLASHBACK, share::SCN::min_scn()))) {
            LOG_WARN("failed to change access mode to FLASHBACK", KR(ret));
          } else {
            LOG_INFO("[ROLE_TRANSITION] changed access mode to FLASHBACK");
            // Refresh mode_version for palf flashback call
            if (OB_FAIL(log_handler->get_access_mode(mode_version, current_mode))) {
              LOG_WARN("get_access_mode failed after FLASHBACK", KR(ret));
            }
          }
        }

        // Step 3: Call palf flashback (for single replica, flashback_scn >= max_scn, this is a no-op)
        if (OB_SUCC(ret)) {
          if (OB_FAIL(log_handler->palf_handle_.flashback(mode_version, flashback_scn,
                  ctx.get_timeout()))) {
            LOG_WARN("failed to flashback", KR(ret), K(mode_version), K(flashback_scn));
          } else {
            LOG_INFO("[ROLE_TRANSITION] palf flashback success", K(flashback_scn));
          }
        }
      }
    }
    int64_t log_flashback = ObTimeUtility::current_time() - begin_time;
    if (OB_LIKELY(NULL != cost_detail_)) {
      (void) cost_detail_->add_cost(ObTenantRoleTransCostDetail::LOG_FLASHBACK, log_flashback);
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    CLUSTER_EVENT_ADD_LOG(ret, "flashback end");
    // Update status to SWITCHING_TO_PRIMARY
    ObAllTenantInfo new_tenant_info;
    if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_switchover_status(
            tenant_info.get_switchover_status(), share::SWITCHING_TO_PRIMARY_SWITCHOVER_STATUS))) {
      LOG_WARN("failed to update tenant switchover status", KR(ret), K(tenant_info));
    } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(false, new_tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_SWITCHING_TO_PRIMARY)) {
      LOG_WARN("errsim: after persist switching to primary", KR(ret));
    } else if (OB_FAIL(do_switch_access_mode_to_append(new_tenant_info, share::PRIMARY_TENANT_ROLE))) {
      LOG_WARN("failed to switch access mode to append", KR(ret), K(new_tenant_info));
    }
  }
  return ret;
}

int ObTenantRoleTransitionService::do_switch_access_mode_to_append(
    const share::ObAllTenantInfo &tenant_info,
    const share::ObTenantRole &target_tenant_role)
{
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  palf::AccessMode access_mode = logservice::ObLogService::get_palf_access_mode(target_tenant_role);
  SCN ref_scn;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(!tenant_info.is_switching_to_primary_status()
        || target_tenant_role == tenant_info.get_tenant_role())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant switchover status not valid", KR(ret), K(tenant_info),
        K(target_tenant_role));
  } else if (OB_FAIL(get_tenant_ref_scn_(ref_scn))) {
    LOG_WARN("failed to get tenant ref_scn", KR(ret));
    //TODO(yaoying):xianming
  } else if (OB_FAIL(get_all_ls_status_and_change_access_mode_(
      access_mode,
      ref_scn,
      share::SCN::min_scn()))) {
    LOG_WARN("fail to execute get_all_ls_status_and_change_access_mode_", KR(ret),
        K(tenant_info), K(access_mode), K(ref_scn));
  } else {
    share::ObAllTenantInfo cur_tenant_info;
    if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(false, cur_tenant_info))) {
      LOG_WARN("failed to load all tenant info", KR(ret));
    } else if (OB_UNLIKELY(tenant_info.get_switchover_status() != cur_tenant_info.get_switchover_status())) {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("Tenant status changed by concurrent operation, switch to primary is not allowed",
              KR(ret), K(tenant_info), K(cur_tenant_info));
      TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(OB_OP_NOT_ALLOW,
          "tenant status changed by concurrent operation", switch_optype_);
    } else if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_role(
        share::PRIMARY_TENANT_ROLE, tenant_info.get_switchover_status(),
        share::NORMAL_SWITCHOVER_STATUS))) {
      LOG_WARN("failed to update tenant switchover status", KR(ret), K(tenant_info), K(cur_tenant_info));
      // Removed update_tenant_recovery_until_scn - recovery not supported in single tenant/single LS scenario
    }
  }
  if (OB_LIKELY(NULL != cost_detail_)) {
    int64_t log_mode_change = ObTimeUtility::current_time() - begin_time;
    (void) cost_detail_->add_cost(ObTenantRoleTransCostDetail::CHANGE_ACCESS_MODE, log_mode_change);
  }
  return ret;
}

int ObTenantRoleTransitionService::get_tenant_ref_scn_(share::SCN &ref_scn)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else {
    share::ObLSRecoveryStat ls_recovery_stat;
    if (OB_FAIL(share::get_ls_recovery_stat(
            ls_recovery_stat))) {
      LOG_WARN("failed to get ls recovery stat", KR(ret));
    } else {
      ref_scn = share::SCN::max(ls_recovery_stat.get_sync_scn(), ls_recovery_stat.get_create_scn());
      if (!ref_scn.is_valid_and_not_min()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid ref_scn from LS recovery stat", KR(ret), K(ref_scn), K(ls_recovery_stat));
      }
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_TENANT_NOT_REPLAY_TO_LATEST);

int ObTenantRoleTransitionService::do_switch_access_mode_to_raw_rw(
    const share::ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  palf::AccessMode target_access_mode = logservice::ObLogService::get_palf_access_mode(STANDBY_TENANT_ROLE);
  share::ObLSStatusInfo sys_status_info;
  sys_status_info.ls_id_ = share::SYS_LS;
  sys_status_info.status_ = share::OB_LS_NORMAL;
  share::SCN sys_ls_sync_scn = SCN::min_scn();
  bool is_sys_ls_sync_to_latest = false;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(!tenant_info.is_switching_to_standby_status()
        || !tenant_info.is_standby())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant switchover status not valid", KR(ret), K(tenant_info));
    // Removed create_abort_ls_in_switch_role - not needed for single tenant/single LS scenario
  } else if (OB_FAIL(change_ls_access_mode_(sys_status_info, target_access_mode, SCN::base_scn(), share::SCN::min_scn()))) {
    // step 1: change sys ls access mode
    LOG_WARN("fail to execute change_ls_access_mode_", KR(ret), K(sys_status_info),
        K(target_access_mode));
  } else if (OB_FAIL(get_sys_ls_sync_scn_(
      false /*need_check_sync_to_latest*/,
      sys_ls_sync_scn,
      is_sys_ls_sync_to_latest))) {
    LOG_WARN("fail to get sys_ls_sync_scn", KR(ret));
  } else if (OB_FAIL(get_all_ls_status_and_change_access_mode_(
      target_access_mode,
      SCN::base_scn(),
      sys_ls_sync_scn))) {
    // step 2: let user ls wait some time until their end scn being is larger than sys ls's
    //         then change user ls access mode
    // note: there's no need to remove sys ls in arg status_info_array, it will be removed in change_ls_access_mode_
    LOG_WARN("fail to execute get_all_ls_status_and_change_access_mode_", KR(ret), K(tenant_info),
        K(target_access_mode), K(sys_ls_sync_scn));
  }

  if (OB_LIKELY(NULL != cost_detail_)) {
    int64_t log_mode_change = ObTimeUtility::current_time() - begin_time;
    int64_t change_access_mode = log_mode_change - cost_detail_->get_wait_log_end();
    (void) cost_detail_->add_cost(ObTenantRoleTransCostDetail::CHANGE_ACCESS_MODE, change_access_mode);
  }
  DEBUG_SYNC(AFTER_CHANGE_ACCESS_MODE);
  return ret;
}

int ObTenantRoleTransitionService::get_all_ls_status_and_change_access_mode_(
    const palf::AccessMode target_access_mode,
    const share::SCN &ref_scn,
    const share::SCN &sys_ls_sync_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  share::ObLSStatusInfo status_info;
  status_info.ls_id_ = share::SYS_LS;
  status_info.status_ = share::OB_LS_NORMAL;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else {
    if (OB_FAIL(change_ls_access_mode_(status_info, target_access_mode, ref_scn, sys_ls_sync_scn))) {
      // ref_scn and target_access_mode will be checked in this func
      LOG_WARN("fail to execute change_ls_access_mode_", KR(ret), K(status_info),
          K(target_access_mode), K(ref_scn), K(sys_ls_sync_scn));
    }
    if (OB_TMP_FAIL(ls_status_stats_when_change_access_mode_(status_info))) {
      LOG_WARN("fail to gather ls status", KR(ret), KR(tmp_ret), K(status_info));
    }
  }
  return ret;
}

int ObTenantRoleTransitionService::change_ls_access_mode_(
    const share::ObLSStatusInfo &status_info,
    const palf::AccessMode target_access_mode,
    const share::SCN &ref_scn,
    const share::SCN &sys_ls_sync_scn)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(palf::AccessMode::INVALID_ACCESS_MODE == target_access_mode
                         || !ref_scn.is_valid_and_not_min())) {
    // no need to check sys_ls_sync_scn, since this is only valid when it's needed
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(target_access_mode), K(ref_scn));
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.internal_sql_execute_timeout))) {
    LOG_WARN("failed to set default timeout", KR(ret));
  } else {
    //ignore error, try until success
    bool need_retry = true;
    do {
      ObLSAccessModeInfo ls_mode_info;
      ObLSAccessModeInfo need_change_info;
      //1. get current access mode
      if (ctx.is_timeouted()) {
        need_retry = false;
        ret = OB_TIMEOUT;
        LOG_WARN("already timeout", KR(ret));
      } else if (OB_FAIL(get_ls_access_mode_(status_info, ls_mode_info))) {
        LOG_WARN("failed to get ls access mode", KR(ret));
      } else {
        if (ls_mode_info.get_access_mode() == target_access_mode) {
          //nothing, no need change
        } else if (OB_FAIL(need_change_info.assign(ls_mode_info))) {
          LOG_WARN("failed to assign need_change_info", KR(ret));
        } else {
          need_change_info.assign(ls_mode_info);
          //2. check status not change
          ObAllTenantInfo new_tenant_info;
          if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                  false, new_tenant_info))) {
            LOG_WARN("failed to load tenant info", KR(ret));
          }
          // Removed epoch check - epoch field removed
          //3. change access mode
          if (OB_SUCC(ret)) {
            if (OB_FAIL(do_change_ls_access_mode_(need_change_info,
                    target_access_mode, ref_scn, sys_ls_sync_scn))) {
              LOG_WARN("failed to change ls access mode", KR(ret), K(need_change_info),
                  K(target_access_mode), K(ref_scn), K(sys_ls_sync_scn));
            }
          }
        }
        if (OB_SUCC(ret)) {
          break;
        }
      }
    } while (need_retry);

  }
  LOG_INFO("[ROLE_TRANSITION] finish change ls mode", KR(ret),
      K(target_access_mode), K(ref_scn));
  return ret;
}

int ObTenantRoleTransitionService::ls_status_stats_when_change_access_mode_(
    const share::ObLSStatusInfo &status_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else {
    FLOG_INFO("gather ls_status_stats", KR(ret), K(status_info));
  }
  return ret;
}

int ObTenantRoleTransitionService::get_ls_access_mode_(
    const share::ObLSStatusInfo &status_info,
    obrpc::ObLSAccessModeInfo &ls_access_info)
{
  int ret = OB_SUCCESS;
  // Simplified for single LS scenario: directly get access mode from local LS
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else {
    const share::ObLSStatusInfo &info = status_info;
    MTL_SWITCH(OB_SYS_TENANT_ID) {
      storage::ObLSService *ls_service = MTL(storage::ObLSService*);
      storage::ObLSHandle ls_handle;
      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls_service is null", KR(ret));
      } else if (OB_FAIL(ls_service->get_ls(share::SYS_LS, ls_handle, storage::ObLSGetMod::RS_MOD))) {
        LOG_WARN("get sys ls failed", KR(ret));
      } else {
        storage::ObLS *sys_ls = ls_handle.get_ls();
        if (OB_ISNULL(sys_ls)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sys_ls is null", KR(ret));
        } else {
          logservice::ObLogHandler *log_handler = sys_ls->get_log_handler();
          if (OB_ISNULL(log_handler)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("log_handler is null", KR(ret));
          } else {
            int64_t mode_version = palf::INVALID_PROPOSAL_ID;
            palf::AccessMode access_mode = palf::AccessMode::INVALID_ACCESS_MODE;
            share::SCN ref_scn;
            share::SCN sys_ls_end_scn;
            if (OB_FAIL(log_handler->get_access_mode(mode_version, access_mode))) {
              LOG_WARN("get_access_mode failed", KR(ret));
            } else if (OB_FAIL(log_handler->get_end_scn(sys_ls_end_scn))) {
              LOG_WARN("get_end_scn failed", KR(ret));
            } else {
              // Get ref_scn if access_mode is APPEND
              if (palf::AccessMode::APPEND == access_mode) {
                if (OB_FAIL(log_handler->get_append_mode_initial_scn(ref_scn))) {
                  LOG_WARN("get_append_mode_initial_scn failed", KR(ret));
                  ref_scn = share::SCN::min_scn(); // Fallback to min_scn
                }
              } else {
                ref_scn = share::SCN::min_scn();
              }

              if (OB_SUCC(ret)) {
                if (OB_FAIL(ls_access_info.init(OB_SYS_TENANT_ID, share::SYS_LS, mode_version, access_mode, ref_scn, sys_ls_end_scn))) {
                  LOG_WARN("failed to init ls_access_mode_info", KR(ret));
                } else {
                  LOG_INFO("[ROLE_TRANSITION] get ls access mode from local",
                          K(mode_version), K(access_mode), K(ref_scn), K(sys_ls_end_scn));
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObTenantRoleTransitionService::do_change_ls_access_mode_(
    const obrpc::ObLSAccessModeInfo &ls_access_info,
    const palf::AccessMode target_access_mode,
    const SCN &ref_scn,
    const share::SCN &sys_ls_sync_scn)
{
  int ret = OB_SUCCESS;
  // Simplified for single LS scenario: directly change access mode on local LS
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_UNLIKELY(palf::AccessMode::INVALID_ACCESS_MODE == target_access_mode
                         || !ref_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(target_access_mode), K(ls_access_info), K(ref_scn));
  } else {
    const obrpc::ObLSAccessModeInfo &info = ls_access_info;
    MTL_SWITCH(OB_SYS_TENANT_ID) {
      storage::ObLSService *ls_service = MTL(storage::ObLSService*);
      storage::ObLSHandle ls_handle;
      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls_service is null", KR(ret));
      } else if (OB_FAIL(ls_service->get_ls(share::SYS_LS, ls_handle, storage::ObLSGetMod::RS_MOD))) {
        LOG_WARN("get sys ls failed", KR(ret));
      } else {
        storage::ObLS *sys_ls = ls_handle.get_ls();
        if (OB_ISNULL(sys_ls)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sys_ls is null", KR(ret));
        } else {
          logservice::ObLogHandler *log_handler = sys_ls->get_log_handler();
          if (OB_ISNULL(log_handler)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("log_handler is null", KR(ret));
          } else {
            const int64_t begin_time = ObTimeUtility::current_time();
            int64_t mode_version = palf::INVALID_PROPOSAL_ID;
            palf::AccessMode current_mode = palf::AccessMode::INVALID_ACCESS_MODE;
            // Get current mode_version first
            if (OB_FAIL(log_handler->get_access_mode(mode_version, current_mode))) {
              LOG_WARN("get_access_mode failed", KR(ret));
            } else if (OB_FAIL(log_handler->change_access_mode(mode_version, target_access_mode, ref_scn))) {
              LOG_WARN("failed to change sys ls access mode", KR(ret), K(mode_version), K(target_access_mode), K(ref_scn));
            } else {
              const int64_t wait_scn_t = ObTimeUtility::current_time() - begin_time;
              if (OB_LIKELY(NULL != cost_detail_)) {
                (void) cost_detail_->add_cost(ObTenantRoleTransCostDetail::WAIT_LOG_END, wait_scn_t);
              }
              LOG_INFO("[ROLE_TRANSITION] change ls access mode success", KR(ret), K(info), K(target_access_mode), K(ref_scn));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObTenantRoleTransitionService::check_and_update_sys_ls_recovery_stat_in_switchover_(
    const bool switch_to_primary,
    const SCN &max_sys_ls_sync_scn/* SYS LS real max sync scn */,
    const SCN &target_tenant_sync_scn/* tenant target sync scn in switchover */)
{
  int ret = OB_SUCCESS;
  bool need_update = false;
  share::ObLSRecoveryStat ls_recovery_stat;

  if (OB_UNLIKELY(max_sys_ls_sync_scn > target_tenant_sync_scn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(max_sys_ls_sync_scn),
        K(target_tenant_sync_scn));
  }
  // Get latest SYS LS sync scn in table, which means SYS log recovery point
  else if (OB_FAIL(share::get_ls_recovery_stat(
      ls_recovery_stat))) {
    LOG_WARN("failed to get SYS ls recovery stat", KR(ret));
  } else if (max_sys_ls_sync_scn < ls_recovery_stat.get_sync_scn()) {
    // SYS LS sync scn can not be greater than real max sync scn
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected SYS LS sync scn in table, it is greater than max sync scn",
        K(max_sys_ls_sync_scn), K(ls_recovery_stat), K(switch_to_primary));
  }
  // When switchover from STANDBY to PRIMARY,
  // SYS LS sync scn in table must be same with its real max sync scn
  else if (switch_to_primary && max_sys_ls_sync_scn != ls_recovery_stat.get_sync_scn()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("SYS LS sync scn in table is not latest, unexpected error",
        K(switch_to_primary), K(ls_recovery_stat), K(max_sys_ls_sync_scn));
  } else if (max_sys_ls_sync_scn == ls_recovery_stat.get_sync_scn()) {
    need_update = false;
  } else {
    // SYS LS recovery point should be updated to latest during switchover.
    // Simplified for single LS scenario: recovery stat is read from memory (LS meta),
    // no need to update via DB transaction. The sync_scn will be updated automatically
    // when LS meta is persisted.
    need_update = true;
    LOG_INFO("SYS LS sync scn needs update, but skipped for single LS scenario (handled by LS meta persistence)",
             K(max_sys_ls_sync_scn), K(ls_recovery_stat), K(switch_to_primary));
  }

  LOG_INFO("check and update SYS LS sync scn in table", KR(ret), K(need_update),
      K(target_tenant_sync_scn), K(max_sys_ls_sync_scn), K(switch_to_primary), K(ls_recovery_stat));
  return ret;
}

int ObTenantRoleTransitionService::switchover_update_tenant_status(
    const bool switch_to_primary,
    const ObTenantRole &new_role,
    const ObTenantSwitchoverStatus &old_status,
    const ObTenantSwitchoverStatus &new_status,
    ObAllTenantInfo &new_tenant_info)
{
  int ret = OB_SUCCESS;
  ObAllTenantInfo tenant_info;
  SCN max_checkpoint_scn = SCN::min_scn();
  SCN max_sys_ls_sync_scn = SCN::min_scn();
  share::ObLSStatusInfo status_info;
  status_info.ls_id_ = share::SYS_LS;
  status_info.status_ = share::OB_LS_NORMAL;
  obrpc::ObCheckpoint switchover_checkpoint;
  bool is_sync_to_latest = false;
  SCN final_sync_scn;
  final_sync_scn.set_min();

  if (OB_UNLIKELY(!new_role.is_valid()
                  || !old_status.is_valid()
                  || !new_status.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(new_role), K(old_status), K(new_status));
  } else if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(get_checkpoint(status_info, false/* check_sync_to_latest */,
                                             switchover_checkpoint))) {
    LOG_WARN("fail to get_checkpoint", KR(ret), K(status_info));
  } else if (FALSE_IT(max_checkpoint_scn = switchover_checkpoint.get_cur_sync_scn())) {
  } else if (OB_FAIL(get_sys_ls_sync_scn_(switchover_checkpoint, max_sys_ls_sync_scn, is_sync_to_latest))) {
    LOG_WARN("failed to get_sys_ls_sync_scn_", KR(ret), K(switchover_checkpoint));
  } else if (OB_UNLIKELY(!max_checkpoint_scn.is_valid_and_not_min() || !max_sys_ls_sync_scn.is_valid_and_not_min())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid checkpoint_scn", KR(ret), K(max_checkpoint_scn),
                                       K(max_sys_ls_sync_scn), K(switchover_checkpoint));
  } else {
    ObAllTenantInfo tmp_tenant_info;
    /**
     * @description:
     * In order to make timestamp monotonically increasing before and after switch tenant role
     * set readable_scn to gts_upper_limit:
     *    MAX{MAX_SYS_LS_LOG_TS + 2 * preallocated_range}
     * set sync_snc to
     *    MAX{MAX{LS max_log_ts}, MAX_SYS_LS_LOG_TS + 2 * preallocated_range}
     * Because replayable point is not support, set replayable_scn = sync_scn
     */
    const SCN gts_upper_limit = transaction::ObTimestampService::get_sts_start_scn(max_sys_ls_sync_scn);
    final_sync_scn = MAX(max_checkpoint_scn, gts_upper_limit);
    if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                       true /* for update */, tmp_tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    } else if (OB_UNLIKELY(old_status != tmp_tenant_info.get_switchover_status())) {
      ret = OB_NEED_RETRY;
      LOG_WARN("switchover may concurrency, need retry", KR(ret),
               K(old_status), K(tmp_tenant_info));
    } else if (OB_FAIL(check_and_update_sys_ls_recovery_stat_in_switchover_(
        switch_to_primary, max_sys_ls_sync_scn, final_sync_scn))) {
      LOG_WARN("fail to check and update sys ls recovery stat in switchover", KR(ret),
          K(switch_to_primary), K(max_sys_ls_sync_scn));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_status(
                                                                  new_role,
                                                                  old_status,
                                                                  new_status))) {
      LOG_WARN("failed to update_tenant_status", KR(ret), K(new_role),
               K(old_status), K(new_status));
    } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(
                    false /* for update */, new_tenant_info))) {
      LOG_WARN("failed to load tenant info", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    so_scn_ = final_sync_scn;
  }
  CLUSTER_EVENT_ADD_LOG(ret, "update tenant status",
      K(new_role), K(old_status), K(new_status));
  return ret;
}

int ObTenantRoleTransitionService::wait_sys_ls_sync_to_latest_until_timeout_(
    const ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  bool only_check_sys_ls = true;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("inner stat error", KR(ret));
  } else if (obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_PRIMARY != switch_optype_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("only switchover to primary can execute this logic", KR(ret), K(switch_optype_));
  } else if (OB_UNLIKELY(!tenant_info.is_normal_status())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant switchover status is not normal", KR(ret), K(tenant_info));
  } else if (OB_FAIL(check_restore_source_for_switchover_to_primary_())) {
    LOG_WARN("fail to check restore source", KR(ret));
  } else if (!has_restore_source_) {
    LOG_INFO("no restore source", K(tenant_info));
  } else if (OB_FAIL(check_sync_to_latest_do_while_(tenant_info))) {
    LOG_WARN("fail to check whether sys ls is synced", KR(ret), K(tenant_info));
  }
  return ret;
}

int ObTenantRoleTransitionService::check_restore_source_for_switchover_to_primary_()
{
  int ret = OB_SUCCESS;
  ObSqlString standby_source_value;
  ObRestoreSourceServiceAttr service_attr;
  has_restore_source_ = true;

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("inner stat error", KR(ret));
  } else {
    // Read restore source from config parameter
    const common::ObString config_value = GCONF.log_restore_source.str();
    if (config_value.empty()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "empty log_restore_source is");
    } else if (OB_FAIL(standby_source_value.assign(config_value))) {
      LOG_WARN("fail to assign standby source value", K(config_value));
    } else if (OB_FAIL(service_attr.parse_service_attr_from_str(standby_source_value))) {
      LOG_WARN("fail to parse service attr", K(config_value), K(standby_source_value));
    } else {
      // net service mode, check whether previous primary tenant becomes standby
      // Use gRPC to get tenant info instead of MySQL client
      if (OB_UNLIKELY(service_attr.addr_.count() <= 0)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("service_attr has no server address", KR(ret), K(service_attr));
      } else {
        // Use first available server address
        const common::ObAddr& server_addr = service_attr.addr_.at(0);
        const int64_t timeout = 10 * 1000 * 1000; // 10 seconds

        rootserver::standby::ObServiceGrpcClient grpc_client;
        if (OB_FAIL(grpc_client.init(server_addr, timeout))) {
          LOG_WARN("fail to init grpc client", KR(ret), K(server_addr), K(service_attr));
        } else {
          share::ObAllTenantInfo tenant_info;
          if (OB_FAIL(grpc_client.get_tenant_info(tenant_info))) {
            LOG_WARN("fail to get tenant info via gRPC", KR(ret), K(server_addr));
          } else if (OB_UNLIKELY(!tenant_info.is_standby())) {
            ret = OB_OP_NOT_ALLOW;
            LOG_WARN("tenant role not match", KR(ret), K(tenant_info.get_tenant_role()), K(service_attr));
            LOG_USER_ERROR(OB_OP_NOT_ALLOW, "restore source is primary, switchover to primary is");
          } else if (OB_UNLIKELY(!tenant_info.is_normal_status())) {
            ret = OB_OP_NOT_ALLOW;
            LOG_WARN("tenant switchover status not match", KR(ret), K(tenant_info.get_switchover_status()), K(service_attr));
            LOG_USER_ERROR(OB_OP_NOT_ALLOW, "restore source is not in normal switchover status, switchover to primary is");
          }
        }
      }
    }
  }
  return ret;
}

int ObTenantRoleTransitionService::check_sync_to_latest_do_while_(
  const ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  bool is_sys_ls_synced = false;
  while (!THIS_WORKER.is_timeout() && !logservice::ObLogRestoreHandler::need_fail_when_switch_to_primary(ret)) {
    ret = OB_SUCCESS;
    if (OB_FAIL(check_sync_to_latest_(tenant_info, is_sys_ls_synced))) {
      LOG_WARN("fail to execute check_sync_to_latest_", KR(ret), K(tenant_info));
    } else {
      if (is_sys_ls_synced) {
        LOG_INFO("sync to latest");
        break;
      } else {
        LOG_WARN("not sync to latest, wait a while");
      }
    }
    ob_usleep(10L * 1000L);
  }
  if (logservice::ObLogRestoreHandler::need_fail_when_switch_to_primary(ret)) {
  } else if (THIS_WORKER.is_timeout() || !is_sys_ls_synced) {
    // return NOT_ALLOW instead of timeout
    if (OB_SUCC(ret)) {
      ret = OB_TIMEOUT; // to print in err_msg
    }
    ObSqlString err_msg;
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(err_msg.assign_fmt("wait tenant sync to latest failed(original error code: %d), switchover to primary is", ret))) {
      LOG_WARN("fail to assign error msg", KR(ret), KR(tmp_ret));
    } else {
      // convert OB_TIMEOUT or other failure code to OB_OP_NOT_ALLOW
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("has not sync to latest, can not swithover to primary", KR(ret));
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, err_msg.ptr());
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("finish check sync to latest");
  }
  return ret;
}
int ObTenantRoleTransitionService::check_sync_to_latest_(
    const ObAllTenantInfo &tenant_info,
    bool &is_sys_ls_synced)
{
  int ret = OB_SUCCESS;
  int64_t begin_ts = ObTimeUtility::current_time();
  ObLSRecoveryStat sys_ls_recovery_stat;
  SCN sys_ls_sync_scn = SCN::min_scn();
  bool sys_ls_sync_has_all_log = false;
  LOG_INFO("start to check_sync_to_latest", KR(ret), K(tenant_info));
  is_sys_ls_synced = false;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_FAIL(share::get_ls_recovery_stat(
        sys_ls_recovery_stat))) {
    LOG_WARN("failed to get ls recovery stat", KR(ret));
  } else if (OB_FAIL(get_sys_ls_sync_scn_(
      true /*need_check_sync_to_latest*/,
      sys_ls_sync_scn,
      sys_ls_sync_has_all_log))) {
    LOG_WARN("failed to get_sys_ls_sync_scn_", KR(ret));
  } else {
    is_sys_ls_synced = (sys_ls_sync_scn.is_valid_and_not_min() && sys_ls_sync_has_all_log
        && sys_ls_recovery_stat.get_sync_scn() == sys_ls_sync_scn);
  }
  int64_t cost = ObTimeUtility::current_time() - begin_ts;
  LOG_INFO("check sync to latest", KR(ret), K(is_verify_), K(cost),
      K(is_sys_ls_synced));
  return ret;
}
int ObTenantRoleTransitionService::get_checkpoint(
  const share::ObLSStatusInfo &status_info,
  const bool check_sync_to_latest,
  obrpc::ObCheckpoint &checkpoint)
{
  int ret = OB_SUCCESS;
  obrpc::ObGetLSSyncScnArg arg;
  obrpc::ObGetLSSyncScnRes res;

  LOG_INFO("start to get_checkpoint", KR(ret), K(check_sync_to_latest), K(status_info));
  if (OB_ISNULL(GCTX.ob_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ob_service_ is null", KR(ret));
  } else if (OB_FAIL(arg.init(OB_SYS_TENANT_ID, share::SYS_LS, check_sync_to_latest))) {
    LOG_WARN("failed to init ObGetLSSyncScnArg", KR(ret));
  } else if (OB_FAIL(GCTX.ob_service_->get_ls_sync_scn(arg, res))) {
    LOG_WARN("failed to call get_ls_sync_scn", KR(ret));
  } else {
    // When not checking sync_to_latest, cur_restore_source_next_scn is min_scn.
    // Set it to cur_sync_scn so that ObCheckpoint::is_sync_to_latest() returns true.
    const share::SCN restore_source_next_scn = check_sync_to_latest
        ? res.get_cur_restore_source_next_scn()
        : res.get_cur_sync_scn();
    checkpoint = obrpc::ObCheckpoint(share::SYS_LS, res.get_cur_sync_scn(), restore_source_next_scn);
    LOG_INFO("get checkpoint from local LS", K(checkpoint),
             "is_sync_to_latest", checkpoint.is_sync_to_latest());
  }
  return ret;
}

int ObTenantRoleTransitionService::get_sys_ls_sync_scn_(
    const bool need_check_sync_to_latest,
    share::SCN &sys_ls_sync_scn,
    bool &is_sync_to_latest)
{
  int ret = OB_SUCCESS;
  share::ObLSStatusInfo sys_ls_status;
  sys_ls_status.ls_id_ = share::SYS_LS;
  sys_ls_status.status_ = share::OB_LS_NORMAL;
  obrpc::ObCheckpoint switchover_checkpoint;
  sys_ls_sync_scn = SCN::min_scn();
  is_sync_to_latest = false;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("error unexpected", KR(ret));
  } else if (OB_FAIL(get_checkpoint(sys_ls_status, need_check_sync_to_latest,
                                            switchover_checkpoint))) {
    LOG_WARN("fail to get_checkpoint", KR(ret), K(sys_ls_status));
  } else {
    sys_ls_sync_scn = switchover_checkpoint.get_cur_sync_scn();
    is_sync_to_latest = switchover_checkpoint.is_sync_to_latest();
  }
  return ret;
}

int ObTenantRoleTransitionService::get_sys_ls_sync_scn_(
  obrpc::ObCheckpoint &checkpoint,
  SCN &sys_ls_sync_scn,
  bool &is_sync_to_latest)
{
  int ret = OB_SUCCESS;
  sys_ls_sync_scn.reset();
  is_sync_to_latest = false;
  sys_ls_sync_scn = checkpoint.get_cur_sync_scn();
  is_sync_to_latest = checkpoint.is_sync_to_latest();
  return ret;
}

}
}
