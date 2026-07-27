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

#ifndef OCEANBASE_SHARE_RESOURCE_OB_SERVER_RESOURCE_CONFIG_H_
#define OCEANBASE_SHARE_RESOURCE_OB_SERVER_RESOURCE_CONFIG_H_

#include "lib/utility/ob_unify_serialize.h"
#include "share/resource/ob_server_resource.h"

namespace oceanbase
{
namespace share
{

// Validated numeric resources used to configure the local server runtime.
struct ObServerResourceConfig
{
  OB_UNIS_VERSION(1);

public:
  ObServerResourceConfig();
  ObServerResourceConfig(const ObServerResourceConfig &other);
  explicit ObServerResourceConfig(const ObServerResource &resource) : resource_(resource) {}
  ~ObServerResourceConfig() {}

  bool is_valid() const;
  void reset();
  int init(const ObServerResource &resource);
  int assign(const ObServerResourceConfig &other);

  const ObServerResource &resource() const { return resource_; }
  double max_cpu() const { return resource_.max_cpu(); }
  double min_cpu() const { return resource_.min_cpu(); }
  int64_t memory_size() const { return resource_.memory_size(); }
  int64_t log_disk_size() const { return resource_.log_disk_size(); }
  int64_t max_iops() const { return resource_.max_iops(); }
  int64_t min_iops() const { return resource_.min_iops(); }
  int64_t iops_weight() const { return resource_.iops_weight(); }
  int64_t max_net_bandwidth() const { return resource_.max_net_bandwidth(); }
  int64_t net_bandwidth_weight() const { return resource_.net_bandwidth_weight(); }

  int update_resource(const ObServerResource &resource);
  int generate_default(const int64_t log_disk_size);

  ObServerResourceConfig operator+(const ObServerResourceConfig &config) const
  {
    return ObServerResourceConfig(resource_ + config.resource_);
  }
  ObServerResourceConfig operator-(const ObServerResourceConfig &config) const
  {
    return ObServerResourceConfig(resource_ - config.resource_);
  }
  ObServerResourceConfig &operator+=(const ObServerResourceConfig &config)
  {
    resource_ += config.resource_;
    return *this;
  }
  ObServerResourceConfig &operator-=(const ObServerResourceConfig &config)
  {
    resource_ -= config.resource_;
    return *this;
  }
  ObServerResourceConfig operator*(const int64_t count) const
  {
    return ObServerResourceConfig(resource_ * count);
  }
  bool operator==(const ObServerResourceConfig &config) const
  {
    return resource_ == config.resource_;
  }

  TO_STRING_KV(K_(resource));

private:
  // Keep this as the sole serialized field.
  ObServerResource resource_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RESOURCE_OB_SERVER_RESOURCE_CONFIG_H_
