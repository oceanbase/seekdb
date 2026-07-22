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

#ifndef OCEANBASE_SHARE_RESOURCE_OB_SERVER_RESOURCE_H_
#define OCEANBASE_SHARE_RESOURCE_OB_SERVER_RESOURCE_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/utility.h"

namespace oceanbase
{
namespace share
{

#define __SERVER_RESOURCE_TO_STR(x) #x
#define _SERVER_RESOURCE_TO_STR(x) __SERVER_RESOURCE_TO_STR(x)

#define _SERVER_MIN_CPU 1
#define SERVER_MIN_CPU_STR _SERVER_RESOURCE_TO_STR(_SERVER_MIN_CPU)

// Numeric CPU, memory, log-disk, IOPS, and network limits for the local server.
class ObServerResource
{
  OB_UNIS_VERSION(1);

public:
  static const int64_t MB = (1LL << 20);
  static const int64_t GB = (1LL << 30);

  static constexpr double SERVER_MIN_CPU = _SERVER_MIN_CPU;
  static constexpr double CPU_EPSILON = 0.00001;

  static const int64_t SERVER_MIN_MEMORY = 1LL * GB;

  static const int64_t SERVER_MIN_LOG_DISK_SIZE = 2LL * GB;
  static const int64_t MEMORY_TO_LOG_DISK_FACTOR = 3;
  static const int64_t INVALID_LOG_DISK_SIZE = -1;

  static const int64_t SERVER_MIN_IOPS = 1024;
  static const int64_t INVALID_IOPS_WEIGHT = -1;
  static const int64_t DEFAULT_IOPS_WEIGHT = 0;

  static const int64_t SERVER_MIN_NET_BANDWIDTH = 1LL * MB;
  static const int64_t INVALID_NET_BANDWIDTH = 0;
  static const int64_t DEFAULT_NET_BANDWIDTH = INT64_MAX;
  static const int64_t INVALID_NET_BANDWIDTH_WEIGHT = -1;
  static const int64_t DEFAULT_NET_BANDWIDTH_WEIGHT = 0;

public:
  ObServerResource() { reset(); }
  ObServerResource(const ObServerResource &resource) { *this = resource; }
  ObServerResource(
      const double max_cpu,
      const double min_cpu,
      const int64_t memory_size,
      const int64_t log_disk_size,
      const int64_t max_iops,
      const int64_t min_iops,
      const int64_t iops_weight,
      const int64_t max_net_bandwidth,
      const int64_t net_bandwidth_weight);
  virtual ~ObServerResource() {}

  void reset();
  void reset_all_invalid();

  // Fill unspecified values in requested and validate the resulting server limits.
  int init_and_check_valid(const ObServerResource &requested);

  // Apply specified values from requested. On failure this object is unchanged.
  int update_and_check_valid(const ObServerResource &requested);

  bool is_valid() const;
  bool is_valid_for_server() const;

  bool operator==(const ObServerResource &config) const;
  ObServerResource &operator=(const ObServerResource &resource);
  ObServerResource operator+(const ObServerResource &config) const;
  ObServerResource operator-(const ObServerResource &config) const;
  ObServerResource &operator+=(const ObServerResource &config);
  ObServerResource &operator-=(const ObServerResource &config);
  ObServerResource operator*(const int64_t count) const;

  double max_cpu() const { return max_cpu_; }
  bool is_max_cpu_valid() const { return max_cpu_ > 0; }
  bool is_max_cpu_valid_for_server() const { return max_cpu_ >= SERVER_MIN_CPU; }

  double min_cpu() const { return min_cpu_; }
  bool is_min_cpu_valid() const { return min_cpu_ > 0; }
  bool is_min_cpu_valid_for_server() const { return min_cpu_ >= SERVER_MIN_CPU; }

  int64_t memory_size() const { return memory_size_; }
  bool is_memory_size_valid() const { return memory_size_ > 0; }
  bool is_memory_size_valid_for_server() const { return memory_size_ >= SERVER_MIN_MEMORY; }

  int64_t log_disk_size() const { return log_disk_size_; }
  bool is_log_disk_size_valid() const { return log_disk_size_ >= 0; }
  bool is_log_disk_size_valid_for_server() const;

  int64_t max_iops() const { return max_iops_; }
  bool is_max_iops_valid() const { return max_iops_ > 0; }
  bool is_max_iops_valid_for_server() const { return max_iops_ >= SERVER_MIN_IOPS; }

  int64_t min_iops() const { return min_iops_; }
  bool is_min_iops_valid() const { return min_iops_ > 0; }
  bool is_min_iops_valid_for_server() const { return min_iops_ >= SERVER_MIN_IOPS; }

  int64_t iops_weight() const { return iops_weight_; }
  bool is_iops_weight_valid() const { return iops_weight_ >= 0; }
  bool is_iops_weight_valid_for_server() const { return is_iops_weight_valid(); }

  int64_t max_net_bandwidth() const { return max_net_bandwidth_; }
  bool is_max_net_bandwidth_valid() const { return max_net_bandwidth_ > 0; }
  bool is_max_net_bandwidth_valid_for_server() const
  {
    return max_net_bandwidth_ >= SERVER_MIN_NET_BANDWIDTH;
  }

  int64_t net_bandwidth_weight() const { return net_bandwidth_weight_; }
  bool is_net_bandwidth_weight_valid() const { return net_bandwidth_weight_ >= 0; }
  bool is_net_bandwidth_weight_valid_for_server() const
  {
    return is_net_bandwidth_weight_valid();
  }

  bool has_expanded_resource_than(const ObServerResource &other) const;
  bool has_shrunk_resource_than(const ObServerResource &other) const;

  // Materialize the server defaults from process configuration and log storage.
  int generate_default(const int64_t log_disk_size);

  static int64_t get_default_log_disk_size(const int64_t memory_size);
  static int64_t get_default_iops() { return INT64_MAX; }
  static int64_t get_default_iops_weight(const double cpu)
  {
    return static_cast<int64_t>(cpu);
  }
  static int64_t get_default_net_bandwidth() { return DEFAULT_NET_BANDWIDTH; }
  static int64_t get_default_net_bandwidth_weight(const double cpu)
  {
    return static_cast<int64_t>(cpu);
  }

  DECLARE_TO_STRING;

private:
  int init_and_check_cpu_(const ObServerResource &requested);
  int init_and_check_mem_(const ObServerResource &requested);
  int init_and_check_log_disk_(const ObServerResource &requested);
  int init_and_check_iops_(const ObServerResource &requested);
  int init_and_check_net_bandwidth_(const ObServerResource &requested);
  int update_and_check_cpu_(const ObServerResource &requested);
  int update_and_check_mem_(const ObServerResource &requested);
  int update_and_check_log_disk_(const ObServerResource &requested);
  int update_and_check_iops_(const ObServerResource &requested);
  int update_and_check_net_bandwidth_(const ObServerResource &requested);

protected:
  // Keep this order in sync with the serialized representation.
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

#endif // OCEANBASE_SHARE_RESOURCE_OB_SERVER_RESOURCE_H_
