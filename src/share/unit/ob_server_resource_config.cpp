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

#include "share/unit/ob_server_resource_config.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObServerResourceConfig,
                    max_cpu_,
                    min_cpu_,
                    memory_size_,
                    log_disk_size_,
                    max_iops_,
                    min_iops_,
                    iops_weight_,
                    max_net_bandwidth_,
                    net_bandwidth_weight_);

ObServerResourceConfig::ObServerResourceConfig()
{
  reset();
}

int ObServerResourceConfig::init(
    const double max_cpu,
    const double min_cpu,
    const int64_t memory_size,
    const int64_t log_disk_size,
    const int64_t max_iops,
    const int64_t min_iops,
    const int64_t iops_weight,
    const int64_t max_net_bandwidth,
    const int64_t net_bandwidth_weight)
{
  int ret = OB_SUCCESS;
  max_cpu_ = max_cpu;
  min_cpu_ = min_cpu;
  memory_size_ = memory_size;
  log_disk_size_ = log_disk_size;
  max_iops_ = max_iops;
  min_iops_ = min_iops;
  iops_weight_ = iops_weight;
  max_net_bandwidth_ = max_net_bandwidth;
  net_bandwidth_weight_ = net_bandwidth_weight;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server resource config", KR(ret), KPC(this));
    reset();
  }
  return ret;
}

int ObServerResourceConfig::init_default(const int64_t log_disk_size)
{
  const double min_cpu = GCONF.get_database_default_min_cpu();
  return init(GCONF.get_database_default_max_cpu(),
              min_cpu,
              GMEMCONF.get_server_memory_budget(),
              log_disk_size,
              INT64_MAX,
              INT64_MAX,
              static_cast<int64_t>(min_cpu),
              INT64_MAX,
              static_cast<int64_t>(min_cpu));
}

int ObServerResourceConfig::update_cpu_and_log_disk(
    const double max_cpu,
    const double min_cpu,
    const int64_t log_disk_size)
{
  ObServerResourceConfig new_config;
  int ret = new_config.init(max_cpu,
                            min_cpu,
                            memory_size_,
                            log_disk_size,
                            max_iops_,
                            min_iops_,
                            iops_weight_,
                            max_net_bandwidth_,
                            net_bandwidth_weight_);
  if (OB_FAIL(ret)) {
    LOG_WARN("fail to update server cpu and log disk resource", KR(ret),
             K(max_cpu), K(min_cpu), K(log_disk_size), KPC(this));
  } else {
    *this = new_config;
  }
  return ret;
}

void ObServerResourceConfig::reset()
{
  max_cpu_ = 0;
  min_cpu_ = 0;
  memory_size_ = 0;
  log_disk_size_ = -1;
  max_iops_ = 0;
  min_iops_ = 0;
  iops_weight_ = -1;
  max_net_bandwidth_ = 0;
  net_bandwidth_weight_ = -1;
}

int ObServerResourceConfig::assign(const ObServerResourceConfig &other)
{
  *this = other;
  return OB_SUCCESS;
}

bool ObServerResourceConfig::is_valid() const
{
  return max_cpu_ >= MIN_CPU
      && min_cpu_ >= MIN_CPU
      && min_cpu_ <= max_cpu_
      && memory_size_ >= MIN_MEMORY_SIZE
      && (0 == log_disk_size_ || log_disk_size_ >= MIN_LOG_DISK_SIZE)
      && min_iops_ >= MIN_IOPS
      && max_iops_ >= min_iops_
      && iops_weight_ >= 0
      && max_net_bandwidth_ >= MIN_NET_BANDWIDTH
      && net_bandwidth_weight_ >= 0;
}

bool ObServerResourceConfig::operator==(const ObServerResourceConfig &other) const
{
  return std::fabs(max_cpu_ - other.max_cpu_) < CPU_EPSILON
      && std::fabs(min_cpu_ - other.min_cpu_) < CPU_EPSILON
      && memory_size_ == other.memory_size_
      && log_disk_size_ == other.log_disk_size_
      && max_iops_ == other.max_iops_
      && min_iops_ == other.min_iops_
      && iops_weight_ == other.iops_weight_
      && max_net_bandwidth_ == other.max_net_bandwidth_
      && net_bandwidth_weight_ == other.net_bandwidth_weight_;
}

} // namespace share
} // namespace oceanbase
