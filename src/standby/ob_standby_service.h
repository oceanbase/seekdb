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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_SERVICE_H_
#define OCEANBASE_STANDBY_OB_STANDBY_SERVICE_H_

#include <functional>
#include "standby/ob_standby_schema_refresh_trigger.h"
#include "standby/control/ob_standby_role_transition_service.h"

namespace oceanbase
{
namespace obgrpc
{
class ObGrpcServer;
}
namespace standby
{

struct ObStandbyStartupProfile
{
  ObStandbyStartupProfile()
    : bootstrap_from_source_(false),
      enable_log_sync_(false),
      wait_schema_ready_(true),
      wait_timezone_usable_(true)
  {}

  bool bootstrap_from_source_;
  bool enable_log_sync_;
  bool wait_schema_ready_;
  bool wait_timezone_usable_;
};

class ObStandbyService final
{
public:
  static int init(
      ObStandbySubmitSchemaRefreshTask submit_schema_refresh_task,
      const bool rpc_tls_enabled);
  static int stop();
  static int wait();
  static void destroy();
  static int start_rpc_service(int rpc_port);
  static bool is_rpc_tls_enabled();
  static ObStandbyStartupProfile startup_profile(const bool embed_mode);
  static int restore_persisted_role();
  static int bootstrap();
  static int activate_current_role();
  static int start_role_services(const bool embed_mode);
  static int wait_startup_ready(
      const bool embed_mode,
      const std::function<bool()> &is_stopping);
  static share::ObITenantRoleTransitionService *role_transition_service();

private:
  ObStandbyService();
  ~ObStandbyService();
  static ObStandbyService &instance_();
  int init_(
      ObStandbySubmitSchemaRefreshTask submit_schema_refresh_task,
      const bool rpc_tls_enabled);
  int stop_();
  int wait_();
  void destroy_();
  int start_rpc_service_(int rpc_port);

private:
  bool is_inited_;
  bool rpc_tls_enabled_;
  obgrpc::ObGrpcServer *grpc_server_;
  ObStandbySchemaRefreshTrigger schema_refresh_trigger_;
  ObStandbyRoleTransitionService role_transition_service_;
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_SERVICE_H_ */
