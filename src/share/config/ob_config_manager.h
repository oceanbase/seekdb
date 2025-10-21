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
// Remove code changes are significant, keep for now

namespace oceanbase
{

namespace obrpc
{
  class ObTenantConfigArg;
}

namespace common
{
class ObMySQLProxy;
using UpdateTenantConfigCb = common::ObFunction<void(uint64_t tenant_id)>;

class ObConfigManager
{
  friend class UpdateTask;
public:
  static const int64_t DEFAULT_VERSION = 1;

  ObConfigManager(ObServerConfig &server_config, ObReloadConfig &reload_config);
  virtual ~ObConfigManager();

  // get newest version received
  const int64_t &get_version() const;
  // config version current used
  int64_t get_current_version() const;

  int base_init();

  int init(ObMySQLProxy &sql_proxy, const ObAddr &server,
           const UpdateTenantConfigCb &update_tenant_config_cb);
  void stop();
  void wait();
  void destroy();

  int check_header_change(const char* path, const char* buf) const;
  // manual dump to file named by path
  int dump2file(const char *path = NULL) const;

  // set dump path (filename) for autodump
  void set_dump_path(const char *path);

  // This function should been invoked after @base_init
  int load_config(const char *path = NULL);

  // Reload config really
  int reload_config();

  ObServerConfig &get_config(void);

  int update_local(int64_t expected_version);
  virtual int got_version(int64_t version, const bool remove_repeat = false);
  int add_extra_config(const obrpc::ObTenantConfigArg &arg);
  void notify_tenant_config_changed(uint64_t tenant_id);
  int init_tenant_config(const obrpc::ObTenantConfigArg &arg);
private:
  class UpdateTask
    : public ObTimerTask
  {
  public:
    UpdateTask()
        : config_mgr_(NULL), update_local_(false), version_(0), scheduled_time_(0)
    {}
    virtual ~UpdateTask() {}
    // main routine
    void runTimerTask(void);
    ObConfigManager *config_mgr_;
    bool update_local_;
    volatile int64_t version_;
    volatile int64_t scheduled_time_;
  private:
    DISALLOW_COPY_AND_ASSIGN(UpdateTask);
  };

private:
  // whitout lock, only used inner
  int dump2file_unsafe(const char *path = NULL) const;

private:
  bool inited_;
  bool init_config_load_; // 
  UpdateTask update_task_; // Update local config from internal table
  ObAddr self_;
  ObMySQLProxy *sql_proxy_;
  ObSystemConfig system_config_;
  ObServerConfig &server_config_;
  int64_t current_version_;
  char dump_path_[OB_MAX_FILE_NAME_LENGTH];
  ObReloadConfig &reload_config_func_;
  UpdateTenantConfigCb update_tenant_config_cb_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigManager);
};

inline ObConfigManager::ObConfigManager(ObServerConfig &server_config,
                                        ObReloadConfig &reload_config)
    : inited_(false),
      init_config_load_(false),
      update_task_(),
      self_(),
      sql_proxy_(NULL),
      system_config_(),
      server_config_(server_config),
      current_version_(1),
      reload_config_func_(reload_config)
{
  dump_path_[0] = '\0';
}

inline int64_t ObConfigManager::get_current_version() const
{
  return current_version_;
}

inline ObServerConfig &ObConfigManager::get_config(void)
{
  return server_config_;
}

inline void ObConfigManager::set_dump_path(const char *path)
{
  snprintf(dump_path_, OB_MAX_FILE_NAME_LENGTH, "%s", path);
}

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_MANAGER_H_
