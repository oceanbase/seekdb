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

#ifndef OCEANBASE_ROOTSERVER_OB_ROOTSERVER_LOCAL_RUNTIME_H_
#define OCEANBASE_ROOTSERVER_OB_ROOTSERVER_LOCAL_RUNTIME_H_

#include "rootserver/ob_i_debug_sync_local_runtime.h"
#include "share/ob_rpc_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ob_storage_rpc_arg.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace rootserver
{

// Local server operations required by Rootserver. Rootserver owns this
// interface; Observer supplies the production adapter during composition.
class ObIRootserverLocalRuntime : public ObIDebugSyncLocalRuntime
{
public:
  virtual ~ObIRootserverLocalRuntime() = default;

  virtual int calc_column_checksum_request(
      const obcall::ObCalcColumnChecksumRequestArg &arg,
      obcall::ObCalcColumnChecksumRequestRes &res) = 0;
  virtual int build_ddl_local(
      const obcall::ObDDLLocalBuildArg &arg,
      obcall::ObDDLLocalBuildResult &res) = 0;
  virtual int check_and_cancel_ddl_complement_data_dag(
      const obcall::ObDDLLocalBuildArg &arg,
      bool &is_dag_exist) = 0;
  virtual int check_and_cancel_delete_lob_meta_row_dag(
      const obcall::ObDDLLocalBuildArg &arg,
      bool &is_dag_exist) = 0;
  virtual int minor_freeze(
      const obcall::ObMinorFreezeArg &arg,
      obcall::Int64 &result) = 0;
  virtual int check_schema_version_elapsed(
      const obcall::ObCheckSchemaVersionElapsedArg &arg,
      obcall::ObCheckSchemaVersionElapsedResult &result) = 0;
  virtual int check_modify_time_elapsed(
      const obcall::ObCheckModifyTimeElapsedArg &arg,
      obcall::ObCheckModifyTimeElapsedResult &result) = 0;
  virtual int check_ddl_tablet_merge_status(
      const obcall::ObDDLCheckTabletMergeStatusArg &arg,
      obcall::ObDDLCheckTabletMergeStatusResult &result) = 0;
  virtual int check_server_empty(bool &is_empty) = 0;
  virtual int wait_until_change_stream_refreshed(
      common::ObMySQLProxy &mysql_proxy,
      int64_t timeout_us) = 0;
};

inline ObIRootserverLocalRuntime *rootserver_local_runtime()
{
  return ::oceanbase::share::server_service<
      ::oceanbase::rootserver::ObIRootserverLocalRuntime>();
}

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_ROOTSERVER_LOCAL_RUNTIME_H_
