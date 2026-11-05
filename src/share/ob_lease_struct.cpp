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
#include "share/ob_lease_struct.h"
namespace oceanbase
{
using namespace common;
using namespace observer;
namespace share
{

const char *DataDiskSuggestedOperationType::get_str(const TYPE &type)
{
  const char *str = "UNKNOWN";
  switch (type) {
    case NONE:
      str = "NONE";
      break;
    case EXPAND:
      str = "EXPAND";
      break;
    case SHRINK:
      str = "SHRINK";
      break;
    default:
      str = "UNKNOWN";
      break;
  }
  return str;
}


ObServerResourceInfo::ObServerResourceInfo()
{
  reset();
}

void ObServerResourceInfo::reset()
{
  cpu_ = 0;
  report_cpu_assigned_ = 0;
  report_cpu_max_assigned_ = 0;

  mem_total_ = 0;
  report_mem_assigned_ = 0;
  mem_in_use_ = 0;

  log_disk_total_ = 0;
  report_log_disk_assigned_ = 0;
  log_disk_in_use_ = 0;

  data_disk_total_ = 0;
  report_data_disk_assigned_ = 0;
  data_disk_in_use_ = 0;

  report_data_disk_suggested_operation_ = DataDiskSuggestedOperationType::NONE;
  report_data_disk_suggested_size_ = 0;
}

bool ObServerResourceInfo::is_valid() const
{
  return cpu_ > 0
         && report_cpu_assigned_ >= 0
         && report_cpu_max_assigned_ >= 0
         && mem_total_ > 0
         && report_mem_assigned_ >= 0
         && mem_in_use_ >= 0
         && log_disk_total_ > 0
         && report_log_disk_assigned_ >= 0
         && log_disk_in_use_ >= 0
         && data_disk_total_ > 0
         && report_data_disk_assigned_ >= 0
         && data_disk_in_use_ >= 0;
}


bool ObServerResourceInfo::operator!=(const ObServerResourceInfo &other) const
{
  return std::fabs(cpu_ - other.cpu_) > OB_DOUBLE_EPSINON
      || std::fabs(report_cpu_assigned_ - other.report_cpu_assigned_) > OB_DOUBLE_EPSINON
      || std::fabs(report_cpu_max_assigned_ - other.report_cpu_max_assigned_) > OB_DOUBLE_EPSINON
      || mem_total_ != other.mem_total_
      || report_mem_assigned_ !=  other.report_mem_assigned_
      || mem_in_use_ != other.mem_in_use_
      || log_disk_total_ != other.log_disk_total_
      || report_log_disk_assigned_ != other.report_log_disk_assigned_
      || log_disk_in_use_ != other.log_disk_in_use_
      || data_disk_total_ != other.data_disk_total_
      || report_data_disk_assigned_ != other.report_data_disk_assigned_
      || data_disk_in_use_ != other.data_disk_in_use_;
}


OB_SERIALIZE_MEMBER(ObServerResourceInfo,
                    cpu_,
                    report_cpu_assigned_,
                    report_cpu_max_assigned_,
                    mem_total_,
                    report_mem_assigned_,
                    mem_in_use_,
                    log_disk_total_,
                    report_log_disk_assigned_,
                    data_disk_total_,   // 'disk_total_' in earlier version  // FARM COMPAT WHITELIST
                    data_disk_in_use_,  // 'disk_in_use_' in earlier version // FARM COMPAT WHITELIST
                    report_data_disk_assigned_,
                    log_disk_in_use_,
                    report_data_disk_suggested_operation_,
                    report_data_disk_suggested_size_);

DEF_TO_STRING(ObServerResourceInfo)
{
  int64_t pos = 0;
  J_OBJ_START();
  (void)databuff_printf(buf, buf_len, pos,
      "cpu_capacity:%.6g, "
      "cpu_assigned:%.6g, "
      "cpu_assigned_max:%.6g, "
      "mem_capacity:\"%.9gGB\", "
      "mem_assigned:\"%.9gGB\", "
      "mem_in_use:%.9gGB, "
      "log_disk_capacity:%.9gGB, "
      "log_disk_assigned:%.9gGB, "
      "log_disk_in_use:%.9gGB, "
      "data_disk_capacity:%.9gGB, "
      "data_disk_assigned:%.9gGB, "
      "data_disk_in_use:%.9gGB",
      cpu_,
      report_cpu_assigned_,
      report_cpu_max_assigned_,
      (double)mem_total_/1024/1024/1024,
      (double)report_mem_assigned_/1024/1024/1024,
      (double)mem_in_use_/1024/1024/1024,
      (double)log_disk_total_/1024/1024/1024,
      (double)report_log_disk_assigned_/1024/1024/1024,
      (double)log_disk_in_use_/1024/1024/1024,
      (double)data_disk_total_/1024/1024/1024,
      report_data_disk_assigned_ < 0 ? report_data_disk_assigned_ 
        : (double)report_data_disk_assigned_/1024/1024/1024,
      (double)data_disk_in_use_/1024/1024/1024);
  J_OBJ_END();
  return pos;
}

} // end namespace share
} // end namespace oceanbase
