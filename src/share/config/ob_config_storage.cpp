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

#include <cstring>
#include <cstdio>

namespace oceanbase
{
namespace common
{

namespace
{
constexpr const char *AUTO_CONFIG_PATH = "./etc/seekdb.auto.conf";

struct LoadContext
{
  std::vector<ObConfigStorage::Entry> &entries;
  int ret;
};

int append_entry(void *context, const char *name, const char *value, uint32_t line)
{
  LoadContext &load = *static_cast<LoadContext *>(context);
  if (nullptr == name || nullptr == value) {
    load.ret = OB_INVALID_ARGUMENT;
  } else if (std::strlen(name) >= OB_MAX_CONFIG_NAME_LEN ||
             std::strlen(value) >= OB_MAX_CONFIG_VALUE_LEN) {
    load.ret = OB_INVALID_CONFIG;
    int ret = load.ret;
    LOG_ERROR("auto-config entry exceeds parameter limits", K(ret), K(line), K(name));
  } else {
    load.entries.push_back({name, value, line});
  }
  return load.ret;
}

int report_error(const char *operation, const AutoConfigError &error)
{
  int ret = OB_INVALID_CONFIG;
  LOG_ERROR("auto-config operation failed", K(operation), "detail", error.message,
            "line", error.line, "after_replace", error.after_replace);
  std::fprintf(stderr, "seekdb auto-config %s failed: %s\n", operation, error.message);
  std::fflush(stderr);
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

int ObConfigStorage::load_all_configs(std::vector<Entry> &entries)
{
  int ret = OB_SUCCESS;
  entries.clear();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    AutoConfigError error = {};
    LoadContext context{entries, OB_SUCCESS};
    if (0 != auto_config_load(AUTO_CONFIG_PATH, append_entry, &context, &error)) {
      ret = OB_SUCCESS != context.ret ? context.ret : report_error("load", error);
    }
  }
  return ret;
}

int ObConfigStorage::get_config_value(
    const char *name, ObString &value, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  value.reset();
  std::vector<Entry> entries;
  if (nullptr == name) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(load_all_configs(entries))) {
  } else {
    ret = OB_ENTRY_NOT_EXIST;
    for (const Entry &entry : entries) {
      if (entry.name == name && !entry.value.empty()) {
        char *buffer = static_cast<char *>(allocator.alloc(entry.value.size()));
        if (nullptr == buffer) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(buffer, entry.value.data(), entry.value.size());
          value.assign_ptr(buffer, static_cast<int32_t>(entry.value.size()));
          ret = OB_SUCCESS;
        }
        break;
      }
    }
  }
  return ret;
}

int ObConfigStorage::save_config(const char *name, const char *value)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (nullptr == name || nullptr == value) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    AutoConfigError error = {};
    if (0 != auto_config_update(AUTO_CONFIG_PATH, name, value, 0, &error)) {
      ret = report_error("save", error);
    }
  }
  return ret;
}

int ObConfigStorage::reset_config(const char *name)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (nullptr == name) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    AutoConfigError error = {};
    if (0 != auto_config_update(AUTO_CONFIG_PATH, name, nullptr, 1, &error)) {
      ret = report_error("reset", error);
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
