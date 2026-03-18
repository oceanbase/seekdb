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

#include "lib/thread/thread_mgr_interface.h"
#include "share/config/ob_server_config.h"
#include "share/config/ob_system_config.h"
#include "share/config/ob_reload_config.h"
#include "share/config/ob_config_storage.h"
// Remove code changes are significant, keep for now

namespace oceanbase
{

namespace obrpc
{
  class ObTenantConfigArg;
}

namespace common
{
class ObConfigManager
{
public:
  ObConfigManager(ObServerConfig &server_config, ObReloadConfig &reload_config);
  virtual ~ObConfigManager();

  int init(share::ObSQLiteConnectionPool *pool);
  void stop();
  void wait();
  void destroy();

  int check_header_change(const char* path, const char* buf) const;
  // manual dump to file named by path
  int dump2file(const char *path = NULL) const;

  // set dump path (filename) for autodump

  // Reload config really
  int reload_config();

  ObServerConfig &get_config(void);

  ObConfigStorage &get_storage() { return storage_; }

  int update_local();
  virtual int got_version();
  int64_t get_current_version() const { return server_config_.get_current_version(); }
  int save_configs(int64_t base_version);
  int save_config(
      const char *config_name,
      const char *value);
  int add_extra_config(const obrpc::ObTenantConfigArg &arg);
  int init_tenant_config(const obrpc::ObTenantConfigArg &arg);
  void enable_static_effect() { enable_static_effect_ = true; }
private:
  // whitout lock, only used inner
  int dump2file_unsafe(const char *path = NULL) const;

private:
  bool inited_;
  ObSystemConfig system_config_;
  ObServerConfig &server_config_;
  ObReloadConfig &reload_config_func_;
  ObConfigStorage storage_;  // Will be initialized with shared storage from ObServer
  bool enable_static_effect_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigManager);
};

inline ObConfigManager::ObConfigManager(ObServerConfig &server_config,
                                        ObReloadConfig &reload_config)
    : inited_(false),
      system_config_(),
      server_config_(server_config),
      reload_config_func_(reload_config),
      enable_static_effect_(false)
{
}

inline ObServerConfig &ObConfigManager::get_config(void)
{
  return server_config_;
}

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_MANAGER_H_
