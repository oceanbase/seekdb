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

#define USING_LOG_PREFIX SHARE_CONFIG

#include "ob_server_config.h"
#include "observer/ob_server.h"
namespace oceanbase
{
namespace common
{

int64_t get_cpu_count()
{
  int64_t cpu_cnt = GCONF.cpu_count;
  return cpu_cnt > 0 ? cpu_cnt : get_cpu_num();
}

using namespace share;

ObServerConfig::ObServerConfig()
  : disk_actual_space_(0), self_addr_(), rwlock_(ObLatchIds::CONFIG_LOCK), system_config_(NULL), global_version_(0)
{
#undef DEF_PARAM
#define DEF_PARAM(name, args...) name.update_cb_ = this;
#include "share/parameter/ob_parameter_seed.ipp"
#undef DEF_PARAM
}

ObServerConfig::~ObServerConfig()
{
}

ObServerConfig &ObServerConfig::get_instance()
{
  static ObServerConfig config;
  return config;
}

int ObServerConfig::init(const ObSystemConfig &config)
{
  int ret = OB_SUCCESS;
  system_config_ = &config;
  if (OB_ISNULL(system_config_)) {
    ret = OB_INIT_FAIL;
  }
  return ret;
}

bool ObServerConfig::in_upgrade_mode() const
{
  bool bret = false;
  if (enable_upgrade_mode) {
    bret = true;
  } else {
    obrpc::ObUpgradeStage stage = GCTX.get_upgrade_stage();
    bret = (stage >= obrpc::OB_UPGRADE_STAGE_PREUPGRADE
            && stage <= obrpc::OB_UPGRADE_STAGE_POSTUPGRADE);
  }
  return bret;
}


int ObServerConfig::read_config(const bool enable_static_effect)
{
  int ret = OB_SUCCESS;
  int temp_ret = OB_SUCCESS;
  ObSystemConfigKey key;
  ObConfigContainer::const_iterator it = container_.begin();
  for (; OB_SUCC(ret) && it != container_.end(); ++it) {
    key.set_name(it->first.str());
    if (OB_ISNULL(it->second)) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(ERROR, "config item is null", "name", it->first.str(), K(ret));
    } else if (!it->second->reboot_effective() || !enable_static_effect) {
      temp_ret = system_config_->read_config(get_tenant_id(), key, *(it->second));
      if (OB_SUCCESS != temp_ret) {
        OB_LOG(DEBUG, "Read config error", "name", it->first.str(), K(temp_ret));
      }
    }
  }
  return ret;
}

int ObServerConfig::check_all() const
{
  int ret = OB_SUCCESS;
  ObConfigContainer::const_iterator it = container_.begin();
  for (; OB_SUCC(ret) && it != container_.end(); ++it) {
    if (OB_ISNULL(it->second)) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(ERROR, "config item is null", "name", it->first.str(), K(ret));
    } else if (!it->second->check()) {
      int temp_ret = OB_INVALID_CONFIG;
      OB_LOG_RET(WARN, temp_ret, "Configure setting invalid",
             "name", it->first.str(), "value", it->second->str(), K(temp_ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

void ObServerConfig::print() const
{
  OB_LOG(INFO, "===================== *begin server config report * =====================");
  ObConfigContainer::const_iterator it = container_.begin();
  for (; it != container_.end(); ++it) {
    if (OB_ISNULL(it->second)) {
      OB_LOG_RET(WARN, OB_ERROR, "config item is null", "name", it->first.str());
    } else {
      _OB_LOG(INFO, "| %-36s = %s", it->first.str(), it->second->str());
    }
  }
  OB_LOG(INFO, "===================== *stop server config report* =======================");
}

int ObServerConfig::add_extra_config(const char *config_str,
                                     const int64_t version /* = 0 */,
                                     const bool check_config /* = true */)
{
  DRWLock::WRLockGuard guard(GCONF.rwlock_);
  return add_extra_config_unsafe(config_str, version, check_config);
}

double ObServerConfig::get_sys_tenant_default_min_cpu()
{
  double min_cpu = server_cpu_quota_min;
  if (0 == min_cpu) {
    int64_t cpu_count = get_cpu_count();
    if (cpu_count < 8) {
      min_cpu = 1;
    } else if (cpu_count < 16) {
      min_cpu = 2;
    } else if (cpu_count < 32) {
      min_cpu = 3;
    } else {
      min_cpu = 4;
    }
  }
  return min_cpu;
}

double ObServerConfig::get_sys_tenant_default_max_cpu()
{
  double max_cpu = server_cpu_quota_max;
  if (0 == max_cpu) {
    int64_t cpu_count = get_cpu_count();
    if (cpu_count < 8) {
      max_cpu = 1;
    } else if (cpu_count < 16) {
      max_cpu = 2;
    } else if (cpu_count < 32) {
      max_cpu = 3;
    } else {
      max_cpu = 4;
    }
  }
  return max_cpu;
}

ObServerMemoryConfig::ObServerMemoryConfig()
  : memory_limit_(0), hard_memory_limit_(INT64_MAX)
{}

ObServerMemoryConfig &ObServerMemoryConfig::get_instance()
{
  static ObServerMemoryConfig memory_config;
  return memory_config;
}

int ObServerMemoryConfig::reload_config(const ObServerConfig& server_config)
{
  int ret = OB_SUCCESS;
  int64_t memory_limit = server_config.memory_limit;
  int64_t hard_memory_limit = server_config.memory_hard_limit;
  int64_t phy_mem_size = get_phy_mem_size();
  if (0 == memory_limit) {
    memory_limit = phy_mem_size * server_config.memory_limit_percentage / 100;
  }
  if (0 == hard_memory_limit) {
    hard_memory_limit = phy_mem_size * MAX_PHY_MEM_PERCENTAGE / 100;
  }
  hard_memory_limit_ = hard_memory_limit;
  memory_limit_ = MIN(memory_limit, hard_memory_limit_);
  LOG_INFO("update observer memory config", K_(memory_limit), K_(hard_memory_limit));
  return ret;
}

void ObServerMemoryConfig::check_limit(bool ignore_error)
{
  int ret = OB_SUCCESS;
  // check unmanaged memory size
  const int64_t UNMANAGED_MEMORY_LIMIT = 2LL<<30;
  int64_t unmanaged_memory_size = get_unmanaged_memory_size();
  if (unmanaged_memory_size > UNMANAGED_MEMORY_LIMIT) {
    if (ignore_error) {
      LOG_WARN("unmanaged_memory_size is over the limit", K(unmanaged_memory_size), K(UNMANAGED_MEMORY_LIMIT));
    } else {
      LOG_ERROR("unmanaged_memory_size is over the limit", K(unmanaged_memory_size), K(UNMANAGED_MEMORY_LIMIT));
    }
  }
}

int ObServerConfig::publish_special_config_after_dump()
{
  int ret = OB_SUCCESS;
  return ret;
}


} // end of namespace common
namespace obrpc {
int64_t get_max_rpc_packet_size()
{
  return GCONF._max_rpc_packet_size;
}

int64_t get_stream_rpc_max_wait_timeout(int64_t tenant_id)
{
  int64_t stream_rpc_max_wait_timeout = ObRpcProcessorBase::DEFAULT_WAIT_NEXT_PACKET_TIMEOUT;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id));
  if (OB_LIKELY(tenant_config.is_valid())) {
    stream_rpc_max_wait_timeout = tenant_config->_stream_rpc_max_wait_timeout;
  }
  return stream_rpc_max_wait_timeout;
}

bool stream_rpc_update_timeout()
{
  return true;
}

} // end of namespace obrpc
namespace obgrpc {
bool ob_grpc_is_rpc_tls_enabled()
{
  return GCONF.enable_rpc_tls;
}
} // end of namespace obgrpc
} // end of namespace oceanbase

namespace easy
{
};
