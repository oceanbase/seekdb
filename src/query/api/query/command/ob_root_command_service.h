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

#ifndef OCEANBASE_QUERY_COMMAND_OB_ROOT_COMMAND_SERVICE_H_
#define OCEANBASE_QUERY_COMMAND_OB_ROOT_COMMAND_SERVICE_H_

#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}

namespace query
{

// Type-safe Query -> Rootserver command seam.  It intentionally preserves the
// currently observed command surface while the reverse dependency is removed;
// capability-specific interfaces can be split out after every caller crosses
// this seam.
class ObIRootCommandService
{
public:
  virtual ~ObIRootCommandService() = default;

  // Schema and DDL commands.
  virtual int modify_system_variable(const obcall::ObModifySysVarArg &arg) = 0;
  virtual int create_database(const obcall::ObCreateDatabaseArg &arg, obcall::UInt64 &db_id) = 0;
  virtual int parallel_create_table(const obcall::ObCreateTableArg &arg, obcall::ObCreateTableRes &res) = 0;
  virtual int alter_database(const obcall::ObAlterDatabaseArg &arg) = 0;
  virtual int set_comment(const obcall::ObSetCommentArg &arg, obcall::ObParallelDDLRes &res) = 0;
  virtual int alter_table(const obcall::ObAlterTableArg &arg, obcall::ObAlterTableRes &res) = 0;
  virtual int maintain_obj_dependency_info(const obcall::ObDependencyObjDDLArg &arg) = 0;
  virtual int rename_table(const obcall::ObRenameTableArg &arg) = 0;
  virtual int fork_table(const obcall::ObForkTableArg &arg, obcall::ObDDLRes &res) = 0;
  virtual int fork_database(const obcall::ObForkDatabaseArg &arg, obcall::ObDDLRes &res) = 0;
  virtual int truncate_table(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res) = 0;
  virtual int truncate_table_v2(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res) = 0;
  virtual int exchange_partition(const obcall::ObExchangePartitionArg &arg, obcall::ObAlterTableRes &res) = 0;
  virtual int create_index(const obcall::ObCreateIndexArg &arg, obcall::ObAlterTableRes &res) = 0;
  virtual int parallel_create_index(const obcall::ObCreateIndexArg &arg, obcall::ObAlterTableRes &res) = 0;
  virtual int drop_table(const obcall::ObDropTableArg &arg, obcall::ObDDLRes &res) = 0;
  virtual int parallel_drop_table(const obcall::ObDropTableArg &arg, obcall::ObDropTableRes &res) = 0;
  virtual int drop_database(const obcall::ObDropDatabaseArg &arg, obcall::ObDropDatabaseRes &res) = 0;
  virtual int drop_index(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res) = 0;
  virtual int purge_index(const obcall::ObPurgeIndexArg &arg) = 0;
  virtual int create_table_like(const obcall::ObCreateTableLikeArg &arg) = 0;
  virtual int purge_table(const obcall::ObPurgeTableArg &arg) = 0;
  virtual int restore_table_from_recyclebin(const obcall::ObRecyclebinRestoreTableArg &arg) = 0;
  virtual int purge_database(const obcall::ObPurgeDatabaseArg &arg) = 0;
  virtual int restore_database(const obcall::ObRecyclebinRestoreDatabaseArg &arg) = 0;
  virtual int purge_expire_recycle_objects(const obcall::ObPurgeRecycleBinArg &arg,
                                           obcall::Int64 &affected_rows) = 0;
  virtual int optimize_table(const obcall::ObOptimizeTableArg &arg) = 0;

  // Security commands.
  virtual int create_user(obcall::ObCreateUserArg &arg,
                          common::ObSArray<int64_t> &failed_index) = 0;
  virtual int drop_user(const obcall::ObDropUserArg &arg,
                        common::ObSArray<int64_t> &failed_index) = 0;
  virtual int rename_user(const obcall::ObRenameUserArg &arg,
                          common::ObSArray<int64_t> &failed_index) = 0;
  virtual int set_passwd(const obcall::ObSetPasswdArg &arg) = 0;
  virtual int grant(const obcall::ObGrantArg &arg) = 0;
  virtual int revoke_user(const obcall::ObRevokeUserArg &arg) = 0;
  virtual int lock_user(const obcall::ObLockUserArg &arg,
                        common::ObSArray<int64_t> &failed_index) = 0;
  virtual int alter_user_default_role(
      const obcall::ObAlterUserRoleArg &arg) = 0;
  virtual int revoke_database(const obcall::ObRevokeDBArg &arg) = 0;
  virtual int revoke_table(const obcall::ObRevokeTableArg &arg) = 0;
  virtual int revoke_routine(const obcall::ObRevokeRoutineArg &arg) = 0;
  virtual int alter_role(const obcall::ObAlterRoleArg &arg) = 0;
  virtual int revoke_object(const obcall::ObRevokeObjMysqlArg &arg) = 0;

  // Stored-program and named-object commands.
  virtual int create_outline(const obcall::ObCreateOutlineArg &arg) = 0;
  virtual int alter_outline(const obcall::ObAlterOutlineArg &arg) = 0;
  virtual int drop_outline(const obcall::ObDropOutlineArg &arg) = 0;
  virtual int create_routine(const obcall::ObCreateRoutineArg &arg) = 0;
  virtual int drop_routine(const obcall::ObDropRoutineArg &arg) = 0;
  virtual int alter_routine(const obcall::ObCreateRoutineArg &arg) = 0;
  virtual int create_package(const obcall::ObCreatePackageArg &arg) = 0;
  virtual int drop_package(const obcall::ObDropPackageArg &arg) = 0;
  virtual int create_trigger_with_res(const obcall::ObCreateTriggerArg &arg,
                                      obcall::ObCreateTriggerRes &res) = 0;
  virtual int alter_trigger(const obcall::ObAlterTriggerArg &arg) = 0;
  virtual int drop_trigger(const obcall::ObDropTriggerArg &arg) = 0;
  virtual int create_ai_model(const obcall::ObCreateAiModelArg &arg) = 0;
  virtual int drop_ai_model(const obcall::ObDropAiModelArg &arg) = 0;

  // Administrative commands.
  virtual int root_minor_freeze(const obcall::ObMinorFreezeArg &arg) = 0;
  virtual int tablet_major_freeze(const common::ObTabletID &tablet_id) = 0;
  virtual int major_freeze() = 0;
  virtual int suspend_merge() = 0;
  virtual int resume_merge() = 0;
  virtual int clear_merge_error() = 0;
  virtual int admin_set_config(obcall::ObAdminSetConfigArg &arg) = 0;
  virtual int check_partition_exchange_schema_for_user(
      const share::schema::ObTableSchema &base_table_schema,
      const share::schema::ObTableSchema &inc_table_schema,
      const common::ObString &partition_name,
      share::schema::ObPartitionLevel exchange_part_level) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_COMMAND_OB_ROOT_COMMAND_SERVICE_H_
