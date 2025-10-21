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

#ifndef OCEANBASE_SHARE_HEARTBEAT_OB_LEASE_STRUCT_H_
#define OCEANBASE_SHARE_HEARTBEAT_OB_LEASE_STRUCT_H_

#include "share/ob_define.h"
#include "lib/string/ob_fixed_length_string.h"
#include "lib/net/ob_addr.h"
#include "common/ob_zone.h"
#include "common/ob_role.h"
#include "common/storage/ob_freeze_define.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
// Observer status recognized by RootService
// RSS_IS_STOPPED after stop server/stop zone,
// RSS_IS_WORKING in other cases
enum RSServerStatus
{
  RSS_INVALID,
  RSS_IS_WORKING,
  RSS_IS_STOPPED,
  RSS_MAX,
};

// OBS judges whether it can provide log services based on this value;
enum ServerServiceStatus
{
  OBSERVER_INVALID_STATUS = 0,
  OBSERVER_ACTIVE = 1,
  OBSERVER_SWITCHING = 2,
  OBSERVER_FLASHBACK_USER = 3,
  OBSERVER_DISABLED = 4,
  //Mainly to distinguish lossless failover and lossy failover.
  //Lossless failover will take over in the cleanup phase, lossy failover must take over in the flashback phase
  //Taking office during the flashback phase is lossy.
  OBSERVER_CLEANUP = 5,
  OBSERVER_FLASHBACK_INNER = 6,
};

enum ServerPreProceStatus
{
  SPPS_INVALID_STATUS = 0,
  SPPS_SERVER_NOT_EXIST,
  SPPS_PRE_PROCE_NOT_START,
  SPPS_IN_PRE_PROCE,
  SPPS_PRE_PROCE_FINISHED,
};


// Is the state value represented by a bit field
enum LeaseRequestServerStatus
{
  LEASE_REQUEST_NORMAL = 0,
  LEASE_REQUEST_DATA_DISK_ERROR = 0x1,
};

struct DataDiskSuggestedOperationType
{
  enum TYPE : uint8_t
  {
    NONE = 0,
    EXPAND = 1,
    SHRINK = 2,
  };
  static const char *get_str(const TYPE &type);
  static OB_INLINE bool is_valid(const TYPE &type)
  {
    return type >= 0 && type <= TYPE::SHRINK;
  }
};

struct ObServerResourceInfo
{
  OB_UNIS_VERSION(1);
public:
  double cpu_;                          // Total CPU capacity
  double report_cpu_assigned_;          // CPU assigned size: total min_cpu of all units on the server
  double report_cpu_max_assigned_;      // Maximum CPU size assigned: total max_cpu of all units on the server

  int64_t mem_total_;                   // total memory capacity
  int64_t report_mem_assigned_;         // Memory assigned size: total sum of memory_size for all units in server
  int64_t mem_in_use_;                  // size of memory in use

  int64_t log_disk_total_;              // total capacity of log disk
  int64_t report_log_disk_assigned_;    // Log disk assigned size: total sum of server all unit log_disk_size
  int64_t log_disk_in_use_;             // Log disk size in use

  int64_t data_disk_total_;             // total capacity of data disk (old version disk_total_) // FARM COMPAT WHITELIST: Type not match
  int64_t report_data_disk_assigned_;   // Data disk assigned size: total sum of server all unit data_disk_size.
                                        //   Only valid in shared-storage mode. 0 in shared-nothing mode.
  int64_t data_disk_in_use_;            // Data disk usage size (old version disk_in_use_) // FARM COMPAT WHITELIST: Type not match

  DataDiskSuggestedOperationType::TYPE report_data_disk_suggested_operation_;  // Data disk suggested operation: none (no operation), expand (expand), shrink (shrink). Only valid in SS mode. In SN mode, it is none
  int64_t report_data_disk_suggested_size_;                                    // Suggested size of data disk: only valid in SS mode. 0 in SN mode.

  ObServerResourceInfo();
  void reset();
  bool is_valid() const;
  bool operator!=(const ObServerResourceInfo &other) const;

  DECLARE_TO_STRING;
};

struct ObLeaseRequest
{
  OB_UNIS_VERSION(1);
public:
  static const int64_t LEASE_VERSION = 1;
  // Server lease length, 50s
  static const int64_t SERVICE_LEASE = 40 * 1000000;
  // rs waits for 30s after the lease timeout, and then tries to take over the observer,
  // This 30s is to wait for the end of the writing of the observer who lost his heartbeat
  static const int64_t RS_TAKENOVER_OBS_INTERVAL = 30 * 1000000;
  int64_t version_;
  common::ObZone zone_;
  common::ObAddr server_;
  int64_t sql_port_;  // mysql listen port
  char build_version_[common::OB_SERVER_VERSION_LENGTH];
  ObServerResourceInfo resource_info_;
  int64_t start_service_time_;
  int64_t current_server_time_;
  int64_t round_trip_time_;
  int64_t server_status_;
  int64_t ssl_key_expired_time_;
  common::ObSEArray<std::pair<uint64_t, int64_t>, 10> tenant_config_version_;
  //The order cannot be changed, this is a new field added by 22x
  int64_t timeout_partition_;
  int64_t request_lease_time_;
  /* 1 Regular deployment cluster summary, request_lease_time_ is not used, always 0
   * 2 In the scenario of single-zone OFS deployment, request_lease_time_ is not 0, indicating a request for lease,
   *   request_lease_time_ is 0, indicating reporting other observer information.
   */

  ObLeaseRequest();
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(version), K_(zone), K_(server), K_(sql_port),
      K_(build_version), K_(resource_info), K_(start_service_time),
      K_(current_server_time), K_(round_trip_time), K_(tenant_config_version),
      K_(ssl_key_expired_time), K_(timeout_partition),
      K_(request_lease_time));
};

struct ObLeaseResponse
{
  OB_UNIS_VERSION(1);
public:
  static const int64_t LEASE_VERSION = 1;
  int64_t version_;
  int64_t lease_expire_time_;
  int64_t lease_info_version_;  // check whether need to update info from __all_zone_stat
  int64_t frozen_version_;
  int64_t schema_version_;
  uint64_t server_id_;
  storage::ObFrozenStatus frozen_status_;
  bool force_frozen_status_;
  RSServerStatus rs_server_status_;
  share::schema::ObRefreshSchemaInfo refresh_schema_info_;
  ServerServiceStatus server_service_status_;
  common::ObSEArray<std::pair<uint64_t, int64_t>, 10> tenant_config_version_;
  int64_t baseline_schema_version_;
  int64_t heartbeat_expire_time_;

  ObLeaseResponse();
  int set(const ObLeaseResponse &that);
  TO_STRING_KV(K_(version), K_(lease_expire_time), K_(lease_info_version),
      K_(frozen_version), K_(schema_version), K_(server_id), K_(frozen_status),
      K_(force_frozen_status), K_(rs_server_status),
      K_(refresh_schema_info),
      K_(server_service_status), K_(tenant_config_version),
      K_(baseline_schema_version),
      K_(heartbeat_expire_time));

  void reset();
  bool is_valid() const;

};

struct ObInZoneHbRequest
{
  OB_UNIS_VERSION(1);
public:
  ObInZoneHbRequest() : server_() {}
public:
  TO_STRING_KV(K_(server));
public:
  common::ObAddr server_;
};

struct ObInZoneHbResponse
{
  OB_UNIS_VERSION(1);
public:
  ObInZoneHbResponse() : in_zone_hb_expire_time_(-1) {}
public:
  TO_STRING_KV(K_(in_zone_hb_expire_time));
public:
  int64_t in_zone_hb_expire_time_;
};


struct ObZoneLeaseInfo
{
  common::ObZone zone_;
  int64_t privilege_version_;
  int64_t config_version_;
  int64_t lease_info_version_;
  int64_t time_zone_info_version_;
  int64_t sys_var_version_; // system variable version

  ObZoneLeaseInfo(): zone_(), privilege_version_(0), config_version_(0),
    lease_info_version_(0), time_zone_info_version_(0), sys_var_version_(0)
  {
  }
  TO_STRING_KV(K_(zone), K_(privilege_version), K_(config_version), K_(lease_info_version),
               K_(time_zone_info_version), K_(sys_var_version));

};

} // end namespace share
} // end namespace oceanbase
#endif
