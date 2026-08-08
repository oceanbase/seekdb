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
#include "standby/ob_standby_service.h"
#include "grpc/ob_grpc_server.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/oblog/ob_log.h"
#include "standby/ob_standby_bootstrap_service.h"
#include "standby/control/ob_standby_timestamp_provider.h"
#include "standby/ob_standby_grpc_service.h"
#include "standby/ob_standby_log_sync_service.h"
#include "share/ob_server_info.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{

using StandbyGrpcServer = obgrpc::ObGrpcServer;

ObStandbyService::ObStandbyService()
  : is_inited_(false),
    rpc_tls_enabled_(false),
    grpc_server_(nullptr),
    schema_refresh_trigger_()
{
}

ObStandbyService::~ObStandbyService()
{
}

int ObStandbyService::init(
    ObStandbySubmitSchemaRefreshTask submit_schema_refresh_task,
    const bool rpc_tls_enabled)
{
  return instance_().init_(submit_schema_refresh_task, rpc_tls_enabled);
}

int ObStandbyService::stop()
{
  return instance_().stop_();
}

int ObStandbyService::wait()
{
  return instance_().wait_();
}

void ObStandbyService::destroy()
{
  instance_().destroy_();
}

int ObStandbyService::start_rpc_service(const int rpc_port)
{
  return instance_().start_rpc_service_(rpc_port);
}

bool ObStandbyService::is_rpc_tls_enabled()
{
  return instance_().rpc_tls_enabled_;
}

ObStandbyService &ObStandbyService::instance_()
{
  static ObStandbyService service;
  return service;
}

int ObStandbyService::init_(
    ObStandbySubmitSchemaRefreshTask submit_schema_refresh_task,
    const bool rpc_tls_enabled)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(grpc_server_ = OB_NEW(StandbyGrpcServer, ObModIds::OB_COMMON_NETWORK))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate standby gRPC server", KR(ret));
  } else if (OB_FAIL(register_standby_grpc_service(*grpc_server_))) {
    LOG_WARN("failed to register standby gRPC service", KR(ret));
  } else if (OB_FAIL(schema_refresh_trigger_.init(submit_schema_refresh_task))) {
    LOG_WARN("failed to init standby schema refresh trigger", KR(ret));
  } else if (OB_FAIL(ObStandbyLogSyncService::init())) {
    LOG_WARN("failed to init standby log sync service", KR(ret));
    schema_refresh_trigger_.destroy();
  } else {
    share::bind_server_service<share::ObITenantRoleTransitionService>(&role_transition_service_);
    rpc_tls_enabled_ = rpc_tls_enabled;
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && nullptr != grpc_server_) {
    OB_DELETE(StandbyGrpcServer, ObModIds::OB_COMMON_NETWORK, grpc_server_);
    grpc_server_ = nullptr;
  }
  return ret;
}

int ObStandbyService::stop_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (nullptr != grpc_server_) {
    grpc_server_->stop();
  }
  if (OB_FAIL(ObStandbyLogSyncService::stop())) {
    LOG_WARN("failed to stop standby log sync service", KR(ret));
  }
  if (OB_SUCCESS != (tmp_ret = schema_refresh_trigger_.stop())) {
    LOG_WARN("failed to stop standby schema refresh trigger", KR(tmp_ret));
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
  }
  return ret;
}

int ObStandbyService::wait_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_FAIL(ObStandbyLogSyncService::wait())) {
    LOG_WARN("failed to wait standby log sync service", KR(ret));
  }
  if (OB_SUCCESS != (tmp_ret = schema_refresh_trigger_.wait())) {
    LOG_WARN("failed to wait standby schema refresh trigger", KR(tmp_ret));
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
  }
  return ret;
}

void ObStandbyService::destroy_()
{
  if (!is_inited_) {
    return;
  }
  (void)stop_();
  (void)wait_();
  ObStandbyLogSyncService::destroy();
  schema_refresh_trigger_.destroy();
  if (share::server_service<share::ObITenantRoleTransitionService>() == &role_transition_service_) {
    share::unbind_server_service<share::ObITenantRoleTransitionService>();
  }
  if (nullptr != grpc_server_) {
    OB_DELETE(StandbyGrpcServer, ObModIds::OB_COMMON_NETWORK, grpc_server_);
    grpc_server_ = nullptr;
  }
  rpc_tls_enabled_ = false;
  is_inited_ = false;
}

ObStandbyStartupProfile ObStandbyService::startup_profile(const bool embed_mode)
{
  ObStandbyStartupProfile profile;
  profile.enable_log_sync_ = !embed_mode;
  if (GCTX.is_standby_server()) {
    profile.bootstrap_from_source_ = true;
    profile.wait_schema_ready_ = false;
    profile.wait_timezone_usable_ = false;
  }
  return profile;
}

int ObStandbyService::restore_persisted_role()
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (OB_FAIL(share::ObServerInfoProxy::load_server_info(
      GCTX.config_mgr_, GCTX.server_role_, server_info))) {
    LOG_WARN("failed to restore persisted server role", KR(ret));
  } else if (!server_info.is_valid()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("persisted server role is invalid", KR(ret), K(server_info));
  } else {
    GCTX.server_role_ = server_info.server_role_.value();
    share::set_server_role(GCTX.server_role_);
    const bool promotion_in_progress = server_info.is_prepare_switching_to_primary_status()
        || server_info.is_prepare_flashback_for_failover_to_primary_status()
        || server_info.is_flashback_status()
        || server_info.is_switching_to_primary_status();
    if (promotion_in_progress && OB_FAIL(ObStandbyLogSyncService::pause())) {
      LOG_WARN("failed to keep log import paused during promotion recovery",
          KR(ret), K(server_info));
    } else {
      LOG_INFO("restored persisted server role before storage replay", K(server_info));
    }
  }
  return ret;
}

int ObStandbyService::bootstrap()
{
  int ret = OB_SUCCESS;
  share::SCN source_end_scn;
  FLOG_INFO("[OBSERVICE_NOTICE] bootstrap standby begin");

  // The restored LS must never allocate a local timestamp before source-log
  // replay starts, otherwise its local end SCN can skip source history.
  if (OB_FAIL(ObStandbyTimestampProvider::enable())) {
    LOG_WARN("failed to prepare standby timestamp provider for bootstrap", KR(ret));
  } else if (OB_FAIL(ObStandbyBootstrapService::bootstrap(source_end_scn))) {
    LOG_WARN("failed to bootstrap standby", KR(ret));
  } else if (OB_FAIL(ObStandbyLogSyncService::set_startup_target_scn(source_end_scn))) {
    LOG_WARN("failed to record standby startup replay target", KR(ret), K(source_end_scn));
  }

  FLOG_INFO("[OBSERVICE_NOTICE] bootstrap standby end", KR(ret));
  return ret;
}

int ObStandbyService::activate_current_role()
{
  int ret = OB_SUCCESS;
  if (GCTX.is_standby_server()
      && OB_FAIL(ObStandbyTimestampProvider::enable())) {
    LOG_WARN("failed to activate standby timestamp provider", KR(ret));
  } else if (GCTX.is_standby_server()
             && OB_FAIL(ObStandbyTimestampProvider::prepare_for_startup())) {
    LOG_WARN("failed to prepare standby timestamp for startup", KR(ret), K(GCTX.server_role_));
  } else if (!GCTX.is_standby_server()
             && OB_FAIL(ObStandbyTimestampProvider::disable())) {
    LOG_WARN("failed to activate primary timestamp provider", KR(ret));
  }
  return ret;
}

int ObStandbyService::start_role_services(const bool embed_mode)
{
  int ret = OB_SUCCESS;
  const ObStandbyStartupProfile profile = startup_profile(embed_mode);
  if (OB_FAIL(instance_().schema_refresh_trigger_.start())) {
    LOG_WARN("failed to start standby schema refresh trigger", KR(ret));
  } else if (!profile.enable_log_sync_) {
  } else if (OB_FAIL(ObStandbyLogSyncService::start())) {
    LOG_WARN("failed to start standby log sync service", KR(ret), K(GCTX.server_role_));
  }
  return ret;
}

int ObStandbyService::wait_startup_ready(
    const bool embed_mode,
    const std::function<bool()> &is_stopping)
{
  int ret = OB_SUCCESS;
  const ObStandbyStartupProfile profile = startup_profile(embed_mode);
  if (profile.enable_log_sync_
      && GCTX.is_standby_server()
      && OB_FAIL(ObStandbyLogSyncService::wait_startup_replay(is_stopping))) {
    LOG_WARN("failed to wait standby startup replay", KR(ret));
  }
  return ret;
}

share::ObITenantRoleTransitionService *ObStandbyService::role_transition_service()
{
  return &instance_().role_transition_service_;
}

int ObStandbyService::start_rpc_service_(const int rpc_port)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (nullptr == grpc_server_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (rpc_port <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(grpc_server_->start(rpc_port, rpc_tls_enabled_))) {
    LOG_WARN("failed to start standby gRPC service", KR(ret), K(rpc_port));
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
