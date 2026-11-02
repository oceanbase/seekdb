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

#ifndef OCEANBASE_SHARE_OB_ALL_TENANT_INFO_H_
#define OCEANBASE_SHARE_OB_ALL_TENANT_INFO_H_

#include "share/ob_tenant_role.h"              // ObTenantRole
#include "share/ob_tenant_switchover_status.h"  // ObTenantSwitchoverStatus
#include "share/scn.h"                         // SCN
#include "share/ob_cluster_role.h"            // ObClusterRole
#include "lib/utility/ob_print_utils.h"        // TO_STRING_KV

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObISQLClient;
}
namespace share
{

// Simplified ObAllTenantInfo for single tenant/single LS scenario
// tenant_role is persisted via config parameter (tenant_role), not in KV storage
struct ObAllTenantInfo
{
  ObAllTenantInfo()
    : tenant_role_(ObTenantRole::INVALID_TENANT),
      switchover_status_(ObTenantSwitchoverStatus::INVALID_STATUS) {}
  ~ObAllTenantInfo() {}

  bool is_valid() const {
    return tenant_role_.is_valid()
           && switchover_status_.is_valid();
  }

  void reset() {
    tenant_role_.reset();
    switchover_status_.reset();
  }

  int assign(const ObAllTenantInfo &other) {
    tenant_role_ = other.tenant_role_;
    switchover_status_ = other.switchover_status_;
    return OB_SUCCESS;
  }

  // Getters
  const ObTenantRole &get_tenant_role() const { return tenant_role_; }
  const ObTenantSwitchoverStatus &get_switchover_status() const { return switchover_status_; }

  // Convenience methods
  bool is_primary() const { return tenant_role_.is_primary(); }
  bool is_standby() const { return tenant_role_.is_standby(); }
  bool is_normal_status() const { return switchover_status_.is_normal_status(); }
  bool is_switching_to_primary_status() const { return switchover_status_.is_switching_to_primary_status(); }
  bool is_switching_to_standby_status() const { return switchover_status_.is_switching_to_standby_status(); }
  bool is_prepare_switching_to_standby_status() const { return switchover_status_.is_prepare_switching_to_standby_status(); }
  bool is_prepare_flashback_for_failover_to_primary_status() const { return switchover_status_.is_prepare_flashback_for_failover_to_primary_status(); }
  bool is_prepare_flashback_for_switch_to_primary_status() const { return switchover_status_.is_prepare_flashback_for_switch_to_primary_status(); }
  bool is_flashback_status() const { return switchover_status_.is_flashback_status(); }

  TO_STRING_KV(K_(tenant_role), K_(switchover_status));

  ObTenantRole tenant_role_;  // Read from config parameter tenant_role
  ObTenantSwitchoverStatus switchover_status_;

  OB_UNIS_VERSION(1);
};

// Simplified ObAllTenantInfoProxy for single tenant/single LS scenario
// Uses KV storage instead of database table
class ObAllTenantInfoProxy
{
public:
  // Load tenant info from KV storage (always uses sys tenant)
  static int load_tenant_info(
      const bool for_update,
      ObAllTenantInfo &tenant_info);

  // Update tenant role via config parameter (always uses sys tenant)
  static int update_tenant_role(
      const ObTenantRole &new_role,
      const ObTenantSwitchoverStatus &old_status,
      const ObTenantSwitchoverStatus &new_status);

  // Update tenant switchover status (always uses sys tenant)
  static int update_tenant_switchover_status(
      const ObTenantSwitchoverStatus &old_status,
      const ObTenantSwitchoverStatus &new_status);

  // Update tenant status (role via config, status in KV) (always uses sys tenant)
  static int update_tenant_status(
      const ObTenantRole &new_role,
      const ObTenantSwitchoverStatus &old_status,
      const ObTenantSwitchoverStatus &new_status);

  // Initialize tenant info from server role (startup parameter) (always uses sys tenant)
  // This should be called during system startup to set initial tenant role
  static int init_tenant_info_from_server_role(
      const ObClusterRole server_role);

};

} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_ALL_TENANT_INFO_H_ */
