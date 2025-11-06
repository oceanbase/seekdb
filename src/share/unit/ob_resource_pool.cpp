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


#include "ob_resource_pool.h"

namespace oceanbase
{
using namespace common;
namespace share
{
ObResourcePool::ObResourcePool()
{
  reset();
}

void ObResourcePool::reset()
{
  resource_pool_id_ = OB_INVALID_ID;
  name_.reset();
  unit_count_ = 0;
  unit_config_id_ = OB_INVALID_ID;
  zone_list_.reset();
  tenant_id_ = OB_INVALID_ID;
  replica_type_ = REPLICA_TYPE_FULL;
}

bool ObResourcePool::is_valid() const
{
  return !name_.is_empty() && unit_count_ > 0;
}

int ObResourcePool::assign(const ObResourcePool &other)
{
  int ret = OB_SUCCESS;
  resource_pool_id_ = other.resource_pool_id_;
  name_ = other.name_;
  unit_count_ = other.unit_count_;
  unit_config_id_ = other.unit_config_id_;
  if (OB_FAIL(copy_assign(zone_list_, other.zone_list_))) {
   SHARE_LOG(WARN, "failed to assign zone_list_", KR(ret));
  }
  tenant_id_ = other.tenant_id_;
  replica_type_ = other.replica_type_;
  return ret;
}

DEF_TO_STRING(ObResourcePool)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(resource_pool_id),
       K_(name),
       K_(unit_count),
       K_(unit_config_id),
       K_(zone_list),
       K_(tenant_id),
       K_(replica_type));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObResourcePool,
                    resource_pool_id_,
                    name_,
                    unit_count_,
                    unit_config_id_,
                    zone_list_,
                    tenant_id_,
                    replica_type_);

}//end namespace share
}//end namespace oceanbase
