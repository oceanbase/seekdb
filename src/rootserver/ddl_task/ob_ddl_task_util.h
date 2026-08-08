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

#ifndef OCEANBASE_ROOTSERVER_DDL_TASK_OB_DDL_TASK_UTIL_H_
#define OCEANBASE_ROOTSERVER_DDL_TASK_OB_DDL_TASK_UTIL_H_

#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace obcall
{
struct ObCreateIndexArg;
}
namespace rootserver
{

class ObDDLTask;
class ObRootService;
class ObLocalManagementService;

// Rootserver-owned DDL orchestration helpers.  These operations deliberately
// depend on Rootserver task/service implementations and are not Share APIs.
class ObDDLTaskUtil final
{
public:
  static int generate_build_replica_sql(
      const int64_t data_table_id,
      const int64_t dest_table_id,
      const int64_t schema_version,
      const int64_t snapshot_version,
      const int64_t execution_id,
      const int64_t task_id,
      const int64_t parallelism,
      const bool use_heap_table_ddl_plan,
      const bool use_schema_version_hint_for_src_table,
      const share::ObColumnNameMap *col_name_map,
      const common::ObString &partition_names,
      common::ObSqlString &sql_string);
  static int generate_partition_names(
      const common::ObIArray<common::ObString> &partition_names_array,
      common::ObIAllocator &allocator,
      common::ObString &partition_names);
  static int check_target_partition_is_running(
      const common::ObString &running_sql_info,
      const common::ObString &partition_name,
      common::ObIAllocator &allocator,
      bool &is_running_status);
  static int get_task_tablet_slice_count(
      const int64_t task_id,
      bool &is_partition_table,
      common::hash::ObHashMap<int64_t, int64_t> &tablet_slice_count_map);
  static int check_table_empty(
      const share::schema::ObSysVariableSchema &sys_var_schema,
      const common::ObString &database_name,
      const share::schema::ObTableSchema &table_schema,
      const ObSQLMode sql_mode,
      bool &is_table_empty);
  static int64_t get_real_parallelism(const int64_t parallelism);
  static int obtain_snapshot(
      const share::ObDDLTaskStatus next_task_status,
      const uint64_t table_id,
      const uint64_t target_table_id,
      int64_t &snapshot_version,
      ObDDLTask *task);
  static int release_snapshot(
      ObDDLTask *task,
      const uint64_t table_id,
      const uint64_t target_table_id,
      const int64_t snapshot_version);
  static int check_and_cancel_single_replica_dag(
      ObDDLTask *task,
      const uint64_t table_id,
      const uint64_t target_table_id,
      common::hash::ObHashMap<common::ObTabletID, common::ObTabletID> &check_dag_exit_tablets_map,
      const uint64_t data_format_version,
      int64_t &check_dag_exit_retry_cnt,
      bool is_complement_data_dag,
      bool &all_dag_exit);
  static int obtain_snapshot(
      common::ObMySQLTransaction &trans,
      const share::schema::ObTableSchema &data_table_schema,
      const share::schema::ObTableSchema &index_table_schema,
      int64_t &new_fetched_snapshot);
  static int calc_snapshot_with_gts(
      int64_t &snapshot,
      const int64_t ddl_task_id = 0,
      const int64_t trans_end_snapshot = 0,
      const int64_t index_snapshot_version_diff = 0);
  static int construct_domain_index_arg(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema *table_schema,
      const share::schema::ObTableSchema *&index_schema,
      ObDDLTask &task,
      obcall::ObCreateIndexArg &create_index_arg,
      share::ObDDLType &ddl_type);
  static int get_domain_index_share_table_snapshot(
      const share::schema::ObTableSchema *table_schema,
      const share::schema::ObTableSchema *index_schema,
      const int64_t task_id,
      const obcall::ObCreateIndexArg &create_index_arg,
      int64_t &fts_snapshot_version);
  static int write_defensive_and_obtain_snapshot(
      common::ObMySQLTransaction &trans,
      const share::schema::ObTableSchema &data_table_schema,
      const share::schema::ObTableSchema &index_table_schema,
      share::schema::ObSchemaService *schema_service,
      int64_t &new_fetched_snapshot);
  static int load_ddl_task(
      const int64_t task_id,
      common::ObIAllocator &allocator,
      ObDDLTask &task);

private:
  static int generate_order_by_str(
      const common::ObIArray<int64_t> &select_column_ids,
      const common::ObIArray<int64_t> &order_column_ids,
      common::ObSqlString &sql_string);
  static int check_need_update_domain_index_share_table_snapshot(
      const share::schema::ObTableSchema *table_schema,
      const share::schema::ObTableSchema *index_schema,
      const int64_t task_id,
      const obcall::ObCreateIndexArg &create_index_arg,
      bool &need_update_snapshot);
  static int hold_snapshot(
      common::ObMySQLTransaction &trans,
      ObDDLTask *task,
      const uint64_t table_id,
      const uint64_t target_table_id,
      ObLocalManagementService *local_management_service,
      const int64_t snapshot_version);
  static int hold_snapshot(
      common::ObMySQLTransaction &trans,
      const share::schema::ObTableSchema &data_table_schema,
      const share::schema::ObTableSchema &index_table_schema,
      const int64_t snapshot);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_DDL_TASK_OB_DDL_TASK_UTIL_H_
