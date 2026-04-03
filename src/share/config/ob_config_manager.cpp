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


#include "ob_config_manager.h"
#include "observer/ob_sql_client_decorator.h"
#include "observer/ob_server.h"

namespace oceanbase
{
namespace common
{
ObConfigManager::~ObConfigManager()
{
}

int ObConfigManager::init(share::ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid storage", K(ret));
  } else if (OB_FAIL(system_config_.init())) {
    LOG_ERROR("init system config failed", K(ret));
  } else if (OB_FAIL(server_config_.init(system_config_))) {
    LOG_ERROR("init server config failed", K(ret));
  } else if (OB_FAIL(storage_.init(pool))) {
    LOG_WARN("failed to init storage", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

void ObConfigManager::stop()
{
}

void ObConfigManager::wait()
{
}

void ObConfigManager::destroy()
{
}

int ObConfigManager::reload_config()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(server_config_.check_all())) {
    LOG_WARN("Check configuration failed, can't reload", K(ret));
  } else if (OB_FAIL(reload_config_func_())) {
    LOG_WARN("Reload configuration failed.", K(ret));
  } else if (OB_FAIL(OBSERVER.get_net_frame().reload_ssl_config())) {
    LOG_WARN("reload ssl config for net frame fail", K(ret));

  } else if (OB_FAIL(OBSERVER.get_net_frame().reload_sql_thread_config())) {
    LOG_WARN("reload config for mysql login thread count failed", K(ret));
  } else if (OB_FAIL(ObTdeEncryptEngineLoader::get_instance().reload_config())) {
    LOG_WARN("reload config for tde encrypt engine fail", K(ret));
  } else if (OB_FAIL(GCTX.omt_->update_hidden_sys_tenant())) {
    LOG_WARN("update hidden sys tenant failed", K(ret));
  } else {
    g_enable_ob_error_msg_style = GCONF.enable_ob_error_msg_style;
  }
  return ret;
}

int ObConfigManager::check_header_change(const char* path, const char* buf) const
{
  UNUSED(path);
  UNUSED(buf);
  return OB_SUCCESS;
}

int ObConfigManager::dump2file_unsafe(const char* path) const
{
  UNUSED(path);
  return OB_SUCCESS;
}

int ObConfigManager::dump2file(const char* path) const
{
  DRWLock::RDLockGuard guard(GCONF.rwlock_);
  return dump2file_unsafe(path);
}

int ObConfigManager::update_local()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(system_config_.clear())) {
    LOG_WARN("Clear system config map failed", K(ret));
  } else {
    DRWLock::WRLockGuard guard(GCONF.rwlock_);
    if (OB_FAIL(storage_.load_all_configs(system_config_))) {
      LOG_WARN("failed to load config", K(ret));
    } else {
      LOG_INFO("read config success");
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(server_config_.read_config(enable_static_effect_))) {
      LOG_ERROR("Read server config failed", K(ret));
    }
    server_config_.print();
  } else {
    LOG_WARN("Read system config error", K(ret));
  }
  return ret;
}

int ObConfigManager::got_version()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("config manager not inited", K(ret));
  } else {
    if (OB_FAIL(update_local())) {
      LOG_WARN("update local config failed", K(ret));
    } else {
      LOG_INFO("loaded new config synchronously");
    }
  }
  return ret;
}

int ObConfigManager::add_extra_config(const obrpc::ObTenantConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid arg", K(ret), K(arg));
  } else {
    ret = server_config_.add_extra_config(arg.config_str_.ptr());
  }
  LOG_INFO("add tenant extra config", K(arg));
  return ret;
}

int ObConfigManager::init_tenant_config(const obrpc::ObTenantConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_extra_config(arg))) {
    LOG_WARN("fail to add extra config", KR(ret), K(arg));
  } else {
    if (OB_FAIL(server_config_.publish_special_config_after_dump())) {
      LOG_WARN("publish special config after dump failed", K(ret));
    }
  }
  return ret;
}

int ObConfigManager::save_config(
    const char *config_name,
    const char *value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config_name) || OB_ISNULL(value)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(config_name), KP(value));
  } else {
    // Get config item from server_config_ container
    ObConfigItem *const *ci_ptr = server_config_.get_container().get(
                                     ObConfigStringKey(config_name));
    if (OB_ISNULL(ci_ptr)) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
      LOG_WARN("can't found config item", K(ret), K(config_name));
    } else if (OB_ISNULL(*ci_ptr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("config item is null", K(ret), K(config_name));
    } else {
      const ObConfigItem *config_item = *ci_ptr;
      if (OB_FAIL(storage_.upsert_config(
          config_name,
          config_item->data_type(), value, config_item->info(), config_item->section(), config_item->scope(),
          config_item->source(), config_item->edit_level()))) {
        LOG_WARN("failed to save config ", K(ret),
                 "name", config_name, "value", value);
      }
    }
  }
  return ret;
}

int ObConfigManager::save_configs(int64_t base_version)
{
  int ret = OB_SUCCESS;
  ObConfigContainer::const_iterator it = server_config_.get_container().begin();
  for (; OB_SUCC(ret) && it != server_config_.get_container().end(); ++it) {
    if (OB_ISNULL(it->second)) {
      // ignore ret
      LOG_WARN("config item is null", "name", it->first.str());
      continue;
    }
      if (it->second->version() > base_version) {
      if (OB_FAIL(save_config(it->first.str(), it->second->str()))) {
        LOG_WARN("failed to save startup config", K(ret),
                 "name", it->first.str());
      }
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
