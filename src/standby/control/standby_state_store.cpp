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
#include "standby/control/standby_state_store.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_mod_define.h"
#include "share/config/ob_config_manager.h"
#include "share/rc/ob_server_runtime.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace oceanbase
{
namespace standby
{
namespace
{

const char *SERVER_ROLE_STATE_CONFIG = "server_role_info";

int serialize_server_info(
    const share::ObServerInfo &server_info,
    common::ObString &str,
    common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  str.reset();
  const char *role = server_info.get_server_role().to_str();
  const char *pending_role = server_info.get_pending_role().to_str();
  const char *status = server_info.get_switchover_status().to_str();
  if (OB_ISNULL(role) || OB_ISNULL(pending_role) || OB_ISNULL(status)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t capacity = strlen(role) + strlen(pending_role) + strlen(status) + 3 + 32;
    char *buf = static_cast<char *>(allocator.alloc(capacity + 1));
    const uint64_t cutover_scn = server_info.get_cutover_scn().is_valid()
        ? server_info.get_cutover_scn().get_val_for_logservice()
        : 0;
    int64_t pos = 0;
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(databuff_printf(buf, capacity + 1, pos, "%s:%s:%s:%llu",
        role, pending_role, status,
        static_cast<unsigned long long>(cutover_scn)))) {
      LOG_WARN("failed to serialize server role state", KR(ret), K(server_info));
    } else {
      str.assign_ptr(buf, static_cast<int32_t>(pos));
    }
  }
  return ret;
}

int parse_role(const common::ObString &str, share::ObServerRole &role)
{
  int ret = OB_SUCCESS;
  if (0 == str.case_compare("INVALID")) {
    role.reset();
  } else {
    role = share::ObServerRole(str);
    if (!role.is_valid()) {
      ret = OB_INVALID_DATA;
    }
  }
  return ret;
}

int parse_scn(const common::ObString &str, share::SCN &scn)
{
  int ret = OB_SUCCESS;
  scn.reset();
  if (str.empty() || (1 == str.length() && '0' == str.ptr()[0])) {
  } else {
    char value_buf[32];
    if (str.length() >= static_cast<int32_t>(sizeof(value_buf))) {
      ret = OB_INVALID_DATA;
    } else {
      MEMCPY(value_buf, str.ptr(), str.length());
      value_buf[str.length()] = '\0';
      char *end = nullptr;
      errno = 0;
      const unsigned long long value = strtoull(value_buf, &end, 10);
      if (OB_UNLIKELY(ERANGE == errno || end != value_buf + str.length()
          || value > share::OB_MAX_SCN_TS_NS)) {
        ret = OB_INVALID_DATA;
      } else if (OB_FAIL(scn.convert_for_logservice(static_cast<uint64_t>(value)))) {
        LOG_WARN("failed to parse persisted cutover scn", KR(ret), K(str));
      }
    }
  }
  return ret;
}

int deserialize_server_info(
    const common::ObString &str,
    share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  server_info.reset();
  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    common::ObString fields[4];
    int64_t field_count = 0;
    int32_t field_start = 0;
    for (int32_t i = 0; OB_SUCC(ret) && i <= str.length(); ++i) {
      if (i == str.length() || ':' == str.ptr()[i]) {
        if (field_count >= ARRAYSIZEOF(fields)) {
          ret = OB_INVALID_DATA;
        } else {
          fields[field_count++].assign_ptr(str.ptr() + field_start, i - field_start);
          field_start = i + 1;
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (2 != field_count && 4 != field_count) {
      ret = OB_INVALID_DATA;
    } else if (OB_FAIL(parse_role(fields[0], server_info.server_role_))) {
      LOG_WARN("invalid active role in persisted state", KR(ret), K(str));
    } else if (2 == field_count) {
      // Stable legacy directories used "active_role:NORMAL". Interrupted
      // states from the old hot-switchover machine have no durable boundary.
      server_info.switchover_status_ = share::ObServerSwitchoverStatus(fields[1]);
      if (!server_info.switchover_status_.is_normal_status()) {
        ret = OB_INVALID_DATA;
      }
    } else if (OB_FAIL(parse_role(fields[1], server_info.pending_role_))) {
      LOG_WARN("invalid pending role in persisted state", KR(ret), K(str));
    } else {
      server_info.switchover_status_ = share::ObServerSwitchoverStatus(fields[2]);
      if (!server_info.switchover_status_.is_valid()) {
        ret = OB_INVALID_DATA;
      } else if (OB_FAIL(parse_scn(fields[3], server_info.cutover_scn_))) {
        LOG_WARN("invalid cutover scn in persisted state", KR(ret), K(str));
      }
    }
  }
  if (OB_SUCC(ret) && !server_info.is_valid()) {
    ret = OB_INVALID_DATA;
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("invalid persisted server role state", KR(ret), K(str), K(server_info));
  }
  return ret;
}

int load_from_config(
    common::ObConfigManager &config_manager,
    share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  common::ObString value;
  common::ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
  server_info.reset();
  if (OB_FAIL(config_manager.get_storage().get_config_value(
      SERVER_ROLE_STATE_CONFIG, value, allocator))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to load server role state", KR(ret));
    }
  } else if (value.empty()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_FAIL(deserialize_server_info(value, server_info))) {
    LOG_WARN("failed to deserialize server role state", KR(ret), K(value));
  }
  return ret;
}

int save_to_config(
    common::ObConfigManager &config_manager,
    const share::ObServerInfo &server_info)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator(ObModIds::OB_TEMP_VARIABLES);
  common::ObString value;
  if (!server_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialize_server_info(server_info, value, allocator))) {
    LOG_WARN("failed to serialize server role state", KR(ret), K(server_info));
  } else {
    char *buf = static_cast<char *>(allocator.alloc(value.length() + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buf, value.ptr(), value.length());
      buf[value.length()] = '\0';
      if (OB_FAIL(config_manager.save_config(SERVER_ROLE_STATE_CONFIG, buf))) {
        LOG_WARN("failed to persist server role state", KR(ret), K(value));
      } else {
        LOG_INFO("persisted server role state", K(value), K(server_info));
      }
    }
  }
  return ret;
}

} // namespace

int StandbyStateStore::init(
    common::ObConfigManager &config_manager,
    const share::ObServerRole::Role boot_role)
{
  int ret = OB_SUCCESS;
  if (nullptr != config_manager_) {
    ret = OB_INIT_TWICE;
  } else if (share::ObServerRole::INVALID_ROLE == boot_role) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    config_manager_ = &config_manager;
    boot_role_ = boot_role;
  }
  return ret;
}

void StandbyStateStore::reset()
{
  config_manager_ = nullptr;
  boot_role_ = share::ObServerRole::INVALID_ROLE;
}

int StandbyStateStore::load(share::ObServerInfo &server_info) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config_manager_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(load_from_config(*config_manager_, server_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      const share::ObServerRole::Role active_role = share::server_role();
      const share::ObServerRole::Role fallback_role =
          share::ObServerRole::INVALID_ROLE == active_role ? boot_role_ : active_role;
      if (share::ObServerRole::PRIMARY_ROLE == fallback_role) {
        server_info.server_role_ = share::PRIMARY_SERVER_ROLE;
      } else if (share::ObServerRole::STANDBY_ROLE == fallback_role) {
        server_info.server_role_ = share::STANDBY_SERVER_ROLE;
      } else {
        LOG_WARN("cannot infer missing server role state", KR(ret), K(fallback_role));
      }
      if (server_info.server_role_.is_valid()) {
        server_info.pending_role_.reset();
        server_info.switchover_status_ = share::NORMAL_SWITCHOVER_STATUS;
        server_info.cutover_scn_.reset();
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int StandbyStateStore::initialize() const
{
  int ret = OB_SUCCESS;
  share::ObServerInfo server_info;
  if (share::ObServerRole::PRIMARY_ROLE == boot_role_) {
    server_info.server_role_ = share::PRIMARY_SERVER_ROLE;
  } else if (share::ObServerRole::STANDBY_ROLE == boot_role_) {
    server_info.server_role_ = share::STANDBY_SERVER_ROLE;
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret)) {
    server_info.pending_role_.reset();
    server_info.switchover_status_ = share::NORMAL_SWITCHOVER_STATUS;
    server_info.cutover_scn_.reset();
    ret = update(server_info);
  }
  return ret;
}

int StandbyStateStore::update(const share::ObServerInfo &server_info) const
{
  return nullptr == config_manager_
      ? OB_NOT_INIT
      : save_to_config(*config_manager_, server_info);
}

} // namespace standby
} // namespace oceanbase
