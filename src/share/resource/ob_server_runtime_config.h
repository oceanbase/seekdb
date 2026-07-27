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

#ifndef OCEANBASE_SHARE_RESOURCE_OB_SERVER_RUNTIME_CONFIG_H_
#define OCEANBASE_SHARE_RESOURCE_OB_SERVER_RUNTIME_CONFIG_H_

#include "lib/utility/ob_unify_serialize.h"
#include "lib/worker.h"
#include "share/resource/ob_server_resource_config.h"

namespace oceanbase
{
namespace share
{

// Complete persisted configuration for the one local server runtime.
struct ObServerRuntimeConfig
{
  OB_UNIS_VERSION(1);

public:
  ObServerRuntimeConfig();
  ObServerRuntimeConfig(const ObServerRuntimeConfig &) = default;
  ObServerRuntimeConfig &operator=(const ObServerRuntimeConfig &) = default;
  ~ObServerRuntimeConfig() {}

  int init(const ObServerResourceConfig &resource_config,
           lib::Worker::CompatMode compat_mode,
           const bool has_memstore);

  void reset();
  bool is_valid() const
  {
    return resource_config_.is_valid()
        && mode_ != lib::Worker::CompatMode::INVALID;
  }
  bool operator==(const ObServerRuntimeConfig &other) const;
  int assign(const ObServerRuntimeConfig &other);

  TO_STRING_KV(K_(resource_config), K_(mode), K_(has_memstore));

  // Keep this order in sync with the persisted representation.
  ObServerResourceConfig resource_config_;
  lib::Worker::CompatMode mode_;
  bool has_memstore_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RESOURCE_OB_SERVER_RUNTIME_CONFIG_H_
