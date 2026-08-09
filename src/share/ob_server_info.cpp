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
#include <errno.h>
#include <stdlib.h>
#include <string.h>  // strlen, MEMCPY

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObServerInfo, server_role_, pending_role_, switchover_status_, cutover_scn_);

// Format: "active_role:pending_role:transition_status:cutover_scn".
static int serialize_server_info_to_string(const ObServerInfo &server_info, common::ObString &str, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  str.reset();

  const char *role_str = server_info.get_server_role().to_str();
  const char *pending_role_str = server_info.get_pending_role().to_str();
  const char *status_str = server_info.get_switchover_status().to_str();

  if (OB_ISNULL(role_str) || OB_ISNULL(pending_role_str) || OB_ISNULL(status_str)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server_info", KR(ret), K(server_info));
  } else {
    const int64_t total_len = strlen(role_str) + strlen(pending_role_str)
        + strlen(status_str) + 3 + 32;

    char *buf = static_cast<char *>(allocator.alloc(total_len + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", KR(ret), K(total_len));
    } else {
      const uint64_t cutover_scn = server_info.get_cutover_scn().is_valid()
          ? server_info.get_cutover_scn().get_val_for_logservice() : 0;
      int64_t pos = 0;
      if (OB_FAIL(databuff_printf(buf, total_len + 1, pos, "%s:%s:%s:%llu",
          role_str, pending_role_str, status_str,
          static_cast<unsigned long long>(cutover_scn)))) {
        LOG_WARN("failed to serialize server_info", KR(ret), K(server_info));
      } else {
        str.assign_ptr(buf, static_cast<int32_t>(pos));
      }
    }
  }
  return ret;
}

static int parse_role(const common::ObString &str, ObServerRole &role)
{
  int ret = OB_SUCCESS;
  if (0 == str.case_compare("INVALID")) {
    role.reset();
  } else {
    role = ObServerRole(str);
    if (!role.is_valid()) {
      ret = OB_INVALID_DATA;
    }
  }
  return ret;
}

static int parse_scn(const common::ObString &str, SCN &scn)
{
  int ret = OB_SUCCESS;
  scn.reset();
  if (str.empty() || (str.length() == 1 && str.ptr()[0] == '0')) {
  } else {
    // ObString is not required to be NUL terminated. Copy the bounded field
    // before using the libc numeric parser so malformed data cannot escape
    // this persisted field.
    char value_buf[32];
    if (str.length() >= static_cast<int32_t>(sizeof(value_buf))) {
      ret = OB_INVALID_DATA;
    } else {
      MEMCPY(value_buf, str.ptr(), str.length());
      value_buf[str.length()] = '\0';
    }
    char *end_ptr = nullptr;
    if (OB_SUCC(ret)) {
      errno = 0;
      const unsigned long long value = strtoull(value_buf, &end_ptr, 10);
      if (OB_UNLIKELY(errno == ERANGE || end_ptr != value_buf + str.length()
          || value > OB_MAX_SCN_TS_NS)) {
        ret = OB_INVALID_DATA;
      } else if (OB_FAIL(scn.convert_for_logservice(static_cast<uint64_t>(value)))) {
        LOG_WARN("failed to convert persisted cutover scn", KR(ret), K(str));
      }
    }
  }
  return ret;
}

// Stable legacy data directories used "active_role:NORMAL". Interrupted
// states from the removed hot-switchover state machine have no trustworthy C
// and are deliberately rejected.
static int deserialize_server_info_from_string(const common::ObString &str, ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.reset();

  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty server_info string", KR(ret));
  } else {
    common::ObString fields[4];
    int64_t field_count = 0;
    int32_t field_start = 0;
    for (int32_t i = 0; i <= str.length(); ++i) {
      if (i == str.length() || str.ptr()[i] == ':') {
        if (field_count >= ARRAYSIZEOF(fields)) {
          ret = OB_INVALID_DATA;
          break;
        }
        fields[field_count++].assign_ptr(str.ptr() + field_start, i - field_start);
        field_start = i + 1;
      }
    }

    if (OB_FAIL(ret)) {
    } else if (field_count != 2 && field_count != 4) {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid server_info field count", KR(ret), K(str), K(field_count));
    } else if (OB_FAIL(parse_role(fields[0], server_info.server_role_))) {
      LOG_WARN("invalid active role in server_info", KR(ret), K(str));
    } else if (field_count == 2) {
      server_info.switchover_status_ = ObServerSwitchoverStatus(fields[1]);
      if (!server_info.switchover_status_.is_normal_status()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("legacy hot-switchover state cannot be recovered safely", KR(ret), K(str));
      }
    } else if (OB_FAIL(parse_role(fields[1], server_info.pending_role_))) {
      LOG_WARN("invalid pending role in server_info", KR(ret), K(str));
    } else {
      server_info.switchover_status_ = ObServerSwitchoverStatus(fields[2]);
      if (!server_info.switchover_status_.is_valid()) {
        ret = OB_INVALID_DATA;
      } else if (OB_FAIL(parse_scn(fields[3], server_info.cutover_scn_))) {
        LOG_WARN("invalid cutover scn in server_info", KR(ret), K(str));
      }
    }
  }
  if (OB_SUCC(ret) && !server_info.is_valid()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("invalid server_info after deserialization", KR(ret), K(str), K(server_info));
  }
  return ret;
}

// Helper function to load server_info from config parameter using config manager interface
// Query config table via config storage interface using load_all_configs, not from memory
// Format: "active_role:pending_role:transition_status:cutover_scn".
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
// Format: "active_role:pending_role:transition_status:cutover_scn".
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

  // Load server_info from config parameter (using config manager interface).
  if (OB_FAIL(load_server_info_from_config(config_mgr, server_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      // A fresh data directory has no persisted role yet.
      if (ObServerRole::PRIMARY_ROLE == fallback_role) {
        server_info.server_role_ = PRIMARY_SERVER_ROLE;
        server_info.pending_role_.reset();
        server_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        server_info.cutover_scn_.reset();
        ret = OB_SUCCESS;
      } else if (ObServerRole::STANDBY_ROLE == fallback_role) {
        server_info.server_role_ = STANDBY_SERVER_ROLE;
        server_info.pending_role_.reset();
        server_info.switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        server_info.cutover_scn_.reset();
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

  server_info.pending_role_.reset();
  server_info.cutover_scn_.reset();

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
