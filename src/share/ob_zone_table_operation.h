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

#ifndef OCEANBASE_SHARE_OB_ZONE_TABLE_OPERATION_H_
#define OCEANBASE_SHARE_OB_ZONE_TABLE_OPERATION_H_

#include "lib/container/ob_iarray.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "common/ob_zone.h"
#include "share/ob_lease_struct.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
class ObZoneInfoItem;
class ObZoneInfo;
struct ObGlobalInfo;

class ObZoneTableOperation
{
public:
  static int update_info_item(common::ObISQLClient &sql_client,
      const common::ObZone &zone, const ObZoneInfoItem &item, bool insert = false);

  static int get_zone_list(
      common::ObISQLClient &sql_client, common::ObIArray<common::ObZone> &zone_list);

  static int get_zone_lease_info(common::ObISQLClient &sql_client, ObZoneLeaseInfo &info);
  // check_zone_exists is a newly added arg.
  // If the zone not exists,
  // in the previous implementation, OB_SUCCESS will be returned.
  // However, if check_zone_exists is set, OB_ZONE_INFO_NOT_EXIST will be returned.
  static int load_global_info(
      common::ObISQLClient &sql_client,
      ObGlobalInfo &info,
      const bool check_zone_exists = false);
  // check_zone_exists is a newly added arg.
  // If the zone not exists,
  // in the previous implementation, OB_SUCCESS will be returned.
  // However, if check_zone_exists is set, OB_ZONE_INFO_NOT_EXIST will be returned.
  static int load_zone_info(
      common::ObISQLClient &sql_client,
      ObZoneInfo &info,
      const bool check_zone_exists = false);

  static int get_zone_region_list(
      common::ObISQLClient &sql_client,
      hash::ObHashMap<ObZone, ObRegion> &zone_info_map);

  static int insert_global_info(common::ObISQLClient &sql_client, ObGlobalInfo &info);
  static int insert_zone_info(common::ObISQLClient &sql_client, ObZoneInfo &info);

  static int remove_zone_info(common::ObISQLClient &sql_client, const common::ObZone &zone);
  static int get_region_list(
      common::ObISQLClient &sql_client, common::ObIArray<common::ObRegion> &region_list);
  static int check_zone_active(
      common::ObISQLClient &sql_client,
      const common::ObZone &zone,
      bool &is_active);
  static int get_inactive_zone_list(
      common::ObISQLClient &sql_client,
      common::ObIArray<common::ObZone> &zone_list);
  static int get_active_zone_list(
      common::ObISQLClient &sql_client,
      common::ObIArray<common::ObZone> &zone_list);
  static int get_zone_info(
      const ObZone &zone,
      common::ObISQLClient &sql_client,
      ObZoneInfo &zone_info);
  static int update_global_config_version_with_lease(
      ObMySQLTransaction &trans,
      const int64_t global_config_version);
private:
  template <typename T>
      static int set_info_item(const char *name, const int64_t value, const char *info_str,
          T &info);
  template <typename T>
      static int load_info(
          common::ObISQLClient &sql_client,
          T &info,
          const bool check_zone_exists);
  template <typename T>
      static int insert_info(common::ObISQLClient &sql_client, T &info);
  static int get_zone_item_count(int64_t &cnt);
  // if is_active, then get active zone_list
  // if !is_active, then get inactive zone_list
  static int get_zone_list_(
      common::ObISQLClient &sql_client,
      common::ObIArray<common::ObZone> &zone_list,
      const bool is_active);
  static int get_config_version_with_lease_(
      ObMySQLTransaction &trans,
      int64_t &current_config_version,
      int64_t &current_lease_info_version);
  static int inner_update_global_config_version_with_lease_(
      ObMySQLTransaction &trans,
      const int64_t global_config_version_to_update,
      const int64_t lease_info_version_to_update);
};

}//end namespace share
}//end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_ZONE_TABLE_OPERATION_H_
