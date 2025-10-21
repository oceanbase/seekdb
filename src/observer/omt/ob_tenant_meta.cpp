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

#define USING_LOG_PREFIX SERVER_OMT

#include "ob_tenant_meta.h"

namespace oceanbase
{
namespace omt
{

OB_SERIALIZE_MEMBER(ObTenantMeta, unit_, super_block_, create_status_);

int ObTenantMeta::build(const share::ObUnitInfoGetter::ObTenantConfig &unit,
                        const storage::ObTenantSuperBlock &super_block)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!unit.is_valid() || !super_block.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(unit), K(super_block));
  } else {
    unit_ = unit;
    super_block_ = super_block;
    create_status_ = storage::ObTenantCreateStatus::CREATING;
    epoch_ = 0;
  }

  return ret;
}


}  // end namespace omt
}  // end namespace oceanbase
