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
#include "share/ob_server_info.h"
#include "standby/ob_standby_log_sync_service.h"
#include "standby/standby_host.h"

namespace oceanbase
{
namespace standby
{
namespace
{

int load_server_info(IStandbyHost &host, share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(host.load_server_info(server_info))) {
    LOG_WARN("failed to load server profile", KR(ret));
  } else if (!server_info.is_valid()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("loaded invalid server profile", KR(ret), K(server_info));
  }
  return ret;
}

int persist_pending_role(
    IStandbyHost &host,
    share::ObServerInfo &server_info,
    const share::ObServerRole::Role pending_role,
    const share::SCN &cutover_scn)
{
  int ret = OB_SUCCESS;
  server_info.pending_role_ = pending_role;
  server_info.switchover_status_ = share::PREPARING_SWITCHOVER_STATUS;
  server_info.cutover_scn_ = cutover_scn;
  if (OB_FAIL(host.update_server_info(server_info))) {
    LOG_WARN("failed to persist pending server role", KR(ret), K(server_info));
  }
  return ret;
}

int prepare_to_standby(
    const bool is_verify,
    ObStandbyLogSyncService &log_sync_service,
    IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (OB_FAIL(load_server_info(host, server_info))) {
  } else if (!server_info.is_primary()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only a primary profile can prepare standby", KR(ret), K(server_info));
  } else if (server_info.has_pending_role()
             && server_info.get_pending_role().value() != share::ObServerRole::STANDBY_ROLE) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("another pending role is already recorded", KR(ret), K(server_info));
  } else if (server_info.has_pending_role()) {
    // Repeating PREPARE is safe after a client timeout. The fence is
    // deliberately one-way until the next process start.
    LOG_INFO("standby preparation is already persisted", K(server_info), K(is_verify));
  } else if (is_verify) {
    // VERIFY is observational. It does not fence writes or alter metadata.
    LOG_INFO("verified preparation for standby", K(server_info));
  } else {
    share::SCN cutover_scn;
    share::SCN replay_scn;
    host.set_write_enabled(false);
    if (OB_FAIL(log_sync_service.pause())) {
      LOG_WARN("failed to pause standby importer while fencing primary", KR(ret));
    } else if (OB_FAIL(ObStandbyLogSyncService::wait_local_append())) {
      LOG_WARN("failed to wait for primary log append quiescence", KR(ret));
    } else if (OB_FAIL(ObStandbyLogSyncService::get_local_progress(cutover_scn, replay_scn))) {
      LOG_WARN("failed to capture primary cutover scn", KR(ret));
    } else if (!cutover_scn.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("primary returned invalid cutover scn", KR(ret), K(cutover_scn));
    } else if (OB_FAIL(persist_pending_role(
        host, server_info, share::ObServerRole::STANDBY_ROLE, cutover_scn))) {
      LOG_WARN("failed to persist prepare-to-standby state", KR(ret), K(server_info));
    } else {
      LOG_INFO("primary is fenced and ready for restart as standby",
          K(server_info), K(cutover_scn));
    }
  }
  return ret;
}

int prepare_to_primary(
    const bool is_verify,
    const bool is_failover,
    ObStandbyLogSyncService &log_sync_service,
    IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (OB_FAIL(load_server_info(host, server_info))) {
  } else if (server_info.is_primary() && !server_info.has_pending_role()) {
    // A repeated command after the restart is an idempotent no-op.
    LOG_INFO("server is already running with primary profile", K(server_info), K(is_verify));
  } else if (!server_info.is_standby()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only a standby profile can prepare primary", KR(ret), K(server_info));
  } else if (server_info.has_pending_role()
             && server_info.get_pending_role().value() != share::ObServerRole::PRIMARY_ROLE) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("another pending role is already recorded", KR(ret), K(server_info));
  } else if (server_info.has_pending_role()) {
    LOG_INFO("primary preparation is already persisted", K(server_info), K(is_verify));
  } else if (is_verify) {
    if (OB_FAIL(log_sync_service.validate_switch_to_primary(is_failover))) {
      LOG_WARN("standby is not ready for primary preparation", KR(ret), K(is_failover));
    } else {
      LOG_INFO("verified preparation for primary", K(is_failover), K(server_info));
    }
  } else {
    share::SCN cutover_scn;
    share::SCN replay_scn;
    if (OB_FAIL(log_sync_service.prepare_switch_to_primary(is_failover))) {
      LOG_WARN("failed to pause standby importer at a safe boundary", KR(ret), K(is_failover));
    } else if (OB_FAIL(ObStandbyLogSyncService::get_local_progress(cutover_scn, replay_scn))) {
      LOG_WARN("failed to capture standby promotion boundary", KR(ret));
    } else if (!cutover_scn.is_valid() || !replay_scn.is_valid()
               || replay_scn < cutover_scn) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("standby replay is not at the promotion boundary", KR(ret),
          K(cutover_scn), K(replay_scn), K(is_failover));
    } else if (OB_FAIL(persist_pending_role(
        host, server_info, share::ObServerRole::PRIMARY_ROLE, cutover_scn))) {
      LOG_WARN("failed to persist prepare-to-primary state", KR(ret), K(server_info));
    } else {
      host.set_write_enabled(false);
      LOG_INFO("standby is ready for restart as primary", K(server_info), K(cutover_scn));
    }
  }
  return ret;
}

} // namespace

int ObStandbyRoleTransitionService::init(
    ObStandbyLogSyncService &log_sync_service,
    IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  if ((OB_NOT_NULL(log_sync_service_) && log_sync_service_ != &log_sync_service)
      || (OB_NOT_NULL(host_) && host_ != &host)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("standby role transition service is already initialized", KR(ret));
  } else {
    log_sync_service_ = &log_sync_service;
    host_ = &host;
  }
  return ret;
}

void ObStandbyRoleTransitionService::destroy()
{
  log_sync_service_ = nullptr;
  host_ = nullptr;
}

int ObStandbyRoleTransitionService::execute(
    const share::ObTenantRoleTransitionOp op,
    const bool is_verify)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock server role preparation", KR(ret), K(op), K(is_verify));
  } else if (OB_ISNULL(log_sync_service_) || OB_ISNULL(host_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby role preparation service is not initialized", KR(ret));
  } else {
    switch (op) {
      case share::ObTenantRoleTransitionOp::SWITCHOVER_TO_STANDBY:
        ret = prepare_to_standby(is_verify, *log_sync_service_, *host_);
        break;
      case share::ObTenantRoleTransitionOp::SWITCHOVER_TO_PRIMARY:
        ret = prepare_to_primary(is_verify, false /*is_failover*/, *log_sync_service_, *host_);
        break;
      case share::ObTenantRoleTransitionOp::FAILOVER_TO_PRIMARY:
        ret = prepare_to_primary(is_verify, true /*is_failover*/, *log_sync_service_, *host_);
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid server role preparation operation", KR(ret), K(op));
        break;
    }
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
