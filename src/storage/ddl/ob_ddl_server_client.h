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

#ifndef OCEANBASE_STORAGE_OB_DDL_SERVER_CLIENT_H
#define OCEANBASE_STORAGE_OB_DDL_SERVER_CLIENT_H

#include "share/ob_rpc_struct.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace storage
{
class ObDDLServerClient final
{
public:
  /**
   * For recover restore table ddl task, including:
   * 1. create a user hidden table under the target tenant(dest tenant).
   * 2. import the backup tenant's data into the target tenant by the table redefinition task.
  */
  /**
   * for load data.
  */
  static int create_hidden_table(
      const obrpc::ObCreateHiddenTableArg &arg, 
      obrpc::ObCreateHiddenTableRes &res, 
      int64_t &snapshot_version,
      uint64_t &data_format_version,
      sql::ObSQLSessionInfo &session);
  static int copy_table_dependents(const obrpc::ObCopyTableDependentsArg &arg, sql::ObSQLSessionInfo &session);
  static int finish_redef_table(const obrpc::ObFinishRedefTableArg &finish_redef_arg,
                                const obrpc::ObDDLBuildSingleReplicaResponseArg &build_single_arg,
                                sql::ObSQLSessionInfo &session);
  static int finish_redef_table(const obrpc::ObFinishRedefTableArg &finish_redef_arg);
  static int abort_redef_table(const obrpc::ObAbortRedefTableArg &arg, sql::ObSQLSessionInfo *session = nullptr);
  static int build_ddl_single_replica_response(const obrpc::ObDDLBuildSingleReplicaResponseArg &arg);
private:
  static int wait_task_reach_pending(
      const uint64_t tenant_id, 
      const int64_t task_id, 
      int64_t &snapshot_version,
      uint64_t &data_format_version, 
      ObMySQLProxy &sql_proxy,
      bool &is_no_logging);
  static int heart_beat_clear(const int64_t task_id, const uint64_t tenant_id);
  static int check_need_stop(const uint64_t tenant_id);
};

}  // end of namespace observer
}  // end of namespace oceanbase

#endif /*_OCEANBASE_STORAGE_OB_DDL_SERVER_CLIENT_H_ */
