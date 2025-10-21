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

#ifndef OCEANBASE_OBRPC_MOCK_OB_SRV_RPC_PROXY_H_
#define OCEANBASE_OBRPC_MOCK_OB_SRV_RPC_PROXY_H_

#include <gmock/gmock.h>
#include "share/ob_srv_rpc_proxy.h"
#include "share/ob_rpc_struct.h"
#include "common/storage/ob_freeze_define.h"
#include "lib/container/ob_array_iterator.h"

namespace oceanbase
{
namespace obrpc
{
class MockObSrvRpcProxy : public ObSrvRpcProxy
{
public:
  MockObSrvRpcProxy()
    : ObSrvRpcProxy(this)
  {
  }

  MOCK_METHOD2(fetch_sys_lsn, int(share::ObLSReplica &replica,
                                         const ObRpcOpts &opts));
  MOCK_METHOD3(get_wrs_info, int(const ObGetWRSArg &arg, AsyncCB<OB_GET_WRS_INFO> *cb, const ObRpcOpts &opts));
  MOCK_METHOD2(report_single_replica, int(const ObReportSingleReplicaArg &arg, const ObRpcOpts &opts));
  MOCK_METHOD3(check_migrate_task_exist, int(const share::ObTaskId &, Bool &, const ObRpcOpts &opts));
  MOCK_METHOD3(check_backup_task_exist, int(const obrpc::ObBackupCheckTaskArg &, Bool &, const ObRpcOpts &opts));

  MOCK_METHOD2(sync_frozen_status, int(const storage::ObFrozenStatus &frozen_status,
                                       const ObRpcOpts &opts));
  MOCK_METHOD2(switch_leader, int(const ObSwitchLeaderArg &arg, const ObRpcOpts &opts));
  MOCK_METHOD2(switch_schema, int(const Int64 &schema_version, const ObRpcOpts &opts));
  MOCK_METHOD2(bootstrap, int(const ObServerInfoList &server_infos, const ObRpcOpts &opts));
  MOCK_METHOD2(check_server_empty, int(Bool &is_empty, const ObRpcOpts &opts));
  MOCK_METHOD1(report_replica, int(const ObRpcOpts &));
  MOCK_METHOD1(recycle_replica, int(const ObRpcOpts &));
  MOCK_METHOD1(clear_location_cache, int(const ObRpcOpts &));
  MOCK_METHOD2(broadcast_sys_schema, int(const common::ObSArray<share::schema::ObTableSchema> &args , const ObRpcOpts &opts));

  MOCK_METHOD2(sync_partition_table, int(const Int64 &, const ObRpcOpts &));
  MOCK_METHOD2(get_partition_count, int(obrpc::ObGetPartitionCountResult &result, const ObRpcOpts &opts));
  MOCK_METHOD2(get_rootserver_role, int(obrpc::ObGetRootserverRoleResult &result, const ObRpcOpts &opts));

int get_wrs_info_wrapper(const ObGetWRSArg &arg, ObSrvRpcProxy::AsyncCB<OB_GET_WRS_INFO> *cb, const obrpc::ObRpcOpts &opts)
  {
    UNUSED(arg);
    UNUSED(opts);
    static int64_t index = 0;
    if (index <= 3) {
      cb->rcode_.rcode_ = OB_SUCCESS;
    } else {
      cb->rcode_.rcode_ = OB_TIMEOUT;
    }
    return cb->process();
  }
};

}//end namespace obrpc
}//end namespace oceanbase

#endif
