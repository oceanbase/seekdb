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

#include "lib/alloc/alloc_func.h"
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
#include "share/cache/ob_kvcache_struct.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace common
{

namespace
{
constexpr int64_t MEMORY_BUDGET_PERCENTAGE = 50;
constexpr int64_t KV_CACHE_MEMORY_PERCENTAGE = 30;
constexpr int64_t SHARED_MODULE_MEMORY_PERCENTAGE = 80;

int64_t resolve_shared_module_memory_limit(const int64_t configured_limit,
                                           const int64_t memory_budget)
{
  return configured_limit > 0
      ? configured_limit
      : lib::get_memory_by_percentage(memory_budget, SHARED_MODULE_MEMORY_PERCENTAGE);
}
}

int64_t get_cpu_count()
{
  int64_t cpu_cnt = GCONF.cpu_count;
  return cpu_cnt > 0 ? cpu_cnt : get_cpu_num();
}

using namespace share;

ObServerConfig::ObServerConfig()
  : disk_actual_space_(0), self_addr_(), rwlock_(ObLatchIds::CONFIG_LOCK), global_version_(0)
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

int ObServerConfig::read_config(const ObSystemConfig &system_config,
                                const bool enable_static_effect)
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
      temp_ret = system_config.read_config(key, *(it->second));
      if (OB_SUCCESS != temp_ret) {
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
  : kvcache_memory_limit_(resolve_kvcache_memory_limit(0, get_effective_memory_size())),
    kvcache_memory_capacity_(0),
    memstore_memory_limit_(resolve_memstore_memory_limit(
        0, calculate_automatic_memory_budget(get_effective_memory_size()))),
    vector_memory_limit_(resolve_vector_memory_limit(
        0, calculate_automatic_memory_budget(get_effective_memory_size())))
{}

ObServerMemoryConfig &ObServerMemoryConfig::get_instance()
{
  static ObServerMemoryConfig memory_config;
  return memory_config;
}

int64_t ObServerMemoryConfig::calculate_automatic_memory_budget(
    const int64_t system_memory)
{
  const int64_t automatic_memory_budget = lib::get_memory_by_percentage(
      system_memory, MEMORY_BUDGET_PERCENTAGE);
  return automatic_memory_budget > lib::DEFAULT_MEMORY_BUDGET
      ? automatic_memory_budget
      : lib::DEFAULT_MEMORY_BUDGET;
}

int64_t ObServerMemoryConfig::resolve_kvcache_memory_limit(
    const int64_t configured_limit,
    const int64_t system_memory)
{
  const int64_t requested_limit = configured_limit > 0
      ? configured_limit
      : lib::get_memory_by_percentage(system_memory, KV_CACHE_MEMORY_PERCENTAGE);
  return requested_limit < MAX_KVCACHE_MEMORY_SIZE
      ? requested_limit
      : MAX_KVCACHE_MEMORY_SIZE;
}

int64_t ObServerMemoryConfig::resolve_memstore_memory_limit(
    const int64_t configured_limit,
    const int64_t memory_budget)
{
  return resolve_shared_module_memory_limit(configured_limit, memory_budget);
}

int64_t ObServerMemoryConfig::resolve_vector_memory_limit(
    const int64_t configured_limit,
    const int64_t memory_budget)
{
  return resolve_shared_module_memory_limit(configured_limit, memory_budget);
}

int ObServerMemoryConfig::reload_config(const ObServerConfig& server_config)
{
  int ret = OB_SUCCESS;
  const int64_t configured_memory_budget = server_config._memory_budget;
  int64_t memory_budget = configured_memory_budget;
  const int64_t physical_memory = get_phy_mem_size();
  const int64_t cgroup_memory_limit = get_cgroup_memory_limit();
  const int64_t effective_memory = cgroup_memory_limit > 0 &&
      (physical_memory <= 0 || cgroup_memory_limit < physical_memory)
      ? cgroup_memory_limit
      : physical_memory;
  const int64_t automatic_memory_budget =
      calculate_automatic_memory_budget(effective_memory);
  if (0 == memory_budget) {
    memory_budget = automatic_memory_budget;
  }
  const int64_t configured_kvcache_memory_limit =
      server_config.kvcache_memory_limit;
  const int64_t configured_memstore_memory_limit =
      server_config.memstore_memory_limit;
  const int64_t configured_vector_memory_limit =
      server_config.vector_memory_limit;
  const int64_t resolved_kvcache_memory_limit = resolve_kvcache_memory_limit(
      configured_kvcache_memory_limit, effective_memory);
  int64_t kvcache_memory_capacity = get_kvcache_memory_capacity();
  if (0 == kvcache_memory_capacity) {
    kvcache_memory_capacity =
        MIN(resolved_kvcache_memory_limit, MAX_KVCACHE_MEMORY_SIZE / 2) * 2;
    kvcache_memory_capacity_.store(kvcache_memory_capacity, std::memory_order_release);
  }
  const int64_t kvcache_memory_limit =
      MIN(resolved_kvcache_memory_limit, kvcache_memory_capacity);
  const int64_t memstore_memory_limit = resolve_memstore_memory_limit(
      configured_memstore_memory_limit, memory_budget);
  const int64_t vector_memory_limit = resolve_vector_memory_limit(
      configured_vector_memory_limit, memory_budget);
  lib::set_memory_budget(memory_budget);
  kvcache_memory_limit_.store(kvcache_memory_limit, std::memory_order_release);
  memstore_memory_limit_.store(memstore_memory_limit, std::memory_order_release);
  vector_memory_limit_.store(vector_memory_limit, std::memory_order_release);
  LOG_INFO("update observer memory config", K(memory_budget),
           K(configured_memory_budget), K(physical_memory),
           K(cgroup_memory_limit), K(effective_memory),
           K(automatic_memory_budget), K(kvcache_memory_limit),
           K(resolved_kvcache_memory_limit), K(kvcache_memory_capacity),
           K(configured_kvcache_memory_limit), K(memstore_memory_limit),
           K(configured_memstore_memory_limit), K(vector_memory_limit),
           K(configured_vector_memory_limit));
  return ret;
}

int64_t ObServerMemoryConfig::get_server_memory_budget() const
{
  return lib::get_memory_budget();
}

int64_t ObServerMemoryConfig::get_kvcache_memory_limit() const
{
  return kvcache_memory_limit_.load(std::memory_order_acquire);
}

int64_t ObServerMemoryConfig::get_kvcache_memory_capacity() const
{
  return kvcache_memory_capacity_.load(std::memory_order_acquire);
}

int64_t ObServerMemoryConfig::get_memstore_memory_limit() const
{
  return memstore_memory_limit_.load(std::memory_order_acquire);
}

int64_t ObServerMemoryConfig::get_vector_memory_limit() const
{
  return vector_memory_limit_.load(std::memory_order_acquire);
}

} // end of namespace common
} // end of namespace oceanbase
