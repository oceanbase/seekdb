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

#ifndef OCEANBASE_WR_OB_WORKLOAD_REPOSITORY_RPC_PROXY_H_
#define OCEANBASE_WR_OB_WORKLOAD_REPOSITORY_RPC_PROXY_H_

#include "share/ob_define.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "observer/ob_server_struct.h"
#include "share/rpc/ob_async_rpc_proxy.h"

namespace oceanbase
{

namespace share
{
class ObWrCreateSnapshotArg;
class ObWrPurgeSnapshotArg;
class ObWrUserSubmitSnapArg;
class ObWrUserSubmitSnapResp;
class ObWrUserModifySettingsArg;
}  // namespace share

namespace obrpc
{
class ObWrRpcProxy : public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObWrRpcProxy);
  virtual ~ObWrRpcProxy() {}
  RPC_AP(@PR5 wr_async_take_snapshot, obrpc::OB_WR_ASYNC_SNAPSHOT_TASK,
      (share::ObWrCreateSnapshotArg));
  RPC_AP(@PR5 wr_async_purge_snapshot, obrpc::OB_WR_ASYNC_PURGE_SNAPSHOT_TASK,
      (share::ObWrPurgeSnapshotArg));
  RPC_S(@PR5 wr_sync_user_submit_snapshot_task, obrpc::OB_WR_SYNC_USER_SUBMIT_SNAPSHOT_TASK,
      (share::ObWrUserSubmitSnapArg), share::ObWrUserSubmitSnapResp);
  RPC_S(@PR5 wr_sync_user_modify_settings_task, obrpc::OB_WR_SYNC_USER_MODIFY_SETTINGS_TASK,
      (share::ObWrUserModifySettingsArg));
};

}  // namespace obrpc
}  // namespace oceanbase
#endif  // OCEANBASE_WR_OB_WORKLOAD_REPOSITORY_RPC_PROXY_H_
