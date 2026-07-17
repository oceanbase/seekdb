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

#ifndef OCEANBASE_SHARE_OB_TENANT_SWITCHOVER_STATUS_H_
#define OCEANBASE_SHARE_OB_TENANT_SWITCHOVER_STATUS_H_

#include "lib/string/ob_string.h" // ObString
#include "lib/utility/ob_unify_serialize.h"   // serialize
#include "lib/utility/ob_print_utils.h"             // TO_STRING_KV
#include "lib/oblog/ob_log_module.h"      // LOG*

namespace oceanbase {
namespace share {

class ObTenantSwitchoverStatus
{
  OB_UNIS_VERSION(1);
public:
  // Tenant Switchover Status
  enum Status
  {
    INVALID_STATUS = 0,
    NORMAL_STATUS = 1,
    SWITCHING_TO_PRIMARY_STATUS = 2,
    PREPARE_FLASHBACK_FOR_FAILOVER_TO_PRIMARY_STATUS = 3,
    FLASHBACK_STATUS = 4,
    PREPARE_SWITCHING_TO_STANDBY_STATUS = 5,
    SWITCHING_TO_STANDBY_STATUS = 6,
    PREPARE_FLASHBACK_FOR_SWITCH_TO_PRIMARY_STATUS = 7,
    FLASHBACK_AND_STAY_STANDBY_STATUS = 8,
    MAX_STATUS = 9
  };
public:
  ObTenantSwitchoverStatus() : value_(INVALID_STATUS) {}
  explicit ObTenantSwitchoverStatus(const ObTenantSwitchoverStatus::Status value) : value_(value) {}
  explicit ObTenantSwitchoverStatus(const ObString &str);
  ~ObTenantSwitchoverStatus() { reset(); }

public:
  void reset() { value_ = INVALID_STATUS; }
  bool is_valid() const { return INVALID_STATUS != value_; }
  ObTenantSwitchoverStatus::Status value() const { return value_; }
  const char* to_str() const;

  // compare operator
  bool operator == (const ObTenantSwitchoverStatus &other) const { return value_ == other.value_; }
  bool operator != (const ObTenantSwitchoverStatus &other) const { return value_ != other.value_; }

  // assignment
  ObTenantSwitchoverStatus &operator=(const ObTenantSwitchoverStatus::Status value)
  {
    value_ = value;
    return *this;
  }

  // Tenant Switchover attribute interface
#define IS_TENANT_STATUS(TENANT_STATUS, STATUS) \
  bool is_##STATUS##_status() const { return TENANT_STATUS == value_; };

IS_TENANT_STATUS(NORMAL_STATUS, normal) 
IS_TENANT_STATUS(SWITCHING_TO_PRIMARY_STATUS, switching_to_primary) 
IS_TENANT_STATUS(PREPARE_FLASHBACK_FOR_FAILOVER_TO_PRIMARY_STATUS, prepare_flashback_for_failover_to_primary) 
IS_TENANT_STATUS(FLASHBACK_STATUS, flashback) 
IS_TENANT_STATUS(PREPARE_SWITCHING_TO_STANDBY_STATUS, prepare_switching_to_standby) 
IS_TENANT_STATUS(SWITCHING_TO_STANDBY_STATUS, switching_to_standby) 
IS_TENANT_STATUS(PREPARE_FLASHBACK_FOR_SWITCH_TO_PRIMARY_STATUS, prepare_flashback_for_switch_to_primary) 
#undef IS_TENANT_STATUS 

  TO_STRING_KV("switchover_status", to_str(), K_(value));
  DECLARE_TO_YSON_KV;
private:
  ObTenantSwitchoverStatus::Status value_;
};

static const ObTenantSwitchoverStatus INVALID_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::INVALID_STATUS);
static const ObTenantSwitchoverStatus NORMAL_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::NORMAL_STATUS);
static const ObTenantSwitchoverStatus PREPARE_FLASHBACK_FOR_FAILOVER_TO_PRIMARY_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::PREPARE_FLASHBACK_FOR_FAILOVER_TO_PRIMARY_STATUS);
static const ObTenantSwitchoverStatus FLASHBACK_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::FLASHBACK_STATUS);
static const ObTenantSwitchoverStatus SWITCHING_TO_PRIMARY_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::SWITCHING_TO_PRIMARY_STATUS);
static const ObTenantSwitchoverStatus PREP_SWITCHING_TO_STANDBY_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::PREPARE_SWITCHING_TO_STANDBY_STATUS);
static const ObTenantSwitchoverStatus SWITCHING_TO_STANDBY_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::SWITCHING_TO_STANDBY_STATUS);
static const ObTenantSwitchoverStatus PREPARE_FLASHBACK_FOR_SWITCH_TO_PRIMARY_SWITCHOVER_STATUS(ObTenantSwitchoverStatus::PREPARE_FLASHBACK_FOR_SWITCH_TO_PRIMARY_STATUS);

}  // share
}  // oceanbase

#endif /* OCEANBASE_SHARE_OB_TENANT_SWITCHOVER_STATUS_H_ */
