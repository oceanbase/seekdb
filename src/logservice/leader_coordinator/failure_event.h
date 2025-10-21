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

#ifndef LOGSERVICE_COORDINATOR_FAILURE_EVENT_H
#define LOGSERVICE_COORDINATOR_FAILURE_EVENT_H

#include "lib/list/ob_dlist.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_table_access_helper.h"
#include <cstdint>
namespace oceanbase
{
namespace logservice
{
namespace coordinator
{

enum class FailureType
{
  INVALID_FAILURE = 0,
  FATAL_ERROR_OCCUR = 1,// Fatal error, usually a bug is found in defensive code
  RESOURCE_NOT_ENOUGH = 2,// Resource insufficient, such as disk and memory, usually caused by environmental factors
  PROCESS_HANG = 3,// Process blocking, a major process is found to never end or keep retrying without success
  MAJORITY_FAILURE = 4,// Majority failure, such as a replica's network disconnecting from the majority
  SCHEMA_NOT_REFRESHED = 5, // sql may failed when tenant schema not refreshed yet
  ENTER_ELECTION_SILENT = 6, // Replica enters election silent state
};

enum class FailureModule
{
  UNKNOWN_MODULE = 0,
  TENANT = 1,
  LOG = 2,
  TRANSACTION = 3,
  STORAGE = 4,
  SCHEMA = 5,
};

enum class FailureLevel
{
  UNKNOWN_LEVEL = 0,
  FATAL = 1,// This level of exception is unexpected and is reported from defensive code indicating a critical error, with a higher leader-follower switch priority than SERIOUS_FAILURE
  SERIOUS = 2,// This level of exception is expected, often caused by environmental factors, and the occurrence of such an exception will trigger a leader switch
  NOTICE = 3,// This level of exception is only used for display, shown in the internal table to the user, but will not affect the election priority, nor will it trigger a leader switch
};

inline const char *obj_to_cstring(FailureType type)
{
  const char *ret = "INVALID";
  switch (type) {
    case FailureType::FATAL_ERROR_OCCUR:
      ret = "FATAL ERROR OCCUR";
      break;
    case FailureType::RESOURCE_NOT_ENOUGH:
      ret = "RESOURCE NOT ENOUGH";
      break;
    case FailureType::PROCESS_HANG:
      ret = "PROCESS HANG";
      break;
    case FailureType::MAJORITY_FAILURE:
      ret = "MAJORITY FAILURE";
      break;
    case FailureType::SCHEMA_NOT_REFRESHED:
      ret = "SCHEMA NOT REFRESHED";
      break;
    case FailureType::ENTER_ELECTION_SILENT:
      ret = "ENTER ELECTION SILENT";
      break;
    default:
      break;
  }
  return ret;
}

inline const char *obj_to_cstring(FailureModule module)
{
  const char *ret = "UNKNOWN";
  switch (module) {
    case FailureModule::LOG:
      ret = "LOG";
      break;
    case FailureModule::TENANT:
      ret = "TENANT";
      break;
    case FailureModule::TRANSACTION:
      ret = "TRANSACTION";
      break;
    case FailureModule::STORAGE:
      ret = "STORAGE";
      break;
    case FailureModule::SCHEMA:
      ret = "SCHEMA";
      break;
    default:
      break;
  }
  return ret;
}

inline const char *obj_to_cstring(FailureLevel level)
{
  const char *ret = "UNKNOWN";
  switch (level) {
    case FailureLevel::FATAL:
      ret = "FATAL";
      break;
    case FailureLevel::SERIOUS:
      ret = "SERIOUS";
      break;
    case FailureLevel::NOTICE:
      ret = "NOTICE";
      break;
    default:
      break;
  }
  return ret;
}


class FailureEvent
{
  OB_UNIS_VERSION(1);
public:
  FailureEvent() :
  type_(FailureType::INVALID_FAILURE),
  module_(FailureModule::UNKNOWN_MODULE),
  level_(FailureLevel::UNKNOWN_LEVEL) {}
  FailureEvent(FailureType type, FailureModule module, FailureLevel level = FailureLevel::SERIOUS) :
  type_(type),
  module_(module),
  level_(level) {}
  FailureLevel get_failure_level() const { return level_; }
  FailureModule get_failure_module() const { return module_; }
  int set_info(const ObString &info) {
    return info_.assign(info);
  }
  int assign(const FailureEvent &rhs) {
    type_ = rhs.type_;
    module_ = rhs.module_;
    level_ = rhs.level_;
    return info_.assign(rhs.info_);
  }
  bool operator==(const FailureEvent &rhs) {
    bool ret = false;
    if (type_ == rhs.type_ && module_ == rhs.module_ && level_ == rhs.level_) {
      ret = (0 == info_.get_ob_string().case_compare(rhs.info_.get_ob_string()));
    }
    return ret;
  }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    common::databuff_printf(buf, buf_len, pos, "{type:%s, ", obj_to_cstring(type_));
    common::databuff_printf(buf, buf_len, pos, "module:%s, ", obj_to_cstring(module_));
    common::databuff_printf(buf, buf_len, pos, "info:");
    common::databuff_printf(buf, buf_len, pos, info_);
    common::databuff_printf(buf, buf_len, pos, ", ");
    common::databuff_printf(buf, buf_len, pos, "level:%s}", obj_to_cstring(level_));
    return pos;
  }
public:
  FailureType type_;
  FailureModule module_;
  FailureLevel level_;
  common::ObStringHolder info_;
};
OB_SERIALIZE_MEMBER_TEMP(inline, FailureEvent, type_, module_, level_, info_);

}
}
}

#endif
