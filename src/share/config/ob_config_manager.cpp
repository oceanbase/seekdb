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
#include "share/config/ob_config_rpc_types.h"
#include "config_bridge.h"
#include "config_checkers.h"
#include "auto_config.h"

#include <cstring>

namespace oceanbase
{
namespace obcall
{

OB_SERIALIZE_MEMBER(ObAdminSetConfigItem, name_, value_, comment_, is_reset_);

} // namespace obcall

namespace common
{
namespace
{
int check_load_entry(void *, const char *name, const char *value, uint32_t line)
{
  int ret = OB_SUCCESS;
  if (nullptr == name || nullptr == value) {
    ret = OB_INVALID_ARGUMENT;
  } else if (config::parameter_exists(rust::Str(name)) &&
             !config::check_parameter(name, value)) {
    ret = OB_INVALID_CONFIG;
    LOG_ERROR("invalid auto-config entry", K(ret), K(line), K(name));
  }
  return ret;
}
}

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
  return reload_config_func_();
}

int ObConfigManager::update_local()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(storage_.load_active_checked(!enable_static_effect_, check_load_entry,
                                           nullptr))) {
  } else {
    LOG_INFO("read config success");
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
    const char *value,
    bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr != after_replace) {
    *after_replace = false;
  }
  if (OB_ISNULL(config_name) || OB_ISNULL(value)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (!config::parameter_exists(rust::Str(config_name))) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
    } else if (OB_FAIL(storage_.save_config(config_name, value, after_replace))) {
    }
  }
  return ret;
}

int ObConfigManager::reset_config(const char *config_name, bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr != after_replace) {
    *after_replace = false;
  }
  if (OB_ISNULL(config_name)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!config::parameter_exists(rust::Str(config_name))) {
    ret = OB_ERR_SYS_CONFIG_UNKNOWN;
  } else if (OB_FAIL(storage_.reset_config(config_name, after_replace))) {
  }
  return ret;
}

int ObConfigManager::save_internal_state(const char *name, const char *value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(name) || OB_ISNULL(value)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 != std::strcmp(name, "server_create_time") &&
             0 != std::strcmp(name, "server_role_info")) {
    ret = OB_ERR_SYS_CONFIG_UNKNOWN;
  } else if (OB_FAIL(storage_.save_config(name, value))) {
  }
  return ret;
}

int ObConfigManager::update_checked(const char *name, const char *value, bool reset,
                                    AutoConfigCheckCallback callback, void *context,
                                    bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr == name) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!config::parameter_exists(rust::Str(name))) {
    ret = OB_ERR_SYS_CONFIG_UNKNOWN;
  } else if (OB_FAIL(storage_.update_checked(name, value, reset,
                                             callback, context, after_replace))) {
  }
  return ret;
}

int ObConfigManager::save_configs()
{
  int ret = OB_SUCCESS;
  AutoConfigError error = {};
  if (0 != auto_config_save_bootstrap(AUTO_CONFIG_PATH, &error)) {
    ret = OB_INVALID_CONFIG;
    LOG_ERROR("failed to save startup parameters", K(ret), "detail", error.message,
              "after_replace", error.after_replace);
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
