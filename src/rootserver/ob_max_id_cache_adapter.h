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

#ifndef OCEANBASE_ROOTSERVER_OB_MAX_ID_CACHE_ADAPTER_H_
#define OCEANBASE_ROOTSERVER_OB_MAX_ID_CACHE_ADAPTER_H_

#include "share/ob_i_max_id_cache.h"

namespace oceanbase
{
namespace rootserver
{
class ObLocalManagementService;

class ObMaxIdCacheAdapter final : public share::ObIMaxIdCache
{
public:
  explicit ObMaxIdCacheAdapter(ObLocalManagementService &management_service)
      : management_service_(management_service)
  {}

  int fetch_max_id(share::ObMaxIdType id_type, uint64_t &min_id,
                   uint64_t size) override;

private:
  ObLocalManagementService &management_service_;
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_MAX_ID_CACHE_ADAPTER_H_
