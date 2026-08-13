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

#ifndef OCEANBASE_SHARE_OB_FORK_TABLE_UTIL_H
#define OCEANBASE_SHARE_OB_FORK_TABLE_UTIL_H

#include "common/ob_tablet_id.h"
#include "share/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/hash/ob_hashmap.h"
#include "share/schema/ob_table_schema.h"
#include "share/ob_fork_table_info.h"


namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace rootserver
{
class ObDDLTask;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
}
}

namespace rootserver
{
class ObForkTableUtil final
{
public:
  static int collect_tablet_ids_from_table(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t table_id,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_tablet_ids_from_table(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_index_tablet_ids(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_lob_aux_tablet_ids(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_table_ids_from_table(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<uint64_t> &table_ids);

  static int get_tablet_ids(
      const common::ObIArray<share::schema::ObTableSchema> &table_schemas,
      common::ObIArray<common::ObTabletID> &tablet_ids);

  static int collect_complete_domain_index_schemas(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &table_schema,
      common::hash::ObHashMap<uint64_t, share::schema::ObTableSchema> &complete_index_schema_map);

  static bool is_domain_or_aux_index(const share::schema::ObTableSchema &index_schema);

  // Obtain snapshot for multiple tables at once to ensure consistency
  static int obtain_snapshot(
      common::ObMySQLTransaction &trans,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const common::ObIArray<const share::schema::ObTableSchema*> &data_table_schemas,
      int64_t &new_fetched_snapshot);

  // Release snapshot for multiple tables at once
  static int release_snapshot(
      rootserver::ObDDLTask* task,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const common::ObIArray<uint64_t> &table_ids,
      const int64_t snapshot_version);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_FORK_TABLE_UTIL_H
