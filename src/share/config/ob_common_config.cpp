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

#include "ob_common_config.h"
#include "lib/container/ob_array_iterator.h"
#include "lib/utility/ob_sort.h"
#include "observer/omt/ob_tenant_config_mgr.h"

namespace oceanbase
{
namespace common
{

ObConfigContainer *&ObInitConfigContainer::local_container()
{
  RLOCAL(ObConfigContainer*, l_container);
  return l_container;
}

const ObConfigContainer &ObInitConfigContainer::get_container()
{
  return container_;
}

int ObBaseConfig::init()
{
  int ret = OB_SUCCESS;
  const int64_t buf_len= OB_MAX_CONFIG_LENGTH;
  if (OB_UNLIKELY(inited_)) {
    LOG_ERROR("init twice", K(inited_));
    ret = OB_INIT_TWICE;
  } else {
    inited_ = true;
  }
  return ret;
}

void ObBaseConfig::destroy()
{
  inited_ = false;
}

int ObBaseConfig::check_all()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(! inited_)) {
    LOG_ERROR("Config has not been initialized");
    ret = OB_NOT_INIT;
  } else {
    for (auto it = container_.begin(); OB_SUCC(ret) && it != container_.end(); it++) {
      if (OB_ISNULL(it->second)) {
        LOG_ERROR("config item const_iterator second element is NULL",
            "first_item", it->first.str());
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(it->second->str())) {
        LOG_ERROR("config item string value is NULL",
            "first_item", it->first.str());
        ret = OB_ERR_UNEXPECTED;
      } else if (! it->second->check()) {
        _LOG_ERROR("invalid config, name: [%s], value: [%s]", it->first.str(), it->second->str());
        ret = OB_INVALID_CONFIG;
      } else if (0 == strlen(it->second->str())) {
        // All configuration items are not allowed to be empty
        _LOG_ERROR("invalid empty config, name: [%s], value: [%s]",
            it->first.str(), it->second->str());
        ret = OB_INVALID_CONFIG;
      } else {
        // normal
      }
    }
  }

  return ret;
}
void ObBaseConfig::get_sorted_config_items(ConfigItemArray &configs) const
{
  // Transfer the configuration items to an array and sort the output
  for (auto it = container_.begin(); it != container_.end(); ++it) {
    ConfigItem item(it->first.str(), NULL == it->second ? "" : it->second->str());
    (void)configs.push_back(item);
  }
  lib::ob_sort(configs.begin(), configs.end());
}

int ObBaseConfig::load_from_buffer(const char *config_str, const int64_t config_str_len,
                              const int64_t version, const bool check_name)
{
  return OB_SUCCESS;
}

int ObBaseConfig::load_from_file(const char *config_file,
    const int64_t version /* = 0 */,
    const bool check_name /* = false */)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObBaseConfig::dump2file(const char *file) const
{
  int ret = OB_SUCCESS;
  return ret;
}


ObInitConfigContainer::ObInitConfigContainer()
{
  local_container() = &container_;
}

ObCommonConfig::ObCommonConfig()
{
}

ObCommonConfig::~ObCommonConfig()
{
}

int ObCommonConfig::add_extra_config_unsafe(const char *config_str,
                                            int64_t version,
                                            bool check_config)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_OPTS_LENGTH = sysconf(_SC_ARG_MAX);
  int64_t config_str_length = 0;
  char *buf = NULL;
  char *saveptr = NULL;
  char *token = NULL;
  const char *delimiters[] = {"\n", "|\n", ",\n"};
  const char *delimiter = "\n";

  if (OB_ISNULL(config_str)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("config str is null", K(ret));
  } else if ((config_str_length = static_cast<int64_t>(STRLEN(config_str))) >= MAX_OPTS_LENGTH) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("Extra config is too long", K(ret));
  } else if (OB_ISNULL(buf = new (std::nothrow) char[config_str_length + 1])) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("ob tc malloc memory for buf fail", K(ret));
  } else {
    MEMCPY(buf, config_str, config_str_length);
    buf[config_str_length] = '\0';
    for (int i = 0; i < sizeof(delimiters)/sizeof(delimiters[0]); i++) {
      token = STRTOK_R(buf, delimiters[i], &saveptr);
      if (OB_NOT_NULL(saveptr) && 0 != STRLEN(saveptr)) {
        delimiter = delimiters[i];
        break;
      }
    }
    auto func = [&]() {
      char *saveptr_one = NULL;
      const char *name = NULL;
      const char *value = NULL;
      ObConfigItem *const *pp_item = NULL;
      if (OB_ISNULL(name = STRTOK_R(token, "=", &saveptr_one))) {
        ret = OB_INVALID_CONFIG;
        LOG_ERROR("Invalid config string", K(token), K(ret));
      } else if (OB_ISNULL(saveptr_one) || OB_UNLIKELY('\0' == *(value = saveptr_one))) {
        LOG_INFO("Empty config string", K(token), K(name));
        // ret = OB_INVALID_CONFIG;
        name = "";
      }
      if (OB_SUCC(ret)) {
        const int value_len = static_cast<int>(strlen(value));
        // hex2cstring -> value_len / 2 + 1
        // '\0' -> 1
        const int external_info_val_len = value_len / 2 + 1 + 1;
        char *external_info_val = (char*)ob_malloc(external_info_val_len, "temp");
        DEFER(if (external_info_val != nullptr) ob_free(external_info_val););
        if (OB_ISNULL(external_info_val)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("failed to alloc", K(ret));
        } else if (FALSE_IT(external_info_val[0] = '\0')) {
        } else if (OB_ISNULL(pp_item = container_.get(ObConfigStringKey(name)))) {
          ret = OB_SUCCESS;
          LOG_WARN("Invalid config string, no such config item", K(name), K(value), K(ret));
        }
        if (OB_FAIL(ret) || OB_ISNULL(pp_item)) {
        } else if (!(*pp_item)->set_value_unsafe(value)) {
          ret = OB_INVALID_CONFIG;
          LOG_ERROR("Invalid config value", K(name), K(value), K(ret));
        } else if (check_config && (!(*pp_item)->check_unit(value) || !(*pp_item)->check())) {
          ret = OB_INVALID_CONFIG;
          const char* range = (*pp_item)->range();
          if (OB_ISNULL(range) || strlen(range) == 0) {
            LOG_ERROR("Invalid config, value out of range", K(name), K(value), K(ret));
          } else {
            _LOG_ERROR("Invalid config, value out of %s (for reference only). name=%s, value=%s, ret=%d", range, name, value, ret);
          }
        } else {
          LOG_INFO("Load config succ", K(name), K(value));
        }
      }
    };
    // reset
    MEMCPY(buf, config_str, config_str_length);
    buf[config_str_length] = '\0';
    saveptr = nullptr;
    delimiter = "\n";
    for (int i = 0; i < sizeof(delimiters)/sizeof(delimiters[0]); i++) {
      token = STRTOK_R(buf, delimiters[i], &saveptr);
      if (OB_NOT_NULL(saveptr) && 0 != STRLEN(saveptr)) {
        delimiter = delimiters[i];
        break;
      }
    }
    while (OB_SUCC(ret) && OB_NOT_NULL(token)) {
      func();
      token = STRTOK_R(NULL, delimiter, &saveptr);
    }
  }

  if (NULL != buf) {
    delete [] buf;
    buf = NULL;
  }
  return ret;
}


} // end of namespace common
} // end of namespace oceanbase
