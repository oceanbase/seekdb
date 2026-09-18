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
#include "lib/time/ob_time_utility.h"
#include "lib/worker.h"
#include "share/ob_debug_sync.h"
#include "share/ob_server_info.h"
#include "standby/ob_standby_log_sync_service.h"
#include "standby/ob_standby_schema_refresh_trigger.h"
#include "standby/control/ob_standby_timestamp_provider.h"
#include "standby/control/standby_state_store.h"
#include "standby/standby_host.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx/ob_id_service.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{
namespace
{

int load_server_info(StandbyStateStore &state_store, share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(state_store.load(server_info))) {
    LOG_WARN("failed to load server profile", KR(ret));
  } else if (!server_info.is_valid()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("loaded invalid server profile", KR(ret), K(server_info));
  }
  return ret;
}

int persist_pending_role(
    StandbyStateStore &state_store,
    share::ObServerInfo &server_info,
    const share::ObServerRole::Role pending_role,
    const share::SCN &cutover_scn)
{
  int ret = OB_SUCCESS;
  server_info.pending_role_ = pending_role;
  server_info.switchover_status_ = share::PREPARING_SWITCHOVER_STATUS;
  server_info.cutover_scn_ = cutover_scn;
  if (OB_FAIL(state_store.update(server_info))) {
    LOG_WARN("failed to persist pending server role", KR(ret), K(server_info));
  }
  return ret;
}

int commit_primary_role(StandbyStateStore &state_store, share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.server_role_ = share::ObServerRole::PRIMARY_ROLE;
  server_info.pending_role_.reset();
  server_info.switchover_status_ = share::NORMAL_SWITCHOVER_STATUS;
  server_info.cutover_scn_.reset();
  if (OB_FAIL(state_store.update(server_info))) {
    LOG_WARN("failed to commit durable primary role", KR(ret), K(server_info));
  }
  return ret;
}

int64_t operation_deadline(const int64_t operation_timeout_us)
{
  return THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : common::ObTimeUtility::current_time() + operation_timeout_us;
}

int get_ls_service(storage::ObLSService *&ls_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls_service = share::server_service<storage::ObLSService>())) {
    ret = OB_NOT_INIT;
    LOG_WARN("local log stream service is not initialized", KR(ret));
  }
  return ret;
}

int prepare_primary_id_services(const int64_t deadline_us)
{
  int ret = OB_SUCCESS;
  static const int64_t RETRY_INTERVAL_US = 1000;
  transaction::ObIDService *trans_id_service = nullptr;
  transaction::ObTimestampService *timestamp_service =
      share::server_service<transaction::ObTimestampService>();
  storage::ObLSService *ls_service = nullptr;
  storage::ObLS *ls = nullptr;
  share::SCN durable_scn;
  bool trans_id_ready = false;
  if (OB_FAIL(transaction::ObIDService::get_id_service(
      transaction::ObIDService::TransIDService, trans_id_service))) {
    LOG_WARN("failed to get transaction id service", KR(ret));
  } else if (OB_FAIL(get_ls_service(ls_service))) {
  } else if (OB_ISNULL(trans_id_service) || OB_ISNULL(timestamp_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("primary id service is null", KR(ret), KP(trans_id_service), KP(timestamp_service));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ls->get_end_scn(durable_scn))) {
  } else if (OB_FAIL(timestamp_service->recover(durable_scn))) {
  }

  while (OB_SUCC(ret) && !trans_id_ready) {
    if (!trans_id_ready) {
      const int tmp_ret = trans_id_service->prepare_next_number(0);
      if (OB_SUCCESS == tmp_ret) {
        trans_id_ready = true;
      } else if (OB_EAGAIN != tmp_ret) {
        ret = tmp_ret;
        LOG_WARN("failed to prepare primary transaction id service", KR(ret));
      }
    }
    if (OB_SUCC(ret) && !trans_id_ready) {
      if (common::ObTimeUtility::current_time() >= deadline_us) {
        ret = OB_TIMEOUT;
        LOG_WARN("timed out preparing primary id services", KR(ret), K(trans_id_ready), K(deadline_us));
      } else {
        ob_usleep(RETRY_INTERVAL_US);
      }
    }
  }
  return ret;
}

int stop_recovery_tasks(
    ObStandbyLogSyncService &log_sync_service,
    ObStandbySchemaRefreshTrigger &schema_refresh_trigger)
{
  int ret = OB_SUCCESS;
  int tmp_ret = log_sync_service.stop();
  if (OB_SUCCESS != tmp_ret) {
    ret = tmp_ret;
    LOG_WARN("failed to stop standby log importer", KR(tmp_ret));
  }
  if (OB_SUCCESS != (tmp_ret = schema_refresh_trigger.stop())) {
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
    LOG_WARN("failed to stop standby schema refresh trigger", KR(tmp_ret));
  }
  if (OB_SUCCESS != (tmp_ret = log_sync_service.wait())) {
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
    LOG_WARN("failed to wait standby log importer", KR(tmp_ret));
  }
  if (OB_SUCCESS != (tmp_ret = schema_refresh_trigger.wait())) {
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
    LOG_WARN("failed to wait standby schema refresh trigger", KR(tmp_ret));
  }
  return ret;
}

int finish_committed_promotion()
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service = nullptr;
  if (OB_FAIL(get_ls_service(ls_service))) {
  } else if (OB_FAIL(ls_service->activate_local_append())) {
    LOG_WARN("failed to activate primary local append runtime", KR(ret));
  } else {
    share::set_server_recovery_mode(false);
    share::set_server_switchover_epoch(common::ObTimeUtility::current_time());
    share::set_server_role(share::ObServerRole::PRIMARY_ROLE);
    // This is the only operation that makes newly started transactions
    // writable. Every durable and fallible promotion step precedes it.
    share::set_server_write_enabled(true);
    LOG_INFO("online standby promotion completed");
  }
  return ret;
}

int complete_promotion(
    ObStandbyLogSyncService &log_sync_service,
    ObStandbySchemaRefreshTrigger &schema_refresh_trigger,
    StandbyStateStore &state_store,
    const int64_t operation_timeout_us,
    IStandbyHost &host,
    share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service = nullptr;
  const int64_t deadline_us = operation_deadline(operation_timeout_us);

  if (!server_info.cutover_scn_.is_valid()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("pending promotion has no durable replay boundary", KR(ret), K(server_info));
  } else if (OB_FAIL(log_sync_service.prepare_persisted_promotion(
      server_info.cutover_scn_))) {
    LOG_WARN("failed to validate persisted promotion boundary", KR(ret), K(server_info));
  } else if (OB_FAIL(stop_recovery_tasks(log_sync_service, schema_refresh_trigger))) {
    LOG_WARN("failed to retire recovery-only tasks", KR(ret));
  } else if (OB_FAIL(get_ls_service(ls_service))) {
  } else if (OB_FAIL(ls_service->prepare_local_append(deadline_us))) {
    LOG_WARN("failed to prepare local append infrastructure", KR(ret));
  } else if (OB_FAIL(prepare_primary_id_services(deadline_us))) {
    LOG_WARN("failed to prepare primary id services", KR(ret));
  } else if (OB_FAIL(ObStandbyTimestampProvider::disable())) {
    LOG_WARN("failed to bind primary timestamp provider", KR(ret));
  } else {
    host.reset_max_id_cache();
    if (OB_FAIL(commit_primary_role(state_store, server_info))) {
      LOG_WARN("failed to commit primary role after runtime preparation", KR(ret));
    } else if (OB_FAIL(DEBUG_SYNC(common::AFTER_STANDBY_PRIMARY_ROLE_COMMITTED))) {
      LOG_WARN("debug sync failed after primary role commit", KR(ret));
    } else if (OB_FAIL(finish_committed_promotion())) {
      LOG_WARN("failed to publish committed primary runtime", KR(ret));
    }
  }
  return ret;
}

int prepare_to_standby(
    const bool is_verify,
    StandbyStateStore &state_store,
    const int64_t operation_timeout_us)
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (OB_FAIL(load_server_info(state_store, server_info))) {
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
    storage::ObLSService *ls_service = nullptr;
    const int64_t deadline_us = operation_deadline(operation_timeout_us);
    share::set_server_write_enabled(false);
    if (OB_FAIL(get_ls_service(ls_service))) {
    } else if (OB_FAIL(ls_service->fence_local_transactions(deadline_us))) {
      LOG_WARN("failed to drain primary write transactions", KR(ret));
    } else if (OB_FAIL(ls_service->fence_local_append(deadline_us))) {
      LOG_WARN("failed to fence primary log append", KR(ret));
    } else if (OB_FAIL(ObStandbyLogSyncService::get_local_progress(cutover_scn, replay_scn))) {
      LOG_WARN("failed to capture primary cutover scn", KR(ret));
    } else if (!cutover_scn.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("primary returned invalid cutover scn", KR(ret), K(cutover_scn));
    } else if (OB_FAIL(persist_pending_role(
        state_store, server_info, share::ObServerRole::STANDBY_ROLE, cutover_scn))) {
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
    ObStandbySchemaRefreshTrigger &schema_refresh_trigger,
    StandbyStateStore &state_store,
    const int64_t operation_timeout_us,
    IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (OB_FAIL(load_server_info(state_store, server_info))) {
  } else if (server_info.is_primary() && !server_info.has_pending_role()) {
    if (!is_verify && !share::server_is_write_enabled()
        && OB_FAIL(finish_committed_promotion())) {
      LOG_WARN("failed to resume committed primary publication", KR(ret), K(server_info));
    } else {
      LOG_INFO("server is already running with primary profile", K(server_info), K(is_verify));
    }
  } else if (!server_info.is_standby()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only a standby profile can prepare primary", KR(ret), K(server_info));
  } else if (server_info.has_pending_role()
             && server_info.get_pending_role().value() != share::ObServerRole::PRIMARY_ROLE) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("another pending role is already recorded", KR(ret), K(server_info));
  } else if (is_verify && server_info.has_pending_role()) {
    share::SCN end_scn;
    share::SCN sync_scn;
    if (!server_info.cutover_scn_.is_valid()
        || OB_FAIL(ObStandbyLogSyncService::get_local_progress(end_scn, sync_scn))) {
      LOG_WARN("failed to inspect pending promotion", KR(ret), K(server_info));
    } else if (end_scn < server_info.cutover_scn_
               || sync_scn < server_info.cutover_scn_) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("pending promotion boundary is not locally replayed", KR(ret),
          K(server_info), K(end_scn), K(sync_scn));
    }
  } else if (is_verify) {
    if (OB_FAIL(log_sync_service.validate_switch_to_primary(is_failover))) {
      LOG_WARN("standby is not ready for primary preparation", KR(ret), K(is_failover));
    } else {
      LOG_INFO("verified preparation for primary", K(is_failover), K(server_info));
    }
  } else {
    if (!server_info.has_pending_role()) {
      share::SCN target_scn;
      if (OB_FAIL(log_sync_service.prepare_promotion(is_failover, target_scn))) {
        LOG_WARN("failed to reach a safe promotion boundary", KR(ret), K(is_failover));
      } else if (OB_FAIL(persist_pending_role(
          state_store, server_info, share::ObServerRole::PRIMARY_ROLE, target_scn))) {
        LOG_WARN("failed to persist promotion intent", KR(ret), K(is_failover), K(target_scn));
        log_sync_service.cancel_promotion_preparation();
      } else {
        // Tests use this point to prove restart resumes a durable intent.
        DEBUG_SYNC(common::AFTER_STANDBY_PROMOTION_INTENT_PERSISTED);
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(complete_promotion(
        log_sync_service,
        schema_refresh_trigger,
        state_store,
        operation_timeout_us,
        host,
        server_info))) {
      LOG_WARN("failed to complete online standby promotion", KR(ret), K(server_info));
    }
  }
  return ret;
}

} // namespace

int ObStandbyRoleTransitionService::init(
    ObStandbyLogSyncService &log_sync_service,
    ObStandbySchemaRefreshTrigger &schema_refresh_trigger,
    StandbyStateStore &state_store,
    const StandbyConfig &config,
    IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  if ((OB_NOT_NULL(log_sync_service_) && log_sync_service_ != &log_sync_service)
      || (OB_NOT_NULL(schema_refresh_trigger_)
          && schema_refresh_trigger_ != &schema_refresh_trigger)
      || (OB_NOT_NULL(state_store_) && state_store_ != &state_store)
      || (OB_NOT_NULL(host_) && host_ != &host)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("standby role transition service is already initialized", KR(ret));
  } else {
    log_sync_service_ = &log_sync_service;
    schema_refresh_trigger_ = &schema_refresh_trigger;
    state_store_ = &state_store;
    operation_timeout_us_ = config.operation_timeout_us_;
    host_ = &host;
  }
  return ret;
}

void ObStandbyRoleTransitionService::destroy()
{
  log_sync_service_ = nullptr;
  schema_refresh_trigger_ = nullptr;
  state_store_ = nullptr;
  operation_timeout_us_ = 0;
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
  } else if (OB_ISNULL(log_sync_service_)
             || OB_ISNULL(schema_refresh_trigger_)
             || OB_ISNULL(state_store_)
             || OB_ISNULL(host_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby role preparation service is not initialized", KR(ret));
  } else {
    switch (op) {
      case share::ObTenantRoleTransitionOp::SWITCHOVER_TO_STANDBY:
        ret = prepare_to_standby(is_verify, *state_store_, operation_timeout_us_);
        break;
      case share::ObTenantRoleTransitionOp::SWITCHOVER_TO_PRIMARY:
        ret = prepare_to_primary(
            is_verify,
            false /*is_failover*/,
            *log_sync_service_,
            *schema_refresh_trigger_,
            *state_store_,
            operation_timeout_us_,
            *host_);
        break;
      case share::ObTenantRoleTransitionOp::FAILOVER_TO_PRIMARY:
        ret = prepare_to_primary(
            is_verify,
            true /*is_failover*/,
            *log_sync_service_,
            *schema_refresh_trigger_,
            *state_store_,
            operation_timeout_us_,
            *host_);
        break;
      default:
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid server role preparation operation", KR(ret), K(op));
        break;
    }
  }
  return ret;
}

int ObStandbyRoleTransitionService::resume_pending_promotion()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  share::ObServerInfo server_info;
  if (OB_FAIL(guard.get_ret())) {
    LOG_WARN("failed to lock pending standby promotion", KR(ret));
  } else if (OB_ISNULL(log_sync_service_)
             || OB_ISNULL(schema_refresh_trigger_)
             || OB_ISNULL(state_store_)
             || OB_ISNULL(host_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(load_server_info(*state_store_, server_info))) {
    LOG_WARN("failed to load pending standby promotion", KR(ret));
  } else if (server_info.is_primary() && !server_info.has_pending_role()) {
    if (!share::server_is_write_enabled()
        && OB_FAIL(finish_committed_promotion())) {
      LOG_WARN("failed to resume committed primary publication", KR(ret));
    }
  } else if (!server_info.is_standby()
             || !server_info.has_pending_role()
             || !server_info.get_pending_role().is_primary()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("no resumable standby promotion is persisted", KR(ret), K(server_info));
  } else if (OB_FAIL(complete_promotion(
      *log_sync_service_,
      *schema_refresh_trigger_,
      *state_store_,
      operation_timeout_us_,
      *host_,
      server_info))) {
    LOG_WARN("failed to resume pending standby promotion", KR(ret), K(server_info));
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
