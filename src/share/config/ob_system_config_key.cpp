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

#include "share/config/ob_system_config_key.h"

namespace oceanbase
{
namespace common
{
const char *ObSystemConfigKey::DEFAULT_VALUE = "ANY";

int ObSystemConfigKey::set_varchar(const ObString &key, const char *strvalue)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(strvalue)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "check varchar value failed", K(ret));
  } else if (key == "zone") {
    if (OB_FAIL(zone_.assign(strvalue))) {
      OB_LOG(WARN, "zone assign failed", K(strvalue), K(ret));
    }
  } else if (key == "svr_type") {
    strncpy(server_type_, strvalue, sizeof(server_type_));
    server_type_[OB_SERVER_TYPE_LENGTH - 1] = '\0';
  } else if (key == "svr_ip") {
    strncpy(server_ip_, strvalue, sizeof(server_ip_));
    server_ip_[OB_MAX_SERVER_ADDR_SIZE - 1] = '\0';
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(ERROR, "unknown sys config column name", "name", key.ptr(), K(ret));
  }
  return ret;
}

void ObSystemConfigKey::set_version(const int64_t version)
{
  version_ = version;
}

int64_t ObSystemConfigKey::get_version() const
{
  return version_;
}

int ObSystemConfigKey::set_int(const ObString &key, int64_t intval)
{
  int ret = OB_SUCCESS;
  if (key == "svr_port") {
    server_port_ = intval;
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(ERROR, "unknown sys config column name", "name", key.ptr(), K(ret));
  }
  return ret;
}


} // end of namespace common
} // end of namespace oceanbase
