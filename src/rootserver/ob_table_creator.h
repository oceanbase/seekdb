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

#ifndef OB_TABLE_CREATOR_H
#define OB_TABLE_CREATOR_H

#include "rootserver/ob_tablet_creator.h"
#include "rootserver/fork_table/ob_fork_table_info_builder.h"
#include "share/tablet/ob_tablet_info.h" // ObTabletTablePair

#include "share/tablet/ob_tablet_to_ls_operator.h"
#include "storage/column_store/ob_column_store_replica_util.h"
#include "share/ob_ddl_common.h" // ObForkTabletInfo

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace rootserver
{
class ObTableCreator
{
public:
  ObTableCreator(
      const uint64_t tenant_id,
      const share::SCN &frozen_scn,
      ObMySQLTransaction &trans)
                : tenant_id_(tenant_id),
                  tablet_creator_(tenant_id, frozen_scn, trans),
                  trans_(trans),
                  ls_id_array_(),
                  inited_(false),
                  fork_table_info_builder_(tenant_id) {}

  virtual ~ObTableCreator();
  int init(const bool need_tablet_cnt_check);
  // for fork table creation
  int init_with_fork_table_info(
      const share::ObForkTableInfo &main_fork_table_info,
      const common::ObIArray<uint64_t> &dest_table_ids,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const bool need_tablet_cnt_check);
  int execute();

  // create tablets in some tables, one of which is data table, others are its local indexes
  //
  // @param [in] schemas, tables schema for creating tablets, the first is data table, others are its local indexes
  int add_create_tablets_of_tables_arg(
      const common::ObIArray<const share::schema::ObTableSchema*> &schemas,
      const common::ObIArray<share::ObLSID> &ls_id_array,
      const uint64_t tenant_data_version,
      const common::ObIArray<bool> &need_create_empty_majors,
      const bool ignore_cs_replica = false,
      share::schema::ObSchemaGetterGuard *schema_guard = nullptr);

  // create tablets for local aux tables(include local_index/aux_lob_table), which are belong to a data table.
  //
  // @param [in] schemas, indexes/aux_lob_table schema for creating tablets
  // @param [in] data_table_schema, data table schema of indexes
  int add_create_tablets_of_local_aux_tables_arg(
      const common::ObIArray<const share::schema::ObTableSchema*> &schemas,
      const share::schema::ObTableSchema *data_table_schema,
      const common::ObIArray<share::ObLSID> &ls_id_array,
      const uint64_t tenant_data_version,
      const common::ObIArray<bool> &need_create_empty_majors);

  // create tablets of hidden table from original table, used by ddl table redefinition
  int add_create_bind_tablets_of_hidden_table_arg(
      const share::schema::ObTableSchema &orig_table_schema,
      const share::schema::ObTableSchema &hidden_table_schema,
      const common::ObIArray<share::ObLSID> &ls_id_array,
      const uint64_t tenant_data_version,
      const bool ignore_cs_replica);

  // create tablets in a table
  //
  // @param [in] table_schema, table schema for creating tablets
  int add_create_tablets_of_table_arg(
      const share::schema::ObTableSchema &table_schema,
      const common::ObIArray<share::ObLSID> &ls_id_array,
      const uint64_t tenant_data_version,
      const bool need_create_empty_major_sstable,
      share::schema::ObSchemaGetterGuard *schema_guard = nullptr);
private:
  int add_create_tablets_of_tables_arg_(
      const common::ObIArray<const share::schema::ObTableSchema*> &schemas,
      const share::schema::ObTableSchema *data_table_schema,
      const common::ObIArray<share::ObLSID> &ls_id_array,
      const uint64_t tenant_data_version,
      const common::ObIArray<bool> &need_create_empty_majors,
      const bool ignore_cs_replica = false,
      share::schema::ObSchemaGetterGuard *schema_guard = nullptr);
  int generate_create_tablet_arg_(
      const common::ObIArray<const share::schema::ObTableSchema*> &schemas,
      const ObTableSchema &data_table_schema,
      const lib::Worker::CompatMode &mode,
      const share::ObLSID &ls_id,
      common::ObIArray<share::ObTabletTablePair> &pairs,
      const int64_t part_idx,
      const int64_t subpart_idx,
      const bool is_create_bind_hidden_tablets,
      const uint64_t tenant_data_version,
      const common::ObIArray<bool> &need_create_empty_majors,
      const ObGlobalCSReplicaMgr &cs_replica_mgr,
      ObSchemaGetterGuard *schema_guard = nullptr);
  int get_tablet_list_str_(
      const share::schema::ObTableSchema &table_schema,
      ObSqlString &tablet_list);

private:
  const uint64_t tenant_id_;
  ObTabletCreator tablet_creator_;
  ObMySQLTransaction &trans_;
  common::ObArray<share::ObLSID> ls_id_array_;
  common::ObArray<share::ObTabletToLSInfo> tablet_infos_;
  bool inited_;
  ObForkTableInfoBuilder fork_table_info_builder_;
};
}
}


#endif /* !OB_TABLE_CREATOR_H */
