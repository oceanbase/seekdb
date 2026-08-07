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

#include "share/ob_server_info.h"
#include "lib/oblog/ob_log_module.h"
#include "share/config/ob_config_manager.h"
#include "lib/utility/ob_mod_define.h"  // ObModIds
#include "lib/string/ob_string.h"  // ObString
#include <string.h>  // strlen, MEMCPY

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObServerInfo, server_role_, switchover_status_);

// Helper function to serialize server_info to string format: "server_role:switchover_status"
static int serialize_server_info_to_string(const ObServerInfo &server_info, common::ObString &str, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  str.reset();

  const char *role_str = server_info.get_server_role().to_str();
  const char *status_str = server_info.get_switchover_status().to_str();

  if (OB_ISNULL(role_str) || OB_ISNULL(status_str)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server_info", KR(ret), K(server_info));
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

// Helper function to deserialize server_info from string format: "server_role:switchover_status"
static int deserialize_server_info_from_string(const common::ObString &str, ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.reset();

  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty server_info string", KR(ret));
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
      LOG_WARN("invalid server_info format, missing colon separator", KR(ret), K(str));
    } else {
      int32_t role_len = static_cast<int32_t>(colon_pos - str.ptr());
      int32_t status_len = str.length() - role_len - 1;

      if (role_len <= 0 || status_len <= 0) {
        ret = OB_INVALID_DATA;
        LOG_WARN("invalid server_info format, empty role or status", KR(ret), K(str), K(role_len), K(status_len));
      } else {
        common::ObString role_str(role_len, str.ptr());
        common::ObString status_str(status_len, colon_pos + 1);

        server_info.server_role_ = ObServerRole(role_str);
        server_info.switchover_status_ = ObServerSwitchoverStatus(status_str);

        if (!server_info.is_valid()) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid server_info after deserialization", KR(ret), K(str), K(server_info));
        }
      }
    }
  }
  return ret;
}

// Helper function to load server_info from config parameter using config manager interface
// Query config table via config storage interface using load_all_configs, not from memory
// Format: "server_role:switchover_status"
static int load_server_info_from_config(
    common::ObConfigManager *config_mgr,
    ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.reset();

  if (OB_ISNULL(config_mgr)) {
    ret = OB_NOT_INIT;
    LOG_WARN("config manager is not initialized", KR(ret));
  } else {
    // Query config table via config storage interface using load_all_configs
    common::ObString config_value;
    common::ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
    if (OB_FAIL(config_mgr->get_storage().get_config_value(
        "server_role_info", config_value, allocator))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        LOG_WARN("server_role_info config not found in table", KR(ret));
      } else {
        LOG_WARN("failed to query server_role_info config from table", KR(ret));
      }
    } else if (config_value.empty()) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("server_role_info config value is empty", KR(ret));
    } else if (OB_FAIL(deserialize_server_info_from_string(config_value, server_info))) {
    }
  }
  return ret;
}

// Helper function to update server_info config parameter via internal table
// Only persists to table, reload is handled by caller
// Format: "server_role:switchover_status"
static int update_server_info_config(
    common::ObConfigManager *config_mgr,
    const ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(config_mgr)) {
    ret = OB_NOT_INIT;
    LOG_WARN("config manager is not initialized", KR(ret));
  } else if (!server_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server_info", KR(ret), K(server_info));
  } else {
    // Serialize server_info to string
    common::ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
    common::ObString config_value;
    if (OB_FAIL(serialize_server_info_to_string(server_info, config_value, allocator))) {
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

        if (OB_FAIL(config_mgr->save_config("server_role_info", buf))) {
        } else {
          LOG_INFO("persisted server_role_info config to internal table", K(config_value), K(server_info));
        }
      }
    }
  }
  return ret;
}


int ObServerInfoProxy::load_server_info(
    common::ObConfigManager *config_mgr,
    const ObServerRole::Role fallback_role,
    ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.reset();

  // Load server_info from config parameter (using config manager interface)
  // Format: "server_role:switchover_status"
  if (OB_FAIL(load_server_info_from_config(config_mgr, server_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      // A fresh data directory has no persisted role yet.
      if (ObServerRole::PRIMARY_ROLE == fallback_role) {
        server_info.server_role_ = PRIMARY_SERVER_ROLE;
        server_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        ret = OB_SUCCESS;
      } else if (ObServerRole::STANDBY_ROLE == fallback_role) {
        server_info.server_role_ = STANDBY_SERVER_ROLE;
        server_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("cannot infer server_info from server_role",
            KR(ret), K(fallback_role));
      }
    } else {
      LOG_WARN("failed to load server_info from config", KR(ret));
    }
  }
  return ret;
}

int ObServerInfoProxy::init_server_info_from_role(
    common::ObConfigManager *config_mgr,
    const ObServerRole::Role server_role)
{
  int ret = OB_SUCCESS;

  ObServerInfo server_info;
  // Initialize the persisted state from the startup role.
  if (ObServerRole::PRIMARY_ROLE == server_role) {
    server_info.server_role_ = PRIMARY_SERVER_ROLE;
    server_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
  } else if (ObServerRole::STANDBY_ROLE == server_role) {
    server_info.server_role_ = STANDBY_SERVER_ROLE;
    server_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server role", KR(ret), K(server_role));
  }

  if (OB_SUCC(ret)) {
    // Update server_info via config parameter
    if (OB_FAIL(update_server_info_config(config_mgr, server_info))) {
    } else {
      LOG_INFO("initialized server_info from server role", K(server_role), K(server_info));
    }
  }
  return ret;
}

int ObServerInfoProxy::update_server_info(
    common::ObConfigManager *config_mgr,
    const ObServerInfo &server_info)
{
  return update_server_info_config(config_mgr, server_info);
}

} // namespace share
} // namespace oceanbase
