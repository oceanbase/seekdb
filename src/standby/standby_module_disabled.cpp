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
#include "standby/control/standby_state_store.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/oblog/ob_log.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace standby
{

class StandbyModule::Impl final
{
public:
  Impl()
    : is_inited_(false),
      boot_role_(share::ObServerRole::INVALID_ROLE),
      state_store_(),
      host_(nullptr)
  {}

  int init(const StandbyConfig &config, IStandbyHost &host)
  {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      ret = OB_INIT_TWICE;
    } else if (!config.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(state_store_.init(*config.config_manager_, config.boot_role_))) {
      LOG_WARN("failed to initialize server role state store", KR(ret));
    } else {
      boot_role_ = config.boot_role_;
      host_ = &host;
      share::bind_server_service<share::IServerRoleStateProvider>(&state_store_);
      is_inited_ = true;
    }
    return ret;
  }

  int prepare_storage_replay()
  {
    int ret = OB_SUCCESS;
    share::ObServerInfo server_info;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(state_store_.load(server_info))) {
      LOG_WARN("failed to load server role without standby support", KR(ret));
    } else if (!server_info.is_primary() || !server_info.is_normal_status()) {
      ret = OB_NOT_SUPPORTED;
      LOG_ERROR("binary without standby support cannot restore this server role",
          KR(ret), K(server_info));
    } else {
      share::set_server_role(share::ObServerRole::PRIMARY_ROLE);
      share::set_server_recovery_mode(false);
    }
    return ret;
  }

  int prepare_service_start(const bool need_bootstrap)
  {
    int ret = OB_SUCCESS;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (share::ObServerRole::PRIMARY_ROLE != boot_role_) {
      ret = OB_NOT_SUPPORTED;
      LOG_ERROR("standby role requires a standby-enabled binary", KR(ret), K_(boot_role));
    } else if (need_bootstrap && OB_FAIL(state_store_.initialize())) {
      LOG_WARN("failed to initialize primary server role", KR(ret));
    } else if (need_bootstrap && OB_FAIL(host_->bootstrap_primary())) {
      LOG_WARN("failed to bootstrap primary server", KR(ret));
    } else if (need_bootstrap) {
      const int tmp_ret = host_->report_bootstrap_telemetry();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("failed to report bootstrap telemetry synchronously", KR(tmp_ret));
      }
    }
    return ret;
  }

  int wait_metadata_ready()
  {
    int ret = OB_SUCCESS;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(host_->wait_primary_metadata_ready())) {
      LOG_WARN("failed to wait for primary metadata readiness", KR(ret));
    }
    return ret;
  }

  int start_listener()
  {
    int ret = OB_SUCCESS;
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else {
      host_->publish_rpc_cert_expire_time(0);
    }
    return ret;
  }

  int reload_config(const bool)
  {
    return is_inited_ ? OB_SUCCESS : OB_NOT_INIT;
  }

  void destroy()
  {
    if (share::server_service<share::IServerRoleStateProvider>() == &state_store_) {
      share::unbind_server_service<share::IServerRoleStateProvider>();
    }
    state_store_.reset();
    boot_role_ = share::ObServerRole::INVALID_ROLE;
    host_ = nullptr;
    is_inited_ = false;
  }

  bool is_inited_;
  share::ObServerRole::Role boot_role_;
  StandbyStateStore state_store_;
  IStandbyHost *host_;
};

StandbyModule::StandbyModule() : impl_(nullptr) {}
StandbyModule::~StandbyModule() { destroy(); }

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

int StandbyModule::stop() { return nullptr == impl_ ? OB_NOT_INIT : OB_SUCCESS; }
int StandbyModule::wait() { return nullptr == impl_ ? OB_NOT_INIT : OB_SUCCESS; }

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

int StandbyModule::start() { return nullptr == impl_ ? OB_NOT_INIT : OB_SUCCESS; }

int StandbyModule::wait_replay_ready(const std::function<bool()> &)
{
  return nullptr == impl_ ? OB_NOT_INIT : OB_SUCCESS;
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

} // namespace standby
} // namespace oceanbase
