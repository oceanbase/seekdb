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

#include "share/resource/ob_server_resource_config.h"

namespace oceanbase
{
using namespace common;
namespace share
{

ObServerResourceConfig::ObServerResourceConfig()
  : resource_()
{
}

ObServerResourceConfig::ObServerResourceConfig(const ObServerResourceConfig &other)
  : resource_(other.resource())
{
}

void ObServerResourceConfig::reset()
{
  resource_.reset();
}

bool ObServerResourceConfig::is_valid() const
{
  return resource_.is_valid();
}

OB_SERIALIZE_MEMBER(ObServerResourceConfig, resource_);

int ObServerResourceConfig::assign(const ObServerResourceConfig &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    resource_ = other.resource_;
  }
  return ret;
}

int ObServerResourceConfig::init(const ObServerResource &resource)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!resource.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server resource", KR(ret), K(resource));
  } else {
    resource_ = resource;
  }
  return ret;
}

int ObServerResourceConfig::update_resource(const ObServerResource &resource)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
    LOG_WARN("resource config is not valid", KR(ret), KPC(this), K(resource));
  } else if (OB_UNLIKELY(!resource.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server resource", KR(ret), K(resource));
  } else {
    resource_ = resource;
  }
  return ret;
}

int ObServerResourceConfig::generate_default(const int64_t log_disk_size)
{
  int ret = OB_SUCCESS;
  ObServerResource resource;
  if (OB_FAIL(resource.generate_default(log_disk_size))) {
    LOG_WARN("failed to generate default server resource", KR(ret), K(resource));
  } else if (OB_FAIL(init(resource))) {
    LOG_WARN("failed to initialize resource config", KR(ret), K(resource));
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
