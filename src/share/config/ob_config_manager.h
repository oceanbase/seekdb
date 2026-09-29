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

#ifndef OCEANBASE_SHARE_CONFIG_OB_CONFIG_MANAGER_H_
#define OCEANBASE_SHARE_CONFIG_OB_CONFIG_MANAGER_H_

#include "share/config/ob_server_config.h"
#include "share/config/ob_reload_config.h"
#include "config.h"

namespace oceanbase
{

namespace common
{
class ObConfigManager
{
public:
  explicit ObConfigManager(ObReloadConfig &reload_config);
  virtual ~ObConfigManager();

  int init();

  // Reload config really
  int reload_config();

  int update_local();
  virtual int got_version();
  int save_configs();
  int save_internal_state(const char *name, const char *value);
  int update_checked(const char *name, const char *value, bool reset,
                     ConfigCheckCallback callback, void *context,
                     bool *after_replace);
  void enable_static_effect() { enable_static_effect_ = true; }
private:
  bool inited_;
  ObReloadConfig &reload_config_func_;
  bool enable_static_effect_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigManager);
};

inline ObConfigManager::ObConfigManager(ObReloadConfig &reload_config)
    : inited_(false),
      reload_config_func_(reload_config),
      enable_static_effect_(false)
{
}

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_MANAGER_H_
