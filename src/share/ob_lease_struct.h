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
#include "common/ob_role.h"
#include "common/storage/ob_freeze_define.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
// Observer status recognized by LocalManagementService
// RSS_IS_STOPPED after stopping the server,
// RSS_IS_WORKING in other cases
enum RSServerStatus
{
  RSS_INVALID,
  RSS_IS_WORKING,
  RSS_IS_STOPPED,
  RSS_MAX,
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

  int64_t data_disk_total_;             // Total capacity of the local data disk
  int64_t data_disk_in_use_;            // Local data-disk usage

  ObServerResourceInfo();
  void reset();
  bool is_valid() const;
  bool operator!=(const ObServerResourceInfo &other) const;

  DECLARE_TO_STRING;
};

} // end namespace share
} // end namespace oceanbase
#endif
