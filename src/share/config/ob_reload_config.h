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

#ifndef OCEANBASE_SHARE_CONFIG_OB_RELOAD_CONFIG_H_
#define OCEANBASE_SHARE_CONFIG_OB_RELOAD_CONFIG_H_

#include "share/ob_define.h"
#include "share/cache/ob_kv_storecache.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace common
{
class ObReloadConfig
{
public:
  explicit ObReloadConfig(ObServerConfig *conf): conf_(conf) {};
  virtual ~ObReloadConfig() {}
  virtual int operator()();

protected:
  ObServerConfig *conf_;

private:
  int reload_ob_logger_set();
  DISALLOW_COPY_AND_ASSIGN(ObReloadConfig);
};

inline int ObReloadConfig::operator()()
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(NULL != conf_)) {
    reload_ob_logger_set();
  }
  return ret;
}

} // end of namespace common
} // end of namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_RELOAD_CONFIG_H_
