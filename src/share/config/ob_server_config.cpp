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

#include "lib/alloc/alloc_struct.h"
#include "lib/cpu/ob_cpu_topology.h"
#include "lib/hash/ob_hashtable.h"
#include "lib/hash/ob_hashutils.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/stat/ob_latch_define.h"
#include "lib/utility/utility.h"
#include "share/config/ob_config.h"
#include "share/config/ob_system_config.h"
#include "share/config/ob_system_config_key.h"
#include "share/config/ob_runtime_config.h"
#include "share/ob_errno.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_server_struct.h"

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
      temp_ret = system_config_->read_config(key, *(it->second));
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

static double calc_default_server_cpu(const double quota)
{
  double cpu = quota;
  if (0 == cpu) {
    int64_t n = get_cpu_count();
    if (n <= 4)           cpu = 1;
    else if (n <= 8)      cpu = 2;
    else if (n <= 16)     cpu = 3;
    else if (n <= 32)     cpu = 4;
    else if (n <= 64)     cpu = 6;
    else                  cpu = n / 10.0;
  }
  return cpu;
}

double ObServerConfig::get_server_default_min_cpu()
{
  return calc_default_server_cpu(server_cpu_quota_min);
}

double ObServerConfig::get_server_default_max_cpu()
{
  return calc_default_server_cpu(server_cpu_quota_max);
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

void ObServerMemoryConfig::check_limit()
{
  // check unmanaged memory size
  const int64_t UNMANAGED_MEMORY_LIMIT = 2LL<<30;
  int64_t unmanaged_memory_size = lib::get_unmanaged_memory_size();
  if (unmanaged_memory_size > UNMANAGED_MEMORY_LIMIT) {
    LOG_ERROR_RET(OB_EXCEED_MEM_LIMIT, "unmanaged_memory_size is over the limit",
                  K(unmanaged_memory_size), K(UNMANAGED_MEMORY_LIMIT));
  }
}

int ObServerConfig::publish_special_config_after_dump()
{
  int ret = OB_SUCCESS;
  return ret;
}


} // end of namespace common
namespace obgrpc {
bool ob_grpc_is_rpc_tls_enabled()
{
  return GCONF.enable_rpc_tls;
}
} // end of namespace obgrpc
} // end of namespace oceanbase
