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

#include "ob_system_config.h"
#include "share/config/ob_server_config.h"
#include "share/ob_task_define.h"
#include "share/ob_cluster_version.h"

namespace oceanbase
{
namespace common
{

int ObSystemConfig::find(const ObSystemConfigKey &key,
                         const ObSystemConfigValue *&pvalue) const
{
  int ret = OB_SEARCH_NOT_FOUND;
  hashmap::const_iterator it = map_.begin();
  hashmap::const_iterator last = map_.end();
  pvalue = NULL;

  for (; it != last; ++it) {
    if (it->first.match(key)) {
      pvalue = it->second;
      ret = OB_SUCCESS;
      break; // Found first matching config
    }
  }

  return ret;
}


int ObSystemConfig::update_value(const ObSystemConfigKey &key, const ObSystemConfigValue &value)
{
  int ret = OB_SUCCESS;
  int hash_ret = OB_SUCCESS;
  ObSystemConfigValue *sys_value = nullptr;
  hash_ret = map_.get_refactored(key, sys_value);
  if (OB_SUCCESS != hash_ret) {
    if (OB_HASH_NOT_EXIST == hash_ret) {
      void *ptr = allocator_.alloc(sizeof(ObSystemConfigValue));
      if (OB_ISNULL(ptr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SHARE_LOG(WARN, "alloc memory failed");
      } else {
        sys_value = new (ptr) ObSystemConfigValue();
        sys_value->set_value(value.value());
        hash_ret = map_.set_refactored(key, sys_value);
        if (OB_SUCCESS != hash_ret) {
          if (OB_HASH_EXIST == hash_ret) {
            SHARE_LOG(WARN, "sys config insert repeatly", "name", key.name(), K(hash_ret));
          } else {
            ret = hash_ret;
            SHARE_LOG(WARN, "sys config map set failed", "name", key.name(), K(ret));
          }
        }
      }
    } else {
      ret = hash_ret;
      SHARE_LOG(WARN, "sys config map get failed", "name", key.name(), K(ret));
    }
  } else {
    sys_value->set_value(value.value());
  }
  return ret;
}



int ObSystemConfig::read_int64(const ObSystemConfigKey &key,
                               int64_t &value,
                               const int64_t &def) const
{
  int ret = OB_SUCCESS;
  const ObSystemConfigValue *pvalue = NULL;
  char *p = NULL;
  if (OB_SUCC(find(key, pvalue)) && OB_LIKELY(NULL != pvalue)) {
    value = strtoll(pvalue->value(), &p, 0);
    if (p == pvalue->value()) {
      SHARE_LOG(ERROR, "config is not integer", "name", key.name(), "value", p);
    } else if (OB_ISNULL(p) || OB_UNLIKELY('\0' != *p)) {
      SHARE_LOG(WARN, "config was truncated", "name", key.name(), "value", p);
    } else {
      SHARE_LOG(INFO, "use internal config", "name", key.name(), K(value));
    }
  } else {
    value = def;
    SHARE_LOG(INFO, "use default config", "name", key.name(), K(value), K(pvalue), K(ret));
    ret = OB_SUCCESS;
  }
  return ret;
}


// tenant_id is OB_INVALID_TENANT_ID(0) means it's cluster parameter
int ObSystemConfig::read_config(
    const uint64_t tenant_id,
    const ObSystemConfigKey &key,
    ObConfigItem &item) const
{
  int ret = OB_SUCCESS;
  const ObSystemConfigValue *pvalue = NULL;
  if (OB_SUCC(find(key, pvalue))) {
    if (OB_ISNULL(pvalue)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!item.set_value_unsafe(pvalue->value())) {
      // without set ret
      SHARE_LOG(WARN, "set config item value failed",
                K(key.name()), K(pvalue->value()));
    }
  }
  return ret;
}


} // end of namespace common
} // end of namespace oceanbase
