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

#ifndef OCEANBASE_SHARE_OB_SERVER_SWITCHOVER_STATUS_H_
#define OCEANBASE_SHARE_OB_SERVER_SWITCHOVER_STATUS_H_

#include "lib/string/ob_string.h" // ObString
#include "lib/utility/ob_unify_serialize.h"   // serialize
#include "lib/utility/ob_print_utils.h"             // TO_STRING_KV
#include "lib/oblog/ob_log_module.h"      // LOG*

namespace oceanbase {
namespace share {

class ObServerSwitchoverStatus
{
  OB_UNIS_VERSION(1);
public:
  enum Status
  {
    INVALID_STATUS = 0,
    NORMAL_STATUS = 1,
    SWITCHING_TO_PRIMARY_STATUS = 2,
    PREPARE_SWITCHING_TO_STANDBY_STATUS = 3,
    SWITCHING_TO_STANDBY_STATUS = 4,
    PREPARE_SWITCHING_TO_PRIMARY_STATUS = 5,
    MAX_STATUS = 6
  };
public:
  ObServerSwitchoverStatus() : value_(INVALID_STATUS) {}
  explicit ObServerSwitchoverStatus(const ObServerSwitchoverStatus::Status value) : value_(value) {}
  explicit ObServerSwitchoverStatus(const ObString &str);
  ~ObServerSwitchoverStatus() { reset(); }

public:
  void reset() { value_ = INVALID_STATUS; }
  bool is_valid() const { return INVALID_STATUS != value_; }
  ObServerSwitchoverStatus::Status value() const { return value_; }
  const char* to_str() const;

  // compare operator
  bool operator == (const ObServerSwitchoverStatus &other) const { return value_ == other.value_; }
  bool operator != (const ObServerSwitchoverStatus &other) const { return value_ != other.value_; }

  // assignment
  ObServerSwitchoverStatus &operator=(const ObServerSwitchoverStatus::Status value)
  {
    value_ = value;
    return *this;
  }

  // Switchover-state helpers.
#define IS_SWITCHOVER_STATUS(STATUS_VALUE, STATUS_NAME) \
  bool is_##STATUS_NAME##_status() const { return STATUS_VALUE == value_; };

IS_SWITCHOVER_STATUS(NORMAL_STATUS, normal)
IS_SWITCHOVER_STATUS(SWITCHING_TO_PRIMARY_STATUS, switching_to_primary)
IS_SWITCHOVER_STATUS(PREPARE_SWITCHING_TO_STANDBY_STATUS, prepare_switching_to_standby)
IS_SWITCHOVER_STATUS(SWITCHING_TO_STANDBY_STATUS, switching_to_standby)
IS_SWITCHOVER_STATUS(PREPARE_SWITCHING_TO_PRIMARY_STATUS, prepare_switching_to_primary)
#undef IS_SWITCHOVER_STATUS

  TO_STRING_KV("switchover_status", to_str(), K_(value));
  DECLARE_TO_YSON_KV;
private:
  ObServerSwitchoverStatus::Status value_;
};

static const ObServerSwitchoverStatus INVALID_SWITCHOVER_STATUS(ObServerSwitchoverStatus::INVALID_STATUS);
static const ObServerSwitchoverStatus NORMAL_SWITCHOVER_STATUS(ObServerSwitchoverStatus::NORMAL_STATUS);
static const ObServerSwitchoverStatus SWITCHING_TO_PRIMARY_SWITCHOVER_STATUS(ObServerSwitchoverStatus::SWITCHING_TO_PRIMARY_STATUS);
static const ObServerSwitchoverStatus PREP_SWITCHING_TO_STANDBY_SWITCHOVER_STATUS(ObServerSwitchoverStatus::PREPARE_SWITCHING_TO_STANDBY_STATUS);
static const ObServerSwitchoverStatus SWITCHING_TO_STANDBY_SWITCHOVER_STATUS(ObServerSwitchoverStatus::SWITCHING_TO_STANDBY_STATUS);
static const ObServerSwitchoverStatus PREP_SWITCHING_TO_PRIMARY_SWITCHOVER_STATUS(ObServerSwitchoverStatus::PREPARE_SWITCHING_TO_PRIMARY_STATUS);

}  // share
}  // oceanbase

#endif /* OCEANBASE_SHARE_OB_SERVER_SWITCHOVER_STATUS_H_ */
