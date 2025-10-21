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

#ifndef OB_OMT_TENANT_META_H_
#define OB_OMT_TENANT_META_H_

#include "share/ob_unit_getter.h"
#include "storage/ob_super_block_struct.h"

namespace oceanbase
{
namespace omt
{

struct ObTenantMeta final
{
public:
  ObTenantMeta()
    : unit_(),
      super_block_(),
      create_status_(storage::ObTenantCreateStatus::CREATING),
      epoch_(0) {}
  ObTenantMeta(const ObTenantMeta &) = default;
  ObTenantMeta &operator=(const ObTenantMeta &) = default;

  ~ObTenantMeta() = default;

  bool is_valid() const
  {
    return unit_.is_valid() && super_block_.is_valid() && epoch_ >= 0;
  }

  int build(const share::ObUnitInfoGetter::ObTenantConfig &unit,
            const storage::ObTenantSuperBlock &super_block);

  TO_STRING_KV(K_(unit), K_(super_block), K_(create_status), K_(epoch));

  OB_UNIS_VERSION_V(1);

public:
  share::ObUnitInfoGetter::ObTenantConfig unit_;
  storage::ObTenantSuperBlock super_block_;
  storage::ObTenantCreateStatus create_status_;
  int64_t epoch_; // no need serialize for shared-nothing
};

}  // end namespace omt
}  // end namespace oceanbase

#endif  // OB_OMT_TENANT_META_H_
