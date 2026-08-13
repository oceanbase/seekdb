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

#ifndef OCEANBASE_STANDBY_CONTROL_OB_STANDBY_ROLE_TRANSITION_SERVICE_H_
#define OCEANBASE_STANDBY_CONTROL_OB_STANDBY_ROLE_TRANSITION_SERVICE_H_

#include "lib/lock/ob_mutex.h"
#include "share/ob_tenant_role_transition_service.h"

namespace oceanbase
{
namespace standby
{
class ObStandbyLogSyncService;
class ObStandbySchemaRefreshTrigger;
class IStandbyHost;
class StandbyStateStore;
struct StandbyConfig;

class ObStandbyRoleTransitionService final : public share::ObITenantRoleTransitionService
{
public:
  ObStandbyRoleTransitionService()
      : lock_(),
        log_sync_service_(nullptr),
        schema_refresh_trigger_(nullptr),
        state_store_(nullptr),
        operation_timeout_us_(0),
        host_(nullptr)
  {}
  int init(ObStandbyLogSyncService &log_sync_service,
           ObStandbySchemaRefreshTrigger &schema_refresh_trigger,
           StandbyStateStore &state_store,
           const StandbyConfig &config,
           IStandbyHost &host);
  void destroy();
  int execute(const share::ObTenantRoleTransitionOp op, const bool is_verify) override;
  int resume_pending_promotion();

private:
  lib::ObMutex lock_;
  ObStandbyLogSyncService *log_sync_service_;
  ObStandbySchemaRefreshTrigger *schema_refresh_trigger_;
  StandbyStateStore *state_store_;
  int64_t operation_timeout_us_;
  IStandbyHost *host_;
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_CONTROL_OB_STANDBY_ROLE_TRANSITION_SERVICE_H_
