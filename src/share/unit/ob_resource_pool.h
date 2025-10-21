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

#ifndef OCEANBASE_COMMON_UNIT_OB_RESOURCE_POOL_H_
#define OCEANBASE_COMMON_UNIT_OB_RESOURCE_POOL_H_

#include "lib/ob_define.h"                        // is_valid_tenant_id, ObReplicaType
#include "lib/utility/ob_unify_serialize.h"       // OB_UNIS_VERSION
#include "lib/string/ob_fixed_length_string.h"    // ObFixedLengthString
#include "lib/container/ob_se_array.h"            // ObSEArray
#include "common/ob_zone.h"                       // ObZone

namespace oceanbase
{
namespace share
{
typedef common::ObFixedLengthString<common::MAX_RESOURCE_POOL_LENGTH> ObResourcePoolName;
struct ObResourcePool
{
  OB_UNIS_VERSION(1);

public:
  ObResourcePool();
  ~ObResourcePool() {}
  void reset();
  bool is_valid() const;
  int assign(const ObResourcePool &other);
  bool is_granted_to_tenant() const { return is_valid_tenant_id(tenant_id_); }
  DECLARE_TO_STRING;

  uint64_t resource_pool_id_;
  ObResourcePoolName name_;
  int64_t unit_count_;
  uint64_t unit_config_id_;
  common::ObSEArray<common::ObZone, DEFAULT_ZONE_COUNT> zone_list_;
  uint64_t tenant_id_;
  common::ObReplicaType replica_type_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObResourcePool);
};
}//end namespace share
}//end namespace oceanbase

#endif //OCEANBASE_COMMON_UNIT_OB_RESOURCE_POOL_H_
