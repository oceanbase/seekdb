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

#ifndef OCEANBASE_STORAGE_OB_COMMON_ID_UTILS_H_
#define OCEANBASE_STORAGE_OB_COMMON_ID_UTILS_H_

#include "share/ob_common_id.h"             // ObCommonID

#include "share/ob_max_id_fetcher.h"     // ObMaxIdType, ObMaxIdFetcher

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}

namespace share
{
enum ObMaxIdType;
}
namespace storage
{

// Utils for ObCommonID
class ObCommonIDUtils
{
public:
  // Use ObUniqueIDService to generate Unique ID for ObCommonID in target tenant.
  //
  // NOTE: ID is unique, but not monotonic.
  //
  //
  // @param [in] tenant_id  target tenant id
  // @param [out] id        generated ID
  //
  // @return
  //    - OB_INVALID_ARGUMENT  tenant_id is invalid or not matched with MTL_ID
  static int gen_unique_id(const uint64_t tenant_id, share::ObCommonID &id);

  // Send rpc to the leader of sys LS of target tenant to execute gen_unique_id.
  //
  // Use this one when target tenant doesn't exist on current machine.
  static int gen_unique_id_by_rpc(const uint64_t tenant_id, share::ObCommonID &id);

  // Use ObMaxIdFetcher to generate monotonically increasing ID for ObCommonID in target tenant
  //
  // @param [in] tenant_id    target tenant id
  // @param [in] id_type      id type for ObMaxIdFetcher
  // @param [in] proxy        sql proxy
  // @param [out] id          generated monotonically increasing ID
};

}
}

#endif /* OCEANBASE_STORAGE_OB_COMMON_ID_UTILS_H_ */
