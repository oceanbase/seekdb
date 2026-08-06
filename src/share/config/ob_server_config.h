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

#ifndef OCEANBASE_SHARE_CONFIG_OB_SERVER_CONFIG_H_
#define OCEANBASE_SHARE_CONFIG_OB_SERVER_CONFIG_H_

#include <atomic>

#include "share/config/ob_common_config.h"
#include "share/config/ob_system_config.h"
#include "lib/lock/ob_drw_lock.h"

namespace oceanbase
{
namespace unittest
{
  class ObSimpleClusterTestBase;
  class ObMultiReplicaTestBase;
}
namespace common
{
class ObISQLClient;
const char* const MERGER_CHECK_INTERVAL = "merger_check_interval";
const char* const ENABLE_MAJOR_FREEZE = "enable_major_freeze";
const char* const ENABLE_DDL = "enable_ddl";
const char* const ENABLE_AUTO_LEADER_SWITCH = "enable_auto_leader_switch";
const char* const MAJOR_COMPACT_TRIGGER = "major_compact_trigger";
const char* const ENABLE_PERF_EVENT = "enable_perf_event";
const char* const CONFIG_TRUE_VALUE_BOOL = "1";
const char* const CONFIG_FALSE_VALUE_BOOL = "0";
const char* const CONFIG_TRUE_VALUE_STRING = "true";
const char* const CONFIG_FALSE_VALUE_STRING = "false";
const char* const SCHEMA_HISTORY_RECYCLE_INTERVAL = "schema_history_recycle_interval";
const char* const _RECYCLEBIN_OBJECT_PURGE_FREQUENCY = "_recyclebin_object_purge_frequency";
const char* const FREEZE_TRIGGER_PERCENTAGE = "freeze_trigger_percentage";
const char* const WRITING_THROTTLEIUNG_TRIGGER_PERCENTAGE = "writing_throttling_trigger_percentage";
const char* const DATA_DISK_WRITE_LIMIT_PERCENTAGE = "data_disk_write_limit_percentage";
const char* const DATA_DISK_USAGE_LIMIT_PERCENTAGE = "data_disk_usage_limit_percentage";
const char* const COMPATIBLE = "compatible";
const char* const ENABLE_COMPATIBLE_MONOTONIC = "_enable_compatible_monotonic";
const char* const WEAK_READ_VERSION_REFRESH_INTERVAL = "weak_read_version_refresh_interval";
const char* const LOG_DISK_UTILIZATION_LIMIT_THRESHOLD = "log_disk_utilization_limit_threshold";
const char* const LOG_DISK_THROTTLING_PERCENTAGE = "log_disk_throttling_percentage";
const char* const DEFAULT_TABLE_ORGANIZATION = "default_table_organization";

class ObServerMemoryConfig;

class ObServerConfig : public ObCommonConfig, ObConfigUpdateCb
{
public:
  friend class ObServerMemoryConfig;
  static ObServerConfig &get_instance();

  // Copy all applicable values from a temporary system config snapshot.
  virtual int read_config(const ObSystemConfig &system_config,
                          const bool enable_static_effect);

  // check if all config is validated
  virtual int check_all() const;
  // print all config to log file
  void print() const;

  int64_t get_current_version() const { return global_version_; }
  int add_extra_config(const char *config_str,
                       const int64_t version = 0,
                       const bool check_config = true);

  double get_server_default_min_cpu();
  double get_server_default_max_cpu();

  virtual int64_t update_version() { return ATOMIC_AAF(&global_version_, 1); }
  virtual bool is_debug_sync_enabled() const { return static_cast<int64_t>(debug_sync_timeout) > 0; }

  bool is_sql_operator_dump_enabled() const { return enable_sql_operator_dump; }

  bool enable_defensive_check() const
  {
    int64_t v = _enable_defensive_check;
    return v > 0;
  }

  bool enable_strict_defensive_check() const
  {
    int64_t v = _enable_defensive_check;
    return v == 2;
  }

  int publish_special_config_after_dump();

public:
  int64_t disk_actual_space_;
  ObAddr self_addr_;
  mutable common::DRWLock rwlock_;
public:
///////////////////////////////////////////////////////////////////////////////
// use MACRO 'OB_CLUSTER_PARAMETER' to define new cluster parameters
// in ob_parameter_seed.ipp:
///////////////////////////////////////////////////////////////////////////////
#undef OB_CLUSTER_PARAMETER
#define OB_CLUSTER_PARAMETER(args...) args
#include "share/parameter/ob_parameter_seed.ipp"
#undef OB_CLUSTER_PARAMETER

protected:
  ObServerConfig();
  virtual ~ObServerConfig();
  static const int16_t OB_CONFIG_MAGIC = static_cast<int16_t>(0XBCDE);
  static const int16_t OB_CONFIG_VERSION = 1;

private:
  int64_t global_version_;
  DISALLOW_COPY_AND_ASSIGN(ObServerConfig);
};

class ObServerMemoryConfig
{
public:
  friend class unittest::ObSimpleClusterTestBase;
  friend class unittest::ObMultiReplicaTestBase;
  ObServerMemoryConfig();
  static ObServerMemoryConfig &get_instance();
  int reload_config(const ObServerConfig& server_config);
  static int64_t calculate_automatic_memory_budget(const int64_t physical_memory);
  static int64_t resolve_kvcache_memory_limit(const int64_t configured_limit,
                                              const int64_t physical_memory);
  static int64_t resolve_memstore_memory_limit(const int64_t configured_limit,
                                               const int64_t physical_memory);
  static int64_t resolve_vector_memory_limit(const int64_t configured_limit,
                                             const int64_t physical_memory);
  int64_t get_server_memory_budget() const;
  int64_t get_kvcache_memory_limit() const;
  int64_t get_kvcache_memory_capacity() const;
  void publish_kvcache_memory_capacity(const int64_t bytes);
  int64_t get_memstore_memory_limit() const;
  int64_t get_vector_memory_limit() const;
  int64_t get_reserved_server_memory() { return 1LL<<30; }
  void check_limit();
private:
  std::atomic<int64_t> kvcache_memory_limit_;
  std::atomic<int64_t> kvcache_memory_capacity_;
  std::atomic<int64_t> memstore_memory_limit_;
  std::atomic<int64_t> vector_memory_limit_;
  DISALLOW_COPY_AND_ASSIGN(ObServerMemoryConfig);
};
}
}

#define GCONF (::oceanbase::common::ObServerConfig::get_instance())
#define GMEMCONF (::oceanbase::common::ObServerMemoryConfig::get_instance())
#endif // OCEANBASE_SHARE_CONFIG_OB_SERVER_CONFIG_H_
