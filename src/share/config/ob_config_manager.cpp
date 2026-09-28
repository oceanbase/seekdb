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
#include "config_ffi.h"

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
constexpr char CONFIG_PATH[] = "./etc/seekdb.conf";

int report_error(const char *operation, const ConfigError &error)
{
  int ret = OB_INVALID_CONFIG;
  LOG_ERROR("config operation failed", K(operation), "detail", error.message,
            "line", error.line, "after_replace", error.after_replace);
  if (error.after_replace != 0) {
    LOG_USER_ERROR(OB_INVALID_CONFIG,
                   "config file was replaced, but durability could not be confirmed");
  } else {
    LOG_USER_ERROR(OB_INVALID_CONFIG, error.message);
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
  ConfigError error = {};
  if (0 != config_supported(CONFIG_PATH, &error)) {
    ret = report_error("initialize", error);
  } else {
    inited_ = true;
  }
  return ret;
}

int ObConfigManager::reload_config()
{
  return reload_config_func_();
}

int ObConfigManager::update_local()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
  } else {
    ConfigError error = {};
    if (0 != config_load_active(CONFIG_PATH,
                                    enable_static_effect_ ? 0 : 1, &error)) {
      ret = report_error("load active", error);
    } else {
      LOG_INFO("read config success");
    }
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

int ObConfigManager::save_internal_state(const char *name, const char *value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(name) || OB_ISNULL(value)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!inited_) {
    ret = OB_NOT_INIT;
  } else {
    ConfigError error = {};
    if (0 != config_update_internal_state(CONFIG_PATH, name, value, &error)) {
      ret = report_error("save internal state", error);
    }
  }
  return ret;
}

int ObConfigManager::update_checked(const char *name, const char *value, bool reset,
                                    ConfigCheckCallback callback, void *context,
                                    bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr != after_replace) {
    *after_replace = false;
  }
  if (nullptr == name) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!config::parameter_exists(rust::Str(name))) {
    ret = OB_ERR_SYS_CONFIG_UNKNOWN;
  } else if (!inited_) {
    ret = OB_NOT_INIT;
  } else if ((!reset && nullptr == value) || nullptr == callback) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ConfigError error = {};
    if (0 != config_update_checked(CONFIG_PATH, name, value,
                                        reset ? 1 : 0, callback, context, &error)) {
      if (nullptr != after_replace) {
        *after_replace = 0 != error.after_replace;
      }
      ret = report_error(reset ? "checked reset" : "checked save", error);
    }
  }
  return ret;
}

int ObConfigManager::save_configs()
{
  int ret = OB_SUCCESS;
  ConfigError error = {};
  if (0 != config_save_bootstrap(CONFIG_PATH, &error)) {
    ret = OB_INVALID_CONFIG;
    LOG_ERROR("failed to save startup parameters", K(ret), "detail", error.message,
              "after_replace", error.after_replace);
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
