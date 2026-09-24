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

#include "ob_config_storage.h"
#include "auto_config.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

namespace
{
int report_error(const char *operation, const AutoConfigError &error)
{
  int ret = OB_INVALID_CONFIG;
  LOG_ERROR("auto-config operation failed", K(operation), "detail", error.message,
            "line", error.line, "after_replace", error.after_replace);
  if (error.after_replace != 0) {
    LOG_USER_ERROR(OB_INVALID_CONFIG,
                   "auto-config file was replaced, but durability could not be confirmed");
  } else {
    LOG_USER_ERROR(OB_INVALID_CONFIG, error.message);
  }
  return ret;
}
} // namespace

int ObConfigStorage::init()
{
  int ret = OB_SUCCESS;
  AutoConfigError error = {};
  if (0 != auto_config_supported(AUTO_CONFIG_PATH, &error)) {
    ret = report_error("initialize", error);
  } else {
    inited_ = true;
  }
  return ret;
}

int ObConfigStorage::load_active_checked(bool startup,
                                         AutoConfigEntryCallback callback,
                                         void *context)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (nullptr == callback) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    AutoConfigError error = {};
    if (0 != auto_config_load_active_checked(AUTO_CONFIG_PATH, startup ? 1 : 0,
                                             callback, context, &error)) {
      ret = report_error("load active", error);
    }
  }
  return ret;
}

int ObConfigStorage::save_config(const char *name, const char *value, bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr != after_replace) {
    *after_replace = false;
  }
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (nullptr == name || nullptr == value) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    AutoConfigError error = {};
    if (0 != auto_config_update(AUTO_CONFIG_PATH, name, value, 0, &error)) {
      if (nullptr != after_replace) {
        *after_replace = 0 != error.after_replace;
      }
      ret = report_error("save", error);
    }
  }
  return ret;
}

int ObConfigStorage::reset_config(const char *name, bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr != after_replace) {
    *after_replace = false;
  }
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (nullptr == name) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    AutoConfigError error = {};
    if (0 != auto_config_update(AUTO_CONFIG_PATH, name, nullptr, 1, &error)) {
      if (nullptr != after_replace) {
        *after_replace = 0 != error.after_replace;
      }
      ret = report_error("reset", error);
    }
  }
  return ret;
}

int ObConfigStorage::update_checked(const char *name, const char *value, bool reset,
                                    AutoConfigCheckCallback callback, void *context,
                                    bool *after_replace)
{
  int ret = OB_SUCCESS;
  if (nullptr != after_replace) {
    *after_replace = false;
  }
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (nullptr == name || (!reset && nullptr == value) || nullptr == callback) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    AutoConfigError error = {};
    if (0 != auto_config_update_checked(AUTO_CONFIG_PATH, name, value,
                                        reset ? 1 : 0, callback, context, &error)) {
      if (nullptr != after_replace) {
        *after_replace = 0 != error.after_replace;
      }
      ret = report_error(reset ? "checked reset" : "checked save", error);
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
