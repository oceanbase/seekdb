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
#include "standby/standby_module.h"
#include "grpc/ob_grpc_context.h"
#include "grpc/ob_grpc_server.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/lock/ob_mutex.h"
#include "lib/oblog/ob_log.h"
#include "standby/ob_standby_bootstrap_service.h"
#include "standby/ob_standby_schema_refresh_trigger.h"
#include "standby/control/ob_standby_timestamp_provider.h"
#include "standby/control/ob_standby_role_transition_service.h"
#include "standby/control/standby_state_store.h"
#include "standby/ob_standby_grpc_service.h"
#include "standby/ob_standby_log_sync_service.h"
#include "standby/standby_host.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace standby
{

using StandbyGrpcServer = obgrpc::ObGrpcServer;

class StandbyModule::Impl final
{
public:
  Impl();
  ~Impl();
  int init(const StandbyConfig &config, IStandbyHost &host);
  int stop();
  int wait();
  void destroy();
  int prepare_storage_replay();
  int prepare_service_start(const bool need_bootstrap);
  int start();
  int wait_replay_ready(const std::function<bool()> &is_stopping);
  int wait_metadata_ready();
  int reload_config(const bool rpc_service_enabled);
  int start_listener();

private:
  int bootstrap_standby_();
  int activate_current_role_();
  int start_listener_if_enabled_();

  bool is_inited_;
  bool standby_profile_;
  bool resume_pending_promotion_;
  bool listener_ready_;
  lib::ObMutex listener_lock_;
  StandbyConfig config_;
  IStandbyHost *host_;
  StandbyStateStore state_store_;
  StandbyGrpcServer *grpc_server_;
  StandbyGrpcService *grpc_service_;
  ObStandbySchemaRefreshTrigger schema_refresh_trigger_;
  ObStandbyLogSyncService log_sync_service_;
  ObStandbyRoleTransitionService role_transition_service_;
};

StandbyModule::Impl::Impl()
  : is_inited_(false),
    standby_profile_(false),
    resume_pending_promotion_(false),
    listener_ready_(false),
    listener_lock_(),
    config_(),
    host_(nullptr),
    state_store_(),
    grpc_server_(nullptr),
    grpc_service_(nullptr),
    schema_refresh_trigger_(),
    log_sync_service_(),
    role_transition_service_()
{
}

StandbyModule::Impl::~Impl()
{
  destroy();
}

StandbyModule::StandbyModule()
  : impl_(nullptr)
{
}

StandbyModule::~StandbyModule()
{
  destroy();
}

int StandbyModule::init(const StandbyConfig &config, IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  if (nullptr != impl_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(impl_ = OB_NEW(Impl, "StandbyModule"))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(impl_->init(config, host))) {
    OB_DELETE(Impl, "StandbyModule", impl_);
    impl_ = nullptr;
  }
  return ret;
}

int StandbyModule::stop()
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->stop();
}

int StandbyModule::wait()
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->wait();
}

void StandbyModule::destroy()
{
  if (nullptr != impl_) {
    impl_->destroy();
    OB_DELETE(Impl, "StandbyModule", impl_);
    impl_ = nullptr;
  }
}

int StandbyModule::prepare_storage_replay()
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->prepare_storage_replay();
}

int StandbyModule::prepare_service_start(const bool need_bootstrap)
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->prepare_service_start(need_bootstrap);
}

int StandbyModule::start()
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->start();
}

int StandbyModule::wait_replay_ready(const std::function<bool()> &is_stopping)
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->wait_replay_ready(is_stopping);
}

int StandbyModule::wait_metadata_ready()
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->wait_metadata_ready();
}

int StandbyModule::reload_config(const bool rpc_service_enabled)
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->reload_config(rpc_service_enabled);
}

int StandbyModule::start_listener()
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->start_listener();
}

int StandbyModule::Impl::init(const StandbyConfig &config, IStandbyHost &host)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (!config.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby module configuration", KR(ret), K(config.self_addr_),
        K(config.rpc_port_), K(config.embedded_mode_), K(config.io_timeout_ms_),
        K(config.operation_timeout_us_), K(config.boot_role_));
  } else if (FALSE_IT(config_ = config)) {
  } else if (FALSE_IT(host_ = &host)) {
  } else if (OB_FAIL(state_store_.init(*config_.config_manager_, config_.boot_role_))) {
    LOG_WARN("failed to initialize standby state store", KR(ret));
  } else if (OB_ISNULL(grpc_server_ = OB_NEW(StandbyGrpcServer, ObModIds::OB_COMMON_NETWORK))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate standby gRPC server", KR(ret));
  } else if (OB_FAIL(create_and_register_standby_grpc_service(
      *grpc_server_, config_, state_store_, *host_, grpc_service_))) {
    LOG_WARN("failed to register standby gRPC service", KR(ret));
  } else if (OB_FAIL(schema_refresh_trigger_.init(config_, *host_))) {
    LOG_WARN("failed to init standby schema refresh trigger", KR(ret));
  } else if (OB_FAIL(log_sync_service_.init(config_, *host_))) {
    LOG_WARN("failed to init standby log sync service", KR(ret));
    schema_refresh_trigger_.destroy();
  } else if (OB_FAIL(role_transition_service_.init(
      log_sync_service_, schema_refresh_trigger_, state_store_, config_, *host_))) {
    LOG_WARN("failed to init standby role transition service", KR(ret));
    log_sync_service_.destroy();
    schema_refresh_trigger_.destroy();
  } else {
    share::bind_server_service<share::IServerRoleStateProvider>(&state_store_);
    share::bind_server_service<share::ObITenantRoleTransitionService>(&role_transition_service_);
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && nullptr != grpc_server_) {
    OB_DELETE(StandbyGrpcServer, ObModIds::OB_COMMON_NETWORK, grpc_server_);
    grpc_server_ = nullptr;
  }
  if (OB_FAIL(ret)) {
    destroy_standby_grpc_service(grpc_service_);
    config_ = StandbyConfig();
    state_store_.reset();
    host_ = nullptr;
  }
  return ret;
}

int StandbyModule::Impl::stop()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  {
    lib::ObMutexGuard guard(listener_lock_);
    listener_ready_ = false;
    if (nullptr != grpc_server_) {
      grpc_server_->stop();
      host_->publish_rpc_cert_expire_time(0);
    }
  }
  if (OB_FAIL(log_sync_service_.stop())) {
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

int StandbyModule::Impl::wait()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_FAIL(log_sync_service_.wait())) {
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

void StandbyModule::Impl::destroy()
{
  if (!is_inited_) {
    return;
  }
  (void)stop();
  (void)wait();
  log_sync_service_.destroy();
  schema_refresh_trigger_.destroy();
  if (share::server_service<share::ObITenantRoleTransitionService>() == &role_transition_service_) {
    share::unbind_server_service<share::ObITenantRoleTransitionService>();
  }
  role_transition_service_.destroy();
  if (share::server_service<share::IServerRoleStateProvider>() == &state_store_) {
    share::unbind_server_service<share::IServerRoleStateProvider>();
  }
  if (nullptr != grpc_server_) {
    OB_DELETE(StandbyGrpcServer, ObModIds::OB_COMMON_NETWORK, grpc_server_);
    grpc_server_ = nullptr;
  }
  destroy_standby_grpc_service(grpc_service_);
  state_store_.reset();
  config_ = StandbyConfig();
  host_ = nullptr;
  is_inited_ = false;
  standby_profile_ = false;
  resume_pending_promotion_ = false;
  listener_ready_ = false;
}

int StandbyModule::Impl::prepare_storage_replay()
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(state_store_.load(server_info))) {
    LOG_WARN("failed to load persisted server profile", KR(ret));
  } else if (!server_info.is_valid()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("persisted server profile is invalid", KR(ret), K(server_info));
  } else {
    resume_pending_promotion_ = false;
    if (server_info.has_pending_role()
        && server_info.get_pending_role().is_standby()) {
      if (OB_FAIL(server_info.activate_pending_role())) {
        LOG_WARN("failed to activate persisted pending role", KR(ret), K(server_info));
      } else if (OB_FAIL(state_store_.update(server_info))) {
        LOG_WARN("failed to commit activated server profile", KR(ret), K(server_info));
      }
    } else if (server_info.has_pending_role()
               && server_info.is_standby()
               && server_info.get_pending_role().is_primary()) {
      // A pending promotion is resumed only after storage replay and runtime
      // startup. Until then this process deliberately keeps the recovery
      // profile and the write gate closed.
      resume_pending_promotion_ = true;
    } else if (server_info.has_pending_role()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("persisted role transition cannot be recovered", KR(ret), K(server_info));
    }
    if (OB_SUCC(ret)) {
      standby_profile_ = server_info.is_standby();
      share::set_server_role(server_info.server_role_.value());
      share::set_server_recovery_mode(standby_profile_);
      // Startup publishes write capability only after the selected profile is
      // fully activated. This also covers a crash after durable role commit.
      share::set_server_write_enabled(false);
      LOG_INFO("selected server boot profile", K(server_info), K_(standby_profile));
    }
  }
  return ret;
}

int StandbyModule::Impl::prepare_service_start(const bool need_bootstrap)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (need_bootstrap && OB_FAIL(state_store_.initialize())) {
    LOG_WARN("failed to initialize server role state before bootstrap", KR(ret),
        K(config_.boot_role_));
  } else if (need_bootstrap
             && !standby_profile_
             && OB_FAIL(activate_current_role_())) {
    LOG_WARN("failed to activate primary capabilities before bootstrap", KR(ret));
  } else if (need_bootstrap
             && standby_profile_
             && OB_FAIL(bootstrap_standby_())) {
    LOG_WARN("failed to bootstrap standby server", KR(ret));
  } else if (need_bootstrap
             && !standby_profile_
             && OB_FAIL(host_->bootstrap_primary())) {
    LOG_WARN("failed to bootstrap primary server", KR(ret));
  } else if (need_bootstrap) {
    const int tmp_ret = host_->report_bootstrap_telemetry();
    if (OB_SUCCESS != tmp_ret) {
      FLOG_WARN("failed to report bootstrap telemetry synchronously", KR(tmp_ret));
    }
  }
  if (OB_SUCC(ret)
      && (!need_bootstrap || standby_profile_)
      && OB_FAIL(activate_current_role_())) {
    LOG_WARN("failed to activate current server role", KR(ret), K(config_.boot_role_));
  }
  return ret;
}

int StandbyModule::Impl::bootstrap_standby_()
{
  int ret = OB_SUCCESS;
  ObStandbyBootstrapParam param;
  share::SCN source_end_scn;
  FLOG_INFO("[OBSERVICE_NOTICE] bootstrap standby begin");

  param.is_standby_cluster_ = true;
  common::ObArenaAllocator source_allocator("StandbySource");
  int64_t source_version = 0;
  if (OB_FAIL(host_->load_log_restore_source(
      source_allocator, param.source_, source_version))) {
    LOG_WARN("failed to load standby bootstrap source", KR(ret));
  }
  param.bandwidth_throttle_ = config_.bandwidth_throttle_;
  param.restore_config_ = &config_;

  // The restored LS must never allocate a local timestamp before source-log
  // replay starts, otherwise its local end SCN can skip source history.
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObStandbyTimestampProvider::enable())) {
    LOG_WARN("failed to prepare standby timestamp provider for bootstrap", KR(ret));
  } else if (OB_FAIL(ObStandbyBootstrapService::bootstrap(param, source_end_scn))) {
    LOG_WARN("failed to bootstrap standby", KR(ret));
  } else if (OB_FAIL(log_sync_service_.set_startup_target_scn(source_end_scn))) {
    LOG_WARN("failed to record standby startup replay target", KR(ret), K(source_end_scn));
  }

  FLOG_INFO("[OBSERVICE_NOTICE] bootstrap standby end", KR(ret));
  return ret;
}

int StandbyModule::Impl::activate_current_role_()
{
  int ret = OB_SUCCESS;
  if (standby_profile_
      && OB_FAIL(ObStandbyTimestampProvider::enable())) {
    LOG_WARN("failed to activate standby timestamp provider", KR(ret));
  } else if (standby_profile_
             && OB_FAIL(ObStandbyTimestampProvider::prepare_for_startup())) {
    LOG_WARN("failed to prepare standby timestamp for startup", KR(ret));
  } else if (!standby_profile_
             && OB_FAIL(ObStandbyTimestampProvider::disable())) {
    LOG_WARN("failed to activate primary timestamp provider", KR(ret));
  } else {
    share::set_server_write_enabled(!standby_profile_);
  }
  return ret;
}

int StandbyModule::Impl::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (standby_profile_ && OB_FAIL(schema_refresh_trigger_.start())) {
    LOG_WARN("failed to start standby schema refresh trigger", KR(ret));
  } else if (standby_profile_ && !resume_pending_promotion_ && !config_.embedded_mode_
             && OB_FAIL(log_sync_service_.start())) {
    LOG_WARN("failed to start standby log sync service", KR(ret), K(config_.boot_role_));
  }
  return ret;
}

int StandbyModule::Impl::wait_replay_ready(const std::function<bool()> &is_stopping)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!config_.embedded_mode_ && standby_profile_) {
    if (!resume_pending_promotion_
        && OB_FAIL(log_sync_service_.wait_startup_replay(is_stopping))) {
      LOG_WARN("failed to wait standby startup replay", KR(ret));
    } else if (resume_pending_promotion_
               && OB_FAIL(role_transition_service_.resume_pending_promotion())) {
      LOG_WARN("failed to resume persisted standby promotion", KR(ret));
    } else if (resume_pending_promotion_) {
      standby_profile_ = false;
      resume_pending_promotion_ = false;
    }
  }
  return ret;
}

int StandbyModule::Impl::wait_metadata_ready()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (standby_profile_) {
    if (OB_FAIL(host_->start_timezone_manager())) {
      LOG_WARN("failed to start standby timezone manager", KR(ret));
    }
  } else if (OB_FAIL(host_->wait_primary_metadata_ready())) {
    LOG_WARN("failed to wait for primary metadata readiness", KR(ret));
  }
  return ret;
}

int StandbyModule::Impl::reload_config(const bool rpc_service_enabled)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(listener_lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!listener_ready_) {
    config_.rpc_service_enabled_ = rpc_service_enabled;
  } else if (rpc_service_enabled) {
    config_.rpc_service_enabled_ = true;
    if (OB_FAIL(start_listener_if_enabled_())) {
      LOG_WARN("failed to enable standby gRPC service", KR(ret));
    }
  }
  return ret;
}

int StandbyModule::Impl::start_listener()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(listener_lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    listener_ready_ = true;
    if (OB_FAIL(start_listener_if_enabled_())) {
      LOG_WARN("failed to start configured standby gRPC service", KR(ret),
          K(config_.rpc_service_enabled_));
    }
  }
  return ret;
}

int StandbyModule::Impl::start_listener_if_enabled_()
{
  int ret = OB_SUCCESS;
  if (config_.embedded_mode_ || !config_.rpc_service_enabled_) {
    host_->publish_rpc_cert_expire_time(0);
  } else if (nullptr == grpc_server_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(grpc_server_->start(config_.rpc_port_, config_.rpc_tls_enabled_))) {
    LOG_WARN("failed to start standby gRPC service", KR(ret), K(config_.rpc_port_));
  } else {
    host_->publish_rpc_cert_expire_time(
        config_.rpc_tls_enabled_ ? obgrpc::get_rpc_cert_expire_time() : 0);
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
