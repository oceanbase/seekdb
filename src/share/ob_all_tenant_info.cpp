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

#define USING_LOG_PREFIX SHARE

#include "share/ob_all_tenant_info.h"
#include "share/ob_server_struct.h"  // GCTX
#include "share/ob_cluster_role.h"    // ObClusterRole
#include "lib/oblog/ob_log_module.h"
#include "share/config/ob_server_config.h"  // GCONF
#include "share/config/ob_config_manager.h"  // GCTX.config_mgr_
#include "lib/allocator/ob_mod_define.h"  // ObModIds
#include "lib/string/ob_string.h"  // ObString
#include <string.h>  // strlen, MEMCPY

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObAllTenantInfo, tenant_role_, switchover_status_);

// Helper function to serialize tenant_info to string format: "tenant_role:switchover_status"
static int serialize_tenant_info_to_string(const ObAllTenantInfo &tenant_info, common::ObString &str, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  str.reset();

  const char *role_str = tenant_info.get_tenant_role().to_str();
  const char *status_str = tenant_info.get_switchover_status().to_str();

  if (OB_ISNULL(role_str) || OB_ISNULL(status_str)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_info", KR(ret), K(tenant_info));
  } else {
    int64_t role_len = strlen(role_str);
    int64_t status_len = strlen(status_str);
    int64_t total_len = role_len + 1 + status_len;  // role + ':' + status

    char *buf = static_cast<char *>(allocator.alloc(total_len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", KR(ret), K(total_len));
    } else {
      MEMCPY(buf, role_str, role_len);
      buf[role_len] = ':';
      MEMCPY(buf + role_len + 1, status_str, status_len);
      str.assign_ptr(buf, static_cast<int32_t>(total_len));
    }
  }
  return ret;
}

// Helper function to deserialize tenant_info from string format: "tenant_role:switchover_status"
static int deserialize_tenant_info_from_string(const common::ObString &str, ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  tenant_info.reset();

  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty tenant_info string", KR(ret));
  } else {
    // Find the colon separator
    const char *colon_pos = nullptr;
    for (int32_t i = 0; i < str.length(); ++i) {
      if (str.ptr()[i] == ':') {
        colon_pos = str.ptr() + i;
        break;
      }
    }

    if (OB_ISNULL(colon_pos)) {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid tenant_info format, missing colon separator", KR(ret), K(str));
    } else {
      int32_t role_len = static_cast<int32_t>(colon_pos - str.ptr());
      int32_t status_len = str.length() - role_len - 1;

      if (role_len <= 0 || status_len <= 0) {
        ret = OB_INVALID_DATA;
        LOG_WARN("invalid tenant_info format, empty role or status", KR(ret), K(str), K(role_len), K(status_len));
      } else {
        common::ObString role_str(role_len, str.ptr());
        common::ObString status_str(status_len, colon_pos + 1);

        tenant_info.tenant_role_ = ObTenantRole(role_str);
        tenant_info.switchover_status_ = ObTenantSwitchoverStatus(status_str);

        if (!tenant_info.is_valid()) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid tenant_info after deserialization", KR(ret), K(str), K(tenant_info));
        }
      }
    }
  }
  return ret;
}

// Helper function to load tenant_info from config parameter using config manager interface
// Query config table via config storage interface using load_all_configs, not from memory
// Format: "tenant_role:switchover_status"
static int load_tenant_info_from_config(ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  tenant_info.reset();

  if (OB_ISNULL(GCTX.config_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("config_mgr_ is not initialized", KR(ret));
  } else {
    // Query config table via config storage interface using load_all_configs
    common::ObString config_value;
    common::ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
    if (OB_FAIL(GCTX.config_mgr_->get_storage().get_config_value("ha_info", config_value, allocator))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        LOG_WARN("ha_info config not found in table", KR(ret));
      } else {
        LOG_WARN("failed to query ha_info config from table", KR(ret));
      }
    } else if (config_value.empty()) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("ha_info config value is empty", KR(ret));
    } else if (OB_FAIL(deserialize_tenant_info_from_string(config_value, tenant_info))) {
      LOG_WARN("failed to deserialize tenant_info from config", KR(ret), K(config_value));
    }
  }
  return ret;
}

// Helper function to update tenant_info config parameter via internal table
// Only persists to table, reload is handled by caller
// Format: "tenant_role:switchover_status"
static int update_tenant_info_config(const ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;

  if (!tenant_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_info", KR(ret), K(tenant_info));
  } else if (OB_ISNULL(GCTX.config_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("config_mgr_ is not initialized", KR(ret));
  } else {
    // Serialize tenant_info to string
    common::ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
    common::ObString config_value;
    if (OB_FAIL(serialize_tenant_info_to_string(tenant_info, config_value, allocator))) {
      LOG_WARN("failed to serialize tenant_info to string", KR(ret), K(tenant_info));
    } else {
      // Save config to internal table only (no reload)
      // config_value is allocated from allocator, need to ensure null-terminated for save_config
      char *buf = static_cast<char *>(allocator.alloc(config_value.length() + 1));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory for persistent value", KR(ret), K(config_value.length()));
      } else {
        MEMCPY(buf, config_value.ptr(), config_value.length());
        buf[config_value.length()] = '\0';

        if (OB_FAIL(GCTX.config_mgr_->save_config("ha_info", buf))) {
          LOG_WARN("failed to save config ha_info", KR(ret), K(config_value));
        } else {
          LOG_INFO("persisted ha_info config to internal table", K(config_value), K(tenant_info));
        }
      }
    }
  }
  return ret;
}


int ObAllTenantInfoProxy::load_tenant_info(
    const bool for_update,
    ObAllTenantInfo &tenant_info)
{
  int ret = OB_SUCCESS;
  UNUSED(for_update);

  tenant_info.reset();

  // Load tenant_info from config parameter (using config manager interface)
  // Format: "tenant_role:switchover_status"
  if (OB_FAIL(load_tenant_info_from_config(tenant_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      // If config is empty, try to infer from GCTX.server_role_
      if (common::PRIMARY_CLUSTER == GCTX.server_role_) {
        tenant_info.tenant_role_ = PRIMARY_TENANT_ROLE;
        tenant_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        ret = OB_SUCCESS;
      } else if (common::STANDBY_CLUSTER == GCTX.server_role_) {
        tenant_info.tenant_role_ = STANDBY_TENANT_ROLE;
        tenant_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("cannot infer tenant_info from server_role", KR(ret), K(GCTX.server_role_));
      }
    } else {
      LOG_WARN("failed to load tenant_info from config", KR(ret));
    }
  }
  return ret;
}

int ObAllTenantInfoProxy::update_tenant_role(
    const ObTenantRole &new_role,
    const ObTenantSwitchoverStatus &old_status,
    const ObTenantSwitchoverStatus &new_status)
{
  int ret = OB_SUCCESS;
  UNUSED(old_status);

  // Update tenant_info via config parameter (atomic update of both role and status)
  ObAllTenantInfo tenant_info;
  tenant_info.tenant_role_ = new_role;
  tenant_info.switchover_status_ = new_status;

  if (OB_FAIL(update_tenant_info_config(tenant_info))) {
    LOG_WARN("failed to update tenant_info config", KR(ret), K(tenant_info));
  } else {
    // Update GCTX.server_role_ immediately after config update
    // This ensures memory state is consistent with persisted state
    if (new_role.is_primary()) {
      GCTX.server_role_ = common::PRIMARY_CLUSTER;
    } else if (new_role.is_standby()) {
      GCTX.server_role_ = common::STANDBY_CLUSTER;
    }
    LOG_INFO("updated tenant role and switchover_status via config", K(new_role), K(new_status), K(GCTX.server_role_));
  }
  return ret;
}

int ObAllTenantInfoProxy::update_tenant_switchover_status(
    const ObTenantSwitchoverStatus &old_status,
    const ObTenantSwitchoverStatus &new_status)
{
  int ret = OB_SUCCESS;
  UNUSED(old_status);

  // Load current tenant_info first, then update switchover_status
  ObAllTenantInfo tenant_info;
  if (OB_FAIL(load_tenant_info(false, tenant_info))) {
    LOG_WARN("failed to load tenant_info", KR(ret));
  } else {
    tenant_info.switchover_status_ = new_status;
    if (OB_FAIL(update_tenant_info_config(tenant_info))) {
      LOG_WARN("failed to update tenant_info config", KR(ret), K(tenant_info));
    } else {
      LOG_INFO("updated tenant switchover_status via config", K(new_status));
    }
  }
  return ret;
}

int ObAllTenantInfoProxy::update_tenant_status(
    const ObTenantRole &new_role,
    const ObTenantSwitchoverStatus &old_status,
    const ObTenantSwitchoverStatus &new_status)
{
  int ret = OB_SUCCESS;
  UNUSED(old_status);

  // Update tenant_info via config parameter (atomic update of both role and status)
  ObAllTenantInfo tenant_info;
  tenant_info.tenant_role_ = new_role;
  tenant_info.switchover_status_ = new_status;

  if (OB_FAIL(update_tenant_info_config(tenant_info))) {
    LOG_WARN("failed to update tenant_info config", KR(ret), K(tenant_info));
  } else {
    // Update GCTX.server_role_ immediately after config update
    // This ensures memory state is consistent with persisted state
    if (new_role.is_primary()) {
      GCTX.server_role_ = common::PRIMARY_CLUSTER;
    } else if (new_role.is_standby()) {
      GCTX.server_role_ = common::STANDBY_CLUSTER;
    }
    LOG_INFO("updated tenant status via config (role and switchover_status)", K(new_role), K(new_status), K(GCTX.server_role_));
  }
  return ret;
}

int ObAllTenantInfoProxy::init_tenant_info_from_server_role(
    const common::ObClusterRole server_role)
{
  int ret = OB_SUCCESS;

  ObAllTenantInfo tenant_info;
  // Set tenant role and status based on server role
  if (common::PRIMARY_CLUSTER == server_role) {
    tenant_info.tenant_role_ = PRIMARY_TENANT_ROLE;
    tenant_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
  } else if (common::STANDBY_CLUSTER == server_role) {
    tenant_info.tenant_role_ = STANDBY_TENANT_ROLE;
    tenant_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server role", KR(ret), K(server_role));
  }

  if (OB_SUCC(ret)) {
    // Update tenant_info via config parameter
    if (OB_FAIL(update_tenant_info_config(tenant_info))) {
      LOG_WARN("failed to update tenant_info config", KR(ret), K(tenant_info));
    } else {
      LOG_INFO("initialized tenant_info from server role", K(server_role), K(tenant_info));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
