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

#ifndef OCEANBASE_ROOTSERVER_OB_LS_ALL_PART_BUILDER_H
#define OCEANBASE_ROOTSERVER_OB_LS_ALL_PART_BUILDER_H

#include "share/transfer/ob_transfer_info.h"    // ObTransferPartList, ObTransferPartInfo

namespace oceanbase
{
namespace common
{
class ObISQLClient;
} // ns common

namespace share
{
class ObLSID;
class ObTabletToLSInfo;

namespace schema
{
class ObMultiVersionSchemaService;
class ObSchemaGetterGuard;
} // ns schema
} // ns share

namespace rootserver
{

class ObLSAllPartBuilder
{
public:
  // build current all part list on specific LS
  //
  // @return OB_NEED_RETRY     schema not latest or schema changed during handling
  static int build(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      share::schema::ObMultiVersionSchemaService &schema_service,
      common::ObISQLClient &sql_proxy,
      share::ObTransferPartList &part_list);

private:
  static int get_latest_schema_guard_(
      const uint64_t tenant_id,
      share::schema::ObMultiVersionSchemaService &schema_service,
      common::ObISQLClient &sql_proxy,
      share::schema::ObSchemaGetterGuard &schema_guard);
  static int build_part_info_(
      const uint64_t tenant_id,
      share::ObTabletToLSInfo &tablet,
      share::schema::ObSchemaGetterGuard &schema_guard,
      share::ObTransferPartInfo &part_info,
      bool &need_skip);
};

}
}
#endif /* !OCEANBASE_ROOTSERVER_OB_LS_ALL_PART_BUILDER_H */
