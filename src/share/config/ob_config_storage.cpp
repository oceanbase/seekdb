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
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/time/ob_time_utility.h"
#include "share/config/ob_system_config.h"
#include "share/config/ob_config.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "share/storage/ob_sqlite_table_schema.h"
#include "lib/string/ob_string.h"  // ObString
#include "lib/allocator/ob_allocator.h"  // ObIAllocator
#include <string.h>

namespace oceanbase
{
namespace common
{

ObConfigStorage::ObConfigStorage()
  : pool_(nullptr)
{
}

ObConfigStorage::~ObConfigStorage()
{
}

int ObConfigStorage::init(share::ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_ = pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else if (OB_FAIL(create_table_if_not_exists())) {
    LOG_WARN("failed to create table", K(ret));
  }
  if (OB_FAIL(ret)) {
    pool_ = NULL;
  }
  return ret;
}

int ObConfigStorage::create_table_if_not_exists()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("pool not set", K(ret));
  } else {
    share::ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_SYS_PARAMETER, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObConfigStorage::load_all_configs(ObSystemConfig &system_config)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT name, data_type, value, info, section, scope, source, edit_level "
      "FROM __all_sys_parameter;";

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      ObSystemConfigKey key;
      ObSystemConfigValue value;

      const char *name_str = reader.get_text();
      if (nullptr != name_str) {
        ObString name_val((int32_t)strlen(name_str), name_str);
        key.set_name(name_val);
      }

      const char *data_type_str = reader.get_text();
      const char *value_str = reader.get_text();
      if (nullptr != value_str) {
        ObString value_val((int32_t)strlen(value_str), value_str);
        value.set_value(value_val);
      }

      const char *info_str = reader.get_text();
      if (nullptr != info_str) {
        ObString info_val((int32_t)strlen(info_str), info_str);
        value.set_info(info_val);
      }

      const char *section_str = reader.get_text();
      if (nullptr != section_str) {
        ObString section_val((int32_t)strlen(section_str), section_str);
        value.set_section(section_val);
      }

      const char *scope_str = reader.get_text();
      if (nullptr != scope_str) {
        ObString scope_val((int32_t)strlen(scope_str), scope_str);
        value.set_scope(scope_val);
      }

      const char *source_str = reader.get_text();
      if (nullptr != source_str) {
        ObString source_val((int32_t)strlen(source_str), source_str);
        value.set_source(source_val);
      }

      const char *edit_level_str = reader.get_text();
      if (nullptr != edit_level_str) {
        ObString edit_level_val((int32_t)strlen(edit_level_str), edit_level_str);
        value.set_edit_level(edit_level_val);
      }

      if (OB_FAIL(system_config.update_value(key, value))) {
        LOG_WARN("failed to update system config", K(ret));
      }
      return ret;
    };

    share::ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, nullptr, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query configs", K(ret));
      } else {
        ret = OB_SUCCESS; // No rows is acceptable
        LOG_INFO("load all configs from sqlite success (empty)");
      }
    } else {
      LOG_INFO("load all configs from sqlite success");
    }
  }
  return ret;
}

int ObConfigStorage::get_config_value(const char *name, ObString &value, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  value.reset();

  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("name is null", K(ret));
  } else {
    // Use load_all_configs interface to load configs from table
    ObSystemConfig system_config;
    if (OB_FAIL(system_config.init())) {
      LOG_WARN("failed to init system_config", K(ret));
    } else if (OB_FAIL(load_all_configs(system_config))) {
      LOG_WARN("failed to load all configs", K(ret));
    } else {
      // Find config value from system_config
      ObSystemConfigKey key;
      key.set_name(ObString::make_string(name));
      const ObSystemConfigValue *pvalue = nullptr;

      if (OB_FAIL(system_config.find(key, pvalue))) {
        if (OB_SEARCH_NOT_FOUND == ret) {
          ret = OB_ENTRY_NOT_EXIST;
          LOG_WARN("config not found", K(ret), K(name));
        } else {
          LOG_WARN("failed to find config", K(ret), K(name));
        }
      } else if (OB_ISNULL(pvalue)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pvalue is null", K(ret));
      } else {
        const char *config_value_str = pvalue->value();
        if (OB_ISNULL(config_value_str) || 0 == strlen(config_value_str)) {
          ret = OB_ENTRY_NOT_EXIST;
          LOG_WARN("config value is empty", K(ret), K(name));
        } else {
          // Allocate memory from allocator and copy the value to ensure lifetime
          int64_t value_len = strlen(config_value_str);
          char *buf = static_cast<char *>(allocator.alloc(value_len));
          if (OB_ISNULL(buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate memory", K(ret), K(value_len));
          } else {
            MEMCPY(buf, config_value_str, value_len);
            value.assign_ptr(buf, static_cast<int32_t>(value_len));
          }
        }
      }
    }
  }
  return ret;
}

int ObConfigStorage::upsert_config(
    const char *name,
    const char *data_type,
    const char *value,
    const char *info,
    const char *section,
    const char *scope,
    const char *source,
    const char *edit_level)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *upsert_sql =
      "INSERT OR REPLACE INTO __all_sys_parameter "
      "(name, data_type, value, info, "
      " section, scope, source, edit_level, config_version, gmt_modified) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, ?);";

    int64_t current_time = ObTimeUtility::current_time();

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_text(name);
      b.bind_text(data_type);
      b.bind_text(value);
      b.bind_text(info);
      b.bind_text(section);
      b.bind_text(scope);
      b.bind_text(source);
      b.bind_text(edit_level);
      b.bind_int64(current_time);
      return OB_SUCCESS;
    };

    share::ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(upsert_sql, binder))) {
      LOG_WARN("failed to execute upsert", K(ret));
    } else {
      LOG_INFO("upsert config to sqlite success", K(name));
    }
  }
  return ret;
}


} // namespace common
} // namespace oceanbase
