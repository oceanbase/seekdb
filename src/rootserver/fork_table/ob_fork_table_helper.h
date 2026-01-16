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

#ifndef OCEANBASE_ROOTSERVER_OB_FORK_TABLE_HELPER_H_
#define OCEANBASE_ROOTSERVER_OB_FORK_TABLE_HELPER_H_

#include "share/ob_fork_table_util.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/ob_autoincrement_service.h"

namespace oceanbase
{
namespace rootserver
{

// Helper class for fork table operations
// This class encapsulates fork table logic to reduce intrusion into create_tables_in_trans
class ObForkTableHelper
{
public:
  ObForkTableHelper(
      share::schema::ObMultiVersionSchemaService &schema_service,
      common::ObMySQLProxy &sql_proxy,
      common::ObMySQLTransaction &trans,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const uint64_t tenant_id,
      const share::ObForkTableInfo &fork_table_info,
      const share::schema::ObTableSchema *dst_table_schema = nullptr)
    : schema_service_(schema_service),
      sql_proxy_(sql_proxy),
      trans_(trans),
      schema_guard_(schema_guard),
      tenant_id_(tenant_id),
      fork_table_info_(fork_table_info),
      dst_table_schema_(dst_table_schema),
      src_table_schema_(nullptr),
      src_tablet_ids_(),
      dst_tablet_ids_(),
      inited_(false) {}
  ~ObForkTableHelper() {}
  int init(const common::ObIArray<share::schema::ObTableSchema> &table_schemas);
  int execute();

private:
  int copy_tablet_autoinc_seq_info_();
  int copy_tablet_truncate_info_();
  int copy_table_autoinc_seq_info_();
  int copy_table_statistics_();
  int copy_stat_info_(
      const char *table_name,
      const uint64_t src_table_id,
      const int64_t src_part_id,
      const uint64_t dst_table_id,
      const int64_t dst_part_id);
  const char *get_table_schema_(const char *table_name);
  int get_tablet_handle_(const common::ObTabletID &tablet_id, storage::ObTabletHandle &tablet_handle) const;
  share::schema::ObMultiVersionSchemaService &schema_service_;
  common::ObMySQLProxy &sql_proxy_;
  common::ObMySQLTransaction &trans_;
  share::schema::ObSchemaGetterGuard &schema_guard_;
  const uint64_t tenant_id_;
  const share::ObForkTableInfo &fork_table_info_;
  const share::schema::ObTableSchema *dst_table_schema_;
  const share::schema::ObTableSchema *src_table_schema_;
  uint64_t src_table_id_;
  uint64_t dst_table_id_;
  common::ObSEArray<common::ObTabletID, 4> src_tablet_ids_;
  common::ObSEArray<common::ObTabletID, 4> dst_tablet_ids_;
  bool inited_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObForkTableHelper);
};

} // namespace rootserver
} // namespace oceanbase

#endif  // OCEANBASE_ROOTSERVER_OB_FORK_TABLE_HELPER_H_
