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

#include "share/ob_server_switchover_status.h"
#include "lib/json/ob_yson.h"

using namespace oceanbase;
using namespace oceanbase::common;

namespace oceanbase {
namespace share {

static const char *SERVER_SWITCHOVER_STATUS_STRS[] =
{
  "INVALID",
  "NORMAL",
  "PREPARING",
};

OB_SERIALIZE_MEMBER(ObServerSwitchoverStatus, value_);
DEFINE_TO_YSON_KV(ObServerSwitchoverStatus,
                  OB_ID(value), value_);

const char* ObServerSwitchoverStatus::to_str() const
{
  STATIC_ASSERT(ARRAYSIZEOF(SERVER_SWITCHOVER_STATUS_STRS) == MAX_STATUS, "array size mismatch");
  const char *type_str = "UNKNOWN";
  if (OB_UNLIKELY(value_ >= ARRAYSIZEOF(SERVER_SWITCHOVER_STATUS_STRS)
                  || value_ < INVALID_STATUS)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "fatal error, unknown switchover status", K_(value));
  } else {
    type_str = SERVER_SWITCHOVER_STATUS_STRS[value_];
  }
  return type_str;
}

ObServerSwitchoverStatus::ObServerSwitchoverStatus(const ObString &str)
{
  value_ = INVALID_STATUS;
  if (str.empty()) {
  } else {
    for (int64_t i = 0; i < ARRAYSIZEOF(SERVER_SWITCHOVER_STATUS_STRS); i++) {
      if (0 == str.case_compare(SERVER_SWITCHOVER_STATUS_STRS[i])) {
        value_ = static_cast<ObServerSwitchoverStatus::Status>(i);
        break;
      }
    }
  }

  if (INVALID_STATUS == value_) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid switchover status", K_(value), K(str));
  }
}

}  // share
}  // oceanbase
