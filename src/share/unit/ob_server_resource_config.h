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

#ifndef OCEANBASE_SHARE_UNIT_OB_SERVER_RESOURCE_CONFIG_H_
#define OCEANBASE_SHARE_UNIT_OB_SERVER_RESOURCE_CONFIG_H_

#include <climits>
#include <cmath>
#include <cstdint>
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace share
{

struct ObServerResourceConfig
{
  OB_UNIS_VERSION(1);

public:
  static constexpr int64_t MIN_MEMORY_SIZE = 1LL << 30;
  static constexpr int64_t MIN_LOG_DISK_SIZE = 2LL << 30;
  static constexpr int64_t MIN_IOPS = 1024;
  static constexpr int64_t MIN_NET_BANDWIDTH = 1LL << 20;
  static constexpr double MIN_CPU = 1.0;
  static constexpr double CPU_EPSILON = 0.00001;

  ObServerResourceConfig();
  ObServerResourceConfig(const ObServerResourceConfig &) = default;
  ObServerResourceConfig &operator=(const ObServerResourceConfig &) = default;
  ~ObServerResourceConfig() = default;

  int init(const double max_cpu,
           const double min_cpu,
           const int64_t memory_size,
           const int64_t log_disk_size,
           const int64_t max_iops,
           const int64_t min_iops,
           const int64_t iops_weight,
           const int64_t max_net_bandwidth,
           const int64_t net_bandwidth_weight);
  int init_default(const int64_t log_disk_size);
  int update_cpu_and_log_disk(const double max_cpu,
                              const double min_cpu,
                              const int64_t log_disk_size);
  void reset();
  int assign(const ObServerResourceConfig &other);
  bool is_valid() const;
  bool operator==(const ObServerResourceConfig &other) const;

  double max_cpu() const { return max_cpu_; }
  double min_cpu() const { return min_cpu_; }
  int64_t memory_size() const { return memory_size_; }
  int64_t log_disk_size() const { return log_disk_size_; }
  int64_t max_iops() const { return max_iops_; }
  int64_t min_iops() const { return min_iops_; }
  int64_t iops_weight() const { return iops_weight_; }
  int64_t max_net_bandwidth() const { return max_net_bandwidth_; }
  int64_t net_bandwidth_weight() const { return net_bandwidth_weight_; }

  TO_STRING_KV(K_(max_cpu), K_(min_cpu), K_(memory_size), K_(log_disk_size),
               K_(max_iops), K_(min_iops), K_(iops_weight),
               K_(max_net_bandwidth), K_(net_bandwidth_weight));

private:
  double max_cpu_;
  double min_cpu_;
  int64_t memory_size_;
  int64_t log_disk_size_;
  int64_t max_iops_;
  int64_t min_iops_;
  int64_t iops_weight_;
  int64_t max_net_bandwidth_;
  int64_t net_bandwidth_weight_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_UNIT_OB_SERVER_RESOURCE_CONFIG_H_
