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

#ifndef OCEANBASE_TRANSACTION_OB_TIMESTAMP_ACCESS_
#define OCEANBASE_TRANSACTION_OB_TIMESTAMP_ACCESS_

#include "share/rc/ob_tenant_base.h"
#include "common/ob_role.h"

namespace oceanbase
{

namespace obrpc
{
class ObGtsRpcResult;
}

namespace transaction
{
class ObGtsRequest;

class ObTimestampAccess
{
public:
  ObTimestampAccess() : service_type_(FOLLOWER) {}
  ~ObTimestampAccess() {}
  static int mtl_init(ObTimestampAccess *&timestamp_access)
  {
    timestamp_access->reset();
    return OB_SUCCESS;
  }
  void destroy() { reset();}
  void reset() { service_type_ = FOLLOWER; }
  enum ServiceType {
    FOLLOWER = 0,
    GTS_LEADER,
    STS_LEADER,
  };
  void set_service_type(const ServiceType service_type) { service_type_ = service_type; }
  ServiceType get_service_type() const { return service_type_; }
  int handle_request(const ObGtsRequest &request, obrpc::ObGtsRpcResult &result);
  int get_number(int64_t &gts);
  void get_virtual_info(int64_t &ts_value,
                        ServiceType &service_type,
                        common::ObRole &role,
                        int64_t &proposal_id);
  static const char *service_type_to_cstr(const ServiceType service_type)
  {
    const char *str;
    switch (service_type) {
      case ServiceType::FOLLOWER:
        str = "FOLLOWER";
        break;
      case ServiceType::GTS_LEADER:
        str = "GTS_LEADER";
        break;
      case ServiceType::STS_LEADER:
        str = "STS_LEADER";
        break;
      default:
        str = "UNKNOWN";
        break;
    }
    return str;
  }
  static const char *ts_type_to_cstr(bool is_primary)
  {
    const char *str;
    if (is_primary) {
      str = "GTS";
    } else {
      str = "STS";
    }
    return str;
  }
private:
  ServiceType service_type_;
};


}
}
#endif
