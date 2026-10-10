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

#include "share/config/ob_config_rpc_types.h"
#include "lib/lock/ob_drw_lock.h"
#include "lib/utility/ob_macro_utils.h"

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
double get_server_default_min_cpu();
double get_server_default_max_cpu();

#ifdef ERRSIM
struct ErrsimConfig
{
  std::atomic<int64_t> errsim_ddl_major_delay_time{0};
  std::atomic<int64_t> errsim_storage_meta_macro_ids_threshold{0};
  std::atomic<int64_t> errsim_max_ddl_block_count{0};
  std::atomic<int64_t> errsim_test_tablet_id{0};
  std::atomic<int64_t> errsim_migration_tablet_id{0};
  std::atomic<int64_t> macro_block_builder_errsim_flag{0};
};

inline ErrsimConfig &errsim_config()
{
  static ErrsimConfig config;
  return config;
}
#endif

class ObServerMemoryConfig
{
public:
  friend class unittest::ObSimpleClusterTestBase;
  friend class unittest::ObMultiReplicaTestBase;
  ObServerMemoryConfig();
  static ObServerMemoryConfig &get_instance();
  int reload_config();
  static int64_t calculate_automatic_memory_budget(const int64_t system_memory);
  static int64_t resolve_kvcache_memory_limit(const int64_t configured_limit,
                                              const int64_t memory_budget);
  static int64_t resolve_memstore_memory_limit(const int64_t configured_limit,
                                               const int64_t memory_budget);
  static int64_t resolve_vector_memory_limit(const int64_t configured_limit,
                                             const int64_t effective_memory);
  int64_t get_server_memory_budget() const;
  int64_t get_kvcache_memory_limit() const;
  int64_t get_kvcache_memory_capacity() const;
  int64_t get_memstore_memory_limit() const;
  int64_t get_vector_memory_limit() const;
  int64_t get_reserved_server_memory() { return 1LL<<30; }
private:
  std::atomic<int64_t> kvcache_memory_limit_;
  std::atomic<int64_t> kvcache_memory_capacity_;
  std::atomic<int64_t> memstore_memory_limit_;
  std::atomic<int64_t> vector_memory_limit_;
  DISALLOW_COPY_AND_ASSIGN(ObServerMemoryConfig);
};
}
}

#define GMEMCONF (::oceanbase::common::ObServerMemoryConfig::get_instance())
#endif // OCEANBASE_SHARE_CONFIG_OB_SERVER_CONFIG_H_
