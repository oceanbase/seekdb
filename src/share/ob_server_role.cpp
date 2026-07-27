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

#include "share/ob_server_role.h"
#include "lib/json/ob_yson.h"

using namespace oceanbase;
using namespace oceanbase::common;

namespace oceanbase {
namespace share {

static const char *SERVER_ROLE_STRS[] =
{
  "INVALID",
  "PRIMARY",
  "STANDBY",
  "RESTORE",
};

OB_SERIALIZE_MEMBER(ObServerRole, value_);
DEFINE_TO_YSON_KV(ObServerRole,
                  OB_ID(value), value_);

const char* ObServerRole::to_str() const
{
  STATIC_ASSERT(ARRAYSIZEOF(SERVER_ROLE_STRS) == MAX_ROLE, "array size mismatch");
  const char *type_str = "UNKNOWN";
  if (OB_UNLIKELY(value_ >= ARRAYSIZEOF(SERVER_ROLE_STRS)
                  || value_ < INVALID_ROLE)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "fatal error, unknown server role", K_(value));
  } else {
    type_str = SERVER_ROLE_STRS[value_];
  }
  return type_str;
}

ObServerRole::ObServerRole(const ObString &str)
{
  value_ = INVALID_ROLE;
  if (str.empty()) {
  } else {
    for (int64_t i = 0; i < ARRAYSIZEOF(SERVER_ROLE_STRS); i++) {
      if (0 == str.case_compare(SERVER_ROLE_STRS[i])) {
        value_ = static_cast<ObServerRole::Role>(i);
        break;
      }
    }
  }

  if (INVALID_ROLE == value_) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid server role", K_(value), K(str));
  }
}

#define GEN_IS_SERVER_ROLE(ROLE_VALUE, ROLE_NAME) \
  bool is_##ROLE_NAME##_role(const ObServerRole::Role value) { return ROLE_VALUE == value; }

GEN_IS_SERVER_ROLE(ObServerRole::Role::INVALID_ROLE, invalid)
GEN_IS_SERVER_ROLE(ObServerRole::Role::PRIMARY_ROLE, primary)
GEN_IS_SERVER_ROLE(ObServerRole::Role::STANDBY_ROLE, standby)
GEN_IS_SERVER_ROLE(ObServerRole::Role::RESTORE_ROLE, restore)
#undef GEN_IS_SERVER_ROLE


}  // share
}  // oceanbase
