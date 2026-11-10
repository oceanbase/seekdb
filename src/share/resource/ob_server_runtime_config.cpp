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

#include "share/resource/ob_server_runtime_config.h"

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObServerRuntimeConfig,
                    resource_config_);

ObServerRuntimeConfig::ObServerRuntimeConfig()
  : resource_config_()
{
}

int ObServerRuntimeConfig::init(
    const ObServerResourceConfig &resource_config)
{
  return resource_config_.assign(resource_config);
}

void ObServerRuntimeConfig::reset()
{
  resource_config_.reset();
}

bool ObServerRuntimeConfig::operator==(const ObServerRuntimeConfig &other) const
{
  return resource_config_ == other.resource_config_;
}

int ObServerRuntimeConfig::assign(const ObServerRuntimeConfig &other)
{
  return this == &other ? OB_SUCCESS : resource_config_.assign(other.resource_config_);
}

} // namespace share
} // namespace oceanbase
