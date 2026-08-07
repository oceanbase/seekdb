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

#include "standby/control/ob_standby_role_transition_service.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_tracepoint.h"
#include "share/ob_server_info.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/ob_standby_log_sync_service.h"
#include "standby/ob_standby_observer_adapter.h"
#include "standby/control/ob_standby_timestamp_provider.h"
#include "storage/tx/ob_id_service.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{
namespace
{

ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_PREP_SW_TO_STANDBY);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_SWITCHING_TO_STANDBY);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_PREPARE_FLASHBACK);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_FLASHBACK);
ERRSIM_POINT_DEF(ERRSIM_AFTER_PERSIST_SWITCHING_TO_PRIMARY);

int load_server_info(share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(share::ObServerInfoProxy::load_server_info(
      GCTX.config_mgr_, GCTX.server_role_, server_info))) {
    LOG_WARN("failed to load server role transition state", KR(ret));
  } else if (!server_info.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("loaded invalid server role transition state", KR(ret), K(server_info));
  }
  return ret;
}

int persist_server_info(
    const share::ObServerRole::Role new_role,
    const share::ObServerSwitchoverStatus &new_status,
    share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.server_role_ = new_role;
  server_info.switchover_status_ = new_status;
  if (OB_FAIL(share::ObServerInfoProxy::update_server_info(GCTX.config_mgr_, server_info))) {
    LOG_WARN("failed to persist server role transition state",
        KR(ret), K(new_role), K(new_status), K(server_info));
  }
  return ret;
}

void publish_server_role(const share::ObServerRole::Role role)
{
  GCTX.server_role_ = role;
  share::set_server_role(role);
}

int switch_local_log_to_replay_mode()
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service = share::server_service<storage::ObLSService>();
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null while switching to standby", KR(ret));
  } else if (OB_FAIL(ls_service->switch_to_local_replay_mode())) {
    LOG_WARN("failed to switch local log to replay mode", KR(ret));
  }
  return ret;
}

int switch_local_log_to_append_mode()
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service = share::server_service<storage::ObLSService>();
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null while switching to primary", KR(ret));
  } else if (OB_FAIL(ls_service->switch_to_local_append_mode())) {
    LOG_WARN("failed to switch local log to append mode", KR(ret));
  }
  return ret;
}

int prepare_primary_write_services()
{
  int ret = OB_SUCCESS;
  static const int64_t READY_TIMEOUT_US = 5 * 1000 * 1000;
  static const int64_t RETRY_INTERVAL_US = 1000;
  transaction::ObIDService *trans_id_service = nullptr;
  transaction::ObIDService *timestamp_service = nullptr;
  bool trans_id_ready = false;
  bool timestamp_ready = false;
  int64_t trans_id_start = 0;
  int64_t trans_id_end = 0;
  int64_t timestamp = 0;
  const int64_t start_us = common::ObTimeUtility::current_time();

  if (OB_FAIL(transaction::ObIDService::get_id_service(
      transaction::ObIDService::TransIDService, trans_id_service))) {
    LOG_WARN("failed to get transaction id service", KR(ret));
  } else if (OB_FAIL(transaction::ObIDService::get_id_service(
      transaction::ObIDService::TimestampService, timestamp_service))) {
    LOG_WARN("failed to get timestamp service", KR(ret));
  } else if (OB_ISNULL(trans_id_service) || OB_ISNULL(timestamp_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("primary write service is null", KR(ret), KP(trans_id_service), KP(timestamp_service));
  }
  while (OB_SUCC(ret) && (!trans_id_ready || !timestamp_ready)) {
    if (!trans_id_ready) {
      const int tmp_ret = trans_id_service->get_number(1, 0, trans_id_start, trans_id_end);
      if (OB_SUCCESS == tmp_ret) {
        trans_id_ready = true;
      } else if (OB_EAGAIN != tmp_ret) {
        ret = tmp_ret;
        LOG_WARN("failed to prepare primary transaction id service", KR(ret));
      }
    }
    if (OB_SUCC(ret) && !timestamp_ready) {
      int64_t timestamp_end = 0;
      const int64_t now_ns = common::ObTimeUtility::current_time_ns();
      const int tmp_ret = timestamp_service->get_number(
          1, now_ns, timestamp, timestamp_end);
      if (OB_SUCCESS == tmp_ret) {
        timestamp_ready = true;
      } else if (OB_EAGAIN != tmp_ret) {
        ret = tmp_ret;
        LOG_WARN("failed to prepare primary timestamp service", KR(ret));
      }
    }
    if (OB_SUCC(ret) && (!trans_id_ready || !timestamp_ready)) {
      if (common::ObTimeUtility::current_time() - start_us >= READY_TIMEOUT_US) {
        ret = OB_TIMEOUT;
        LOG_WARN("timed out preparing primary write services",
            KR(ret), K(trans_id_ready), K(timestamp_ready));
      } else {
        ob_usleep(RETRY_INTERVAL_US);
      }
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("primary write services are ready",
        K(trans_id_start), K(trans_id_end), K(timestamp));
  }
  return ret;
}

int switch_to_standby(const bool is_verify)
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (OB_FAIL(load_server_info(server_info))) {
    LOG_WARN("failed to load state before switching to standby", KR(ret));
  } else if (server_info.is_standby() && server_info.is_normal_status()) {
    LOG_INFO("server is already standby", K(server_info), K(is_verify));
  } else if (is_verify) {
    LOG_INFO("verified switchover to standby", K(server_info));
  } else if (!server_info.is_normal_status()
             && !server_info.is_prepare_switching_to_standby_status()
             && !server_info.is_switching_to_standby_status()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("current transition state cannot switch to standby", KR(ret), K(server_info));
  } else if (server_info.is_normal_status()) {
    if (!server_info.is_primary()) {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("only a primary server can switch to standby", KR(ret), K(server_info));
    } else if (OB_FAIL(persist_server_info(
        share::ObServerRole::PRIMARY_ROLE,
        share::PREP_SWITCHING_TO_STANDBY_SWITCHOVER_STATUS,
        server_info))) {
      LOG_WARN("failed to persist prepare-to-standby state", KR(ret), K(server_info));
    } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_PREP_SW_TO_STANDBY)) {
      LOG_WARN("errsim after persisting prepare-to-standby state", KR(ret), K(server_info));
    }
  }

  if (OB_SUCC(ret) && !is_verify && server_info.is_prepare_switching_to_standby_status()) {
    if (OB_FAIL(ObStandbyLogSyncService::pause())) {
      LOG_WARN("failed to pause standby log import before demotion", KR(ret), K(server_info));
    } else if (OB_FAIL(persist_server_info(
        share::ObServerRole::STANDBY_ROLE,
        share::SWITCHING_TO_STANDBY_SWITCHOVER_STATUS,
        server_info))) {
      LOG_WARN("failed to persist switching-to-standby state", KR(ret), K(server_info));
    } else {
      // Persisted role and the in-memory write gate move together. A restart
      // from this state also restores the standby gate before SQL starts.
      publish_server_role(share::ObServerRole::STANDBY_ROLE);
      if (OB_FAIL(ERRSIM_AFTER_PERSIST_SWITCHING_TO_STANDBY)) {
        LOG_WARN("errsim after persisting switching-to-standby state", KR(ret), K(server_info));
      } else if (OB_FAIL(ObStandbyTimestampProvider::enable())) {
        LOG_WARN("failed to activate standby timestamp provider", KR(ret), K(server_info));
      }
    }
  }

  if (OB_SUCC(ret) && !is_verify && server_info.is_switching_to_standby_status()) {
    publish_server_role(share::ObServerRole::STANDBY_ROLE);
    if (OB_FAIL(ObStandbyTimestampProvider::enable())) {
      LOG_WARN("failed to activate standby timestamp provider", KR(ret), K(server_info));
    } else if (OB_FAIL(switch_local_log_to_replay_mode())) {
      LOG_WARN("failed to activate local replay before entering standby", KR(ret), K(server_info));
    } else if (OB_FAIL(ObStandbyLogSyncService::resume())) {
      LOG_WARN("failed to resume standby log import", KR(ret), K(server_info));
    } else if (OB_FAIL(persist_server_info(
        share::ObServerRole::STANDBY_ROLE,
        share::NORMAL_SWITCHOVER_STATUS,
        server_info))) {
      LOG_WARN("failed to persist normal standby state", KR(ret), K(server_info));
    } else {
      LOG_INFO("switched server to standby", K(server_info));
    }
  }
  return ret;
}

int switch_to_primary(const bool is_verify, const bool is_failover)
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  const char *switch_op = is_failover ? "failover to primary" : "switchover to primary";

  if (OB_FAIL(load_server_info(server_info))) {
    LOG_WARN("failed to load state before switching to primary", KR(ret), K(is_failover));
  } else if (server_info.is_primary() && server_info.is_normal_status()) {
    LOG_INFO("server is already primary", K(server_info), K(is_verify), K(is_failover));
  } else if (is_verify) {
    LOG_INFO("verified switch to primary", K(server_info), K(is_failover));
  } else if (!server_info.is_normal_status()
             && !server_info.is_prepare_switching_to_primary_status()
             && !server_info.is_prepare_flashback_for_failover_to_primary_status()
             && !server_info.is_flashback_status()
             && !server_info.is_switching_to_primary_status()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("current transition state cannot switch to primary",
        KR(ret), K(server_info), K(switch_op));
  } else if (!server_info.is_standby()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only a standby server can switch to primary", KR(ret), K(server_info), K(switch_op));
  } else if (OB_FAIL(ObStandbyLogSyncService::prepare_switch_to_primary(is_failover))) {
    LOG_WARN("failed to stop at a promotable log boundary", KR(ret), K(server_info), K(switch_op));
  } else if (server_info.is_normal_status()) {
    const share::ObServerSwitchoverStatus prepare_status = is_failover
        ? share::PREPARE_FLASHBACK_FOR_FAILOVER_TO_PRIMARY_SWITCHOVER_STATUS
        : share::PREP_SWITCHING_TO_PRIMARY_SWITCHOVER_STATUS;
    if (OB_FAIL(persist_server_info(
        share::ObServerRole::STANDBY_ROLE,
        prepare_status,
        server_info))) {
      LOG_WARN("failed to persist prepare-to-primary state", KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_PREPARE_FLASHBACK)) {
      LOG_WARN("errsim after persisting prepare-to-primary state", KR(ret), K(server_info), K(switch_op));
    }
  }

  if (OB_SUCC(ret)
      && !is_verify
      && (server_info.is_prepare_switching_to_primary_status()
          || server_info.is_prepare_flashback_for_failover_to_primary_status())) {
    const bool prepare_status_matches_op = is_failover
        ? server_info.is_prepare_flashback_for_failover_to_primary_status()
        : server_info.is_prepare_switching_to_primary_status();
    if (!prepare_status_matches_op) {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("prepare-to-primary state does not match operation",
          KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(persist_server_info(
        share::ObServerRole::STANDBY_ROLE,
        share::FLASHBACK_SWITCHOVER_STATUS,
        server_info))) {
      LOG_WARN("failed to persist pre-promotion checkpoint", KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_FLASHBACK)) {
      LOG_WARN("errsim after persisting pre-promotion checkpoint", KR(ret), K(server_info), K(switch_op));
    }
  }

  // Single-LS SeekDB does not run a separate physical flashback operation, but
  // this durable checkpoint keeps promotion restartable between its side effects.
  if (OB_SUCC(ret) && !is_verify && server_info.is_flashback_status()) {
    if (OB_FAIL(persist_server_info(
        share::ObServerRole::STANDBY_ROLE,
        share::SWITCHING_TO_PRIMARY_SWITCHOVER_STATUS,
        server_info))) {
      LOG_WARN("failed to persist switching-to-primary state", KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(ERRSIM_AFTER_PERSIST_SWITCHING_TO_PRIMARY)) {
      LOG_WARN("errsim after persisting switching-to-primary state", KR(ret), K(server_info), K(switch_op));
    }
  }

  if (OB_SUCC(ret) && !is_verify && server_info.is_switching_to_primary_status()) {
    if (OB_FAIL(switch_local_log_to_append_mode())) {
      LOG_WARN("failed to activate local append before entering primary", KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(prepare_primary_write_services())) {
      LOG_WARN("failed to prepare write services before entering primary", KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(ObStandbyTimestampProvider::disable())) {
      LOG_WARN("failed to activate primary timestamp provider", KR(ret), K(server_info), K(switch_op));
    } else if (OB_FAIL(persist_server_info(
        share::ObServerRole::PRIMARY_ROLE,
        share::NORMAL_SWITCHOVER_STATUS,
        server_info))) {
      LOG_WARN("failed to persist normal primary state", KR(ret), K(server_info), K(switch_op));
    } else {
      publish_server_role(share::ObServerRole::PRIMARY_ROLE);
      ObStandbyObserverAdapter::reset_max_id_cache();
      LOG_INFO("switched server to primary", K(server_info), K(switch_op));
    }
  }
  return ret;
}

} // namespace

int ObStandbyRoleTransitionService::execute(
    const share::ObTenantRoleTransitionOp op,
    const bool is_verify)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock server role transition", KR(ret), K(op), K(is_verify));
  } else {
    switch (op) {
      case share::ObTenantRoleTransitionOp::SWITCHOVER_TO_STANDBY:
        ret = switch_to_standby(is_verify);
        break;
      case share::ObTenantRoleTransitionOp::SWITCHOVER_TO_PRIMARY:
        ret = switch_to_primary(is_verify, false /*is_failover*/);
        break;
      case share::ObTenantRoleTransitionOp::FAILOVER_TO_PRIMARY:
        ret = switch_to_primary(is_verify, true /*is_failover*/);
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid server role transition operation", KR(ret), K(op), K(is_verify));
        break;
    }
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
