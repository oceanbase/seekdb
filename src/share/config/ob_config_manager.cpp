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


#include "ob_config_manager.h"
#include "share/ob_sql_client_decorator.h"
#include "share/config/ob_system_config.h"
#include "share/config/ob_config_rpc_types.h"

#include <memory>
#include <new>
#include <cstdio>

namespace oceanbase
{
namespace obcall
{

OB_SERIALIZE_MEMBER(ObAdminSetConfigItem, name_, value_, comment_, is_reset_);

} // namespace obcall

namespace common
{
ObConfigManager::~ObConfigManager()
{
}

int ObConfigManager::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(storage_.init())) {
  } else {
    inited_ = true;
  }
  return ret;
}

void ObConfigManager::stop()
{
}

void ObConfigManager::wait()
{
}

void ObConfigManager::destroy()
{
}

int ObConfigManager::reload_config()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(server_config_.check_all())) {
  } else if (OB_FAIL(reload_config_func_())) {
  }
  return ret;
}

int ObConfigManager::check_header_change(const char* path, const char* buf) const
{
  UNUSED(path);
  UNUSED(buf);
  return OB_SUCCESS;
}

int ObConfigManager::dump2file_unsafe(const char* path) const
{
  UNUSED(path);
  return OB_SUCCESS;
}

int ObConfigManager::dump2file(const char* path) const
{
  DRWLock::RDLockGuard guard(server_config_.rwlock_);
  return dump2file_unsafe(path);
}

int ObConfigManager::update_local()
{
  int ret = OB_SUCCESS;
  ObSystemConfig system_config;
  std::vector<ObConfigStorage::Entry> entries;
  struct ConfigSnapshotChecker : public ObServerConfig {};
  std::unique_ptr<ConfigSnapshotChecker> checker(new (std::nothrow) ConfigSnapshotChecker());

  if (!checker) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(system_config.init())) {
  } else if (OB_FAIL(storage_.load_all_configs(entries))) {
  }

  // Build the complete next snapshot before mutating the live configuration.
  if (OB_SUCC(ret)) {
    for (ObConfigContainer::const_iterator it = checker->get_container().begin();
         OB_SUCC(ret) && it != checker->get_container().end(); ++it) {
      if (OB_ISNULL(it->second)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObSystemConfigKey key;
        ObSystemConfigValue value;
        key.set_name(it->first.str());
        value.set_value(it->second->default_str());
        if (OB_FAIL(system_config.update_value(key, value))) {
        }
      }
    }
  }
  for (const ObConfigStorage::Entry &entry : entries) {
    if (OB_FAIL(ret)) {
      break;
    }
    ObConfigItem *const *item = checker->get_container().get(
        ObConfigStringKey(entry.name.c_str()));
    if (OB_ISNULL(item) || OB_ISNULL(*item)) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
    } else if (!(*item)->check_unit(entry.value.c_str()) ||
               !(*item)->set_value_for_validation(entry.value.c_str()) ||
               !(*item)->check()) {
      ret = OB_INVALID_CONFIG;
    } else {
      ObSystemConfigKey key;
      ObSystemConfigValue value;
      key.set_name(entry.name.c_str());
      value.set_value(entry.value.c_str());
      if (OB_FAIL(system_config.update_value(key, value))) {
      }
    }
    if (OB_FAIL(ret)) {
      LOG_ERROR("invalid auto-config entry", K(ret), "name", entry.name.c_str(),
                "line", entry.line);
      std::fprintf(stderr, "seekdb auto-config line %u, parameter %s: %s (%d)\n",
                   entry.line, entry.name.c_str(),
                   ret == OB_ERR_SYS_CONFIG_UNKNOWN ? "unknown parameter" : "invalid value",
                   ret);
      std::fflush(stderr);
    }
  }

  if (OB_SUCC(ret)) {
    DRWLock::WRLockGuard guard(server_config_.rwlock_);
    if (OB_FAIL(server_config_.read_config(system_config, enable_static_effect_))) {
    } else {
      LOG_INFO("read config success");
    }
  }

  if (OB_SUCC(ret)) {
    server_config_.print();
  } else {
  }
  return ret;
}

int ObConfigManager::got_version()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(update_local())) {
    } else {
      LOG_INFO("loaded new config synchronously");
    }
  }
  return ret;
}

int ObConfigManager::save_config(
    const char *config_name,
    const char *value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config_name) || OB_ISNULL(value)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // Get config item from server_config_ container
    ObConfigItem *const *ci_ptr = server_config_.get_container().get(
                                     ObConfigStringKey(config_name));
    if (OB_ISNULL(ci_ptr)) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
    } else if (OB_ISNULL(*ci_ptr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(storage_.save_config(config_name, value))) {
    }
  }
  return ret;
}

int ObConfigManager::reset_config(const char *config_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config_name)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(server_config_.get_container().get(
                 ObConfigStringKey(config_name)))) {
    ret = OB_ERR_SYS_CONFIG_UNKNOWN;
  } else if (OB_FAIL(storage_.reset_config(config_name))) {
  }
  return ret;
}

int ObConfigManager::get_config_value(
    const char *name, ObString &value, ObIAllocator &allocator)
{
  return storage_.get_config_value(name, value, allocator);
}

int ObConfigManager::save_configs(int64_t base_version)
{
  int ret = OB_SUCCESS;
  ObConfigContainer::const_iterator it = server_config_.get_container().begin();
  for (; OB_SUCC(ret) && it != server_config_.get_container().end(); ++it) {
    if (OB_ISNULL(it->second)) {
      // ignore ret
      LOG_WARN("config item is null", "name", it->first.str());
      continue;
    }
      if (it->second->version() > base_version) {
      if (OB_FAIL(save_config(it->first.str(), it->second->str()))) {
      }
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
