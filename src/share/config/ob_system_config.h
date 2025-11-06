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

#ifndef OCEANBASE_SHARE_CONFIG_OB_SYSTEM_CONFIG_H_
#define OCEANBASE_SHARE_CONFIG_OB_SYSTEM_CONFIG_H_

#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_array.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/config/ob_system_config_key.h"
#include "share/config/ob_system_config_value.h"

namespace oceanbase
{
namespace common
{
class ObConfigItem;

class ObSystemConfig
{
public:
  static const int64_t INIT_VERSION = 2;
  typedef hash::ObHashMap<ObSystemConfigKey, ObSystemConfigValue*> hashmap;
public:
  ObSystemConfig() : allocator_("SystemConfig", common::OB_MALLOC_MIDDLE_BLOCK_SIZE),
                     map_(), version_(0) {};
  virtual ~ObSystemConfig() {};

  int clear();
  int init();
  int update(ObMySQLProxy::MySQLResult &result);

  int find_newest(const ObSystemConfigKey &key,
                  const ObSystemConfigValue *&pvalue,
                  int64_t &max_version) const;
  int read_int64(const ObSystemConfigKey &key, int64_t &value, const int64_t &def) const;
  int read_int(const ObSystemConfigKey &key, int64_t &value, const int64_t &def) const;
  int read_config(const uint64_t tenant_id, const ObSystemConfigKey &key, ObConfigItem &item) const;
  int64_t get_version() const { return version_; }

private:
  int update_value(const ObSystemConfigKey &key, const ObSystemConfigValue &value);
private:
  static const int64_t MAP_SIZE = 512;
  ObArenaAllocator allocator_;
  hashmap map_;
  int64_t version_;
  DISALLOW_COPY_AND_ASSIGN(ObSystemConfig);
};

inline int ObSystemConfig::clear()
{
  int ret = map_.clear();
  allocator_.reset();
  return ret;
}

inline int ObSystemConfig::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(map_.create(MAP_SIZE, ObModIds::OB_HASH_BUCKET_SYS_CONF))) {
    OB_LOG(WARN, "create params_map_ fail", K(ret));
  }
  return ret;
}

inline int ObSystemConfig::read_int(const ObSystemConfigKey &key,
                                    int64_t &value,
                                    const int64_t &def) const
{
  return read_int64(key, value, def);
}
} // end of namespace common
} // end of namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_SYSTEM_CONFIG_H_
