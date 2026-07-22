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
                    resource_config_,
                    mode_,
                    has_memstore_);

ObServerRuntimeConfig::ObServerRuntimeConfig()
  : resource_config_(),
    mode_(lib::Worker::CompatMode::INVALID),
    has_memstore_(true)
{
}

int ObServerRuntimeConfig::init(
    const ObServerResourceConfig &resource_config,
    lib::Worker::CompatMode compat_mode,
    const bool has_memstore)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resource_config_.assign(resource_config))) {
    LOG_WARN("failed to assign resource config", KR(ret), K(resource_config));
  } else {
    mode_ = compat_mode;
    has_memstore_ = has_memstore;
  }
  return ret;
}

void ObServerRuntimeConfig::reset()
{
  resource_config_.reset();
  mode_ = lib::Worker::CompatMode::INVALID;
  has_memstore_ = true;
}

bool ObServerRuntimeConfig::operator==(const ObServerRuntimeConfig &other) const
{
  return resource_config_ == other.resource_config_
      && mode_ == other.mode_
      && has_memstore_ == other.has_memstore_;
}

int ObServerRuntimeConfig::assign(const ObServerRuntimeConfig &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    if (OB_FAIL(resource_config_.assign(other.resource_config_))) {
      LOG_WARN("failed to assign resource config", KR(ret), K(other));
    } else {
      mode_ = other.mode_;
      has_memstore_ = other.has_memstore_;
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
